// Einstellungen lesen und schreiben (siehe Settings.h)
#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "Json.h"
#include "Worlds.h"

namespace {

std::filesystem::path settingsFile() { return worlds::dataDir() / L"settings.json"; }

Json makeBool(bool b) {
    Json j;
    j.type = Json::Bool;
    j.num = b ? 1 : 0;
    return j;
}

// Grafikstufen (Werte wie in GameSettings)
struct Preset {
    int msaa;
    bool supersampling;
    int shadowQuality;
    double viewDistance, plants;
};
constexpr Preset kPresets[kGraphicsPresets] = {
    {1, false, 0, 0.6, 0.4},  // Niedrig
    {2, false, 1, 0.8, 0.7},  // Mittel
    {4, false, 2, 1.0, 1.0},  // Hoch
    {8, true, 3, 1.5, 1.5},   // Ultra
};

} // namespace

const char* const kGraphicsPresetNames[kGraphicsPresets] = {"low", "medium", "high", "ultra"};

void applyGraphicsPreset(GameSettings& s, int preset) {
    const Preset& p = kPresets[std::clamp(preset, 0, kGraphicsPresets - 1)];
    s.msaa = p.msaa;
    s.supersampling = p.supersampling;
    s.shadowQuality = p.shadowQuality;
    s.viewDistance = p.viewDistance;
    s.plants = p.plants;
}

int graphicsPresetOf(const GameSettings& s) {
    for (int i = 0; i < kGraphicsPresets; ++i) {
        const Preset& p = kPresets[i];
        if (s.msaa == p.msaa && s.supersampling == p.supersampling && s.shadowQuality == p.shadowQuality &&
            std::abs(s.viewDistance - p.viewDistance) < 0.01 && std::abs(s.plants - p.plants) < 0.01)
            return i;
    }
    return -1;
}

int shadowMapSize(int q) {
    static const int size[] = {2048, 2048, 4096, 8192};
    return size[std::clamp(q, 0, 3)];
}

double shadowRange(int q) {
    static const double range[] = {10, 14, 18, 36};
    return range[std::clamp(q, 0, 3)];
}

std::string defaultPlayerName() {
    const char* user = std::getenv("USERNAME");
    return user && *user ? user : "Spieler";
}

GameSettings loadSettings() {
    GameSettings s;
    std::ifstream in(worlds::existingOrBackup(settingsFile()), std::ios::binary);
    if (!in) return s;
    std::ostringstream ss;
    ss << in.rdbuf();
    Json j;
    if (!parseJson(ss.str(), j, nullptr) || j.type != Json::Object) return s;
    if (j["playerName"].type == Json::String) s.playerName = j["playerName"].str;
    if (j["mouseSensitivity"].type == Json::Number) s.mouseSensitivity = std::clamp(j["mouseSensitivity"].num, 0.1, 5.0);
    if (j["fov"].type == Json::Number) s.fovDeg = std::clamp(j["fov"].num, 30.0, 110.0);
    if (j["shadows"].type == Json::Bool) s.shadows = j["shadows"].num != 0;
    if (j["edges"].type == Json::Bool) s.edges = j["edges"].num != 0;
    if (j["grid"].type == Json::Bool) s.grid = j["grid"].num != 0;
    // Biomgrenzen: bis Version 1 der Datei stand der alte Standard (an) drin, ohne dass man ihn gewählt hatte
    if (j["biomeBorders"].type == Json::Bool && j["version"].type == Json::Number && j["version"].num >= 2) s.biomeBorders = j["biomeBorders"].num != 0;
    if (j["autosaveMinutes"].type == Json::Number) s.autosaveMinutes = std::clamp((int)j["autosaveMinutes"].num, 0, 120);
    if (const Json& g = j["graphics"]; g.type == Json::Object) {
        if (g["msaa"].type == Json::Number) {
            const int m = (int)g["msaa"].num;
            s.msaa = m >= 8 ? 8 : m >= 4 ? 4 : m >= 2 ? 2 : 1;
        }
        if (g["supersampling"].type == Json::Bool) s.supersampling = g["supersampling"].num != 0;
        if (g["shadowQuality"].type == Json::Number) s.shadowQuality = std::clamp((int)g["shadowQuality"].num, 0, 3);
        if (g["viewDistance"].type == Json::Number) s.viewDistance = std::clamp(g["viewDistance"].num, 0.5, 2.0);
        if (g["plants"].type == Json::Number) s.plants = std::clamp(g["plants"].num, 0.0, 1.5);
        if (g["vsync"].type == Json::Bool) s.vsync = g["vsync"].num != 0;
    }
    return s;
}

bool saveSettings(const GameSettings& s, std::string* err) {
    Json j = Json::makeObject();
    j.set("version", Json::makeNumber(2));
    j.set("playerName", Json::makeString(s.playerName));
    j.set("mouseSensitivity", Json::makeNumber(s.mouseSensitivity));
    j.set("fov", Json::makeNumber(s.fovDeg));
    j.set("shadows", makeBool(s.shadows));
    j.set("edges", makeBool(s.edges));
    j.set("grid", makeBool(s.grid));
    j.set("biomeBorders", makeBool(s.biomeBorders));
    j.set("autosaveMinutes", Json::makeNumber(s.autosaveMinutes));
    Json g = Json::makeObject();
    g.set("msaa", Json::makeNumber(s.msaa));
    g.set("supersampling", makeBool(s.supersampling));
    g.set("shadowQuality", Json::makeNumber(s.shadowQuality));
    g.set("viewDistance", Json::makeNumber(s.viewDistance));
    g.set("plants", Json::makeNumber(s.plants));
    g.set("vsync", makeBool(s.vsync));
    j.set("graphics", g);
    const std::filesystem::path file = settingsFile();
    std::filesystem::path tmp = file;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << writeJson(j) << "\n";
        if (!out) {
            if (err) *err = "cannot write " + tmp.u8string();
            return false;
        }
    }
    return worlds::replaceFile(tmp, file, err);
}
