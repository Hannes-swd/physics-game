// Einstellungen lesen und schreiben (siehe Settings.h)
#include "Settings.h"

#include <algorithm>
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

} // namespace

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
    if (j["biomeBorders"].type == Json::Bool) s.biomeBorders = j["biomeBorders"].num != 0;
    if (j["autosaveMinutes"].type == Json::Number) s.autosaveMinutes = std::clamp((int)j["autosaveMinutes"].num, 0, 120);
    return s;
}

bool saveSettings(const GameSettings& s, std::string* err) {
    Json j = Json::makeObject();
    j.set("playerName", Json::makeString(s.playerName));
    j.set("mouseSensitivity", Json::makeNumber(s.mouseSensitivity));
    j.set("fov", Json::makeNumber(s.fovDeg));
    j.set("shadows", makeBool(s.shadows));
    j.set("edges", makeBool(s.edges));
    j.set("grid", makeBool(s.grid));
    j.set("biomeBorders", makeBool(s.biomeBorders));
    j.set("autosaveMinutes", Json::makeNumber(s.autosaveMinutes));
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
