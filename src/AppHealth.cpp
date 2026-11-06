// Leben der Spielfiguren: Schaden erkennen (Sturz, Treffer, Feuer, heiße Teile, Kälte, Wasser, Tiere), Verbinden (H),
// Tod und Neustart, Körperkarte im HUD und der Befehl "health". Das Modell selbst steht in Health.cpp.
// Der Gastgeber rechnet alle Figuren: die eigene (me_) und die der Mitspieler (Peer::vitals); ein Mitspieler bekommt
// seinen Zustand mit der Spielerliste und zeigt nur die Körperkarte.
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include "App.h"
#include "Materials.h"
#include "Terrain.h"
#include "WorldGen.h"

namespace {

// Lage der Körperteile an der Figur: seitlich (m, + = rechts) und Höhe über den Füßen (m)
struct PartPlace {
    double side, height;
};
const PartPlace kPlace[kBodyParts] = {
    {0.0, 1.66},   {0.0, 1.49},   {0.0, 1.30},   {0.0, 1.02},                     // Kopf, Hals, Brust, Bauch
    {-0.24, 1.30}, {-0.27, 1.04}, {-0.28, 0.82},                                  // linker Arm
    {0.24, 1.30},  {0.27, 1.04},  {0.28, 0.82},                                   // rechter Arm
    {-0.10, 0.70}, {-0.10, 0.34}, {-0.10, 0.05},                                  // linkes Bein
    {0.10, 0.70},  {0.10, 0.34},  {0.10, 0.05},                                   // rechtes Bein
};

bool isExtremity(BodyPart p) {
    return p == BodyPart::HandL || p == BodyPart::HandR || p == BodyPart::FootL || p == BodyPart::FootR || p == BodyPart::Head;
}

// Schwere aus Energie (J) und Schwellen
int levelOf(double e, double a, double b, double c) { return e >= c ? 3 : e >= b ? 2 : e >= a ? 1 : 0; }

glm::dvec3 rightOf(double yaw) { return glm::dvec3(std::cos(yaw), 0, -std::sin(yaw)); }

std::mt19937& rng() {
    static std::mt19937 r(std::random_device{}());
    return r;
}
double rnd(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng()); }

// Treffer auf Figuren, die noch keine Figur abgeholt hat (Nummer who)
std::vector<CharacterHit> gHits;

} // namespace

// ============================================================================ Körper und Umgebung

glm::dvec3 App::bodyPartPos(int who, double yaw, BodyPart p) const {
    const PartPlace& q = kPlace[(int)p];
    return physics_.characterPosition(who) + rightOf(yaw) * q.side + glm::dvec3(0, q.height, 0);
}

BodyPart App::bodyPartAt(int who, double yaw, const glm::dvec3& point) const {
    const glm::dvec3 d = point - physics_.characterPosition(who);
    const double side = std::clamp(glm::dot(d, rightOf(yaw)), -0.35, 0.35), h = d.y;
    int best = 0;
    double bestD = 1e30;
    for (int i = 0; i < kBodyParts; ++i) {
        const double ds = side - kPlace[i].side, dh = h - kPlace[i].height;
        const double dist = ds * ds + dh * dh;
        if (dist < bestD) {
            bestD = dist;
            best = i;
        }
    }
    return (BodyPart)best;
}

// Luft am Ort: Umgebungstemperatur der Welt, dazu das Klima des Geländes (kalter Norden, heiße Wüste), Schnee und Tageszeit
double App::airTemperature(const glm::dvec3& p) const {
    double t = world_->ambient();
    if (const Terrain* tr = world_->terrain()) {
        const WorldGen& gen = tr->gen();
        const double v = gen.climate(p.x, p.z).v[kTemperature];
        t += v < 0.45 ? (v - 0.45) * 70.0 : (v - 0.45) * 30.0;
        GenSample s;
        gen.sample(p.x, p.z, s);
        t -= 12.0 * s.snow;
    }
    t += 4.0 * std::cos((dayHours_ - 15.0) / 24.0 * 6.283185307);  // am wärmsten um 15 Uhr
    return t;
}

void App::healthDamage(Vitals& v, BodyPart p, Injury t, int sev, const char* why, bool own) {
    if (sev <= 0 || !healthOn_) return;
    if (v.health.hurt(p, t, sev, v.clock) && own)
        log(std::string("Verletzung: ") + bodyPartName(p) + ", " + injuryName(t) + " (" + injuryStatus(t, v.health.part(p).sev) + ")" +
                (why ? std::string(" - ") + why : std::string()),
            0);
}

void App::healthDied(Vitals& v, bool own) {
    v.deadUntil = v.clock + 4.0;
    v.bandageUntil = -1;
    if (own) {
        hand_.grabbed = 0;
        log("Tot: " + v.health.deathCause(), 2);
    }
}

// Mitspieler gestorben: am Startpunkt neu (wie "tp spawn")
void App::respawnPeer(Peer& p) {
    glm::dvec3 at(0, 0, 0);
    at.y = world_->prepareGround(at) + 0.05;
    p.hand.grabbed = 0;
    if (physics_.hasCharacter((int)p.id)) physics_.setCharacterPosition(at, (int)p.id);
}

// Ein Tier beißt, stößt oder sticht: wo, hängt von seiner Größe ab (Schlange und Spinne die Füße, der Wolf Beine und
// Arme, der Wisent den Rumpf), auf welcher Seite, davon, wo es steht
void App::animalBite(Vitals& v, int who, double yaw, int species, const glm::dvec3& from) {
    if (!animals_ || !healthOn_ || v.deadUntil >= 0 || netClient()) return;
    const AnimalConfig& cfg = animals_->config();
    if (species < 0 || species >= (int)cfg.species.size()) return;
    const AnimalSpecies& s = cfg.species[(size_t)species];
    const bool left = glm::dot(from - physics_.characterPosition(who), rightOf(yaw)) < 0;
    auto side = [&](BodyPart l, BodyPart r) { return left ? l : r; };
    std::vector<BodyPart> parts;
    if (s.height < 0.45f) {
        parts = {side(BodyPart::FootL, BodyPart::FootR), side(BodyPart::FootL, BodyPart::FootR), side(BodyPart::LowerLegL, BodyPart::LowerLegR)};
    } else if (s.height < 1.3f) {
        parts = {side(BodyPart::LowerLegL, BodyPart::LowerLegR), side(BodyPart::LowerLegL, BodyPart::LowerLegR),
                 side(BodyPart::UpperLegL, BodyPart::UpperLegR), side(BodyPart::LowerArmL, BodyPart::LowerArmR),
                 side(BodyPart::HandL, BodyPart::HandR), BodyPart::Belly};
    } else {
        parts = {BodyPart::Belly, BodyPart::Chest, side(BodyPart::UpperLegL, BodyPart::UpperLegR), side(BodyPart::UpperArmL, BodyPart::UpperArmR)};
    }
    const BodyPart part = parts[(size_t)std::min(rnd(0, (double)parts.size()), (double)parts.size() - 1)];
    const double base = s.attackPower * rnd(0.7, 1.3);
    Injury type = Injury::Cut;
    int sev = std::clamp((int)std::lround(base), 1, 3);
    if (s.attackKind == "gore") {  // Hauer, Hörner, Kopf: prellt, bei großer Wucht bricht es
        type = base >= 2.5 ? Injury::Break : Injury::Bruise;
        sev = type == Injury::Break ? std::clamp((int)std::lround(base - 1.5), 1, 3) : std::clamp((int)std::lround(base), 1, 3);
    }
    // noch einmal in dieselbe Wunde: manchmal wird sie schlimmer (ein langer Kampf wird gefährlich)
    const Health::Part& q = v.health.part(part);
    if (q.type == type && q.sev >= sev && q.sev < 3 && rnd(0, 1) < 0.4) sev = q.sev + 1;
    const bool own = &v == &me_;
    healthDamage(v, part, type, sev, s.label.c_str(), own);
    if (v.health.dead() && v.deadUntil < 0) healthDied(v, own);
}

// Ein Verband aus dem Inventar (hergestellt aus Fell, Rezept "bandage") wird verbraucht
bool App::takeBandage() {
    for (size_t i = 0; i < inv_.size(); ++i) {
        const ObjectState& s = inv_[i].state;
        if (s.name.rfind("Verband", 0) != 0 || std::strcmp(materials()[(size_t)s.material].name, "hide") != 0) continue;
        inv_.erase(inv_.begin() + (long)i);
        invPushContents();
        return true;
    }
    return false;
}

// ============================================================================ Schaden

// Eigene Figur. Als Mitspieler rechnet der Gastgeber, hier läuft nur die Uhr (für die Anzeige) und H wird gezählt.
void App::updateHealth(float dt, bool wasOnGround, double vyBefore, float swim) {
    ImGuiIO& io = ImGui::GetIO();
    const bool key = (play_ && cursorCaptured_ && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_H, false)) || bandageCmd_ > 0;
    bandageCmd_ = 0;
    if (netClient()) {
        me_.clock += dt;
        if (key) ++netBandageKeys_;
        return;
    }
    if (!play_ || !physics_.hasCharacter()) return;
    updateVitals(me_, 0, playYaw_, hand_, dt, wasOnGround, vyBefore, swim, key ? 1 : 0, true);
}

void App::updateVitals(Vitals& v, int who, double yaw, Hand& hand, float dt, bool wasOnGround, double vyBefore, float swim,
                       int bandageKeys, bool own) {
    v.clock += dt;
    {  // Treffer dieser Figur abholen (die anderen warten auf ihre Figur)
        std::vector<CharacterHit> fresh = physics_.takeCharacterHits();
        gHits.insert(gHits.end(), fresh.begin(), fresh.end());
        if (gHits.size() > 512) gHits.erase(gHits.begin(), gHits.end() - 512);
    }
    std::vector<CharacterHit> hits;
    for (auto it = gHits.begin(); it != gHits.end();) {
        if (it->who == who) {
            hits.push_back(*it);
            it = gHits.erase(it);
        } else {
            ++it;
        }
    }
    Peer* peer = own ? nullptr : findPeer((uint32_t)who);
    auto msg = [&](const std::string& text, bool ok) {
        if (own) toast(text, ok);
        else if (peer) netEventTo(*peer, text, ok);
    };

    // Tot: kurz dunkel, dann am Startpunkt neu, ganz gesund
    if (v.deadUntil >= 0) {
        if (v.clock >= v.deadUntil) {
            v.deadUntil = -1;
            v.health.healAll();
            v.fallPeakVy = 0;
            if (own) teleportCommand({"tp", "spawn"});
            else if (peer) respawnPeer(*peer);
            msg("Neu am Startpunkt", true);
        }
        return;
    }
    if (!healthOn_) {
        v.fallPeakVy = 0;
        return;
    }
    auto hurt = [&](BodyPart p, Injury t, int sev, const char* why) { healthDamage(v, p, t, sev, why, own); };

    // Sturz: aus der schnellsten Fallgeschwindigkeit beim Aufsetzen (ins Wasser fallen tut nicht weh)
    const bool onGround = physics_.characterOnGround(who);
    if (!onGround) v.fallPeakVy = std::min(v.fallPeakVy, vyBefore);
    if (swim > 0.3f) v.fallPeakVy = 0;
    if (onGround && !wasOnGround) {
        const double sp = -std::min(v.fallPeakVy, vyBefore);
        v.fallPeakVy = 0;
        auto both = [&](BodyPart l, BodyPart r, Injury t, int sev) {
            hurt(l, t, sev, "Sturz");
            hurt(r, t, sev, "Sturz");
        };
        if (sp >= 18.0) {
            hurt(BodyPart::Chest, Injury::Break, 3, "Sturz");
        } else if (sp >= 15.5) {
            both(BodyPart::LowerLegL, BodyPart::LowerLegR, Injury::Break, 3);
            both(BodyPart::UpperLegL, BodyPart::UpperLegR, Injury::Break, 1);
            hurt(BodyPart::Belly, Injury::Bruise, 2, "Sturz");
        } else if (sp >= 13.0) {
            both(BodyPart::LowerLegL, BodyPart::LowerLegR, Injury::Break, 2);
            both(BodyPart::UpperLegL, BodyPart::UpperLegR, Injury::Bruise, 2);
        } else if (sp >= 11.0) {
            both(BodyPart::LowerLegL, BodyPart::LowerLegR, Injury::Break, 1);
            both(BodyPart::FootL, BodyPart::FootR, Injury::Bruise, 2);
        } else if (sp >= 9.5) {
            both(BodyPart::FootL, BodyPart::FootR, Injury::Bruise, 2);
            both(BodyPart::LowerLegL, BodyPart::LowerLegR, Injury::Bruise, 1);
        } else if (sp >= 8.0) {
            both(BodyPart::FootL, BodyPart::FootR, Injury::Bruise, 1);
        }
    }
    if (onGround) v.fallPeakVy = 0;

    // Treffer: Teile, die gegen die Figur fliegen oder auf sie fallen. Spitze und Schneiden schneiden, Stumpfes prellt
    // und bricht. Je Teil zählt der stärkste Treffer (ein Teil meldet oft mehrere Kontakte).
    std::sort(hits.begin(), hits.end(), [](const CharacterHit& a, const CharacterHit& b) { return a.speed * a.speed * a.mass > b.speed * b.speed * b.mass; });
    std::vector<BodyHandle> seen;
    for (const CharacterHit& h : hits) {
        if (std::find(seen.begin(), seen.end(), h.other) != seen.end()) continue;
        seen.push_back(h.other);
        // derselbe Aufprall kommt oft zweimal (Figur und Physikschritt, ein Bild später schwächer): kurz sperren
        auto last = v.hitTime.find((uint32_t)h.other);
        if (last != v.hitTime.end() && v.clock - last->second < 0.4) continue;
        v.hitTime[(uint32_t)h.other] = v.clock;
        if (v.hitTime.size() > 256)
            for (auto it = v.hitTime.begin(); it != v.hitTime.end();) it = v.clock - it->second > 1.0 ? v.hitTime.erase(it) : std::next(it);
        Object* o = world_->fromBody(h.other);
        if (o && o->id == hand.grabbed) continue;  // was man selbst trägt
        const BodyPart part = bodyPartAt(who, yaw, h.point);
        double e = 0.5 * std::min(h.mass, 3000.0) * h.speed * h.speed;
        if (part == BodyPart::Head || part == BodyPart::Neck) e *= 2.0;  // empfindlich
        const int form = o && h.speed > 4.0 ? world_->strikeForm(*o, h.into, h.point) : 0;
        if (form != 0) {
            const int sev = levelOf(e, 15, 60, 200);
            if (sev > 0) hurt(part, Injury::Cut, sev, o ? o->name.c_str() : nullptr);
            continue;
        }
        if (h.speed < 2.5) continue;
        const int lvl = levelOf(e, 25, 80, 200);
        if (e >= 200) hurt(part, Injury::Break, levelOf(e, 200, 450, 1200), o ? o->name.c_str() : nullptr);
        else if (lvl > 0) hurt(part, Injury::Bruise, lvl, o ? o->name.c_str() : nullptr);
    }

    // Hitze und Kälte (alle 0,1 s)
    v.envAccum += dt;
    bool warm = true;
    if (v.envAccum >= 0.1) {
        const double step = v.envAccum;
        v.envAccum = 0;
        const glm::dvec3 feet = physics_.characterPosition(who);
        double nearFire = 0;
        // Feuer: Strahlung (Punktquelle, ca. 30 % der Leistung strahlt), in der Flamme sehr heiß
        for (const Fire& f : world_->fires()) {
            const double r = std::max(0.15, 0.5 * f.diameter);
            const glm::dvec3 c = f.base + glm::dvec3(0, 0.4 * f.height, 0);
            if (glm::length(c - feet) > 25.0) continue;
            for (int i = 0; i < kBodyParts; ++i) {
                const glm::dvec3 p = bodyPartPos(who, yaw, (BodyPart)i);
                const double horiz = glm::length(glm::dvec2(p.x - f.base.x, p.z - f.base.z));
                double q;  // kW/m²
                if (horiz < r && p.y > f.base.y - 0.05 && p.y < f.base.y + f.height) q = 60.0;
                else q = 0.3 * f.power / (4.0 * 3.14159265 * std::max(r * r, glm::dot(p - c, p - c))) / 1000.0;
                nearFire = std::max(nearFire, q);
                v.health.addBurnDose((BodyPart)i, std::max(0.0, q - 2.0) / 25.0 * step, v.clock);
            }
        }
        // Heißes Teil in der Hand (schwere Teile mit beiden Händen)
        if (Object* g = world_->find(hand.grabbed)) {
            const double t = world_->temperatureAt(*g, g->pos + g->rot * hand.grabLocal);
            const double rate = std::max(0.0, t - 50.0) / 15.0;
            if (rate > 0) {
                v.health.addBurnDose(BodyPart::HandR, rate * step, v.clock);
                if (g->mass > 5.0) v.health.addBurnDose(BodyPart::HandL, rate * step, v.clock);
            }
            if (t < -20.0) {  // sehr kaltes Metall kann anfrieren
                v.health.addFrostDose(BodyPart::HandR, (-20.0 - t) / 200.0 * step, v.clock);
            }
        }
        // Auf etwas Heißem stehen (Schuhe schützen etwas)
        RayHit rh;
        if (world_->raycast(feet + glm::dvec3(0, 0.2, 0), glm::dvec3(0, -1, 0), rh, hand.grabbed) && rh.obj && rh.t < 0.4) {
            const double t = world_->temperatureAt(*rh.obj, rh.point);
            const double rate = std::max(0.0, t - 60.0) / 45.0;
            if (rate > 0) {
                v.health.addBurnDose(BodyPart::FootL, rate * step, v.clock);
                v.health.addBurnDose(BodyPart::FootR, rate * step, v.clock);
            }
        }
        // Wasser: heißes verbrüht, kaltes kühlt die Glieder darin aus
        double surface = 0;
        bool cold = false;
        if (const WaterBody* wb = world_->waterColumn(feet, surface)) {
            const double tw = world_->waterTemp(*wb);
            for (int i = 0; i < kBodyParts; ++i) {
                const glm::dvec3 p = bodyPartPos(who, yaw, (BodyPart)i);
                if (p.y > surface) continue;
                if (tw > 48.0) v.health.addBurnDose((BodyPart)i, (tw - 48.0) / 15.0 * step, v.clock);
                if (tw < 12.0) {
                    v.health.addFrostDose((BodyPart)i, (12.0 - tw) / 900.0 * step, v.clock);
                    cold = true;
                }
            }
        }
        // Kalte Luft: Hände, Füße und Kopf erfrieren zuerst, Unterarme und Schienbeine später
        const double ta = airTemperature(feet);
        if (ta < -5.0 && nearFire < 1.0) {
            cold = true;
            const double rate = (-5.0 - ta) / 1200.0;
            for (int i = 0; i < kBodyParts; ++i) {
                const BodyPart p = (BodyPart)i;
                double f = isExtremity(p) ? 1.0 : (p == BodyPart::LowerArmL || p == BodyPart::LowerArmR || p == BodyPart::LowerLegL || p == BodyPart::LowerLegR) ? 0.4 : 0.0;
                if (f > 0) v.health.addFrostDose(p, rate * f * step, v.clock);
            }
        }
        warm = !cold && (ta > 8.0 || nearFire > 0.5);
    }

    // Verbinden (H): mit einem Verband aus dem Inventar 3 s, die schlimmste offene Stelle ist verbunden. Ohne Verband:
    // 4 s Druck auf die Wunde, aus "blutet stark" wird "blutet". Mitspieler haben noch kein Inventar: bei ihnen reicht H.
    if (bandageKeys > 0 && v.bandageUntil < 0) {
        Health probe = v.health;
        if (!probe.bandage(v.clock)) {
            msg("Nichts zu verbinden", false);
        } else if (!own || takeBandage()) {
            v.bandageUntil = v.clock + 3.0;
            v.bandageItem = true;
        } else if (Health p2 = v.health; p2.pressWound(v.clock)) {
            v.bandageUntil = v.clock + 4.0;
            v.bandageItem = false;
            msg("Kein Verband im Inventar: Druck auf die Wunde (Verband aus Fell herstellen: C)", false);
        } else {
            msg("Kein Verband im Inventar - aus Fell herstellen (C)", false);
        }
    }
    if (v.bandageUntil >= 0 && v.clock >= v.bandageUntil) {
        v.bandageUntil = -1;
        BodyPart p;
        if (v.bandageItem ? v.health.bandage(v.clock, &p) : v.health.pressWound(v.clock, &p))
            msg(std::string(bodyPartName(p)) + (v.bandageItem ? " verbunden" : ": Blutung schwächer"), true);
    }

    v.health.update(dt, v.clock, warm);
    if (v.health.dead()) healthDied(v, own);
}

// ============================================================================ Körperkarte (HUD)

namespace {

// Umrisse der Körperteile (wie im Entwurf), Bildkoordinaten 0..120 x 0..290. Die Figur ist von hinten gesehen: links
// im Bild ist die linke Körperseite. Die rechte Seite ist das Spiegelbild der linken.
const char* const kHalf[6] = {
    "M36.5 50 C31 51 27.5 54 26.6 59 L24.4 95 C26.5 97.5 31 98 33.6 96 L37.4 62 Z",                       // Oberarm
    "M24.2 99.5 C26.5 101.5 31 102 33.4 100 L31.2 140 C29 142 25.5 142 23.5 140.5 Z",                     // Unterarm
    "M23.4 144 C25.5 145.5 29 145.5 31 144 C32 150 31.5 157 28.5 161.5 C26 163.5 22.5 162 21.8 158 C21.2 153 22 148 23.4 144 Z",  // Hand
    "M42.5 137 L58.4 139.5 L57.2 199 C54 201 48 201 45.5 199 C43.5 180 42 158 42.5 137 Z",               // Oberschenkel
    "M45.6 203 C48 205 54 205 57 203 L55.4 260 C53 262 49.5 262 47.6 260 Z",                             // Schienbein
    "M47.4 264 C50 265.5 53.5 265.5 55.4 264 L57 274 C57 277 55 278 52 278 L42.5 278 C40 278 39.5 275.5 41.5 274 Z",  // Fuß
};
const char* const kCenter[4] = {
    "M60 4 C68.5 4 73.5 10.5 73.5 19.5 C73.5 29 68 36.5 60 36.5 C52 36.5 46.5 29 46.5 19.5 C46.5 10.5 51.5 4 60 4 Z",
    "M54.5 38.5 L65.5 38.5 L66.5 45 L53.5 45 Z",
    "M40 48 C46 46.5 53 46 60 46 C67 46 74 46.5 80 48 C82 52 82 60 80.5 70 L78.5 92 L41.5 92 L39.5 70 C38 60 38 52 40 48 Z",
    "M41.6 95 L78.4 95 L77.2 116 C77.5 124 78.5 130 79 134 L41 134 C41.5 130 42.5 124 42.8 116 Z",
};

// Pfad (nur M, L, C, Z mit absoluten Koordinaten) zu einem Vieleck; mirror: an x = 60 spiegeln
std::vector<ImVec2> pathPoints(const char* d, bool mirror) {
    std::vector<ImVec2> pts;
    char cmd = 0;
    const char* s = d;
    auto num = [&]() {
        while (*s == ' ' || *s == ',') ++s;
        char* end;
        const float v = std::strtof(s, &end);
        s = end;
        return v;
    };
    auto pt = [&](float x, float y) { return ImVec2(mirror ? 120.0f - x : x, y); };
    while (*s) {
        while (*s == ' ') ++s;
        if (!*s) break;
        if (std::isalpha((unsigned char)*s)) cmd = *s++;
        if (cmd == 'Z') break;
        if (cmd == 'M' || cmd == 'L') {
            const float x = num(), y = num();
            pts.push_back(pt(x, y));
        } else if (cmd == 'C') {
            const ImVec2 p0 = pts.empty() ? ImVec2(0, 0) : pts.back();
            const ImVec2 p1 = pt(num(), 0);
            ImVec2 a = p1;
            a.y = num();
            ImVec2 b = pt(num(), 0);
            b.y = num();
            ImVec2 e = pt(num(), 0);
            e.y = num();
            for (int k = 1; k <= 8; ++k) {
                const float t = k / 8.0f, u = 1 - t;
                pts.push_back(ImVec2(u * u * u * p0.x + 3 * u * u * t * a.x + 3 * u * t * t * b.x + t * t * t * e.x,
                                     u * u * u * p0.y + 3 * u * u * t * a.y + 3 * u * t * t * b.y + t * t * t * e.y));
            }
        } else {
            ++s;
        }
    }
    if (pts.size() > 2 && std::abs(pts.front().x - pts.back().x) < 0.01f && std::abs(pts.front().y - pts.back().y) < 0.01f) pts.pop_back();
    return pts;
}

// Umriss je Körperteil (in der Reihenfolge von BodyPart), einmal berechnet
const std::vector<std::vector<ImVec2>>& bodyShapes() {
    static std::vector<std::vector<ImVec2>> shapes;
    if (shapes.empty()) {
        shapes.resize(kBodyParts);
        for (int i = 0; i < 4; ++i) shapes[i] = pathPoints(kCenter[i], false);
        const BodyPart left[6] = {BodyPart::UpperArmL, BodyPart::LowerArmL, BodyPart::HandL, BodyPart::UpperLegL, BodyPart::LowerLegL, BodyPart::FootL};
        const BodyPart right[6] = {BodyPart::UpperArmR, BodyPart::LowerArmR, BodyPart::HandR, BodyPart::UpperLegR, BodyPart::LowerLegR, BodyPart::FootR};
        for (int k = 0; k < 6; ++k) {
            shapes[(int)left[k]] = pathPoints(kHalf[k], false);
            shapes[(int)right[k]] = pathPoints(kHalf[k], true);
        }
    }
    return shapes;
}

ImVec4 injuryColor(Injury t) {
    switch (t) {
    case Injury::Burn: return ImVec4(1.00f, 0.54f, 0.24f, 1);
    case Injury::Frost: return ImVec4(0.42f, 0.78f, 1.00f, 1);
    case Injury::Cut: return ImVec4(1.00f, 0.29f, 0.29f, 1);
    case Injury::Bruise: return ImVec4(0.71f, 0.55f, 1.00f, 1);
    case Injury::Break: return ImVec4(1.00f, 0.83f, 0.30f, 1);
    default: return ImVec4(1, 1, 1, 1);
    }
}

ImU32 col(ImVec4 c, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * std::clamp(a, 0.0f, 1.0f))); }

// Symbol der Verletzungsart, Mitte c, Größe r (halbe Breite)
void drawInjuryIcon(ImDrawList* dl, Injury t, ImVec2 c, float r, ImU32 cl) {
    switch (t) {
    case Injury::Burn: {  // Flamme
        dl->PathLineTo(ImVec2(c.x, c.y - r));
        dl->PathBezierCubicCurveTo(ImVec2(c.x + 0.4f * r, c.y - 0.4f * r), ImVec2(c.x + 0.85f * r, c.y - 0.1f * r), ImVec2(c.x + 0.8f * r, c.y + 0.35f * r));
        dl->PathBezierCubicCurveTo(ImVec2(c.x + 0.75f * r, c.y + 0.85f * r), ImVec2(c.x - 0.75f * r, c.y + 0.85f * r), ImVec2(c.x - 0.8f * r, c.y + 0.35f * r));
        dl->PathBezierCubicCurveTo(ImVec2(c.x - 0.82f * r, c.y - 0.05f * r), ImVec2(c.x - 0.45f * r, c.y - 0.25f * r), ImVec2(c.x - 0.3f * r, c.y - 0.55f * r));
        dl->PathBezierCubicCurveTo(ImVec2(c.x - 0.15f * r, c.y - 0.25f * r), ImVec2(c.x + 0.05f * r, c.y - 0.45f * r), ImVec2(c.x, c.y - r));
        dl->PathFillConcave(cl);
        break;
    }
    case Injury::Frost:  // Schneekristall
        for (int k = 0; k < 3; ++k) {
            const float a = k * 3.14159265f / 3.0f + 1.5707963f;
            const ImVec2 d(std::cos(a) * r, std::sin(a) * r);
            dl->AddLine(ImVec2(c.x - d.x, c.y - d.y), ImVec2(c.x + d.x, c.y + d.y), cl, 1.6f);
        }
        break;
    case Injury::Cut:  // Tropfen
        dl->PathLineTo(ImVec2(c.x, c.y - r));
        dl->PathBezierCubicCurveTo(ImVec2(c.x + 0.3f * r, c.y - 0.5f * r), ImVec2(c.x + 0.75f * r, c.y - 0.05f * r), ImVec2(c.x + 0.75f * r, c.y + 0.3f * r));
        dl->PathBezierCubicCurveTo(ImVec2(c.x + 0.75f * r, c.y + 0.95f * r), ImVec2(c.x - 0.75f * r, c.y + 0.95f * r), ImVec2(c.x - 0.75f * r, c.y + 0.3f * r));
        dl->PathBezierCubicCurveTo(ImVec2(c.x - 0.75f * r, c.y - 0.05f * r), ImVec2(c.x - 0.3f * r, c.y - 0.5f * r), ImVec2(c.x, c.y - r));
        dl->PathFillConcave(cl);
        break;
    case Injury::Bruise:  // Kreis mit dunklem Kern
        dl->AddCircleFilled(c, 0.72f * r, cl, 20);
        dl->AddCircleFilled(c, 0.32f * r, IM_COL32(0, 0, 0, 110), 12);
        break;
    case Injury::Break: {  // Riss
        const ImVec2 z[5] = {ImVec2(c.x - 0.7f * r, c.y - 0.8f * r), ImVec2(c.x - 0.05f * r, c.y - 0.15f * r), ImVec2(c.x - 0.35f * r, c.y + 0.1f * r),
                             ImVec2(c.x + 0.3f * r, c.y + 0.25f * r), ImVec2(c.x + 0.75f * r, c.y + 0.85f * r)};
        dl->AddPolyline(z, 5, cl, 0, 2.0f);
        break;
    }
    default: break;
    }
}

void shadowText(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 c, const char* t, float a) {
    dl->AddText(f, size, ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, (int)(200 * a)), t);
    dl->AddText(f, size, ImVec2(p.x, p.y + 1.5f), IM_COL32(0, 0, 0, (int)(120 * a)), t);
    dl->AddText(f, size, p, c, t);
}

} // namespace

void App::drawHealthHud() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float now = (float)me_.clock;
    const bool bandaging = me_.bandageUntil >= 0;
    const bool dead = me_.deadUntil >= 0;

    // Tot: Bild wird dunkel, Ursache in der Mitte
    if (dead) {
        const float t = std::clamp((float)(4.0 - (me_.deadUntil - me_.clock)) / 1.2f, 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(8, 4, 4, (int)(225 * t)));
        ImFont* fb = hudBold_ ? hudBold_ : ImGui::GetFont();
        ImFont* fs = hudSmall_ ? hudSmall_ : ImGui::GetFont();
        const char* title = "Du bist gestorben";
        const float big = 34.0f;
        ImVec2 ts = fb->CalcTextSizeA(big, FLT_MAX, 0, title);
        const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.45f);
        dl->AddText(fb, big, ImVec2(c.x - ts.x / 2, c.y - ts.y), IM_COL32(236, 225, 220, (int)(255 * t)), title);
        const std::string why = me_.health.deathCause();
        ts = fs->CalcTextSizeA(16.0f, FLT_MAX, 0, why.c_str());
        dl->AddText(fs, 16.0f, ImVec2(c.x - ts.x / 2, c.y + 10), IM_COL32(236, 225, 220, (int)(160 * t)), why.c_str());
    }

    // Sichtbarkeit: nur bei Verletzung. Voll bei Schwerem, Blutung, frischer Verletzung oder beim Verbinden, sonst halb
    float target = 0;
    if (me_.health.injured() && !dead) {
        const bool urgent = me_.health.worst() >= 2 || me_.health.bleeding() > 0 || bandaging || me_.health.blood() < 0.8;
        target = urgent || now - (float)me_.health.lastChange() < 6.0f ? 1.0f : 0.6f;
    }
    hudHealthAlpha_ += (target - hudHealthAlpha_) * (1.0f - std::exp(-io.DeltaTime / 0.3f));
    const float A = hudHealthAlpha_;
    if (A < 0.01f) return;

    // Lage: links unten über der Statusleiste, Größe mit der Fensterhöhe
    const float k = std::clamp(io.DisplaySize.y / 1080.0f, 0.75f, 2.0f);
    const float statusH = ImGui::GetFrameHeight() + 6;
    const float figH = 200.0f * k, s = figH / 290.0f;
    const ImVec2 o(26.0f * k - 21.0f * s, io.DisplaySize.y - statusH - 18.0f * k - 280.0f * s);
    auto P = [&](ImVec2 p) { return ImVec2(o.x + p.x * s, o.y + p.y * s); };

    const auto& shapes = bodyShapes();
    const float pulse = 0.55f + 0.45f * std::cos(now * 5.4f);
    std::vector<ImVec2> pts;
    for (int i = 0; i < kBodyParts; ++i) {
        const Health::Part& q = me_.health.part((BodyPart)i);
        pts.clear();
        for (const ImVec2& p : shapes[i]) pts.push_back(P(p));
        if (pts.size() < 3) continue;
        if (q.sev == 0) {
            dl->AddConcavePolyFilled(pts.data(), (int)pts.size(), IM_COL32(10, 14, 10, (int)(80 * A)));
            dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(236, 242, 236, (int)(110 * A)), ImDrawFlags_Closed, 1.1f);
            continue;
        }
        const ImVec4 c = injuryColor(q.type);
        float a = A * (q.sev == 1 ? 0.5f : q.sev == 2 ? 0.75f : 0.95f);
        if ((q.type == Injury::Cut && q.sev >= 2 && !q.bandaged) || (q.type == Injury::Break && q.sev == 3 && !q.bandaged)) a *= pulse;
        // Schein: breite, durchsichtige Umrisse
        dl->AddPolyline(pts.data(), (int)pts.size(), col(c, 0.18f * a), ImDrawFlags_Closed, 7.0f * k);
        dl->AddPolyline(pts.data(), (int)pts.size(), col(c, 0.30f * a), ImDrawFlags_Closed, 3.5f * k);
        dl->AddConcavePolyFilled(pts.data(), (int)pts.size(), col(c, a));
        dl->AddPolyline(pts.data(), (int)pts.size(), col(c, A), ImDrawFlags_Closed, 1.3f);
        // frisch verletzt: kurz weiß aufblitzen
        const float age = now - (float)q.changed;
        if (age >= 0 && age < 0.8f) dl->AddConcavePolyFilled(pts.data(), (int)pts.size(), IM_COL32(255, 255, 255, (int)(150 * A * (1 - age / 0.8f))));
        // verbunden: zwei helle Streifen quer
        if (q.bandaged) {
            float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
            for (const ImVec2& p : pts) {
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
            }
            for (float f : {0.42f, 0.58f}) {
                const float y = minY + (maxY - minY) * f;
                dl->AddLine(ImVec2(minX + 1, y), ImVec2(maxX - 1, y + 1.5f * k), IM_COL32(245, 240, 228, (int)(220 * A)), 2.0f * k);
            }
        }
    }

    // Liste rechts daneben, nach Schwere sortiert
    struct Row {
        std::string title, status;
        Injury type;
        int sev;
    };
    std::vector<Row> rows;
    if (bandaging) {
        char b[48];
        std::snprintf(b, sizeof b, "noch %.0f s", std::max(0.0, me_.bandageUntil - me_.clock) + 0.49);
        rows.push_back({me_.bandageItem ? "Verbinden …" : "Druck auf die Wunde …", b, Injury::None, 0});
    }
    if (me_.health.blood() < 0.97 || me_.health.bleeding() > 0) {
        const double b = me_.health.blood();
        const int sev = b > 0.8 ? 1 : b > 0.6 ? 2 : 3;
        const char* st[3] = {"leicht", "mittel", "schwer"};
        rows.push_back({"Blutverlust", std::string(st[sev - 1]) + (me_.health.bleeding() > 0 ? " · blutet" : " · erholt sich"), Injury::Cut, sev});
    }
    std::vector<int> order;
    for (int i = 0; i < kBodyParts; ++i)
        if (me_.health.part((BodyPart)i).sev > 0) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return me_.health.part((BodyPart)a).sev > me_.health.part((BodyPart)b).sev; });
    for (int i : order) {
        const Health::Part& q = me_.health.part((BodyPart)i);
        std::string st = std::string(injuryName(q.type)) + " · " + injuryStatus(q.type, q.sev);
        if (q.bandaged) st = std::string(injuryName(q.type)) + " · verbunden";
        rows.push_back({bodyPartName((BodyPart)i), st, q.type, q.sev});
    }
    const int maxRows = 7;
    if ((int)rows.size() > maxRows) {
        const int more = (int)rows.size() - (maxRows - 1);
        rows.resize(maxRows - 1);
        rows.push_back({"+ " + std::to_string(more) + " weitere", "", Injury::None, 0});
    }

    ImFont* fb = hudBold_ ? hudBold_ : ImGui::GetFont();
    ImFont* fs = hudSmall_ ? hudSmall_ : ImGui::GetFont();
    const float bSize = 18.0f * k, sSize = 16.0f * k, rowH = 46.0f * k;
    const float x = o.x + 98.0f * s + 14.0f * k;
    float y = o.y + 278.0f * s - rows.size() * rowH + 4.0f * k;
    for (const Row& r : rows) {
        const ImVec4 c = injuryColor(r.type);
        if (r.type != Injury::None) drawInjuryIcon(dl, r.type, ImVec2(x + 7 * k, y + 9 * k), 7.0f * k, col(c, A));
        const float tx = x + 22 * k;
        shadowText(dl, fb, bSize, ImVec2(tx, y), IM_COL32(236, 242, 236, (int)(240 * A)), r.title.c_str(), A);
        if (!r.status.empty()) {
            const float sy = y + bSize + 1 * k;
            shadowText(dl, fs, sSize, ImVec2(tx, sy), IM_COL32(226, 234, 226, (int)(200 * A)), r.status.c_str(), A);
            if (r.sev > 0) {
                const float px = tx + fs->CalcTextSizeA(sSize, FLT_MAX, 0, r.status.c_str()).x + 8 * k, py = sy + sSize * 0.55f;
                for (int p = 0; p < 3; ++p) {
                    const ImVec2 a(px + p * 11 * k, py), b(px + p * 11 * k + 9 * k, py + 3 * k);
                    dl->AddRectFilled(a, b, p < r.sev ? col(c, A) : IM_COL32(255, 255, 255, (int)(46 * A)), 1.0f);
                }
            }
        }
        y += rowH;
    }
}

// ============================================================================ Befehl

std::string App::healthCommand(const std::vector<std::string>& t) {
    const std::string w = t.size() > 1 ? t[1] : "";
    if (w.empty() || w == "status") return std::string(healthOn_ ? "" : "(aus) ") + me_.health.describe();
    if (netClient() && w != "use") return "Error: as a co-player only 'health' and 'health use' (the host computes injuries)";
    if (w == "on" || w == "off") {
        healthOn_ = w == "on";
        return healthOn_ ? "Verletzungen an" : "Verletzungen aus";
    }
    if (w == "heal") {
        me_.health.healAll();
        me_.deadUntil = -1;
        me_.bandageUntil = -1;
        return "geheilt";
    }
    if (w == "use") {  // wie H: Verband aus dem Inventar, sonst Druck auf die Wunde
        ++bandageCmd_;
        return "bandaging (like H)";
    }
    if (w == "bandage") {
        BodyPart p;
        return me_.health.bandage(me_.clock, &p) ? std::string(bodyPartName(p)) + " verbunden" : "nichts zu verbinden";
    }
    if (w == "blood" && t.size() > 2) {
        me_.health.setBlood(std::clamp(std::atof(t[2].c_str()), 0.0, 1.0));
        return me_.health.describe();
    }
    if (w == "hurt" && t.size() > 3) {
        BodyPart p;
        Injury ty;
        if (!parseBodyPart(t[2], p)) return "Error: unknown body part '" + t[2] + "' (head neck chest belly upperarm_l lowerarm_l hand_l upperarm_r lowerarm_r hand_r upperleg_l lowerleg_l foot_l upperleg_r lowerleg_r foot_r)";
        if (!parseInjury(t[3], ty)) return "Error: unknown injury '" + t[3] + "' (burn frost cut bruise break)";
        const int sev = t.size() > 4 ? std::clamp(std::atoi(t[4].c_str()), 1, 3) : 1;
        if (!me_.health.hurt(p, ty, sev, me_.clock)) return "unverändert\n" + me_.health.describe();
        if (me_.health.dead() && play_ && me_.deadUntil < 0) healthDied(me_, true);
        return me_.health.describe();
    }
    return "Error: health [status] | health on|off | health heal | health use | health bandage | health blood <0..1> | health hurt <part> <burn|frost|cut|bruise|break> [1-3]";
}
