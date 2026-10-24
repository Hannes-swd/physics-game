// Welt aus der Weltliste: öffnen, speichern (Strg+S, automatisch, beim Verlassen), Einstellungen, zurück ins Hauptmenü.
// Gespeichert wird in den Ordner der Welt (Worlds.h): erst die Szene, dann world.json mit Ansicht und Umgebung.
#include "App.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

#include "Profile.h"

namespace fs = std::filesystem;

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
    if (haveScene || haveBak) {
        std::string lerr, berr;
        bool ok = haveScene && world_->load(scene, &lerr);
        if (!ok && haveBak && world_->load(bak, &berr)) {
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
    if (haveWorld_ && !netClient()) saveWorld(true);
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
        if (opt_.menu) ss << "menu          'menu main|worlds|multiplayer|settings' saves and goes back to that page of the main menu; any other "
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
