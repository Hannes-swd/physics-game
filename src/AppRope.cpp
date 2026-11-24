// Seile (hergestellt aus Fell, Rezept "rope", liegen im Inventar; so viele Seile wie dort liegen, lassen sich anbinden).
// Mit leerer Hand (Werkzeug 1):
//  - Linksklick auf ein Teil, das zu schwer zum Tragen ist (gefällter Stamm, großer Stein): das Seil wird dort angebunden,
//    das andere Ende hält man. Läuft man weiter, als das Seil lang ist, zieht es das Teil hinterher (bis 6000 N; im Sattel
//    zieht das Pferd, viermal so stark), und die Figur wird umso langsamer, je stärker es zieht.
//  - Hält man ein Ende: Linksklick auf ein Pferd oder ein anderes Teil (Baum, Pfosten, noch ein Stamm) macht es dort fest.
//    Reitet man das Pferd, an dem es festgemacht ist, zieht das Pferd den Stamm.
//  - Rechtsklick: das gehaltene Ende loslassen, sonst alle Seile am angeschauten Teil lösen.
//  - Linksklick auf ein Tier (am Boden, auch ein Pferd): angeleint, es läuft einem hinterher (bleibt zahm, flieht nicht
//    mehr). Mehrere Tiere: je ein Seil. Das Ende lässt sich wie oben an ein Pferd, einen Baum oder ein Teil binden.
// Zu weit gezerrt (Teil hängt fest) rutscht ein Seil ab. Der Gastgeber rechnet (Mitspieler: noch nicht, kein Inventar).
#include "App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Animals.h"
#include "Materials.h"
#include "Terrain.h"

namespace {
constexpr double kRopeReach = 3.5;      // so weit weg kann man anbinden (m)
constexpr double kHandForce = 6000;     // N: mit dem ganzen Körper ins Seil gelehnt
constexpr double kHorseForce = 24000;   // N: ein Pferd
constexpr double kTiedForce = 30000;    // N: an einem festen Teil (Baum) oder einem stehenden Pferd
constexpr double kRopeSlip = 5.0;       // so viel länger als das Seil gezerrt: es rutscht ab
constexpr double kLiftForce = 420;      // wie AppInput.cpp: schwerer lässt es sich nicht tragen
}  // namespace

int App::ropeItems() const {
    const int hide = materialIndex("hide");
    int n = 0;
    for (const InvItem& it : inv_)
        if (it.state.material == hide && it.state.name.rfind("Seil", 0) == 0) ++n;
    return n;
}

int App::heldRope() const {
    for (size_t i = 0; i < ropes_.size(); ++i)
        if (ropes_[i].end == Rope::End::Hand) return (int)i;
    return -1;
}

// Wo ein Ende gerade ist (Welt). false: das Ende gibt es nicht mehr
glm::dvec3 App::animalRopePoint(const Animal& an) const {
    const AnimalSpecies& s = animals_->config().species[(size_t)an.species];
    return an.pos + glm::dvec3(-std::sin(an.yaw), 0, -std::cos(an.yaw)) * (0.25 * s.height) + glm::dvec3(0, 0.7 * s.height, 0);
}

bool App::ropeEnds(const Rope& r, glm::dvec3& a, glm::dvec3& b) const {
    if (r.animal) {  // angeleintes Tier: Seil am Hals
        const Animal* an = animals_ ? animals_->find(r.animal) : nullptr;
        if (!an || an->dead) return false;
        a = animalRopePoint(*an);
    } else {
        const Object* o = world_->find(r.obj);
        if (!o) return false;
        a = o->pos + o->rot * r.local;
    }
    switch (r.end) {
    case Rope::End::Hand:
        if (!physics_.hasCharacter()) return false;
        b = physics_.characterPosition() + glm::dvec3(0, 1.0 + rideLift_, 0);
        return true;
    case Rope::End::Animal: {
        const Animal* an = animals_ ? animals_->find(r.endId) : nullptr;
        if (!an || an->dead) return false;
        b = animalRopePoint(*an);
        return true;
    }
    case Rope::End::Part: {
        const Object* e = world_->find(r.endId);
        if (!e) return false;
        b = e->pos + e->rot * r.endLocal;
        return true;
    }
    }
    return false;
}

bool App::ropeClick(bool lmb, bool rmb) {
    const int held = heldRope();
    Object* o = world_->find(lookAt_);
    const bool inReach = o && lookHit_.obj == o && lookDist_ <= kRopeReach;
    const glm::dvec3 eye = playerEye(), fwd = aimForward();
    const uint32_t horse = animals_ ? animals_->pickAlive(eye, fwd, kRopeReach) : 0;  // ein Tier im Blick
    if (rmb) {  // lösen
        if (held >= 0) {
            ropes_.erase(ropes_.begin() + held);
            toast("Seil losgelassen");
            return true;
        }
        const size_t before = ropes_.size();
        ropes_.erase(std::remove_if(ropes_.begin(), ropes_.end(), [&](const Rope& r) {
                         return (inReach && !horse && (r.obj == o->id || (r.end == Rope::End::Part && r.endId == o->id))) ||
                                (horse && (r.animal == horse || (r.end == Rope::End::Animal && r.endId == horse)));
                     }),
                     ropes_.end());
        if (ropes_.size() == before) return false;
        toast(before - ropes_.size() == 1 ? "Seil gelöst" : std::to_string(before - ropes_.size()) + " Seile gelöst");
        return true;
    }
    if (!lmb) return false;
    if (held >= 0) {  // das gehaltene Ende festmachen
        Rope& r = ropes_[(size_t)held];
        glm::dvec3 a, b;
        if (!ropeEnds(r, a, b)) return false;
        if (horse && horse != r.animal) {
            const Animal* an = animals_->find(horse);
            r.end = Rope::End::Animal;
            r.endId = horse;
            r.len = std::max(1.5, glm::length(an->pos - a) + 0.3);
            toast(animals_->config().species[(size_t)an->species].rideable
                      ? "Seil am " + animals_->config().species[(size_t)an->species].label + " festgemacht - aufsteigen (" +
                            keys::label(keys::Act::Use) + ") und reiten zieht es"
                      : "Seil am " + animals_->config().species[(size_t)an->species].label + " festgemacht");
            return true;
        }
        if (inReach && o->id != r.obj) {
            r.end = Rope::End::Part;
            r.endId = o->id;
            r.endLocal = lookHit_.localPoint;
            r.len = std::max(0.5, glm::length(lookHit_.point - a) + 0.1);
            toast("Seil an " + o->name + " festgemacht");
            return true;
        }
        return false;  // ins Leere: weiter halten (rechts lässt los)
    }
    const int items = ropeItems();
    if (horse && items > 0) {  // ein Tier anleinen
        bool tied = false;
        for (const Rope& r : ropes_) tied |= r.animal == horse;
        const Animal* an = animals_->find(horse);
        if (tied || !an) return false;
        if ((int)ropes_.size() >= items) {
            toast("Alle Seile sind schon angebunden (rechts auf ein Tier oder Teil löst eins)", false);
            return true;
        }
        animals_->leash(horse);
        Rope r;
        r.animal = horse;
        r.len = 2.5;
        ropes_.push_back(r);
        toast(animals_->config().species[(size_t)an->species].label + " angeleint - es läuft dir nach; links auf ein Pferd, einen Baum "
              "oder ein Teil macht das Seil dort fest, rechts lässt los");
        return true;
    }
    if (!inReach || o->fixed || o->mass * 9.81 <= kLiftForce) return false;  // leicht: wie immer tragen
    if (items == 0) return false;  // kein Seil: wie bisher schieben
    if ((int)ropes_.size() >= items) {
        toast("Alle Seile sind schon angebunden (rechts auf ein Teil löst eins)", false);
        return true;
    }
    Rope r;
    r.obj = o->id;
    r.local = lookHit_.localPoint;
    r.len = std::max(1.5, glm::length(lookHit_.point - (physics_.characterPosition() + glm::dvec3(0, 1.0, 0))) + 0.3);
    ropes_.push_back(r);
    toast("Seil an " + o->name + " (" + fmtNum(o->mass, 0) + " kg) - losgehen zieht es, links auf ein Pferd oder Teil macht es dort fest, rechts lässt los");
    return true;
}

std::string App::ropeHint() const {
    if (heldRope() >= 0) return "Seil in der Hand: links auf Pferd / Teil = festmachen, rechts = loslassen";
    if (handTool_ != HandTool::None || hand_.grabbed) return "";
    if (animals_ && !rideId_)
        if (const uint32_t an = animals_->pickAlive(playerEye(), aimForward(), kRopeReach)) {
            for (const Rope& r : ropes_)
                if (r.animal == an || (r.end == Rope::End::Animal && r.endId == an)) return "rechts: Seil lösen";
            if ((int)ropes_.size() < ropeItems()) return "links: Seil anlegen";
        }
    const Object* o = world_->find(lookAt_);
    if (!o || lookDist_ > kRopeReach) return "";
    for (const Rope& r : ropes_)
        if (r.obj == o->id || (r.end == Rope::End::Part && r.endId == o->id)) return "rechts: Seil lösen";
    if (!o->fixed && o->mass * 9.81 > kLiftForce && (int)ropes_.size() < ropeItems()) return "links: Seil anbinden";
    return "";
}

void App::updateRope(float dt) {
    ropeDrag_ = 0;
    if (ropes_.empty()) return;
    if (!play_ || netClient() || me_.deadUntil >= 0) {
        ropes_.clear();
        return;
    }
    int leashed = 0;  // angeleinte Tiere am selben Ende gehen nebeneinander statt aufeinander
    for (size_t i = 0; i < ropes_.size();) {
        Rope& r = ropes_[i];
        glm::dvec3 p, e;
        if (r.animal) {
            const int slot = leashed++;  // angeleintes Tier: es geht hinterher, sobald das Seil gespannt ist
            const Animal* an = animals_ ? animals_->find(r.animal) : nullptr;
            const Terrain* tr = world_->terrain();
            if (!an || !ropeEnds(r, p, e) || !tr) {
                ropes_.erase(ropes_.begin() + (long)i);
                continue;
            }
            ++i;
            if (an->ridden) continue;
            glm::dvec2 to(e.x - an->pos.x, e.z - an->pos.z);
            const double flat = glm::length(to);
            float speed = 0;
            glm::dvec3 feet = an->pos;
            float yaw = an->yaw;
            if (flat > r.len && dt > 0) {
                to /= flat;
                // Platz hinter dem Ziehenden: abwechselnd links und rechts, weiter hinten
                const glm::dvec2 side(-to.y, to.x);
                const double off = (slot % 2 ? 1.0 : -1.0) * 0.9 * ((slot + 1) / 2);
                const glm::dvec2 goal = glm::dvec2(e.x, e.z) - to * (r.len + 0.6 * (slot / 2)) + side * off;
                feet = glm::dvec3(goal.x, tr->surfaceHeight(goal.x, goal.y), goal.y);
                speed = (float)(glm::length(glm::dvec2(feet.x - an->pos.x, feet.z - an->pos.z)) / dt);
                yaw = (float)std::atan2(-to.x, -to.y);  // Blick (-sin, -cos) zum Ziehenden
            }
            animals_->steer(r.animal, feet, yaw, speed);
            continue;
        }
        Object* o = world_->find(r.obj);
        if (!o || o->body == kNoBody || !ropeEnds(r, p, e)) {
            ropes_.erase(ropes_.begin() + (long)i);
            continue;
        }
        // wer zieht: die Hand (im Sattel: das Pferd), das gerittene Pferd, sonst ein festes Ende
        const bool byRider = r.end == Rope::End::Hand || (r.end == Rope::End::Animal && r.endId == rideId_ && rideId_);
        const double maxF = byRider ? (rideId_ ? kHorseForce : kHandForce) : kTiedForce;
        const glm::dvec3 d = e - p;
        const double dist = glm::length(d);
        if (dist > r.len + kRopeSlip) {
            toast("Das Seil ist abgerutscht - " + o->name + " hängt fest oder ist zu schwer", false);
            ropes_.erase(ropes_.begin() + (long)i);
            continue;
        }
        ++i;
        if (dist <= r.len || dt <= 0 || o->fixed) continue;  // locker
        const glm::dvec3 dir = d / dist;
        // gespannt: Feder mit Dämpfung (zieht nur, drückt nie)
        const double stretch = dist - r.len;
        const double along = glm::dot(glm::dvec3(physics_.pointVelocity(o->body, p)), dir);  // > 0: kommt schon hinterher
        const double f = std::clamp(9000.0 * stretch + 4.0 * o->mass * std::max(0.0, 1.0 - along), 0.0, maxF);
        physics_.addForceAt(o->body, glm::vec3(dir * f), p);
        if (r.end == Rope::End::Part)  // zwei Teile: das andere wird gegengezogen (ein festes nicht)
            if (Object* t = world_->find(r.endId); t && !t->fixed && t->body != kNoBody) physics_.addForceAt(t->body, glm::vec3(-dir * f), e);
        if (byRider) ropeDrag_ = std::max(ropeDrag_, (float)(f / maxF) * (rideId_ ? 0.6f : 1.0f));
    }
}

// Seile als durchhängende Linien (dreifach nebeneinander: etwas dicker), braun
void App::addRope(FrameDesc& fd) {
    if (ropes_.empty() || !play_) return;
    const glm::dvec3 O = renderOrigin_;
    const glm::vec4 col(0.42f, 0.3f, 0.18f, 1.0f);
    for (const Rope& r : ropes_) {
        glm::dvec3 b, a;
        if (!ropeEnds(r, b, a)) continue;
        if (r.end == Rope::End::Hand)  // in der Hand: etwas vor der Figur
            a += glm::dvec3(-std::sin(playYaw_), 0, -std::cos(playYaw_)) * 0.25;
        const double len = glm::length(b - a);
        const double sag = std::max(0.0, r.len - len) * 0.5 + 0.02;  // locker: hängt durch
        const int n = 16;
        for (int s = 0; s < 3; ++s) {
            const glm::dvec3 off(0, s * 0.006, 0);
            for (int i = 0; i < n; ++i) {
                const double t0 = (double)i / n, t1 = (double)(i + 1) / n;
                const glm::dvec3 p0 = a + (b - a) * t0 - glm::dvec3(0, 4 * sag * t0 * (1 - t0), 0) + off;
                const glm::dvec3 p1 = a + (b - a) * t1 - glm::dvec3(0, 4 * sag * t1 * (1 - t1), 0) + off;
                fd.lines.push_back({glm::vec3(p0 - O), col});
                fd.lines.push_back({glm::vec3(p1 - O), col});
            }
        }
    }
}
