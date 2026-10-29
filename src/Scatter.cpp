// Zeug am Boden: Steine, Stöcke, umgefallene Stämme, Baumstümpfe (siehe Scatter.h)
#include "Scatter.h"

#include <algorithm>
#include <cmath>

#include "Materials.h"
#include "Noise.h"
#include "Profile.h"
#include "Terrain.h"
#include "Trees.h"

namespace {

using glm::dvec3;

constexpr double kPi = 3.14159265358979323846;
constexpr int kVariants[kScatterKinds] = {16, 12, 10, 8};  // Vorlagen je Art
// Schlankheit der Vorlagen (Stöcke, Stämme: Dicke / Länge; Stümpfe: Höhe / Breite), von .. bis
constexpr double kAspectLo[kScatterKinds] = {0, 0.012, 0.03, 0.35}, kAspectHi[kScatterKinds] = {0, 0.07, 0.16, 1.1};

struct Rng {
    uint64_t s;
    double next() {
        s = noise::mix64(s + 0x9E3779B97F4A7C15ull);
        return double(s >> 11) * (1.0 / 9007199254740992.0);
    }
    double range(double a, double b) { return a + (b - a) * next(); }
};

// eine Richtung senkrecht zu a, um a gedreht um den Winkel ang
dvec3 across(const dvec3& a, double ang) {
    const dvec3 t = std::abs(a.y) < 0.9 ? dvec3(0, 1, 0) : dvec3(1, 0, 0);
    const dvec3 u = glm::normalize(glm::cross(a, t)), v = glm::cross(a, u);
    return u * std::cos(ang) + v * std::sin(ang);
}

// Mittelpunkt auf den Schwerpunkt; Ebenen, tiefster und weitester Punkt
void finish(ScatterShape& sh) {
    dvec3 com;
    solid::volume(sh.solid, &com);
    solid::translate(sh.solid, -com);
    for (const solid::Piece& p : sh.solid.pieces) {
        sh.planes.push_back(solid::planesOf(p));
        for (const solid::Face& f : p.faces)
            for (const dvec3& v : f.v) {
                sh.bottom = std::min(sh.bottom, v.y);
                sh.radius = std::max(sh.radius, glm::length(v));
            }
    }
}

// Stein: konvexe Hülle von Punkten auf einem flachgedrückten Ellipsoid, auf 1 m Größe gebracht
ScatterShape stoneShape(Rng& r) {
    const dvec3 axes(0.5, 0.5 * r.range(0.45, 0.78), 0.5 * r.range(0.62, 0.95));
    std::vector<dvec3> pts;
    const int n = 22 + (int)(r.next() * 12);
    for (int i = 0; i < n; ++i) {
        const double z = r.range(-1.0, 1.0), a = r.next() * 2.0 * kPi, s = std::sqrt(1.0 - z * z);
        pts.push_back(dvec3(s * std::cos(a), z, s * std::sin(a)) * axes * r.range(0.82, 1.06));
    }
    ScatterShape sh;
    sh.solid.pieces.push_back(solid::hullPiece(pts));
    dvec3 mn, mx;
    solid::bounds(sh.solid, mn, mx);
    const dvec3 ext = mx - mn;
    solid::transformAffine(sh.solid, glm::dmat3(1.0 / std::max({ext.x, ext.y, ext.z, 1e-6})));
    finish(sh);
    return sh;
}

// Stock: 1 m lang entlang Y, leicht gebogen (zwei Stücke), manchmal mit einem Zweig
ScatterShape stickShape(Rng& r, double aspect) {
    ScatterShape sh;
    const double rad = aspect * 0.5;
    const dvec3 a(0, -0.5, 0), b(r.range(-0.035, 0.035), r.range(-0.12, 0.12), r.range(-0.035, 0.035)), c(0, 0.5, 0);
    sh.solid.pieces.push_back(barkFrustum(a, rad, b, rad * 0.88, glm::normalize(b - a), 6));
    sh.solid.pieces.push_back(barkFrustum(b, rad * 0.88, c, rad * 0.62, glm::normalize(c - b), 6));
    if (r.next() < 0.45) {  // Zweig, schräg nach vorn
        const double at = r.range(0.25, 0.75);
        const dvec3 base = at < 0.5 ? glm::mix(a, b, at * 2.0) : glm::mix(b, c, at * 2.0 - 1.0);
        const dvec3 dir = glm::normalize(dvec3(0, 0.75, 0) + across(dvec3(0, 1, 0), r.next() * 2.0 * kPi));
        sh.solid.pieces.push_back(barkFrustum(base, rad * 0.5, base + dir * r.range(0.12, 0.3), rad * 0.3, dir, 5));
    }
    finish(sh);
    sh.aspect = aspect;
    return sh;
}

// Liegender Stamm: 1 m lang entlang Y, zum Ende dünner, mit bis zu zwei Aststummeln
ScatterShape logShape(Rng& r, double aspect) {
    ScatterShape sh;
    const double rad = aspect * 0.5;
    sh.solid.pieces.push_back(barkFrustum(dvec3(0, -0.5, 0), rad, dvec3(0, 0.5, 0), rad * 0.85, dvec3(0, 1, 0), 12));
    const int stubs = (int)(r.next() * 3);
    for (int i = 0; i < stubs; ++i) {
        const double y = r.range(-0.25, 0.4);
        const dvec3 side = across(dvec3(0, 1, 0), r.next() * 2.0 * kPi), dir = glm::normalize(side + dvec3(0, 0.6, 0));
        const dvec3 base = dvec3(0, y, 0) + side * rad * 0.4;
        sh.solid.pieces.push_back(barkFrustum(base, rad * r.range(0.25, 0.4), base + dir * rad * r.range(1.6, 2.8), rad * 0.15, dir, 6));
    }
    finish(sh);
    sh.aspect = aspect;
    return sh;
}

// Baumstumpf: 1 m breit, unten ausgestellt (Wurzelanlauf), oben die Schnittfläche
ScatterShape stumpShape(Rng& r, double aspect) {
    ScatterShape sh;
    const double h = aspect, top = 0.5 * r.range(0.9, 0.97);
    sh.solid.pieces.push_back(barkFrustum(dvec3(0), 0.5 * r.range(1.2, 1.35), dvec3(0, h * 0.22, 0), 0.5, dvec3(0, 1, 0), 12));
    sh.solid.pieces.push_back(barkFrustum(dvec3(0, h * 0.22, 0), 0.5, dvec3(0, h, 0), top, dvec3(0, 1, 0), 12));
    finish(sh);
    sh.aspect = aspect;
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

// Drehung, die +Y auf die Richtung d bringt (d nicht senkrecht nach unten)
glm::dquat fromUp(const dvec3& d) {
    const dvec3 ax = glm::cross(dvec3(0, 1, 0), d);
    const double s = glm::length(ax);
    if (s < 1e-9) return glm::dquat(1, 0, 0, 0);
    return glm::angleAxis(std::atan2(s, d.y), ax / s);
}

} // namespace

ScatterField::ScatterField(std::shared_ptr<const WorldGen> gen, const Terrain* terrain, Forest* forest, Renderer* renderer)
    : gen_(std::move(gen)), terrain_(terrain), forest_(forest), renderer_(renderer) {
    seed_ = noise::subSeed(gen_->seed(), "scatter");
    // Biome ohne Angaben (Welten von vor diesem Zeug): die Werte der eingebauten biomes.json mit gleicher id
    const GenConfig& builtin = builtinGenConfig();
    static const char* const fallback[kScatterKinds][2] = {{"fieldstone", "granite"}, {"spruce", "oak"}, {"spruce", "oak"}, {"spruce", "oak"}};
    for (const GenConfig::Biome& b : gen_->config().biomes)
        for (int k = 0; k < kScatterKinds; ++k) {
            GenConfig::Biome::Scatter s = b.scatter[k];
            if (!s.given)
                if (const int i = builtin.findBiome(b.id); i >= 0) s = builtin.biomes[(size_t)i].scatter[k];
            std::vector<int> mats;
            for (const std::string& m : s.materials)
                if (const int i = findMaterialIndex(m.c_str()); i >= 0) mats.push_back(i);
            for (const char* m : fallback[k])
                if (mats.empty() && findMaterialIndex(m) >= 0) mats.push_back(findMaterialIndex(m));
            if (mats.empty()) mats.push_back(0);
            biomes_[k].push_back(s);
            materials_[k].push_back(mats);
            maxDensity_[k] = std::max(maxDensity_[k], s.perHa / 10000.0);
            any_ |= s.perHa > 0;
        }
    if (!any_) return;
    for (int k = 0; k < kScatterKinds; ++k)
        for (int i = 0; i < kVariants[k]; ++i) {
            Rng r{noise::mix64(seed_ + 0x51A0E5ull * (uint64_t)(k * 64 + i + 1))};
            const double aspect = kAspectLo[k] * std::pow(kAspectHi[k] / std::max(kAspectLo[k], 1e-9), double(i) / (kVariants[k] - 1));
            ScatterShape sh = k == kScatterStone ? stoneShape(r) : k == kScatterStick ? stickShape(r, aspect) : k == kScatterLog ? logShape(r, aspect) : stumpShape(r, aspect);
            sh.kind = k;
            if (renderer_) {
                std::vector<solid::Vertex> v;
                solid::buildTriangles(sh.solid, v);
                sh.mesh = renderer_->createMesh(v);
            }
            kindShapes_[k].push_back((int)shapes_.size());
            shapes_.push_back(std::move(sh));
        }
}

ScatterField::~ScatterField() {
    if (renderer_)
        for (ScatterShape& s : shapes_)
            if (s.mesh.valid()) renderer_->destroyMesh(s.mesh);
}

int ScatterField::pickShape(int kind, double aspect, double r) const {
    // die zwei Vorlagen mit der passendsten Schlankheit, zufällig eine davon
    const std::vector<int>& list = kindShapes_[kind];
    int best = list.front(), second = list.front();
    double e1 = 1e300, e2 = 1e300;
    for (int i : list) {
        const double e = std::abs(std::log(std::max(shapes_[(size_t)i].aspect, 1e-9) / std::max(aspect, 1e-9)));
        if (e < e1) {
            e2 = e1, second = best;
            e1 = e, best = i;
        } else if (e < e2) {
            e2 = e, second = i;
        }
    }
    return r < 0.5 ? best : second;
}

bool ScatterField::clearOfTrees(const glm::dvec3& a, const glm::dvec3& b, double radius) {
    if (!forest_ || !forest_->any()) return true;
    const glm::dvec2 p(a.x, a.z), q(b.x, b.z), mid = (p + q) * 0.5;
    std::vector<TreeSpot> trees;
    forest_->around(mid, glm::length(q - p) * 0.5 + radius + 3.0, trees);
    const TreeConfig& cfg = forest_->library().config();
    for (const TreeSpot& s : trees) {
        const TreeType& tt = cfg.types[(size_t)s.type];
        const double trunk = tt.diameter * 0.5 * tt.flare * s.scale;
        const glm::dvec2 c(s.pos.x, s.pos.z), pq = q - p;
        const double len2 = glm::dot(pq, pq), t = len2 > 1e-12 ? std::clamp(glm::dot(c - p, pq) / len2, 0.0, 1.0) : 0.0;
        if (glm::length(c - (p + pq * t)) < trunk + radius + 0.1) return false;
    }
    return true;
}

void ScatterField::compute(int64_t tx, int64_t tz, Tile& t) {
    t.spots.clear();
    t.rev = terrain_ ? terrain_->editRevision() : 0;
    if (!any_) return;
    const WorldGen& gen = *gen_;
    const GenConfig& cfg = gen.config();
    const bool rivers = terrain_ && terrain_->hasRivers();
    const double waterLine = terrain_ ? terrain_->riverLevel() + 0.05 : -1e30;
    auto groundAt = [&](double x, double z, double fallback) { return terrain_ ? terrain_->surfaceHeight(x, z) : fallback; };
    const uint64_t tileSeed = noise::mix64(seed_ ^ noise::mix64((uint64_t)tx * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)tz * 0xC2B2AE3D27D4EB4Full));
    const dvec3 up(0, 1, 0);
    for (int kind = 0; kind < kScatterKinds; ++kind) {
        if (maxDensity_[kind] <= 0) continue;
        const uint64_t kindSeed = noise::mix64(tileSeed + 0xC0FFEEull * (uint64_t)(kind + 1));
        Rng r{kindSeed};
        const double expect = maxDensity_[kind] * kTile * kTile;
        const int n = std::min(255, (int)expect + (r.next() < expect - std::floor(expect) ? 1 : 0));
        for (int k = 0; k < n; ++k) {
            Rng q{noise::mix64(kindSeed + 0x5707E5ull * (uint64_t)(k + 1))};  // eigene Zahlen je Stück: Schlüssel bleiben gleich
            const double x = (tx + q.next()) * kTile, z = (tz + q.next()) * kTile;
            const double accept = q.next(), pick = q.next(), sizeR = q.next(), thickR = q.next(), matR = q.next();
            const double yaw = q.next() * 2.0 * kPi, roll = q.next() * 2.0 * kPi, shapeR = q.next(), tiltAxis = q.next() * 2.0 * kPi;
            const double tilt = q.range(-1.0, 1.0);
            GenSample g;
            gen.sample(x, z, g);
            if (g.wet) continue;  // Flussbett
            // Dichte aus den Biomen: an Grenzen weich; welches Biom das Stück bestimmt, nach seinem Anteil an der Dichte
            double w[kMaxBiomes] = {}, sum = 0, density = 0;
            if (gen.singleBiome() >= 0) {
                w[gen.singleBiome()] = sum = 1;
            } else {
                for (size_t b = 0; b < cfg.biomes.size(); ++b) {
                    const double v = std::clamp(0.5 + g.score[b] / (2.0 * cfg.blend), 0.0, 1.0);
                    w[b] = v * v * (3.0 - 2.0 * v);
                    sum += w[b];
                }
                if (sum < 1e-9) w[g.biome] = sum = 1;
            }
            for (size_t b = 0; b < biomes_[kind].size(); ++b) density += w[b] / sum * biomes_[kind][b].perHa / 10000.0;
            if (accept * maxDensity_[kind] >= density || density <= 0) continue;
            double p = pick * density * sum * 10000.0;
            size_t bi = 0;
            for (size_t b = 0; b < biomes_[kind].size(); ++b) {
                const double share = w[b] * biomes_[kind][b].perHa;
                if (share <= 0) continue;
                bi = b;
                if (p < share) break;
                p -= share;
            }
            const GenConfig::Biome::Scatter& st = biomes_[kind][bi];
            // viele kleine, wenige große; dicker, je länger
            const double f = std::pow(sizeR, st.skew), size = st.sizeMin * std::pow(st.sizeMax / st.sizeMin, f);
            const double thick = st.thickMin + (st.thickMax - st.thickMin) * std::clamp(0.6 * f + 0.4 * thickR, 0.0, 1.0);
            const std::vector<int>& mats = materials_[kind][bi];
            const double ground = groundAt(x, z, g.height);
            if (rivers && ground < waterLine) continue;  // unter Wasser (z.B. ausgehoben)

            ScatterSpot s;
            s.kind = kind;
            s.key = (1ull << 62) | ((uint64_t)kind << 56) | ((uint64_t)(tx & 0xFFFFFF) << 32) | ((uint64_t)(tz & 0xFFFFFF) << 8) | (uint64_t)k;
            s.material = mats[std::min((size_t)(matR * mats.size()), mats.size() - 1)];
            if (kind == kScatterStone) {
                s.shape = kindShapes_[kind][std::min((size_t)(shapeR * kindShapes_[kind].size()), kindShapes_[kind].size() - 1)];
                s.size = size;
                s.rot = glm::angleAxis(yaw, up) * glm::angleAxis(0.35 * tilt, dvec3(std::cos(tiltAxis), 0, std::sin(tiltAxis)));
                s.pos = dvec3(x, ground - shapes_[(size_t)s.shape].bottom * size * 0.62, z);  // ein gutes Drittel steckt im Boden
                if (size > 0.5 && !clearOfTrees(s.pos, s.pos, size * 0.5)) continue;
            } else if (kind == kScatterStick) {
                s.shape = pickShape(kind, thick / size, shapeR);
                s.size = size;  // Länge
                s.rot = glm::angleAxis(yaw, up) * glm::angleAxis(0.5 * kPi + 0.06 * tilt, dvec3(0, 0, 1)) * glm::angleAxis(roll, up);
                s.pos = dvec3(x, ground + thick * 0.3, z);
            } else if (kind == kScatterLog) {
                // liegt in Richtung yaw und folgt dem Hang: Höhe an beiden Enden
                s.shape = pickShape(kind, thick / size, shapeR);
                s.size = size;  // Länge
                const dvec3 along(std::cos(yaw), 0, std::sin(yaw));
                const dvec3 e1 = dvec3(x, 0, z) - along * (size * 0.5), e2 = dvec3(x, 0, z) + along * (size * 0.5);
                const double h1 = groundAt(e1.x, e1.z, g.height), h2 = groundAt(e2.x, e2.z, g.height);
                if (rivers && std::min(h1, h2) < waterLine) continue;
                const double pitch = std::atan2(h2 - h1, size);
                s.rot = fromUp(dvec3(along.x * std::cos(pitch), std::sin(pitch), along.z * std::cos(pitch))) * glm::angleAxis(roll, up);
                s.pos = dvec3(x, 0.5 * (h1 + h2) + thick * 0.5 * 0.55, z);  // knapp die Hälfte eingesunken
                if (!clearOfTrees(e1, e2, thick * 0.5 + 0.1)) continue;
            } else {
                // Stumpf: steht, Höhe / Breite wie gewürfelt
                s.shape = pickShape(kind, size / std::max(thick, 0.05), shapeR);
                s.size = thick;  // Breite
                s.rot = glm::angleAxis(yaw, up) * glm::angleAxis(0.05 * tilt, dvec3(std::cos(tiltAxis), 0, std::sin(tiltAxis)));
                s.pos = dvec3(x, ground - shapes_[(size_t)s.shape].bottom * thick * 0.8, z);  // unten im Boden
                if (!clearOfTrees(s.pos, s.pos, thick * 0.65)) continue;
            }
            t.spots.push_back(s);
        }
    }
}

const std::vector<ScatterSpot>& ScatterField::tile(int64_t tx, int64_t tz, bool fresh) {
    auto it = tiles_.find(tileKey(tx, tz));
    if (it == tiles_.end()) {
        it = tiles_.emplace(tileKey(tx, tz), Tile{}).first;
        compute(tx, tz, it->second);
    } else if (fresh && terrain_ && it->second.rev != terrain_->editRevision()) {
        compute(tx, tz, it->second);
    }
    return it->second.spots;
}

void ScatterField::around(const glm::dvec2& c, double radius, std::vector<ScatterSpot>& out) {
    out.clear();
    if (!any_) return;
    const int64_t x0 = (int64_t)std::floor((c.x - radius) / kTile), x1 = (int64_t)std::floor((c.x + radius) / kTile);
    const int64_t z0 = (int64_t)std::floor((c.y - radius) / kTile), z1 = (int64_t)std::floor((c.y + radius) / kTile);
    for (int64_t tz = z0; tz <= z1; ++tz)
        for (int64_t tx = x0; tx <= x1; ++tx)
            for (const ScatterSpot& s : tile(tx, tz, true))
                if (glm::length(glm::dvec2(s.pos.x, s.pos.z) - c) <= radius) out.push_back(s);
}

void ScatterField::prepare(const glm::dvec2& c, double radius, int budget) {
    if (!any_) return;
    PROFILE("scatter.prepare");
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
    for (size_t i = 0; i < want.size() && (int)i < budget; ++i) {
        const auto [tx, tz] = want[i].second;
        compute(tx, tz, tiles_[tileKey(tx, tz)]);
    }
    // weit weg: vergessen (entsteht bei Bedarf neu)
    if (tiles_.size() > 4096)
        for (auto it = tiles_.begin(); it != tiles_.end();) {
            const int64_t tx = (int32_t)(it->first >> 32), tz = (int32_t)(it->first & 0xffffffffu);
            if (glm::length(glm::dvec2((tx + 0.5) * kTile, (tz + 0.5) * kTile) - c) > radius + 4 * kTile) it = tiles_.erase(it);
            else ++it;
        }
}

bool ScatterField::raycast(const glm::dvec3& o, const glm::dvec3& dir, double maxT, const std::function<bool(uint64_t)>& skip,
                           ScatterSpot& hit, double& t) {
    if (!any_) return false;
    bool found = false;
    t = maxT;
    const glm::dvec3 e = o + dir * maxT;
    const double reach = 6.0;  // so weit ragt ein Stück (liegender Stamm) über seine Kachel
    const int64_t x0 = (int64_t)std::floor((std::min(o.x, e.x) - reach) / kTile), x1 = (int64_t)std::floor((std::max(o.x, e.x) + reach) / kTile);
    const int64_t z0 = (int64_t)std::floor((std::min(o.z, e.z) - reach) / kTile), z1 = (int64_t)std::floor((std::max(o.z, e.z) + reach) / kTile);
    for (int64_t tz = z0; tz <= z1; ++tz)
        for (int64_t tx = x0; tx <= x1; ++tx)
            for (const ScatterSpot& s : tile(tx, tz, true)) {
                const ScatterShape& sh = shapes_[(size_t)s.shape];
                // erst die umhüllende Kugel, dann die Stücke der Vorlage (in ihrem Raum, Größe 1)
                const glm::dvec3 rel = s.pos - o;
                const double along = glm::dot(rel, dir), r = sh.radius * s.size;
                if (along < -r || along > t + r || glm::length(rel - dir * along) > r) continue;
                if (skip && skip(s.key)) continue;
                const glm::dquat inv = glm::conjugate(s.rot);
                const glm::dvec3 lo = inv * (o - s.pos) / s.size, ld = inv * dir / s.size;
                for (const std::vector<solid::Plane>& planes : sh.planes) {
                    double ts;
                    if (rayConvex(planes, lo, ld, t, ts) && ts < t) {
                        t = ts;
                        hit = s;
                        found = true;
                    }
                }
            }
    return found;
}
