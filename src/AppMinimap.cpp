// Karten im Spiel-Modus: Minimap oben rechts und die große Karte (M). Norden (−z) ist oben, Osten (+x) rechts. Man selbst
// ist ein Pfeil in Blickrichtung, Mitspieler im LAN Pfeile in ihrer Farbe (außerhalb der Karte am Rand, kleiner).
// Das Gelände kommt aus dem Generator: je Zelle einmal Farbe, Höhe und Wasser gerechnet und behalten. Zellen gibt es in
// Stufen (4 m, 8 m, 16 m ...): je weiter herausgezoomt, desto gröber, damit eine Zelle immer ein paar Pixel groß ist.
// Fehlende Zellen werden je Bild nachgerechnet (die nächsten zuerst, auf allen Hilfs-Threads); bis dahin zeigt die
// gröbere Stufe, sonst grau.
#include "App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "Jobs.h"
#include "Terrain.h"

namespace {

constexpr double kCell = 4.0;          // m je Zelle der feinsten Stufe
constexpr int kLevels = 8;             // 4 m .. 512 m
constexpr float kMiniSize = 210.0f;    // Minimap: Pixel
constexpr double kMiniMeters = 240.0;  // Minimap: Breite in m

uint64_t cellKey(int level, int64_t i, int64_t j) {
    return ((uint64_t)level << 60) | (((uint64_t)i & 0x3FFFFFFFull) << 30) | ((uint64_t)j & 0x3FFFFFFFull);
}

ImU32 rgb(const glm::vec3& c, float a = 1.0f) {
    const glm::vec3 v = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f)) * 255.0f;
    return IM_COL32((int)v.r, (int)v.g, (int)v.b, (int)(a * 255.0f));
}

// schöne Länge für den Maßstab (1, 2, 5 · 10^n m), etwa "want" Meter
double niceLength(double want) {
    const double p = std::pow(10.0, std::floor(std::log10(std::max(want, 1e-3))));
    for (double f : {1.0, 2.0, 5.0, 10.0})
        if (f * p >= want * 0.7) return f * p;
    return 10.0 * p;
}

std::string lengthText(double m) { return m >= 1000.0 ? fmtNum(m / 1000.0, m >= 10000.0 ? 0 : 1) + " km" : fmtNum(m, 0) + " m"; }

} // namespace

void App::drawMapView(ImDrawList* dl, ImVec2 a, ImVec2 b, glm::dvec2 center, double mpp, int budget, bool clampOthers) {
    const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    auto toScreen = [&](double x, double z) { return ImVec2(mid.x + float((x - center.x) / mpp), mid.y + float((z - center.y) / mpp)); };
    const glm::dvec3 self = netClient() ? netFeet_ : physics_.characterPosition();
    Terrain* tr = world_->terrain();

    dl->AddRectFilled(a, b, IM_COL32(18, 20, 26, 255));
    dl->PushClipRect(a, b, true);
    if (tr) {
        const WorldGen& gen = tr->gen();
        if (mapGen_ != &gen) {  // neue Welt oder "terrain reload"
            mapCells_.clear();
            mapGen_ = &gen;
        }
        const double waterLevel = gen.riverLevel();
        // Stufe: eine Zelle mindestens ~5 Pixel (Minimap: 3,5, dort sind es wenige)
        int level = 0;
        while (level < kLevels - 1 && kCell * double(1 << level) < ((b.x - a.x) < 400 ? 3.5 : 5.0) * mpp) ++level;
        const double cell = kCell * double(1 << level);
        const double halfW = (b.x - a.x) * 0.5 * mpp, halfH = (b.y - a.y) * 0.5 * mpp;
        const int64_t i0 = (int64_t)std::floor((center.x - halfW) / cell), i1 = (int64_t)std::floor((center.x + halfW) / cell);
        const int64_t j0 = (int64_t)std::floor((center.y - halfH) / cell), j1 = (int64_t)std::floor((center.y + halfH) / cell);
        auto cellAt = [&](int lv, int64_t i, int64_t j) -> const MapCell* {
            auto it = mapCells_.find(cellKey(lv, i, j));
            return it == mapCells_.end() ? nullptr : &it->second;
        };
        // Fehlende rechnen: die der Mitte nächsten zuerst, parallel
        struct Want {
            double d;
            int64_t i, j;
        };
        std::vector<Want> want;
        for (int64_t j = j0; j <= j1; ++j)
            for (int64_t i = i0; i <= i1; ++i)
                if (!mapCells_.count(cellKey(level, i, j))) {
                    const double dx = (i + 0.5) * cell - center.x, dz = (j + 0.5) * cell - center.y;
                    want.push_back({dx * dx + dz * dz, i, j});
                }
        if ((int)want.size() > budget) {
            std::nth_element(want.begin(), want.begin() + budget, want.end(), [](const Want& p, const Want& q) { return p.d < q.d; });
            want.resize((size_t)budget);
        }
        if (!want.empty()) {
            std::vector<MapCell> done(want.size());
            jobs::parallelFor(want.size(), [&](size_t k) {
                GenSample s;
                gen.sample((double(want[k].i) + 0.5) * cell, (double(want[k].j) + 0.5) * cell, s);
                MapCell& c = done[k];
                c.height = (float)s.height;
                c.color = glm::pow(s.color, glm::vec3(1.0f / 2.2f)) * 1.2f;  // linear -> Bildschirm, wie "terrain map"
                const RiverSample& rs = s.river;
                const bool river = rs.strength > 0.3 && rs.dist < std::max(rs.half, cell * 0.6);
                const bool lake = s.lake > 0 && s.wet && s.height < waterLevel;
                if (lake) {  // tiefer = dunkler
                    const float depth = (float)std::clamp((waterLevel - s.height) / 8.0, 0.0, 1.0);
                    c.color = glm::mix(glm::vec3(0.22f, 0.50f, 0.85f), glm::vec3(0.08f, 0.24f, 0.60f), depth);
                } else if (river) {
                    c.color = glm::vec3(0.20f, 0.48f, 0.90f);
                }
                c.water = lake || river;
            });
            for (size_t k = 0; k < want.size(); ++k) mapCells_.emplace(cellKey(level, want[k].i, want[k].j), done[k]);
        }
        // Zeichnen; fehlt eine Zelle, die gröbere Stufe darüber. Zellen einmal nachschlagen (mit Rand für die Nachbarn),
        // dann direkt als Vierecke in die Zeichenliste (schneller als AddRectFilled je Zelle)
        const float px = float(cell / mpp);
        const int64_t nx = i1 - i0 + 2, nz = j1 - j0 + 2;
        std::vector<const MapCell*> grid((size_t)(nx * nz));
        for (int64_t j = j0 - 1; j <= j1; ++j)
            for (int64_t i = i0 - 1; i <= i1; ++i) grid[(size_t)((i - i0 + 1) + nx * (j - j0 + 1))] = cellAt(level, i, j);
        auto G = [&](int64_t i, int64_t j) { return grid[(size_t)((i - i0 + 1) + nx * (j - j0 + 1))]; };
        const int64_t total = (i1 - i0 + 1) * (j1 - j0 + 1);
        int64_t left = total;
        int reserved = 0;
        for (int64_t j = j0; j <= j1; ++j)
            for (int64_t i = i0; i <= i1; ++i) {
                if (reserved == 0) {  // in Blöcken reservieren (16-Bit-Indizes je Zeichenaufruf)
                    reserved = (int)std::min<int64_t>(left, 8000);
                    dl->PrimReserve(reserved * 6, reserved * 4);
                }
                --reserved, --left;
                const ImVec2 p0 = toScreen(double(i) * cell, double(j) * cell);
                const MapCell* c = G(i, j);
                glm::vec3 col(0.35f, 0.36f, 0.38f);
                if (c) {
                    col = c->color;
                    if (!c->water) {  // Hang: Licht von links oben (Nordwesten)
                        const MapCell* w = G(i - 1, j);
                        const MapCell* n = G(i, j - 1);
                        const float dx = w ? c->height - w->height : 0.0f, dz = n ? c->height - n->height : 0.0f;
                        col *= std::clamp(1.0f + 0.9f * (dx + dz) / float(cell), 0.55f, 1.35f);
                    }
                } else {
                    for (int up = 1; level + up < kLevels; ++up)
                        if (const MapCell* g = cellAt(level + up, i >> up, j >> up)) {
                            col = g->color;
                            break;
                        }
                }
                dl->PrimRect(p0, ImVec2(p0.x + px + 0.6f, p0.y + px + 0.6f), rgb(col));
            }
        // Speicher: weit weg und andere Stufen vergessen, wenn es zu viele werden
        if (mapCells_.size() > 300000)
            for (auto it = mapCells_.begin(); it != mapCells_.end();) {
                const int lv = (int)(it->first >> 60);
                const int64_t i = (int64_t)(((it->first >> 30) & 0x3FFFFFFFull) << 34) >> 34, j = (int64_t)((it->first & 0x3FFFFFFFull) << 34) >> 34;
                const double s = kCell * double(1 << lv);
                const bool near = std::abs((i + 0.5) * s - center.x) < 2 * halfW + s && std::abs((j + 0.5) * s - center.y) < 2 * halfH + s;
                if (!near || std::abs(lv - level) > 1) it = mapCells_.erase(it);
                else ++it;
            }
    } else {
        // flache Welt: Raster (16 m, weiter draußen gröber)
        dl->AddRectFilled(a, b, IM_COL32(70, 82, 64, 255));
        double g = 16.0;
        while (g / mpp < 12.0) g *= 4.0;
        const double halfW = (b.x - a.x) * 0.5 * mpp, halfH = (b.y - a.y) * 0.5 * mpp;
        for (double x = std::floor((center.x - halfW) / g) * g; x <= center.x + halfW; x += g)
            dl->AddLine(ImVec2(toScreen(x, 0).x, a.y), ImVec2(toScreen(x, 0).x, b.y), IM_COL32(255, 255, 255, 30));
        for (double z = std::floor((center.y - halfH) / g) * g; z <= center.y + halfH; z += g)
            dl->AddLine(ImVec2(a.x, toScreen(0, z).y), ImVec2(b.x, toScreen(0, z).y), IM_COL32(255, 255, 255, 30));
    }
    dl->PopClipRect();

    // Pfeil: Spitze in Blickrichtung. Blick (−sin yaw, −cos yaw) in x/z, auf der Karte x nach rechts, z nach unten
    auto arrow = [&](ImVec2 c, double yaw, const glm::vec3& color, float s) {
        const float dx = (float)-std::sin(yaw), dy = (float)-std::cos(yaw);
        auto P = [&](float along, float side) { return ImVec2(c.x + dx * along * s - dy * side * s, c.y + dy * along * s + dx * side * s); };
        const ImVec2 tip = P(1.15f, 0), l = P(-0.75f, 0.8f), r = P(-0.75f, -0.8f), notch = P(-0.3f, 0);
        const ImU32 fill = rgb(glm::min(color * 1.25f, glm::vec3(1.0f)));
        dl->AddTriangleFilled(tip, l, notch, fill);
        dl->AddTriangleFilled(tip, notch, r, fill);
        const ImVec2 outline[4] = {tip, l, notch, r};
        dl->AddPolyline(outline, 4, IM_COL32(10, 10, 12, 230), ImDrawFlags_Closed, 1.6f);
    };
    // Pfeile: außerhalb der Karte am Rand festgehalten und kleiner
    auto place = [&](const glm::dvec3& at, double yaw, const glm::vec3& color, float size) {
        ImVec2 c = toScreen(at.x, at.z);
        const float edge = 9.0f;
        const bool outside = c.x < a.x + edge || c.x > b.x - edge || c.y < a.y + edge || c.y > b.y - edge;
        if (outside && !clampOthers) size *= 0.7f;
        c.x = std::clamp(c.x, a.x + edge, b.x - edge);
        c.y = std::clamp(c.y, a.y + edge, b.y - edge);
        arrow(c, yaw, color, outside ? size * 0.65f : size);
    };
    if (netPlayers_.size() >= 2)
        for (const NetPlayer& p : netPlayers_)
            if (p.id != netSelfId_) place(p.play ? p.feet : p.camEye, p.yaw, playerColor(p.id), 8.0f);
    place(self, playYaw_, playerColor(netSelfId_ ? netSelfId_ : 1), 9.0f);

    dl->AddRect(a, b, IM_COL32(90, 95, 105, 230), 0.0f, 0, 1.0f);
    // Maßstab unten links
    const double len = niceLength(std::min(110.0, (b.x - a.x) * 0.3) * mpp);
    const float bar = float(len / mpp);
    const ImVec2 s0(a.x + 8, b.y - 8);
    const ImU32 white = IM_COL32(255, 255, 255, 220);
    dl->AddLine(s0, ImVec2(s0.x + bar, s0.y), white, 2.0f);
    dl->AddLine(ImVec2(s0.x, s0.y - 4), ImVec2(s0.x, s0.y + 1), white, 2.0f);
    dl->AddLine(ImVec2(s0.x + bar, s0.y - 4), ImVec2(s0.x + bar, s0.y + 1), white, 2.0f);
    dl->AddText(ImVec2(s0.x + bar + 4, s0.y - ImGui::GetFontSize() * 0.6f), white, lengthText(len).c_str());
}

void App::drawMinimap() {
    ImGuiIO& io = ImGui::GetIO();
    const float top = ImGui::GetFrameHeight() + 10.0f;  // unter der Menüleiste
    const ImVec2 a(io.DisplaySize.x - kMiniSize - 12.0f, top), b(a.x + kMiniSize, a.y + kMiniSize);
    const glm::dvec3 self = netClient() ? netFeet_ : physics_.characterPosition();
    drawMapView(ImGui::GetBackgroundDrawList(), a, b, glm::dvec2(self.x, self.z), kMiniMeters / kMiniSize, 1500, true);
}

void App::openMap(bool on) {
    if (on == mapOpen_) return;
    mapOpen_ = on;
    if (on) {
        const glm::dvec3 self = netClient() ? netFeet_ : physics_.characterPosition();
        mapCenter_ = glm::dvec2(self.x, self.z);
    }
    setCursorCaptured(!on);
}

void App::drawBigMap() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float top = ImGui::GetFrameHeight();
    const float bottom = ImGui::GetFrameHeight() + 6;  // Statusleiste
    dl->AddRectFilled(ImVec2(0, top), ImVec2(io.DisplaySize.x, io.DisplaySize.y - bottom), IM_COL32(0, 0, 0, 90));
    const float margin = 36.0f;
    const ImVec2 a(margin, top + margin), b(io.DisplaySize.x - margin, io.DisplaySize.y - bottom - margin - 22.0f);
    if (b.x - a.x < 50 || b.y - a.y < 50) return;
    const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);

    // Bedienung: Rad zoomt um den Mauszeiger, Ziehen (links oder rechts) verschiebt, Leertaste: zurück zu mir
    const ImVec2 m = io.MousePos;
    const bool inside = m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y;
    if (inside && io.MouseWheel != 0.0f) {
        const glm::dvec2 rel(m.x - mid.x, m.y - mid.y);
        const glm::dvec2 under = mapCenter_ + rel * mapMpp_;
        mapMpp_ = std::clamp(mapMpp_ * std::pow(0.8, (double)io.MouseWheel), 0.25, 80.0);
        mapCenter_ = under - rel * mapMpp_;
    }
    if (!io.WantCaptureMouse && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) && mapDragging_)
        mapCenter_ -= glm::dvec2(io.MouseDelta.x, io.MouseDelta.y) * mapMpp_;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) mapDragging_ = inside;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) mapDragging_ = false;
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        const glm::dvec3 self = netClient() ? netFeet_ : physics_.characterPosition();
        mapCenter_ = glm::dvec2(self.x, self.z);
    }

    drawMapView(dl, a, b, mapCenter_, mapMpp_, 12000, false);
    if (mapDragging_) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // Unter dem Mauszeiger: Ort (m) und Biom
    std::string info;
    if (inside) {
        const glm::dvec2 w = mapCenter_ + glm::dvec2(m.x - mid.x, m.y - mid.y) * mapMpp_;
        info = "x " + fmtNum(w.x, 0) + "  z " + fmtNum(w.y, 0);
        if (Terrain* tr = world_->terrain()) {
            const WorldGen& gen = tr->gen();
            double sc[kMaxBiomes];
            const int bi = gen.singleBiome() >= 0 ? gen.singleBiome() : gen.classify(gen.climate(w.x, w.y), sc);
            info += "  ·  " + gen.config().biomes[(size_t)bi].label;
        }
    }
    const std::string help = "M / Esc schließen  ·  Mausrad zoomen  ·  Maus ziehen verschieben  ·  Leertaste: zu mir";
    const ImU32 white = IM_COL32(235, 235, 235, 230);
    dl->AddText(ImVec2(a.x, b.y + 6), white, help.c_str());
    if (!info.empty()) {
        const ImVec2 ts = ImGui::CalcTextSize(info.c_str());
        dl->AddText(ImVec2(b.x - ts.x, b.y + 6), white, info.c_str());
    }
}
