// Bewuchs: Gras, Blumen und Büsche auf dem Gelände (siehe docs/18-Bewuchs.md).
//
//   Nur Bild    keine Physik, keine Kollision: man läuft hindurch. Gras und Blumen wiegen sich im Wind (mesh.vert).
//   Biome       wie dicht, wie hoch und in welchen Farben, sagt das Biom (biomes.json "grass", "flowers", "bushes");
//               an Biomgrenzen weich überblendet. Fehlt es in der Weltdatei (alte Welten): die eingebaute biomes.json.
//   Kacheln     16 × 16 m, je Kachel ein Netz für Gras, eins für Blumen, eins für Büsche (wenige Zeichenaufrufe).
//               Lage nur aus Seed und Ort, Höhe aus dem Gelände (mit Änderungen; nach dem Graben neu gebaut).
//   Leistung    Gras in voller Dichte nur bis flora.grass_distance, nach außen ausgedünnt (die Büschel liegen in
//               zufälliger Reihenfolge im Netz, gezeichnet wird nur ein Anfangsstück) und am Rand in den Boden gesenkt.
//               Dahinter bis zum Dreifachen eine ferne Stufe: wenige, breite Büschel, die beim Übergang aus dem Boden
//               wachsen, während das nahe Gras einsinkt (kein sichtbarer Rand). Büsche weiter.
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Renderer.h"
#include "WorldGen.h"

class Terrain;

class Flora {
public:
    static constexpr double kTile = 16.0;
    explicit Flora(Renderer* renderer) : renderer_(renderer) {}
    ~Flora() { clear(); }
    Flora(const Flora&) = delete;
    Flora& operator=(const Flora&) = delete;

    // Kacheln um das Auge bauen (höchstens budget je Aufruf, die nächsten zuerst), ferne verwerfen
    void update(const Terrain& terrain, const glm::dvec3& eye, int budget);
    // Gras, Blumen, Büsche ins Bild (origin: Darstellungsursprung)
    void draw(FrameDesc& fd, const glm::dvec3& origin, const glm::dvec3& eye,
              const std::function<bool(const glm::dvec3&, const glm::dvec3&)>& visible);
    void clear();
    // Einstellungen -> Grafik: plants = Dichte und Reichweite von Gras und Blumen (0 = keine), view = Reichweite der Büsche
    void setQuality(double plants, double view);

    size_t tiles() const { return tiles_.size(); }
    size_t drawnTufts() const { return drawnTufts_; }

private:
    struct Tile {
        int64_t tx = 0, tz = 0;
        GpuMesh grass, flowers, bushes;
        GpuMesh farGrass;            // ferne Stufe des Grases: wenige, breite Büschel
        GpuMesh grassPart, farPart;  // Anfangsstücke davon (dieses Bild)
        bool hasGrass = false;       // Gras und Blumen gebaut (nur nahe Kacheln)
        bool hasFar = false;         // ferne Stufe gebaut
        double lo = 0, hi = 0;       // Höhenbereich
        uint64_t rev = 0;            // Stand der Geländeänderungen beim Bauen
    };
    // Werte der Biome an einem Ort (überblendet), dazu die Gewichte zum Auswählen der Farben
    struct Mix {
        double grass = 0, hMin = 0.2, hMax = 0.5, patches = 0, flowers = 0, bushes = 0, bMin = 0.6, bMax = 1.4;
        double w[kMaxBiomes] = {};
    };
    void reset(const Terrain& terrain);
    double grassDistance() const;
    double farDistance() const;  // bis hierhin die ferne Stufe des Grases
    double bushDistance() const;
    void build(const Terrain& terrain, Tile& t, bool withGrass, bool withFar);
    void sampleGrid(int64_t tx, int64_t tz, std::vector<Mix>& grid) const;

    static uint64_t key(int64_t tx, int64_t tz) { return ((uint64_t)(uint32_t)tx << 32) | (uint32_t)tz; }

    Renderer* renderer_;
    const Terrain* terrain_ = nullptr;
    std::shared_ptr<const WorldGen> gen_;
    std::vector<GenConfig::Biome::Flora> biomes_;  // je Biom (fehlt es in der Welt: eingebaute Werte)
    uint64_t seed_ = 0;
    double plants_ = 1.0, view_ = 1.0;
    std::unordered_map<uint64_t, Tile> tiles_;
    size_t drawnTufts_ = 0;
};
