// Tastenbelegung (siehe Keys.h)
#include "Keys.h"

#include <imgui.h>

#include <sstream>

namespace keys {
namespace {

// Reihenfolge wie enum Act
const ActInfo kActs_[kActs] = {
    {"forward", "Vorwärts", "Bewegen", Ctx::Play, "W"},
    {"back", "Rückwärts", "Bewegen", Ctx::Play, "S"},
    {"left", "Links", "Bewegen", Ctx::Play, "A"},
    {"right", "Rechts", "Bewegen", Ctx::Play, "D"},
    {"jump", "Springen / im Wasser hoch", "Bewegen", Ctx::Play, "Space"},
    {"run", "Rennen", "Bewegen", Ctx::Play, "Shift"},
    {"sneak", "Schleichen / im Wasser tauchen", "Bewegen", Ctx::Play, "Ctrl"},
    {"inventory", "Inventar / einstecken", "Fenster", Ctx::Play, "E"},
    {"craft", "Herstellen", "Fenster", Ctx::Play, "C"},
    {"build", "Bauen", "Fenster", Ctx::Play, "B"},
    {"map", "Karte", "Fenster", Ctx::Play, "M"},
    {"use", "Aufheben (ins Inventar), Zerlegen", "Handeln", Ctx::Play, "F"},
    {"rotate", "Drehen (Shift: andere Achse)", "Handeln", Ctx::Play, "R"},
    {"dismantle", "Abbauen (halten)", "Handeln", Ctx::Play, "Q"},
    {"planFixed", "Bauvorlage fest / lose", "Handeln", Ctx::Play, "G"},
    {"bandage", "Wunde verbinden", "Handeln", Ctx::Play, "H"},
    {"eat", "Essen (aus dem Inventar)", "Handeln", Ctx::Play, "V"},
    {"axe", "Axt nehmen / weglegen", "Handeln", Ctx::Play, "X"},
    {"tool1", "Hand", "Werkzeuge", Ctx::Play, "1"},
    {"tool2", "Axt", "Werkzeuge", Ctx::Play, "2"},
    {"tool3", "Laser", "Werkzeuge", Ctx::Play, "3"},
    {"tool4", "Erdbohrer", "Werkzeuge", Ctx::Play, "4"},
    {"tool5", "Planierer", "Werkzeuge", Ctx::Play, "5"},
    {"tool6", "Traktorstrahl", "Werkzeuge", Ctx::Play, "6"},
    {"tool7", "Spitzhacke", "Werkzeuge", Ctx::Play, "7"},
    {"tool8", "Feuerzeug", "Werkzeuge", Ctx::Play, "8"},
    {"buildSelect", "Auswählen / Bewegen", "Bau-Modus", Ctx::Build, "V Q"},
    {"buildSaw", "Säge", "Bau-Modus", Ctx::Build, "S"},
    {"buildDrill", "Bohrer", "Bau-Modus", Ctx::Build, "B"},
    {"buildRound", "Abrunden / Fasen", "Bau-Modus", Ctx::Build, "E"},
    {"buildMill", "Fräsen", "Bau-Modus", Ctx::Build, "M"},
    {"buildPlane", "Hobeln", "Bau-Modus", Ctx::Build, "P"},
    {"buildHeat", "Wärme / Kälte", "Bau-Modus", Ctx::Build, "H"},
    {"sawX", "Säge: Achse X", "Bau-Modus", Ctx::Build, "X"},
    {"sawY", "Säge: Achse Y", "Bau-Modus", Ctx::Build, "Y"},
    {"sawZ", "Säge: Achse Z", "Bau-Modus", Ctx::Build, "Z"},
    {"buildDelete", "Löschen", "Bau-Modus", Ctx::Build, "Delete"},
    {"buildFocus", "Auf die Auswahl zoomen", "Bau-Modus", Ctx::Build, "F"},
    {"buildDrop", "Absetzen", "Bau-Modus", Ctx::Build, "G"},
    {"buildTurn", "90° drehen (Shift: andere Achse)", "Bau-Modus", Ctx::Build, "R"},
    {"view1", "Ansicht: Perspektive", "Bau-Modus", Ctx::Build, "1"},
    {"view2", "Ansicht: Oben", "Bau-Modus", Ctx::Build, "2"},
    {"view3", "Ansicht: Vorne", "Bau-Modus", Ctx::Build, "3"},
    {"view4", "Ansicht: Seite", "Bau-Modus", Ctx::Build, "4"},
    {"view5", "Ansicht: alle vier", "Bau-Modus", Ctx::Build, "5"},
    {"modeSwitch", "Modus wechseln (Bauen / Spielen)", "Überall", Ctx::Global, "Tab"},
    {"console", "Konsole", "Überall", Ctx::Global, "F1 ^"},
    {"debug", "Debug-Ansicht (Ort, FPS, Rechenzeit)", "Überall", Ctx::Global, "F3"},
    {"inspector", "Physik-Inspektor", "Überall", Ctx::Global, "F6"},
    {"drawing", "Dreitafelprojektion", "Überall", Ctx::Global, "F4"},
    {"viewSwitch", "Ansicht: Augen / Schulter", "Überall", Ctx::Global, "F5"},
    {"photo", "Foto", "Überall", Ctx::Global, "F12"},
};

struct KeyName {
    const char* name;
    const char* text;
    ImGuiKey key;
};

const std::vector<KeyName>& keyNames() {
    static const std::vector<KeyName> list = [] {
        std::vector<KeyName> v;
        static const char* const letters[26] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                                "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
        for (int i = 0; i < 26; ++i) v.push_back({letters[i], letters[i], (ImGuiKey)(ImGuiKey_A + i)});
        static const char* const digits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
        for (int i = 0; i < 10; ++i) v.push_back({digits[i], digits[i], (ImGuiKey)(ImGuiKey_0 + i)});
        static const char* const fkeys[12] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
        for (int i = 0; i < 12; ++i) v.push_back({fkeys[i], fkeys[i], (ImGuiKey)(ImGuiKey_F1 + i)});
        static const char* const pad[10] = {"Num0", "Num1", "Num2", "Num3", "Num4", "Num5", "Num6", "Num7", "Num8", "Num9"};
        static const char* const padText[10] = {"Num 0", "Num 1", "Num 2", "Num 3", "Num 4", "Num 5", "Num 6", "Num 7", "Num 8", "Num 9"};
        for (int i = 0; i < 10; ++i) v.push_back({pad[i], padText[i], (ImGuiKey)(ImGuiKey_Keypad0 + i)});
        const KeyName more[] = {
            {"Space", "Leertaste", ImGuiKey_Space},     {"Tab", "Tab", ImGuiKey_Tab},
            {"Enter", "Eingabe", ImGuiKey_Enter},       {"Backspace", "Rücktaste", ImGuiKey_Backspace},
            {"Delete", "Entf", ImGuiKey_Delete},        {"Insert", "Einfg", ImGuiKey_Insert},
            {"Home", "Pos1", ImGuiKey_Home},            {"End", "Ende", ImGuiKey_End},
            {"PageUp", "Bild ↑", ImGuiKey_PageUp},      {"PageDown", "Bild ↓", ImGuiKey_PageDown},
            {"Up", "↑", ImGuiKey_UpArrow},              {"Down", "↓", ImGuiKey_DownArrow},
            {"Left", "←", ImGuiKey_LeftArrow},          {"Right", "→", ImGuiKey_RightArrow},
            {"Shift", "Shift", ImGuiKey_LeftShift},     {"Ctrl", "Strg", ImGuiKey_LeftCtrl},
            {"Alt", "Alt", ImGuiKey_LeftAlt},           {"CapsLock", "Feststell", ImGuiKey_CapsLock},
            {"^", "^", ImGuiKey_GraveAccent},           {"Comma", ",", ImGuiKey_Comma},
            {"Period", ".", ImGuiKey_Period},           {"Minus", "-", ImGuiKey_Minus},
        };
        for (const KeyName& k : more) v.push_back(k);
        return v;
    }();
    return list;
}

const KeyName* findKey(const std::string& name) {
    for (const KeyName& k : keyNames())
        if (name == k.name) return &k;
    return nullptr;
}

Binding parse(const std::string& s) {
    Binding b;
    std::istringstream in(s);
    std::string w;
    int n = 0;
    while (n < 2 && in >> w)
        if (validKey(w)) b[n++] = w;
    return b;
}

bool overlaps(Ctx a, Ctx b) { return a == b || a == Ctx::Global || b == Ctx::Global; }

// im Spiel: je Aktion zwei ImGui-Tasten (ImGuiKey_None: keine)
ImGuiKey gKeys[kActs][2] = {};
bool gReady = false;

bool keyDown(ImGuiKey k) {
    if (k == ImGuiKey_None) return false;
    if (k == ImGuiKey_LeftShift) return ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    return ImGui::IsKeyDown(k);  // Strg und Alt nur links: AltGr meldet sich als Strg + Alt rechts
}

bool keyPressed(ImGuiKey k) {
    if (k == ImGuiKey_None) return false;
    if (k == ImGuiKey_LeftShift) return ImGui::IsKeyPressed(ImGuiKey_LeftShift, false) || ImGui::IsKeyPressed(ImGuiKey_RightShift, false);
    return ImGui::IsKeyPressed(k, false);
}

} // namespace

const ActInfo& info(Act a) { return kActs_[(int)a]; }

bool byId(const std::string& id, Act& out) {
    for (int i = 0; i < kActs; ++i)
        if (id == kActs_[i].id) {
            out = (Act)i;
            return true;
        }
    return false;
}

bool validKey(const std::string& name) { return findKey(name) != nullptr; }

std::string keyText(const std::string& name) {
    const KeyName* k = findKey(name);
    return k ? k->text : name;
}

Binding defaults(Act a) { return parse(info(a).def); }

Binding binding(const Overrides& o, Act a) {
    const auto it = o.find(info(a).id);
    return it == o.end() ? defaults(a) : parse(it->second);
}

void setBinding(Overrides& o, Act a, const Binding& b) {
    Binding c = b;
    if (c[0].empty()) std::swap(c[0], c[1]);  // die erste Taste zuerst
    if (c[0] == c[1]) c[1].clear();
    if (c == defaults(a)) {
        o.erase(info(a).id);
        return;
    }
    o[info(a).id] = c[1].empty() ? c[0] : c[0] + " " + c[1];
}

std::string conflict(const Overrides& o, Act a, int slot) {
    const std::string k = binding(o, a)[(size_t)slot & 1];
    if (k.empty()) return "";
    for (int i = 0; i < kActs; ++i) {
        if (i == (int)a || !overlaps(info(a).ctx, kActs_[i].ctx)) continue;
        const Binding b = binding(o, (Act)i);
        if (b[0] == k || b[1] == k) return kActs_[i].label;
    }
    return "";
}

const std::vector<FixedRow>& fixedRows() {
    static const std::vector<FixedRow> rows = {
        {"Bewegen", "Umschauen", "Maus"},
        {"Handeln", "Tragen (halten), Werkzeug benutzen, Schlag", "Linke Maustaste"},
        {"Handeln", "Werfen (halten, loslassen)", "Rechte Maustaste"},
        {"Handeln", "Abstand des Getragenen", "Mausrad"},
        {"Bau-Modus", "Rückgängig / Wiederholen", "Strg+Z / Strg+Y"},
        {"Bau-Modus", "Duplizieren / Alles auswählen", "Strg+D / Strg+A"},
        {"Bau-Modus", "Speichern / Laden", "Strg+S / Strg+O"},
        {"Überall", "Pause, Fenster schließen", "Esc"},
    };
    return rows;
}

const std::vector<const char*>& groups() {
    static const std::vector<const char*> g = {"Bewegen", "Fenster", "Handeln", "Werkzeuge", "Bau-Modus", "Überall"};
    return g;
}

void use(const Overrides& o) {
    for (int i = 0; i < kActs; ++i) {
        const Binding b = binding(o, (Act)i);
        for (int s = 0; s < 2; ++s) {
            const KeyName* k = b[s].empty() ? nullptr : findKey(b[s]);
            gKeys[i][s] = k ? k->key : ImGuiKey_None;
        }
    }
    gReady = true;
}

bool pressed(Act a) {
    if (!gReady) use({});
    return keyPressed(gKeys[(int)a][0]) || keyPressed(gKeys[(int)a][1]);
}

bool down(Act a) {
    if (!gReady) use({});
    return keyDown(gKeys[(int)a][0]) || keyDown(gKeys[(int)a][1]);
}

std::string label(Act a) {
    if (!gReady) use({});
    std::string s;
    for (int i = 0; i < 2; ++i) {
        const ImGuiKey k = gKeys[(int)a][i];
        if (k == ImGuiKey_None) continue;
        for (const KeyName& n : keyNames())
            if (n.key == k) {
                s += (s.empty() ? "" : " / ") + std::string(n.text);
                break;
            }
    }
    return s.empty() ? "–" : s;
}

std::string label1(Act a) {
    if (!gReady) use({});
    const ImGuiKey k = gKeys[(int)a][0] != ImGuiKey_None ? gKeys[(int)a][0] : gKeys[(int)a][1];
    for (const KeyName& n : keyNames())
        if (n.key == k) return n.text;
    return "";
}

std::string moveLabel() {
    const Act order[4] = {Act::Forward, Act::Left, Act::Back, Act::Right};
    std::string s;
    bool single = true;
    for (Act a : order) single = single && label1(a).size() == 1;
    for (Act a : order) {
        const std::string t = label1(a);
        if (t.empty()) continue;
        s += (s.empty() || single ? "" : " ") + t;
    }
    return s;
}

} // namespace keys
