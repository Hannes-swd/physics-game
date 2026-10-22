#include "Avatar.h"
#include "Json.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// eingebettet beim Bauen (cmake/embed_file.cmake)
const unsigned char kWorkerGlb[] = {
#include "worker.glb.inc"
};

// ---------------------------------------------------------------- glTF-Zugriffe

struct Gltf {
    Json doc;
    const uint8_t* bin = nullptr;
    size_t binSize = 0;

    // Werte eines Accessors als float (count * Komponenten), normierte Ganzzahlen auf 0..1
    bool read(int accessor, std::vector<float>& out, int& comps) const {
        const Json& a = doc["accessors"][(size_t)accessor];
        const Json& bv = doc["bufferViews"][(size_t)a["bufferView"].integer()];
        if (a.type != Json::Object || bv.type != Json::Object || bv["buffer"].integer(0) != 0) return false;
        const std::string& type = a["type"].str;
        comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;
        const int ct = a["componentType"].integer();
        const int csize = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : ct == 5121 || ct == 5120 ? 1 : 0;
        const size_t count = (size_t)a["count"].integer(0);
        if (!comps || !csize) return false;
        const size_t stride = bv.has("byteStride") ? (size_t)bv["byteStride"].integer() : (size_t)(comps * csize);
        const size_t start = (size_t)bv["byteOffset"].integer(0) + (size_t)a["byteOffset"].integer(0);
        if (count && start + (count - 1) * stride + comps * csize > binSize) return false;
        const bool norm = a["normalized"].type == Json::Bool && a["normalized"].num != 0;
        out.resize(count * comps);
        for (size_t i = 0; i < count; ++i)
            for (int c = 0; c < comps; ++c) {
                const uint8_t* q = bin + start + i * stride + c * csize;
                float v = 0;
                switch (ct) {
                case 5126: std::memcpy(&v, q, 4); break;
                case 5125: { uint32_t u; std::memcpy(&u, q, 4); v = (float)u; break; }
                case 5123: { uint16_t u; std::memcpy(&u, q, 2); v = norm ? u / 65535.0f : (float)u; break; }
                case 5122: { int16_t u; std::memcpy(&u, q, 2); v = norm ? std::max(u / 32767.0f, -1.0f) : (float)u; break; }
                case 5121: v = norm ? q[0] / 255.0f : (float)q[0]; break;
                case 5120: v = norm ? std::max((int8_t)q[0] / 127.0f, -1.0f) : (float)(int8_t)q[0]; break;
                }
                out[i * comps + c] = v;
            }
        return true;
    }
};

glm::mat4 compose(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
    return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
}

} // namespace

// ============================================================================ Laden

bool AvatarModel::load(const uint8_t* data, size_t size, float height, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        parts_.clear();
        return false;
    };
    *this = AvatarModel();
    height_ = height;

    // GLB: Kopf (12 Byte), dann Blöcke JSON und BIN
    uint32_t header[3];
    if (size < 20) return fail("file too small");
    std::memcpy(header, data, 12);
    if (header[0] != 0x46546C67u || header[1] != 2) return fail("not a glTF 2 binary (.glb)");
    Gltf g;
    const uint8_t* json = nullptr;
    size_t jsonSize = 0;
    for (size_t at = 12; at + 8 <= size;) {
        uint32_t len, type;
        std::memcpy(&len, data + at, 4);
        std::memcpy(&type, data + at + 4, 4);
        if (at + 8 + len > size) return fail("broken chunk");
        if (type == 0x4E4F534Au) json = data + at + 8, jsonSize = len;
        if (type == 0x004E4942u) g.bin = data + at + 8, g.binSize = len;
        at += 8 + ((len + 3) & ~3u);
    }
    if (!json || !g.bin) return fail("JSON or BIN chunk missing");
    if (!parseJson((const char*)json, (const char*)json + jsonSize, g.doc)) return fail("JSON unreadable");
    const Json& doc = g.doc;

    // Knoten (Ruhelage) und Reihenfolge Eltern vor Kindern
    const Json& jn = doc["nodes"];
    nodes_.resize(jn.size());
    for (size_t i = 0; i < jn.size(); ++i) {
        const Json& n = jn[i];
        Node& nd = nodes_[i];
        if (n.has("matrix")) {
            glm::mat4 m;
            for (int k = 0; k < 16; ++k) m[k / 4][k % 4] = n["matrix"][(size_t)k].number();
            glm::vec3 skew;
            glm::vec4 persp;
            glm::decompose(m, nd.s, nd.r, nd.t, skew, persp);
        }
        if (n.has("translation")) nd.t = {n["translation"][0].number(), n["translation"][1].number(), n["translation"][2].number()};
        if (n.has("rotation"))
            nd.r = glm::quat(n["rotation"][3].number(1), n["rotation"][0].number(), n["rotation"][1].number(), n["rotation"][2].number());
        if (n.has("scale")) nd.s = {n["scale"][0].number(1), n["scale"][1].number(1), n["scale"][2].number(1)};
    }
    for (size_t i = 0; i < jn.size(); ++i)
        for (const Json& c : jn[i]["children"].arr)
            if (c.integer() >= 0 && c.integer() < (int)nodes_.size()) nodes_[c.integer()].parent = (int)i;
    std::vector<bool> done(nodes_.size(), false);
    for (size_t pass = 0; pass < nodes_.size() && order_.size() < nodes_.size(); ++pass)
        for (size_t i = 0; i < nodes_.size(); ++i)
            if (!done[i] && (nodes_[i].parent < 0 || done[nodes_[i].parent])) {
                done[i] = true;
                order_.push_back((int)i);
            }
    if (order_.size() != nodes_.size()) return fail("node hierarchy has a cycle");
    for (size_t i = 0; i < jn.size(); ++i) {
        names_.push_back(jn[i]["name"].str);
        const std::string& name = names_.back();
        if (foot_ < 0 && name.find("Foot") != std::string::npos && name.find("_end") == std::string::npos) foot_ = (int)i;
    }

    // Netze: Häute (oder starr an ihrem Knoten), nach Material gesammelt
    const Json& mats = doc["materials"];
    std::vector<std::vector<uint32_t>> byMaterial(mats.size() + 1);
    for (size_t ni = 0; ni < jn.size(); ++ni) {
        const Json& n = jn[ni];
        if (!n.has("mesh")) continue;
        const Json& mesh = doc["meshes"][(size_t)n["mesh"].integer()];
        // Gelenke dieser Haut hinten anhängen; ohne Haut hängt das Netz starr an seinem Knoten
        const uint16_t jointBase = (uint16_t)joints_.size();
        if (n.has("skin")) {
            const Json& skin = doc["skins"][(size_t)n["skin"].integer()];
            std::vector<float> ibm;
            int comps = 0;
            bool hasIbm = skin.has("inverseBindMatrices") && g.read(skin["inverseBindMatrices"].integer(), ibm, comps) && comps == 16;
            for (size_t k = 0; k < skin["joints"].size(); ++k) {
                joints_.push_back(skin["joints"][k].integer(0));
                glm::mat4 m(1.0f);
                if (hasIbm && (k + 1) * 16 <= ibm.size()) std::memcpy(&m[0][0], &ibm[k * 16], sizeof(m));
                inverseBind_.push_back(m);
            }
        } else {
            joints_.push_back((int)ni);
            inverseBind_.push_back(glm::mat4(1.0f));
        }
        for (const Json& prim : mesh["primitives"].arr) {
            if (prim["mode"].integer(4) != 4) continue;  // nur Dreiecke
            const Json& at = prim["attributes"];
            std::vector<float> pos, nrm, jnt, wgt, idx;
            int cp = 0, cn = 0, cj = 0, cw = 0, ci = 0;
            if (!g.read(at["POSITION"].integer(), pos, cp) || cp != 3) continue;
            const size_t count = pos.size() / 3;
            bool hasN = g.read(at["NORMAL"].integer(), nrm, cn) && cn == 3 && nrm.size() == pos.size();
            bool skinned = n.has("skin") && g.read(at["JOINTS_0"].integer(), jnt, cj) && cj == 4 && g.read(at["WEIGHTS_0"].integer(), wgt, cw) &&
                           cw == 4 && jnt.size() == count * 4 && wgt.size() == count * 4;
            const uint32_t base = (uint32_t)vertices_.size();
            for (size_t v = 0; v < count; ++v) {
                SkinVertex sv{};
                sv.p = {pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]};
                sv.n = hasN ? glm::vec3(nrm[v * 3], nrm[v * 3 + 1], nrm[v * 3 + 2]) : glm::vec3(0, 1, 0);
                float sum = 0;
                for (int k = 0; k < 4; ++k) {
                    sv.joint[k] = (uint16_t)(jointBase + (skinned ? (uint16_t)jnt[v * 4 + k] : 0));
                    sv.weight[k] = skinned ? wgt[v * 4 + k] : (k == 0 ? 1.0f : 0.0f);
                    if (sv.joint[k] >= joints_.size()) sv.joint[k] = jointBase, sv.weight[k] = 0;
                    sum += sv.weight[k];
                }
                if (sum <= 0) sv.weight[0] = 1, sum = 1;
                for (float& w : sv.weight) w /= sum;
                vertices_.push_back(sv);
            }
            std::vector<uint32_t> tri;
            if (prim.has("indices") && g.read(prim["indices"].integer(), idx, ci) && ci == 1) {
                for (float f : idx) tri.push_back(base + (uint32_t)f);
            } else {
                for (uint32_t v = 0; v < count; ++v) tri.push_back(base + v);
            }
            tri.resize(tri.size() / 3 * 3);
            for (uint32_t& t : tri)
                if (t >= vertices_.size()) t = base;
            if (!hasN)  // Flächennormalen
                for (size_t t = 0; t + 2 < tri.size(); t += 3) {
                    glm::vec3 fn = glm::cross(vertices_[tri[t + 1]].p - vertices_[tri[t]].p, vertices_[tri[t + 2]].p - vertices_[tri[t]].p);
                    for (int k = 0; k < 3; ++k) vertices_[tri[t + k]].n = glm::length(fn) > 0 ? glm::normalize(fn) : glm::vec3(0, 1, 0);
                }
            int mat = prim["material"].integer((int)mats.size());
            if (mat < 0 || mat > (int)mats.size()) mat = (int)mats.size();
            byMaterial[mat].insert(byMaterial[mat].end(), tri.begin(), tri.end());
        }
    }
    for (size_t m = 0; m < byMaterial.size(); ++m) {
        if (byMaterial[m].empty()) continue;
        Part part;
        if (m < mats.size()) {
            part.material = mats[m]["name"].str;
            const Json& c = mats[m]["pbrMetallicRoughness"]["baseColorFactor"];
            if (c.size() >= 3) part.color = {c[0].number(1), c[1].number(1), c[2].number(1)};
        }
        part.first = (uint32_t)triangles_.size();
        part.count = (uint32_t)byMaterial[m].size();
        triangles_.insert(triangles_.end(), byMaterial[m].begin(), byMaterial[m].end());
        parts_.push_back(part);
    }
    if (parts_.empty()) return fail("no triangles");

    // Abläufe
    for (const Json& an : doc["animations"].arr) {
        Clip clip;
        clip.name = an["name"].str;
        for (const Json& ch : an["channels"].arr) {
            const Json& smp = an["samplers"][(size_t)ch["sampler"].integer()];
            const std::string& path = ch["target"]["path"].str;
            Channel c;
            c.node = ch["target"]["node"].integer();
            c.path = path == "translation" ? 0 : path == "rotation" ? 1 : path == "scale" ? 2 : -1;
            if (c.path < 0 || c.node < 0 || c.node >= (int)nodes_.size()) continue;
            std::vector<float> in, out;
            int ci = 0, co = 0;
            if (!g.read(smp["input"].integer(), in, ci) || ci != 1 || !g.read(smp["output"].integer(), out, co)) continue;
            const std::string& interp = smp["interpolation"].str;
            c.step = interp == "STEP";
            const bool cubic = interp == "CUBICSPLINE";  // Tangenten auslassen, nur die Werte nehmen
            const size_t keys = in.size();
            if (keys == 0 || out.size() != keys * co * (cubic ? 3 : 1) || co != (c.path == 1 ? 4 : 3)) continue;
            c.times = in;
            for (size_t k = 0; k < keys; ++k) {
                const float* v = &out[(cubic ? k * 3 + 1 : k) * co];
                c.values.push_back(co == 4 ? glm::vec4(v[0], v[1], v[2], v[3]) : glm::vec4(v[0], v[1], v[2], 0));
            }
            clip.length = std::max(clip.length, in.back());
            clip.channels.push_back(std::move(c));
        }
        clips_.push_back(std::move(clip));
    }

    // Ausrichtung: glTF blickt nach +z, das Spiel nach -z; Sohle auf 0, gewünschte Höhe
    std::vector<glm::mat4> global;
    evaluate(nullptr, 0, nullptr, 0, global);
    rest_ = global;
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const SkinVertex& v : vertices_) {
        glm::vec4 p(0);
        for (int k = 0; k < 4; ++k) p += v.weight[k] * (global[joints_[v.joint[k]]] * inverseBind_[v.joint[k]] * glm::vec4(v.p, 1));
        lo = glm::min(lo, glm::vec3(p));
        hi = glm::max(hi, glm::vec3(p));
    }
    if (!(hi.y > lo.y)) return fail("model has no height");
    const float scale = height_ / (hi.y - lo.y);
    fit_ = glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0, 1, 0)) *
           glm::translate(glm::mat4(1.0f), -glm::vec3(0.5f * (lo.x + hi.x), lo.y, 0.5f * (lo.z + hi.z)));
    unfit_ = glm::inverse(glm::mat3(fit_)) * scale;  // nur Drehung

    // Schrittgeschwindigkeit je Ablauf: wie schnell gleitet ein aufgesetzter Fuß unter der Figur weg?
    // (in jede Richtung, damit auch Seitwärts- und Rückwärtslaufen passen)
    for (size_t ci = 0; ci < clips_.size() && foot_ >= 0; ++ci) {
        Clip& c = clips_[ci];
        if (c.length <= 0) continue;
        const int n = 60;
        std::vector<glm::vec3> foot(n + 1);
        for (int i = 0; i <= n; ++i) {
            Layer l{(int)ci, c.length * i / n, 1.0f};
            evaluate(&l, 1, nullptr, 0, global);
            foot[i] = glm::vec3(fit_ * global[foot_][3]);
        }
        float low = 1e30f, high = -1e30f;
        for (int i = 0; i < n; ++i) {
            low = std::min(low, foot[i].y);
            if (foot[i].y > high) high = foot[i].y, c.sync = (float)i / n;
        }
        double sum = 0, time = 0;
        const float dt = c.length / n;
        for (int i = 0; i < n; ++i)
            if (foot[i].y < low + 0.03f && foot[i + 1].y < low + 0.03f) {
                sum += glm::length(glm::vec2(foot[i + 1].x - foot[i].x, foot[i + 1].z - foot[i].z));
                time += dt;
            }
        c.groundSpeed = time > 0 ? (float)(sum / time) : 0.0f;
    }
    return true;
}

bool AvatarModel::loadBuiltin(float height, std::string* error) { return load(kWorkerGlb, sizeof(kWorkerGlb), height, error); }

int AvatarModel::clip(const std::string& name) const {
    for (size_t i = 0; i < clips_.size(); ++i) {
        const std::string& n = clips_[i].name;
        size_t bar = n.rfind('|');
        if (n == name || (bar != std::string::npos && n.compare(bar + 1, std::string::npos, name) == 0)) return (int)i;
    }
    return -1;
}

float AvatarModel::clipLength(int clip) const { return clip >= 0 && clip < (int)clips_.size() ? clips_[clip].length : 0.0f; }

int AvatarModel::node(const std::string& name) const {
    for (size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == name) return (int)i;
    return -1;
}

glm::vec3 AvatarModel::restPosition(int node) const {
    return node >= 0 && node < (int)rest_.size() ? glm::vec3(fit_ * rest_[node][3]) : glm::vec3(0.0f);
}

void AvatarModel::setFollow(int follower, int leader) {
    if (follower >= 0 && leader >= 0 && follower < (int)nodes_.size() && leader < (int)nodes_.size() && follower != leader)
        follows_.emplace_back(follower, leader);
}

// ============================================================================ Lage

void AvatarModel::evaluate(const Layer* layers, int count, const Bend* bends, int bendCount, std::vector<glm::mat4>& global) const {
    const size_t n = nodes_.size();
    std::vector<glm::vec3> t(n), s(n);
    std::vector<glm::quat> r(n);
    float total = 0;
    for (int l = 0; l < count; ++l)
        if (layers[l].clip >= 0 && layers[l].clip < (int)clips_.size() && layers[l].weight > 0) total += layers[l].weight;
    if (total <= 0) {
        for (size_t i = 0; i < n; ++i) t[i] = nodes_[i].t, r[i] = nodes_[i].r, s[i] = nodes_[i].s;
    } else {
        // jede Schicht für sich auswerten, dann gewichtet mischen (Drehungen auf dieselbe Halbkugel)
        std::vector<glm::vec3> lt(n), ls(n);
        std::vector<glm::quat> lr(n);
        std::vector<glm::vec4> racc(n, glm::vec4(0));
        std::fill(t.begin(), t.end(), glm::vec3(0));
        std::fill(s.begin(), s.end(), glm::vec3(0));
        for (int l = 0; l < count; ++l) {
            const Layer& L = layers[l];
            if (L.clip < 0 || L.clip >= (int)clips_.size() || L.weight <= 0) continue;
            const Clip& c = clips_[L.clip];
            const float w = L.weight / total;
            float time = c.length > 0 ? std::fmod(L.time, c.length) : 0.0f;
            if (time < 0) time += c.length;
            for (size_t i = 0; i < n; ++i) lt[i] = nodes_[i].t, lr[i] = nodes_[i].r, ls[i] = nodes_[i].s;
            for (const Channel& ch : c.channels) {
                size_t k = std::upper_bound(ch.times.begin(), ch.times.end(), time) - ch.times.begin();
                glm::vec4 v;
                if (k == 0) {
                    v = ch.values.front();
                } else if (k >= ch.times.size()) {
                    v = ch.values.back();
                } else {
                    const float t0 = ch.times[k - 1], t1 = ch.times[k];
                    const float f = ch.step || t1 <= t0 ? 0.0f : (time - t0) / (t1 - t0);
                    const glm::vec4 a = ch.values[k - 1], b = ch.values[k];
                    if (ch.path == 1) {
                        glm::quat q = glm::slerp(glm::quat(a.w, a.x, a.y, a.z), glm::quat(b.w, b.x, b.y, b.z), f);
                        v = glm::vec4(q.x, q.y, q.z, q.w);
                    } else {
                        v = a + (b - a) * f;
                    }
                }
                if (ch.path == 0) lt[ch.node] = glm::vec3(v);
                if (ch.path == 1) lr[ch.node] = glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
                if (ch.path == 2) ls[ch.node] = glm::vec3(v);
            }
            for (size_t i = 0; i < n; ++i) {
                t[i] += w * lt[i];
                s[i] += w * ls[i];
                glm::vec4 q(lr[i].x, lr[i].y, lr[i].z, lr[i].w);
                const glm::quat& ref = nodes_[i].r;
                if (glm::dot(q, glm::vec4(ref.x, ref.y, ref.z, ref.w)) < 0) q = -q;
                racc[i] += w * q;
            }
        }
        for (size_t i = 0; i < n; ++i) {
            const glm::vec4& q = racc[i];
            r[i] = glm::dot(q, q) > 1e-12f ? glm::normalize(glm::quat(q.w, q.x, q.y, q.z)) : nodes_[i].r;
        }
    }

    std::vector<glm::mat4> local(n);
    for (size_t i = 0; i < n; ++i) local[i] = compose(t[i], r[i], s[i]);
    // Knochen um sein Gelenk drehen (Achse aus dem Raum der Figur ins Modell zurückgerechnet)
    auto bend = [&](int i, glm::mat4& g) {
        for (int b = 0; b < bendCount; ++b)
            if (bends[b].node == i && bends[b].angle != 0.0f) {
                const glm::vec3 pivot(g[3]);
                const glm::vec3 axis = glm::normalize(unfit_ * bends[b].axis);
                g = glm::translate(glm::mat4(1.0f), pivot) * glm::mat4_cast(glm::angleAxis(bends[b].angle, axis)) *
                    glm::translate(glm::mat4(1.0f), -pivot) * g;
            }
    };
    auto pass = [&](std::vector<glm::mat4>& g, bool withBends) {
        g.assign(n, glm::mat4(1.0f));
        for (int i : order_) {
            g[i] = (nodes_[i].parent >= 0 ? g[nodes_[i].parent] : glm::mat4(1.0f)) * local[i];
            if (withBends) bend(i, g[i]);
        }
    };
    pass(global, bends && bendCount > 0);
    if (follows_.empty() || !bends || bendCount <= 0) return;

    // Folger (z.B. Fuß-Ziel unter der Wurzel) machen die Bewegung mit, die die Beugungen ihrem Führer gegeben haben
    std::vector<glm::mat4> plain;
    pass(plain, false);
    std::vector<bool> moved(n, false);
    for (const auto& [f, l] : follows_) {
        global[f] = global[l] * glm::inverse(plain[l]) * plain[f];
        bend(f, global[f]);
        moved[f] = true;
    }
    for (int i : order_)  // Kinder der Folger nachziehen
        if (!moved[i] && nodes_[i].parent >= 0 && moved[nodes_[i].parent]) {
            global[i] = global[nodes_[i].parent] * local[i];
            moved[i] = true;
        }
}

void AvatarModel::pose(const Layer* layers, int count, const Bend* bends, int bendCount, std::vector<solid::Vertex>& out) const {
    std::vector<glm::mat4> global;
    evaluate(layers, count, bends, bendCount, global);
    std::vector<glm::mat4> skin(joints_.size());
    for (size_t j = 0; j < joints_.size(); ++j) skin[j] = fit_ * global[joints_[j]] * inverseBind_[j];
    std::vector<solid::Vertex> moved(vertices_.size());
    for (size_t i = 0; i < vertices_.size(); ++i) {
        const SkinVertex& v = vertices_[i];
        glm::mat4 m = v.weight[0] * skin[v.joint[0]];
        for (int k = 1; k < 4; ++k)
            if (v.weight[k] > 0) m += v.weight[k] * skin[v.joint[k]];
        const glm::vec3 p(m * glm::vec4(v.p, 1));
        glm::vec3 nn(m * glm::vec4(v.n, 0));
        const float len = glm::length(nn);
        nn = len > 1e-12f ? nn / len : glm::vec3(0, 1, 0);
        moved[i] = {p.x, p.y, p.z, nn.x, nn.y, nn.z};
    }
    const size_t base = out.size();
    out.resize(base + triangles_.size());
    for (size_t i = 0; i < triangles_.size(); ++i) out[base + i] = moved[triangles_[i]];
}
