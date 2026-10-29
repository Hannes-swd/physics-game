// Weltgenerierung: Einstellungen lesen (biomes.json), Klima, Biome, Höhe (siehe WorldGen.h)
#include "WorldGen.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Json.h"
#include "Materials.h"
#include "Noise.h"

const char* const kClimateNames[kClimateCount] = {"temperature", "humidity", "continentalness", "erosion"};
const char* const kScatterNames[kScatterKinds] = {"stones", "sticks", "logs", "stumps"};

namespace {

static const unsigned char kBuiltinBiomes[] = {
#include "biomes.json.inc"
    0};

std::filesystem::path exeDir() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? std::filesystem::path(buf).parent_path() : std::filesystem::current_path();
}

bool readVec3(const Json& j, glm::vec3& out) {
    if (j.type != Json::Array || j.size() != 3) return false;
    for (int i = 0; i < 3; ++i)
        if (j[i].type != Json::Number) return false;
    out = glm::vec3((float)j[0].num, (float)j[1].num, (float)j[2].num);
    return true;
}

// Zahl lesen: fehlt sie, bleibt der Standard; ist sie da, aber falsch, ist das ein Fehler
bool num(const Json& obj, const char* key, double& out, std::string& err, const std::string& where) {
    const Json& j = obj[key];
    if (j.type == Json::Null) return true;
    if (j.type != Json::Number) {
        err = where + "." + key + ": number expected";
        return false;
    }
    out = j.num;
    return true;
}

// Liste von Farben [[r, g, b], ...] (oder eine einzelne [r, g, b])
bool readColors(const Json& j, std::vector<glm::vec3>& out) {
    out.clear();
    glm::vec3 c;
    if (readVec3(j, c)) {
        out.push_back(c);
        return true;
    }
    if (j.type != Json::Array || j.size() == 0) return false;
    for (size_t i = 0; i < j.size(); ++i) {
        if (!readVec3(j[i], c)) return false;
        out.push_back(c);
    }
    return true;
}

// [von, bis]; fehlt er, bleibt der Standard
bool numRange(const Json& obj, const char* key, double& lo, double& hi, std::string& err, const std::string& where) {
    const Json& a = obj[key];
    if (a.type == Json::Null) return true;
    if (a.type != Json::Array || a.size() != 2 || a[0].type != Json::Number || a[1].type != Json::Number) {
        err = where + "." + key + ": [from, to] expected";
        return false;
    }
    lo = a[0].num;
    hi = std::max(a[1].num, lo);
    return true;
}

double smooth01(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
// weiches Minimum / Maximum (Übergang über k)
double smin(double a, double b, double k) {
    const double h = std::max(k - std::abs(a - b), 0.0) / k;
    return std::min(a, b) - h * h * k * 0.25;
}
double smax(double a, double b, double k) { return -smin(-a, -b, k); }

} // namespace

int GenConfig::findBiome(const std::string& id) const {
    for (size_t i = 0; i < biomes.size(); ++i)
        if (biomes[i].id == id) return (int)i;
    return -1;
}

bool parseGenConfig(const std::string& text, GenConfig& out, std::string* errOut) {
    std::string err;
    auto fail = [&](const std::string& e) {
        if (errOut) *errOut = e;
        return false;
    };
    Json j;
    if (!parseJson(text, j, &err)) return fail(err);
    if (j.type != Json::Object) return fail("the file must contain an object { ... }");
    GenConfig c;
    c.text = text;
    if (const Json& cl = j["climate"]; cl.type == Json::Object) {
        for (const auto& [key, v] : cl.obj) {
            int p = -1;
            for (int i = 0; i < kClimateCount; ++i)
                if (key == kClimateNames[i]) p = i;
            if (p < 0) return fail("climate: unknown value '" + key + "' (temperature, humidity, continentalness, erosion)");
            const std::string w = "climate." + key;
            if (v.type != Json::Object) return fail(w + ": object expected");
            if (v["label"].type == Json::String) c.climate[p].label = v["label"].str;
            double oct = c.climate[p].octaves;
            if (!num(v, "scale", c.climate[p].scale, err, w) || !num(v, "octaves", oct, err, w) || !num(v, "contrast", c.climate[p].contrast, err, w))
                return fail(err);
            c.climate[p].octaves = std::clamp((int)oct, 1, 8);
            if (c.climate[p].scale < 1) return fail(w + ".scale: must be at least 1 m");
        }
    }
    if (!num(j, "border_blend", c.blend, err, "")) return fail(err.substr(1));
    c.blend = std::clamp(c.blend, 0.001, 0.5);
    const Json& bl = j["biomes"];
    if (bl.type != Json::Array || bl.size() == 0) return fail("biomes: a list with at least one biome is needed");
    if (bl.size() > kMaxBiomes) return fail("biomes: at most " + std::to_string(kMaxBiomes) + " biomes");
    for (size_t i = 0; i < bl.size(); ++i) {
        const Json& b = bl[i];
        const std::string w = "biomes[" + std::to_string(i) + "]";
        if (b.type != Json::Object) return fail(w + ": object expected");
        GenConfig::Biome d;
        if (b["id"].type != Json::String || b["id"].str.empty()) return fail(w + ".id: text expected (e.g. \"plains\")");
        d.id = b["id"].str;
        d.label = b["label"].type == Json::String ? b["label"].str : d.id;
        if (c.findBiome(d.id) >= 0) return fail(w + ".id: '" + d.id + "' is used twice");
        if (const Json& cl = b["climate"]; cl.type == Json::Object) {
            for (const auto& [key, v] : cl.obj) {
                int p = -1;
                for (int k = 0; k < kClimateCount; ++k)
                    if (key == kClimateNames[k]) p = k;
                if (p < 0) return fail(w + ".climate: unknown value '" + key + "' (temperature, humidity, continentalness, erosion)");
                if (v.type != Json::Array || v.size() != 2 || v[0].type != Json::Number || v[1].type != Json::Number)
                    return fail(w + ".climate." + key + ": [from, to] expected, e.g. [0.2, 0.6]");
                d.lo[p] = std::clamp(v[0].num, 0.0, 1.0);
                d.hi[p] = std::clamp(v[1].num, 0.0, 1.0);
                if (d.hi[p] < d.lo[p]) return fail(w + ".climate." + key + ": 'to' is smaller than 'from'");
            }
        }
        if (const Json& h = b["height"]; h.type == Json::Object) {
            const std::string wh = w + ".height";
            double oct = d.height.octaves;
            if (!num(h, "base", d.height.base, err, wh) || !num(h, "amplitude", d.height.amplitude, err, wh) ||
                !num(h, "scale", d.height.scale, err, wh) || !num(h, "octaves", oct, err, wh) ||
                !num(h, "persistence", d.height.persistence, err, wh) || !num(h, "lacunarity", d.height.lacunarity, err, wh))
                return fail(err);
            d.height.octaves = std::clamp((int)oct, 1, 10);
            if (d.height.scale < 0.5) return fail(wh + ".scale: must be at least 0.5 m");
        }
        if (const Json& s = b["surface"]; s.type == Json::Object) {
            const std::string ws = w + ".surface";
            if (s.has("color") && !readVec3(s["color"], d.color)) return fail(ws + ".color: [r, g, b] expected");
            if (s.has("slope_color") && !readVec3(s["slope_color"], d.slopeColor)) return fail(ws + ".slope_color: [r, g, b] expected");
            double a = d.slopeStart, e = d.slopeEnd;
            if (!num(s, "slope_start", a, err, ws) || !num(s, "slope_end", e, err, ws)) return fail(err);
            d.slopeStart = (float)a;
            d.slopeEnd = (float)std::max(e, a + 0.01);
            if (s["material"].type == Json::String) d.material = s["material"].str;
        }
        if (b.has("border_color") && !readVec3(b["border_color"], d.borderColor)) return fail(w + ".border_color: [r, g, b] expected");
        if (!num(b, "rivers", d.rivers, err, w)) return fail(err);
        d.rivers = std::clamp(d.rivers, 0.0, 1.0);
        if (const Json& tr = b["trees"]; tr.type == Json::Object) {
            if (!num(tr, "per_ha", d.treesPerHa, err, w + ".trees")) return fail(err);
            d.treesPerHa = std::clamp(d.treesPerHa, 0.0, 2000.0);
            if (const Json& ty = tr["types"]; ty.type == Json::Object)
                for (const auto& [key, v] : ty.obj) {
                    if (v.type != Json::Number) return fail(w + ".trees.types." + key + ": weight (number) expected");
                    if (v.num > 0) d.treeTypes.emplace_back(key, v.num);
                }
        }
        GenConfig::Biome::Flora& f = d.flora;
        if (const Json& g = b["grass"]; g.type == Json::Object) {
            const std::string wg = w + ".grass";
            f.given = true;
            if (!num(g, "density", f.grass, err, wg) || !numRange(g, "height", f.grassHeightMin, f.grassHeightMax, err, wg) ||
                !num(g, "patches", f.grassPatches, err, wg))
                return fail(err);
            if (g.has("colors") && !readColors(g["colors"], f.grassColors)) return fail(wg + ".colors: [[r, g, b], ...] expected");
            f.grass = std::clamp(f.grass, 0.0, 40.0);
            f.grassHeightMin = std::clamp(f.grassHeightMin, 0.02, 1.9);
            f.grassHeightMax = std::clamp(f.grassHeightMax, f.grassHeightMin, 1.9);
            f.grassPatches = std::clamp(f.grassPatches, 0.0, 1.0);
        }
        if (const Json& g = b["flowers"]; g.type == Json::Object) {
            f.given = true;
            if (!num(g, "density", f.flowers, err, w + ".flowers")) return fail(err);
            if (g.has("colors") && !readColors(g["colors"], f.flowerColors)) return fail(w + ".flowers.colors: [[r, g, b], ...] expected");
            f.flowers = std::clamp(f.flowers, 0.0, 10.0);
        }
        if (const Json& g = b["bushes"]; g.type == Json::Object) {
            const std::string wb = w + ".bushes";
            f.given = true;
            if (!num(g, "per_ha", f.bushesPerHa, err, wb) || !numRange(g, "size", f.bushSizeMin, f.bushSizeMax, err, wb)) return fail(err);
            if (g.has("colors") && !readColors(g["colors"], f.bushColors)) return fail(wb + ".colors: [[r, g, b], ...] expected");
            f.bushesPerHa = std::clamp(f.bushesPerHa, 0.0, 3000.0);
            f.bushSizeMin = std::clamp(f.bushSizeMin, 0.1, 6.0);
            f.bushSizeMax = std::clamp(f.bushSizeMax, f.bushSizeMin, 6.0);
        }
        if (f.grassColors.empty()) f.grassColors = {d.color * 1.3f};
        if (f.flowerColors.empty()) f.flowerColors = {glm::vec3(0.9f, 0.85f, 0.2f)};
        if (f.bushColors.empty()) f.bushColors = {glm::vec3(0.12f, 0.22f, 0.06f)};
        // Zeug am Boden: "stones" (size), "sticks" und "logs" (length, thickness), "stumps" (height, thickness)
        for (int k = 0; k < kScatterKinds; ++k) {
            const Json& g = b[kScatterNames[k]];
            if (g.type != Json::Object) continue;
            const std::string ws = w + "." + kScatterNames[k];
            GenConfig::Biome::Scatter& st = d.scatter[k];
            st.given = true;
            const char* sizeKey = k == kScatterStone ? "size" : k == kScatterStump ? "height" : "length";
            if (!num(g, "per_ha", st.perHa, err, ws) || !numRange(g, sizeKey, st.sizeMin, st.sizeMax, err, ws) ||
                !numRange(g, "thickness", st.thickMin, st.thickMax, err, ws) || !num(g, "skew", st.skew, err, ws))
                return fail(err);
            if (const Json& m = g["material"]; m.type == Json::String) {
                st.materials = {m.str};
            } else if (m.type == Json::Array) {
                for (size_t i = 0; i < m.size(); ++i)
                    if (m[i].type == Json::String) st.materials.push_back(m[i].str);
            }
            st.perHa = std::clamp(st.perHa, 0.0, 5000.0);
            st.sizeMin = std::clamp(st.sizeMin, 0.02, 20.0);
            st.sizeMax = std::clamp(st.sizeMax, st.sizeMin, 20.0);
            st.thickMin = std::clamp(st.thickMin, 0.005, 3.0);
            st.thickMax = std::clamp(st.thickMax, st.thickMin, 3.0);
            st.skew = std::clamp(st.skew, 0.2, 10.0);
        }
        c.biomes.push_back(std::move(d));
    }
    if (const Json& r = j["rivers"]; r.type == Json::Object) {
        GenConfig::Rivers& v = c.rivers;
        v.enabled = true;
        const std::string w = "rivers";
        auto range = [&](const char* key, double& lo, double& hi) {
            const Json& a = r[key];
            if (a.type == Json::Null) return true;
            if (a.type != Json::Array || a.size() != 2 || a[0].type != Json::Number || a[1].type != Json::Number) {
                err = w + "." + key + ": [from, to] expected";
                return false;
            }
            lo = a[0].num;
            hi = std::max(a[1].num, lo);
            return true;
        };
        double on = 1;
        if (!num(r, "enabled", on, err, w) || !num(r, "level", v.level, err, w) || !num(r, "spacing", v.spacing, err, w) ||
            !num(r, "presence", v.presence, err, w) || !num(r, "meander", v.meander, err, w) ||
            !num(r, "meander_scale", v.meanderScale, err, w) || !range("width", v.widthMin, v.widthMax) ||
            !range("depth", v.depthMin, v.depthMax) || !num(r, "bank", v.bank, err, w) || !num(r, "floor", v.floor, err, w) ||
            !num(r, "valley", v.valley, err, w) || !num(r, "valley_slope", v.valleySlope, err, w) || !range("speed", v.speedMin, v.speedMax))
            return fail(err);
        if (r.has("bed_color") && !readVec3(r["bed_color"], v.bedColor)) return fail(w + ".bed_color: [r, g, b] expected");
        v.enabled = on != 0;
        v.spacing = std::max(v.spacing, 50.0);
        v.presence = std::clamp(v.presence, 0.0, 1.0);
        v.meanderScale = std::max(v.meanderScale, 10.0);
        v.widthMin = std::max(v.widthMin, 0.5);
        v.widthMax = std::max(v.widthMax, v.widthMin);
        v.depthMin = std::max(v.depthMin, 0.1);
        v.depthMax = std::max(v.depthMax, v.depthMin);
        v.bank = std::max(v.bank, 0.05);
        v.floor = std::max(v.floor, 0.05);
        v.valley = std::max(v.valley, 5.0);
        v.valleySlope = std::clamp(v.valleySlope, 0.05, 2.0);
    }
    if (const Json& fl = j["flora"]; fl.type == Json::Object) {
        if (!num(fl, "grass_distance", c.flora.grassDistance, err, "flora") || !num(fl, "bush_distance", c.flora.bushDistance, err, "flora"))
            return fail(err);
        c.flora.grassDistance = std::clamp(c.flora.grassDistance, 0.0, 150.0);
        c.flora.bushDistance = std::clamp(c.flora.bushDistance, 0.0, 400.0);
    }
    if (const Json& sv = j["scatter"]; sv.type == Json::Object) {
        GenConfig::ScatterView& v = c.scatterView;
        if (!num(sv, "distance", v.drawDistance, err, "scatter") || !num(sv, "stick_distance", v.stickDistance, err, "scatter") ||
            !num(sv, "solid_distance", v.solidDistance, err, "scatter") || !num(sv, "big_stone", v.bigStone, err, "scatter"))
            return fail(err);
        v.drawDistance = std::clamp(v.drawDistance, 0.0, 1000.0);
        v.stickDistance = std::clamp(v.stickDistance, 0.0, 200.0);
        v.solidDistance = std::clamp(v.solidDistance, 0.0, 100.0);
        v.bigStone = std::clamp(v.bigStone, 0.05, 5.0);
    }
    out = std::move(c);
    return true;
}

const GenConfig& builtinGenConfig() {
    static const GenConfig cfg = [] {
        GenConfig c;
        std::string err;
        if (!parseGenConfig(reinterpret_cast<const char*>(kBuiltinBiomes), c, &err)) {
            std::fprintf(stderr, "built-in biomes.json: %s\n", err.c_str());
            c.biomes.push_back(GenConfig::Biome{"plains", "Ebene"});
        }
        return c;
    }();
    return cfg;
}

std::string findBiomesFile() {
    std::error_code ec;
    const std::filesystem::path dir = exeDir();
    for (const std::filesystem::path& p : {dir / "biomes.json", std::filesystem::current_path(ec) / "assets" / "biomes.json",
                                           dir / "assets" / "biomes.json", dir.parent_path().parent_path() / "assets" / "biomes.json"})
        if (std::filesystem::is_regular_file(p, ec)) return p.lexically_normal().u8string();
    return {};
}

std::shared_ptr<const GenConfig> loadBiomesFile(std::string* report) {
    const std::string path = findBiomesFile();
    if (!path.empty()) {
        std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        auto c = std::make_shared<GenConfig>();
        std::string err;
        if (parseGenConfig(ss.str(), *c, &err)) return c;
        if (report) *report = path + ": " + err + " - using the built-in biomes";
    }
    return std::make_shared<GenConfig>(builtinGenConfig());
}

// ============================================================================ Generator

WorldGen::WorldGen(uint64_t seed, std::shared_ptr<const GenConfig> config, int singleBiome)
    : seed_(seed), cfg_(std::move(config)), single_(singleBiome) {
    if (!cfg_ || cfg_->biomes.empty()) cfg_ = std::make_shared<GenConfig>(builtinGenConfig());
    if (single_ >= (int)cfg_->biomes.size()) single_ = -1;
    for (int p = 0; p < kClimateCount; ++p) climateSeed_[p] = noise::subSeed(seed_, std::string("climate/") + kClimateNames[p]);
    const char* riverPurpose[5] = {"rivers/lines", "rivers/meander-x", "rivers/meander-z", "rivers/presence", "rivers/width"};
    for (int i = 0; i < 5; ++i) riverSeed_[i] = noise::subSeed(seed_, riverPurpose[i]);
    for (const GenConfig::Biome& b : cfg_->biomes) {
        heightSeed_.push_back(noise::subSeed(seed_, "height/" + b.id));  // je Biom nach seinem Namen: neue Biome ändern alte nicht
        const int m = findMaterialIndex(b.material.c_str());
        material_.push_back(m >= 0 ? m : materialIndex("concrete"));
    }
}

Climate WorldGen::climate(double x, double z) const {
    Climate c;
    for (int p = 0; p < kClimateCount; ++p) {
        const GenConfig::Climate& k = cfg_->climate[p];
        const double n = noise::fbm(climateSeed_[p], x / k.scale, z / k.scale, k.octaves);
        c.v[p] = std::clamp(0.5 + 0.5 * n * k.contrast, 0.0, 1.0);
    }
    return c;
}

int WorldGen::classify(const Climate& c, double* scoresOut) const {
    // Score eines Bioms: drinnen = Abstand zum nächsten eigenen Rand (Ränder bei 0 und 1 zählen nicht), draußen =
    // -Abstand zum Bereich. Stetig: an einer Grenze zwischen zwei Biomen sind beide Scores gleich
    double local[kMaxBiomes];
    double* scores = scoresOut ? scoresOut : local;
    int best = 0;
    for (size_t b = 0; b < cfg_->biomes.size(); ++b) {
        const GenConfig::Biome& d = cfg_->biomes[b];
        double out2 = 0, depth = 1.0;
        for (int p = 0; p < kClimateCount; ++p) {
            const double v = c.v[p];
            if (v < d.lo[p]) out2 += (d.lo[p] - v) * (d.lo[p] - v);
            else if (v > d.hi[p]) out2 += (v - d.hi[p]) * (v - d.hi[p]);
            else {
                if (d.lo[p] > 0) depth = std::min(depth, v - d.lo[p]);
                if (d.hi[p] < 1) depth = std::min(depth, d.hi[p] - v);
            }
        }
        scores[b] = out2 > 0 ? -std::sqrt(out2) : depth;
        if (scores[b] > scores[best]) best = (int)b;  // bei Gleichstand gewinnt das erste in der Liste
    }
    return best;
}

double WorldGen::biomeHeight(int b, double x, double z) const {
    const GenConfig::Biome::Height& h = cfg_->biomes[(size_t)b].height;
    return h.base + h.amplitude * noise::fbm(heightSeed_[(size_t)b], x / h.scale, z / h.scale, h.octaves, h.persistence, h.lacunarity);
}

void WorldGen::sample(double x, double z, GenSample& out) const {
    const size_t n = cfg_->biomes.size();
    if (single_ >= 0) {  // nur ein Biom: die ganze Welt ist so
        const GenConfig::Biome& d = cfg_->biomes[(size_t)single_];
        out.biome = single_;
        for (size_t b = 0; b < n; ++b) out.score[b] = (int)b == single_ ? 1.0 : -1.0;
        out.height = biomeHeight(single_, x, z);
        out.color = d.color;
        out.slopeColor = d.slopeColor;
        out.slopeStart = d.slopeStart;
        out.slopeEnd = d.slopeEnd;
        out.material = material_[(size_t)single_];
        applyRivers(x, z, out);
        return;
    }
    const Climate c = climate(x, z);
    out.biome = classify(c, out.score);
    // Gewichte: weich über die Übergangsbreite, nur Biome nahe genug werden ausgerechnet
    double w[kMaxBiomes], sum = 0;
    for (size_t b = 0; b < n; ++b) {
        w[b] = std::clamp(0.5 + out.score[b] / (2.0 * cfg_->blend), 0.0, 1.0);
        w[b] = w[b] * w[b] * (3.0 - 2.0 * w[b]);  // weicher Anfang und weiches Ende
        sum += w[b];
    }
    if (sum <= 1e-9) {
        for (size_t b = 0; b < n; ++b) w[b] = (int)b == out.biome ? 1.0 : 0.0;
        sum = 1.0;
    }
    out.height = 0;
    out.color = out.slopeColor = glm::vec3(0);
    out.slopeStart = out.slopeEnd = 0;
    for (size_t b = 0; b < n; ++b) {
        if (w[b] <= 0) continue;
        const double f = w[b] / sum;
        const GenConfig::Biome& d = cfg_->biomes[b];
        out.height += f * biomeHeight((int)b, x, z);
        out.color += (float)f * d.color;
        out.slopeColor += (float)f * d.slopeColor;
        out.slopeStart += (float)f * d.slopeStart;
        out.slopeEnd += (float)f * d.slopeEnd;
    }
    out.material = material_[(size_t)out.biome];
    applyRivers(x, z, out);
}

// ============================================================================ Flüsse

double WorldGen::riverFactor(const double* scores) const {
    // Je Biom mit weniger Flüssen: wo es gewinnt oder knapp dahinter liegt sein Wert, weiter weg (Abstand zum besten
    // Score, über 4 × border_blend) bis 1. Davon das Kleinste: stetig, und wo ein Biom ohne Flüsse gewinnt, genau 0 (sie
    // laufen schon vor seiner Grenze aus). Relativ zum besten Score, weil in Lücken des Klimas auch ein Biom gewinnt,
    // das gar nicht passt (Score < 0)
    if (single_ >= 0) return cfg_->biomes[(size_t)single_].rivers;
    const size_t n = cfg_->biomes.size();
    double best = scores[0];
    for (size_t b = 1; b < n; ++b) best = std::max(best, scores[b]);
    const double wide = 4.0 * cfg_->blend;
    double f = 1.0;
    for (size_t b = 0; b < n; ++b) {
        const double r = cfg_->biomes[b].rivers;
        if (r < 1.0) f = std::min(f, r + (1.0 - r) * smooth01((best - scores[b] - cfg_->blend) / wide));
    }
    return f;
}

void WorldGen::river(double x, double z, RiverSample& out) const {
    double s[kMaxBiomes] = {};
    if (single_ < 0) classify(climate(x, z), s);
    river(x, z, riverFactor(s), out);
}

void WorldGen::river(double x, double z, double factor, RiverSample& out) const {
    out = RiverSample();
    const GenConfig::Rivers& rv = cfg_->rivers;
    if (!rv.enabled || factor <= 0.0 || rv.presence <= 0.0) return;
    // Lücken: nur wo ein weiteres Rauschfeld über der Schwelle liegt, führt die Linie Wasser. Weniger Flüsse im Biom heißt
    // höhere Schwelle. Am Übergang läuft der Fluss aus (schmaler, flacher, bis das Bett über dem Wasser liegt)
    const double ps = rv.spacing * 3.0;  // groß: lange Flüsse mit seltenen Lücken
    const double p = std::clamp(0.5 + 0.8 * noise::fbm(riverSeed_[3], x / ps, z / ps, 2), 0.0, 1.0);
    const double q = smooth01((p - (1.0 - rv.presence * factor)) / 0.12);
    if (q <= 0.0) return;
    // Linien: wo das Rauschfeld 0 ist. Die zweite Oktave ist schwach (sonst viele kleine Schleifen statt langer Flüsse),
    // aber nötig: das Rauschen allein ist an jedem Gitterpunkt 0, die Linien gingen sonst durch ein regelmäßiges Gitter.
    // Die Eingabe wird von zwei weiteren Feldern verschoben: Schlingen
    auto field = [&](double px, double pz) {
        const double mx = px / rv.meanderScale, mz = pz / rv.meanderScale;
        const double wx = noise::gradient(riverSeed_[1], mx, mz) * rv.meander, wz = noise::gradient(riverSeed_[2], mx, mz) * rv.meander;
        return noise::fbm(riverSeed_[0], (px + wx) / rv.spacing, (pz + wz) / rv.spacing, 2, 0.3);
    };
    const double e = 0.5;
    const double f0 = field(x, z);
    const glm::dvec2 g((field(x + e, z) - f0) / e, (field(x, z + e) - f0) / e);
    const double gl = glm::length(g);
    if (!(gl > 1e-12)) return;
    out.dist = std::abs(f0) / gl;  // Abstand zur Linie (erste Näherung, gut bis zu einigen hundert Metern)
    const double wv = std::clamp(0.5 + 0.7 * noise::gradient(riverSeed_[4], x / 700.0, z / 700.0), 0.0, 1.0);  // Breite je Abschnitt
    out.strength = q;
    out.half = 0.5 * (rv.widthMin + (rv.widthMax - rv.widthMin) * wv) * (0.4 + 0.6 * q);
    out.depth = (rv.depthMin + (rv.depthMax - rv.depthMin) * wv) * q;
    // Richtung: entlang der Linie, auf ihrer ganzen Länge gleich herum (quer zum Anstieg des Felds); breit = langsamer
    out.flow = glm::dvec2(-g.y, g.x) / gl * ((rv.speedMax + (rv.speedMin - rv.speedMax) * wv) * q);
}

void WorldGen::applyRivers(double x, double z, GenSample& out) const {
    const GenConfig::Rivers& rv = cfg_->rivers;
    if (!rv.enabled) return;
    const double L = rv.level;
    // Außerhalb der Flüsse bleibt das Gelände über dem Wasser (Senken weich angehoben): sonst stünden sie unter Wasser
    const double h = smax(out.height, L + rv.floor, 1.0);
    out.height = h;
    RiverSample& r = out.river;
    river(x, z, riverFactor(out.score), r);
    if (r.strength <= 0.0 || r.dist > r.half + rv.valley) return;
    // Talhänge: höchstens so hoch wie ein Kegel vom Ufer aus (weich), nie unter "floor"; nach außen wieder das alte Gelände
    const double F = L + rv.bank;
    auto slope = [&](double e) { return std::max(smin(h, F + rv.valleySlope * e, 2.0), L + rv.floor); };
    double carved;
    if (r.dist < r.half) {  // Rinne: am Rand das Ufer, in der Mitte die Tiefe unter dem Wasserspiegel
        const double u = r.dist / r.half, edge = slope(0.0);
        carved = edge - (edge - (L - r.depth)) * (1.0 - u * u);
    } else {
        const double e = r.dist - r.half;
        carved = h + (slope(e) - h) * smooth01((rv.valley - e) / (0.5 * rv.valley));
    }
    const double s = smooth01(r.strength / 0.3);
    out.height = h + (carved - h) * s;
    out.wet = r.dist < r.half + 3.0;
    // Flussbett und Ufer in eigener Farbe
    const float bed = (float)(smooth01((r.half + 2.5 - r.dist) / 3.0) * s);
    out.color += (rv.bedColor - out.color) * bed;
    out.slopeColor += (rv.bedColor - out.slopeColor) * bed;
}

double WorldGen::height(double x, double z) const {
    GenSample s;
    sample(x, z, s);
    return s.height;
}
