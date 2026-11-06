// Tiere: Arten lesen (animals.json), erscheinen und verschwinden lassen, Verhalten (siehe Animals.h)
#include "Animals.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "Json.h"
#include "Terrain.h"
#include "WorldGen.h"

namespace {

static const unsigned char kBuiltinAnimals[] = {
#include "animals.json.inc"
    0};

constexpr double kPi = 3.14159265358979323846;

std::filesystem::path exeDir() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? std::filesystem::path(buf).parent_path() : std::filesystem::current_path();
}

// ---------------------------------------------------------------- Lesen

struct Reader {
    std::string err;
    bool ok() const { return err.empty(); }
    void fail(const std::string& where, const std::string& what) {
        if (err.empty()) err = where + ": " + what;
    }
    void num(const Json& j, const char* key, float& v, const std::string& w) {
        const Json& x = j[key];
        if (x.type == Json::Null) return;
        if (x.type != Json::Number) return fail(w + "." + key, "number expected");
        v = (float)x.num;
    }
    void num(const Json& j, const char* key, double& v, const std::string& w) {
        float f = (float)v;
        num(j, key, f, w);
        v = f;
    }
    void pair(const Json& j, const char* key, float& a, float& b, const std::string& w) {
        const Json& x = j[key];
        if (x.type == Json::Null) return;
        if (x.type == Json::Number) {
            a = b = (float)x.num;
            return;
        }
        if (x.type != Json::Array || x.size() != 2 || x[0].type != Json::Number || x[1].type != Json::Number)
            return fail(w + "." + key, "[from, to] expected");
        a = (float)x[0].num;
        b = (float)x[1].num;
        if (b < a) std::swap(a, b);
    }
    bool color(const Json& x, glm::vec3& out, const std::string& w) {
        if (x.type != Json::Array || x.size() != 3 || x[0].type != Json::Number || x[1].type != Json::Number || x[2].type != Json::Number) {
            fail(w, "color [r, g, b] expected (0..1)");
            return false;
        }
        out = glm::vec3((float)x[0].num, (float)x[1].num, (float)x[2].num);
        return true;
    }
    void text(const Json& j, const char* key, std::string& v, const std::string& w) {
        const Json& x = j[key];
        if (x.type == Json::Null) return;
        if (x.type != Json::String) return fail(w + "." + key, "text expected");
        v = x.str;
    }
};

template <typename E>
bool pick(const std::string& s, const std::vector<std::pair<const char*, E>>& names, E& out) {
    for (const auto& [n, e] : names)
        if (s == n) {
            out = e;
            return true;
        }
    return false;
}

double hash01(uint64_t a, int64_t b, int64_t c) {
    uint64_t h = (a + 1) * 0x9E3779B97F4A7C15ull ^ (uint64_t)b * 0xC2B2AE3D27D4EB4Full ^ (uint64_t)c * 0x165667B19E3779F9ull;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}

float wrapAngle(float a) {
    while (a > (float)kPi) a -= 2 * (float)kPi;
    while (a < -(float)kPi) a += 2 * (float)kPi;
    return a;
}

// Blickrichtung wie die Spielfigur: yaw 0 schaut nach -z
glm::dvec2 heading(float yaw) { return {-std::sin(yaw), -std::cos(yaw)}; }
float yawOf(const glm::dvec2& d) { return (float)std::atan2(-d.x, -d.y); }

glm::dvec2 xz(const glm::dvec3& p) { return {p.x, p.z}; }

bool activeAt(AnimalActive a, double hours) {
    const bool day = hours >= 5.0 && hours < 19.7;
    return a == AnimalActive::Always || (a == AnimalActive::Day ? day : !day);
}

} // namespace

// ============================================================================ Datei

int AnimalConfig::findSpecies(const std::string& id) const {
    for (size_t i = 0; i < species.size(); ++i)
        if (species[i].id == id) return (int)i;
    return -1;
}

bool parseAnimalConfig(const std::string& text, AnimalConfig& out, std::string* errOut) {
    Json j;
    std::string perr;
    if (!parseJson(text, j, &perr)) {
        if (errOut) *errOut = perr;
        return false;
    }
    Reader r;
    AnimalConfig c;
    c.text = text;
    if (j.type != Json::Object) r.fail("file", "an object { ... } is needed");
    if (j["enabled"].type == Json::Bool) c.enabled = j["enabled"].num != 0;
    const Json& sp = j["spawn"];
    if (sp.type == Json::Object) {
        r.num(sp, "interval", c.interval, "spawn");
        float maxA = (float)c.maxAnimals;
        r.num(sp, "max_animals", maxA, "spawn");
        c.maxAnimals = std::clamp((int)maxA, 0, 2000);
        r.num(sp, "min_distance", c.minDistance, "spawn");
        r.num(sp, "hidden_share", c.hiddenShare, "spawn");
        c.interval = std::clamp(c.interval, 0.005f, 5.0f);
        c.hiddenShare = std::clamp(c.hiddenShare, 0.0f, 1.0f);
    }
    const Json& list = j["species"];
    if (r.ok() && list.type != Json::Array) r.fail("species", "a list of animal species is needed");
    for (size_t i = 0; r.ok() && i < list.size(); ++i) {
        const Json& t = list[i];
        const std::string w = "species[" + std::to_string(i) + "]";
        if (t.type != Json::Object) {
            r.fail(w, "object expected");
            break;
        }
        AnimalSpecies s;
        r.text(t, "id", s.id, w);
        if (s.id.empty()) r.fail(w + ".id", "text expected (e.g. \"roe_deer\")");
        if (c.findSpecies(s.id) >= 0) r.fail(w + ".id", "'" + s.id + "' is used twice");
        s.label = s.id;
        r.text(t, "label", s.label, w);
        r.text(t, "model", s.model, w);
        if (s.model.empty()) r.fail(w + ".model", "a model file in assets/animals (or \"bird\") is needed");
        r.num(t, "height", s.height, w);
        s.height = std::clamp(s.height, 0.02f, 20.0f);
        r.num(t, "shade", s.shade, w);
        r.text(t, "foot", s.foot, w);
        const Json& tint = t["tint"];
        if (tint.type == Json::Object)
            for (const auto& [mat, col] : tint.obj) {
                glm::vec3 c3;
                if (r.color(col, c3, w + ".tint." + mat)) s.tint.emplace_back(mat, c3);
            }
        const Json& clips = t["clips"];
        if (clips.type == Json::Object) {
            const Json& idle = clips["idle"];
            if (idle.type == Json::String) s.idleClips = {idle.str};
            if (idle.type == Json::Array) {
                s.idleClips.clear();
                for (const Json& x : idle.arr)
                    if (x.type == Json::String) s.idleClips.push_back(x.str);
            }
            r.text(clips, "walk", s.walkClip, w + ".clips");
            r.text(clips, "run", s.runClip, w + ".clips");
            r.text(clips, "fly", s.flyClip, w + ".clips");
        }
        r.pair(t, "speed", s.walkSpeed, s.runSpeed, w);
        r.num(t, "turn", s.turnRate, w);
        float g0 = (float)s.groupMin, g1 = (float)s.groupMax;
        r.pair(t, "group", g0, g1, w);
        s.groupMin = std::clamp((int)g0, 1, 60);
        s.groupMax = std::clamp((int)g1, s.groupMin, 60);
        r.num(t, "spread", s.spread, w);
        r.num(t, "wander", s.wander, w);
        std::string b = "shy";
        r.text(t, "behavior", b, w);
        if (!pick<AnimalBehavior>(b, {{"shy", AnimalBehavior::Shy}, {"calm", AnimalBehavior::Calm}, {"curious", AnimalBehavior::Curious},
                                      {"small", AnimalBehavior::Small}},
                                  s.behavior))
            r.fail(w + ".behavior", "'" + b + "': shy, calm, curious or small");
        r.num(t, "flee", s.fleeDistance, w);
        s.alertDistance = s.fleeDistance * 1.7f;
        r.num(t, "alert", s.alertDistance, w);
        r.pair(t, "keep", s.keepNear, s.keepFar, w);
        if (t["takeoff"].type == Json::Bool) s.takeoff = t["takeoff"].num != 0;
        if (const Json& at = t["attack"]; at.type == Json::Object) {
            const std::string wa = w + ".attack";
            std::string mode = "defend";
            r.text(at, "mode", mode, wa);
            if (!pick<AnimalAttack>(mode, {{"hunt", AnimalAttack::Hunt}, {"defend", AnimalAttack::Defend}, {"none", AnimalAttack::None}}, s.attack))
                r.fail(wa + ".mode", "'" + mode + "': hunt, defend or none");
            r.num(at, "range", s.attackRange, wa);
            r.num(at, "reach", s.attackReach, wa);
            r.num(at, "cooldown", s.attackCooldown, wa);
            r.num(at, "power", s.attackPower, wa);
            r.num(at, "chase", s.attackChase, wa);
            r.text(at, "kind", s.attackKind, wa);
            r.text(at, "clip", s.attackClip, wa);
            if (s.attackKind != "bite" && s.attackKind != "gore" && s.attackKind != "sting" && s.attackKind != "pinch")
                r.fail(wa + ".kind", "'" + s.attackKind + "': bite, gore, sting or pinch");
            s.attackRange = std::clamp(s.attackRange, 0.0f, 200.0f);
            s.attackReach = std::clamp(s.attackReach, 0.2f, 5.0f);
            s.attackCooldown = std::clamp(s.attackCooldown, 0.3f, 30.0f);
            s.attackPower = std::clamp(s.attackPower, 0.1f, 5.0f);
        }
        r.num(t, "toughness", s.toughness, w);
        r.num(t, "hide", s.hide, w);
        s.hide = std::clamp(s.hide, 0.0f, 10.0f);
        if (t["clips"].type == Json::Object) r.text(t["clips"], "death", s.deathClip, w + ".clips");
        r.num(t, "visible", s.visible, w);
        s.visible = std::clamp(s.visible, 10.0f, 5000.0f);
        std::string act = "always";
        r.text(t, "active", act, w);
        if (!pick<AnimalActive>(act, {{"always", AnimalActive::Always}, {"day", AnimalActive::Day}, {"night", AnimalActive::Night}}, s.active))
            r.fail(w + ".active", "'" + act + "': always, day or night");
        std::string mo = "ground";
        r.text(t, "motion", mo, w);
        if (!pick<AnimalMotion>(mo, {{"ground", AnimalMotion::Ground}, {"soar", AnimalMotion::Soar}, {"flock", AnimalMotion::Flock},
                                     {"flutter", AnimalMotion::Flutter}},
                                s.motion))
            r.fail(w + ".motion", "'" + mo + "': ground, soar, flock or flutter");
        r.pair(t, "slope", s.slopeMin, s.slopeMax, w);
        r.num(t, "move_slope", s.moveSlopeMax, w);
        float h0 = -1e9f, h1 = 1e9f;
        r.pair(t, "altitude", h0, h1, w);
        s.heightMin = h0, s.heightMax = h1;
        if (t["water"].type == Json::Bool) s.water = t["water"].num != 0;
        r.pair(t, "fly_height", s.flyMin, s.flyMax, w);
        r.pair(t, "circle", s.circleMin, s.circleMax, w);
        r.num(t, "fly_speed", s.flySpeed, w);
        r.num(t, "wingspan", s.wingspan, w);
        const Json& col = t["colors"];
        if (col.type == Json::Object) {
            if (col.has("body")) r.color(col["body"], s.body, w + ".colors.body");
            if (col.has("wing")) r.color(col["wing"], s.wing, w + ".colors.wing");
            if (col.has("beak")) r.color(col["beak"], s.beak, w + ".colors.beak");
        }
        c.species.push_back(std::move(s));
    }
    const Json& biomes = j["biomes"];
    if (r.ok() && biomes.type != Json::Object && biomes.type != Json::Null) r.fail("biomes", "object { \"forest\": { \"roe_deer\": 1.5 } } expected");
    for (const auto& [bid, entries] : biomes.obj) {
        if (!r.ok()) break;
        if (entries.type != Json::Object) {
            r.fail("biomes." + bid, "object { \"species\": groups } expected");
            break;
        }
        std::vector<AnimalConfig::Entry> list2;
        for (const auto& [sid, n] : entries.obj) {
            AnimalConfig::Entry e;
            e.species = c.findSpecies(sid);
            if (e.species < 0) r.fail("biomes." + bid + "." + sid, "unknown species (not in \"species\")");
            if (n.type != Json::Number) r.fail("biomes." + bid + "." + sid, "number of groups expected");
            e.groups = std::max(0.0, n.num);
            if (r.ok()) list2.push_back(e);
        }
        c.biomes.emplace_back(bid, std::move(list2));
    }
    if (!r.ok()) {
        if (errOut) *errOut = r.err;
        return false;
    }
    out = std::move(c);
    return true;
}

const AnimalConfig& builtinAnimalConfig() {
    static const AnimalConfig cfg = [] {
        AnimalConfig c;
        std::string err;
        if (!parseAnimalConfig(reinterpret_cast<const char*>(kBuiltinAnimals), c, &err))
            std::fprintf(stderr, "built-in animals.json: %s\n", err.c_str());
        return c;
    }();
    return cfg;
}

std::string findAnimalsFile() {
    std::error_code ec;
    const std::filesystem::path dir = exeDir();
    for (const std::filesystem::path& p : {dir / "animals.json", std::filesystem::current_path(ec) / "assets" / "animals.json",
                                           dir / "assets" / "animals.json", dir.parent_path().parent_path() / "assets" / "animals.json"})
        if (std::filesystem::is_regular_file(p, ec)) return p.lexically_normal().u8string();
    return {};
}

std::string findAnimalModel(const std::string& file) {
    std::error_code ec;
    const std::filesystem::path dir = exeDir(), f = std::filesystem::u8path(file);
    for (const std::filesystem::path& p : {dir / "animals" / f, std::filesystem::current_path(ec) / "assets" / "animals" / f,
                                           dir / "assets" / "animals" / f, dir.parent_path().parent_path() / "assets" / "animals" / f})
        if (std::filesystem::is_regular_file(p, ec)) return p.lexically_normal().u8string();
    return {};
}

std::shared_ptr<const AnimalConfig> loadAnimalsFile(std::string* report) {
    const std::string path = findAnimalsFile();
    if (!path.empty()) {
        std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        auto c = std::make_shared<AnimalConfig>();
        std::string err;
        if (parseAnimalConfig(ss.str(), *c, &err)) return c;
        if (report) *report = path + ": " + err + " - using the built-in animals";
    }
    return std::make_shared<AnimalConfig>(builtinAnimalConfig());
}

// ============================================================================ Welt

AnimalWorld::AnimalWorld() : cfg_(std::make_shared<AnimalConfig>(builtinAnimalConfig())), rng_(std::random_device{}()) {}

void AnimalWorld::setConfig(std::shared_ptr<const AnimalConfig> cfg) {
    clear();
    cfg_ = cfg ? std::move(cfg) : std::make_shared<AnimalConfig>(builtinAnimalConfig());
    gen_ = nullptr;
}

void AnimalWorld::clear() {
    animals_.clear();
    groups_.clear();
    bites_.clear();
}

AnimalGroup* AnimalWorld::findGroup(uint32_t id) {
    for (AnimalGroup& g : groups_)
        if (g.id == id) return &g;
    return nullptr;
}

bool AnimalWorld::walkable(const Terrain& tr, const AnimalSpecies& s, double x, double z, double* y, double* slope) const {
    glm::dvec3 n;
    const double h = tr.surfaceHeight(x, z, &n);
    if (y) *y = h;
    const double sl = 1.0 - n.y;
    if (slope) *slope = sl;
    if (tr.hasRivers() || tr.gen().hasLakes()) {
        const double level = tr.riverLevel();
        if (h < level + (s.water ? -0.25 : 0.06)) return false;  // Wasser (Frösche und Krabben auch ins flache)
    }
    return sl <= s.moveSlopeMax;
}

std::string AnimalWorld::status(const glm::dvec3& focus) const {
    std::map<std::string, int> count;
    for (const Animal& a : animals_) ++count[cfg_->species[(size_t)a.species].id];
    std::ostringstream o;
    o << "animals " << (cfg_->enabled ? "on" : "off") << ": " << animals_.size() << " in " << groups_.size() << " groups";
    for (const auto& [id, n] : count) o << "\n  " << id << " " << n;
    double nearest = 1e30;
    for (const Animal& a : animals_) nearest = std::min(nearest, glm::length(xz(a.pos) - xz(focus)));
    if (!animals_.empty()) o << "\nnearest " << (int)std::lround(nearest) << " m";
    return o.str();
}

bool AnimalWorld::spawnGroup(const Terrain& tr, int si, const glm::dvec2& at, int count, bool checkTerrain) {
    const AnimalSpecies& s = cfg_->species[(size_t)si];
    const bool ground = s.motion == AnimalMotion::Ground;
    double y0 = 0, slope = 0;
    if (ground) {
        if (!walkable(tr, s, at.x, at.y, &y0, &slope) && checkTerrain) return false;
        if (checkTerrain && (slope < s.slopeMin || slope > s.slopeMax || y0 < s.heightMin || y0 > s.heightMax)) return false;
    } else {
        y0 = tr.surfaceHeight(at.x, at.y);
        if (tr.hasRivers() || tr.gen().hasLakes()) y0 = std::max(y0, tr.riverLevel());
        if (checkTerrain && (y0 < s.heightMin || y0 > s.heightMax)) return false;
    }
    AnimalGroup g;
    g.id = nextId_++;
    g.species = si;
    g.home = at;
    g.pos = at;
    const float groupYaw = rnd(-(float)kPi, (float)kPi);
    for (int k = 0; k < count && (int)animals_.size() < std::max(cfg_->maxAnimals, 1) + 40; ++k) {
        Animal a;
        a.id = nextId_++;
        a.group = g.id;
        a.species = si;
        a.leader = k == 0;
        a.seed = rnd(0, 1);
        glm::dvec2 p = at;
        bool ok = true;
        if (k > 0) {
            ok = false;
            for (int tries = 0; tries < 6 && !ok; ++tries) {
                const float ang = rnd(-(float)kPi, (float)kPi), rad = s.spread * std::sqrt(rnd(0.05f, 1.0f));
                a.offset = glm::dvec2(std::cos(ang), std::sin(ang)) * (double)rad;
                p = at + a.offset;
                ok = !ground || walkable(tr, s, p.x, p.y);
            }
        }
        if (!ok) continue;
        a.yaw = wrapAngle(groupYaw + rnd(-0.6f, 0.6f));
        a.pos = glm::dvec3(p.x, ground ? tr.surfaceHeight(p.x, p.y) : y0, p.y);
        a.target = p;
        a.timer = rnd(0.5f, 6.0f);
        a.idleClip = a.lastIdle = (int)std::floor(rnd(0, (float)s.idleClips.size() - 0.001f));
        a.idleTime = rnd(0, 10);
        a.phase = rnd(0, 1);
        if (!ground) {
            a.airborne = true;
            a.state = Animal::Fly;
            a.altitude = rnd(s.flyMin, s.flyMax);
            a.center = at;
            a.radius = rnd(s.circleMin, s.circleMax);
            a.angle = rnd(-(float)kPi, (float)kPi);
            if (s.motion == AnimalMotion::Soar) {  // gleich auf seinem Kreis
                p = at + glm::dvec2(std::cos(a.angle), std::sin(a.angle)) * a.radius;
                a.yaw = yawOf(glm::dvec2(-std::sin(a.angle), std::cos(a.angle)));
            } else if (s.motion == AnimalMotion::Flutter) {
                a.altitude = rnd(s.flyMin, s.flyMax);
            }
            a.pos = glm::dvec3(p.x, y0 + a.altitude + (k > 0 ? rnd(-2, 2) : 0.0f), p.y);
            a.speed = s.flySpeed;
            a.flapRate = 3.2f / std::sqrt(std::max(s.height, 0.1f));
            a.flapAmount = 1;
        }
        animals_.push_back(std::move(a));
        ++g.size;
    }
    if (g.size == 0) return false;
    groups_.push_back(g);
    return true;
}

void AnimalWorld::trySpawn(const Terrain& tr, const glm::dvec3& focus, const glm::dvec3& view, double dayHours) {
    const std::vector<AnimalSpecies>& all = cfg_->species;
    if (all.empty() || (int)animals_.size() >= cfg_->maxAnimals) return;
    const int si = (int)(nextSpecies_++ % all.size());
    const AnimalSpecies& s = all[(size_t)si];
    if (!activeAt(s.active, dayHours)) return;
    const double R = reach(s);
    const glm::dvec2 f = xz(focus);
    int have = 0;
    for (const AnimalGroup& g : groups_)
        if (g.species == si && glm::length(g.pos - f) < R) ++have;
    // Ort: gleichmäßig in der Fläche, nicht zu nah; im Blickfeld erst weiter weg (sonst sähe man es auftauchen)
    const double rmin = std::max((double)cfg_->minDistance, 0.15 * R), rmax = 0.92 * R;
    if (rmin >= rmax) return;
    const double r = std::sqrt(rnd((float)(rmin * rmin), (float)(rmax * rmax)));
    const float ang = rnd(-(float)kPi, (float)kPi);
    const glm::dvec2 dir(std::cos(ang), std::sin(ang)), at = f + dir * r;
    glm::dvec2 v2(view.x, view.z);
    if (glm::length(v2) > 1e-6 && glm::dot(dir, glm::normalize(v2)) > 0.45 && r < cfg_->hiddenShare * R) return;
    GenSample smp;
    tr.gen().sample(at.x, at.y, smp);
    if (smp.biome < 0 || smp.biome >= (int)biomeEntries_.size()) return;
    double groups = 0;
    for (const AnimalConfig::Entry& e : biomeEntries_[(size_t)smp.biome])
        if (e.species == si) groups += e.groups;
    if (groups <= 0) return;
    // Bruchteile fest je Gegend (so groß wie die Sichtweite): mal eine Gruppe mehr, mal nicht, aber nicht ständig neu
    const double h = hash01((uint64_t)si * 7919u + std::hash<std::string>{}(s.id), (int64_t)std::floor(f.x / R), (int64_t)std::floor(f.y / R));
    const int target = (int)std::floor(groups + h);
    if (have >= target) return;
    const int count = s.groupMin + (int)std::floor(rnd(0, (float)(s.groupMax - s.groupMin + 1) - 0.001f));
    spawnGroup(tr, si, at, count, true);
}

std::string AnimalWorld::spawnAt(const Terrain& tr, const std::string& id, int count, const glm::dvec3& at) {
    const int si = cfg_->findSpecies(id);
    if (si < 0) {
        std::string ids;
        for (const AnimalSpecies& s : cfg_->species) ids += (ids.empty() ? "" : ", ") + s.id;
        return "Error: unknown animal '" + id + "' (" + ids + ")";
    }
    const AnimalSpecies& s = cfg_->species[(size_t)si];
    if (count <= 0) count = s.groupMax;
    if (!spawnGroup(tr, si, xz(at), count, false)) return "Error: no room for '" + id + "' there (water?)";
    return "spawned " + std::to_string(groups_.back().size) + " " + s.id + " (" + s.label + ")";
}

// ============================================================================ Bewegung

void AnimalWorld::moveGround(Animal& a, const AnimalSpecies& s, const Terrain& tr, double dt, const glm::dvec2& goal, float speed) {
    if (a.hp < 0.5f) speed = std::min(speed, s.walkSpeed * 1.3f);  // verwundet: humpelt, kann nicht rennen
    const glm::dvec2 p = xz(a.pos), to = goal - p;
    const double dist = glm::length(to);
    float want = dist > 0.25 ? speed : 0.0f;
    if (want > 0) {
        const float desired = wrapAngle(yawOf(to) + a.detour);
        const float diff = wrapAngle(desired - a.yaw);
        const float step = s.turnRate * (float)dt * (a.speed > s.walkSpeed * 1.5f ? 0.7f : 1.0f);
        a.yaw = wrapAngle(a.yaw + std::clamp(diff, -step, step));
        want *= std::clamp(1.0f - std::abs(diff) / 2.2f, 0.12f, 1.0f);  // erst drehen, dann laufen
        if (dist < 2.0) want *= (float)std::max(dist / 2.0, 0.3);
    }
    a.detour *= (float)std::exp(-dt / 2.5);
    const float accel = std::max(2.0f, s.runSpeed * 0.9f) * (want < a.speed ? 1.8f : 1.0f);
    a.speed += std::clamp(want - a.speed, -accel * (float)dt, accel * (float)dt);
    if (a.speed > 1e-3f) {
        const glm::dvec2 next = p + heading(a.yaw) * (double)(a.speed * dt);
        if (walkable(tr, s, next.x, next.y)) {
            a.pos.x = next.x;
            a.pos.z = next.y;
        } else {  // Wasser oder zu steil: anhalten und einen Umweg suchen
            a.speed *= 0.3f;
            a.detour = (rnd(0, 1) < 0.5f ? -1.0f : 1.0f) * rnd(0.9f, 1.8f);
            a.stuck += (float)dt;
        }
    }
    // Höhe und Neigung am Hang (vorn/hinten)
    const glm::dvec2 q = xz(a.pos), hd = heading(a.yaw);
    a.pos.y = tr.surfaceHeight(q.x, q.y);
    const double L = std::max(0.15, s.height * 0.55);
    const double hf = tr.surfaceHeight(q.x + hd.x * L * 0.5, q.y + hd.y * L * 0.5), hb = tr.surfaceHeight(q.x - hd.x * L * 0.5, q.y - hd.y * L * 0.5);
    const float pitch = std::clamp((float)std::atan2(hf - hb, L), -0.6f, 0.6f);
    a.pitch += (pitch - a.pitch) * (1.0f - (float)std::exp(-dt / 0.15));
    a.roll *= (float)std::exp(-dt / 0.2);
}

void AnimalWorld::think(Animal& a, AnimalGroup& g, const Animal* lead, const Terrain& tr, double dt, const std::vector<AnimalScarer>& scarers) {
    const AnimalSpecies& s = cfg_->species[(size_t)a.species];
    // der nächste Spieler
    double d = 1e30, scSpeed = 0;
    glm::dvec2 from(0);
    int nearest = -1;
    for (size_t i = 0; i < scarers.size(); ++i) {
        const AnimalScarer& sc = scarers[i];
        if (std::abs(sc.pos.y - a.pos.y) > 40) continue;
        const double dd = glm::length(xz(sc.pos) - xz(a.pos));
        if (dd < d) d = dd, from = xz(sc.pos), scSpeed = sc.speed, nearest = (int)i;
    }
    a.attackCool = std::max(0.0f, a.attackCool - (float)dt);
    if (a.attackAnim >= 0 && (a.attackAnim += (float)dt) > 2.0f) a.attackAnim = -1;
    if (a.hitAnim >= 0 && (a.hitAnim += (float)dt) > 2.0f) a.hitAnim = -1;

    // Angriff: Jäger (Wolf) kommen ab range heran (nachts von doppelt so weit) und geben nach "chase" s auf; Verteidiger
    // (Wildschwein, Schlange) greifen nur an, wer ihnen zu nahe kommt
    bool attacking = false;
    if (s.attack != AnimalAttack::None && !a.airborne && nearest >= 0 && scarers[(size_t)nearest].prey && g.calmUntil < clock_) {
        const bool night = dayHours_ < 6.0 || dayHours_ > 20.5;
        const double range = s.attackRange * (s.attack == AnimalAttack::Hunt && night ? 2.0 : 1.0);
        if (d < range) {
            if (g.attackUntil < clock_) g.attackStart = clock_;
            g.attackUntil = std::max(g.attackUntil, clock_ + (s.attack == AnimalAttack::Hunt ? 4.0 : 1.5));
        }
        if (s.attack == AnimalAttack::Hunt && g.attackUntil > clock_ && d < range * 2.5)
            g.attackUntil = std::max(g.attackUntil, clock_ + 1.0);  // an der Beute dranbleiben
        if (s.attack == AnimalAttack::Hunt && g.attackUntil > clock_ && clock_ - g.attackStart > s.attackChase) {
            g.attackUntil = -1;  // aufgegeben
            g.calmUntil = clock_ + 90.0;
        }
        attacking = g.attackUntil > clock_;
    }
    if (!attacking && a.state == Animal::Attack) a.state = Animal::Idle;

    // wer rennt, erschreckt weiter; wer steht oder schleicht, kommt näher heran
    const double scare = scSpeed > 4.0 ? 1.5 : scSpeed < 0.4 ? 0.75 : 1.0;
    if (attacking) {
        g.fleeUntil = -1;
        a.state = Animal::Attack;
        const double reach = s.attackReach + 0.35;  // bis an die Figur (ihr Radius)
        if (a.retreat > 0) {  // nach dem Biss ein paar Sprünge zurück, dann wieder heran
            a.retreat -= (float)dt;
            glm::dvec2 away = xz(a.pos) - from;
            if (glm::length(away) < 1e-3) away = -heading(a.yaw);
            moveGround(a, s, tr, dt, xz(a.pos) + glm::normalize(away) * 6.0, s.runSpeed * 0.6f);
        } else if (d > reach * 0.85) {
            const float sp = d > 5.0 ? s.runSpeed : std::max(s.walkSpeed * 2.0f, s.runSpeed * 0.5f);
            moveGround(a, s, tr, dt, from, sp);
        } else {  // dran: zur Beute drehen, stehen
            const float diff = wrapAngle(yawOf(from - xz(a.pos)) - a.yaw), step = s.turnRate * (float)dt;
            a.yaw = wrapAngle(a.yaw + std::clamp(diff, -step, step));
            moveGround(a, s, tr, dt, xz(a.pos), 0);
        }
        if (d < reach && a.attackCool <= 0 && a.retreat <= 0) {
            bites_.push_back({a.species, a.pos, nearest});
            a.attackCool = s.attackCooldown * rnd(0.8f, 1.25f);
            a.attackAnim = 0;
            if (s.attack == AnimalAttack::Hunt) a.retreat = rnd(0.8f, 1.6f);
            if (s.attack == AnimalAttack::Defend) {  // gewarnt: zurückziehen
                g.attackUntil = -1;
                g.calmUntil = clock_ + rnd(6, 12);
                g.fleeUntil = clock_ + rnd(2, 4);
                g.fleeFrom = from;
            }
        }
        if (a.leader) g.home = xz(a.pos);
        return;
    }
    if (d < s.fleeDistance * scare) {
        const double len = s.behavior == AnimalBehavior::Small ? rnd(2, 4) : s.behavior == AnimalBehavior::Shy ? rnd(5, 9) : rnd(3, 5);
        g.fleeUntil = std::max(g.fleeUntil, clock_ + len);
        g.fleeFrom = from;
    }
    a.idleTime += (float)dt;
    a.idleBlend = std::min(1.0f, a.idleBlend + (float)dt / 0.4f);
    auto setIdle = [&](float lo, float hi) {
        a.state = Animal::Idle;
        a.timer = rnd(lo, hi);
        a.lastIdle = a.idleClip;
        a.idleClip = (int)std::floor(rnd(0, (float)s.idleClips.size() - 0.001f));
        if (a.idleClip != a.lastIdle) a.idleBlend = 0;
    };

    if (g.fleeUntil > clock_) {
        if (s.takeoff && !a.airborne) {  // Tauben: auffliegen
            a.airborne = true;
            a.state = Animal::Fly;
            a.altitude = 0;
            a.timer = rnd(5, 9);
            a.flapRate = 3.2f / std::sqrt(std::max(s.wingspan, 0.1f));
            glm::dvec2 away = xz(a.pos) - g.fleeFrom;
            a.yaw = yawOf(glm::length(away) > 1e-3 ? away : heading(a.yaw)) + rnd(-0.4f, 0.4f);
            return;
        }
        if (a.airborne) return;
        a.state = Animal::Flee;
        glm::dvec2 away = xz(a.pos) - g.fleeFrom;
        if (glm::length(away) < 1e-3) away = heading(a.yaw);
        const float sp = s.behavior == AnimalBehavior::Calm ? std::min(s.runSpeed, s.walkSpeed * 2.6f) : s.runSpeed;
        moveGround(a, s, tr, dt, xz(a.pos) + glm::normalize(away) * 25.0, sp);
        if (a.leader) g.home = xz(a.pos);
        return;
    }
    if (a.state == Animal::Flee) setIdle(1, 4);
    if (a.airborne) return;

    // neugierig (Wolf): Abstand halten und umkreisen
    if (s.behavior == AnimalBehavior::Curious && d < s.keepFar * 2.2) {
        const glm::dvec2 u = (xz(a.pos) - from) / std::max(d, 1e-3), t(-u.y * (a.seed < 0.5 ? 1 : -1), u.x * (a.seed < 0.5 ? 1 : -1));
        glm::dvec2 goal;
        float sp = s.walkSpeed;
        if (d < s.keepNear) goal = xz(a.pos) + u * 8.0, sp = s.walkSpeed * 2.0f;
        else if (d > s.keepFar) goal = from + u * (0.5 * (s.keepNear + s.keepFar)) + t * 4.0, sp = s.walkSpeed * 1.6f;
        else goal = xz(a.pos) + t * 6.0 + u * 0.5;
        a.state = Animal::Walk;
        moveGround(a, s, tr, dt, goal, sp);
        if (a.leader) g.home = xz(a.pos);
        return;
    }
    // scheu: stehen bleiben und hinsehen
    if ((s.behavior == AnimalBehavior::Shy || s.behavior == AnimalBehavior::Small) && d < s.alertDistance * scare) {
        a.state = Animal::Alert;
        const glm::dvec2 look = from - xz(a.pos);
        const float diff = wrapAngle(yawOf(look) - a.yaw), step = s.turnRate * 0.6f * (float)dt;
        a.yaw = wrapAngle(a.yaw + std::clamp(diff, -step, step));
        moveGround(a, s, tr, dt, xz(a.pos), 0);
        return;
    }
    if (a.state == Animal::Alert) setIdle(1, 3);

    // Herde: dem ersten nach
    if (!a.leader && lead) {
        const glm::dvec2 goal = xz(lead->pos) + a.offset;
        const double dist = glm::length(goal - xz(a.pos));
        a.timer -= (float)dt;
        if (dist > s.spread * 0.6 + 1.0) {
            a.state = Animal::Follow;
            const float sp = dist > s.spread * 3 + 6 ? std::min(s.runSpeed, s.walkSpeed * 3.0f) : s.walkSpeed * (dist > s.spread * 1.5 ? 1.4f : 1.0f);
            moveGround(a, s, tr, dt, goal, sp);
            if (a.stuck > 2.0f) {  // kommt nicht hin: neuer Platz in der Herde
                a.stuck = 0;
                const float ang = rnd(-(float)kPi, (float)kPi);
                a.offset = glm::dvec2(std::cos(ang), std::sin(ang)) * (double)(s.spread * rnd(0.3f, 1.0f));
            }
        } else {
            if (a.state != Animal::Idle) setIdle(2, 8);
            moveGround(a, s, tr, dt, xz(a.pos), 0);
            if (a.timer <= 0) {  // ab und zu ein paar Schritte innerhalb der Herde
                const float ang = rnd(-(float)kPi, (float)kPi);
                a.offset = glm::dvec2(std::cos(ang), std::sin(ang)) * (double)(s.spread * std::sqrt(rnd(0.05f, 1.0f)));
                a.timer = rnd(4, 14);
            }
        }
        return;
    }
    // allein oder vorneweg: stehen, fressen, ein Stück gehen
    if (a.state == Animal::Walk) {
        moveGround(a, s, tr, dt, a.target, s.walkSpeed);
        if (glm::length(a.target - xz(a.pos)) < 1.2 || a.stuck > 1.5f) {
            a.stuck = 0;
            setIdle(3, 12);
        }
        return;
    }
    if (a.state != Animal::Idle) setIdle(1, 5);
    moveGround(a, s, tr, dt, xz(a.pos), 0);
    a.timer -= (float)dt;
    if (a.timer <= 0) {
        for (int tries = 0; tries < 4; ++tries) {
            const float ang = rnd(-(float)kPi, (float)kPi), rad = s.wander * std::sqrt(rnd(0.05f, 1.0f));
            const glm::dvec2 t = g.home + glm::dvec2(std::cos(ang), std::sin(ang)) * (double)rad;
            if (walkable(tr, s, t.x, t.y)) {
                a.target = t;
                a.state = Animal::Walk;
                a.stuck = 0;
                return;
            }
        }
        a.timer = rnd(1, 3);
    }
}

void AnimalWorld::setHitBox(int species, bool dead, const glm::vec3& mn, const glm::vec3& mx) {
    if (species < 0) return;
    std::vector<HitBox>& v = dead ? hitDead_ : hitAlive_;
    if ((size_t)species >= v.size()) v.resize((size_t)species + 1);
    v[(size_t)species] = {mn, mx, true};
}

// Trefferbox: die Maße des Modells (lebend: stehend, tot: am Ende des Sterbe-Ablaufs), gedreht wie das Tier im Bild
// (Blickrichtung, Hangneigung, auf die Seite gekippt), etwas großzügiger. Ohne gemessene Box: aus der Höhe geschätzt.
bool AnimalWorld::hitTest(const Animal& a, const glm::dvec3& eye, const glm::dvec3& dir, double reach, double& t) const {
    const AnimalSpecies& s = cfg_->species[(size_t)a.species];
    const std::vector<HitBox>& v = a.dead && (size_t)a.species < hitDead_.size() && hitDead_[(size_t)a.species].set ? hitDead_ : hitAlive_;
    HitBox b = (size_t)a.species < v.size() ? v[(size_t)a.species] : HitBox{};
    if (!b.set && a.dead) b = HitBox{{-0.22f, 0.0f, -0.5f}, {0.22f, 0.35f, 0.5f}, false};  // liegt
    const glm::vec3 pad = (b.mx - b.mn) * 0.06f + glm::vec3(0.06f / std::max(s.height, 0.05f));
    // Welt -> Modell: Lage wie beim Zeichnen (Drehen um y, Neigen um x, Kippen um z), dann durch die Höhe teilen
    const double cy = std::cos(a.yaw), sy = std::sin(a.yaw), cp = std::cos(a.pitch), sp = std::sin(a.pitch),
                 cr = std::cos(-a.roll), sr = std::sin(-a.roll);
    const glm::dmat3 ry(cy, 0, -sy, 0, 1, 0, sy, 0, cy), rx(1, 0, 0, 0, cp, sp, 0, -sp, cp), rz(cr, sr, 0, -sr, cr, 0, 0, 0, 1);
    const glm::dmat3 inv = glm::transpose(ry * rx * rz);
    const double h = std::max((double)s.height, 0.02);
    const glm::dvec3 o = inv * (eye - a.pos) / h, d = inv * dir / h;
    double t0 = 0, t1 = reach;
    for (int k = 0; k < 3; ++k) {
        const double lo = b.mn[k] - pad[k], hi = b.mx[k] + pad[k];
        if (std::abs(d[k]) < 1e-12) {
            if (o[k] < lo || o[k] > hi) return false;
            continue;
        }
        double ta = (lo - o[k]) / d[k], tb = (hi - o[k]) / d[k];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    t = t0;
    return true;
}

uint32_t AnimalWorld::strike(const glm::dvec3& eye, const glm::dvec3& dirIn, double reach, float damage, bool* killed) {
    if (killed) *killed = false;
    const glm::dvec3 dir = glm::normalize(dirIn);
    Animal* best = nullptr;
    double bestT = 1e30;
    for (Animal& a : animals_) {
        const AnimalSpecies& s = cfg_->species[(size_t)a.species];
        double t;
        if (a.dead || a.airborne || s.model == "bird" || !hitTest(a, eye, dir, reach, t) || t >= bestT) continue;
        best = &a;
        bestT = t;
    }
    if (!best) return 0;
    Animal& a = *best;
    const AnimalSpecies& s = cfg_->species[(size_t)a.species];
    a.hp -= damage / toughness(s);
    a.attackAnim = -1;
    a.hitAnim = 0;  // zuckt (Treffer-Ablauf), von der Seite, von der der Schlag kam
    a.hitLeft = glm::dot(glm::dvec2(eye.x - a.pos.x, eye.z - a.pos.z), glm::dvec2(std::cos(a.yaw), -std::sin(a.yaw))) < 0;
    AnimalGroup* g = findGroup(a.group);
    const glm::dvec2 from = xz(eye);
    if (a.hp <= 0) {  // tot: liegt da, die anderen fliehen
        a.hp = 0;
        a.dead = true;
        a.deadTime = 0;
        a.speed = a.wantSpeed = 0;
        a.state = Animal::Idle;
        a.leader = false;
        a.group = 0;
        if (g) {
            g->attackUntil = -1;
            g->calmUntil = clock_ + 60.0;
            g->fleeUntil = clock_ + rnd(6, 10);
            g->fleeFrom = from;
        }
        if (killed) *killed = true;
        return a.id;
    }
    if (g) {
        if (s.attack != AnimalAttack::None && a.hp >= 0.45f) {  // wehrt sich
            if (g->attackUntil < clock_) g->attackStart = clock_;
            g->calmUntil = -1;
            g->attackUntil = clock_ + 6.0;
            a.retreat = 0.6f;
        } else {  // flieht (verwundet nur humpelnd)
            g->attackUntil = -1;
            g->calmUntil = clock_ + 60.0;
            g->fleeUntil = clock_ + rnd(6, 10);
            g->fleeFrom = from;
        }
    }
    return a.id;
}

uint32_t AnimalWorld::pickDead(const glm::dvec3& eye, const glm::dvec3& dirIn, double reach) const {
    const glm::dvec3 dir = glm::normalize(dirIn);
    uint32_t best = 0;
    double bestT = 1e30, t;
    for (const Animal& a : animals_)
        if (a.dead && hitTest(a, eye, dir, reach, t) && t < bestT) best = a.id, bestT = t;
    return best;
}

const Animal* AnimalWorld::find(uint32_t id) const {
    for (const Animal& a : animals_)
        if (a.id == id) return &a;
    return nullptr;
}

bool AnimalWorld::remove(uint32_t id) {
    const size_t n = animals_.size();
    animals_.erase(std::remove_if(animals_.begin(), animals_.end(), [&](const Animal& a) { return a.id == id; }), animals_.end());
    return animals_.size() != n;
}

int AnimalWorld::scare(const glm::dvec3& at, double radius) {
    int n = 0;
    for (const Animal& a : animals_) {
        if (a.dead || a.airborne || std::abs(a.pos.y - at.y) > 3.0 || glm::length(xz(a.pos) - xz(at)) > radius) continue;
        AnimalGroup* g = findGroup(a.group);
        if (!g || g->calmUntil > clock_ + 30.0) continue;  // schon vertrieben
        g->attackUntil = -1;
        g->calmUntil = clock_ + 45.0;
        g->fleeUntil = clock_ + rnd(4, 7);
        g->fleeFrom = xz(at);
        ++n;
    }
    return n;
}

void AnimalWorld::fly(Animal& a, AnimalGroup& g, const Animal* lead, const Terrain& tr, double dt) {
    const AnimalSpecies& s = cfg_->species[(size_t)a.species];
    const glm::dvec2 p = xz(a.pos);
    double ground = tr.surfaceHeight(p.x, p.y);
    if (tr.hasRivers() || tr.gen().hasLakes()) ground = std::max(ground, tr.riverLevel());
    a.timer -= (float)dt;
    float speed = s.flySpeed, turn = 1.2f;
    glm::dvec2 desired = heading(a.yaw);
    double wantAlt = a.altitude;
    float flapWant = 1.0f;

    if (s.motion == AnimalMotion::Ground) {
        // aufgeflogene Taube: weg vom Spieler, steigen, später wieder landen
        speed = std::max(s.flySpeed, 4.0f);
        turn = 2.0f;
        if (a.timer > 0) {
            wantAlt = 9.0 + 4.0 * a.seed;
        } else {
            wantAlt = 0;
            speed *= 0.6f;
        }
        a.altitude += std::clamp(wantAlt - a.altitude, -2.5 * dt, 4.0 * dt);
        if (a.timer <= 0 && a.altitude < 0.05) {
            if (walkable(tr, s, p.x, p.y)) {
                a.airborne = false;
                a.altitude = 0;
                a.speed = 0;
                a.state = Animal::Idle;
                a.timer = rnd(2, 6);
                if (a.leader) g.home = p;
                a.offset = lead && !a.leader ? glm::dvec2(p - xz(lead->pos)) : a.offset;
                if (glm::length(a.offset) > s.spread * 2) a.offset *= s.spread / glm::length(a.offset);
                a.pos.y = tr.surfaceHeight(p.x, p.y);
                return;
            }
            a.timer = 1.0f;  // über Wasser: weiterfliegen
        }
    } else if (s.motion == AnimalMotion::Soar) {
        // Kreise ziehen; ab und zu ein neuer Kreis in der Nähe
        if (a.timer <= 0) {
            const float ang = rnd(-(float)kPi, (float)kPi);
            a.center = g.home + glm::dvec2(std::cos(ang), std::sin(ang)) * (double)(s.wander * std::sqrt(rnd(0, 1)));
            a.radius = rnd(s.circleMin, s.circleMax);
            a.altitude = rnd(s.flyMin, s.flyMax);
            a.timer = rnd(20, 60);
        }
        const glm::dvec2 rel = p - a.center;
        const double dist = std::max(glm::length(rel), 1e-3);
        const glm::dvec2 u = rel / dist, t = glm::dvec2(-u.y, u.x) * (a.seed < 0.5 ? 1.0 : -1.0);
        desired = glm::normalize(t + (-u) * std::clamp((dist - a.radius) / a.radius * 2.0, -1.5, 3.0));
        turn = std::max(0.25f, speed / (float)a.radius * 1.6f);
        // meist gleiten, selten ein paar Flügelschläge
        flapWant = std::sin(clock_ * 0.37 + a.seed * 20.0) > 0.82 ? 1.0f : 0.0f;
    } else if (s.motion == AnimalMotion::Flock) {
        if (a.leader || !lead) {
            if (a.timer <= 0 || glm::length(a.target - p) < 15.0) {
                const float ang = rnd(-(float)kPi, (float)kPi);
                a.target = g.home + glm::dvec2(std::cos(ang), std::sin(ang)) * (double)(s.wander * std::sqrt(rnd(0.1f, 1)));
                a.altitude = rnd(s.flyMin, s.flyMax);
                a.timer = rnd(8, 20);
            }
            desired = glm::normalize(a.target - p + glm::dvec2(1e-6, 0));
            turn = 1.4f;
        } else {
            // im Schwarm: Platz neben dem ersten, etwas wogend
            const glm::dvec2 slot = xz(lead->pos) + a.offset * 0.7 + glm::dvec2(std::sin(clock_ * 0.7 + a.seed * 9), std::cos(clock_ * 0.6 + a.seed * 7)) * 2.0;
            const glm::dvec2 to = slot - p;
            const double dist = glm::length(to);
            desired = dist > 1e-3 ? glm::normalize(to + heading(lead->yaw) * 6.0) : heading(lead->yaw);
            speed = (float)std::clamp(s.flySpeed + (dist - 3.0) * 0.8, s.flySpeed * 0.6, s.flySpeed * 1.6);
            wantAlt = lead->altitude + a.offset.y * 0.15;
            turn = 2.5f;
        }
        flapWant = std::sin(clock_ * 0.8 + a.seed * 30.0) > 0.6 ? 0.0f : 1.0f;  // Flügelschläge, dazwischen kurz gleiten
    } else {  // Flutter (Fledermaus): zickzack um ihren Platz
        if (a.timer <= 0 || glm::length(a.target - p) < 2.0) {
            const float ang = rnd(-(float)kPi, (float)kPi);
            a.target = g.home + glm::dvec2(std::cos(ang), std::sin(ang)) * (double)(s.wander * std::sqrt(rnd(0.05f, 1)));
            a.altitude = rnd(s.flyMin, s.flyMax);
            a.timer = rnd(0.8f, 2.5f);
        }
        desired = glm::normalize(a.target - p + glm::dvec2(1e-6, 0));
        turn = 5.0f;
    }
    if (s.motion != AnimalMotion::Ground) {
        const double dz = wantAlt - (a.pos.y - ground);
        a.altitude = wantAlt;
        a.pos.y += std::clamp(dz, -3.0, 3.0) * std::min(1.0, dt * 1.2);
        a.pos.y = std::max(a.pos.y, ground + (s.motion == AnimalMotion::Flutter ? 1.0 : 3.0));
    } else {
        a.pos.y = std::max(tr.surfaceHeight(p.x, p.y), ground) + a.altitude;
    }
    const float diff = wrapAngle(yawOf(desired) - a.yaw), step = turn * (float)dt;
    const float yawRate = std::clamp(diff, -step, step) / std::max((float)dt, 1e-4f);
    a.yaw = wrapAngle(a.yaw + yawRate * (float)dt);
    a.speed += (speed - a.speed) * (1.0f - (float)std::exp(-dt / 0.8));
    const glm::dvec2 next = p + heading(a.yaw) * (double)(a.speed * dt);
    a.pos.x = next.x;
    a.pos.z = next.y;
    // in der Kurve schräg, beim Steigen Nase hoch
    const float roll = std::clamp(std::atan(a.speed * yawRate / 9.81f), -0.9f, 0.9f);
    a.roll += (roll - a.roll) * (1.0f - (float)std::exp(-dt / 0.3));
    a.pitch *= (float)std::exp(-dt / 0.3);
    a.flapAmount += (flapWant - a.flapAmount) * (1.0f - (float)std::exp(-dt / 0.35));
    a.flap += a.flapRate * (float)dt * (0.35f + 0.65f * a.flapAmount);
    a.flap -= std::floor(a.flap);
    if (a.leader && s.motion == AnimalMotion::Flock) g.home += (p - g.home) * std::min(1.0, dt * 0.02);  // der Schwarm zieht weiter
}

void AnimalWorld::update(double dt, const Terrain& tr, const glm::dvec3& focus, const glm::dvec3& view, const std::vector<AnimalScarer>& scarers,
                         double dayHours, double viewScale) {
    dt = std::clamp(dt, 0.0, 0.1);
    clock_ += dt;
    dayHours_ = dayHours;
    viewScale_ = std::clamp(viewScale, 0.3, 3.0);
    if (!cfg_->enabled) {
        clear();
        return;
    }
    if (gen_ != &tr.gen()) {  // neuer Generator: Biome neu zuordnen, alte Tiere weg
        gen_ = &tr.gen();
        clear();
        const GenConfig& gc = gen_->config();
        biomeEntries_.assign(gc.biomes.size(), {});
        for (const auto& [bid, entries] : cfg_->biomes) {
            const int b = gc.findBiome(bid);
            if (b >= 0) biomeEntries_[(size_t)b] = entries;
        }
    }
    spawnClock_ += dt;
    for (int tries = 0; spawnClock_ >= cfg_->interval && tries < 8; ++tries) {
        spawnClock_ -= cfg_->interval;
        trySpawn(tr, focus, view, dayHours);
    }
    spawnClock_ = std::min(spawnClock_, (double)cfg_->interval);

    // Gruppen: erstes Tier (sonst das nächste) führt
    std::vector<int> leaderOf(groups_.size(), -1);
    std::map<uint32_t, size_t> groupIndex;
    for (size_t i = 0; i < groups_.size(); ++i) groupIndex[groups_[i].id] = i;
    for (size_t i = 0; i < animals_.size(); ++i) {
        auto it = groupIndex.find(animals_[i].group);
        if (it == groupIndex.end()) continue;
        int& l = leaderOf[it->second];
        if (animals_[i].leader) l = (int)i;
    }
    for (size_t i = 0; i < animals_.size(); ++i) {
        auto it = groupIndex.find(animals_[i].group);
        if (it != groupIndex.end() && leaderOf[it->second] < 0) {
            leaderOf[it->second] = (int)i;
            animals_[i].leader = true;
        }
    }
    for (size_t i = 0; i < animals_.size(); ++i) {
        Animal& a = animals_[i];
        if (a.dead) {  // liegt da
            a.deadTime += (float)dt;
            continue;
        }
        a.hp = std::min(1.0f, a.hp + 0.004f * (float)dt);  // Wunden heilen langsam (ca. 4 min)
        auto it = groupIndex.find(a.group);
        if (it == groupIndex.end()) continue;
        AnimalGroup& g = groups_[it->second];
        const int li = leaderOf[it->second];
        const Animal* lead = li >= 0 && li != (int)i ? &animals_[(size_t)li] : nullptr;
        const AnimalSpecies& s = cfg_->species[(size_t)a.species];
        if (s.motion == AnimalMotion::Ground && !a.airborne) think(a, g, lead, tr, dt, scarers);
        if (s.motion == AnimalMotion::Ground && a.airborne) {
            think(a, g, lead, tr, dt, scarers);  // nur Schreck (der Flug kommt gleich)
            if (a.airborne) fly(a, g, lead, tr, dt);
        } else if (s.motion != AnimalMotion::Ground) {
            fly(a, g, lead, tr, dt);
        }
        if (a.leader) g.pos = xz(a.pos);
    }

    // verschwinden: zu weit weg, oder nicht mehr ihre Tageszeit (dann nur, wenn man nicht hinsieht)
    const glm::dvec2 f = xz(focus);
    glm::dvec2 v2(view.x, view.z);
    if (glm::length(v2) > 1e-6) v2 = glm::normalize(v2);
    std::vector<Animal> keep;
    keep.reserve(animals_.size());
    for (Animal& a : animals_) {
        const AnimalSpecies& s = cfg_->species[(size_t)a.species];
        const glm::dvec2 rel = xz(a.pos) - f;
        const double dist = glm::length(rel);
        bool gone = dist > reach(s) * 1.08 || (a.dead && a.deadTime > 600.0f);  // Kadaver: nach 10 min weg
        if (!gone && !activeAt(s.active, dayHours) && dist > 20) gone = glm::dot(rel / dist, v2) < 0.3;
        if (!gone) keep.push_back(std::move(a));
    }
    animals_.swap(keep);
    std::map<uint32_t, int> sizes;
    for (const Animal& a : animals_) ++sizes[a.group];
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [&](AnimalGroup& g) {
                      auto it = sizes.find(g.id);
                      g.size = it == sizes.end() ? 0 : it->second;
                      return g.size == 0;
                  }),
                  groups_.end());
}
