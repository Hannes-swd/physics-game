// Wasser im Fenster: Gewässer für die Shader (Wellen, Farbe, Lichtflecken), Spritzer und Dampf, Schwimmen der Figur.
#include "App.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "Profile.h"
#include "Renderer.h"
#include "Terrain.h"

namespace {

std::mt19937& rng() {
    static std::mt19937 r(4711);
    return r;
}
float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng()); }

} // namespace

// Die nächsten Gewässer (höchstens kMaxWater) an die Shader. Wellen-Phasen in doppelter Genauigkeit: enthalten den
// Ursprung und die Zeit, damit die Wellen auch Kilometer vom Weltursprung entfernt genau dort sind, wo die Physik sie sieht
void App::addWaterData(FrameDesc& fd) {
    PROFILE("app.water");
    const auto& list = world_->waters();
    if (list.empty()) return;
    const glm::dvec3 O = renderOrigin_;
    const glm::dvec3 eye = views_.empty() ? O : views_[0].eye;
    world_->setWaterFocus(play_ ? eye : orbitTarget_);  // Meer: dort wird die Strömung gerechnet
    std::vector<std::pair<double, const WaterBody*>> order;
    for (const WaterBody& b : list) {
        if (b.shape && !(world_->terrain() && world_->terrain()->waterInView())) continue;  // Flüsse: keiner zu sehen
        const double d = b.sea ? 0.0 : glm::length(glm::clamp(eye, b.lo, b.hi) - eye);
        if (d < 5000.0) order.emplace_back(d, &b);
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (order.size() > (size_t)kMaxWater) order.resize(kMaxWater);
    fd.waveDir = glm::vec2(world_->waveDirection());
    const double t = world_->waterTime();
    for (const auto& [d, b] : order) {
        const WaveSet ws = world_->wavesOf(*b);
        const Liquid& L = liquids()[b->liquid];
        WaterGpu g;
        g.lo = glm::vec4(glm::vec3(b->lo - O), (float)ws.lambda0);
        g.hi = glm::vec4(glm::vec3(b->hi - O), ws.calm ? 0.0f : (float)b->waves);
        if (b->sea) {
            g.hi.y = (float)(b->level() - O.y);
            // Standardgrund. Flüsse: außerhalb des Fensters tief, dort zeigt die Tiefenprüfung das Wasser nur, wo das
            // Gelände unter dem Spiegel liegt (in den Flussbetten)
            g.lo.y = (float)((b->shape ? b->level() - 1000.0 : b->ground ? b->ground->base : b->bottomAt(0, 0)) - O.y);
            // Fenster der Grundkarte um die Kamera (128 m, in 16-m-Schritten): neu nur, wenn es wandert oder der Grund sich ändert
            constexpr int N = kWaterGroundCells;
            const glm::dvec2 origin = glm::floor(glm::dvec2(eye.x, eye.z) / 16.0) * 16.0 - N / 2.0;
            auto it = std::find_if(groundWindows_.begin(), groundWindows_.end(), [&](const GroundWindow& gw) { return gw.water == b->id; });
            if (it == groundWindows_.end()) it = groundWindows_.insert(groundWindows_.end(), GroundWindow{b->id, 0, glm::dvec2(1e300), true, {}});
            GroundWindow& gw = *it;
            const uint64_t rev = b->shape ? b->shape->revision() : b->ground ? b->ground->rev : 0;
            if (gw.rev != rev || gw.origin != origin) {
                PROFILE("app.waterGround");
                gw.rev = rev;
                gw.origin = origin;
                double lo = 0, hi = 0;
                gw.flat = !b->shape && (!b->ground || (b->ground->range(origin.x, origin.y, origin.x + N, origin.y + N, lo, hi) && lo == b->ground->base));
                gw.cells.clear();
                if (!gw.flat) {
                    gw.cells.resize((size_t)N * N / 4);
                    for (int z = 0; z < N; ++z)
                        for (int x = 0; x < N; ++x) {
                            const size_t i = (size_t)x + (size_t)N * z;
                            // Flüsse: trocken = knapp über dem Spiegel (die Shader mitteln benachbarte Zellen)
                            gw.cells[i / 4][(int)(i % 4)] = (float)std::min(b->bottomAt(origin.x + x + 0.5, origin.y + z + 0.5), b->level() + 2.0);
                        }
                }
            }
            if (!gw.flat) {
                g.ground = glm::vec4((float)(origin.x - O.x), (float)(origin.y - O.z), (float)-O.y, (float)fd.waterCells.size());
                fd.waterCells.insert(fd.waterCells.end(), gw.cells.begin(), gw.cells.end());
            }
        }
        float ph[4] = {0, 0, 0, 0};
        if (!ws.calm)
            for (int i = 0; i < 4; ++i)
                ph[i] = (float)std::fmod(ws.k[i] * (ws.dir[i].x * O.x + ws.dir[i].y * O.z) - ws.omega[i] * t, 2.0 * 3.14159265358979323846);
        g.phase = glm::vec4(ph[0], ph[1], ph[2], ph[3]);
        g.absorb = glm::vec4(L.absorb, b->sea ? 1.0f : 0.0f);
        // Raster der Oberfläche: Meer fein um die Kamera; Becken nur so fein, wie die Wellen es brauchen
        int n = 192;
        if (!b->sea) {
            const double size = std::max(b->hi.x - b->lo.x, b->hi.z - b->lo.z);
            n = ws.calm ? 2 : (int)std::clamp(std::ceil(size / (ws.lambda0 / 8.0)), 4.0, 160.0);
        }
        // Strömungsgitter: Strömung, Schaum, Stau je Zelle. Das Becken braucht dann ein so feines Raster wie das Gitter (Stau)
        g.grid1 = glm::vec4(0, 0, (float)b->flow.x, (float)b->flow.z);
        if (const FlowGrid* fg = world_->flowGrid(b->id)) {
            g.grid0 = glm::vec4((float)(fg->origin.x - O.x), (float)(fg->origin.y - O.z), (float)fg->cell, (float)fd.waterCells.size());
            g.grid1 = glm::vec4((float)fg->nx, (float)fg->nz, (float)fg->base.x, (float)fg->base.y);
            for (size_t i = 0; i < fg->u.size(); ++i) fd.waterCells.emplace_back(fg->u[i].x, fg->u[i].y, fg->foam[i], fg->rise[i]);
            if (!b->sea) n = std::clamp(std::max({n, fg->nx, fg->nz}), 2, 160);
        }
        g.scatter = glm::vec4(L.scatter, (float)n);
        fd.water.push_back(g);
    }
}

// Spritzer (ein Teil schlägt auf) und Dampf (heißes Metall im Wasser) als Partikel
void App::updateWaterFx(float) {
    for (const World::Splash& s : world_->takeSplashes()) {
        const int n = std::clamp((int)(s.speed * s.size * 80.0), 6, 40);
        for (int i = 0; i < n; ++i) {
            FxParticle p;
            p.kind = 6;
            const float a = rnd(0.0f, 6.2832f), r = rnd(0.2f, 1.0f) * (float)s.size * 0.5f;
            p.pos = s.pos + glm::dvec3(std::cos(a) * r, 0.01, std::sin(a) * r);
            const float up = rnd(0.3f, 0.75f) * (float)std::min(s.speed, 8.0), out = rnd(0.2f, 0.6f) * (float)std::min(s.speed, 8.0);
            p.vel = glm::vec3(std::cos(a) * out, up, std::sin(a) * out);
            p.life = up / 9.81f * 2.0f + 0.05f;  // bis es wieder unten ist
            p.size = rnd(0.006f, 0.016f) * (0.7f + (float)s.size);
            p.grow = 0.7f;
            p.strength = rnd(0.6f, 1.0f);
            p.seed = rnd(0, 1);
            fx_.push_back(p);
        }
    }
    for (const World::Steam& s : world_->takeSteam()) {  // je Wärmeschritt (20/s) und heiße Stelle
        float n = (float)std::min(2.0, s.power / 4000.0);
        while (n > 0) {
            if (n < 1 && rnd(0, 1) > n) break;
            n -= 1;
            FxParticle p;
            p.kind = 5;
            p.pos = s.pos + glm::dvec3(rnd(-0.03f, 0.03f), 0.01, rnd(-0.03f, 0.03f));
            p.vel = glm::vec3(rnd(-0.1f, 0.1f), rnd(0.25f, 0.6f), rnd(-0.1f, 0.1f));
            p.life = rnd(1.2f, 2.5f);
            p.size = rnd(0.03f, 0.05f);
            p.grow = 4.0f;
            p.strength = (float)std::clamp(0.25 + s.power / 40000.0, 0.25, 0.7);
            p.seed = rnd(0, 1);
            fx_.push_back(p);
        }
    }
}

float App::swimDepth(const glm::dvec3& feet) const {
    double s = 0;
    if (!world_->waterColumn(feet, s)) return 0.0f;
    return (float)std::clamp((s - feet.y) / 1.7, 0.0, 1.0);  // Figur 1,7 m (bis zu den Augen etwas weniger)
}
