// Tastenbelegung: jede Aktion, die eine Taste auslöst, mit Namen, Bereich und Standardtasten. Was man ändert, steht in
// settings.json unter "keys" (nur was vom Standard abweicht) und wird auf der Seite Einstellungen → Steuerung bearbeitet.
// Das Spiel fragt Tasten nur über pressed() / down() ab. Esc und die Strg-Kürzel im Bau-Modus sind fest (fixedRows).
#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>

namespace keys {

enum class Act : int {
    // Spiel-Modus: Bewegen
    Forward, Back, Left, Right, Jump, Run, Sneak,
    // Spiel-Modus: Fenster
    Inventory, Craft, Build, Map,
    // Spiel-Modus: Handeln
    Use, Rotate, Dismantle, PlanFixed, Bandage, Eat, Axe,
    // Spiel-Modus: Werkzeuge der Leiste (1 Hand … 7 Spitzhacke, 8 Feuerzeug)
    Tool1, Tool2, Tool3, Tool4, Tool5, Tool6, Tool7, Tool8, Tool9,
    // Bau-Modus
    BSelect, BSaw, BDrill, BRound, BMill, BPlane, BHeat, SawX, SawY, SawZ,
    BDelete, BFocus, BDrop, BTurn,
    View1, View2, View3, View4, View5,
    // überall
    ModeSwitch, Console, Debug, Inspector, Drawing, ViewSwitch, Photo,
    Count
};
constexpr int kActs = (int)Act::Count;

// Wo eine Taste gilt: zwei Aktionen dürfen dieselbe Taste haben, wenn sie nie zugleich gelten (Spiel- / Bau-Modus)
enum class Ctx { Play, Build, Global };

struct ActInfo {
    const char* id;     // Schlüssel in settings.json ("jump")
    const char* label;  // Anzeige ("Springen")
    const char* group;  // Bereich auf der Einstellungsseite ("Bewegen")
    Ctx ctx;
    const char* def;    // Standardtasten, mit Leerzeichen getrennt ("V Q")
};
const ActInfo& info(Act a);
bool byId(const std::string& id, Act& out);

// Tastennamen (so stehen sie in settings.json): "A" … "Z", "0" … "9", "F1" … "F12", "Space", "Tab", "Enter",
// "Backspace", "Delete", "Insert", "Home", "End", "PageUp", "PageDown", "Up", "Down", "Left", "Right", "Shift", "Ctrl",
// "Alt", "CapsLock", "^", "Comma", "Period", "Minus", "Num0" … "Num9"
bool validKey(const std::string& name);
std::string keyText(const std::string& name);  // Anzeige: "Leertaste", "Strg", "↑"

// Belegung einer Aktion: bis zu zwei Tasten (leer: keine)
using Binding = std::array<std::string, 2>;
using Overrides = std::map<std::string, std::string>;  // settings.json "keys": id -> "W Up" (leer: keine Taste)
Binding defaults(Act a);
Binding binding(const Overrides& o, Act a);
// Belegung setzen; ist sie gleich dem Standard, fällt der Eintrag weg
void setBinding(Overrides& o, Act a, const Binding& b);
// Welche andere Aktion dieselbe Taste hat und zugleich gilt (Anzeige "auch bei …"); leer: keine
std::string conflict(const Overrides& o, Act a, int slot);

// Feste Eingaben, die auf der Einstellungsseite nur angezeigt werden (Maus, Esc, Strg-Kürzel)
struct FixedRow {
    const char* group;
    const char* label;
    const char* keys;
};
const std::vector<FixedRow>& fixedRows();
const std::vector<const char*>& groups();  // Reihenfolge der Bereiche

// ---- im Spiel (ImGui-Tasten)
void use(const Overrides& o);  // Belegung übernehmen (beim Start und wenn sich settings.json ändert)
bool pressed(Act a);           // eine der Tasten wurde eben gedrückt
bool down(Act a);              // eine der Tasten ist gedrückt
std::string label(Act a);      // Anzeige der Tasten für Hinweise: "E", "W / ↑"; "–" ohne Taste
std::string label1(Act a);     // nur die erste Taste ("E"); leer ohne Taste
std::string moveLabel();       // die vier Lauftasten zusammen: "WASD" (sonst mit Leerzeichen getrennt)

} // namespace keys
