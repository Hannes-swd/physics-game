// Materialien: alle Werte stehen in der Datei assets/materials.json (beim Bauen auch ins Programm eingebettet, falls
// die Datei fehlt). Die Feldtabelle materialFields() beschreibt jeden Wert einmal: Einlesen, Speichern, Befehle
// ("materials set ...") und der Material-Editor im Fenster benutzen sie.
#pragma once
#include <glm/glm.hpp>

#include <string>
#include <vector>

// Art der Oberflächen-Darstellung im Shader (prozedurale Textur)
enum class Shading : int { Wood = 0, Brick = 1, Metal = 2, Stone = 3, Rubber = 4, Concrete = 5, Plain = 6, Plastic = 7, Ground = 10 };
// Verhalten: Reibungspaare, Verschleiß, Nägel und Dübel, Faser, Gummi federt zurück ...
enum class MaterialClass : int { Wood = 0, Metal = 1, Stone = 2, Rubber = 3, Plastic = 4 };
// Anlauffarben nach dem Erhitzen
enum class Temper : int { None = 0, Steel = 1, Copper = 2 };

// Wärme. Temperaturen in °C.
struct Thermal {
    float conductivity = 1;        // Wärmeleitfähigkeit λ W/(m·K)
    float specificHeat = 1000;     // spezifische Wärmekapazität c J/(kg·K)
    float emissivity = 0.9f;       // Emissionsgrad ε der Oberfläche (Wärmestrahlung)
    float meltC = -1;              // Schmelzpunkt (Kunststoff: fließt), <= 0: schmilzt nicht (Holz, Gummi zersetzen sich)
    float latentHeat = 0;          // Schmelzwärme J/kg
    float puddleMm = 10;           // Höhe einer Pfütze aus der Schmelze
    float softenC = 300;           // ab hier sinken Festigkeit und Härte
    float halfC = 600;             // hier ist noch die halbe Festigkeit übrig
    float brittleC = -1000;        // Übergang zäh/spröde bei Kälte, < -400: wird nie spröde
    float pyrolysisC = 0;          // ab hier zersetzt es sich und wird schwarz (verkohlt), <= 0: nie
    float ignitionC = 0;           // Zündtemperatur mit Flamme in der Nähe (ohne Flamme etwa 150 °C mehr), <= 0: brennt nicht
    float heatOfCombustion = 0;    // Heizwert J/kg
    float burnRate = 0;            // Abbrand an der Oberfläche kg/(m²·s) bei freiem Brand
    float soot = 0;                // Rauch 0..1 (Holz wenig, Gummi und Kunststoff viel)
};

struct Material {
    const char* name = "";         // Name in Befehlen und Ausgaben (englisch, klein). Zeigt in einen festen Speicher
    const char* label = "";        // Anzeige in den Menüs (deutsch)
    std::vector<std::string> aliases;  // weitere Namen (alte Szenen, deutsch)
    MaterialClass cls = MaterialClass::Stone;
    Shading shading = Shading::Plain;
    glm::vec3 color{0.6f};         // Grundfarbe (linear)
    glm::vec3 color2{0.5f};        // zweite Farbe (Maserung, Sprenkel)
    float roughness = 0.5f;
    float metallic = 0;
    Temper temper = Temper::None;
    float density = 1000;          // kg/m³
    float frictionStatic = 0.5f;   // Haftreibung gegen sich selbst / typische Partner
    float frictionKinetic = 0.4f;  // Gleitreibung
    float restitution = 0.3f;      // Stoßzahl (0 = plastisch, 1 = elastisch)
    float tensileMPa = 10;         // Zugfestigkeit
    float compressiveMPa = 10;     // Druckfestigkeit
    float youngGPa = 1;            // Elastizitätsmodul
    float hardness = 100;          // Brinell-Härte (MPa), Holz quer zur Faser
    float fractureEnergy = 10000;  // Bruchenergie bei Schlag (J/m² Bruchquerschnitt, abgestimmt auf Alltagserfahrung)
    float rolling = 0.005f;        // Rollreibungszahl c_rr (Rollwiderstand = c_rr · Normalkraft)
    float minBendRatio = -1;       // kleinster Biegeradius innen / Dicke (< 0: lässt sich nicht biegen)
    float splitEnergy = 0;         // Spaltenergie für einen Keil (J/m² Rissfläche, Holz entlang der Faser). 0 = spaltet nicht
    Thermal heat;
    std::string residue;           // Material, das nach dem Abbrennen übrig bleibt (Holz: Holzkohle), leer = Asche
    float residueShare = 0;        // Anteil des Volumens, der als Rest liegen bleibt
};

const std::vector<Material>& materials();
std::vector<Material>& editableMaterials();  // Editor und Befehle; danach World::applyMaterials aufrufen
// Index zu einem Namen (auch Aliasse, z. B. alte deutsche Namen aus gespeicherten Szenen). 0 wenn unbekannt.
int materialIndex(const char* name);
// -1 wenn unbekannt (für Befehle)
int findMaterialIndex(const char* name);
MaterialClass materialClass(int material);
int residueOf(int material);  // Index des Rests nach dem Abbrennen, -1 = Asche

// ---------------------------------------------------------------- Materialdatei
// Beschreibung eines Werts: Schlüssel in der Datei, deutsche Bezeichnung, Gruppe, Art
struct MaterialField {
    enum Type { Text, Words, Choice, Number, Optional, Pair, Color, Name };  // Name: ein anderes Material oder none
    const char* key;
    const char* label;
    const char* group;      // "Allgemein", "Aussehen", "Mechanik", "Wärme"
    Type type;
    const char* unit;
    const char* const* choices;  // bei Choice: Namen (Index = Wert), nullptr-beendet
    float lo, hi;                // sinnvoller Bereich (Editor)
};
const std::vector<MaterialField>& materialFields();
// Wert als Text (wie in der Datei) lesen bzw. setzen. Setzen: false + err bei falschem Wert
std::string materialValue(const Material& m, const MaterialField& f);
bool setMaterialValue(Material& m, const MaterialField& f, const std::string& value, std::string* err);
const MaterialField* findMaterialField(const std::string& key);
// Zahlen hinter einem Feld direkt bearbeiten (Editor): Zeiger auf bis zu 3 Werte, Anzahl in count
float* materialNumbers(Material& m, const MaterialField& f, int& count);
int materialChoice(const Material& m, const MaterialField& f);
void setMaterialChoice(Material& m, const MaterialField& f, int value);

// Materialien aus einer Datei bzw. aus Text laden. Bei Fehlern bleibt die alte Liste; report sammelt Hinweise.
bool loadMaterialFile(const std::string& path, std::string* report);
bool loadMaterialText(const std::string& text, const std::string& source, std::string* report);
void loadBuiltinMaterials();             // die eingebettete Datei (Selbsttest, Rückfall)
std::string materialsText();             // alle Materialien und Paare im Dateiformat
std::string materialText(int material);  // ein Abschnitt
bool saveMaterialFile(const std::string& path, std::string* err);
const std::string& materialFile();       // geladene Datei ("" = eingebaut)
std::string defaultMaterialFile();       // wohin gespeichert wird, wenn nichts geladen war
std::string materialSavePath();          // geladene Datei (eine alte .txt: daneben als .json) oder die Standarddatei
// Neues Material (Kopie von base), Name umbenennen, Text-Feld setzen. -1 + err bei Fehler
int addMaterial(const std::string& name, int base, std::string* err);
bool renameMaterial(int material, const std::string& name, std::string* err);
void setMaterialLabel(int material, const std::string& label);
const char* internString(const std::string& s);  // fester Speicher für Namen (bleibt gültig)

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

// Härte (Brinell, MPa) bei Raumtemperatur: weicheres Material reibt sich schneller ab (Archard)
float hardnessMPa(int material);
// Verschleißkoeffizient k (Archard, trocken): abgetragenes Volumen = k · Normalkraft · Gleitweg / Härte
float wearCoefficient(int a, int b);
// Lochleibungsfestigkeit (MPa) eines Stifts mit Durchmesser d in einer Bohrung. Holz: Last unter dem Winkel zur Faser
// (cosToGrain = |cos|), nach EC5; Metall nach EC3; Stein über die Druckfestigkeit.
float embedmentMPa(int material, double diameterMm, double cosToGrain);
// Scherfestigkeit (MPa) für das Ausreißen zum Rand hin
float shearMPa(int material);

// ---------------------------------------------------------------- Wärme
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
