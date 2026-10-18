// Stift in Bohrung: Kräfte an der Lochwand, Pressung, Abrieb (Archard), Ausreißen, Schmieren, Passung.
// Die Kontakte selbst rechnet die Physik mit der exakt runden Bohrungswand (BoreShape); hier wird ausgewertet.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

#include "Materials.h"
#include "World.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

const char* axisName(const glm::dvec3& a) {
    if (std::abs(a.x) > 0.999) return "x";
    if (std::abs(a.y) > 0.999) return "y";
    if (std::abs(a.z) > 0.999) return "z";
    return "slanted";
}

// Passung nach dem Spiel (Durchmesser) im Verhältnis zum Durchmesser, grob nach ISO 286
const char* fitName(double clearance, double d) {
    if (clearance < -1e-7) return "interference: too big, does not go in";
    if (clearance < 0.0005 * d) return "push fit, tight without play";
    if (clearance < 0.002 * d) return "sliding fit";
    if (clearance < 0.006 * d) return "running fit";
    return "loose fit, noticeable play";
}

// Stift eines Teils, der in einer Bohrung (Weltachse, Mitte) steckt: Radius oder 0
double pinRadiusIn(const Object& p, const glm::dvec3& center, const glm::dvec3& axis, double boreR) {
    auto test = [&](const glm::dvec3& c, const glm::dvec3& a, double r) {
        const glm::dvec3 wc = p.pos + p.rot * c, wa = glm::normalize(p.rot * a);
        if (std::abs(glm::dot(wa, axis)) < 0.98) return 0.0;
        glm::dvec3 off = wc - center;
        off -= axis * glm::dot(off, axis);
        return glm::length(off) < boreR ? r : 0.0;
    };
    double best = 0;
    for (const CylGeom& c : p.coll.cylinders) best = std::max(best, test(c.center, c.axis, c.radius));
    if (p.prim == 3 && p.edits == 0) best = std::max(best, test(glm::dvec3(0), glm::dvec3(0, 1, 0), p.primSize.x));
    return best;
}

} // namespace

void World::setLubricant(Object& o, int lubricant) {
    o.lubricant = std::clamp(lubricant, 0, (int)lubricants().size() - 1);
    physics_.setLubricant(o.body, o.lubricant);
}

double World::boreCapacity(const Object& o, size_t i, const glm::dvec3& dirLocal) const {
    if (i >= o.coll.bores.size()) return 1e30;
    const BoreGeom& b = o.coll.bores[i];
    const double d = 2.0 * b.radius, L = 2.0 * b.halfLength;
    const double len = glm::length(dirLocal);
    if (len < 1e-12) return 1e30;
    const glm::dvec3 dir = dirLocal / len;
    // Holz: Faser entlang der lokalen x-Achse
    const double cosGrain = std::abs(dir.x);
    const double fEmb = embedmentMPa(o.material, d * 1000.0, cosGrain) * 1e6 * d * L;
    // Materialstärke bis zum Rand in Lastrichtung (zwischen den 8 gemessenen Richtungen)
    double ang = std::atan2(glm::dot(dir, b.w), glm::dot(dir, b.u));
    if (ang < 0) ang += 2.0 * kPi;
    const double s = ang / (kPi / 4.0);
    const int k0 = int(std::floor(s)) % 8, k1 = (k0 + 1) % 8;
    const double f = s - std::floor(s);
    const double e = b.edge[k0] * (1.0 - f) + b.edge[k1] * f;
    double tau = shearMPa(o.material);
    if (materials()[o.material].shading == Shading::Wood) tau *= cosGrain * cosGrain + 0.25 * (1.0 - cosGrain * cosGrain);  // quer zur Faser spaltet Holz leichter
    const double fTear = 2.0 * e * L * tau * 1e6;
    return std::max(1.0, std::min(fEmb, fTear));
}

void World::processBores(float h) {
    std::unordered_map<BodyHandle, Object*> bored;
    for (auto& op : objects_)
        if (op->body != kNoBody && !op->coll.bores.empty()) {
            bored[op->body] = op.get();
            if (!physics_.isActive(op->body)) continue;  // schläft: letzte Werte bleiben stehen
            for (auto& rt : op->boreRt) {
                rt.force = rt.forceTop = rt.forceBottom = glm::dvec3(0);
                rt.normal = 0;
                rt.slip = 0;
                rt.partner = 0;
            }
        }
    if (bored.empty()) return;
    const auto& lubs = lubricants();
    for (const ContactImpulse& c : physics_.stepImpulses()) {
        for (int side = 0; side < 2; ++side) {
            auto it = bored.find(side == 0 ? c.a : c.b);
            if (it == bored.end()) continue;
            Object& o = *it->second;
            const glm::dquat inv = glm::conjugate(o.rot);
            const glm::dvec3 local = inv * (c.point - o.pos), nl = inv * glm::dvec3(c.normal);
            for (size_t i = 0; i < o.coll.bores.size() && i < o.boreRt.size(); ++i) {
                const BoreGeom& b = o.coll.bores[i];
                const glm::dvec3 rel = local - b.center;
                const double t = glm::dot(rel, b.axis), r = glm::length(rel - b.axis * t);
                if (std::abs(t) > b.halfLength + 0.001 || r < b.radius - 0.001 || r > b.radius + b.band + 0.001) continue;
                if (std::abs(glm::dot(nl, b.axis)) > 0.5) continue;  // Stirnkontakt, nicht die Lochwand
                const glm::dvec3 J = glm::dvec3(c.normal) * double(c.normalImpulse) + glm::dvec3(c.frictionImpulse);
                Object::BoreRuntime& rt = o.boreRt[i];
                const glm::dvec3 F = (side == 1 ? J : -J) / double(h);  // Kraft auf dieses Teil
                rt.force += F;
                (t >= 0 ? rt.forceTop : rt.forceBottom) += F;
                const double N = std::max(0.0, double(c.normalImpulse)) / h;
                rt.normal += N;
                rt.slip = std::max(rt.slip, double(c.slip));
                Object* other = fromBody(side == 0 ? c.b : c.a);
                if (other) rt.partner = other->id;
                // Abrieb nach Archard: Volumen = k · Normalkraft · Gleitweg / Härte (beide Seiten, weicheres mehr)
                if (c.slip > 1e-6 && N > 0) {
                    const int om = other ? other->material : o.material;
                    double k = wearCoefficient(o.material, om);
                    double lf = lubs[std::clamp(o.lubricant, 0, (int)lubs.size() - 1)].wear;
                    if (other) lf = std::min(lf, (double)lubs[std::clamp(other->lubricant, 0, (int)lubs.size() - 1)].wear);
                    k *= lf;
                    const double slide = double(c.slip) * h;
                    const double vBore = k * N * slide / (hardnessMPa(o.material) * 1e6);
                    o.wornVolume += vBore;
                    // die belastete Seite trägt ab: Fläche ≈ Durchmesser · Länge
                    if (i < o.boreWear.size()) o.boreWear[i] += vBore / (2.0 * b.radius * 2.0 * b.halfLength);
                    if (other) other->wornVolume += k * N * slide / (hardnessMPa(other->material) * 1e6);
                }
                break;
            }
        }
    }
    struct Tear {
        uint32_t id;
        size_t bore;
        glm::dvec3 dir;
        double force, capacity;
    };
    std::vector<Tear> tears;
    for (auto& [body, op] : bored) {
        Object& o = *op;
        if (!physics_.isActive(body)) continue;
        // Liegt etwas an einer Bohrungswand an? Dann rechnet die Physik diese Gruppe genauer (mehr Durchläufe)
        bool used = false;
        for (const auto& rt : o.boreRt) used |= rt.partner != 0;
        if (used != o.boreInUse) {
            physics_.setSolverBoost(body, used);
            o.boreInUse = used;
        }
        for (size_t i = 0; i < o.coll.bores.size() && i < o.boreRt.size(); ++i) {
            const BoreGeom& b = o.coll.bores[i];
            Object::BoreRuntime& rt = o.boreRt[i];
            rt.pressure = rt.normal / (2.0 * b.radius * 2.0 * b.halfLength) / 1e6;
            // Abrieb in der Physik nachführen (Wand weicht zurück)
            const double wear = i < o.boreWear.size() ? std::min(o.boreWear[i], 0.8 * b.band) : 0.0;
            if (std::abs(wear - rt.applied) > 2e-6) {
                physics_.setBoreRadius(o.body, int(i), b.radius + wear);
                rt.applied = wear;
            }
            // Last quer zur Achse -> Ausreißen. Jede Hälfte trägt mit ihrer halben Länge; bei einem Hebel drücken
            // die Enden entgegengesetzt, die Summe wäre fast null.
            const glm::dvec3 axisW = o.rot * b.axis;
            const glm::dvec3 ft = rt.forceTop - axisW * glm::dot(rt.forceTop, axisW);
            const glm::dvec3 fb = rt.forceBottom - axisW * glm::dot(rt.forceBottom, axisW);
            const glm::dvec3 fr = glm::length(ft) >= glm::length(fb) ? ft : fb;
            const double F = 2.0 * glm::length(fr);
            rt.load_N = F;
            if (F < 1e-6) {
                rt.load = 0;
                rt.loadPrev *= 1.0 - std::min(1.0, double(h) / 0.015);
                continue;
            }
            const glm::dvec3 dirLocal = glm::conjugate(o.rot) * glm::normalize(fr);
            rt.capacity = boreCapacity(o, i, dirLocal);
            rt.load = F / rt.capacity;
            // wie bei Verbindungen: Überlast muss etwa 15 ms anhalten (loadPrev = geglättete Auslastung)
            rt.loadPrev += (rt.load - rt.loadPrev) * std::min(1.0, double(h) / 0.015);
            if (breakage_ && !o.unbreakable && simTime_ >= o.breakCooldown && rt.loadPrev > 1.0)
                tears.push_back({o.id, i, dirLocal, F, rt.capacity});
        }
    }
    for (const Tear& t : tears) tearOut(t.id, t.bore, t.dir, t.force, t.capacity);
}

void World::tearOut(uint32_t id, size_t bore, const glm::dvec3& dirLocal, double force, double capacity) {
    Object* o = find(id);
    if (!o || bore >= o->coll.bores.size() || o->kind != ObjKind::Solid) return;
    const BoreGeom b = o->coll.bores[bore];
    const std::string name = o->name;
    const Material& mat = materials()[o->material];
    glm::dvec3 u = dirLocal - b.axis * glm::dot(dirLocal, b.axis);
    if (glm::length(u) < 1e-9) return;
    u = glm::normalize(u);
    const glm::dvec3 side = glm::cross(b.axis, u);
    // Reißt die Wand zum Rand hin aus (Scherung) oder wird das Loch nur zum Langloch gedrückt (Lochleibung)?
    const double d = 2.0 * b.radius, L = 2.0 * b.halfLength;
    const double fEmb = embedmentMPa(o->material, d * 1000.0, std::abs(u.x)) * 1e6 * d * L;
    const bool toEdge = capacity < fEmb * 0.999;
    const glm::dvec3 ext = o->bbMax - o->bbMin;
    const double reach = toEdge ? glm::length(ext) + 0.01 : b.radius * 1.5;
    auto plane = [](const glm::dvec3& n, double dd) {
        solid::Plane p;
        p.n = n;
        p.d = dd;
        return p;
    };
    std::vector<solid::Plane> slot = {
        plane(side, glm::dot(side, b.center) + b.radius),
        plane(-side, -glm::dot(side, b.center) + b.radius),
        plane(b.axis, glm::dot(b.axis, b.center) + b.halfLength + 1e-4),
        plane(-b.axis, -glm::dot(b.axis, b.center) + b.halfLength + 1e-4),
        plane(u, glm::dot(u, b.center) + reach),
        plane(-u, -glm::dot(u, b.center)),
    };
    const glm::dvec3 v = glm::dvec3(physics_.linearVelocity(o->body)), w = glm::dvec3(physics_.angularVelocity(o->body));
    const glm::dvec3 pos0 = o->pos;
    std::vector<uint32_t> ids;
    std::string err;
    if (!removeMaterial(id, {slot}, nullptr, &ids, &err)) return;
    for (uint32_t pid : ids)
        if (Object* p = find(pid)) {
            physics_.activate(p->body);
            physics_.setVelocity(p->body, glm::vec3(v + glm::cross(w, p->pos - pos0)), glm::vec3(w));
            p->breakCooldown = simTime_ + 0.2;
        }
    ++brokenCount_;
    events_.push_back(name + ": hole D" + fmtNum(d * 1000.0, 1) + " mm " + (toEdge ? "torn out to the edge" : "pressed into a slot") +
                      " (bearing load " + fmtNum(force, 0) + " N, holds " + fmtNum(capacity, 0) + " N, " + mat.name + ")");
}

std::string World::boresText(Object& o) {
    std::ostringstream out;
    out << "#" << o.id << "  " << o.name << "  [" << materials()[o.material].name << "]\n";
    const auto& lubs = lubricants();
    if (o.coll.bores.empty()) out << "  no round bores (exact bore walls need a full round hole of at least D 2 mm)\n";
    for (size_t i = 0; i < o.coll.bores.size(); ++i) {
        const BoreGeom& b = o.coll.bores[i];
        double emin = 1e30, emax = 0;
        for (double e : b.edge) {
            emin = std::min(emin, e);
            emax = std::max(emax, e);
        }
        out << "  b" << i << "  D " << fmtNum(2000.0 * b.radius, 2) << " mm x " << fmtNum(2000.0 * b.halfLength, 1) << " mm, axis "
            << axisName(b.axis) << ", " << (b.openTop && b.openBottom ? "through" : "blind") << ", wall to the edge "
            << fmtNum(emin * 1000.0, 1) << "-" << fmtNum(emax * 1000.0, 1) << " mm\n";
        const Object::BoreRuntime rt = i < o.boreRt.size() ? o.boreRt[i] : Object::BoreRuntime();
        if (rt.partner)
            if (Object* p = find(rt.partner)) {
                const glm::dvec3 axisW = glm::normalize(o.rot * b.axis), cW = o.pos + o.rot * b.center;
                const double pr = pinRadiusIn(*p, cW, axisW, b.radius + b.band);
                out << "      in it: #" << p->id << " " << p->name;
                if (pr > 0) {
                    const double clearance = 2.0 * (b.radius - pr);
                    out << ", pin D " << fmtNum(2000.0 * pr, 2) << " mm, play " << fmtNum(clearance * 1000.0, 2) << " mm ("
                        << fitName(clearance, 2.0 * b.radius) << ")";
                }
                out << "\n";
            }
        const glm::dvec3 axisW = o.rot * b.axis;
        const double F = glm::length(rt.force - axisW * glm::dot(rt.force, axisW));
        out << "      load " << fmtNum(rt.load_N, 1) << " N";
        if (rt.capacity > 0 && rt.load_N > 0) out << " (" << fmtNum(100.0 * rt.load, 1) << " % of " << fmtNum(rt.capacity, 0) << " N until it tears out)";
        if (rt.load_N > 2.0 * F + 1.0) out << ", ends pressed in opposite directions (lever), net " << fmtNum(F, 1) << " N";
        out << ", pressure " << fmtNum(rt.pressure, 3) << " MPa, sliding " << fmtNum(rt.slip, 3) << " m/s\n";
        const double wear = i < o.boreWear.size() ? o.boreWear[i] : 0.0;
        out << "      worn " << fmtNum(wear * 1000.0, 6) << " mm\n";
    }
    for (size_t i = 0; i < o.coll.cylinders.size(); ++i) {
        const CylGeom& c = o.coll.cylinders[i];
        out << "  p" << i << "  exact pin D " << fmtNum(2000.0 * c.radius, 2) << " mm x " << fmtNum(2000.0 * c.halfLength, 1) << " mm, axis "
            << axisName(c.axis) << "\n";
    }
    out << "  lubricant: " << lubs[std::clamp(o.lubricant, 0, (int)lubs.size() - 1)].name
        << (o.lubricant ? "" : " (dry)") << ", worn in total " << fmtNum(o.wornVolume * 1e9, 4) << " mm3";
    return out.str();
}

