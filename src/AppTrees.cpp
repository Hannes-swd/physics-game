// Bäume im Bild: die des Generators als Vorlagen (gleiches Netz, viele Lagen, gebündelt gezeichnet), dazu die Kronen der
// Baum-Teile (Trees.h)
#include "App.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "Materials.h"
#include "Profile.h"
#include "Trees.h"

namespace {

ObjectGpuData woodData(int material, float seed) {
    const Material& m = materials()[(size_t)std::clamp(material, 0, (int)materials().size() - 1)];
    ObjectGpuData d;
    d.color = glm::vec4(m.color, m.roughness);
    d.color2 = glm::vec4(m.color2, m.metallic);
    d.params = glm::vec4((float)m.shading, seed, 0, 0);
    return d;
}

ObjectGpuData leafData(const TreeType& t, float seed) {
    ObjectGpuData d;
    d.color = glm::vec4(t.leafColor, 0.85f);
    d.color2 = glm::vec4(t.leafColor2, 0.0f);
    d.params = glm::vec4(12, seed, 0, 0);
    return d;
}

} // namespace

bool App::addCrown(FrameDesc& fd, const Object& o, const glm::mat4& model, bool low, bool shadow) {
    if (!o.crown || o.treeType.empty()) return false;
    const TreeLibrary& lib = world_->trees();
    const int ti = lib.find(o.treeType);
    if (ti < 0) return false;
    const TreeModel& m = lib.model(ti, o.treeVariant);
    DrawItem it;
    it.mesh = low ? &m.leafLowMesh : &m.leafMesh;
    it.castShadow = shadow;
    it.data = leafData(lib.config().types[(size_t)ti], o.seed);
    it.data.model = model * glm::translate(glm::mat4(1.0f), glm::vec3(o.treeOrigin)) * glm::scale(glm::mat4(1.0f), glm::vec3(o.treeScale));
    fd.items.push_back(it);
    return true;
}

void App::addTrees(FrameDesc& fd, const std::function<bool(const glm::dvec3&, const glm::dvec3&)>& visible) {
    Forest* forest = world_->forest();
    treesDrawn_ = 0;
    if (!forest || !forest->any()) return;
    PROFILE("app.trees");
    const TreeLibrary& lib = forest->library();
    const TreeConfig& cfg = lib.config();
    const glm::dvec3 O = renderOrigin_;
    glm::dvec3 eye = play_ ? playerEye() : orbitEye();
    for (const View& v : views_)
        if (!v.ortho) {
            eye = v.eye;
            break;
        }
    const glm::dvec2 e2(eye.x, eye.z);
    forest->prepare(e2, cfg.drawDistance, 4);  // fehlende Kacheln nach und nach (die nächsten zuerst)

    // je Netz alle Lagen sammeln, dann hintereinander (der Renderer zeichnet sie in einem Aufruf)
    std::unordered_map<const GpuMesh*, size_t> index;
    struct Bucket {
        const GpuMesh* mesh;
        bool shadow;
        std::vector<ObjectGpuData> data;
    };
    std::vector<Bucket> buckets;
    auto put = [&](const GpuMesh* mesh, bool shadow, const ObjectGpuData& d) {
        if (!mesh->valid()) return;
        auto it = index.find(mesh);
        if (it == index.end()) {
            it = index.emplace(mesh, buckets.size()).first;
            buckets.push_back({mesh, shadow, {}});
        }
        buckets[it->second].data.push_back(d);
    };
    std::vector<int> woodMat((size_t)lib.types());
    for (int t = 0; t < lib.types(); ++t) {
        const int m = findMaterialIndex(cfg.types[(size_t)t].wood.c_str());
        woodMat[(size_t)t] = m >= 0 ? m : materialIndex("oak");
    }
    const double R = cfg.drawDistance, kTile = Forest::kTile;
    const int64_t t0x = (int64_t)std::floor((e2.x - R) / kTile), t1x = (int64_t)std::floor((e2.x + R) / kTile);
    const int64_t t0z = (int64_t)std::floor((e2.y - R) / kTile), t1z = (int64_t)std::floor((e2.y + R) / kTile);
    for (int64_t tz = t0z; tz <= t1z; ++tz)
        for (int64_t tx = t0x; tx <= t1x; ++tx) {
            if (!forest->tileReady(tx, tz)) continue;
            const glm::dvec2 mid((tx + 0.5) * kTile, (tz + 0.5) * kTile);
            if (glm::length(mid - e2) > R + kTile) continue;
            const std::vector<TreeSpot>& spots = forest->tile(tx, tz);
            if (spots.empty()) continue;
            double lo = 1e30, hi = -1e30;
            for (const TreeSpot& s : spots) {
                lo = std::min(lo, s.pos.y);
                hi = std::max(hi, s.pos.y + lib.model(s.type, s.variant).height * s.scale);
            }
            const double pad = 12.0;  // Kronen ragen über die Kachel
            if (!visible(glm::dvec3(tx * kTile - pad, lo - 1.0, tz * kTile - pad), glm::dvec3((tx + 1) * kTile + pad, hi, (tz + 1) * kTile + pad)))
                continue;
            for (const TreeSpot& s : spots) {
                if (world_->treeHidden(s.key)) continue;  // steht gerade als Teil da oder ist gefällt
                const double d = glm::length(s.pos - eye);
                if (d > R) continue;
                const TreeModel& m = lib.model(s.type, s.variant);
                const bool detail = d < cfg.detailDistance;
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(s.pos - O)) *
                                        glm::rotate(glm::mat4(1.0f), s.yaw, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(s.scale));
                const float seed = (float)((s.key * 2654435761ull) % 1000) / 1000.0f;
                ObjectGpuData w = woodData(woodMat[(size_t)s.type], seed);
                w.model = model;
                put(detail ? &m.woodMesh : &m.woodLowMesh, detail, w);
                ObjectGpuData l = leafData(cfg.types[(size_t)s.type], seed);
                l.model = model;
                put(detail ? &m.leafMesh : &m.leafLowMesh, detail, l);
                ++treesDrawn_;
            }
        }
    for (const Bucket& b : buckets)
        for (const ObjectGpuData& d : b.data) {
            if (fd.items.size() >= kMaxDrawItems) return;
            DrawItem it;
            it.mesh = b.mesh;
            it.castShadow = b.shadow;
            it.data = d;
            fd.items.push_back(it);
        }
}
