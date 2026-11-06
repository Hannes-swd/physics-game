// Tiere: Arten und Vorkommen je Biom (assets/animals.json), Erscheinen um den Spieler, Verhalten (siehe docs/24-Tiere.md).
//
// Tiere sind nur Bild und Verhalten, keine Physik, und werden nicht gespeichert. Sie erscheinen immer in der Nähe des
// Spielers, je Biom mit den Arten aus animals.json, und verschwinden ganz, sobald sie weiter weg sind, als man sie noch
// sehen würde ("visible": ein Eichhörnchen schnell, ein Vogel am Himmel oder eine Gämse am Berg erst weit weg).
// Jede Art hat ein Verhalten: scheu (flieht), ruhig (weicht nur aus), neugierig (hält Abstand und umkreist), klein
// (flitzt kurz weg). Herden bleiben zusammen und fliehen gemeinsam. Vögel kreisen, ziehen im Schwarm oder flattern.
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "Solid.h"

class Terrain;
class WorldGen;

enum class AnimalBehavior { Shy = 0, Calm = 1, Curious = 2, Small = 3 };
// Ground: läuft; Soar: kreist hoch oben (Greifvögel, Möwen); Flock: Schwarm zieht umher; Flutter: flattert (Fledermaus)
enum class AnimalMotion { Ground = 0, Soar = 1, Flock = 2, Flutter = 3 };
enum class AnimalActive { Always = 0, Day = 1, Night = 2 };
enum class AnimalAttack { None = 0, Hunt = 1, Defend = 2 };

struct AnimalSpecies {
    std::string id, label;
    std::string model;              // Datei in assets/animals; "bird": eingebaute Vogelform (Körper, Flügel, Schnabel)
    float height = 1.0f;            // ganze Höhe (m); Vogelform: Spannweite
    std::vector<std::pair<std::string, glm::vec3>> tint;  // Material des Modells -> andere Farbe (linear, wie im Modell)
    float shade = 1.0f;             // alle Farben mal diesen Wert
    std::string foot;               // Knochen, an dem die Schrittlänge gemessen wird (Vierbeiner: Unterschenkel)
    std::vector<std::string> idleClips{"Idle"};
    std::string walkClip = "Walk", runClip = "Gallop", flyClip;
    float walkSpeed = 1.2f, runSpeed = 7.0f;  // m/s
    float turnRate = 3.0f;                     // rad/s
    int groupMin = 1, groupMax = 1;
    float spread = 5.0f;                       // Herde: so weit (m) stehen die Tiere um das erste herum
    float wander = 30.0f;                      // so weit (m) entfernt sich die Gruppe von ihrem Platz
    AnimalBehavior behavior = AnimalBehavior::Shy;
    float fleeDistance = 25.0f, alertDistance = 45.0f;  // m
    float keepNear = 14.0f, keepFar = 30.0f;            // neugierig: Abstand zum Spieler
    bool takeoff = false;                      // flieht fliegend (Tauben) und landet später wieder
    float visible = 150.0f;                    // so weit (m) bleibt das Tier, dahinter verschwindet es
    AnimalActive active = AnimalActive::Always;
    AnimalMotion motion = AnimalMotion::Ground;
    float slopeMin = 0.0f, slopeMax = 0.5f;    // erscheint nur dort (1 - Normale.y wie in biomes.json)
    float moveSlopeMax = 0.75f;                // so steil darf es laufen
    double heightMin = -1e9, heightMax = 1e9;  // erscheint nur in dieser Höhe (m)
    bool water = false;                        // darf ins flache Wasser (Frösche, Krabben)
    float flyMin = 20.0f, flyMax = 60.0f;      // Flughöhe über dem Boden (m)
    float circleMin = 30.0f, circleMax = 70.0f;  // Kreise (m)
    float flySpeed = 9.0f;                     // m/s
    float wingspan = 0.0f;                     // Modell mit Flügeln zum Abheben (takeoff): Spannweite (m)
    glm::vec3 body{0.2f}, wing{0.1f}, beak{0.8f, 0.6f, 0.1f};  // Vogelform, Flügel beim Abheben (wing)
    // Angriff: hunt = jagt den Spieler ab range (m, nachts doppelt so weit), gibt nach chase s auf; defend = greift nur an,
    // wer näher als range kommt, und zieht sich danach zurück. reach: so nah muss es heran; kind: bite, gore, sting, pinch
    AnimalAttack attack = AnimalAttack::None;
    float attackRange = 0, attackReach = 1.0f, attackCooldown = 1.6f, attackPower = 1.0f, attackChase = 25.0f;
    std::string attackKind = "bite", attackClip;
    // Jagd: so viel hält es aus (0: aus der Größe), Fell beim Zerlegen (m², 0: keins), Sterbe-Ablauf
    float toughness = 0, hide = 0;
    std::string deathClip;
};

struct AnimalConfig {
    bool enabled = true;
    float interval = 0.05f;   // s zwischen zwei Versuchen, ein Tier erscheinen zu lassen (je Versuch eine Art)
    int maxAnimals = 160;     // höchstens so viele gleichzeitig
    float minDistance = 18;   // nie näher am Spieler erscheinen (m)
    float hiddenShare = 0.6f; // im Blickfeld erst ab diesem Anteil von "visible" erscheinen
    std::vector<AnimalSpecies> species;
    struct Entry {
        int species = -1;
        double groups = 1.0;  // so viele Gruppen gleichzeitig in Sichtweite (Bruchteile: mal ja, mal nein)
    };
    std::vector<std::pair<std::string, std::vector<Entry>>> biomes;  // Biom-Id (biomes.json) -> Arten
    std::string text;
    int findSpecies(const std::string& id) const;
};

bool parseAnimalConfig(const std::string& text, AnimalConfig& out, std::string* err);
const AnimalConfig& builtinAnimalConfig();          // eingebaute Kopie von assets/animals.json
std::string findAnimalsFile();                      // leer: nicht gefunden
std::shared_ptr<const AnimalConfig> loadAnimalsFile(std::string* report = nullptr);
std::string findAnimalModel(const std::string& file);  // assets/animals/<file>, leer: nicht gefunden

struct Animal {
    uint32_t id = 0, group = 0;
    int species = 0;
    bool leader = false;
    glm::dvec3 pos{0};       // Füße (Vögel: Körpermitte)
    float yaw = 0;           // Blick: (-sin, -cos) wie die Spielfigur
    float pitch = 0, roll = 0;
    float speed = 0, wantSpeed = 0;
    glm::dvec2 target{0};
    glm::dvec2 offset{0};    // Herde: Platz relativ zum ersten Tier
    float detour = 0;        // Umweg um Wasser und Steilhänge (rad, klingt ab)
    float stuck = 0;         // so lange (s) kam es nicht weiter
    enum State { Idle, Walk, Alert, Flee, Follow, Fly, Attack } state = Idle;
    float attackCool = 0;    // bis zum nächsten Biss (s)
    float attackAnim = -1;   // Angriffs-Ablauf läuft seit so vielen s (-1: keiner)
    float retreat = 0;       // Jäger nach einem Biss: so lange (s) zurückweichen, dann wieder heran
    float hp = 1.0f;         // Lebenskraft 0..1: unter 0,5 humpelt es (kein Rennen), bei 0 ist es tot (erholt sich langsam)
    bool dead = false;
    float deadTime = 0;      // so lange (s) schon tot
    float hitAnim = -1;      // Treffer-Ablauf (Zucken) läuft seit so vielen s (-1: keiner)
    bool hitLeft = false;    // von links getroffen
    float timer = 0;
    int idleClip = 0, lastIdle = 0;
    float idleBlend = 1, idleTime = 0, phase = 0;
    // Flug
    bool airborne = false;
    double altitude = 0;     // über dem Boden (m)
    double angle = 0;        // Kreis: Winkel
    double radius = 30;
    glm::dvec2 center{0};
    float flap = 0, flapRate = 0, flapAmount = 0;
    float seed = 0;
    // Bild: zuletzt gerechnete Lage (wird je nach Abstand seltener erneuert)
    std::vector<solid::Vertex> pose;
    double poseTime = -1;
};

struct AnimalGroup {
    uint32_t id = 0;
    int species = 0;
    glm::dvec2 home{0};      // Platz, um den die Gruppe zieht
    glm::dvec2 pos{0};       // wo das erste Tier gerade ist
    double fleeUntil = -1;
    double attackUntil = -1, attackStart = -1;  // greift an bis dahin, seit dann
    double calmUntil = -1;                     // vertrieben oder aufgegeben: bis dahin kein Angriff
    glm::dvec2 fleeFrom{0};
    int size = 0;
};

// wer die Tiere aufschreckt: Spielfiguren (Füße) und wie schnell sie laufen
struct AnimalScarer {
    glm::dvec3 pos{0};
    double speed = 0;
    bool prey = false;  // kann angegriffen werden (eigene Figur, lebt)
};

// Ein Tier hat zugebissen (gestoßen, gestochen): wer, und welcher Spieler (Index in scarers)
struct AnimalBite {
    int species = 0;
    glm::dvec3 from{0};
    int target = 0;
};

class AnimalWorld {
public:
    AnimalWorld();
    void setConfig(std::shared_ptr<const AnimalConfig> cfg);
    const AnimalConfig& config() const { return *cfg_; }
    // focus: um diesen Punkt erscheinen Tiere (Spieler oder Kamera); view: Blickrichtung (zum Verstecken des Erscheinens)
    void update(double dt, const Terrain& tr, const glm::dvec3& focus, const glm::dvec3& view, const std::vector<AnimalScarer>& scarers,
                double dayHours, double viewScale);
    std::vector<Animal>& animals() { return animals_; }
    const std::vector<Animal>& animals() const { return animals_; }
    void clear();
    // Befehl "animals spawn": eine Gruppe dieser Art in dist m vor dem Punkt (ohne Rücksicht auf Biom und Dichte)
    std::string spawnAt(const Terrain& tr, const std::string& id, int count, const glm::dvec3& at);
    std::string status(const glm::dvec3& focus) const;
    double now() const { return clock_; }
    double reach(const AnimalSpecies& s) const { return s.visible * viewScale_; }
    std::vector<AnimalBite> takeBites() { std::vector<AnimalBite> r; r.swap(bites_); return r; }
    // Schlag in diese Richtung: Tiere im Umkreis lassen ab und fliehen. Rückgabe: so viele vertrieben
    int scare(const glm::dvec3& at, double radius);
    // Schlag entlang eines Strahls (bis reach m): trifft das nächste lebende Tier am Boden. damage: Anteil der Lebenskraft
    // eines Tiers der Zähigkeit 1. Rückgabe: Nummer des Tiers (0: keins); killed: daran gestorben
    uint32_t strike(const glm::dvec3& eye, const glm::dvec3& dir, double reach, float damage, bool* killed = nullptr);
    uint32_t pickDead(const glm::dvec3& eye, const glm::dvec3& dir, double reach) const;  // totes Tier im Blick
    const Animal* find(uint32_t id) const;
    bool remove(uint32_t id);
    float toughness(const AnimalSpecies& s) const { return s.toughness > 0 ? s.toughness : 0.35f + s.height; }
    // Trefferbox je Art (Modellmaße bei Höhe 1, x quer, y hoch, z längs), lebend und tot (liegend); vom Bild aus gemessen
    void setHitBox(int species, bool dead, const glm::vec3& mn, const glm::vec3& mx);
    glm::vec3 hitSize(int species, bool dead) const {  // Breite, Höhe, Länge der Trefferbox (m)
        const auto& v = dead ? hitDead_ : hitAlive_;
        const float h = cfg_->species[(size_t)species].height;
        return (size_t)species < v.size() && v[(size_t)species].set ? (v[(size_t)species].mx - v[(size_t)species].mn) * h : glm::vec3(0);
    }
    // Strahl gegen die Trefferbox eines Tiers: Abstand t (m) bis reach
    bool hitTest(const Animal& a, const glm::dvec3& eye, const glm::dvec3& dir, double reach, double& t) const;

private:
    bool walkable(const Terrain& tr, const AnimalSpecies& s, double x, double z, double* y = nullptr, double* slope = nullptr) const;
    void trySpawn(const Terrain& tr, const glm::dvec3& focus, const glm::dvec3& view, double dayHours);
    bool spawnGroup(const Terrain& tr, int species, const glm::dvec2& at, int count, bool checkTerrain);
    void think(Animal& a, AnimalGroup& g, const Animal* lead, const Terrain& tr, double dt, const std::vector<AnimalScarer>& scarers);
    void moveGround(Animal& a, const AnimalSpecies& s, const Terrain& tr, double dt, const glm::dvec2& goal, float speed);
    void fly(Animal& a, AnimalGroup& g, const Animal* lead, const Terrain& tr, double dt);
    AnimalGroup* findGroup(uint32_t id);
    float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng_); }

    std::shared_ptr<const AnimalConfig> cfg_;
    const WorldGen* gen_ = nullptr;                // für diesen Generator gilt biomeEntries_
    std::vector<std::vector<AnimalConfig::Entry>> biomeEntries_;  // je Biom (Index in biomes.json) die Arten
    std::vector<Animal> animals_;
    std::vector<AnimalGroup> groups_;
    std::mt19937 rng_;
    uint32_t nextId_ = 1;
    double clock_ = 0, spawnClock_ = 0;
    size_t nextSpecies_ = 0;
    double viewScale_ = 1.0;
    double dayHours_ = 12.0;
    struct HitBox {
        glm::vec3 mn{-0.22f, 0.0f, -0.5f}, mx{0.22f, 1.0f, 0.5f};
        bool set = false;
    };
    std::vector<HitBox> hitAlive_, hitDead_;  // je Art
    std::vector<AnimalBite> bites_;
};
