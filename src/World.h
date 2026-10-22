// Die Spielwelt: alle Teile, ihre Geometrie, Physik-Körper und die Bearbeitungsoperationen.
// Unabhängig von Fenster/Grafik, damit alles auch per Befehl und ohne Fenster funktioniert.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <unordered_map>

#include "Chunks.h"
#include "Collision.h"
#include "Physics.h"
#include "Renderer.h"
#include "Solid.h"

enum class ObjKind : int { Solid = 0, Mesh = 1 };

struct ImportedMesh {
    std::vector<solid::Vertex> vertices;           // zentriert um den Schwerpunkt
    std::vector<std::vector<glm::vec3>> hulls;     // konvexe Kollisionshüllen
    double volume = 0;
    std::string file;
    double scale = 1.0;
};

// Wärme im Inneren eines Teils: ein grobes Gitter aus Zellen über die lokale Begrenzung (Heat.cpp)
struct HeatCell {
    float temp = 20.0f;      // °C
    float peak = 20.0f;      // höchste erreichte Temperatur (Anlauffarben, Zunder)
    float chr = 0.0f;        // Verkohlung/Zersetzung 0..1 (Holz wird schwarz)
    float fuel = 0.0f;       // brennbarer Anteil, der noch übrig ist 0..1
    float melt = 0.0f;       // geschmolzener Anteil 0..1
    float area = 0.0f;       // Oberfläche der Zelle zur Luft (m²), berechnet
    uint8_t inside = 0;      // Zelle enthält Material (berechnet)
    uint8_t burning = 0;     // brennt gerade
};

struct HeatGrid {
    glm::ivec3 dim{0};              // 0: noch nicht angelegt, das ganze Teil hat Umgebungstemperatur
    glm::dvec3 origin{0}, cell{0};  // Ecke (lokal) und Zellgröße je Achse (m)
    std::vector<HeatCell> cells;    // x schnell, dann y, dann z
    bool empty() const { return dim.x <= 0; }
    int index(int x, int y, int z) const { return x + dim.x * (y + dim.y * z); }
};

// Zustand einer Schmelze: Tropfen fällt, Pfütze liegt (fest oder flüssig)
enum class MeltForm : int { None = 0, Drop = 1, Puddle = 2 };

// Ein Feuer: zusammenhängende brennende Stellen (auch über mehrere Teile)
struct Fire {
    glm::dvec3 base{0};          // Mitte des Flammenfußes (Welt)
    double power = 0;            // freigesetzte Wärme (W)
    double diameter = 0;         // Flammenfuß (m)
    double height = 0;           // Flammenhöhe (m, Heskestad)
    double tempC = 0;            // mittlere Flammentemperatur
    double emissivity = 0;       // Strahlung der Flamme (dicke Flammen strahlen mehr)
    double soot = 0;             // Rauch 0..1
    struct Spot {
        glm::dvec3 pos{0};       // brennende Oberfläche (Welt)
        glm::vec3 normal{0, 1, 0};
        float size = 0.05f;      // Zellgröße (m)
        float power = 0;         // W
    };
    std::vector<Spot> spots;
};

// Alles, was ein Teil ausmacht (wird für Rückgängig und Speichern kopiert)
struct ObjectState {
    uint32_t id = 0;
    std::string name;
    int material = 0;
    ObjKind kind = ObjKind::Solid;
    solid::Solid solid;                               // lokal, Ursprung = Schwerpunkt
    std::shared_ptr<const ImportedMesh> imported;
    glm::dvec3 pos{0};
    glm::dquat rot{1, 0, 0, 0};
    bool fixed = false;                               // fest verankert (unbeweglich)
    int hingeAxis = -1;                               // -1 = keine Achse, 0..2 = lokale X/Y/Z
    float motorRpm = 0.0f;
    float motorTorque = 5.0f;
    std::string shape;                                // Beschreibung der Ausgangsform
    int prim = 0;                                     // Grundform für die Physik: 1 Quader, 2 Kugel, 3 Zylinder
    int collision = 0;                                // Kollisionsform: 0 automatisch, 1 genau, 2 eine Hülle
    bool unbreakable = false;                         // zerbricht nie (unabhängig von "breakage on")
    glm::dvec3 primSize{0};                           // Halbmaße / Radius / (Radius, halbe Höhe)
    int edits = 0;                                    // Anzahl Bearbeitungen (Sägen/Bohren)
    float seed = 0.0f;                                // Zufallswert für die Maserung
    // berechnete Kollisionsform (teuer), bleibt bei Rückgängig/Kopieren erhalten, solange die Form gleich ist
    std::shared_ptr<const CollisionShape> hullCache;
    int hullCacheMode = -1;
    int lubricant = 0;                                // Schmierstoff (Index in lubricants())
    std::vector<double> boreWear;                     // Abrieb je Bohrung: Wand weicht so weit zurück (m)
    HeatGrid heat;                                    // Temperatur, Verkohlung, Schmelze
    MeltForm meltForm = MeltForm::None;
};

struct Object : ObjectState {
    double volume = 0;     // m³
    double mass = 0;       // kg
    glm::dvec3 bbMin{0}, bbMax{0};  // lokale Begrenzung
    glm::dvec3 wMin{0}, wMax{0};    // Weltbegrenzung (aktuell gehalten)
    GpuMesh mesh;
    std::vector<glm::vec3> edges;   // lokale Kantenlinien (Paare)
    CollisionShape coll;            // Kollisionsform: Hüllen, exakte Zylinder, exakte Bohrungen (lokal)
    // Bohrungen im Betrieb (gleiche Reihenfolge wie coll.bores)
    struct BoreRuntime {
        glm::dvec3 force{0};        // Kraft der Lochwand auf das Teil im letzten Schritt (Welt, N)
        glm::dvec3 forceTop{0}, forceBottom{0};  // getrennt nach Bohrungshälften (Hebel: oben und unten entgegengesetzt)
        double load_N = 0;          // maßgebende Querlast = 2 · größere Hälfte (N)
        double normal = 0;          // Summe der Kontakt-Normalkräfte (N)
        double pressure = 0;        // mittlere Pressung = Normalkraft / (d · L) (MPa)
        double slip = 0;            // größte Gleitgeschwindigkeit an der Wand (m/s)
        double load = 0, loadPrev = 0;  // Auslastung bis zum Ausreißen (1 = reißt), loadPrev: über ca. 15 ms geglättet
        double capacity = 0;        // Tragfähigkeit in Lastrichtung (N)
        double applied = 0;         // in der Physik eingestellter Abrieb (m)
        uint32_t partner = 0;       // Teil, das in der Bohrung anliegt (Stift)
    };
    std::vector<BoreRuntime> boreRt;
    bool boreInUse = false;         // etwas liegt an einer Bohrungswand an: genauer rechnen
    double wornVolume = 0;          // seit dem Laden abgeriebenes Volumen (m³), alle Bohrungs-/Stiftkontakte
    struct AeroFace {
        glm::vec3 c, n;  // Mittelpunkt und Normale (lokal)
        float area;      // m²
    };
    std::vector<AeroFace> aero;     // Außenflächen für die Luftkräfte
    glm::dvec3 aeroForce{0}, aeroTorque{0};  // Luftkraft/-moment im letzten Schritt (ohne Auftrieb)
    double breakCooldown = 0;       // bis zu dieser Simulationszeit nicht erneut zerbrechen
    BodyHandle body = kNoBody;
    uint32_t hinge = 0;
    bool ccd = false;               // kontinuierliche Kollision gerade an (nur bei schnellen Teilen)
    // Kanten (für Abrunden/Fasen), bei Bedarf berechnet
    std::vector<solid::Edge> featureEdges;
    bool edgesValid = false;
    // Letzte Lage ohne Ineinanderstecken: dorthin zurück, wenn das Teil tief in ein anderes gerät
    glm::dvec3 safePos{0};
    glm::dquat safeRot{1, 0, 0, 0};
    bool hasSafe = false;
    uint32_t netRev = 0;            // neu bei jeder neuen Geometrie (LAN: Teil neu an die Mitspieler senden)
    // Wärme (Heat.cpp): Zusammenfassung nach jedem Wärmeschritt
    struct HeatInfo {
        float tMin = 20, tMax = 20, tMean = 20;
        float chr = 0, melt = 0;    // Mittel über alle Zellen
        int burning = 0;            // brennende Zellen
        bool idle = true;           // im Gleichgewicht mit der Umgebung: nicht rechnen
        double age = 0;             // Tropfen: so lange schon unterwegs (s)
        double cellMass = 0;        // kg je Zelle mit Material
    } heatInfo;
};

// Verbindungen zwischen zwei Teilen
enum class JointType : int { Glue = 0, Nail = 1, Screw = 2, Weld = 3, Hinge = 4, Spring = 5, Rope = 6 };

struct JointState {
    uint32_t id = 0;
    JointType type = JointType::Glue;
    uint32_t a = 0, b = 0;               // a hält, b wird gehalten
    glm::dvec3 anchorA{0}, anchorB{0};   // Ankerpunkte (lokal in a bzw. b)
    glm::dvec3 normalA{0, 1, 0};         // Fugen-/Nagelrichtung (lokal in a, von a nach b)
    glm::dvec3 axisB{0, 1, 0};           // Scharnierachse (lokal in b)
    std::string preset;                  // Kleber-Art (holzleim, epoxid ...)
    double area = 0;                     // Berührfläche m²
    double thickA = 0;                   // Dicke von a entlang der Fuge (m)
    double ft = 0, fs = 0, gapMm = 0;    // Kleber: Haft- (Zug) und Scherfestigkeit MPa, max. Spalt
    int count = 0;                       // Nägel/Schrauben
    double lengthMm = 0, diameterMm = 0;
    double seamMm = 0;                   // Schweißnaht-Dicke
    double maxForce = 0;                 // Scharnier/Feder/Seil: Bruchkraft N
    double stiffness = 0, damping = 0;   // Feder N/m, N·s/m
    double restLength = 0;               // Feder/Seil (m)
    float motorRpm = 0, motorTorque = 0; // Scharnier mit Motor
    bool unbreakable = false;            // hält alles aus
};

struct Joint : JointState {
    uint32_t constraint = 0;
    double rT = 0, rS = 0, rM = 0, rMax = 0;  // Tragfähigkeit: Zug N, Scherung N, Moment N·m, Kraft N
    double load = 0, loadPrev = 0;            // Auslastung (1 = Bruchgrenze)
    double loadSlow = 0;                      // geglättet über ca. 15 ms: entscheidet über den Bruch
    glm::dvec3 lastForce{0}, lastTorque{0};
};

// Live-Messwerte für den Physik-Inspektor (F3)
struct InspectData {
    uint32_t id = 0;
    bool valid = false;                 // Werte stammen aus laufender Simulation
    glm::dvec3 acc{0};                  // Beschleunigung (geglättet)
    glm::dvec3 fGravity{0}, fBuoyancy{0}, fDrag{0}, fExternal{0}, fContact{0}, fNet{0};
    glm::dvec3 tAero{0};                // Drehmoment der Luft
    std::vector<ContactInfo> contacts;
};

struct RayHit {
    Object* obj = nullptr;
    double t = 0;
    glm::dvec3 point{0};      // Welt
    glm::dvec3 normal{0};     // Welt
    glm::dvec3 localPoint{0};
    glm::dvec3 localNormal{0};
};

class World {
public:
    World(Physics& physics, Renderer* renderer);
    ~World();

    // ---------------------------------------------------------- Erzeugen
    Object* addSolid(solid::Solid s, int material, const glm::dvec3& framePos, const glm::dquat& frameRot,
                     const std::string& name, const std::string& shape);
    Object* addBox(int material, glm::dvec3 size, const glm::dvec3& pos, const std::string& name = "");
    Object* addCylinder(int material, double diameter, double height, const glm::dvec3& pos, const std::string& name = "");
    Object* addTube(int material, double dOuter, double dInner, double height, const glm::dvec3& pos, const std::string& name = "");
    Object* addSphere(int material, double diameter, const glm::dvec3& pos, const std::string& name = "");
    Object* addGear(int material, int teeth, double module, double width, double bore, const glm::dvec3& pos,
                    const std::string& name = "");
    Object* addPropeller(int material, double diameter, int blades, double pitchDeg, double chord, double thickness,
                         const glm::dvec3& pos, const std::string& name = "");
    // Keil (Axt-/Meißelschneide): Schneide entlang x unten, Länge L, Höhe H, Keilwinkel (ganz) angleDeg
    Object* addWedge(int material, double length, double height, double angleDeg, const glm::dvec3& pos, const std::string& name = "");
    // Spitze (Kegel nach unten): Durchmesser oben, Länge, Rundung der Spitze (Radius, 0 = spitz)
    Object* addSpike(int material, double diameter, double length, double tipRadius, const glm::dvec3& pos, const std::string& name = "");
    Object* addImported(const std::filesystem::path& file, int material, double scale, const glm::dvec3& pos,
                        std::string* error);

    // freie Stelle auf dem Boden suchen (Mittelpunkt), Höhe = halbe Teilhöhe
    glm::dvec3 findFreeSpot(const glm::dvec3& halfExtents, const glm::dvec3& reference) const;

    // ---------------------------------------------------------- Zugriff
    Object* find(uint32_t id);
    const std::vector<std::unique_ptr<Object>>& objects() const { return objects_; }
    Object* fromBody(BodyHandle b);
    void worldBounds(const Object& o, glm::dvec3& mn, glm::dvec3& mx) const;
    glm::dvec3 sizeOf(const Object& o) const { return o.bbMax - o.bbMin; }

    // ---------------------------------------------------------- Ändern
    void remove(uint32_t id);
    void clear();
    Object* duplicate(uint32_t id, const glm::dvec3& offset);
    void setTransform(Object& o, const glm::dvec3& pos, const glm::dquat& rot);
    void setMaterial(Object& o, int material);
    void setFixed(Object& o, bool fixed);
    void setHinge(Object& o, int axis, float rpm, float torque);
    void setCollisionMode(Object& o, int mode);  // 0 automatisch, 1 genau, 2 eine Hülle
    void setLubricant(Object& o, int lubricant);  // 0 = trocken
    // Materialliste wurde bearbeitet oder neu geladen: remap[alter Index] = neuer Index (leer: gleich geblieben).
    // Masse, Reibung, Wärmekapazität und Physik-Körper aller Teile neu, auch in Rückgängig-Schritten
    void applyMaterials(const std::vector<int>& remap);
    // Bohrungen und Stifte eines Teils als Text (Maße, Passung, Last, Pressung, Abrieb)
    std::string boresText(Object& o);
    // Tragfähigkeit einer Bohrung bis zum Ausreißen für eine Lastrichtung (lokal, quer zur Achse), N
    double boreCapacity(const Object& o, size_t bore, const glm::dvec3& dirLocal) const;
    // Teil nach unten absetzen, bis es auf etwas aufliegt
    void dropToSurface(Object& o);

    // ---------------------------------------------------------- Werkzeuge
    // Sägen entlang einer lokalen Ebene. kerf = Schnittfuge (m). resultIds: alle entstandenen Teile.
    bool cut(uint32_t id, const solid::Plane& localPlane, double kerf, std::vector<uint32_t>* resultIds, std::string* err);
    // Bohren: lokaler Startpunkt + Richtung, Durchmesser/Tiefe in m (Tiefe <= 0: durchgehend)
    bool drill(uint32_t id, const glm::dvec3& localFrom, const glm::dvec3& localDir, double diameter, double depth,
               std::vector<uint32_t>* resultIds, double* removedVolume, std::string* err);

    // ---------------------------------------------------------- Bearbeiten (Edit.cpp)
    const std::vector<solid::Edge>& edgesOf(Object& o);
    // Kanten abrunden (segments >= 2) oder fasen (segments = 1); size = Radius bzw. Fase in m
    bool blendEdges(uint32_t id, const std::vector<int>& edges, double size, int segments, std::string* err);
    // Konvexe Bereiche (lokal) abtragen: Fräsen, Senken ... removed = abgetragenes Volumen (m³)
    bool removeMaterial(uint32_t id, const std::vector<std::vector<solid::Plane>>& tools, double* removed,
                        std::vector<uint32_t>* resultIds, std::string* err);
    // Fläche abhobeln: auf der Seite sign (+1/-1) der lokalen Achse depth (m) abnehmen
    bool planeOff(uint32_t id, int axis, int sign, double depth, std::string* err);
    bool mirrorPart(uint32_t id, int axis, std::string* err);
    bool scalePart(uint32_t id, const glm::dvec3& f, std::string* err);
    // b in a aufgehen lassen (gleiches Material), b verschwindet
    bool mergeParts(uint32_t a, uint32_t b, std::string* err);
    // a minus b (intersect = false) oder Schnittmenge (true); b bleibt nur mit keepTool
    bool booleanCut(uint32_t a, uint32_t tool, bool intersect, bool keepTool, std::vector<uint32_t>* resultIds, double* removed,
                    std::string* err);
    // Drehbank: zwischen from und to (m ab dem Ende, entlang der lokalen Achse) auf Durchmesser d0 -> d1 abdrehen
    bool turnPart(uint32_t id, int axis, double from, double to, double d0, double d1, double* removed, std::string* err);
    // Biegen quer zur lokalen Achse bei "at" (m ab dem Ende), Innenradius radius (m)
    bool bendPart(uint32_t id, int axis, double at, double angleDeg, double radius, std::string* err, std::string* info);

    // ---------------------------------------------------------- Bau-Modus: Teile berühren sich, stecken nicht ineinander
    // Teile gemeinsam um delta verschieben; sie halten am ersten Hindernis an und gleiten an ihm entlang.
    // Rückgabe: tatsächlich gefahrene Strecke. blocker = Teil, an dem sie anhielten (0 = Boden).
    glm::dvec3 sweepMove(const std::vector<uint32_t>& ids, const glm::dvec3& delta, uint32_t* blocker = nullptr, bool* blocked = nullptr);
    // Teile, in die dieses hineinragt: (Nummer, Tiefe in m), Nummer 0 = Boden
    std::vector<std::pair<uint32_t, double>> overlapsOf(const Object& o, double minDepth);
    int rescueCount() const { return rescues_; }
    // Im Bau-Modus halten verschobene Teile an anderen an (Maus und Befehl "move")
    void setBuildCollision(bool on) { buildCollision_ = on; }
    bool buildCollision() const { return buildCollision_; }

    // ---------------------------------------------------------- Physik
    void setSimulating(bool on);
    bool simulating() const { return simulating_; }
    void step(float dt, const std::function<void(float)>& preStep);
    void simulateSeconds(double seconds, bool wakeAll = true);  // wakeAll: ruhende Teile anstoßen (z.B. Befehl sim)
    void syncFromPhysics();
    Physics& physics() { return physics_; }
    void applyImpulse(Object& o, const glm::vec3& impulse);
    // Luft: Dichte (kg/m³, 0 = Vakuum) und Wind (m/s)
    void setAir(double density, const glm::dvec3& wind);
    double airDensity() const { return airDensity_; }
    glm::dvec3 wind() const { return wind_; }

    // ---------------------------------------------------------- Verbindungen (Joints.cpp)
    Joint* glue(uint32_t a, uint32_t b, const std::string& preset, double ftMPa, double gapMm, std::string* err);
    Joint* nail(uint32_t a, uint32_t b, bool screw, int count, double lengthMm, double diameterMm, std::string* err);
    Joint* weld(uint32_t a, uint32_t b, double seamMm, std::string* err);
    Joint* hinge(uint32_t a, uint32_t b, int axis, float rpm, float torque, std::string* err);
    Joint* spring(uint32_t a, uint32_t b, double stiffness, double damping, double maxForce, std::string* err);
    Joint* rope(uint32_t a, uint32_t b, double length, double maxForce, std::string* err);
    Joint* findJoint(uint32_t id);
    const std::vector<std::unique_ptr<Joint>>& joints() const { return joints_; }
    void removeJoint(uint32_t id);
    void updateJoint(Joint& j);  // nach Änderung der Einstellungen
    void jointPoints(const Joint& j, glm::dvec3& pa, glm::dvec3& pb) const;
    std::string jointText(const Joint& j, bool detailed) const;
    static const char* jointTypeName(JointType t);
    static const char* jointName(const JointState& j);  // wie jointTypeName, steckengebliebenes Werkzeug = "stuck"
    // Bruch an/aus: aus = alles hält (Engine/Editor), an = realistisch (Spiel)
    void setBreakage(bool on) { breakage_ = on; }
    bool breakage() const { return breakage_; }
    int brokenCount() const { return brokenCount_; }
    std::vector<std::string> takeEvents();
    bool rayToObject(const Object& o, const glm::dvec3& origin, const glm::dvec3& dir, double& t) const;

    // ---------------------------------------------------------- Wärme und Feuer (Heat.cpp)
    // Wärme an/aus: aus = alles behält seine Temperatur (keine Wärmeleitung, kein Feuer, kein Schmelzen)
    void setHeat(bool on) { heatOn_ = on; }
    bool heat() const { return heatOn_; }
    void setAmbient(double tempC);                      // Luft, Boden und unberührte Teile
    double ambient() const { return ambient_; }
    void setHeatSpeed(double f) { heatSpeed_ = std::clamp(f, 0.1, 100.0); }  // Zeitraffer nur für die Wärme
    double heatSpeed() const { return heatSpeed_; }
    // Nur die Wärme weiterrechnen (Bau-Modus, die Physik steht). Im Spiel-Modus läuft sie mit den Physikschritten.
    void stepHeatOnly(double dt);
    void setTemperature(Object& o, double tempC);
    // Anzünden an einer Stelle (lokal, nullptr = unterste Stelle). false, wenn das Material nicht brennt
    bool ignite(Object& o, const glm::dvec3* localPoint, std::string* err);
    int extinguish(Object* o);                          // nullptr = alles. Anzahl gelöschter Stellen
    // Wärmequelle (heatTools()) auf eine Stelle (lokal) richten, für "seconds" Sekunden
    void addHeatSource(uint32_t id, const glm::dvec3& localPoint, int tool, double seconds);
    double temperatureAt(const Object& o, const glm::dvec3& worldPoint) const;
    double charAt(const Object& o, const glm::dvec3& worldPoint) const;
    double hardnessAt(const Object& o, const glm::dvec3& worldPoint) const;  // Härte bei der Temperatur dort (MPa)
    std::string heatText(Object& o);
    const std::vector<Fire>& fires() const { return fires_; }
    int burningParts() const;

    // ---------------------------------------------------------- Physik-Inspektor
    void setInspect(uint32_t id);
    uint32_t inspectId() const { return inspectId_; }
    const InspectData& inspect() const { return inspect_; }
    void addExternalForce(uint32_t id, const glm::dvec3& f);  // z.B. Hand, für die Kraftbilanz
    std::string inspectText(uint32_t id);

    // ---------------------------------------------------------- Strahl
    bool raycast(const glm::dvec3& origin, const glm::dvec3& dir, RayHit& hit, uint32_t ignoreId = 0);

    // ---------------------------------------------------------- Rückgängig
    void pushUndo();
    bool undo();
    bool redo();
    void discardUndo() { if (!undo_.empty()) undo_.pop_back(); }
    size_t undoCount() const { return undo_.size(); }
    size_t redoCount() const { return redo_.size(); }

    // ---------------------------------------------------------- Szene
    void demoScene();
    void showcaseScene();  // große Beispielwelt mit allem (Showcase.cpp)
    void fireScene();      // Feuerstelle: Wärme, Feuer, Schmelzen, Kälte (Showcase.cpp)
    bool save(const std::filesystem::path& file, std::string* err) const;
    bool load(const std::filesystem::path& file, std::string* err);

    const GpuMesh& groundMesh() const { return groundMesh_; }
    const GpuMesh& boxMesh() const { return boxMesh_; }  // Einheitswürfel für grobe Detailstufe
    ChunkGrid& grid() { return grid_; }
    std::string nextName(const std::string& base) const;
    double simTime() const { return simTime_; }            // simulierte Zeit in s
    uint32_t lastCreatedId() const { return lastCreated_; }  // zuletzt erstelltes Teil
    // Nur für Messungen: alte Kollision (keine Grundformen, keine Vereinfachung, CCD nur für dünne Teile)
    void setLegacyCollision(bool on) { legacyCollision_ = on; }

    // ---------------------------------------------------------- LAN-Mehrspieler: Mitspieler übernimmt die Welt vom Gastgeber
    // Teil anlegen oder (gleiche Nummer) ersetzen; Verbindungen bleiben bestehen
    Object* netPut(const ObjectState& s);
    // Lage setzen ohne Physik-Nebenwirkungen (der Mitspieler simuliert nicht selbst)
    void netMove(Object& o, const glm::dvec3& pos, const glm::dquat& rot);
    // alle Verbindungen ersetzen
    void netSetJoints(const std::vector<JointState>& joints);

private:
    struct Snapshot {
        std::vector<ObjectState> objects;
        std::vector<JointState> joints;
        uint32_t nextId = 1, nextJointId = 1;
    };
    Joint* addJoint(JointState s, std::string* err);
    void computeCapacity(Joint& j);
    void buildConstraint(Joint& j);
    void releaseJointsOf(uint32_t objectId);
    void rebuildJointsOf(uint32_t objectId);
    void checkJoints(float h);
    void processImpacts();
    void fracture(uint32_t id, const glm::dvec3& worldPoint, double ratio, double energy, double threshold);
    // Aufprall eines deutlich härteren Teils: Form an der Kontaktstelle (Spitze, Schneide, stumpf) entscheidet über
    // Delle/Loch, Steckenbleiben, Spalten oder Zerbrechen (Impact.cpp). true = das getroffene Teil wurde verändert
    bool toolImpact(Object& tool, Object& target, const ImpactInfo& imp, bool toolIsA);
    Snapshot capture() const;
    void restore(const Snapshot& s);
    Object* insert(std::unique_ptr<Object> o);
    void rebuildGeometry(Object& o);
    void rebuildBody(Object& o);
    void releaseBody(Object& o);
    void releaseAll(Object& o);
    void refreshPlacement(Object& o);
    void buildAeroFaces(Object& o, const solid::Solid* s, const std::vector<solid::Vertex>* tris);
    Object* setPrimitive(Object* o, int prim, const glm::dvec3& size);
    void beforeStep(float h);
    void afterStep(float h);
    void handleDeepContacts();
    void processBores(float h);  // Kräfte in Bohrungen, Abrieb, Ausreißen
    void tearOut(uint32_t id, size_t bore, const glm::dvec3& dirLocal, double force, double capacity);
    bool replaceWithParts(Object& o, std::vector<solid::Solid>& parts, std::vector<uint32_t>* resultIds);
    // neue Geometrie, in Teile zerfallen lassen (wenn getrennt), Kollision/Masse neu
    bool applyResult(Object& o, solid::Solid s, std::vector<uint32_t>* resultIds, std::string* err);
    // Wärme (Heat.cpp)
    void stepHeat(double dt);
    bool ensureHeat(Object& o);   // Gitter anlegen (mit Umgebungstemperatur)
    void fitHeat(Object& o);      // Gitter an eine neue Form anpassen, Werte übernehmen
    void heatShapes(bool cells);  // Tropfen landen als Pfütze; cells: abgebrannte und geschmolzene Stellen entfernen
    void collectFrictionHeat(float h);
    double jointHeatFactor(const Joint& j) const;
    void makePuddle(Object& o, double floorY);

    Physics& physics_;
    Renderer* renderer_;
    std::vector<std::unique_ptr<Object>> objects_;
    uint32_t nextId_ = 1;
    bool simulating_ = false;
    std::vector<Snapshot> undo_, redo_;
    BodyHandle groundBody_ = kNoBody;
    GpuMesh groundMesh_, boxMesh_;
    ChunkGrid grid_;
    std::unordered_map<uint32_t, Object*> byId_;
    uint32_t inspectId_ = 0;
    InspectData inspect_;
    glm::dvec3 velBefore_{0}, extForce_{0}, aeroDrag_{0}, aeroTorque_{0};
    double simTime_ = 0;
    bool legacyCollision_ = false;
    std::vector<std::unique_ptr<Joint>> joints_;
    uint32_t nextJointId_ = 1;
    bool breakage_ = true;
    int brokenCount_ = 0;
    std::vector<std::string> events_;
    double airDensity_ = 1.204;
    glm::dvec3 wind_{0};
    uint32_t lastCreated_ = 0;
    uint32_t netRevs_ = 0;                              // Zähler für Object::netRev
    bool buildCollision_ = true;
    int rescues_ = 0;                                   // so oft wurde ein Teil an seine letzte freie Stelle zurückgesetzt
    std::unordered_map<uint64_t, std::pair<double, int>> rescueLog_;  // Paar -> (Beginn, Anzahl) im Zeitfenster
    std::unordered_map<uint64_t, double> stuckPairs_;   // Paar steckt dauerhaft: bis zu dieser Zeit nur sanft trennen
    // Wärme
    struct HeatSource {
        uint32_t id = 0;
        glm::dvec3 local{0};
        int tool = 0;
        double until = 0;   // Wärmezeit
    };
    struct FrictionHeat {
        double energy = 0;          // J seit dem letzten Wärmeschritt
        glm::dvec3 point{0};        // energiegewichtete Summe der Stellen
        BodyHandle a = kNoBody, b = kNoBody;
    };
    std::vector<HeatSource> heatSources_;
    std::vector<Fire> fires_;
    std::unordered_map<uint64_t, FrictionHeat> frictionHeat_;
    bool heatOn_ = true;
    double ambient_ = 20.0, heatSpeed_ = 1.0;
    double heatAccum_ = 0, heatTime_ = 0, heatShapeAccum_ = 0;
};

std::string fmtNum(double v, int decimals = 1);
// Kleber-Sorte zu ihrem Schlüssel (woodglue, epoxy ...; alte deutsche Namen werden auch erkannt)
std::string gluePresetKey(const std::string& name);
// Kleber-Schlüssel für ein beim Aufprall steckengebliebenes Werkzeug (hält nur durch Reibung)
inline constexpr const char* kStuckPreset = "stuck";
