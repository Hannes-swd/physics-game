// Verbindungen zwischen Teilen: Kleber, Nägel, Schrauben, Schweißnaht, Scharnier, Feder, Seil.
// Jede Verbindung hat eine Tragfähigkeit (Zug, Scherung, Moment bzw. Kraft) und bricht bei Überlast.
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

#include "Commands.h"
#include "Materials.h"
#include "World.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

struct GluePreset {
    const char* key;
    const char* name;
    double ft, fs, gapMm;  // Zug- und Scherfestigkeit (MPa), größter Spalt
};
// Typische Festigkeiten (Datenblätter, Bruch in der Klebefuge bei optimaler Verarbeitung)
const GluePreset kGlues[] = {
    {"woodglue", "wood glue (PVA, D3)", 6.0, 10.0, 0.3},
    {"epoxy", "epoxy (2-part)", 25.0, 20.0, 3.0},
    {"superglue", "superglue (cyanoacrylate)", 15.0, 15.0, 0.1},
    {"pu", "PU glue", 8.0, 9.0, 1.0},
    {"hotglue", "hot glue", 2.0, 2.5, 2.0},
    {"construction", "construction adhesive", 1.5, 2.0, 5.0},
};

// Namen aus alten Szenen
const char* legacyGlue(const std::string& k) {
    static const char* map[][2] = {{"holzleim", "woodglue"},    {"epoxid", "epoxy"},          {"sekundenkleber", "superglue"},
                                   {"heisskleber", "hotglue"}, {"montagekleber", "construction"}};
    for (auto& m : map)
        if (k == m[0]) return m[1];
    return nullptr;
}

const GluePreset* findGlue(const std::string& key) {
    std::string k = normalizeWord(key);
    if (const char* l = legacyGlue(k)) k = l;
    for (const auto& g : kGlues)
        if (k == g.key) return &g;
    if (k.size() >= 3)
        for (const auto& g : kGlues)
            if (std::string(g.key).rfind(k, 0) == 0) return &g;
    return nullptr;
}

bool isWood(const Material& m) { return m.cls == MaterialClass::Wood; }
bool isMetal(const Material& m) { return m.cls == MaterialClass::Metal; }
bool isMasonry(const Material& m) { return m.cls == MaterialClass::Stone; }

glm::dmat4 frameOf(const Object& o) { return glm::translate(glm::dmat4(1.0), o.pos) * glm::mat4_cast(o.rot); }

double thicknessAlong(const Object& o, const glm::dvec3& localDir) {
    double lo = 1e30, hi = -1e30;
    for (const auto& p : o.solid.pieces)
        for (const auto& f : p.faces)
            for (const auto& v : f.v) {
                double s = glm::dot(v, localDir);
                lo = std::min(lo, s);
                hi = std::max(hi, s);
            }
    return hi > lo ? hi - lo : 0.0;
}

std::string glueList() {
    std::string s;
    for (const auto& g : kGlues) s += std::string(s.empty() ? "" : ", ") + g.key;
    return s;
}

} // namespace

std::string gluePresetKey(const std::string& name) {
    const GluePreset* g = findGlue(name);
    return g ? g->key : name;
}

const char* World::jointTypeName(JointType t) {
    switch (t) {
    case JointType::Glue: return "glue";
    case JointType::Nail: return "nails";
    case JointType::Screw: return "screws";
    case JointType::Weld: return "weld";
    case JointType::Hinge: return "hinge";
    case JointType::Spring: return "spring";
    case JointType::Rope: return "rope";
    }
    return "joint";
}

const char* World::jointName(const JointState& j) {
    return j.type == JointType::Glue && j.preset == kStuckPreset ? "stuck" : jointTypeName(j.type);
}

bool World::rayToObject(const Object& o, const glm::dvec3& origin, const glm::dvec3& dir, double& t) const {
    glm::dquat inv = glm::conjugate(o.rot);
    glm::dvec3 lo = inv * (origin - o.pos), ld = inv * dir, n;
    if (o.kind == ObjKind::Solid) return solid::rayHit(o.solid, lo, ld, t, n);
    return false;
}

// ============================================================================ Anlegen

Joint* World::addJoint(JointState s, std::string* err) {
    Object* A = find(s.a);
    Object* B = find(s.b);
    if (!A || !B || s.a == s.b) {
        if (err) *err = "give two different, existing parts";
        return nullptr;
    }
    auto j = std::make_unique<Joint>();
    static_cast<JointState&>(*j) = s;
    if (!j->id) j->id = nextJointId_++;
    nextJointId_ = std::max(nextJointId_, j->id + 1);
    computeCapacity(*j);
    buildConstraint(*j);
    joints_.push_back(std::move(j));
    return joints_.back().get();
}

// Berührfläche bestimmen und Anker setzen (Kleber, Nagel, Schraube, Schweißnaht)
static bool contactJoint(World& w, JointState& s, double gap, std::string* err) {
    Object* A = w.find(s.a);
    Object* B = w.find(s.b);
    if (!A || !B || s.a == s.b) {
        if (err) *err = "give two different, existing parts";
        return false;
    }
    if (A->kind != ObjKind::Solid || B->kind != ObjKind::Solid) {
        if (err) *err = "imported models cannot be joined (yet)";
        return false;
    }
    glm::dvec3 c, n;
    double area = solid::contactArea(A->solid, frameOf(*A), B->solid, frameOf(*B), gap, &c, &n);
    if (area < 1e-7) {
        if (err) *err = "the parts do not touch (gap larger than " + fmtNum(gap * 1000, 1) + " mm). Move them together first (drop) or allow a larger gap.";
        return false;
    }
    glm::dquat ia = glm::conjugate(A->rot), ib = glm::conjugate(B->rot);
    s.area = area;
    s.anchorA = ia * (c - A->pos);
    s.anchorB = ib * (c - B->pos);
    s.normalA = glm::normalize(ia * n);
    s.thickA = thicknessAlong(*A, s.normalA);
    return true;
}

Joint* World::glue(uint32_t a, uint32_t b, const std::string& preset, double ftMPa, double gapMm, std::string* err) {
    const GluePreset* g = findGlue(preset.empty() ? "woodglue" : preset);
    if (!g) {
        if (err) *err = "unknown glue '" + preset + "' (" + glueList() + ")";
        return nullptr;
    }
    JointState s;
    s.type = JointType::Glue;
    s.a = a;
    s.b = b;
    s.preset = g->key;
    s.gapMm = gapMm > 0 ? gapMm : g->gapMm;
    s.ft = ftMPa > 0 ? ftMPa : g->ft;
    s.fs = ftMPa > 0 ? ftMPa * g->fs / g->ft : g->fs;
    if (!contactJoint(*this, s, s.gapMm / 1000.0, err)) return nullptr;
    return addJoint(s, err);
}

Joint* World::nail(uint32_t a, uint32_t b, bool screw, int count, double lengthMm, double diameterMm, std::string* err) {
    JointState s;
    s.type = screw ? JointType::Screw : JointType::Nail;
    s.a = a;
    s.b = b;
    if (!contactJoint(*this, s, 0.002, err)) return nullptr;
    const Material& ma = materials()[find(a)->material];
    const Material& mb = materials()[find(b)->material];
    if (!screw && !isWood(mb)) {
        if (err) *err = std::string("nails only hold in wood (the second part is ") + mb.name + "). Use 'screw' (with wall plug) or 'glue'.";
        return nullptr;
    }
    if (!screw && !isWood(ma)) {
        if (err) *err = std::string("you cannot nail through ") + ma.name + ". Use 'screw' or 'glue'.";
        return nullptr;
    }
    const double t1 = s.thickA * 1000.0;
    s.lengthMm = lengthMm > 0 ? lengthMm : std::clamp(std::round(t1 * (screw ? 2.2 : 2.5)), 20.0, 300.0);
    s.diameterMm = diameterMm > 0 ? diameterMm : std::clamp(s.lengthMm / (screw ? 12.0 : 25.0), screw ? 3.0 : 1.4, screw ? 12.0 : 7.0);
    s.count = count > 0 ? count : std::clamp((int)std::round(s.area * 1e4 / 75.0), 1, 8);
    if (s.lengthMm < t1 + 4 * s.diameterMm) {
        if (err) *err = "too short: " + fmtNum(s.lengthMm, 0) + " mm through " + fmtNum(t1, 0) + " mm thickness. It must reach at least " +
                        fmtNum(4 * s.diameterMm, 0) + " mm (4 x diameter) into the second part.";
        return nullptr;
    }
    return addJoint(s, err);
}

Joint* World::weld(uint32_t a, uint32_t b, double seamMm, std::string* err) {
    Object* A = find(a);
    Object* B = find(b);
    if (A && B && (!isMetal(materials()[A->material]) || !isMetal(materials()[B->material]))) {
        if (err) *err = "welding only works between metal parts";
        return nullptr;
    }
    JointState s;
    s.type = JointType::Weld;
    s.a = a;
    s.b = b;
    s.seamMm = seamMm > 0 ? seamMm : 3.0;
    if (!contactJoint(*this, s, 0.002, err)) return nullptr;
    return addJoint(s, err);
}

Joint* World::hinge(uint32_t a, uint32_t b, int axis, float rpm, float torque, std::string* err) {
    Object* A = find(a);
    Object* B = find(b);
    if (!A || !B || a == b || axis < 0 || axis > 2) {
        if (err) *err = "hinge <holder> <rotating_part> <x|y|z> [rpm] [Nm]";
        return nullptr;
    }
    JointState s;
    s.type = JointType::Hinge;
    s.a = a;
    s.b = b;
    s.anchorA = glm::conjugate(A->rot) * (B->pos - A->pos);  // Achse durch den Schwerpunkt des Drehteils
    s.anchorB = glm::dvec3(0);
    s.axisB = glm::dvec3(0);
    s.axisB[axis] = 1.0;
    s.motorRpm = rpm;
    s.motorTorque = torque > 0 ? torque : 5.0f;
    s.maxForce = 3000;
    return addJoint(s, err);
}

// Ankerpunkte auf den einander zugewandten Oberflächen
static void facingAnchors(const World& w, const Object& A, const Object& B, glm::dvec3& pa, glm::dvec3& pb) {
    glm::dvec3 d = B.pos - A.pos;
    double len = glm::length(d);
    pa = A.pos;
    pb = B.pos;
    if (len < 1e-9) return;
    d /= len;
    double t;
    if (w.rayToObject(A, B.pos, -d, t)) pa = B.pos - d * t;
    if (w.rayToObject(B, A.pos, d, t)) pb = A.pos + d * t;
}

Joint* World::spring(uint32_t a, uint32_t b, double stiffness, double damping, double maxForce, std::string* err) {
    Object* A = find(a);
    Object* B = find(b);
    if (!A || !B || a == b) {
        if (err) *err = "spring <a> <b> [N/m] [N·s/m] [max_N]";
        return nullptr;
    }
    glm::dvec3 pa, pb;
    facingAnchors(*this, *A, *B, pa, pb);
    JointState s;
    s.type = JointType::Spring;
    s.a = a;
    s.b = b;
    s.anchorA = glm::conjugate(A->rot) * (pa - A->pos);
    s.anchorB = glm::conjugate(B->rot) * (pb - B->pos);
    s.restLength = glm::length(pb - pa);
    s.stiffness = stiffness > 0 ? stiffness : 2000;
    s.damping = damping > 0 ? damping : 20;
    s.maxForce = maxForce > 0 ? maxForce : 1000;
    return addJoint(s, err);
}

Joint* World::rope(uint32_t a, uint32_t b, double length, double maxForce, std::string* err) {
    Object* A = find(a);
    Object* B = find(b);
    if (!A || !B || a == b) {
        if (err) *err = "rope <a> <b> [length_cm] [max_N]";
        return nullptr;
    }
    glm::dvec3 pa, pb;
    facingAnchors(*this, *A, *B, pa, pb);
    JointState s;
    s.type = JointType::Rope;
    s.a = a;
    s.b = b;
    s.anchorA = glm::conjugate(A->rot) * (pa - A->pos);
    s.anchorB = glm::conjugate(B->rot) * (pb - B->pos);
    s.restLength = length > 0 ? length : std::max(0.01, glm::length(pb - pa));
    s.maxForce = maxForce > 0 ? maxForce : 2000;
    return addJoint(s, err);
}

// ============================================================================ Tragfähigkeit

void World::computeCapacity(Joint& j) {
    Object* A = find(j.a);
    Object* B = find(j.b);
    j.rT = j.rS = j.rM = j.rMax = 1e30;
    if (!A || !B) return;
    const Material& ma = materials()[A->material];
    const Material& mb = materials()[B->material];
    const double A_ = std::max(j.area, 1e-8), lever = std::sqrt(A_);
    switch (j.type) {
    case JointType::Glue: {
        // Die Fuge ist höchstens so fest wie das schwächere Material (Holzleim-Fugen brechen im Holz)
        double weakest = std::min(ma.tensileMPa, mb.tensileMPa);
        double ft = std::min(j.ft, weakest), fs = std::min(j.fs, weakest);
        j.rT = ft * 1e6 * A_;
        j.rS = fs * 1e6 * A_;
        j.rM = ft * 1e6 * A_ * lever / 6.0;
        break;
    }
    case JointType::Nail:
    case JointType::Screw: {
        const bool screw = j.type == JointType::Screw;
        const double d = j.diameterMm, t1 = j.thickA * 1000.0, lpen = std::max(0.0, j.lengthMm - t1);
        double fax, fv;
        if (isWood(mb)) {
            const double rhoB = mb.density, rhoMin = isWood(ma) ? std::min(ma.density, mb.density) : mb.density;
            fax = 20e-6 * rhoB * rhoB * d * lpen;                 // Auszugswiderstand (N), glatter Nagel
            const double fh = 0.082 * rhoMin * std::pow(d, -0.3);  // Lochleibungsfestigkeit (N/mm²)
            const double my = 0.3 * (screw ? 800.0 : 600.0) * std::pow(d, 2.6);  // Fließmoment des Stifts (N·mm)
            // Johansen: Holz wird eingedrückt oder der Stift biegt sich (der kleinere Wert zählt)
            fv = std::min({fh * t1 * d, fh * lpen * d, 1.15 * std::sqrt(2.0 * my * fh * d)});
            if (screw) fax *= 3.0;  // Gewinde hält etwa dreimal so gut
        } else if (isMasonry(mb)) {  // Schraube mit Dübel
            fax = 120.0 * d * std::min(lpen / 40.0, 1.5);
            fv = fax;
        } else {  // Metall: Gewinde
            fax = 0.6 * mb.tensileMPa * kPi * d * d / 4.0;
            fv = fax * 0.6;
        }
        const double steel = 600.0 * kPi * d * d / 4.0;  // Stahl des Nagels/der Schraube
        fax = std::min(fax, steel);
        fv = std::min(fv, 0.6 * steel);
        j.rT = j.count * fax;
        j.rS = j.count * fv;
        j.rM = std::max(j.rT, j.rS) * 0.3 * lever;
        break;
    }
    case JointType::Weld: {
        const double fw = 0.6 * std::min(ma.tensileMPa, mb.tensileMPa) * 1e6;
        const double aw = 4.0 * lever * j.seamMm / 1000.0;  // Naht rundherum
        j.rT = j.rS = fw * std::min(aw, A_);
        j.rM = j.rT * 0.3 * lever;
        break;
    }
    default:
        j.rMax = std::max(1.0, j.maxForce);
        break;
    }
}

// ============================================================================ Physik-Verbindung

void World::jointPoints(const Joint& j, glm::dvec3& pa, glm::dvec3& pb) const {
    const Object* A = const_cast<World*>(this)->find(j.a);
    const Object* B = const_cast<World*>(this)->find(j.b);
    pa = A ? A->pos + A->rot * j.anchorA : glm::dvec3(0);
    pb = B ? B->pos + B->rot * j.anchorB : glm::dvec3(0);
}

void World::buildConstraint(Joint& j) {
    if (j.constraint) physics_.destroyConstraint(j.constraint);
    j.constraint = 0;
    Object* A = find(j.a);
    Object* B = find(j.b);
    if (!A || !B || A->body == kNoBody || B->body == kNoBody) return;
    if (A->fixed && B->fixed) return;  // beides fest: nichts zu tun
    glm::dvec3 pa, pb;
    jointPoints(j, pa, pb);
    JointDesc d;
    // Ein festes Teil wirkt wie die Welt; das bewegliche ist immer "b"
    const bool swap = B->fixed;
    d.a = swap ? kNoBody : (A->fixed ? kNoBody : A->body);
    d.b = swap ? A->body : B->body;
    switch (j.type) {
    case JointType::Hinge:
        d.kind = 1;
        d.pointA = d.pointB = pb;
        d.axis = B->rot * j.axisB;
        d.motor = std::abs(j.motorRpm) > 1e-3f;
        d.motorRpm = j.motorRpm;
        d.motorTorque = j.motorTorque;
        break;
    case JointType::Spring:
    case JointType::Rope:
        d.kind = 2;
        d.pointA = swap ? pb : pa;
        d.pointB = swap ? pa : pb;
        d.minDist = j.type == JointType::Spring ? j.restLength : 0.0;
        d.maxDist = j.restLength;
        if (j.type == JointType::Spring) {
            d.stiffness = (float)j.stiffness;
            d.damping = (float)j.damping;
        }
        break;
    default:
        d.kind = 0;
        d.pointA = d.pointB = pa;
        break;
    }
    j.constraint = physics_.createJoint(d);
}

void World::releaseJointsOf(uint32_t id) {
    for (auto& j : joints_)
        if ((j->a == id || j->b == id) && j->constraint) {
            physics_.destroyConstraint(j->constraint);
            j->constraint = 0;
        }
}

void World::rebuildJointsOf(uint32_t id) {
    for (auto& j : joints_)
        if (j->a == id || j->b == id) {
            computeCapacity(*j);
            buildConstraint(*j);
        }
}

void World::updateJoint(Joint& j) {
    computeCapacity(j);
    buildConstraint(j);
}

Joint* World::findJoint(uint32_t id) {
    for (auto& j : joints_)
        if (j->id == id) return j.get();
    return nullptr;
}

void World::removeJoint(uint32_t id) {
    for (size_t i = 0; i < joints_.size(); ++i)
        if (joints_[i]->id == id) {
            if (joints_[i]->constraint) physics_.destroyConstraint(joints_[i]->constraint);
            joints_.erase(joints_.begin() + i);
            return;
        }
}

std::vector<std::string> World::takeEvents() {
    std::vector<std::string> e;
    e.swap(events_);
    return e;
}

// Belastung messen und bei Überlast brechen (nach jedem Physikschritt)
void World::checkJoints(float h) {
    std::vector<uint32_t> broken;
    for (auto& jp : joints_) {
        Joint& j = *jp;
        if (!j.constraint) continue;
        glm::dvec3 f, t;
        if (!physics_.jointLoad(j.constraint, f, t)) continue;
        Object* A = find(j.a);
        Object* B = find(j.b);
        if (!A || !B) continue;
        if (B->fixed) f = -f;  // gemessen wurde die Kraft auf a
        j.lastForce = f;
        j.lastTorque = t;
        double u;
        if (j.type == JointType::Hinge) {
            u = glm::length(f) / j.rMax;
        } else if (j.type == JointType::Spring || j.type == JointType::Rope) {
            u = f.x / j.rMax;
        } else {
            const glm::dvec3 n = A->rot * j.normalA;
            const double tension = std::max(0.0, -glm::dot(f, n));  // Zug: b wird von a weggezogen
            const double shear = glm::length(f - n * glm::dot(f, n));
            const double moment = glm::length(t);
            const double a = tension / j.rT + moment / j.rM, b = shear / j.rS;
            u = std::sqrt(a * a + b * b);
        }
        // Wärme: Kleber erweicht, Holz verkohlt um Nägel, Stahl verliert Festigkeit
        const double hf = heatOn_ ? jointHeatFactor(j) : 1.0;
        u /= hf;
        if (hf <= 2e-3 && u < 1.5 && breakage_ && !j.unbreakable) u = 1.5;  // durchgeschmolzen/durchgebrannt: hält nichts mehr
        j.loadPrev = j.load;
        j.load = u;
        // Bruch erst, wenn die Überlast etwa 15 ms anhält. Starre Körper stoßen in der Rechnung in Millisekunden
        // aufeinander; echtes Holz, Kleber und Stahl federn etwas nach und nehmen solchen Spitzen die Härte.
        // Ein echter Schlag oder eine dauerhafte Überlast bricht die Verbindung weiterhin.
        j.loadSlow += (u - j.loadSlow) * std::min(1.0, double(h) / 0.015);
        if (breakage_ && !j.unbreakable && j.loadSlow >= 1.0) broken.push_back(j.id);
    }
    for (uint32_t id : broken) {
        Joint* j = findJoint(id);
        if (!j) continue;
        Object* A = find(j->a);
        Object* B = find(j->b);
        std::string msg = std::string(jointName(*j)) + " J" + std::to_string(id) + " (" + (A ? A->name : "?") + " - " +
                          (B ? B->name : "?") + ") broke";
        if (j->type == JointType::Glue || j->type == JointType::Nail || j->type == JointType::Screw || j->type == JointType::Weld)
        {
            const glm::dvec3 n = A ? A->rot * j->normalA : glm::dvec3(0, 1, 0);
            const glm::dvec3& f = j->lastForce;
            msg += ": tension " + fmtNum(std::max(0.0, -glm::dot(f, n)), 0) + " N (holds " + fmtNum(j->rT, 0) + "), shear " +
                   fmtNum(glm::length(f - n * glm::dot(f, n)), 0) + " N (holds " + fmtNum(j->rS, 0) + "), moment " +
                   fmtNum(glm::length(j->lastTorque), 1) + " N·m";
        }
        else
            msg += ": " + fmtNum(j->type == JointType::Hinge ? glm::length(j->lastForce) : j->lastForce.x, 0) + " N > " +
                   fmtNum(j->rMax, 0) + " N";
        if (heatOn_) {
            const double hf = jointHeatFactor(*j);
            if (hf < 0.9) msg += " - weakened by heat to " + fmtNum(hf * 100, hf < 0.1 ? 1 : 0) + " %";
        }
        events_.push_back(msg);
        ++brokenCount_;
        removeJoint(id);
        for (Object* o : {A, B})  // ruhende Teile fallen jetzt
            if (o && o->body != kNoBody && !o->fixed) physics_.activate(o->body);
    }
}

// ============================================================================ Beschreibung

std::string World::jointText(const Joint& j, bool detailed) const {
    const Object* A = const_cast<World*>(this)->find(j.a);
    const Object* B = const_cast<World*>(this)->find(j.b);
    std::ostringstream ss;
    ss << "J" << j.id << "  " << jointName(j) << "  #" << j.a << " " << (A ? A->name : "?") << " <-> #" << j.b << " "
       << (B ? B->name : "?") << "  load " << fmtNum(j.load * 100.0, 0) << " %" << (j.unbreakable ? "  (unbreakable)" : "")
       << (j.constraint ? "" : "  (no effect: both fixed)");
    if (!detailed) return ss.str();
    ss << "\n";
    auto kN = [](double n) { return n >= 1e29 ? std::string("unlimited") : fmtNum(n, 0) + " N"; };
    switch (j.type) {
    case JointType::Glue: {
        const GluePreset* g = findGlue(j.preset);
        auto mpa = [](double v) { return fmtNum(v, v < 0.1 ? 4 : (v < 1 ? 2 : 1)); };
        if (j.preset == kStuckPreset) {
            ss << "  stuck         " << fmtNum(j.lengthMm, 0) << " mm deep, held by friction (" << mpa(j.ft) << " MPa on the flanks)\n";
            ss << "  flanks        " << fmtNum(j.area * 1e4, 2) << " cm²\n";
            break;
        }
        ss << "  glue          " << (g ? g->name : j.preset.c_str()) << ", strength " << mpa(j.ft) << " MPa, shear " << mpa(j.fs)
           << " MPa, max. gap " << fmtNum(j.gapMm, 1) << " mm\n";
        ss << "  area          " << fmtNum(j.area * 1e4, 2) << " cm²\n";
        break;
    }
    case JointType::Nail:
    case JointType::Screw:
        ss << "  " << (j.type == JointType::Nail ? "nails         " : "screws        ") << j.count << " x " << fmtNum(j.diameterMm, 1) << " x "
           << fmtNum(j.lengthMm, 0) << " mm, part a " << fmtNum(j.thickA * 1000, 1) << " mm thick, penetration "
           << fmtNum(j.lengthMm - j.thickA * 1000, 0) << " mm\n";
        break;
    case JointType::Weld:
        ss << "  seam          " << fmtNum(j.seamMm, 1) << " mm, contact area " << fmtNum(j.area * 1e4, 2) << " cm²\n";
        break;
    case JointType::Hinge:
        ss << "  axis          local " << (j.axisB.x > 0.5 ? "x" : j.axisB.y > 0.5 ? "y" : "z") << " through the center of mass of #" << j.b;
        if (std::abs(j.motorRpm) > 1e-3f) ss << ", motor " << fmtNum(j.motorRpm, 0) << " rpm, max " << fmtNum(j.motorTorque, 1) << " N·m";
        ss << "\n";
        break;
    case JointType::Spring:
        ss << "  spring        " << fmtNum(j.stiffness, 0) << " N/m, damping " << fmtNum(j.damping, 1) << " N·s/m, rest length "
           << fmtNum(j.restLength * 100, 1) << " cm\n";
        break;
    case JointType::Rope:
        ss << "  rope          length " << fmtNum(j.restLength * 100, 1) << " cm\n";
        break;
    }
    if (j.type == JointType::Hinge || j.type == JointType::Spring || j.type == JointType::Rope)
        ss << "  holds         " << kN(j.rMax) << "\n";
    else
        ss << "  holds         tension " << kN(j.rT) << ", shear " << kN(j.rS) << ", moment " << (j.rM >= 1e29 ? "unlimited" : fmtNum(j.rM, 1) + " N·m")
           << "\n";
    ss << "  load          " << fmtNum(j.load * 100, 1) << " %  (force " << fmtNum(glm::length(j.lastForce), 1) << " N, moment "
       << fmtNum(glm::length(j.lastTorque), 2) << " N·m)\n";
    ss << "  breaks        " << (j.unbreakable ? "never (unbreakable)" : (breakage_ ? "at 100 % load" : "off (breakage off)"));
    return ss.str();
}
