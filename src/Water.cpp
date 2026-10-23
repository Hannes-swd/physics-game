// Wasser: Gewässer (Becken, Meer), Auftrieb, Widerstand und Hohlräume, die volllaufen.
//   - Auftrieb (Archimedes) exakt: jedes konvexe Stück wird an der Wasseroberfläche (bei Wellen: an ihrer Tangentialebene
//     über dem Stück) und an den Wänden des Beckens geschnitten. Kraft am Schwerpunkt des verdrängten Wassers -> Teile
//     richten sich auf, Bretter schwimmen flach, Kugeln drehen sich nicht.
//   - Widerstand: Plattenmodell wie in der Luft (plateForce), mit der Dichte des Wassers und der Strömung bzw. der
//     Bewegung des Wassers in den Wellen. Dazu die Abstrahlung von Wellen: dämpft das Auf- und Abschaukeln an der Oberfläche.
//   - Hohlräume (Boot, Becher, Rohr): konvexe Hülle minus Material. Die Luft darin trägt mit. Wasser läuft durch die
//     Öffnungen hinein und hinaus (Torricelli), aber nur, wenn die Luft entweichen bzw. nachströmen kann (umgedrehter Becher
//     unter Wasser: die Luft bleibt gefangen). Das Wasser darin wird zur Masse des Physik-Körpers addiert.
// Kosten: nur Teile, die ein Gewässer berühren und wach sind. Ruhende Teile (gesunken, still schwimmend) kosten nichts.
#include "Water.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

#include "Commands.h"
#include "Jobs.h"
#include "Materials.h"
#include "Physics.h"
#include "Profile.h"
#include "World.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kG = 9.81;
// Wellen: Wellenlänge relativ zur größten, Anteil an der halben Wellenhöhe, Richtung gegen die Hauptrichtung (rad).
// Dieselben Zahlen stehen in shaders/common.glsl.
constexpr double kWaveScale[4] = {1.0, 0.61, 0.37, 0.23};
constexpr double kWaveAmp[4] = {0.55, 0.25, 0.13, 0.07};
constexpr double kWaveAngle[4] = {0.0, 0.65, -0.5, 1.25};
constexpr double kOrifice = 0.6;          // Ausflusszahl einer scharfkantigen Öffnung
constexpr double kWaveDamping = 0.15;     // Abstrahlung von Wellen (Anteil der kritischen Dämpfung, etwa)
constexpr size_t kMaxRoomPieces = 64;     // mehr Stücke: keine Hohlräume suchen (z.B. viele Bohrungen)
constexpr size_t kMaxRooms = 8;

bool touches(const WaterBody& b, const glm::dvec3& mn, const glm::dvec3& mx) {
    const double margin = 0.5 * b.waves + 0.01 + glm::dot(b.flow, b.flow) / (2 * kG);  // Wellen, Stau vor Hindernissen
    if (mn.y > b.level() + margin) return false;
    if (b.sea) return true;
    return mx.y >= b.lo.y && mx.x >= b.lo.x && mn.x <= b.hi.x && mx.z >= b.lo.z && mn.z <= b.hi.z;
}

std::string cm(double m, int dec = 0) { return fmtNum(m * 100.0, dec); }

} // namespace

// ============================================================================ Flüssigkeiten

const std::vector<Liquid>& liquids() {
    // Name, Anzeige, Dichte, Wärmeübergang, Siedepunkt, Absorption je m (rgb), Streufarbe, löscht Feuer
    static const std::vector<Liquid> list = {
        {"water", "Wasser", 998.0f, 500.0f, 100.0f, {0.42f, 0.10f, 0.07f}, {0.010f, 0.045f, 0.060f}, true},
        {"seawater", "Meerwasser", 1025.0f, 500.0f, 100.5f, {0.40f, 0.085f, 0.10f}, {0.008f, 0.050f, 0.050f}, true},
        {"muddy", "Trübes Wasser", 1010.0f, 500.0f, 100.0f, {1.6f, 1.25f, 1.15f}, {0.10f, 0.085f, 0.05f}, true},
        {"oil", "Speiseöl", 920.0f, 150.0f, -1.0f, {2.0f, 2.6f, 9.0f}, {0.20f, 0.14f, 0.025f}, false},
    };
    return list;
}

int findLiquid(const char* nameIn) {
    const std::string name = normalizeWord(nameIn ? nameIn : "");
    static const char* aliases[][2] = {{"wasser", "water"}, {"meerwasser", "seawater"}, {"sea", "seawater"}, {"salt", "seawater"},
                                       {"mud", "muddy"},    {"truebes", "muddy"},       {"oel", "oil"}};
    std::string n = name;
    for (auto& a : aliases)
        if (n == a[0]) n = a[1];
    for (size_t i = 0; i < liquids().size(); ++i)
        if (n == liquids()[i].name) return (int)i;
    return -1;
}

// ============================================================================ Wellen

WaveSet waveSet(double height, glm::dvec2 dir) {
    WaveSet w;
    if (height <= 1e-4) return w;
    w.calm = false;
    if (glm::length(dir) < 1e-9) dir = glm::dvec2(1, 0);
    dir = glm::normalize(dir);
    w.lambda0 = std::max(14.0 * height, 0.2);
    for (int i = 0; i < 4; ++i) {
        const double c = std::cos(kWaveAngle[i]), s = std::sin(kWaveAngle[i]);
        w.dir[i] = glm::dvec2(dir.x * c - dir.y * s, dir.x * s + dir.y * c);
        w.k[i] = 2.0 * kPi / (w.lambda0 * kWaveScale[i]);
        w.omega[i] = std::sqrt(kG * w.k[i]);  // Tiefwasser
        w.amp[i] = 0.5 * height * kWaveAmp[i];
    }
    return w;
}

double waveHeight(const WaveSet& w, double x, double z, double t, glm::dvec2* grad) {
    if (grad) *grad = glm::dvec2(0);
    if (w.calm) return 0.0;
    double h = 0;
    for (int i = 0; i < 4; ++i) {
        const double th = w.k[i] * (w.dir[i].x * x + w.dir[i].y * z) - w.omega[i] * t;
        h += w.amp[i] * std::sin(th);
        if (grad) *grad += w.amp[i] * w.k[i] * std::cos(th) * w.dir[i];
    }
    return h;
}

glm::dvec3 waveVelocity(const WaveSet& w, double x, double z, double depth, double t) {
    glm::dvec3 u(0);
    if (w.calm) return u;
    for (int i = 0; i < 4; ++i) {
        const double th = w.k[i] * (w.dir[i].x * x + w.dir[i].y * z) - w.omega[i] * t;
        const double a = w.amp[i] * w.omega[i] * std::exp(-w.k[i] * std::max(depth, 0.0));  // nimmt nach unten schnell ab
        u.x += a * std::sin(th) * w.dir[i].x;
        u.z += a * std::sin(th) * w.dir[i].y;
        u.y -= a * std::cos(th);
    }
    return u;
}

// ============================================================================ Strömung

bool flowSample(const FlowGrid& g, double x, double z, glm::dvec2* u, double* rise, double* foam) {
    if (g.nx < 2 || g.nz < 2) return false;
    const double fx = (x - g.origin.x) / g.cell - 0.5, fz = (z - g.origin.y) / g.cell - 0.5;  // Zellmitten
    if (fx < -0.5 || fz < -0.5 || fx > g.nx - 0.5 || fz > g.nz - 0.5) return false;
    const double cx = std::clamp(fx, 0.0, g.nx - 1.000001), cz = std::clamp(fz, 0.0, g.nz - 1.000001);
    const int ix = (int)cx, iz = (int)cz;
    const double tx = cx - ix, tz = cz - iz;
    const size_t a = g.index(ix, iz), b = a + 1, c = a + g.nx, d = c + 1;
    const double wa = (1 - tx) * (1 - tz), wb = tx * (1 - tz), wc = (1 - tx) * tz, wd = tx * tz;
    if (u) *u = glm::dvec2(g.u[a]) * wa + glm::dvec2(g.u[b]) * wb + glm::dvec2(g.u[c]) * wc + glm::dvec2(g.u[d]) * wd;
    if (rise) *rise = g.rise[a] * wa + g.rise[b] * wb + g.rise[c] * wc + g.rise[d] * wd;
    if (foam) *foam = g.foam[a] * wa + g.foam[b] * wb + g.foam[c] * wc + g.foam[d] * wd;
    return true;
}

// ============================================================================ Geometrie

// Volumen eines konvexen Stücks unter einer einzigen Ebene, ohne ein neues Stück zu bauen: Bezugspunkt der Tetraeder liegt
// auf der Ebene, dann trägt die Schnittfläche nichts bei, und es reicht, jede Fläche zu beschneiden (keine Speicheranforderung)
static double volumeBelow(const solid::Piece& p, const solid::Plane& pl, glm::dvec3& centroid) {
    thread_local std::vector<glm::dvec3> poly;
    const glm::dvec3 ref = pl.n * pl.d;  // Punkt auf der Ebene
    double vol = 0;
    glm::dvec3 c(0);
    for (const solid::Face& f : p.faces) {
        poly.clear();
        const size_t m = f.v.size();
        for (size_t i = 0; i < m; ++i) {
            const glm::dvec3& a = f.v[i];
            const glm::dvec3& b = f.v[(i + 1) % m];
            const double sa = glm::dot(pl.n, a) - pl.d, sb = glm::dot(pl.n, b) - pl.d;
            if (sa <= 0) poly.push_back(a - ref);
            if ((sa < 0 && sb > 0) || (sa > 0 && sb < 0)) poly.push_back(a + (b - a) * (sa / (sa - sb)) - ref);
        }
        for (size_t i = 1; i + 1 < poly.size(); ++i) {
            const double vt = glm::dot(poly[0], glm::cross(poly[i], poly[i + 1])) / 6.0;
            vol += vt;
            c += vt * (poly[0] + poly[i] + poly[i + 1]) / 4.0;
        }
    }
    centroid = vol > 1e-18 ? ref + c / vol : ref;
    return std::max(vol, 0.0);
}

double clippedVolume(const solid::Piece& p, const solid::Plane* planes, int count, glm::dvec3& centroid, double fullVolume,
                     const glm::dvec3* fullCentroid, double radius) {
    // Welche Ebenen schneiden das Stück überhaupt? (erst mit der umschließenden Kugel, dann mit den Ecken)
    int cutting[8], nCut = 0;
    for (int i = 0; i < count; ++i) {
        const solid::Plane& pl = planes[i];
        if (fullCentroid && radius > 0) {
            const double dist = glm::dot(pl.n, *fullCentroid) - pl.d;
            if (dist >= radius) return 0.0;  // ganz außerhalb
            if (dist <= -radius) continue;   // ganz innerhalb
        }
        double lo = 1e30, hi = -1e30;
        for (const solid::Face& f : p.faces)
            for (const glm::dvec3& v : f.v) {
                const double s = glm::dot(pl.n, v) - pl.d;
                lo = std::min(lo, s);
                hi = std::max(hi, s);
            }
        if (lo >= -1e-12) return 0.0;
        if (hi > 1e-12 && nCut < 8) cutting[nCut++] = i;
    }
    if (nCut == 0) {
        if (fullVolume >= 0 && fullCentroid) {
            centroid = *fullCentroid;
            return fullVolume;
        }
        return std::max(0.0, solid::volume(p, &centroid));
    }
    if (nCut == 1) return volumeBelow(p, planes[cutting[0]], centroid);  // der häufige Fall: nur die Wasseroberfläche
    const solid::Piece* cur = &p;
    solid::Piece a, b;
    for (int k = 0; k < nCut; ++k) {
        solid::Piece& out = cur == &a ? b : a;
        if (!solid::clip(*cur, planes[cutting[k]], out)) return 0.0;
        cur = &out;
    }
    return std::max(0.0, solid::volume(*cur, &centroid));
}

double polygonBelow(const std::vector<glm::dvec3>& poly, const glm::dvec3& n, double d, glm::dvec3& centroid) {
    centroid = glm::dvec3(0);
    std::vector<glm::dvec3> out;
    const size_t m = poly.size();
    for (size_t i = 0; i < m; ++i) {
        const glm::dvec3& a = poly[i];
        const glm::dvec3& b = poly[(i + 1) % m];
        const double sa = glm::dot(n, a) - d, sb = glm::dot(n, b) - d;
        if (sa <= 0) out.push_back(a);
        if ((sa < 0 && sb > 0) || (sa > 0 && sb < 0)) out.push_back(a + (b - a) * (sa / (sa - sb)));
    }
    if (out.size() < 3) return 0.0;
    double area = 0;
    glm::dvec3 c(0);
    for (size_t i = 1; i + 1 < out.size(); ++i) {
        const double t = 0.5 * glm::length(glm::cross(out[i] - out[0], out[i + 1] - out[0]));
        area += t;
        c += t * (out[0] + out[i] + out[i + 1]) / 3.0;
    }
    centroid = area > 0 ? c / area : out[0];
    return area;
}

std::shared_ptr<WaterCache> buildWaterCache(const solid::Solid* s, const glm::dvec3& bbMin, const glm::dvec3& bbMax, double volume) {
    auto c = std::make_shared<WaterCache>();  // läuft auf den Hilfs-Threads (kein PROFILE: die Messung ist nicht threadsicher)
    if (s) {
        c->pieces = s->pieces;
    } else {  // importiertes Modell: sein Hüllquader, auf das echte Volumen umgerechnet
        solid::Piece box = solid::makeBox((bbMax - bbMin) * 0.5);
        solid::transform(box, glm::translate(glm::dmat4(1.0), (bbMin + bbMax) * 0.5));
        c->pieces.push_back(box);
        const glm::dvec3 e = bbMax - bbMin;
        c->scale = volume / std::max(e.x * e.y * e.z, 1e-12);
    }
    for (const solid::Piece& p : c->pieces) {
        glm::dvec3 ct;
        c->pieceVolume.push_back(solid::volume(p, &ct));
        c->pieceCentroid.push_back(ct);
        double r = 0;
        for (const solid::Face& f : p.faces)
            for (const glm::dvec3& v : f.v) r = std::max(r, glm::length(v - ct));
        c->pieceRadius.push_back(r);
    }
    if (!s || s->pieces.empty() || s->pieces.size() > kMaxRoomPieces) return c;

    // ---- Hohlräume: konvexe Hülle minus Material
    std::vector<glm::dvec3> pts;
    for (const solid::Piece& p : s->pieces)
        for (const solid::Face& f : p.faces) pts.insert(pts.end(), f.v.begin(), f.v.end());
    std::vector<glm::dvec4> hp;
    if (!Physics::hullPlanes(pts, hp)) return c;
    // Auf die Ebenen der Außenflächen einrasten: Hülle und Material treffen sich genau (sonst hauchdünne Spalte als "Hohlraum")
    std::vector<solid::Plane> planes;
    for (const glm::dvec4& h : hp) {
        solid::Plane pl;
        pl.n = glm::dvec3(h);
        pl.d = h.w;
        for (const solid::Piece& p : s->pieces)
            for (const solid::Face& f : p.faces)
                if (glm::dot(f.n, pl.n) > 1.0 - 1e-9 && std::abs(f.d - pl.d) < 1e-7) {
                    pl.n = f.n;
                    pl.d = f.d;
                }
        planes.push_back(pl);
    }
    const double extent = 2.0 * std::max(glm::length(bbMin), glm::length(bbMax)) + 1.0;
    const solid::Piece hull = solid::fromPlanes(planes, extent);
    if (hull.faces.empty()) return c;
    const double hullVol = solid::volume(hull);
    if (hullVol - volume < std::max(0.02 * hullVol, 1e-7)) return c;  // (fast) konvex: kein Hohlraum
    solid::Solid cav;
    cav.pieces.push_back(hull);
    for (const solid::Piece& p : s->pieces) {
        cav = solid::subtract(cav, solid::planesOf(p), nullptr, false);
        if (cav.pieces.empty() || cav.pieces.size() > 400) return c;
    }
    for (solid::Solid& comp : solid::connectedComponents(cav)) {
        WaterRoom r;
        r.volume = solid::volume(comp, &r.centroid);
        if (r.volume < std::max(1e-6, 0.005 * hullVol)) continue;
        // Öffnungen: Flächen des Hohlraums, die auf der Hülle liegen (dort ist kein Material)
        for (const solid::Piece& p : comp.pieces)
            for (const solid::Face& f : p.faces)
                for (const solid::Face& hf : hull.faces)
                    if (glm::dot(f.n, hf.n) > 1.0 - 1e-9 && std::abs(f.d - hf.d) < 1e-9) {
                        r.openings.push_back(f.v);
                        r.openingArea += solid::faceArea(f);
                        break;
                    }
        r.lo = glm::dvec3(1e30);
        r.hi = glm::dvec3(-1e30);
        for (const solid::Piece& p : comp.pieces)
            for (const solid::Face& f : p.faces)
                for (const glm::dvec3& v : f.v) {
                    r.lo = glm::min(r.lo, v);
                    r.hi = glm::max(r.hi, v);
                }
        r.pieces = std::move(comp.pieces);
        c->rooms.push_back(std::move(r));
    }
    std::sort(c->rooms.begin(), c->rooms.end(), [](const WaterRoom& a, const WaterRoom& b) { return a.volume > b.volume; });
    if (c->rooms.size() > kMaxRooms) c->rooms.resize(kMaxRooms);
    return c;
}

// ============================================================================ Gewässer

uint32_t World::addWater(WaterBody b) {
    b.id = nextWaterId_++;
    if (!b.sea) {
        const glm::dvec3 lo = glm::min(b.lo, b.hi), hi = glm::max(b.lo, b.hi);
        b.lo = lo;
        b.hi = hi;
    }
    b.liquid = std::clamp(b.liquid, 0, (int)liquids().size() - 1);
    waters_.push_back(b);
    changedWater(waters_.back());
    return b.id;
}

bool World::removeWater(uint32_t id) {
    auto it = std::find_if(waters_.begin(), waters_.end(), [&](const WaterBody& b) { return b.id == id; });
    if (id != 0 && it == waters_.end()) return false;
    std::vector<WaterBody> gone;
    if (id == 0) gone.swap(waters_);
    else {
        gone.push_back(*it);
        waters_.erase(it);
    }
    for (const WaterBody& b : gone) changedWater(b);  // was schwamm, fällt jetzt
    return true;
}

WaterBody* World::findWater(uint32_t id) {
    for (WaterBody& b : waters_)
        if (b.id == id) return &b;
    return nullptr;
}

void World::changedWater(const WaterBody& b) {
    WaterBody wide = b;
    wide.waves = std::max(wide.waves, 0.0) + 2.0;  // auch was knapp darüber liegt (der Spiegel kann gesunken sein)
    wide.hi.y += 1.0;
    for (auto& o : objects_)
        if (o->body != kNoBody && !o->fixed && touches(wide, o->wMin, o->wMax)) physics_.activate(o->body);
}

void World::setWaters(const std::vector<WaterBody>& list) {
    waters_ = list;
    for (const WaterBody& b : waters_) nextWaterId_ = std::max(nextWaterId_, b.id + 1);
}

const WaterBody* World::waterTouching(const glm::dvec3& mn, const glm::dvec3& mx) const {
    for (const WaterBody& b : waters_)
        if (touches(b, mn, mx)) return &b;
    return nullptr;
}

glm::dvec2 World::waveDirection() const {
    const glm::dvec2 w(wind_.x, wind_.z);
    return glm::length(w) > 0.3 ? glm::normalize(w) : glm::dvec2(0.8, 0.6);  // Wellen laufen mit dem Wind
}

WaveSet World::wavesOf(const WaterBody& b) const { return waveSet(b.waves, waveDirection()); }

const WaterBody* World::waterColumn(const glm::dvec3& p, double& surface) const {
    const WaterBody* best = nullptr;
    for (const WaterBody& b : waters_) {
        if (!b.covers(p.x, p.z) || (!b.sea && p.y < b.lo.y - 0.01)) continue;
        const double s = surfaceHeight(b, wavesOf(b), p.x, p.z);
        if (!best || s > surface) {
            best = &b;
            surface = s;
        }
    }
    return best;
}

bool World::inWater(const glm::dvec3& p) const {
    double s = 0;
    return waterColumn(p, s) && p.y <= s;
}

double World::waterTemp(const WaterBody& b) const { return b.ownTemp ? b.tempC : ambient_; }

const FlowGrid* World::flowGrid(uint32_t waterId) const {
    for (const FlowGrid& g : flowGrids_)
        if (g.water == waterId) return &g;
    return nullptr;
}

double World::surfaceHeight(const WaterBody& b, const WaveSet& w, double x, double z, glm::dvec2* grad) const {
    double h = b.level() + waveHeight(w, x, z, waterTime_, grad);
    if (const FlowGrid* g = flowGrid(b.id)) {
        double r = 0;
        if (flowSample(*g, x, z, nullptr, &r, nullptr)) {
            h += r;
            if (grad) {  // Steigung des Staus (zentrale Differenz über eine Zelle)
                double rx0 = r, rx1 = r, rz0 = r, rz1 = r;
                flowSample(*g, x - g->cell, z, nullptr, &rx0, nullptr);
                flowSample(*g, x + g->cell, z, nullptr, &rx1, nullptr);
                flowSample(*g, x, z - g->cell, nullptr, &rz0, nullptr);
                flowSample(*g, x, z + g->cell, nullptr, &rz1, nullptr);
                *grad += glm::dvec2(rx1 - rx0, rz1 - rz0) / (2 * g->cell);
            }
        }
    }
    return h;
}

glm::dvec3 World::currentAt(const WaterBody& b, double x, double z) const {
    glm::dvec2 u;
    if (const FlowGrid* g = flowGrid(b.id))
        if (flowSample(*g, x, z, &u, nullptr, nullptr)) return glm::dvec3(u.x, 0, u.y);
    return b.flow;
}

void World::advanceWaterClock(double dt) {
    if (simulating_) return;  // dann zählen die Physikschritte
    waterTime_ += dt;
    flowAccum_ += dt;
    if (flowAccum_ >= 0.05) {
        updateFlow(std::min(flowAccum_, 0.2));
        flowAccum_ = 0;
    }
}

void World::updateFlow(double dt) {
    // Gitter zu den Gewässern passend halten: neu, entfernt, andere Größe, Meer-Fenster gewandert (Werte übernehmen)
    std::vector<FlowGrid> grids;
    for (const WaterBody& b : waters_) {
        if (glm::length(b.flow) < 0.01 && b.waves < 0.005) continue;  // ruhiges Wasser: kein Gitter, kostet nichts
        glm::dvec2 lo, hi;
        if (b.sea) {
            const glm::dvec2 c = glm::floor(glm::dvec2(waterFocus_.x, waterFocus_.z) / 4.0) * 4.0;  // in 4-m-Schritten
            lo = c - 24.0;
            hi = c + 24.0;
        } else {
            lo = glm::dvec2(b.lo.x, b.lo.z);
            hi = glm::dvec2(b.hi.x, b.hi.z);
        }
        FlowGrid g;
        g.water = b.id;
        g.cell = std::max(std::max(hi.x - lo.x, hi.y - lo.y) / 96.0, 0.05);
        g.nx = std::max(2, (int)std::ceil((hi.x - lo.x) / g.cell - 1e-9));
        g.nz = std::max(2, (int)std::ceil((hi.y - lo.y) / g.cell - 1e-9));
        g.origin = lo;
        g.base = glm::dvec2(b.flow.x, b.flow.z);
        const size_t n = (size_t)g.nx * g.nz;
        g.u.assign(n, glm::vec2(g.base));
        g.foam.assign(n, 0.0f);
        g.rise.assign(n, 0.0f);
        g.p.assign(n, 0.0f);
        g.solid.assign(n, 0);
        if (const FlowGrid* old = flowGrid(b.id)) {
            if (old->nx == g.nx && old->nz == g.nz && old->origin == g.origin && old->cell == g.cell) {
                g.foam = old->foam;
                g.p = old->p;
                g.rise = old->rise;
                g.solid = old->solid;
                g.solidSig = old->solidSig;
            } else {
                for (int z = 0; z < g.nz; ++z)
                    for (int x = 0; x < g.nx; ++x) {
                        double f = 0;
                        if (flowSample(*old, g.origin.x + (x + 0.5) * g.cell, g.origin.y + (z + 0.5) * g.cell, nullptr, nullptr, &f))
                            g.foam[g.index(x, z)] = (float)f;
                    }
            }
        }
        grids.push_back(std::move(g));
    }
    flowGrids_.swap(grids);

    prof::Lap lap;  // Abschnitte für "profile"
    for (FlowGrid& g : flowGrids_) {
        const WaterBody* b = findWater(g.water);
        if (!b) continue;
        const double level = b->level(), U = glm::length(g.base), c = g.cell;
        const glm::dvec2 gHi = g.origin + glm::dvec2(g.nx, g.nz) * c;
        // ---- Hindernisse: Teile, die durch die Oberfläche ragen und (fast) stillstehen. Was mit dem Wasser treibt, nicht.
        // Nur neu rastern, wenn sich an ihnen etwas geändert hat (Lage, Form, welche es sind)
        std::vector<const Object*> obstacles;
        uint64_t sig = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
        for (const auto& op : objects_) {
            const Object& o = *op;
            if (o.wMax.y < level - 0.01 || o.wMin.y > level + 0.02 || o.wMax.x < g.origin.x || o.wMin.x > gHi.x || o.wMax.z < g.origin.y ||
                o.wMin.z > gHi.y)
                continue;
            if (!o.fixed && o.body != kNoBody && physics_.isActive(o.body) &&
                glm::length(glm::dvec3(physics_.linearVelocity(o.body))) > 0.5 * std::max(U, 0.2))
                continue;
            obstacles.push_back(&o);
            mix(o.id);
            mix(o.netRev);
            for (int k = 0; k < 3; ++k) mix((uint64_t)std::llround(o.pos[k] * 1000.0));  // mm
            for (int k = 0; k < 4; ++k) mix((uint64_t)std::llround(o.rot[k] * 1000.0));
        }
        mix((uint64_t)std::llround(level * 1000.0));
        if (sig != g.solidSig) {
            g.solidSig = sig;
            std::fill(g.solid.begin(), g.solid.end(), 0);
        } else {
            obstacles.clear();  // unverändert: Raster bleibt
        }
        for (const Object* op : obstacles) {
            const Object& o = *op;
            const glm::dquat inv = glm::conjugate(o.rot);
            const int x0 = std::max(0, (int)std::floor((o.wMin.x - g.origin.x) / c)), x1 = std::min(g.nx - 1, (int)std::floor((o.wMax.x - g.origin.x) / c));
            const int z0 = std::max(0, (int)std::floor((o.wMin.z - g.origin.y) / c)), z1 = std::min(g.nz - 1, (int)std::floor((o.wMax.z - g.origin.y) / c));
            const double y = std::clamp(level, o.wMin.y + 1e-4, o.wMax.y - 1e-4);  // flache Teile knapp an der Oberfläche zählen mit
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) {
                    const glm::dvec3 lp = inv * (glm::dvec3(g.origin.x + (x + 0.5) * c, y, g.origin.y + (z + 0.5) * c) - o.pos);
                    bool in = false;
                    if (o.kind == ObjKind::Solid) {
                        for (const solid::Piece& p : o.solid.pieces) {
                            in = !p.faces.empty();
                            for (const solid::Face& f : p.faces)
                                if (glm::dot(f.n, lp) > f.d) {
                                    in = false;
                                    break;
                                }
                            if (in) break;
                        }
                    } else {
                        in = glm::all(glm::greaterThanEqual(lp, o.bbMin)) && glm::all(glm::lessThanEqual(lp, o.bbMax));
                    }
                    if (in) g.solid[g.index(x, z)] = 1;
                }
        }
        auto solidAt = [&](int x, int z) { return x >= 0 && z >= 0 && x < g.nx && z < g.nz && g.solid[g.index(x, z)]; };

        lap.mark("flow.obstacles");
        // ---- Umströmung: freie Strömung, dann quellenfrei machen (Druck lösen, Gradient abziehen). Geschwindigkeiten auf den
        // Zellkanten (versetztes Gitter: Divergenz und Gradient passen genau zusammen), Gauß-Seidel mit Überrelaxation, der
        // Druck vom letzten Mal ist der Startwert. Ränder des Gitters sind offen (Wasser kommt herein und fließt hinaus),
        // Kanten an Hindernissen dicht
        if (U > 0.01) {
            const int nx = g.nx, nz = g.nz;
            auto open = [&](int x, int z) { return x < 0 || z < 0 || x >= nx || z >= nz || !g.solid[g.index(x, z)]; };
            std::vector<float> fx((size_t)(nx + 1) * nz), fz((size_t)nx * (nz + 1));  // Kante links von Zelle x / unter Zelle z
            auto FX = [&](int x, int z) -> float& { return fx[(size_t)x + (size_t)(nx + 1) * z]; };
            auto FZ = [&](int x, int z) -> float& { return fz[(size_t)x + (size_t)nx * z]; };
            for (int z = 0; z < nz; ++z)
                for (int x = 0; x <= nx; ++x) FX(x, z) = open(x - 1, z) && open(x, z) ? (float)g.base.x : 0.0f;
            for (int z = 0; z <= nz; ++z)
                for (int x = 0; x < nx; ++x) FZ(x, z) = open(x, z - 1) && open(x, z) ? (float)g.base.y : 0.0f;
            std::vector<float> div(g.u.size(), 0.0f);
            for (int z = 0; z < nz; ++z)
                for (int x = 0; x < nx; ++x)
                    div[g.index(x, z)] = (float)((FX(x + 1, z) - FX(x, z) + FZ(x, z + 1) - FZ(x, z)) / c);
            auto pAt = [&](int x, int z) { return x < 0 || z < 0 || x >= nx || z >= nz ? 0.0f : g.p[g.index(x, z)]; };  // offen: 0
            // Druck mit einem Rand aus Nullen (offene Ränder) und je Zelle die offenen Kanten als Gewichte 0/1: die innere
            // Schleife braucht keine Abfragen. Abbruch, sobald sich nichts mehr ändert (mit dem Startwert meist nach wenigen)
            const int px = nx + 2;
            std::vector<float> pp((size_t)px * (nz + 2), 0.0f), wR(g.u.size()), wL(g.u.size()), wU(g.u.size()), wD(g.u.size()), invK(g.u.size(), 0.0f);
            for (int z = 0; z < nz; ++z)
                for (int x = 0; x < nx; ++x) {
                    const size_t i = g.index(x, z);
                    pp[(size_t)(x + 1) + (size_t)px * (z + 1)] = g.p[i];
                    if (g.solid[i]) continue;
                    wR[i] = open(x + 1, z);  // dichte Kante: kein Fluss, kein Beitrag
                    wL[i] = open(x - 1, z);
                    wU[i] = open(x, z + 1);
                    wD[i] = open(x, z - 1);
                    const float k = wR[i] + wL[i] + wU[i] + wD[i];
                    invK[i] = k > 0 ? 1.0f / k : 0.0f;
                }
            // Rot-Schwarz-Reihenfolge (Schachbrett): innerhalb einer Farbe hängen die Zellen nicht voneinander ab, der Prozessor
            // muss nicht auf die Nachbarzelle warten
            const float omega = 1.7f, cc = (float)(c * c), tol = (float)(2e-4 * U * c);
            for (int it = 0; it < 40; ++it) {
                float change = 0;
                for (int color = 0; color < 2; ++color)
                    for (int z = 0; z < nz; ++z) {
                        float* row = &pp[(size_t)px * (z + 1) + 1];
                        const size_t r0 = g.index(0, z);
                        for (int x = (z + color) & 1; x < nx; x += 2) {
                            const size_t i = r0 + x;
                            const float sum = wR[i] * row[x + 1] + wL[i] * row[x - 1] + wU[i] * row[x + px] + wD[i] * row[x - px];
                            const float d = invK[i] == 0 ? 0.0f : omega * ((sum - div[i] * cc) * invK[i] - row[x]);
                            row[x] += d;
                            change = std::max(change, std::abs(d));
                        }
                    }
                if (change < tol) break;
            }
            for (int z = 0; z < nz; ++z)
                for (int x = 0; x < nx; ++x) g.p[g.index(x, z)] = pp[(size_t)(x + 1) + (size_t)px * (z + 1)];
            for (int z = 0; z < nz; ++z)
                for (int x = 0; x <= nx; ++x)
                    if (open(x - 1, z) && open(x, z)) FX(x, z) -= (float)((pAt(x, z) - pAt(x - 1, z)) / c);
            for (int z = 0; z <= nz; ++z)
                for (int x = 0; x < nx; ++x)
                    if (open(x, z - 1) && open(x, z)) FZ(x, z) -= (float)((pAt(x, z) - pAt(x, z - 1)) / c);
            for (int z = 0; z < nz; ++z)
                for (int x = 0; x < nx; ++x) {
                    const size_t i = g.index(x, z);
                    g.u[i] = g.solid[i] ? glm::vec2(0) : glm::vec2(0.5f * (FX(x, z) + FX(x + 1, z)), 0.5f * (FZ(x, z) + FZ(x, z + 1)));
                    const float sp = glm::length(g.u[i]), lim = (float)(3.0 * U);
                    if (sp > lim) g.u[i] *= lim / sp;
                }
        } else {
            for (size_t i = 0; i < g.u.size(); ++i) g.u[i] = g.solid[i] ? glm::vec2(0) : glm::vec2(g.base);
        }

        lap.mark("flow.solve");
        // ---- Stau (Bernoulli): wo das Wasser langsamer wird, steigt es, wo es schneller wird, sinkt es. Einmal geglättet
        {
            std::vector<float> r(g.u.size());
            for (size_t i = 0; i < r.size(); ++i) {
                const double u2 = g.solid[i] ? 0.0 : glm::dot(glm::dvec2(g.u[i]), glm::dvec2(g.u[i]));
                r[i] = (float)std::clamp((U * U - u2) / (2 * kG), -0.3, 0.5);
            }
            for (int z = 0; z < g.nz; ++z)
                for (int x = 0; x < g.nx; ++x) {
                    float s = 0, wsum = 0;
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int a = x + dx, bz = z + dz;
                            if (a < 0 || bz < 0 || a >= g.nx || bz >= g.nz) continue;
                            const float wgt = (dx == 0 && dz == 0) ? 4.0f : (dx == 0 || dz == 0) ? 2.0f : 1.0f;
                            s += r[g.index(a, bz)] * wgt;
                            wsum += wgt;
                        }
                    g.rise[g.index(x, z)] = s / wsum;
                }
        }

        lap.mark("flow.rise");
        // ---- Schaum: entsteht, wo Wasser gegen ein Hindernis läuft oder Wellen anschlagen, treibt mit und zerfällt
        {
            std::vector<float> f(g.foam.size());
            const float decay = (float)std::exp(-dt / 4.0);
            for (int z = 0; z < g.nz; ++z)
                for (int x = 0; x < g.nx; ++x) {
                    const size_t i = g.index(x, z);
                    const glm::dvec2 pos = g.origin + (glm::dvec2(x, z) + 0.5) * c - glm::dvec2(g.u[i]) * dt;  // woher das Wasser kommt
                    double v = 0;
                    flowSample(g, pos.x, pos.y, nullptr, nullptr, &v);
                    f[i] = (float)v * decay;
                }
            int sprays = 0;
            for (int z = 0; z < g.nz; ++z)
                for (int x = 0; x < g.nx; ++x) {
                    const size_t i = g.index(x, z);
                    if (g.solid[i]) {
                        f[i] = 0;
                        continue;
                    }
                    // Verwirbelung (Wirbelstärke): an den Kanten der Hindernisse und im Kielwasser dahinter schäumt es
                    if (U > 0.01 && x > 0 && z > 0 && x + 1 < g.nx && z + 1 < g.nz) {
                        const double curl = ((g.u[g.index(x + 1, z)].y - g.u[g.index(x - 1, z)].y) - (g.u[g.index(x, z + 1)].x - g.u[g.index(x, z - 1)].x)) / (2 * c);
                        f[i] = (float)std::min(1.0, f[i] + dt * 0.08 * std::max(0.0, std::abs(curl) - 1.5));
                    }
                    double impact = 0;  // wie schnell das freie Wasser auf ein Hindernis daneben zuläuft
                    bool shore = false;
                    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (const auto& k : d)
                        if (solidAt(x + k[0], z + k[1])) {
                            shore = true;
                            impact = std::max(impact, g.base.x * k[0] + g.base.y * k[1]);
                        }
                    if (!shore) continue;
                    f[i] = (float)std::min(1.0, f[i] + dt * (5.0 * impact + 6.0 * b->waves + 0.6 * U));
                    // Gischt, wo schnelles Wasser aufprallt
                    if (impact > 1.0 && sprays < 3 && splashes_.size() < 64 && std::fmod(waterTime_ * 7.31 + x * 0.37 + z * 0.61, 1.0) < dt * impact * 0.4) {
                        ++sprays;
                        splashes_.push_back({glm::dvec3(g.origin.x + (x + 0.5) * c, level + g.rise[i], g.origin.y + (z + 0.5) * c), impact * 0.8, c});
                    }
                }
            g.foam.swap(f);
            lap.mark("flow.foam");
        }
    }
}

// ============================================================================ Teile im Wasser

void World::ensureWaterCache(Object& o) {
    if (o.waterCache && o.waterCache->rev == o.netRev) return;
    o.waterCache = buildWaterCache(o.kind == ObjKind::Solid ? &o.solid : nullptr, o.bbMin, o.bbMax, o.volume);
    o.waterCache->rev = o.netRev;
    o.rooms.assign(o.waterCache->rooms.size(), Object::RoomState());
}

bool World::fillWater(Object& o, double fraction, std::string* err) {
    ensureWaterCache(o);
    if (o.rooms.empty()) {
        if (err) *err = o.name + " has no hollow that could hold water (cup, boat, pocket)";
        return false;
    }
    for (size_t j = 0; j < o.rooms.size(); ++j) {
        o.rooms[j].water = std::clamp(fraction, 0.0, 1.0) * o.waterCache->rooms[j].volume;
        o.rooms[j].age = 99;
    }
    if (o.body != kNoBody && !o.fixed) physics_.activate(o.body);
    return true;
}

// Ein Physik-Teilschritt für ein waches Teil: Auftrieb, Widerstand, Hohlräume. false: das Teil ist nicht im Wasser.
// Läuft für viele Teile gleichzeitig auf den Hilfs-Threads: liest die Welt nur, schreibt nur in dieses Teil (und über die
// sperrenden Aufrufe in seinen Physik-Körper)
bool World::waterStep(Object& o, float hStep) {
    Object::WaterState& st = o.water;
    const glm::dvec3 v = glm::dvec3(physics_.linearVelocity(o.body)), w = glm::dvec3(physics_.angularVelocity(o.body));
    const double h = hStep;
    const double prevDisplaced = st.displaced;
    st.displaced = st.solidSub = 0;
    st.buoyancy = st.drag = glm::dvec3(0);
    st.body = 0;
    st.splash = 0;
    st.wet.clear();
    const WaterBody* wb = waterTouching(o.wMin, o.wMax);
    bool carries = false;  // Wasser in Hohlräumen (auch außerhalb eines Gewässers: ein voller Becher)
    for (const Object::RoomState& r : o.rooms) carries |= r.water > 0;
    if (!wb && !carries) {
        if (st.extraMass > 0) {
            st.extraMass = 0;
            physics_.setMass(o.body, o.mass);
        }
        st.lastVel = v;
        return false;
    }
    ensureWaterCache(o);
    const WaterCache& wc = *o.waterCache;
    const glm::dmat3 R = glm::mat3_cast(o.rot), Rt = glm::transpose(R);
    const double t = waterTime_;
    const double rho = wb ? liquids()[wb->liquid].density : 998.0;
    const WaveSet waves = wb ? wavesOf(*wb) : WaveSet();
    const glm::dvec3 up(0, 1, 0);
    // Ebene (Welt) -> lokal: innen gilt n·x <= d
    auto local = [&](const glm::dvec3& n, double d) {
        solid::Plane p;
        p.n = Rt * n;
        p.d = d - glm::dot(n, o.pos);
        return p;
    };
    // Oberfläche über einem Punkt: Tangentialebene der Wellen (lokal), Höhe in s
    auto surfaceAt = [&](const glm::dvec3& p, double& s) {
        glm::dvec2 g(0);
        s = surfaceHeight(*wb, waves, p.x, p.z, &g);
        const glm::dvec3 n = glm::normalize(glm::dvec3(-g.x, 1.0, -g.y));
        return local(n, glm::dot(n, glm::dvec3(p.x, s, p.z)));
    };
    solid::Plane planes[6];
    int nPlanes = 1;
    if (wb && !wb->sea) {  // Becken: Wände und Boden begrenzen das Wasser (ein Brett ragt über den Rand)
        planes[nPlanes++] = local({1, 0, 0}, wb->hi.x);
        planes[nPlanes++] = local({-1, 0, 0}, -wb->lo.x);
        planes[nPlanes++] = local({0, 0, 1}, wb->hi.z);
        planes[nPlanes++] = local({0, 0, -1}, -wb->lo.z);
        planes[nPlanes++] = local({0, -1, 0}, -wb->lo.y);
    }
    glm::dvec3 force(0), torque(0);

    // ---- Auftrieb des Materials: jedes Stück an seiner Stelle der Oberfläche geschnitten
    if (wb) {
        double vol = 0;
        glm::dvec3 mom(0);
        for (size_t i = 0; i < wc.pieces.size(); ++i) {
            double s;
            planes[0] = surfaceAt(o.pos + R * wc.pieceCentroid[i], s);
            glm::dvec3 c;
            const double pv = clippedVolume(wc.pieces[i], planes, nPlanes, c, wc.pieceVolume[i], &wc.pieceCentroid[i], wc.pieceRadius[i]);
            vol += pv;
            mom += pv * c;
        }
        vol *= wc.scale;
        mom *= wc.scale;
        if (vol > 0) {
            const glm::dvec3 F = up * (rho * kG * vol);
            force += F;
            torque += glm::cross(R * (mom / vol), F);
            st.solidSub = st.displaced = vol;
        }
    }

    // ---- Hohlräume: die Luft darin trägt mit, Wasser läuft durch die Öffnungen hinein und hinaus
    if (o.rooms.size() != wc.rooms.size()) o.rooms.assign(wc.rooms.size(), Object::RoomState());
    glm::dvec3 accel = (v - st.lastVel) / std::max(h, 1e-6);
    glm::dvec3 gApp = glm::dvec3(0, -kG, 0) - accel;  // scheinbare Schwerkraft im Teil (freier Fall: keine)
    if (glm::length(gApp) > 3 * kG) gApp *= 3 * kG / glm::length(gApp);
    double waterMass = 0;
    bool roomFlow = false;
    for (size_t j = 0; j < wc.rooms.size(); ++j) {
        const WaterRoom& room = wc.rooms[j];
        Object::RoomState& rs = o.rooms[j];
        const glm::dvec3 rc = o.pos + R * room.centroid;
        double sOut = -1e30;  // Oberfläche draußen
        double vAir = 0;      // Hohlraum unter der Oberfläche draußen
        glm::dvec3 cAir(0);
        if (wb && wb->covers(rc.x, rc.z)) {
            planes[0] = surfaceAt(rc, sOut);
            for (const solid::Piece& p : room.pieces) {
                glm::dvec3 c;
                const double pv = clippedVolume(p, planes, nPlanes, c);
                vAir += pv;
                cAir += pv * c;
            }
            if (vAir > 0) cAir /= vAir;
        }
        // Höhe des Hohlraums (Welt): oben sammelt sich die Luft
        double rLo = 1e30, rHi = -1e30;
        for (const solid::Piece& p : room.pieces)
            for (const solid::Face& f : p.faces)
                for (const glm::dvec3& q : f.v) {
                    const double y = o.pos.y + (R * q).y;
                    rLo = std::min(rLo, y);
                    rHi = std::max(rHi, y);
                }
        // Wasserspiegel innen (waagrecht): so hoch, dass das Volumen darunter dem Wasser darin entspricht
        if (rs.water <= 0) {
            rs.level = -1e30;
        } else if (rs.age >= 4) {
            double lo = rLo, hi = rHi;
            glm::dvec3 cen = room.centroid;
            for (int it = 0; it < 22 && hi - lo > 2e-4; ++it) {
                const double mid = 0.5 * (lo + hi);
                const solid::Plane pl = local(up, mid);
                double vol = 0;
                glm::dvec3 m(0);
                for (const solid::Piece& p : room.pieces) {
                    glm::dvec3 c;
                    const double pv = clippedVolume(p, &pl, 1, c);
                    vol += pv;
                    m += pv * c;
                }
                if (vol > 0) cen = m / vol;
                (vol < rs.water ? lo : hi) = mid;
            }
            rs.level = 0.5 * (lo + hi);
            rs.centroid = cen;
            rs.age = 0;
        } else {
            ++rs.age;
        }
        // Durchfluss durch die Öffnungen (Torricelli). Dafür muss Luft heraus bzw. hinein können:
        //  - hinein läuft Wasser nur, wenn die Luft oben entweichen kann: die Öffnung reicht bis an den höchsten Punkt des
        //    Hohlraums und liegt dort über dem Spiegel innen (umgedrehter Becher unter Wasser: die Luft bleibt gefangen)
        //  - hinaus läuft es, wenn Luft nachströmt: über dem Spiegel innen oder (gluckernd) durch eine weite Öffnung an der
        //    Luft (volles Glas kopfüber). Liegt der Rand unter Wasser, hält der Wasserdruck das Wasser im Glas
        double qIn = 0, qOut = 0;
        std::vector<glm::dvec3> pw;
        for (const auto& poly : room.openings) {
            pw.clear();
            double top = -1e30;
            for (const glm::dvec3& q : poly) {
                pw.push_back(o.pos + R * q);
                top = std::max(top, pw.back().y);
            }
            glm::dvec3 cAll, cO, cI;
            const double total = polygonBelow(pw, up, 1e30, cAll);
            const double aIn = rs.water > 0 ? polygonBelow(pw, up, rs.level, cI) : 0.0;
            const double aOut = sOut > -1e29 ? polygonBelow(pw, up, sOut, cO) : 0.0;
            const bool aboveInside = total - aIn > 0.02 * total;
            const bool airOut = aboveInside && top >= rHi - 0.002 - 0.02 * (rHi - rLo);
            const bool airIn = aboveInside || (total > 1e-4 && total - aOut > 0.02 * total);
            if (airOut && aOut > 0 && sOut > rs.level) qIn += kOrifice * aOut * std::sqrt(2 * kG * (sOut - std::max(rs.level, cO.y)));
            if (airIn && aIn > 0 && rs.level > sOut) qOut += kOrifice * aIn * std::sqrt(2 * kG * std::max(0.0, rs.level - std::max(sOut, cI.y)));
        }
        double vNew = rs.water + (qIn - qOut) * h;
        if (qIn > qOut) vNew = std::min(vNew, std::max(rs.water, vAir));  // nicht über den Spiegel draußen hinaus
        vNew = std::clamp(vNew, 0.0, room.volume);
        if (std::abs(vNew - rs.water) > 2e-3 * room.volume) rs.age = 99;  // Spiegel im nächsten Schritt neu
        roomFlow |= vNew != rs.water;
        if (qIn > qOut && wb) rs.liquid = wb->liquid;
        rs.water = vNew;
        if (vAir > 0) {  // Hohlraum unter der Oberfläche verdrängt Wasser (das Wasser darin trägt das Teil als Masse)
            const glm::dvec3 F = up * (rho * kG * vAir);
            force += F;
            torque += glm::cross(R * cAir, F);
            st.displaced += vAir;
        }
        if (rs.water > 0) {
            // Die Physik trägt das Wasser als Masse im Schwerpunkt; es liegt aber tiefer bzw. seitlich: Moment ergänzen
            const double m = rs.water * liquids()[std::clamp(rs.liquid, 0, (int)liquids().size() - 1)].density;
            waterMass += m;
            torque += glm::cross(R * rs.centroid, m * gApp);
        }
    }
    if (waterMass > 0 || st.extraMass > 0) {
        const double target = o.mass + waterMass;
        if (std::abs(physics_.mass(o.body) - target) > 0.002 * target) physics_.setMass(o.body, target);
        st.extraMass = waterMass;
    }
    st.buoyancy = force;
    const glm::dvec3 buoyTorque = torque;
    // Auf und Ab strahlt Wellen ab und wird dadurch gedämpft: Kraft proportional dazu, wie schnell sich das verdrängte
    // Volumen ändert (so dämpft auch die Luft unter einem umgedrehten Becher). Leichte Teile: höchstens so stark, dass die
    // explizite Rechnung stabil bleibt
    if (wb && st.step + 1 == waterSteps_ && prevDisplaced > 0 && st.displaced > 0) {
        const glm::dvec3 e = o.wMax - o.wMin;
        const double foot = std::max(e.x * e.z, 1e-6);
        const double b = std::min(kWaveDamping * rho * std::sqrt(kG * std::sqrt(foot)), 0.5 * (o.mass + waterMass) / (h * foot));
        force.y += b * (st.displaced - prevDisplaced) / h;
    }

    // ---- Widerstand an den Außenflächen unter Wasser; Wellen dämpfen das Schaukeln an der Oberfläche
    if (wb) {
        st.wet.assign(o.aero.size(), 0.0f);
        BodyState bs;
        const bool haveState = physics_.bodyState(o.body, bs) && bs.mass > 0;
        glm::dvec3 drag(0), dragTorque(0);
        double relMax = 0;  // schnellste Anströmung einer Fläche
        // Wellen einmal je Teil: Tangentialebene und Bewegung des Wassers an der Oberfläche über dem Schwerpunkt. Für den
        // Widerstand genau genug (der Auftrieb oben rechnet jedes Stück an seiner eigenen Stelle)
        glm::dvec2 g0(0);
        const double s0 = surfaceHeight(*wb, waves, o.pos.x, o.pos.z, &g0);
        const FlowGrid* fg = flowGrid(wb->id);
        const glm::dvec3 u0 = waveVelocity(waves, o.pos.x, o.pos.z, 0.0, t);
        const double k0 = waves.calm ? 0.0 : waves.k[0];
        for (size_t i = 0; i < o.aero.size(); ++i) {
            const Object::AeroFace& f = o.aero[i];
            const glm::dvec3 r = R * glm::dvec3(f.c), n = R * glm::dvec3(f.n), c = o.pos + r;
            if (!wb->covers(c.x, c.z) || (!wb->sea && c.y < wb->lo.y)) continue;
            const double s = s0 + g0.x * r.x + g0.y * r.z;
            const double ext = 0.5 * std::sqrt(f.area) * std::sqrt(std::max(0.0, 1.0 - n.y * n.y)) + 1e-4;  // halbe Höhe der Fläche
            const double wet = std::clamp((s - c.y + ext) / (2 * ext), 0.0, 1.0);
            if (wet <= 0) continue;
            st.wet[i] = (float)wet;
            const double depth = std::max(0.0, s - c.y);
            // Strömung an der Fläche: um Hindernisse herum (Gitter), dazu die Bewegung in den Wellen
            const glm::dvec3 u = (fg ? currentAt(*wb, c.x, c.z) : wb->flow) + (k0 > 0 ? u0 * std::exp(-k0 * depth) : glm::dvec3(0));
            const glm::dvec3 vRel = v + glm::cross(w, r) - u;
            relMax = std::max(relMax, glm::length(vRel));
            const double A = f.area * wet;
            glm::dvec3 df(0);
            plateForce(vRel, n, A, rho, df);
            const double vn = glm::dot(vRel, n);
            const double L = std::sqrt(f.area), bArea = kWaveDamping * rho * std::sqrt(kG * L) * std::exp(-2.0 * depth / L);
            if (vn > 0) {  // seitlich (Auf und Ab steckt schon in der Änderung des verdrängten Volumens oben)
                double b = bArea * A * (1.0 - n.y * n.y);
                if (haveState) b = std::min(b, 0.25 * bs.mass / h);  // leichte Teile: stabil bleiben
                df -= b * vn * n;
            }
            drag += df;
            dragTorque += glm::cross(r, df);
            // Dreht sich die Fläche um eine Achse in ihrer Ebene (Rollen, Stampfen), drückt eine Hälfte ins Wasser, die andere
            // zieht: die Mitte steht still, trotzdem bremst das Wasser. Moment für eine quadratische Fläche (∫s² dA = A²/12)
            const glm::dvec3 wt = w - n * glm::dot(w, n);
            const double wl = glm::length(wt);
            if (wl > 1e-6) dragTorque -= wt * (wet * (bArea * A * A / 12.0 + 0.5 * rho * 1.2 * wl * A * A * std::sqrt(A) / 64.0));
        }
        // Explizit gerechnet: ein leichtes Teil darf in einem Schritt höchstens so stark beschleunigt werden, dass es die
        // Geschwindigkeit des Wassers erreicht (sonst schwingt es über und schaukelt sich auf)
        if (haveState) {
            const double dv = glm::length(drag) * h / bs.mass, lim = 0.8 * relMax + 1e-6;
            if (dv > lim) drag *= lim / dv;
            const double reach = std::max(0.5 * glm::length(o.bbMax - o.bbMin), 1e-3);
            const double dw = glm::length(bs.invInertiaWorld * dragTorque) * h, limW = 0.8 * (glm::length(w) + relMax / reach) + 1e-6;
            if (dw > limW) dragTorque *= limW / dw;
        }
        force += drag;
        torque += dragTorque;
        st.drag = drag;
        st.body = wb->id;
        double s = 0;
        waterColumn(o.pos, s);
        st.depth = s - o.pos.y;
        // Jolt legt langsame Teile schlafen, auch mitten im Ausschwingen oder beim langsamen Absinken. Im Wasser erst, wenn
        // Auftrieb und Gewicht im Gleichgewicht sind und nichts sie bewegt (Wellen und Strömung an der Oberfläche,
        // einlaufendes Wasser). Was auf dem Grund oder irgendwo aufliegt, bewegt sich nicht mehr und darf schlafen
        const double weight = (o.mass + waterMass) * kG, size = std::max(0.5 * glm::length(o.bbMax - o.bbMin), 1e-3);
        const bool unbalanced = std::abs(st.buoyancy.y - weight) > 0.01 * weight || glm::length(buoyTorque) > 0.005 * weight * size;
        const bool moving = glm::length(v) + glm::length(w) * size > 0.003;
        const bool surface = o.wMin.y < s + 0.5 * wb->waves && o.wMax.y > s - 0.5 * wb->waves;
        if ((unbalanced && moving) || (surface && (!waves.calm || glm::length(wb->flow) > 0.01)) || roomFlow) physics_.keepAwake(o.body);
    } else if (roomFlow) {
        physics_.keepAwake(o.body);
    }
    physics_.addForceNoWake(o.body, glm::vec3(force));
    physics_.addTorqueNoWake(o.body, glm::vec3(torque));

    if (wb && prevDisplaced <= 0 && st.displaced > 0 && v.y < -1.0) st.splash = -v.y;  // Spritzer beim Eintauchen
    st.lastVel = v;
    return wb != nullptr;
}

// Alle wachen Teile im Wasser (oder mit Wasser in Hohlräumen): auf alle Kerne verteilt. Danach weiß jedes Teil, ob es
// in diesem Schritt nass war (water.step), und die Spritzer werden eingesammelt
void World::waterSteps(float h) {
    ++waterSteps_;
    waterParts_.clear();
    for (BodyHandle b : awake_) {
        Object* o = fromBody(b);
        if (o && !o->fixed && (!o->rooms.empty() || waterTouching(o->wMin, o->wMax) || o->water.body)) waterParts_.push_back(o);
    }
    if (waterParts_.empty()) return;
    jobs::parallelFor(waterParts_.size(), [&](size_t k) {
        Object& o = *waterParts_[k];
        if (waterStep(o, h)) o.water.step = waterSteps_;
    });
    for (Object* o : waterParts_) {
        if (o->water.splash > 0 && splashes_.size() < 64) {
            double s = 0;
            waterColumn(o->pos, s);
            const glm::dvec3 e = o->wMax - o->wMin;
            splashes_.push_back({glm::dvec3(o->pos.x, s, o->pos.z), o->water.splash, std::sqrt(std::max(e.x * e.z, 1e-4))});
        }
    }
}

std::vector<World::Splash> World::takeSplashes() {
    std::vector<Splash> r;
    r.swap(splashes_);
    return r;
}

std::vector<World::Steam> World::takeSteam() {
    std::vector<Steam> r;
    r.swap(steam_);
    return r;
}

// ============================================================================ Texte

std::string World::waterText(const WaterBody& b) const {
    std::ostringstream ss;
    const Liquid& L = liquids()[b.liquid];
    ss << "#" << b.id << " ";
    if (b.sea) {
        ss << "sea, level " << cm(b.level()) << " cm";
    } else {
        const glm::dvec3 e = b.hi - b.lo, c = (b.lo + b.hi) * 0.5;
        ss << "pool " << cm(e.x) << " x " << cm(e.z) << " cm, " << cm(e.y) << " cm deep, bottom center " << cm(c.x) << " " << cm(b.lo.y)
           << " " << cm(c.z) << " cm";
    }
    ss << ", " << L.name << " " << fmtNum(waterTemp(b), 0) << " °C" << (b.ownTemp ? "" : " (air)");
    if (b.waves > 0) ss << ", waves " << cm(b.waves) << " cm";
    if (glm::length(b.flow) > 0) ss << ", current " << fmtNum(b.flow.x, 2) << " " << fmtNum(b.flow.z, 2) << " m/s";
    if (const FlowGrid* g = flowGrid(b.id)) {  // Strömung um Hindernisse
        int solid = 0;
        float fastest = 0, foam = 0, rise = -1, dip = 1;
        for (size_t i = 0; i < g->u.size(); ++i) {
            solid += g->solid[i];
            if (g->solid[i]) continue;
            fastest = std::max(fastest, glm::length(g->u[i]));
            foam = std::max(foam, g->foam[i]);
            rise = std::max(rise, g->rise[i]);
            dip = std::min(dip, g->rise[i]);
        }
        ss << "\n     flow grid " << g->nx << " x " << g->nz << " cells of " << cm(g->cell, 1) << " cm, " << solid << " blocked, fastest "
           << fmtNum(fastest, 2) << " m/s, water piles up " << cm(rise, 1) << " cm / drops " << cm(-dip, 1) << " cm, foam "
           << fmtNum(foam * 100, 0) << " %";
    }
    return ss.str();
}

std::string World::waterText() const {
    std::ostringstream ss;
    ss << "water         " << waters_.size() << " bod" << (waters_.size() == 1 ? "y" : "ies");
    for (const WaterBody& b : waters_) ss << "\n  " << waterText(b);
    int floating = 0, under = 0;
    for (const auto& o : objects_) {
        if (o->water.displaced <= 0 || o->water.body == 0) continue;
        (o->water.solidSub >= o->volume * 0.999 && o->water.displaced <= o->volume * 1.001 ? under : floating)++;
    }
    ss << "\nparts         " << floating << " floating, " << under << " under water";
    ss << "\nliquids      ";
    for (const Liquid& L : liquids()) ss << " " << L.name << " (" << fmtNum(L.density, 0) << " kg/m3)";
    return ss.str();
}

std::string World::waterTextOf(Object& o) {
    std::ostringstream ss;
    ensureWaterCache(o);
    const Object::WaterState& st = o.water;
    ss << "#" << o.id << " " << o.name << ": " << fmtNum(o.mass, 3) << " kg, " << fmtNum(o.volume * 1e6, 1) << " cm3 ("
       << fmtNum(materials()[o.material].density, 0) << " kg/m3)\n";
    if (st.body) {
        const WaterBody* b = findWater(st.body);
        const double rho = b ? liquids()[b->liquid].density : 998.0;
        ss << "in water      #" << st.body << ", center of mass " << cm(std::abs(st.depth), 1) << " cm " << (st.depth >= 0 ? "below" : "above")
           << " the surface\n";
        ss << "displaced     " << fmtNum(st.displaced * 1e6, 1) << " cm3 = " << fmtNum(st.displaced * rho, 3) << " kg of water"
           << " (" << fmtNum(100.0 * st.solidSub / std::max(o.volume, 1e-12), 0) << " % of the material under water)\n";
        ss << "buoyancy      " << fmtNum(glm::length(st.buoyancy), 2) << " N, weight " << fmtNum((o.mass + st.extraMass) * kG, 2)
           << " N, water drag " << fmtNum(glm::length(st.drag), 2) << " N\n";
    } else {
        ss << "not in water\n";
    }
    const WaterCache& wc = *o.waterCache;
    if (wc.rooms.empty()) {
        ss << "hollows       none (water cannot get in)";
    } else {
        for (size_t j = 0; j < wc.rooms.size(); ++j) {
            const WaterRoom& r = wc.rooms[j];
            const double in = j < o.rooms.size() ? o.rooms[j].water : 0.0;
            ss << "hollow " << j + 1 << "      " << fmtNum(r.volume * 1e6, 1) << " cm3, " << (r.openings.empty() ? "sealed" : "open " + fmtNum(r.openingArea * 1e4, 1) + " cm2")
               << ", water inside " << fmtNum(in * 1e6, 1) << " cm3 (" << fmtNum(100.0 * in / r.volume, 0) << " %)\n";
        }
    }
    return ss.str();
}
