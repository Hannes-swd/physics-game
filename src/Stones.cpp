// Steine (siehe Stones.h)
#include "Stones.h"

#include <algorithm>
#include <cmath>

#include "Materials.h"
#include "Noise.h"
#include "Profile.h"
#include "Terrain.h"

namespace {

using glm::dvec3;

constexpr int kShapes = 16;
constexpr double kPi = 3.14159265358979323846;

struct Rng {
    uint64_t s;
    double next() {
        s = noise::mix64(s + 0x9E3779B97F4A7C15ull);
        return double(s >> 11) * (1.0 / 9007199254740992.0);
    }
    double range(double a, double b) { return a + (b - a) * next(); }
};

// Kantiger Brocken: konvexe Hülle von Punkten auf einem flachgedrückten Ellipsoid, auf 1 m Größe gebracht
StoneShape makeShape(uint64_t seed) {
    Rng r{seed};
    const dvec3 axes(0.5, 0.5 * r.range(0.45, 0.78), 0.5 * r.range(0.62, 0.95));
    std::vector<dvec3> pts;
    const int n = 22 + (int)(r.next() * 12);
    for (int i = 0; i < n; ++i) {
        const double z = r.range(-1.0, 1.0), a = r.next() * 2.0 * kPi, s = std::sqrt(1.0 - z * z);
        pts.push_back(dvec3(s * std::cos(a), z, s * std::sin(a)) * axes * r.range(0.82, 1.06));
    }
    StoneShape sh;
    sh.solid.pieces.push_back(solid::hullPiece(pts));
    dvec3 mn, mx, com;
    solid::bounds(sh.solid, mn, mx);
    const glm::dvec3 ext = mx - mn;
    const double scale = 1.0 / std::max({ext.x, ext.y, ext.z, 1e-6});
    solid::volume(sh.solid, &com);
    solid::translate(sh.solid, -com);
    solid::transformAffine(sh.solid, glm::dmat3(scale));
    sh.planes = solid::planesOf(sh.solid.pieces[0]);
    sh.bottom = 0;
    for (const solid::Face& f : sh.solid.pieces[0].faces)
        for (const dvec3& v : f.v) {
            sh.bottom = std::min(sh.bottom, v.y);
            sh.radius = std::max(sh.radius, glm::length(v));
        }
    return sh;
}

// Strahl (lokal, Richtung nicht normiert) gegen einen konvexen Körper aus Ebenen: Eintritt t (>= 0)
bool rayConvex(const std::vector<solid::Plane>& planes, const dvec3& o, const dvec3& d, double maxT, double& t) {
    double t0 = 0, t1 = maxT;
    for (const solid::Plane& p : planes) {
        const double denom = glm::dot(p.n, d), dist = p.d - glm::dot(p.n, o);  // innen: dist >= 0
        if (std::abs(denom) < 1e-12) {
            if (dist < 0) return false;
            continue;
        }
        const double s = dist / denom;
        if (denom < 0) t0 = std::max(t0, s);
        else t1 = std::min(t1, s);
        if (t0 > t1) return false;
    }
    t = t0;
    return true;
}

} // namespace

StoneField::StoneField(std::shared_ptr<const WorldGen> gen, const Terrain* terrain, Renderer* renderer)
    : gen_(std::move(gen)), terrain_(terrain), renderer_(renderer) {
    seed_ = noise::subSeed(gen_->seed(), "stones");
    // Biome ohne Angaben (Welten von vor den Steinen): die Werte der eingebauten biomes.json mit gleicher id
    const GenConfig& builtin = builtinGenConfig();
    for (const GenConfig::Biome& b : gen_->config().biomes) {
        GenConfig::Biome::Stones s = b.stones;
        if (!s.given)
            if (const int k = builtin.findBiome(b.id); k >= 0) s = builtin.biomes[(size_t)k].stones;
        int mat = findMaterialIndex(s.material.c_str());
        if (mat < 0) mat = findMaterialIndex("fieldstone");
        if (mat < 0) mat = materialIndex("granite");
        biomes_.push_back(s);
        material_.push_back(mat);
        maxDensity_ = std::max(maxDensity_, s.perHa / 10000.0);
        any_ |= s.perHa > 0;
    }
    if (!any_) return;
    for (int i = 0; i < kShapes; ++i) {
        shapes_.push_back(makeShape(noise::mix64(seed_ + 0x51A0E5ull * (uint64_t)(i + 1))));
        if (renderer_) {
            std::vector<solid::Vertex> v;
            solid::buildTriangles(shapes_.back().solid, v);
            shapes_.back().mesh = renderer_->createMesh(v);
        }
    }
}

StoneField::~StoneField() {
    if (renderer_)
        for (StoneShape& s : shapes_)
            if (s.mesh.valid()) renderer_->destroyMesh(s.mesh);
}

void StoneField::compute(int64_t tx, int64_t tz, Tile& t) const {
    t.spots.clear();
    t.rev = terrain_ ? terrain_->editRevision() : 0;
    if (!any_) return;
    const WorldGen& gen = *gen_;
    const GenConfig& cfg = gen.config();
    const bool rivers = terrain_ && terrain_->hasRivers();
    const double waterLine = terrain_ ? terrain_->riverLevel() + 0.05 : -1e30;
    const uint64_t tileSeed = noise::mix64(seed_ ^ noise::mix64((uint64_t)tx * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)tz * 0xC2B2AE3D27D4EB4Full));
    Rng r{tileSeed};
    const double expect = maxDensity_ * kTile * kTile;
    const int n = std::min(255, (int)expect + (r.next() < expect - std::floor(expect) ? 1 : 0));
    for (int k = 0; k < n; ++k) {
        Rng q{noise::mix64(tileSeed + 0x5707E5ull * (uint64_t)(k + 1))};  // eigene Zahlen je Stein: Schlüssel bleiben gleich
        const double x = (tx + q.next()) * kTile, z = (tz + q.next()) * kTile;
        const double accept = q.next(), pick = q.next(), sizeR = q.next(), yaw = q.next() * 2.0 * kPi;
        const double tiltAxis = q.next() * 2.0 * kPi, tilt = q.range(-0.35, 0.35);
        const int shape = std::min((int)(q.next() * kShapes), kShapes - 1);
        GenSample g;
        gen.sample(x, z, g);
        if (g.wet) continue;  // Flussbett
        // Dichte aus den Biomen: an Grenzen weich (wie Höhe und Farbe); welches Biom den Stein bestimmt, nach Gewicht
        double w[kMaxBiomes] = {}, sum = 0, density = 0;
        if (gen.singleBiome() >= 0) {
            w[gen.singleBiome()] = sum = 1;
        } else {
            for (size_t b = 0; b < cfg.biomes.size(); ++b) {
                double v = std::clamp(0.5 + g.score[b] / (2.0 * cfg.blend), 0.0, 1.0);
                w[b] = v * v * (3.0 - 2.0 * v);
                sum += w[b];
            }
            if (sum < 1e-9) w[g.biome] = sum = 1;
        }
        for (size_t b = 0; b < biomes_.size(); ++b) density += w[b] / sum * biomes_[b].perHa / 10000.0;
        if (accept * maxDensity_ >= density || density <= 0) continue;
        // Biom, das Größe und Material bestimmt: nach seinem Anteil an der Dichte hier
        double p = pick * density * sum * 10000.0;
        size_t bi = 0;
        for (size_t b = 0; b < biomes_.size(); ++b) {
            const double share = w[b] * biomes_[b].perHa;
            if (share <= 0) continue;
            bi = b;
            if (p < share) break;
            p -= share;
        }
        const GenConfig::Biome::Stones& st = biomes_[bi];
        // viele kleine, wenige große
        const double size = st.sizeMin * std::pow(st.sizeMax / st.sizeMin, std::pow(sizeR, st.skew));
        const double ground = terrain_ ? terrain_->surfaceHeight(x, z) : g.height;
        if (rivers && ground < waterLine) continue;  // unter Wasser (z.B. ausgehoben)
        StoneSpot s;
        s.key = (1ull << 62) | ((uint64_t)(tx & 0xFFFFFF) << 32) | ((uint64_t)(tz & 0xFFFFFF) << 8) | (uint64_t)k;
        s.size = size;
        s.shape = shape;
        s.material = material_[bi];
        s.rot = glm::angleAxis(yaw, dvec3(0, 1, 0)) * glm::angleAxis(tilt, dvec3(std::cos(tiltAxis), 0, std::sin(tiltAxis)));
        // etwas eingesunken: ein gutes Drittel der unteren Hälfte steckt im Boden
        s.pos = dvec3(x, ground - shapes_[(size_t)shape].bottom * size * 0.62, z);
        t.spots.push_back(s);
    }
}

const std::vector<StoneSpot>& StoneField::tile(int64_t tx, int64_t tz, bool fresh) {
    auto it = tiles_.find(tileKey(tx, tz));
    if (it == tiles_.end()) {
        it = tiles_.emplace(tileKey(tx, tz), Tile{}).first;
        compute(tx, tz, it->second);
    } else if (fresh && terrain_ && it->second.rev != terrain_->editRevision()) {
        compute(tx, tz, it->second);
    }
    return it->second.spots;
}

void StoneField::around(const glm::dvec2& c, double radius, std::vector<StoneSpot>& out) {
    out.clear();
    if (!any_) return;
    const int64_t x0 = (int64_t)std::floor((c.x - radius) / kTile), x1 = (int64_t)std::floor((c.x + radius) / kTile);
    const int64_t z0 = (int64_t)std::floor((c.y - radius) / kTile), z1 = (int64_t)std::floor((c.y + radius) / kTile);
    for (int64_t tz = z0; tz <= z1; ++tz)
        for (int64_t tx = x0; tx <= x1; ++tx)
            for (const StoneSpot& s : tile(tx, tz, true))
                if (glm::length(glm::dvec2(s.pos.x, s.pos.z) - c) <= radius) out.push_back(s);
}

void StoneField::prepare(const glm::dvec2& c, double radius, int budget) {
    if (!any_) return;
    PROFILE("stones.prepare");
    const uint64_t rev = terrain_ ? terrain_->editRevision() : 0;
    const int64_t x0 = (int64_t)std::floor((c.x - radius) / kTile), x1 = (int64_t)std::floor((c.x + radius) / kTile);
    const int64_t z0 = (int64_t)std::floor((c.y - radius) / kTile), z1 = (int64_t)std::floor((c.y + radius) / kTile);
    std::vector<std::pair<double, std::pair<int64_t, int64_t>>> want;
    for (int64_t tz = z0; tz <= z1; ++tz)
        for (int64_t tx = x0; tx <= x1; ++tx) {
            const double d = glm::length(glm::dvec2((tx + 0.5) * kTile, (tz + 0.5) * kTile) - c);
            if (d > radius + kTile) continue;
            auto it = tiles_.find(tileKey(tx, tz));
            if (it == tiles_.end() || it->second.rev != rev) want.push_back({d, {tx, tz}});
        }
    std::sort(want.begin(), want.end());
    for (size_t i = 0; i < want.size() && (int)i < budget; ++i) compute(want[i].second.first, want[i].second.second, tiles_[tileKey(want[i].second.first, want[i].second.second)]);
    // weit weg: vergessen (entsteht bei Bedarf neu)
    if (tiles_.size() > 4096)
        for (auto it = tiles_.begin(); it != tiles_.end();) {
            const int64_t tx = (int32_t)(it->first >> 32), tz = (int32_t)(it->first & 0xffffffffu);
            if (glm::length(glm::dvec2((tx + 0.5) * kTile, (tz + 0.5) * kTile) - c) > radius + 4 * kTile) it = tiles_.erase(it);
            else ++it;
        }
}

bool StoneField::raycast(const glm::dvec3& o, const glm::dvec3& dir, double maxT, const std::function<bool(uint64_t)>& skip, StoneSpot& hit,
                         double& t) {
    if (!any_) return false;
    bool found = false;
    t = maxT;
    const glm::dvec3 e = o + dir * maxT;
    const int64_t x0 = (int64_t)std::floor((std::min(o.x, e.x) - 3.0) / kTile), x1 = (int64_t)std::floor((std::max(o.x, e.x) + 3.0) / kTile);
    const int64_t z0 = (int64_t)std::floor((std::min(o.z, e.z) - 3.0) / kTile), z1 = (int64_t)std::floor((std::max(o.z, e.z) + 3.0) / kTile);
    for (int64_t tz = z0; tz <= z1; ++tz)
        for (int64_t tx = x0; tx <= x1; ++tx)
            for (const StoneSpot& s : tile(tx, tz, true)) {
                const StoneShape& sh = shapes_[(size_t)s.shape];
                // erst die umhüllende Kugel, dann die Form (im Raum des Steins, Größe 1)
                const glm::dvec3 rel = s.pos - o;
                const double along = glm::dot(rel, dir), r = sh.radius * s.size;
                if (along < -r || along > t + r || glm::length(rel - dir * along) > r) continue;
                if (skip && skip(s.key)) continue;
                const glm::dquat inv = glm::conjugate(s.rot);
                double ts;
                if (rayConvex(sh.planes, inv * (o - s.pos) / s.size, inv * dir / s.size, t, ts) && ts < t) {
                    t = ts;
                    hit = s;
                    found = true;
                }
            }
    return found;
}
