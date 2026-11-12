// Werkzeuge in der Hand (Spiel-Modus): Axt (X), Laser, Erdbohrer, Planierer und Traktorstrahl (1-4).
// Die Wirkung läuft über Befehle (runLine), damit Mehrspieler und Skripte dasselbe tun wie die Maus.
#include "App.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "Icons.h"
#include "Scatter.h"
#include "Terrain.h"

namespace {
constexpr double kAxeReach = 2.6;      // m
constexpr double kLaserReach = 8.0;    // m
constexpr double kDrillReach = 2.5;    // m, auf Teile
constexpr double kDigReach = 6.0;      // m, auf Gelände
constexpr double kTractorReach = 10.0; // m
constexpr double kDigRadius = 0.6;     // m: Planierer und Erdbohrer graben eine Mulde von gut 1 m Breite
constexpr double kDigStep = 0.08;      // m je Schritt, solange die Taste gehalten wird
constexpr double kLaserStep = 0.08;    // s zwischen zwei Brennschritten des Lasers

// Punkt in cm (scale 100) oder Richtung (scale 1), für Befehle
std::string vecText(const glm::dvec3& v, double scale, int digits) {
    return fmtNum(v.x * scale, digits) + " " + fmtNum(v.y * scale, digits) + " " + fmtNum(v.z * scale, digits);
}
}

void App::selectHandTool(HandTool t) {
    handTool_ = t;
    if (t != HandTool::None) planMode_ = false;  // Bauvorlagen und Werkzeug schließen sich aus
    switch (t) {
    case HandTool::None:
        toast("Hand: Maus links nimmt Teile auf, rechts wirft sie (" + keys::label1(keys::Act::Tool1) + "-" + keys::label1(keys::Act::Tool8) +
              " wählt ein Werkzeug)");
        break;
    case HandTool::Axe:
        toast("Axt: Linksklick auf Holz hackt (" + keys::label1(keys::Act::Tool2) + " oder " + keys::label(keys::Act::Axe) +
              " legt sie zurück in die Hand)");
        break;
    case HandTool::Laser: toast("Laser: Linksklick halten brennt dort, wo du hinzielst (bis 8 m), " + keys::label1(keys::Act::Tool1) + " legt ihn weg"); break;
    case HandTool::Drill: toast("Erdbohrer: Linksklick bohrt in Teile, auf Gelände halten gräbt er eine Grube"); break;
    case HandTool::Terra: toast("Planierer: Linksklick halten gräbt, rechts halten füllt auf"); break;
    case HandTool::Tractor: toast("Traktorstrahl: Linksklick zieht Steine, Stöcke und Stämme bis 10 m ins Inventar"); break;
    case HandTool::Pickaxe: toast("Spitzhacke: Linksklick auf Stein oder Erz schlägt einen Brocken ab (er kommt ins Inventar)"); break;
    case HandTool::Lighter: toast("Feuerzeug: Maus links halten hält die Flamme an das Teil, das du anschaust (bis 3 m)"); break;
    }
}

// Loch bohren (Laser und Erdbohrer): von from aus in Richtung dir, Durchmesser und Tiefe in mm (Tiefe 0: durch das ganze Teil).
// newStep: ein neuer Schritt für Rückgängig. Sonst fügt sich das Bohren dem letzten Schritt an (der Laser brennt so in einem Zug).
// Rückgabe: Fehlermeldung, leer wenn es geklappt hat. Beim Mitspieler rechnet der Gastgeber den Befehl "drillat".
std::string App::boreHole(uint32_t id, const glm::dvec3& from, const glm::dvec3& dir, double diameterMm, double depthMm, bool newStep) {
    if (netClient()) {
        runLine("drillat " + std::to_string(id) + " " + vecText(from, 100, 1) + " " + vecText(dir, 1, 4) + " " + fmtNum(diameterMm, 1) + " " +
                    fmtNum(depthMm, 1),
                "drill");
        return "";
    }
    Object* o = world_->find(id);
    if (!o) return "part not found";
    const glm::dvec3 ndir = glm::normalize(dir);
    std::vector<uint32_t> ids;
    double removed = 0;
    std::string err;
    if (newStep) world_->pushUndo();
    if (!world_->drill(id, world_->toPartSpace(*o, from), world_->dirToPartSpace(*o, ndir), diameterMm / 1000.0, depthMm / 1000.0, &ids,
                       &removed, &err)) {
        if (newStep) world_->discardUndo();
        return err;
    }
    return "";
}

// Gelände heben (amount > 0) oder senken (amount < 0), in m. Beim Mitspieler rechnet der Gastgeber den Befehl "terrain".
void App::digGround(const glm::dvec3& p, double amount, double radius) {
    if (netClient()) {
        runLine(std::string(amount < 0 ? "terrain lower " : "terrain raise ") + fmtNum(p.x * 100, 1) + " " + fmtNum(p.z * 100, 1) + " " +
                    fmtNum(radius * 100, 0) + " " + fmtNum(std::abs(amount) * 100, 1),
                "terra");
        return;
    }
    if (Terrain* tr = world_->terrain()) tr->raise(p.x, p.z, radius, amount);
}

void App::handToolAction(const glm::dvec3& eye, const glm::dvec3& fwd, bool lmbDown, bool lmbPressed, bool rmbDown, bool rmbPressed) {
    if (handTool_ == HandTool::None) return;
    RayHit h;
    const bool hitAny = world_->raycast(eye, fwd, h, 0);
    const bool hitPart = hitAny && h.obj;  // sonst trifft der Strahl das Gelände (h.obj == nullptr)
    const bool ready = time_ >= toolReady_;
    switch (handTool_) {
    case HandTool::Axe:
        if (lmbPressed && ready && hitPart && h.t < kAxeReach) {
            toolReady_ = time_ + 0.4;  // ein Hieb braucht etwas Zeit
            const glm::dvec3 back = eye - h.point;  // Richtung zum Schlagenden
            runLine("chop " + std::to_string(h.obj->id) + " " + vecText(h.point, 100, 1) + " " + fmtNum(back.x, 3) + " " + fmtNum(back.z, 3),
                    "axe");
        }
        break;
    case HandTool::Laser:
        // Halten brennt dort, wo man hinzielt: je Schritt 2 cm tiefer in das Teil, 3 cm breit. Zieht man den Strahl
        // durch, entsteht ein Schnitt; so lange, bis das Teil durch ist. Ein Schritt ist ein Rückgängig-Schritt.
        if (lmbDown && ready) {
            if (!hitPart || h.t > kLaserReach) {
                if (lmbPressed) toast("Laser: auf ein Teil zielen (bis 8 m)", false);
                break;
            }
            toolReady_ = time_ + kLaserStep;
            const std::string e = boreHole(h.obj->id, h.point - fwd * 0.01, fwd, 30, 30, lmbPressed);
            if (!e.empty() && lmbPressed) toast("Laser: " + e, false);
        }
        break;
    case HandTool::Drill:
        if (hitPart && h.t < kDrillReach) {
            if (lmbPressed && ready) {
                // 2 cm vor der Oberfläche ansetzen, 12 mm Durchmesser, 8 cm tief
                toolReady_ = time_ + 0.3;
                if (const std::string e = boreHole(h.obj->id, h.point + h.normal * 0.02, -h.normal, 12, 80, true); !e.empty())
                    toast("Erdbohrer: " + e, false);
            }
        } else if (!hitPart && hitAny && h.t < kDigReach && lmbDown && ready) {
            // Gelände: halten gräbt die Grube tiefer
            toolReady_ = time_ + 0.2;
            digGround(h.point, -kDigStep, kDigRadius);
        }
        break;
    case HandTool::Terra: {
        // Planierer: gräbt und füllt den Boden, den der Blick trifft (Teile davor stören nicht)
        Terrain* tr = world_->terrain();
        double t = 0;
        glm::dvec3 n(0, 1, 0);
        if (!tr) {
            if (lmbPressed || rmbPressed) toast("Diese Welt ist flach: kein Gelände zum Graben", false);
        } else if ((lmbDown || rmbDown) && ready && tr->raycast(eye, fwd, kDigReach, t, n)) {
            toolReady_ = time_ + 0.2;
            digGround(eye + fwd * t, lmbDown ? -kDigStep : kDigStep, kDigRadius);
        }
        break;
    }
    case HandTool::Tractor:
        if (lmbPressed && ready && !netClient()) {
            toolReady_ = time_ + 0.2;
            // wie F: Zeug am Boden zuerst, sonst das Teil vor der Figur
            const double reach = hitPart ? std::min(h.t, kTractorReach) : kTractorReach;
            ScatterSpot spot;
            double ts = 0;
            Object* o = nullptr;
            if (world_->pickScatter(eye, fwd, reach, spot, ts)) o = world_->takeScatter(spot);
            else if (hitPart) o = h.obj;
            if (o && o->fixed) world_->wakeScatter(*o);
            if (o && !o->fixed) invStore(o->id);
            else if (o) toast(o->name + " ist fest und lässt sich nicht einstecken", false);
        }
        break;
    case HandTool::Pickaxe:
        if (lmbPressed && ready) {
            // Erst Zeug am Boden, das nur Bild ist (ein kleiner Stein), wird dabei ein Teil; dann der getroffene Stein
            Object* o = hitPart && h.t < kAxeReach ? h.obj : nullptr;
            glm::dvec3 at = h.point;
            ScatterSpot spot;
            double ts = 0;
            if (!netClient() && world_->pickScatter(eye, fwd, o ? h.t : kAxeReach, spot, ts))
                if (Object* s = world_->takeScatter(spot)) o = s, at = eye + fwd * ts;
            if (!o) break;
            toolReady_ = time_ + 0.45;  // ein Hieb braucht etwas Zeit
            const glm::dvec3 back = eye - at;
            if (netClient()) {  // der Gastgeber rechnet; der Brocken bleibt dort liegen
                runLine("mine " + std::to_string(o->id) + " " + vecText(at, 100, 1) + " " + vecText(back, 1, 3), "pickaxe");
                break;
            }
            world_->pushUndo();
            uint32_t chunk = 0;
            double removed = 0;
            std::string err;
            if (!world_->mine(o->id, at, back, &chunk, &removed, &err)) {
                world_->discardUndo();
                toast("Spitzhacke: " + err, false);
                break;
            }
            if (chunk) invStore(chunk);  // passt er nicht mehr hinein, fällt er herunter
        }
        break;
    case HandTool::None: break;
    }
}

// Leiste unten rechts (Spiel-Modus): Hand und Werkzeuge als runde Holzmarken mit eingravierten Symbolen (Icons.cpp,
// Stil der Wald-Menüs). Die gewählte Marke ist heller, hat einen moosgrünen Ring und darüber ihren Namen.
void App::drawHandToolBar() {
    struct Slot {
        HandTool tool;
        icons::Id icon;
        const char* name;
        keys::Act key;
    };
    static const Slot slots[] = {{HandTool::None, icons::Id::Hand, "Hand", keys::Act::Tool1},
                                 {HandTool::Axe, icons::Id::Axe, "Axt", keys::Act::Tool2},
                                 {HandTool::Laser, icons::Id::Laser, "Laser", keys::Act::Tool3},
                                 {HandTool::Drill, icons::Id::Drill, "Erdbohrer", keys::Act::Tool4},
                                 {HandTool::Terra, icons::Id::Shovel, "Planierer", keys::Act::Tool5},
                                 {HandTool::Tractor, icons::Id::Tractor, "Traktorstrahl", keys::Act::Tool6},
                                 {HandTool::Pickaxe, icons::Id::Pickaxe, "Spitzhacke", keys::Act::Tool7},
                                 {HandTool::Lighter, icons::Id::Lighter, "Feuerzeug", keys::Act::Tool8}};
    const int n = (int)(sizeof(slots) / sizeof(slots[0]));
    const float r = 28, gap = 9;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float statusH = statusBarHeight();  // im Spiel-Modus 0: keine Statusleiste
    const float x0 = io.DisplaySize.x - (n * 2 * r + (n - 1) * gap) - 18;
    const float cy = io.DisplaySize.y - statusH - r - 12;
    ImFont* fb = hudBold_ ? hudBold_ : ImGui::GetFont();
    const ImU32 cream = IM_COL32(243, 230, 198, 255), bright = IM_COL32(255, 244, 220, 255);

    for (int k = 0; k < n; ++k) {
        const ImVec2 c(x0 + r + k * (2 * r + gap), cy);
        const bool sel = handTool_ == slots[k].tool;
        icons::woodToken(dl, c, r, sel);
        // nicht gewählt: Creme etwas ins Holz gemischt (deckend, damit sich Strichenden nicht abzeichnen)
        ImVec4 ic = ImGui::ColorConvertU32ToFloat4(sel ? bright : cream);
        if (!sel) {
            const ImVec4 w = ImGui::ColorConvertU32ToFloat4(icons::tokenColor(false));
            ic = ImVec4(ic.x * 0.8f + w.x * 0.2f, ic.y * 0.8f + w.y * 0.2f, ic.z * 0.8f + w.z * 0.2f, 1);
        }
        icons::drawEngraved(dl, slots[k].icon, c, 36, ImGui::ColorConvertFloat4ToU32(ic), 2.5f);

        // Taste als kleine Plakette unten rechts (längere Namen: länglich)
        const std::string key = keys::label1(slots[k].key);
        if (!key.empty()) {
            const ImVec2 kc(c.x + r * 0.77f, c.y + r * 0.77f);
            const float ks = 13.0f;
            const ImVec2 kt = fb->CalcTextSizeA(ks, FLT_MAX, 0, key.c_str());
            const float hw = std::max(0.0f, kt.x * 0.5f - 4.0f);
            const ImVec2 ka(kc.x - hw - 8.5f, kc.y - 8.5f), kb(kc.x + hw + 8.5f, kc.y + 8.5f);
            dl->AddRectFilled(ka, kb, sel ? IM_COL32(95, 138, 53, 255) : IM_COL32(36, 21, 10, 255), 8.5f);
            dl->AddRect(ka, kb, sel ? IM_COL32(182, 224, 122, 255) : IM_COL32(201, 151, 92, 130), 8.5f, 0, 1.0f);
            dl->AddText(fb, ks, ImVec2(kc.x - kt.x * 0.5f, kc.y - kt.y * 0.5f), sel ? bright : cream, key.c_str());
        }

        if (sel) {  // Name des gewählten Werkzeugs auf einem Holzschild über der Marke
            const float ns = 16.0f;
            const ImVec2 ts = fb->CalcTextSizeA(ns, FLT_MAX, 0, slots[k].name);
            const ImVec2 a(c.x - ts.x * 0.5f - 10, c.y - r - 14 - ts.y - 4), b(c.x + ts.x * 0.5f + 10, c.y - r - 10);
            dl->AddRectFilled(ImVec2(a.x, a.y + 2), ImVec2(b.x, b.y + 2), IM_COL32(0, 0, 0, 70), 5);
            dl->AddRectFilledMultiColor(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), IM_COL32(74, 47, 26, 255),
                                        IM_COL32(74, 47, 26, 255), IM_COL32(46, 28, 15, 255), IM_COL32(46, 28, 15, 255));
            dl->AddLine(ImVec2(a.x + 4, a.y + 2), ImVec2(b.x - 4, a.y + 2), IM_COL32(201, 151, 92, 90), 1.0f);
            dl->AddRect(a, b, IM_COL32(27, 16, 8, 255), 5, 0, 1.5f);
            dl->AddText(fb, ns, ImVec2(c.x - ts.x * 0.5f, a.y + 2 + ((b.y - a.y) - ts.y) * 0.5f - 1), cream, slots[k].name);
        }
    }
}
