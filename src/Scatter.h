// Zeug am Boden: Steine, Stöcke, umgefallene Stämme, Baumstümpfe (siehe docs/19-Steine-und-Holz.md).
//
//   Formen     je Art einige Vorlagen aus dem Seed der Welt, Mittelpunkt = Schwerpunkt. Steine: kantige Brocken (konvexe
//              Hülle zufälliger Punkte auf einem Ellipsoid, 1 m groß). Stöcke, Stämme, Stümpfe: Kegelstümpfe mit Rinde wie
//              die Bäume (Stöcke 1 m lang, auch gebogen oder mit Zweig; Stämme 1 m lang, mit Aststummeln; Stümpfe 1 m
//              breit), in verschiedenen Schlankheiten. Jedes Stück ist eine davon, gedreht und gleichmäßig skaliert.
//   Plätze     je Kachel von 16 m, nur aus Seed und Ort; wie viele, wie groß und woraus, sagt das Biom (biomes.json:
//              "stones", "sticks", "logs", "stumps"). Liegende Stämme folgen dem Hang; große Stücke nie in einem Baum.
//              Nicht im Flussbett, nicht unter Wasser. Höhe aus dem Gelände (nach dem Graben neu).
//   Bild       alle als Vorlagen (gleiches Netz, viele Lagen); kleine Steine und Stöcke nur in der Nähe.
//   Physik     zuerst gar keine. Große (Steine ab scatter.big_stone, Stämme, Stümpfe) werden in der Nähe der Spieler zu
//              festen Teilen wie Bäume (statisch: es wird nichts gerechnet, man läuft aber nicht hindurch). Fasst man eins
//              an, wird es beweglich (Stümpfe nicht: sie wurzeln). Kleine bleiben Bild, bis man sie aufhebt.
//              Wie bei Bäumen (Object::treeKey): unverändert = nicht gespeichert, entfernte merkt sich die Welt.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Renderer.h"
#include "Solid.h"
#include "WorldGen.h"

class Terrain;
class Forest;

// Eine Vorlage, Mittelpunkt = Schwerpunkt
struct ScatterShape {
    int kind = kScatterStone;
    solid::Solid solid;
    std::vector<std::vector<solid::Plane>> planes;  // je Stück (zum Treffen mit dem Blick)
    double bottom = 0;                              // tiefster Punkt (y, negativ)
    double radius = 0;                              // weitester Punkt vom Mittelpunkt
    double aspect = 0;                              // Stöcke, Stämme: Dicke / Länge; Stümpfe: Höhe / Breite
    GpuMesh mesh;                                   // nur mit Bild
};

// Ein Stück des Generators
struct ScatterSpot {
    uint64_t key = 0;        // eindeutig, gleich bei allen Mitspielern, nie 0 (ScatterField::isKey)
    int kind = kScatterStone;
    glm::dvec3 pos{0};       // Schwerpunkt
    glm::dquat rot{1, 0, 0, 0};
    double size = 0.1;       // Skalierung der Vorlage: Steine größte Ausdehnung, Stöcke/Stämme Länge, Stümpfe Breite (m)
    int shape = 0, material = 0;
};

class ScatterField {
public:
    static constexpr double kTile = 16.0;
    // forest: Bäume (große Stücke liegen nicht in ihnen), darf fehlen
    ScatterField(std::shared_ptr<const WorldGen> gen, const Terrain* terrain, Forest* forest, Renderer* renderer);
    ~ScatterField();
    ScatterField(const ScatterField&) = delete;
    ScatterField& operator=(const ScatterField&) = delete;

    bool any() const { return any_; }
    const GenConfig::ScatterView& view() const { return gen_->config().scatterView; }
    // groß: in der Nähe ein festes Teil (man läuft nicht hindurch); sonst nur Bild, bis man es aufhebt
    bool big(const ScatterSpot& s) const {
        return s.kind == kScatterLog || s.kind == kScatterStump || (s.kind == kScatterStone && s.size >= view().bigStone);
    }
    static bool rooted(int kind) { return kind == kScatterStump; }  // bleibt fest, auch wenn man ihn anfasst
    int shapes() const { return (int)shapes_.size(); }
    const ScatterShape& shape(int i) const { return shapes_[(size_t)i]; }
    // Schlüssel: Bit 62 gesetzt (Bäume haben Bit 63), Art in Bit 56..59
    static bool isKey(uint64_t key) { return (key >> 62) == 1; }
    static int kindOf(uint64_t key) { return (int)((key >> 56) & 0xF); }

    // Stücke einer Kachel (berechnet bei Bedarf). fresh: nach Änderungen am Gelände sofort neu, sonst erst in prepare()
    const std::vector<ScatterSpot>& tile(int64_t tx, int64_t tz, bool fresh);
    bool tileReady(int64_t tx, int64_t tz) const { return tiles_.count(tileKey(tx, tz)) != 0; }
    // Stücke im Umkreis (aktuell)
    void around(const glm::dvec2& c, double radius, std::vector<ScatterSpot>& out);
    // fehlende oder veraltete Kacheln im Umkreis berechnen, höchstens budget (die nächsten zuerst)
    void prepare(const glm::dvec2& c, double radius, int budget);
    // Strahl gegen die Stücke (nur Bild, bis maxT); skip: Stücke, die nicht zählen (schon Teile oder weggenommen)
    bool raycast(const glm::dvec3& o, const glm::dvec3& dir, double maxT, const std::function<bool(uint64_t)>& skip, ScatterSpot& hit,
                 double& t);
    size_t cachedTiles() const { return tiles_.size(); }

private:
    struct Tile {
        std::vector<ScatterSpot> spots;
        uint64_t rev = 0;  // Stand der Geländeänderungen beim Berechnen
    };
    static uint64_t tileKey(int64_t tx, int64_t tz) { return ((uint64_t)(uint32_t)tx << 32) | (uint32_t)tz; }
    void compute(int64_t tx, int64_t tz, Tile& t);
    bool clearOfTrees(const glm::dvec3& a, const glm::dvec3& b, double radius);  // Strecke a..b mit Radius trifft keinen Stamm
    int pickShape(int kind, double aspect, double r) const;

    std::shared_ptr<const WorldGen> gen_;
    const Terrain* terrain_;
    Forest* forest_;
    Renderer* renderer_;
    std::vector<ScatterShape> shapes_;
    std::vector<int> kindShapes_[kScatterKinds];                     // Vorlagen je Art
    std::vector<GenConfig::Biome::Scatter> biomes_[kScatterKinds];  // je Art und Biom (fehlt es in der Welt: eingebaute Werte)
    std::vector<std::vector<int>> materials_[kScatterKinds];        // je Art und Biom: Materialien
    double maxDensity_[kScatterKinds] = {};                          // je m², das dichteste Biom
    uint64_t seed_ = 0;
    bool any_ = false;
    std::unordered_map<uint64_t, Tile> tiles_;
};
