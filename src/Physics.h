// Dünne Hülle um Jolt Physics, damit der Rest des Spiels keine Jolt-Header braucht.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

using BodyHandle = uint32_t;
constexpr BodyHandle kNoBody = 0xffffffffu;

// Exakt runde Bohrungswand (lokal): Achse, Mitte, Radius, halbe Länge, Ringzone, offene Enden (+axis / -axis)
struct BoreDesc {
    glm::dvec3 center{0}, axis{0, 1, 0};
    double radius = 0, halfLength = 0, band = 0;
    bool openTop = true, openBottom = true;
};

// Exakter Zylinder als Teil der Form (lokal)
struct CylDesc {
    glm::dvec3 center{0}, axis{0, 1, 0};
    double radius = 0, halfLength = 0;
};

struct BodyDesc {
    std::vector<std::vector<glm::vec3>> hulls;  // konvexe Punktwolken im lokalen Raum
    std::vector<CylDesc> cylinders;             // exakte Zylinder (z.B. gedrehte Stifte)
    std::vector<BoreDesc> bores;                // exakte Bohrungswände
    int lubricant = 0;                          // Index in lubricants()
    // Exakte Grundform statt Hüllen (schneller und genauer): 1 Quader (Halbmaße), 2 Kugel (x = Radius),
    // 3 Zylinder entlang y (x = Radius, y = halbe Höhe)
    int primitive = 0;
    glm::dvec3 primSize{0};
    float density = 1000.0f;                    // kg/m³
    float massOverride = 0.0f;                  // > 0: Masse fest vorgeben
    glm::dvec3 position{0};
    glm::dquat rotation{1, 0, 0, 0};
    bool isStatic = false;
    int material = 0;                           // Index in materials(): Reibungspaare und Rollreibung
    float restitution = 0.2f;
    bool ccd = false;                           // kontinuierliche Kollision für kleine/schnelle Teile
    uint32_t userData = 0;                      // Teilnummer
};

struct HingeDesc {
    glm::dvec3 point{0};      // Weltpunkt auf der Achse
    glm::dvec3 axis{0, 1, 0}; // Weltrichtung der Achse
    bool motor = false;
    float motorRpm = 0.0f;
    float motorTorque = 10.0f;   // N·m
    float bearingFriction = 0.02f; // N·m
};

struct BodyState {
    double mass = 0;
    glm::dvec3 com{0};          // Schwerpunkt (Welt)
    glm::dvec3 vel{0}, angVel{0};
    glm::dvec3 inertia{0};      // Hauptträgheitsmomente (kg·m²)
    glm::dmat3 invInertiaWorld{0.0};  // inverser Trägheitstensor (Welt)
    bool active = false, dynamic = false;
};

// Aufprall (neuer Kontakt mit Annäherungsgeschwindigkeit)
struct ImpactInfo {
    BodyHandle a = kNoBody, b = kNoBody;
    glm::dvec3 point{0}, normal{0};  // Kontaktpunkt auf a, Normale von a nach b
    glm::dvec3 pointB{0};            // Kontaktpunkt auf b
    double speed = 0;   // Annäherungsgeschwindigkeit m/s
    double energy = 0;  // ½·µ·v² (µ = reduzierte Masse) in J
};

struct ContactInfo {
    glm::dvec3 point{0};
    glm::dvec3 normal{0};       // Richtung der Kontaktkraft auf den beobachteten Körper
    BodyHandle other = kNoBody;
    float friction = 0;         // verwendete Reibungszahl (Haft- oder Gleitreibung)
    float slip = 0;             // Gleitgeschwindigkeit m/s
};

// Kontakt im letzten Teilschritt (alle Paare mit mindestens einem beweglichen Körper)
struct StepContact {
    BodyHandle a = kNoBody, b = kNoBody;
    glm::dvec3 point{0};
    glm::vec3 normal{0};   // von a nach b
    float depth = 0;       // Eindringtiefe (m), negativ = noch Abstand (vorausschauender Kontakt)
};

// Verbindung zwischen zwei Körpern (a darf kNoBody sein = Welt)
struct JointDesc {
    int kind = 0;                          // 0 fest, 1 Scharnier, 2 Abstand (Feder/Seil)
    BodyHandle a = kNoBody, b = kNoBody;
    glm::dvec3 pointA{0}, pointB{0};       // Weltpunkte (bei fest/Scharnier gleich)
    glm::dvec3 axis{0, 1, 0};              // Scharnierachse (Welt)
    double minDist = 0, maxDist = 0;       // Abstand
    float stiffness = 0, damping = 0;      // Feder: N/m und N·s/m (0 = starr)
    bool motor = false;
    float motorRpm = 0, motorTorque = 0;
};

// Impuls eines Kontaktpunkts im letzten Teilschritt (aus dem Löser). Wirkt auf b; auf a entgegengesetzt.
struct ContactImpulse {
    BodyHandle a = kNoBody, b = kNoBody;
    glm::dvec3 point{0};
    glm::vec3 normal{0};          // von a nach b
    float normalImpulse = 0;      // N·s (Kraft = Impuls / Zeitschritt)
    glm::vec3 frictionImpulse{0}; // N·s
    float slip = 0;               // Gleitgeschwindigkeit an der Stelle (m/s)
};

struct PhysicsStats {
    double frameMs = 0;        // Rechenzeit der Physik im letzten Bild
    int steps = 0;             // Teilschritte im letzten Bild
    int contacts = 0;          // Kontaktpunkte im letzten Teilschritt
    float maxPenetration = 0;  // tiefstes Ineinanderstecken im letzten Bild (m)
    int activeBodies = 0;
};

// Ergebnis einer Bewegungsprüfung (Form entlang eines Wegs schieben)
struct SweepHit {
    bool hit = false;
    double fraction = 1.0;          // Anteil des Wegs bis zur Berührung
    BodyHandle body = kNoBody;      // getroffener Körper (kNoBody + hit = Boden)
    glm::dvec3 normal{0, 1, 0};     // Kontaktnormale, zeigt vom Hindernis weg
};

struct OverlapHit {
    BodyHandle body = kNoBody;
    double depth = 0;               // Eindringtiefe (m)
    glm::dvec3 axis{0};             // in diese Richtung muss der geprüfte Körper, um frei zu kommen
};

class Physics {
public:
    const PhysicsStats& stats() const;
    Physics();
    ~Physics();

    bool init();
    void shutdown();

    BodyHandle createBody(const BodyDesc& d);
    BodyHandle createGroundPlane(double y, int material, float restitution);  // (fast) unendliche Ebene
    BodyHandle groundBody() const;

    // Konvexe Stücke für die Kollision zusammenfassen: Paare, deren gemeinsame Hülle höchstens
    // "tolerance" m³ mehr Volumen hat als beide zusammen, werden verschmolzen (weniger, größere Hüllen = schneller).
    static double convexHullVolume(const std::vector<glm::vec3>& points);  // 0 bei Fehler
    // Ebenen der konvexen Hülle (xyz = Normale nach außen, w = Abstand: innen gilt n·x <= w)
    static bool hullPlanes(const std::vector<glm::dvec3>& points, std::vector<glm::dvec4>& planes);
    static std::vector<std::vector<glm::vec3>> simplifyHulls(std::vector<std::vector<glm::vec3>> hulls,
                                                              std::vector<double> volumes, double tolerance,
                                                              size_t targetCount = 0, double maxTolerance = 0,
                                                              double maxRelative = 0.10);
    void destroyBody(BodyHandle h);
    void setTransform(BodyHandle h, const glm::dvec3& pos, const glm::dquat& rot, bool activate);
    void getTransform(BodyHandle h, glm::dvec3& pos, glm::dquat& rot) const;
    void setVelocity(BodyHandle h, const glm::vec3& lin, const glm::vec3& ang);
    glm::vec3 linearVelocity(BodyHandle h) const;
    glm::vec3 angularVelocity(BodyHandle h) const;
    glm::vec3 pointVelocity(BodyHandle h, const glm::dvec3& p) const;
    float mass(BodyHandle h) const;
    glm::dvec3 centerOfMass(BodyHandle h) const;
    void addForceAt(BodyHandle h, const glm::vec3& f, const glm::dvec3& p);
    void addTorque(BodyHandle h, const glm::vec3& t);
    void addForceNoWake(BodyHandle h, const glm::vec3& f);  // am Schwerpunkt, weckt nicht auf
    void addTorqueNoWake(BodyHandle h, const glm::vec3& t);
    void addImpulseAt(BodyHandle h, const glm::vec3& j, const glm::dvec3& p);
    // Kontinuierliche Kollision (Weg prüfen) an/aus: nur für schnelle Teile nötig
    void setContinuous(BodyHandle h, bool on);
    void activate(BodyHandle h);
    void deactivate(BodyHandle h);
    bool isActive(BodyHandle h) const;
    bool bodyState(BodyHandle h, BodyState& out) const;
    // Kontakte eines Körpers mitschreiben (für den Physik-Inspektor)
    void watchContacts(BodyHandle h);
    // Paare von Teilen (Teilnummern, a < b als (a << 32) | b), die sich nicht berühren: ein steckengebliebenes Werkzeug
    // sitzt genau in seinem Abdruck, die Verbindung hält es (sonst drückt die Kontaktkorrektur es heraus)
    void setNoContactPairs(std::vector<uint64_t> pairs);
    void setSpeculativeDistance(float d);  // nur für Messungen
    void setLegacySolver(bool on);         // nur für Messungen: alte Einstellungen (eine Reibungszahl, schnelles Herausdrücken)
    std::vector<ContactInfo> watchedContacts() const;
    std::vector<ImpactInfo> takeImpacts();  // Aufpralle seit dem letzten Aufruf
    const std::vector<StepContact>& stepContacts() const;  // Kontakte im letzten Teilschritt
    const std::vector<ContactImpulse>& stepImpulses() const;  // Kontaktimpulse im letzten Teilschritt (aus dem Löser)
    // Bohrungswand nachführen (Abrieb): Radius der Bohrung Nummer index im Körper
    void setBoreRadius(BodyHandle h, int index, double radius);
    // Kontakte im nächsten Schritt neu berechnen statt sie bei kleinen Bewegungen weiterzuverwenden (Stift dreht in Bohrung)
    void invalidateContacts(BodyHandle h);
    // Mehr Löser-Durchläufe für die Gruppe dieses Körpers (Stift in Bohrung: Hebelkräfte, enge Passung)
    void setSolverBoost(BodyHandle h, bool on);
    void setLubricant(BodyHandle h, int lubricant);

    // ---------------------------------------------------------- Abfragen (Bau-Modus, Überlappung)
    // Form des Körpers um "delta" verschieben: erster Treffer. Körper, in denen er schon steckt, und "ignore" zählen nicht.
    SweepHit sweep(BodyHandle h, const glm::dvec3& delta, const std::vector<BodyHandle>& ignore) const;
    // Körper, in die dieser gerade hineinragt (tiefer als minDepth)
    std::vector<OverlapHit> overlaps(BodyHandle h, double minDepth, const std::vector<BodyHandle>& ignore) const;

    uint32_t createHinge(BodyHandle body, const HingeDesc& d);
    uint32_t createJoint(const JointDesc& d);
    // Kraft auf Körper b und Moment im letzten Teilschritt (Welt). Bei Abstand: force.x = Zugkraft.
    bool jointLoad(uint32_t id, glm::dvec3& force, glm::dvec3& torque) const;
    void destroyConstraint(uint32_t c);
    float hingeTorque(uint32_t c) const;   // aktuelles Motormoment (N·m)

    // Feste Teilschritte. preStep vor jedem Teilschritt (Kräfte), postStep danach (Messungen).
    void step(float frameDt, const std::function<void(float)>& preStep,
              const std::function<void(float)>& postStep = nullptr);
    float fixedDt() const { return 1.0f / 240.0f; }

    // Spielfiguren (Ego-Modus). who = 0: eigene Figur, weitere Nummern: Mitspieler im LAN
    void createCharacter(const glm::dvec3& feet, int who = 0);
    void destroyCharacter(int who = 0);
    bool hasCharacter(int who = 0) const;
    void updateCharacter(float dt, const glm::vec3& wishVelocity, bool jump, BodyHandle ignore, int who = 0);
    glm::dvec3 characterPosition(int who = 0) const;
    void setCharacterPosition(const glm::dvec3& feet, int who = 0);
    bool characterOnGround(int who = 0) const;

private:
    void applyRollingResistance(float dt);
    struct Impl;
    std::unique_ptr<Impl> m;
};
