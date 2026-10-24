// Bäume (siehe docs/17-Baeume.md).
//
//   Arten     assets/trees.json: Stamm, Äste, Krone als wenige Werte. Daraus entsteht je Art und Variante eine Form
//             (TreeModel): ein Teil aus Holz (Stamm und Äste, Mantel = Rinde) und die Krone (nur Bild, ohne Gewicht).
//   Plätze    Raster mit Feldern von "spacing" m; in jedem Feld höchstens ein Baum. Ob einer dort steht, welche Art, wie
//             groß und wie gedreht, kommt nur aus Seed und Feld (gleicher Seed, gleicher Wald). Wie dicht und welche
//             Arten, sagt das Biom (biomes.json "trees"). Nicht in Flussbetten.
//   Bild      ferne Bäume sind Vorlagen (dieselben Netze, viele Lagen), gebündelt gezeichnet
//   Teile     in der Nähe der Spieler werden sie zu echten, festen Teilen (World::plantTree): sägen, bohren, anzünden,
//             fällen. Unveränderte verschwinden wieder, wenn niemand mehr nah ist; veränderte bleiben und werden
//             gespeichert. Entfernte merkt sich die Welt (World::treesTaken), sie wachsen nicht nach.
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Renderer.h"
#include "Solid.h"
#include "WorldGen.h"

struct TreeType {
    std::string id, label, wood = "oak";
    // Stamm
    double height = 10, diameter = 0.4, topDiameter = 0.08, flare = 1.25, bend = 0.15;
    int sections = 5;
    // Äste (nur Laubbäume)
    int branches = 0;
    double branchFrom = 0.5, branchTo = 0.9, branchLenMin = 2, branchLenMax = 3, branchDiaMin = 0.08, branchDiaMax = 0.12,
           branchRise = 30;
    // Krone
    bool cone = false;  // Nadelbaum (Kegel) statt Büschel
    double crownBottom = 4, crownTop = 12, crownRadius = 3;
    glm::vec3 leafColor{0.15f, 0.27f, 0.07f}, leafColor2{0.08f, 0.17f, 0.04f};
    double scaleMin = 0.85, scaleMax = 1.15;
    int variants = 3;
};

struct TreeConfig {
    double spacing = 6, solidDistance = 40, detailDistance = 150, drawDistance = 450;
    std::vector<TreeType> types;
    std::string text;  // der JSON-Text, aus dem sie stammen (wird in die Welt kopiert)
    int find(const std::string& id) const;  // -1: unbekannt
};
bool parseTreeConfig(const std::string& text, TreeConfig& out, std::string* err);
const TreeConfig& builtinTreeConfig();  // beim Bauen eingebettete Kopie von assets/trees.json
std::string findTreesFile();            // assets/trees.json suchen; leer: nicht gefunden
std::shared_ptr<const TreeConfig> loadTreesFile(std::string* report = nullptr);

// Form einer Art (eine Variante). Ursprung = Mitte des Stammfußes auf dem Boden, Stamm entlang +Y
struct TreeModel {
    solid::Solid wood;                              // Stamm und Äste, reicht 0,4 m in den Boden (Wurzel)
    std::vector<solid::Vertex> leaves;              // Krone (nur Bild)
    std::vector<solid::Vertex> woodLow, leavesLow;  // grob, für ferne Bäume
    glm::dvec3 crownAnchor{0};                      // Stelle am Stamm, an der die Krone hängt (beim Sägen: wer sie behält)
    double height = 0, radius = 0;                  // Höhe über dem Boden, halbe Breite der Krone (m)
    double volume = 0;                              // Holz (m³)
    GpuMesh woodMesh, leafMesh, woodLowMesh, leafLowMesh;  // nur mit Bild
};
TreeModel buildTreeModel(const TreeType& t, uint64_t seed);

// Alle Formen einer Welt (aus ihrer trees.json), dazu ihre Netze fürs Bild
class TreeLibrary {
public:
    TreeLibrary(Renderer* renderer, std::shared_ptr<const TreeConfig> cfg);
    ~TreeLibrary();
    TreeLibrary(const TreeLibrary&) = delete;
    TreeLibrary& operator=(const TreeLibrary&) = delete;

    const TreeConfig& config() const { return *cfg_; }
    std::shared_ptr<const TreeConfig> configPtr() const { return cfg_; }
    int find(const std::string& id) const { return cfg_->find(id); }
    int types() const { return (int)models_.size(); }
    int variants(int type) const { return type >= 0 && type < types() ? (int)models_[(size_t)type].size() : 0; }
    const TreeModel& model(int type, int variant) const;

private:
    Renderer* renderer_;
    std::shared_ptr<const TreeConfig> cfg_;
    std::vector<std::vector<std::unique_ptr<TreeModel>>> models_;  // [Art][Variante]
};

// Ein Baum des Generators
struct TreeSpot {
    uint64_t key = 0;   // Feld des Rasters (eindeutig, gleich bei allen Mitspielern), nie 0
    glm::dvec3 pos{0};  // Stammfuß (Höhe aus dem Generator, ohne Änderungen am Gelände)
    float yaw = 0, scale = 1;
    int type = 0, variant = 0;
};

// Die Bäume einer Welt mit Gelände: Plätze aus dem Seed, je Kachel von 64 m zwischengespeichert (Hauptthread)
class Forest {
public:
    static constexpr double kTile = 64.0;
    Forest(std::shared_ptr<const WorldGen> gen, std::shared_ptr<const TreeLibrary> lib);

    const TreeLibrary& library() const { return *lib_; }
    const WorldGen& gen() const { return *gen_; }
    bool any() const { return any_; }  // irgendein Biom hat Bäume
    // Bäume einer Kachel (berechnet sie bei Bedarf)
    const std::vector<TreeSpot>& tile(int64_t tx, int64_t tz);
    bool tileReady(int64_t tx, int64_t tz) const { return tiles_.count(tileKey(tx, tz)) != 0; }
    // Bäume im Umkreis (berechnet fehlende Kacheln)
    void around(const glm::dvec2& c, double radius, std::vector<TreeSpot>& out);
    // Fehlende Kacheln im Umkreis berechnen, höchstens budget Stück (die nächsten zuerst). Rückgabe: wie viele fehlen noch
    size_t prepare(const glm::dvec2& c, double radius, int budget);
    // nächster Baum (type -1: jede Art); false: keiner bis maxDist
    bool nearest(const glm::dvec2& from, double maxDist, int type, TreeSpot& out);
    // ein Feld berechnen (Selbsttest, Befehle)
    bool spotOf(int64_t cx, int64_t cz, TreeSpot& out) const;
    size_t cachedTiles() const { return tiles_.size(); }
    static uint64_t spotKey(int64_t cx, int64_t cz) {
        return (1ull << 63) | ((uint64_t)(cx & 0x7fffffff) << 32) | (uint64_t)(uint32_t)(cz & 0xffffffff);
    }

private:
    static uint64_t tileKey(int64_t tx, int64_t tz) { return ((uint64_t)(uint32_t)tx << 32) | (uint32_t)tz; }
    std::shared_ptr<const WorldGen> gen_;
    std::shared_ptr<const TreeLibrary> lib_;
    std::vector<std::vector<std::pair<int, double>>> biomeTypes_;  // je Biom: Art (Index) und Gewicht
    std::vector<double> biomeDensity_;                              // je Biom: Bäume je m²
    uint64_t seed_ = 0;
    bool any_ = false;
    std::unordered_map<uint64_t, std::vector<TreeSpot>> tiles_;
};
