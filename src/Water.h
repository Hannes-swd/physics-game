// Wasser und andere Flüssigkeiten: Becken und Meer (Water.cpp).
// Die Physik rechnet auf der CPU, aber nur dort, wo etwas im Wasser ist: Auftrieb exakt aus den konvexen Stücken (an der
// Wasseroberfläche geschnitten), Widerstand an den Außenflächen, Hohlräume (Boot, Becher) laufen durch ihre Öffnungen voll.
// Ruhendes Wasser kostet nichts. Wellen, Lichtbrechung, Farbe und Lichtflecken am Grund macht der Shader (water.vert/.frag,
// waterWave in common.glsl rechnet dieselben Wellen wie waveHeight hier).
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Solid.h"

struct Liquid {
    const char* name;      // Befehl (englisch)
    const char* label;     // Menü (deutsch)
    float density;         // kg/m³
    float heatTransfer;    // Wärmeübergang ohne Sieden (freie Konvektion) W/(m²·K)
    float boilC;           // Siedepunkt: darüber verdampft es an heißen Teilen (Abschrecken), <= 0: siedet nicht
    glm::vec3 absorb;      // Lichtabsorption je m (rot, grün, blau)
    glm::vec3 scatter;     // Farbe des Streulichts (trübes Wasser leuchtet in dieser Farbe)
    bool quench;           // löscht Feuer
};
const std::vector<Liquid>& liquids();
int findLiquid(const char* name);  // -1 wenn unbekannt

// Grund unter einem Gewässer ohne Wände (Meer; später Seen und Flüsse der offenen Welt): Höhe des Grunds je Zelle (0,5 m),
// in Kacheln von 16 m wie die Chunks. Wasser gibt es nur zwischen Grund und Wasserspiegel: darunter ist Erde, und wo der
// Grund über dem Spiegel liegt, ist Land. Wer dort tiefer gräbt, als der Meeresspiegel liegt, bleibt im Trockenen.
// Ohne eigene Kachel gilt der Standardgrund (kostet nichts, auch in einer unbegrenzten Welt). Später schreibt der
// Weltgenerator das Gelände je Chunk hinein (set). Die Karte wird nicht verändert, sondern ersetzt (Rückgängig und
// Mehrspieler teilen sie): edit() liefert eine Kopie mit neuer Nummer (rev).
struct GroundMap {
    static constexpr int kCells = 32;                     // Zellen je Kachel und Achse
    static constexpr double kCell = 0.5;                  // Zellgröße (m)
    static constexpr double kTile = kCells * kCell;       // Kachel (m), = kChunkSize
    struct Tile {
        std::vector<float> h = std::vector<float>((size_t)kCells * kCells, 0.0f);  // Höhe des Grunds je Zelle (m)
        float lo = 0, hi = 0;                             // niedrigste / höchste Zelle
    };
    double base = 0;                                      // Standardgrund (m)
    std::unordered_map<uint64_t, Tile> tiles;             // Schlüssel: tileKey(Kachel x, Kachel z)
    double tilesLo = 0, tilesHi = 0;                      // niedrigste / höchste Zelle aller Kacheln
    uint64_t rev = 0;                                     // Nummer dieses Stands (neu bei jeder Änderung)

    static uint64_t tileKey(int64_t tx, int64_t tz) { return ((uint64_t)(uint32_t)tx << 32) | (uint32_t)tz; }
    static void tileOf(uint64_t key, int64_t& tx, int64_t& tz) { tx = (int32_t)(key >> 32); tz = (int32_t)(key & 0xffffffffu); }
    double cell(int64_t cx, int64_t cz) const;            // Höhe einer Zelle (Zellnummern weltweit)
    double at(double x, double z) const;                  // Höhe des Grunds (bilinear zwischen den Zellmitten)
    // niedrigste und höchste Höhe im Rechteck (großzügig: je Kachel ihre Grenzen). true: überall gleich (flach)
    bool range(double x0, double z0, double x1, double z1, double& lo, double& hi) const;
    // Kopie mit neuer Nummer, Rechteck auf die Höhe h setzen (Zellen, deren Mitte darin liegt)
    std::shared_ptr<GroundMap> edit() const;
    void set(double x0, double z0, double x1, double z1, double h);
    void updateBounds();                                  // tilesLo/tilesHi nach einer Änderung (auch von base)
    void clearTiles() { tiles.clear(); tilesLo = tilesHi = base; }
};
uint64_t newGroundRev();

// Ein Gewässer. Becken: Quader (lo..hi), Wasserspiegel hi.y. Meer: unbegrenzt in der Fläche, Wasser zwischen dem Grund
// (ground) und dem Wasserspiegel; lo und hi.xz zählen beim Meer nicht.
struct WaterBody {
    uint32_t id = 0;
    int liquid = 0;
    bool sea = false;
    glm::dvec3 lo{0}, hi{0};
    std::shared_ptr<const GroundMap> ground;  // nur Meer (nullptr: Grund 10 m unter dem Spiegel)
    double waves = 0;          // Wellenhöhe: Tal bis Kamm der größten Welle (m)
    glm::dvec3 flow{0};        // Strömung (m/s), z.B. ein Fluss
    bool ownTemp = false;      // false: hat die Lufttemperatur
    double tempC = 20;
    double level() const { return hi.y; }
    // Grund (Becken: der Boden, Meer: die Karte)
    double bottomAt(double x, double z) const { return !sea ? lo.y : ground ? ground->at(x, z) : hi.y - 10.0; }
    // Punkt (waagrecht) gehört zum Gewässer: im Becken, beim Meer überall, wo der Grund unter dem Spiegel liegt
    bool covers(double x, double z) const {
        return sea ? bottomAt(x, z) < hi.y : x >= lo.x && x <= hi.x && z >= lo.z && z <= hi.z;
    }
};
// Standardgrund eines neuen Meers: der Boden der Welt (0), liegt der Spiegel darunter, 10 m unter dem Spiegel
double defaultSeaBottom(double level);

// Strömung eines Gewässers mit Hindernissen (Water.cpp): 2D-Gitter auf der Oberfläche. Teile, die durch die Oberfläche
// ragen und (fast) stillstehen, sind Hindernisse; das Wasser fließt quellenfrei um sie herum (Druckprojektion wie bei
// Rauch-Simulationen). Daraus: Geschwindigkeit je Zelle (Physik: Teile treiben mit), Stau vor Hindernissen nach Bernoulli
// (Δh = (U² − u²) / 2g, Physik und Bild) und Schaum, der mit dem Wasser treibt und zerfällt (nur Bild).
// Becken: das ganze Becken; Meer: ein Fenster um den Blickpunkt.
struct FlowGrid {
    uint32_t water = 0;               // Gewässer
    glm::dvec2 origin{0};             // Ecke (Welt x, z)
    double cell = 0.1;                // Zellgröße (m)
    int nx = 0, nz = 0;
    glm::dvec2 base{0};               // Strömung ohne Hindernisse (m/s)
    std::vector<glm::vec2> u;         // Geschwindigkeit je Zelle (x, z)
    std::vector<float> foam, rise, p; // Schaum 0..1, Stau (m), Druck (Startwert der nächsten Rechnung)
    std::vector<uint8_t> solid;       // Hindernis
    uint64_t solidSig = 0;            // Lage aller Hindernisse: unverändert -> nicht neu rastern
    size_t index(int x, int z) const { return (size_t)x + (size_t)nx * (size_t)z; }
};
// Werte an einem Punkt (bilinear). false: außerhalb des Gitters
bool flowSample(const FlowGrid& g, double x, double z, glm::dvec2* u, double* rise, double* foam);

// Wellen: Summe aus vier Sinuswellen (Tiefwasser: ω² = g·k). Die größte hat die Steilheit 1:14.
// Dieselben Zahlen stehen in shaders/common.glsl (kWaveScale, kWaveAmp, kWaveAngle).
struct WaveSet {
    double k[4] = {}, omega[4] = {}, amp[4] = {};
    glm::dvec2 dir[4];
    double lambda0 = 1;        // Wellenlänge der größten Welle (m)
    bool calm = true;
};
WaveSet waveSet(double height, glm::dvec2 mainDir);
// Auslenkung der Oberfläche (m) bei (x, z) zur Zeit t, grad = Steigung (dh/dx, dh/dz)
double waveHeight(const WaveSet& w, double x, double z, double t, glm::dvec2* grad = nullptr);
// Geschwindigkeit des Wassers in der Tiefe "depth" unter der Oberfläche (Orbitalbewegung der Wellen)
glm::dvec3 waveVelocity(const WaveSet& w, double x, double z, double depth, double t);

// Hohlraum eines Teils (lokal): konvexe Hülle minus Material. Wasser läuft durch die Öffnungen hinein und heraus
struct WaterRoom {
    std::vector<solid::Piece> pieces;
    double volume = 0;
    glm::dvec3 centroid{0}, lo{0}, hi{0};
    std::vector<std::vector<glm::dvec3>> openings;  // Öffnungen auf der Hülle (Polygone), leer: dicht verschlossen
    double openingArea = 0;
};
// Daten eines Teils für das Wasser, bei der ersten Berührung berechnet und behalten, solange sich die Form nicht ändert
struct WaterCache {
    uint32_t rev = 0;                   // Object::netRev, für den sie gilt
    std::vector<solid::Piece> pieces;   // Material: die Stücke (importierte Modelle: ihr Hüllquader)
    std::vector<double> pieceVolume, pieceRadius;
    std::vector<glm::dvec3> pieceCentroid;
    double scale = 1.0;                 // importierte Modelle: echtes Volumen / Volumen des Quaders
    std::vector<WaterRoom> rooms;
};
std::shared_ptr<WaterCache> buildWaterCache(const solid::Solid* s, const glm::dvec3& bbMin, const glm::dvec3& bbMax, double volume);

// Volumen eines konvexen Stücks im Durchschnitt der Halbräume (innen n·x <= d), dazu der Schwerpunkt.
// Mit fullCentroid/radius (umschließende Kugel) und fullVolume: schneller, wenn keine Ebene das Stück schneidet.
double clippedVolume(const solid::Piece& p, const solid::Plane* planes, int count, glm::dvec3& centroid, double fullVolume = -1,
                     const glm::dvec3* fullCentroid = nullptr, double radius = 0);
// Fläche eines ebenen Polygons unterhalb der Ebene n·x = d und Schwerpunkt dieses Teils
double polygonBelow(const std::vector<glm::dvec3>& poly, const glm::dvec3& n, double d, glm::dvec3& centroid);
