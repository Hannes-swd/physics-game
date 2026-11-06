// Leben der Spielfigur: Verletzungen je Körperteil (siehe Health.h)
#include "Health.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace {

struct PartInfo {
    const char* id;
    const char* name;
};
const PartInfo kParts[kBodyParts] = {
    {"head", "Kopf"},
    {"neck", "Hals"},
    {"chest", "Brust"},
    {"belly", "Bauch"},
    {"upperarm_l", "Linker Oberarm"},
    {"lowerarm_l", "Linker Unterarm"},
    {"hand_l", "Linke Hand"},
    {"upperarm_r", "Rechter Oberarm"},
    {"lowerarm_r", "Rechter Unterarm"},
    {"hand_r", "Rechte Hand"},
    {"upperleg_l", "Linker Oberschenkel"},
    {"lowerleg_l", "Linkes Schienbein"},
    {"foot_l", "Linker Fuß"},
    {"upperleg_r", "Rechter Oberschenkel"},
    {"lowerleg_r", "Rechtes Schienbein"},
    {"foot_r", "Rechter Fuß"},
};

// Dauer je Stufe bis zur nächst leichteren (s Spielzeit)
double healTime(Injury t) {
    switch (t) {
    case Injury::Bruise: return 45;
    case Injury::Cut: return 90;
    case Injury::Burn: return 150;
    case Injury::Frost: return 150;
    case Injury::Break: return 300;
    default: return 60;
    }
}

constexpr double kDoseLevels[3] = {1.0, 2.5, 5.0};

bool isLeg(int i) {
    return i >= (int)BodyPart::UpperLegL && i <= (int)BodyPart::FootR;
}
bool isCore(int i) { return i <= (int)BodyPart::Belly; }

} // namespace

const char* bodyPartName(BodyPart p) { return (int)p >= 0 && (int)p < kBodyParts ? kParts[(int)p].name : "?"; }
const char* bodyPartId(BodyPart p) { return (int)p >= 0 && (int)p < kBodyParts ? kParts[(int)p].id : "?"; }

const char* injuryName(Injury t) {
    switch (t) {
    case Injury::Burn: return "Verbrennung";
    case Injury::Frost: return "Erfrierung";
    case Injury::Cut: return "Schnitt";
    case Injury::Bruise: return "Prellung";
    case Injury::Break: return "Bruch";
    default: return "gesund";
    }
}

const char* injuryId(Injury t) {
    switch (t) {
    case Injury::Burn: return "burn";
    case Injury::Frost: return "frost";
    case Injury::Cut: return "cut";
    case Injury::Bruise: return "bruise";
    case Injury::Break: return "break";
    default: return "none";
    }
}

const char* injuryStatus(Injury t, int sev) {
    static const char* const s[6][3] = {
        {"", "", ""},
        {"gerötet", "Brandblasen", "schwer verbrannt"},
        {"taub", "erfroren", "Gewebe stirbt ab"},
        {"oberflächlich", "blutet", "blutet stark"},
        {"leicht", "geschwollen", "stark geschwollen"},
        {"angebrochen", "gebrochen", "offener Bruch"},
    };
    return s[std::clamp((int)t, 0, 5)][std::clamp(sev, 1, 3) - 1];
}

bool parseBodyPart(const std::string& s, BodyPart& out) {
    for (int i = 0; i < kBodyParts; ++i)
        if (s == kParts[i].id) {
            out = (BodyPart)i;
            return true;
        }
    return false;
}

bool parseInjury(const std::string& s, Injury& out) {
    for (int i = 1; i <= (int)Injury::Break; ++i)
        if (s == injuryId((Injury)i)) {
            out = (Injury)i;
            return true;
        }
    return false;
}

// ============================================================================ Verletzungen

static bool hurtPart(Health::Part& q, Injury t, int sev, double now, bool escalate) {
    sev = std::clamp(sev, 1, 3);
    if (q.type == t) {
        int n = std::max(q.sev, sev);
        if (escalate && sev == q.sev && sev >= 2) n = std::min(3, n + 1);  // derselbe schwere Schaden noch einmal
        if (n == q.sev) return false;
        q.sev = n;
    } else {
        // die schwerere Verletzung bleibt; ein Bruch zählt eine Stufe mehr (eine starke Prellung ersetzt keinen Bruch)
        auto weight = [](Injury ty, int s) { return s + (ty == Injury::Break ? 1 : 0); };
        const int wNew = weight(t, sev), wOld = weight(q.type, q.sev);
        if (wNew < wOld || (wNew == wOld && q.type == Injury::Break)) return false;
        q.type = t;
        q.sev = sev;
    }
    q.heal = 0;
    if (t == Injury::Cut) q.bandaged = false;
    q.changed = now;
    return true;
}

bool Health::hurt(BodyPart p, Injury t, int sev, double now) {
    if (dead_ || t == Injury::None || (int)p < 0 || (int)p >= kBodyParts) return false;
    const bool worse = hurtPart(parts_[(int)p], t, sev, now, true);
    if (worse) lastChange_ = now;
    checkDeath();
    return worse;
}

void Health::addBurnDose(BodyPart p, double dose, double now) {
    if (dead_ || dose <= 0) return;
    Part& q = parts_[(int)p];
    q.burnDose = std::min(q.burnDose + dose, 8.0);
    // Wärme taut Erfrierungs-Dosis weg
    q.frostDose = std::max(0.0, q.frostDose - dose);
    for (int k = 2; k >= 0; --k)
        if (q.burnDose >= kDoseLevels[k]) {
            if (hurtPart(q, Injury::Burn, k + 1, now, false)) lastChange_ = now;
            break;
        }
    checkDeath();
}

void Health::addFrostDose(BodyPart p, double dose, double now) {
    if (dead_ || dose <= 0) return;
    Part& q = parts_[(int)p];
    q.frostDose = std::min(q.frostDose + dose, 8.0);
    for (int k = 2; k >= 0; --k)
        if (q.frostDose >= kDoseLevels[k]) {
            if (hurtPart(q, Injury::Frost, k + 1, now, false)) lastChange_ = now;
            break;
        }
    checkDeath();
}

double Health::bleeding() const {
    double b = 0;
    for (const Part& q : parts_) {
        if (q.type == Injury::Cut && !q.bandaged) b += q.sev == 3 ? 0.010 : q.sev == 2 ? 0.003 : 0.0;
        if (q.type == Injury::Break && q.sev == 3 && !q.bandaged) b += 0.004;  // offener Bruch
    }
    return b;
}

void Health::update(double dt, double now, bool warm) {
    if (dead_ || dt <= 0) return;
    const double loss = bleeding();
    if (loss > 0) blood_ -= loss * dt;
    else blood_ = std::min(1.0, blood_ + 0.0015 * dt);
    for (Part& q : parts_) {
        q.burnDose = std::max(0.0, q.burnDose - 0.08 * dt);
        q.frostDose = std::max(0.0, q.frostDose - (warm ? 0.05 : 0.01) * dt);
        if (q.sev == 0) continue;
        double speed = 1.0;
        if (q.type == Injury::Frost && !warm) speed = 0.0;                     // in der Kälte heilt Erfrierung nicht
        if (q.type == Injury::Cut && !q.bandaged && q.sev >= 2) speed = q.sev == 2 ? 0.75 : 0.0;  // gerinnt / muss verbunden werden
        if (q.type == Injury::Break && q.sev == 3 && !q.bandaged) speed = 0.0;
        if (q.type == Injury::Burn && q.burnDose > 0.2) speed = 0.0;           // solange es noch heiß ist
        q.heal += dt * speed;
        if (q.heal >= healTime(q.type)) {
            q.heal = 0;
            if (--q.sev <= 0) {
                q.sev = 0;
                q.type = Injury::None;
                q.bandaged = false;
            } else if (q.type == Injury::Cut && q.sev < 2) {
                q.bandaged = false;  // verheilt bis auf einen Kratzer
            }
        }
    }
    checkDeath();
}

bool Health::bandage(double now, BodyPart* which) {
    int best = -1;
    double bestRate = 0;
    for (int i = 0; i < kBodyParts; ++i) {
        const Part& q = parts_[i];
        if (q.bandaged) continue;
        double r = 0;
        if (q.type == Injury::Cut) r = q.sev == 3 ? 3 : q.sev == 2 ? 2 : 0.5;
        if (q.type == Injury::Break && q.sev >= 2) r = q.sev;  // schienen
        if (q.type == Injury::Burn && q.sev >= 2) r = 0.4;
        if (r > bestRate) {
            bestRate = r;
            best = i;
        }
    }
    if (best < 0) return false;
    parts_[best].bandaged = true;
    parts_[best].changed = now;
    if (which) *which = (BodyPart)best;
    return true;
}

bool Health::pressWound(double now, BodyPart* which) {
    for (int i = 0; i < kBodyParts; ++i) {
        Part& q = parts_[i];
        if (q.type != Injury::Cut || q.sev < 3 || q.bandaged) continue;
        q.sev = 2;
        q.heal = 0;
        q.changed = now;
        if (which) *which = (BodyPart)i;
        return true;
    }
    return false;
}

void Health::healAll() {
    parts_ = {};
    blood_ = 1.0;
    dead_ = false;
    deathCause_.clear();
}

void Health::clear(BodyPart p) { parts_[(int)p] = Part{}; }

void Health::checkDeath() {
    if (dead_) return;
    if (blood_ <= kBloodDeath) {
        dead_ = true;
        deathCause_ = "verblutet";
        return;
    }
    int severe = 0;
    for (int i = 0; i < kBodyParts; ++i) {
        const Part& q = parts_[i];
        if (q.sev < 3) continue;
        ++severe;
        if (isCore(i) && q.type != Injury::Frost) {
            dead_ = true;
            deathCause_ = std::string(bodyPartName((BodyPart)i)) + ": " + injuryName(q.type) + ", " + injuryStatus(q.type, 3);
            return;
        }
    }
    if (severe >= 5) {
        dead_ = true;
        deathCause_ = "zu viele schwere Verletzungen";
    }
}

bool Health::injured() const { return worst() > 0 || blood_ < 0.995; }

int Health::worst() const {
    int w = 0;
    for (const Part& q : parts_) w = std::max(w, q.sev);
    return w;
}

// ============================================================================ Auswirkungen

double Health::moveFactor() const {
    double f = 1.0;
    for (int i = 0; i < kBodyParts; ++i) {
        if (!isLeg(i)) continue;
        const Part& q = parts_[i];
        double g = 1.0;
        if (q.sev == 1) g = 0.92;
        if (q.sev == 2) g = q.type == Injury::Break ? 0.6 : 0.75;
        if (q.sev == 3) g = 0.45;
        f = std::min(f, g);
    }
    // wenig Blut: schwach
    f *= 0.7 + 0.3 * std::clamp((blood_ - kBloodDeath) / (1.0 - kBloodDeath), 0.0, 1.0);
    return f;
}

bool Health::canRun() const {
    for (int i = 0; i < kBodyParts; ++i)
        if (isLeg(i) && parts_[i].sev >= 2) return false;
    return blood_ > 0.65;
}

bool Health::canJump() const {
    for (int i = 0; i < kBodyParts; ++i)
        if (isLeg(i) && parts_[i].type == Injury::Break && parts_[i].sev >= 2) return false;
    return true;
}

// ============================================================================ Speichern

std::string Health::save() const {
    std::ostringstream s;
    char buf[64];
    std::snprintf(buf, sizeof buf, "1;%.4f", blood_);
    s << buf;
    for (int i = 0; i < kBodyParts; ++i) {
        const Part& q = parts_[i];
        if (q.sev == 0 && q.burnDose < 0.01 && q.frostDose < 0.01) continue;
        std::snprintf(buf, sizeof buf, ";%s,%s,%d,%.1f,%d,%.3f,%.3f", kParts[i].id, injuryId(q.type), q.sev, q.heal,
                      q.bandaged ? 1 : 0, q.burnDose, q.frostDose);
        s << buf;
    }
    return s.str();
}

void Health::load(const std::string& text, bool exact) {
    healAll();
    std::vector<std::string> items;
    std::string cur;
    for (char c : text) {
        if (c == ';') {
            items.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    items.push_back(cur);
    if (items.size() < 2 || items[0] != "1") return;
    blood_ = std::clamp(std::atof(items[1].c_str()), exact ? 0.0 : kBloodDeath + 0.01, 1.0);
    for (size_t k = 2; k < items.size(); ++k) {
        std::vector<std::string> f;
        std::stringstream ss(items[k]);
        std::string v;
        while (std::getline(ss, v, ',')) f.push_back(v);
        if (f.size() < 7) continue;
        BodyPart p;
        if (!parseBodyPart(f[0], p)) continue;
        Part& q = parts_[(int)p];
        Injury t = Injury::None;
        parseInjury(f[1], t);
        q.type = t;
        q.sev = t == Injury::None ? 0 : std::clamp(std::atoi(f[2].c_str()), 0, 3);
        if (q.sev == 0) q.type = Injury::None;
        q.heal = std::max(0.0, std::atof(f[3].c_str()));
        q.bandaged = f[4] == "1";
        q.burnDose = std::clamp(std::atof(f[5].c_str()), 0.0, 8.0);
        q.frostDose = std::clamp(std::atof(f[6].c_str()), 0.0, 8.0);
    }
    // eine gespeicherte tödliche Lage nicht wieder laden: schwerste Kernverletzungen auf Stufe 2
    for (int i = 0; !exact && i <= (int)BodyPart::Belly; ++i)
        if (parts_[i].sev >= 3 && parts_[i].type != Injury::Frost) parts_[i].sev = 2;
}

std::string Health::describe() const {
    std::ostringstream s;
    char buf[96];
    std::snprintf(buf, sizeof buf, "Blut %.0f %%", blood_ * 100.0);
    s << buf;
    if (const double b = bleeding(); b > 0) {
        std::snprintf(buf, sizeof buf, ", verliert %.1f %%/s", b * 100.0);
        s << buf;
    }
    if (dead_) s << ", tot (" << deathCause_ << ")";
    bool any = false;
    for (int i = 0; i < kBodyParts; ++i) {
        const Part& q = parts_[i];
        if (q.sev == 0) continue;
        any = true;
        s << "\n  " << kParts[i].id << ": " << injuryName(q.type) << " " << q.sev << " (" << (q.bandaged ? "verbunden" : injuryStatus(q.type, q.sev)) << ")";
    }
    if (!any) s << "\n  keine Verletzungen";
    std::snprintf(buf, sizeof buf, "\n  Tempo %.0f %%%s%s", moveFactor() * 100.0, canRun() ? "" : ", kein Sprinten",
                  canJump() ? "" : ", kein Springen");
    s << buf;
    return s.str();
}
