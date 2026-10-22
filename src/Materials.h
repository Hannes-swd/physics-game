#pragma once
#include <glm/glm.hpp>
#include <vector>

// Art der Oberflächen-Darstellung im Shader (prozedurale Textur)
enum class Shading : int { Wood = 0, Brick = 1, Metal = 2, Stone = 3, Rubber = 4, Concrete = 5, Plain = 6, Plastic = 7, Ground = 10 };

struct Material {
    const char* name;       // Name in Befehlen und Ausgaben (englisch, klein)
    const char* label;      // Anzeige in den Menüs (deutsch)
    Shading shading;
    glm::vec3 color;        // Grundfarbe (linear)
    glm::vec3 color2;       // zweite Farbe (Maserung, Sprenkel)
    float roughness;
    float metallic;
    float density;          // kg/m³
    float frictionStatic;   // Haftreibung gegen sich selbst / typische Partner
    float frictionKinetic;  // Gleitreibung
    float restitution;      // Stoßzahl (0 = plastisch, 1 = elastisch)
    float tensileMPa;       // Zugfestigkeit
    float compressiveMPa;   // Druckfestigkeit
    float youngGPa;         // Elastizitätsmodul
    float fractureEnergy;   // Bruchenergie bei Schlag (J/m² Bruchquerschnitt, abgestimmt auf Alltagserfahrung)
    float rolling;          // Rollreibungszahl c_rr (Rollwiderstand = c_rr · Normalkraft)
    float minBendRatio;     // kleinster Biegeradius innen / Dicke (< 0: lässt sich nicht biegen)
    float splitEnergy;      // Spaltenergie für einen Keil (J/m² Rissfläche, Holz entlang der Faser). 0 = lässt sich nicht spalten
};

const std::vector<Material>& materials();
// Index zu einem Namen (englisch; alte deutsche Namen aus gespeicherten Szenen werden auch erkannt). 0 wenn unbekannt.
int materialIndex(const char* name);
// -1 wenn unbekannt (für Befehle)
int findMaterialIndex(const char* name);

// Reibung zwischen zwei Materialien (Tabellenwerte für bekannte Paare, sonst gemittelt)
void frictionPair(int a, int b, float& staticMu, float& kineticMu);
float rollingPair(int a, int b);

// Schmierstoffe. Index 0 = keiner. Reibung geschmiert (Grenzreibung), Abrieb-Faktor gegenüber trocken.
struct Lubricant {
    const char* name;    // Befehl (englisch)
    const char* label;   // Menü (deutsch)
    float muS, muK;      // Haft- und Gleitreibung mit diesem Schmierstoff (höchstens; trocken kann schon weniger sein)
    float wear;          // Abrieb im Verhältnis zu trocken
};
const std::vector<Lubricant>& lubricants();
int findLubricant(const char* name);  // -1 wenn unbekannt

// Härte (Brinell, MPa): weicheres Material reibt sich schneller ab (Archard)
float hardnessMPa(int material);
// Verschleißkoeffizient k (Archard, trocken): abgetragenes Volumen = k · Normalkraft · Gleitweg / Härte
float wearCoefficient(int a, int b);
// Lochleibungsfestigkeit (MPa) eines Stifts mit Durchmesser d in einer Bohrung. Holz: Last unter dem Winkel zur Faser
// (cosToGrain = |cos|), nach EC5; Metall nach EC3; Stein über die Druckfestigkeit.
float embedmentMPa(int material, double diameterMm, double cosToGrain);
// Scherfestigkeit (MPa) für das Ausreißen zum Rand hin
float shearMPa(int material);

// ---------------------------------------------------------------- Wärme
// Tabellenwerte bei Raumtemperatur. Temperaturen in °C.
struct Thermal {
    float conductivity;      // Wärmeleitfähigkeit λ W/(m·K)
    float specificHeat;      // spezifische Wärmekapazität c J/(kg·K)
    float emissivity;        // Emissionsgrad ε der Oberfläche (Wärmestrahlung)
    float meltC;             // Schmelzpunkt (Kunststoff: fließt), < 0: schmilzt nicht (Holz, Gummi zersetzen sich)
    float latentHeat;        // Schmelzwärme J/kg
    float softenC;           // ab hier sinken Festigkeit und Härte
    float halfC;             // hier ist noch die halbe Festigkeit übrig
    float brittleC;          // Übergang zäh/spröde bei Kälte (Stahl, Gummi, Kunststoff), < -400: wird nie spröde
    float pyrolysisC;        // ab hier zersetzt es sich und wird schwarz (verkohlt), 0: nie
    float ignitionC;         // Zündtemperatur mit Flamme in der Nähe (ohne Flamme etwa 150 °C mehr), 0: brennt nicht
    float heatOfCombustion;  // Heizwert J/kg
    float burnRate;          // Abbrand an der Oberfläche kg/(m²·s) bei freiem Brand
    float soot;              // Rauch 0..1 (Holz wenig, Gummi und Kunststoff viel)
};
const Thermal& thermal(int material);
bool combustible(int material);
// Anteil der Festigkeit/Härte bei der Temperatur (1 bei Raumtemperatur, nahe dem Schmelzpunkt fast 0)
float strengthFactor(int material, double tempC);
// Sprödigkeit bei Kälte: 0 zäh, 1 ganz spröde
float brittleness(int material, double tempC);
// Bruchenergie (J/m²) bei der Temperatur: Kälte macht Stahl, Gummi und Kunststoff spröde
double fractureEnergyAt(int material, double tempC);

// Wärmequellen zum Erhitzen und Abkühlen (Werkzeug, Befehl torch)
struct HeatTool {
    const char* name;    // Befehl (englisch)
    const char* label;   // Menü (deutsch)
    float tempC;         // Flammen- bzw. Kältemittel-Temperatur
    float h;             // Wärmeübergang an der Auftreffstelle W/(m²·K)
    float radius;        // Größe des Flecks (m)
    bool flame;          // zündet Brennbares an
};
const std::vector<HeatTool>& heatTools();
int findHeatTool(const char* name);  // -1 wenn unbekannt
