// Tiere im Spiel (Animals.h): Modelle laden, jedes Bild bewegen und zeichnen, Befehl "animals" (siehe docs/24-Tiere.md)
#include "App.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "Materials.h"
#include "Profile.h"
#include "Terrain.h"

namespace {

float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

void addTri(std::vector<solid::Vertex>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    glm::vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    n = len > 1e-9f ? n / len : glm::vec3(0, 1, 0);
    // beide Seiten (dünne Flügel; beim geschlossenen Körper fällt die innere weg)
    out.push_back({a.x, a.y, a.z, n.x, n.y, n.z});
    out.push_back({b.x, b.y, b.z, n.x, n.y, n.z});
    out.push_back({c.x, c.y, c.z, n.x, n.y, n.z});
    out.push_back({a.x, a.y, a.z, -n.x, -n.y, -n.z});
    out.push_back({c.x, c.y, c.z, -n.x, -n.y, -n.z});
    out.push_back({b.x, b.y, b.z, -n.x, -n.y, -n.z});
}

// Low-Poly-Vogel mit Spannweite 1, Blick nach -z, y oben. flap: Phase 0..1, amount: 0 gleitet .. 1 schlägt.
// range[k] = (erster Punkt, Anzahl) für Körper, Flügel, Schnabel
void buildBird(std::vector<solid::Vertex>& out, float flap, float amount, bool wingsOnly, uint32_t range[3][2]) {
    using glm::vec3;
    const float theta = 0.08f + amount * 0.7f * std::sin(flap * 6.2831853f);
    auto wing = [&](float side) {
        const float c = std::cos(theta), s = std::sin(theta), c2 = std::cos(theta * 1.5f), s2 = std::sin(theta * 1.5f);
        const float sx = 0.04f;
        auto inner = [&](float x, float z) { return vec3(side * (sx + (x - sx) * c), (x - sx) * s, z); };
        const vec3 ef = inner(0.25f, -0.05f), eb = inner(0.25f, 0.07f);
        auto outer = [&](float x, float z) {  // um den Ellbogen weiter gedreht
            const float dx = x - 0.25f;
            return vec3(ef.x + side * dx * c2, ef.y + dx * s2, z);
        };
        addTri(out, inner(sx, -0.07f), inner(sx, 0.05f), eb);
        addTri(out, inner(sx, -0.07f), eb, ef);
        addTri(out, ef, eb, outer(0.5f, 0.11f));
        addTri(out, ef, outer(0.5f, 0.11f), outer(0.5f, 0.04f));
    };
    const vec3 N(0, 0, -0.20f), T(0, 0.01f, 0.20f), L(-0.045f, 0, 0), R(0.045f, 0, 0), U(0, 0.045f, -0.02f), D(0, -0.04f, 0);
    range[0][0] = (uint32_t)out.size();
    if (!wingsOnly) {
        addTri(out, N, U, L), addTri(out, N, R, U), addTri(out, N, L, D), addTri(out, N, D, R);
        addTri(out, T, L, U), addTri(out, T, U, R), addTri(out, T, D, L), addTri(out, T, R, D);
    }
    range[0][1] = (uint32_t)out.size() - range[0][0];
    range[1][0] = (uint32_t)out.size();
    wing(1.0f);
    wing(-1.0f);
    if (!wingsOnly) addTri(out, vec3(0, 0.005f, 0.15f), vec3(-0.075f, 0, 0.31f), vec3(0.075f, 0, 0.31f));  // Schwanz
    range[1][1] = (uint32_t)out.size() - range[1][0];
    range[2][0] = (uint32_t)out.size();
    if (!wingsOnly) {
        const vec3 tip(0, -0.008f, -0.27f), a(-0.02f, 0, -0.19f), b(0.02f, 0, -0.19f), u(0, 0.025f, -0.19f), d(0, -0.02f, -0.19f);
        addTri(out, tip, a, u), addTri(out, tip, u, b), addTri(out, tip, b, d), addTri(out, tip, d, a);
    }
    range[2][1] = (uint32_t)out.size() - range[2][0];
}

} // namespace

void App::loadAnimalLooks() {
    animalLooks_.clear();
    if (!animals_) return;
    const AnimalConfig& cfg = animals_->config();
    for (const AnimalSpecies& s : cfg.species) {
        AnimalLook look;
        if (s.model != "bird") {
            auto it = animalModels_.find(s.model);
            if (it == animalModels_.end()) {  // jede Datei einmal laden (Höhe 1 m, die Art skaliert)
                auto m = std::make_unique<AvatarModel>();
                const std::string path = findAnimalModel(s.model);
                std::string err = "file not found in assets/animals";
                bool ok = false;
                if (!path.empty()) {
                    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
                    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    ok = m->load(data.data(), data.size(), 1.0f, &err);
                }
                if (!ok) {
                    log("Animal model '" + s.model + "' could not be loaded: " + err, 2);
                    m.reset();
                }
                it = animalModels_.emplace(s.model, std::move(m)).first;
            }
            look.model = it->second.get();
        }
        if (AvatarModel* m = look.model) {
            if (!s.foot.empty()) m->setFootNode(m->node(s.foot));
            for (const std::string& c : s.idleClips)
                if (m->clip(c) >= 0) look.idle.push_back(m->clip(c));
            if (look.idle.empty()) look.idle.push_back(m->clip("Idle"));
            look.walk = m->clip(s.walkClip);
            look.run = m->clip(s.runClip);
            if (look.run == look.walk) look.run = -1;
            look.fly = s.flyClip.empty() ? -1 : m->clip(s.flyClip);
            look.attack = s.attackClip.empty() ? -1 : m->clip(s.attackClip);
            // Sterben: aus der Datei, sonst ein Ablauf namens "Death" oder "..._Death" (Rat_Death, Spider_Death)
            look.death = s.deathClip.empty() ? -1 : m->clip(s.deathClip);
            for (int c = 0; look.death < 0 && c < m->clipCount(); ++c) {
                const std::string& n = m->clipName(c);
                const std::string tail = n.substr(n.rfind('|') == std::string::npos ? 0 : n.rfind('|') + 1);
                if (tail == "Death" || (tail.size() > 6 && tail.compare(tail.size() - 6, 6, "_Death") == 0)) look.death = c;
            }
            // Schrittweite aus dem Modell (m/s bei Abspieltempo 1), sonst die Geschwindigkeit aus der Datei
            const float gw = m->clipGroundSpeed(look.walk) * s.height, gr = m->clipGroundSpeed(look.run) * s.height;
            look.walkPace = gw > 0.05f ? gw : s.walkSpeed;
            look.runPace = gr > 0.05f ? gr : s.runSpeed;
            for (const AvatarModel::Part& p : m->parts()) {
                glm::vec3 c = p.color;
                for (const auto& [mat, col] : s.tint)
                    if (mat == p.material) c = col;
                look.colors.push_back(c * s.shade);
            }
        }
        animalLooks_.push_back(std::move(look));
    }
}

void App::updateAnimals(float dt) {
    const Terrain* tr = world_->terrain();
    if (!tr || !showAnimals_) {
        if (animals_) animals_->clear();
        return;
    }
    PROFILE("app.animals");
    if (!animals_) {
        animals_ = std::make_unique<AnimalWorld>();
        std::string report;
        animals_->setConfig(loadAnimalsFile(&report));
        if (!report.empty()) log(report, 2);
        loadAnimalLooks();
    }
    if (pause_ != Pause::None) dt = 0;  // Pausenfenster: alles steht
    // um wen sie erscheinen, und wer sie aufschreckt
    glm::dvec3 focus, view;
    std::vector<AnimalScarer> scarers;
    if (play_) {
        focus = netClient() ? netFeet_ : physics_.characterPosition();
        view = playerForward();
    } else {
        focus = orbitEye();
        view = glm::dvec3(0, 0, -1);
        for (const View& v : views_)
            if (!v.ortho) {
                focus = v.eye;
                view = v.target - v.eye;
                break;
            }
    }
    double speed = 0;
    if (dt > 0 && animalFocusInit_) speed = glm::length(glm::dvec2(focus.x - animalFocus_.x, focus.z - animalFocus_.z)) / dt;
    animalFocusSpeed_ += (speed - animalFocusSpeed_) * (1.0 - std::exp(-dt / 0.3));
    animalFocus_ = focus;
    animalFocusInit_ = true;
    // wer gebissen werden kann: nur beim Gastgeber (bzw. allein), lebende Figuren, Verletzungen an
    std::vector<uint32_t> who;  // je Eintrag: 0 = eigene Figur, sonst Nummer des Mitspielers
    if (play_) {
        scarers.push_back({focus, animalFocusSpeed_, !netClient() && healthOn_ && me_.deadUntil < 0});
        who.push_back(0);
    } else if (focus.y - tr->surfaceHeight(focus.x, focus.z) < 3.0) {  // Bau-Modus: nur eine Kamera dicht am Boden
        scarers.push_back({focus, animalFocusSpeed_});
        who.push_back(UINT32_MAX);
    }
    for (const NetPlayer& p : netPlayers_) {
        if (p.id == netSelfId_ || !p.play) continue;
        const Peer* peer = netRole_ == NetRole::Host ? findPeer(p.id) : nullptr;
        scarers.push_back({p.feet, 2.0, peer && healthOn_ && peer->vitals.deadUntil < 0});
        who.push_back(p.id);
    }
    animals_->update(dt, *tr, focus, view, scarers, dayHours_, settings_.viewDistance);
    // Bisse und Stöße: auf die eigene Figur oder (als Gastgeber) auf einen Mitspieler
    for (const AnimalBite& b : animals_->takeBites()) {
        if (b.target < 0 || b.target >= (int)who.size()) continue;
        const uint32_t id = who[(size_t)b.target];
        if (id == 0 && play_) animalBite(me_, 0, playYaw_, b.species, b.from);
        else if (Peer* peer = id != 0 && id != UINT32_MAX ? findPeer(id) : nullptr) animalBite(peer->vitals, (int)id, peer->yaw, b.species, b.from);
    }
    animalDt_ = dt;
}

void App::addAnimals(FrameDesc& fd, const std::function<bool(const glm::dvec3&, const glm::dvec3&)>& visible) {
    animalsDrawn_ = 0;
    if (!animals_ || !showAnimals_ || !world_->terrain()) return;
    PROFILE("app.animals.draw");
    const glm::dvec3 O = renderOrigin_;
    glm::dvec3 eye = play_ ? playerEye() : orbitEye();
    double pxPerM = 800;  // Pixel je Meter in 1 m Abstand
    for (const View& v : views_)
        if (!v.ortho) {
            eye = v.eye;
            pxPerM = v.h * 0.5 / std::tan(fov_ / 2);
            break;
        }
    const float dt = animalDt_;
    const glm::mat4 I(1.0f);
    const AnimalConfig& cfg = animals_->config();
    std::vector<AvatarModel::Layer> layers;
    for (Animal& a : animals_->animals()) {
        if (a.species < 0 || a.species >= (int)animalLooks_.size()) continue;
        const AnimalSpecies& s = cfg.species[(size_t)a.species];
        const AnimalLook& look = animalLooks_[(size_t)a.species];
        if (a.dead && look.death < 0) a.roll = std::min(a.deadTime * 3.0f, 1.5f);  // ohne Sterbe-Ablauf: auf die Seite kippen
        // Abläufe weiterzählen (auch wenn es gerade nicht zu sehen ist)
        // Gehen -> Rennen überblenden; die Schritte folgen der Geschwindigkeit, damit die Hufe nicht rutschen
        const float run = look.run >= 0 ? smooth01((a.speed - s.walkSpeed * 1.3f) / std::max(s.runSpeed * 0.55f - s.walkSpeed * 1.3f, 0.1f)) : 0.0f;
        const float move = smooth01(a.speed / std::max(s.walkSpeed * 0.5f, 0.05f));
        if (look.model) {
            const float lw = look.model->clipLength(look.walk), lr = look.model->clipLength(look.run);
            const float stride = (1 - run) * look.walkPace * std::max(lw, 0.1f) + run * (look.runPace * std::max(lr, 0.1f));
            // schneller als der Ablauf: längere Sprünge statt nur schnellerer Beine (wie die Spielfigur)
            const float natural = (1 - run) * look.walkPace + run * look.runPace;
            const float cadence = a.speed <= natural ? a.speed : natural * std::sqrt(a.speed / natural);
            if (stride > 1e-3f) a.phase += cadence * dt / stride;
            a.phase -= std::floor(a.phase);
        }

        const double dist = glm::length(a.pos - eye);
        const double size = s.model == "bird" ? s.height * 0.5 : s.height;
        const double px = size / std::max(dist, 0.1) * pxPerM;
        if (px < 0.7) continue;  // kleiner als ein Pixel
        const double r = std::max(s.height, s.wingspan) * 0.8 + 0.2;
        if (!visible(a.pos - glm::dvec3(r, 0.2, r), a.pos + glm::dvec3(r, s.height * 1.2 + 0.2, r))) continue;
        if (fd.items.size() >= 7800) break;
        ++animalsDrawn_;

        const bool bird = s.model == "bird";
        const float scale = s.height;
        glm::mat4 model = glm::translate(I, glm::vec3(a.pos - O)) * glm::rotate(I, a.yaw, glm::vec3(0, 1, 0)) *
                          glm::rotate(I, a.pitch, glm::vec3(1, 0, 0)) * glm::rotate(I, -a.roll, glm::vec3(0, 0, 1));
        const bool shadow = dist < 60;
        auto addPart = [&](uint32_t first, uint32_t count, const glm::vec3& c, const glm::mat4& m) {
            if (!count) return;
            DrawItem it;
            it.frameFirst = first;
            it.frameCount = count;
            it.castShadow = shadow;
            it.data.model = m;
            it.data.color = glm::vec4(c, 0.85f);
            it.data.color2 = glm::vec4(c * 0.85f, 0.0f);
            it.data.params = glm::vec4((float)Shading::Plain, 0, 0, 0);
            fd.items.push_back(it);
        };
        if (bird || (a.airborne && s.wingspan > 0)) {
            uint32_t range[3][2];
            buildBird(fd.frameVertices, a.flap, a.flapAmount, !bird, range);
            const float span = bird ? scale : s.wingspan;
            const glm::mat4 m = bird ? model * glm::scale(I, glm::vec3(span))
                                     : model * glm::translate(I, glm::vec3(0, scale * 0.5f, 0)) * glm::scale(I, glm::vec3(span));
            addPart(range[0][0], range[0][1], s.body, m);
            addPart(range[1][0], range[1][1], s.wing, m);
            addPart(range[2][0], range[2][1], s.beak, m);
            if (bird) continue;
        }
        if (!look.model) continue;

        // Lage neu rechnen: nah jedes Bild, fern seltener (die Haut wird auf der CPU verformt)
        const double every = px > 140 ? 0.0 : px > 50 ? 1.0 / 30 : px > 15 ? 1.0 / 12 : 1.0 / 5;
        if (a.pose.empty() || time_ - a.poseTime >= every || time_ < a.poseTime) {
            a.poseTime = time_;
            layers.clear();
            const float still = 1.0f - move;
            if (a.dead && look.death >= 0) {  // tot: Sterbe-Ablauf einmal, dann liegen bleiben
                layers.push_back({look.death, std::min(a.deadTime, look.model->clipLength(look.death) - 0.001f), 1.0f});
            } else if (a.airborne && look.fly < 0) {
                layers.push_back({look.idle[0], a.idleTime, 1.0f});
            } else if (look.fly >= 0 && a.airborne) {
                layers.push_back({look.fly, a.idleTime * 1.4f, 1.0f});
            } else {
                const int cur = look.idle[(size_t)std::clamp(a.idleClip, 0, (int)look.idle.size() - 1)];
                const int last = look.idle[(size_t)std::clamp(a.lastIdle, 0, (int)look.idle.size() - 1)];
                layers.push_back({cur, a.idleTime, still * a.idleBlend});
                if (last != cur) layers.push_back({last, a.idleTime, still * (1.0f - a.idleBlend)});
                auto cycle = [&](int clip, float w) {
                    if (clip < 0 || w <= 0) return;
                    float p = a.phase + look.model->clipSync(clip);
                    p -= std::floor(p);
                    layers.push_back({clip, p * look.model->clipLength(clip), w});
                };
                cycle(look.walk, move * (1.0f - run));
                cycle(look.run, move * run);
                // Biss/Stoß: Angriffs-Ablauf einmal über alles andere legen (sanft ein- und ausblenden)
                if (look.attack >= 0 && a.attackAnim >= 0) {
                    const float len = std::max(look.model->clipLength(look.attack), 0.1f), t = a.attackAnim;
                    const float w = t >= len ? 0.0f : std::min({1.0f, t / 0.08f, (len - t) / 0.15f});
                    if (w > 0) {
                        for (AvatarModel::Layer& l : layers) l.weight *= 1.0f - w;
                        layers.push_back({look.attack, t, w});
                    }
                }
            }
            a.pose.clear();
            look.model->pose(layers.data(), (int)layers.size(), nullptr, 0, a.pose);
        }
        const uint32_t base = (uint32_t)fd.frameVertices.size();
        fd.frameVertices.insert(fd.frameVertices.end(), a.pose.begin(), a.pose.end());
        const glm::mat4 m = model * glm::scale(I, glm::vec3(scale));
        const std::vector<AvatarModel::Part>& parts = look.model->parts();
        for (size_t k = 0; k < parts.size(); ++k) addPart(base + parts[k].first, parts[k].count, look.colors[k], m);
    }
}

std::string App::animalsCommand(const std::vector<std::string>& t) {
    const std::string sub = t.size() > 1 ? t[1] : "";
    const Terrain* tr = world_->terrain();
    if (sub == "on" || sub == "off") {
        showAnimals_ = sub == "on";
        if (!showAnimals_ && animals_) animals_->clear();
        return std::string("animals ") + (showAnimals_ ? "on" : "off");
    }
    if (!tr) return "Error: animals live only in worlds with terrain";
    if (!animals_) updateAnimals(0);
    // Jagd per Befehl: das nächste Tier bis 3 m anvisieren und schlagen bzw. ein totes zerlegen
    if (sub == "hit" || sub == "harvest") {
        if (!play_ || netClient()) return "Error: only for the own figure in play mode";
        const bool dead = sub == "harvest";
        const glm::dvec3 eye = playerEye();
        const Animal* best = nullptr;
        double bestD = 3.0;
        for (const Animal& a : animals_->animals()) {
            const AnimalSpecies& s = animals_->config().species[(size_t)a.species];
            if (a.dead != dead || a.airborne || s.model == "bird") continue;
            const double d = glm::length(glm::dvec2(a.pos.x - eye.x, a.pos.z - eye.z));
            if (d < bestD) bestD = d, best = &a;
        }
        if (!best) {
            double nearest = 1e9;
            for (const Animal& a : animals_->animals())
                if (a.dead == dead) nearest = std::min(nearest, glm::length(glm::dvec2(a.pos.x - eye.x, a.pos.z - eye.z)));
            return std::string(dead ? "no dead animal" : "no animal") + " within 3 m" + (nearest < 1e8 ? " (nearest " + fmtNum(nearest, 1) + " m)" : "");
        }
        const AnimalSpecies& s = animals_->config().species[(size_t)best->species];
        const glm::dvec3 aim = best->pos + glm::dvec3(0, dead ? std::min(0.25, s.height * 0.3) : s.height * 0.5, 0) - eye;
        if (dead) return animalHarvest(eye, aim) ? "harvested " + s.id : "Error: could not harvest";
        HandTool tool = handTool_;
        const std::string w = t.size() > 2 ? t[2] : "";
        if (w == "hand") tool = HandTool::None;
        if (w == "axe") tool = HandTool::Axe;
        if (w == "pickaxe") tool = HandTool::Pickaxe;
        if (w == "laser") tool = HandTool::Laser;
        const uint32_t id = best->id;
        std::string msg;
        if (!animalStrike(eye, aim, tool, &msg)) return "Error: missed";
        const Animal* a = animals_->find(id);
        return s.id + (a && a->dead ? " killed" : " hit, strength " + fmtNum(a ? a->hp * 100.0 : 0.0, 0) + " %") + (msg.empty() ? "" : " (" + msg + ")");
    }
    if (!animals_) return "Error: animals are off ('animals on')";
    if (sub.empty() || sub == "status") return animals_->status(animalFocus_);
    if (sub == "reload") {
        std::string report;
        std::shared_ptr<const AnimalConfig> cfg = loadAnimalsFile(&report);
        if (!report.empty()) return "Error: " + report;
        animals_->setConfig(cfg);
        loadAnimalLooks();
        const std::string path = findAnimalsFile();
        return "animals reloaded from " + (path.empty() ? std::string("the built-in copy") : path) + " (" + std::to_string(cfg->species.size()) +
               " species)";
    }
    if (sub == "clear") {
        animals_->clear();
        return "animals cleared (new ones appear around you)";
    }
    if (sub == "list") {
        std::ostringstream o;
        const AnimalConfig& cfg = animals_->config();
        for (size_t i = 0; i < cfg.species.size(); ++i) {
            const AnimalSpecies& s = cfg.species[i];
            o << (i ? "\n" : "") << s.id << "  " << s.label << "  (" << s.model << ", visible " << (int)s.visible << " m)";
            if (i < animalLooks_.size() && s.model != "bird" && !animalLooks_[i].model) o << "  MODEL MISSING";
        }
        return o.str();
    }
    if (sub == "info" && t.size() > 2) {
        const int si = animals_->config().findSpecies(t[2]);
        if (si < 0 || si >= (int)animalLooks_.size()) return "Error: unknown animal '" + t[2] + "' ('animals list')";
        const AnimalSpecies& s = animals_->config().species[(size_t)si];
        const AnimalLook& look = animalLooks_[(size_t)si];
        std::ostringstream o;
        o << s.id << " (" << s.label << "): model " << s.model << ", height " << s.height << " m";
        if (look.model) {
            o << ", " << look.model->triangleCount() << " triangles, clips:";
            for (int c = 0; c < look.model->clipCount(); ++c) o << " " << look.model->clipName(c);
            o << "\nstride: walk " << look.walkPace << " m/s, run " << look.runPace << " m/s at normal pace (moves at " << s.walkSpeed << " / "
              << s.runSpeed << " m/s)";
        }
        return o.str();
    }
    if (sub == "spawn" && t.size() > 2) {
        const int n = t.size() > 3 ? std::atoi(t[3].c_str()) : 0;
        const double dist = t.size() > 4 ? std::clamp(std::atof(t[4].c_str()), 0.0, 2000.0) : 15.0;
        glm::dvec3 at = animalFocus_;
        glm::dvec3 f = glm::dvec3(0, 0, -1);
        if (play_) {
            f = playerForward();
        } else {
            for (const View& v : views_)
                if (!v.ortho) {
                    f = v.target - v.eye;
                    break;
                }
        }
        f.y = 0;
        if (glm::length(f) > 1e-6) at += glm::normalize(f) * dist;
        return animals_->spawnAt(*tr, t[2], n, at);
    }
    return "Error: animals [status|list|info <id>|spawn <id> [count] [distance m]|hit [hand|axe|pickaxe|laser]|harvest|clear|reload|on|off]";
}

// ============================================================================ Jagd

// Schlag mit dem, was man in der Hand hat: die Hand allein kaum (ein Fuchs braucht viele Schläge), Axt und Spitzhacke
// kräftig. Getroffene Tiere fliehen, Wehrhafte (Wolf, Wildschwein) greifen erst recht an, bis sie schwer verwundet sind.
// Unter halber Kraft humpeln sie und können nicht mehr rennen.
bool App::animalStrike(const glm::dvec3& eye, const glm::dvec3& fwd, HandTool tool, std::string* msg) {
    if (!animals_) return false;
    float damage = 0.12f;
    double reach = 1.9;
    switch (tool) {
    case HandTool::Axe: damage = 0.55f, reach = 2.4; break;
    case HandTool::Pickaxe: damage = 0.5f, reach = 2.4; break;
    case HandTool::Laser: damage = 0.8f, reach = 3.0; break;
    case HandTool::Drill: damage = 0.3f, reach = 2.2; break;
    case HandTool::Terra: damage = 0.15f, reach = 2.2; break;
    case HandTool::Tractor: return false;
    default: break;
    }
    bool killed = false;
    const uint32_t id = animals_->strike(eye, fwd, reach, damage, &killed);
    if (!id) return false;
    if (const Animal* a = animals_->find(id)) {
        const AnimalSpecies& s = animals_->config().species[(size_t)a->species];
        std::string text;
        if (killed) text = s.label + " erlegt" + (s.hide > 0 ? " - F zum Zerlegen" : "");
        else if (a->hp < 0.5f) text = s.label + " ist verwundet und humpelt";
        if (msg) *msg = text;
        else if (!text.empty()) toast(text);
    }
    return true;
}

// Totes Tier im Blick zerlegen: das Fell (Größe je Art, "hide" in animals.json) kommt ins Inventar, der Kadaver ist weg
bool App::animalHarvest(const glm::dvec3& eye, const glm::dvec3& fwd) {
    if (!animals_) return false;
    const uint32_t id = animals_->pickDead(eye, fwd, 3.0);
    const Animal* a = id ? animals_->find(id) : nullptr;
    if (!a) return false;
    const AnimalSpecies& s = animals_->config().species[(size_t)a->species];
    const glm::dvec3 at = a->pos + glm::dvec3(0, 0.3, 0);
    animals_->remove(id);
    if (s.hide <= 0) {
        toast(s.label + ": daran ist nichts Brauchbares", false);
        return true;
    }
    const int mat = materialIndex("hide");
    const double side = std::sqrt((double)s.hide);
    Object* o = world_->addBox(mat, glm::dvec3(side, 0.006, side * 0.8), at, "Fell (" + s.label + ")");
    if (!o) return true;
    if (!invStore(o->id, true)) toast("Fell (" + s.label + ") - passt nicht mehr ins Inventar, es liegt am Boden", false);
    else toast("Fell (" + s.label + ", " + fmtNum(o->mass, 1) + " kg) im Inventar - C: daraus einen Verband herstellen");
    return true;
}
