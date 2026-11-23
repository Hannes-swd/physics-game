// Seil (Werkzeug 9 der Leiste, hergestellt aus Fell, Rezept "rope"): Linksklick auf ein loses Teil (gefällter Stamm,
// Stein, Brett) bindet es an der Stelle an, die man anschaut. Läuft man weiter, als das Seil lang ist, zieht es das Teil
// hinterher: eine Kraft am Anbindepunkt, wie ein gespanntes Seil (bis 6000 N, mit dem ganzen Körper), und die Figur wird
// umso langsamer, je stärker es zieht. Nochmal links oder rechts löst es; zu weit gezerrt (Teil hängt fest) rutscht es ab.
// Der Gastgeber rechnet (Mitspieler: noch nicht, kein Inventar).
#include "App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Materials.h"

namespace {
constexpr double kRopeReach = 3.5;     // so weit weg kann man anbinden (m)
constexpr double kRopeMaxForce = 6000;  // N: mit dem ganzen Körper ins Seil gelehnt
constexpr double kRopeSlip = 5.0;      // so viel länger als das Seil gezerrt: es rutscht ab
}  // namespace

bool App::hasRope() const {
    const int hide = materialIndex("hide");
    for (const InvItem& it : inv_)
        if (it.state.material == hide && it.state.name.rfind("Seil", 0) == 0) return true;
    return false;
}

void App::ropeAction(bool lmbPressed, bool rmbPressed) {
    if (ropeObj_ && (lmbPressed || rmbPressed)) {
        ropeObj_ = 0;
        ropeDrag_ = 0;
        toast("Seil gelöst");
        return;
    }
    if (!lmbPressed) return;
    if (netClient()) {
        toast("Seil: als Mitspieler im LAN noch nicht möglich", false);
        return;
    }
    if (!hasRope()) {
        toast("Kein Seil im Inventar: " + keys::label(keys::Act::Craft) + " → Seil herstellen (aus Fell)", false);
        return;
    }
    Object* o = world_->find(lookAt_);
    if (!o || lookDist_ > kRopeReach || lookHit_.obj != o) {
        toast("Seil: auf ein Teil schauen (bis 3,5 m), z. B. einen gefällten Stamm", false);
        return;
    }
    if (o->fixed) {
        toast(o->name + " ist fest verankert (ein stehender Baum muss erst gefällt werden)", false);
        return;
    }
    ropeObj_ = o->id;
    ropeLocal_ = lookHit_.localPoint;
    const glm::dvec3 hand = physics_.characterPosition() + glm::dvec3(0, 1.0, 0);
    ropeLen_ = std::max(1.5, glm::length(lookHit_.point - hand) + 0.3);
    toast(o->name + " angebunden (" + fmtNum(o->mass, 0) + " kg) - losgehen zieht es, nochmal klicken löst");
}

void App::updateRope(float dt) {
    ropeDrag_ = 0;
    if (!ropeObj_) return;
    Object* o = world_->find(ropeObj_);
    if (!play_ || netClient() || !o || o->fixed || o->body == kNoBody || !physics_.hasCharacter() || me_.deadUntil >= 0) {
        ropeObj_ = 0;
        return;
    }
    if (handTool_ != HandTool::Rope && handTool_ != HandTool::None) {  // anderes Werkzeug genommen: Seil loslassen
        ropeObj_ = 0;
        toast("Seil gelöst");
        return;
    }
    const glm::dvec3 p = o->pos + o->rot * ropeLocal_;
    const glm::dvec3 hand = physics_.characterPosition() + glm::dvec3(0, 1.0, 0);
    const glm::dvec3 d = hand - p;
    const double dist = glm::length(d);
    if (dist > ropeLen_ + kRopeSlip) {
        ropeObj_ = 0;
        toast("Das Seil ist abgerutscht - " + o->name + " hängt fest oder ist zu schwer", false);
        return;
    }
    if (dist <= ropeLen_ || dt <= 0) return;  // locker
    const glm::dvec3 dir = d / dist;
    // gespannt: Feder mit Dämpfung (zieht nur, drückt nie), höchstens kRopeMaxForce
    const double stretch = dist - ropeLen_;
    const double along = glm::dot(glm::dvec3(physics_.pointVelocity(o->body, p)), dir);  // > 0: kommt schon hinterher
    double f = 9000.0 * stretch + 4.0 * o->mass * std::max(0.0, 1.0 - along);
    const double maxF = rideId_ ? kRopeMaxForce * 4 : kRopeMaxForce;  // ein Pferd zieht viel mehr
    f = std::clamp(f, 0.0, maxF);
    physics_.addForceAt(o->body, glm::vec3(dir * f), p);
    ropeDrag_ = (float)std::clamp(f / maxF, 0.0, 1.0) * (rideId_ ? 0.6f : 1.0f);
}

// Das Seil als durchhängende Linie (mehrfach nebeneinander: etwas dicker), braun
void App::addRope(FrameDesc& fd) {
    if (!ropeObj_ || !play_) return;
    const Object* o = world_->find(ropeObj_);
    if (!o || !physics_.hasCharacter()) return;
    const glm::dvec3 O = renderOrigin_;
    const glm::dvec3 a = physics_.characterPosition() + glm::dvec3(0, 1.0, 0) + glm::dvec3(-std::sin(playYaw_), 0, -std::cos(playYaw_)) * 0.25;
    const glm::dvec3 b = o->pos + o->rot * ropeLocal_;
    const double len = glm::length(b - a);
    const double sag = std::max(0.0, ropeLen_ - len) * 0.5 + 0.02;  // locker: hängt durch
    const glm::vec4 col(0.42f, 0.3f, 0.18f, 1.0f);
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
