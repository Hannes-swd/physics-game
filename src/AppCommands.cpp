// Live-Steuerung per Befehl: Aufträge (Befehlsfolgen mit wait), Spielfigur, Werkzeuge, Fenster, Status
#include "App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Materials.h"
#include "Terrain.h"
#include "Scatter.h"
#include "Trees.h"
#include "WorldGen.h"

namespace {

constexpr double kReach = 3.5;  // so weit kann die Figur greifen (m)

// Nächster Punkt (Raster "step" in quadratischen Ringen um from), für den pred gilt; sucht weiter bis zum Ring, der
// nichts Näheres mehr enthalten kann. false: keiner bis maxDist
template <class Pred>
bool nearestSpot(const glm::dvec2& from, double step, double maxDist, Pred pred, glm::dvec2& out) {
    const int rings = (int)std::ceil(maxDist / step);
    int last = rings;
    double best = maxDist * maxDist;
    bool found = false;
    for (int ring = 1; ring <= last; ++ring)
        for (int j = -ring; j <= ring; ++j)
            for (int i = -ring; i <= ring; i += (std::abs(j) == ring ? 1 : 2 * ring)) {
                const glm::dvec2 p = from + glm::dvec2(i, j) * step;
                const double d2 = glm::dot(p - from, p - from);
                if (d2 >= best || !pred(p)) continue;
                best = d2;
                out = p;
                found = true;
                last = std::min(rings, (int)std::floor(std::sqrt(best) / step));
            }
    return found;
}

bool parseNum(const std::vector<std::string>& t, size_t i, double& v) {
    if (i >= t.size()) return false;
    std::string s = t[i];
    std::replace(s.begin(), s.end(), ',', '.');
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == 0 && !s.empty();
}

std::string arg(const std::vector<std::string>& t, size_t i) { return i < t.size() ? normalizeWord(t[i]) : std::string(); }

// "on"/"off"/leer (= umschalten)
bool onOff(const std::vector<std::string>& t, size_t i, bool current) {
    std::string s = arg(t, i);
    if (s.empty() || s == "toggle") return !current;
    return s == "on" || s == "yes" || s == "1" || s == "true" || s == "open";
}

const char* viewName(ViewMode m) {
    switch (m) {
    case ViewMode::Top: return "top";
    case ViewMode::Front: return "front";
    case ViewMode::Side: return "side";
    case ViewMode::Quad: return "quad";
    default: return "perspective";
    }
}

} // namespace

const char* App::toolName(Tool t) {
    switch (t) {
    case Tool::Saw: return "saw";
    case Tool::Drill: return "drill";
    case Tool::Round: return "round";
    case Tool::Mill: return "mill";
    case Tool::Plane: return "plane";
    case Tool::Heat: return "heat";
    default: return "select";
    }
}

// ============================================================================ Aufträge

void App::toast(const std::string& text, bool ok) {
    toasts_.push_back({text, time_, ok});
    while (toasts_.size() > 6) toasts_.pop_front();
}

void App::submit(const std::string& line, int source, uint64_t client) {
    if (source == 2) {
        log("[remote] > " + line, 1);
        toast("Remote: " + line);
    }
    if (netClient() && !localCommand(line)) {  // die Welt gehört dem Gastgeber
        netSendCommand(line, source, client, false);
        return;
    }
    std::string err;
    auto job = parseScript(line, &err);
    if (!job) {
        std::string msg = "Error: " + err;
        log(msg, 2);
        if (source == 2) {
            remote_->respond(client, msg);
            toast(msg, false);
        }
        if (source == 1) {
            std::printf("%s\n", msg.c_str());
            scriptBusy_ = false;
        }
        return;
    }
    Job j;
    j.job = std::move(job);
    j.source = source;
    j.client = client;
    j.line = line;
    jobs_.push_back(std::move(j));
    stepJobs();  // sofort ausführen, soweit kein Warten nötig ist
}

void App::stepJobs() {
    for (size_t i = 0; i < jobs_.size();) {
        Peer* peer = jobs_[i].peer ? findPeer(jobs_[i].peer) : nullptr;
        if (jobs_[i].peer && !peer) {  // Mitspieler ist weg
            jobs_.erase(jobs_.begin() + i);
            continue;
        }
        CommandHost& host = peer ? *peer->commands : static_cast<CommandHost&>(*this);
        bool done = stepScript(*jobs_[i].job, host, time_, [&](const std::string& t, bool ok) {
            if (!peer) log(t, ok ? 0 : 2);
        });
        if (!done) {
            ++i;
            continue;
        }
        Job j = std::move(jobs_[i]);
        jobs_.erase(jobs_.begin() + i);
        const std::string& out = j.job->output;
        if (peer) {
            netSendResult(*peer, j.request, *j.job);
        } else if (j.source == 2) {
            remote_->respond(j.client, out);
            std::string first;
            size_t p = 0;
            while (p < out.size()) {  // erste Ergebniszeile (ohne Echo "> ...")
                size_t e = out.find('\n', p);
                std::string l = out.substr(p, e == std::string::npos ? std::string::npos : e - p);
                if (!l.empty() && l.rfind("> ", 0) != 0) {
                    first = l;
                    break;
                }
                if (e == std::string::npos) break;
                p = e + 1;
            }
            if (!first.empty()) toast("\xE2\x86\x92 " + first, j.job->ok);  // "→"
        } else if (j.source == 1) {
            std::printf("%s", out.c_str());
            std::fflush(stdout);
            scriptBusy_ = false;
        }
    }
}

void App::processScriptQueue() {
    while (!scriptBusy_ && !scriptQueue_.empty()) {
        std::string line = scriptQueue_.front();
        scriptQueue_.pop_front();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos || line[s] == '#') continue;
        std::printf("> %s\n", line.c_str());
        log("[script] > " + line, 1);
        scriptBusy_ = true;
        submit(line, 1);
    }
}

void App::drawToasts() {
    while (!toasts_.empty() && time_ - toasts_.front().time > 6.0) toasts_.pop_front();
    if (toasts_.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float y = 34;
    for (const Toast& t : toasts_) {
        float age = (float)(time_ - t.time);
        float a = age < 4.5f ? 1.0f : std::max(0.0f, 1.0f - (age - 4.5f) / 1.5f);
        std::string text = t.text.size() > 110 ? t.text.substr(0, 107) + "..." : t.text;
        ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        float x = io.DisplaySize.x * 0.5f - ts.x * 0.5f;
        dl->AddRectFilled(ImVec2(x - 10, y - 3), ImVec2(x + ts.x + 10, y + ts.y + 3), IM_COL32(20, 20, 26, (int)(200 * a)), 5);
        ImU32 col = t.ok ? IM_COL32(150, 210, 255, (int)(255 * a)) : IM_COL32(255, 120, 110, (int)(255 * a));
        dl->AddText(ImVec2(x, y), col, text.c_str());
        y += ts.y + 8;
    }
}

// ============================================================================ Spielfigur

void App::lookAtPoint(const glm::dvec3& p) {
    glm::dvec3 d = p - playerEye();
    if (glm::length(d) < 1e-6) return;
    playYaw_ = std::atan2(-d.x, -d.z);
    playPitch_ = std::clamp(std::atan2(d.y, glm::length(glm::dvec2(d.x, d.z))), -1.52, 1.52);
}

bool App::grabObject(uint32_t id, double dist, const glm::dvec3& localPoint) {
    Object* o = world_->find(id);
    if (!o || o->fixed || o->body == kNoBody) return false;
    hand_.grabbed = id;
    hand_.grabLocal = localPoint;
    hand_.holdDist = std::clamp(dist, 0.5, kReach);
    hand_.holdRel = glm::conjugate(glm::angleAxis(playYaw_, glm::dvec3(0, 1, 0))) * o->rot;
    hand_.heldByMouse = false;
    physics_.activate(o->body);
    return true;
}

void App::throwHeld(double impulse) {
    Object* o = world_->find(hand_.grabbed);
    hand_.grabbed = 0;
    if (!o || o->body == kNoBody) return;
    double m = std::max(0.01, (double)physics_.mass(o->body));
    double j = impulse > 0 ? impulse : std::min(m * 9.0, 45.0);
    physics_.addImpulseAt(o->body, glm::vec3(playerForward() * j), o->pos + o->rot * hand_.grabLocal);
}

std::string App::playerCommand(const std::vector<std::string>& t) {
    const std::string sub = arg(t, 1);
    // Mitspieler im LAN: die Figur läuft beim Gastgeber, Befehle wirken wie Tasten (laufen, springen, E, werfen)
    const bool remote = netClient();
    auto partArg = [&](const std::string& a) -> uint32_t {
        if (a == "selected" || a == "sel") return selectedId();
        if (a == "last") return world_->lastCreatedId();
        return (uint32_t)std::atoi(a.c_str());
    };
    if (sub.empty() || sub == "info" || sub == "status") {
        if (remote ? !play_ : !physics_.hasCharacter()) return "No player yet (mode play)";
        glm::dvec3 p = (playerEye() - glm::dvec3(0, 1.65, 0)) * 100.0;
        std::string s = "position_cm   " + fmtNum(p.x, 1) + " " + fmtNum(p.y, 1) + " " + fmtNum(p.z, 1) + "  (feet)\n";
        s += "view          heading " + fmtNum(glm::degrees(playYaw_), 1) + "°, pitch " + fmtNum(glm::degrees(playPitch_), 1) + "°\n";
        if (!remote) s += std::string("on_ground     ") + (physics_.characterOnGround() ? "yes" : "no") + "\n";
        Object* g = world_->find(hand_.grabbed);
        s += "holding       " + (g ? "#" + std::to_string(g->id) + " " + g->name + " (" + fmtNum(g->mass, 3) + " kg)" : std::string("nothing")) + "\n";
        RayHit h;
        if (world_->raycast(playerEye(), playerForward(), h, hand_.grabbed) && h.obj && h.t < 10)
            s += "looking_at    #" + std::to_string(h.obj->id) + " " + h.obj->name + " (" + fmtNum(h.t, 2) + " m)\n";
        else
            s += "looking_at    nothing nearby\n";
        if (autoMoveTime_ > 0) s += "walking       " + fmtNum(autoMoveTime_, 1) + " s left\n";
        s += std::string("mode          ") + (play_ ? "play" : "build (player paused)") + "\n";
        return s;
    }
    if (!play_) enterPlay();
    if (sub == "view") {
        const std::string v = arg(t, 2);
        if (v == "first" || v == "1") thirdPerson_ = false;
        else if (v == "third" || v == "3") thirdPerson_ = true;
        else if (v.empty()) thirdPerson_ = !thirdPerson_;
        else return "Error: player view [first|third]";
        return thirdPerson_ ? "View from behind the player (F5)" : "View through the player's eyes (F5)";
    }
    if (remote && (sub == "position" || sub == "teleport" || sub == "rotate"))
        return "Error: player " + sub + " only works for the host's player (co-players: walk there, R/Shift+R turn a carried part)";
    if (sub == "position" || sub == "teleport") {
        double x, y, z;
        if (!parseNum(t, 2, x) || !parseNum(t, 3, y) || !parseNum(t, 4, z)) return "Error: player position x y z (cm, feet)";
        physics_.setCharacterPosition(glm::dvec3(x, y, z) / 100.0);
        return "Player stands at " + fmtNum(x) + " " + fmtNum(y) + " " + fmtNum(z) + " cm";
    }
    if (sub == "look") {
        double x, y, z;
        glm::dvec3 target;
        if (parseNum(t, 2, x) && parseNum(t, 3, y) && parseNum(t, 4, z)) {
            target = glm::dvec3(x, y, z) / 100.0;
        } else {
            Object* o = world_->find(partArg(arg(t, 2)));
            if (!o) return "Error: player look <id | x y z>";
            target = (o->wMin + o->wMax) * 0.5;
        }
        lookAtPoint(target);
        return "View: heading " + fmtNum(glm::degrees(playYaw_), 1) + "°, pitch " + fmtNum(glm::degrees(playPitch_), 1) + "°";
    }
    if (sub == "turn") {
        double deg;
        if (!parseNum(t, 2, deg)) return "Error: player turn <degrees> [pitch]   (+ = right)";
        playYaw_ -= glm::radians(deg);
        double pitch;
        if (parseNum(t, 3, pitch)) playPitch_ = std::clamp(glm::radians(pitch), -1.52, 1.52);
        return "View: heading " + fmtNum(glm::degrees(playYaw_), 1) + "°, pitch " + fmtNum(glm::degrees(playPitch_), 1) + "°";
    }
    if (sub == "rotate") {  // getragenes Teil drehen (wie Taste R / Shift+R)
        double deg, tilt = 0;
        if (!hand_.grabbed || !parseNum(t, 2, deg)) return "Error: player rotate <degrees> [tilt_degrees]   (only while carrying)";
        parseNum(t, 3, tilt);
        hand_.holdRel = glm::normalize(glm::angleAxis(glm::radians(tilt), glm::dvec3(1, 0, 0)) * glm::angleAxis(glm::radians(deg), glm::dvec3(0, 1, 0)) * hand_.holdRel);
        return "Carried part turned by " + fmtNum(deg, 1) + "°" + (tilt != 0 ? ", tilted by " + fmtNum(tilt, 1) + "°" : std::string());
    }
    if (sub == "walk" || sub == "run") {
        std::string dir = arg(t, 2);
        double sec = 1.0;
        parseNum(t, 3, sec);
        // auch schräg, wie zwei Tasten zugleich: forward-right, back-left ...
        glm::dvec2 move(0);
        std::string rest = dir;
        while (!rest.empty()) {
            const size_t dash = rest.find('-');
            const std::string part = rest.substr(0, dash);
            rest = dash == std::string::npos ? "" : rest.substr(dash + 1);
            if (part == "forward" || part == "f") move += glm::dvec2(1, 0);
            else if (part == "back" || part == "backward") move += glm::dvec2(-1, 0);
            else if (part == "left") move += glm::dvec2(0, -1);
            else if (part == "right") move += glm::dvec2(0, 1);
            else return "Error: player walk <forward|back|left|right|forward-right|...> <seconds> [run]";
        }
        if (glm::length(move) < 1e-9) return "Error: player walk: directions cancel out";
        autoMove_ = move;
        autoMoveTime_ = std::clamp(sec, 0.0, 120.0);
        autoRun_ = sub == "run" || arg(t, 4) == "run";
        return "Player walks " + dir + " for " + fmtNum(autoMoveTime_, 1) + " s" + (autoRun_ ? " (running)" : "");
    }
    if (sub == "stop") {
        autoMoveTime_ = 0;
        return "Player stops";
    }
    if (sub == "jump") {
        jumpRequest_ = true;
        return "Player jumps";
    }
    if (sub == "grab" || sub == "take") {
        std::string a = arg(t, 2);
        uint32_t want = 0;
        if (!a.empty()) {
            want = partArg(a);
            Object* o = world_->find(want);
            if (!o) return "Error: part " + a + " does not exist";
            lookAtPoint((o->wMin + o->wMax) * 0.5);
        }
        RayHit h;
        if (!world_->raycast(playerEye(), playerForward(), h, 0) || !h.obj) return "Error: there is nothing to grab";
        if (want && h.obj->id != want) return "Error: #" + std::to_string(want) + " is hidden behind #" + std::to_string(h.obj->id) + " " + h.obj->name;
        if (h.t > kReach) return "Error: too far away (" + fmtNum(h.t, 2) + " m, reach " + fmtNum(kReach, 1) + " m). Walk there first: player walk forward 1";
        if (h.obj->fixed) return "Error: " + h.obj->name + " is fixed";
        if (remote) {  // wie Taste E (erst loslassen, was schon in der Hand ist); der Gastgeber nimmt das Teil auf
            netHand_.ePresses += hand_.grabbed ? 2 : 1;
            return "Grabbing #" + std::to_string(h.obj->id) + " " + h.obj->name + " (" + fmtNum(h.obj->mass, 3) + " kg)";
        }
        grabObject(h.obj->id, h.t, h.localPoint);
        std::string s = "Now holding #" + std::to_string(h.obj->id) + " " + h.obj->name + " (" + fmtNum(h.obj->mass, 3) + " kg)";
        if (h.obj->mass * 9.81 > 420.0) s += " - too heavy to lift, it is only pushed/dragged";
        return s;
    }
    if (sub == "release" || sub == "drop") {
        Object* g = world_->find(hand_.grabbed);
        if (remote && g) netHand_.ePresses += 1;
        else hand_.grabbed = 0;
        return g ? "Released: " + g->name : "Player holds nothing";
    }
    if (sub == "throw") {
        Object* g = world_->find(hand_.grabbed);
        if (!g) return "Error: player holds nothing";
        double j = 0;
        parseNum(t, 2, j);
        std::string name = g->name;
        if (remote) netHand_.rmbPresses += 1;  // wie rechte Maustaste
        else throwHeld(j);
        return "Thrown: " + name;
    }
    return "Error: unknown. player [position|look|turn|walk|run|stop|jump|grab|release|throw|view]";
}

// ============================================================================ Teleportieren ("tp")

double App::standHeight(double x, double z, double fromY) {
    const double ground = world_->groundHeight(x, z);
    double y = ground;
    RayHit h;
    if (world_->raycast(glm::dvec3(x, std::max(fromY, ground + 0.5), z), glm::dvec3(0, -1, 0), h) && std::isfinite(h.point.y))
        y = std::max(y, h.point.y);
    return y;
}

std::string App::teleportTo(glm::dvec3 feet, const glm::dvec3& look, double camDist) {
    glm::dvec3 dir = look - feet;
    dir.y = 0;
    const bool hasDir = glm::length(dir) > 1e-6;
    if (!play_) {
        // Bau-Modus: die Kamera schaut von schräg oben hin. Ihr Ziel liegt 2,5 m voraus, so setzt Tab die Figur genau
        // auf feet (enterPlay)
        const glm::dvec3 f = hasDir ? glm::normalize(dir) : glm::normalize(glm::dvec3(-std::sin(orbitYaw_), 0, -std::cos(orbitYaw_)));
        orbitTarget_ = feet + f * 2.5 + glm::dvec3(0, 0.9, 0);
        orbitYaw_ = std::atan2(-f.x, -f.z);
        orbitPitch_ = std::clamp(orbitPitch_, 0.2, 0.9);
        orbitDist_ = camDist;
        return "camera";
    }
    hand_.grabbed = 0;  // Getragenes bleibt hier (die Hand zöge es sonst quer durch die Welt)
    autoMoveTime_ = 0;
    eyeLag_ = 0;
    if (netClient()) {  // die Figur steht beim Gastgeber: dort absetzen lassen (wie beim Wechsel in den Spiel-Modus)
        netSpawn_ = true;
        netSpawnFeet_ = netFeet_ = netFeetTarget_ = feet;
        netSpawnUntil_ = time_ + 2.0;
    } else {
        feet.y = std::max(feet.y, world_->prepareGround(feet) + 0.05);  // Gelände: Physik dort schon jetzt
        physics_.setCharacterPosition(feet);
        lastFeetY_ = feet.y;
    }
    if (hasDir) lookAtPoint(look);
    return "player";
}

std::string App::teleportCommand(const std::vector<std::string>& t) {
    Terrain* tr = world_->terrain();
    const glm::dvec3 here = !play_ ? orbitTarget_ : netClient() ? netFeet_ : physics_.characterPosition();
    const glm::dvec2 here2(here.x, here.z);
    const std::string w = arg(t, 1);
    std::string rest;  // alles nach "tp" bzw. "tp player" (Namen mit Leerzeichen)
    for (size_t i = (w == "player" || w == "biome") ? 2 : 1; i < t.size(); ++i) rest += (rest.empty() ? "" : " ") + normalizeWord(t[i]);
    auto cm = [](const glm::dvec3& p) { return fmtNum(p.x * 100, 0) + " " + fmtNum(p.y * 100, 0) + " " + fmtNum(p.z * 100, 0); };
    auto done = [&](const glm::dvec3& feet, const glm::dvec3& look, double camDist, const std::string& what) {
        const std::string who = teleportTo(feet, look, camDist);
        return std::string(who == "player" ? "Teleported to " : "Camera moved to ") + what + ": " + cm(feet) + " cm, " +
               fmtNum(glm::length(glm::dvec2(feet.x, feet.z) - here2), 0) + " m away" +
               (who == "camera" ? " (build mode: Tab puts the player there)" : "");
    };
    auto onTop = [&](double x, double z) { return glm::dvec3(x, standHeight(x, z, world_->groundHeight(x, z) + 100.0) + 0.05, z); };

    // Ziele: Biome (Name oder Bezeichnung), Mitspieler (Name, Anfang des Namens oder Nummer)
    auto biomeIndex = [&](const std::string& s) -> int {
        if (!tr || s.empty()) return -1;
        const auto& bs = tr->gen().config().biomes;
        for (size_t i = 0; i < bs.size(); ++i)
            if (normalizeWord(bs[i].id) == s || normalizeWord(bs[i].label) == s) return (int)i;
        return -1;
    };
    auto biomeList = [&]() {
        std::string s;
        if (tr)
            for (const auto& b : tr->gen().config().biomes) s += (s.empty() ? "" : ", ") + b.id + " (" + b.label + ")";
        return s.empty() ? std::string("none (this world is flat)") : s;
    };
    const uint32_t self = netClient() ? netSelfId_ : 1;
    auto others = [&]() {
        std::vector<const NetPlayer*> v;
        if (netRole_ != NetRole::Off)
            for (const NetPlayer& p : netPlayers_)
                if (p.id != self) v.push_back(&p);
        return v;
    };
    auto findPlayer = [&](const std::string& s) -> const NetPlayer* {
        if (s.empty()) return nullptr;
        const NetPlayer* prefix = nullptr;
        int prefixes = 0;
        for (const NetPlayer* p : others()) {
            const std::string n = normalizeWord(p->name);
            if (n == s || std::to_string(p->id) == s) return p;
            if (n.compare(0, s.size(), s) == 0) {
                prefix = p;
                ++prefixes;
            }
        }
        return prefixes == 1 ? prefix : nullptr;
    };
    const bool riverWord = w == "river" || w == "fluss";
    const bool waterWord = riverWord || w == "water" || w == "wasser" || w == "sea" || w == "meer" || w == "lake" || w == "see" ||
                           w == "pool" || w == "becken";

    if (w.empty() || w == "list" || w == "help") {
        int rivers = 0;
        bool terrainRivers = false;
        for (const WaterBody& b : world_->waters())
            if (b.generated()) terrainRivers = true;
            else if (b.river()) ++rivers;
        std::string riverText = terrainRivers ? "the rivers of the terrain" : "";
        if (rivers) riverText += (riverText.empty() ? "" : " + ") + std::to_string(rivers) + " water bod" + (rivers == 1 ? "y" : "ies") + " with a current";
        std::string players;
        for (const NetPlayer* p : others()) players += (players.empty() ? "" : ", ") + p->name + " (" + std::to_string(p->id) + ")";
        return "tp <target>   moves the player (build mode: the camera) to the nearest such spot, always onto the surface\n"
               "biomes        " + biomeList() + "\n"
               "river         " + (riverText.empty() ? std::string("none") : riverText) + "\n"
               "water         " + std::to_string(world_->waters().size()) + " body/bodies (pools, sea, rivers)\n"
               "players       " + (players.empty() ? std::string(netRole_ == NetRole::Off ? "none (not in a LAN game)" : "none") : players) + "\n"
               "also          tp spawn | tp <x> <z> | tp <x> <y> <z>   (cm) | tp biome <name> | tp player <name>";
    }

    // Koordinaten (cm): x z = oben auf der Oberfläche, x y z = genau dort (aber nie unter dem Gelände)
    double x, y, z;
    if (parseNum(t, 1, x) && parseNum(t, 2, y)) {
        glm::dvec3 feet;
        if (parseNum(t, 3, z)) {
            feet = glm::dvec3(x, y, z) / 100.0;
            feet.y = std::max(feet.y, world_->groundHeight(feet.x, feet.z) + 0.05);
        } else {
            feet = onTop(x / 100.0, y / 100.0);
        }
        return done(feet, feet, 8.0, "position");
    }
    if (w == "spawn" || w == "start") return done(onTop(0, 0), onTop(0, 0), 8.0, "spawn");

    // Baum: neben den nächsten (eine Art oder jeder), Blick auf den Stamm
    if (w == "tree" || w == "baum") {
        Forest* f = world_->forest();
        if (!f || !f->any()) return "Error: this world has no forest (flat world, or made before trees)";
        int type = -1;
        if (t.size() > 2) {
            const TreeLibrary& lib = f->library();
            for (int k = 0; k < lib.types(); ++k)
                if (normalizeWord(lib.config().types[(size_t)k].id) == arg(t, 2) || normalizeWord(lib.config().types[(size_t)k].label) == arg(t, 2)) type = k;
            if (type < 0) return "Error: tp tree [type]   ('trees' lists the types)";
        }
        TreeSpot s;
        if (!f->nearest(here2, 5000.0, type, s)) return "Error: no tree within 5 km";
        glm::dvec2 away = here2 - glm::dvec2(s.pos.x, s.pos.z);
        away = glm::length(away) > 1e-6 ? glm::normalize(away) : glm::dvec2(1, 0);
        const glm::dvec2 at = glm::dvec2(s.pos.x, s.pos.z) + away * (2.0 + f->library().config().types[(size_t)s.type].diameter * s.scale);
        const glm::dvec3 feet(at.x, world_->groundHeight(at.x, at.y) + 0.05, at.y);
        return done(feet, glm::dvec3(s.pos.x, feet.y + 1.0, s.pos.z), 6.0, "tree " + f->library().config().types[(size_t)s.type].id);
    }

    // Zeug am Boden: neben das nächste Stück dieser Art (Stein, Stock, Stamm, Stumpf), Blick darauf
    int scatterKind = -1;
    for (int k = 0; k < kScatterKinds; ++k)
        if (w == kScatterNames[k] || w + "s" == kScatterNames[k]) scatterKind = k;
    if (scatterKind >= 0) {
        ScatterField* f = world_->scatter();
        if (!f || !f->any()) return "Error: this world has nothing lying on the ground (flat world, or made before)";
        std::vector<ScatterSpot> spots;
        for (double r = 30.0; r <= 960.0; r *= 2.0) {
            f->around(here2, r, spots);
            spots.erase(std::remove_if(spots.begin(), spots.end(), [&](const ScatterSpot& s) { return s.kind != scatterKind || world_->scatterHidden(s.key); }),
                        spots.end());
            if (!spots.empty()) break;
        }
        if (spots.empty()) return std::string("Error: no ") + kScatterNames[scatterKind] + " within 1 km";
        const ScatterSpot* s = &spots[0];
        for (const ScatterSpot& c : spots)
            if (glm::length(glm::dvec2(c.pos.x, c.pos.z) - here2) < glm::length(glm::dvec2(s->pos.x, s->pos.z) - here2)) s = &c;
        glm::dvec2 away = here2 - glm::dvec2(s->pos.x, s->pos.z);
        away = glm::length(away) > 1e-6 ? glm::normalize(away) : glm::dvec2(1, 0);
        const glm::dvec2 at = glm::dvec2(s->pos.x, s->pos.z) + away * (1.5 + f->shape(s->shape).radius * s->size);
        const glm::dvec3 feet(at.x, world_->groundHeight(at.x, at.y) + 0.05, at.y);
        return done(feet, s->pos, 4.0, std::string(kScatterNames[scatterKind]).substr(0, std::string(kScatterNames[scatterKind]).size() - 1));
    }

    // Biom: die nächste Stelle, an der es ganz es selbst ist; man schaut tiefer hinein
    const int bArg = biomeIndex(w == "biome" ? rest : w);
    if (w == "biome" || (bArg >= 0 && !waterWord)) {
        if (!tr) return "Error: this world is flat (no terrain, no biomes)";
        if (bArg < 0) return "Error: tp <biome>   (biomes: " + biomeList() + ")";
        const GenConfig& cfg = tr->gen().config();
        const GenConfig::Biome& bio = cfg.biomes[(size_t)bArg];
        const std::string what = bio.id + " (" + bio.label + ")";
        glm::dvec2 f;
        if (!tr->findBiome(bArg, here2, 20000.0, f)) {
            const int single = tr->gen().singleBiome();
            return "Error: no " + bio.id + " within 20 km" +
                   (single >= 0 ? " (this world has only " + cfg.biomes[(size_t)single].id + ")" : std::string());
        }
        if (f == here2) return "Already in " + what + " here";
        const glm::dvec3 feet = onTop(f.x, f.y);
        const glm::dvec2 d = glm::normalize(f - here2);
        return done(feet, feet + glm::dvec3(d.x * 20.0, 1.65, d.y * 20.0), 25.0, what);
    }

    // Gewässer (river: nur mit Strömung): auf das Ufer an der nächsten Stelle, Blick aufs Wasser
    if (waterWord) {
        const WaterBody* best = nullptr;
        double bestD = 1e300;
        glm::dvec2 shore(0), into(0, 1);  // Uferlinie und Richtung ins Wasser
        for (const WaterBody& b : world_->waters()) {
            if (riverWord && !b.river()) continue;
            double d;
            glm::dvec2 s, n;
            if (b.shape) {  // Flüsse des Geländes: nächste Stelle mit Wasser, dann zurück bis zum Ufer
                glm::dvec2 f;
                if (!tr || !tr->findRiver(here2, 5000.0, f)) continue;
                if (f == here2) {
                    d = 0;
                    s = here2;
                    n = glm::dvec2(0, 1);
                } else {
                    glm::dvec2 a = here2, c = f;
                    for (int k = 0; k < 24; ++k) {
                        const glm::dvec2 m = (a + c) * 0.5;
                        (b.covers(m.x, m.y) ? c : a) = m;
                    }
                    s = c;
                    n = glm::normalize(f - here2);
                    d = glm::length(s - here2);
                }
            } else if (!b.sea) {
                const glm::dvec2 lo(b.lo.x, b.lo.z), hi(b.hi.x, b.hi.z);
                const glm::dvec2 q = glm::clamp(here2, lo, hi);
                d = glm::length(here2 - q);
                if (d < 1e-6) {  // schon über dem Wasser
                    d = 0;
                    s = here2;
                    n = glm::dvec2(0, 1);
                } else {
                    s = q;
                    n = (q - here2) / d;
                }
            } else if (b.covers(here2.x, here2.y)) {
                d = 0;
                s = here2;
                n = glm::dvec2(0, 1);
            } else {
                glm::dvec2 f;
                if (!nearestSpot(here2, 8.0, 3000.0, [&](const glm::dvec2& p) { return b.covers(p.x, p.y); }, f)) continue;
                glm::dvec2 a = here2, c = f;  // Uferlinie zwischen trocken (a) und nass (c)
                for (int k = 0; k < 24; ++k) {
                    const glm::dvec2 m = (a + c) * 0.5;
                    (b.covers(m.x, m.y) ? c : a) = m;
                }
                s = c;
                n = glm::normalize(f - here2);
                d = glm::length(s - here2);
            }
            if (d < bestD) {
                bestD = d;
                best = &b;
                shore = s;
                into = n;
            }
        }
        const std::string kind = riverWord ? "river" : "water";
        if (!best)
            return riverWord ? "Error: no river within 5 km (flat worlds, deserts and worlds made before rivers have none; 'scene river' builds a test river)"
                             : "Error: no water in this world ('water pool ...', 'water sea ...', 'scene water')";
        const std::string what = best->generated() ? std::string("river") : kind + " #" + std::to_string(best->id) + (best->sea ? " (sea)" : "");
        if (bestD < 1.0) return "Already at " + what + " here";
        const glm::dvec2 bank = shore - into * 0.7;
        const double top = std::max(world_->groundHeight(bank.x, bank.y), best->level()) + 30.0;
        const glm::dvec3 feet(bank.x, standHeight(bank.x, bank.y, top) + 0.05, bank.y);
        const glm::dvec3 look(shore.x + into.x * 2.0, best->level(), shore.y + into.y * 2.0);
        return done(feet, look, 6.0, what);
    }

    // Mitspieler: neben ihn (vor ihm, sonst rechts, links, dahinter), Blick auf ihn
    if (const NetPlayer* p = findPlayer(rest)) {
        const glm::dvec3 base = p->play ? p->feet : glm::dvec3(p->camEye.x, standHeight(p->camEye.x, p->camEye.z, p->camEye.y), p->camEye.z);
        const glm::dvec3 fwd(-std::sin(p->yaw), 0, -std::cos(p->yaw)), right(std::cos(p->yaw), 0, -std::sin(p->yaw));
        glm::dvec3 feet(0), first(0);
        bool ok = false;
        for (const glm::dvec3& off : {fwd * 1.5, right * 1.5, -right * 1.5, -fwd * 1.5, fwd * 3.0}) {
            const glm::dvec3 c = base + off;
            const double top = standHeight(c.x, c.z, base.y + 1.2);
            const glm::dvec3 at(c.x, top + 0.05, c.z);
            if (off == fwd * 1.5) first = at;
            RayHit h;  // Platz für die Figur: nicht zu hoch oder tief, darüber frei
            if (top > base.y + 0.6 || top < base.y - 2.0) continue;
            if (world_->raycast(at + glm::dvec3(0, 0.05, 0), glm::dvec3(0, 1, 0), h) && h.t < 1.8) continue;
            feet = at;
            ok = true;
            break;
        }
        if (!ok) feet = first;
        return done(feet, base + glm::dvec3(0, 1.6, 0), 5.0, "player " + p->name);
    }
    if (w == "player") return "Error: tp player <name|number>   (players: 'tp')";
    return "Error: tp <biome> | tp river | tp water | tp <player> | tp spawn | tp <x> <z> | tp <x> <y> <z>   (cm; 'tp' lists the targets)";
}

// ============================================================================ App-Befehle

bool App::appCommand(const std::string& cmd, const std::vector<std::string>& t, std::string& out) {
    if (cmd == "player") {
        out = playerCommand(t);
        return true;
    }
    if ((cmd == "trees" || cmd == "tree") && arg(t, 1) == "reload") {
        out = reloadTrees();
        return true;
    }
    if (cmd == "tp" || cmd == "teleport") {
        out = teleportCommand(t);
        return true;
    }
    if (cmd == "lan") {
        out = lanCommand(t);
        return true;
    }
    if (cmd == "world") {
        out = worldCommand(t);
        return true;
    }
    if (cmd == "terrain" && terrainAppCommand(t, out)) return true;  // borders, goto, reload (der Rest: Commands.cpp)
    if (cmd == "pause") {  // Pausenfenster des Spiel-Modus (wie ESC), siehe Hilfe
        const std::string v = t.size() > 1 ? t[1] : "";
        if (!play_) {
            out = "Error: the pause window belongs to play mode ('mode play')";
            return true;
        }
        PauseUi* ui = opt_.pauseUi;
        if (v == "settings") {
            setPause(Pause::Menu);
            if (ui) ui->showPage("settings");
            else showSettings_ = true;
        } else if (v == "quit") {
            setPause(Pause::Quit);
        } else if (v == "on" || v == "off" || v.empty()) {
            setPause(v == "on" ? Pause::Menu : v == "off" ? Pause::None : (pause_ == Pause::None ? Pause::Menu : Pause::None));
        } else if (v == "set" && ui) {  // Einstellung wie im Hauptmenü ("menu set ..."), gilt sofort
            if (pause_ == Pause::None) setPause(Pause::Menu);
            std::string line = "menu";  // "pause set graphics ultra" -> "menu set graphics ultra"
            for (size_t i = 1; i < t.size(); ++i) line += " \"" + t[i] + "\"";
            out = ui->command(line);
            if (ui->takeSettingsChanged()) reloadSettings();
            return true;
        } else if (v == "shot" && t.size() > 2 && ui) {  // Bild des Pausenfensters
            if (pause_ == Pause::None) setPause(Pause::Menu);
            out = ui->grab(t[2]) ? "pause window saved: " + t[2] : "Error: could not save " + t[2];
            return true;
        } else {
            out = ui ? "Error: pause [on|off|settings|quit] | pause set <key> <value> | pause shot <file.png>" : "Error: pause [on|off|settings|quit]";
            return true;
        }
        if (pause_ == Pause::None) out = "pause window closed, playing";
        else out = "pause window open, page " + (ui ? ui->page() : showSettings_ ? std::string("settings") : pause_ == Pause::Quit ? "quit" : "pause") +
                   (pauseStopped_ ? ", world paused" : "");
        return true;
    }
    if (cmd == "inventory") {  // Inventar des Spiel-Modus (E), siehe docs/21
        const std::string v = t.size() > 1 ? t[1] : "";
        if (!play_) {
            out = "Error: the inventory belongs to play mode ('mode play')";
            return true;
        }
        if (v == "open" || v == "close") {
            invOpen(v == "open");
        } else if (v == "store" && t.size() > 2) {
            const uint32_t id = (uint32_t)std::strtoul(t[2].c_str(), nullptr, 10);
            if (!invStore(id, true)) {
                out = "Error: part " + t[2] + " not stored (unknown, fixed, too heavy or no room)";
                return true;
            }
        } else if (v == "take" && t.size() > 2) {
            const uint32_t key = (uint32_t)std::strtoul(t[2].c_str(), nullptr, 10);
            if (!invTakeOut(key)) {
                out = "Error: no item " + t[2] + " in the inventory";
                return true;
            }
        } else if (v == "shot" && t.size() > 2 && opt_.inventoryUi) {
            invOpen(true);
            out = opt_.inventoryUi->grab(t[2]) ? "inventory window saved: " + t[2] : "Error: could not save " + t[2];
            return true;
        } else if (!v.empty() && v != "list") {
            out = "Error: inventory [list|open|close] | inventory store <part id> | inventory take <item> | inventory shot <file.png>";
            return true;
        }
        out = "inventory " + std::string(invOpen_ ? "open" : "closed") + ", " + fmtNum(invMass(), 2) + " kg";
        for (const InvItem& it : inv_)
            out += "\n  item " + std::to_string(it.key) + ": " + it.state.name + ", " + fmtNum(it.mass, 2) + " kg, cells " +
                   std::to_string(it.x) + "," + std::to_string(it.y) + " " + std::to_string(it.w) + "x" + std::to_string(it.h);
        return true;
    }
    if (cmd == "craft" || cmd == "herstellen") {  // Herstellen (C), siehe docs/22; setzen: plan place craft:<rezept> ...
        const std::string v = t.size() > 1 ? t[1] : "";
        if (v.empty() || v == "list") {
            out = "recipes (place with 'plan place craft:<id> <x y z>', or C in play mode):";
            for (const PlanTemplate& r : craftRecipes()) out += "\n  " + r.id + "  " + r.label + ": " + craftNeeds(r);
        } else if (v == "fill" && t.size() > 2) {
            const uint32_t id = (uint32_t)std::strtoul(t[2].c_str(), nullptr, 10);
            const size_t before = inv_.size();
            if (!play_ || !craftFill(id)) {
                out = play_ ? "Error: plan " + t[2] + " is not a recipe hologram" : "Error: crafting from the inventory belongs to play mode";
                return true;
            }
            const BuildPlan* p = world_->findPlan(id);
            int full = 0;
            if (p)
                for (const PlanPiece& c : p->pieces) full += p->pieceDone(c) ? 1 : 0;
            out = p ? "plan " + t[2] + ": " + std::to_string(full) + "/" + std::to_string(p->pieces.size()) + " pieces full, inventory " +
                          std::to_string(before) + " -> " + std::to_string(inv_.size()) + " items"
                    : "plan " + t[2] + " is done, the result is in the inventory (or next to where the hologram was)";
        } else {
            out = "Error: craft [list] | craft fill <plan>";
        }
        return true;
    }
    if (cmd == "menu") {
        out = menuCommandInGame(t);
        return true;
    }
    if (cmd == "tool") {
        std::string w = arg(t, 1);
        if (w == "heat" || w == "h") {  // Wärme-Werkzeug: im Spiel-Modus mit F, darum bleibt der Modus
            if (t.size() > 2) {
                const int k = findHeatTool(arg(t, 2).c_str());
                if (k < 0) {
                    std::string names;
                    for (const auto& h : heatTools()) names += std::string(names.empty() ? "" : " ") + h.name;
                    out = "Error: tool heat [" + names + "]";
                    return true;
                }
                heatTool_ = k;
            }
            if (!play_) tool_ = Tool::Heat;
            const HeatTool& h = heatTools()[heatTool_];
            out = std::string("Tool: heat (") + h.name + ", " + fmtNum(h.tempC, 0) + " °C) - " +
                  (play_ ? "hold F and look at a part" : "hold the left mouse button on a part");
            return true;
        }
        if (play_) leavePlay();
        double v;
        if (w == "select" || w == "move" || w == "v") {
            tool_ = Tool::Select;
        } else if (w == "saw" || w == "s") {
            tool_ = Tool::Saw;
            std::string ax = arg(t, 2);
            if (ax == "x") sawAxis_ = 0;
            if (ax == "y") sawAxis_ = 1;
            if (ax == "z") sawAxis_ = 2;
            if (parseNum(t, 3, v)) sawAngle_ = (float)std::clamp(v, -60.0, 60.0);
            if (parseNum(t, 4, v)) sawKerfMm_ = (float)std::clamp(v, 0.0, 20.0);
        } else if (w == "drill" || w == "b") {
            tool_ = Tool::Drill;
            if (parseNum(t, 2, v)) drillDiaMm_ = (float)std::clamp(v, 0.5, 500.0);
            if (parseNum(t, 3, v)) drillDepthMm_ = (float)std::clamp(v, 0.0, 5000.0);
        } else if (w == "round" || w == "chamfer" || w == "r") {
            tool_ = Tool::Round;
            if (parseNum(t, 2, v)) roundMm_ = (float)std::clamp(v, 0.1, 500.0);
            roundChamfer_ = w == "chamfer" || arg(t, 3) == "chamfer";
        } else if (w == "mill" || w == "m") {
            tool_ = Tool::Mill;
            if (parseNum(t, 2, v)) millDims_[0] = (float)std::clamp(v, 0.5, 2000.0);
            if (parseNum(t, 3, v)) millDims_[1] = (float)std::clamp(v, 0.5, 2000.0);
            if (parseNum(t, 4, v)) millDims_[2] = (float)std::clamp(v, 0.1, 2000.0);
        } else if (w == "plane" || w == "p") {
            tool_ = Tool::Plane;
            if (parseNum(t, 2, v)) planeMm_ = (float)std::clamp(v, 0.05, 500.0);
        } else {
            out = "Error: tool select | saw [x|y|z] [angle] [kerf_mm] | drill [D_mm] [depth_mm] | round [r_mm] [chamfer] | mill [L W depth] | plane [mm]"
                  " | heat [lighter|gas|welding|spray|nitrogen]";
            return true;
        }
        out = std::string("Tool: ") + toolName(tool_);
        if (tool_ == Tool::Saw)
            out += std::string(" (axis ") + "XYZ"[sawAxis_] + ", miter " + fmtNum(sawAngle_, 1) + "°, kerf " + fmtNum(sawKerfMm_, 1) + " mm)";
        if (tool_ == Tool::Drill)
            out += " (D " + fmtNum(drillDiaMm_, 1) + " mm, depth " + (drillDepthMm_ > 0 ? fmtNum(drillDepthMm_, 1) + " mm" : std::string("through")) + ")";
        if (tool_ == Tool::Round) out += std::string(roundChamfer_ ? " (chamfer " : " (radius ") + fmtNum(roundMm_, 1) + " mm)";
        if (tool_ == Tool::Mill)
            out += " (pocket " + fmtNum(millDims_[0], 1) + " x " + fmtNum(millDims_[1], 1) + " mm, " + fmtNum(millDims_[2], 1) + " mm deep)";
        if (tool_ == Tool::Plane) out += " (" + fmtNum(planeMm_, 2) + " mm per pass)";
        return true;
    }
    if (cmd == "log") {  // letzte Meldungen der Konsole (z.B. Brüche), auch per Fernsteuerung lesbar
        double n = 20;
        parseNum(t, 1, n);
        const size_t count = std::min(log_.size(), (size_t)std::clamp(n, 1.0, 500.0));
        for (size_t i = log_.size() - count; i < log_.size(); ++i) out += log_[i].text + "\n";
        if (out.empty()) out = "log is empty";
        else out.pop_back();
        return true;
    }
    if (cmd == "window") {
        std::string w = arg(t, 1);
        bool* flag = nullptr;
        if (w == "inspector" || w == "physics") flag = &showInspector_;
        else if (w == "materials") flag = &showMaterials_;
        else if (w == "drawing") flag = &showDrawing_;
        else if (w == "console") flag = &showConsole_;
        else if (w == "help" || w == "controls") flag = &showHelp_;
        if (!flag) {
            out = "Error: window inspector|drawing|console|help|materials [on|off]";
            return true;
        }
        *flag = onOff(t, 2, *flag);
        if (flag == &showDrawing_ && showDrawing_ && !selection_.empty()) showDrawing(selection_);
        if (flag == &showInspector_ && showInspector_ && selectedId()) inspectTarget_ = selectedId();
        out = "window " + w + (*flag ? " on" : " off");
        return true;
    }
    if (cmd == "display") {
        std::string w = arg(t, 1);
        bool* flag = w == "shadows" ? &showShadows_ : w == "edges" ? &showEdges_ : w == "grid" ? &showGrid_ : nullptr;
        if (!flag) {
            out = "Error: display shadows|edges|grid [on|off]";
            return true;
        }
        *flag = onOff(t, 2, *flag);
        out = w + (*flag ? " on" : " off");
        return true;
    }
    return false;
}

std::string App::statusText() {
    std::string s;
    s += std::string("mode          ") + (play_ ? "play" : "build") + "\n";
    s += std::string("view          ") + (play_ ? "first person" : viewName(viewMode_)) + "\n";
    s += std::string("tool          ") + toolName(tool_) + "\n";
    glm::dvec3 c = orbitTarget_ * 100.0;
    s += "camera        target " + fmtNum(c.x) + " " + fmtNum(c.y) + " " + fmtNum(c.z) + " cm, distance " + fmtNum(orbitDist_, 2) + " m\n";
    if (physics_.hasCharacter()) {
        glm::dvec3 p = physics_.characterPosition() * 100.0;
        Object* g = world_->find(hand_.grabbed);
        s += "player        " + fmtNum(p.x) + " " + fmtNum(p.y) + " " + fmtNum(p.z) + " cm" + (g ? ", holds #" + std::to_string(g->id) : std::string("")) + "\n";
    }
    s += std::string("windows       inspector ") + (showInspector_ ? "on" : "off") + ", drawing " + (showDrawing_ ? "on" : "off") +
         ", console " + (showConsole_ ? "on" : "off") + "\n";
    s += "commands      " + std::to_string(jobs_.size()) + " running\n";
    s += "fps           " + fmtNum(fps_, 0) + "\n";
    const PhysicsStats& ps = physics_.stats();
    s += "physics       " + fmtNum(ps.frameMs, 2) + " ms/frame, " + std::to_string(ps.steps) + " steps (240/s), " +
         std::to_string(ps.contacts) + " contacts, " + std::to_string(ps.activeBodies) + " active, max penetration " +
         fmtNum(std::max(0.0f, ps.maxPenetration) * 1000.0, 1) + " mm\n";
    s += "remote        " + std::string(remote_->running() ? "active, port " + std::to_string(opt_.port) : "off") + "\n";
    if (netRole_ == NetRole::Host) s += "lan           host, " + std::to_string(peers_.size()) + " co-player(s) connected\n";
    else if (netClient()) s += "lan           co-player, connected to " + netHostName_ + "\n";
    return s;
}

bool App::queryValue(const std::string& n, double& num, std::string& str) {
    str.clear();
    if (n == "time") num = time_;
    else if (n == "fps") num = fps_;
    else if (n == "mode") str = play_ ? "play" : "build";
    else if (n == "view") str = play_ ? "firstperson" : viewName(viewMode_);
    else if (n == "tool") str = toolName(tool_);
    else if (n.rfind("camera.", 0) == 0) {
        std::string p = n.substr(7);
        if (p == "x") num = orbitTarget_.x * 100;
        else if (p == "y") num = orbitTarget_.y * 100;
        else if (p == "z") num = orbitTarget_.z * 100;
        else if (p == "distance") num = orbitDist_;
        else return false;
    } else if (n.rfind("player.", 0) == 0) {
        if (!physics_.hasCharacter()) return false;
        std::string p = n.substr(7);
        glm::dvec3 pos = physics_.characterPosition();
        if (p == "x") num = pos.x * 100;
        else if (p == "y") num = pos.y * 100;
        else if (p == "z") num = pos.z * 100;
        else if (p == "onground") num = physics_.characterOnGround() ? 1 : 0;
        else if (p == "holding") num = hand_.grabbed;
        else if (p == "heading") num = glm::degrees(playYaw_);
        else if (p == "pitch") num = glm::degrees(playPitch_);
        else if (p == "lookingat") {
            RayHit h;
            num = (world_->raycast(playerEye(), playerForward(), h, hand_.grabbed) && h.obj && h.t < 10) ? h.obj->id : 0;
        } else {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

std::string App::wait(double) { return "wait works in command sequences, e.g.: mode play ; wait 2 ; print 17.y"; }
