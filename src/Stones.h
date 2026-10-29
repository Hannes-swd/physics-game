// Steine (siehe docs/19-Steine.md).
//
//   Formen     16 unregelmäßige, kantige Brocken aus dem Seed der Welt (konvexe Hülle zufälliger Punkte auf einem
//              Ellipsoid), 1 m groß, Mittelpunkt = Schwerpunkt. Jeder Stein ist eine davon, gedreht und skaliert.
//   Plätze     je Kachel von 16 m, nur aus Seed und Ort: wie viele, wie groß und aus welchem Material, sagt das Biom
//              (biomes.json "stones"; viele kleine, wenige große). Höhe aus dem Gelände (nach dem Graben neu).
//              Nicht im Flussbett und nicht unter Wasser.
//   Bild       alle als Vorlagen (gleiches Netz, viele Lagen); kleine nur in der Nähe, je kleiner, desto näher.
//   Physik     zuerst gar keine. Große (ab stones.big) werden in der Nähe der Spieler zu festen Teilen wie Bäume
//              (statisch: es wird nichts gerechnet, aber man läuft nicht hindurch); fasst man einen an, wird er beweglich
//              (meist zu schwer zum Tragen). Kleine bleiben Bild, bis man sie aufhebt: dann entsteht ein echtes Teil.
//              Wie bei Bäumen (Object::treeKey): unverändert = nicht gespeichert (entsteht aus dem Seed neu), entfernte
//              merkt sich die Welt.
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

// Eine Form, 1 m groß (größte Ausdehnung), Mittelpunkt = Schwerpunkt
struct StoneShape {
    solid::Solid solid;
    std::vector<solid::Plane> planes;  // zum Treffen mit dem Blick
    double bottom = 0;                 // tiefster Punkt (y, negativ)
    double radius = 0;                 // weitester Punkt vom Mittelpunkt
    GpuMesh mesh;                      // nur mit Bild
};

// Ein Stein des Generators
struct StoneSpot {
    uint64_t key = 0;                  // eindeutig, gleich bei allen Mitspielern, nie 0 (StoneField::isStoneKey)
    glm::dvec3 pos{0};                 // Schwerpunkt
    glm::dquat rot{1, 0, 0, 0};
    double size = 0.1;                 // größte Ausdehnung (m) = Skalierung der Form
    int shape = 0, material = 0;
};

class StoneField {
public:
    static constexpr double kTile = 16.0;
    StoneField(std::shared_ptr<const WorldGen> gen, const Terrain* terrain, Renderer* renderer);
    ~StoneField();
    StoneField(const StoneField&) = delete;
    StoneField& operator=(const StoneField&) = delete;

    bool any() const { return any_; }  // irgendein Biom hat Steine
    const GenConfig::StoneView& view() const { return gen_->config().stoneView; }
    bool big(const StoneSpot& s) const { return s.size >= view().big; }
    int shapes() const { return (int)shapes_.size(); }
    const StoneShape& shape(int i) const { return shapes_[(size_t)i]; }
    // Schlüssel der Steine: Bit 62 gesetzt, Bit 63 frei (Bäume haben Bit 63)
    static bool isStoneKey(uint64_t key) { return (key >> 62) == 1; }

    // Steine einer Kachel (berechnet bei Bedarf). fresh: nach Änderungen am Gelände sofort neu, sonst erst in prepare()
    const std::vector<StoneSpot>& tile(int64_t tx, int64_t tz, bool fresh);
    bool tileReady(int64_t tx, int64_t tz) const { return tiles_.count(tileKey(tx, tz)) != 0; }
    // Steine im Umkreis (aktuell)
    void around(const glm::dvec2& c, double radius, std::vector<StoneSpot>& out);
    // fehlende oder veraltete Kacheln im Umkreis berechnen, höchstens budget (die nächsten zuerst)
    void prepare(const glm::dvec2& c, double radius, int budget);
    // Strahl gegen die Steine (nur Bild, bis maxT); skip: Steine, die nicht zählen (schon Teile oder weggenommen)
    bool raycast(const glm::dvec3& o, const glm::dvec3& dir, double maxT, const std::function<bool(uint64_t)>& skip, StoneSpot& hit,
                 double& t);
    size_t cachedTiles() const { return tiles_.size(); }

private:
    struct Tile {
        std::vector<StoneSpot> spots;
        uint64_t rev = 0;  // Stand der Geländeänderungen beim Berechnen
    };
    static uint64_t tileKey(int64_t tx, int64_t tz) { return ((uint64_t)(uint32_t)tx << 32) | (uint32_t)tz; }
    void compute(int64_t tx, int64_t tz, Tile& t) const;

    std::shared_ptr<const WorldGen> gen_;
    const Terrain* terrain_;
    Renderer* renderer_;
    std::vector<StoneShape> shapes_;
    std::vector<GenConfig::Biome::Stones> biomes_;  // je Biom (fehlt es in der Welt: eingebaute Werte)
    std::vector<int> material_;                     // je Biom
    double maxDensity_ = 0;                         // Steine je m², das dichteste Biom
    uint64_t seed_ = 0;
    bool any_ = false;
    std::unordered_map<uint64_t, Tile> tiles_;
};
