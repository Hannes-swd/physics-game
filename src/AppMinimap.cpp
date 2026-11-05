// Minimap oben rechts (Spiel-Modus): Norden (−z) ist oben, Osten (+x) rechts. In der Mitte man selbst als Pfeil in
// Blickrichtung, Mitspieler im LAN als Pfeile in ihrer Farbe (außerhalb der Karte am Rand, kleiner).
// Das Gelände kommt aus dem Generator: je Zelle von 4 m einmal Farbe, Höhe und Wasser gerechnet und behalten. Bewegt man
// sich, kommen nur die neuen Zellen am Rand dazu (höchstens kBudget je Bild, fehlende sind so lange grau).
#include "App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "Terrain.h"

namespace {

constexpr double kCell = 4.0;     // m je Zelle
constexpr int kCells = 60;        // Zellen über die Breite der Karte (240 m)
constexpr float kSize = 210.0f;   // Pixel
constexpr int kBudget = 700;      // neue Zellen je Bild

uint64_t cellKey(int64_t i, int64_t j) { return ((uint64_t)(uint32_t)i << 32) | (uint32_t)j; }

ImU32 rgb(const glm::vec3& c, float a = 1.0f) {
    const glm::vec3 v = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f)) * 255.0f;
    return IM_COL32((int)v.r, (int)v.g, (int)v.b, (int)(a * 255.0f));
}

} // namespace

void App::drawMinimap() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float top = ImGui::GetFrameHeight() + 10.0f;  // unter der Menüleiste
    const ImVec2 a(io.DisplaySize.x - kSize - 12.0f, top), b(a.x + kSize, a.y + kSize);
    const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float px = kSize / float(kCells);  // Pixel je Zelle
    const float pxPerM = px / float(kCell);

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
        const double level = gen.riverLevel();
        const int64_t ci = (int64_t)std::floor(self.x / kCell), cj = (int64_t)std::floor(self.z / kCell);
        const int half = kCells / 2 + 2;
        // Fehlende Zellen rechnen: von innen nach außen, damit die Mitte zuerst da ist
        int budget = kBudget;
        for (int r = 0; r <= half && budget > 0; ++r)
            for (int dj = -r; dj <= r && budget > 0; ++dj)
                for (int di = -r; di <= r; di += (std::abs(dj) == r ? 1 : 2 * r)) {
                    const uint64_t k = cellKey(ci + di, cj + dj);
                    if (mapCells_.count(k)) {
                        continue;
                    }
                    GenSample s;
                    gen.sample((double(ci + di) + 0.5) * kCell, (double(cj + dj) + 0.5) * kCell, s);
                    MapCell c;
                    c.height = (float)s.height;
                    c.color = glm::pow(s.color, glm::vec3(1.0f / 2.2f)) * 1.2f;  // linear -> Bildschirm, wie "terrain map"
                    const RiverSample& rs = s.river;
                    const bool river = rs.strength > 0.3 && rs.dist < std::max(rs.half, kCell * 0.6);
                    const bool lake = s.lake > 0 && s.wet && s.height < level;
                    if (lake) {  // tiefer = dunkler
                        const float depth = (float)std::clamp((level - s.height) / 8.0, 0.0, 1.0);
                        c.color = glm::mix(glm::vec3(0.22f, 0.50f, 0.85f), glm::vec3(0.08f, 0.24f, 0.60f), depth);
                    } else if (river) {
                        c.color = glm::vec3(0.20f, 0.48f, 0.90f);
                    }
                    c.water = lake || river;
                    mapCells_.emplace(k, c);
                    --budget;
                }
        if (mapCells_.size() > 40000)  // weit weg: vergessen
            for (auto it = mapCells_.begin(); it != mapCells_.end();) {
                const int64_t i = (int32_t)(it->first >> 32), j = (int32_t)(it->first & 0xffffffffu);
                if (std::abs(i - ci) > 2 * half || std::abs(j - cj) > 2 * half) it = mapCells_.erase(it);
                else ++it;
            }
        // Zeichnen: die Zellen gleiten mit der genauen Lage mit (man bleibt in der Mitte)
        auto cellAt = [&](int64_t i, int64_t j) -> const MapCell* {
            auto it = mapCells_.find(cellKey(i, j));
            return it == mapCells_.end() ? nullptr : &it->second;
        };
        for (int dj = -half; dj <= half; ++dj)
            for (int di = -half; di <= half; ++di) {
                const int64_t i = ci + di, j = cj + dj;
                const float x0 = mid.x + float((double(i) * kCell - self.x) * pxPerM), y0 = mid.y + float((double(j) * kCell - self.z) * pxPerM);
                if (x0 > b.x || y0 > b.y || x0 + px < a.x || y0 + px < a.y) continue;
                const MapCell* c = cellAt(i, j);
                glm::vec3 col(0.35f, 0.36f, 0.38f);
                if (c) {
                    col = c->color;
                    if (!c->water) {  // Hang: Licht von links oben (Nordwesten)
                        const MapCell* w = cellAt(i - 1, j);
                        const MapCell* n = cellAt(i, j - 1);
                        const float dx = w ? c->height - w->height : 0.0f, dz = n ? c->height - n->height : 0.0f;
                        col *= std::clamp(1.0f + 0.9f * (dx + dz) / float(kCell), 0.55f, 1.35f);
                    }
                }
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + px + 0.6f, y0 + px + 0.6f), rgb(col));
            }
    } else {
        // flache Welt: Raster je Chunk (16 m)
        dl->AddRectFilled(a, b, IM_COL32(70, 82, 64, 255));
        const double g = 16.0;
        for (double x = std::floor((self.x - kCells * kCell) / g) * g; x <= self.x + kCells * kCell; x += g) {
            const float sx = mid.x + float((x - self.x) * pxPerM);
            dl->AddLine(ImVec2(sx, a.y), ImVec2(sx, b.y), IM_COL32(255, 255, 255, 30));
        }
        for (double z = std::floor((self.z - kCells * kCell) / g) * g; z <= self.z + kCells * kCell; z += g) {
            const float sy = mid.y + float((z - self.z) * pxPerM);
            dl->AddLine(ImVec2(a.x, sy), ImVec2(b.x, sy), IM_COL32(255, 255, 255, 30));
        }
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
    // Mitspieler: ihre Lage relativ zu mir; außerhalb der Karte am Rand festgehalten und kleiner
    if (netPlayers_.size() >= 2)
        for (const NetPlayer& p : netPlayers_) {
            if (p.id == netSelfId_) continue;
            const glm::dvec3 at = p.play ? p.feet : p.camEye;
            ImVec2 c(mid.x + float((at.x - self.x) * pxPerM), mid.y + float((at.z - self.z) * pxPerM));
            const float edge = 9.0f;
            const bool outside = c.x < a.x + edge || c.x > b.x - edge || c.y < a.y + edge || c.y > b.y - edge;
            c.x = std::clamp(c.x, a.x + edge, b.x - edge);
            c.y = std::clamp(c.y, a.y + edge, b.y - edge);
            arrow(c, p.yaw, playerColor(p.id), outside ? 5.5f : 8.0f);
        }
    arrow(mid, playYaw_, playerColor(netSelfId_ ? netSelfId_ : 1), 9.0f);

    // Rahmen, Norden, Maßstab
    dl->AddRect(a, b, IM_COL32(90, 95, 105, 230), 0.0f, 0, 1.0f);
    const float bar = 50.0f * pxPerM;  // 50 m
    const ImVec2 s0(a.x + 8, b.y - 8);
    dl->AddLine(s0, ImVec2(s0.x + bar, s0.y), IM_COL32(255, 255, 255, 220), 2.0f);
    dl->AddLine(ImVec2(s0.x, s0.y - 4), ImVec2(s0.x, s0.y + 1), IM_COL32(255, 255, 255, 220), 2.0f);
    dl->AddLine(ImVec2(s0.x + bar, s0.y - 4), ImVec2(s0.x + bar, s0.y + 1), IM_COL32(255, 255, 255, 220), 2.0f);
    dl->AddText(ImVec2(s0.x + bar + 4, s0.y - ImGui::GetFontSize() * 0.6f), IM_COL32(255, 255, 255, 220), "50 m");
}
