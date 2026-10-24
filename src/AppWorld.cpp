// Welt aus der Weltliste: öffnen, speichern (Strg+S, automatisch, beim Verlassen), Einstellungen, zurück ins Hauptmenü.
// Gespeichert wird in den Ordner der Welt (Worlds.h): erst die Szene, dann world.json mit Ansicht und Umgebung.
// Dazu der Ladebildschirm: beim Öffnen einer Welt, beim Beitreten zu einem LAN-Spiel und beim Speichern am Ende.
#include "App.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

#include "Profile.h"

namespace fs = std::filesystem;

namespace {
constexpr float kLoadingFade = 0.35f;  // Ausblenden am Ende (s); Eingaben gehen dabei schon durch
}

// ============================================================================ Ladebildschirm

void App::beginLoading(LoadKind kind, const std::string& title, const std::string& name) {
    // Nur im Spiel aus dem Hauptmenü (oder --world). Ohne: Tests, Fotos und Skripte sehen wie bisher gleich die Welt
    if (!opt_.screenshotPath.empty() || (!opt_.menu && opt_.worldDir.empty())) return;
    loading_ = Loading();
    loading_.kind = kind;
    loading_.title = title;
    loading_.name = name;
    loading_.since = time_;
}

void App::updateLoading(float dt) {
    if (loading_.kind == LoadKind::None) return;
    if (loading_.fade > 0) {
        loading_.fade -= dt;
        if (loading_.fade <= 0) loading_.kind = LoadKind::None;
        return;
    }
    auto done = [&] {
        loading_.progress = 1;
        loading_.fade = kLoadingFade;
    };
    if (loading_.kind == LoadKind::World) {
        // Die Teile sind da, ihre genauen Kollisionsformen entstehen im Hintergrund (bis dahin grobe): abwarten, aber
        // nicht ewig (die Welt ist auch mit groben Formen spielbar)
        const size_t n = world_->pendingCollisionJobs();
        if (n == 0 || time_ - loading_.since > 20.0) return done();
        loading_.jobsAtStart = std::max(loading_.jobsAtStart, n);
        loading_.progress = 1.0f - (float)n / (float)loading_.jobsAtStart;
        loading_.detail = "Kollisionsformen berechnen: noch " + std::to_string(n);
    } else if (loading_.kind == LoadKind::Join) {
        if (netRole_ != NetRole::Client) {  // fehlgeschlagen oder abgebrochen: es geht zurück ins Hauptmenü
            loading_.kind = LoadKind::None;
            return;
        }
        loading_.progress = -1;
        if (!netConn_ || !netConn_->connected()) loading_.detail = "Verbinde mit " + netHostName_ + " ...";
        else if (!netWelcomed_) loading_.detail = netStatus_.rfind("Warte", 0) == 0 ? netStatus_ : "Warte auf Antwort von " + netHostName_ + " ...";
        else if (!netWorldReady_) loading_.detail = "Welt wird übertragen: " + std::to_string(world_->objects().size()) + " Teile";
        else done();
    }
}

void App::drawLoading() {
    if (loading_.kind == LoadKind::None) return;
    const float alpha = loading_.fade > 0 ? std::clamp(loading_.fade / kLoadingFade, 0.0f, 1.0f) : 1.0f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (loading_.fade <= 0) ImGui::SetNextWindowFocus();  // über allen anderen Fenstern
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    if (loading_.fade > 0) flags |= ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##loading", nullptr, flags)) {
        const float w = std::min(520.0f, vp->Size.x - 40.0f);
        ImGui::SetCursorPos(ImVec2((vp->Size.x - w) * 0.5f, vp->Size.y * 0.5f - 70.0f));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextUnformatted(loading_.title.c_str());
        if (!loading_.name.empty()) ImGui::TextDisabled("%s", loading_.name.c_str());
        ImGui::Spacing();
        // Unbestimmt (Beitreten): ein Stück läuft hin und her
        ImGui::ProgressBar(loading_.progress < 0 ? -1.0f * (float)ImGui::GetTime() : loading_.progress, ImVec2(w, 0), "");
        ImGui::TextUnformatted(loading_.detail.c_str());
        if (loading_.kind == LoadKind::Join && loading_.fade <= 0) {
            ImGui::Spacing();
            if (ImGui::Button("Abbrechen")) {
                netStop("");
                joinPending_ = false;
                exitMessage_ = "Beitreten abgebrochen";
                loading_.kind = LoadKind::None;
                leaveToMenu();
            }
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void App::renderLoadingFrame(bool force) {
    if (loading_.kind == LoadKind::None) return;
    const double now = glfwGetTime();
    if (!force && now - loading_.lastFrame < 1.0 / 30.0) return;  // während des Ladens höchstens 30 Bilder/s
    loading_.lastFrame = now;
    glfwPollEvents();  // das Fenster bleibt bedienbar (verschieben, kein "reagiert nicht")
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(window_, &fw, &fh);
    if (fw == 0 || fh == 0) return;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawLoading();
    ImGui::Render();
    FrameDesc fd;  // keine Ansichten: nur der Ladebildschirm
    renderer_.render(fd);
    firstFrameShown();
}

void App::firstFrameShown() {
    if (firstFrameShown_) return;
    firstFrameShown_ = true;
    if (opt_.onFirstFrame) opt_.onFirstFrame();  // Hauptmenü: sein Ladebild kann jetzt zu
}

// ============================================================================ Welt

bool App::openWorld(std::string* err) {
    WorldInfo w;
    if (!worlds::readInfo(fs::u8path(opt_.worldDir), w, err)) return false;
    worldInfo_ = w;
    haveWorld_ = true;
    worldReadOnly_ = false;

    // Szene: die Datei, sonst die Sicherung vom vorletzten Speichern. Fehlen beide, ist die Welt neu (flach und leer,
    // bis es einen Generator gibt)
    const fs::path scene = worlds::sceneFile(w);
    fs::path bak = scene;
    bak += L".bak";
    std::error_code ec;
    const bool haveScene = fs::exists(scene, ec), haveBak = fs::exists(bak, ec);
    beginLoading(LoadKind::World, "Welt wird geladen", w.name);
    loading_.detail = haveScene || haveBak ? "Szene lesen" : "Neue Welt";
    renderLoadingFrame(true);
    if (haveScene || haveBak) {
        auto progress = [&](size_t done, size_t all) {  // beim Anlegen der Teile: Ladebild mit Fortschritt
            loading_.progress = all ? (float)done / (float)all : 0.0f;
            loading_.detail = "Teile anlegen: " + std::to_string(done) + " / " + std::to_string(all);
            renderLoadingFrame(false);
        };
        std::string lerr, berr;
        bool ok = haveScene && world_->load(scene, &lerr, progress);
        if (!ok && haveBak && world_->load(bak, &berr, progress)) {
            ok = true;
            log("World loaded from the backup (" + (lerr.empty() ? std::string("scene file missing") : lerr) + ")", 2);
            toast("The world was loaded from its backup", false);
        }
        if (!ok) {  // nichts überschreiben: vielleicht lässt sich die Datei noch retten
            worldReadOnly_ = true;
            const std::string why = lerr.empty() ? berr : lerr;
            log("The world could not be loaded: " + why + ". It will not be saved, so nothing gets overwritten.", 2);
            toast("World not loaded: " + why, false);
        }
    }
    if (w.env.valid) {
        world_->setAir(w.env.airDensity, w.env.wind);
        world_->setBreakage(w.env.breakage);
        world_->setBuildCollision(w.env.buildCollision);
        world_->setHeat(w.env.heat);
        world_->setAmbient(w.env.ambient);
        world_->setHeatSpeed(w.env.heatSpeed);
    }
    if (w.view.valid) {
        orbitTarget_ = w.view.orbitTarget;
        orbitYaw_ = w.view.orbitYaw;
        orbitPitch_ = w.view.orbitPitch;
        orbitDist_ = std::clamp(w.view.orbitDist, 0.03, 20000.0);
        viewMode_ = (ViewMode)std::clamp(w.view.viewMode, 0, 4);
        if (w.view.play) {
            enterPlay();
            if (!netClient()) physics_.setCharacterPosition(w.view.feet);
            playYaw_ = w.view.yaw;
            playPitch_ = w.view.pitch;
        }
    }
    worldInfo_.lastPlayed = worlds::now();
    std::string werr;
    if (!worldReadOnly_ && !worlds::writeInfo(worldInfo_, &werr)) log("world.json: " + werr, 2);
    playTimeMark_ = time_;
    autosaveAccum_ = 0;
    log("World \"" + w.name + "\" (" + w.dir.u8string() + ")", 3);
    // Weiter mit den Kollisionsformen im Hintergrund (updateLoading), dann blendet der Ladebildschirm aus
    loading_.progress = 0;
    loading_.jobsAtStart = world_->pendingCollisionJobs();
    loading_.detail = "Kollisionsformen berechnen";
    return true;
}

bool App::saveWorld(bool quiet, bool evenAsClient) {
    if (!haveWorld_) return false;
    if (worldReadOnly_) {
        if (!quiet) toast("Not saved: the world could not be loaded, so it is not overwritten", false);
        return false;
    }
    if (netClient() && !evenAsClient) return false;  // die Welt gehört gerade dem Gastgeber
    PROFILE("world.save");
    const auto t0 = std::chrono::steady_clock::now();
    WorldInfo& w = worldInfo_;
    w.view.valid = true;
    w.view.play = play_;
    w.view.orbitTarget = orbitTarget_;
    w.view.orbitYaw = orbitYaw_;
    w.view.orbitPitch = orbitPitch_;
    w.view.orbitDist = orbitDist_;
    w.view.viewMode = (int)viewMode_;
    if (play_ && physics_.hasCharacter()) {
        w.view.feet = physics_.characterPosition();
        w.view.yaw = playYaw_;
        w.view.pitch = playPitch_;
    }
    w.env.valid = true;
    w.env.airDensity = world_->airDensity();
    w.env.wind = world_->wind();
    w.env.breakage = world_->breakage();
    w.env.buildCollision = world_->buildCollision();
    w.env.heat = world_->heat();
    w.env.ambient = world_->ambient();
    w.env.heatSpeed = world_->heatSpeed();
    w.playTime += time_ - playTimeMark_;
    playTimeMark_ = time_;
    w.lastPlayed = worlds::now();

    // Erst in eine .tmp-Datei, dann ersetzen (die alte wird zu .bak): ein Absturz beim Schreiben zerstört nichts
    const fs::path scene = worlds::sceneFile(w);
    fs::path tmp = scene;
    tmp += L".tmp";
    std::string err;
    const bool ok = world_->save(tmp, &err) && worlds::replaceFile(tmp, scene, &err) && worlds::writeInfo(w, &err);
    autosaveAccum_ = 0;
    if (!ok) {
        log("Saving the world failed: " + err, 2);
        toast("Saving failed: " + err, false);
        return false;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    char buf[64];
    std::snprintf(buf, sizeof(buf), " (%.0f ms)", ms);
    log(std::string(quiet ? "World saved automatically" : "World saved") + buf, 3);
    if (!quiet) toast("World saved");
    return true;
}

void App::updateWorldSession(float dt) {
    // Beitritt aus dem Hauptmenü: klappt er nicht (abgelehnt, keine Antwort), zurück ins Menü mit der Meldung
    if (joinPending_) {
        if (netWelcomed_) {
            joinPending_ = false;
        } else if (netRole_ == NetRole::Off) {
            joinPending_ = false;
            exitMessage_ = "Beitreten fehlgeschlagen: " + (netStatus_.empty() ? std::string("no answer") : netStatus_);
            backToMenu_ = quit_ = true;
        }
    }
    if (haveWorld_ && settings_.autosaveMinutes > 0 && !worldReadOnly_ && !netClient()) {
        autosaveAccum_ += dt;
        if (autosaveAccum_ >= settings_.autosaveMinutes * 60.0) saveWorld(true);
    }
}

void App::endWorldSession() {
    if (haveWorld_ && !netClient() && !worldReadOnly_) {
        beginLoading(LoadKind::Save, "Welt wird gespeichert", worldInfo_.name);
        renderLoadingFrame(true);
        saveWorld(true);
    }
    if (opt_.menu || haveWorld_ || !opt_.worldDir.empty()) storeSettings();
}

void App::applySettings() {
    settings_ = loadSettings();
    const std::string name = settings_.playerName.empty() ? defaultPlayerName() : settings_.playerName;
    std::snprintf(netName_, sizeof(netName_), "%s", name.c_str());
    mouseSens_ = settings_.mouseSensitivity;
    fov_ = glm::radians(settings_.fovDeg);
    showShadows_ = settings_.shadows;
    showEdges_ = settings_.edges;
    showGrid_ = settings_.grid;
}

void App::storeSettings() {
    // Was man im Spiel umgestellt hat, gilt auch beim nächsten Mal (der Rest bleibt, wie er in der Datei steht)
    GameSettings s = loadSettings();
    s.shadows = showShadows_;
    s.edges = showEdges_;
    s.grid = showGrid_;
    if (netName_[0] && netName_ != defaultPlayerName()) s.playerName = netName_;
    std::string err;
    if (!saveSettings(s, &err)) log("settings: " + err, 2);
}

void App::leaveToMenu() {
    backToMenu_ = true;
    quit_ = true;  // die Schleife endet, endWorldSession speichert
}

bool App::isMenuAction(const std::string& line) {
    std::istringstream in(line);
    std::string a, b;
    in >> a >> b;
    return a == "menu" && !b.empty() && b != "status";
}

// "menu" im Spiel: wo man ist. "menu <...>" (z.B. "menu worlds", "menu play Insel"): zurück ins Hauptmenü, das den
// Befehl dort ausführt. Über die Fernsteuerung kommt "menu <...>" gar nicht hier an (App::run reicht die Zeile weiter,
// damit die Antwort vom Hauptmenü kommt); das hier gilt für die Konsole und Skripte
std::string App::menuCommandInGame(const std::vector<std::string>& t) {
    const std::string sub = t.size() > 1 ? t[1] : "";
    if (sub.empty() || sub == "status") {
        std::ostringstream ss;
        ss << "screen        game";
        if (haveWorld_) ss << ", world \"" << worldInfo_.name << "\"";
        else if (netClient()) ss << ", LAN game of " << netHostName_;
        else ss << ", no world from the world list";
        ss << ", " << (play_ ? "play mode" : "build mode") << "\n";
        if (loading_.kind != LoadKind::None && loading_.fade <= 0) {  // Ladebildschirm: was gerade geladen wird
            static const char* kinds[] = {"", "world", "join", "save"};
            ss << "loading       " << kinds[(int)loading_.kind] << ": " << loading_.detail;
            if (loading_.progress >= 0) ss << " (" << (int)(loading_.progress * 100) << " %)";
            ss << "\n";
        }
        if (opt_.menu) ss << "menu         'menu main|worlds|multiplayer|settings' saves and goes back to that page of the main menu; any other "
                             "'menu ...' command runs there ('menu help' in the main menu lists them)";
        else ss << "menu          not available (the game was not started from the main menu)";
        return ss.str();
    }
    if (!opt_.menu) return "Error: the game was not started from the main menu";
    std::string line = "menu";
    for (size_t i = 1; i < t.size(); ++i) line += " " + (t[i].find(' ') != std::string::npos ? "\"" + t[i] + "\"" : t[i]);
    menuCommand_ = line;
    leaveToMenu();
    return "back to the main menu, then: " + line;
}

std::string App::worldCommand(const std::vector<std::string>& t) {
    const std::string sub = t.size() > 1 ? t[1] : "";
    if (sub == "save") {
        if (!haveWorld_) return "Error: no world is open (started without the world list)";
        if (netClient()) return "Error: in a LAN game the host saves the world";
        return saveWorld(false) ? "world saved: " + worlds::sceneFile(worldInfo_).u8string() : "Error: the world was not saved (see log)";
    }
    if (sub == "menu") {
        if (!opt_.menu) return "Error: the game was not started from the main menu";
        leaveToMenu();
        return "back to the main menu";
    }
    if (!sub.empty()) return "Error: world | world save | world menu";
    if (!haveWorld_) return "no world open (started without the world list; 'save <file>' saves the scene)";
    const WorldInfo& w = worldInfo_;
    std::ostringstream ss;
    ss << "world         " << w.name << (worldReadOnly_ ? " (not loaded, will not be saved)" : "") << "\n";
    ss << "folder        " << w.dir.u8string() << "\n";
    ss << "seed          " << w.seed << (w.seedText.empty() ? " (random)" : " (from \"" + w.seedText + "\")") << "\n";
    ss << "generator     " << w.generator << " v" << w.generatorVersion << "\n";
    ss << "created       " << worlds::formatTime(w.created) << ", played " << worlds::formatDuration(w.playTime + time_ - playTimeMark_) << "\n";
    ss << "autosave      ";
    if (settings_.autosaveMinutes > 0) ss << "every " << settings_.autosaveMinutes << " min, next in " << (int)std::max(0.0, settings_.autosaveMinutes * 60.0 - autosaveAccum_) << " s";
    else ss << "off";
    return ss.str();
}
