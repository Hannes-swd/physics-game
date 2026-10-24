// Oberfläche: Menüs, Werkzeuge, Eigenschaften, Konsole, Physik-Inspektor, Dreitafelprojektion
#include "App.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "Materials.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <commdlg.h>

namespace {

const char* kShapes[] = {"Quader / Brett", "Zylinder / Stab", "Rohr", "Kugel", "Zahnrad", "Propeller"};
const char* kSnapNames[] = {"1 mm", "5 mm", "1 cm", "5 cm", "10 cm"};
const ImVec4 kAccent(1.0f, 0.62f, 0.15f, 1.0f);

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string fileDialog(GLFWwindow* win, bool save, const wchar_t* filter, const wchar_t* ext) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(win);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? wideToUtf8(buf) : std::string();
}

std::string quoteArg(const std::string& s) { return "\"" + s + "\""; }

// Linie mit DIN-Linienart auf den Bildschirm
void styledLine(ImDrawList* dl, ImVec2 a, ImVec2 b, LineStyle st, float k) {
    ImU32 col = IM_COL32(15, 15, 20, 255);
    float w = 1.0f;
    switch (st) {
    case LineStyle::Visible: w = std::max(1.6f, 0.5f * k); break;
    case LineStyle::Frame: w = std::max(1.8f, 0.7f * k); break;
    case LineStyle::Hidden: col = IM_COL32(40, 40, 60, 255); break;
    case LineStyle::Center: col = IM_COL32(40, 60, 140, 255); break;
    default: break;
    }
    if (st != LineStyle::Hidden && st != LineStyle::Center) {
        dl->AddLine(a, b, col, w);
        return;
    }
    const float pattern[2][4] = {{2.5f, 1.2f, 0, 0}, {8.0f, 1.2f, 1.0f, 1.2f}};
    const float* p = pattern[st == LineStyle::Center ? 1 : 0];
    const int np = st == LineStyle::Center ? 4 : 2;
    ImVec2 d(b.x - a.x, b.y - a.y);
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 0.5f) return;
    d.x /= len;
    d.y /= len;
    float t = 0;
    int i = 0;
    while (t < len) {
        float seg = std::max(1.0f, p[i % np] * k);
        float t1 = std::min(len, t + seg);
        if (i % 2 == 0) dl->AddLine(ImVec2(a.x + d.x * t, a.y + d.y * t), ImVec2(a.x + d.x * t1, a.y + d.y * t1), col, 1.0f);
        t = t1;
        ++i;
    }
}

} // namespace

// ============================================================================ Stil

void App::setupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    s.WindowRounding = 6;
    s.FrameRounding = 4;
    s.GrabRounding = 4;
    s.WindowBorderSize = 1;
    s.FramePadding = ImVec2(7, 4);
    s.ItemSpacing = ImVec2(8, 6);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.105f, 0.12f, 0.94f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.14f, 0.08f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.45f, 0.30f, 0.12f, 0.6f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.6f, 0.4f, 0.15f, 0.8f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.7f, 0.45f, 0.15f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.30f, 0.22f, 0.12f, 0.8f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.37f, 0.15f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.75f, 0.48f, 0.15f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.21f, 1.0f);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1, 0.75f, 0.3f, 1);
}

void App::log(const std::string& text, int kind) {
    std::istringstream ss(text);
    std::string l;
    while (std::getline(ss, l)) log_.push_back({l, kind});
    if (log_.size() > 3000) log_.erase(log_.begin(), log_.begin() + (log_.size() - 3000));
    scrollLog_ = true;
}

// ============================================================================ Aufbau

void App::drawUI() {
    drawMenuBar();
    if (!play_) {
        drawToolPanel();
        drawProperties();
        drawViewLabels();
    } else {
        drawPlayHud();
    }
    drawStatusBar();
    drawToasts();
    drawPlayerTags();
    if (showInspector_) drawInspector();
    if (showMaterials_) drawMaterialEditor();
    if (showDrawing_) drawDrawingWindow();
    if (showConsole_) drawConsole();
    if (showHelp_) drawHelp();
    if (showNet_) drawNetWindow();
    drawNetRequests();
    drawLoading();  // Ladebildschirm über allem (AppWorld.cpp)
}

void App::openImportDialog() {
    std::string f = fileDialog(window_, false, L"3D-Modell (*.obj)\0*.obj\0Alle Dateien\0*.*\0", L"obj");
    if (!f.empty()) runLine("import " + quoteArg(f) + " " + materials()[newMaterial_].name, "konsole");
}

void App::saveDialog(bool save) {
    std::string f = fileDialog(window_, save, L"Werkstatt-Szene (*.werkstatt)\0*.werkstatt\0Alle Dateien\0*.*\0", L"werkstatt");
    if (!f.empty()) runLine((save ? "save " : "load ") + quoteArg(f), "konsole");
}

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Datei")) {
        if (haveWorld_) {  // Welt aus der Weltliste
            if (ImGui::MenuItem("Welt speichern", "Strg+S", false, !worldReadOnly_ && !netClient())) saveWorld(false);
            ImGui::TextDisabled("Welt: %s", worldInfo_.name.c_str());
        }
        if (opt_.menu && ImGui::MenuItem("Zurück zum Hauptmenü")) leaveToMenu();
        if (haveWorld_ || opt_.menu) ImGui::Separator();
        if (ImGui::MenuItem("Leere Werkstatt")) runLine("scene empty", "konsole");
        if (ImGui::MenuItem("Beispiel-Werkstatt")) runLine("scene demo", "konsole");
        if (ImGui::MenuItem("Beispiel-Welt (alles)")) runLine("scene showcase", "konsole");
        if (ImGui::MenuItem("Feuerstelle (Wärme, Feuer, Kälte)")) runLine("scene fire", "konsole");
        if (ImGui::MenuItem("Wasserbecken (Schwimmen, Sinken, Boote)")) runLine("scene water", "konsole");
        if (ImGui::MenuItem("Fluss (Strömung, Hindernisse, Treibgut)")) runLine("scene river", "konsole");
        if (ImGui::MenuItem("Baumschule (alle Baumarten)")) runLine("scene trees", "konsole");
        ImGui::Separator();
        if (ImGui::MenuItem(haveWorld_ ? "Szene speichern unter..." : "Speichern...", haveWorld_ ? nullptr : "Strg+S")) saveDialog(true);
        if (ImGui::MenuItem("Laden...", "Strg+O")) saveDialog(false);
        if (ImGui::MenuItem("Modell importieren (.obj)...")) openImportDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Foto speichern", "F12")) log(screenshot("foto_" + std::to_string((long long)(time_ * 1000)) + ".png"), 3);
        if (ImGui::MenuItem("Beenden")) quit_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Bearbeiten")) {
        if (ImGui::MenuItem("Rückgängig", "Strg+Z", false, undoSteps() > 0)) {
            if (netClient()) runLine("undo", "konsole");
            else world_->undo();
        }
        if (ImGui::MenuItem("Wiederholen", "Strg+Y", false, redoSteps() > 0)) {
            if (netClient()) runLine("redo", "konsole");
            else world_->redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Alles auswählen", "Strg+A")) {
            selection_.clear();
            for (auto& o : world_->objects()) selection_.push_back(o->id);
        }
        if (ImGui::MenuItem("Duplizieren", "Strg+D", false, !selection_.empty())) duplicateSelected();
        if (ImGui::MenuItem("Löschen", "Entf", false, !selection_.empty())) deleteSelected();
        if (ImGui::MenuItem("Absetzen", "G", false, !selection_.empty())) dropSelected();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ansicht")) {
        const char* names[] = {"Perspektive", "Oben (Draufsicht)", "Vorne", "Seite", "Vier Ansichten"};
        const char* keys[] = {"1", "2", "3", "4", "5"};
        for (int i = 0; i < 5; ++i)
            if (ImGui::MenuItem(names[i], keys[i], (int)viewMode_ == i && !play_)) {
                if (play_) leavePlay();
                viewMode_ = (ViewMode)i;
            }
        ImGui::Separator();
        ImGui::MenuItem("Schatten", nullptr, &showShadows_);
        ImGui::MenuItem("Kanten", nullptr, &showEdges_);
        ImGui::MenuItem("Raster", nullptr, &showGrid_);
        if (world_->terrain()) ImGui::MenuItem("Biomgrenzen", nullptr, &showBiomeBorders_);
        ImGui::Separator();
        ImGui::MenuItem("Physik-Inspektor", "F3", &showInspector_);
        ImGui::MenuItem("Materialien bearbeiten", nullptr, &showMaterials_);
        if (ImGui::MenuItem("Dreitafelprojektion", "F4", false, !selection_.empty())) showDrawing(selection_);
        ImGui::MenuItem("Konsole", "F1", &showConsole_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Physik")) {
        bool br = world_->breakage();
        if (ImGui::MenuItem("Bruch (Verbindungen und Teile)", nullptr, &br)) runLine(br ? "breakage on" : "breakage off", "konsole");
        bool vac = world_->airDensity() == 0.0;
        if (ImGui::MenuItem("Vakuum (keine Luft)", nullptr, &vac)) runLine(vac ? "air vacuum" : "air normal", "konsole");
        bool bc = world_->buildCollision();
        if (ImGui::MenuItem("Bau-Modus: Teile stoßen an", nullptr, &bc)) runLine(bc ? "buildcollision on" : "buildcollision off", "konsole");
        if (ImGui::BeginMenu("Wind")) {
            if (ImGui::MenuItem("kein Wind")) runLine("wind 0 0 0", "konsole");
            if (ImGui::MenuItem("leicht, 3 m/s von links")) runLine("wind 3 0 0", "konsole");
            if (ImGui::MenuItem("stark, 10 m/s von links")) runLine("wind 10 0 0", "konsole");
            if (ImGui::MenuItem("Sturm, 25 m/s von links")) runLine("wind 25 0 0", "konsole");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        bool heat = world_->heat();
        if (ImGui::MenuItem("Wärme und Feuer", nullptr, &heat)) runLine(heat ? "heat on" : "heat off", "konsole");
        if (ImGui::BeginMenu("Lufttemperatur")) {
            const double amb = world_->ambient();
            for (int c : {-40, -20, 0, 20, 35})
                if (ImGui::MenuItem((std::to_string(c) + " °C").c_str(), nullptr, std::abs(amb - c) < 0.5)) runLine("heat ambient " + std::to_string(c), "konsole");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Wärme-Zeitraffer")) {
            const double sp = world_->heatSpeed();
            for (int f : {1, 5, 20, 60})
                if (ImGui::MenuItem((std::to_string(f) + "x").c_str(), nullptr, std::abs(sp - f) < 0.01)) runLine("heat speed " + std::to_string(f), "konsole");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Alle Feuer löschen", nullptr, false, world_->burningParts() > 0)) runLine("extinguish all", "konsole");
        ImGui::Separator();
        if (ImGui::BeginMenu("Wasser")) {
            if (ImGui::MenuItem("Becken hier (2 x 1,5 m, 50 cm tief, mit Rand)")) runLine("water pool 200 150 50 walls", "konsole");
            bool sea = false;
            for (const WaterBody& b : world_->waters()) sea |= b.sea && !b.generated();
            if (ImGui::MenuItem("Meer (Wasserspiegel 40 cm)", nullptr, &sea)) runLine(sea ? "water sea 40" : "water sea off", "konsole");
            if (ImGui::BeginMenu("Wellen", !world_->waters().empty())) {
                const char* names[] = {"ruhig", "leicht (10 cm)", "bewegt (40 cm)", "Sturm (150 cm)"};
                const int cm[] = {0, 10, 40, 150};
                for (int i = 0; i < 4; ++i)
                    if (ImGui::MenuItem(names[i])) runLine("water waves all " + std::to_string(cm[i]), "konsole");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Strömung", !world_->waters().empty())) {
                const char* names[] = {"keine", "langsam (0,4 m/s nach rechts)", "Fluss (1,2 m/s nach rechts)", "reißend (2,5 m/s nach rechts)"};
                const char* speeds[] = {"0", "0.4", "1.2", "2.5"};
                for (int i = 0; i < 4; ++i)
                    if (ImGui::MenuItem(names[i])) runLine(std::string("water flow all ") + speeds[i] + " 0", "konsole");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Ausgewähltes Teil füllen", nullptr, false, !selection_.empty()))
                runLine("water fill " + std::to_string(selection_[0]), "konsole");
            if (ImGui::MenuItem("Alles Wasser entfernen", nullptr, false, !world_->waters().empty())) runLine("water remove all", "konsole");
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Modus")) {
        if (ImGui::MenuItem("Bauen (CAD)", "Tab", !play_)) setMode(false);
        if (ImGui::MenuItem("Spielen (Physik)", "Tab", play_)) setMode(true);
        ImGui::EndMenu();
    }
    drawNetMenu();
    if (ImGui::BeginMenu("Hilfe")) {
        ImGui::MenuItem("Steuerung", nullptr, &showHelp_);
        ImGui::EndMenu();
    }
    if (netRole_ != NetRole::Off) {
        std::string lan = netRole_ == NetRole::Host ? "LAN: Gastgeber · " + std::to_string(netPlayers_.size()) + " Spieler"
                                                    : (netWelcomed_ ? "LAN: bei " + netHostName_ : std::string("LAN: verbinde ..."));
        ImGui::SameLine(ImGui::GetWindowWidth() - 350 - ImGui::CalcTextSize(lan.c_str()).x);
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1), "%s", lan.c_str());
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 330);
    const bool simOn = netClient() ? netHostSim_ : world_->simulating();  // im LAN läuft sie, sobald irgendwer spielt
    ImGui::TextColored(play_ ? ImVec4(0.4f, 1.0f, 0.5f, 1) : kAccent,
                       play_ ? "SPIEL-MODUS (Physik an)" : simOn ? "BAU-MODUS (Physik an)" : "BAU-MODUS (Physik aus)");
    ImGui::EndMainMenuBar();
}

void App::drawToolPanel() {
    ImGui::SetNextWindowPos(ImVec2(10, 32), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Werkzeuge", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    int t = (int)tool_;
    ImGui::RadioButton("Auswählen / Bewegen (V)", &t, 0);
    ImGui::RadioButton("Säge (S)", &t, 1);
    ImGui::SameLine(170);
    ImGui::RadioButton("Bohrer (B)", &t, 2);
    ImGui::RadioButton("Abrunden / Fasen (E)", &t, 3);
    ImGui::RadioButton("Fräsen (M)", &t, 4);
    ImGui::SameLine(170);
    ImGui::RadioButton("Hobeln (P)", &t, 5);
    ImGui::RadioButton("Wärme / Kälte (H)", &t, 6);
    tool_ = (Tool)t;
    ImGui::Separator();
    if (tool_ == Tool::Heat) {
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##heattool", heatTools()[heatTool_].label)) {
            for (int i = 0; i < (int)heatTools().size(); ++i) {
                const HeatTool& h = heatTools()[i];
                const std::string label = std::string(h.label) + "  (" + fmtNum(h.tempC, 0) + " °C)";
                if (ImGui::Selectable(label.c_str(), i == heatTool_)) heatTool_ = i;
            }
            ImGui::EndCombo();
        }
        if (hoverValid_ && hoverHit_.obj)
            ImGui::TextColored(kAccent, "Hier: %.0f °C", world_->temperatureAt(*hoverHit_.obj, hoverHit_.point));
        ImGui::TextWrapped("Maustaste auf einem Teil halten: Flamme bzw. Kälte trifft diese Stelle. Holz wird erst schwarz und "
                           "brennt dann, Metall glüht, wird weich und schmilzt. Im Spiel-Modus: F halten.");
        if (!world_->heat()) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Wärme ist aus (Menü Physik).");
    } else if (tool_ == Tool::Select) {
        ImGui::TextWrapped("Klick = auswählen, Shift/Strg+Klick = mehrere. Ziehen = bewegen (Shift = hoch/runter, Alt = ohne Raster, "
                           "Strg = durch andere Teile hindurch).");
    } else if (tool_ == Tool::Round) {
        ImGui::Checkbox("Fase statt Rundung", &roundChamfer_);
        ImGui::InputFloat(roundChamfer_ ? "Fase mm" : "Radius mm", &roundMm_, 0.5f, 2.0f, "%.1f");
        roundMm_ = std::clamp(roundMm_, 0.1f, 500.0f);
        ImGui::TextWrapped("Maus an eine Kante: Sie leuchtet (gelb = Außenkante, blau = Innenkante/Hohlkehle). Klick = abrunden.");
    } else if (tool_ == Tool::Mill) {
        ImGui::InputFloat("Länge mm", &millDims_[0], 1, 10, "%.1f");
        ImGui::InputFloat("Breite mm", &millDims_[1], 1, 10, "%.1f");
        ImGui::InputFloat("Tiefe mm", &millDims_[2], 0.5f, 5, "%.1f");
        ImGui::InputFloat("Eckradius mm", &millCornerMm_, 0.5f, 2, "%.1f");
        for (float& d : millDims_) d = std::clamp(d, 0.1f, 2000.0f);
        millCornerMm_ = std::clamp(millCornerMm_, 0.0f, 500.0f);
        ImGui::TextWrapped("Klick auf eine Fläche fräst eine Tasche (z.B. Zapfenloch). Nut, Falz, Zapfen, Langloch: per Befehl (F1, 'help').");
    } else if (tool_ == Tool::Plane) {
        ImGui::InputFloat("Abnahme mm", &planeMm_, 0.5f, 2, "%.2f");
        planeMm_ = std::clamp(planeMm_, 0.05f, 500.0f);
        ImGui::TextWrapped("Klick auf eine Fläche hobelt diese Schicht ab.");
    } else if (tool_ == Tool::Saw) {
        ImGui::Text("Schnitt quer zur lokalen Achse:");
        ImGui::RadioButton("X (X)", &sawAxis_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Y (Y)", &sawAxis_, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Z (Z)", &sawAxis_, 2);
        ImGui::SliderFloat("Gehrung", &sawAngle_, -60.0f, 60.0f, "%.1f°");
        ImGui::InputFloat("Schnittfuge mm", &sawKerfMm_, 0.5f, 1.0f, "%.1f");
        sawKerfMm_ = std::clamp(sawKerfMm_, 0.0f, 20.0f);
        if (preview_.valid) ImGui::TextColored(kAccent, "Abstand vom Ende: %.1f cm", preview_.distFromEnd * 100.0);
        ImGui::TextWrapped("Mit der Maus über ein Teil fahren und klicken.");
    } else {
        ImGui::InputFloat("Durchmesser mm", &drillDiaMm_, 0.5f, 1.0f, "%.1f");
        ImGui::InputFloat("Tiefe mm (0=durch)", &drillDepthMm_, 1.0f, 5.0f, "%.1f");
        drillDiaMm_ = std::clamp(drillDiaMm_, 0.5f, 500.0f);
        drillDepthMm_ = std::clamp(drillDepthMm_, 0.0f, 5000.0f);
        ImGui::TextWrapped("Auf eine Fläche klicken: Es wird senkrecht hineingebohrt.");
    }
    ImGui::Separator();
    ImGui::Checkbox("Einrasten", &snap_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##snap", &snapIndex_, kSnapNames, 5);

    ImGui::SeparatorText("Neues Teil");
    ImGui::SetNextItemWidth(180);
    ImGui::Combo("Form", &newShape_, kShapes, 6);
    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Material", materials()[newMaterial_].label)) {
        for (int i = 0; i < (int)materials().size(); ++i)
            if (ImGui::Selectable(materials()[i].label, i == newMaterial_)) newMaterial_ = i;
        ImGui::EndCombo();
    }
    std::string cmd;
    const char* mat = materials()[newMaterial_].name;
    auto f = [](float v) { return fmtNum(v, 2); };
    ImGui::PushItemWidth(120);
    switch (newShape_) {
    case 0:
        ImGui::InputFloat("Länge cm (x)", &newDims_[0], 1, 10, "%.2f");
        ImGui::InputFloat("Breite cm (z)", &newDims_[1], 1, 10, "%.2f");
        ImGui::InputFloat("Höhe cm (y)", &newDims_[2], 0.5f, 5, "%.2f");
        cmd = "new box " + std::string(mat) + " " + f(newDims_[0]) + " " + f(newDims_[1]) + " " + f(newDims_[2]);
        break;
    case 1:
        ImGui::InputFloat("Durchmesser cm", &newDims_[0], 0.5f, 5, "%.2f");
        ImGui::InputFloat("Höhe cm", &newDims_[1], 1, 10, "%.2f");
        cmd = "new cylinder " + std::string(mat) + " " + f(newDims_[0]) + " " + f(newDims_[1]);
        break;
    case 2:
        ImGui::InputFloat("Außen-D cm", &newDims_[0], 0.5f, 5, "%.2f");
        ImGui::InputFloat("Innen-D cm", &newDims_[1], 0.5f, 5, "%.2f");
        ImGui::InputFloat("Länge cm", &newDims_[2], 1, 10, "%.2f");
        cmd = "new tube " + std::string(mat) + " " + f(newDims_[0]) + " " + f(newDims_[1]) + " " + f(newDims_[2]);
        break;
    case 3:
        ImGui::InputFloat("Durchmesser cm", &newDims_[0], 0.5f, 5, "%.2f");
        cmd = "new sphere " + std::string(mat) + " " + f(newDims_[0]);
        break;
    case 4:
        ImGui::InputInt("Zähne", &gearTeeth_);
        ImGui::InputFloat("Modul mm", &gearModule_, 0.5f, 1, "%.2f");
        ImGui::InputFloat("Breite cm", &gearWidth_, 0.5f, 1, "%.2f");
        ImGui::InputFloat("Bohrung mm", &gearBore_, 1, 5, "%.1f");
        gearTeeth_ = std::clamp(gearTeeth_, 6, 200);
        cmd = "new gear " + std::string(mat) + " " + std::to_string(gearTeeth_) + " " + f(gearModule_) + " " + f(gearWidth_) + " " + f(gearBore_);
        ImGui::TextDisabled("Teilkreis-D: %.1f mm", gearTeeth_ * gearModule_);
        break;
    default:
        ImGui::InputFloat("Durchmesser cm", &propDiameter_, 1, 10, "%.1f");
        ImGui::InputInt("Blätter", &propBlades_);
        ImGui::InputFloat("Blattwinkel °", &propPitch_, 1, 5, "%.1f");
        propDiameter_ = std::max(propDiameter_, 2.0f);
        propBlades_ = std::clamp(propBlades_, 1, 12);
        propPitch_ = std::clamp(propPitch_, -60.0f, 60.0f);
        cmd = "new propeller " + std::string(mat) + " " + f(propDiameter_) + " " + std::to_string(propBlades_) + " " + f(propPitch_);
        ImGui::TextDisabled("Achse y, Schub nach +y bei positiver Drehung");
        break;
    }
    ImGui::PopItemWidth();
    for (float& d : newDims_) d = std::max(d, 0.0f);
    if (ImGui::Button("Erstellen", ImVec2(-1, 0))) runLine(cmd, "konsole");
    if (ImGui::Button("Blender-Modell importieren (.obj)", ImVec2(-1, 0))) openImportDialog();
    ImGui::End();
}

void App::drawProperties() {
    Object* o = world_->find(selectedId());
    if (!o) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 340, 32), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Eigenschaften", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (selection_.size() > 1) {
        double mass = 0;
        for (uint32_t id : selection_)
            if (Object* p = world_->find(id)) mass += p->mass;
        ImGui::TextColored(kAccent, "%d Teile ausgewählt (gesamt %.3f kg)", (int)selection_.size(), mass);
        if (ImGui::Button("Dreitafelprojektion aller (F4)")) showDrawing(selection_);
        if (selection_.size() == 2) {
            // Zwei Teile verbinden: das zuerst gewählte hält das zweite
            ImGui::SeparatorText("Verbinden");
            const char* types[] = {"Kleben", "Nageln", "Schrauben", "Schweißen", "Scharnier", "Feder", "Seil"};
            ImGui::SetNextItemWidth(160);
            ImGui::Combo("Art##join", &joinType_, types, 7);
            const std::string a = std::to_string(selection_[0]), b = std::to_string(selection_[1]);
            std::string cmd;
            ImGui::PushItemWidth(120);
            switch (joinType_) {
            case 0: {
                const char* glueKeys[] = {"woodglue", "epoxy", "superglue", "pu", "hotglue", "construction"};
                const char* glues[] = {"Holzleim", "Epoxid", "Sekundenkleber", "PU-Kleber", "Heißkleber", "Montagekleber"};
                ImGui::Combo("Kleber", &gluePreset_, glues, 6);
                ImGui::DragFloat("Haftung MPa (0=typisch)", &joinV1_, 0.05f, 0.0f, 100.0f, "%.3f");
                ImGui::DragFloat("Spalt mm (0=typisch)", &joinV2_, 0.05f, 0.0f, 20.0f, "%.2f");
                cmd = "glue " + a + " " + b + " " + glueKeys[gluePreset_] + " " + fmtNum(joinV1_, 4) + " " + fmtNum(joinV2_, 2);
                break;
            }
            case 1:
            case 2: {
                int n = (int)joinV1_;
                ImGui::DragInt("Anzahl (0=auto)", &n, 0.2f, 0, 50);
                joinV1_ = (float)n;
                ImGui::DragFloat("Länge mm (0=auto)", &joinV2_, 1.0f, 0.0f, 300.0f, "%.0f");
                cmd = std::string(joinType_ == 1 ? "nail " : "screw ") + a + " " + b + " " + std::to_string(n) + " " + fmtNum(joinV2_, 0);
                break;
            }
            case 3:
                ImGui::DragFloat("Naht mm (0=3)", &joinV1_, 0.1f, 0.0f, 20.0f, "%.1f");
                cmd = "weld " + a + " " + b + " " + fmtNum(joinV1_, 1);
                break;
            case 4:
                ImGui::Combo("Achse von Teil 2", &joinAxis_, "x\0y\0z\0");
                ImGui::DragFloat("Motor U/min", &joinV1_, 5.0f, -20000.0f, 20000.0f, "%.0f");
                ImGui::DragFloat("Moment Nm (0=5)", &joinV2_, 0.1f, 0.0f, 1000.0f, "%.1f");
                cmd = "hinge " + a + " " + b + " " + std::string(1, "xyz"[joinAxis_]) + " " + fmtNum(joinV1_, 0) + " " + fmtNum(joinV2_, 1);
                break;
            case 5:
                ImGui::DragFloat("Steifigkeit N/m (0=2000)", &joinV1_, 10.0f, 0.0f, 1e6f, "%.0f");
                ImGui::DragFloat("Dämpfung N·s/m (0=20)", &joinV2_, 0.5f, 0.0f, 1e4f, "%.1f");
                cmd = "spring " + a + " " + b + " " + fmtNum(joinV1_, 0) + " " + fmtNum(joinV2_, 1);
                break;
            default:
                ImGui::DragFloat("Länge cm (0=jetzt)", &joinV1_, 1.0f, 0.0f, 10000.0f, "%.1f");
                ImGui::DragFloat("Bruchkraft N (0=2000)", &joinV2_, 10.0f, 0.0f, 1e6f, "%.0f");
                cmd = "rope " + a + " " + b + " " + fmtNum(joinV1_, 1) + " " + fmtNum(joinV2_, 0);
                break;
            }
            ImGui::PopItemWidth();
            if (ImGui::Button("Verbinden", ImVec2(-1, 0))) runLine(cmd, "konsole");
            ImGui::TextDisabled("Teil #%u (zuerst gewählt) hält Teil #%u", selection_[0], selection_[1]);
            // Formen kombinieren
            ImGui::SeparatorText("Formen kombinieren");
            if (ImGui::Button("Vereinen")) runLine("merge " + a + " " + b, "konsole");
            ImGui::SameLine();
            if (ImGui::Button("#2 aus #1 ausschneiden")) runLine("subtract " + a + " " + b, "konsole");
            ImGui::SameLine();
            if (ImGui::Button("Schnittmenge")) runLine("intersect " + a + " " + b, "konsole");
        }
        ImGui::Separator();
        ImGui::TextDisabled("Hauptauswahl:");
    }
    if (nameBufFor_ != o->id) {
        std::snprintf(nameBuf_, sizeof(nameBuf_), "%s", o->name.c_str());
        nameBufFor_ = o->id;
    }
    // Mitspieler: Änderungen als Befehl an den Gastgeber (er schickt das Ergebnis zurück)
    const bool remote = netClient();
    const std::string idText = std::to_string(o->id);
    ImGui::SetNextItemWidth(-1);
    bool renamed = ImGui::InputText("##name", nameBuf_, sizeof(nameBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
    renamed |= ImGui::IsItemDeactivatedAfterEdit();
    if (renamed && o->name != nameBuf_) {
        if (remote) runLine("name " + idText + " " + quoteArg(nameBuf_), "konsole");
        else o->name = nameBuf_;
    }
    ImGui::TextDisabled("#%u  %s%s", o->id, o->shape.c_str(), o->edits ? " (bearbeitet)" : "");

    int mat = o->material;
    if (ImGui::BeginCombo("Material", materials()[mat].label)) {
        for (int i = 0; i < (int)materials().size(); ++i)
            if (ImGui::Selectable(materials()[i].label, i == mat)) {
                if (remote) {
                    runLine("material " + idText + " " + materials()[i].name, "konsole");
                    continue;
                }
                world_->pushUndo();
                world_->setMaterial(*o, i);
            }
        ImGui::EndCombo();
    }
    const Material& m = materials()[o->material];
    glm::dvec3 s = (o->bbMax - o->bbMin) * 100.0;
    ImGui::Text("Größe:   %.2f x %.2f x %.2f cm", s.x, s.y, s.z);
    ImGui::Text("Volumen: %.2f cm³", o->volume * 1e6);
    ImGui::Text("Masse:   %.4f kg  (Gewicht %.3f N)", o->mass, o->mass * 9.81);
    ImGui::Text("Dichte %.0f kg/m³ · Haft %.2f / Gleit %.2f · Stoß %.2f", m.density, m.frictionStatic, m.frictionKinetic, m.restitution);
    ImGui::Text("Zugfestigkeit %.1f MPa · E-Modul %.1f GPa", m.tensileMPa, m.youngGPa);

    ImGui::SeparatorText("Temperatur");
    {
        const Thermal& th = thermal(o->material);
        const bool has = !o->heat.empty();
        const float amb = (float)world_->ambient();
        const float tMean = has ? o->heatInfo.tMean : amb, tMin = has ? o->heatInfo.tMin : amb, tMax = has ? o->heatInfo.tMax : amb;
        const ImVec4 col = tMax > 400 ? ImVec4(1.0f, 0.5f, 0.15f, 1) : tMin < 0 ? ImVec4(0.55f, 0.8f, 1.0f, 1) : ImVec4(0.85f, 0.85f, 0.85f, 1);
        ImGui::TextColored(col, "%.0f °C   (kältester Punkt %.0f °C, heißester %.0f °C)", tMean, tMin, tMax);
        std::string state;
        auto add = [&](const std::string& s) { state += (state.empty() ? "" : " · ") + s; };
        if (has && o->heatInfo.burning) add("brennt (" + std::to_string(o->heatInfo.burning) + " Stellen)");
        if (has && o->heatInfo.chr > 0.01f) add("verkohlt " + fmtNum(o->heatInfo.chr * 100, 0) + " %");
        if (has && o->heatInfo.melt > 0.001f) add("geschmolzen " + fmtNum(o->heatInfo.melt * 100, 0) + " %");
        if (o->meltForm == MeltForm::Puddle) add(o->heatInfo.melt > 0.5f ? "flüssige Pfütze" : "erstarrte Pfütze");
        const float sf = strengthFactor(o->material, tMax);
        if (sf < 0.95f && !combustible(o->material)) add("weich: " + fmtNum(sf * 100, 0) + " % Festigkeit");
        if (brittleness(o->material, tMin) > 0.05f) add("spröde vor Kälte");
        ImGui::TextWrapped("%s", state.empty() ? "normal" : state.c_str());
        std::string info = th.meltC > 0 ? "schmilzt bei " + fmtNum(th.meltC, 0) + " °C" : std::string("schmilzt nicht");
        if (combustible(o->material)) info += " · verkohlt ab " + fmtNum(th.pyrolysisC, 0) + " °C · brennt ab " + fmtNum(th.ignitionC, 0) + " °C";
        if (th.brittleC > -400) info += " · spröde unter " + fmtNum(th.brittleC, 0) + " °C";
        ImGui::TextDisabled("%s", info.c_str());
        ImGui::SetNextItemWidth(120);
        ImGui::InputFloat("##settemp", &heatSet_, 10.0f, 100.0f, "%.0f °C");
        heatSet_ = std::clamp(heatSet_, -273.0f, 5000.0f);
        ImGui::SameLine();
        if (ImGui::Button("Temperatur setzen")) runLine("heat " + idText + " " + fmtNum(heatSet_, 0), "konsole");
        if (combustible(o->material)) {
            if (ImGui::Button("Anzünden")) runLine("ignite " + idText, "konsole");
            ImGui::SameLine();
        }
        if (ImGui::Button("Löschen (Wasser)")) runLine("extinguish " + idText, "konsole");
    }

    ImGui::SeparatorText("Lage");
    double pos[3] = {o->pos.x * 100, o->pos.y * 100, o->pos.z * 100};
    if (ImGui::DragScalarN("Position cm", ImGuiDataType_Double, pos, 3, 0.1f, nullptr, nullptr, "%.2f")) {
        world_->setTransform(*o, glm::dvec3(pos[0], pos[1], pos[2]) / 100.0, o->rot);
        shareTransforms({o->id});
    }
    if (ImGui::IsItemActivated()) pushUndo();
    glm::dvec3 e = glm::degrees(glm::eulerAngles(o->rot));
    double eul[3] = {e.x, e.y, e.z};
    if (ImGui::DragScalarN("Drehung °", ImGuiDataType_Double, eul, 3, 0.5f, nullptr, nullptr, "%.1f")) {
        world_->setTransform(*o, o->pos, glm::dquat(glm::radians(glm::dvec3(eul[0], eul[1], eul[2]))));
        shareTransforms({o->id});
    }
    if (ImGui::IsItemActivated()) pushUndo();
    if (ImGui::Button("90° Y (R)")) rotateSelected(1, 90);
    ImGui::SameLine();
    if (ImGui::Button("90° X")) rotateSelected(0, 90);
    ImGui::SameLine();
    if (ImGui::Button("90° Z")) rotateSelected(2, 90);
    ImGui::SameLine();
    if (ImGui::Button("Absetzen (G)")) dropSelected();

    ImGui::SeparatorText("Lagerung");
    bool fixed = o->fixed;
    if (ImGui::Checkbox("Fest verankert (unbeweglich)", &fixed)) {
        if (remote) {
            runLine("fix " + idText + (fixed ? " on" : " off"), "konsole");
        } else {
            world_->pushUndo();
            world_->setFixed(*o, fixed);
        }
    }
    if (!o->fixed) {
        const char* axes[] = {"keine Achse", "Drehachse lokal X", "Drehachse lokal Y", "Drehachse lokal Z"};
        auto axleCmd = [&](int axis, float rpm, float tq) {
            runLine("axle " + idText + " " + (axis < 0 ? std::string("off") : std::string(1, "xyz"[axis])) + " " + fmtNum(rpm, 2) + " " +
                        fmtNum(tq, 3),
                    "konsole");
        };
        int ax = o->hingeAxis + 1;
        if (ImGui::Combo("Achse", &ax, axes, 4)) {
            if (remote) {
                axleCmd(ax - 1, o->motorRpm, o->motorTorque);
            } else {
                world_->pushUndo();
                world_->setHinge(*o, ax - 1, o->motorRpm, o->motorTorque);
            }
        }
        if (o->hingeAxis >= 0) {
            float rpm = o->motorRpm, tq = o->motorTorque;
            bool changed = ImGui::DragFloat("Motor U/min", &rpm, 0.5f, -3000, 3000, "%.1f");
            bool done = ImGui::IsItemDeactivatedAfterEdit();
            changed |= ImGui::DragFloat("Max. Moment Nm", &tq, 0.1f, 0, 10000, "%.2f");
            done |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemActivated() && !remote) world_->pushUndo();
            if (changed) world_->setHinge(*o, o->hingeAxis, rpm, tq);  // beim Mitspieler nur Vorschau bis zum Loslassen
            if (done && remote) axleCmd(o->hingeAxis, o->motorRpm, o->motorTorque);
        }
    }
    ImGui::Separator();
    // Verbindungen dieses Teils mit Belastung
    {
        int nj = 0;
        for (auto& jp : world_->joints()) nj += (jp->a == o->id || jp->b == o->id);
        if (nj > 0 && ImGui::CollapsingHeader(("Verbindungen (" + std::to_string(nj) + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            uint32_t removeId = 0;
            for (auto& jp : world_->joints()) {
                Joint& j = *jp;
                if (j.a != o->id && j.b != o->id) continue;
                Object* p = world_->find(j.a == o->id ? j.b : j.a);
                ImGui::PushID((int)j.id);
                ImGui::Text("V%u %s mit %s", j.id, World::jointName(j), p ? p->name.c_str() : "?");
                std::string pct = fmtNum(j.load * 100.0, 0) + " % belastet";
                ImGui::ProgressBar((float)std::min(j.load, 1.0), ImVec2(170, 0), pct.c_str());
                ImGui::SameLine();
                const std::string jn = "j" + std::to_string(j.id);
                bool ub = j.unbreakable;
                if (ImGui::Checkbox("hält alles", &ub)) {
                    if (remote) {
                        runLine("joint " + jn + " unbreakable " + (ub ? "on" : "off"), "konsole");
                    } else {
                        world_->pushUndo();
                        j.unbreakable = ub;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("lösen")) removeId = j.id;
                ImGui::PushItemWidth(120);
                bool changed = false, done = false;
                std::string setting;  // Mitspieler: Befehl "joint jN <setting> <wert>" beim Loslassen
                if (j.type == JointType::Glue) {
                    float ft = (float)j.ft;
                    if (ImGui::DragFloat("Haftung MPa", &ft, 0.02f, 0.0001f, 100.0f, "%.3f") && ft > 0) {
                        j.fs *= ft / j.ft;
                        j.ft = ft;
                        changed = true;
                    }
                    setting = "strength " + fmtNum(j.ft, 4);
                } else if (j.type == JointType::Nail || j.type == JointType::Screw) {
                    changed |= ImGui::DragInt("Anzahl", &j.count, 0.2f, 1, 100);
                    setting = "count " + std::to_string(j.count);
                } else {
                    float f = (float)j.maxForce;
                    if (ImGui::DragFloat("Bruchkraft N", &f, 10.0f, 1.0f, 1e7f, "%.0f")) {
                        j.maxForce = f;
                        changed = true;
                    }
                    setting = "force " + fmtNum(j.maxForce, 1);
                    if (j.type == JointType::Hinge) {
                        done |= ImGui::IsItemDeactivatedAfterEdit();
                        if (done && remote) runLine("joint " + jn + " " + setting, "konsole");
                        done = false;
                        changed |= ImGui::DragFloat("Motor U/min", &j.motorRpm, 5.0f, -20000.0f, 20000.0f, "%.0f");
                        setting = "motor " + fmtNum(j.motorRpm, 1);
                    } else if (j.type == JointType::Spring) {
                        done |= ImGui::IsItemDeactivatedAfterEdit();
                        if (done && remote) runLine("joint " + jn + " " + setting, "konsole");
                        done = false;
                        float k = (float)j.stiffness;
                        if (ImGui::DragFloat("Steifigkeit N/m", &k, 10.0f, 1.0f, 1e7f, "%.0f")) {
                            j.stiffness = k;
                            changed = true;
                        }
                        setting = "stiffness " + fmtNum(j.stiffness, 1);
                    }
                }
                done |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::IsItemActivated() && !remote) world_->pushUndo();
                if (changed) world_->updateJoint(j);  // beim Mitspieler nur Vorschau bis zum Loslassen
                if (done && remote) runLine("joint " + jn + " " + setting, "konsole");
                ImGui::PopItemWidth();
                ImGui::PopID();
            }
            if (removeId) {
                if (remote) {
                    runLine("detach j" + std::to_string(removeId), "konsole");
                } else {
                    world_->pushUndo();
                    world_->removeJoint(removeId);
                }
            }
        }
    }
    // Bearbeiten: alles erzeugt einen Befehl (steht dann in der Konsole)
    if (o->kind == ObjKind::Solid && ImGui::CollapsingHeader("Bearbeiten")) {
        const std::string id = std::to_string(o->id);
        ImGui::PushItemWidth(110);
        ImGui::DragFloat("Radius / Fase mm", &editRadius_, 0.1f, 0.1f, 200.0f, "%.1f");
        if (ImGui::Button("Alle Kanten abrunden")) runLine("round " + id + " all " + fmtNum(editRadius_, 2), "konsole");
        ImGui::SameLine();
        if (ImGui::Button("fasen")) runLine("chamfer " + id + " all " + fmtNum(editRadius_, 2), "konsole");
        ImGui::SameLine();
        if (ImGui::Button("schleifen")) runLine("sand " + id + " 1", "konsole");
        ImGui::SeparatorText("Drehbank");
        ImGui::Combo("Achse##turn", &turnAxis_, "x\0y\0z\0");
        ImGui::DragFloat("von cm", &turnFrom_, 0.1f, 0.0f, 10000.0f, "%.1f");
        ImGui::DragFloat("bis cm", &turnTo_, 0.1f, 0.0f, 10000.0f, "%.1f");
        ImGui::DragFloat("Durchmesser mm", &turnD_, 0.1f, 0.1f, 5000.0f, "%.1f");
        ImGui::DragFloat("Ende-D mm (0=gleich)", &turnD2_, 0.1f, 0.0f, 5000.0f, "%.1f");
        if (ImGui::Button("Abdrehen"))
            runLine("turn " + id + " " + std::string(1, "xyz"[turnAxis_]) + " " + fmtNum(turnFrom_, 2) + " " + fmtNum(turnTo_, 2) + " " +
                        fmtNum(turnD_, 2) + (turnD2_ > 0 ? " " + fmtNum(turnD2_, 2) : ""),
                    "konsole");
        ImGui::SeparatorText("Biegen");
        ImGui::Combo("Achse##bend", &bendAxis_, "x\0y\0z\0");
        ImGui::DragFloat("bei cm", &bendAt_, 0.1f, 0.0f, 10000.0f, "%.1f");
        ImGui::DragFloat("Winkel °", &bendAngle_, 1.0f, -180.0f, 180.0f, "%.0f");
        ImGui::DragFloat("Innenradius mm (0=klein)", &bendRadius_, 0.1f, 0.0f, 10000.0f, "%.1f");
        if (ImGui::Button("Biegen"))
            runLine("bend " + id + " " + std::string(1, "xyz"[bendAxis_]) + " " + fmtNum(bendAt_, 2) + " " + fmtNum(bendAngle_, 1) +
                        (bendRadius_ > 0 ? " " + fmtNum(bendRadius_, 2) : ""),
                    "konsole");
        ImGui::SeparatorText("Spiegeln / Skalieren");
        ImGui::Checkbox("als Kopie", &mirrorCopy_);
        for (int ax = 0; ax < 3; ++ax) {
            ImGui::SameLine();
            std::string label = std::string("Spiegeln ") + "XYZ"[ax];
            if (ImGui::Button(label.c_str())) runLine("mirror " + id + " " + std::string(1, "xyz"[ax]) + (mirrorCopy_ ? " copy" : ""), "konsole");
        }
        ImGui::DragFloat("Faktor", &scaleF_, 0.01f, 0.01f, 100.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::Button("Skalieren")) runLine("scale " + id + " " + fmtNum(scaleF_, 3), "konsole");
        ImGui::PopItemWidth();
        ImGui::TextDisabled("%d Kanten · Kollision: %d Hülle(n)", (int)world_->edgesOf(*o).size(), (int)o->coll.hulls.size());
    }
    // Bohrungen und Stifte: Maße, was darin steckt, Last; Schmieren
    if (!o->coll.bores.empty() || !o->coll.cylinders.empty()) {
        ImGui::SeparatorText("Bohrungen & Stifte");
        for (size_t i = 0; i < o->coll.bores.size(); ++i) {
            const BoreGeom& b = o->coll.bores[i];
            const Object::BoreRuntime rt = i < o->boreRt.size() ? o->boreRt[i] : Object::BoreRuntime();
            ImGui::Text("Bohrung Ø%.2f mm × %.1f mm%s", b.radius * 2000.0, b.halfLength * 2000.0, b.openTop && b.openBottom ? "" : " (Sackloch)");
            if (rt.partner) {
                ImGui::SameLine();
                ImGui::TextDisabled("· darin #%u", rt.partner);
            }
            ImGui::ProgressBar(float(std::min(1.0, rt.load)), ImVec2(-1, 0),
                               (fmtNum(rt.load_N, 0) + " N · " + fmtNum(rt.pressure, 2) + " MPa · Abrieb " +
                                fmtNum((i < o->boreWear.size() ? o->boreWear[i] : 0.0) * 1000.0, 4) + " mm")
                                   .c_str());
        }
        for (const CylGeom& c : o->coll.cylinders) ImGui::TextDisabled("Stift Ø%.2f mm × %.1f mm (exakt rund)", c.radius * 2000.0, c.halfLength * 2000.0);
    }
    {  // Schmieren (Öl, Fett, Wachs, Seife): weniger Reibung und Abrieb, z.B. für Scharniere, Achsen, Schubladen
        int lub = o->lubricant;
        std::string items;
        for (const auto& l : lubricants()) items += std::string(l.label) + std::string(1, 0);
        if (ImGui::Combo("Schmieren", &lub, items.c_str()) && lub != o->lubricant)
            runLine("lubricate " + std::to_string(o->id) + " " + lubricants()[lub].name, "konsole");
    }
    ImGui::Separator();
    bool unb = o->unbreakable;
    if (ImGui::Checkbox("Unzerbrechlich", &unb)) {
        if (remote) {
            runLine("unbreakable " + idText + (unb ? " on" : " off"), "konsole");
        } else {
            world_->pushUndo();
            o->unbreakable = unb;
        }
    }
    if (ImGui::Button("Duplizieren")) duplicateSelected();
    ImGui::SameLine();
    if (ImGui::Button("Löschen")) deleteSelected();
    ImGui::SameLine();
    if (ImGui::Button("Zeichnung (F4)")) showDrawing(selection_);
    ImGui::SameLine();
    if (ImGui::Button("Physik (F3)")) showInspector_ = !showInspector_;
    if (o->kind == ObjKind::Solid) ImGui::TextDisabled("%d konvexe Stücke, %d Flächen", (int)o->solid.pieces.size(), (int)solid::faceCount(o->solid));
    ImGui::End();
}

void App::drawStatusBar() {
    ImGuiIO& io = ImGui::GetIO();
    const float h = ImGui::GetFrameHeight() + 6;
    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - h));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
    std::string left;
    if (!status_.empty() && time_ - statusTime_ < 6.0) {
        left = status_;
    } else if (play_ && axe_) {
        left = "AXT: Maus links hackt (auf Holz zielen, bis 2,6 m) · X Axt weglegen · WASD laufen · Tab Bauen";
    } else if (play_) {
        left = "WASD laufen · Leertaste springen · Shift rennen · Maus links tragen · E aufnehmen · rechts werfen · F " +
               std::string(heatTools()[heatTool_].label) + " · X Axt · Tab Bauen";
    } else if (Object* o = world_->find(hovered_)) {
        left = o->name + "  [" + materials()[o->material].label + ", " + fmtNum(o->mass, 3) + " kg";
        if (!o->heat.empty() && (std::abs(o->heatInfo.tMax - world_->ambient()) > 5 || o->heatInfo.tMin < world_->ambient() - 5))
            left += ", " + fmtNum(world_->temperatureAt(*o, hoverHit_.point), 0) + " °C";
        left += "]";
    } else {
        left = "Rechte Maus drehen · Mitte/Shift+Rechts verschieben · Rad zoomen · 1-5 Ansichten · Tab Spielen · F1 Konsole · F3 Physik · F4 Zeichnung";
    }
    ImGui::TextUnformatted(left.c_str());
    char right[200];
    const PhysicsStats& ps = physics_.stats();
    if (play_)
        std::snprintf(right, sizeof(right), "Physik %.2f ms (%d Schritte, %d Kontakte, %d aktiv) · Teile %d · %.0f FPS", ps.frameMs, ps.steps,
                      ps.contacts, ps.activeBodies, drawnObjects_, fps_);
    else
        std::snprintf(right, sizeof(right), "Chunks %zu · Teile %d (LOD %d/%d/%d) · ausgeblendet %d · %.0f FPS", world_->grid().chunkCount(),
                      drawnObjects_, drawnLod_[0], drawnLod_[1], drawnLod_[2], culledObjects_, fps_);
    float w = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, io.DisplaySize.x - w - 16));
    ImGui::TextDisabled("%s", right);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void App::drawConsole() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 380, io.DisplaySize.y - 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 360), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Konsole (F1)", &showConsole_)) {
        ImGui::End();
        return;
    }
    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("log", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clip;  // bis zu 3000 Zeilen: nur die sichtbaren zeichnen
    clip.Begin((int)log_.size());
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const LogLine& l = log_[i];
            ImVec4 c = l.kind == 1 ? kAccent : l.kind == 2 ? ImVec4(1, 0.4f, 0.35f, 1) : l.kind == 3 ? ImVec4(0.5f, 0.8f, 1, 1) : ImVec4(0.88f, 0.88f, 0.88f, 1);
            ImGui::TextColored(c, "%s", l.text.c_str());
        }
    if (scrollLog_) {
        ImGui::SetScrollHereY(1.0f);
        scrollLog_ = false;
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1);
    if (focusConsole_) {
        ImGui::SetKeyboardFocusHere();
        focusConsole_ = false;
    }
    auto cb = [](ImGuiInputTextCallbackData* d) -> int {
        App* app = static_cast<App*>(d->UserData);
        if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory && !app->history_.empty()) {
            int n = (int)app->history_.size();
            if (d->EventKey == ImGuiKey_UpArrow) app->historyPos_ = app->historyPos_ < 0 ? n - 1 : std::max(0, app->historyPos_ - 1);
            else if (d->EventKey == ImGuiKey_DownArrow) app->historyPos_ = app->historyPos_ < 0 ? -1 : (app->historyPos_ + 1 >= n ? -1 : app->historyPos_ + 1);
            d->DeleteChars(0, d->BufTextLen);
            if (app->historyPos_ >= 0) d->InsertChars(0, app->history_[app->historyPos_].c_str());
        }
        return 0;
    };
    if (ImGui::InputTextWithHint("##cmd", "Befehl eingeben, z.B. hilfe", consoleInput_, sizeof(consoleInput_),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, cb, this)) {
        std::string line = consoleInput_;
        consoleInput_[0] = 0;
        if (!line.empty()) {
            history_.push_back(line);
            historyPos_ = -1;
            runLine(line, "konsole");
        }
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}

void App::drawHelp() {
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Steuerung", &showHelp_)) {
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Bau-Modus (CAD, keine Schwerkraft)");
    ImGui::BulletText("Rechte Maus: Kamera drehen · Mittlere Maus / Shift+Rechts: verschieben · Mausrad: zoomen");
    ImGui::BulletText("1 Perspektive · 2 Oben · 3 Vorne · 4 Seite · 5 Vier Ansichten");
    ImGui::BulletText("V Auswahl/Bewegen · S Säge (X/Y/Z = Achse) · B Bohrer · E Abrunden · M Fräsen · P Hobeln");
    ImGui::BulletText("Klick auswählen · Shift/Strg+Klick mehrere · ziehen = bewegen (Shift = Höhe, Alt = ohne Raster)");
    ImGui::BulletText("Beim Ziehen stoßen Teile an anderen an und gleiten entlang · Strg halten = hindurch");
    ImGui::BulletText("R 90° drehen · G absetzen · F hinzoomen · Entf löschen · Strg+D duplizieren");
    ImGui::BulletText("Strg+Z rückgängig · Strg+Y wiederholen · Strg+S speichern · Strg+O laden");
    ImGui::SeparatorText("Spiel-Modus (Physik)");
    ImGui::BulletText("WASD laufen · Shift rennen · Leertaste springen · Maus schauen");
    ImGui::BulletText("Maus links halten: tragen · E: aufnehmen/ablegen · Maus rechts: werfen · Rad: Abstand");
    ImGui::BulletText("Esc: Maus freigeben · Tab: zurück in den Bau-Modus");
    ImGui::SeparatorText("Überall");
    ImGui::BulletText("F1 Konsole · F3 Physik-Inspektor · F4 Dreitafelprojektion · F12 Foto");
    ImGui::End();
}

void App::drawViewLabels() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float sx = io.DisplayFramebufferScale.x, sy = io.DisplayFramebufferScale.y;
    for (const View& v : views_) {
        const char* name = v.mode == ViewMode::Top ? "Oben (Draufsicht, X/Z)" : v.mode == ViewMode::Front ? "Vorne (X/Y)" : v.mode == ViewMode::Side ? "Seite rechts (Z/Y)" : "Perspektive";
        float x = v.x / sx + 10, y = v.y / sy + (v.y == 0 ? 30.0f : 8.0f);
        if (views_.size() == 1) x = io.DisplaySize.x * 0.5f - ImGui::CalcTextSize(name).x * 0.5f;
        dl->AddText(ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 160), name);
        dl->AddText(ImVec2(x, y), IM_COL32(255, 210, 150, 230), name);
    }
    if (views_.size() == 4) {
        float cx = (views_[1].x - 1) / sx, cy = (views_[2].y - 1) / sy;
        dl->AddLine(ImVec2(cx, 0), ImVec2(cx, io.DisplaySize.y), IM_COL32(255, 160, 60, 160), 2);
        dl->AddLine(ImVec2(0, cy), ImVec2(io.DisplaySize.x, cy), IM_COL32(255, 160, 60, 160), 2);
    }
}

void App::drawPlayHud() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImU32 col = lookAt_ ? IM_COL32(255, 200, 80, 230) : IM_COL32(255, 255, 255, 200);
    dl->AddLine(ImVec2(c.x - 8, c.y), ImVec2(c.x + 8, c.y), col, 2);
    dl->AddLine(ImVec2(c.x, c.y - 8), ImVec2(c.x, c.y + 8), col, 2);
    std::string info;
    if (Object* g = world_->find(hand_.grabbed)) {
        info = "Trägt: " + g->name + " (" + fmtNum(g->mass, 2) + " kg)";
        if (hand_.tooHeavy) info += " - zu schwer zum Heben!";
    } else if (Object* o = world_->find(lookAt_)) {
        info = o->name + "  " + fmtNum(o->mass, 2) + " kg" + (o->fixed ? "  (fest)" : "  [Maus links / E]");
        // Temperatur nur, wenn sie auffällt (heiß, kalt, brennt)
        const double tc = lookHit_.obj == o ? world_->temperatureAt(*o, lookHit_.point) : world_->ambient();
        if (std::abs(tc - world_->ambient()) > 15 || o->heatInfo.burning) info += "  " + fmtNum(tc, 0) + " °C" + (o->heatInfo.burning ? ", brennt" : "");
        if (heatActive_) info += "  [F: " + std::string(heatTools()[heatTool_].label) + "]";
    }
    if (!info.empty()) {
        ImVec2 ts = ImGui::CalcTextSize(info.c_str());
        dl->AddRectFilled(ImVec2(c.x - ts.x / 2 - 8, c.y + 22), ImVec2(c.x + ts.x / 2 + 8, c.y + 26 + ts.y), IM_COL32(0, 0, 0, 140), 4);
        dl->AddText(ImVec2(c.x - ts.x / 2, c.y + 24), IM_COL32(255, 255, 255, 240), info.c_str());
    }
    if (!cursorCaptured_) {
        const char* t = "Pausiert - klicken zum Weiterspielen (Tab = Bau-Modus)";
        ImVec2 ts = ImGui::CalcTextSize(t);
        dl->AddRectFilled(ImVec2(c.x - ts.x / 2 - 12, c.y - 60), ImVec2(c.x + ts.x / 2 + 12, c.y - 52 + ts.y), IM_COL32(0, 0, 0, 170), 6);
        dl->AddText(ImVec2(c.x - ts.x / 2, c.y - 56), IM_COL32(255, 220, 160, 255), t);
    }
}

// ============================================================================ Material-Editor

// Alle Werte kommen aus der Feldtabelle (materialFields): neue Werte erscheinen hier, in der Datei und bei
// "materials set" automatisch. Änderungen gelten sofort für alle Teile, gespeichert wird mit "Speichern".
void App::drawMaterialEditor() {
    ImGui::SetNextWindowSize(ImVec2(760, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Materialien", &showMaterials_)) {
        ImGui::End();
        return;
    }
    auto& list = editableMaterials();
    matEdit_ = std::clamp(matEdit_, 0, (int)list.size() - 1);
    // Liste links
    ImGui::BeginChild("##matlist", ImVec2(210, 0), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)list.size(); ++i) {
        const std::string item = std::string(list[i].label) + "  (" + list[i].name + ")";
        if (ImGui::Selectable(item.c_str(), i == matEdit_)) matEdit_ = i;
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##newmat", "neuer Name, z. B. pine", matNew_, sizeof(matNew_));
    if (ImGui::Button("Neu (Kopie)", ImVec2(-1, 0))) {
        std::string err;
        const int m = addMaterial(matNew_, matEdit_, &err);
        if (m >= 0) {
            matEdit_ = m;
            matNew_[0] = 0;
            materialsDirty_ = true;
            matStatus_ = std::string("neu: ") + list[m].name;
        } else {
            matStatus_ = err;
        }
    }
    if (ImGui::Button("Speichern", ImVec2(-1, 0))) {
        const std::string path = materialSavePath();
        std::string err;
        if (saveMaterialFile(path, &err)) {
            materialsDirty_ = false;
            matStatus_ = "gespeichert: " + path;
        } else {
            matStatus_ = err;
        }
    }
    if (ImGui::Button("Datei neu laden", ImVec2(-1, 0))) {
        runLine("materials reload", "konsole");
        materialsDirty_ = false;
        matBufFor_ = -1;
        matStatus_ = "neu geladen";
    }
    ImGui::TextWrapped("%s", materialFile().empty() ? "(eingebaute Liste)" : materialFile().c_str());
    if (materialsDirty_) ImGui::TextColored(kAccent, "geändert, noch nicht gespeichert");
    if (!matStatus_.empty()) ImGui::TextWrapped("%s", matStatus_.c_str());
    ImGui::EndChild();
    ImGui::SameLine();

    // Werte rechts
    ImGui::BeginChild("##matvalues", ImVec2(0, 0));
    Material& m = list[matEdit_];
    if (matBufFor_ != matEdit_) {  // Textfelder füllen, wenn ein anderes Material gewählt wurde
        matBufFor_ = matEdit_;
        std::snprintf(matName_, sizeof(matName_), "%s", m.name);
        std::snprintf(matLabel_, sizeof(matLabel_), "%s", m.label);
        std::snprintf(matAliases_, sizeof(matAliases_), "%s", materialValue(m, *findMaterialField("aliases")).c_str());
    }
    bool apply = false;  // Physik der Teile neu (beim Loslassen, nicht bei jedem Zwischenwert)
    auto changed = [&](bool done) {
        materialsDirty_ = true;
        apply |= done;
    };
    ImGui::PushItemWidth(260);
    if (ImGui::InputText("Befehlsname", matName_, sizeof(matName_), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
        std::string err;
        if (std::string(matName_) != m.name) {
            if (renameMaterial(matEdit_, matName_, &err)) changed(true);
            else matStatus_ = err;
            std::snprintf(matName_, sizeof(matName_), "%s", m.name);
        }
    }
    const char* group = "";
    for (const MaterialField& f : materialFields()) {
        if (std::strcmp(group, f.group) != 0) ImGui::SeparatorText(f.group);
        group = f.group;
        const std::string label = std::string(f.label) + (*f.unit ? std::string(" (") + f.unit + ")" : std::string()) + "##" + f.key;
        switch (f.type) {
        case MaterialField::Text:
            ImGui::InputText(label.c_str(), matLabel_, sizeof(matLabel_));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                setMaterialLabel(matEdit_, matLabel_);
                changed(false);
            }
            break;
        case MaterialField::Words:
            ImGui::InputText(label.c_str(), matAliases_, sizeof(matAliases_));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                setMaterialValue(m, f, matAliases_, nullptr);
                changed(false);
            }
            break;
        case MaterialField::Choice: {
            int v = materialChoice(m, f);
            if (ImGui::BeginCombo(label.c_str(), f.choices[v])) {
                for (int i = 0; f.choices[i]; ++i)
                    if (ImGui::Selectable(f.choices[i], i == v)) {
                        setMaterialChoice(m, f, i);
                        changed(true);
                    }
                ImGui::EndCombo();
            }
            break;
        }
        case MaterialField::Color: {
            int n = 0;
            float* v = materialNumbers(m, f, n);
            if (ImGui::ColorEdit3(label.c_str(), v, ImGuiColorEditFlags_Float)) changed(false);
            break;
        }
        case MaterialField::Name: {  // ein anderes Material (Rest nach dem Abbrennen) oder keins
            const std::string cur = materialValue(m, f);
            if (ImGui::BeginCombo(label.c_str(), cur == "none" ? "none (Asche)" : cur.c_str())) {
                if (ImGui::Selectable("none (Asche)", cur == "none")) {
                    setMaterialValue(m, f, "none", nullptr);
                    changed(false);
                }
                for (const Material& r : list)
                    if (&r != &m && ImGui::Selectable(r.name, cur == r.name)) {
                        setMaterialValue(m, f, r.name, nullptr);
                        changed(false);
                    }
                ImGui::EndCombo();
            }
            break;
        }
        case MaterialField::Optional: {
            int n = 0;
            float* v = materialNumbers(m, f, n);
            bool on = materialValue(m, f) != "none";
            if (ImGui::Checkbox(("##on" + std::string(f.key)).c_str(), &on)) {
                setMaterialValue(m, f, on ? std::to_string(0.5f * (f.lo + f.hi)) : std::string("none"), nullptr);
                changed(true);
            }
            ImGui::SameLine();
            if (on) {
                ImGui::SetNextItemWidth(230);
                if (ImGui::InputFloat(label.c_str(), v, 0, 0, "%.6g")) changed(false);
                if (ImGui::IsItemDeactivatedAfterEdit()) changed(true);
            } else {
                ImGui::TextDisabled("%s: keins", f.label);
            }
            break;
        }
        default: {  // Zahl oder Zahlenpaar
            int n = 0;
            float* v = materialNumbers(m, f, n);
            if (n == 2 ? ImGui::InputFloat2(label.c_str(), v, "%.6g") : ImGui::InputFloat(label.c_str(), v, 0, 0, "%.6g")) changed(false);
            if (ImGui::IsItemDeactivatedAfterEdit()) changed(true);
            if (f.hi > f.lo) v[0] = std::clamp(v[0], std::min(f.lo, 0.0f), f.hi * 10.0f);  // offensichtlich falsche Werte abfangen
            break;
        }
        }
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    ImGui::TextDisabled("Klasse: Verhalten (Reibungspaare, Nägel, Faser, Gummi federt). Textur: nur das Aussehen.");
    ImGui::TextDisabled("Reibung zwischen zwei Materialien: Abschnitte [pair ...] in der Datei.");
    ImGui::EndChild();
    if (apply && !netClient()) world_->applyMaterials({});
    ImGui::End();
}

// ============================================================================ Physik-Inspektor (F3)

void App::drawInspector() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 470, io.DisplaySize.y * 0.35f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Physik-Inspektor (F3)", &showInspector_)) {
        ImGui::End();
        return;
    }
    if (!world_->find(inspectTarget_)) {
        ImGui::TextWrapped(play_ ? "Schau ein Teil an oder trage es." : "Wähle ein Teil aus.");
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Pfeile: rot Gewicht · grün Kontakt · lila Hand · cyan Luft · blau Geschwindigkeit · weiß Summe");
    if (netClient()) {  // Messwerte kommen vom Gastgeber (dort läuft die Physik)
        if (time_ >= netInspectAt_) {
            netInspectAt_ = time_ + 0.5;
            netSendCommand("inspect " + std::to_string(inspectTarget_), 0, 0, true);
        }
        if (netInspectText_.empty()) netInspectText_ = world_->inspectText(inspectTarget_);
    }
    std::istringstream ss(netClient() ? netInspectText_ : world_->inspectText(inspectTarget_));
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line[0] != ' ') {
            ImGui::Spacing();
            ImGui::TextColored(kAccent, "%s", line.c_str());
            ImGui::Separator();
        } else {
            ImGui::TextUnformatted(line.c_str());
        }
    }
    ImGui::End();
}

// ============================================================================ Dreitafelprojektion (F4)

void App::refreshDrawing() {
    std::string err;
    std::vector<uint32_t> ids;
    for (uint32_t id : drawingIds_)
        if (world_->find(id)) ids.push_back(id);
    drawingIds_ = ids;
    if (!makeThreeView(*world_, drawingIds_, drawing_, &err)) {
        drawingInfo_ = err;
        sheet_ = Sheet{};
        return;
    }
    sheet_ = layoutSheet(drawing_);
    drawingInfo_ = drawingSummary(drawing_, sheet_);
}

void App::drawDrawingWindow() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 520, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1040, 820), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dreitafelprojektion (F4)", &showDrawing_)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Aktualisieren")) refreshDrawing();
    ImGui::SameLine();
    if (ImGui::Button("Auswahl zeichnen") && !selection_.empty()) showDrawing(selection_);
    ImGui::SameLine();
    if (ImGui::Button("Als SVG speichern...")) {
        std::string f = fileDialog(window_, true, L"SVG-Zeichnung (*.svg)\0*.svg\0", L"svg");
        if (!f.empty()) {
            std::ofstream out(std::filesystem::u8path(f), std::ios::binary);
            out << sheetToSvg(sheet_);
            log("Zeichnung gespeichert: " + f, 3);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Maßstab %s · Projektionsmethode 1 · Maße in mm", sheet_.scaleText.c_str());
    if (ImGui::CollapsingHeader("Details")) ImGui::TextUnformatted(drawingInfo_.c_str());

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 50 || avail.y < 50) {
        ImGui::End();
        return;
    }
    const float k = std::min(avail.x / (float)sheet_.width, avail.y / (float)sheet_.height);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    p0.x += (avail.x - (float)sheet_.width * k) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + (float)sheet_.width * k, p0.y + (float)sheet_.height * k), IM_COL32(252, 252, 248, 255));
    auto P = [&](const glm::dvec2& p) { return ImVec2(p0.x + (float)p.x * k, p0.y + (float)p.y * k); };
    for (const SheetLine& l : sheet_.lines) styledLine(dl, P(l.a), P(l.b), l.style, k);
    for (const SheetTri& t : sheet_.tris) dl->AddTriangleFilled(P(t.a), P(t.b), P(t.c), IM_COL32(15, 15, 20, 255));
    ImFont* font = ImGui::GetFont();
    for (const SheetText& t : sheet_.texts) {
        float size = std::max(9.0f, (float)t.size * k * 1.35f);
        ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, t.text.c_str());
        ImVec2 c = P(t.p);
        if (t.vertical) c.x -= ts.x * 0.5f;
        dl->AddText(font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(10, 10, 15, 255), t.text.c_str());
    }
    ImGui::Dummy(ImVec2((float)sheet_.width * k, (float)sheet_.height * k));
    ImGui::End();
}
