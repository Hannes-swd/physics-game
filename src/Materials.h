#pragma once
#include <glm/glm.hpp>
#include <vector>

// Art der Oberflächen-Darstellung im Shader (prozedurale Textur)
enum class Shading : int { Wood = 0, Brick = 1, Metal = 2, Stone = 3, Rubber = 4, Concrete = 5, Plain = 6, Ground = 10 };

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
