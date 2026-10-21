// Spielfiguren: animiertes Modell (eingebettete glTF-Datei, assets/characters), Ansicht von hinten (F5).
#include "App.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

#include "Materials.h"

namespace {

constexpr float kFigureHeight = 1.80f;            // Sohle bis Helm, wie die Kapsel der Physik
constexpr const char* kTintMaterial = "Worker_Yellow";  // im LAN in der Farbe des Spielers (Helm, Streifen)
float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

} // namespace

void App::addAvatars(FrameDesc& fd) {
    if (!avatarLoaded_) {
        avatarLoaded_ = true;
        std::string err;
        if (avatar_.loadBuiltin(kFigureHeight, &err)) {
            clipIdle_ = avatar_.clip("Idle");
            clipWalk_ = avatar_.clip("Walk");
            clipRun_ = avatar_.clip("Run");
            clipLeft_ = avatar_.clip("Run_Left");
            clipRight_ = avatar_.clip("Run_Right");
            clipBack_ = avatar_.clip("Run_Back");
            // Sprung und Landung bewegt das Spiel selbst (das Modell hat keinen Sprung-Ablauf)
            const char* side[2] = {".L", ".R"};
            rig_.head = avatar_.node("Head");
            rig_.torso = avatar_.node("Torso");
            for (int k = 0; k < 2; ++k) {
                rig_.thigh[k] = avatar_.node(std::string("UpperLeg") + side[k]);
                rig_.shin[k] = avatar_.node(std::string("LowerLeg") + side[k]);
                rig_.foot[k] = avatar_.node(std::string("Foot") + side[k]);
                rig_.arm[k] = avatar_.node(std::string("UpperArm") + side[k]);
                rig_.armSide[k] = avatar_.restPosition(rig_.arm[k]).x < 0 ? -1.0f : 1.0f;
                // das Fuß-Ziel hängt an der Wurzel, nicht am Bein: beim Anziehen der Knie mitnehmen
                avatar_.setFollow(rig_.foot[k], rig_.shin[k]);
            }
            const int ankle = avatar_.node("LowerLeg.L_end") >= 0 ? avatar_.node("LowerLeg.L_end") : rig_.foot[0];
            if (rig_.thigh[0] >= 0 && rig_.shin[0] >= 0 && ankle >= 0)
                rig_.legLength = glm::length(avatar_.restPosition(rig_.thigh[0]) - avatar_.restPosition(rig_.shin[0])) +
                                 glm::length(avatar_.restPosition(rig_.shin[0]) - avatar_.restPosition(ankle));
        } else {
            log("Player model could not be loaded: " + err, 2);
        }
    }
    const float dt = figureTime_ < 0 ? 0.0f : (float)std::clamp(time_ - figureTime_, 0.0, 0.1);
    figureTime_ = time_;

    const bool lan = netPlayers_.size() >= 2;
    const glm::dvec3 O = renderOrigin_;
    const glm::mat4 I(1.0f);
    if (lan)
        for (const NetPlayer& p : netPlayers_) {
            if (p.id == netSelfId_) continue;
            const glm::vec3 col = playerColor(p.id);
            if (p.play) {
                addFigure(fd, p.id, p.feet, p.yaw, p.pitch, p.onGround, &col, dt);
                continue;
            }
            // Bau-Modus: schwebende Kamera mit Objektiv in Blickrichtung
            const glm::mat4 cam = glm::translate(I, glm::vec3(p.camEye - O)) * glm::rotate(I, (float)p.yaw, glm::vec3(0, 1, 0)) *
                                  glm::rotate(I, (float)p.pitch, glm::vec3(1, 0, 0));
            for (int k = 0; k < 2; ++k) {
                DrawItem it;
                it.mesh = &world_->boxMesh();
                it.data.model = k == 0 ? cam * glm::scale(I, glm::vec3(0.18f, 0.12f, 0.22f))
                                       : cam * glm::translate(I, glm::vec3(0, 0, -0.13f)) * glm::scale(I, glm::vec3(0.09f, 0.09f, 0.05f));
                const glm::vec3 c = k == 0 ? col : glm::vec3(0.05f, 0.05f, 0.06f);
                it.data.color = glm::vec4(c, 0.6f);
                it.data.color2 = glm::vec4(c * 0.85f, 0.0f);
                it.data.params = glm::vec4((float)Shading::Plain, 0, 0, 0);
                fd.items.push_back(it);
            }
        }
    if (play_ && thirdPerson_) {
        const glm::vec3 col = playerColor(netSelfId_);
        const glm::dvec3 feet = netClient() ? netFeet_ : physics_.characterPosition();
        bool onGround = physics_.characterOnGround();
        if (netClient())  // die eigene Figur läuft beim Gastgeber
            for (const NetPlayer& p : netPlayers_)
                if (p.id == netSelfId_) onGround = p.onGround;
        addFigure(fd, lan ? netSelfId_ : 0, feet, playYaw_, playPitch_, onGround, lan ? &col : nullptr, dt);
    }
}

void App::addFigure(FrameDesc& fd, uint32_t key, const glm::dvec3& feet, double yaw, double pitch, bool onGround, const glm::vec3* tint,
                    float dt) {
    const glm::mat4 I(1.0f);
    FigureAnim& a = figureAnim_[key];
    if (!a.init || glm::length(feet - a.feet) > 3.0) {  // neu oder versetzt
        a = FigureAnim();
        a.init = true;
        a.feet = feet;
    }
    if (dt > 0) {
        const glm::vec2 v(float(feet.x - a.feet.x) / dt, float(feet.z - a.feet.z) / dt);
        // Lage aus der eigenen Physik: kaum glätten (sonst laufen die Beine nach dem Anhalten weiter);
        // beim Mitspieler kommen die Lagen in Paketen und brauchen etwas mehr
        a.vel += (v - a.vel) * (1.0f - std::exp(-dt / (netClient() ? 0.08f : 0.035f)));
        a.vy += (float(feet.y - a.feet.y) / dt - a.vy) * (1.0f - std::exp(-dt / 0.05f));
    }
    a.feet = feet;
    const float speed = glm::length(a.vel);
    const glm::vec3 at = glm::vec3(feet - renderOrigin_);

    if (!avatar_.valid()) {  // Ersatz ohne Modell: Säule mit Kopf
        if (!avatarMesh_.valid()) {
            std::vector<solid::Vertex> v;
            solid::buildTriangles(solid::makeCylinder(0.25, 1.0, 20), v);
            avatarMesh_ = renderer_.createMesh(v);
        }
        const glm::vec3 c = tint ? *tint : glm::vec3(0.8f, 0.55f, 0.2f);
        DrawItem it;
        it.mesh = &avatarMesh_;
        it.data.model = glm::translate(I, at + glm::vec3(0, 0.75f, 0)) * glm::scale(I, glm::vec3(1, 1.4f, 1));
        it.data.color = glm::vec4(c, 0.6f);
        it.data.color2 = glm::vec4(c * 0.85f, 0.0f);
        it.data.params = glm::vec4((float)Shading::Plain, 0, 0, 0);
        fd.items.push_back(it);
        it.mesh = &world_->boxMesh();
        it.data.model = glm::translate(I, at + glm::vec3(0, 1.62f, 0)) * glm::rotate(I, (float)yaw, glm::vec3(0, 1, 0)) *
                        glm::scale(I, glm::vec3(0.24f, 0.26f, 0.24f));
        fd.items.push_back(it);
        return;
    }

    // Laufrichtung relativ zum Blick: 0 = vorn, +90° = rechts, ±180° = zurück
    const glm::vec2 fwd(-std::sin((float)yaw), -std::cos((float)yaw)), right(std::cos((float)yaw), -std::sin((float)yaw));
    if (speed > 0.3f) {
        const glm::vec2 want = glm::normalize(glm::vec2(glm::dot(a.vel, right), glm::dot(a.vel, fwd)));
        a.dir = glm::normalize(a.dir + (want - a.dir) * (1.0f - std::exp(-dt / 0.06f)) + glm::vec2(0, 1e-4f));
    }
    const float theta = std::atan2(a.dir.x, a.dir.y), absTheta = std::abs(theta), sgn = theta < 0 ? -1.0f : 1.0f;
    // Schräg laufen Hüfte und Beine in Laufrichtung, Oberkörper und Kopf bleiben beim Blick. Die Seitwärts- und
    // Rückwärtsschritte kommen erst, wenn es fast genau seitwärts bzw. rückwärts geht. Gemischt werden die Abläufe nur
    // im schmalen Übergang (und dann im Gleichschritt), sonst kreuzen sich die Beine.
    const bool hasSide = clipLeft_ >= 0 && clipRight_ >= 0;
    const float wb = hasSide ? smooth01((absTheta - 1.99f) / 0.2f) : smooth01((absTheta - 1.47f) / 0.2f);
    const float ws = (hasSide ? smooth01((absTheta - 0.95f) / 0.2f) : 0.0f) * (1.0f - wb);
    const float wf = 1.0f - ws - wb;
    const float turn = theta - sgn * (ws * 1.5708f + wb * 3.14159f);  // Hüfte gegen den Blick gedreht

    // Stehen -> Gehen -> Rennen überblenden; die Schrittfrequenz folgt der Geschwindigkeit, damit die Füße nicht rutschen
    auto groundSpeed = [&](int clip, float fallback) {
        const float v = avatar_.clipGroundSpeed(clip);
        return v > 0.3f ? v : fallback;
    };
    const float vw = groundSpeed(clipWalk_, 1.4f), vr = std::max(groundSpeed(clipRun_, 4.0f), vw + 0.5f);
    const float move = smooth01((speed - 0.1f) / 0.5f);
    const float run = clipRun_ >= 0 ? smooth01((speed - vw) / (vr - vw)) : 0.0f;
    struct Moving {
        int clip;
        float weight, speed, dir;  // dir -1: rückwärts abspielen (wenn der Rückwärts-Ablauf fehlt)
    };
    const bool hasBack = clipBack_ >= 0;
    const Moving moving[5] = {{clipWalk_, wf * (1 - run), vw, 1},
                              {clipRun_, wf * run, vr, 1},
                              {hasBack ? clipBack_ : clipRun_, wb, groundSpeed(hasBack ? clipBack_ : clipRun_, vr), hasBack ? 1.0f : -1.0f},
                              {clipLeft_, sgn < 0 ? ws : 0.0f, groundSpeed(clipLeft_, vr), 1},
                              {clipRight_, sgn > 0 ? ws : 0.0f, groundSpeed(clipRight_, vr), 1}};
    float stride = 0, natural = 0;  // Strecke je Schrittzyklus, Tempo der Abläufe
    for (const Moving& m : moving) {
        stride += m.weight * m.speed * avatar_.clipLength(m.clip);
        natural += m.weight * m.speed;
    }
    // schneller als der Ablauf: wie beim echten Laufen längere Schritte statt nur schnellerer
    const float cadence = speed <= natural ? speed : natural * std::sqrt(speed / natural);

    // Sprung: sofort beim Absprung, beim Herunterfallen (Stufe, Kante) erst nach kurzer Zeit
    if (!onGround) {
        a.airTime += dt;
    } else {
        if (a.airTime > 0.2f) a.land = std::max(a.land, std::clamp(-a.vy / 6.0f, 0.35f, 1.0f));
        a.airTime = 0;
    }
    const float inAir = !onGround && (a.airTime > 0.12f || a.vy > 1.0f) ? 1.0f : 0.0f;
    a.air += (inAir - a.air) * (1.0f - std::exp(-dt / (inAir > a.air ? 0.07f : 0.04f)));
    a.land *= std::exp(-dt / 0.16f);

    // in der Luft bleiben die Beine im Schritt stehen
    if (stride > 1e-3f) a.phase += cadence * dt / stride * (1.0f - a.air);
    a.phase -= std::floor(a.phase);
    a.idleTime += dt;
    AvatarModel::Layer layers[6] = {{clipIdle_, a.idleTime, 1.0f - move}};
    for (int i = 0; i < 5; ++i) {  // Gleichschritt: bei Phase 0 schwingt in jedem Ablauf derselbe Fuß am höchsten
        float p = moving[i].dir * a.phase + avatar_.clipSync(moving[i].clip);
        p -= std::floor(p);
        layers[i + 1] = {moving[i].clip, p * avatar_.clipLength(moving[i].clip), move * moving[i].weight};
    }

    // Haltung: Knie anziehen beim Steigen, Beine strecken beim Fallen, Arme zum Ausbalancieren; nach der Landung
    // federn die Knie ein, die Figur sinkt dabei so weit ab, dass die Füße am Boden bleiben
    const float rise = std::clamp(a.vy / 4.0f, -1.0f, 1.0f), up = 0.5f * (rise + 1.0f);
    const float crouch = 0.6f * a.land;
    std::vector<AvatarModel::Bend> bends;
    // Hüfte nur beim Laufen gedreht; Oberkörper und Kopf zusammen zurück zum Blick, der Kopf nickt um dessen Querachse
    const float hip = turn * move;
    bends.push_back({rig_.torso, {0, 1, 0}, -0.6f * hip});
    bends.push_back({rig_.head, {0, 1, 0}, -0.4f * hip});
    bends.push_back({rig_.head, {std::cos(hip), 0, std::sin(hip)}, (float)std::clamp(pitch * 0.6, -0.6, 0.5)});
    bends.push_back({rig_.torso, {1, 0, 0}, -0.12f * a.air - 0.35f * a.land});
    for (int k = 0; k < 2; ++k) {
        const float lead = k == 0 ? 1.15f : 0.85f;  // ein Bein etwas weiter vorn
        const float thigh = a.air * (0.30f + 0.55f * up) * lead + crouch;
        const float knee = a.air * (0.45f + 0.95f * up) * lead + 2.0f * crouch;
        bends.push_back({rig_.thigh[k], {1, 0, 0}, thigh});
        bends.push_back({rig_.shin[k], {1, 0, 0}, -knee});
        bends.push_back({rig_.foot[k], {1, 0, 0}, crouch});  // Sohle bleibt flach
        bends.push_back({rig_.arm[k], {0, 0, 1}, rig_.armSide[k] * (a.air * (0.85f - 0.4f * up) + 0.25f * a.land)});
        bends.push_back({rig_.arm[k], {1, 0, 0}, a.air * 0.6f * up + 0.4f * a.land});
    }
    const float drop = rig_.legLength * (1.0f - std::cos(crouch));

    const uint32_t base = (uint32_t)fd.frameVertices.size();
    avatar_.pose(layers, 6, bends.data(), (int)bends.size(), fd.frameVertices);
    const glm::mat4 model = glm::translate(I, at - glm::vec3(0, drop, 0)) * glm::rotate(I, (float)yaw + hip, glm::vec3(0, 1, 0));
    for (const AvatarModel::Part& part : avatar_.parts()) {
        const glm::vec3 c = tint && part.material == kTintMaterial ? *tint : part.color;
        DrawItem it;
        it.frameFirst = base + part.first;
        it.frameCount = part.count;
        it.data.model = model;
        it.data.color = glm::vec4(c, 0.75f);
        it.data.color2 = glm::vec4(c * 0.9f, 0.0f);
        it.data.params = glm::vec4((float)Shading::Plain, 0, 0, 0);
        fd.items.push_back(it);
    }
}

void App::updateThirdPersonEye() {
    // über die rechte Schulter, nicht durch Wände und Teile hindurch
    const glm::dvec3 head = playerEye(), fwd = playerForward();
    const glm::dvec3 right(std::cos(playYaw_), 0, -std::sin(playYaw_));
    const glm::dvec3 off = -fwd * 3.0 + right * 0.55 + glm::dvec3(0, 0.3, 0);
    const double len = glm::length(off);
    const glm::dvec3 d = off / len;
    RayHit h;
    double reach = len;
    if (world_->raycast(head, d, h, hand_.grabbed) && h.t < len + 0.25) reach = std::max(0.15, h.t - 0.25);
    thirdEye_ = head + d * reach;

    // Ziel: was mitten im Bild liegt, vom Kopf aus angepeilt
    glm::dvec3 target = thirdEye_ + fwd * 60.0;
    if (world_->raycast(thirdEye_, fwd, h, hand_.grabbed) && h.t < 60.0) target = h.point;
    const glm::dvec3 aim = target - head;
    aim_ = glm::length(aim) > 1e-6 && glm::dot(glm::normalize(aim), fwd) > 0.3 ? glm::normalize(aim) : fwd;
}
