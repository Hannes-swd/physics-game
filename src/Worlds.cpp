// Welten: Ordner, world.json, sicheres Ersetzen von Dateien, Papierkorb (siehe Worlds.h)
#include "Worlds.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

#include "Json.h"

namespace fs = std::filesystem;

namespace worlds {

namespace {

std::string u8(const fs::path& p) { return p.u8string(); }
fs::path fromU8(const std::string& s) { return fs::u8path(s); }

bool readText(const fs::path& file, std::string& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeText(const fs::path& file, const std::string& text, std::string* err) {
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (out) out << text;
        if (out) out.flush();
        if (!out) {
            if (err) *err = "cannot write " + u8(file);
            return false;
        }
    }
    return true;
}

Json vec3(const glm::dvec3& v) {
    Json a = Json::makeArray();
    a.push(Json::makeNumber(v.x));
    a.push(Json::makeNumber(v.y));
    a.push(Json::makeNumber(v.z));
    return a;
}

glm::dvec3 vec3Of(const Json& j, const glm::dvec3& def) {
    if (j.type != Json::Array || j.size() != 3) return def;
    return glm::dvec3(j[0].num, j[1].num, j[2].num);
}

double numOf(const Json& j, double def) { return j.type == Json::Number ? j.num : def; }
bool boolOf(const Json& j, bool def) { return j.type == Json::Bool ? j.num != 0 : def; }
std::string strOf(const Json& j, const std::string& def) { return j.type == Json::String ? j.str : def; }

Json makeBool(bool b) {
    Json j;
    j.type = Json::Bool;
    j.num = b ? 1 : 0;
    return j;
}

// Ordnername aus dem Namen: verbotene Zeichen ersetzen, reservierte Namen meiden, nicht zu lang
std::string folderName(const std::string& name) {
    std::string s;
    for (unsigned char c : name) {
        if (c < 32 || std::string("<>:\"/\\|?*").find((char)c) != std::string::npos) s += '_';
        else s += (char)c;
    }
    if (s.size() > 40) {
        size_t n = 40;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;  // kein UTF-8-Zeichen zerschneiden
        s.resize(n);
    }
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    if (s.empty()) s = "Welt";
    std::string up = s;
    for (char& c : up) c = (char)std::toupper((unsigned char)c);
    const std::string base = up.substr(0, up.find('.'));
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                     "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* r : reserved)
        if (base == r) s += "_";
    return s;
}

// Liegt der Ordner wirklich unter worldsDir()? (Löschen und Umbenennen nur dort)
bool insideWorlds(const fs::path& dir) {
    std::error_code ec;
    const fs::path a = fs::weakly_canonical(dir, ec), root = fs::weakly_canonical(worldsDir(), ec);
    if (ec) return false;
    return a.parent_path() == root;
}

} // namespace

fs::path dataDir() {
    static fs::path dir = [] {
        fs::path d;
        if (const wchar_t* app = _wgetenv(L"APPDATA"); app && *app) d = fs::path(app) / L"Werkstatt";
        else d = fs::current_path() / L"Werkstatt-Daten";
        std::error_code ec;
        fs::create_directories(d, ec);
        return d;
    }();
    return dir;
}

fs::path worldsDir() {
    fs::path d = dataDir() / L"worlds";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

fs::path sceneFile(const WorldInfo& w) { return w.dir / L"scene.werkstatt"; }

fs::path existingOrBackup(const fs::path& file) {
    std::error_code ec;
    if (fs::exists(file, ec)) return file;
    fs::path bak = file;
    bak += L".bak";
    if (fs::exists(bak, ec)) return bak;
    return file;
}

bool replaceFile(const fs::path& tmp, const fs::path& file, std::string* err) {
    std::error_code ec;
    fs::path bak = file;
    bak += L".bak";
    if (fs::exists(file, ec) && !MoveFileExW(file.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (err) *err = "cannot keep the backup " + u8(bak);
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (err) *err = "cannot replace " + u8(file);
        return false;
    }
    return true;
}

int64_t now() { return (int64_t)std::time(nullptr); }

std::string cleanName(const std::string& in) {
    std::string s = in;
    for (char& c : s)
        if ((unsigned char)c < 32) c = ' ';
    s.erase(0, s.find_first_not_of(' '));
    s.erase(s.find_last_not_of(' ') + 1);
    if (s.size() > 64) {
        size_t n = 64;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;
        s.resize(n);
    }
    return s;
}

uint64_t seedFromText(const std::string& in) {
    const std::string t = cleanName(in);
    if (t.empty()) {
        std::random_device rd;
        const uint64_t a = ((uint64_t)rd() << 32) ^ rd();
        return a ^ (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    }
    const bool neg = t[0] == '-';
    const std::string digits = neg ? t.substr(1) : t;
    if (!digits.empty() && digits.size() <= 19 && std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const uint64_t v = std::stoull(digits);
        return neg ? (uint64_t)(-(int64_t)v) : v;
    }
    uint64_t h = 1469598103934665603ull;  // FNV-1a: gleicher Text, gleicher Seed
    for (unsigned char c : t) h = (h ^ c) * 1099511628211ull;
    return h;
}

std::string formatTime(int64_t t) {
    if (t <= 0) return "nie";
    std::time_t tt = (std::time_t)t, nw = std::time(nullptr);
    std::tm a{}, b{};
    localtime_s(&a, &tt);
    localtime_s(&b, &nw);
    char hm[16];
    std::snprintf(hm, sizeof(hm), "%02d:%02d", a.tm_hour, a.tm_min);
    std::tm y = b;
    y.tm_mday -= 1;
    std::mktime(&y);  // gestern (auch über Monatsgrenzen)
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) return std::string("heute, ") + hm;
    if (a.tm_year == y.tm_year && a.tm_yday == y.tm_yday) return std::string("gestern, ") + hm;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%02d.%02d.%04d, %s", a.tm_mday, a.tm_mon + 1, a.tm_year + 1900, hm);
    return buf;
}

std::string formatDuration(double s) {
    const int64_t m = (int64_t)(s / 60.0);
    char buf[40];
    if (m < 60) std::snprintf(buf, sizeof(buf), "%lld min", (long long)m);
    else std::snprintf(buf, sizeof(buf), "%lld h %02lld min", (long long)(m / 60), (long long)(m % 60));
    return buf;
}

bool readInfo(const fs::path& dir, WorldInfo& w, std::string* err) {
    w = WorldInfo();
    w.dir = dir;
    w.name = u8(dir.filename());
    std::string text;
    const fs::path file = existingOrBackup(dir / L"world.json");
    Json j;
    std::string perr;
    if (!readText(file, text) || !parseJson(text, j, &perr) || j.type != Json::Object) {
        fs::path bak = dir / L"world.json.bak";  // die Datei ist da, aber kaputt: die Sicherung versuchen
        if (!(file != bak && readText(bak, text) && parseJson(text, j, nullptr) && j.type == Json::Object)) {
            w.broken = true;
            if (err) *err = "world.json: " + (perr.empty() ? std::string("missing") : perr);
            return false;
        }
    }
    w.format = (int)numOf(j["format"], 1);
    w.name = strOf(j["name"], w.name);
    w.seedText = strOf(j["seedText"], "");
    const std::string seed = strOf(j["seed"], "0");  // als Text: 64 Bit passen in keine JSON-Zahl
    w.seed = std::strtoull(seed.c_str(), nullptr, 10);
    w.generator = strOf(j["generator"], "flat");
    w.generatorVersion = (int)numOf(j["generatorVersion"], 1);
    w.created = (int64_t)numOf(j["created"], 0);
    w.lastPlayed = (int64_t)numOf(j["lastPlayed"], 0);
    w.playTime = numOf(j["playTime"], 0);
    if (const Json& v = j["view"]; v.type == Json::Object) {
        w.view.valid = true;
        w.view.play = boolOf(v["play"], false);
        w.view.orbitTarget = vec3Of(v["orbitTarget"], w.view.orbitTarget);
        w.view.orbitYaw = numOf(v["orbitYaw"], w.view.orbitYaw);
        w.view.orbitPitch = numOf(v["orbitPitch"], w.view.orbitPitch);
        w.view.orbitDist = numOf(v["orbitDist"], w.view.orbitDist);
        w.view.viewMode = (int)numOf(v["viewMode"], 0);
        w.view.feet = vec3Of(v["feet"], w.view.feet);
        w.view.yaw = numOf(v["yaw"], 0);
        w.view.pitch = numOf(v["pitch"], 0);
    }
    if (const Json& e = j["environment"]; e.type == Json::Object) {
        w.env.valid = true;
        w.env.airDensity = numOf(e["airDensity"], w.env.airDensity);
        w.env.wind = vec3Of(e["wind"], w.env.wind);
        w.env.breakage = boolOf(e["breakage"], w.env.breakage);
        w.env.buildCollision = boolOf(e["buildCollision"], w.env.buildCollision);
        w.env.heat = boolOf(e["heat"], w.env.heat);
        w.env.ambient = numOf(e["ambient"], w.env.ambient);
        w.env.heatSpeed = numOf(e["heatSpeed"], w.env.heatSpeed);
    }
    return true;
}

bool writeInfo(const WorldInfo& w, std::string* err) {
    Json j = Json::makeObject();
    j.set("format", Json::makeNumber(w.format));
    j.set("name", Json::makeString(w.name));
    j.set("seed", Json::makeString(std::to_string(w.seed)));
    j.set("seedText", Json::makeString(w.seedText));
    j.set("generator", Json::makeString(w.generator));
    j.set("generatorVersion", Json::makeNumber(w.generatorVersion));
    j.set("created", Json::makeNumber((double)w.created));
    j.set("lastPlayed", Json::makeNumber((double)w.lastPlayed));
    j.set("playTime", Json::makeNumber(std::floor(w.playTime)));
    if (w.view.valid) {
        Json v = Json::makeObject();
        v.set("play", makeBool(w.view.play));
        v.set("orbitTarget", vec3(w.view.orbitTarget));
        v.set("orbitYaw", Json::makeNumber(w.view.orbitYaw));
        v.set("orbitPitch", Json::makeNumber(w.view.orbitPitch));
        v.set("orbitDist", Json::makeNumber(w.view.orbitDist));
        v.set("viewMode", Json::makeNumber(w.view.viewMode));
        v.set("feet", vec3(w.view.feet));
        v.set("yaw", Json::makeNumber(w.view.yaw));
        v.set("pitch", Json::makeNumber(w.view.pitch));
        j.set("view", std::move(v));
    }
    if (w.env.valid) {
        Json e = Json::makeObject();
        e.set("airDensity", Json::makeNumber(w.env.airDensity));
        e.set("wind", vec3(w.env.wind));
        e.set("breakage", makeBool(w.env.breakage));
        e.set("buildCollision", makeBool(w.env.buildCollision));
        e.set("heat", makeBool(w.env.heat));
        e.set("ambient", Json::makeNumber(w.env.ambient));
        e.set("heatSpeed", Json::makeNumber(w.env.heatSpeed));
        j.set("environment", std::move(e));
    }
    const fs::path file = w.dir / L"world.json", tmp = w.dir / L"world.json.tmp";
    return writeText(tmp, writeJson(j) + "\n", err) && replaceFile(tmp, file, err);
}

std::vector<WorldInfo> list() {
    std::vector<WorldInfo> out;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(worldsDir(), ec)) {
        if (!e.is_directory(ec)) continue;
        WorldInfo w;
        readInfo(e.path(), w, nullptr);  // kaputte Welten erscheinen trotzdem (zum Löschen)
        out.push_back(std::move(w));
    }
    std::sort(out.begin(), out.end(), [](const WorldInfo& a, const WorldInfo& b) {
        if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed;
        return a.name < b.name;
    });
    return out;
}

bool create(const std::string& nameIn, const std::string& seedText, WorldInfo& w, std::string* err) {
    const std::string name = cleanName(nameIn);
    if (name.empty()) {
        if (err) *err = "Bitte einen Namen eingeben.";
        return false;
    }
    const std::string folder = folderName(name);
    fs::path dir = worldsDir() / fromU8(folder);
    std::error_code ec;
    for (int i = 2; fs::exists(dir, ec); ++i) dir = worldsDir() / fromU8(folder + " (" + std::to_string(i) + ")");
    if (!fs::create_directories(dir, ec)) {
        if (err) *err = "Ordner kann nicht angelegt werden: " + u8(dir);
        return false;
    }
    w = WorldInfo();
    w.dir = dir;
    w.name = name;
    w.seedText = cleanName(seedText);
    w.seed = seedFromText(seedText);
    w.created = w.lastPlayed = now();
    if (!writeInfo(w, err)) {
        fs::remove_all(dir, ec);
        return false;
    }
    return true;
}

bool rename(const fs::path& dir, const std::string& nameIn, std::string* err) {
    const std::string name = cleanName(nameIn);
    if (name.empty()) {
        if (err) *err = "Bitte einen Namen eingeben.";
        return false;
    }
    WorldInfo w;
    if (!insideWorlds(dir) || !readInfo(dir, w, err)) {
        if (err && err->empty()) *err = "Welt nicht gefunden.";
        return false;
    }
    w.name = name;  // der Ordner bleibt (nur der angezeigte Name ändert sich)
    return writeInfo(w, err);
}

bool remove(const fs::path& dir, std::string* err) {
    std::error_code ec;
    if (!insideWorlds(dir) || !fs::is_directory(dir, ec)) {
        if (err) *err = "Welt nicht gefunden.";
        return false;
    }
    std::wstring from = fs::absolute(dir, ec).wstring();
    from.push_back(L'\0');  // die Liste endet mit zwei Nullen
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) {
        if (err) *err = "Die Welt konnte nicht in den Papierkorb gelegt werden (ist eine Datei darin noch geöffnet?).";
        return false;
    }
    return true;
}

} // namespace worlds
