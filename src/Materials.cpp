// Materialien: Liste aus der Datei assets/materials.json (Rückfall: beim Bauen eingebettete Kopie), Reibungspaare,
// abgeleitete Werte (Verschleiß, Lochleibung, Festigkeit bei Wärme und Kälte), Schmierstoffe, Wärme-Werkzeuge.
#include "Materials.h"
#include "Json.h"

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
#include "materials.json.inc"
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
        {{"residue", "bleibt nach dem Abbrennen", "Wärme", F::Name, "", nullptr, 0, 0}, {nullptr, 0, 0}},
        {{"residue_share", "Anteil, der übrig bleibt", "Wärme", F::Number, "", nullptr, 0, 1}, {NUM(m.residueShare), 0, 0}},
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
    // zuerst materials.json, sonst eine alte materials.txt
    for (const char* file : {"materials.json", "materials.txt"})
        for (const std::filesystem::path& p : {dir / file, std::filesystem::current_path(ec) / "assets" / file,
                                               dir / "assets" / file, dir.parent_path().parent_path() / "assets" / file})
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

int residueOf(int material) {
    const auto& m = materials();
    if (m.empty() || m[std::clamp(material, 0, (int)m.size() - 1)].residue.empty()) return -1;
    const std::string& r = m[std::clamp(material, 0, (int)m.size() - 1)].residue;
    for (size_t i = 0; i < m.size(); ++i)
        if (r == m[i].name) return (int)i;
    return -1;
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
    case MaterialField::Name: return m.residue.empty() ? "none" : m.residue;
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
    case MaterialField::Name: {
        const std::string l = lower(v.c_str());
        if (l.empty() || l == "none" || l == "-" || l == "ash") {
            m.residue.clear();
            return true;
        }
        if (!validName(l)) {
            if (err) *err = std::string(f.key) + ": '" + l + "' is not a material name (or 'none')";
            return false;
        }
        m.residue = l;  // ob es das Material gibt, prüft das Laden (es darf weiter unten in der Datei stehen)
        return true;
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

namespace {

// Gemeinsamer Abschluss beider Formate: Verweise prüfen, bei Erfolg die Liste übernehmen
bool commitMaterials(std::vector<Material>& list, std::vector<PairEntry>& pairs, const std::string& header, const std::string& source,
                     std::string problems, bool fatal, std::string* report) {
    // Paare dürfen nur bekannte Materialien oder Klassen nennen
    for (const PairEntry& p : pairs)
        for (const std::string& n : {p.a, p.b}) {
            bool known = false;
            for (int i = 0; kClassNames[i]; ++i) known |= n == kClassNames[i];
            for (const Material& m : list) known |= n == m.name;
            if (!known) problems += source + ": pair " + p.a + " " + p.b + ": '" + n + "' is neither a material nor a class\n";
        }
    for (const Material& m : list)
        if (!m.residue.empty()) {
            bool known = false;
            for (const Material& r : list) known |= m.residue == r.name;
            if (!known) problems += source + ": " + m.name + ": residue '" + m.residue + "' is not a material (burns to ashes)\n";
        }
    if (list.empty()) {
        problems += source + ": no material found\n";
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

// JSON-Wert als Text, wie ihn setMaterialValue versteht: Listen mit Leerzeichen, null = none
bool jsonToValue(const Json& j, std::string& out) {
    switch (j.type) {
    case Json::Null: out = "none"; return true;
    case Json::Number: {
        char b[40];
        std::snprintf(b, sizeof(b), "%.9g", j.num);
        out = b;
        return true;
    }
    case Json::String: out = j.str; return true;
    case Json::Array: {
        out.clear();
        for (const Json& e : j.arr) {
            std::string s;
            if (e.type == Json::Array || e.type == Json::Object || !jsonToValue(e, s)) return false;
            out += (out.empty() ? "" : " ") + s;
        }
        return true;
    }
    default: return false;
    }
}

// Materialdatei im JSON-Format: {"materials": [{"name": "steel", "density": 7850, ...}], "pairs": [{"a": .., "b": .., "friction": [.., ..]}]}
// Schlüssel, die mit "_" beginnen, sind Erklärungen und werden übergangen.
bool loadMaterialJson(const std::string& text, const std::string& source, std::string* report) {
    Json doc;
    std::string err;
    if (!parseJson(text, doc, &err)) {
        if (report) *report = source + ": " + err + "\n";
        return false;
    }
    std::vector<Material> list;
    std::vector<PairEntry> pairs;
    std::string problems;
    bool fatal = false;
    if (doc.type != Json::Object || doc["materials"].type != Json::Array) {
        if (report) *report = source + ": expected { \"materials\": [ ... ] }\n";
        return false;
    }
    for (const auto& [key, value] : doc.obj)
        if (key != "materials" && key != "pairs" && (key.empty() || key[0] != '_')) problems += source + ": unknown key '" + key + "'\n";
    for (size_t i = 0; i < doc["materials"].arr.size(); ++i) {
        const Json& jm = doc["materials"].arr[i];
        const std::string where = source + ": material " + std::to_string(i + 1);
        if (jm.type != Json::Object || jm["name"].type != Json::String) {
            problems += where + ": needs \"name\": \"...\"\n";
            fatal = true;
            continue;
        }
        const std::string name = lower(jm["name"].str.c_str());
        if (!validName(name)) {
            problems += where + ": name '" + name + "': only a-z, 0-9, _ and -\n";
            fatal = true;
            continue;
        }
        for (const Material& m : list)
            if (name == m.name) {
                problems += where + ": material '" + name + "' exists twice\n";
                fatal = true;
            }
        Material m;
        m.name = internString(name);
        m.label = m.name;
        for (const auto& [key, value] : jm.obj) {
            if (key == "name" || (!key.empty() && key[0] == '_')) continue;
            const MaterialField* f = findMaterialField(lower(key.c_str()));
            std::string v;
            if (!f) problems += source + ": " + name + ": unknown key '" + key + "'\n";
            else if (!jsonToValue(value, v)) problems += source + ": " + name + ": " + key + ": unexpected value\n";
            else if (!setMaterialValue(m, *f, v, &err)) problems += source + ": " + name + ": " + err + "\n";
        }
        list.push_back(std::move(m));
    }
    for (const Json& jp : doc["pairs"].arr) {
        const Json& fr = jp["friction"];
        if (jp["a"].type != Json::String || jp["b"].type != Json::String || fr.size() != 2 || fr[0].type != Json::Number ||
            fr[1].type != Json::Number) {
            problems += source + ": pair: {\"a\": \"..\", \"b\": \"..\", \"friction\": [static, sliding]} expected\n";
            continue;
        }
        pairs.push_back({lower(jp["a"].str.c_str()), lower(jp["b"].str.c_str()), (float)fr[0].num, (float)fr[1].num});
    }
    return commitMaterials(list, pairs, "", source, problems, fatal, report);
}

// altes Textformat ([material name], key = value)
bool loadMaterialIni(const std::string& text, const std::string& source, std::string* report) {
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
    return commitMaterials(list, pairs, header, source, problems, fatal, report);
}

} // namespace

bool loadMaterialText(const std::string& text, const std::string& source, std::string* report) {
    const size_t first = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (first != std::string::npos && text[first] == '{') return loadMaterialJson(text, source, report);
    return loadMaterialIni(text, source, report);  // alte materials.txt
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
    loadMaterialText(reinterpret_cast<const char*>(kBuiltinMaterials), "built-in materials.json", &report);
    reg().file.clear();
}

const std::string& materialFile() { return reg().file; }

std::string materialSavePath() {
    const std::string& f = reg().file;
    if (f.empty()) return defaultMaterialFile();
    std::filesystem::path p = std::filesystem::u8path(f);
    if (p.extension() != ".json") p.replace_extension(".json");  // alte materials.txt: daneben als JSON
    return p.u8string();
}

std::string defaultMaterialFile() {
    const std::filesystem::path repo = exeDir().parent_path().parent_path() / "assets";
    std::error_code ec;
    if (std::filesystem::is_directory(repo, ec)) return (repo / "materials.json").lexically_normal().u8string();
    return (exeDir() / "materials.json").u8string();
}

namespace {

// ein Material als JSON-Objekt, Schlüssel in der Reihenfolge der Feldtabelle
Json materialJson(const Material& m0) {
    Material& m = const_cast<Material&>(m0);  // nur gelesen
    Json j = Json::makeObject();
    j.set("name", Json::makeString(m.name));
    for (const MaterialField& f : materialFields()) {
        Json v;
        switch (f.type) {
        case MaterialField::Text: v = Json::makeString(m.label); break;
        case MaterialField::Words:
            v = Json::makeArray();
            for (const std::string& a : m.aliases) v.push(Json::makeString(a));
            break;
        case MaterialField::Choice: v = Json::makeString(f.choices[materialChoice(m, f)]); break;
        case MaterialField::Name:
            if (!m.residue.empty()) v = Json::makeString(m.residue);  // sonst null = nur Asche
            break;
        default: {
            int n = 0;
            const float* x = materialNumbers(m, f, n);
            if (!x || (f.type == MaterialField::Optional && materialValue(m, f) == "none")) break;  // null = keins
            if (n == 1) {
                v = Json::makeNumber(std::stod(fmt(x[0])));
            } else {
                v = Json::makeArray();
                for (int i = 0; i < n; ++i) v.push(Json::makeNumber(std::stod(fmt(x[i]))));
            }
        }
        }
        j.set(f.key, std::move(v));
    }
    return j;
}

} // namespace

std::string materialText(int i) {
    const auto& list = materials();
    if (i < 0 || i >= (int)list.size()) return {};
    return writeJson(materialJson(list[i]));
}

std::string materialsText() {
    const Registry& r = reg();
    Json doc = Json::makeObject();
    // Erklärungen (werden beim Laden übergangen): was jeder Wert bedeutet, aus der Feldtabelle
    Json& info = doc.set("_info", Json::makeArray());
    info.push(Json::makeString("Werkstatt - Materialien. Jedes Material ist ein Eintrag in \"materials\"; fehlende Werte nehmen den Standard."));
    info.push(Json::makeString("\"name\" ist der Befehlsname (klein, a-z 0-9 _ -). null schaltet etwas ab (schmilzt nicht, brennt nicht ...)."));
    info.push(Json::makeString("Reibung zweier Materialien: \"pairs\" (a und b sind Materialnamen oder Klassen). Mehr: docs/13-Materialien.md"));
    info.push(Json::makeString("Im Spiel: Ansicht -> Materialien bearbeiten, oder Befehle 'materials ...'."));
    Json& keys = doc.set("_keys", Json::makeObject());
    for (const MaterialField& f : materialFields()) {
        std::string d = std::string(f.group) + ": " + f.label;
        if (*f.unit) d += std::string(" (") + f.unit + ")";
        if (f.type == MaterialField::Choice) {
            d += ", eines von:";
            for (int c = 0; f.choices[c]; ++c) d += std::string(" ") + f.choices[c];
        }
        if (f.type == MaterialField::Pair) d += ", [Haft, Gleit]";
        if (f.type == MaterialField::Color) d += ", [r, g, b] 0..1";
        if (f.type == MaterialField::Words) d += ", Liste von Namen";
        if (f.type == MaterialField::Optional) d += ", null = keins";
        if (f.type == MaterialField::Name) d += ", Materialname oder null (nur Asche)";
        keys.set(f.key, Json::makeString(d));
    }
    Json& mats = doc.set("materials", Json::makeArray());
    for (const Material& m : r.list) mats.push(materialJson(m));
    Json& pairs = doc.set("pairs", Json::makeArray());
    for (const PairEntry& p : r.pairs) {
        Json jp = Json::makeObject();
        jp.set("a", Json::makeString(p.a));
        jp.set("b", Json::makeString(p.b));
        Json fr = Json::makeArray();
        fr.push(Json::makeNumber(std::stod(fmt(p.s))));
        fr.push(Json::makeNumber(std::stod(fmt(p.k))));
        jp.set("friction", std::move(fr));
        pairs.push(std::move(jp));
    }
    return writeJson(doc);
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
    for (Material& m : r.list)
        if (m.residue == old) m.residue = name;
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
