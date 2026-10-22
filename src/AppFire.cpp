// Wärme und Feuer im Fenster: Werkzeug (Brenner, Kältespray), Flammen, Rauch, Funken, Kältenebel, Feuerschein und die
// Temperaturgitter für den Shader (Glühen, Verkohlung, Anlauffarben, Reif).
#include "App.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

#include "Materials.h"

namespace {

std::mt19937& rng() {
    static std::mt19937 r(2024);
    return r;
}
float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng()); }

// fester Zufallswert für eine Stelle (Flammen springen nicht von Bild zu Bild)
float hashPos(const glm::dvec3& p) {
    const double v = std::sin(std::floor(p.x * 50.0) * 12.9898 + std::floor(p.y * 50.0) * 78.233 + std::floor(p.z * 50.0) * 37.719) * 43758.5453;
    return (float)(v - std::floor(v));
}

// Rechteck aus zwei Dreiecken: unten Mitte c, nach oben "up", halbe Breite "right"
void quad(std::vector<LineVertex>& out, const glm::vec3& c, const glm::vec3& up, const glm::vec3& right, float strength, int kind, float seed) {
    const float w = kind * 10.0f + std::clamp(seed, 0.0f, 0.999f);
    const LineVertex bl{c - right, {0, 0, strength, w}}, br{c + right, {1, 0, strength, w}};
    const LineVertex tl{c - right + up, {0, 1, strength, w}}, tr{c + right + up, {1, 1, strength, w}};
    out.insert(out.end(), {bl, br, tr, bl, tr, tl});
}

} // namespace

// ============================================================================ Werkzeug

void App::heatToolInput(float) {
    heatActive_ = false;
    if (netClient()) return;  // die Wärme rechnet der Gastgeber
    ImGuiIO& io = ImGui::GetIO();
    const RayHit* hit = nullptr;
    if (!play_) {
        if (tool_ == Tool::Heat && hoverValid_ && hoverHit_.obj && !io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Left)) hit = &hoverHit_;
    } else if (cursorCaptured_ && !io.WantTextInput && glfwGetKey(window_, GLFW_KEY_F) == GLFW_PRESS && lookHit_.obj &&
               world_->find(lookAt_) == lookHit_.obj && lookHit_.t < 3.0) {
        hit = &lookHit_;
    }
    if (!hit) return;
    heatTool_ = std::clamp(heatTool_, 0, (int)heatTools().size() - 1);
    world_->addHeatSource(hit->obj->id, hit->localPoint, heatTool_, 0.15);
    heatActive_ = true;
    heatTarget_ = hit->obj->id;
    heatPoint_ = hit->point;
    heatNormal_ = hit->normal;
    heatFrom_ = hit->point + hit->normal * 0.12;
}

// ============================================================================ Partikel

void App::updateFx(float dt) {
    dt = std::min(dt, 0.1f);
    const glm::vec3 wind = glm::vec3(world_->wind());
    // Rauch und Funken aus den Feuern
    for (const Fire& F : world_->fires()) {
        const double ub = 2.5 * std::sqrt(F.height + 0.1);
        const glm::dvec3 axis = glm::normalize(glm::dvec3(0, ub, 0) + glm::dvec3(wind) * 0.8);
        const double kw = F.power / 1000.0;
        float smoke = (float)std::min(30.0, 1.5 + kw * (1.0 + 6.0 * F.soot)) * dt;
        while (smoke > 0) {
            if (smoke < 1 && rnd(0, 1) > smoke) break;
            smoke -= 1;
            FxParticle p;
            p.kind = 1;
            const double r = 0.3 * F.diameter;
            p.pos = F.base + axis * (F.height * rnd(0.55f, 0.9f)) + glm::dvec3(rnd(-1, 1) * r, 0, rnd(-1, 1) * r);
            p.vel = glm::vec3(axis * std::min(ub * 0.22, 0.7)) + glm::vec3(rnd(-0.1f, 0.1f), 0, rnd(-0.1f, 0.1f));
            p.life = rnd(4.0f, 7.0f);
            p.size = (float)std::max(0.5 * F.diameter, 0.1);
            p.grow = rnd(3.0f, 4.5f);
            p.strength = (float)std::clamp((0.12 + 0.33 * F.soot) * std::min(1.0, 0.4 + kw / 3.0), 0.05, 0.5);
            p.seed = (float)(0.55 - 0.5 * F.soot);  // Helligkeit: Holzrauch grau, Gummirauch schwarz
            fx_.push_back(p);
        }
        float sparks = kw > 1.5 ? (float)std::min(15.0, kw / 3.0) * dt : 0.0f;
        while (sparks > 0 && !F.spots.empty()) {
            if (sparks < 1 && rnd(0, 1) > sparks) break;
            sparks -= 1;
            FxParticle p;
            p.kind = 2;
            p.pos = F.spots[rng()() % F.spots.size()].pos;
            p.vel = glm::vec3(axis * (double)rnd(1.0f, 3.5f)) + glm::vec3(rnd(-0.5f, 0.5f), 0, rnd(-0.5f, 0.5f));
            p.life = rnd(0.7f, 2.0f);
            p.size = rnd(0.003f, 0.007f);
            p.grow = 0.6f;
            p.strength = rnd(0.6f, 1.2f);
            fx_.push_back(p);
        }
    }
    // Kältenebel am Kältespray / Stickstoff
    if (heatActive_ && !heatTools()[heatTool_].flame) {
        float n = 30.0f * dt;
        while (n > 0) {
            if (n < 1 && rnd(0, 1) > n) break;
            n -= 1;
            FxParticle p;
            p.kind = 3;
            p.pos = heatPoint_ + heatNormal_ * 0.02;
            p.vel = glm::vec3(heatNormal_ * 0.25) + glm::vec3(rnd(-0.25f, 0.25f), rnd(-0.2f, 0.05f), rnd(-0.25f, 0.25f));
            p.life = rnd(1.0f, 2.0f);
            p.size = 0.04f;
            p.grow = 3.5f;
            p.strength = heatTool_ == 4 ? 0.9f : 0.5f;
            p.seed = rnd(0, 1);
            fx_.push_back(p);
        }
    }
    // bewegen
    for (FxParticle& p : fx_) {
        p.age += dt;
        p.pos += glm::dvec3(p.vel * dt);
        if (p.kind == 1) {
            p.vel += (wind - glm::vec3(p.vel.x, 0, p.vel.z)) * glm::vec3(0.6f, 0, 0.6f) * dt;  // treibt mit dem Wind
            p.vel.y *= 1.0f - 0.25f * dt;
        } else if (p.kind == 2) {
            p.vel.y -= 1.5f * dt;  // Funken kühlen ab und fallen
            p.vel += (wind - p.vel) * 0.3f * dt;
        } else {
            p.vel *= 1.0f - 1.5f * dt;
            p.vel.y -= 0.4f * dt;  // kalte Luft sinkt
        }
    }
    fx_.erase(std::remove_if(fx_.begin(), fx_.end(), [](const FxParticle& p) { return p.age >= p.life; }), fx_.end());
    if (fx_.size() > 1500) fx_.erase(fx_.begin(), fx_.begin() + (fx_.size() - 1500));
}

// ============================================================================ Zeichnen

void App::addFireFx(FrameDesc& fd) {
    if (views_.empty()) return;
    const glm::dvec3 O = renderOrigin_;
    const glm::dvec3 eye = views_[0].eye;
    const float t = (float)time_;
    struct Light {
        glm::vec3 pos, color;
        float strength;
    };
    std::vector<Light> lights;
    auto billboard = [&](const glm::dvec3& p, const glm::dvec3& up, glm::dvec3& right) {
        const glm::dvec3 view = views_[0].ortho ? glm::normalize(views_[0].target - views_[0].eye) : glm::normalize(p - eye);
        right = glm::cross(up, view);
        if (glm::length(right) < 1e-6) right = glm::dvec3(1, 0, 0);
        right = glm::normalize(right);
    };

    // ---- Rauch (hinten zuerst), dann leuchtende Partikel
    std::vector<const FxParticle*> order;
    for (const FxParticle& p : fx_) order.push_back(&p);
    std::sort(order.begin(), order.end(),
              [&](const FxParticle* a, const FxParticle* b) { return glm::length(a->pos - eye) > glm::length(b->pos - eye); });
    for (const FxParticle* p : order) {
        const float k = p->age / p->life;
        const float size = p->size * (1.0f + (p->grow - 1.0f) * k);
        const float fade = std::min(1.0f, k / 0.04f) * (1.0f - k);
        glm::dvec3 right;
        const glm::dvec3 view = glm::normalize(p->pos - eye);
        billboard(p->pos, glm::dvec3(0, 1, 0), right);
        const glm::dvec3 up = glm::normalize(glm::cross(view, right));
        const glm::vec3 c = glm::vec3(p->pos - O - up * (double)size);
        quad(fd.fire, c, glm::vec3(up) * (2.0f * size), glm::vec3(right) * size, p->strength * fade, p->kind, p->seed);
    }

    // ---- Flammen
    for (const Fire& F : world_->fires()) {
        const double ub = 2.5 * std::sqrt(F.height + 0.1);
        const glm::dvec3 axis = glm::normalize(glm::dvec3(0, ub, 0) + world_->wind() * 0.8);
        const double kw = F.power / 1000.0;
        // Je brennende Stelle eine Flammenzunge (höchstens 24). Viele übereinander: einzeln schwächer, sonst wird es weiß
        const size_t n = F.spots.size(), step = std::max<size_t>(1, n / 24);
        const float overlap = 1.0f / std::sqrt(std::max(1.0f, (float)(n / step) / 3.0f));
        for (size_t i = 0; i < n; i += step) {
            const Fire::Spot& s = F.spots[i];
            const float seed = hashPos(s.pos);
            const double h = std::clamp(F.height * (0.4 + 0.45 * seed) * (0.9 + 0.1 * std::sin(t * 6.0 + seed * 20.0)), 2.0 * s.size, 1.1 * F.height);
            glm::dvec3 right;
            billboard(s.pos, axis, right);
            const double w = std::max(1.3 * s.size, 0.04);
            const float strength = (float)std::clamp(0.5 + s.power / 600.0, 0.5, 1.2) * overlap;
            quad(fd.fire, glm::vec3(s.pos - O - axis * (0.3 * s.size)), glm::vec3(axis * h), glm::vec3(right * w), strength, 0, seed);
        }
        if (n > 6 || kw > 4) {  // großes Feuer: ein paar große Flammenzungen über allem
            for (int k = 0; k < 3; ++k) {
                glm::dvec3 right;
                billboard(F.base, axis, right);
                const glm::dvec3 r = glm::angleAxis(glm::radians(60.0 * k), axis) * right;
                const double h = F.height * (1.0 + 0.08 * std::sin(t * 4.3 + k));
                quad(fd.fire, glm::vec3(F.base - O), glm::vec3(axis * h), glm::vec3(r * (0.6 * F.diameter)), 0.6f, 0, 0.13f + 0.31f * k);
            }
        }
        const float flick = 0.82f + 0.12f * std::sin(t * 9.1f + (float)F.base.x * 3.0f) + 0.06f * std::sin(t * 23.0f);
        lights.push_back({glm::vec3(F.base + axis * (0.35 * F.height) - O), glm::vec3(1.0f, 0.47f, 0.15f), (float)(0.05 + 0.08 * std::sqrt(kw)) * flick});
    }

    // ---- Brenner bzw. Kältestrahl
    if (heatActive_) {
        const HeatTool& tool = heatTools()[heatTool_];
        if (tool.flame) {
            const glm::dvec3 jet = heatPoint_ - heatFrom_;
            glm::dvec3 right;
            billboard(heatFrom_, glm::normalize(jet), right);
            if (heatTool_ == 0) {  // Feuerzeug: kleine gelbe Flamme an der Stelle
                billboard(heatPoint_, glm::dvec3(0, 1, 0), right);
                quad(fd.fire, glm::vec3(heatPoint_ - O - glm::dvec3(0, 0.02, 0)), glm::vec3(0, 0.07f, 0), glm::vec3(right * 0.012), 0.9f, 0, 0.4f);
            } else {
                const double w = heatTool_ == 2 ? 0.008 : 0.016;
                quad(fd.fire, glm::vec3(heatFrom_ - O), glm::vec3(jet), glm::vec3(right * w), 1.0f, 4, 0.7f);
                billboard(heatPoint_, glm::dvec3(0, 1, 0), right);  // Flammen lecken an der Auftreffstelle hoch
                quad(fd.fire, glm::vec3(heatPoint_ - O - glm::dvec3(0, 0.01, 0)), glm::vec3(0, heatTool_ == 2 ? 0.05f : 0.09f, 0),
                     glm::vec3(right * 0.03), 0.8f, 0, 0.2f);
            }
            lights.push_back({glm::vec3(heatPoint_ + heatNormal_ * 0.05 - O), heatTool_ == 2 ? glm::vec3(0.75f, 0.85f, 1.0f) : glm::vec3(1.0f, 0.6f, 0.3f),
                              heatTool_ == 0 ? 0.01f : 0.04f});
        }
    }

    // ---- stärkste Lichter an den Shader
    std::sort(lights.begin(), lights.end(), [](const Light& a, const Light& b) { return a.strength > b.strength; });
    for (size_t i = 0; i < lights.size() && i < (size_t)kMaxLights; ++i) {
        fd.lightPos.push_back(lights[i].pos);
        fd.lightColor.push_back(lights[i].color * lights[i].strength);
    }
}

// Temperaturgitter eines Teils für den Shader, nur wenn man etwas sieht (Glühen, Kohle, Anlauffarben, Reif, Schmelze)
void App::addHeatData(FrameDesc& fd, DrawItem& it, const Object& o) {
    const HeatGrid& g = o.heat;
    if (g.empty() || g.cells.empty()) return;
    const bool metal = materials()[o.material].shading == Shading::Metal;
    float peak = -1e9f, chr = 0, melt = 0, tMin = 1e9f, tMax = -1e9f;
    for (const HeatCell& c : g.cells) {
        if (!c.inside) continue;
        peak = std::max(peak, c.peak);
        chr = std::max(chr, c.chr);
        melt = std::max(melt, c.melt);
        tMin = std::min(tMin, c.temp);
        tMax = std::max(tMax, c.temp);
    }
    if (!(tMax > 420 || chr > 0.003f || melt > 0 || tMin < -2 || (metal && peak > 190))) return;
    if (fd.heatCells.size() + g.cells.size() > 1000000) return;
    const size_t base = fd.heatCells.size();
    for (int i = 0; i < (int)g.cells.size(); ++i) {
        const HeatCell& c = g.cells[i];
        if (c.inside) {
            fd.heatCells.emplace_back(c.temp, c.peak, c.chr, c.melt);
            continue;
        }
        // leere Zelle: Mittel der Nachbarn mit Material (sonst zieht die Mittelung den Rand zur Umgebung)
        const int x = i % g.dim.x, y = (i / g.dim.x) % g.dim.y, z = i / (g.dim.x * g.dim.y);
        glm::vec4 sum(0);
        int n = 0;
        const int d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const auto& k : d) {
            const int a = x + k[0], b = y + k[1], e = z + k[2];
            if (a < 0 || b < 0 || e < 0 || a >= g.dim.x || b >= g.dim.y || e >= g.dim.z) continue;
            const HeatCell& q = g.cells[g.index(a, b, e)];
            if (!q.inside) continue;
            sum += glm::vec4(q.temp, q.peak, q.chr, q.melt);
            ++n;
        }
        fd.heatCells.push_back(n ? sum / (float)n : glm::vec4(o.heatInfo.tMean, peak, 0, 0));
    }
    it.data.heat0 = glm::vec4(glm::vec3(g.origin), (float)base);
    it.data.heat1 = glm::vec4(glm::vec3(g.cell), (float)(g.dim.x + 64 * g.dim.y + 4096 * g.dim.z));
}
