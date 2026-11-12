// Debug-Ansicht (F3, wie in Minecraft; ohne Cheats): oben links in zwei Spalten, frei schwebend ohne Kasten.
// Links: Bildrate, Welt, Ort und Blick, Ziel. Rechts: was geladen ist und existiert, Grafik, Rechenzeit je Abschnitt in
// der letzten Sekunde (prof::recent, die Messpunkte stehen als PROFILE("...") im Code).
#include "App.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "Animals.h"
#include "Flora.h"
#include "Materials.h"
#include "Profile.h"
#include "Scatter.h"
#include "Terrain.h"
#include "WorldGen.h"

namespace {

// Abschnitte der Zeitmessung auf Deutsch (eingerückt: Teil eines größeren Abschnitts); sonst der Name aus dem Code
std::string sectionLabel(const std::string& id) {
    static const std::pair<const char*, const char*> names[] = {
        {"render (cpu + gpu wait)", "Zeichnen (mit Warten auf die GPU)"},
        {"render.waitGpu", "  Warten auf die Grafikkarte"},
        {"render.acquire (vsync)", "  Bild holen (VSync)"},
        {"world.step (play)", "Welt (Physik, Wärme, Wasser)"},
        {"world.heatOnly", "Welt (nur Wärme)"},
        {"physics.jolt", "  Physik"},
        {"step.heat", "  Wärme"},
        {"step.water", "  Wasser"},
        {"step.flow", "  Strömung"},
        {"step.joints", "  Verbindungen"},
        {"step.impacts", "  Stöße und Bruch"},
        {"step.bores", "  Bohren"},
        {"step.before (air, water)", "  Luft, Auftrieb"},
        {"step.sync+deepContacts", "  Abgleich, tiefe Kontakte"},
        {"app.input", "Eingabe"},
        {"app.fx", "Effekte (Rauch, Funken, Spuren)"},
        {"app.ui", "Oberfläche"},
        {"app.buildFrame", "Bild zusammenstellen"},
        {"app.trees", "  Bäume"},
        {"app.flora", "  Gras und Blumen"},
        {"app.scatter", "  Steine und Stöcke am Boden"},
        {"app.animals", "Tiere"},
        {"app.animals.draw", "  Tiere zeichnen"},
        {"app.water", "  Wasser (Bild)"},
        {"app.fireFx", "  Feuer (Bild)"},
        {"app.heatData", "  Temperaturen (Bild)"},
        {"terrain.view", "Gelände: Kacheln wählen"},
        {"terrain.upload", "  Gelände hochladen"},
        {"terrain.physics", "Gelände: Physik"},
        {"terrain.body", "  Gelände-Körper"},
        {"flora.build", "  Gras erzeugen"},
        {"scatter.prepare", "  Bodenzeug erzeugen"},
    };
    for (const auto& n : names)
        if (id == n.first) return n.second;
    return id;
}

const char* compass(double deg) {
    static const char* const dirs[8] = {"Norden", "Nordosten", "Osten", "Südosten", "Süden", "Südwesten", "Westen", "Nordwesten"};
    return dirs[(int)std::floor(std::fmod(deg + 22.5 + 720.0, 360.0) / 45.0) % 8];
}

} // namespace

void App::drawDebugOverlay() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* f = hudSmall_ ? hudSmall_ : ImGui::GetFont();
    ImFont* fb = hudBold_ ? hudBold_ : f;
    const float k = std::clamp(io.DisplaySize.y / 1080.0f, 0.85f, 1.6f);
    const float size = 16.0f * k, line = 21.0f * k, tab = 160.0f * k, pad = 5.0f * k;
    const ImU32 cHead = IM_COL32(182, 224, 122, 255), cKey = IM_COL32(190, 198, 192, 255), cVal = IM_COL32(255, 255, 255, 255);
    const ImU32 cWarn = IM_COL32(255, 170, 120, 255), cBack = IM_COL32(14, 16, 20, 175);

    float x0 = 16.0f * k, y = ImGui::GetFrameHeight() + 14.0f * k, x = x0, keyW = tab;
    // Jede Zeile auf einem dunklen, halb durchsichtigen Streifen so breit wie ihr Text (wie in Minecraft): lesbar vor
    // hellem Himmel und Bäumen, ohne einen großen Kasten über das Bild zu legen
    auto back = [&](float w) { dl->AddRectFilled(ImVec2(x - pad, y), ImVec2(x + w + pad, y + line), cBack); };
    auto width = [&](ImFont* font, const std::string& s) { return font->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x; };
    auto text = [&](ImFont* font, float px, float py, ImU32 c, const std::string& s) {
        const float ty = py + (line - size) * 0.5f - 1.0f;
        dl->AddText(font, size, ImVec2(px + 1, ty + 1), IM_COL32(0, 0, 0, 200), s.c_str());
        dl->AddText(font, size, ImVec2(px, ty), c, s.c_str());
    };
    auto head = [&](const std::string& s) {
        y += 6.0f * k;
        back(width(fb, s));
        text(fb, x, y, cHead, s);
        y += line;
    };
    auto row = [&](const std::string& key, const std::string& val, ImU32 vc = 0) {
        back(keyW + width(f, val));
        text(f, x, y, cKey, key);
        text(f, x + keyW, y, vc ? vc : cVal, val);
        y += line;
    };
    auto num = [](double v, int digits) {
        char b[48];
        std::snprintf(b, sizeof(b), "%.*f", digits, v);
        return std::string(b);
    };

    // =========================================================== links
    const prof::Recent& rc = prof::recent();
    {
        const std::string title = "Werkstatt  ·  " + num(rc.fps > 0 ? rc.fps : fps_, 0) + " FPS";
        back(width(fb, title));
        text(fb, x, y, cVal, title);
        y += line;
    }
    row("Bildzeit", num(rc.frameMs, 1) + " ms, langsamstes " + num(rc.worstMs, 1) + " ms", rc.worstMs > 50 ? cWarn : 0);

    head("WELT");
    row("Name", haveWorld_ ? worldInfo_.name : std::string("(keine Welt aus der Liste)"));
    if (haveWorld_) row("Seed", worldInfo_.seedText.empty() ? std::to_string(worldInfo_.seed) : worldInfo_.seedText);
    row("Modus", std::string(play_ ? "Spielen" : "Bauen") + (cheats_ ? ", Cheats an" : ""));
    row("Netz", netClient() ? "Mitspieler im LAN" : !peers_.empty() ? "Gastgeber, " + std::to_string(peers_.size()) + " Mitspieler" : "allein");
    {
        const int hh = (int)std::floor(dayHours_), mm = (int)std::floor((dayHours_ - hh) * 60.0);
        char b[32];
        std::snprintf(b, sizeof(b), "%02d:%02d", hh % 24, mm);
        row("Uhrzeit", std::string(b) + (dayRunning_ ? "" : " (steht)"));
    }

    head(play_ ? "SPIELFIGUR" : "KAMERA");
    glm::dvec3 pos = play_ ? (netClient() ? netFeet_ : physics_.hasCharacter() ? physics_.characterPosition() : playerEye()) : orbitTarget_;
    row("Position (m)", num(pos.x, 2) + " / " + num(pos.y, 2) + " / " + num(pos.z, 2));
    row("Chunk", std::to_string((int)std::floor(pos.x / kChunkSize)) + ", " + std::to_string((int)std::floor(pos.z / kChunkSize)) +
                     "   (" + num(kChunkSize, 0) + " m)");
    {
        const double yawDeg = glm::degrees(play_ ? -playYaw_ : -orbitYaw_ + glm::pi<double>());
        const double pitchDeg = glm::degrees(play_ ? playPitch_ : -orbitPitch_);
        const double head360 = std::fmod(yawDeg + 720.0, 360.0);
        row("Blick", std::string(compass(head360)) + " (" + num(head360, 0) + "°), Neigung " + num(pitchDeg, 0) + "°");
    }
    if (play_ && !netClient() && physics_.hasCharacter()) {
        const glm::dvec3 v = physics_.characterVelocity();
        const double sw = swimDepth(pos);
        std::string state = physics_.characterOnGround() ? "am Boden" : "in der Luft";
        if (sw > 0.05) state = "im Wasser (" + num(sw * 100, 0) + " %)";
        if (sneaking_) state += ", schleicht";
        row("Tempo", num(glm::length(glm::dvec2(v.x, v.z)), 1) + " m/s, " + state);
    }
    if (const Terrain* tr = world_->terrain()) {
        const WorldGen& gen = tr->gen();
        double sc[kMaxBiomes];
        const int bi = gen.singleBiome() >= 0 ? gen.singleBiome() : gen.classify(gen.climate(pos.x, pos.z), sc);
        row("Biom", gen.config().biomes[(size_t)bi].label);
    }

    head("ZIEL");
    const uint32_t target = play_ ? lookAt_ : hovered_;
    if (const Object* o = world_->find(target)) {
        row("Teil", "#" + std::to_string(o->id) + " " + o->name + (o->fixed ? " (fest)" : ""));
        row("Material", std::string(materials()[(size_t)std::clamp(o->material, 0, (int)materials().size() - 1)].label) + ", " +
                            num(o->mass, o->mass < 10 ? 2 : 1) + " kg");
        if (play_) row("Abstand", num(lookDist_, 2) + " m");
    } else {
        row("Teil", "–");
    }

    // =========================================================== rechts
    x = x0 + 520.0f * k;
    y = ImGui::GetFrameHeight() + 14.0f * k;
    head("GELADEN UND VORHANDEN");
    const PhysicsStats& ps = physics_.stats();
    int fixedParts = 0;
    for (const auto& o : world_->objects()) fixedParts += o->fixed ? 1 : 0;
    row("Teile", std::to_string(world_->objects().size()) + " vorhanden (" + std::to_string(fixedParts) + " fest), " +
                     std::to_string(drawnObjects_) + " gezeichnet");
    row("  Detailstufen", std::to_string(drawnLod_[0]) + " / " + std::to_string(drawnLod_[1]) + " / " + std::to_string(drawnLod_[2]) +
                              ", " + std::to_string(culledObjects_) + " ausgeblendet");
    row("Chunks mit Teilen", std::to_string(world_->grid().chunkCount()));
    if (const Terrain* tr = world_->terrain())
        row("Gelände", std::to_string(tr->drawList().size()) + " Kacheln, " + std::to_string(tr->missingTiles()) + " fehlen, " +
                           std::to_string(tr->pendingJobs()) + " in Arbeit, " + std::to_string(tr->editedChunks()) + " verändert");
    row("Bäume", std::to_string(treesDrawn_) + " gezeichnet");
    if (flora_) row("Gras und Blumen", std::to_string(flora_->tiles()) + " Kacheln, " + std::to_string(flora_->drawnTufts()) + " Büschel");
    if (const ScatterField* sf = world_->scatter()) row("Bodenzeug", std::to_string(sf->cachedTiles()) + " Kacheln");
    if (animals_) {
        int alive = 0;
        for (const Animal& a : animals_->animals()) alive += a.dead ? 0 : 1;
        row("Tiere", std::to_string(animals_->animals().size()) + " (" + std::to_string(alive) + " leben)");
    }
    row("Feuer, Vorlagen", std::to_string(world_->fires().size()) + " Feuer, " + std::to_string(world_->plans().size()) + " Bauvorlagen");
    row("Partikel, Spuren", std::to_string(fx_.size()) + " Partikel, " + std::to_string(snowPrints_.size()) + " Schneespuren");
    row("Inventar", std::to_string(inv_.size()) + " Teile, " + num(invMass(), 1) + " kg");

    head("PHYSIK");
    row("Rechenzeit", num(ps.frameMs, 2) + " ms in " + std::to_string(ps.steps) + " Schritten");
    row("Körper, Kontakte", std::to_string(ps.activeBodies) + " in Bewegung, " + std::to_string(ps.contacts) + " Kontakte");
    row("Eindringen", num(ps.maxPenetration * 1000, 1) + " mm", ps.maxPenetration > 0.02f ? cWarn : 0);

    head("GRAFIK");
    row("Grafikkarte", renderer_.gpuName());
    row("Bild", std::to_string((int)io.DisplaySize.x) + " × " + std::to_string((int)io.DisplaySize.y) + ", " +
                    std::to_string(renderer_.msaa()) + "× Kantenglättung" + (renderer_.sampleShading() ? " + Supersampling" : ""));
    row("Schatten", std::to_string(renderer_.shadowSize()) + " px" + std::string(showShadows_ ? "" : " (aus)"));

    head("RECHENZEIT JE BILD (letzte Sekunde)");
    keyW = 270.0f * k;  // lange Namen
    int shown = 0;
    for (const auto& s : rc.sections) {
        if (shown >= 14 || s.second < 0.01) break;
        const double pct = rc.frameMs > 0 ? 100.0 * s.second / rc.frameMs : 0;
        row(sectionLabel(s.first), num(s.second, 2) + " ms  (" + num(pct, 0) + " %)", s.second > 8 ? cWarn : 0);
        ++shown;
    }
    if (shown == 0) row("–", "noch keine Messung");
}
