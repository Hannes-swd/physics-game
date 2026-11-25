// Dynamit (Rezept "dynamite", hergestellt aus einem beliebigen Stab und einer Fell-Hülle; das Teil heißt "Dynamit").
// Wird es heiß (Feuerzeug, Feuer, über 120 °C) oder brennt es, glimmt die Lunte 3 s (Funken), dann explodiert es:
//  - Krater: das Gelände wird um bis zu 1 m ausgehoben (Radius 2,2 m)
//  - Teile bis 4,5 m fliegen weg (je näher, desto schneller); feste Teile (Bäume, Felsen, Stümpfe) bis 1,8 m reißt es los
//  - Spieler bis 5 m: Verbrennungen und Prellungen (nah: schwer), Tiere bis 5 m: Schaden bis zum Tod
//  - anderes Dynamit bis 4 m zündet nach 0,15 bis 0,4 s: Kettenreaktion
// Der Gastgeber rechnet (Mitspieler sehen das Ergebnis).
#include "App.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "Animals.h"
#include "Blueprints.h"
#include "Health.h"
#include "Terrain.h"

namespace {
constexpr double kBlastRadius = 4.5;   // Teile fliegen weg (m)
constexpr double kLooseRadius = 1.8;   // feste Teile reißt es los (m)
constexpr double kHurtRadius = 5.0;    // Spieler und Tiere nehmen Schaden (m)
constexpr double kChainRadius = 4.0;   // anderes Dynamit zündet mit (m)
constexpr double kFuse = 3.0;          // Lunte nach dem Zünden (s)

}  // namespace

bool App::isDynamite(const Object& o) { return o.name.rfind("Dynamit", 0) == 0; }

void App::igniteDynamite(uint32_t id) {
    if (fuses_.count(id)) return;
    fuses_[id] = kFuse;
    toast("Dynamit brennt! Weg da!", false);
}

void App::updateExplosives(float dt) {
    if (netClient() || !world_->simulating()) return;
    // Zünden: heiß oder brennend
    for (const auto& o : world_->objects()) {
        if (!isDynamite(*o) || fuses_.count(o->id)) continue;
        if (o->heatInfo.tMax > 120.0f || o->heatInfo.burning > 0) igniteDynamite(o->id);
    }
    if (fuses_.empty()) return;
    std::vector<uint32_t> boom;
    for (auto it = fuses_.begin(); it != fuses_.end();) {
        Object* o = world_->find(it->first);
        if (!o) {
            it = fuses_.erase(it);
            continue;
        }
        it->second -= dt;
        // Lunte: Funken am Ende des Stabs
        if (fx_.size() < 4000)
            for (int k = 0; k < 2; ++k) {
                FxParticle p;
                p.kind = 2;
                p.pos = o->pos + o->rot * glm::dvec3(kDynamiteFuseX, 0, 0);  // am Ende der Zündschnur
                p.vel = glm::vec3((rand() % 200 - 100) * 0.01f, 0.8f + (rand() % 100) * 0.01f, (rand() % 200 - 100) * 0.01f);
                p.life = 0.35f;
                p.size = 0.02f;
                fx_.push_back(p);
            }
        if (it->second <= 0) boom.push_back(it->first);
        ++it;
    }
    for (uint32_t id : boom) explode(id);
}

void App::explode(uint32_t id) {
    fuses_.erase(id);
    Object* d = world_->find(id);
    if (!d) return;
    const glm::dvec3 P = (d->wMin + d->wMax) * 0.5;
    world_->remove(id);
    // Kettenreaktion: anderes Dynamit in der Nähe zündet gleich (nicht genau gleichzeitig: ein Knall nach dem anderen)
    for (const auto& o : world_->objects())
        if (isDynamite(*o) && glm::length((o->wMin + o->wMax) * 0.5 - P) < kChainRadius) {
            auto f = fuses_.find(o->id);
            const double soon = 0.15 + 0.25 * (rand() % 100) / 100.0;
            if (f == fuses_.end() || f->second > soon) fuses_[o->id] = soon;
        }
    // Krater
    if (Terrain* tr = world_->terrain()) {
        const double ground = tr->surfaceHeight(P.x, P.z);
        if (P.y - ground < 1.5) digGround(glm::dvec3(P.x, ground, P.z), -1.0 * (1.0 - std::max(0.0, P.y - ground) / 1.5), 2.2);
    }
    // Teile wegschleudern, feste in der Nähe losreißen
    std::vector<Object*> hit;
    for (const auto& o : world_->objects()) {
        const glm::dvec3 c = (o->wMin + o->wMax) * 0.5;
        if (glm::length(c - P) < kBlastRadius + 0.5 * glm::length(o->wMax - o->wMin)) hit.push_back(o.get());
    }
    for (Object* o : hit) {
        const glm::dvec3 c = (o->wMin + o->wMax) * 0.5;
        // nächster Punkt der Box zur Explosion: große Teile (Stämme) trifft es auch, wenn ihre Mitte weit weg ist
        const glm::dvec3 near = glm::clamp(P, o->wMin, o->wMax);
        const double dist = glm::length(near - P);
        if (dist > kBlastRadius) continue;
        if (o->fixed) {
            if (dist > kLooseRadius) continue;
            world_->setFixed(*o, false);  // Baum, Felsen, Stumpf: losgerissen
            o->rooted = false;
        }
        if (o->body == kNoBody) continue;
        const double f = 1.0 - dist / kBlastRadius;
        glm::dvec3 dir = c - P + glm::dvec3(0, 0.6, 0);
        dir = glm::length(dir) > 1e-6 ? glm::normalize(dir) : glm::dvec3(0, 1, 0);
        const double dv = std::min(18.0, 22.0 * f * f + 2.0);  // m/s
        physics_.addImpulseAt(o->body, glm::vec3(dir * (dv * std::min(o->mass, 4000.0))), near);
    }
    // Spieler: Verbrennungen und Prellungen, je näher, desto schwerer
    if (play_ && healthOn_ && physics_.hasCharacter() && me_.deadUntil < 0) {
        const double dist = glm::length(physics_.characterPosition() + glm::dvec3(0, 0.9, 0) - P);
        if (dist < kHurtRadius) {
            const int sev = dist < 1.5 ? 3 : dist < 3.0 ? 2 : 1;
            me_.health.hurt(BodyPart::Chest, Injury::Burn, sev, me_.clock);
            me_.health.hurt(BodyPart::Head, Injury::Bruise, std::max(1, sev - 1), me_.clock);
            me_.health.hurt(BodyPart::UpperLegL, Injury::Bruise, sev, me_.clock);
            me_.health.hurt(BodyPart::UpperLegR, Injury::Bruise, sev, me_.clock);
            if (sev == 3) me_.health.setBlood(std::min(me_.health.blood(), 0.5));
        }
    }
    // Tiere
    if (animals_)
        for (Animal& a : animals_->animals()) {
            if (a.dead) continue;
            const double dist = glm::length(a.pos - P);
            if (dist < kHurtRadius) animals_->hurt(a, (float)(3.0 * (1.0 - dist / kHurtRadius)), glm::dvec2(P.x, P.z));
        }
    // Bild: Blitz aus Funken, dazu eine dicke Rauchwolke
    for (int i = 0; i < 160 && fx_.size() < 6000; ++i) {
        FxParticle p;
        p.kind = 2;
        p.pos = P;
        const glm::vec3 r(rand() % 200 - 100, rand() % 140 - 20, rand() % 200 - 100);
        p.vel = glm::normalize(r + glm::vec3(0.001f)) * (6.0f + (rand() % 100) * 0.12f);
        p.life = 0.5f + (rand() % 100) * 0.006f;
        p.size = 0.05f;
        fx_.push_back(p);
    }
    for (int i = 0; i < 40 && fx_.size() < 6000; ++i) {
        FxParticle p;
        p.kind = 1;
        p.pos = P + glm::dvec3((rand() % 200 - 100) * 0.012, (rand() % 100) * 0.01, (rand() % 200 - 100) * 0.012);
        p.vel = glm::vec3((rand() % 200 - 100) * 0.02f, 1.5f + (rand() % 100) * 0.02f, (rand() % 200 - 100) * 0.02f);
        p.life = 4.0f + (rand() % 100) * 0.03f;
        p.size = 0.9f;
        p.grow = 1.8f;
        p.strength = 1.6f;
        fx_.push_back(p);
    }
    log("BUMM: Dynamit explodiert bei " + fmtNum(P.x, 1) + " " + fmtNum(P.y, 1) + " " + fmtNum(P.z, 1) + " m", 3);
}
