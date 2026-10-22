// Aufprall: spröde Teile zerbrechen, und ein deutlich härteres Werkzeug hinterlässt je nach Form Spuren.
// Die Form des Werkzeugs an der Kontaktstelle wird vermessen (Querschnitt in mehreren Tiefen hinter der Spitze):
//   Spitze   -> Loch (Holz: bleibt stecken), in Ziegel/Beton/Granit bricht das Teil schon bei weniger Energie
//   Schneide -> Spalt, wenn die Energie für den Riss reicht (Holz nur entlang der Faser), sonst Kerbe (bleibt stecken)
//   stumpf   -> höchstens eine Delle, sonst wie bisher Bruch durch den Schlag
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <random>

#include "Materials.h"
#include "World.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

bool isWood(const Material& m) { return m.shading == Shading::Wood; }
bool isRubber(const Material& m) { return m.shading == Shading::Rubber; }
bool isBrittle(const Material& m) { return m.minBendRatio < 0; }  // Ziegel, Beton, Granit

glm::dmat4 frameOf(const Object& o) { return glm::translate(glm::dmat4(1.0), o.pos) * glm::mat4_cast(o.rot); }

void basisOf(const glm::dvec3& n, glm::dvec3& u, glm::dvec3& w) {
    u = glm::normalize(glm::cross(n, std::abs(n.x) < 0.9 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0)));
    w = glm::cross(n, u);
}

double extentAlong(const solid::Solid& s, const glm::dvec3& dir) {
    double lo = 1e30, hi = -1e30;
    for (const auto& p : s.pieces)
        for (const auto& f : p.faces)
            for (const auto& v : f.v) {
                const double d = glm::dot(v, dir);
                lo = std::min(lo, d);
                hi = std::max(hi, d);
            }
    return hi > lo ? hi - lo : 0.0;
}

// konvexe Hülle in 2D (monotone Kette), gegen den Uhrzeigersinn
std::vector<glm::dvec2> hull2d(std::vector<glm::dvec2> p) {
    std::sort(p.begin(), p.end(), [](const glm::dvec2& a, const glm::dvec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    if (p.size() < 3) return p;
    auto cross = [](const glm::dvec2& o, const glm::dvec2& a, const glm::dvec2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<glm::dvec2> h(2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 1e-18) --k;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 1e-18) --k;
        h[k++] = p[i - 1];
    }
    h.resize(k - 1);
    return h;
}

double polyArea(const std::vector<glm::dvec2>& h) {
    double a = 0;
    for (size_t i = 0; i < h.size(); ++i) {
        const glm::dvec2 &p = h[i], &q = h[(i + 1) % h.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return 0.5 * std::abs(a);
}

// Querschnitt durch die Ebene dot(n, x) = d (alles lokal im Teil)
struct Section {
    double area = 0, perimeter = 0;
    double narrow = 0, wide = 0;  // kleinste Breite und Breite quer dazu (m)
    glm::dvec3 center{0};         // Mitte des Querschnitts
    glm::dvec3 longDir{0};        // Richtung der größeren Breite: bei einer Schneide die Schneide selbst
};

// outline: hängt den Umriss vereinfacht an (höchstens 8 äußerste Punkte, etwas nach außen gerückt, damit er den echten
// Umriss umschließt)
Section sectionOf(const std::vector<solid::Piece>& pieces, const glm::dvec3& n, double d, std::vector<glm::dvec3>* outline = nullptr) {
    glm::dvec3 u, w;
    basisOf(n, u, w);
    Section s;
    std::vector<glm::dvec2> all, pts;
    for (const auto& pc : pieces) {
        pts.clear();
        for (const auto& f : pc.faces) {
            const size_t k = f.v.size();
            for (size_t i = 0; i < k; ++i) {
                const glm::dvec3 &a = f.v[i], &b = f.v[(i + 1) % k];
                const double da = glm::dot(n, a) - d, db = glm::dot(n, b) - d;
                if (std::abs(da) < 1e-9)
                    pts.push_back({glm::dot(a, u), glm::dot(a, w)});
                else if ((da < 0) != (db < 0) && std::abs(db) >= 1e-9) {
                    const glm::dvec3 p = a + (b - a) * (da / (da - db));
                    pts.push_back({glm::dot(p, u), glm::dot(p, w)});
                }
            }
        }
        if (pts.size() < 3) continue;
        auto h = hull2d(pts);
        if (h.size() < 3) continue;
        s.area += polyArea(h);  // Stücke überlappen nicht: Flächen addieren sich
        all.insert(all.end(), h.begin(), h.end());
    }
    if (s.area < 1e-12) return Section{};
    const auto h = hull2d(all);
    glm::dvec2 c(0);
    for (size_t i = 0; i < h.size(); ++i) {
        s.perimeter += glm::length(h[(i + 1) % h.size()] - h[i]);
        c += h[i];
    }
    c /= double(h.size());
    s.center = u * c.x + w * c.y + n * d;
    if (outline)
        for (int k = 0; k < 8; ++k) {
            const glm::dvec2 dir(std::cos(k * kPi / 4), std::sin(k * kPi / 4));
            glm::dvec2 far = h[0];
            for (const auto& p : h)
                if (glm::dot(p, dir) > glm::dot(far, dir)) far = p;
            const glm::dvec2 q = c + (far - c) * 1.09;  // 1/cos 22,5°: Achteck um den Umriss
            outline->push_back(u * q.x + w * q.y + n * d);
        }
    // kleinste Breite über 36 Richtungen, die größere Breite quer dazu
    double best = 1e30;
    glm::dvec2 bestDir(1, 0);
    for (int k = 0; k < 36; ++k) {
        const glm::dvec2 dir(std::cos(k * kPi / 36), std::sin(k * kPi / 36));
        double lo = 1e30, hi = -1e30;
        for (const auto& p : h) {
            lo = std::min(lo, glm::dot(p, dir));
            hi = std::max(hi, glm::dot(p, dir));
        }
        if (hi - lo < best) {
            best = hi - lo;
            bestDir = dir;
        }
    }
    const glm::dvec2 across(-bestDir.y, bestDir.x);
    double lo = 1e30, hi = -1e30;
    for (const auto& p : h) {
        lo = std::min(lo, glm::dot(p, across));
        hi = std::max(hi, glm::dot(p, across));
    }
    s.narrow = best;
    s.wide = hi - lo;
    s.longDir = u * across.x + w * across.y;
    return s;
}

// Tiefen hinter der Spitze, in denen das Werkzeug vermessen wird (m)
constexpr double kDepths[] = {0.0005, 0.001, 0.002, 0.003, 0.005, 0.0075, 0.01, 0.015, 0.02, 0.03, 0.045, 0.06};
constexpr int kNumDepths = int(sizeof(kDepths) / sizeof(kDepths[0]));
constexpr int k2mm = 2, k5mm = 4, k10mm = 6;
constexpr size_t kMaxImpactPieces = 40;  // mehr Stücke: kein Abdruck, kein Spalten durch Werkzeuge

enum class ToolForm { Blunt, Point, Edge };

struct ToolProfile {
    bool ok = false;
    ToolForm form = ToolForm::Blunt;
    double area[kNumDepths] = {}, perimeter[kNumDepths] = {};  // Querschnitt je Tiefe (hinter dem Werkzeugende: größter bisher)
    glm::dvec3 tipPoint{0};   // Spitze (lokal im Werkzeug)
    glm::dvec3 edgeDir{0};    // Schneide (lokal)
    double halfAngle = 0;     // halber Keilwinkel der Schneide (rad)
    double width5 = 0, narrow5 = 0;
    std::vector<solid::Piece> near;  // Werkzeug im Umkreis der Kontaktstelle (lokal)
    glm::dvec3 n{0};                 // Stoßrichtung (lokal)
    double tip = 0;                  // Lage der Spitze entlang n

    static double interp(const double* v, double s) {
        if (s <= 0) return 0;
        if (s <= kDepths[0]) return v[0] * s / kDepths[0];
        for (int i = 1; i < kNumDepths; ++i)
            if (s <= kDepths[i]) return v[i - 1] + (v[i] - v[i - 1]) * (s - kDepths[i - 1]) / (kDepths[i] - kDepths[i - 1]);
        return v[kNumDepths - 1];
    }
    double areaAt(double s) const { return interp(area, s); }
    double perimeterAt(double s) const { return interp(perimeter, s); }
};

// Querschnitte des Werkzeugs hinter seiner Spitze (in Stoßrichtung n), nur nahe der Kontaktstelle:
// eine lange Klinge trifft das Teil nur dort, wo sie auftrifft.
ToolProfile measureTool(const Object& t, const glm::dvec3& nWorld, const glm::dvec3& contactWorld) {
    ToolProfile p;
    const glm::dquat inv = glm::conjugate(t.rot);
    const glm::dvec3 n = glm::normalize(inv * nWorld), c = inv * (contactWorld - t.pos);
    glm::dvec3 u, w;
    basisOf(n, u, w);
    const double R = 0.1;
    std::vector<solid::Piece> near;
    for (const auto& pc : t.solid.pieces) {
        solid::Piece cur = pc, tmp;
        bool keep = true;
        for (const glm::dvec3& a : {u, -u, w, -w}) {
            solid::Plane pl;
            pl.n = a;
            pl.d = glm::dot(a, c) + R;
            if (!solid::clip(cur, pl, tmp)) {
                keep = false;
                break;
            }
            cur = std::move(tmp);
        }
        if (keep) near.push_back(std::move(cur));
    }
    double tip = -1e30;
    for (const auto& pc : near)
        for (const auto& f : pc.faces)
            for (const auto& v : f.v) tip = std::max(tip, glm::dot(n, v));
    if (near.empty() || tip < -1e29) return p;
    p.n = n;
    p.tip = tip;
    p.near = near;
    Section sec[kNumDepths];
    double maxA = 0, maxP = 0;
    for (int i = 0; i < kNumDepths; ++i) {
        sec[i] = sectionOf(near, n, tip - kDepths[i]);
        maxA = std::max(maxA, sec[i].area);
        maxP = std::max(maxP, sec[i].perimeter);
        p.area[i] = maxA;  // hinter dem Ende des Werkzeugs drückt seine Rückseite weiter
        p.perimeter[i] = maxP;
        if (sec[i].area > 0 && !p.ok) {
            p.tipPoint = sec[i].center + n * kDepths[i];
            p.ok = true;
        }
    }
    if (!p.ok) return p;
    const Section &s2 = sec[k2mm], &s5 = sec[k5mm], &s10 = sec[k10mm];
    p.width5 = s5.wide;
    p.narrow5 = s5.narrow;
    // Keilwinkel aus der Zunahme der Dicke zwischen 2 und 10 mm hinter der Schneide
    const double tanHalf = std::clamp((s10.narrow - s2.narrow) / (2.0 * (kDepths[k10mm] - kDepths[k2mm])), 0.03, 1.0);
    p.halfAngle = std::atan(tanHalf);
    if (s5.area > 0 && s5.narrow < 0.012 && s5.wide > 2.5 * s5.narrow && p.halfAngle < glm::radians(40.0)) {
        p.form = ToolForm::Edge;
        p.edgeDir = glm::normalize(s5.longDir);
    } else if (s5.wide < 0.02) {
        p.form = ToolForm::Point;
    }
    return p;
}

// Eindringtiefe: Die Energie wird gegen die Härte verbraucht, W(x) = ∫ H · A(s) ds (A = Querschnitt in der Tiefe s).
// Tritt die Spitze hinten aus einem dünnen Teil aus, trägt nur noch der Querschnitt im Material.
// flank = eingedrungene Seitenfläche (hält das Werkzeug durch Reibung fest).
double penetration(const ToolProfile& p, double H, double thick, double E, double& flank) {
    const double ds = 0.00025, maxX = kDepths[kNumDepths - 1];
    double W = 0, x = 0;
    flank = 0;
    while (x < maxX) {
        const double s = x + ds * 0.5;
        const double a = p.areaAt(s) - p.areaAt(s - thick);
        const double per = p.perimeterAt(s) - p.perimeterAt(s - thick);
        const double dW = H * a * ds;
        if (dW > 0 && W + dW >= E) {
            const double f = (E - W) / dW;
            x += ds * f;
            flank += per * ds * f;
            break;
        }
        W += dW;
        x += ds;
        flank += per * ds;
    }
    return x;
}

const char* formName(ToolForm f) { return f == ToolForm::Point ? "point" : f == ToolForm::Edge ? "edge" : "blunt face"; }

} // namespace

// ============================================================================ Aufprall

// Je Paar zählt der stärkste Aufprall. Ist ein Teil deutlich härter, entscheidet seine Form (toolImpact).
// Sonst: Aufprallenergie gegen Bruchenergie, E > Bruchenergie (J/m²) × Bruchquerschnitt -> das Teil zerbricht.
// Der Querschnitt ist der kleinste Querschnitt quer zur längsten Achse (dort bricht ein Ziegel oder Brett).
void World::processImpacts() {
    std::vector<ImpactInfo> impacts = physics_.takeImpacts();
    if (!breakage_ || impacts.empty()) return;
    std::sort(impacts.begin(), impacts.end(), [](const ImpactInfo& a, const ImpactInfo& b) { return a.energy > b.energy; });
    struct Pending {
        uint32_t id;
        glm::dvec3 point;
        double ratio, energy, threshold;
    };
    std::vector<Pending> pending;
    std::vector<std::pair<BodyHandle, BodyHandle>> seen;
    for (const ImpactInfo& imp : impacts) {
        if (imp.energy < 0.05) continue;
        const std::pair<BodyHandle, BodyHandle> key = std::minmax(imp.a, imp.b);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;  // zusammengesetzte Formen melden mehrere Kontakte
        seen.push_back(key);
        Object* A = fromBody(imp.a);
        Object* B = fromBody(imp.b);
        if (A && B && (toolImpact(*A, *B, imp, true) || toolImpact(*B, *A, imp, false))) continue;
        if (imp.energy < 0.2) continue;
        for (BodyHandle bh : {imp.a, imp.b}) {
            Object* o = fromBody(bh);
            if (!o || o->fixed || o->unbreakable || o->kind != ObjKind::Solid || simTime_ < o->breakCooldown) continue;
            glm::dvec3 ext = o->bbMax - o->bbMin;
            double e[3] = {ext.x, ext.y, ext.z};
            std::sort(e, e + 3);
            if (e[0] < 0.008 || o->volume < 2e-6) continue;  // zu klein zum weiteren Zerbrechen
            const double threshold = materials()[o->material].fractureEnergy * e[0] * e[1];
            if (imp.energy < threshold) continue;
            bool dup = false;
            for (auto& p : pending) dup |= p.id == o->id;
            if (!dup) pending.push_back({o->id, imp.point, imp.energy / threshold, imp.energy, threshold});
        }
    }
    for (const Pending& p : pending) fracture(p.id, p.point, p.ratio, p.energy, p.threshold);
}

bool World::toolImpact(Object& T, Object& O, const ImpactInfo& imp, bool toolIsA) {
    if (T.kind != ObjKind::Solid || O.kind != ObjKind::Solid || O.unbreakable || simTime_ < O.breakCooldown || O.volume < 2e-6)
        return false;
    // Stark bearbeitete Teile (viele Stücke) bleiben unverändert: ihre Kollision neu zu berechnen dauert dann Sekunden
    // und würde das Spiel bei jedem Treffer ruckeln lassen
    if (O.solid.pieces.size() > kMaxImpactPieces) return false;
    const Material& mo = materials()[O.material];
    if (isRubber(mo)) return false;  // Gummi federt zurück, es bleibt nichts
    const double hardO = hardnessMPa(O.material);
    if (hardnessMPa(T.material) < 1.5 * hardO) return false;  // nur ein deutlich härteres Teil wirkt als Werkzeug
    const glm::dvec3 n = glm::normalize(toolIsA ? imp.normal : -imp.normal);  // vom Werkzeug in das Teil
    const glm::dvec3 pT = toolIsA ? imp.point : imp.pointB, pO = toolIsA ? imp.pointB : imp.point;
    const glm::dvec3 vO = glm::dvec3(physics_.linearVelocity(O.body)), wO = glm::dvec3(physics_.angularVelocity(O.body));
    // Liegt das Teil still auf etwas und wird von oben getroffen, stützt die Unterlage es ab (Brett auf dem Hackklotz):
    // dann wirkt die ganze Bewegungsenergie des Werkzeugs, nicht nur der Anteil aus der reduzierten Masse.
    double E = imp.energy;
    if (!O.fixed && glm::length(vO) < 0.3 && n.y < -0.5) E = std::max(E, 0.5 * T.mass * imp.speed * imp.speed);
    if (E < 0.05) return false;

    const ToolProfile tp = measureTool(T, n, pT);
    if (!tp.ok) return false;
    const glm::dvec3 grain = O.rot * glm::dvec3(1, 0, 0);  // Holzfaser entlang der lokalen x-Achse
    double H = hardO * 1e6;                                 // Härte (Pa), Holz quer zur Faser
    if (isWood(mo)) H *= 1.0 + 1.5 * glm::dot(n, grain) * glm::dot(n, grain);  // Hirnholz ist etwa 2,5-mal härter
    // Reibung zwischen Werkzeug und Teil, mit Schmierstoff
    float muS = 0, muK = 0;
    frictionPair(T.material, O.material, muS, muK);
    const auto& lub = lubricants();
    for (int l : {T.lubricant, O.lubricant})
        if (l > 0 && l < (int)lub.size()) {
            muS = std::min(muS, lub[l].muS);
            muK = std::min(muK, lub[l].muK);
        }
    const glm::dquat invO = glm::conjugate(O.rot);
    double flank = 0;
    const double x = penetration(tp, H, extentAlong(O.solid, invO * n), E, flank);
    std::string note;

    // ---- Schneide: spalten, wenn der Keil genug Energie in den Riss bringt
    if (tp.form == ToolForm::Edge && mo.splitEnergy > 0) {
        glm::dvec3 s = glm::cross(T.rot * tp.edgeDir, n);  // Normale der Spaltebene (enthält Schneide und Stoßrichtung)
        double G = mo.splitEnergy;
        double across = 0;
        if (isWood(mo) && glm::length(s) > 1e-6) {
            s = glm::normalize(s);
            across = std::abs(glm::dot(s, grain));  // 0: Schneide entlang der Faser, 1: quer
            G += (mo.fractureEnergy - mo.splitEnergy) * across * across;
            s -= grain * glm::dot(s, grain);  // der Riss läuft entlang der Faser
        }
        if (glm::length(s) > 0.2) {
            s = glm::normalize(s);
            const glm::dvec3 sL = glm::normalize(invO * s), pL = invO * (pO - O.pos);
            const double crack = sectionOf(O.solid.pieces, sL, glm::dot(sL, pL)).area;
            // Keil: Von der Arbeit F·dx öffnet nur der Anteil tanθ / (tanθ + µ) den Spalt, der Rest wird an den Flanken zu
            // Reibungswärme. Stumpfe Keile zerdrücken das Material eher, als dass sie es spalten: Faktor cos 2θ.
            const double ta = std::tan(tp.halfAngle);
            const double eta = ta / (ta + muK) * std::cos(2.0 * tp.halfAngle);
            const double open = E * eta, need = G * crack;
            if (crack > 0 && open >= need && !O.fixed) {
                const std::string name = O.name;
                const double mass = O.mass;
                const glm::dvec3 pos0 = O.pos;
                solid::Plane pl;
                pl.n = sL;
                pl.d = glm::dot(sL, pL);
                std::vector<uint32_t> pieces;
                std::string err;
                if (cut(O.id, pl, 0.0, &pieces, &err)) {
                    // die Hälften fliegen auseinander, das Werkzeug fährt mit der übrigen Energie weiter
                    const double rest = open - need;
                    const double vs = 0.3 + std::min(2.5, std::sqrt(rest / std::max(mass, 1e-3)));
                    for (uint32_t pid : pieces)
                        if (Object* p = find(pid)) {
                            const double side = glm::dot(p->pos - pO, s) >= 0 ? 1.0 : -1.0;
                            physics_.activate(p->body);
                            physics_.setVelocity(p->body, glm::vec3(vO + glm::cross(wO, p->pos - pos0) + s * side * vs), glm::vec3(wO));
                            p->breakCooldown = simTime_ + 0.2;
                        }
                    const glm::dvec3 vT = glm::dvec3(physics_.linearVelocity(T.body));
                    const double forward = imp.speed * std::sqrt(std::clamp(1.0 - need / open, 0.0, 1.0)) * 0.5;
                    physics_.setVelocity(T.body, glm::vec3(vT + n * (forward - glm::dot(vT - vO, n))), physics_.angularVelocity(T.body));
                    T.breakCooldown = simTime_ + 0.2;
                    ++brokenCount_;
                    events_.push_back(name + " split" + (isWood(mo) ? " along the grain" : "") + " by " + T.name + " (" +
                                      std::to_string(pieces.size()) + " pieces): " + fmtNum(open, 1) + " J open the crack (" +
                                      fmtNum(eta * 100, 0) + " % of " + fmtNum(E, 1) + " J, edge " +
                                      fmtNum(glm::degrees(2 * tp.halfAngle), 0) + "°, friction " + fmtNum(muK, 2) + "), crack " +
                                      fmtNum(crack * 1e4, 0) + " cm² needs " + fmtNum(need, 1) + " J");
                    return true;
                }
            }
            if (crack > 0 && !O.fixed)
                note = ", too weak to split (" + fmtNum(open, 1) + " of " + fmtNum(need, 1) + " J)";
        }
        if (across > 0.5) note = ", edge across the grain: cuts in, does not split";
    }

    // ---- Spitze in Ziegel, Beton, Granit: die Spannung sammelt sich an der Spitze, das Teil bricht viel früher
    glm::dvec3 ext = O.bbMax - O.bbMin;
    double e[3] = {ext.x, ext.y, ext.z};
    std::sort(e, e + 3);
    const double bluntLimit = mo.fractureEnergy * e[0] * e[1];
    if (tp.form == ToolForm::Point && isBrittle(mo) && !O.fixed && e[0] >= 0.008 && E >= 0.4 * bluntLimit) {
        fracture(O.id, pO, E / (0.4 * bluntLimit), E, 0.4 * bluntLimit);
        T.breakCooldown = simTime_ + 0.2;
        return true;
    }
    if (!O.fixed && imp.energy >= bluntLimit) return false;  // zerbricht ohnehin durch den Schlag

    // ---- Delle, Loch oder Kerbe: ab 2 mm Tiefe drückt sich das Werkzeug sichtbar ab
    if (x < 0.002) return false;
    // Werkzeug mit allem, was daran befestigt ist (Stiel ...), gemeinsam verschieben
    std::vector<Object*> group = {&T};
    bool movable = !T.fixed && !T.hinge;
    for (size_t gi = 0; gi < group.size(); ++gi)
        for (auto& j : joints_) {
            const uint32_t other = j->a == group[gi]->id ? j->b : (j->b == group[gi]->id ? j->a : 0);
            if (!other || other == O.id) continue;
            Object* g = find(other);
            if (!g || std::find(group.begin(), group.end(), g) != group.end()) continue;
            movable &= !g->fixed && !g->hinge;
            group.push_back(g);
        }
    // Steckenbleiben: Holz federt zurück und klemmt eine Spitze oder Schneide ein
    const bool stick = isWood(mo) && tp.form != ToolForm::Blunt && x >= 0.004 && flank > 1e-6 && movable;
    // Oberfläche unter der Spitze
    const glm::dvec3 tipW = T.pos + T.rot * tp.tipPoint;
    glm::dvec3 surf = pO;
    double t = 0;
    if (rayToObject(O, tipW - n * 0.05, n, t)) surf = tipW - n * 0.05 + n * t;
    const double gap = glm::dot(surf - tipW, n);
    if (std::abs(gap) > 0.05) return false;  // Spitze liegt nicht an der Kontaktstelle
    const glm::dvec3 shift = n * (gap + x);
    // Abdruck in der Lage nach dem Eindringen, als ein einfaches konvexes Stück: Umriss des Werkzeugs an der Spitze, in
    // halber Tiefe und knapp über der Oberfläche, je auf 8 Punkte vereinfacht. Wenige Ebenen zerlegen das Teil kaum,
    // seine Kollision bleibt schnell neu zu berechnen (die echte Werkzeugform machte aus einem Brett Hunderte Stücke).
    std::vector<glm::dvec3> outline;
    for (double d : {kDepths[0], x + 0.003}) sectionOf(tp.near, tp.n, tp.tip - d, &outline);
    solid::Piece imprint = solid::hullPiece(outline);
    if (imprint.faces.empty()) return false;
    solid::transform(imprint, glm::inverse(frameOf(O)) * glm::translate(glm::dmat4(1.0), shift) * frameOf(T));
    const std::string name = O.name;
    const glm::dvec3 pos0 = O.pos;
    std::vector<uint32_t> ids;
    double removed = 0;
    std::string err;
    // ohne Vorab-Zerlegung: möglichst wenige neue Stücke
    solid::Solid carved = solid::subtract(O.solid, solid::planesOf(imprint), &removed, false);
    if (removed < 1e-12 || !applyResult(O, std::move(carved), &ids, &err)) return false;
    for (uint32_t id : ids)
        if (Object* p = find(id)) {
            if (!p->fixed) {
                physics_.activate(p->body);
                physics_.setVelocity(p->body, glm::vec3(vO + glm::cross(wO, p->pos - pos0)), glm::vec3(wO));
            }
            p->breakCooldown = simTime_ + 0.2;
        }
    T.breakCooldown = simTime_ + 0.2;
    const char* mark = tp.form == ToolForm::Point ? "hole" : tp.form == ToolForm::Edge ? "notch" : "dent";
    const std::string depth = fmtNum(x * 1000, 0) + " mm deep (" + fmtNum(E, 1) + " J, " + formName(tp.form) + ")";
    if (!stick) {
        events_.push_back(std::string(mark) + " in " + name + " from " + T.name + ": " + depth + note);
        return true;
    }
    // Werkzeug in seinen Abdruck setzen, es bewegt sich ab jetzt mit dem Teil
    for (Object* g : group) setTransform(*g, g->pos + shift, g->rot);
    const glm::dvec3 vAt = O.fixed ? glm::dvec3(0) : vO + glm::cross(wO, T.pos - pos0);
    for (Object* g : group) physics_.setVelocity(g->body, glm::vec3(vAt), glm::vec3(O.fixed ? glm::dvec3(0) : wO));
    // Haltekraft: Das Holz drückt mit etwa 3 % seiner Härte auf die Flanken, Haftreibung hält das Werkzeug fest
    const double clampMPa = 0.03 * hardO;
    JointState js;
    js.type = JointType::Glue;
    js.a = O.id;
    js.b = T.id;
    js.preset = kStuckPreset;
    const glm::dvec3 mid = surf + n * (x * 0.5);
    js.anchorA = glm::conjugate(O.rot) * (mid - O.pos);
    js.anchorB = glm::conjugate(T.rot) * (mid - T.pos);
    js.normalA = glm::normalize(glm::conjugate(O.rot) * -n);
    js.area = flank;
    js.ft = muS * clampMPa;
    js.fs = 2.0 * js.ft;  // seitlich stützt zusätzlich die Lochwand
    js.lengthMm = x * 1000;
    Joint* j = addJoint(js, &err);
    events_.push_back(T.name + " sticks in " + name + ": " + mark + " " + depth + note +
                      (j ? ", held by friction up to " + fmtNum(j->rT, 0) + " N" : std::string()));
    return true;
}

// ============================================================================ Zerbrechen

void World::fracture(uint32_t id, const glm::dvec3& worldPoint, double ratio, double energy, double threshold) {
    static std::mt19937 rng(4711);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    Object* o = find(id);
    if (!o) return;
    const std::string name = o->name;
    const glm::dvec3 v = glm::dvec3(physics_.linearVelocity(o->body)), w = glm::dvec3(physics_.angularVelocity(o->body));
    const glm::dvec3 pos0 = o->pos;

    auto cutAt = [&](Object* t, const glm::dvec3& wp, bool alongLong, std::vector<uint32_t>& out) {
        glm::dvec3 ext = t->bbMax - t->bbMin;
        int k = 0;
        for (int i = 1; i < 3; ++i)
            if (ext[i] > ext[k]) k = i;
        glm::dvec3 n(0);
        if (alongLong) {
            n[k] = 1;  // Bruch quer zur längsten Achse, leicht schräg
            glm::dvec3 tilt(uni(rng), uni(rng), uni(rng));
            tilt[k] = 0;
            n = glm::normalize(n + tilt * 0.35);
        } else {
            n = glm::normalize(glm::dvec3(uni(rng), uni(rng), uni(rng)) + glm::dvec3(1e-3));
        }
        glm::dvec3 lp = glm::conjugate(t->rot) * (wp - t->pos);
        // nicht ganz am Rand brechen (sonst nur ein Splitter)
        glm::dvec3 lo = t->bbMin + ext * 0.25, hi = t->bbMax - ext * 0.25;
        lp = glm::clamp(lp, lo, hi);
        solid::Plane pl;
        pl.n = n;
        pl.d = glm::dot(n, lp);
        std::string err;
        return cut(t->id, pl, 0.0, &out, &err);
    };

    std::vector<uint32_t> pieces;
    if (!cutAt(o, worldPoint, true, pieces)) return;
    if (ratio > 3.0 && !pieces.empty()) {  // sehr harter Schlag: weiter zersplittern
        std::vector<uint32_t> more;
        if (Object* big = find(pieces[0])) {
            glm::dvec3 ext = big->bbMax - big->bbMin;
            if (std::min({ext.x, ext.y, ext.z}) > 0.01 && cutAt(big, worldPoint, false, more))
                for (uint32_t m : more)
                    if (std::find(pieces.begin(), pieces.end(), m) == pieces.end()) pieces.push_back(m);
        }
    }
    // Bruchstücke fliegen mit der Bewegung des ganzen Teils weiter
    for (uint32_t pid : pieces)
        if (Object* p = find(pid)) {
            physics_.activate(p->body);
            physics_.setVelocity(p->body, glm::vec3(v + glm::cross(w, p->pos - pos0)), glm::vec3(w));
            p->breakCooldown = simTime_ + 0.2;
        }
    ++brokenCount_;
    events_.push_back(name + " shattered (" + std::to_string(pieces.size()) + " pieces): impact " + fmtNum(energy, 1) +
                      " J, breaking limit " + fmtNum(threshold, 1) + " J");
}
