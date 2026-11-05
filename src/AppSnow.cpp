// Spuren im Schnee: Fußabdrücke der Spielfiguren (abwechselnd links und rechts, in Laufrichtung) und Abdrücke von Teilen,
// die auf Schnee liegen oder darüber rutschen (so groß wie ihr Umriss). Schnee ist, wo der Generator Schnee meldet
// (GenSample::snow: Schneewald, Tundra, Gletscher, Gipfel) und der Boden nicht steil ist. Nur Bild: flache, bläuliche
// Mulden knapp über der Oberfläche; jeder Rechner zeichnet sie selbst, nach kLifetime verschwinden sie.
#include "App.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

#include "Materials.h"
#include "Terrain.h"

namespace {

constexpr size_t kMaxPrints = 3000;
constexpr double kLifetime = 600.0;   // s
constexpr double kStride = 0.72;      // m je Schritt
constexpr double kDrawDistance = 90;  // m
constexpr uint32_t kSelfKey = 0xFFFFFFFFu;

// Drehung, die +Y auf n bringt
glm::mat4 alignUp(const glm::vec3& n) {
    const glm::vec3 up(0, 1, 0);
    const glm::vec3 ax = glm::cross(up, n);
    const float s = glm::length(ax);
    if (s < 1e-5f) return glm::mat4(1.0f);
    return glm::rotate(glm::mat4(1.0f), std::atan2(s, glm::dot(up, n)), ax / s);
}

} // namespace

bool App::onSnow(double x, double z, glm::dvec3* ground, glm::vec3* normal) const {
    Terrain* tr = world_->terrain();
    if (!tr) return false;
    GenSample s;
    tr->gen().sample(x, z, s);
    if (s.snow < 0.5f || s.wet) return false;
    glm::dvec3 n;
    const double y = tr->surfaceHeight(x, z, &n);
    if (n.y < 0.8) return false;  // steile Hänge: Fels, kein Schnee
    if (ground) *ground = glm::dvec3(x, y, z);
    if (normal) *normal = glm::vec3(n);
    return true;
}

void App::addSnowPrint(const SnowPrint& p) {
    if (snowPrints_.size() < kMaxPrints) {
        snowPrints_.push_back(p);
        return;
    }
    snowPrints_[snowNext_] = p;  // voll: den ältesten überschreiben
    snowNext_ = (snowNext_ + 1) % kMaxPrints;
}

void App::updateSnow(float) {
    Terrain* tr = world_->terrain();
    if (!tr || &tr->gen() != snowGen_) {  // andere Welt oder neuer Generator: alte Spuren weg
        snowPrints_.clear();
        strides_.clear();
        imprintAt_.clear();
        snowNext_ = 0;
        snowGen_ = tr ? &tr->gen() : nullptr;
        if (!tr) return;
    }
    // alte verschwinden
    const size_t before = snowPrints_.size();
    snowPrints_.erase(std::remove_if(snowPrints_.begin(), snowPrints_.end(), [&](const SnowPrint& p) { return time_ - p.born > kLifetime; }),
                      snowPrints_.end());
    if (snowPrints_.size() != before) snowNext_ = 0;  // wieder Platz: hinten anfügen

    // Fußabdrücke: je Figur am Boden alle kStride ein Abdruck, abwechselnd links und rechts der Laufrichtung
    auto walk = [&](uint32_t key, const glm::dvec3& feet, bool onGround) {
        Stride& s = strides_[key];
        if (!s.init || glm::length(feet - s.last) > 5.0) {  // neu oder versetzt (tp)
            s.init = true;
            s.last = feet;
            return;
        }
        glm::dvec2 d(feet.x - s.last.x, feet.z - s.last.z);
        const double len = glm::length(d);
        if (len < kStride) return;
        if (!onGround) {  // in der Luft: kein Abdruck, aber weiterzählen
            s.last = feet;
            return;
        }
        d /= len;
        s.last = feet;
        s.side = -s.side;
        const glm::dvec2 side(-d.y, d.x);
        const double x = feet.x + side.x * 0.11 * s.side, z = feet.z + side.y * 0.11 * s.side;
        SnowPrint p;
        if (!onSnow(x, z, &p.pos, &p.normal)) return;
        p.yaw = (float)std::atan2(-d.x, -d.y);
        p.side = s.side;
        p.born = time_;
        addSnowPrint(p);
    };
    if (play_) {
        bool onGround = physics_.characterOnGround();
        if (netClient())
            for (const NetPlayer& p : netPlayers_)
                if (p.id == netSelfId_) onGround = p.onGround;
        walk(kSelfKey, netClient() ? netFeet_ : physics_.characterPosition(), onGround);
    }
    if (netPlayers_.size() >= 2)
        for (const NetPlayer& p : netPlayers_)
            if (p.id != netSelfId_ && p.play) walk(p.id, p.feet, p.onGround);

    // Teile: liegt eines auf Schnee (Unterkante an der Oberfläche), drückt es seinen Umriss hinein; neu, wenn es ein Stück
    // weiterrutscht oder -rollt
    const glm::dvec3 eye = play_ ? playerEye() : orbitTarget_;
    std::unordered_map<uint32_t, glm::dvec3> seen;
    world_->grid().forEachNear(eye, 50.0, [&](Chunk& ch) {
        for (uint32_t id : ch.objects) {
            const Object* o = world_->find(id);
            if (!o || o->fixed || id == hand_.grabbed) continue;
            const glm::dvec3 ext = o->wMax - o->wMin;
            if (std::max(ext.x, ext.z) < 0.04 || std::max(ext.x, ext.z) > 6.0) continue;
            const glm::dvec3 c = (o->wMin + o->wMax) * 0.5;
            glm::dvec3 ground;
            glm::vec3 n;
            if (!onSnow(c.x, c.z, &ground, &n) || o->wMin.y - ground.y > 0.05) continue;
            auto it = imprintAt_.find(id);
            const double step = std::clamp(0.4 * std::min(ext.x, ext.z), 0.08, 0.5);
            if (it != imprintAt_.end() && glm::length(glm::dvec2(c.x - it->second.x, c.z - it->second.z)) < step) {
                seen[id] = it->second;
                continue;
            }
            SnowPrint p;
            p.pos = ground;
            p.normal = n;
            p.w = (float)ext.x * 0.9f;
            p.l = (float)ext.z * 0.9f;
            p.born = time_;
            addSnowPrint(p);
            seen[id] = c;
        }
    });
    imprintAt_ = std::move(seen);  // Teile, die nicht mehr auf Schnee liegen, fangen neu an
}

void App::addSnowPrints(FrameDesc& fd) {
    if (snowPrints_.empty()) return;
    const glm::dvec3 O = renderOrigin_;
    const glm::dvec3 eye = views_.empty() ? O : views_[0].eye;
    const glm::mat4 I(1.0f);
    // Mulde: etwas dunkler und bläulicher als der Schnee
    ObjectGpuData d;
    d.color = glm::vec4(0.56f, 0.62f, 0.74f, 0.95f);
    d.color2 = glm::vec4(0.50f, 0.56f, 0.70f, 0.0f);
    d.params = glm::vec4((float)Shading::Plain, 0, 0, 0);
    auto put = [&](const glm::mat4& m) {
        if (fd.items.size() >= kMaxDrawItems) return;
        DrawItem it;
        it.mesh = &world_->boxMesh();
        it.castShadow = false;
        it.data = d;
        it.data.model = m;
        fd.items.push_back(it);
    };
    for (const SnowPrint& p : snowPrints_) {
        if (glm::length(p.pos - eye) > kDrawDistance) continue;
        const glm::mat4 base = glm::translate(I, glm::vec3(p.pos - O)) * alignUp(p.normal) * glm::rotate(I, p.yaw, glm::vec3(0, 1, 0));
        if (p.w > 0) {  // Teil: flache Mulde in seinem Umriss
            put(base * glm::translate(I, glm::vec3(0, 0.003f, 0)) * glm::scale(I, glm::vec3(p.w, 0.01f, p.l)));
        } else {        // Fuß: Sohle vorn (−z ist vorn), Absatz hinten
            put(base * glm::translate(I, glm::vec3(0, 0.003f, -0.06f)) * glm::scale(I, glm::vec3(0.10f, 0.01f, 0.17f)));
            put(base * glm::translate(I, glm::vec3(0, 0.003f, 0.09f)) * glm::scale(I, glm::vec3(0.08f, 0.01f, 0.08f)));
        }
    }
}
