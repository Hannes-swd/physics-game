// Gelände: Oberfläche aus dem Generator (WorldGen), in Kacheln mit Detailstufen gezeichnet, Physik als Höhenfeld je
// Chunk, Änderungen je Chunk gespeichert (siehe docs/16-Weltgenerierung.md).
//
//   Raster     Rasterpunkt (i, j) liegt bei (i · 0,5 m, j · 0,5 m). Höhe = Generator + Änderung des Punkts.
//              Physik und Bild teilen jedes Viereck entlang der Diagonale (i, j) -> (i+1, j+1): gleiche Oberfläche.
//   Chunk      16 × 16 m (32 × 32 Punkte), wie die Chunks der Teile. Einheit für Physik und Änderungen.
//   Kachel     32 × 32 Vierecke; Stufe L hat Vierecke von 0,5 · 2^L m (Stufe 0 = ein Chunk, Stufe 7 = 2048 m).
//              Nahe der Kamera feine, weiter weg grobe Kacheln. Ränder mit "Schürzen" nach unten (keine Spalten
//              zwischen Stufen). Erzeugt im Hintergrund (Jobs), nur die Oberfläche.
//   Änderungen nur geänderte Chunks, in Regionsdateien (32 × 32 Chunks = 512 m): regions/r.<x>.<z>.bin
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Physics.h"
#include "Renderer.h"
#include "Water.h"
#include "WorldGen.h"

constexpr int kTerrainCells = 32;                                // Vierecke je Kachel und Achse, Punkte je Chunk
constexpr double kTerrainCell = 0.5;                             // feinstes Raster (m)
constexpr double kTerrainChunk = kTerrainCells * kTerrainCell;   // 16 m
constexpr int kTerrainLevels = 8;                                // Stufen 0..7

struct TerrainKey {
    int level = 0;
    int64_t x = 0, z = 0;
    bool operator==(const TerrainKey& o) const { return level == o.level && x == o.x && z == o.z; }
};
struct TerrainKeyHash {
    size_t operator()(const TerrainKey& k) const {
        uint64_t h = (uint64_t)k.x * 0x9E3779B97F4A7C15ull ^ ((uint64_t)k.z * 0xC2B2AE3D27D4EB4Full + (uint64_t)k.level);
        return (size_t)(h ^ (h >> 29));
    }
};

// Höhenänderung eines Chunks (m) je Rasterpunkt: Punkt (cx·32 + a, cz·32 + b) bei d[a + 32·b]
struct TerrainEdit {
    float d[kTerrainCells * kTerrainCells] = {};
};

class Terrain {
public:
    Terrain(Physics& physics, Renderer* renderer, std::shared_ptr<const WorldGen> gen, const std::filesystem::path& regionDir);
    ~Terrain();

    const WorldGen& gen() const { return *gen_; }
    std::shared_ptr<const WorldGen> genPtr() const { return gen_; }
    // Neue Einstellungen ("terrain reload"): alles wird neu erzeugt, die Änderungen bleiben
    void setGen(std::shared_ptr<const WorldGen> gen);

    // ---- Abfragen. So, wie Physik und Bild die Oberfläche haben. Auch aus Hintergrund-Threads (Wasserphysik), solange
    // der Hauptthread nicht gleichzeitig ändert
    double latticeHeight(int64_t i, int64_t j) const;
    bool latticeWet(int64_t i, int64_t j) const;  // im Flussbett (Generator)
    double latticeHeightFresh(int64_t i, int64_t j) const;  // ohne Zwischenspeicher (zum Prüfen)
    double surfaceHeight(double x, double z, glm::dvec3* normal = nullptr) const;
    // Strahl gegen die Oberfläche (bis maxT); t und Normale des Treffers
    bool raycast(const glm::dvec3& origin, const glm::dvec3& dir, double maxT, double& t, glm::dvec3& normal) const;
    // höchster Punkt der Oberfläche unter einem Rechteck (Teile absetzen)
    double maxHeightUnder(double x0, double z0, double x1, double z1) const;
    // nächste Stelle, an der das Biom ganz es selbst ist (außerhalb des Übergangs; auf 0,5 m genau, nur das Klima: schnell).
    // Liegt from schon darin: from. false: nicht bis maxDist
    bool findBiome(int biome, const glm::dvec2& from, double maxDist, glm::dvec2& out) const;

    // ---- Flüsse: ein Gewässer (World), Wasser zwischen Grund und riverLevel()
    bool hasRivers() const { return gen_->hasRivers(); }
    double riverLevel() const { return gen_->riverLevel(); }
    // Grund unter dem Flusswasser: im Flussbett der Boden; sonst weit über dem Spiegel (dort ist kein Wasser, auch wenn
    // man tiefer gräbt als der Fluss)
    double waterBottom(double x, double z) const;
    glm::dvec3 riverFlow(double x, double z) const;  // Strömung (m/s), in der Mitte schneller; weit weg 0
    // nächste Stelle mit Flusswasser (auf 0,5 m); false: keine bis maxDist
    bool findRiver(const glm::dvec2& from, double maxDist, glm::dvec2& out) const;
    bool waterInView() const;                       // eine gezeichnete Kachel reicht unter den Wasserspiegel
    uint64_t waterRevision() const { return editRev_ * 1000003ull + genRev_; }  // neu bei Änderungen und neuem Generator
    std::shared_ptr<const WaterShape> waterShape() const;  // Form des Gewässers "Flüsse" (World::syncTerrainWater)

    // ---- je Bild
    // Kacheln um das Auge wählen, Erzeugung anstoßen, Fertiges übernehmen (nur mit Bild)
    void updateView(const glm::dvec3& eye);
    // Physik: Höhenfelder für alle Chunks unter diesen Bereichen (Teile, Spielfigur); andere nach einer Weile weg
    void updatePhysics(const std::vector<std::pair<glm::dvec3, glm::dvec3>>& boxes);

    struct Drawn {
        const GpuMesh* mesh = nullptr;
        glm::dvec3 origin{0};                    // Ecke der Kachel (Welt, y = 0)
        glm::dvec3 lo{0}, hi{0};                 // Begrenzung (Welt)
        int level = 0;
        const std::vector<LineVertex>* border = nullptr;  // Biomgrenzen, relativ zu origin
    };
    const std::vector<Drawn>& drawList() const { return draw_; }
    size_t missingTiles() const { return missing_; }       // gewünschte Kacheln, die noch fehlen (Ladebildschirm)
    size_t pendingJobs() const { return inFlight_; }

    // ---- Änderungen
    void raise(double x, double z, double radius, double amount);     // Hügel (amount > 0) oder Mulde, weich auslaufend
    void flatten(double x, double z, double radius, double height);   // auf diese Höhe bringen, weich auslaufend
    void clearEdits();
    size_t editedChunks() const { return edits_.size(); }
    bool save(std::string* err);                                      // geänderte Regionen schreiben
    // Alle Änderungen (Mehrspieler: an die Mitspieler) bzw. alle ersetzen
    std::vector<std::pair<uint64_t, TerrainEdit>> allEdits() const;
    void setEdits(const std::vector<std::pair<uint64_t, TerrainEdit>>& list);
    uint64_t editRevision() const { return editRev_; }

    std::string statusText() const;
    static uint64_t chunkKey(int64_t cx, int64_t cz) { return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz; }
    static void chunkOf(uint64_t key, int64_t& cx, int64_t& cz) { cx = (int32_t)(key >> 32); cz = (int32_t)(key & 0xffffffffu); }

private:
    struct Tile {
        TerrainKey key;
        std::vector<float> h;            // (N+3)² Höhen, Punkt (i, j) mit i, j = -1 .. N+1 bei (i+1) + (N+3)·(j+1)
        float lo = 0, hi = 0;            // niedrigste / höchste Höhe der Kachel (ohne Schürzen)
        GpuMesh mesh;
        std::vector<LineVertex> border;
        bool ready = false, dirty = false;
        uint64_t job = 0;                // laufende Erzeugung (0: keine)
        double lastUsed = 0;
    };
    struct Result;
    struct Queue;
    using EditMap = std::unordered_map<uint64_t, std::shared_ptr<const TerrainEdit>>;

    static double tileSize(int level) { return kTerrainChunk * double(1ll << level); }
    // Eine Kachel erzeugen (Hintergrund-Thread: liest nur gen und die übergebenen Änderungen)
    static void generateTile(const WorldGen& gen, const EditMap& edits, const TerrainKey& key, bool withMesh, Result& out);
    bool selectTile(const TerrainKey& key, const glm::dvec3& eye, double eyeAbove, std::vector<Drawn>& out, std::vector<std::pair<double, TerrainKey>>& want);
    void request(Tile& t, double priority, std::vector<std::pair<double, TerrainKey>>& want);
    void collect();
    EditMap editsFor(int64_t i0, int64_t j0, int64_t i1, int64_t j1) const;
    void markDirty(int64_t i0, int64_t j0, int64_t i1, int64_t j1);
    void rebuildBodies(int64_t i0, int64_t j0, int64_t i1, int64_t j1);
    BodyHandle buildBody(int64_t cx, int64_t cz);
    TerrainEdit& editChunk(int64_t cx, int64_t cz);  // zum Ändern (Kopie, falls eine Erzeugung sie noch liest)
    void afterEdit(int64_t i0, int64_t j0, int64_t i1, int64_t j1);
    double edit(int64_t i, int64_t j) const;
    void loadRegions();
    double now() const;

    Physics& physics_;
    Renderer* renderer_;
    std::shared_ptr<const WorldGen> gen_;
    std::filesystem::path regionDir_;

    std::unordered_map<TerrainKey, Tile, TerrainKeyHash> tiles_;
    std::vector<Drawn> draw_;
    size_t missing_ = 0, inFlight_ = 0;
    uint64_t nextJob_ = 1;
    std::shared_ptr<Queue> queue_;

    struct Body {
        BodyHandle handle = kNoBody;
        double lastNeeded = 0;
    };
    std::unordered_map<uint64_t, Body> bodies_;     // Physik je Chunk

    EditMap edits_;
    std::unordered_set<uint64_t> dirtyRegions_;
    uint64_t editRev_ = 0, genRev_ = 0;

    struct Point {
        float h = 0;       // Höhe mit Änderung
        bool wet = false;  // im Flussbett
    };
    Point latticePoint(int64_t i, int64_t j) const;
    void clearCache();
    mutable std::unordered_map<uint64_t, Point> heightCache_;  // Rasterpunkte (Abfragen)
    mutable std::mutex cacheMutex_;
};
