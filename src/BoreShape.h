// Exakt runde Bohrungswand als eigene Jolt-Form.
// Bohrungen werden in der Kollision sonst durch Vieleck-Hüllen nachgebildet: Ein Stift stößt dann an Ecken und
// Kanten, rattert beim Drehen und passt bei engem Spiel nicht hinein. Diese Form beschreibt die Wand als echten
// Kreiszylinder: Kontakte zwischen Stift und Lochwand werden genau berechnet (Abstand zur Wand, Normale radial),
// mit vorausschauenden Kontakten auf allen Seiten. Die Hüllen des Teils rund um die Bohrung liegen um "band" weiter
// außen; diese Ringzone (radius .. radius + band) gehört der Bohrungsform, auch ihre Stirnflächen an offenen Enden.
#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

class BoreShape final : public JPH::Shape {
public:
    JPH_OVERRIDE_NEW_DELETE

    static constexpr JPH::EShapeSubType kSubType = JPH::EShapeSubType::User1;
    static constexpr JPH::uint kIdBits = 5;  // Kontakt-Nummer (für den Warmstart der Kontakte)

    // Achse = lokale y-Achse, Mitte im Ursprung, Länge 2 * halfLength. openTop/openBottom: Ende bei +y/-y ist offen.
    BoreShape(float radius, float halfLength, float band, bool openTop, bool openBottom, int index);

    float radius() const { return mRadius; }
    float halfLength() const { return mHalfLength; }
    float band() const { return mBand; }
    int index() const { return mIndex; }
    // Abrieb: Wand weicht zurück (nur zwischen Physikschritten aufrufen)
    void setRadius(float r) const { mRadius = r; }

    static void sRegister();
    // Die Physik lässt Eindringen bis zu dieser Tiefe unkorrigiert ("slop"). Die Wand wird um so viel näher gemeldet,
    // damit der Stift unter Last an der echten Wand anliegt statt bis zu dieser Tiefe einzusinken.
    static inline float sSlop = 0.0f;

    JPH::AABox GetLocalBounds() const override;
    JPH::uint GetSubShapeIDBitsRecursive() const override { return kIdBits; }
    float GetInnerRadius() const override { return mBand; }
    JPH::MassProperties GetMassProperties() const override { return JPH::MassProperties(); }
    const JPH::PhysicsMaterial* GetMaterial(const JPH::SubShapeID&) const override;
    JPH::Vec3 GetSurfaceNormal(const JPH::SubShapeID& inSubShapeID, JPH::Vec3Arg inLocalSurfacePosition) const override;
    void GetSubmergedVolume(JPH::Mat44Arg, JPH::Vec3Arg, const JPH::Plane&, float& outTotalVolume, float& outSubmergedVolume,
                            JPH::Vec3& outCenterOfBuoyancy
#ifdef JPH_DEBUG_RENDERER
                            , JPH::RVec3Arg
#endif
                            ) const override;
#ifdef JPH_DEBUG_RENDERER
    void Draw(JPH::DebugRenderer*, JPH::RMat44Arg, JPH::Vec3Arg, JPH::ColorArg, bool, bool) const override {}
#endif
    bool CastRay(const JPH::RayCast&, const JPH::SubShapeIDCreator&, JPH::RayCastResult&) const override { return false; }
    void CastRay(const JPH::RayCast&, const JPH::RayCastSettings&, const JPH::SubShapeIDCreator&, JPH::CastRayCollector&,
                 const JPH::ShapeFilter& = {}) const override {}
    void CollidePoint(JPH::Vec3Arg inPoint, const JPH::SubShapeIDCreator& inSubShapeIDCreator, JPH::CollidePointCollector& ioCollector,
                      const JPH::ShapeFilter& inShapeFilter = {}) const override;
    void CollideSoftBodyVertices(JPH::Mat44Arg, JPH::Vec3Arg, const JPH::CollideSoftBodyVertexIterator&, JPH::uint, int) const override {}
    void GetTrianglesStart(GetTrianglesContext& ioContext, const JPH::AABox&, JPH::Vec3Arg, JPH::QuatArg, JPH::Vec3Arg) const override;
    int GetTrianglesNext(GetTrianglesContext&, int, JPH::Float3*, const JPH::PhysicsMaterial** = nullptr) const override { return 0; }
    Stats GetStats() const override { return Stats(sizeof(*this), 0); }
    float GetVolume() const override { return 0.0f; }

    // Ein Kontakt mit der Wand (alles im lokalen Raum der Bohrung)
    struct Contact {
        JPH::Vec3 onConvex;  // Punkt auf der anderen Form
        JPH::Vec3 onWall;    // Punkt auf der Wand bzw. Stirnfläche der Ringzone
        JPH::Vec3 axis;      // Richtung, in die die Bohrung muss, um frei zu kommen (von der anderen Form zur Wand)
        float depth;         // > 0 eingedrungen, < 0 Abstand (vorausschauend)
        JPH::uint id;        // 0..15 radial (Querschnitt * 4 + Richtung), 16..19 Stirn oben, 20..23 Stirn unten
    };
    // Kontakte einer konvexen Form (Transformation: Form -> Bohrung). Nur Kontakte mit depth > -maxSeparation.
    // Rückgabe: Anzahl (höchstens 24). false, wenn die Formart nicht unterstützt wird.
    bool contacts(const JPH::Shape* convex, JPH::Vec3Arg scale, JPH::Mat44Arg convexToBore, float maxSeparation, Contact* out,
                  int& count) const;

private:
    mutable float mRadius;
    float mHalfLength, mBand;
    bool mOpenTop, mOpenBottom;
    int mIndex;
};
