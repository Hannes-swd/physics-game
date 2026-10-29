// Bewuchs: Gras, Blumen, Büsche (siehe Flora.h)
#include "Flora.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

#include "Noise.h"
#include "Profile.h"
#include "Terrain.h"

namespace {

using glm::dvec3;
using glm::vec3;

constexpr int kGrid = 5;                                   // Biomwerte je Kachel: 5 × 5 Punkte, 4 m Abstand
constexpr double kGridStep = Flora::kTile / (kGrid - 1);
constexpr int kBladesPerTuft = 3;
constexpr uint32_t kTuftVerts = kBladesPerTuft * 6;       // je Halm ein Dreieck, beide Seiten
constexpr int kMaxTufts = 9000, kMaxFlowers = 1500;        // je Kachel
constexpr double kPatchBoost = 3.5;                        // so viel dichter ist Gras in einem Fleck
// Ferne Stufe des Grases (außerhalb von grass_distance): so viele Büschel wie nah (Anteil), so viel breitere Halme,
// bis zum Wievielfachen von grass_distance
constexpr double kFarDensity = 0.16, kFarWide = 3.0, kFarReach = 3.0;
constexpr int kMaxFarTufts = 2500;

struct Rng {
    uint64_t s;
    double next() {
        s = noise::mix64(s + 0x9E3779B97F4A7C15ull);
        return double(s >> 11) * (1.0 / 9007199254740992.0);
    }
    double range(double a, double b) { return a + (b - a) * next(); }
};

double smooth(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// RGBA8, r im untersten Byte. a = Höhe der Spitze über dem Boden / 2 m (Wind, Ausblenden in mesh.vert), 0: steht fest
uint32_t pack(vec3 c, double height = 0) {
    auto b = [](double v) { return (uint32_t)std::lround(std::clamp(v, 0.0, 1.0) * 255.0); };
    return b(c.r) | (b(c.g) << 8) | (b(c.b) << 16) | (b(height / 2.0) << 24);
}

void put(std::vector<solid::Vertex>& out, const vec3& p, const vec3& n, uint32_t color) {
    solid::Vertex v;
    v.px = p.x, v.py = p.y, v.pz = p.z;
    v.nx = n.x, v.ny = n.y, v.nz = n.z;
    v.color = color;
    out.push_back(v);
}

// Dreieck von beiden Seiten (Halme, Blüten), gleiche Normale: beide Seiten so hell wie der Boden darunter
void twoSided(std::vector<solid::Vertex>& out, const vec3& a, const vec3& b, const vec3& c, const vec3& n, uint32_t ca, uint32_t cb, uint32_t cc) {
    put(out, a, n, ca), put(out, b, n, cb), put(out, c, n, cc);
    put(out, b, n, cb), put(out, a, n, ca), put(out, c, n, cc);
}

// Dreieck, dessen Vorderseite nach außen zeigt
void outward(std::vector<solid::Vertex>& out, const vec3& center, const vec3* p, const vec3* n, const uint32_t* c, int i0, int i1, int i2) {
    const vec3 face = glm::cross(p[i1] - p[i0], p[i2] - p[i0]);
    if (glm::dot(face, (p[i0] + p[i1] + p[i2]) / 3.0f - center) < 0) std::swap(i1, i2);
    for (int i : {i0, i1, i2}) put(out, p[i], n[i], c[i]);
}

vec3 pickColor(const std::vector<vec3>& list, Rng& r) {
    if (list.empty()) return vec3(0.3f, 0.4f, 0.15f);
    const vec3 a = list[(size_t)(r.next() * list.size()) % list.size()];
    const vec3 b = list[(size_t)(r.next() * list.size()) % list.size()];
    return glm::mix(a, b, (float)r.next());
}

} // namespace

void Flora::clear() {
    for (auto& [k, t] : tiles_)
        for (GpuMesh* m : {&t.grass, &t.flowers, &t.bushes, &t.farGrass})
            if (m->valid()) renderer_->destroyMesh(*m);
    tiles_.clear();
    drawnTufts_ = 0;
}

void Flora::setQuality(double plants, double view) {
    if (std::abs(plants - plants_) < 1e-6 && std::abs(view - view_) < 1e-6) return;
    plants_ = std::max(plants, 0.0), view_ = std::max(view, 0.1);
    clear();  // neu bauen
}

double Flora::grassDistance() const {
    if (!gen_ || plants_ <= 0) return 0;
    return gen_->config().flora.grassDistance * std::clamp(0.5 + 0.5 * plants_, 0.5, 1.25);
}

double Flora::farDistance() const { return grassDistance() * kFarReach; }

double Flora::bushDistance() const { return gen_ ? gen_->config().flora.bushDistance * view_ : 0; }

void Flora::reset(const Terrain& terrain) {
    clear();
    terrain_ = &terrain;
    gen_ = terrain.genPtr();
    seed_ = noise::subSeed(gen_->seed(), "flora");
    // Biome ohne Angaben (Welten von vor dem Bewuchs): die Werte der eingebauten biomes.json mit gleicher id
    biomes_.clear();
    const GenConfig& builtin = builtinGenConfig();
    for (const GenConfig::Biome& b : gen_->config().biomes) {
        GenConfig::Biome::Flora f = b.flora;
        if (!f.given) {
            const int k = builtin.findBiome(b.id);
            if (k >= 0) f = builtin.biomes[(size_t)k].flora;
        }
        biomes_.push_back(f);
    }
}

void Flora::sampleGrid(int64_t tx, int64_t tz, std::vector<Mix>& grid) const {
    const WorldGen& gen = *gen_;
    const GenConfig& cfg = gen.config();
    grid.assign(kGrid * kGrid, Mix{});
    for (int j = 0; j < kGrid; ++j)
        for (int i = 0; i < kGrid; ++i) {
            const double x = tx * kTile + i * kGridStep, z = tz * kTile + j * kGridStep;
            GenSample g;
            gen.sample(x, z, g);
            Mix& m = grid[(size_t)(i + kGrid * j)];
            // Gewichte wie bei den Bäumen: an Grenzen weich
            double sum = 0;
            if (gen.singleBiome() >= 0) {
                m.w[gen.singleBiome()] = sum = 1;
            } else {
                for (size_t b = 0; b < cfg.biomes.size(); ++b) {
                    const double w = smooth(0.0, 1.0, 0.5 + g.score[b] / (2.0 * cfg.blend));
                    m.w[b] = w;
                    sum += w;
                }
                if (sum < 1e-9) m.w[g.biome] = sum = 1;
            }
            m.hMin = m.hMax = m.bMin = m.bMax = 0;
            for (size_t b = 0; b < biomes_.size(); ++b) {
                const double w = (m.w[b] /= sum);
                const GenConfig::Biome::Flora& f = biomes_[b];
                m.grass += w * f.grass, m.patches += w * f.grassPatches, m.flowers += w * f.flowers;
                m.hMin += w * f.grassHeightMin, m.hMax += w * f.grassHeightMax;
                m.bushes += w * f.bushesPerHa / 10000.0, m.bMin += w * f.bushSizeMin, m.bMax += w * f.bushSizeMax;
            }
            m.grass *= plants_, m.flowers *= plants_;
            if (g.wet) m.grass = m.flowers = m.bushes = 0;  // Flussbett
        }
}

void Flora::build(const Terrain& terrain, Tile& t, bool withGrass, bool withFar) {
    PROFILE("flora.build");
    std::vector<Mix> grid;
    sampleGrid(t.tx, t.tz, grid);
    const double ox = t.tx * kTile, oz = t.tz * kTile;
    const bool rivers = terrain.hasRivers();
    const double dryAbove = terrain.riverLevel() + 0.12;
    // Werte an einer Stelle der Kachel (lx, lz in 0..16): Zahlen bilinear, Gewichte vom nächsten Punkt
    auto mixAt = [&](double lx, double lz) {
        const double gx = std::clamp(lx / kGridStep, 0.0, kGrid - 1.001), gz = std::clamp(lz / kGridStep, 0.0, kGrid - 1.001);
        const int i = (int)gx, j = (int)gz;
        const double fx = gx - i, fz = gz - j;
        const Mix& a = grid[(size_t)(i + kGrid * j)];
        const Mix& b = grid[(size_t)(i + 1 + kGrid * j)];
        const Mix& c = grid[(size_t)(i + kGrid * (j + 1))];
        const Mix& d = grid[(size_t)(i + 1 + kGrid * (j + 1))];
        auto L = [&](double Mix::*f) { return (a.*f * (1 - fx) + b.*f * fx) * (1 - fz) + (c.*f * (1 - fx) + d.*f * fx) * fz; };
        Mix m = grid[(size_t)(std::lround(gx) + kGrid * std::lround(gz))];
        m.grass = L(&Mix::grass), m.hMin = L(&Mix::hMin), m.hMax = L(&Mix::hMax), m.patches = L(&Mix::patches);
        m.flowers = L(&Mix::flowers), m.bushes = L(&Mix::bushes), m.bMin = L(&Mix::bMin), m.bMax = L(&Mix::bMax);
        return m;
    };
    auto pickBiome = [&](const Mix& m, Rng& r) {
        double p = r.next();
        for (size_t b = 0; b < biomes_.size(); ++b) {
            if (p < m.w[b]) return b;
            p -= m.w[b];
        }
        return biomes_.size() - 1;
    };
    // Boden an einer Stelle; false: zu steil oder unter Wasser
    auto ground = [&](double x, double z, double steepRoll, double& y, vec3& n) {
        dvec3 nd(0, 1, 0);
        y = terrain.surfaceHeight(x, z, &nd);
        n = vec3(nd);
        if (rivers && y < dryAbove) return false;
        return steepRoll < smooth(0.68, 0.85, nd.y);
    };
    double maxGrass = 0, maxFlowers = 0, maxBushes = 0;
    for (const Mix& m : grid) {
        maxGrass = std::max(maxGrass, m.grass * (1.0 + (kPatchBoost - 1.0) * m.patches));
        maxFlowers = std::max(maxFlowers, m.flowers * 3.0);
        maxBushes = std::max(maxBushes, m.bushes);
    }
    const uint64_t tileSeed = noise::mix64(seed_ ^ noise::mix64((uint64_t)t.tx * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)t.tz * 0xC2B2AE3D27D4EB4Full));
    const uint64_t patchSeed = noise::subSeed(seed_, "patches"), bloomSeed = noise::subSeed(seed_, "flowers");
    double lo = 1e30, hi = -1e30;
    const vec3 org((float)ox, 0.0f, (float)oz);

    for (GpuMesh* m : {&t.grass, &t.flowers, &t.bushes, &t.farGrass})
        if (m->valid()) renderer_->destroyMesh(*m);

    // Grasbüschel aus drei Halmen, in zufälliger Reihenfolge (zum Ausdünnen in der Ferne). density: Anteil der Büschel,
    // wide: Halme so viel breiter (ferne Stufe: wenige breite Büschel decken wie viele schmale)
    auto tufts = [&](uint64_t salt, double density, double wide, int maxTufts, std::vector<solid::Vertex>& v) {
        Rng r{tileSeed ^ salt};
        const double maxD = maxGrass * density;
        const int n = std::min(maxTufts, (int)(maxD * kTile * kTile));
        v.reserve((size_t)n * kTuftVerts);
        for (int k = 0; k < n; ++k) {
            const double lx = r.next() * kTile, lz = r.next() * kTile, accept = r.next(), steep = r.next();
            Rng q{r.s ^ 0xA5A5A5A5ull};  // eigene Zahlen je Büschel: abgelehnte verschieben die anderen nicht
            const Mix m = mixAt(lx, lz);
            if (m.grass <= 0) continue;
            const double x = ox + lx, z = oz + lz;
            const double pn = noise::fbm(patchSeed, x / 9.0, z / 9.0, 2);
            const double patch = 1.0 + (smooth(0.05, 0.35, pn) * kPatchBoost - 1.0) * m.patches;
            if (accept * maxD >= m.grass * density * patch) continue;
            double y;
            vec3 nt;
            if (!ground(x, z, steep, y, nt)) continue;
            const GenConfig::Biome::Flora& f = biomes_[pickBiome(m, q)];
            const vec3 col = pickColor(f.grassColors, q) * (float)q.range(0.85, 1.12);
            const double h = q.range(m.hMin, m.hMax) * (0.75 + 0.25 * std::min(patch, 1.5));
            const vec3 base = vec3((float)x, (float)(y - 0.03 * wide), (float)z) - org;  // fern: Gelände gröber, tiefer setzen
            for (int b = 0; b < kBladesPerTuft; ++b) {
                const double a = q.next() * 6.2832, lean = q.range(0.1, 0.4) * h, hb = h * q.range(0.7, 1.0);
                const vec3 dir((float)std::cos(a), 0.0f, (float)std::sin(a));
                const vec3 side = vec3(-dir.z, 0.0f, dir.x) * (float)((0.012 + 0.03 * h) * wide);
                const vec3 foot = base + dir * (float)(q.range(0.0, 0.06) * wide);
                const vec3 tip = foot + dir * (float)lean + vec3(0.0f, (float)hb, 0.0f);
                const vec3 nb = glm::normalize(nt + dir * 0.35f);
                twoSided(v, foot - side, foot + side, tip, nb, pack(col * 0.55f, 0.01), pack(col * 0.55f, 0.01), pack(col * 1.05f, hb));
            }
            lo = std::min(lo, y - 0.05 * wide), hi = std::max(hi, y + h);
        }
    };

    if (withFar) {  // ---- ferne Stufe: wenige, breite Büschel (nur ganze Büschel, wie unten)
        std::vector<solid::Vertex> v;
        tufts(0x6661726772ull, kFarDensity, kFarWide, kMaxFarTufts, v);
        t.farGrass = v.empty() ? GpuMesh{} : renderer_->createMesh(v);
    } else {
        t.farGrass = GpuMesh{};
    }
    t.hasFar = withFar;

    if (withGrass) {
        // ---- Gras (nur ganze Büschel im Netz: beim Ausdünnen wird an Büschelgrenzen abgeschnitten)
        std::vector<solid::Vertex> v;
        tufts(0x6772617373ull, 1.0, 1.0, kMaxTufts, v);
        t.grass = v.empty() ? GpuMesh{} : renderer_->createMesh(v);

        // ---- Blumen: in Gruppen, Stängel und eine Blüte aus fünf Blättern
        v.clear();
        Rng rf{tileSeed ^ 0x666c6f776572ull};
        const int nf = std::min(kMaxFlowers, (int)std::ceil(maxFlowers * kTile * kTile));
        for (int k = 0; k < nf; ++k) {
            const double lx = rf.next() * kTile, lz = rf.next() * kTile, accept = rf.next(), steep = rf.next();
            Rng q{rf.s ^ 0x5A5A5A5Aull};
            const Mix m = mixAt(lx, lz);
            if (m.flowers <= 0) continue;
            const double x = ox + lx, z = oz + lz;
            const double group = smooth(0.0, 0.4, noise::fbm(bloomSeed, x / 5.0, z / 5.0, 2)) * 3.0;
            if (accept * maxFlowers >= m.flowers * group) continue;
            double y;
            vec3 nt;
            if (!ground(x, z, steep, y, nt)) continue;
            const GenConfig::Biome::Flora& f = biomes_[pickBiome(m, q)];
            if (f.flowers <= 0) continue;
            const vec3 petal = f.flowerColors[(size_t)(q.next() * f.flowerColors.size()) % f.flowerColors.size()] * (float)q.range(0.88, 1.08);
            const double hs = std::clamp(m.hMax * q.range(0.8, 1.25), 0.12, 0.6);
            const vec3 foot = vec3((float)x, (float)y - 0.02f, (float)z) - org;
            const double a0 = q.next() * 6.2832;
            const vec3 lean((float)(std::cos(a0) * 0.05), 0.0f, (float)(std::sin(a0) * 0.05));
            const vec3 top = foot + lean + vec3(0.0f, (float)hs, 0.0f);
            const vec3 side((float)(-std::sin(a0) * 0.007), 0.0f, (float)(std::cos(a0) * 0.007));
            const vec3 stem(0.18f, 0.30f, 0.09f);
            twoSided(v, foot - side, foot + side, top, nt, pack(stem * 0.6f, 0.01), pack(stem * 0.6f, 0.01), pack(stem, hs));
            const double rr = q.range(0.03, 0.055);
            const vec3 up(0, 1, 0), mid = top + vec3(0.0f, 0.008f, 0.0f);
            const uint32_t cm = pack(petal * 0.75f + vec3(0.15f, 0.12f, 0.0f), hs), cp = pack(petal, hs);
            for (int p = 0; p < 5; ++p) {
                const double a = a0 + p * 1.2566, b = a + 0.9;
                const vec3 p1 = top + vec3((float)(std::cos(a) * rr), 0.0f, (float)(std::sin(a) * rr));
                const vec3 p2 = top + vec3((float)(std::cos(b) * rr), 0.0f, (float)(std::sin(b) * rr));
                twoSided(v, mid, p1, p2, up, cm, cp, cp);
            }
            hi = std::max(hi, y + hs);
        }
        t.flowers = v.empty() ? GpuMesh{} : renderer_->createMesh(v);
    } else {
        t.grass = t.flowers = GpuMesh{};
    }
    t.hasGrass = withGrass;

    // ---- Büsche: zwei bis vier unregelmäßige Kugeln, unten dunkler
    std::vector<solid::Vertex> v;
    Rng rb{tileSeed ^ 0x6275736865ull};
    const double expect = maxBushes * kTile * kTile;
    const int nb = (int)expect + (rb.next() < expect - std::floor(expect) ? 1 : 0);
    for (int k = 0; k < nb; ++k) {
        const double lx = rb.next() * kTile, lz = rb.next() * kTile, accept = rb.next(), steep = rb.next();
        Rng q{rb.s ^ 0x3C3C3C3Cull};
        const Mix m = mixAt(lx, lz);
        if (accept * maxBushes >= m.bushes) continue;
        const double x = ox + lx, z = oz + lz;
        double y;
        vec3 nt;
        if (!ground(x, z, steep, y, nt)) continue;
        const GenConfig::Biome::Flora& f = biomes_[pickBiome(m, q)];
        const double size = q.range(m.bMin, m.bMax);
        const vec3 col = pickColor(f.bushColors, q);
        const int blobs = 3 + (int)(q.next() * 3);
        for (int bl = 0; bl < blobs; ++bl) {
            const double a = q.next() * 6.2832, off = bl == 0 ? 0.0 : q.range(0.15, 0.32) * size;
            const double rad = size * 0.5 * (bl == 0 ? q.range(0.7, 0.85) : q.range(0.5, 0.7));
            const vec3 c = vec3((float)(x + std::cos(a) * off), (float)(y + rad * 0.55 - 0.05 * size), (float)(z + std::sin(a) * off)) - org;
            const float shade = (float)q.range(0.88, 1.12);
            constexpr int kSeg = 7, kRing = 5;
            vec3 P[(kRing + 1) * kSeg], N[(kRing + 1) * kSeg];
            uint32_t C[(kRing + 1) * kSeg];
            for (int rI = 0; rI <= kRing; ++rI)
                for (int s = 0; s < kSeg; ++s) {
                    const double th = 3.14159 * rI / kRing, ph = 6.2832 * (s + 0.5 * (rI & 1)) / kSeg;
                    const vec3 d((float)(std::sin(th) * std::cos(ph)), (float)std::cos(th), (float)(std::sin(th) * std::sin(ph)));
                    const int idx = rI * kSeg + s;
                    const float jitter = (float)q.range(0.75, 1.25);
                    P[idx] = c + vec3(d.x, d.y * 0.8f, d.z) * (float)rad * jitter;
                    N[idx] = d;
                    C[idx] = pack(col * shade * (0.6f + 0.55f * (d.y * 0.5f + 0.5f)));
                    lo = std::min(lo, (double)P[idx].y), hi = std::max(hi, (double)P[idx].y);
                }
            for (int rI = 0; rI < kRing; ++rI)
                for (int s = 0; s < kSeg; ++s) {
                    const int a0 = rI * kSeg + s, a1 = rI * kSeg + (s + 1) % kSeg;
                    const int b0 = (rI + 1) * kSeg + s, b1 = (rI + 1) * kSeg + (s + 1) % kSeg;
                    if (rI > 0) outward(v, c, P, N, C, a0, a1, b0);
                    if (rI < kRing - 1) outward(v, c, P, N, C, a1, b1, b0);
                }
        }
    }
    t.bushes = v.empty() ? GpuMesh{} : renderer_->createMesh(v);
    if (lo > hi) lo = hi = terrain.surfaceHeight(ox + kTile * 0.5, oz + kTile * 0.5);
    t.lo = lo, t.hi = hi;
    t.rev = terrain.editRevision();
}

void Flora::update(const Terrain& terrain, const glm::dvec3& eye, int budget) {
    if (&terrain != terrain_ || terrain.genPtr() != gen_) reset(terrain);
    const double grassR = grassDistance(), farR = farDistance(), R = std::max(bushDistance(), farR);
    const glm::dvec2 e(eye.x, eye.z);
    auto dist = [&](int64_t tx, int64_t tz) { return glm::length(glm::dvec2((tx + 0.5) * kTile, (tz + 0.5) * kTile) - e); };
    auto drop = [&](GpuMesh& m) {
        if (m.valid()) renderer_->destroyMesh(m);
        m = GpuMesh{};
    };

    // ferne Kacheln verwerfen, bei den übrigen, was zu weit weg ist (nahes Gras, ferne Stufe)
    for (auto it = tiles_.begin(); it != tiles_.end();) {
        Tile& t = it->second;
        const double d = dist(t.tx, t.tz);
        if (d > R + kTile * 1.5) {
            for (GpuMesh* m : {&t.grass, &t.flowers, &t.bushes, &t.farGrass}) drop(*m);
            it = tiles_.erase(it);
            continue;
        }
        if (t.hasGrass && d > grassR + kTile * 1.5) {
            drop(t.grass), drop(t.flowers);
            t.hasGrass = false;
        }
        if (t.hasFar && d > farR + kTile * 1.5) {
            drop(t.farGrass);
            t.hasFar = false;
        }
        ++it;
    }
    if (R <= 0) return;

    // fehlende (oder nach Geländeänderungen veraltete) Kacheln: zuerst nahes Gras, dann die ferne Stufe, dann der Rest,
    // jeweils die nächsten zuerst
    const uint64_t rev = terrain.editRevision();
    std::vector<std::pair<double, uint64_t>> want;
    const int64_t r = (int64_t)std::ceil(R / kTile) + 1;
    const int64_t cx = (int64_t)std::floor(e.x / kTile), cz = (int64_t)std::floor(e.y / kTile);
    for (int64_t tz = cz - r; tz <= cz + r; ++tz)
        for (int64_t tx = cx - r; tx <= cx + r; ++tx) {
            const double d = dist(tx, tz);
            if (d > R + kTile * 0.75) continue;
            const bool grass = d < grassR + kTile * 0.75, far = d < farR + kTile * 0.75;
            const double prio = d + (grass ? 0.0 : far ? 500.0 : 1000.0);
            auto it = tiles_.find(key(tx, tz));
            if (it == tiles_.end() || (grass && !it->second.hasGrass) || (far && !it->second.hasFar) || it->second.rev != rev)
                want.emplace_back(prio, key(tx, tz));
        }
    std::sort(want.begin(), want.end());
    for (size_t i = 0; i < want.size() && (int)i < budget; ++i) {
        const uint64_t k = want[i].second;
        Tile& t = tiles_[k];
        t.tx = (int32_t)(k >> 32), t.tz = (int32_t)(k & 0xffffffffu);
        const double d = dist(t.tx, t.tz);
        build(terrain, t, d < grassR + kTile * 0.75, d < farR + kTile * 0.75);
    }
}

void Flora::draw(FrameDesc& fd, const glm::dvec3& origin, const glm::dvec3& eye,
                 const std::function<bool(const glm::dvec3&, const glm::dvec3&)>& visible) {
    drawnTufts_ = 0;
    if (!gen_) return;
    const double grassR = grassDistance(), farR = farDistance(), bushR = bushDistance();
    const float farInner = (float)(grassR * 0.85);  // hier ist die ferne Stufe ganz da, das nahe Gras sinkt schon ein
    const glm::dvec2 e(eye.x, eye.z);
    for (auto& [k, t] : tiles_) {
        if (fd.items.size() + 4 >= kMaxDrawItems) return;
        const double d = glm::length(glm::dvec2((t.tx + 0.5) * kTile, (t.tz + 0.5) * kTile) - e);
        const double pad = 2.0;  // Büsche ragen über die Kachel
        if (!visible(dvec3(t.tx * kTile - pad, t.lo, t.tz * kTile - pad), dvec3((t.tx + 1) * kTile + pad, t.hi + 0.5, (t.tz + 1) * kTile + pad))) continue;
        DrawItem it;
        it.data.model = glm::translate(glm::mat4(1.0f), vec3((float)(t.tx * kTile - origin.x), (float)(-origin.y), (float)(t.tz * kTile - origin.z)));
        it.data.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.8f);
        it.data.color2 = glm::vec4(0.0f);
        it.data.params = glm::vec4(13, 0, 0, 0);
        if (t.bushes.valid() && d < bushR + kTile) {
            it.mesh = &t.bushes;
            it.castShadow = d < 70;
            fd.items.push_back(it);
        }
        it.castShadow = false;
        // ferne Stufe: wächst ab farInner aus dem Boden (mesh.vert, heat0.x), sinkt am Ende von farR wieder ein
        if (t.hasFar && t.farGrass.valid() && d < farR + kTile && d + kTile * 0.71 > farInner * 0.8) {
            const double frac = std::clamp(1.0 - std::max(d - grassR, 0.0) / std::max(farR - grassR, 1.0) * 0.65, 0.35, 1.0);
            const uint32_t tufts = t.farGrass.vertexCount / kTuftVerts;
            t.farPart = t.farGrass;
            t.farPart.vertexCount = (uint32_t)(tufts * frac) * kTuftVerts;
            if (t.farPart.vertexCount > 0) {
                DrawItem far = it;
                far.mesh = &t.farPart;
                far.data.params.w = (float)farR;
                far.data.heat0.x = farInner;
                fd.items.push_back(far);
                drawnTufts_ += t.farPart.vertexCount / kTuftVerts;
            }
        }
        if (!t.hasGrass || d > grassR + kTile) continue;
        it.data.params.w = (float)grassR;  // am Rand in den Boden senken (mesh.vert)
        if (t.grass.valid()) {
            // nach außen ausdünnen: nur ein Anfangsstück der (zufällig geordneten) Büschel
            const double frac = std::clamp(1.0 - std::max(d - 14.0, 0.0) / std::max(grassR, 1.0) * 0.7, 0.3, 1.0);
            const uint32_t tufts = t.grass.vertexCount / kTuftVerts;
            t.grassPart = t.grass;
            t.grassPart.vertexCount = (uint32_t)(tufts * frac) * kTuftVerts;
            if (t.grassPart.vertexCount > 0) {
                it.mesh = &t.grassPart;
                fd.items.push_back(it);
                drawnTufts_ += t.grassPart.vertexCount / kTuftVerts;
            }
        }
        if (t.flowers.valid()) {
            it.mesh = &t.flowers;
            fd.items.push_back(it);
        }
    }
}
