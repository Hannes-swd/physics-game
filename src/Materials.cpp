#include "Materials.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

// Werte: typische Tabellenwerte (Holz entlang der Faser, Raumtemperatur, trocken).
const std::vector<Material>& materials() {
    static const std::vector<Material> list = {
        // name       label        shading            color                    color2                   rough metal dichte  haft   gleit  stoß   zug   druck E      Bruch J/m²  roll    biegen
        {"spruce",    "Fichte",    Shading::Wood,     {0.72f, 0.50f, 0.28f},   {0.50f, 0.30f, 0.13f},   0.70f, 0.0f, 470,   0.50f, 0.35f, 0.35f, 80,   40,   11,    40000,      0.005f, 50},
        {"oak",       "Eiche",     Shading::Wood,     {0.46f, 0.30f, 0.15f},   {0.26f, 0.15f, 0.06f},   0.60f, 0.0f, 720,   0.55f, 0.40f, 0.30f, 110,  55,   12.5f, 50000,      0.004f, 50},
        {"brick",     "Ziegel",    Shading::Brick,    {0.50f, 0.15f, 0.07f},   {0.30f, 0.08f, 0.04f},   0.90f, 0.0f, 1800,  0.65f, 0.55f, 0.10f, 3.5f, 20,   15,    3000,       0.008f, -1},
        {"concrete",  "Beton",     Shading::Concrete, {0.45f, 0.45f, 0.43f},   {0.30f, 0.30f, 0.29f},   0.90f, 0.0f, 2400,  0.65f, 0.55f, 0.10f, 2.6f, 30,   31,    4000,       0.006f, -1},
        {"granite",   "Granit",    Shading::Stone,    {0.50f, 0.49f, 0.48f},   {0.10f, 0.10f, 0.11f},   0.45f, 0.0f, 2700,  0.60f, 0.50f, 0.15f, 10,   180,  50,    6000,       0.003f, -1},
        {"steel",     "Stahl",     Shading::Metal,    {0.56f, 0.57f, 0.58f},   {0.45f, 0.46f, 0.47f},   0.35f, 1.0f, 7850,  0.60f, 0.45f, 0.40f, 360,  360,  210,   5e7f,       0.0005f, 1.0f},
        {"aluminium", "Aluminium", Shading::Metal,    {0.91f, 0.92f, 0.92f},   {0.80f, 0.81f, 0.82f},   0.30f, 1.0f, 2700,  0.60f, 0.45f, 0.35f, 150,  150,  70,    2e7f,       0.001f, 1.0f},
        {"brass",     "Messing",   Shading::Metal,    {0.91f, 0.74f, 0.38f},   {0.80f, 0.62f, 0.30f},   0.30f, 1.0f, 8500,  0.50f, 0.40f, 0.35f, 350,  350,  100,   5e7f,       0.0008f, 0.5f},
        {"copper",    "Kupfer",    Shading::Metal,    {0.95f, 0.60f, 0.48f},   {0.85f, 0.50f, 0.40f},   0.30f, 1.0f, 8960,  0.55f, 0.40f, 0.30f, 220,  220,  120,   5e7f,       0.0008f, 0.25f},
        {"rubber",    "Gummi",     Shading::Rubber,   {0.04f, 0.04f, 0.04f},   {0.02f, 0.02f, 0.02f},   0.90f, 0.0f, 1100,  1.00f, 0.80f, 0.80f, 15,   15,   0.01f, 1e9f,       0.015f, 0},
    };
    return list;
}

namespace {

std::string lower(const char* s) {
    std::string r = s ? s : "";
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

// Namen aus alten Szenen (vor der Umstellung auf englische Befehle)
const char* legacyName(const std::string& n) {
    static const char* map[][2] = {{"fichte", "spruce"},   {"eiche", "oak"},          {"ziegel", "brick"},    {"beton", "concrete"},
                                   {"granit", "granite"},  {"stahl", "steel"},        {"messing", "brass"},   {"kupfer", "copper"},
                                   {"gummi", "rubber"},    {"aluminum", "aluminium"}};
    for (auto& m : map)
        if (n == m[0]) return m[1];
    return nullptr;
}

enum class Kind { Wood, Metal, Stone, Rubber };

Kind kindOf(const Material& m) {
    switch (m.shading) {
    case Shading::Wood: return Kind::Wood;
    case Shading::Metal: return Kind::Metal;
    case Shading::Rubber: return Kind::Rubber;
    default: return Kind::Stone;
    }
}

} // namespace

int findMaterialIndex(const char* name) {
    const auto& m = materials();
    std::string n = lower(name);
    if (const char* l = legacyName(n)) n = l;
    for (size_t i = 0; i < m.size(); ++i)
        if (n == m[i].name) return (int)i;
    return -1;
}

int materialIndex(const char* name) {
    int i = findMaterialIndex(name);
    return i < 0 ? 0 : i;
}

// Reibungszahlen trocken, sauber (Tabellenbuch Metall / Engineering Toolbox, gerundet)
void frictionPair(int a, int b, float& s, float& k) {
    const auto& m = materials();
    a = std::clamp(a, 0, (int)m.size() - 1);
    b = std::clamp(b, 0, (int)m.size() - 1);
    const Material &A = m[a], &B = m[b];
    const std::string na = A.name, nb = B.name;
    auto pair = [&](const char* x, const char* y) { return (na == x && nb == y) || (na == y && nb == x); };
    // bekannte Metallpaare
    if (pair("steel", "steel")) { s = 0.74f; k = 0.57f; return; }
    if (pair("aluminium", "steel")) { s = 0.61f; k = 0.47f; return; }
    if (pair("brass", "steel")) { s = 0.51f; k = 0.44f; return; }
    if (pair("copper", "steel")) { s = 0.53f; k = 0.36f; return; }
    if (pair("aluminium", "aluminium")) { s = 1.05f; k = 1.0f; return; }
    if (pair("copper", "copper")) { s = 1.0f; k = 0.8f; return; }
    const Kind ka = kindOf(A), kb = kindOf(B);
    auto kinds = [&](Kind x, Kind y) { return (ka == x && kb == y) || (ka == y && kb == x); };
    if (kinds(Kind::Rubber, Kind::Rubber)) { s = 1.15f; k = 1.0f; return; }
    if (kinds(Kind::Rubber, Kind::Stone)) { s = 1.0f; k = 0.8f; return; }   // Gummi auf Beton/Ziegel
    if (kinds(Kind::Rubber, Kind::Wood)) { s = 0.8f; k = 0.6f; return; }
    if (kinds(Kind::Rubber, Kind::Metal)) { s = 0.8f; k = 0.6f; return; }
    if (kinds(Kind::Wood, Kind::Wood)) { s = 0.5f; k = 0.3f; return; }      // Holz auf Holz, trocken
    if (kinds(Kind::Wood, Kind::Metal)) { s = 0.45f; k = 0.35f; return; }
    if (kinds(Kind::Wood, Kind::Stone)) { s = 0.6f; k = 0.5f; return; }
    if (kinds(Kind::Metal, Kind::Stone)) { s = 0.57f; k = 0.45f; return; }  // Stahl auf Beton
    if (kinds(Kind::Stone, Kind::Stone)) { s = 0.65f; k = 0.55f; return; }
    s = std::sqrt(A.frictionStatic * B.frictionStatic);
    k = std::sqrt(A.frictionKinetic * B.frictionKinetic);
}

float rollingPair(int a, int b) {
    const auto& m = materials();
    a = std::clamp(a, 0, (int)m.size() - 1);
    b = std::clamp(b, 0, (int)m.size() - 1);
    return 0.5f * (m[a].rolling + m[b].rolling);
}

const std::vector<Lubricant>& lubricants() {
    // Grenzreibung mit Schmierfilm (Tabellenwerte, gerundet): Öl/Fett für Metall, Wachs/Seife alter Schreinertrick für Holz
    static const std::vector<Lubricant> list = {
        {"none", "keiner", 10.0f, 10.0f, 1.0f},
        {"oil", "Öl", 0.16f, 0.10f, 0.05f},
        {"grease", "Fett", 0.12f, 0.08f, 0.03f},
        {"wax", "Wachs", 0.20f, 0.14f, 0.20f},
        {"soap", "Seife", 0.25f, 0.18f, 0.30f},
    };
    return list;
}

int findLubricant(const char* name) {
    const std::string n = lower(name);
    const auto& l = lubricants();
    for (size_t i = 0; i < l.size(); ++i)
        if (n == l[i].name) return (int)i;
    if (n == "off" || n == "dry") return 0;
    if (n == "oel" || n == "öl") return 1;
    if (n == "fett") return 2;
    if (n == "wachs") return 3;
    if (n == "seife") return 4;
    return -1;
}

float hardnessMPa(int material) {
    // Brinell-Härte in MPa (HB · 9,81), typische Werte; Holz quer zur Faser
    static const float h[] = {12, 35, 500, 400, 2500, 1400, 600, 1000, 400, 5};
    const int n = (int)(sizeof(h) / sizeof(h[0]));
    return h[std::clamp(material, 0, n - 1)];
}

float wearCoefficient(int a, int b) {
    // Archard, trockenes Gleiten: gleiche Metalle fressen eher (adhäsiv), sonst milder Verschleiß
    const auto& m = materials();
    a = std::clamp(a, 0, (int)m.size() - 1);
    b = std::clamp(b, 0, (int)m.size() - 1);
    const Kind ka = kindOf(m[a]), kb = kindOf(m[b]);
    if (ka == Kind::Metal && kb == Kind::Metal) return a == b ? 1e-4f : 3e-5f;
    if (ka == Kind::Rubber || kb == Kind::Rubber) return 1e-5f;
    if (ka == Kind::Stone || kb == Kind::Stone) return 1e-4f;  // Stein wirkt wie Schleifpapier
    return 5e-5f;
}

float embedmentMPa(int material, double diameterMm, double cosToGrain) {
    const auto& m = materials();
    const Material& mat = m[std::clamp(material, 0, (int)m.size() - 1)];
    switch (kindOf(mat)) {
    case Kind::Wood: {
        // EC5: f_h,0 = 0,082 (1 - 0,01 d) ρ; quer zur Faser geteilt durch k90 = 1,35 + 0,015 d
        const double d = std::clamp(diameterMm, 1.0, 60.0);
        const double fh0 = 0.082 * (1.0 - 0.01 * d) * mat.density;
        const double k90 = 1.35 + 0.015 * d;
        const double c2 = cosToGrain * cosToGrain;
        return float(fh0 / (k90 * (1.0 - c2) + c2));
    }
    case Kind::Metal: return 2.5f * mat.tensileMPa;       // EC3 Lochleibung bei großem Randabstand
    case Kind::Rubber: return mat.tensileMPa;
    default: return 1.5f * mat.compressiveMPa;            // Stein: Teilflächenpressung
    }
}

float shearMPa(int material) {
    const auto& m = materials();
    const Material& mat = m[std::clamp(material, 0, (int)m.size() - 1)];
    switch (kindOf(mat)) {
    case Kind::Wood: return 0.05f * mat.tensileMPa;   // Fichte 4, Eiche 5,5 MPa (Schub entlang der Faser)
    case Kind::Metal: return 0.6f * mat.tensileMPa;
    default: return mat.tensileMPa;                    // Stein spaltet über die Zugfestigkeit
    }
}
