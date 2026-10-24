#include "Physics.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/ConvexHullBuilder.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Constraints/ContactConstraintManager.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "BoreShape.h"
#include "Materials.h"
#include "Profile.h"

using namespace JPH;

namespace {

namespace Layers {
constexpr ObjectLayer STATIC = 0;
constexpr ObjectLayer MOVING = 1;
constexpr ObjectLayer COUNT = 2;
} // namespace Layers

namespace BPLayers {
constexpr BroadPhaseLayer STATIC(0);
constexpr BroadPhaseLayer MOVING(1);
constexpr uint COUNT = 2;
} // namespace BPLayers

class BPLayerInterface final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return BPLayers::COUNT; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override {
        return l == Layers::STATIC ? BPLayers::STATIC : BPLayers::MOVING;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer l) const override {
        return l == BPLayers::STATIC ? "STATIC" : "MOVING";
    }
#endif
};

class ObjVsBPFilter final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer a, BroadPhaseLayer b) const override {
        return a == Layers::MOVING || b == BPLayers::MOVING;
    }
};

class ObjPairFilter final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        return a == Layers::MOVING || b == Layers::MOVING;
    }
};

void traceImpl(const char* fmt, ...) {
    va_list list;
    va_start(list, fmt);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, list);
    va_end(list);
    std::fprintf(stderr, "[Jolt] %s\n", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool assertFailedImpl(const char* expr, const char* msg, const char* file, uint line) {
    std::fprintf(stderr, "[Jolt] Assert %s:%u: (%s) %s\n", file, line, expr, msg ? msg : "");
    return false;  // nicht in den Debugger springen
}
#endif

inline BodyID toId(BodyHandle h) { return BodyID(h); }
inline RVec3 toR(const glm::dvec3& v) { return RVec3(Real(v.x), Real(v.y), Real(v.z)); }
inline Vec3 toV(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
inline Vec3 toV(const glm::dvec3& v) { return Vec3(float(v.x), float(v.y), float(v.z)); }
inline glm::vec3 fromV(Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline glm::dvec3 fromR(RVec3Arg v) { return glm::dvec3(v.GetX(), v.GetY(), v.GetZ()); }
inline Quat toQ(const glm::dquat& q) {
    glm::dquat n = glm::normalize(q);
    return Quat(float(n.x), float(n.y), float(n.z), float(n.w));
}
inline glm::dquat fromQ(QuatArg q) { return glm::dquat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }
inline BodyHandle handleOf(const Body& b) { return b.GetID().GetIndexAndSequenceNumber(); }

// Benutzerdaten: Teilnummer (Bit 0-31), Material + 1 (Bit 32-47), Schmierstoff (Bit 48-55)
inline uint64 packUser(uint32_t id, int material, int lubricant = 0) {
    return uint64(id) | (uint64(material + 1) << 32) | (uint64(std::clamp(lubricant, 0, 255)) << 48);
}
inline int materialOf(const Body& b) { return int((b.GetUserData() >> 32) & 0xffff) - 1; }
inline int lubricantOf(const Body& b) { return int((b.GetUserData() >> 48) & 0xff); }

inline bool isBoreContact(const Body& b, const SubShapeID& id) {
    SubShapeID rem;
    const Shape* leaf = b.GetShape()->GetLeafShape(id, rem);
    return leaf && leaf->GetSubType() == BoreShape::kSubType;
}

// Kontaktimpulse aus dem Löser (Jolt-Patch), fester Speicher, Platz per Zähler
struct ImpulseSink {
    std::vector<ContactImpulse> buffer = std::vector<ContactImpulse>(65536);
    std::atomic<uint32> count{0};
};
ImpulseSink* gImpulses = nullptr;

void onContactImpulse(const Body& b1, const Body& b2, RVec3Arg p, Vec3Arg n, float normalImpulse, Vec3Arg friction) {
    ImpulseSink* sink = gImpulses;
    if (!sink || (normalImpulse <= 0.0f && friction.LengthSq() <= 0.0f)) return;
    const uint32 idx = sink->count.fetch_add(1);
    if (idx >= sink->buffer.size()) return;
    ContactImpulse& c = sink->buffer[idx];
    c.a = b1.GetID().GetIndexAndSequenceNumber();
    c.b = b2.GetID().GetIndexAndSequenceNumber();
    c.point = glm::dvec3(p.GetX(), p.GetY(), p.GetZ());
    c.normal = glm::vec3(n.GetX(), n.GetY(), n.GetZ());
    c.normalImpulse = normalImpulse;
    c.frictionImpulse = glm::vec3(friction.GetX(), friction.GetY(), friction.GetZ());
    const Vec3 v = b2.GetPointVelocity(p) - b1.GetPointVelocity(p);
    c.slip = (v - v.Dot(n) * n).Length();
}

// Reibung je Materialpaar, Kontakte für Rollreibung/Überlappung, Aufpralle, Kontakte des beobachteten Körpers.
// Wird aus mehreren Physik-Threads gleichzeitig aufgerufen.
class Listener final : public ContactListener {
public:
    // Reibungstabellen (n x n), beim Start gefüllt
    int nMat = 0;
    std::vector<float> muS, muK;
    bool legacy = false;  // nur für Messungen: eine gemittelte Reibungszahl wie früher

    std::atomic<uint32> watch{0xffffffffu};
    mutable std::mutex mutex;
    std::vector<ContactInfo> current;

    // Statistik über alle Kontakte (für die Leistungsanzeige)
    std::atomic<int> contactPoints{0};
    std::atomic<float> maxPenetration{0.0f};

    // alle Kontakte dieses Teilschritts (fester Speicher, Platz per Zähler vergeben)
    std::vector<StepContact> buffer = std::vector<StepContact>(32768);
    std::atomic<uint32> count{0};

    // Aufpralle (für Bruch und später Geräusche)
    std::mutex impactMutex;
    std::vector<ImpactInfo> impacts;

    // Teilpaare ohne Kontakt (sortiert), siehe Physics::setNoContactPairs
    std::mutex noContactMutex;
    std::vector<uint64_t> noContact;
    std::atomic<bool> anyNoContact{false};

    ValidateResult OnContactValidate(const Body& b1, const Body& b2, RVec3Arg, const CollideShapeResult&) override {
        if (!anyNoContact.load(std::memory_order_relaxed)) return ValidateResult::AcceptAllContactsForThisBodyPair;
        const uint64_t a = uint32_t(b1.GetUserData()), b = uint32_t(b2.GetUserData());
        const uint64_t key = a < b ? (a << 32) | b : (b << 32) | a;
        std::lock_guard<std::mutex> lock(noContactMutex);
        return std::binary_search(noContact.begin(), noContact.end(), key) ? ValidateResult::RejectAllContactsForThisBodyPair
                                                                           : ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    // Haftreibung im Stillstand, Gleitreibung beim Rutschen; dazwischen weicher Übergang (Stribeck-Kurve)
    float friction(const Body& b1, const Body& b2, const ContactManifold& m, float& slip) const {
        slip = 0;
        const int a = materialOf(b1), b = materialOf(b2);
        if (a < 0 || b < 0 || a >= nMat || b >= nMat || m.mRelativeContactPointsOn1.empty()) return -1.0f;
        float s = muS[a * nMat + b], k = muK[a * nMat + b];
        if (legacy) return 0.5f * (s + k);
        // geschmiert: Grenzreibung des Schmierstoffs (wenn kleiner)
        const auto& lub = lubricants();
        for (int l : {lubricantOf(b1), lubricantOf(b2)})
            if (l > 0 && l < (int)lub.size()) {
                s = std::min(s, lub[l].muS);
                k = std::min(k, lub[l].muK);
            }
        RVec3 p = m.GetWorldSpaceContactPointOn1(0);
        Vec3 n = m.mWorldSpaceNormal;
        Vec3 v = b2.GetPointVelocity(p) - b1.GetPointVelocity(p);
        Vec3 vt = v - v.Dot(n) * n;
        slip = vt.Length();
        return k + (s - k) * std::exp(-slip / 0.02f);
    }

    void record(const Body& b1, const Body& b2, const ContactManifold& m, ContactSettings& settings) {
        const uint np = m.mRelativeContactPointsOn1.size();
        contactPoints += (int)np;
        float pen = m.mPenetrationDepth, cur = maxPenetration.load();
        while (pen > cur && !maxPenetration.compare_exchange_weak(cur, pen)) {
        }
        float slip = 0;
        float mu = friction(b1, b2, m, slip);
        if (mu >= 0) settings.mCombinedFriction = mu;
        // Stift in der Bohrung: kein Rückprall (sonst klappert er bei jedem Anliegen)
        if (isBoreContact(b1, m.mSubShapeID1) || isBoreContact(b2, m.mSubShapeID2)) settings.mCombinedRestitution = 0.0f;

        if (np > 0) {
            uint32 idx = count.fetch_add(1);
            if (idx < buffer.size()) {
                StepContact& c = buffer[idx];
                c.a = handleOf(b1);
                c.b = handleOf(b2);
                RVec3 sum = RVec3::sZero();
                for (uint i = 0; i < np; ++i) sum += m.GetWorldSpaceContactPointOn1(i);
                c.point = fromR(sum / Real(np));
                c.normal = fromV(m.mWorldSpaceNormal);
                c.depth = m.mPenetrationDepth;
            }
        }

        uint32 w = watch.load();
        if (w == 0xffffffffu) return;
        bool is1 = b1.GetID().GetIndexAndSequenceNumber() == w;
        bool is2 = b2.GetID().GetIndexAndSequenceNumber() == w;
        if (!is1 && !is2) return;
        Vec3 n = is1 ? -m.mWorldSpaceNormal : m.mWorldSpaceNormal;
        BodyHandle other = handleOf(is1 ? b2 : b1);
        std::lock_guard<std::mutex> lock(mutex);
        for (uint i = 0; i < np; ++i) {
            RVec3 p = m.GetWorldSpaceContactPointOn1(i);
            current.push_back({fromR(p), glm::dvec3(n.GetX(), n.GetY(), n.GetZ()), other, settings.mCombinedFriction, slip});
        }
    }

    void OnContactAdded(const Body& b1, const Body& b2, const ContactManifold& m, ContactSettings& s) override {
        record(b1, b2, m, s);
        impact(b1, b2, m, -0.3f);
    }
    // Aufprall: Annäherung schneller als minApproach (negativ, m/s) an der Kontaktstelle
    void impact(const Body& b1, const Body& b2, const ContactManifold& m, float minApproach) {
        if (m.mRelativeContactPointsOn1.empty()) return;
        RVec3 p = m.GetWorldSpaceContactPointOn1(0);
        Vec3 n = m.mWorldSpaceNormal;  // von Körper 1 zu Körper 2
        float vn = (b2.GetPointVelocity(p) - b1.GetPointVelocity(p)).Dot(n);
        if (vn > minApproach) return;  // kaum Annäherung
        float im1 = b1.IsDynamic() ? b1.GetMotionProperties()->GetInverseMass() : 0.0f;
        float im2 = b2.IsDynamic() ? b2.GetMotionProperties()->GetInverseMass() : 0.0f;
        if (im1 + im2 <= 0) return;
        double mu = 1.0 / (im1 + im2);
        ImpactInfo info;
        info.a = handleOf(b1);
        info.b = handleOf(b2);
        info.point = fromR(p);
        info.pointB = fromR(m.GetWorldSpaceContactPointOn2(0));
        info.normal = glm::dvec3(n.GetX(), n.GetY(), n.GetZ());
        info.speed = -vn;
        info.energy = 0.5 * mu * vn * vn;
        std::lock_guard<std::mutex> lock(impactMutex);
        if (impacts.size() < 4096) impacts.push_back(info);
    }
    void OnContactPersisted(const Body& b1, const Body& b2, const ContactManifold& m, ContactSettings& s) override {
        record(b1, b2, m, s);
        // Schlag auf ein Teil, das schon anliegt (Nagel angesetzt, dann Hammerschlag): der Kontakt besteht bereits
        // (vorausschauend ab 2 cm), ein neuer entsteht nicht. Ruhende Kontakte nähern sich nicht an.
        impact(b1, b2, m, -1.0f);
    }
};

} // namespace

struct Physics::Impl {
    Listener listener;
    ImpulseSink impulses;
    PhysicsStats stats;
    std::vector<ContactInfo> lastContacts;
    std::vector<StepContact> stepContacts;
    std::vector<ContactImpulse> stepImpulses;
    std::vector<float> rolling;  // Rollreibungszahl je Materialpaar
    std::unique_ptr<TempAllocatorImpl> temp;
    std::unique_ptr<JobSystemThreadPool> jobs;
    BPLayerInterface bpLayers;
    ObjVsBPFilter objVsBp;
    ObjPairFilter objPair;
    std::unique_ptr<PhysicsSystem> system;
    std::unordered_map<uint32_t, Ref<Constraint>> constraints;
    uint32_t nextConstraint = 1;
    float accum = 0.0f;
    struct Figure {
        Ref<CharacterVirtual> c;
        Vec3 ground = Vec3::sZero();  // übernommene (gefederte) Bodengeschwindigkeit
    };
    std::map<int, Figure> characters;  // 0 = eigene Figur, sonst Mitspieler
    CharacterVirtual* character(int who) const {
        auto it = characters.find(who);
        return it == characters.end() ? nullptr : it->second.c.GetPtr();
    }
    BodyHandle ground = kNoBody;
    bool legacy = false;
    bool initialized = false;
};

static constexpr float kCharMass = 75.0f;  // Spielfigur (kg)

Physics::Physics() : m(std::make_unique<Impl>()) {}
Physics::~Physics() { shutdown(); }

bool Physics::init() {
    RegisterDefaultAllocator();
    Trace = traceImpl;
    JPH_IF_ENABLE_ASSERTS(AssertFailed = assertFailedImpl;)
    Factory::sInstance = new Factory();
    RegisterTypes();
    BoreShape::sRegister();  // exakt runde Bohrungswand (eigene Form)
    gImpulses = &m->impulses;
    ContactConstraintManager::sContactImpulseCallback = onContactImpulse;

    m->temp = std::make_unique<TempAllocatorImpl>(64 * 1024 * 1024);
    int threads = std::max(1, (int)std::thread::hardware_concurrency() - 1);    m->jobs = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, threads);

    m->system = std::make_unique<PhysicsSystem>();
    m->system->Init(20000, 0, 65536, 20480, m->bpLayers, m->objVsBp, m->objPair);
    m->system->SetGravity(Vec3(0, -9.81f, 0));

    // Reibungs- und Rolltabellen aus den Materialwerten
    const int n = (int)materials().size();
    m->listener.nMat = n;
    m->listener.muS.assign(n * n, 0.5f);
    m->listener.muK.assign(n * n, 0.4f);
    m->rolling.assign(n * n, 0.0f);
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b) {
            frictionPair(a, b, m->listener.muS[a * n + b], m->listener.muK[a * n + b]);
            m->rolling[a * n + b] = rollingPair(a, b);
        }

    setLegacySolver(false);
    m->system->SetContactListener(&m->listener);
    m->initialized = true;
    return true;
}

void Physics::setLegacySolver(bool on) {
    m->legacy = on;
    m->listener.legacy = on;
    PhysicsSettings s = m->system->GetPhysicsSettings();
    // Eindringen bis 0,1 mm bleibt unkorrigiert (Jolt: 2 cm). Fein genug für Stifte mit Zehntelmillimeter-Spiel, die
    // Lochwand gleicht den Rest aus (BoreShape::sSlop). Alte Messung: 1 mm.
    s.mPenetrationSlop = on ? 0.001f : 0.0001f;
    s.mSpeculativeContactDistance = 0.02f;  // Vorausschau für Kontakte (schnelle Teile zusätzlich per CCD)
    s.mNumVelocitySteps = 15;
    s.mNumPositionSteps = 4;
    s.mLinearCastThreshold = 0.5f;
    // Ineinandersteckende Teile sanft auseinanderschieben (höchstens 0,5 mm je Durchgang, ca. 0,5 m/s) statt
    // sie in einem Schritt herauszuschleudern. Normale Kontakte liegen weit darunter und merken davon nichts.
    s.mMaxPenetrationDistance = on ? 0.2f : 0.0005f;
    m->system->SetPhysicsSettings(s);
    BoreShape::sSlop = on ? 0.0f : s.mPenetrationSlop;  // Lochwand gleicht die Toleranz aus
}

void Physics::shutdown() {
    if (!m || !m->initialized) return;
    ContactConstraintManager::sContactImpulseCallback = nullptr;
    gImpulses = nullptr;
    m->characters.clear();
    for (auto& [id, c] : m->constraints) m->system->RemoveConstraint(c);
    m->constraints.clear();
    m->system.reset();
    m->jobs.reset();
    m->temp.reset();
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
    m->initialized = false;
}

static RefConst<Shape> buildShape(const BodyDesc& d) {
    // Exakte Grundformen: echte Kugel/Zylinder rollen glatt und sind viel schneller als Vielflächner
    if (d.primitive != 0) {
        const glm::vec3 s(d.primSize);
        ShapeSettings::ShapeResult r;
        if (d.primitive == 1) {
            float cr = std::clamp(std::min({s.x, s.y, s.z}) * 0.1f, 0.0f, 0.002f);
            BoxShapeSettings bs(Vec3(s.x, s.y, s.z), cr);
            bs.SetDensity(d.density);
            r = bs.Create();
        } else if (d.primitive == 2) {
            SphereShapeSettings ss(s.x);
            ss.SetDensity(d.density);
            r = ss.Create();
        } else if (d.primitive == 3) {
            float cr = std::clamp(std::min(s.x, s.y) * 0.1f, 0.0f, 0.002f);
            CylinderShapeSettings cs(s.y, s.x, cr);
            cs.SetDensity(d.density);
            r = cs.Create();
        }
        if (r.IsValid()) return r.Get();
    }
    std::vector<RefConst<Shape>> subs;
    for (const auto& hull : d.hulls) {
        if (hull.size() < 4) continue;
        // doppelte Punkte (auf 1 µm genau) zusammenfassen: sie stören den Hüllenbau
        glm::vec3 mn(1e30f), mx(-1e30f);
        Array<Vec3> pts;
        pts.reserve(hull.size());
        {
            std::unordered_map<uint64_t, bool> seen;
            for (const auto& p : hull) {
                const uint64_t key = (uint64_t(int64_t(std::llround(p.x * 1e6)) & 0x1fffff) << 42) |
                                     (uint64_t(int64_t(std::llround(p.y * 1e6)) & 0x1fffff) << 21) | (uint64_t(int64_t(std::llround(p.z * 1e6)) & 0x1fffff));
                if (!seen.emplace(key, true).second) continue;
                pts.push_back(toV(p));
                mn = glm::min(mn, p);
                mx = glm::max(mx, p);
            }
        }
        if (pts.size() < 4) continue;
        glm::vec3 ext = mx - mn;
        float minExt = std::min({ext.x, ext.y, ext.z});
        if (minExt < 1e-5f) continue;  // zu dünn für eine stabile Kollisionsform
        float radius = std::clamp(minExt * 0.05f, 0.0f, 0.002f);
        // Gekrümmte Flächen (Senkungen, Rundungen) liefern viele fast ebene Punkte. Jolt lehnt die Hülle dann ab, wenn
        // ein Punkt etwas außerhalb liegt -> mit etwas größerer Toleranz erneut versuchen, statt die Hülle wegzulassen.
        ShapeSettings::ShapeResult r;
        for (float tol : {std::max(1e-5f, minExt * 0.001f), 1e-4f, 4e-4f, std::max(1e-3f, minExt * 0.02f)}) {
            ConvexHullShapeSettings hs(pts, radius);
            hs.mHullTolerance = tol;
            hs.SetDensity(d.density);
            r = hs.Create();
            if (r.IsValid()) break;
        }
        if (r.IsValid()) subs.push_back(r.Get());
        else if (minExt > 0.0005f) std::fprintf(stderr, "collision hull dropped (%zu points, min extent %.2f mm): %s\n", (size_t)pts.size(), minExt * 1000, r.GetError().c_str());
    }
    // Drehung, die die lokale y-Achse auf "axis" bringt
    auto fromY = [](const glm::dvec3& axis) {
        const Vec3 a = toV(glm::normalize(axis));
        if (a.GetY() < -0.999999f) return Quat::sRotation(Vec3::sAxisX(), JPH_PI);
        return Quat::sFromTo(Vec3::sAxisY(), a);
    };
    struct Placed {
        RefConst<Shape> shape;
        Vec3 pos;
        Quat rot;
    };
    std::vector<Placed> placed;
    for (auto& s : subs) placed.push_back({s, Vec3::sZero(), Quat::sIdentity()});
    for (const CylDesc& c : d.cylinders) {  // exakte Zylinder (gedrehte Stifte)
        const float hh = float(c.halfLength), rr = float(c.radius);
        CylinderShapeSettings cs(hh, rr, std::clamp(std::min(hh, rr) * 0.1f, 0.0f, 0.001f));
        cs.SetDensity(d.density);
        ShapeSettings::ShapeResult r = cs.Create();
        if (r.IsValid()) placed.push_back({r.Get(), toV(c.center), fromY(c.axis)});
    }
    const size_t solidCount = placed.size();
    for (size_t i = 0; i < d.bores.size(); ++i) {  // exakt runde Bohrungswände
        const BoreDesc& b = d.bores[i];
        RefConst<Shape> s = new BoreShape(float(b.radius), float(b.halfLength), float(b.band), b.openTop, b.openBottom, int(i));
        placed.push_back({s, toV(b.center), fromY(b.axis)});
    }
    if (solidCount == 0) return nullptr;
    if (placed.size() == 1 && placed[0].pos == Vec3::sZero() && placed[0].rot.IsClose(Quat::sIdentity())) return placed[0].shape;
    if (placed.size() == 1) {
        RotatedTranslatedShapeSettings rs(placed[0].pos, placed[0].rot, placed[0].shape);
        ShapeSettings::ShapeResult r = rs.Create();
        return r.IsValid() ? r.Get() : nullptr;
    }
    StaticCompoundShapeSettings cs;
    for (auto& p : placed) cs.AddShape(p.pos, p.rot, p.shape.GetPtr());
    ShapeSettings::ShapeResult r = cs.Create();
    if (!r.IsValid()) {
        std::fprintf(stderr, "Compound shape failed: %s\n", r.GetError().c_str());
        return nullptr;
    }
    return r.Get();
}

BodyHandle Physics::createBody(const BodyDesc& d) {
    RefConst<Shape> shape = buildShape(d);
    if (!shape) return kNoBody;
    BodyCreationSettings bcs(shape, toR(d.position), toQ(d.rotation),
                             d.isStatic ? EMotionType::Static : EMotionType::Dynamic,
                             d.isStatic ? Layers::STATIC : Layers::MOVING);
    const Material& mat = materials()[std::clamp(d.material, 0, (int)materials().size() - 1)];
    bcs.mFriction = mat.frictionKinetic;  // wird je Kontakt durch das Materialpaar ersetzt
    bcs.mRestitution = d.restitution;
    bcs.mLinearDamping = 0.02f;
    bcs.mAngularDamping = 0.03f;
    bcs.mMaxAngularVelocity = 1500.0f;  // ca. 14 000 U/min (Propeller, Bohrer)
    bcs.mUserData = packUser(d.userData, d.material, d.lubricant);
    // Zusammengesetzte Formen: Kontakte jeder Teilhülle behalten. Jolt fasst sie sonst auf 4 Punkte zusammen und kann
    // dabei eine Ecke verlieren -> das Teil kippt langsam über diese Ecke und taucht ein.
    bcs.mUseManifoldReduction = shape->GetType() != EShapeType::Compound;
    if (!d.isStatic) {
        // Kontinuierliche Kollision: Jolt prüft den Weg nur bei schnellen Teilen, langsame kosten nichts extra
        bcs.mMotionQuality = d.ccd ? EMotionQuality::LinearCast : EMotionQuality::Discrete;
        if (d.massOverride > 0) {
            bcs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
            bcs.mMassPropertiesOverride.mMass = d.massOverride;
        }
    }
    BodyInterface& bi = m->system->GetBodyInterface();
    Body* body = bi.CreateBody(bcs);
    if (!body) return kNoBody;
    bi.AddBody(body->GetID(), d.isStatic ? EActivation::DontActivate : EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

// Volumen der konvexen Hülle einer Punktwolke (false, wenn entartet)
static bool hullVolume(const std::vector<glm::vec3>& pts, double& vol) {
    if (pts.size() < 4) return false;
    ConvexHullBuilder::Positions p;
    p.reserve(pts.size());
    for (const auto& v : pts) p.push_back(Vec3(v.x, v.y, v.z));
    ConvexHullBuilder b(p);
    const char* err = nullptr;
    ConvexHullBuilder::EResult r = b.Initialize(INT_MAX, 1e-7f, err);
    if (r != ConvexHullBuilder::EResult::Success && r != ConvexHullBuilder::EResult::MaxVerticesReached) return false;
    Vec3 com;
    float v = 0;
    b.GetCenterOfMassAndVolume(com, v);
    vol = v;
    return true;
}

double Physics::convexHullVolume(const std::vector<glm::vec3>& points) {
    double v = 0;
    return hullVolume(points, v) ? v : 0.0;
}

bool Physics::hullPlanes(const std::vector<glm::dvec3>& pts, std::vector<glm::dvec4>& planes) {
    planes.clear();
    if (pts.size() < 4) return false;
    glm::dvec3 c(0);
    for (const auto& p : pts) c += p;
    c /= double(pts.size());
    double ext = 0;
    for (const auto& p : pts) ext = std::max(ext, glm::length(p - c));
    if (ext < 1e-9) return false;
    // relativ zum Mittelpunkt in float rechnen (genug für Kollisionsformen)
    ConvexHullBuilder::Positions p;
    p.reserve(pts.size());
    for (const auto& v : pts) p.push_back(toV(v - c));
    ConvexHullBuilder b(p);
    const char* err = nullptr;
    ConvexHullBuilder::EResult r = b.Initialize(INT_MAX, float(ext * 1e-6), err);
    if (r != ConvexHullBuilder::EResult::Success && r != ConvexHullBuilder::EResult::MaxVerticesReached) return false;
    for (const ConvexHullBuilder::Face* f : b.GetFaces()) {
        if (f->mRemoved) continue;
        // Ebene genau (double) aus den Eckpunkten der Fläche (Newell)
        std::vector<int> idx;
        const ConvexHullBuilder::Edge* e = f->mFirstEdge;
        do {
            idx.push_back(e->mStartIdx);
            e = e->mNextEdge;
        } while (e && e != f->mFirstEdge && idx.size() < 1000);
        if (idx.size() < 3) continue;
        glm::dvec3 n(0), fc(0);
        for (size_t i = 0; i < idx.size(); ++i) {
            const glm::dvec3& a = pts[idx[i]];
            const glm::dvec3& q = pts[idx[(i + 1) % idx.size()]];
            n.x += (a.y - q.y) * (a.z + q.z);
            n.y += (a.z - q.z) * (a.x + q.x);
            n.z += (a.x - q.x) * (a.y + q.y);
            fc += a;
        }
        fc /= double(idx.size());
        double len = glm::length(n);
        if (len < 1e-18) continue;
        n /= len;
        if (glm::dot(n, fc - c) < 0) n = -n;
        double d = glm::dot(n, fc);
        for (int i : idx) d = std::max(d, glm::dot(n, pts[i]));
        planes.emplace_back(n, d);
    }
    return planes.size() >= 4;
}

std::vector<std::vector<glm::vec3>> Physics::simplifyHulls(std::vector<std::vector<glm::vec3>> hulls, std::vector<double> volumes,
                                                           double tolerance, size_t targetCount, double maxTolerance, double maxRelative) {
    const size_t n = hulls.size();
    if (n < 2) return hulls;
    std::vector<glm::vec3> mn(n), mx(n);
    std::vector<bool> alive(n, true);
    for (size_t i = 0; i < n; ++i) {
        mn[i] = glm::vec3(1e30f);
        mx[i] = glm::vec3(-1e30f);
        for (auto& v : hulls[i]) {
            mn[i] = glm::min(mn[i], v);
            mx[i] = glm::max(mx[i], v);
        }
    }
    auto touching = [&](size_t a, size_t b) {
        const float e = 1e-5f;
        return mn[a].x <= mx[b].x + e && mn[b].x <= mx[a].x + e && mn[a].y <= mx[b].y + e && mn[b].y <= mx[a].y + e &&
               mn[a].z <= mx[b].z + e && mn[b].z <= mx[a].z + e;
    };
    struct Pair {
        size_t a, b;
        double cost;
    };
    auto cost = [&](size_t a, size_t b) {
        std::vector<glm::vec3> u = hulls[a];
        u.insert(u.end(), hulls[b].begin(), hulls[b].end());
        double v;
        if (!hullVolume(u, v)) return 1e30;
        return v - volumes[a] - volumes[b];
    };
    std::vector<Pair> pairs;
    for (size_t a = 0; a < n; ++a)
        for (size_t b = a + 1; b < n; ++b)
            if (touching(a, b)) pairs.push_back({a, b, cost(a, b)});

    size_t aliveCount = n;
    for (;;) {
        size_t best = SIZE_MAX;
        for (size_t i = 0; i < pairs.size(); ++i)
            if (best == SIZE_MAX || pairs[i].cost < pairs[best].cost) best = i;
        if (best == SIZE_MAX) break;
        const double c = pairs[best].cost;
        auto coarseOk = [&](const Pair& p) {
            return p.cost <= maxTolerance && p.cost <= maxRelative * std::min(volumes[p.a], volumes[p.b]);
        };
        if (c > tolerance) {
            best = SIZE_MAX;
            if (aliveCount > targetCount)
                for (size_t i = 0; i < pairs.size(); ++i)
                    if (coarseOk(pairs[i]) && (best == SIZE_MAX || pairs[i].cost < pairs[best].cost)) best = i;
            if (best == SIZE_MAX) break;
        }
        --aliveCount;
        const size_t a = pairs[best].a, b = pairs[best].b;
        hulls[a].insert(hulls[a].end(), hulls[b].begin(), hulls[b].end());
        hulls[b].clear();
        volumes[a] += volumes[b];
        mn[a] = glm::min(mn[a], mn[b]);
        mx[a] = glm::max(mx[a], mx[b]);
        alive[b] = false;
        std::vector<Pair> next;
        next.reserve(pairs.size());
        for (const Pair& p : pairs) {
            if (p.a == a || p.b == a || p.a == b || p.b == b) continue;
            next.push_back(p);
        }
        for (size_t k = 0; k < n; ++k)
            if (alive[k] && k != a && touching(a, k)) next.push_back({std::min(a, k), std::max(a, k), cost(a, k)});
        pairs.swap(next);
    }
    std::vector<std::vector<glm::vec3>> out;
    for (size_t i = 0; i < n; ++i)
        if (alive[i]) out.push_back(std::move(hulls[i]));
    return out;
}

BodyHandle Physics::createGroundPlane(double y, int material, float restitution) {
    PlaneShapeSettings ps(Plane(Vec3::sAxisY(), 0.0f), nullptr, 100000.0f);
    ShapeSettings::ShapeResult r = ps.Create();
    if (!r.IsValid()) return kNoBody;
    BodyCreationSettings bcs(r.Get(), RVec3(0, Real(y), 0), Quat::sIdentity(), EMotionType::Static, Layers::STATIC);
    bcs.mFriction = materials()[std::clamp(material, 0, (int)materials().size() - 1)].frictionKinetic;
    bcs.mRestitution = restitution;
    bcs.mUserData = packUser(0, material);
    BodyInterface& bi = m->system->GetBodyInterface();
    Body* body = bi.CreateBody(bcs);
    if (!body) return kNoBody;
    bi.AddBody(body->GetID(), EActivation::DontActivate);
    m->ground = body->GetID().GetIndexAndSequenceNumber();
    return m->ground;
}

BodyHandle Physics::groundBody() const { return m->ground; }

BodyHandle Physics::createHeightField(const std::vector<float>& heights, int count, const glm::dvec2& corner, double cell, int material) {
    if (count < 4 || (count & 1) || heights.size() != (size_t)count * count) return kNoBody;
    // Höhen relativ zur niedrigsten (Form in float), der Körper steht in doppelter Genauigkeit an der Ecke
    const float base = *std::min_element(heights.begin(), heights.end());
    std::vector<float> rel(heights.size());
    for (size_t i = 0; i < heights.size(); ++i) rel[i] = heights[i] - base;
    HeightFieldShapeSettings hs(rel.data(), Vec3::sZero(), Vec3((float)cell, 1.0f, (float)cell), (uint32)count);
    hs.mBlockSize = 2;
    hs.mBitsPerSample = 8;
    ShapeSettings::ShapeResult r = hs.Create();
    if (!r.IsValid()) {
        std::fprintf(stderr, "terrain: height field rejected: %s\n", r.GetError().c_str());
        return kNoBody;
    }
    BodyCreationSettings bcs(r.Get(), RVec3(Real(corner.x), Real(base), Real(corner.y)), Quat::sIdentity(), EMotionType::Static, Layers::STATIC);
    const Material& mat = materials()[std::clamp(material, 0, (int)materials().size() - 1)];
    bcs.mFriction = mat.frictionKinetic;
    bcs.mRestitution = mat.restitution;
    bcs.mUserData = packUser(0, material);  // wie der Boden: kein Teil (Nummer 0), Reibung nach Material
    BodyInterface& bi = m->system->GetBodyInterface();
    Body* body = bi.CreateBody(bcs);
    if (!body) return kNoBody;
    bi.AddBody(body->GetID(), EActivation::DontActivate);
    return body->GetID().GetIndexAndSequenceNumber();
}

void Physics::destroyBody(BodyHandle h) {
    if (h == kNoBody) return;
    BodyInterface& bi = m->system->GetBodyInterface();
    bi.RemoveBody(toId(h));
    bi.DestroyBody(toId(h));
    if (h == m->ground) m->ground = kNoBody;
}

void Physics::setTransform(BodyHandle h, const glm::dvec3& pos, const glm::dquat& rot, bool act) {
    if (h == kNoBody) return;
    m->system->GetBodyInterface().SetPositionAndRotation(toId(h), toR(pos), toQ(rot),
                                                          act ? EActivation::Activate : EActivation::DontActivate);
}

void Physics::getTransform(BodyHandle h, glm::dvec3& pos, glm::dquat& rot) const {
    RVec3 p;
    Quat q;
    m->system->GetBodyInterface().GetPositionAndRotation(toId(h), p, q);
    pos = fromR(p);
    rot = fromQ(q);
}

void Physics::setVelocity(BodyHandle h, const glm::vec3& lin, const glm::vec3& ang) {
    if (h == kNoBody) return;
    BodyInterface& bi = m->system->GetBodyInterface();
    bi.SetLinearAndAngularVelocity(toId(h), toV(lin), toV(ang));
    // Jolt setzt den Schlaf-Zähler eines wachen Körpers sonst nicht zurück: lag er fast 0,5 s still, schläft er nach
    // dem ersten Schritt wieder ein (Bewegung < 1,5 cm) und die neue Geschwindigkeit ist verloren.
    if (glm::dot(lin, lin) + glm::dot(ang, ang) > 0) bi.ResetSleepTimer(toId(h));
}

glm::vec3 Physics::linearVelocity(BodyHandle h) const {
    return fromV(m->system->GetBodyInterface().GetLinearVelocity(toId(h)));
}

glm::vec3 Physics::angularVelocity(BodyHandle h) const {
    return fromV(m->system->GetBodyInterface().GetAngularVelocity(toId(h)));
}

glm::vec3 Physics::pointVelocity(BodyHandle h, const glm::dvec3& p) const {
    return fromV(m->system->GetBodyInterface().GetPointVelocity(toId(h), toR(p)));
}

float Physics::mass(BodyHandle h) const {
    BodyLockRead lock(m->system->GetBodyLockInterface(), toId(h));
    if (!lock.Succeeded()) return 0;
    const Body& b = lock.GetBody();
    if (!b.IsDynamic()) return 0;
    float inv = b.GetMotionProperties()->GetInverseMass();
    return inv > 0 ? 1.0f / inv : 0.0f;
}

void Physics::setMass(BodyHandle h, double mass) {
    if (h == kNoBody || mass <= 0) return;
    BodyLockWrite lock(m->system->GetBodyLockInterface(), toId(h));
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return;
    MotionProperties* mp = lock.GetBody().GetMotionProperties();
    const float inv = mp->GetInverseMass();
    if (inv <= 0) return;
    const float k = float((1.0 / inv) / mass);  // altes / neues Gewicht
    mp->SetInverseMass(float(1.0 / mass));
    mp->SetInverseInertia(mp->GetInverseInertiaDiagonal() * k, mp->GetInertiaRotation());
}

glm::dvec3 Physics::centerOfMass(BodyHandle h) const {
    return fromR(m->system->GetBodyInterface().GetCenterOfMassPosition(toId(h)));
}

void Physics::addForceAt(BodyHandle h, const glm::vec3& f, const glm::dvec3& p) {
    m->system->GetBodyInterface().AddForce(toId(h), toV(f), toR(p));
    m->system->GetBodyInterface().ResetSleepTimer(toId(h));  // gehalten/geschoben: nicht einschlafen
}

void Physics::addTorque(BodyHandle h, const glm::vec3& t) {
    m->system->GetBodyInterface().AddTorque(toId(h), toV(t));
    m->system->GetBodyInterface().ResetSleepTimer(toId(h));
}

void Physics::addForceNoWake(BodyHandle h, const glm::vec3& f) {
    m->system->GetBodyInterface().AddForce(toId(h), toV(f), EActivation::DontActivate);
}

void Physics::addTorqueNoWake(BodyHandle h, const glm::vec3& t) {
    m->system->GetBodyInterface().AddTorque(toId(h), toV(t), EActivation::DontActivate);
}

void Physics::addImpulseAt(BodyHandle h, const glm::vec3& j, const glm::dvec3& p) {
    m->system->GetBodyInterface().AddImpulse(toId(h), toV(j), toR(p));
    m->system->GetBodyInterface().ResetSleepTimer(toId(h));  // sonst schläft ein fast ruhendes Teil gleich wieder ein
}

void Physics::setContinuous(BodyHandle h, bool on) {
    if (h == kNoBody) return;
    m->system->GetBodyInterface().SetMotionQuality(toId(h), on ? EMotionQuality::LinearCast : EMotionQuality::Discrete);
}

void Physics::activate(BodyHandle h) {
    if (h != kNoBody) m->system->GetBodyInterface().ActivateBody(toId(h));
}

void Physics::deactivate(BodyHandle h) {
    if (h != kNoBody) m->system->GetBodyInterface().DeactivateBody(toId(h));
}

void Physics::keepAwake(BodyHandle h) {
    if (h != kNoBody) m->system->GetBodyInterface().ResetSleepTimer(toId(h));
}

bool Physics::isActive(BodyHandle h) const { return m->system->GetBodyInterface().IsActive(toId(h)); }

void Physics::activeBodies(std::vector<BodyHandle>& out) const {
    BodyIDVector ids;
    m->system->GetActiveBodies(EBodyType::RigidBody, ids);
    out.clear();
    out.reserve(ids.size());
    for (const BodyID& id : ids) out.push_back(id.GetIndexAndSequenceNumber());
}

uint32_t Physics::createHinge(BodyHandle body, const HingeDesc& d) {
    glm::dvec3 axis = glm::normalize(d.axis);
    glm::dvec3 helper = std::abs(axis.y) < 0.9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
    glm::dvec3 normal = glm::normalize(glm::cross(axis, helper));

    HingeConstraintSettings hs;
    hs.mSpace = EConstraintSpace::WorldSpace;
    hs.mPoint1 = hs.mPoint2 = toR(d.point);
    hs.mHingeAxis1 = hs.mHingeAxis2 = toV(axis);
    hs.mNormalAxis1 = hs.mNormalAxis2 = toV(normal);
    hs.mMaxFrictionTorque = d.bearingFriction;
    hs.mMotorSettings.SetTorqueLimit(std::max(0.0f, d.motorTorque));

    Ref<Constraint> c;
    {
        BodyLockWrite lock(m->system->GetBodyLockInterface(), toId(body));
        if (!lock.Succeeded()) return 0;
        c = hs.Create(Body::sFixedToWorld, lock.GetBody());
    }
    if (d.motor) {
        auto* hc = static_cast<HingeConstraint*>(c.GetPtr());
        hc->SetMotorState(EMotorState::Velocity);
        hc->SetTargetAngularVelocity(d.motorRpm * 2.0f * JPH_PI / 60.0f);
    }
    m->system->AddConstraint(c);
    m->system->GetBodyInterface().ActivateBody(toId(body));
    uint32_t id = m->nextConstraint++;
    m->constraints[id] = c;
    return id;
}

uint32_t Physics::createJoint(const JointDesc& d) {
    if (d.b == kNoBody) return 0;
    BodyID ids[2] = {d.a == kNoBody ? BodyID() : toId(d.a), toId(d.b)};
    Ref<Constraint> c;
    {
        // beide Körper sperren (Welt = fester Körper)
        BodyLockMultiWrite lock(m->system->GetBodyLockInterface(), d.a == kNoBody ? ids + 1 : ids, d.a == kNoBody ? 1 : 2);
        Body* b1 = d.a == kNoBody ? &Body::sFixedToWorld : lock.GetBody(0);
        Body* b2 = lock.GetBody(d.a == kNoBody ? 0 : 1);
        if (!b1 || !b2) return 0;
        if (d.kind == 0) {
            FixedConstraintSettings fs;
            fs.mSpace = EConstraintSpace::WorldSpace;
            fs.mAutoDetectPoint = false;
            fs.mPoint1 = fs.mPoint2 = toR(d.pointA);
            c = fs.Create(*b1, *b2);
        } else if (d.kind == 1) {
            glm::dvec3 axis = glm::normalize(d.axis);
            glm::dvec3 helper = std::abs(axis.y) < 0.9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
            glm::dvec3 normal = glm::normalize(glm::cross(axis, helper));
            HingeConstraintSettings hs;
            hs.mSpace = EConstraintSpace::WorldSpace;
            hs.mPoint1 = hs.mPoint2 = toR(d.pointA);
            hs.mHingeAxis1 = hs.mHingeAxis2 = toV(axis);
            hs.mNormalAxis1 = hs.mNormalAxis2 = toV(normal);
            hs.mMaxFrictionTorque = 0.02f;
            hs.mMotorSettings.SetTorqueLimit(std::max(0.0f, d.motorTorque));
            c = hs.Create(*b1, *b2);
            if (d.motor) {
                auto* hc = static_cast<HingeConstraint*>(c.GetPtr());
                hc->SetMotorState(EMotorState::Velocity);
                hc->SetTargetAngularVelocity(d.motorRpm * 2.0f * JPH_PI / 60.0f);
            }
        } else {
            DistanceConstraintSettings ds;
            ds.mSpace = EConstraintSpace::WorldSpace;
            ds.mPoint1 = toR(d.pointA);
            ds.mPoint2 = toR(d.pointB);
            ds.mMinDistance = (float)d.minDist;
            ds.mMaxDistance = (float)std::max(d.minDist, d.maxDist);
            if (d.stiffness > 0) {
                ds.mLimitsSpringSettings.mMode = ESpringMode::StiffnessAndDamping;
                ds.mLimitsSpringSettings.mStiffness = d.stiffness;
                ds.mLimitsSpringSettings.mDamping = d.damping;
            }
            c = ds.Create(*b1, *b2);
        }
    }
    if (!c) return 0;
    m->system->AddConstraint(c);
    m->system->GetBodyInterface().ActivateBody(toId(d.b));
    uint32_t id = m->nextConstraint++;
    m->constraints[id] = c;
    return id;
}

bool Physics::jointLoad(uint32_t id, glm::dvec3& force, glm::dvec3& torque) const {
    auto it = m->constraints.find(id);
    if (it == m->constraints.end()) return false;
    const float inv = 1.0f / fixedDt();
    Constraint* c = it->second.GetPtr();
    force = torque = glm::dvec3(0);
    switch (c->GetSubType()) {
    case EConstraintSubType::Fixed: {
        auto* f = static_cast<FixedConstraint*>(c);
        force = glm::dvec3(fromV(f->GetTotalLambdaPosition() * inv));
        torque = glm::dvec3(fromV(f->GetTotalLambdaRotation() * inv));
        return true;
    }
    case EConstraintSubType::Hinge: {
        auto* h = static_cast<HingeConstraint*>(c);
        force = glm::dvec3(fromV(h->GetTotalLambdaPosition() * inv));
        Vector<2> r = h->GetTotalLambdaRotation();
        torque = glm::dvec3(std::sqrt(r[0] * r[0] + r[1] * r[1]) * inv, 0, 0);
        return true;
    }
    case EConstraintSubType::Distance: {
        auto* d = static_cast<DistanceConstraint*>(c);
        force = glm::dvec3(std::abs(d->GetTotalLambdaPosition()) * inv, 0, 0);
        return true;
    }
    default:
        return false;
    }
}

void Physics::destroyConstraint(uint32_t id) {
    auto it = m->constraints.find(id);
    if (it == m->constraints.end()) return;
    m->system->RemoveConstraint(it->second);
    m->constraints.erase(it);
}

float Physics::hingeTorque(uint32_t id) const {
    auto it = m->constraints.find(id);
    if (it == m->constraints.end()) return 0;
    auto* hc = static_cast<HingeConstraint*>(it->second.GetPtr());
    return hc->GetTotalLambdaMotor() / fixedDt();
}

// Rollreibung: Rollende Teile verformen sich und ihre Unterlage ein wenig. Das bremst mit dem Moment
// c_rr · Normalkraft · Hebel. Ohne das rollt eine Kugel auf ebenem Boden ewig weiter.
void Physics::applyRollingResistance(float dt) {

    if (m->legacy || m->stepContacts.empty()) return;
    struct Acc {
        BodyHandle a, b;
        glm::dvec3 point{0};
        glm::dvec3 normal{0};
        int n = 0;
    };
    std::unordered_map<uint64_t, Acc> pairs;
    for (const StepContact& c : m->stepContacts) {
        if (c.depth < -0.001f) continue;  // nur vorausschauend, berührt noch nicht
        uint64_t key = (uint64_t(c.a) << 32) | c.b;
        Acc& acc = pairs[key];
        acc.a = c.a;
        acc.b = c.b;
        acc.point += c.point;
        acc.normal += glm::dvec3(c.normal);
        ++acc.n;
    }
    const double g = 9.81;
    const BodyLockInterfaceNoLock& li = m->system->GetBodyLockInterfaceNoLock();
    for (auto& [key, acc] : pairs) {
        BodyLockWrite la(li, toId(acc.a)), lb(li, toId(acc.b));
        if (!la.Succeeded() || !lb.Succeeded()) continue;
        Body& A = la.GetBody();
        Body& B = lb.GetBody();
        const int ma = materialOf(A), mb = materialOf(B);
        if (ma < 0 || mb < 0) continue;
        const double crr = m->rolling[ma * m->listener.nMat + mb];
        if (crr <= 0) continue;
        const glm::dvec3 n = glm::length(acc.normal) > 1e-9 ? glm::normalize(acc.normal) : glm::dvec3(0, 1, 0);
        const glm::dvec3 p = acc.point / double(acc.n);
        for (int side = 0; side < 2; ++side) {
            Body& X = side == 0 ? A : B;
            Body& O = side == 0 ? B : A;
            if (!X.IsDynamic() || !X.IsActive()) continue;
            const glm::dvec3 support = side == 0 ? -n : n;  // Kontaktkraft drückt X von O weg
            const double up = support.y;
            if (up <= 0.05) continue;  // nur Auflagen von unten tragen das Gewicht
            const MotionProperties* mp = X.GetMotionProperties();
            const double invM = mp->GetInverseMass();
            if (invM <= 0) continue;
            const double N = up * g / invM;
            // Hebel: Höhe des Schwerpunkts über der Auflage (bei Kugel und Rad = Radius)
            const glm::dvec3 lever = p - fromR(X.GetCenterOfMassPosition());
            const double arm = std::abs(glm::dot(lever, support));
            glm::dvec3 w = glm::dvec3(fromV(X.GetAngularVelocity())) - (O.IsDynamic() ? glm::dvec3(fromV(O.GetAngularVelocity())) : glm::dvec3(0));
            const glm::dvec3 wSpin = support * glm::dot(w, support);
            const glm::dvec3 wRoll = w - wSpin;
            const Mat44 invI = X.GetInverseInertia();
            auto brake = [&](const glm::dvec3& wPart, double torque) {
                const double len = glm::length(wPart);
                if (len < 1e-5 || torque <= 0) return;
                const glm::dvec3 dir = wPart / len;
                glm::dvec3 L = -dir * (torque * double(dt));  // Drehimpuls, der abgebaut wird
                const glm::dvec3 dw = glm::dvec3(fromV(invI.Multiply3x3(toV(L))));
                const double dec = -glm::dot(dw, dir);
                if (dec > len) L *= len / dec;  // nicht umkehren, nur bis zum Stillstand
                X.AddAngularImpulse(toV(L));
                if (O.IsDynamic() && O.IsActive()) O.AddAngularImpulse(toV(-L));
            };
            brake(wRoll, crr * N * arm);
            brake(wSpin, 0.5 * crr * N * std::max(arm, 0.005));  // Bohrreibung (Drehen auf der Stelle)
        }
    }
}

void Physics::step(float frameDt, const std::function<void(float)>& preStep, const std::function<void(float)>& postStep) {
    const float dt = fixedDt();
    m->accum += std::min(frameDt, 0.1f);
    int n = 0;
    m->stats.frameMs = 0;
    m->stats.steps = 0;
    m->listener.maxPenetration = 0.0f;
    const auto frameStart = std::chrono::steady_clock::now();
    while (m->accum >= dt && n < 10) {
        if (preStep) preStep(dt);
        {
            std::lock_guard<std::mutex> lock(m->listener.mutex);
            m->listener.current.clear();
        }
        m->listener.contactPoints = 0;
        m->listener.count = 0;
        m->impulses.count = 0;
        // Gewicht der Figuren als stetige Kraft auf das Teil unter den Füßen. Jolt gäbe es einmal pro Bild als Stoß:
        // ein leichtes Teil schlägt davon auf den Boden, prallt zurück und wirft die Figur hoch.
        const Vec3 gravity = m->system->GetGravity();
        for (const auto& entry : m->characters) {
            const CharacterVirtual& c = *entry.second.c;
            const float down = c.GetGroundNormal().Dot(gravity);
            if (!c.GetGroundBodyID().IsInvalid() && down < 0.0f)
                m->system->GetBodyInterface().AddForce(c.GetGroundBodyID(), -(kCharMass * down / gravity.Length()) * gravity,
                                                       c.GetGroundPosition());
        }
        auto t0 = std::chrono::steady_clock::now();
        {
            PROFILE("physics.jolt");
            m->system->Update(dt, 1, m->temp.get(), m->jobs.get());
        }
        const uint32_t nc = std::min<uint32_t>(m->listener.count.load(), (uint32_t)m->listener.buffer.size());
        m->stepContacts.assign(m->listener.buffer.begin(), m->listener.buffer.begin() + nc);
        const uint32_t ni = std::min<uint32_t>(m->impulses.count.load(), (uint32_t)m->impulses.buffer.size());
        m->stepImpulses.assign(m->impulses.buffer.begin(), m->impulses.buffer.begin() + ni);
        applyRollingResistance(dt);
        m->stats.frameMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        m->stats.steps++;
        m->stats.contacts = m->listener.contactPoints.load();
        m->stats.maxPenetration = m->listener.maxPenetration.load();
        m->stats.activeBodies = (int)m->system->GetNumActiveBodies(EBodyType::RigidBody);
        {
            std::lock_guard<std::mutex> lock(m->listener.mutex);
            m->lastContacts = m->listener.current;
        }
        if (postStep) postStep(dt);
        m->accum -= dt;
        ++n;
        // Höchstens ca. 20 ms Physik je Bild: Schafft der Rechner die 240 Schritte/s nicht (sehr viele bewegte Teile,
        // Debug-Build), würde jedes Bild länger und das nächste müsste noch mehr Schritte nachholen
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count() > 20.0) break;
    }
    // Rechner zu langsam: lieber Zeitlupe als Spirale. Ein kleiner Rückstand (ein einzelnes langsames Bild) wird nachgeholt.
    m->accum = std::min(m->accum, 4.0f * dt);
}

const PhysicsStats& Physics::stats() const { return m->stats; }

const std::vector<StepContact>& Physics::stepContacts() const { return m->stepContacts; }

const std::vector<ContactImpulse>& Physics::stepImpulses() const { return m->stepImpulses; }

void Physics::setBoreRadius(BodyHandle h, int index, double radius) {
    if (h == kNoBody) return;
    BodyLockRead lock(m->system->GetBodyLockInterface(), toId(h));
    if (!lock.Succeeded()) return;
    const Shape* s = lock.GetBody().GetShape();
    if (s->GetSubType() != EShapeSubType::StaticCompound) return;
    const StaticCompoundShape* c = static_cast<const StaticCompoundShape*>(s);
    for (uint i = 0; i < c->GetNumSubShapes(); ++i) {
        const Shape* sub = c->GetSubShape(i).mShape;
        if (sub->GetSubType() != BoreShape::kSubType) continue;
        const BoreShape* b = static_cast<const BoreShape*>(sub);
        if (b->index() == index) b->setRadius(float(radius));
    }
}

void Physics::invalidateContacts(BodyHandle h) {
    if (h != kNoBody) m->system->GetBodyInterface().InvalidateContactCache(toId(h));
}

void Physics::setSolverBoost(BodyHandle h, bool on) {
    if (h == kNoBody) return;
    BodyLockWrite lock(m->system->GetBodyLockInterface(), toId(h));
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return;
    MotionProperties* mp = lock.GetBody().GetMotionProperties();
    mp->SetNumVelocityStepsOverride(on ? 40 : 0);
    mp->SetNumPositionStepsOverride(on ? 12 : 0);
}

void Physics::setLubricant(BodyHandle h, int lubricant) {
    if (h == kNoBody) return;
    BodyLockWrite lock(m->system->GetBodyLockInterface(), toId(h));
    if (!lock.Succeeded()) return;
    Body& b = lock.GetBody();
    b.SetUserData(packUser(uint32_t(b.GetUserData() & 0xffffffffu), materialOf(b), lubricant));
}

void Physics::setSpeculativeDistance(float d) {
    PhysicsSettings s = m->system->GetPhysicsSettings();
    s.mSpeculativeContactDistance = d;
    m->system->SetPhysicsSettings(s);
}

bool Physics::bodyState(BodyHandle h, BodyState& out) const {
    if (h == kNoBody) return false;
    BodyLockRead lock(m->system->GetBodyLockInterface(), toId(h));
    if (!lock.Succeeded()) return false;
    const Body& b = lock.GetBody();
    out.dynamic = b.IsDynamic();
    out.active = b.IsActive();
    RVec3 c = b.GetCenterOfMassPosition();
    out.com = glm::dvec3(c.GetX(), c.GetY(), c.GetZ());
    out.vel = glm::dvec3(fromV(b.GetLinearVelocity()));
    out.angVel = glm::dvec3(fromV(b.GetAngularVelocity()));
    if (out.dynamic) {
        const MotionProperties* mp = b.GetMotionProperties();
        out.mass = mp->GetInverseMass() > 0 ? 1.0 / mp->GetInverseMass() : 0.0;
        Vec3 inv = mp->GetInverseInertiaDiagonal();
        out.inertia = glm::dvec3(inv.GetX() > 0 ? 1.0 / inv.GetX() : 0.0, inv.GetY() > 0 ? 1.0 / inv.GetY() : 0.0,
                                 inv.GetZ() > 0 ? 1.0 / inv.GetZ() : 0.0);
        Mat44 ii = b.GetInverseInertia();
        for (int c = 0; c < 3; ++c) {
            Vec3 col = ii.GetColumn3(c);
            out.invInertiaWorld[c] = glm::dvec3(col.GetX(), col.GetY(), col.GetZ());
        }
    }
    return true;
}

void Physics::watchContacts(BodyHandle h) { m->listener.watch = h; }

void Physics::setNoContactPairs(std::vector<uint64_t> pairs) {
    std::sort(pairs.begin(), pairs.end());
    Listener& l = m->listener;
    std::lock_guard<std::mutex> lock(l.noContactMutex);
    if (pairs == l.noContact) return;
    l.noContact = std::move(pairs);
    l.anyNoContact = !l.noContact.empty();
}

std::vector<ContactInfo> Physics::watchedContacts() const { return m->lastContacts; }

std::vector<ImpactInfo> Physics::takeImpacts() {
    std::lock_guard<std::mutex> lock(m->listener.impactMutex);
    std::vector<ImpactInfo> r;
    r.swap(m->listener.impacts);
    return r;
}

// ============================================================================ Abfragen

std::vector<OverlapHit> Physics::overlaps(BodyHandle h, double minDepth, const std::vector<BodyHandle>& ignore) const {
    std::vector<OverlapHit> out;
    if (h == kNoBody) return out;
    RefConst<Shape> shape;
    RMat44 com;
    {
        BodyLockRead lock(m->system->GetBodyLockInterface(), toId(h));
        if (!lock.Succeeded()) return out;
        shape = lock.GetBody().GetShape();
        com = lock.GetBody().GetCenterOfMassTransform();
    }
    CollideShapeSettings cs;
    cs.mMaxSeparationDistance = minDepth < 0 ? float(-minDepth) : 0.0f;
    cs.mActiveEdgeMode = EActiveEdgeMode::CollideWithAll;
    cs.mBackFaceMode = EBackFaceMode::CollideWithBackFaces;
    AllHitCollisionCollector<CollideShapeCollector> col;
    IgnoreMultipleBodiesFilter filter;
    filter.IgnoreBody(toId(h));
    for (BodyHandle b : ignore) filter.IgnoreBody(toId(b));
    m->system->GetNarrowPhaseQuery().CollideShape(shape, Vec3::sOne(), com, cs, com.GetTranslation(), col, {}, {}, filter);
    for (const CollideShapeResult& r : col.mHits) {
        if (r.mPenetrationDepth <= minDepth) continue;
        BodyHandle b = r.mBodyID2.GetIndexAndSequenceNumber();
        Vec3 ax = r.mPenetrationAxis.LengthSq() > 1e-20f ? -r.mPenetrationAxis.Normalized() : Vec3::sAxisY();
        auto it = std::find_if(out.begin(), out.end(), [&](const OverlapHit& o) { return o.body == b; });
        if (it == out.end()) out.push_back({b, (double)r.mPenetrationDepth, glm::dvec3(fromV(ax))});
        else if (r.mPenetrationDepth > it->depth) *it = {b, (double)r.mPenetrationDepth, glm::dvec3(fromV(ax))};
    }
    return out;
}

SweepHit Physics::sweep(BodyHandle h, const glm::dvec3& delta, const std::vector<BodyHandle>& ignore) const {
    SweepHit res;
    const double len = glm::length(delta);
    if (h == kNoBody || len < 1e-9) return res;
    RefConst<Shape> shape;
    RMat44 com;
    {
        BodyLockRead lock(m->system->GetBodyLockInterface(), toId(h));
        if (!lock.Succeeded()) return res;
        shape = lock.GetBody().GetShape();
        com = lock.GetBody().GetCenterOfMassTransform();
    }
    IgnoreMultipleBodiesFilter filter;
    filter.IgnoreBody(toId(h));
    for (BodyHandle b : ignore) filter.IgnoreBody(toId(b));
    // Schon berührte Körper: wegbewegen oder entlanggleiten ist erlaubt, hineinschieben nicht.
    // Steckt das Teil schon tiefer drin, darf es frei heraus.
    for (const OverlapHit& o : overlaps(h, -0.0002, ignore)) {
        if (o.depth > 0.001 || glm::dot(delta, o.axis) >= -1e-9 * len) {
            filter.IgnoreBody(toId(o.body));
            continue;
        }
        res.hit = true;
        res.fraction = 0.0;
        res.body = o.body;
        res.normal = o.axis;
        return res;
    }
    RShapeCast cast(shape, Vec3::sOne(), com, toV(delta));
    ShapeCastSettings ss;
    ss.mUseShrunkenShapeAndConvexRadius = true;
    ss.mBackFaceModeConvex = EBackFaceMode::IgnoreBackFaces;
    ClosestHitCollisionCollector<CastShapeCollector> col;
    m->system->GetNarrowPhaseQuery().CastShape(cast, ss, com.GetTranslation(), col, {}, {}, filter);
    if (!col.HadHit()) return res;
    res.hit = true;
    res.fraction = std::clamp((double)col.mHit.mFraction, 0.0, 1.0);
    res.body = col.mHit.mBodyID2.GetIndexAndSequenceNumber();
    Vec3 ax = col.mHit.mPenetrationAxis;
    res.normal = ax.LengthSq() > 1e-20f ? glm::dvec3(fromV(-ax.Normalized())) : -delta / len;
    return res;
}

// ============================================================================ Spielfigur

static constexpr float kCharHeight = 1.80f;
static constexpr float kCharRadius = 0.30f;

void Physics::createCharacter(const glm::dvec3& feet, int who) {
    Ref<CharacterVirtualSettings> s = new CharacterVirtualSettings();
    float halfCyl = 0.5f * kCharHeight - kCharRadius;
    RefConst<Shape> capsule = RotatedTranslatedShapeSettings(Vec3(0, 0.5f * kCharHeight, 0), Quat::sIdentity(),
                                                             new CapsuleShape(halfCyl, kCharRadius))
                                  .Create()
                                  .Get();
    s->mShape = capsule;
    s->mMaxSlopeAngle = DegreesToRadians(50.0f);
    s->mSupportingVolume = Plane(Vec3::sAxisY(), -kCharRadius);
    s->mMass = 0.0f;  // Gewicht auf den Untergrund bringt Physics::step als Kraft auf (kCharMass)
    s->mMaxStrength = 350.0f;  // so stark kann die Figur gegen Dinge drücken (N)
    s->mCharacterPadding = 0.02f;
    s->mPenetrationRecoverySpeed = 1.0f;
    s->mPredictiveContactDistance = 0.1f;
    s->mInnerBodyShape = capsule;
    s->mInnerBodyLayer = Layers::MOVING;
    m->characters[who] = {new CharacterVirtual(s, toR(feet), Quat::sIdentity(), 0, m->system.get())};
}

void Physics::destroyCharacter(int who) { m->characters.erase(who); }
bool Physics::hasCharacter(int who) const { return m->character(who) != nullptr; }

void Physics::updateCharacter(float dt, const glm::vec3& wish, bool jump, BodyHandle ignore, int who, float swim) {
    auto it = m->characters.find(who);
    if (it == m->characters.end()) return;
    CharacterVirtual* c = it->second.c;
    dt = std::clamp(dt, 1e-4f, 1.0f / 30.0f);
    c->UpdateGroundVelocity();
    Vec3 up = c->GetUp();
    Vec3 vertical = c->GetLinearVelocity().Dot(up) * up;
    const bool onGround = c->GetGroundState() == CharacterBase::EGroundState::OnGround;

    // Bodengeschwindigkeit wie über die Beine gefedert übernehmen. Ungefiltert schleudert jedes Zucken des Teils unter
    // den Füßen (Rückprall vom Boden unter dem Gewicht der Figur, Kippen, Stöße) sie hoch in die Luft.
    Vec3 target = Vec3::sZero();
    if (onGround) {
        target = c->GetGroundVelocity();
        // Ein leichtes Teil kann die schwere Figur kaum anheben: aufwärts nur ihr Anteil am gemeinsamen Impuls
        float lift = target.Dot(up);
        if (lift > 0.0f) {
            BodyLockRead lock(m->system->GetBodyLockInterface(), c->GetGroundBodyID());
            if (lock.Succeeded() && lock.GetBody().IsDynamic()) {
                float invMass = lock.GetBody().GetMotionProperties()->GetInverseMass();
                float share = invMass > 0.0f ? 1.0f / (1.0f + kCharMass * invMass) : 1.0f;
                target -= (1.0f - share) * lift * up;
                lift *= share;
            }
            if (lift > 3.0f) target -= (lift - 3.0f) * up;  // höchstens ca. 0,45 m Schwung nach oben
        }
        float speed = target.Length();
        if (speed > 12.0f) target *= 12.0f / speed;
    }
    Vec3& ground = it->second.ground;
    Vec3 d = target - ground;
    Vec3 dUp = d.Dot(up) * up;
    ground += (d - dUp) * (1.0f - std::exp(-dt / 0.06f))  // waagrecht: schnell mitgenommen
              + dUp * (1.0f - std::exp(-dt / 0.15f));     // senkrecht: Knie federn Rucke ab

    Vec3 v;
    const bool swimming = swim > 0.5f;
    bool towardsGround = (vertical - ground).Dot(up) < 0.1f;
    if (swimming) {
        // Schwimmen: Auftrieb (Körper mit Luft in der Lunge, im Gleichgewicht ragt der Kopf heraus), das Wasser bremst und
        // die Figur gleitet in die gewünschte Richtung, auch auf und ab
        v = c->GetLinearVelocity();
        v += m->system->GetGravity() * dt * (1.0f - 1.06f * std::min(swim, 1.0f));
        v += (Vec3(wish.x, wish.y, wish.z) - v) * (1.0f - std::exp(-dt * 2.5f));
    } else {
        if (onGround && towardsGround) {
            v = ground;
            if (jump) v += 4.6f * up;  // ca. 1,1 m Sprunghöhe
        } else {
            v = vertical;
        }
        v += m->system->GetGravity() * dt;
        v += Vec3(wish.x, 0, wish.z);
    }
    c->SetLinearVelocity(v);

    CharacterVirtual::ExtendedUpdateSettings us;
    us.mWalkStairsStepUp = Vec3(0, 0.35f, 0);
    if (swimming) us.mStickToFloorStepDown = Vec3::sZero();  // im Wasser nicht an den Boden ziehen
    IgnoreSingleBodyFilter ignoreFilter(ignore == kNoBody ? BodyID() : toId(ignore));
    BodyFilter defaultFilter;
    const BodyFilter& bf = ignore == kNoBody ? defaultFilter : static_cast<const BodyFilter&>(ignoreFilter);
    c->ExtendedUpdate(dt, swimming ? Vec3::sZero() : m->system->GetGravity(), us, m->system->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                      m->system->GetDefaultLayerFilter(Layers::MOVING), bf, {}, *m->temp);
}

glm::dvec3 Physics::characterPosition(int who) const {
    const CharacterVirtual* c = m->character(who);
    return c ? fromR(c->GetPosition()) : glm::dvec3(0);
}

void Physics::setCharacterPosition(const glm::dvec3& feet, int who) {
    CharacterVirtual* c = m->character(who);
    if (!c) return;
    c->SetPosition(toR(feet));
    c->SetLinearVelocity(Vec3::sZero());
    m->characters[who].ground = Vec3::sZero();
}

bool Physics::characterOnGround(int who) const {
    const CharacterVirtual* c = m->character(who);
    return c && c->GetGroundState() == CharacterBase::EGroundState::OnGround;
}
