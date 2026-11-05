// Werkzeuge in der Hand (Spiel-Modus): Axt (X), Laser, Erdbohrer, Planierer und Traktorstrahl (1-4).
// Die Wirkung läuft über Befehle (runLine), damit Mehrspieler und Skripte dasselbe tun wie die Maus.
#include "App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

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
    case HandTool::None: toast("Hand: Maus links nimmt Teile auf, rechts wirft sie (1-7 wählt ein Werkzeug)"); break;
    case HandTool::Axe: toast("Axt: Linksklick auf Holz hackt (2 oder X legt sie zurück in die Hand)"); break;
    case HandTool::Laser: toast("Laser: Linksklick halten brennt dort, wo du hinzielst (bis 8 m), 1 legt ihn weg"); break;
    case HandTool::Drill: toast("Erdbohrer: Linksklick bohrt in Teile, auf Gelände halten gräbt er eine Grube"); break;
    case HandTool::Terra: toast("Planierer: Linksklick halten gräbt, rechts halten füllt auf"); break;
    case HandTool::Tractor: toast("Traktorstrahl: Linksklick zieht Steine, Stöcke und Stämme bis 10 m ins Inventar"); break;
    case HandTool::Pickaxe: toast("Spitzhacke: Linksklick auf Stein oder Erz schlägt einen Brocken ab (er kommt ins Inventar)"); break;
    }
}

std::string App::handToolHelp() const {
    const std::string tail = " · WASD laufen · Leertaste springen · 1 Hand" + std::string(cheats_ ? " · Tab Bauen" : "");
    switch (handTool_) {
    case HandTool::Axe: return "AXT: Maus links hackt (auf Holz zielen, bis 2,6 m)" + tail;
    case HandTool::Laser: return "LASER: Maus links halten brennt, wo du hinzielst (bis 8 m)" + tail;
    case HandTool::Drill: return "ERDBOHRER: Maus links bohrt in Teile, halten gräbt im Gelände" + tail;
    case HandTool::Terra: return "PLANIERER: Maus links halten gräbt, rechts halten füllt auf" + tail;
    case HandTool::Tractor: return "TRAKTORSTRAHL: Maus links zieht ins Inventar (bis 10 m)" + tail;
    case HandTool::Pickaxe: return "SPITZHACKE: Maus links schlägt Brocken aus Stein und Erz (bis 2,6 m)" + tail;
    case HandTool::None: break;
    }
    return "";
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

// Leiste unten rechts (Spiel-Modus): Hand und vier Werkzeuge als kleine Symbole, das gewählte ist hervorgehoben.
// Die Symbole sind einfache Formen (ImGui-Zeichenliste), damit keine Bilddateien nötig sind.
void App::drawHandToolBar() {
    struct Slot {
        HandTool tool;
        const char* name;
        const char* key;
    };
    static const Slot slots[] = {{HandTool::None, "Hand", "1"},          {HandTool::Axe, "Axt", "2"},
                                 {HandTool::Laser, "Laser", "3"},        {HandTool::Drill, "Erdbohrer", "4"},
                                 {HandTool::Terra, "Planierer", "5"},    {HandTool::Tractor, "Traktorstrahl", "6"},
                                 {HandTool::Pickaxe, "Spitzhacke", "7"}};
    const int n = (int)(sizeof(slots) / sizeof(slots[0]));
    const float size = 58, gap = 6;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float statusH = ImGui::GetFrameHeight() + 6;  // so hoch ist die Statusleiste unten (drawStatusBar)
    const float x0 = io.DisplaySize.x - (n * size + (n - 1) * gap) - 16;
    const float y0 = io.DisplaySize.y - statusH - size - 10;

    // Symbol mit Mittelpunkt c; Koordinaten u, v von -1 bis 1 (v nach unten)
    auto icon = [&](HandTool t, ImVec2 c) {
        const float r = 21.0f;
        auto P = [&](float u, float v) { return ImVec2(c.x + u * r, c.y + v * r); };
        const ImU32 skin = IM_COL32(230, 180, 140, 255), wood = IM_COL32(150, 100, 55, 255), steel = IM_COL32(185, 190, 200, 255);
        switch (t) {
        case HandTool::None:  // offene Hand: Handfläche, vier Finger, Daumen
            dl->AddRectFilled(P(-0.5f, -0.1f), P(0.5f, 0.9f), skin, 0.3f * r);
            for (int i = 0; i < 4; ++i) {
                const float x = -0.42f + i * 0.28f;
                dl->AddRectFilled(P(x - 0.11f, -0.95f), P(x + 0.11f, 0.05f), skin, 0.11f * r);
            }
            dl->AddLine(P(-0.4f, 0.3f), P(-0.85f, -0.2f), skin, 0.26f * r);
            break;
        case HandTool::Axe:  // Holzstiel, Stahlkopf
            dl->AddLine(P(-0.55f, 0.9f), P(0.2f, -0.45f), wood, 0.2f * r);
            dl->AddQuadFilled(P(-0.1f, -0.3f), P(0.05f, -0.9f), P(0.85f, -0.75f), P(0.3f, -0.25f), steel);
            break;
        case HandTool::Laser:  // Stift mit rotem Strahl und Lichtpunkt
            dl->AddLine(P(-0.75f, 0.75f), P(0.05f, -0.05f), IM_COL32(95, 100, 112, 255), 0.42f * r);
            dl->AddLine(P(0.1f, -0.1f), P(0.9f, -0.9f), IM_COL32(255, 60, 60, 90), 0.5f * r);
            dl->AddLine(P(0.1f, -0.1f), P(0.9f, -0.9f), IM_COL32(255, 70, 70, 255), 0.12f * r);
            dl->AddCircleFilled(P(0.9f, -0.9f), 0.16f * r, IM_COL32(255, 120, 120, 255));
            break;
        case HandTool::Drill:  // Gehäuse in Blau, Griff, Spannfutter, Bohrer
            dl->AddRectFilled(P(-0.9f, -0.3f), P(0.1f, 0.25f), IM_COL32(55, 110, 200, 255), 0.2f * r);
            dl->AddRectFilled(P(-0.5f, 0.2f), P(-0.15f, 0.9f), IM_COL32(40, 40, 46, 255), 0.1f * r);
            dl->AddRectFilled(P(0.1f, -0.15f), P(0.35f, 0.1f), IM_COL32(120, 125, 135, 255));
            dl->AddTriangleFilled(P(0.35f, -0.2f), P(0.35f, 0.15f), P(0.98f, -0.02f), IM_COL32(205, 210, 220, 255));
            break;
        case HandTool::Terra:  // Schaufel: Stiel und Blatt
            dl->AddLine(P(-0.75f, -0.9f), P(0.05f, 0.1f), wood, 0.2f * r);
            {
                const ImVec2 blade[] = {P(0.05f, 0.05f), P(0.55f, 0.2f), P(0.8f, 0.6f), P(0.6f, 0.95f), P(0.15f, 0.95f), P(-0.05f, 0.5f)};
                dl->AddConvexPolyFilled(blade, 6, IM_COL32(160, 165, 175, 255));
            }
            break;
        case HandTool::Tractor:  // Gerät mit Sog-Kegel, der Steine einsammelt
            dl->AddTriangleFilled(P(-0.85f, 0.05f), P(0.9f, -0.75f), P(0.9f, 0.85f), IM_COL32(90, 180, 255, 90));
            dl->AddRectFilled(P(-0.95f, 0.1f), P(-0.7f, 0.9f), IM_COL32(60, 62, 70, 255), 0.08f * r);
            dl->AddCircleFilled(P(-0.85f, 0.05f), 0.22f * r, IM_COL32(120, 200, 255, 255));
            dl->AddCircleFilled(P(0.5f, -0.2f), 0.13f * r, IM_COL32(150, 140, 120, 255));
            dl->AddCircleFilled(P(0.65f, 0.4f), 0.11f * r, IM_COL32(150, 140, 120, 255));
            dl->AddCircleFilled(P(0.3f, 0.55f), 0.12f * r, IM_COL32(150, 140, 120, 255));
            break;
        case HandTool::Pickaxe: {  // Holzstiel, gebogener Stahlkopf mit zwei Spitzen, darunter ein Erzbrocken
            dl->AddCircleFilled(P(0.55f, 0.7f), 0.24f * r, IM_COL32(110, 70, 50, 255));
            dl->AddCircleFilled(P(0.48f, 0.64f), 0.07f * r, IM_COL32(200, 120, 70, 255));
            dl->AddLine(P(-0.6f, 0.9f), P(0.15f, -0.55f), wood, 0.2f * r);
            const ImVec2 head[] = {P(-0.85f, -0.35f), P(-0.3f, -0.72f), P(0.15f, -0.78f), P(0.6f, -0.72f), P(0.95f, -0.3f),
                                   P(0.55f, -0.55f), P(0.15f, -0.58f), P(-0.25f, -0.55f)};
            dl->AddPolyline(head, 8, steel, ImDrawFlags_Closed, 0.12f * r);
            dl->AddConvexPolyFilled(head + 1, 3, steel);
            dl->AddConvexPolyFilled(head + 5, 3, steel);
            break;
        }
        }
    };

    for (int k = 0; k < n; ++k) {
        const ImVec2 a(x0 + k * (size + gap), y0), b(a.x + size, y0 + size);
        const bool sel = handTool_ == slots[k].tool;
        dl->AddRectFilled(a, b, sel ? IM_COL32(60, 52, 30, 225) : IM_COL32(18, 20, 26, 190), 8);
        dl->AddRect(a, b, sel ? IM_COL32(255, 200, 80, 255) : IM_COL32(90, 95, 105, 200), 8, 0, sel ? 2.5f : 1.5f);
        icon(slots[k].tool, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f + 2));
        dl->AddText(ImVec2(a.x + 5, a.y + 3), IM_COL32(230, 230, 230, 220), slots[k].key);
        if (sel) {  // Name des gewählten Werkzeugs über dem Feld
            const ImVec2 ts = ImGui::CalcTextSize(slots[k].name);
            const ImVec2 mid((a.x + b.x) * 0.5f, a.y - 6);
            dl->AddRectFilled(ImVec2(mid.x - ts.x / 2 - 6, mid.y - ts.y - 3), ImVec2(mid.x + ts.x / 2 + 6, mid.y + 3), IM_COL32(0, 0, 0, 150), 4);
            dl->AddText(ImVec2(mid.x - ts.x / 2, mid.y - ts.y), IM_COL32(255, 220, 140, 255), slots[k].name);
        }
    }
}
