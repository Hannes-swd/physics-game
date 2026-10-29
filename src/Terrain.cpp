// Gelände: Kacheln, Detailstufen, Erzeugung im Hintergrund, Physik je Chunk, Änderungen und Regionsdateien
// (siehe Terrain.h und docs/16-Weltgenerierung.md)
#include "Terrain.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <climits>
#include <cstring>
#include <fstream>
#include <sstream>

#include "Jobs.h"
#include "Profile.h"
#include "Worlds.h"

namespace {

constexpr int N = kTerrainCells;
constexpr int M = N + 3;                 // Höhen mit Rand: Punkte -1 .. N+1
constexpr double kSplit = 2.0;           // Kachel teilen, wenn das Auge näher als kSplit · Kachelgröße ist
constexpr double kViewDistance = 2500.0; // so weit wird Gelände gezeichnet (m)
constexpr size_t kMaxCollect = 24;       // fertige Kacheln je Bild übernehmen (GPU-Speicher anlegen)
constexpr double kKeepTiles = 4.0;       // nicht mehr gebrauchte Kacheln so lange behalten (s)
constexpr double kKeepBodies = 5.0;      // nicht mehr gebrauchte Höhenfelder so lange behalten (s)
constexpr double kPhysMargin = 1.5;      // Physik auch so weit um Teile herum (m)
constexpr int kBodySamples = N + 2;      // Höhenfeld je Chunk: 34 Punkte (Jolt: gerade Anzahl), reicht 0,5 m in den Nachbarn

constexpr double kPi = 3.14159265358979323846;

int64_t floorDiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

uint64_t latticeKey(int64_t i, int64_t j) { return ((uint64_t)(uint32_t)(int32_t)i << 32) | (uint32_t)(int32_t)j; }

uint32_t packColor(const glm::vec3& c) {
    auto b = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return b(c.r) | (b(c.g) << 8) | (b(c.b) << 16) | 0xff000000u;
}

float smooth01(float a, float b, float x) {
    const float t = std::clamp((x - a) / std::max(b - a, 1e-4f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

struct Terrain::Result {
    TerrainKey key;
    uint64_t job = 0;
    std::vector<float> h;
    float lo = 0, hi = 0;
    std::vector<solid::Vertex> verts;
    std::vector<LineVertex> border;
};

struct Terrain::Queue {
    std::mutex m;
    std::vector<Result> done;
};

// ============================================================================ Aufbau

Terrain::Terrain(Physics& physics, Renderer* renderer, std::shared_ptr<const WorldGen> gen, const std::filesystem::path& regionDir)
    : physics_(physics), renderer_(renderer), gen_(std::move(gen)), regionDir_(regionDir), queue_(std::make_shared<Queue>()) {
    loadRegions();
}

Terrain::~Terrain() {
    // Laufende Erzeugungen schreiben in queue_ (gemeinsamer Besitz) und dürfen länger leben als das Gelände
    for (auto& [k, t] : tiles_)
        if (renderer_) renderer_->destroyMesh(t.mesh);
    for (auto& [k, b] : bodies_) physics_.destroyBody(b.handle);
}

double Terrain::now() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Terrain::setGen(std::shared_ptr<const WorldGen> gen) {
    gen_ = std::move(gen);
    ++genRev_;
    clearCache();
    for (auto& [k, t] : tiles_) t.dirty = true;  // die alten bleiben sichtbar, bis die neuen da sind
    for (auto& [k, b] : bodies_) physics_.destroyBody(b.handle);
    bodies_.clear();  // updatePhysics baut sie neu
}

// ============================================================================ Änderungen (Raster)

double Terrain::edit(int64_t i, int64_t j) const {
    const int64_t cx = floorDiv(i, N), cz = floorDiv(j, N);
    auto it = edits_.find(chunkKey(cx, cz));
    return it == edits_.end() ? 0.0 : it->second->d[(i - cx * N) + N * (j - cz * N)];
}

Terrain::EditMap Terrain::editsFor(int64_t i0, int64_t j0, int64_t i1, int64_t j1) const {
    EditMap out;
    if (edits_.empty()) return out;
    const int64_t cx0 = floorDiv(i0, N), cx1 = floorDiv(i1, N), cz0 = floorDiv(j0, N), cz1 = floorDiv(j1, N);
    for (const auto& [key, e] : edits_) {  // wenige geänderte Chunks: alle durchsehen
        int64_t cx, cz;
        chunkOf(key, cx, cz);
        if (cx >= cx0 && cx <= cx1 && cz >= cz0 && cz <= cz1) out.emplace(key, e);
    }
    return out;
}

TerrainEdit& Terrain::editChunk(int64_t cx, int64_t cz) {
    std::shared_ptr<const TerrainEdit>& p = edits_[chunkKey(cx, cz)];
    if (!p) p = std::make_shared<TerrainEdit>();
    else if (p.use_count() > 1) p = std::make_shared<TerrainEdit>(*p);  // eine Erzeugung liest den alten Stand noch
    return const_cast<TerrainEdit&>(*p);  // nur wir besitzen ihn (nicht const angelegt)
}

Terrain::Point Terrain::latticePoint(int64_t i, int64_t j) const {
    // Der Schlüssel des Zwischenspeichers hat 32 Bit je Achse: was nicht hineinpasst (über 1 Million km, oder eine
    // ungültige Abfrage), wird nicht gespeichert, sonst landet es unter dem Schlüssel eines echten Punkts.
    // Gesperrt, weil die Wasserphysik aus mehreren Threads fragt (gerechnet wird außerhalb der Sperre)
    const bool cacheable = i >= INT32_MIN && i <= INT32_MAX && j >= INT32_MIN && j <= INT32_MAX;
    const uint64_t k = latticeKey(i, j);
    if (cacheable) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = heightCache_.find(k);
        if (it != heightCache_.end()) return it->second;
    }
    GenSample s;
    gen_->sample(double(i) * kTerrainCell, double(j) * kTerrainCell, s);
    const Point p{(float)(s.height + edit(i, j)), s.wet};
    if (cacheable) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (heightCache_.size() > 2000000) heightCache_.clear();
        heightCache_.emplace(k, p);
    }
    return p;
}

void Terrain::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    heightCache_.clear();
}

double Terrain::latticeHeight(int64_t i, int64_t j) const { return latticePoint(i, j).h; }
bool Terrain::latticeWet(int64_t i, int64_t j) const { return latticePoint(i, j).wet; }

double Terrain::latticeHeightFresh(int64_t i, int64_t j) const {
    return (float)(gen_->height(i * kTerrainCell, j * kTerrainCell) + edit(i, j));
}

double Terrain::surfaceHeight(double x, double z, glm::dvec3* normal) const {
    if (!std::isfinite(x) || !std::isfinite(z)) {  // ungültige Abfrage (z.B. Strahl aus einem leeren Fenster)
        if (normal) *normal = glm::dvec3(0, 1, 0);
        return 0.0;
    }
    // Wie Jolts Höhenfeld: Viereck entlang (i, j) -> (i+1, j+1) geteilt
    const double fx = x / kTerrainCell, fz = z / kTerrainCell;
    const double ix = std::floor(fx), iz = std::floor(fz), tx = fx - ix, tz = fz - iz;
    const int64_t i = (int64_t)ix, j = (int64_t)iz;
    const double h00 = latticeHeight(i, j), h10 = latticeHeight(i + 1, j), h01 = latticeHeight(i, j + 1), h11 = latticeHeight(i + 1, j + 1);
    double h, dx, dz;
    if (tz > tx) {
        h = h00 + tz * (h01 - h00) + tx * (h11 - h01);
        dx = h11 - h01;
        dz = h01 - h00;
    } else {
        h = h00 + tz * (h11 - h10) + tx * (h10 - h00);
        dx = h10 - h00;
        dz = h11 - h10;
    }
    if (normal) *normal = glm::normalize(glm::dvec3(-dx / kTerrainCell, 1.0, -dz / kTerrainCell));
    return h;
}

double Terrain::maxHeightUnder(double x0, double z0, double x1, double z1) const {
    double best = -1e30;
    const int n = 8;
    for (int b = 0; b <= n; ++b)
        for (int a = 0; a <= n; ++a) best = std::max(best, surfaceHeight(x0 + (x1 - x0) * a / n, z0 + (z1 - z0) * b / n));
    return best;
}

bool Terrain::findBiome(int biome, const glm::dvec2& from, double maxDist, glm::dvec2& out) const {
    // Gesucht ist die nächste Stelle, an der das Biom ganz es selbst ist (Score >= Übergangsbreite: kein anderes mischt
    // sich mehr hinein). Grob in quadratischen Ringen (32 m), weiter bis zum Ring, der nichts Näheres mehr enthalten kann,
    // dann um den besten Fund feiner (4 m, 0,5 m). Gerechnet wird nur das Klima (schnell)
    if (biome < 0 || biome >= (int)gen_->config().biomes.size()) return false;
    if (gen_->singleBiome() >= 0) {
        if (biome != gen_->singleBiome()) return false;
        out = from;
        return true;
    }
    const double need = gen_->config().blend;
    auto inside = [&](const glm::dvec2& p) {
        double s[kMaxBiomes];
        return gen_->classify(gen_->climate(p.x, p.y), s) == biome && s[biome] >= need;
    };
    if (inside(from)) {
        out = from;
        return true;
    }
    const double step = 32.0;
    const int rings = (int)std::ceil(maxDist / step);
    int last = rings;
    double best = maxDist * maxDist;
    bool found = false;
    for (int ring = 1; ring <= last; ++ring)
        for (int j = -ring; j <= ring; ++j)
            for (int i = -ring; i <= ring; i += (std::abs(j) == ring ? 1 : 2 * ring)) {  // nur der Rand des Rings
                const glm::dvec2 p = from + glm::dvec2(i, j) * step;
                const double d2 = glm::dot(p - from, p - from);
                if (d2 >= best || !inside(p)) continue;
                best = d2;
                out = p;
                found = true;
                last = std::min(rings, (int)std::floor(std::sqrt(best) / step));  // weiter draußen ist alles weiter weg
            }
    if (!found) return false;
    // Im Raster lag die nächste Stelle bis zu einer Maschenweite neben dem Fund
    for (const double s : {4.0, 0.5}) {
        const glm::dvec2 c = out;
        const int n = s > 1.0 ? 10 : 8;
        for (int j = -n; j <= n; ++j)
            for (int i = -n; i <= n; ++i) {
                const glm::dvec2 p = c + glm::dvec2(i, j) * s;
                const double d2 = glm::dot(p - from, p - from);
                if (d2 < best && inside(p)) {
                    best = d2;
                    out = p;
                }
            }
    }
    return true;
}

// ============================================================================ Flüsse

namespace {

constexpr double kDry = 1000.0;  // Grund außerhalb der Flussbetten: so weit über dem Spiegel (dort ist nie Wasser)

// Das Gewässer "Flüsse" (World): Grund und Strömung kommen aus dem Gelände
class TerrainWater : public WaterShape {
public:
    explicit TerrainWater(const Terrain& t) : t_(t) {}
    double bottom(double x, double z) const override { return t_.waterBottom(x, z); }
    glm::dvec3 flow(double x, double z) const override { return t_.riverFlow(x, z); }
    double lowest(double x0, double z0, double x1, double z1) const override {
        const int nx = std::clamp((int)std::ceil((x1 - x0) / 1.0), 1, 12), nz = std::clamp((int)std::ceil((z1 - z0) / 1.0), 1, 12);
        double lo = 1e30;
        for (int b = 0; b <= nz; ++b)
            for (int a = 0; a <= nx; ++a) lo = std::min(lo, t_.waterBottom(x0 + (x1 - x0) * a / nx, z0 + (z1 - z0) * b / nz));
        return lo;
    }
    uint64_t revision() const override { return t_.waterRevision(); }

private:
    const Terrain& t_;
};

} // namespace

std::shared_ptr<const WaterShape> Terrain::waterShape() const { return std::make_shared<TerrainWater>(*this); }

double Terrain::waterBottom(double x, double z) const {
    const double L = gen_->riverLevel();
    if (!gen_->hasRivers() || !std::isfinite(x) || !std::isfinite(z)) return L + kDry;
    const int64_t i = (int64_t)std::floor(x / kTerrainCell), j = (int64_t)std::floor(z / kTerrainCell);
    if (!latticeWet(i, j) && !latticeWet(i + 1, j) && !latticeWet(i, j + 1) && !latticeWet(i + 1, j + 1)) return L + kDry;
    return surfaceHeight(x, z);
}

glm::dvec3 Terrain::riverFlow(double x, double z) const {
    RiverSample r;
    gen_->river(x, z, r);
    if (r.strength <= 0.0 || r.dist > r.half + 60.0) return glm::dvec3(0);
    const double u = r.dist / std::max(r.half, 0.1);  // in der Rinne: Mitte schnell, Ufer halb so schnell
    return glm::dvec3(r.flow.x, 0, r.flow.y) * (u < 1.0 ? 1.0 - 0.5 * u * u : 1.0);
}

bool Terrain::findRiver(const glm::dvec2& from, double maxDist, glm::dvec2& out) const {
    // Grob in Ringen (16 m) Stellen nahe einer Rinne (nur der Generator), um jede davon das echte Wasser suchen (2 m, dann
    // 0,5 m). Weiter, bis kein Ring mehr Näheres enthalten kann
    if (!gen_->hasRivers()) return false;
    const double L = gen_->riverLevel();
    auto water = [&](const glm::dvec2& p) { return waterBottom(p.x, p.y) < L - 0.02; };
    if (water(from)) {
        out = from;
        return true;
    }
    const double step = 16.0, reach = 20.0;
    const int rings = (int)std::ceil(maxDist / step);
    int last = rings;
    double best = maxDist * maxDist;
    bool found = false;
    for (int ring = 0; ring <= last; ++ring)
        for (int j = -ring; j <= ring; ++j)
            for (int i = -ring; i <= ring; i += (std::abs(j) == ring || ring == 0 ? 1 : 2 * ring)) {
                const glm::dvec2 p = from + glm::dvec2(i, j) * step;
                if (glm::length(p - from) - reach * 1.42 > std::sqrt(best)) continue;
                RiverSample r;
                gen_->river(p.x, p.y, r);
                if (!(r.strength > 0.02 && r.dist < r.half + reach)) continue;
                for (int b = -10; b <= 10; ++b)
                    for (int a = -10; a <= 10; ++a) {
                        const glm::dvec2 q = p + glm::dvec2(a, b) * 2.0;
                        const double d2 = glm::dot(q - from, q - from);
                        if (d2 < best && water(q)) {
                            best = d2;
                            out = q;
                            found = true;
                        }
                    }
                if (found) last = std::min(rings, (int)std::ceil((std::sqrt(best) + reach * 1.42) / step));
            }
    if (!found) return false;
    const glm::dvec2 c = out;
    for (int b = -4; b <= 4; ++b)
        for (int a = -4; a <= 4; ++a) {
            const glm::dvec2 q = c + glm::dvec2(a, b) * 0.5;
            const double d2 = glm::dot(q - from, q - from);
            if (d2 < best && water(q)) {
                best = d2;
                out = q;
            }
        }
    return true;
}

bool Terrain::waterInView() const {
    if (!gen_->hasRivers()) return false;
    for (const Drawn& d : draw_)
        if (d.lo.y < gen_->riverLevel()) return true;
    return false;
}

bool Terrain::raycast(const glm::dvec3& o, const glm::dvec3& d, double maxT, double& t, glm::dvec3& normal) const {
    // Den Strahl entlanglaufen, nah fein, weiter weg in größeren Schritten (dort auch gröberes Raster), dann den
    // Schnittpunkt halbieren. Liegt der Anfang unter der Oberfläche (Kamera im Hang), gibt es keinen Treffer
    auto coarse = [&](double x, double z, double step) {
        if (step < 1.0) return surfaceHeight(x, z);
        int64_t s = 1;  // Raster passend zur Schrittweite: Punkte im Abstand s · 0,5 m (bilinear)
        while (s * kTerrainCell * 2 <= step && s < 128) s *= 2;
        const double g = s * kTerrainCell;
        const double fx = std::floor(x / g), fz = std::floor(z / g), tx = x / g - fx, tz = z / g - fz;
        const int64_t i = (int64_t)fx * s, j = (int64_t)fz * s;
        const double a = latticeHeight(i, j), b = latticeHeight(i + s, j), c = latticeHeight(i, j + s), e = latticeHeight(i + s, j + s);
        return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + e * tx) * tz;
    };
    if (!std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.z) || !std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z))
        return false;
    if (o.y < surfaceHeight(o.x, o.z)) return false;
    double prev = 0, tt = 0;
    while (tt < maxT) {
        const double step = std::clamp(0.2 + tt * 0.01, 0.2, 24.0);
        tt = std::min(tt + step, maxT);
        const glm::dvec3 p = o + d * tt;
        const double diff = p.y - coarse(p.x, p.z, step);
        if (diff < 0) {
            double a = prev, b = tt;
            for (int k = 0; k < 24; ++k) {
                const double m = 0.5 * (a + b);
                const glm::dvec3 q = o + d * m;
                (q.y < surfaceHeight(q.x, q.z) ? b : a) = m;
            }
            t = b;
            const glm::dvec3 q = o + d * t;
            surfaceHeight(q.x, q.z, &normal);
            return true;
        }
        if (d.y >= 0 && diff > 600) break;  // geht nach oben weg
        prev = tt;
    }
    return false;
}

// ============================================================================ Erzeugung einer Kachel

void Terrain::generateTile(const WorldGen& gen, const EditMap& edits, const TerrainKey& key, bool withMesh, Result& r) {
    const int64_t s = 1ll << key.level;  // Rasterpunkte je Viereck
    const double cell = kTerrainCell * double(s);
    const int64_t i0 = key.x * N * s, j0 = key.z * N * s;
    const size_t nb = gen.config().biomes.size();
    auto editAt = [&](int64_t I, int64_t J) -> double {
        if (edits.empty()) return 0.0;
        const int64_t cx = floorDiv(I, N), cz = floorDiv(J, N);
        auto it = edits.find(chunkKey(cx, cz));
        return it == edits.end() ? 0.0 : it->second->d[(I - cx * N) + N * (J - cz * N)];
    };
    r.h.assign((size_t)M * M, 0.0f);
    const int V = N + 1;  // Eckpunkte je Achse
    std::vector<int> label((size_t)V * V);
    std::vector<double> score((size_t)V * V * nb);
    std::vector<glm::vec3> col((size_t)V * V), slopeCol((size_t)V * V);
    std::vector<glm::vec2> slope((size_t)V * V);
    GenSample gs;
    r.lo = 1e30f;
    r.hi = -1e30f;
    for (int j = -1; j <= N + 1; ++j)
        for (int i = -1; i <= N + 1; ++i) {
            const int64_t I = i0 + i * s, J = j0 + j * s;
            gen.sample(double(I) * kTerrainCell, double(J) * kTerrainCell, gs);
            const float h = (float)(gs.height + editAt(I, J));
            r.h[(size_t)(i + 1) + (size_t)M * (j + 1)] = h;
            if (i < 0 || j < 0 || i > N || j > N) continue;
            const size_t v = (size_t)i + (size_t)V * j;
            r.lo = std::min(r.lo, h);
            r.hi = std::max(r.hi, h);
            label[v] = gs.biome;
            for (size_t b = 0; b < nb; ++b) score[v * nb + b] = gs.score[b];
            col[v] = gs.color;
            slopeCol[v] = gs.slopeColor;
            slope[v] = glm::vec2(gs.slopeStart, gs.slopeEnd);
        }
    if (!withMesh) return;

    auto H = [&](int i, int j) { return r.h[(size_t)(i + 1) + (size_t)M * (j + 1)]; };
    // Eckpunkte: Normale aus den Nachbarn (auch über den Rand: gleiche Schattierung wie die Nachbarkachel)
    std::vector<solid::Vertex> grid((size_t)V * V);
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i) {
            const size_t v = (size_t)i + (size_t)V * j;
            const float dx = (H(i + 1, j) - H(i - 1, j)) / float(2 * cell), dz = (H(i, j + 1) - H(i, j - 1)) / float(2 * cell);
            const glm::vec3 n = glm::normalize(glm::vec3(-dx, 1.0f, -dz));
            const float k = smooth01(slope[v].x, slope[v].y, 1.0f - n.y);  // steil: Fels/Erde statt Gras
            solid::Vertex& vx = grid[v];
            vx.px = float(i * cell);
            vx.py = H(i, j);
            vx.pz = float(j * cell);
            vx.nx = n.x;
            vx.ny = n.y;
            vx.nz = n.z;
            vx.color = packColor(glm::mix(col[v], slopeCol[v], k));
        }
    r.verts.reserve((size_t)N * N * 6 + (size_t)4 * N * 12);
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const solid::Vertex &a = grid[(size_t)i + (size_t)V * j], &b = grid[(size_t)i + 1 + (size_t)V * j];
            const solid::Vertex &c = grid[(size_t)i + 1 + (size_t)V * (j + 1)], &d = grid[(size_t)i + (size_t)V * (j + 1)];
            // Diagonale a -> c wie im Höhenfeld der Physik; gegen den Uhrzeigersinn von oben
            r.verts.push_back(a);
            r.verts.push_back(d);
            r.verts.push_back(c);
            r.verts.push_back(a);
            r.verts.push_back(c);
            r.verts.push_back(b);
        }
    // Schürzen: an jedem Rand senkrecht nach unten, damit zwischen feinen und groben Kacheln keine Spalten zu sehen
    // sind (beidseitig, die Richtung ist dann egal)
    const float skirt = std::max(1.0f, 0.5f * (r.hi - r.lo) + float(cell));
    auto skirtEdge = [&](const solid::Vertex& p, const solid::Vertex& q) {
        solid::Vertex p2 = p, q2 = q;
        p2.py -= skirt;
        q2.py -= skirt;
        const solid::Vertex *P = &p, *Q = &q, *P2 = &p2, *Q2 = &q2;
        for (const solid::Vertex* t : {P, Q, Q2, P, Q2, P2, Q, P, P2, Q, P2, Q2}) r.verts.push_back(*t);
    };
    for (int k = 0; k < N; ++k) {
        skirtEdge(grid[(size_t)k], grid[(size_t)k + 1]);                                          // z = 0
        skirtEdge(grid[(size_t)k + (size_t)V * N], grid[(size_t)k + 1 + (size_t)V * N]);          // z = N
        skirtEdge(grid[(size_t)V * k], grid[(size_t)V * (k + 1)]);                                // x = 0
        skirtEdge(grid[(size_t)N + (size_t)V * k], grid[(size_t)N + (size_t)V * (k + 1)]);        // x = N
    }

    // Biomgrenzen: wo das beste Biom wechselt. Auf jeder Viereckskante mit zwei Biomen liegt der Punkt dort, wo ihre
    // Scores gleich sind (linear dazwischen); die Punkte eines Vierecks werden verbunden
    if (nb > 1 && gen.singleBiome() < 0) {
        const float lift = 0.05f + 0.03f * float(cell);
        auto point = [&](int ia, int ja, int ib, int jb, glm::vec3& out, glm::vec4& color) {
            const size_t p = (size_t)ia + (size_t)V * ja, q = (size_t)ib + (size_t)V * jb;
            const int A = label[p], B = label[q];
            const double fp = score[p * nb + A] - score[p * nb + B], fq = score[q * nb + A] - score[q * nb + B];
            const double t = fp - fq > 1e-12 ? std::clamp(fp / (fp - fq), 0.0, 1.0) : 0.5;
            const solid::Vertex &a = grid[p], &b = grid[q];
            out = glm::vec3(a.px + (b.px - a.px) * (float)t, a.py + (b.py - a.py) * (float)t + lift, a.pz + (b.pz - a.pz) * (float)t);
            const glm::vec3 ca = gen.config().biomes[(size_t)A].borderColor, cb = gen.config().biomes[(size_t)B].borderColor;
            color = glm::vec4(0.5f * (ca + cb), 1.0f);
        };
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                const int c[4][2] = {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}};
                glm::vec3 pts[4];
                glm::vec4 cols[4];
                int n = 0;
                for (int e = 0; e < 4; ++e) {
                    const int* p = c[e];
                    const int* q = c[(e + 1) & 3];
                    if (label[(size_t)p[0] + (size_t)V * p[1]] != label[(size_t)q[0] + (size_t)V * q[1]]) {
                        point(p[0], p[1], q[0], q[1], pts[n], cols[n]);
                        ++n;
                    }
                }
                auto seg = [&](int a, int b) {
                    r.border.push_back({pts[a], cols[a]});
                    r.border.push_back({pts[b], cols[b]});
                };
                if (n == 2) seg(0, 1);
                else if (n == 4) {
                    seg(0, 1);
                    seg(2, 3);
                } else if (n == 3) {  // drei Biome treffen sich: alle zur Mitte
                    const glm::vec3 m = (pts[0] + pts[1] + pts[2]) / 3.0f;
                    for (int a = 0; a < 3; ++a) {
                        r.border.push_back({pts[a], cols[a]});
                        r.border.push_back({m, cols[a]});
                    }
                }
            }
    }
}

// ============================================================================ Kacheln wählen und erzeugen

void Terrain::request(Tile& t, double priority, std::vector<std::pair<double, TerrainKey>>& want) {
    if (t.job == 0) want.push_back({priority, t.key});
}

bool Terrain::selectTile(const TerrainKey& key, const glm::dvec3& eye, double eyeAbove, std::vector<Drawn>& out,
                         std::vector<std::pair<double, TerrainKey>>& want) {
    Tile& t = tiles_[key];  // Verweise in unordered_map bleiben gültig, auch wenn neue dazukommen
    t.key = key;
    t.lastUsed = now();
    const double size = tileSize(key.level);
    const double x0 = double(key.x) * size, z0 = double(key.z) * size;
    const double dx = std::max({x0 - eye.x, 0.0, eye.x - (x0 + size)}), dz = std::max({z0 - eye.z, 0.0, eye.z - (z0 + size)});
    const double dy = t.ready ? std::max({double(t.lo) - eye.y, 0.0, eye.y - double(t.hi)}) : eyeAbove;
    const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double prio = d / size;  // klein = wichtig (nah im Verhältnis zur Größe)
    auto drawn = [&]() {
        Drawn dr;
        dr.mesh = &t.mesh;
        dr.origin = glm::dvec3(x0, 0, z0);
        dr.lo = glm::dvec3(x0, double(t.lo) - std::max(1.0, 0.5 * (t.hi - t.lo) + size / N), z0);
        dr.hi = glm::dvec3(x0 + size, double(t.hi), z0 + size);
        dr.level = key.level;
        dr.border = &t.border;
        return dr;
    };
    if (key.level > 0 && d < kSplit * std::clamp(viewScale_, 0.75, 1.5) * size) {
        std::vector<Drawn> sub;
        bool all = true;
        for (int c = 0; c < 4; ++c) {
            const TerrainKey child{key.level - 1, key.x * 2 + (c & 1), key.z * 2 + (c >> 1)};
            all = selectTile(child, eye, eyeAbove, sub, want) && all;  // alle besuchen: sie werden angefordert
        }
        if (all) {
            out.insert(out.end(), sub.begin(), sub.end());
            return true;
        }
        if (t.ready) {  // bis die feineren fertig sind: diese
            if (t.dirty) request(t, prio, want);
            out.push_back(drawn());
            return true;
        }
        request(t, prio, want);  // als Ersatz, falls die feineren länger brauchen
        return false;
    }
    if (t.ready) {
        if (t.dirty) request(t, prio, want);  // die alte bleibt sichtbar, bis die neue da ist
        out.push_back(drawn());
        return true;
    }
    ++missing_;
    request(t, prio, want);
    return false;
}

void Terrain::collect() {
    std::vector<Result> done;
    {
        std::lock_guard<std::mutex> lock(queue_->m);
        const size_t n = std::min(queue_->done.size(), kMaxCollect);
        done.assign(std::make_move_iterator(queue_->done.begin()), std::make_move_iterator(queue_->done.begin() + n));
        queue_->done.erase(queue_->done.begin(), queue_->done.begin() + n);
    }
    for (Result& r : done) {
        if (inFlight_ > 0) --inFlight_;
        auto it = tiles_.find(r.key);
        if (it == tiles_.end() || it->second.job != r.job) continue;  // veraltet
        Tile& t = it->second;
        t.job = 0;
        if (renderer_) {
            PROFILE("terrain.upload");
            GpuMesh m = renderer_->createMesh(r.verts);
            renderer_->destroyMesh(t.mesh);
            t.mesh = m;
        }
        t.h = std::move(r.h);
        t.lo = r.lo;
        t.hi = r.hi;
        t.border = std::move(r.border);
        t.ready = true;
    }
}

void Terrain::updateView(const glm::dvec3& eye) {
    if (!renderer_) return;  // ohne Bild keine Kacheln (die Physik braucht sie nicht)
    PROFILE("terrain.view");
    collect();
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z)) return;  // die Auswahl vom letzten Bild bleibt
    const double eyeAbove = std::max(0.0, eye.y - surfaceHeight(eye.x, eye.z));
    const double viewDist = kViewDistance * std::clamp(viewScale_, 0.5, 2.0);
    draw_.clear();
    missing_ = 0;
    std::vector<std::pair<double, TerrainKey>> want;
    const int top = kTerrainLevels - 1;
    const double size = tileSize(top);
    const int64_t tx0 = (int64_t)std::floor((eye.x - viewDist) / size), tx1 = (int64_t)std::floor((eye.x + viewDist) / size);
    const int64_t tz0 = (int64_t)std::floor((eye.z - viewDist) / size), tz1 = (int64_t)std::floor((eye.z + viewDist) / size);
    for (int64_t tz = tz0; tz <= tz1; ++tz)
        for (int64_t tx = tx0; tx <= tx1; ++tx) {
            const double dx = std::max({tx * size - eye.x, 0.0, eye.x - (tx + 1) * size});
            const double dz = std::max({tz * size - eye.z, 0.0, eye.z - (tz + 1) * size});
            if (dx * dx + dz * dz > viewDist * viewDist) continue;
            selectTile(TerrainKey{top, tx, tz}, eye, eyeAbove, draw_, want);
        }
    // Erzeugen, wichtigste zuerst; nicht mehr gleichzeitig, als Hilfs-Threads da sind (das Bild soll flüssig bleiben)
    std::sort(want.begin(), want.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const size_t maxInFlight = std::max<size_t>(4, jobs::threads() * 2);
    for (const auto& [prio, key] : want) {
        if (inFlight_ >= maxInFlight) break;
        Tile& t = tiles_[key];
        if (t.job) continue;
        const int64_t s = 1ll << key.level;
        EditMap e = editsFor(key.x * N * s - s, key.z * N * s - s, (key.x + 1) * N * s + s, (key.z + 1) * N * s + s);
        t.job = nextJob_++;
        t.dirty = false;
        ++inFlight_;
        jobs::submit([gen = gen_, e = std::move(e), key, job = t.job, q = queue_]() {
            Result r;
            r.key = key;
            r.job = job;
            generateTile(*gen, e, key, true, r);
            std::lock_guard<std::mutex> lock(q->m);
            q->done.push_back(std::move(r));
        });
    }
    // Alte Kacheln freigeben
    const double t = now();
    for (auto it = tiles_.begin(); it != tiles_.end();) {
        if (it->second.job == 0 && t - it->second.lastUsed > kKeepTiles) {
            renderer_->destroyMesh(it->second.mesh);
            it = tiles_.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================ Physik

BodyHandle Terrain::buildBody(int64_t cx, int64_t cz) {
    PROFILE("terrain.body");
    std::vector<float> hs((size_t)kBodySamples * kBodySamples);
    auto it = tiles_.find(TerrainKey{0, cx, cz});
    if (it != tiles_.end() && it->second.ready && !it->second.dirty) {  // die Kachel hat die Höhen schon
        for (int j = 0; j < kBodySamples; ++j)
            for (int i = 0; i < kBodySamples; ++i)
                hs[(size_t)i + (size_t)kBodySamples * j] = it->second.h[(size_t)(i + 1) + (size_t)M * (j + 1)];
    } else {
        for (int j = 0; j < kBodySamples; ++j)
            for (int i = 0; i < kBodySamples; ++i)
                hs[(size_t)i + (size_t)kBodySamples * j] = (float)latticeHeight(cx * N + i, cz * N + j);
    }
    GenSample gs;
    gen_->sample((double(cx) + 0.5) * kTerrainChunk, (double(cz) + 0.5) * kTerrainChunk, gs);
    return physics_.createHeightField(hs, kBodySamples, glm::dvec2(double(cx) * kTerrainChunk, double(cz) * kTerrainChunk), kTerrainCell, gs.material);
}

void Terrain::updatePhysics(const std::vector<std::pair<glm::dvec3, glm::dvec3>>& boxes) {
    PROFILE("terrain.physics");
    const double t = now();
    for (const auto& [mn, mx] : boxes) {
        int64_t cx0 = (int64_t)std::floor((mn.x - kPhysMargin) / kTerrainChunk), cx1 = (int64_t)std::floor((mx.x + kPhysMargin) / kTerrainChunk);
        int64_t cz0 = (int64_t)std::floor((mn.z - kPhysMargin) / kTerrainChunk), cz1 = (int64_t)std::floor((mx.z + kPhysMargin) / kTerrainChunk);
        if (cx1 - cx0 > 16 || cz1 - cz0 > 16) {  // riesiges Teil: nur um die Mitte
            const int64_t cx = (cx0 + cx1) / 2, cz = (cz0 + cz1) / 2;
            cx0 = cx - 8, cx1 = cx + 8, cz0 = cz - 8, cz1 = cz + 8;
        }
        for (int64_t cz = cz0; cz <= cz1; ++cz)
            for (int64_t cx = cx0; cx <= cx1; ++cx) {
                Body& b = bodies_[chunkKey(cx, cz)];
                if (b.handle == kNoBody) b.handle = buildBody(cx, cz);
                b.lastNeeded = t;
            }
    }
    for (auto it = bodies_.begin(); it != bodies_.end();) {
        if (t - it->second.lastNeeded > kKeepBodies) {
            physics_.destroyBody(it->second.handle);
            it = bodies_.erase(it);
        } else {
            ++it;
        }
    }
}

void Terrain::rebuildBodies(int64_t i0, int64_t j0, int64_t i1, int64_t j1) {
    // Höhenfeld eines Chunks: Punkte cx·32 .. cx·32 + 33
    for (auto& [key, b] : bodies_) {
        int64_t cx, cz;
        chunkOf(key, cx, cz);
        if (cx * N + kBodySamples - 1 < i0 || cx * N > i1 || cz * N + kBodySamples - 1 < j0 || cz * N > j1) continue;
        physics_.destroyBody(b.handle);
        b.handle = buildBody(cx, cz);
    }
}

// ============================================================================ Ändern

void Terrain::markDirty(int64_t i0, int64_t j0, int64_t i1, int64_t j1) {
    for (auto& [key, t] : tiles_) {
        const int64_t s = 1ll << key.level;
        const int64_t a0 = key.x * N * s - s, a1 = (key.x + 1) * N * s + s, b0 = key.z * N * s - s, b1 = (key.z + 1) * N * s + s;
        if (a1 < i0 || a0 > i1 || b1 < j0 || b0 > j1) continue;
        t.dirty = true;
    }
}

void Terrain::afterEdit(int64_t i0, int64_t j0, int64_t i1, int64_t j1) {
    clearCache();
    markDirty(i0, j0, i1, j1);
    rebuildBodies(i0, j0, i1, j1);
    for (int64_t cz = floorDiv(j0, N); cz <= floorDiv(j1, N); ++cz)
        for (int64_t cx = floorDiv(i0, N); cx <= floorDiv(i1, N); ++cx) dirtyRegions_.insert(chunkKey(floorDiv(cx, 32), floorDiv(cz, 32)));
    ++editRev_;
}

void Terrain::raise(double x, double z, double radius, double amount) {
    radius = std::clamp(radius, kTerrainCell, 200.0);
    const int64_t i0 = (int64_t)std::floor((x - radius) / kTerrainCell), i1 = (int64_t)std::ceil((x + radius) / kTerrainCell);
    const int64_t j0 = (int64_t)std::floor((z - radius) / kTerrainCell), j1 = (int64_t)std::ceil((z + radius) / kTerrainCell);
    for (int64_t j = j0; j <= j1; ++j)
        for (int64_t i = i0; i <= i1; ++i) {
            const double d = std::hypot(i * kTerrainCell - x, j * kTerrainCell - z);
            if (d >= radius) continue;
            const double w = 0.5 * (1.0 + std::cos(kPi * d / radius));  // weich auslaufend
            const int64_t cx = floorDiv(i, N), cz = floorDiv(j, N);
            editChunk(cx, cz).d[(i - cx * N) + N * (j - cz * N)] += (float)(amount * w);
        }
    afterEdit(i0, j0, i1, j1);
}

void Terrain::flatten(double x, double z, double radius, double height) {
    radius = std::clamp(radius, kTerrainCell, 200.0);
    const int64_t i0 = (int64_t)std::floor((x - radius) / kTerrainCell), i1 = (int64_t)std::ceil((x + radius) / kTerrainCell);
    const int64_t j0 = (int64_t)std::floor((z - radius) / kTerrainCell), j1 = (int64_t)std::ceil((z + radius) / kTerrainCell);
    for (int64_t j = j0; j <= j1; ++j)
        for (int64_t i = i0; i <= i1; ++i) {
            const double d = std::hypot(i * kTerrainCell - x, j * kTerrainCell - z);
            if (d >= radius) continue;
            // innen ganz eben, zum Rand hin weich in das alte Gelände
            const double w = std::clamp((radius - d) / (0.35 * radius), 0.0, 1.0);
            const double ws = w * w * (3.0 - 2.0 * w);
            const double cur = latticeHeight(i, j);
            const int64_t cx = floorDiv(i, N), cz = floorDiv(j, N);
            editChunk(cx, cz).d[(i - cx * N) + N * (j - cz * N)] += (float)((height - cur) * ws);
        }
    afterEdit(i0, j0, i1, j1);
}

void Terrain::clearEdits() {
    if (edits_.empty()) return;
    for (const auto& [key, e] : edits_) {
        int64_t cx, cz;
        chunkOf(key, cx, cz);
        dirtyRegions_.insert(chunkKey(floorDiv(cx, 32), floorDiv(cz, 32)));
    }
    edits_.clear();
    clearCache();
    for (auto& [k, t] : tiles_) t.dirty = true;
    for (auto& [k, b] : bodies_) physics_.destroyBody(b.handle);
    bodies_.clear();
    ++editRev_;
}

std::vector<std::pair<uint64_t, TerrainEdit>> Terrain::allEdits() const {
    std::vector<std::pair<uint64_t, TerrainEdit>> out;
    for (const auto& [key, e] : edits_) out.emplace_back(key, *e);
    return out;
}

void Terrain::setEdits(const std::vector<std::pair<uint64_t, TerrainEdit>>& list) {
    edits_.clear();
    for (const auto& [key, e] : list) edits_[key] = std::make_shared<TerrainEdit>(e);
    clearCache();
    for (auto& [k, t] : tiles_) t.dirty = true;
    for (auto& [k, b] : bodies_) physics_.destroyBody(b.handle);
    bodies_.clear();
    ++editRev_;
}

// ============================================================================ Regionsdateien

// regions/r.<rx>.<rz>.bin: "WTRE", Version 1, Punkte je Chunk (32), Anzahl, je Chunk: cx, cz (int32), 32·32 float
void Terrain::loadRegions() {
    std::error_code ec;
    if (regionDir_.empty() || !std::filesystem::is_directory(regionDir_, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(regionDir_, ec)) {
        const std::string name = entry.path().filename().u8string();
        long long rx = 0, rz = 0;
        if (std::sscanf(name.c_str(), "r.%lld.%lld.bin", &rx, &rz) != 2) continue;
        std::ifstream in(entry.path(), std::ios::binary);
        char magic[4] = {};
        uint32_t version = 0, cells = 0, count = 0;
        in.read(magic, 4);
        in.read(reinterpret_cast<char*>(&version), 4);
        in.read(reinterpret_cast<char*>(&cells), 4);
        in.read(reinterpret_cast<char*>(&count), 4);
        if (!in || std::memcmp(magic, "WTRE", 4) != 0 || version != 1 || cells != (uint32_t)N || count > 32 * 32) {
            std::fprintf(stderr, "terrain: %s is not a region file, ignored\n", entry.path().u8string().c_str());
            continue;
        }
        for (uint32_t k = 0; k < count; ++k) {
            int32_t cx = 0, cz = 0;
            auto e = std::make_shared<TerrainEdit>();
            in.read(reinterpret_cast<char*>(&cx), 4);
            in.read(reinterpret_cast<char*>(&cz), 4);
            in.read(reinterpret_cast<char*>(e->d), sizeof(e->d));
            if (!in) break;
            edits_[chunkKey(cx, cz)] = e;
        }
    }
}

bool Terrain::save(std::string* err) {
    if (regionDir_.empty() || dirtyRegions_.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(regionDir_, ec);
    std::unordered_map<uint64_t, std::vector<uint64_t>> byRegion;
    for (const auto& [key, e] : edits_) {
        int64_t cx, cz;
        chunkOf(key, cx, cz);
        const uint64_t rk = chunkKey(floorDiv(cx, 32), floorDiv(cz, 32));
        if (!dirtyRegions_.count(rk)) continue;
        bool any = false;  // ganz ohne Änderung (alles 0): nicht speichern
        for (float v : e->d) any |= v != 0.0f;
        if (any) byRegion[rk].push_back(key);
    }
    for (uint64_t rk : dirtyRegions_) {
        int64_t rx, rz;
        chunkOf(rk, rx, rz);
        const std::filesystem::path file = regionDir_ / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".bin");
        auto it = byRegion.find(rk);
        if (it == byRegion.end()) {  // keine Änderungen mehr in dieser Region
            std::filesystem::remove(file, ec);
            continue;
        }
        std::filesystem::path tmp = file;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            const uint32_t version = 1, cells = N, count = (uint32_t)it->second.size();
            out.write("WTRE", 4);
            out.write(reinterpret_cast<const char*>(&version), 4);
            out.write(reinterpret_cast<const char*>(&cells), 4);
            out.write(reinterpret_cast<const char*>(&count), 4);
            for (uint64_t key : it->second) {
                int64_t cx, cz;
                chunkOf(key, cx, cz);
                const int32_t x = (int32_t)cx, z = (int32_t)cz;
                out.write(reinterpret_cast<const char*>(&x), 4);
                out.write(reinterpret_cast<const char*>(&z), 4);
                out.write(reinterpret_cast<const char*>(edits_[key]->d), sizeof(TerrainEdit::d));
            }
            if (!out) {
                if (err) *err = "cannot write " + tmp.u8string();
                return false;
            }
        }
        if (!worlds::replaceFile(tmp, file, err)) return false;
    }
    dirtyRegions_.clear();
    return true;
}

// ============================================================================ Text

std::string Terrain::statusText() const {
    std::ostringstream ss;
    const GenConfig& c = gen_->config();
    ss << "terrain       seed " << gen_->seed() << ", ";
    if (gen_->singleBiome() >= 0) ss << "only biome " << c.biomes[(size_t)gen_->singleBiome()].id;
    else {
        ss << "biomes";
        for (const GenConfig::Biome& b : c.biomes) ss << " " << b.id;
    }
    ss << "\n";
    size_t ready = 0;
    for (const auto& [k, t] : tiles_) ready += t.ready;
    ss << "tiles         " << draw_.size() << " drawn, " << ready << " ready, " << missing_ << " missing, " << inFlight_ << " being generated ("
       << jobs::threads() << " threads)\n";
    ss << "physics       " << bodies_.size() << " chunk(s) of 16 m with collision\n";
    ss << "edits         " << edits_.size() << " chunk(s) changed, " << dirtyRegions_.size() << " region file(s) to save";
    return ss.str();
}
