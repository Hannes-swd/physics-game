// Weltgenerierung: Klima, Biome, Höhe und Farbe der Oberfläche, nur aus Seed und Ort (siehe docs/16-Weltgenerierung.md).
//
//   Seed -> vier Klimafelder (Wärme, Feuchtigkeit, Kontinentalität, Zerklüftung; je 0..1, ändern sich nur langsam)
//        -> an jedem Ort das Biom, dessen Klimabereiche (biomes.json) am besten passen; nahe der Grenze werden die
//           Biome weich überblendet (Höhe und Farbe), das Biom selbst wechselt an einer scharfen Linie
//        -> Höhe = Summe (Gewicht · Höhe des Bioms)
//        -> Flüsse: die Nulllinien eines weiteren Rauschfelds (mit Schlingen). Ein Teil der Linien führt Wasser, wie viel,
//           sagt das Biom ("rivers": Wüste 0). Entlang eines Flusses wird eine Rinne mit Tal ins Gelände gegraben. Alle
//           Flüsse haben denselben Wasserspiegel; außerhalb der Flüsse bleibt das Gelände darüber (keine Pfützen)
//
// Die Einstellungen (GenConfig) kommen aus assets/biomes.json; jede Welt hat ihre eigene Kopie (generator.json).
// WorldGen ist nach dem Erstellen unveränderlich und darf von mehreren Threads gleichzeitig benutzt werden.
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

constexpr int kClimateCount = 4;
enum ClimateParam { kTemperature = 0, kHumidity = 1, kContinentalness = 2, kErosion = 3 };
extern const char* const kClimateNames[kClimateCount];  // "temperature", "humidity", "continentalness", "erosion"
constexpr int kMaxBiomes = 32;
// Zeug am Boden (Scatter.h): Steine, Stöcke, umgefallene Stämme, Baumstümpfe
enum ScatterKind { kScatterStone = 0, kScatterStick = 1, kScatterLog = 2, kScatterStump = 3, kScatterKinds = 4 };
extern const char* const kScatterNames[kScatterKinds];  // Schlüssel in biomes.json: "stones", "sticks", "logs", "stumps"

struct GenConfig {
    struct Climate {
        std::string label;
        double scale = 2000;    // m
        int octaves = 3;
        double contrast = 1.5;
    } climate[kClimateCount];
    double blend = 0.04;        // Übergangsbreite in Klimaeinheiten
    struct Biome {
        std::string id, label;
        double lo[kClimateCount] = {0, 0, 0, 0}, hi[kClimateCount] = {1, 1, 1, 1};
        struct Height {
            double base = 0, amplitude = 1, scale = 100, persistence = 0.5, lacunarity = 2.0;
            int octaves = 3;
        } height;
        glm::vec3 color{0.35f, 0.45f, 0.22f}, slopeColor{0.45f, 0.4f, 0.35f};
        float slopeStart = 0.35f, slopeEnd = 0.6f;
        std::string material = "soil";
        glm::vec3 borderColor{1.0f, 0.85f, 0.2f};
        double rivers = 1.0;    // wie viele Flüsse hier Wasser führen (0 = keine, 1 = so viele wie "rivers.presence")
        double treesPerHa = 0;  // Bäume je Hektar
        std::vector<std::pair<std::string, double>> treeTypes;  // Art (trees.json) und Gewicht
        // Bewuchs (nur Bild, siehe Flora.h): Gras, Blumen, Büsche
        struct Flora {
            bool given = false;                       // "grass", "flowers" oder "bushes" steht in der Datei
            double grass = 0;                         // Grasbüschel je m²
            double grassHeightMin = 0.2, grassHeightMax = 0.5;  // m
            double grassPatches = 0;                  // 0 = gleichmäßig, 1 = nur in Flecken
            std::vector<glm::vec3> grassColors;
            double flowers = 0;                       // Blumen je m²
            std::vector<glm::vec3> flowerColors;
            double bushesPerHa = 0;                   // Büsche je Hektar
            double bushSizeMin = 0.6, bushSizeMax = 1.4;  // Durchmesser (m)
            std::vector<glm::vec3> bushColors;
        } flora;
        // Zeug am Boden (Scatter.h), je Art (kScatterNames): wie viele, wie groß, woraus
        struct Scatter {
            bool given = false;                        // steht in der Datei
            double perHa = 0;                          // je Hektar
            double sizeMin = 0.05, sizeMax = 0.6;      // Steine: größte Ausdehnung, Stöcke/Stämme: Länge, Stümpfe: Höhe (m)
            double thickMin = 0.02, thickMax = 0.05;   // Stöcke, Stämme, Stümpfe: Durchmesser (m)
            double skew = 1.0;                         // je größer, desto mehr kleine (1 = alle Größen gleich oft)
            std::vector<std::string> materials;        // eines davon, zufällig
        } scatter[kScatterKinds];
    };
    std::vector<Biome> biomes;
    // Flüsse. Fehlt "rivers" in der Datei (alte Welten): keine Flüsse, das Gelände bleibt genau wie vorher
    struct Rivers {
        bool enabled = false;
        double level = 0.0;                     // Wasserspiegel aller Flüsse (m)
        double spacing = 2500;                 // Größe des Rauschens, dessen Nulllinien die Flüsse sind (m): größer = seltener
        double presence = 0.6;                  // Anteil der Linien, die Wasser führen (0..1)
        double meander = 60, meanderScale = 260;  // Schlingen: Verschiebung und Größe der Bögen (m)
        double widthMin = 5, widthMax = 14;     // Breite der Rinne (m)
        double depthMin = 1.0, depthMax = 2.2;  // Tiefe in der Mitte (m)
        double bank = 0.35;                     // Ufer über dem Wasser (m)
        double floor = 0.3;                     // so weit bleibt das Gelände außerhalb der Flüsse mindestens über dem Wasser (m)
        double valley = 90;                     // Breite des Tals je Seite (m)
        double valleySlope = 0.35;              // höchste Steigung der Talhänge
        double speedMin = 0.4, speedMax = 1.1;  // Strömung: breiter Fluss / schmaler Fluss (m/s)
        glm::vec3 bedColor{0.30f, 0.26f, 0.18f};  // Flussbett und Ufer
    } rivers;
    // Bewuchs: bis wohin Gras/Blumen und Büsche gezeichnet werden (m)
    struct FloraView {
        double grassDistance = 45, bushDistance = 130;
    } flora;
    // Zeug am Boden: bis wohin gezeichnet (m; kleine Steine nur näher, je nach Größe), Stöcke bis stickDistance, bis wohin
    // große Stücke um die Spieler feste Teile sind (m), ab welcher Größe ein Stein groß ist (m; kleinere hebt man auf)
    struct ScatterView {
        double drawDistance = 250, stickDistance = 45, solidDistance = 25, bigStone = 0.3;
    } scatterView;
    std::string text;          // der JSON-Text, aus dem sie stammen (wird in die Welt kopiert)

    int findBiome(const std::string& id) const;  // -1: unbekannt
};

// Text lesen und prüfen (Fehler mit Ort, z.B. "biomes[1].climate: unknown value 'warmth'")
bool parseGenConfig(const std::string& text, GenConfig& out, std::string* err);
// Eingebaute Kopie von assets/biomes.json (beim Bauen eingebettet)
const GenConfig& builtinGenConfig();
// assets/biomes.json suchen (neben dem Programm, im Projektordner); leer: nicht gefunden
std::string findBiomesFile();
// biomes.json laden (Datei, sonst die eingebaute); report: Fehler der Datei
std::shared_ptr<const GenConfig> loadBiomesFile(std::string* report = nullptr);

struct Climate {
    double v[kClimateCount] = {0.5, 0.5, 0.5, 0.5};
};

// Fluss an einem Ort
struct RiverSample {
    double dist = 1e30;    // Abstand zur Mittellinie (m), aus dem Rauschfeld geschätzt
    double half = 0;       // halbe Breite der Rinne (m)
    double depth = 0;      // Tiefe in der Mitte unter dem Wasserspiegel (m)
    double strength = 0;   // 0: kein Fluss, 1: voller Fluss (dazwischen läuft er aus: Quelle, Lücke, anderes Biom)
    glm::dvec2 flow{0};    // Strömung (m/s), entlang der Linie
};

// Ergebnis an einem Ort
struct GenSample {
    double height = 0;
    int biome = 0;                  // das Biom an diesem Ort (scharfe Grenze)
    double score[kMaxBiomes] = {};  // wie gut jedes Biom passt (> 0: drinnen); Grenze: wo zwei Scores gleich sind
    glm::vec3 color{0}, slopeColor{0};
    float slopeStart = 0.35f, slopeEnd = 0.6f;
    int material = 0;               // Material (Reibung) des gewinnenden Bioms
    bool wet = false;               // im Flussbett (mit Rand): Wasser, wo der Boden unter dem Wasserspiegel liegt
    RiverSample river;              // der Fluss hier (strength 0: keiner)
};

class WorldGen {
public:
    // singleBiome: -1 = alle Biome, sonst nur dieses (Index in config.biomes)
    WorldGen(uint64_t seed, std::shared_ptr<const GenConfig> config, int singleBiome);

    uint64_t seed() const { return seed_; }
    const GenConfig& config() const { return *cfg_; }
    std::shared_ptr<const GenConfig> configPtr() const { return cfg_; }
    int singleBiome() const { return single_; }

    Climate climate(double x, double z) const;
    // Scores aller Biome für ein Klima, Rückgabe: das beste
    int classify(const Climate& c, double* scores) const;
    void sample(double x, double z, GenSample& out) const;
    double height(double x, double z) const;
    double biomeHeight(int biome, double x, double z) const;  // nur dieses Biom (ohne Überblenden)

    bool hasRivers() const { return cfg_->rivers.enabled; }
    double riverLevel() const { return cfg_->rivers.level; }
    // Wie viele Flüsse die Biome hier zulassen (0..1); scores aus classify. In einem Biom mit "rivers": 0 genau 0
    double riverFactor(const double* scores) const;
    void river(double x, double z, double factor, RiverSample& out) const;
    void river(double x, double z, RiverSample& out) const;  // rechnet das Klima selbst

private:
    void applyRivers(double x, double z, GenSample& out) const;

    uint64_t seed_;
    std::shared_ptr<const GenConfig> cfg_;
    int single_;
    uint64_t climateSeed_[kClimateCount];
    uint64_t riverSeed_[5] = {};  // Linien, Schlingen x, Schlingen z, Lücken, Breite
    std::vector<uint64_t> heightSeed_;
    std::vector<int> material_;
};
