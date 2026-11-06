// Leben der Spielfigur: keine Lebenspunkte, sondern Verletzungen je Körperteil (Health.cpp, Anzeige in AppHealth.cpp).
//
// Jeder der 16 Körperteile hat höchstens eine Verletzung (die schwerste zählt) mit einer Art (Verbrennung, Erfrierung,
// Schnitt, Prellung, Bruch) und einer Schwere 1..3. Verletzungen heilen mit der Zeit Stufe um Stufe. Schnitte ab Stufe 2
// bluten, bis sie verbunden sind (H) oder von selbst gerinnen; Blut kommt langsam wieder.
// Tod: zu viel Blut verloren, Kopf, Hals, Brust oder Bauch schwer (Stufe 3, außer Erfrierung) oder sehr viele schwere
// Verletzungen auf einmal.
// Hitze und Kälte wirken als Dosis: sie sammelt sich je Körperteil an, klingt langsam ab und macht beim Überschreiten von
// 1, 2,5 und 5 eine Verbrennung bzw. Erfrierung der Stufe 1, 2, 3.
#pragma once

#include <array>
#include <string>
#include <vector>

enum class BodyPart : int {
    Head, Neck, Chest, Belly,
    UpperArmL, LowerArmL, HandL, UpperArmR, LowerArmR, HandR,
    UpperLegL, LowerLegL, FootL, UpperLegR, LowerLegR, FootR,
    Count
};
constexpr int kBodyParts = (int)BodyPart::Count;

enum class Injury : int { None = 0, Burn, Frost, Cut, Bruise, Break };

const char* bodyPartName(BodyPart p);            // "Linke Hand" (deutsch, für die Anzeige)
const char* bodyPartId(BodyPart p);              // "hand_l" (Befehle, Speichern)
const char* injuryName(Injury t);                // "Verbrennung"
const char* injuryId(Injury t);                  // "burn"
const char* injuryStatus(Injury t, int sev);     // "Brandblasen", "blutet stark" ...
bool parseBodyPart(const std::string& s, BodyPart& out);
bool parseInjury(const std::string& s, Injury& out);

class Health {
public:
    struct Part {
        Injury type = Injury::None;
        int sev = 0;            // 0 = gesund, 1..3
        double heal = 0;        // Fortschritt bis zur nächsten leichteren Stufe (s)
        bool bandaged = false;  // Schnitt verbunden: blutet nicht mehr
        double burnDose = 0, frostDose = 0;
        double changed = -1e9;  // Spielzeit der letzten Verschlechterung (Anzeige: kurz hervorheben)
    };

    // Verletzung zufügen. Die schwerere bleibt; gleiche Art und Schwere ab 2 wird eine Stufe schlimmer. true: verschlechtert
    bool hurt(BodyPart p, Injury t, int sev, double now);
    // Hitze/Kälte-Dosis (Stufen-Einheiten) aufaddieren; macht beim Überschreiten der Schwellen Verletzungen
    void addBurnDose(BodyPart p, double dose, double now);
    void addFrostDose(BodyPart p, double dose, double now);
    // warm: Körper ist im Warmen (Erfrierungen heilen, Kälte-Dosis klingt schneller ab)
    void update(double dt, double now, bool warm);
    // Den am stärksten blutenden Schnitt verbinden. false: nichts zu verbinden
    bool bandage(double now, BodyPart* which = nullptr);
    // Ohne Verband: Druck auf den am stärksten blutenden Schnitt, aus "blutet stark" wird "blutet". false: keiner
    bool pressWound(double now, BodyPart* which = nullptr);
    void healAll();
    void clear(BodyPart p);

    bool dead() const { return dead_; }
    const std::string& deathCause() const { return deathCause_; }
    double blood() const { return blood_; }             // 1 = voll, Tod bei kBloodDeath
    void setBlood(double b) { blood_ = b; }
    double bleeding() const;                             // Blutverlust pro Sekunde
    bool injured() const;                                // irgendeine Verletzung oder Blutverlust
    int worst() const;                                   // schwerste Stufe (0 = gesund)
    double lastChange() const { return lastChange_; }    // Spielzeit der letzten Verschlechterung
    const Part& part(BodyPart p) const { return parts_[(int)p]; }

    // Auswirkungen
    double moveFactor() const;  // Laufgeschwindigkeit (Beine)
    bool canRun() const;
    bool canJump() const;

    std::string save() const;            // eine Zeile Text
    // exact: so wie gespeichert (Mitspieler: Zustand vom Gastgeber), sonst eine tödliche Lage abgeschwächt
    void load(const std::string& text, bool exact = false);
    void setDeath(const std::string& cause) { dead_ = !cause.empty(); deathCause_ = cause; }
    void touch(BodyPart p, double now) { parts_[(int)p].changed = lastChange_ = now; }  // Anzeige: gerade verschlimmert
    std::string describe() const;        // für Befehle (deutsch)

    static constexpr double kBloodDeath = 0.4;

private:
    void checkDeath();
    std::array<Part, kBodyParts> parts_;
    double blood_ = 1.0;
    bool dead_ = false;
    std::string deathCause_;
    double lastChange_ = -1e9;
};
