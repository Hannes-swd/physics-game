#include "BoreShape.h"

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace JPH;

namespace {

struct P2 {
    float x = 0, z = 0;
};

// Querschnitt einer konvexen Form in der waagerechten Ebene y = const (Raum der Bohrung, Achse y):
// Ellipse (Zylinder, Kugel, Kapsel) oder Punktmenge (Vielflächner).
struct Section {
    bool valid = false;
    bool points = false;
    P2 c;                    // Ellipse: Mitte
    float a = 0, b = 0;      // Halbachsen: a entlang m, b quer dazu
    P2 m{1, 0};
    std::vector<P2>* pts = nullptr;

    P2 support(P2 u) const {
        if (points) {
            P2 best = (*pts)[0];
            float bd = best.x * u.x + best.z * u.z;
            for (const P2& p : *pts) {
                const float d = p.x * u.x + p.z * u.z;
                if (d > bd) {
                    bd = d;
                    best = p;
                }
            }
            return best;
        }
        const float um = u.x * m.x + u.z * m.z, up = -u.x * m.z + u.z * m.x;
        const float s = std::sqrt(a * a * um * um + b * b * up * up);
        if (s < 1e-12f) return c;
        const float km = a * a * um / s, kp = b * b * up / s;
        return {c.x + km * m.x - kp * m.z, c.z + km * m.z + kp * m.x};
    }
    // kleinster Abstand von der Bohrungsachse (Punktmenge: über die Ecken)
    float minRadius() const {
        if (points) {
            float m = 1e30f;
            for (const P2& p : *pts) m = std::min(m, p.x * p.x + p.z * p.z);
            return std::sqrt(m);
        }
        return std::max(0.0f, std::sqrt(c.x * c.x + c.z * c.z) - std::max(a, b));
    }
    // Punkt, der am weitesten von der Bohrungsachse entfernt ist
    P2 farthest() const {
        if (points) {
            P2 best = (*pts)[0];
            float bd = -1;
            for (const P2& p : *pts) {
                const float d = p.x * p.x + p.z * p.z;
                if (d > bd) {
                    bd = d;
                    best = p;
                }
            }
            return best;
        }
        const float l = std::sqrt(c.x * c.x + c.z * c.z);
        return l > 1e-9f ? support({c.x / l, c.z / l}) : support(m);
    }
};

// Die konvexe Form im Raum der Bohrung
struct Geo {
    enum Kind { None, Poly, Cyl, Sph, Cap } kind = None;
    Vec3 C = Vec3::sZero(), A = Vec3::sAxisY();  // Mitte, Achse (Zylinder/Kapsel)
    float hh = 0, r = 0;
    std::vector<Vec3>* v = nullptr;
    std::vector<std::pair<uint16, uint16>>* e = nullptr;

    void addPrism(const Mat44& T, float halfH, float rad, int seg) {
        v->clear();
        e->clear();
        for (int i = 0; i < seg; ++i) {
            const float ang = 2.0f * JPH_PI * i / seg;
            v->push_back(T * Vec3(rad * std::cos(ang), -halfH, rad * std::sin(ang)));
            v->push_back(T * Vec3(rad * std::cos(ang), halfH, rad * std::sin(ang)));
        }
        for (int i = 0; i < seg; ++i) {
            const uint16 a0 = uint16(2 * i), a1 = uint16(2 * i + 1), b0 = uint16(2 * ((i + 1) % seg)), b1 = uint16(2 * ((i + 1) % seg) + 1);
            e->push_back({a0, a1});
            e->push_back({a0, b0});
            e->push_back({a1, b1});
        }
        kind = Poly;
    }

    bool init(const Shape* s, Vec3Arg scale, Mat44Arg T) {
        static thread_local std::vector<Vec3> tv;
        static thread_local std::vector<std::pair<uint16, uint16>> te;
        v = &tv;
        e = &te;
        C = T.GetTranslation();
        A = T.GetAxisY().Normalized();
        switch (s->GetSubType()) {
        case EShapeSubType::Sphere:
            kind = Sph;
            r = static_cast<const SphereShape*>(s)->GetRadius() * std::abs(scale.GetX());
            return true;
        case EShapeSubType::Cylinder: {
            const CylinderShape* c = static_cast<const CylinderShape*>(s);
            hh = c->GetHalfHeight() * std::abs(scale.GetY());
            r = c->GetRadius() * std::abs(scale.GetX());
            if (std::abs(A.GetY()) >= 0.2f) {
                kind = Cyl;
            } else {  // liegt fast quer zur Bohrung: als Vieleck-Prisma
                addPrism(T, hh, r, 24);
            }
            return true;
        }
        case EShapeSubType::Capsule: {
            const CapsuleShape* c = static_cast<const CapsuleShape*>(s);
            hh = c->GetHalfHeightOfCylinder() * std::abs(scale.GetY());
            r = c->GetRadius() * std::abs(scale.GetX());
            kind = Cap;
            return true;
        }
        case EShapeSubType::Box: {
            const Vec3 he = static_cast<const BoxShape*>(s)->GetHalfExtent() * scale.Abs();
            tv.clear();
            te.clear();
            for (int i = 0; i < 8; ++i)
                tv.push_back(T * Vec3((i & 1) ? he.GetX() : -he.GetX(), (i & 2) ? he.GetY() : -he.GetY(), (i & 4) ? he.GetZ() : -he.GetZ()));
            for (int i = 0; i < 8; ++i)
                for (int bit = 1; bit < 8; bit <<= 1)
                    if (!(i & bit)) te.push_back({uint16(i), uint16(i | bit)});
            kind = Poly;
            return true;
        }
        case EShapeSubType::ConvexHull: {
            const ConvexHullShape* h = static_cast<const ConvexHullShape*>(s);
            tv.clear();
            te.clear();
            const uint np = h->GetNumPoints();
            if (np > 65000) return false;
            for (uint i = 0; i < np; ++i) tv.push_back(T * (scale * h->GetPoint(i)));
            uint idx[256];
            for (uint f = 0; f < h->GetNumFaces(); ++f) {
                const uint n = h->GetFaceVertices(f, 256, idx);
                for (uint i = 0; i < n; ++i) {
                    const uint a = idx[i], b = idx[(i + 1) % n];
                    if (a < b) te.push_back({uint16(a), uint16(b)});
                }
            }
            kind = Poly;
            return true;
        }
        default:
            return false;
        }
    }

    void extent(float& lo, float& hi) const {
        switch (kind) {
        case Poly:
            lo = 1e30f;
            hi = -1e30f;
            for (const Vec3& p : *v) {
                lo = std::min(lo, p.GetY());
                hi = std::max(hi, p.GetY());
            }
            return;
        case Cyl: {
            const float ay = std::abs(A.GetY()), ext = hh * ay + r * std::sqrt(std::max(0.0f, 1.0f - ay * ay));
            lo = C.GetY() - ext;
            hi = C.GetY() + ext;
            return;
        }
        case Sph:
            lo = C.GetY() - r;
            hi = C.GetY() + r;
            return;
        case Cap: {
            const float ext = hh * std::abs(A.GetY()) + r;
            lo = C.GetY() - ext;
            hi = C.GetY() + ext;
            return;
        }
        default:
            lo = 1e30f;
            hi = -1e30f;
        }
    }

    Section section(float y) const {
        static thread_local std::vector<P2> sp;
        Section s;
        switch (kind) {
        case Poly: {
            sp.clear();
            for (const Vec3& p : *v)
                if (std::abs(p.GetY() - y) < 1e-7f) sp.push_back({p.GetX(), p.GetZ()});
            for (const auto& [ia, ib] : *e) {
                const Vec3& a = (*v)[ia];
                const Vec3& b = (*v)[ib];
                const float da = a.GetY() - y, db = b.GetY() - y;
                if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
                    const float t = da / (da - db);
                    sp.push_back({a.GetX() + t * (b.GetX() - a.GetX()), a.GetZ() + t * (b.GetZ() - a.GetZ())});
                }
            }
            if (sp.empty()) return s;
            s.points = true;
            s.pts = &sp;
            s.valid = true;
            return s;
        }
        case Sph: {
            const float dy = y - C.GetY();
            if (std::abs(dy) >= r) return s;
            s.c = {C.GetX(), C.GetZ()};
            s.a = s.b = std::sqrt(r * r - dy * dy);
            s.valid = true;
            return s;
        }
        case Cyl:
        case Cap: {
            const float ay = A.GetY();
            if (std::abs(ay) < 0.2f) {  // Kapsel quer: Kreise entlang der Achse als Punktmenge
                sp.clear();
                for (int i = 0; i <= 6; ++i) {
                    const Vec3 q = C + A * (hh * (-1.0f + i / 3.0f));
                    const float dy = y - q.GetY();
                    if (std::abs(dy) >= r) continue;
                    const float rad = std::sqrt(r * r - dy * dy);
                    for (int k = 0; k < 12; ++k) {
                        const float ang = 2.0f * JPH_PI * k / 12;
                        sp.push_back({q.GetX() + rad * std::cos(ang), q.GetZ() + rad * std::sin(ang)});
                    }
                }
                if (sp.empty()) return s;
                s.points = true;
                s.pts = &sp;
                s.valid = true;
                return s;
            }
            const float t = (y - C.GetY()) / ay;
            const float tc = std::clamp(t, -hh, hh);
            const Vec3 q = C + A * tc;
            float rad = r;
            if (kind == Cyl) {
                const float lim = hh + r * std::sqrt(std::max(0.0f, 1.0f - ay * ay)) / std::abs(ay);
                if (std::abs(t) > lim) return s;
            } else {
                const float dy = y - q.GetY();
                if (t != tc) {  // im Halbkugel-Ende
                    if (std::abs(dy) >= r) return s;
                    rad = std::sqrt(r * r - dy * dy);
                }
            }
            s.c = {q.GetX(), q.GetZ()};
            s.b = rad;
            s.a = (t == tc) ? rad / std::abs(ay) : rad;
            const float mx = A.GetX(), mz = A.GetZ(), ml = std::sqrt(mx * mx + mz * mz);
            if (ml > 1e-6f) s.m = {mx / ml, mz / ml};
            s.valid = true;
            return s;
        }
        default:
            return s;
        }
    }
};

inline float len2(P2 p) { return std::sqrt(p.x * p.x + p.z * p.z); }

void collideConvexVsBore(const Shape* inShape1, const Shape* inShape2, Vec3Arg inScale1, Vec3Arg, Mat44Arg inCenterOfMassTransform1,
                         Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator& inSubShapeIDCreator1,
                         const SubShapeIDCreator& inSubShapeIDCreator2, const CollideShapeSettings& inCollideShapeSettings,
                         CollideShapeCollector& ioCollector, const ShapeFilter&) {
    const BoreShape* bore = static_cast<const BoreShape*>(inShape2);
    const Mat44 toBore = inCenterOfMassTransform2.InversedRotationTranslation() * inCenterOfMassTransform1;
    BoreShape::Contact c[24];
    int n = 0;
    if (!bore->contacts(inShape1, inScale1, toBore, inCollideShapeSettings.mMaxSeparationDistance, c, n)) return;
    // Im Physikschritt (große Vorausschau) die Wand um die Eindring-Toleranz näher melden, bei Abfragen nicht
    const float comp = inCollideShapeSettings.mMaxSeparationDistance >= 0.005f ? BoreShape::sSlop : 0.0f;
    for (int i = 0; i < n; ++i) {
        c[i].depth += comp;
        c[i].onWall -= c[i].axis * comp;
        CollideShapeResult r(inCenterOfMassTransform2 * c[i].onConvex, inCenterOfMassTransform2 * c[i].onWall,
                             inCenterOfMassTransform2.Multiply3x3(c[i].axis), c[i].depth, inSubShapeIDCreator1.GetID(),
                             inSubShapeIDCreator2.PushID(c[i].id, BoreShape::kIdBits).GetID(),
                             TransformedShape::sGetBodyID(ioCollector.GetContext()));
        ioCollector.AddHit(r);
        if (ioCollector.ShouldEarlyOut()) break;
    }
}

// Form entlang eines Wegs schieben (Bau-Modus, schnelle Teile): erste Berührung mit der Wand durch Abtasten + Halbieren
void castConvexVsBore(const ShapeCast& inShapeCast, const ShapeCastSettings&, const Shape* inShape, Vec3Arg, const ShapeFilter&,
                      Mat44Arg inCenterOfMassTransform2, const SubShapeIDCreator& inSubShapeIDCreator1,
                      const SubShapeIDCreator& inSubShapeIDCreator2, CastShapeCollector& ioCollector) {
    const BoreShape* bore = static_cast<const BoreShape*>(inShape);
    auto deepest = [&](float t, BoreShape::Contact& best) {
        const Mat44 m = inShapeCast.mCenterOfMassStart.PostTranslated(inShapeCast.mDirection * t);
        BoreShape::Contact c[24];
        int n = 0;
        float d = -1e30f;
        if (!bore->contacts(inShapeCast.mShape, inShapeCast.mScale, m, 0.0f, c, n)) return d;
        for (int i = 0; i < n; ++i)
            if (c[i].depth > d) {
                d = c[i].depth;
                best = c[i];
            }
        return d;
    };
    const float tol = 1e-5f;
    BoreShape::Contact hit{};
    float frac = 0.0f;
    const float d0 = deepest(0.0f, hit);
    if (d0 > tol) {
        BoreShape::Contact tmp{};
        if (deepest(0.02f, tmp) <= d0) return;  // bewegt sich heraus
        if (d0 <= -ioCollector.GetEarlyOutFraction()) return;
        ShapeCastResult r(0.0f, inCenterOfMassTransform2 * hit.onConvex, inCenterOfMassTransform2 * hit.onWall,
                          inCenterOfMassTransform2.Multiply3x3(hit.axis) * d0, false, inSubShapeIDCreator1.GetID(),
                          inSubShapeIDCreator2.PushID(hit.id, BoreShape::kIdBits).GetID(),
                          TransformedShape::sGetBodyID(ioCollector.GetContext()));
        ioCollector.AddHit(r);
        return;
    }
    const int steps = 16;
    float lo = 0.0f, hi = -1.0f;
    for (int i = 1; i <= steps; ++i) {
        const float t = float(i) / steps;
        BoreShape::Contact tmp{};
        if (deepest(t, tmp) > tol) {
            hi = t;
            break;
        }
        lo = t;
    }
    if (hi < 0) return;
    for (int it = 0; it < 14; ++it) {
        const float mid = 0.5f * (lo + hi);
        BoreShape::Contact tmp{};
        if (deepest(mid, tmp) > tol) hi = mid;
        else lo = mid;
    }
    frac = lo;
    if (frac >= ioCollector.GetEarlyOutFraction()) return;
    deepest(hi, hit);
    ShapeCastResult r(frac, inCenterOfMassTransform2 * hit.onConvex, inCenterOfMassTransform2 * hit.onWall,
                      inCenterOfMassTransform2.Multiply3x3(hit.axis).NormalizedOr(Vec3::sAxisY()), false, inSubShapeIDCreator1.GetID(),
                      inSubShapeIDCreator2.PushID(hit.id, BoreShape::kIdBits).GetID(), TransformedShape::sGetBodyID(ioCollector.GetContext()));
    ioCollector.AddHit(r);
}

void collideNothing(const Shape*, const Shape*, Vec3Arg, Vec3Arg, Mat44Arg, Mat44Arg, const SubShapeIDCreator&, const SubShapeIDCreator&,
                    const CollideShapeSettings&, CollideShapeCollector&, const ShapeFilter&) {}
void castNothing(const ShapeCast&, const ShapeCastSettings&, const Shape*, Vec3Arg, const ShapeFilter&, Mat44Arg, const SubShapeIDCreator&,
                 const SubShapeIDCreator&, CastShapeCollector&) {}

} // namespace

BoreShape::BoreShape(float radius, float halfLength, float band, bool openTop, bool openBottom, int index)
    : Shape(EShapeType::User1, kSubType), mRadius(radius), mHalfLength(halfLength), mBand(band), mOpenTop(openTop),
      mOpenBottom(openBottom), mIndex(index) {}

AABox BoreShape::GetLocalBounds() const {
    const float ro = mRadius + mBand;
    return AABox(Vec3(-ro, -mHalfLength, -ro), Vec3(ro, mHalfLength, ro));
}

const PhysicsMaterial* BoreShape::GetMaterial(const SubShapeID&) const { return PhysicsMaterial::sDefault; }

Vec3 BoreShape::GetSurfaceNormal(const SubShapeID& inSubShapeID, Vec3Arg p) const {
    SubShapeID rem;
    const uint id = inSubShapeID.PopID(kIdBits, rem);
    if (id >= 20) return -Vec3::sAxisY();
    if (id >= 16) return Vec3::sAxisY();
    const Vec3 radial(p.GetX(), 0, p.GetZ());
    return radial.LengthSq() > 1e-20f ? -radial.Normalized() : Vec3::sAxisX();
}

void BoreShape::GetSubmergedVolume(Mat44Arg, Vec3Arg, const Plane&, float& outTotalVolume, float& outSubmergedVolume, Vec3& outCenterOfBuoyancy
#ifdef JPH_DEBUG_RENDERER
                                   , RVec3Arg
#endif
                                   ) const {
    outTotalVolume = 0;
    outSubmergedVolume = 0;
    outCenterOfBuoyancy = Vec3::sZero();
}

void BoreShape::CollidePoint(Vec3Arg p, const SubShapeIDCreator& inSubShapeIDCreator, CollidePointCollector& ioCollector,
                             const ShapeFilter& inShapeFilter) const {
    if (!inShapeFilter.ShouldCollide(this, inSubShapeIDCreator.GetID())) return;
    const float r = std::sqrt(p.GetX() * p.GetX() + p.GetZ() * p.GetZ());
    if (r >= mRadius && r <= mRadius + mBand && std::abs(p.GetY()) <= mHalfLength) {
        CollidePointResult res;
        res.mBodyID = TransformedShape::sGetBodyID(ioCollector.GetContext());
        res.mSubShapeID2 = inSubShapeIDCreator.GetID();
        ioCollector.AddHit(res);
    }
}

void BoreShape::GetTrianglesStart(GetTrianglesContext&, const AABox&, Vec3Arg, QuatArg, Vec3Arg) const {}

bool BoreShape::contacts(const Shape* convex, Vec3Arg scale, Mat44Arg T, float maxSep, Contact* out, int& count) const {
    count = 0;
    Geo g;
    if (!g.init(convex, scale, T)) return false;
    const float R = mRadius, Ro = mRadius + mBand, h = mHalfLength, L = 2.0f * h;
    float ylo, yhi;
    g.extent(ylo, yhi);
    if (yhi < -h - maxSep || ylo > h + maxSep || yhi <= ylo) return true;

    // Kürzester Weg heraus über eine offene Stirnseite: nur für eine Spitze, die von außen in der Öffnung steckt
    // (oder kurz davor ist). Ein Stift, der ganz durchgeht oder ganz drin liegt, wird nur radial geführt.
    const float inf = 1e30f;
    const bool fromTop = mOpenTop && yhi > h && ylo > -h;
    const bool fromBottom = mOpenBottom && ylo < -h && yhi < h;
    const float exitUp = fromTop ? h - ylo : inf;       // Form nach oben heraus: Bohrung muss nach -y
    const float exitDown = fromBottom ? yhi + h : inf;  // Form nach unten heraus: Bohrung muss nach +y
    const float exitMin = std::min(exitUp, exitDown);
    const float tipDy = std::min(0.0002f, 0.05f * (yhi - ylo));

    auto rot = [](P2 d, int j) -> P2 {
        switch (j & 3) {
        case 0: return d;
        case 1: return {-d.z, d.x};
        case 2: return {-d.x, -d.z};
        default: return {d.z, -d.x};
        }
    };

    // Radiale Kontakte in bis zu 4 Querschnitten, je 4 Richtungen (tiefste Stelle, quer, gegenüber, quer)
    const int n = std::clamp(int(std::lround(L / (2.0f * R))) + 1, 2, 4);
    float lastY = -inf;
    bool tipAxial = false;
    for (int k = 0; k < n; ++k) {
        float y = -h + L * float(k) / float(n - 1);
        y = std::clamp(y, ylo + tipDy, yhi - tipDy);
        if (y < -h || y > h) continue;
        if (y - lastY < 0.25f * L / float(n - 1)) continue;
        lastY = y;
        const Section s = g.section(y);
        if (!s.valid) continue;
        P2 far = s.farthest();
        const float fl = len2(far);
        const P2 dir = fl > 1e-7f ? P2{far.x / fl, far.z / fl} : P2{1, 0};
        // Steckt die Form in der Bohrung (Stift), wird sie auch zurückgeholt, wenn sie bei einem harten Stoß tiefer als
        // die Ringzone in die Wand gedrückt wurde. Formen, die nur außen an der Ringzone liegen, nicht.
        const bool inside = s.minRadius() < R;
        for (int j = 0; j < 4; ++j) {
            const P2 p = s.support(rot(dir, j));
            const float r = len2(p);
            if (r < 1e-7f || (r > Ro && !inside)) continue;
            const float depth = r - R;
            if (depth < -maxSep) continue;
            if (depth > 0 && exitMin < depth) {  // über die Stirn ist es kürzer: dort anlegen
                tipAxial = true;
                continue;
            }
            Contact& c = out[count++];
            c.onConvex = Vec3(p.x, y, p.z);
            c.onWall = Vec3(p.x * R / r, y, p.z * R / r);
            c.axis = Vec3(p.x / r, 0, p.z / r);
            c.depth = depth;
            c.id = uint(k * 4 + j);
        }
    }

    // Stirnkontakte der Ringzone: Stiftspitze in oder kurz vor einer offenen Öffnung, Teile der Spitze über der Ringzone
    auto tip = [&](bool top) {
        const float depth = top ? h - ylo : yhi + h;  // > 0: Spitze steckt so tief in der Öffnung
        if (depth < -maxSep) return;
        // schon drin und radial geführt (außer die Spitze steckt erst so knapp in der Öffnung, dass noch kein
        // Querschnitt sie erfasst hat)
        const bool covered = top ? ylo + tipDy <= h : yhi - tipDy >= -h;
        if (depth > 0 && !tipAxial && covered) return;
        const float ys = top ? std::min(ylo + tipDy, yhi) : std::max(yhi - tipDy, ylo);
        const Section s = g.section(ys);
        if (!s.valid) return;
        P2 far = s.farthest();
        const float fl = len2(far);
        const P2 dir = fl > 1e-7f ? P2{far.x / fl, far.z / fl} : P2{1, 0};
        for (int j = 0; j < 4; ++j) {
            const P2 p = s.support(rot(dir, j));
            const float r = len2(p);
            if (r <= R || r > Ro) continue;
            if (depth > 0 && r - R <= depth) continue;  // radial kürzer: schon oben behandelt
            Contact& c = out[count++];
            c.onConvex = Vec3(p.x, top ? ylo : yhi, p.z);
            c.onWall = Vec3(p.x, top ? h : -h, p.z);
            c.axis = top ? -Vec3::sAxisY() : Vec3::sAxisY();
            c.depth = depth;
            c.id = uint((top ? 16 : 20) + j);
        }
    };
    if (fromTop) tip(true);
    if (fromBottom) tip(false);
    return true;
}

void BoreShape::sRegister() {
    ShapeFunctions& f = ShapeFunctions::sGet(kSubType);
    f.mConstruct = []() -> Shape* { return new BoreShape(0.01f, 0.01f, 0.001f, true, true, 0); };
    f.mColor = Color::sOrange;
    for (EShapeSubType s : sConvexSubShapeTypes) {
        CollisionDispatch::sRegisterCollideShape(s, kSubType, collideConvexVsBore);
        CollisionDispatch::sRegisterCastShape(s, kSubType, castConvexVsBore);
        CollisionDispatch::sRegisterCollideShape(kSubType, s, CollisionDispatch::sReversedCollideShape);
        CollisionDispatch::sRegisterCastShape(kSubType, s, CollisionDispatch::sReversedCastShape);
    }
    // Wand gegen Wand, Boden-Ebene, Netze: nichts (die Hüllen der Teile übernehmen das)
    for (EShapeSubType s : {kSubType, EShapeSubType::Plane, EShapeSubType::Mesh, EShapeSubType::HeightField, EShapeSubType::Empty,
                            EShapeSubType::SoftBody}) {
        CollisionDispatch::sRegisterCollideShape(s, kSubType, collideNothing);
        CollisionDispatch::sRegisterCollideShape(kSubType, s, collideNothing);
        CollisionDispatch::sRegisterCastShape(s, kSubType, castNothing);
        CollisionDispatch::sRegisterCastShape(kSubType, s, castNothing);
    }
}
