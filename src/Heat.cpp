// Wärme und Feuer.
// Jedes Teil bekommt bei Bedarf ein grobes Gitter aus Zellen (höchstens 10 entlang der längsten Achse, 400 insgesamt).
// Je Zelle: Temperatur, höchste Temperatur, Verkohlung, Brennstoff, Schmelze. Je Wärmeschritt (20 pro Sekunde):
//   - Wärmeleitung im Teil (λ, Zellgrößen), zwischen Teilen an den Kontaktpunkten der Physik und in den Boden
//   - Abkühlen an der Luft: freie bzw. erzwungene Konvektion und Wärmestrahlung (ε·σ·T⁴)
//   - Reibungswärme aus dem Löser (Reibimpuls · Gleitgeschwindigkeit)
//   - Werkzeuge: Flamme oder Kältemittel trifft eine Stelle (Wärmeübergang h zur Flammen- bzw. Kältetemperatur)
//   - Holz, Gummi, Kunststoff verkohlen ab der Zersetzungstemperatur (schwarz) und brennen ab der Zündtemperatur.
//     Brennende Zellen setzen Wärme frei (Abbrand · Heizwert). Zusammenhängende brennende Stellen bilden ein Feuer:
//     Flammenhöhe nach Heskestad, Flammentemperatur und Strahlung steigen mit der Größe. Was in der Flamme steckt,
//     wird durch Konvektion und Strahlung geheizt und fängt selbst Feuer; die Umgebung bekommt Wärmestrahlung.
//   - Metall, Stein, Kunststoff: über dem Schmelzpunkt wird die Schmelzwärme aufgenommen, dann ist die Zelle flüssig.
//     Flüssige Stellen an der Oberfläche tropfen ab (Tropfen -> Pfütze), abgebrannte Stellen verschwinden.
// Übergänge zu einer Zieltemperatur werden exakt (exponentiell) gerechnet: stabil auch im Zeitraffer.
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "Materials.h"
#include "Jobs.h"
#include "Profile.h"
#include "World.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSigma = 5.670374e-8;   // Stefan-Boltzmann W/(m²·K⁴)
constexpr double kKelvin = 273.15;
constexpr double kHeatStep = 0.05;       // Wärmeschritt (s Spielzeit)
constexpr int kMaxDim = 10;              // Zellen entlang der längsten Achse
constexpr int kMaxCells = 400;
constexpr double kMinCell = 0.004;
constexpr size_t kMaxMeltObjects = 80;   // mehr Tropfen und Pfützen nicht
constexpr size_t kMaxResidueObjects = 80;  // mehr Kohlestücke nicht
const glm::ivec3 kNb[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

double kelvin(double c) { return c + kKelvin; }

// Zelle mit der Wärmekapazität C geht über den Leitwert G (W/K) in der Zeit dt auf die Temperatur Tt zu (exakt)
double relax(double T, double Tt, double G, double C, double dt) {
    if (G <= 0 || C <= 0) return T;
    return Tt + (T - Tt) * std::exp(-G * dt / C);
}

// Wärmestrahlung als Wärmeübergang (linearisiert): h_r = ε·σ·(T² + Tu²)·(T + Tu)
double radiationH(double eps, double tC, double uC) {
    const double a = kelvin(tC), b = kelvin(uC);
    return eps * kSigma * (a * a + b * b) * (a + b);
}

glm::ivec3 cellCoord(const HeatGrid& g, int i) { return {i % g.dim.x, (i / g.dim.x) % g.dim.y, i / (g.dim.x * g.dim.y)}; }
glm::dvec3 cellCenter(const HeatGrid& g, int i) { return g.origin + (glm::dvec3(cellCoord(g, i)) + 0.5) * g.cell; }
bool inGrid(const HeatGrid& g, const glm::ivec3& c) { return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < g.dim.x && c.y < g.dim.y && c.z < g.dim.z; }

// Zelle mit Material, die dem Punkt (lokal) am nächsten liegt. -1: Gitter leer
int nearestInside(const HeatGrid& g, const glm::dvec3& p) {
    if (g.empty()) return -1;
    const glm::ivec3 c = glm::clamp(glm::ivec3(glm::floor((p - g.origin) / g.cell)), glm::ivec3(0), g.dim - 1);
    const int i = g.index(c.x, c.y, c.z);
    if (g.cells[i].inside) return i;
    int best = -1;
    double bd = 1e30;
    for (int k = 0; k < (int)g.cells.size(); ++k) {
        if (!g.cells[k].inside) continue;
        const double d = glm::length(cellCenter(g, k) - p);
        if (d < bd) {
            bd = d;
            best = k;
        }
    }
    return best;
}

// Zellen an der Oberfläche: Richtung nach außen (lokal), Summe der offenen Seiten
glm::dvec3 outward(const HeatGrid& g, int i) {
    const glm::ivec3 c = cellCoord(g, i);
    glm::dvec3 n(0);
    for (const glm::ivec3& d : kNb) {
        const glm::ivec3 q = c + d;
        if (!inGrid(g, q) || !g.cells[g.index(q.x, q.y, q.z)].inside) n += glm::dvec3(d);
    }
    return glm::length(n) > 1e-9 ? glm::normalize(n) : glm::dvec3(0, 1, 0);
}

double cellSize(const HeatGrid& g) { return std::max({g.cell.x, g.cell.y, g.cell.z}); }

// Kleber werden weich: ab soft sinkt die Festigkeit, bei fail ist nichts mehr übrig (Datenblätter, gerundet)
void glueLimits(const std::string& key, double& soft, double& fail) {
    soft = 70;
    fail = 140;
    if (key == "woodglue") soft = 60, fail = 110;
    else if (key == "epoxy") soft = 80, fail = 180;
    else if (key == "superglue") soft = 80, fail = 150;
    else if (key == "pu") soft = 90, fail = 180;
    else if (key == "hotglue") soft = 50, fail = 90;
    else if (key == "construction") soft = 70, fail = 140;
}

// Anteil des Zellvolumens, der beim Abbrennen noch steht: Verkohlen allein schrumpft Holz wenig (bis 75 %, das Holzgas
// entweicht), die Flamme frisst dann die Kohle von außen weg
float burnVolume(float fuel) { return fuel >= 0.35f ? 0.75f + 0.25f * (fuel - 0.35f) / 0.65f : 0.75f * fuel / 0.35f; }
// in Viertelschritten: die Form wird nur bei einem Sprung neu gebaut (Kollision neu berechnen kostet Zeit)
float burnLevel(float fuel) {
    if (fuel <= 0.001f) return 0.0f;
    return std::clamp(std::ceil(burnVolume(fuel) * 4.0f - 1e-4f) / 4.0f, 0.25f, 1.0f);
}

// Quader einer Zelle (lokal), auf den Anteil "level" geschrumpft: entlang der Achsen, auf denen die Zelle frei liegt, wie
// Holz von außen nach innen abbrennt. Geschrumpft wird immer zur Mitte des Gitters hin: so behalten Nachbarzellen ihre
// gemeinsame Fläche und das Teil zerfällt nicht in Krümel. keep: Zellen, die stehen bleiben (alles andere gilt als frei)
void cellBox(const HeatGrid& g, int i, float level, const std::vector<uint8_t>& keep, glm::dvec3& mn, glm::dvec3& mx) {
    const glm::ivec3 c = cellCoord(g, i);
    mn = g.origin + glm::dvec3(c) * g.cell;
    mx = mn + g.cell;
    if (level >= 0.999f) return;
    bool open[3];
    int k = 0;
    for (int a = 0; a < 3; ++a) {
        glm::ivec3 qn = c, qp = c;
        --qn[a];
        ++qp[a];
        open[a] = !inGrid(g, qn) || !keep[g.index(qn.x, qn.y, qn.z)] || !inGrid(g, qp) || !keep[g.index(qp.x, qp.y, qp.z)];
        k += open[a];
    }
    const double f = std::pow((double)std::max(level, 0.01f), 1.0 / std::max(k, 1));
    for (int a = 0; a < 3; ++a) {
        if (k > 0 && !open[a]) continue;
        const double L = g.cell[a] * f;
        const double mid = 2 * c[a] + 1 - g.dim[a];  // > 0: Zelle liegt auf der oberen Hälfte des Gitters
        if (mid > 0) mx[a] = mn[a] + L;
        else if (mid < 0) mn[a] = mx[a] - L;
        else {
            const double m = 0.5 * (mn[a] + mx[a]);
            mn[a] = m - 0.5 * L;
            mx[a] = m + 0.5 * L;
        }
    }
}

// Mittel, Kleinst- und Höchstwerte eines Teils neu berechnen. true: im Gleichgewicht mit der Luft (Ta)
bool summarizeHeat(Object& o, double Ta) {
    Object::HeatInfo& hi = o.heatInfo;
    hi.tMin = 1e9f;
    hi.tMax = -1e9f;
    double sum = 0, chr = 0, melt = 0;
    int n = 0, burning = 0;
    bool calm = true;
    for (HeatCell& c : o.heat.cells) {
        if (!c.inside) continue;
        c.peak = std::max(c.peak, c.temp);
        hi.tMin = std::min(hi.tMin, c.temp);
        hi.tMax = std::max(hi.tMax, c.temp);
        sum += c.temp;
        chr += c.chr;
        melt += c.melt;
        burning += c.burning;
        ++n;
        if (std::abs(c.temp - Ta) > 0.3 || c.burning || (c.melt > 0 && c.melt < 1)) calm = false;
    }
    if (n == 0) hi.tMin = hi.tMax = (float)Ta;
    n = std::max(n, 1);
    hi.tMean = (float)(sum / n);
    if (sum == 0 && hi.tMin > 1e8f) hi.tMean = (float)Ta;
    hi.chr = (float)(chr / n);
    hi.melt = (float)(melt / n);
    hi.burning = burning;
    return calm;
}

} // namespace

// ============================================================================ Gitter

bool World::ensureHeat(Object& o) {
    if (!o.heat.empty()) return true;
    if (o.volume <= 0) return false;
    fitHeat(o);
    return !o.heat.empty();
}

void World::fitHeat(Object& o) {
    HeatGrid old = std::move(o.heat);
    HeatGrid& g = o.heat;
    g = HeatGrid();
    if (o.heatInfo.keepLayout && !old.empty()) {
        // Die Form wurde gerade aus diesem Gitter gebaut (Abbrennen): gleiche Zellen, damit Werte und Schrumpfstufen passen
        g.dim = old.dim;
        g.cell = old.cell;
        g.origin = old.origin;
    } else {
        const glm::dvec3 ext = glm::max(o.bbMax - o.bbMin, glm::dvec3(1e-4));
        double cell = std::max(std::max({ext.x, ext.y, ext.z}) / kMaxDim, kMinCell);
        glm::ivec3 dim;
        for (;;) {
            dim = glm::max(glm::ivec3(glm::ceil(ext / cell - 1e-6)), glm::ivec3(1));
            if (dim.x * dim.y * dim.z <= kMaxCells) break;
            cell *= 1.12;
        }
        g.dim = dim;
        g.cell = ext / glm::dvec3(dim);
        g.origin = o.bbMin;
    }
    o.heatInfo.keepLayout = false;
    const glm::ivec3 dim = g.dim;
    g.cells.resize((size_t)dim.x * dim.y * dim.z);
    // Welche Zellen enthalten Material? Mitte und 8 Punkte darum prüfen (dünne Stege)
    const bool exact = o.kind == ObjKind::Solid && !o.solid.pieces.empty();
    // Begrenzung jedes Stücks vorab: die meisten Stücke sind schnell ausgeschlossen
    std::vector<std::pair<glm::dvec3, glm::dvec3>> pieceBox(exact ? o.solid.pieces.size() : 0);
    for (size_t p = 0; p < pieceBox.size(); ++p) solid::bounds(o.solid.pieces[p], pieceBox[p].first, pieceBox[p].second);
    auto inside = [&](const glm::dvec3& q) {
        for (size_t p = 0; p < pieceBox.size(); ++p) {
            const auto& [mn, mx] = pieceBox[p];
            if (q.x < mn.x - 1e-9 || q.y < mn.y - 1e-9 || q.z < mn.z - 1e-9 || q.x > mx.x + 1e-9 || q.y > mx.y + 1e-9 || q.z > mx.z + 1e-9)
                continue;
            bool in = !o.solid.pieces[p].faces.empty();
            for (const auto& f : o.solid.pieces[p].faces)
                if (glm::dot(f.n, q) - f.d > 1e-9) {
                    in = false;
                    break;
                }
            if (in) return true;
        }
        return false;
    };
    int nInside = 0;
    for (int i = 0; i < (int)g.cells.size(); ++i) {
        const glm::dvec3 c = cellCenter(g, i);
        bool in = !exact || inside(c);
        for (int k = 0; k < 8 && !in; ++k) {
            const glm::dvec3 off((k & 1) ? 0.3 : -0.3, (k & 2) ? 0.3 : -0.3, (k & 4) ? 0.3 : -0.3);
            in = inside(c + off * g.cell);
        }
        g.cells[i].inside = in;
        nInside += in;
    }
    if (nInside == 0) {
        for (auto& c : g.cells) c.inside = 1;
        nInside = (int)g.cells.size();
    }
    o.heatInfo.cellMass = o.mass / nInside;
    // Oberfläche je Zelle: offene Seiten, skaliert auf die echte Oberfläche des Teils
    const double faceA[3] = {g.cell.y * g.cell.z, g.cell.x * g.cell.z, g.cell.x * g.cell.y};
    double gridArea = 0;
    for (int i = 0; i < (int)g.cells.size(); ++i) {
        if (!g.cells[i].inside) continue;
        const glm::ivec3 c = cellCoord(g, i);
        double a = 0;
        for (int d = 0; d < 6; ++d) {
            const glm::ivec3 q = c + kNb[d];
            if (!inGrid(g, q) || !g.cells[g.index(q.x, q.y, q.z)].inside) a += faceA[d / 2];
        }
        g.cells[i].area = (float)a;
        gridArea += a;
    }
    double trueArea = 0;
    for (const auto& f : o.aero) trueArea += f.area;
    if (trueArea > 0 && gridArea > 0)
        for (auto& c : g.cells) c.area = (float)(c.area * trueArea / gridArea);
    // Werte übernehmen
    const bool fuel = combustible(o.material);
    const bool same = !old.empty() && old.dim == g.dim && glm::length(old.origin - g.origin) < 1e-9 && glm::length(old.cell - g.cell) < 1e-9;
    for (int i = 0; i < (int)g.cells.size(); ++i) {
        HeatCell& c = g.cells[i];
        const int k = same ? i : nearestInside(old, cellCenter(g, i));
        if (k >= 0 && (same ? old.cells[k].inside : true)) {
            const HeatCell& s = old.cells[k];
            c.temp = s.temp;
            c.peak = s.peak;
            c.chr = s.chr;
            c.fuel = s.fuel;
            c.melt = s.melt;
            c.burning = s.burning && c.inside && c.area > 0;
            // Schrumpfstufe gilt nur im gleichen Gitter; in einem neuen ist die Form schon so, wie der Brennstoff sagt
            c.shape = same ? s.shape : (fuel ? burnLevel(c.fuel) : 1.0f);
        } else {
            c.temp = c.peak = (float)ambient_;
            c.fuel = fuel ? 1.0f : 0.0f;
            c.shape = 1.0f;
        }
    }
    summarizeHeat(o, ambient_);
    o.heatInfo.idle = false;
}

// ============================================================================ Einstellen und Abfragen

void World::setAmbient(double tempC) {
    ambient_ = std::clamp(tempC, -273.0, 3000.0);
    for (auto& o : objects_) o->heatInfo.idle = false;
}

void World::setTemperature(Object& o, double tempC) {
    tempC = std::max(tempC, -273.0);
    if (!ensureHeat(o)) return;
    const Thermal& th = thermal(o.material);
    for (HeatCell& c : o.heat.cells) {
        c.temp = (float)tempC;
        c.peak = std::max(c.peak, c.temp);
        if (th.meltC > 0) c.melt = tempC > th.meltC ? 1.0f : 0.0f;
    }
    o.heatInfo.idle = false;
    o.heatInfo.tMin = o.heatInfo.tMax = o.heatInfo.tMean = (float)tempC;
}

double World::temperatureAt(const Object& o, const glm::dvec3& p) const {
    const int i = nearestInside(o.heat, glm::conjugate(o.rot) * (p - o.pos));
    return i < 0 ? ambient_ : o.heat.cells[i].temp;
}

double World::charAt(const Object& o, const glm::dvec3& p) const {
    const int i = nearestInside(o.heat, glm::conjugate(o.rot) * (p - o.pos));
    return i < 0 ? 0.0 : o.heat.cells[i].chr;
}

double World::hardnessAt(const Object& o, const glm::dvec3& p) const {
    double h = hardnessMPa(o.material);
    if (o.heat.empty()) return h * strengthFactor(o.material, ambient_);
    const int i = nearestInside(o.heat, glm::conjugate(o.rot) * (p - o.pos));
    if (i < 0) return h;
    const HeatCell& c = o.heat.cells[i];
    h *= std::max(0.002f, strengthFactor(o.material, c.temp));
    if (combustible(o.material)) h *= 1.0 - 0.8 * c.chr;  // Holzkohle ist weich
    return h;
}

int World::burningParts() const {
    int n = 0;
    for (const auto& o : objects_) n += o->heatInfo.burning > 0;
    return n;
}

void World::addHeatSource(uint32_t id, const glm::dvec3& local, int tool, double seconds) {
    if (tool < 0 || tool >= (int)heatTools().size()) return;
    const double until = heatTime_ + seconds * heatSpeed_;
    for (HeatSource& s : heatSources_)
        if (s.id == id && s.tool == tool && glm::length(s.local - local) < 0.01) {
            s.local = local;
            s.until = std::max(s.until, until);
            return;
        }
    heatSources_.push_back({id, local, tool, until});
    if (Object* o = find(id)) o->heatInfo.idle = false;
}

bool World::ignite(Object& o, const glm::dvec3* local, std::string* err) {
    if (!combustible(o.material)) {
        if (err) *err = std::string(materials()[o.material].name) + " does not burn - heat it with 'torch' or 'heat <id> <°C>'";
        return false;
    }
    if (airDensity_ < 0.3) {
        if (err) *err = "no oxygen: fire needs air ('air normal')";
        return false;
    }
    if (!ensureHeat(o)) {
        if (err) *err = "the part has no volume";
        return false;
    }
    HeatGrid& g = o.heat;
    int best = -1;
    if (local) {
        double bd = 1e30;
        for (int i = 0; i < (int)g.cells.size(); ++i)
            if (g.cells[i].inside && g.cells[i].area > 0 && g.cells[i].fuel > 0.02) {
                const double d = glm::length(cellCenter(g, i) - *local);
                if (d < bd) bd = d, best = i;
            }
    } else {  // unterste Stelle (dort zündet man ein Feuer an)
        double by = 1e30;
        for (int i = 0; i < (int)g.cells.size(); ++i)
            if (g.cells[i].inside && g.cells[i].area > 0 && g.cells[i].fuel > 0.02) {
                const double y = (o.pos + o.rot * cellCenter(g, i)).y;
                if (y < by - 1e-6) by = y, best = i;
            }
    }
    if (best < 0) {
        if (err) *err = "nothing left to burn";
        return false;
    }
    const Thermal& th = thermal(o.material);
    HeatCell& c = g.cells[best];
    c.temp = std::max(c.temp, th.ignitionC + 120.0f);
    c.peak = std::max(c.peak, c.temp);
    c.chr = std::max(c.chr, 0.5f);
    c.burning = 1;
    addHeatSource(o.id, cellCenter(g, best), 0, 8.0);  // ein paar Sekunden mit dem Feuerzeug nachhelfen
    o.heatInfo.idle = false;
    return true;
}

int World::extinguish(Object* only) {
    int n = 0;
    for (auto& op : objects_) {
        Object& o = *op;
        if ((only && &o != only) || o.heat.empty()) continue;
        for (HeatCell& c : o.heat.cells)
            if (c.burning) {
                c.burning = 0;
                c.temp = std::min(c.temp, 100.0f);  // Wasser: verdampft bei 100 °C
                ++n;
            }
        o.heatInfo.idle = false;
    }
    heatSources_.erase(std::remove_if(heatSources_.begin(), heatSources_.end(),
                                      [&](const HeatSource& s) { return heatTools()[s.tool].flame && (!only || s.id == only->id); }),
                       heatSources_.end());
    if (!only) fires_.clear();
    return n;
}

// Verbindungen werden warm weich: Kleber erweicht, Holz verkohlt um den Nagel, Stahl verliert Festigkeit
double World::jointHeatFactor(const Joint& j) const {
    const Object* A = const_cast<World*>(this)->find(j.a);
    const Object* B = const_cast<World*>(this)->find(j.b);
    if (!A || !B || (A->heat.empty() && B->heat.empty() && ambient_ < 50 && ambient_ > -30)) return 1.0;
    glm::dvec3 pa, pb;
    jointPoints(j, pa, pb);
    const double ta = temperatureAt(*A, pa), tb = temperatureAt(*B, pb);
    const double ca = combustible(A->material) ? charAt(*A, pa) : 0.0, cb = combustible(B->material) ? charAt(*B, pb) : 0.0;
    const double mat = std::min(strengthFactor(A->material, ta), strengthFactor(B->material, tb));
    double f = 1.0;
    switch (j.type) {
    case JointType::Glue: {
        if (j.preset == kStuckPreset) {
            f = mat * (1.0 - 0.9 * std::max(ca, cb));
            break;
        }
        double soft, fail;
        glueLimits(j.preset, soft, fail);
        const double t = 0.5 * (ta + tb);
        const double g = t <= soft ? 1.0 : std::max(0.0, 1.0 - (t - soft) / (fail - soft));
        f = g * mat * (1.0 - 0.95 * std::max(ca, cb));
        break;
    }
    case JointType::Nail:
    case JointType::Screw:
        f = std::min<double>(strengthFactor(5, 0.5 * (ta + tb)), mat) * (1.0 - 0.9 * std::max(ca, cb));  // Stahlstift
        break;
    case JointType::Weld:
    case JointType::Hinge: f = mat * (1.0 - 0.9 * std::max(ca, cb)); break;
    default: break;
    }
    return std::clamp(f, 1e-3, 1.0);
}

// ============================================================================ Reibungswärme

void World::collectFrictionHeat(float) {
    for (const ContactImpulse& c : physics_.stepImpulses()) {
        const double e = glm::length(glm::dvec3(c.frictionImpulse)) * c.slip;  // J in diesem Teilschritt
        if (e < 1e-7) continue;
        FrictionHeat& f = frictionHeat_[(uint64_t(c.a) << 32) | c.b];
        f.energy += e;
        f.point += c.point * e;
        f.a = c.a;
        f.b = c.b;
    }
}

// ============================================================================ Wärmeschritt

void World::stepHeatOnly(double dt) {
    if (!heatOn_ || simulating_) return;
    heatAccum_ += dt;
    for (int n = 0; heatAccum_ >= kHeatStep && n < 4; ++n) {
        heatAccum_ -= kHeatStep;
        stepHeat(kHeatStep * heatSpeed_);
    }
    heatAccum_ = std::min(heatAccum_, kHeatStep);
}

void World::stepHeat(double dt) {
    prof::Lap lap;
    heatTime_ += dt;
    const double Ta = ambient_;
    const double airK = std::clamp(airDensity_ / 1.204, 0.0, 3.0);  // Konvektion und Sauerstoff wachsen mit der Luftdichte
    const bool oxygen = airDensity_ > 0.3;
    // Merker dieses Schritts je Teil (jedes Teil hat seine eigenen: die Schleifen laufen auf mehreren Kernen)
    for (auto& op : objects_) {
        op->heatInfo.hasPilot = false;
        op->heatInfo.boost = 0;
    }
    // Zellen mit Flamme daran (zündet Brennbares)
    auto pilotOf = [&](Object& o) -> std::vector<uint8_t>& {
        if (!o.heatInfo.hasPilot) {
            o.heatInfo.pilot.assign(o.heat.cells.size(), 0);
            o.heatInfo.hasPilot = true;
        }
        return o.heatInfo.pilot;
    };
    auto capacity = [&](const Object& o) { return std::max(1e-6, o.heatInfo.cellMass * thermal(o.material).specificHeat); };

    // ---- Werkzeuge: Flamme oder Kältemittel auf eine Stelle
    heatSources_.erase(std::remove_if(heatSources_.begin(), heatSources_.end(),
                                      [&](const HeatSource& s) { return s.until < heatTime_ || !find(s.id); }),
                       heatSources_.end());
    for (const HeatSource& s : heatSources_) {
        Object* o = find(s.id);
        if (!o || !ensureHeat(*o)) continue;
        const HeatTool& tool = heatTools()[s.tool];
        if (tool.flame && !oxygen) continue;  // ohne Luft keine Flamme
        HeatGrid& g = o->heat;
        const double r = tool.radius, reach = r + 0.5 * glm::length(g.cell);
        std::vector<std::pair<int, double>> hit;
        double wsum = 0;
        for (int i = 0; i < (int)g.cells.size(); ++i) {
            const HeatCell& c = g.cells[i];
            if (!c.inside || c.area <= 0) continue;
            const double d = glm::length(cellCenter(g, i) - s.local);
            if (d > reach) continue;
            const double w = std::exp(-(d / reach) * (d / reach)) * c.area;
            hit.emplace_back(i, w);
            wsum += w;
        }
        if (hit.empty()) {
            const int i = nearestInside(g, s.local);
            if (i < 0) continue;
            hit.emplace_back(i, 1.0);
            wsum = 1.0;
        }
        const double spot = kPi * r * r;
        auto& pl = pilotOf(*o);
        const double C = capacity(*o);
        for (auto& [i, w] : hit) {
            HeatCell& c = g.cells[i];
            const double A = std::min<double>(std::max(c.area, 1e-6f), spot * w / wsum + 1e-9);
            c.temp = (float)relax(c.temp, tool.tempC, tool.h * A, C, dt);
            if (tool.flame) pl[i] = 1;
        }
        o->heatInfo.idle = false;
    }

    lap.mark("heat.tools");
    // ---- Berührung: Wärmeleitung zwischen Teilen und in den Boden (nur wenn die Physik läuft)
    if (simulating_) {
        std::unordered_map<uint64_t, int> points;  // Kontaktpunkte je Paar
        for (const StepContact& c : physics_.stepContacts())
            if (c.depth > -0.001f) ++points[(uint64_t(c.a) << 32) | c.b];
        auto footprint = [](const Object& o) {
            glm::dvec3 e = o.bbMax - o.bbMin;
            double s[3] = {e.x, e.y, e.z};
            std::sort(s, s + 3);
            return s[1] * s[2];
        };
        for (const StepContact& c : physics_.stepContacts()) {
            if (c.depth <= -0.001f) continue;
            Object* A = fromBody(c.a);
            Object* B = fromBody(c.b);
            if (!A && !B) continue;
            if (!A) std::swap(A, B);  // A ist immer ein Teil, B ein Teil oder der Boden
            const bool hotA = !A->heat.empty() && !A->heatInfo.idle;
            const bool hotB = B && !B->heat.empty() && !B->heatInfo.idle;
            if (!hotA && !hotB) continue;  // beide in Ruhe (gleich warm)
            if (!ensureHeat(*A) || (B && !ensureHeat(*B))) continue;
            const int n = std::max(1, points[(uint64_t(c.a) << 32) | c.b]);
            const double area = 0.3 * std::min(footprint(*A), B ? footprint(*B) : 1e9) / n;
            const glm::dvec3 p = c.point;
            const int ia = nearestInside(A->heat, glm::conjugate(A->rot) * (p - A->pos));
            if (ia < 0) continue;
            HeatCell& ca = A->heat.cells[ia];
            const Thermal& ta = thermal(A->material);
            // Wärmewiderstand: Übergang 3000 W/(m²·K) + halbe Zelle in jedem Teil (Holz isoliert, Kupfer leitet)
            double R = 1.0 / 3000.0 + 0.5 * glm::compMin(A->heat.cell) / ta.conductivity;
            const double CA = capacity(*A);
            if (B) {
                const int ib = nearestInside(B->heat, glm::conjugate(B->rot) * (p - B->pos));
                if (ib < 0) continue;
                HeatCell& cb = B->heat.cells[ib];
                R += 0.5 * glm::compMin(B->heat.cell) / thermal(B->material).conductivity;
                const double CB = capacity(*B), G = area / R;
                const double te = (CA * ca.temp + CB * cb.temp) / (CA + CB);
                const double k = 1.0 - std::exp(-G * dt * (1.0 / CA + 1.0 / CB));
                ca.temp += (float)((te - ca.temp) * k);
                cb.temp += (float)((te - cb.temp) * k);
                B->heatInfo.idle = false;
            } else {
                R += 0.05 / 1.7;  // Betonboden: 5 cm bis zur Umgebungstemperatur
                ca.temp = (float)relax(ca.temp, Ta, area / R, CA, dt);
            }
            A->heatInfo.idle = false;
        }
    }

    lap.mark("heat.contacts");
    // ---- Reibungswärme: halb in jedes der beiden Teile
    for (auto& [key, f] : frictionHeat_) {
        const glm::dvec3 p = f.point / std::max(f.energy, 1e-12);
        for (BodyHandle bh : {f.a, f.b}) {
            Object* o = fromBody(bh);
            if (!o) continue;
            const double C0 = o->heatInfo.cellMass * thermal(o->material).specificHeat;
            if (o->heat.empty() && (C0 <= 0 || 0.5 * f.energy / C0 < 0.02)) continue;  // kaum spürbar
            if (!ensureHeat(*o)) continue;
            const int i = nearestInside(o->heat, glm::conjugate(o->rot) * (p - o->pos));
            if (i < 0) continue;
            o->heat.cells[i].temp += (float)(0.5 * f.energy / capacity(*o));
            o->heatInfo.idle = false;
        }
    }
    frictionHeat_.clear();

    lap.mark("heat.friction");
    // ---- Feuer (aus dem letzten Schritt) und glühende Teile heizen ihre Umgebung
    // Feuer: Flamme heizt, was in ihr steckt; Strahlung auf die Umgebung
    struct FireHeat {
        const Fire* F;
        double L, R0, Qr, reach, qMax;
        glm::dvec3 axis, center, lo, hi;
    };
    std::vector<FireHeat> fireHeat;
    for (const Fire& F : fires_) {
        // Die Flamme leckt etwas über den Rand der brennenden Fläche: so kriecht das Feuer über das Holz weiter
        double spot = 0;
        for (const Fire::Spot& s : F.spots) spot += s.size;
        spot /= std::max<size_t>(1, F.spots.size());
        FireHeat h;
        h.F = &F;
        h.L = std::max(F.height, 0.05);
        h.R0 = std::max(0.5 * F.diameter + 0.4 * spot, 0.03);
        const double ub = 2.5 * std::sqrt(h.L + 0.1);  // Auftrieb der Flamme (m/s): Wind drückt sie zur Seite
        h.axis = glm::normalize(glm::dvec3(0, ub, 0) + wind_ * 0.8);
        h.Qr = 0.3 * F.power;                             // abgestrahlter Anteil
        h.reach = std::sqrt(h.Qr / (4.0 * kPi * 300.0));  // bis hier kommen noch 300 W/m²
        h.center = F.base + h.axis * (0.4 * h.L);
        h.qMax = F.emissivity * kSigma * std::pow(kelvin(F.tempC), 4);
        h.lo = F.base - glm::dvec3(h.R0 + h.reach);
        h.hi = F.base + h.axis * h.L + glm::dvec3(h.R0 + h.reach);
        h.lo.y = std::min(h.lo.y, F.base.y - 0.2);
        fireHeat.push_back(h);
    }
    // Glühende Teile (ab 300 °C) strahlen auf ihre Nachbarn. Teile, die nah beieinander liegen (Glutbett), werden zu einer
    // Strahlungsquelle zusammengefasst: sonst strahlt jedes Kohlestück auf jedes andere (quadratisch viel Arbeit).
    struct Radiator {
        double Q = 0, tw = 0;
        glm::dvec3 c{0};
    };
    std::vector<Radiator> own(objects_.size());  // Strahlung jedes Teils (die eigene zählt für es selbst nicht)
    std::vector<int> groupOf(objects_.size(), -1);
    std::vector<Radiator> groups;
    {
        std::vector<std::pair<int64_t, int>> bins;
        for (int k = 0; k < (int)objects_.size(); ++k) {
            const Object& E = *objects_[k];
            if (E.heat.empty() || E.heatInfo.tMax < 300) continue;
            Radiator& r = own[k];
            const double eps = thermal(E.material).emissivity;
            for (int i = 0; i < (int)E.heat.cells.size(); ++i) {
                const HeatCell& hc = E.heat.cells[i];
                if (!hc.inside || hc.area <= 0 || hc.temp < 300) continue;
                const double q = eps * kSigma * (std::pow(kelvin(hc.temp), 4) - std::pow(kelvin(Ta), 4)) * hc.area;
                r.Q += q;
                r.c += (E.pos + E.rot * cellCenter(E.heat, i)) * q;
                r.tw = std::max<double>(r.tw, hc.temp);
            }
            if (r.Q < 50) {
                r = Radiator();
                continue;
            }
            r.c /= r.Q;
            const glm::ivec3 b = glm::ivec3(glm::floor(r.c / 0.3));  // 30 cm: ein Glutbett ist eine Quelle
            bins.push_back({(int64_t(b.x & 0x1fffff) << 42) | (int64_t(b.y & 0x1fffff) << 21) | int64_t(b.z & 0x1fffff), k});
        }
        std::sort(bins.begin(), bins.end());
        for (size_t i = 0; i < bins.size(); ++i) {
            if (i == 0 || bins[i].first != bins[i - 1].first) groups.emplace_back();
            Radiator& g = groups.back();
            const Radiator& r = own[bins[i].second];
            g.c = (g.c * g.Q + r.c * r.Q) / (g.Q + r.Q);
            g.Q += r.Q;
            g.tw = std::max(g.tw, r.tw);
            groupOf[bins[i].second] = (int)groups.size() - 1;
        }
    }
    // Jedes Teil nimmt Flamme und Strahlung auf: unabhängig voneinander, darum auf alle Kerne verteilt
    jobs::parallelFor(objects_.size(), [&](size_t k) {
        Object& o = *objects_[k];
        bool touched = false;
        for (const FireHeat& fh : fireHeat) {
            const Fire& F = *fh.F;
            if (o.wMax.x < fh.lo.x || o.wMin.x > fh.hi.x || o.wMax.y < fh.lo.y || o.wMin.y > fh.hi.y || o.wMax.z < fh.lo.z || o.wMin.z > fh.hi.z)
                continue;
            if (!ensureHeat(o)) continue;
            HeatGrid& g = o.heat;
            const Thermal& th = thermal(o.material);
            const double C = capacity(o), half = 0.5 * cellSize(g);
            auto& pl = pilotOf(o);
            for (int i = 0; i < (int)g.cells.size(); ++i) {
                HeatCell& c = g.cells[i];
                if (!c.inside || c.area <= 0) continue;
                const glm::dvec3 p = o.pos + o.rot * cellCenter(g, i);
                const glm::dvec3 r = p - F.base;
                const double z = glm::dot(r, fh.axis);
                const double radial = glm::length(r - fh.axis * z);
                const double Rz = std::max(fh.R0 * (1.0 - 0.6 * std::clamp(z / fh.L, 0.0, 1.0)), 0.03);
                if (z > -half && z < fh.L + half && radial < Rz + half) {  // in der Flamme
                    const double tl = Ta + (F.tempC - Ta) * (1.0 - 0.55 * std::clamp(z / fh.L, 0.0, 1.0));
                    const double h = 40.0 + F.emissivity * th.emissivity * (radiationH(1.0, tl, c.temp));
                    c.temp = (float)relax(c.temp, tl, h * c.area, C, dt);
                    pl[i] = 1;
                    touched = true;
                    if (c.burning) o.heatInfo.boost = std::max(o.heatInfo.boost, F.emissivity);
                } else {
                    const double d2 = std::max(glm::dot(p - fh.center, p - fh.center), 1e-4);
                    const double q = std::min(fh.Qr / (4.0 * kPi * d2), fh.qMax) * th.emissivity;
                    if (q < 50.0 || c.temp >= F.tempC) continue;
                    const double G = q * c.area / std::max(F.tempC - c.temp, 1.0);  // bis höchstens Flammentemperatur
                    c.temp = (float)relax(c.temp, F.tempC, G, C, dt);
                    touched = true;
                }
            }
        }
        for (int gi = 0; gi < (int)groups.size(); ++gi) {
            Radiator src = groups[gi];
            if (groupOf[k] == gi) {  // die eigene Strahlung zählt nicht
                src.Q = groups[gi].Q - own[k].Q;
                if (src.Q < 50) continue;
                src.c = (groups[gi].c * groups[gi].Q - own[k].c * own[k].Q) / src.Q;
            }
            const double reach = std::sqrt(src.Q / (4.0 * kPi * 300.0));
            const glm::dvec3 q0 = glm::clamp(src.c, o.wMin, o.wMax);
            if (glm::length(q0 - src.c) > reach) continue;
            if (!ensureHeat(o)) continue;
            HeatGrid& g = o.heat;
            const double C = capacity(o), eo = thermal(o.material).emissivity;
            const double qMax = 0.9 * kSigma * std::pow(kelvin(src.tw), 4);
            for (int i = 0; i < (int)g.cells.size(); ++i) {
                HeatCell& hc = g.cells[i];
                if (!hc.inside || hc.area <= 0 || hc.temp >= src.tw) continue;
                const glm::dvec3 p = o.pos + o.rot * cellCenter(g, i);
                const double q = std::min(src.Q / (4.0 * kPi * std::max(glm::dot(p - src.c, p - src.c), 1e-4)), qMax) * eo;
                if (q < 50.0) continue;
                hc.temp = (float)relax(hc.temp, src.tw, q * hc.area / std::max(src.tw - hc.temp, 1.0), C, dt);
                touched = true;
            }
        }
        if (touched) o.heatInfo.idle = false;
    });
    lap.mark("heat.fires+radiators");

    // ---- Jedes Teil: Luft, Verbrennung, Schmelzen, Wärmeleitung im Inneren
    struct BurnSpot {
        glm::dvec3 pos;
        glm::vec3 normal;
        float size, power;
        uint32_t obj;
        float soot;
    };
    // Jedes Teil rechnet nur mit seinen eigenen Zellen: auf alle Kerne verteilt
    std::vector<Object*> active;
    for (auto& op : objects_)
        if (!op->heat.empty() && !op->heatInfo.idle) active.push_back(op.get());
    std::vector<std::vector<BurnSpot>> spotsOf(active.size());
    std::vector<glm::vec3> velocity(active.size(), glm::vec3(0));  // Fahrtwind: Physik vorher abfragen (Hauptthread)
    if (simulating_)
        for (size_t k = 0; k < active.size(); ++k)
            if (active[k]->body != kNoBody) velocity[k] = physics_.linearVelocity(active[k]->body);
    jobs::parallelFor(active.size(), [&](size_t k) {
        Object& o = *active[k];
        std::vector<BurnSpot>& spots = spotsOf[k];
        HeatGrid& g = o.heat;
        const Thermal& th = thermal(o.material);
        const bool fuelMat = combustible(o.material);
        const double C = capacity(o), m = std::max(o.heatInfo.cellMass, 1e-9);
        const std::vector<uint8_t>* pl = o.heatInfo.hasPilot ? &o.heatInfo.pilot : nullptr;
        // Fahrtwind
        double vrel = glm::length(wind_);
        if (o.body != kNoBody && simulating_) vrel = glm::length(glm::dvec3(velocity[k]) - wind_);
        const double forced = 6.0 * std::pow(vrel, 0.8);
        const double fb = o.heatInfo.boost;
        const glm::dmat3 rot = glm::mat3_cast(o.rot);
        for (int i = 0; i < (int)g.cells.size(); ++i) {
            HeatCell& c = g.cells[i];
            if (!c.inside) continue;
            // Luft: Konvektion (frei 1,31·ΔT^⅓, mit Fahrtwind mehr) und Strahlung
            if (c.area > 0) {
                const double h = airK * (1.31 * std::cbrt(std::abs(c.temp - Ta)) + forced) + radiationH(th.emissivity, c.temp, Ta);
                c.temp = (float)relax(c.temp, Ta, h * c.area, C, dt);
            }
            if (fuelMat) {
                // Zersetzen: Holz wird braun, dann schwarz (ab 200 °C langsam, ab 300 °C schnell)
                if (c.temp > th.pyrolysisC) {
                    const double rate = std::min(2.0, 0.01 * std::exp((c.temp - th.pyrolysisC) / 40.0));
                    c.chr = (float)std::min(1.0, c.chr + rate * dt);
                    if (c.fuel > 0.35f) c.fuel = (float)std::max(0.35, c.fuel - rate * dt * 0.5);  // Holzgas entweicht, Kohle bleibt
                }
                // Zünden: mit Flamme daneben ab der Zündtemperatur, ohne Flamme etwa 150 °C später
                if (!c.burning && c.fuel > 0.02f && c.area > 0 && oxygen) {
                    bool flame = pl && (*pl)[i];
                    const glm::ivec3 cc = cellCoord(g, i);
                    for (const glm::ivec3& d : kNb) {
                        const glm::ivec3 q = cc + d;
                        if (inGrid(g, q) && g.cells[g.index(q.x, q.y, q.z)].burning) flame = true;
                    }
                    if ((flame && c.temp >= th.ignitionC) || c.temp >= th.ignitionC + 150) c.burning = 1;
                }
                if (c.burning) {
                    if (!oxygen || c.fuel <= 0.0f || c.area <= 0 || c.temp < th.ignitionC - 50) {
                        c.burning = 0;
                        if (c.fuel <= 0.0f) c.chr = 1.0f;
                    } else {
                        // Abbrand: freie Oberfläche, mehr Sauerstoff und Strahlung eines großen Feuers beschleunigen ihn
                        const double mdot = th.burnRate * c.area * (0.5 + 0.5 * fb) * std::min(1.0, airK);
                        const double dm = std::min<double>(mdot * dt, c.fuel * m);
                        c.fuel = (float)std::max(0.0, c.fuel - dm / m);
                        const double Q = dm * th.heatOfCombustion;
                        // ein Teil der Wärme bleibt in der Glut, der Rest geht in die Flamme
                        c.temp = (float)std::min<double>(c.temp + 0.3 * Q / C, std::max<double>(c.temp, 1050.0));
                        c.chr = std::max(c.chr, (float)(0.6 + 0.4 * (1.0 - c.fuel)));
                        // die Flamme steigt von der ganzen Zelle auf: Mitte der Zelle, Richtung der freien Seiten
                        const glm::dvec3 n = rot * outward(g, i);
                        spots.push_back({o.pos + rot * cellCenter(g, i), glm::vec3(n), (float)(0.8 * cellSize(g)), (float)(Q / dt), o.id,
                                         th.soot});
                    }
                }
            }
            // Schmelzen und Erstarren: die Schmelzwärme hält die Temperatur am Schmelzpunkt
            if (th.meltC > 0 && th.latentHeat > 0) {
                const double mL = m * th.latentHeat;
                if (c.temp > th.meltC && c.melt < 1.0f) {
                    double f = c.melt + (c.temp - th.meltC) * C / mL;
                    if (f > 1.0) {
                        c.temp = (float)(th.meltC + (f - 1.0) * mL / C);
                        f = 1.0;
                    } else {
                        c.temp = th.meltC;
                    }
                    c.melt = (float)f;
                } else if (c.temp < th.meltC && c.melt > 0.0f) {
                    double f = c.melt - (th.meltC - c.temp) * C / mL;
                    if (f < 0.0) {
                        c.temp = (float)(th.meltC + f * mL / C);
                        f = 0.0;
                    } else {
                        c.temp = th.meltC;
                    }
                    c.melt = (float)f;
                }
            }
        }
        // Wärmeleitung im Teil: explizit in Teilschritten, so viele wie für die Stabilität nötig
        if (g.cells.size() > 1) {
            const double G[3] = {th.conductivity * g.cell.y * g.cell.z / g.cell.x, th.conductivity * g.cell.x * g.cell.z / g.cell.y,
                                 th.conductivity * g.cell.x * g.cell.y / g.cell.z};
            const double sumG = 2.0 * (G[0] + G[1] + G[2]);
            int sub = (int)std::ceil(dt * sumG / (0.45 * C));
            double scale = 1.0;
            if (sub > 40) {
                scale = 40.0 / sub;  // sehr dünne, gut leitende Zellen im Zeitraffer: Leitung etwas langsamer, aber stabil
                sub = 40;
            }
            sub = std::max(sub, 1);
            const double h = dt / sub;
            std::vector<float> dT(g.cells.size());
            for (int s = 0; s < sub; ++s) {
                std::fill(dT.begin(), dT.end(), 0.0f);
                for (int z = 0; z < g.dim.z; ++z)
                    for (int y = 0; y < g.dim.y; ++y)
                        for (int x = 0; x < g.dim.x; ++x) {
                            const int i = g.index(x, y, z);
                            if (!g.cells[i].inside) continue;
                            const int nx[3] = {x + 1 < g.dim.x ? g.index(x + 1, y, z) : -1, y + 1 < g.dim.y ? g.index(x, y + 1, z) : -1,
                                               z + 1 < g.dim.z ? g.index(x, y, z + 1) : -1};
                            for (int a = 0; a < 3; ++a) {
                                const int j = nx[a];
                                if (j < 0 || !g.cells[j].inside) continue;
                                const double q = G[a] * scale * (g.cells[j].temp - g.cells[i].temp) * h / C;
                                dT[i] += (float)q;
                                dT[j] -= (float)q;
                            }
                        }
                for (size_t i = 0; i < g.cells.size(); ++i) g.cells[i].temp += dT[i];
            }
        }
        // Viele brennende Zellen eines Teils zu wenigen Stellen zusammenfassen (kleine Teile: eine, große: höchstens 48).
        // Spart Rechenzeit beim Bilden der Feuer und Flammen in der Darstellung.
        const size_t maxSpots = o.volume < 5e-5 ? 1 : 48;
        if (spots.size() > maxSpots) {
            glm::dvec3 lo(1e30), hi(-1e30);
            for (const BurnSpot& s : spots) lo = glm::min(lo, s.pos), hi = glm::max(hi, s.pos);
            const double bin = std::max(1e-3, glm::compMax(hi - lo) / std::max(1.0, std::cbrt((double)maxSpots)) * 1.0001);
            std::vector<std::pair<int64_t, BurnSpot>> merged;
            for (const BurnSpot& s : spots) {
                const glm::ivec3 c = maxSpots == 1 ? glm::ivec3(0) : glm::ivec3(glm::floor((s.pos - lo) / bin));
                const int64_t key = (int64_t(c.x) << 40) | (int64_t(c.y) << 20) | int64_t(c.z);
                auto it = std::find_if(merged.begin(), merged.end(), [&](const auto& m) { return m.first == key; });
                if (it == merged.end()) {
                    merged.push_back({key, s});
                    merged.back().second.pos *= (double)s.power;
                    continue;
                }
                BurnSpot& m = it->second;
                m.pos += s.pos * (double)s.power;
                m.normal += s.normal;
                m.power += s.power;
                m.size = std::max(m.size, s.size);
            }
            spots.clear();
            for (auto& [key, m] : merged) {
                m.pos /= std::max<double>(m.power, 1e-9);
                m.normal = glm::length(m.normal) > 1e-6f ? glm::normalize(m.normal) : glm::vec3(0, 1, 0);
                m.size = std::max<float>(m.size, (float)(maxSpots == 1 ? glm::compMax(hi - lo) : bin));
                spots.push_back(m);
            }
        }
        // Zusammenfassung, Ruhe erkennen
        const bool calm = summarizeHeat(o, Ta);
        o.heatInfo.idle = calm && !o.heatInfo.hasPilot;
    });
    std::vector<BurnSpot> spots;
    for (auto& s : spotsOf) spots.insert(spots.end(), s.begin(), s.end());

    lap.mark("heat.cells (all parts)");
    // ---- Feuer: brennende Stellen zu Feuern zusammenfassen (Nachbarn, deren Flammen sich berühren)
    fires_.clear();
    if (!spots.empty()) {
        auto linkOf = [&](const BurnSpot& a, const BurnSpot& b) { return 1.2 * (a.size + b.size) + 0.03; };
        double link = 0.05;
        for (const BurnSpot& s : spots) link = std::max(link, linkOf(s, s));
        std::vector<int> parent(spots.size());
        std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](int i) {  // Union-Find mit Pfadhalbierung
            while (parent[i] != i) i = parent[i] = parent[parent[i]];
            return i;
        };
        // Stellen nach Rasterzelle sortiert: Nachbarn in den 27 Zellen ringsum per Binärsuche
        auto key = [](const glm::ivec3& c) { return (int64_t(c.x & 0x1fffff) << 42) | (int64_t(c.y & 0x1fffff) << 21) | int64_t(c.z & 0x1fffff); };
        std::vector<std::pair<int64_t, int>> bins(spots.size());
        for (int i = 0; i < (int)spots.size(); ++i) bins[i] = {key(glm::ivec3(glm::floor(spots[i].pos / link))), i};
        std::sort(bins.begin(), bins.end());
        for (int i = 0; i < (int)spots.size(); ++i) {
            const glm::ivec3 c = glm::ivec3(glm::floor(spots[i].pos / link));
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz) {
                        const int64_t k = key(c + glm::ivec3(dx, dy, dz));
                        auto it = std::lower_bound(bins.begin(), bins.end(), std::make_pair(k, -1));
                        for (; it != bins.end() && it->first == k; ++it) {
                            const int j = it->second;
                            if (j <= i) continue;
                            const int ri = root(i), rj = root(j);
                            if (ri == rj) continue;  // schon im selben Feuer
                            const glm::dvec3 d = spots[i].pos - spots[j].pos;
                            const double l = linkOf(spots[i], spots[j]);
                            if (glm::dot(d, d) < l * l) parent[rj] = ri;
                        }
                    }
        }
        std::unordered_map<int, size_t> fireOf;
        std::vector<glm::dvec3> lo, hi;
        std::vector<double> sootSum;
        for (int i = 0; i < (int)spots.size(); ++i) {
            const int r = root(i);
            auto it = fireOf.find(r);
            if (it == fireOf.end()) {
                it = fireOf.emplace(r, fires_.size()).first;
                fires_.emplace_back();
                lo.push_back(spots[i].pos);
                hi.push_back(spots[i].pos);
                sootSum.push_back(0);
            }
            Fire& F = fires_[it->second];
            const BurnSpot& s = spots[i];
            F.power += s.power;
            F.base += s.pos * (double)s.power;
            sootSum[it->second] += s.soot * s.power;
            lo[it->second] = glm::min(lo[it->second], s.pos - glm::dvec3(0.5 * s.size));
            hi[it->second] = glm::max(hi[it->second], s.pos + glm::dvec3(0.5 * s.size));
            F.spots.push_back({s.pos, s.normal, s.size, s.power});
        }
        for (size_t k = 0; k < fires_.size(); ++k) {
            Fire& F = fires_[k];
            const double P = std::max(F.power, 1.0);
            F.base /= P;
            F.base.y = lo[k].y;
            F.soot = sootSum[k] / P;
            F.diameter = std::max(0.05, std::max(hi[k].x - lo[k].x, hi[k].z - lo[k].z));
            // Heskestad: L = 0,235 Q^⅖ − 1,02 D (Q in kW). Über brennende Wände hinweg reicht die Flamme bis über die Oberkante.
            const double Lh = std::max(0.08, 0.235 * std::pow(P / 1000.0, 0.4) - 1.02 * F.diameter);
            F.height = Lh + (hi[k].y - lo[k].y);
            F.tempC = 750.0 + 320.0 * (1.0 - std::exp(-P / 30000.0));    // großes Feuer = heißer (bis etwa 1070 °C)
            F.emissivity = 1.0 - std::exp(-1.2 * (F.diameter + 0.15));  // dicke, rußige Flammen strahlen mehr
            if (F.spots.size() > 96) {
                std::sort(F.spots.begin(), F.spots.end(), [](const Fire::Spot& a, const Fire::Spot& b) { return a.power > b.power; });
                F.spots.resize(96);
            }
        }
    }

    lap.mark("heat.fireClusters");
    // ---- Form: Tropfen landen, abgebrannte und geschmolzene Stellen verschwinden (nur wenn die Physik läuft)
    if (simulating_) {
        for (auto& op : objects_) {  // Tropfen
            Object& o = *op;
            if (o.meltForm != MeltForm::Drop) continue;
            o.heatInfo.age += dt;
        }
        heatShapeAccum_ += dt;
        const bool cells = heatShapeAccum_ >= 0.1;  // reihum mit Zeitbudget, siehe heatShapes
        if (cells) heatShapeAccum_ = 0;
        heatShapes(cells);
        lap.mark(cells ? "heat.shapes (cells)" : "heat.shapes (drops)");
    }
}

// ============================================================================ Form ändern

void World::makePuddle(Object& o, double floorY) {
    const Thermal& th = thermal(o.material);
    double h = std::max(0.001, th.puddleMm / 1000.0);  // Oberflächenspannung: Höhe laut Materialdatei
    const double V = std::max(o.volume, 1e-9);
    double R = std::sqrt(V / (kPi * h));
    if (R < h) R = h = std::cbrt(V / kPi);
    solid::Solid s = solid::makeCylinder(R, h, std::clamp(int(R * 400), 16, 48));
    glm::dvec3 com;
    solid::volume(s, &com);
    solid::translate(s, -com);
    const double mean = o.heatInfo.tMean, melt = o.heatInfo.melt;
    o.solid = std::move(s);
    o.hullCache.reset();
    o.prim = 3;
    o.primSize = glm::dvec3(R, h / 2, 0);
    o.edits = 0;
    o.rot = glm::dquat(1, 0, 0, 0);
    o.pos = glm::dvec3(o.pos.x, floorY + h / 2 + 0.0005, o.pos.z);
    o.meltForm = MeltForm::Puddle;
    o.shape = "puddle D " + fmtNum(2 * R * 100) + " cm";
    if (o.name.rfind("Drop", 0) == 0) o.name = nextName("Puddle");
    rebuildGeometry(o);
    for (HeatCell& c : o.heat.cells) {  // eine Pfütze ist gleichmäßig warm
        c.temp = (float)mean;
        c.melt = (float)melt;
    }
    rebuildBody(o);
    // Liegt etwas im Weg (das Teil, von dem es tropft), staut sich die Schmelze: kleiner und dafür dicker
    for (int tries = 0; tries < 4 && o.body != kNoBody; ++tries) {
        bool blocked = false;
        for (const auto& [other, depth] : overlapsOf(o, 0.001)) {
            const Object* p = find(other);
            if (other != 0 && !(p && p->meltForm != MeltForm::None && p->material == o.material)) blocked = true;
        }
        if (!blocked) break;
        h *= 1.7;
        R = std::max(std::sqrt(V / (kPi * h)), 0.003);
        solid::Solid t = solid::makeCylinder(R, h, std::clamp(int(R * 400), 16, 48));
        solid::volume(t, &com);
        solid::translate(t, -com);
        o.solid = std::move(t);
        o.hullCache.reset();
        o.primSize = glm::dvec3(R, h / 2, 0);
        o.pos.y = floorY + h / 2 + 0.0005;
        rebuildGeometry(o);
        for (HeatCell& c : o.heat.cells) {
            c.temp = (float)mean;
            c.melt = (float)melt;
        }
        rebuildBody(o);
    }
    if (o.body != kNoBody) physics_.setVelocity(o.body, glm::vec3(0), glm::vec3(0));
}

void World::heatShapes(bool cells) {
    // ---- Tropfen: beim Aufkommen zur Pfütze, auf einer flüssigen Pfütze gleichen Materials mit ihr verschmelzen
    bool drops = false;
    for (const auto& o : objects_) drops |= o->meltForm == MeltForm::Drop;
    if (!drops && !cells) return;
    std::unordered_map<BodyHandle, std::vector<const StepContact*>> touching;
    for (const StepContact& c : physics_.stepContacts())
        if (c.depth > -0.002f) {
            touching[c.a].push_back(&c);
            touching[c.b].push_back(&c);
        }
    std::vector<uint32_t> gone;
    for (size_t k = 0; k < objects_.size(); ++k) {
        Object& o = *objects_[k];
        if (o.meltForm != MeltForm::Drop || o.body == kNoBody) continue;
        if (o.heatInfo.melt < 0.5f) {  // im Flug erstarrt: eine Kugel
            o.meltForm = MeltForm::None;
            continue;
        }
        auto it = touching.find(o.body);
        if (it == touching.end() && o.heatInfo.age < 8.0) continue;
        double floorY = o.wMin.y;
        Object* pool = nullptr;
        if (it != touching.end())
            for (const StepContact* c : it->second) {
                floorY = std::min(floorY, c->point.y);
                Object* other = fromBody(c->a == o.body ? c->b : c->a);
                if (other && other->meltForm == MeltForm::Puddle && other->material == o.material && other->heatInfo.melt > 0.3f) pool = other;
            }
        if (!pool)  // daneben gelandet: eine Pfütze gleichen Materials in Reichweite nimmt den Tropfen auf (statt zu überlappen)
            for (const auto& pp : objects_) {
                Object& p = *pp;
                if (&p == &o || p.meltForm != MeltForm::Puddle || p.material != o.material) continue;
                const glm::dvec2 d(p.pos.x - o.pos.x, p.pos.z - o.pos.z);
                if (glm::length(d) < p.primSize.x + 0.5 * (o.bbMax.x - o.bbMin.x) + 0.01 && std::abs(p.wMax.y - floorY) < 0.03) {
                    pool = &p;
                    break;
                }
            }
        if (pool) {  // in die Pfütze fließen: Volumen und Wärme zusammenlegen (makePuddle nimmt Volumen und Mittelwerte)
            const double m1 = pool->mass, m2 = o.mass;
            pool->heatInfo.tMean = (float)((m1 * pool->heatInfo.tMean + m2 * o.heatInfo.tMean) / std::max(m1 + m2, 1e-9));
            pool->heatInfo.melt = (float)((m1 * pool->heatInfo.melt + m2 * o.heatInfo.melt) / std::max(m1 + m2, 1e-9));
            pool->volume += o.volume;
            makePuddle(*pool, pool->wMin.y);
            pool->heatInfo.idle = false;
            gone.push_back(o.id);
            continue;
        }
        makePuddle(o, floorY);
    }
    for (uint32_t id : gone) remove(id);
    if (!cells) return;

    // ---- Abgebrannte und geschmolzene Stellen entfernen
    std::vector<uint32_t> ids;
    for (const auto& o : objects_)
        if (!o->heat.empty() && !o->heatInfo.idle) ids.push_back(o->id);
    size_t meltObjects = 0;
    for (const auto& o : objects_) meltObjects += o->meltForm != MeltForm::None;
    // Kohlestücke zählen (Teile aus einem Material, das als Rest übrig bleibt)
    std::vector<uint8_t> isResidue(materials().size(), 0);
    for (int i = 0; i < (int)materials().size(); ++i)
        if (residueOf(i) >= 0) isResidue[residueOf(i)] = 1;
    size_t residueCount = 0;
    for (const auto& o : objects_) residueCount += isResidue[std::clamp(o->material, 0, (int)isResidue.size() - 1)];
    // Reihum und mit Zeitbudget: viele Teile auf einmal ließen das Bild ruckeln, der Rest kommt im nächsten Wärmeschritt
    if (!ids.empty()) std::rotate(ids.begin(), ids.begin() + heatShapeCursor_ % ids.size(), ids.end());
    const auto budgetStart = std::chrono::steady_clock::now();
    for (uint32_t id : ids) {
        if (std::chrono::steady_clock::now() - budgetStart > std::chrono::microseconds(4000)) break;
        ++heatShapeCursor_;
        Object* o = find(id);
        if (!o || o->heat.empty()) continue;
        HeatGrid& g = o->heat;
        const bool fuelMat = combustible(o->material);
        const bool liquidForm = o->meltForm != MeltForm::None;
        // Unten in der Zelle: Nachbar in Richtung der Schwerkraft (lokal)
        const glm::dvec3 downL = glm::conjugate(o->rot) * glm::dvec3(0, -1, 0);
        int da = 0;
        for (int a = 1; a < 3; ++a)
            if (std::abs(downL[a]) > std::abs(downL[da])) da = a;
        glm::ivec3 down(0);
        down[da] = downL[da] > 0 ? 1 : -1;
        std::vector<int> burnt, molten;
        int inside = 0, liquid = 0;
        for (int i = 0; i < (int)g.cells.size(); ++i) {
            const HeatCell& c = g.cells[i];
            if (!c.inside) continue;
            ++inside;
            liquid += c.melt >= 0.9f;
            if (fuelMat && c.fuel <= 0.001f && !c.burning) {
                burnt.push_back(i);
            } else if (!liquidForm && c.melt >= 0.9f && c.area > 0) {
                // Flüssiges läuft nur ab, wenn darunter nichts mehr trägt (eigenes Material, Boden oder ein anderes Teil);
                // sonst bleibt es als See in der Mulde
                const glm::ivec3 q = cellCoord(g, i) + down;
                if (inGrid(g, q) && g.cells[g.index(q.x, q.y, q.z)].inside) continue;
                const glm::dvec3 p = o->pos + o->rot * cellCenter(g, i);
                const double below = 0.5 * std::abs(glm::dot(downL, g.cell)) + 0.01;
                RayHit h;
                if (p.y < below || (raycast(p, glm::dvec3(0, -1, 0), h, o->id) && h.t < below)) continue;
                molten.push_back(i);
            }
        }
        const int residue = fuelMat ? residueOf(o->material) : -1;
        // kleine verkohlte Reste: Holz wird ganz zu Kohle, Kohlekrümel zu Asche
        if (fuelMat && o->volume < 3e-6 && o->heatInfo.burning == 0 && o->heatInfo.chr > 0.8f) {
            if (residue >= 0 && residue != o->material) {
                setMaterial(*o, residue);
                for (HeatCell& c : o->heat.cells) c.chr = 1.0f, c.shape = 1.0f;
                continue;
            }
            if (o->volume < 5e-7) {
                remove(id);
                continue;
            }
        }
        // Kohle und kleine Holzstücke werden als Ganzes gleichmäßig kleiner: sie bleiben ein Stück, und Skalieren kostet
        // kaum Rechenzeit. Ein kleines Holzstück, das abgebrannt ist, wird ganz zu Kohle; Kohle zu Asche.
        if (fuelMat && o->kind == ObjKind::Solid && (residue < 0 || o->volume < 5e-5)) {
            double now = 0, target = 0;
            int n = 0;
            for (const HeatCell& c : g.cells)
                if (c.inside) {
                    now += c.shape;
                    target += burnLevel(c.fuel);
                    ++n;
                }
            if (n == 0) continue;
            now /= n;
            target /= n;
            if (target <= 0.02 || (int)burnt.size() == inside) {
                if (residue >= 0 && residue != o->material) {
                    setMaterial(*o, residue);
                    for (HeatCell& c : o->heat.cells) c.chr = 1.0f, c.shape = 1.0f;
                } else {
                    remove(id);
                }
                continue;
            }
            if (target < now - 0.1) {
                const glm::dvec3 v = o->body != kNoBody ? glm::dvec3(physics_.linearVelocity(o->body)) : glm::dvec3(0);
                const glm::dvec3 w = o->body != kNoBody ? glm::dvec3(physics_.angularVelocity(o->body)) : glm::dvec3(0);
                std::string err;
                const double f = std::cbrt(target / now);
                g.origin *= f;  // Gitter mitskalieren: jede Zelle behält ihre Werte
                g.cell *= f;
                PROFILE("shapes.scaleSmall");
                if (scalePart(id, glm::dvec3(f), &err)) {
                    for (HeatCell& c : o->heat.cells) c.shape = burnLevel(c.fuel);
                    if (o->body != kNoBody && !o->fixed) {
                        physics_.activate(o->body);
                        physics_.setVelocity(o->body, glm::vec3(v), glm::vec3(w));
                    }
                }
            }
            continue;
        }
        // brennende Stellen werden kleiner (in Vierteln)
        bool shrink = false;
        if (fuelMat)
            for (const HeatCell& c : g.cells)
                if (c.inside && c.fuel > 0.001f && burnLevel(c.fuel) < c.shape - 1e-3f) shrink = true;
        const bool allLiquid = !liquidForm && (int)burnt.size() + liquid >= inside && liquid > 0;
        if (burnt.empty() && molten.empty() && !allLiquid && !shrink) continue;
        const std::string name = o->name;
        const glm::dvec3 v0 = o->body != kNoBody ? glm::dvec3(physics_.linearVelocity(o->body)) : glm::dvec3(0);
        const glm::dvec3 w0 = o->body != kNoBody ? glm::dvec3(physics_.angularVelocity(o->body)) : glm::dvec3(0);
        const glm::dvec3 pos0 = o->pos;
        // Wo das Holz ganz abgebrannt ist, bleibt Kohle liegen: ein Stück so groß wie das, was zuletzt noch stand
        // (höchstens der Anteil residue_share der Zelle), mit der Temperatur der Glut
        int coal = 0;
        if (residue >= 0 && residue != o->material && !burnt.empty()) {
            std::vector<uint8_t> before(g.cells.size());
            for (int i = 0; i < (int)g.cells.size(); ++i) before[i] = g.cells[i].inside;
            const double share = materials()[o->material].residueShare;
            for (int i : burnt) {
                if (residueCount >= kMaxResidueObjects || coal >= 12 || share <= 0) break;
                const HeatCell& c = g.cells[i];
                glm::dvec3 mn, mx;
                cellBox(g, i, std::max(c.shape, 0.25f), before, mn, mx);
                glm::dvec3 size = mx - mn;
                const double frac = std::max(c.shape, 0.25f);
                if (share < frac) size *= std::cbrt(share / frac);
                size -= glm::dvec3(0.002);  // etwas Luft zu den Nachbarn
                if (glm::compMin(size) < 0.003 || size.x * size.y * size.z < 2e-7) continue;
                std::string base = materials()[residue].name;
                base[0] = (char)std::toupper((unsigned char)base[0]);
                PROFILE("shapes.coalSpawn");
                Object* ch = addBox(residue, size, o->pos + o->rot * (0.5 * (mn + mx)), nextName(base));
                if (!ch) continue;
                setTransform(*ch, ch->pos, o->rot);
                setTemperature(*ch, c.temp);
                for (HeatCell& k : ch->heat.cells) {
                    k.chr = 1.0f;
                    k.peak = std::max(k.peak, c.peak);
                }
                if (ch->body != kNoBody) {
                    physics_.activate(ch->body);
                    physics_.setVelocity(ch->body, glm::vec3(v0 + glm::cross(w0, ch->pos - pos0)), glm::vec3(0));
                }
                ++coal;
                ++residueCount;
            }
        }
        if ((int)burnt.size() == inside) {  // nichts mehr übrig
            events_.push_back(name + (coal ? " burned down, " + std::to_string(coal) + " piece(s) of " + materials()[residue].name + " remain"
                                           : std::string(" burned down to ashes")));
            remove(id);
            continue;
        }
        if (allLiquid) {  // ganz geschmolzen: als Tropfen weiter
            if (o->kind == ObjKind::Solid) {
                const double V = o->volume;
                const glm::dvec3 p = o->pos;
                o->solid = solid::makeSphere(std::cbrt(3.0 * V / (4.0 * kPi)), 2);
                glm::dvec3 com;
                solid::volume(o->solid, &com);
                solid::translate(o->solid, -com);
                o->hullCache.reset();
                o->prim = 2;
                o->primSize = glm::dvec3(std::cbrt(3.0 * V / (4.0 * kPi)), 0, 0);
                o->edits = 0;
                o->pos = p;
                o->meltForm = MeltForm::Drop;
                o->heatInfo.age = 0;
                o->fixed = false;  // Flüssiges hält keine Verankerung
                o->hingeAxis = -1;
                rebuildGeometry(*o);
                for (HeatCell& c : o->heat.cells) c.melt = 1.0f, c.temp = std::max(c.temp, thermal(o->material).meltC + 1.0f);
                rebuildBody(*o);
                if (o->body != kNoBody) physics_.activate(o->body);
                events_.push_back(name + " melted completely");
            }
            continue;
        }
        if (o->kind != ObjKind::Solid) continue;
        // Zellen als Quader abziehen; Randzellen reichen etwas über die Begrenzung hinaus
        std::vector<std::vector<solid::Plane>> tools;
        auto box = [&](int i) {
            const glm::ivec3 c = cellCoord(g, i);
            glm::dvec3 mn = g.origin + glm::dvec3(c) * g.cell, mx = mn + g.cell;
            for (int a = 0; a < 3; ++a) {
                if (c[a] == 0) mn[a] -= 0.002;
                if (c[a] == g.dim[a] - 1) mx[a] += 0.002;
            }
            tools.push_back(solid::boxTool(mn - 1e-6, mx + 1e-6));
        };
        for (int i : burnt) box(i);
        for (int i : molten) box(i);
        // Schmelze: unter der untersten geschmolzenen Stelle, mittlere Temperatur
        glm::dvec3 dropAt(0);
        double dropT = 0, lowest = 1e30;
        bool dropBurning = false;
        for (int i : molten) {
            const glm::dvec3 p = o->pos + o->rot * cellCenter(g, i);
            if (p.y < lowest) lowest = p.y, dropAt = p - glm::dvec3(0, 0.5 * std::abs(glm::dot(downL, g.cell)), 0);
            dropT += g.cells[i].temp;
            dropBurning |= g.cells[i].burning != 0;
        }
        if (!molten.empty()) dropT /= molten.size();
        const double vBefore = o->volume;
        const int mat = o->material;
        std::vector<uint32_t> parts;
        double removed = 0;
        std::string err;
        bool ok;
        if (!fuelMat && o->solid.pieces.size() + tools.size() <= 12) {
            // wenige geschmolzene Stellen: genau abziehen, die Form bleibt erhalten
            PROFILE("shapes.subtract");
            ok = removeMaterial(id, tools, &removed, &parts, &err);
        } else {
            // Die Form aus den übrigen Zellen neu bauen (jeder einzelne Schnitt würde das Teil in immer mehr Stücke zerlegen
            // und die Kollision teuer machen): volle Zellen zu möglichst großen Quadern zusammengefasst (erst entlang x, dann
            // über y und z), angebrannte Zellen einzeln und auf ihre Schrumpfstufe verkleinert. Solange das Teil aus wenigen
            // Stücken besteht, wird jeder Quader mit der bisherigen Form geschnitten: runde Scheite bleiben rund.
            std::vector<uint8_t> keep(g.cells.size(), 0), used(g.cells.size(), 0);
            for (int i = 0; i < (int)g.cells.size(); ++i) keep[i] = g.cells[i].inside;
            for (int i : burnt) keep[i] = 0;
            for (int i : molten) keep[i] = 0;
            PROFILE("shapes.buildBoxes");
            std::vector<float> level(g.cells.size(), 1.0f);
            if (fuelMat)
                for (int i = 0; i < (int)g.cells.size(); ++i)
                    if (keep[i]) level[i] = std::min(g.cells[i].shape, std::max(burnLevel(g.cells[i].fuel), 0.25f));
            const bool clip = o->solid.pieces.size() <= 24;
            solid::Solid s;
            auto addPiece = [&](const glm::dvec3& mn, const glm::dvec3& mx) {
                if (clip) {
                    solid::Solid part = solid::intersect(o->solid, solid::boxTool(mn, mx));
                    for (auto& p : part.pieces) s.pieces.push_back(std::move(p));
                    return;
                }
                solid::Piece p = solid::makeBox(0.5 * (mx - mn));
                solid::transform(p, glm::translate(glm::dmat4(1.0), 0.5 * (mn + mx)));
                s.pieces.push_back(std::move(p));
            };
            for (int i = 0; i < (int)g.cells.size(); ++i)
                if (keep[i] && level[i] < 0.999f) {
                    used[i] = 1;
                    glm::dvec3 mn, mx;
                    cellBox(g, i, level[i], keep, mn, mx);
                    addPiece(mn, mx);
                }
            for (int z = 0; z < g.dim.z; ++z)
                for (int y = 0; y < g.dim.y; ++y)
                    for (int x = 0; x < g.dim.x; ++x) {
                        if (!keep[g.index(x, y, z)] || used[g.index(x, y, z)]) continue;
                        auto free = [&](int a, int b, int c) { return keep[g.index(a, b, c)] && !used[g.index(a, b, c)]; };
                        int x1 = x;
                        while (x1 + 1 < g.dim.x && free(x1 + 1, y, z)) ++x1;
                        int y1 = y;
                        for (bool grow = true; grow && y1 + 1 < g.dim.y;) {
                            for (int a = x; a <= x1 && grow; ++a) grow = free(a, y1 + 1, z);
                            if (grow) ++y1;
                        }
                        int z1 = z;
                        for (bool grow = true; grow && z1 + 1 < g.dim.z;) {
                            for (int b = y; b <= y1 && grow; ++b)
                                for (int a = x; a <= x1 && grow; ++a) grow = free(a, b, z1 + 1);
                            if (grow) ++z1;
                        }
                        for (int c = z; c <= z1; ++c)
                            for (int b = y; b <= y1; ++b)
                                for (int a = x; a <= x1; ++a) used[g.index(a, b, c)] = 1;
                        addPiece(g.origin + glm::dvec3(x, y, z) * g.cell, g.origin + glm::dvec3(x1 + 1, y1 + 1, z1 + 1) * g.cell);
                    }
            removed = std::max(0.0, vBefore - solid::volume(s));
            for (int i = 0; i < (int)g.cells.size(); ++i)
                if (keep[i]) g.cells[i].shape = level[i];
            o->heatInfo.keepLayout = true;  // die neue Form passt zu diesem Gitter
            if (fuelMat && s.pieces.size() > 1) {
                // Brennt ein Teil durch, fallen oft viele Krümel ab. Die kleinen sind Asche; höchstens 8 Bruchstücke bleiben
                // (jedes neue Teil kostet Grafik, Physik und ein Wärmegitter, viele auf einmal ließen das Bild ruckeln)
                std::vector<solid::Solid> comps = solid::connectedComponents(s);
                if (comps.size() > 1) {
                    std::vector<std::pair<double, size_t>> vol;
                    double total = 0;
                    for (size_t c = 0; c < comps.size(); ++c) {
                        vol.push_back({solid::volume(comps[c]), c});
                        total += vol.back().first;
                    }
                    std::sort(vol.rbegin(), vol.rend());
                    solid::Solid kept;
                    for (size_t r = 0; r < vol.size(); ++r)
                        if (r == 0 || (r < 8 && vol[r].first > std::max(1e-6, 0.02 * total)))
                            for (auto& p : comps[vol[r].second].pieces) kept.pieces.push_back(std::move(p));
                    s = std::move(kept);
                }
            }
            {
                PROFILE("shapes.rebuildFromCells");
                ok = !s.pieces.empty() && applyResult(*o, std::move(s), &parts, &err);
            }
            if (Object* p = find(id)) p->heatInfo.keepLayout = false;
            if (!ok && err.empty()) err = "nothing would be left";
        }
        if (!ok) {
            if (removed > 0 || err.find("nothing would be left") != std::string::npos) remove(id);
            continue;
        }
        for (uint32_t pid : parts)
            if (Object* p = find(pid)) {
                if (p->volume < 5e-8) {  // Krümel unter 0,05 cm³: Asche bzw. verdampft
                    remove(pid);
                    continue;
                }
                if (p->fixed || p->body == kNoBody) continue;
                physics_.activate(p->body);
                physics_.setVelocity(p->body, glm::vec3(v0 + glm::cross(w0, p->pos - pos0)), glm::vec3(w0));
            }
        if (!burnt.empty() && parts.size() > 1) events_.push_back(name + " burned through (" + std::to_string(parts.size()) + " pieces)");
        // Geschmolzenes tropft ab
        const double share = molten.empty() ? 0.0 : double(molten.size()) / double(burnt.size() + molten.size());
        const double V = removed * share;
        if (V > 1e-8 && meltObjects < kMaxMeltObjects) {
            const double d = std::cbrt(6.0 * V / kPi);
            if (Object* drop = addSphere(mat, d, dropAt - glm::dvec3(0, 0.5 * d + 0.002, 0), nextName("Drop"))) {
                drop->meltForm = MeltForm::Drop;
                drop->heatInfo.age = 0;
                setTemperature(*drop, std::max(dropT, (double)thermal(mat).meltC + 1.0));
                if (dropBurning && combustible(mat))
                    for (HeatCell& c : drop->heat.cells) c.burning = 1;
                if (drop->body != kNoBody) {
                    physics_.activate(drop->body);
                    physics_.setVelocity(drop->body, glm::vec3(v0 + glm::cross(w0, dropAt - pos0)), glm::vec3(0));
                }
                ++meltObjects;
            }
        }
    }
}

// ============================================================================ Beschreibung

std::string World::heatText(Object& o) {
    std::ostringstream ss;
    const Thermal& th = thermal(o.material);
    const Material& mat = materials()[o.material];
    const Object::HeatInfo& hi = o.heatInfo;
    const bool has = !o.heat.empty();
    const double tMean = has ? hi.tMean : ambient_, tMin = has ? hi.tMin : ambient_, tMax = has ? hi.tMax : ambient_;
    ss << "temperature   mean " << fmtNum(tMean, 0) << " °C, min " << fmtNum(tMin, 0) << " °C, max " << fmtNum(tMax, 0)
       << " °C  (air " << fmtNum(ambient_, 0) << " °C" << (heatOn_ ? "" : ", heat off") << ")\n";
    std::string state;
    auto add = [&](const std::string& s) { state += (state.empty() ? "" : ", ") + s; };
    if (o.meltForm == MeltForm::Drop) add("drop");
    if (o.meltForm == MeltForm::Puddle) add(hi.melt > 0.5f ? "liquid puddle" : "solidified puddle");
    if (has && hi.melt > 0.001f) add("molten " + fmtNum(hi.melt * 100, 0) + " %");
    if (has && hi.burning > 0) {
        double p = 0;
        for (const Fire& F : fires_)
            for (const Fire::Spot& s : F.spots) {
                const glm::dvec3 l = glm::conjugate(o.rot) * (s.pos - o.pos);
                if (glm::all(glm::greaterThanEqual(l, o.bbMin - 0.05)) && glm::all(glm::lessThanEqual(l, o.bbMax + 0.05))) p += s.power;
            }
        add("burning at " + std::to_string(hi.burning) + " spots (" + fmtNum(p / 1000.0, 1) + " kW)");
    }
    if (has && hi.chr > 0.01f) add("charred " + fmtNum(hi.chr * 100, 0) + " %");
    if (has && combustible(o.material)) {
        double fuel = 0;
        int n = 0;
        for (const HeatCell& c : o.heat.cells)
            if (c.inside) fuel += c.fuel, ++n;
        if (n && fuel / n < 0.999) add(fmtNum(100.0 - fuel / n * 100.0, 0) + " % burned away");
    }
    const double sf = strengthFactor(o.material, tMax);
    if (sf < 0.95 && !combustible(o.material)) add("softened: " + fmtNum(sf * 100, 0) + " % strength at the hottest spot");
    const double br = brittleness(o.material, tMin);
    if (br > 0.05) add("brittle from cold (" + fmtNum(br * 100, 0) + " %): breaks at " + fmtNum(fractureEnergyAt(o.material, tMin) / mat.fractureEnergy * 100, 1) +
                       " % of the impact energy");
    ss << "state         " << (state.empty() ? "normal" : state) << "\n";
    ss << "material      λ " << fmtNum(th.conductivity, 2) << " W/mK, c " << fmtNum(th.specificHeat, 0) << " J/kgK, ε " << fmtNum(th.emissivity, 2);
    if (th.meltC > 0) ss << ", melts at " << fmtNum(th.meltC, 0) << " °C";
    ss << ", soft from " << fmtNum(th.softenC, 0) << " °C (half strength at " << fmtNum(th.halfC, 0) << " °C)";
    if (th.brittleC > -400) ss << ", brittle below " << fmtNum(th.brittleC, 0) << " °C";
    if (combustible(o.material)) ss << ", chars from " << fmtNum(th.pyrolysisC, 0) << " °C, ignites at " << fmtNum(th.ignitionC, 0) << " °C";
    ss << "\n";
    return ss.str();
}
