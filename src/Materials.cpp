// Materialien: Liste aus der Datei assets/materials.txt (Rückfall: beim Bauen eingebettete Kopie), Reibungspaare,
// abgeleitete Werte (Verschleiß, Lochleibung, Festigkeit bei Wärme und Kälte), Schmierstoffe, Wärme-Werkzeuge.
#include "Materials.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

#include <windows.h>

namespace {

// eingebettet beim Bauen (cmake/embed_file.cmake), mit 0 abgeschlossen
const unsigned char kBuiltinMaterials[] = {
#include "materials.txt.inc"
    0};

std::string lower(const char* s) {
    std::string r = s ? s : "";
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// Reibung für ein Paar aus Materialnamen oder Klassen
struct PairEntry {
    std::string a, b;
    float s = 0.5f, k = 0.4f;
};

struct Registry {
    std::vector<Material> list;
    std::vector<PairEntry> pairs;
    std::string file;    // geladene Datei ("" = eingebaut)
    std::string header;  // Kommentar am Anfang der Datei (bleibt beim Speichern erhalten)
    bool loaded = false;
};

Registry& reg();

const char* const kClassNames[] = {"wood", "metal", "stone", "rubber", "plastic", nullptr};
const char* const kTextureNames[] = {"wood", "brick", "metal", "stone", "rubber", "concrete", "plain", "plastic", nullptr};
const char* const kTemperNames[] = {"none", "steel", "copper", nullptr};

// Zugriff auf die Werte hinter einem Feld
struct FieldAccess {
    float* (*num)(Material&, int&);  // Zahlen (Number, Optional, Pair, Color)
    int none;                        // Optional: 0 = abgeschaltet, wenn <= 0; 1 = wenn < 0; 2 = wenn < -400
    float noneValue;                 // so wird "none" gespeichert
};

#define NUM(expr) [](Material& m, int& n) -> float* { n = 1; return &(expr); }
#define NUM2(expr) [](Material& m, int& n) -> float* { n = 2; return &(expr); }
#define VEC3(expr) [](Material& m, int& n) -> float* { n = 3; return &(expr).x; }
static_assert(sizeof(glm::vec3) == 3 * sizeof(float), "vec3 dicht gepackt");

struct FieldDef {
    MaterialField f;
    FieldAccess a;
};

const std::vector<FieldDef>& fieldDefs() {
    using F = MaterialField;
    // Reihenfolge = Reihenfolge in der Datei und im Editor
    static const std::vector<FieldDef> defs = {
        {{"label", "Anzeigename", "Allgemein", F::Text, "", nullptr, 0, 0}, {nullptr, 0, 0}},
        {{"aliases", "weitere Namen", "Allgemein", F::Words, "", nullptr, 0, 0}, {nullptr, 0, 0}},
        {{"class", "Klasse", "Allgemein", F::Choice, "", kClassNames, 0, 0}, {nullptr, 0, 0}},
        {{"texture", "Textur", "Aussehen", F::Choice, "", kTextureNames, 0, 0}, {nullptr, 0, 0}},
        {{"color", "Farbe", "Aussehen", F::Color, "", nullptr, 0, 1}, {VEC3(m.color), 0, 0}},
        {{"color2", "zweite Farbe", "Aussehen", F::Color, "", nullptr, 0, 1}, {VEC3(m.color2), 0, 0}},
        {{"roughness", "Rauheit", "Aussehen", F::Number, "", nullptr, 0, 1}, {NUM(m.roughness), 0, 0}},
        {{"metallic", "metallisch", "Aussehen", F::Number, "", nullptr, 0, 1}, {NUM(m.metallic), 0, 0}},
        {{"temper", "Anlauffarben", "Aussehen", F::Choice, "", kTemperNames, 0, 0}, {nullptr, 0, 0}},
        {{"density", "Dichte", "Mechanik", F::Number, "kg/m³", nullptr, 1, 25000}, {NUM(m.density), 0, 0}},
        {{"friction", "Haft- / Gleitreibung", "Mechanik", F::Pair, "", nullptr, 0, 2}, {NUM2(m.frictionStatic), 0, 0}},
        {{"rolling", "Rollreibung", "Mechanik", F::Number, "", nullptr, 0, 0.1f}, {NUM(m.rolling), 0, 0}},
        {{"restitution", "Stoßzahl", "Mechanik", F::Number, "", nullptr, 0, 1}, {NUM(m.restitution), 0, 0}},
        {{"tensile", "Zugfestigkeit", "Mechanik", F::Number, "MPa", nullptr, 0, 3000}, {NUM(m.tensileMPa), 0, 0}},
        {{"compressive", "Druckfestigkeit", "Mechanik", F::Number, "MPa", nullptr, 0, 3000}, {NUM(m.compressiveMPa), 0, 0}},
        {{"young", "E-Modul", "Mechanik", F::Number, "GPa", nullptr, 0, 1000}, {NUM(m.youngGPa), 0, 0}},
        {{"hardness", "Härte (Brinell)", "Mechanik", F::Number, "MPa", nullptr, 0.1f, 10000}, {NUM(m.hardness), 0, 0}},
        {{"fracture_energy", "Bruchenergie", "Mechanik", F::Number, "J/m²", nullptr, 100, 1e9f}, {NUM(m.fractureEnergy), 0, 0}},
        {{"split_energy", "Spaltenergie (Keil)", "Mechanik", F::Optional, "J/m²", nullptr, 0, 10000}, {NUM(m.splitEnergy), 0, 0}},
        {{"bend_radius", "kleinster Biegeradius", "Mechanik", F::Optional, "× Dicke", nullptr, 0, 100}, {NUM(m.minBendRatio), 1, -1}},
        {{"conductivity", "Wärmeleitfähigkeit", "Wärme", F::Number, "W/(m·K)", nullptr, 0.01f, 500}, {NUM(m.heat.conductivity), 0, 0}},
        {{"specific_heat", "Wärmekapazität", "Wärme", F::Number, "J/(kg·K)", nullptr, 50, 5000}, {NUM(m.heat.specificHeat), 0, 0}},
        {{"emissivity", "Emissionsgrad", "Wärme", F::Number, "", nullptr, 0.01f, 1}, {NUM(m.heat.emissivity), 0, 0}},
        {{"melts_at", "schmilzt bei", "Wärme", F::Optional, "°C", nullptr, 0, 4000}, {NUM(m.heat.meltC), 0, -1}},
        {{"latent_heat", "Schmelzwärme", "Wärme", F::Number, "J/kg", nullptr, 0, 2e6f}, {NUM(m.heat.latentHeat), 0, 0}},
        {{"puddle_height", "Pfützenhöhe", "Wärme", F::Number, "mm", nullptr, 1, 50}, {NUM(m.heat.puddleMm), 0, 0}},
        {{"soft_from", "weich ab", "Wärme", F::Number, "°C", nullptr, -200, 4000}, {NUM(m.heat.softenC), 0, 0}},
        {{"half_strength_at", "halbe Festigkeit bei", "Wärme", F::Number, "°C", nullptr, -200, 4000}, {NUM(m.heat.halfC), 0, 0}},
        {{"brittle_below", "spröde unter", "Wärme", F::Optional, "°C", nullptr, -273, 200}, {NUM(m.heat.brittleC), 2, -1000}},
        {{"chars_from", "verkohlt ab", "Wärme", F::Optional, "°C", nullptr, 0, 2000}, {NUM(m.heat.pyrolysisC), 0, 0}},
        {{"ignites_at", "brennt ab", "Wärme", F::Optional, "°C", nullptr, 0, 2000}, {NUM(m.heat.ignitionC), 0, 0}},
        {{"heat_of_combustion", "Heizwert", "Wärme", F::Number, "J/kg", nullptr, 0, 6e7f}, {NUM(m.heat.heatOfCombustion), 0, 0}},
        {{"burn_rate", "Abbrand", "Wärme", F::Number, "kg/(m²·s)", nullptr, 0, 0.2f}, {NUM(m.heat.burnRate), 0, 0}},
        {{"smoke", "Rauch", "Wärme", F::Number, "", nullptr, 0, 1}, {NUM(m.heat.soot), 0, 0}},
    };
    return defs;
}

#undef NUM
#undef NUM2
#undef VEC3

const FieldDef* defOf(const MaterialField& f) {
    for (const FieldDef& d : fieldDefs())
        if (std::strcmp(d.f.key, f.key) == 0) return &d;
    return nullptr;
}

bool isNone(int mode, float v) { return mode == 0 ? v <= 0 : mode == 1 ? v < 0 : v < -400; }

// Zahl für die Datei: ganze Zahlen ohne Exponent, sonst bis 6 gültige Stellen
std::string fmt(float v) {
    if (std::abs(v) < 1e10f && v == std::floor(v)) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.0f", (double)v);
        return b;
    }
    char b[32];
    std::snprintf(b, sizeof(b), "%.6g", (double)v);
    return b;
}

bool parseFloat(const std::string& s, float& v) {
    char* end = nullptr;
    v = std::strtof(s.c_str(), &end);
    return end && end != s.c_str() && *end == 0 && std::isfinite(v);
}

std::vector<std::string> words(const std::string& s) {
    std::istringstream ss(s);
    std::vector<std::string> w;
    for (std::string t; ss >> t;) w.push_back(t);
    return w;
}

bool validName(const std::string& n) {
    if (n.empty() || n.size() > 32) return false;
    for (char c : n)
        if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}

std::filesystem::path exeDir() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? std::filesystem::path(buf).parent_path() : std::filesystem::current_path();
}

// Datei suchen: neben dem Programm, im Projektordner (assets), aus dem Build-Ordner heraus
std::string findMaterialFile() {
    std::error_code ec;
    const std::filesystem::path dir = exeDir();
    for (const std::filesystem::path& p : {dir / "materials.txt", std::filesystem::current_path(ec) / "assets" / "materials.txt",
                                           dir / "assets" / "materials.txt", dir.parent_path().parent_path() / "assets" / "materials.txt"})
        if (std::filesystem::is_regular_file(p, ec)) return p.lexically_normal().u8string();
    return {};
}

Registry& reg() {
    static Registry r;
    if (!r.loaded) {
        r.loaded = true;
        std::string report;
        const std::string path = findMaterialFile();
        if (path.empty() || !loadMaterialFile(path, &report)) {
            if (!path.empty()) std::fprintf(stderr, "materials: %s - using the built-in list\n", report.c_str());
            loadBuiltinMaterials();
        } else if (!report.empty()) {
            std::fprintf(stderr, "materials (%s):\n%s", path.c_str(), report.c_str());
        }
    }
    return r;
}

} // namespace

// ============================================================================ Liste

const std::vector<Material>& materials() { return reg().list; }
std::vector<Material>& editableMaterials() { return reg().list; }

const char* internString(const std::string& s) {
    static std::unordered_set<std::string> pool;  // Elemente bleiben an ihrer Adresse
    return pool.insert(s).first->c_str();
}

int findMaterialIndex(const char* name) {
    const auto& m = materials();
    const std::string n = lower(name);
    for (size_t i = 0; i < m.size(); ++i)
        if (n == m[i].name) return (int)i;
    for (size_t i = 0; i < m.size(); ++i)
        for (const std::string& a : m[i].aliases)
            if (n == a) return (int)i;
    return -1;
}

int materialIndex(const char* name) {
    int i = findMaterialIndex(name);
    return i < 0 ? 0 : i;
}

MaterialClass materialClass(int material) {
    const auto& m = materials();
    return m.empty() ? MaterialClass::Stone : m[std::clamp(material, 0, (int)m.size() - 1)].cls;
}

// ============================================================================ Felder

const std::vector<MaterialField>& materialFields() {
    static std::vector<MaterialField> list;
    if (list.empty())
        for (const FieldDef& d : fieldDefs()) list.push_back(d.f);
    return list;
}

const MaterialField* findMaterialField(const std::string& key) {
    for (const MaterialField& f : materialFields())
        if (key == f.key) return &f;
    return nullptr;
}

float* materialNumbers(Material& m, const MaterialField& f, int& count) {
    count = 0;
    const FieldDef* d = defOf(f);
    return d && d->a.num ? d->a.num(m, count) : nullptr;
}

int materialChoice(const Material& m, const MaterialField& f) {
    const std::string k = f.key;
    if (k == "class") return (int)m.cls;
    if (k == "texture") return (int)m.shading;
    if (k == "temper") return (int)m.temper;
    return 0;
}

void setMaterialChoice(Material& m, const MaterialField& f, int v) {
    const std::string k = f.key;
    if (k == "class") m.cls = (MaterialClass)std::clamp(v, 0, 4);
    else if (k == "texture") m.shading = (Shading)std::clamp(v, 0, 7);
    else if (k == "temper") m.temper = (Temper)std::clamp(v, 0, 2);
}

std::string materialValue(const Material& m0, const MaterialField& f) {
    Material& m = const_cast<Material&>(m0);  // nur gelesen
    switch (f.type) {
    case MaterialField::Text: return m.label;
    case MaterialField::Words: {
        std::string s;
        for (const std::string& a : m.aliases) s += (s.empty() ? "" : " ") + a;
        return s;
    }
    case MaterialField::Choice: return f.choices[materialChoice(m, f)];
    default: break;
    }
    int n = 0;
    const float* v = materialNumbers(m, f, n);
    if (!v) return "";
    if (f.type == MaterialField::Optional && isNone(defOf(f)->a.none, v[0])) return "none";
    std::string s;
    for (int i = 0; i < n; ++i) s += (i ? " " : "") + fmt(v[i]);
    return s;
}

bool setMaterialValue(Material& m, const MaterialField& f, const std::string& value, std::string* err) {
    const std::string v = trim(value);
    switch (f.type) {
    case MaterialField::Text:
        if (v.empty()) {
            if (err) *err = "label must not be empty";
            return false;
        }
        m.label = internString(v);
        return true;
    case MaterialField::Words:
        m.aliases.clear();
        for (const std::string& w : words(lower(v.c_str()))) m.aliases.push_back(w);
        return true;
    case MaterialField::Choice: {
        const std::string l = lower(v.c_str());
        for (int i = 0; f.choices[i]; ++i)
            if (l == f.choices[i]) {
                setMaterialChoice(m, f, i);
                return true;
            }
        if (err) {
            *err = std::string(f.key) + " must be one of:";
            for (int i = 0; f.choices[i]; ++i) *err += std::string(" ") + f.choices[i];
        }
        return false;
    }
    default: break;
    }
    int n = 0;
    float* dst = materialNumbers(m, f, n);
    if (!dst) return false;
    const FieldDef* d = defOf(f);
    if (f.type == MaterialField::Optional && (lower(v.c_str()) == "none" || v == "-")) {
        dst[0] = d->a.noneValue;
        return true;
    }
    const std::vector<std::string> w = words(v);
    if ((int)w.size() != n) {
        if (err) *err = std::string(f.key) + " needs " + std::to_string(n) + " number(s)" + (f.type == MaterialField::Optional ? " or 'none'" : "");
        return false;
    }
    float tmp[3];
    for (int i = 0; i < n; ++i)
        if (!parseFloat(w[i], tmp[i])) {
            if (err) *err = std::string(f.key) + ": '" + w[i] + "' is not a number (use a dot: 0.5)";
            return false;
        }
    for (int i = 0; i < n; ++i) dst[i] = tmp[i];
    return true;
}

// ============================================================================ Datei

bool loadMaterialText(const std::string& text, const std::string& source, std::string* report) {
    std::vector<Material> list;
    std::vector<PairEntry> pairs;
    std::string header, problems;
    bool inHeader = true, fatal = false;
    Material* cur = nullptr;
    PairEntry* pair = nullptr;
    std::istringstream in(text);
    int lineNo = 0;
    auto warn = [&](const std::string& s) { problems += source + ":" + std::to_string(lineNo) + ": " + s + "\n"; };
    for (std::string raw; std::getline(in, raw);) {
        ++lineNo;
        if (lineNo == 1 && raw.rfind("\xEF\xBB\xBF", 0) == 0) raw.erase(0, 3);  // BOM
        const size_t hash = raw.find('#');
        const std::string line = trim(hash == std::string::npos ? raw : raw.substr(0, hash));
        if (line.empty()) {
            if (inHeader) header += trim(raw).empty() ? "\n" : raw + "\n";
            continue;
        }
        inHeader = false;
        if (line.front() == '[') {
            cur = nullptr;
            pair = nullptr;
            if (line.back() != ']') {
                warn("']' missing");
                fatal = true;
                continue;
            }
            const std::vector<std::string> w = words(lower(line.substr(1, line.size() - 2).c_str()));
            if (w.size() == 2 && w[0] == "material") {
                if (!validName(w[1])) {
                    warn("material name '" + w[1] + "': only a-z, 0-9, _ and -");
                    fatal = true;
                    continue;
                }
                for (const Material& m : list)
                    if (w[1] == m.name) {
                        warn("material '" + w[1] + "' exists twice");
                        fatal = true;
                    }
                list.emplace_back();
                cur = &list.back();
                cur->name = internString(w[1]);
                cur->label = cur->name;
            } else if (w.size() == 3 && w[0] == "pair") {
                pairs.push_back({w[1], w[2]});
                pair = &pairs.back();
            } else {
                warn("unknown section '" + line + "' ([material <name>] or [pair <a> <b>])");
                fatal = true;
            }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn("'key = value' expected");
            continue;
        }
        const std::string key = lower(trim(line.substr(0, eq)).c_str()), value = trim(line.substr(eq + 1));
        std::string err;
        if (pair) {
            float s, k;
            const std::vector<std::string> w = words(value);
            if (key != "friction" || w.size() != 2 || !parseFloat(w[0], s) || !parseFloat(w[1], k)) warn("pair: 'friction = <static> <sliding>' expected");
            else pair->s = s, pair->k = k;
        } else if (!cur) {
            warn("value outside of a section");
        } else if (const MaterialField* f = findMaterialField(key)) {
            if (!setMaterialValue(*cur, *f, value, &err)) warn(err);
        } else {
            warn("unknown key '" + key + "'");
        }
    }
    // Paare dürfen nur bekannte Materialien oder Klassen nennen
    for (const PairEntry& p : pairs)
        for (const std::string& n : {p.a, p.b}) {
            bool known = false;
            for (int i = 0; kClassNames[i]; ++i) known |= n == kClassNames[i];
            for (const Material& m : list) known |= n == m.name;
            if (!known) problems += source + ": pair " + p.a + " " + p.b + ": '" + n + "' is neither a material nor a class\n";
        }
    if (list.empty()) {
        problems += source + ": no [material ...] section found\n";
        fatal = true;
    }
    if (report) *report = problems;
    if (fatal) return false;
    Registry& r = reg();
    r.list = std::move(list);
    r.pairs = std::move(pairs);
    r.header = header;
    return true;
}

bool loadMaterialFile(const std::string& path, std::string* report) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) {
        if (report) *report = path + ": cannot be read";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    if (!loadMaterialText(ss.str(), std::filesystem::u8path(path).filename().u8string(), report)) return false;
    reg().file = path;
    return true;
}

void loadBuiltinMaterials() {
    std::string report;
    loadMaterialText(reinterpret_cast<const char*>(kBuiltinMaterials), "built-in materials.txt", &report);
    reg().file.clear();
}

const std::string& materialFile() { return reg().file; }

std::string defaultMaterialFile() {
    const std::filesystem::path repo = exeDir().parent_path().parent_path() / "assets";
    std::error_code ec;
    if (std::filesystem::is_directory(repo, ec)) return (repo / "materials.txt").lexically_normal().u8string();
    return (exeDir() / "materials.txt").u8string();
}

std::string materialText(int i) {
    const auto& list = materials();
    if (i < 0 || i >= (int)list.size()) return {};
    const Material& m = list[i];
    std::string s = std::string("[material ") + m.name + "]\n";
    for (const MaterialField& f : materialFields()) {
        std::string key = f.key;
        key.resize(std::max<size_t>(key.size(), 18), ' ');
        s += key + " = " + materialValue(m, f) + "\n";
    }
    return s;
}

std::string materialsText() {
    const Registry& r = reg();
    std::string s = r.header.empty() ? "# Werkstatt - Materialien (Beschreibung der Werte: docs/13-Materialien.md)\n\n" : r.header;
    if (s.size() < 2 || s.substr(s.size() - 2) != "\n\n") s += "\n";
    for (int i = 0; i < (int)r.list.size(); ++i) s += materialText(i) + "\n";
    s += "# ---------------------------------------------------------------- Reibung je Paar\n";
    for (const PairEntry& p : r.pairs) s += "[pair " + p.a + " " + p.b + "]\nfriction = " + fmt(p.s) + " " + fmt(p.k) + "\n";
    return s;
}

bool saveMaterialFile(const std::string& path, std::string* err) {
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    if (!out) {
        if (err) *err = path + ": cannot be written";
        return false;
    }
    out << materialsText();
    if (!out) {
        if (err) *err = path + ": write error";
        return false;
    }
    reg().file = path;
    return true;
}

int addMaterial(const std::string& name0, int base, std::string* err) {
    const std::string name = lower(name0.c_str());
    if (!validName(name)) {
        if (err) *err = "name '" + name + "': only a-z, 0-9, _ and - (e.g. pine, cast_iron)";
        return -1;
    }
    if (findMaterialIndex(name.c_str()) >= 0) {
        if (err) *err = "material '" + name + "' already exists";
        return -1;
    }
    Registry& r = reg();
    Material m = base >= 0 && base < (int)r.list.size() ? r.list[base] : Material();
    m.name = internString(name);
    m.label = m.name;
    m.aliases.clear();
    r.list.push_back(m);
    return (int)r.list.size() - 1;
}

bool renameMaterial(int i, const std::string& name0, std::string* err) {
    Registry& r = reg();
    const std::string name = lower(name0.c_str());
    if (i < 0 || i >= (int)r.list.size()) return false;
    if (!validName(name)) {
        if (err) *err = "name '" + name + "': only a-z, 0-9, _ and -";
        return false;
    }
    const int other = findMaterialIndex(name.c_str());
    if (other >= 0 && other != i) {
        if (err) *err = "material '" + name + "' already exists";
        return false;
    }
    const std::string old = r.list[i].name;
    for (PairEntry& p : r.pairs) {
        if (p.a == old) p.a = name;
        if (p.b == old) p.b = name;
    }
    r.list[i].name = internString(name);
    return true;
}

void setMaterialLabel(int i, const std::string& label) {
    Registry& r = reg();
    if (i >= 0 && i < (int)r.list.size() && !trim(label).empty()) r.list[i].label = internString(trim(label));
}

// ============================================================================ Reibung

void frictionPair(int a, int b, float& s, float& k) {
    const auto& m = materials();
    a = std::clamp(a, 0, (int)m.size() - 1);
    b = std::clamp(b, 0, (int)m.size() - 1);
    const Material &A = m[a], &B = m[b];
    const std::string na = A.name, nb = B.name, ca = kClassNames[(int)A.cls], cb = kClassNames[(int)B.cls];
    auto is = [](const PairEntry& p, const std::string& x, const std::string& y) { return (p.a == x && p.b == y) || (p.a == y && p.b == x); };
    for (const PairEntry& p : reg().pairs)  // erst das Materialpaar, ...
        if (is(p, na, nb)) {
            s = p.s, k = p.k;
            return;
        }
    for (const PairEntry& p : reg().pairs)  // ... dann die Klassen
        if (is(p, ca, cb)) {
            s = p.s, k = p.k;
            return;
        }
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
    const auto& m = materials();
    return m[std::clamp(material, 0, (int)m.size() - 1)].hardness;
}

float wearCoefficient(int a, int b) {
    // Archard, trockenes Gleiten: gleiche Metalle fressen eher (adhäsiv), sonst milder Verschleiß
    const MaterialClass ka = materialClass(a), kb = materialClass(b);
    if (ka == MaterialClass::Metal && kb == MaterialClass::Metal) return a == b ? 1e-4f : 3e-5f;
    if (ka == MaterialClass::Rubber || kb == MaterialClass::Rubber) return 1e-5f;
    if (ka == MaterialClass::Stone || kb == MaterialClass::Stone) return 1e-4f;  // Stein wirkt wie Schleifpapier
    return 5e-5f;
}

float embedmentMPa(int material, double diameterMm, double cosToGrain) {
    const auto& m = materials();
    const Material& mat = m[std::clamp(material, 0, (int)m.size() - 1)];
    switch (mat.cls) {
    case MaterialClass::Wood: {
        // EC5: f_h,0 = 0,082 (1 - 0,01 d) ρ; quer zur Faser geteilt durch k90 = 1,35 + 0,015 d
        const double d = std::clamp(diameterMm, 1.0, 60.0);
        const double fh0 = 0.082 * (1.0 - 0.01 * d) * mat.density;
        const double k90 = 1.35 + 0.015 * d;
        const double c2 = cosToGrain * cosToGrain;
        return float(fh0 / (k90 * (1.0 - c2) + c2));
    }
    case MaterialClass::Metal: return 2.5f * mat.tensileMPa;  // EC3 Lochleibung bei großem Randabstand
    case MaterialClass::Rubber: return mat.tensileMPa;
    case MaterialClass::Plastic: return 1.2f * mat.compressiveMPa;
    default: return 1.5f * mat.compressiveMPa;  // Stein: Teilflächenpressung
    }
}

float shearMPa(int material) {
    const auto& m = materials();
    const Material& mat = m[std::clamp(material, 0, (int)m.size() - 1)];
    switch (mat.cls) {
    case MaterialClass::Wood: return 0.05f * mat.tensileMPa;  // Fichte 4, Eiche 5,5 MPa (Schub entlang der Faser)
    case MaterialClass::Metal:
    case MaterialClass::Plastic: return 0.6f * mat.tensileMPa;
    default: return mat.tensileMPa;  // Stein spaltet über die Zugfestigkeit
    }
}

// ============================================================================ Wärme

const Thermal& thermal(int material) {
    const auto& m = materials();
    return m[std::clamp(material, 0, (int)m.size() - 1)].heat;
}

bool combustible(int material) { return thermal(material).ignitionC > 0; }

float strengthFactor(int material, double c) {
    const Thermal& t = thermal(material);
    double f = 1.0;
    if (c > t.softenC) {
        // Abnahme wie die Abminderungsbeiwerte k_y des Eurocodes (Stahl: 600 °C noch knapp die Hälfte, 1000 °C 5 %)
        const double x = (c - t.softenC) / std::max(1.0f, t.halfC - t.softenC);
        f = 1.0 / (1.0 + x * x * x);
    }
    if (t.meltC > 0 && c > t.meltC - 30) f *= std::clamp((t.meltC - c) / 30.0, 0.0, 1.0);  // am Schmelzpunkt: flüssig
    return (float)std::max(f, 0.0);
}

float brittleness(int material, double c) {
    const Thermal& t = thermal(material);
    if (t.brittleC < -400) return 0.0f;
    const double x = std::clamp((t.brittleC + 15.0 - c) / 60.0, 0.0, 1.0);  // ab 15 °C über dem Übergang, ganz spröde 45 °C darunter
    return (float)(x * x * (3.0 - 2.0 * x));
}

double fractureEnergyAt(int material, double c) {
    const auto& m = materials();
    const double g = m[std::clamp(material, 0, (int)m.size() - 1)].fractureEnergy;
    const double b = brittleness(material, c);
    if (b <= 0) return g;
    constexpr double kBrittle = 3000.0;  // spröde wie Ziegel (J/m²)
    return std::exp(std::log(g) + (std::log(std::min(g, kBrittle)) - std::log(g)) * b);
}

const std::vector<HeatTool>& heatTools() {
    // Wärmeübergang einer auftreffenden Flamme bzw. eines Kältestrahls (gerundete Erfahrungswerte)
    static const std::vector<HeatTool> list = {
        {"lighter", "Feuerzeug", 1000, 60, 0.012f, true},
        {"gas", "Gasbrenner (Propan)", 1900, 250, 0.03f, true},
        {"welding", "Schweißbrenner (Acetylen)", 3100, 3000, 0.012f, true},
        {"spray", "Kältespray", -55, 150, 0.03f, false},
        {"nitrogen", "Flüssigstickstoff", -196, 400, 0.05f, false},
    };
    return list;
}

int findHeatTool(const char* name) {
    const std::string n = lower(name);
    const auto& l = heatTools();
    for (size_t i = 0; i < l.size(); ++i)
        if (n == l[i].name) return (int)i;
    if (n == "torch" || n == "propane" || n == "brenner") return 1;
    if (n == "weld" || n == "acetylene") return 2;
    if (n == "cold" || n == "freeze") return 3;
    if (n == "ln2" || n == "stickstoff") return 4;
    return -1;
}
