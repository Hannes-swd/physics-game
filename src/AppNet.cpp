// LAN-Mehrspieler: Der Gastgeber rechnet alles (Physik, Werkzeuge, Befehle). Mitspieler bekommen seine Welt
// (Teile, Lagen, Verbindungen, Spielfiguren), zeigen sie an und schicken nur ihre Eingaben und Befehle.
#include "App.h"

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <unordered_set>

#include "Materials.h"
#include "Terrain.h"
#include "ObjectIO.h"
#include "Trees.h"
#include "WorldGen.h"

using net::Msg;

namespace {

constexpr double kSyncRate = 60.0;        // Lagen und Spielfiguren je Sekunde an die Mitspieler
constexpr double kLoadRate = 10.0;        // Auslastung der Verbindungen je Sekunde
constexpr size_t kMaxBacklog = 8u << 20;  // mehr ungesendete Bytes: Mitspieler kommt nicht hinterher, Runde auslassen
constexpr double kOwnMoveHold = 0.3;      // so lange gilt beim Mitspieler die eigene Bewegung eines Teils (s)


std::string firstWord(const std::string& line) {
    size_t a = line.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    size_t b = line.find_first_of(" \t;", a);
    return normalizeWord(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
}

glm::dvec3 forwardOf(double yaw, double pitch) {
    return glm::dvec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch));
}

// Blickwinkel zu einer Richtung (wie playerForward)
void anglesOf(const glm::dvec3& dir, double& yaw, double& pitch) {
    glm::dvec3 d = glm::length(dir) > 1e-9 ? glm::normalize(dir) : glm::dvec3(0, 0, -1);
    yaw = std::atan2(-d.x, -d.z);
    pitch = std::asin(std::clamp(d.y, -1.0, 1.0));
}

// ---------------------------------------------------------------- Teile und Verbindungen übertragen



// Eigenschaften, die sich ohne neue Geometrie ändern (Name, Material, Lagerung ...)
void writeMeta(net::Writer& w, const Object& o) {
    w.u32(o.id);
    w.str(o.name);
    w.u32((uint32_t)o.material);
    w.u8(o.fixed);
    w.u32((uint32_t)(o.hingeAxis + 1));
    w.f32(o.motorRpm);
    w.f32(o.motorTorque);
    w.u8(o.unbreakable);
    w.u32((uint32_t)o.lubricant);
}

size_t metaHash(const Object& o) {
    net::Writer w;
    writeMeta(w, o);
    return std::hash<std::string>()(w.data());
}


void writePlayer(net::Writer& w, const NetPlayer& p) {
    w.u32(p.id);
    w.str(p.name);
    w.u8(p.play);
    w.dvec3(p.feet);
    w.dvec3(p.camEye);
    w.f64(p.yaw);
    w.f64(p.pitch);
    w.u32(p.grabbed);
    w.u8(p.tooHeavy);
    w.u8(p.onGround);
}

NetPlayer readPlayer(net::Reader& r) {
    NetPlayer p;
    p.id = r.u32();
    p.name = r.str();
    p.play = r.u8() != 0;
    p.feet = r.dvec3();
    p.camEye = r.dvec3();
    p.yaw = r.f64();
    p.pitch = r.f64();
    p.grabbed = r.u32();
    p.tooHeavy = r.u8() != 0;
    p.onGround = r.u8() != 0;
    return p;
}

// Befehle eines Mitspielers: laufen beim Gastgeber, aber mit Auswahl und Kamera des Mitspielers.
// Dateien und die Spielfigur des Gastgebers bleiben tabu.
class PeerCommands : public CommandHost {
public:
    PeerCommands(App& app, Peer& peer) : app_(app), peer_(peer) {}
    World& world() override { return app_.world(); }
    glm::dvec3 spawnReference() override { return peer_.spawnRef; }
    uint32_t selectedId() const override { return peer_.selection.empty() ? 0 : peer_.selection.back(); }
    std::vector<uint32_t> selectedIds() const override { return peer_.selection; }
    void select(uint32_t id) override {
        peer_.selection.clear();
        if (id) peer_.selection.push_back(id);
        peer_.selectionChanged = true;
    }
    void selectMany(const std::vector<uint32_t>& ids) override {
        peer_.selection = ids;
        peer_.selectionChanged = true;
    }
    bool hasWindow() const override { return true; }
    std::string setMode(bool) override { return "Error: co-players switch the mode on their own computer (Tab)"; }
    bool appCommand(const std::string& cmd, const std::vector<std::string>& t, std::string& out) override {
        if (cmd == "save" || cmd == "load" || cmd == "import" || cmd == "quit" || cmd == "exit" || cmd == "screenshot" || cmd == "photo") {
            out = "Error: '" + cmd + "' is not allowed for co-players (it would use the host's computer)";
            return true;
        }
        if (cmd == "drawing" || cmd == "threeview")
            for (const std::string& a : t)
                if (a.size() > 4 && normalizeWord(a.substr(a.size() - 4)) == ".svg") {
                    out = "Error: co-players save drawings on their own computer (drawing without ';')";
                    return true;
                }
        if (cmd == "player" || cmd == "tp" || cmd == "teleport" || cmd == "tool" || cmd == "window" || cmd == "display" || cmd == "log" ||
            cmd == "lan") {
            out = "Error: '" + cmd + "' only works on your own computer (not inside a command sequence)";
            return true;
        }
        if ((cmd == "inspect" || cmd == "physics") && t.size() > 1) {  // Messwerte lesen, ohne den Inspektor des Gastgebers umzustellen
            std::string n = normalizeWord(t[1]);
            if (!n.empty() && n[0] == '#') n.erase(0, 1);
            const uint32_t id = n == "selected" || n == "sel" ? selectedId() : (uint32_t)std::strtoul(n.c_str(), nullptr, 10);
            out = app_.world().find(id) ? app_.world().inspectText(id) : "Error: inspect <id>";
            return true;
        }
        return false;
    }
    std::string statusText() override { return app_.statusText(); }
    bool queryValue(const std::string& n, double& num, std::string& str) override { return app_.queryValue(n, num, str); }
    std::string wait(double s) override { return app_.wait(s); }

private:
    App& app_;
    Peer& peer_;
};

} // namespace

glm::vec3 playerColor(uint32_t id) {
    static const glm::vec3 c[] = {{0.85f, 0.30f, 0.22f}, {0.22f, 0.50f, 0.92f}, {0.28f, 0.75f, 0.32f},
                                  {0.92f, 0.70f, 0.18f}, {0.65f, 0.38f, 0.88f}, {0.18f, 0.75f, 0.78f}};
    return c[id % 6];
}

// ============================================================================ Allgemein

void App::netUpdate(float dt) {
    if (netRole_ == NetRole::Host) {
        netHostUpdate(dt);
    } else if (netRole_ == NetRole::Client) {
        netClientUpdate(dt);
    } else if (showNet_) {
        netSearch_.update(time_);  // Fenster offen: Spiele im LAN suchen
    } else {
        netSearch_.stop();
    }
}

void App::netStop(const std::string& why) {
    if (netRole_ == NetRole::Host) {
        for (auto& p : peers_) {
            net::Writer w;
            w.str("The host closed the LAN game");
            p->conn->send(Msg::Reject, w);
            p->conn->update();
            physics_.destroyCharacter((int)p->id);
        }
        peers_.clear();
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.peer != 0; }), jobs_.end());
        netListener_.stop();
        netBeacon_.stop();
        netRole_ = NetRole::Off;
        if (!play_) world_->setSimulating(false);
        log(why.empty() ? "LAN game closed" : why, 3);
    } else if (netRole_ == NetRole::Client) {
        if (play_) leavePlay();  // noch als Mitspieler: Kamera bleibt, wo die Figur zuletzt stand
        netConn_.reset();
        netRole_ = NetRole::Off;
        hand_ = Hand();
        for (auto& [req, q] : netRequests_) {  // offene Anfragen beantworten
            if (q.source == 2) remote_->respond(q.client, "Error: connection to the host closed");
            if (q.source == 1) scriptBusy_ = false;
        }
        netRequests_.clear();
        log(why.empty() ? "Left the LAN game. The world stays here as your own copy." : why, why.empty() ? 3 : 2);
        if (!why.empty()) toast(why, false);
    }
    netStatus_ = why;
    netWelcomed_ = false;
    netWorldReady_ = false;
    netPlayers_.clear();
    netSelfId_ = 0;
    netOwnMoves_.clear();
}

void App::netEvent(const std::string& text, bool ok) {
    if (netRole_ != NetRole::Host) return;
    net::Writer w;
    w.str(text);
    w.u8(ok);
    for (auto& p : peers_)
        if (p->accepted && p->closeAt < 0) p->conn->send(Msg::Event, w);
}

bool App::peersPlaying() const {
    for (const auto& p : peers_)
        if (p->accepted && p->play) return true;
    return false;
}

Peer* App::findPeer(uint32_t id) {
    for (auto& p : peers_)
        if (p->id == id) return p.get();
    return nullptr;
}

// ============================================================================ Gastgeber

bool App::netHost(std::string* err) {
    if (netRole_ != NetRole::Off) {
        if (err) *err = "already in a LAN game";
        return false;
    }
    if (!netListener_.start(net::kGamePort)) {
        if (err) *err = "port " + std::to_string(net::kGamePort) + " is in use (is another Werkstatt already hosting on this computer?)";
        return false;
    }
    if (!netBeacon_.start()) log("LAN search not available (UDP port " + std::to_string(net::kDiscoveryPort) + " in use) - co-players can still join by address", 2);
    netSearch_.stop();
    netRole_ = NetRole::Host;
    netSelfId_ = 1;
    netAddresses_ = net::localAddresses();
    netSyncAccum_ = netLoadsAccum_ = 0;
    netStatus_.clear();
    std::string where;
    for (const std::string& a : netAddresses_) where += (where.empty() ? "" : ", ") + a;
    log("LAN game open. Co-players find it under Multiplayer > Join" + (where.empty() ? std::string() : " or connect to " + where), 3);
    toast("LAN game open - co-players can join now");
    return true;
}

void App::netHostUpdate(float dt) {
    while (std::unique_ptr<net::Connection> c = netListener_.accept()) {
        auto p = std::make_unique<Peer>();
        p->id = netNextPeer_++;
        p->conn = std::move(c);
        p->since = time_;
        p->commands = std::make_unique<PeerCommands>(*this, *p);
        peers_.push_back(std::move(p));
    }
    int players = 1;
    for (const auto& p : peers_) players += p->accepted;
    netBeacon_.answer(netName_, net::kGamePort, players);

    for (size_t i = 0; i < peers_.size();) {
        Peer& p = *peers_[i];
        const bool alive = p.conn->update();
        net::Msg type;
        std::string payload;
        while (p.closeAt < 0 && p.conn->next(type, payload)) netPeerMessage(p, type, payload);
        const bool closing = p.closeAt >= 0 && (time_ >= p.closeAt || p.conn->pending() == 0);
        const bool silent = !p.greeted && time_ - p.since > 10.0;  // stellt sich nicht vor: kein Werkstatt-Spiel
        if (!alive || closing || silent) {
            netRemovePeer(i, p.accepted && p.closeAt < 0 ? p.name + " left the game" : "");
            continue;
        }
        ++i;
    }

    // Physik läuft, solange jemand spielt (Gastgeber oder ein Mitspieler)
    const bool sim = play_ || peersPlaying();
    if (sim != world_->simulating()) world_->setSimulating(sim);

    netPlayerList();
    netSyncAccum_ += dt;
    netLoadsAccum_ += dt;
    if (netSyncAccum_ >= 1.0 / kSyncRate) {
        netSyncAccum_ = std::fmod(netSyncAccum_, 1.0 / kSyncRate);
        for (auto& p : peers_)
            if (p->accepted && p->closeAt < 0) netSync(*p);
    }
    if (netLoadsAccum_ >= 1.0 / kLoadRate) {  // Auslastung der Verbindungen (Balken, Farben)
        netLoadsAccum_ = 0;
        net::Writer w;
        w.u32((uint32_t)world_->joints().size());
        for (const auto& j : world_->joints()) {
            w.u32(j->id);
            w.f32((float)(std::round(j->load * 200.0) / 200.0));
        }
        for (auto& p : peers_)
            if (p->accepted && p->closeAt < 0 && w.data() != p->sentLoads) {
                p->conn->send(Msg::JointLoads, w);
                p->sentLoads = w.data();
            }
    }
    for (auto& p : peers_) p->conn->update();  // gleich losschicken
}

void App::netPeerMessage(Peer& p, net::Msg type, const std::string& payload) {
    net::Reader r(payload);
    if (type == Msg::Hello) {
        if (p.greeted) return;
        const uint32_t protocol = r.u32();
        std::string name;
        for (char c : r.str().substr(0, 32))
            if ((unsigned char)c >= 32) name += c;
        p.name = name.empty() ? "Player " + std::to_string(p.id) : name;
        p.greeted = true;
        if (protocol != net::kProtocol) {
            netReject(p, "Different game version - please use the same Werkstatt version as the host");
            return;
        }
        if (netConfirm_) {
            p.conn->send(Msg::Waiting);
            log(p.name + " (" + p.conn->address() + ") wants to join - accept or decline at the top of the screen", 3);
            toast(p.name + " möchte beitreten");
        } else {
            netAccept(p);
        }
        return;
    }
    if (!p.accepted) return;  // erst nach der Zulassung

    if (type == Msg::Command) {
        const uint32_t req = r.u32();
        const std::string line = r.str();
        const bool quiet = r.u8() != 0;
        std::vector<uint32_t> sel(readCount(r, 4));
        for (uint32_t& id : sel) id = r.u32();
        const glm::dvec3 spawnRef = r.dvec3();
        if (!r.ok()) return;
        p.selection = sel;
        p.selectionChanged = false;
        p.spawnRef = spawnRef;
        if (!quiet) log("[" + p.name + "] > " + line, 1);
        std::string err;
        auto job = parseScript(line, &err);
        if (!job) {
            ScriptJob failed;
            failed.ok = false;
            failed.output = "Error: " + err;
            netSendResult(p, req, failed);
            return;
        }
        Job j;
        j.job = std::move(job);
        j.source = 3;
        j.line = line;
        j.peer = p.id;
        j.request = req;
        jobs_.push_back(std::move(j));
        stepJobs();  // sofort ausführen, soweit kein Warten nötig ist
    } else if (type == Msg::Transforms) {
        const bool undo = r.u8() != 0;
        const uint32_t n = readCount(r, 60);
        if (undo) world_->pushUndo();
        for (uint32_t i = 0; i < n && r.ok(); ++i) {
            const uint32_t id = r.u32();
            const glm::dvec3 pos = r.dvec3();
            const glm::dquat rot = r.dquat();
            Object* o = world_->find(id);
            if (!o || !r.ok() || glm::length(rot) < 0.5) continue;
            world_->setTransform(*o, pos, rot);
            if (world_->simulating() && o->body != kNoBody && !o->fixed)  // vom Mitspieler gehalten: nicht fallen lassen
                physics_.setVelocity(o->body, glm::vec3(0), glm::vec3(0));
        }
    } else if (type == Msg::Input) {
        const bool play = r.u8() != 0;
        const glm::dvec3 camEye = r.dvec3(), spawnRef = r.dvec3();
        const double yaw = r.f64(), pitch = r.f64();
        glm::vec3 wish = r.vec3();
        const bool jump = r.u8() != 0;
        HandInput in;
        in.lmbDown = r.u8() != 0;
        in.lmbPresses = (int)std::min(r.u32(), 8u);
        in.rmbPresses = (int)std::min(r.u32(), 8u);
        in.ePresses = (int)std::min(r.u32(), 8u);
        in.turns = (int)std::min(r.u32(), 24u);
        in.tilts = (int)std::min(r.u32(), 24u);
        in.scroll = std::clamp((double)r.f32(), -50.0, 50.0);
        const bool spawn = r.u8() != 0;
        const glm::dvec3 feet = r.dvec3();
        if (!r.ok()) return;
        p.camEye = camEye;
        p.spawnRef = spawnRef;
        if (play && (spawn || !physics_.hasCharacter((int)p.id))) {  // Figur absetzen, immer auf der Oberfläche
            glm::dvec3 at = spawn ? feet : p.spawnRef + glm::dvec3(0, 0.05, 0);
            at.y = std::max(at.y, world_->prepareGround(at) + 0.05);
            if (spawn) p.hand.grabbed = 0;  // tp: Getragenes bleibt zurück
            if (physics_.hasCharacter((int)p.id)) physics_.setCharacterPosition(at, (int)p.id);
            else physics_.createCharacter(at, (int)p.id);
        }
        if (!play && p.play) {  // zurück in den Bau-Modus: Figur verschwindet
            p.hand.grabbed = 0;
            physics_.destroyCharacter((int)p.id);
        }
        p.play = play;
        p.yaw = yaw;
        p.pitch = std::clamp(pitch, -1.52, 1.52);
        if (glm::length(wish) > 7.5f) wish = glm::normalize(wish) * 7.5f;
        p.wish = play ? wish : glm::vec3(0);
        p.jump = p.jump || (play && jump);
        if (play) handActions(p.hand, peerEye(p), forwardOf(p.yaw, p.pitch), p.yaw, in);
    }
}

void App::netAccept(Peer& p) {
    if (p.accepted || p.closeAt >= 0) return;
    p.accepted = true;
    net::Writer w;
    w.u32(p.id);
    w.str(netName_);
    p.conn->send(Msg::Welcome, w);
    p.sent.clear();
    p.sentJoints.clear();
    p.sentLoads.clear();
    p.terrainSent = false;
    netSync(p);  // gleich die ganze Welt
    const std::string msg = p.name + " joined the game";
    log(msg, 3);
    toast(msg);
    netEvent(msg, true);
}

void App::netReject(Peer& p, const std::string& reason) {
    net::Writer w;
    w.str(reason);
    p.conn->send(Msg::Reject, w);
    p.closeAt = time_ + 1.0;  // noch kurz senden lassen
    if (p.accepted) {
        const std::string msg = p.name + " was removed from the game";
        log(msg, 3);
        netEvent(msg, true);
    }
    p.accepted = false;
    p.play = false;
    p.hand.grabbed = 0;
    physics_.destroyCharacter((int)p.id);
}

void App::netRemovePeer(size_t index, const std::string& why) {
    Peer& p = *peers_[index];
    physics_.destroyCharacter((int)p.id);
    const uint32_t id = p.id;
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.peer == id; }), jobs_.end());
    peers_.erase(peers_.begin() + index);
    if (!why.empty()) {
        log(why, 3);
        toast(why);
        netEvent(why, true);
    }
}

// Nur Änderungen seit dem letzten Mal: neue/geänderte Teile komplett, sonst Eigenschaften und Lagen
void App::netSync(Peer& p) {
    if (p.conn->pending() > kMaxBacklog) return;  // Mitspieler kommt nicht hinterher: später weiter
    // Gelände zuerst: nur Seed, Biome und Änderungen. Der Mitspieler erzeugt es daraus selbst, genau wie hier
    {
        const Terrain* tr = world_->terrain();
        const void* gen = tr ? tr->genPtr().get() : nullptr;
        const uint64_t rev = tr ? tr->editRevision() : 0;
        const TreeLibrary& lib = world_->trees();
        const size_t taken = world_->treesTaken().size();
        if (!p.terrainSent || gen != p.sentTerrainGen || rev != p.sentTerrainRev || &lib != p.sentTreeLib || taken != p.sentTaken) {
            net::Writer w;
            // Bäume: die Arten (trees.json) und welche Bäume des Generators entfernt sind
            w.str(lib.config().text);
            w.u32((uint32_t)taken);
            for (uint64_t k : world_->treesTaken()) {
                w.u32((uint32_t)(k >> 32));
                w.u32((uint32_t)k);
            }
            p.sentTreeLib = &lib;
            p.sentTaken = taken;
            w.u8(tr ? 1 : 0);
            if (tr) {
                const WorldGen& g = tr->gen();
                w.u32((uint32_t)g.seed());
                w.u32((uint32_t)(g.seed() >> 32));
                w.u32((uint32_t)(g.singleBiome() + 1));
                w.str(g.config().text);
                const auto edits = tr->allEdits();
                w.u32((uint32_t)edits.size());
                for (const auto& [key, e] : edits) {
                    w.u32((uint32_t)(key >> 32));
                    w.u32((uint32_t)key);
                    for (float v : e.d) w.f32(v);
                }
            }
            p.conn->send(Msg::Terrain, w);
            p.terrainSent = true;
            p.sentTerrainGen = gen;
            p.sentTerrainRev = rev;
        }
    }
    std::vector<const Object*> moved;
    std::unordered_set<uint32_t> alive;
    alive.reserve(world_->objects().size());
    for (const auto& op : world_->objects()) {
        const Object& o = *op;
        alive.insert(o.id);
        auto it = p.sent.find(o.id);
        if (it == p.sent.end() || it->second.rev != o.netRev) {
            net::Writer w;
            writeObject(w, o);
            p.conn->send(Msg::Object, w);
            p.sent[o.id] = {o.netRev, metaHash(o), o.pos, o.rot};
            continue;
        }
        Peer::Sent& s = it->second;
        const size_t meta = metaHash(o);
        if (meta != s.meta) {
            net::Writer w;
            writeMeta(w, o);
            p.conn->send(Msg::ObjectMeta, w);
            s.meta = meta;
        }
        if (glm::length(o.pos - s.pos) > 1e-6 || std::abs(glm::dot(o.rot, s.rot)) < 1.0 - 1e-10) {
            moved.push_back(&o);
            s.pos = o.pos;
            s.rot = o.rot;
        }
    }
    std::vector<uint32_t> removed;
    for (auto it = p.sent.begin(); it != p.sent.end();) {
        if (alive.count(it->first)) {
            ++it;
            continue;
        }
        removed.push_back(it->first);
        it = p.sent.erase(it);
    }
    if (!removed.empty()) {
        net::Writer w;
        w.u32((uint32_t)removed.size());
        for (uint32_t id : removed) w.u32(id);
        p.conn->send(Msg::Remove, w);
    }
    if (!moved.empty()) {
        net::Writer w;
        w.u32((uint32_t)moved.size());
        for (const Object* o : moved) {
            w.u32(o->id);
            w.dvec3(o->pos);
            w.dquat(o->rot);
        }
        p.conn->send(Msg::Poses, w);
    }
    {
        net::Writer w;
        w.u32((uint32_t)world_->joints().size());
        for (const auto& j : world_->joints()) writeJoint(w, *j);
        if (w.data() != p.sentJoints) {
            p.conn->send(Msg::Joints, w);
            p.sentJoints = w.data();
        }
    }
    {
        net::Writer w;
        w.f64(world_->airDensity());
        w.dvec3(world_->wind());
        w.u8(world_->breakage());
        w.u8(world_->buildCollision());
        w.u32((uint32_t)world_->undoCount());
        w.u32((uint32_t)world_->redoCount());
        w.u8(world_->simulating());
        w.f64(world_->waterTime());  // Gewässer und die Uhr der Wellen: schwimmende Teile und Wellen passen zusammen
        // ohne die Flüsse des Geländes: der Mitspieler erzeugt sie selbst
        const size_t ownWaters = (size_t)std::count_if(world_->waters().begin(), world_->waters().end(), [](const WaterBody& b) { return !b.generated(); });
        w.u32((uint32_t)ownWaters);
        for (const WaterBody& b : world_->waters()) {
            if (b.generated()) continue;
            w.u32(b.id);
            w.u8((uint8_t)b.liquid);
            w.u8(b.sea);
            w.dvec3(b.lo);
            w.dvec3(b.hi);
            w.f64(b.waves);
            w.dvec3(b.flow);
            w.u8(b.ownTemp);
            w.f64(b.tempC);
            // Grund des Meers: die Kacheln nur, wenn der Mitspieler diesen Stand noch nicht hat
            const bool ground = b.sea && b.ground;
            w.u8(ground);
            if (!ground) continue;
            const GroundMap& m = *b.ground;
            w.u32((uint32_t)m.rev);
            w.u32((uint32_t)(m.rev >> 32));
            uint64_t& sent = p.sentGround[b.id];
            w.u8(sent != m.rev);
            if (sent == m.rev) continue;
            sent = m.rev;
            w.f64(m.base);
            w.u32((uint32_t)m.tiles.size());
            for (const auto& [key, tile] : m.tiles) {
                int64_t tx, tz;
                GroundMap::tileOf(key, tx, tz);
                w.f64((double)tx);
                w.f64((double)tz);
                for (float h : tile.h) w.f32(h);
            }
        }
        p.conn->send(Msg::Tick, w);
    }
    net::Writer w;
    w.u32((uint32_t)netPlayers_.size());
    for (const NetPlayer& np : netPlayers_) writePlayer(w, np);
    p.conn->send(Msg::Players, w);
}

void App::netPlayerList() {
    netPlayers_.clear();
    NetPlayer me;
    me.id = 1;
    me.name = netName_;
    me.play = play_;
    me.feet = physics_.characterPosition();
    me.camEye = orbitEye();
    me.yaw = playYaw_;
    me.pitch = playPitch_;
    if (!play_) anglesOf(orbitTarget_ - me.camEye, me.yaw, me.pitch);
    me.grabbed = hand_.grabbed;
    me.tooHeavy = hand_.tooHeavy;
    me.onGround = physics_.characterOnGround();
    netPlayers_.push_back(me);
    for (const auto& p : peers_) {
        if (!p->accepted || p->closeAt >= 0) continue;
        NetPlayer np;
        np.id = p->id;
        np.name = p->name;
        np.play = p->play;
        np.feet = physics_.characterPosition((int)p->id);
        np.camEye = p->camEye;
        np.yaw = p->yaw;
        np.pitch = p->pitch;
        np.grabbed = p->hand.grabbed;
        np.tooHeavy = p->hand.tooHeavy;
        np.onGround = physics_.characterOnGround((int)p->id);
        netPlayers_.push_back(np);
    }
}

glm::dvec3 App::peerEye(const Peer& p) const { return physics_.characterPosition((int)p.id) + glm::dvec3(0, 1.65, 0); }

void App::peerForces(float) {
    for (auto& p : peers_)
        if (p->accepted && p->play && p->hand.grabbed) holdForces(p->hand, peerEye(*p), forwardOf(p->yaw, p->pitch), p->yaw);
}

void App::updatePeerFigures(float dt) {
    for (auto& p : peers_) {
        if (!p->accepted || !p->play || !physics_.hasCharacter((int)p->id)) continue;
        BodyHandle ignore = kNoBody;
        if (Object* g = world_->find(p->hand.grabbed)) ignore = g->body;
        physics_.updateCharacter(dt, p->wish, p->jump, ignore, (int)p->id, swimDepth(physics_.characterPosition((int)p->id)));
        p->jump = false;
    }
}

void App::netSendResult(Peer& p, uint32_t request, const ScriptJob& job) {
    netSync(p);  // erst die Änderungen, dann die Antwort: neue Teile sind beim Mitspieler, wenn er sie liest
    net::Writer w;
    w.u32(request);
    w.u8(job.ok);
    w.str(job.output);
    w.u8(p.selectionChanged);
    w.u32((uint32_t)p.selection.size());
    for (uint32_t id : p.selection) w.u32(id);
    p.selectionChanged = false;
    p.conn->send(Msg::Result, w);
}

// ============================================================================ Mitspieler

bool App::netJoin(const std::string& addressIn, std::string* err) {
    if (netRole_ != NetRole::Off) {
        if (err) *err = "already in a LAN game";
        return false;
    }
    std::string host = addressIn;
    host.erase(0, host.find_first_not_of(" \t"));
    host.erase(host.find_last_not_of(" \t") + 1);
    int port = net::kGamePort;
    if (size_t c = host.rfind(':'); c != std::string::npos) {
        port = std::atoi(host.c_str() + c + 1);
        host.erase(c);
    }
    if (host.empty() || port <= 0) {
        if (err) *err = "enter the address of the host, e.g. 192.168.1.20";
        return false;
    }
    auto conn = std::make_unique<net::Connection>();
    if (!conn->connectTo(host, port, err)) return false;
    net::Writer w;
    w.u32(net::kProtocol);
    w.str(netName_);
    conn->send(Msg::Hello, w);
    if (play_) leavePlay();
    physics_.destroyCharacter();  // die eigene Figur läuft ab jetzt beim Gastgeber
    netConn_ = std::move(conn);
    netRole_ = NetRole::Client;
    netWelcomed_ = false;
    netWorldReady_ = false;
    netConnectStart_ = time_;
    netHostName_ = host;
    netStatus_ = "Verbinde mit " + host + " ...";
    netSearch_.stop();
    return true;
}

void App::netClientUpdate(float dt) {
    const bool alive = netConn_->update();
    net::Msg type;
    std::string payload;
    while (netConn_ && netConn_->next(type, payload)) netHostMessage(type, payload);  // "abgelehnt" beendet die Verbindung
    if (netRole_ != NetRole::Client) return;
    if (!alive) {
        netStop(netWelcomed_ ? "Connection to the host lost" : "No connection to " + netHostName_ + " (is the LAN game open?)");
        return;
    }
    if (!netWelcomed_) {
        if (!netConn_->connected() && time_ - netConnectStart_ > 6.0) netStop("No answer from " + netHostName_);
        return;
    }
    // Eigene Figur: Lage vom Gastgeber, weich nachgeführt
    if (glm::length(netFeetTarget_ - netFeet_) > 3.0) netFeet_ = netFeetTarget_;
    else netFeet_ += (netFeetTarget_ - netFeet_) * (1.0 - std::exp(-dt * 30.0));
    // Zurückgehaltene Lagen selbst verschobener Teile übernehmen, sobald die eigene Bewegung vorbei ist
    for (auto it = netOwnMoves_.begin(); it != netOwnMoves_.end();) {
        if (time_ - it->second.time < kOwnMoveHold) {
            ++it;
            continue;
        }
        if (Object* o = it->second.held ? world_->find(it->first) : nullptr) world_->netMove(*o, it->second.pos, it->second.rot);
        it = netOwnMoves_.erase(it);
    }

    netInputAccum_ += dt;
    if (netInputAccum_ >= 1.0 / kSyncRate || netSpawn_) {
        netInputAccum_ = 0;
        double yaw = playYaw_, pitch = playPitch_;
        if (!play_) anglesOf(orbitTarget_ - orbitEye(), yaw, pitch);
        net::Writer w;
        w.u8(play_);
        w.dvec3(orbitEye());
        w.dvec3(spawnReference());
        w.f64(yaw);
        w.f64(pitch);
        w.vec3(play_ ? netWish_ : glm::vec3(0));
        w.u8(netJump_);
        w.u8(netHand_.lmbDown);
        w.u32((uint32_t)netHand_.lmbPresses);
        w.u32((uint32_t)netHand_.rmbPresses);
        w.u32((uint32_t)netHand_.ePresses);
        w.u32((uint32_t)netHand_.turns);
        w.u32((uint32_t)netHand_.tilts);
        w.f32((float)netHand_.scroll);
        w.u8(netSpawn_);
        w.dvec3(netSpawnFeet_);
        netConn_->send(Msg::Input, w);
        netJump_ = netSpawn_ = false;
        netHand_ = HandInput();
    }
    netConn_->update();  // gleich losschicken
}

void App::netHostMessage(net::Msg type, const std::string& payload) {
    net::Reader r(payload);
    switch (type) {
    case Msg::Waiting:
        netStatus_ = "Warte, bis " + netHostName_ + " dich zulässt ...";
        break;
    case Msg::Reject: {
        const std::string why = r.str();
        netStop(why.empty() ? "The host declined" : why);
        return;
    }
    case Msg::Welcome: {
        netSelfId_ = r.u32();
        netHostName_ = r.str();
        netWelcomed_ = true;
        netStatus_.clear();
        if (haveWorld_) {  // eigene Welt: noch speichern; das LAN-Spiel wird nicht in sie gespeichert
            saveWorld(true, true);
            haveWorld_ = false;
            log("Your world \"" + worldInfo_.name + "\" was saved. The LAN game is not saved into it.", 3);
        }
        world_->clear();  // gleich kommt die Welt des Gastgebers
        selection_.clear();
        hovered_ = 0;
        preview_.valid = false;
        dragCandidate_ = dragging_ = false;
        hand_ = Hand();
        netUndo_ = netRedo_ = 0;
        netInspectText_.clear();
        const std::string msg = "Joined the LAN game of " + netHostName_ + " - the host computes all physics";
        log(msg, 3);
        toast(msg);
        break;
    }
    case Msg::Object: {
        ObjectState s;
        if (readObject(r, s)) world_->netPut(s);
        break;
    }
    case Msg::ObjectMeta: {
        Object* o = world_->find(r.u32());
        const std::string name = r.str();
        const int material = std::clamp((int)r.u32(), 0, (int)materials().size() - 1);
        const bool fixed = r.u8() != 0;
        const int axis = (int)r.u32() - 1;
        const float rpm = r.f32(), torque = r.f32();
        const bool unbreakable = r.u8() != 0;
        const int lubricant = std::clamp((int)r.u32(), 0, (int)lubricants().size() - 1);
        if (!o || !r.ok()) break;
        o->name = name;
        o->unbreakable = unbreakable;
        if (o->material != material) world_->setMaterial(*o, material);
        if (o->fixed != fixed) world_->setFixed(*o, fixed);
        if (o->hingeAxis != axis || o->motorRpm != rpm || o->motorTorque != torque) world_->setHinge(*o, axis, rpm, torque);
        if (o->lubricant != lubricant) world_->setLubricant(*o, lubricant);
        break;
    }
    case Msg::Remove: {
        const uint32_t n = readCount(r, 4);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t id = r.u32();
            world_->remove(id);
            selection_.erase(std::remove(selection_.begin(), selection_.end(), id), selection_.end());
        }
        break;
    }
    case Msg::Poses: {
        const uint32_t n = readCount(r, 60);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t id = r.u32();
            const glm::dvec3 pos = r.dvec3();
            const glm::dquat rot = r.dquat();
            if (!r.ok()) break;
            auto own = netOwnMoves_.find(id);
            if (own != netOwnMoves_.end()) {  // gerade selbst bewegt: diese Lage ist älter, merken statt übernehmen
                own->second.held = true;
                own->second.pos = pos;
                own->second.rot = rot;
                continue;
            }
            if (Object* o = world_->find(id)) world_->netMove(*o, pos, rot);
        }
        break;
    }
    case Msg::Joints: {
        std::vector<JointState> joints(readCount(r, 100));
        for (JointState& j : joints) j = readJoint(r);
        if (r.ok()) world_->netSetJoints(joints);
        break;
    }
    case Msg::JointLoads: {
        const uint32_t n = readCount(r, 8);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t id = r.u32();
            const float load = r.f32();
            if (Joint* j = world_->findJoint(id)) j->load = load;
        }
        break;
    }
    case Msg::Players: {
        std::vector<NetPlayer> players(readCount(r, 30));
        for (NetPlayer& p : players) p = readPlayer(r);
        if (!r.ok()) break;
        netPlayers_ = players;
        for (const NetPlayer& p : netPlayers_) {
            if (p.id != netSelfId_) continue;
            // erst, wenn der Gastgeber die Figur abgesetzt hat (nach tp: Lagen von vorher zählen nicht)
            if (p.play && play_ && (time_ >= netSpawnUntil_ || glm::length(p.feet - netSpawnFeet_) < 3.0)) {
                netFeetTarget_ = p.feet;
                netSpawnUntil_ = 0;
            }
            hand_.grabbed = play_ ? p.grabbed : 0;
            hand_.tooHeavy = p.tooHeavy;
        }
        break;
    }
    case Msg::Tick: {
        // Der Gastgeber schickt nach dem Beitritt erst alle Teile und Verbindungen, dann die erste Lage: ab hier ist die
        // Welt komplett (Ladebildschirm beim Beitreten)
        if (netWelcomed_) netWorldReady_ = true;
        const double air = r.f64();
        const glm::dvec3 wind = r.dvec3();
        const bool breakage = r.u8() != 0, buildCollision = r.u8() != 0;
        netUndo_ = r.u32();
        netRedo_ = r.u32();
        netHostSim_ = r.u8() != 0;
        const double waterTime = r.f64();
        std::vector<WaterBody> waters(readCount(r, 96));
        for (WaterBody& b : waters) {
            b.id = r.u32();
            b.liquid = std::min<int>(r.u8(), (int)liquids().size() - 1);
            b.sea = r.u8() != 0;
            b.lo = r.dvec3();
            b.hi = r.dvec3();
            b.waves = r.f64();
            b.flow = r.dvec3();
            b.ownTemp = r.u8() != 0;
            b.tempC = r.f64();
            if (r.u8() == 0) continue;  // ohne Grund
            const uint64_t revLo = r.u32(), revHi = r.u32();
            const uint64_t rev = revLo | (revHi << 32);
            if (r.u8() == 0) {  // unverändert: den bekannten Stand behalten
                for (const WaterBody& old : world_->waters())
                    if (old.id == b.id && old.ground && old.ground->rev == rev) b.ground = old.ground;
                continue;
            }
            auto m = std::make_shared<GroundMap>();
            m->base = r.f64();
            m->rev = rev;
            const uint32_t n = readCount(r, 16 + 4 * GroundMap::kCells * GroundMap::kCells);
            for (uint32_t i = 0; i < n; ++i) {
                const int64_t tx = (int64_t)r.f64(), tz = (int64_t)r.f64();
                GroundMap::Tile t;
                for (float& h : t.h) h = r.f32();
                t.lo = *std::min_element(t.h.begin(), t.h.end());
                t.hi = *std::max_element(t.h.begin(), t.h.end());
                m->tiles[GroundMap::tileKey(tx, tz)] = std::move(t);
            }
            m->updateBounds();
            b.ground = m;
        }
        if (!r.ok()) break;
        if (air != world_->airDensity() || wind != world_->wind()) world_->setAir(air, wind);
        world_->setBreakage(breakage);
        world_->setBuildCollision(buildCollision);
        world_->setWaters(waters);
        world_->setWaterTime(waterTime);
        break;
    }
    case Msg::Result: {
        const uint32_t req = r.u32();
        const bool ok = r.u8() != 0;
        const std::string text = r.str();
        const bool selChanged = r.u8() != 0;
        std::vector<uint32_t> sel(readCount(r, 4));
        for (uint32_t& id : sel) id = r.u32();
        if (!r.ok()) break;
        NetRequest q;
        if (auto it = netRequests_.find(req); it != netRequests_.end()) {
            q = it->second;
            netRequests_.erase(it);
        }
        if (selChanged) selection_ = sel;
        if (q.quiet) {  // Physik-Inspektor
            netInspectText_ = text;
            break;
        }
        if (!text.empty()) log(text, ok ? 0 : 2);
        if (q.source == 0) {  // erste Ergebniszeile in die Statuszeile (wie bei Werkzeugen)
            status_ = text.substr(0, text.find('\n'));
            statusTime_ = time_;
        } else if (q.source == 1) {
            std::printf("%s", text.c_str());
            std::fflush(stdout);
            scriptBusy_ = false;
        } else if (q.source == 2) {
            remote_->respond(q.client, text);
        }
        break;
    }
    case Msg::Event: {
        const std::string text = r.str();
        const bool ok = r.u8() != 0;
        log("!! " + text, ok ? 3 : 2);
        toast(text, ok);
        break;
    }
    case Msg::Terrain: {  // Gelände des Gastgebers: hier aus Seed und Biomen erzeugen, seine Änderungen darüber
        {  // Baumarten des Gastgebers und seine entfernten Bäume
            const std::string treesText = r.str();
            std::unordered_set<uint64_t> taken;
            const uint32_t n = readCount(r, 8);
            for (uint32_t i = 0; i < n && r.ok(); ++i) {
                const uint64_t a = r.u32(), b = r.u32();
                taken.insert((a << 32) | b);
            }
            if (!r.ok()) break;
            if (world_->trees().config().text != treesText) {
                auto cfg = std::make_shared<TreeConfig>();
                std::string err;
                if (!parseTreeConfig(treesText, *cfg, &err)) {
                    log("Trees of the host: " + err + " - using the built-in trees", 2);
                    *cfg = builtinTreeConfig();
                }
                world_->setTrees(cfg);
            }
            world_->setTreesTaken(taken);
        }
        if (r.u8() == 0) {
            world_->clearTerrain();
            break;
        }
        const uint64_t lo = r.u32(), hi = r.u32();
        const uint64_t seed = lo | (hi << 32);
        const int single = (int)r.u32() - 1;
        const std::string text = r.str();
        std::vector<std::pair<uint64_t, TerrainEdit>> edits(readCount(r, 8 + sizeof(TerrainEdit)));
        for (auto& [key, e] : edits) {
            const uint64_t a = r.u32(), b = r.u32();
            key = (a << 32) | b;
            for (float& v : e.d) v = r.f32();
        }
        if (!r.ok()) break;
        Terrain* tr = world_->terrain();
        if (!tr || tr->gen().seed() != seed || tr->gen().singleBiome() != single || tr->gen().config().text != text) {
            auto cfg = std::make_shared<GenConfig>();
            std::string err;
            if (!parseGenConfig(text, *cfg, &err)) {
                log("Terrain of the host: " + err + " - using the built-in biomes", 2);
                *cfg = builtinGenConfig();
            }
            auto gen = std::make_shared<WorldGen>(seed, cfg, single);
            if (tr) world_->setTerrainGen(gen);
            else world_->setTerrain(gen, {});  // ohne Ordner: der Gastgeber speichert
        }
        world_->terrain()->setEdits(edits);
        {  // Beigetreten: die eigene Kamera kommt auf die Oberfläche (die Spielfigur setzt der Gastgeber ebenso dorthin)
            const double ground = world_->groundHeight(orbitTarget_.x, orbitTarget_.z);
            if (orbitTarget_.y < ground + 0.3) {
                orbitTarget_.y = ground + 0.6;
                orbitDist_ = std::max(orbitDist_, 8.0);
            }
        }
        break;
    }
    default:
        break;
    }
}

// lan [status] | host | join <adresse> | leave | accept [name|all] | decline <name> | kick <name> | confirm on|off | name <text>
std::string App::lanCommand(const std::vector<std::string>& t) {
    const std::string sub = t.size() > 1 ? normalizeWord(t[1]) : "status";
    std::string rest;
    for (size_t i = 2; i < t.size(); ++i) rest += (i > 2 ? " " : "") + t[i];
    auto peersNamed = [&](const std::string& who) {
        std::vector<Peer*> out;
        for (auto& p : peers_)
            if (p->greeted && p->closeAt < 0 && (who == "all" || normalizeWord(p->name) == normalizeWord(who) || std::to_string(p->id) == who))
                out.push_back(p.get());
        return out;
    };
    std::string err;
    if (sub == "status" || sub == "info") {
        std::string s;
        if (netRole_ == NetRole::Off) {
            s = "lan off (lan host = open this game, lan join <address> = join one)\n";
            for (const net::FoundGame& g : netSearch_.found()) s += "  found: " + g.name + " at " + g.address + ", " + std::to_string(g.players) + " player(s)\n";
        } else if (netRole_ == NetRole::Host) {
            s = "lan host on port " + std::to_string(net::kGamePort) + ", confirm joins " + (netConfirm_ ? "on" : "off") + "\n";
            for (const std::string& a : netAddresses_) s += "  address " + a + "\n";
        } else {
            s = netWelcomed_ ? "lan co-player in the game of " + netHostName_ + "\n" : "lan connecting: " + netStatus_ + "\n";
        }
        for (const NetPlayer& p : netPlayers_)
            s += "  player " + std::to_string(p.id) + " " + p.name + (p.id == netSelfId_ ? " (you)" : "") + (p.play ? ", playing at " +
                 fmtNum(p.feet.x * 100) + " " + fmtNum(p.feet.y * 100) + " " + fmtNum(p.feet.z * 100) + " cm" : ", building") +
                 (p.grabbed ? ", holds #" + std::to_string(p.grabbed) : "") + "\n";
        for (auto& p : peers_)
            if (p->greeted && !p->accepted && p->closeAt < 0) s += "  waiting: " + p->name + " (" + p->conn->address() + ") - lan accept " + p->name + "\n";
        s.pop_back();
        return s;
    }
    if (sub == "host" || sub == "open") return netHost(&err) ? "LAN game open on port " + std::to_string(net::kGamePort) : "Error: " + err;
    if (sub == "join" || sub == "connect") {
        if (rest.empty()) return "Error: lan join <address>   (e.g. 192.168.1.20 or 127.0.0.1)";
        return netJoin(rest, &err) ? "Connecting to " + rest + " ... (lan status shows when the host let you in)" : "Error: " + err;
    }
    if (sub == "leave" || sub == "stop" || sub == "close") {
        if (netRole_ == NetRole::Off) return "Error: not in a LAN game";
        netStop("");
        return "LAN game left/closed";
    }
    if (sub == "name") {
        if (netRole_ != NetRole::Off) return "Error: the name can only be changed outside a LAN game";
        if (rest.empty()) return std::string("name: ") + netName_;
        std::snprintf(netName_, sizeof(netName_), "%s", rest.c_str());
        return std::string("name: ") + netName_;
    }
    if (sub == "confirm") {
        netConfirm_ = t.size() < 3 || normalizeWord(t[2]) == "on" || t[2] == "1";
        return std::string("confirm joins ") + (netConfirm_ ? "on: co-players wait until you accept them" : "off: everyone may join");
    }
    if (sub == "accept" || sub == "decline" || sub == "kick") {
        if (netRole_ != NetRole::Host) return "Error: only the host can do that";
        auto list = peersNamed(rest.empty() ? "all" : rest);
        int n = 0;
        for (Peer* p : list) {
            if (sub == "accept" && !p->accepted) netAccept(*p), ++n;
            else if (sub == "decline" && !p->accepted) netReject(*p, "The host declined"), ++n;
            else if (sub == "kick" && p->accepted) netReject(*p, "The host removed you from the game"), ++n;
        }
        return n ? std::to_string(n) + " player(s): " + sub : "Error: no matching player (lan status lists them)";
    }
    return "Error: lan [status] | host | join <address> | leave | accept [name|all] | decline <name> | kick <name> | confirm on|off | name <text>";
}

bool App::localCommand(const std::string& line) const {
    // Befehlsfolgen laufen immer beim Gastgeber (sie könnten die Welt ändern)
    if (line.find_first_of(";{") != std::string::npos) return false;
    static const char* local[] = {"help",   "?",        "materials", "mode",    "view",      "camera", "screenshot",
                                  "photo",  "tool",     "window",    "display", "log",       "select", "drawing",
                                  "threeview", "quit",  "exit",      "save",    "edges",     "list",   "ls",
                                  "chunks", "lan",      "player",    "menu",    "world",     "tp",     "teleport"};
    const std::string w = firstWord(line);
    if (w.empty() || w[0] == '#') return true;
    for (const char* l : local)
        if (w == l) return true;
    if (w == "terrain") {  // Gelände ansehen geht hier (es ist dasselbe wie beim Gastgeber), ändern nur dort
        std::istringstream in(line);
        std::string a, sub;
        in >> a >> sub;
        return sub.empty() || sub == "status" || sub == "at" || sub == "here" || sub == "biomes" || sub == "find" || sub == "goto" ||
               sub == "borders";
    }
    return false;
}

void App::netSendCommand(const std::string& line, int source, uint64_t client, bool quiet) {
    auto fail = [&](const std::string& msg) {
        log(msg, 2);
        if (source == 2) remote_->respond(client, msg);
        if (source == 1) {
            std::printf("%s\n", msg.c_str());
            scriptBusy_ = false;
        }
    };
    const std::string w = firstWord(line);
    if (w == "load" || w == "import") return fail("Error: only the host can load scenes and import models (the file is on your computer)");
    if (!netConn_ || !netWelcomed_) return fail("Error: not connected to the host yet");
    const uint32_t req = netNextRequest_++;
    netRequests_[req] = {source, client, quiet};
    net::Writer m;
    m.u32(req);
    m.str(line);
    m.u8(quiet);
    m.u32((uint32_t)selection_.size());
    for (uint32_t id : selection_) m.u32(id);
    m.dvec3(spawnReference());
    netConn_->send(Msg::Command, m);
}

void App::pushUndo() {
    if (netClient()) netUndoPending_ = true;  // kommt mit der nächsten Lage zum Gastgeber
    else world_->pushUndo();
}

void App::shareTransforms(const std::vector<uint32_t>& ids) {
    if (!netClient()) {
        // Physik läuft, weil jemand spielt: im Bau-Modus gesetzte Teile ruhig halten (sonst behalten sie die
        // Fallgeschwindigkeit, zittern beim Ziehen und fliegen beim Loslassen weg)
        if (world_->simulating())
            for (uint32_t id : ids)
                if (Object* o = world_->find(id); o && o->body != kNoBody && !o->fixed)
                    physics_.setVelocity(o->body, glm::vec3(0), glm::vec3(0));
        return;
    }
    if (!netConn_ || !netWelcomed_) return;
    std::vector<const Object*> list;
    for (uint32_t id : ids)
        if (const Object* o = world_->find(id)) list.push_back(o);
    if (list.empty()) return;
    net::Writer w;
    w.u8(netUndoPending_);
    w.u32((uint32_t)list.size());
    for (const Object* o : list) {
        w.u32(o->id);
        w.dvec3(o->pos);
        w.dquat(o->rot);
        OwnMove& m = netOwnMoves_[o->id];
        m.time = time_;
        m.held = false;
    }
    netUndoPending_ = false;
    netConn_->send(Msg::Transforms, w);
}

// ============================================================================ Anzeige

void App::drawPlayerTags() {
    if (netPlayers_.size() < 2 || views_.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    for (const View& v : views_)
        for (const NetPlayer& p : netPlayers_) {
            if (p.id == netSelfId_) continue;
            const glm::dvec3 at = p.play ? p.feet + glm::dvec3(0, 2.0, 0) : p.camEye + glm::dvec3(0, 0.22, 0);
            const glm::dvec4 c = v.viewProjRel * glm::dvec4(at - renderOrigin_, 1.0);
            if (c.w <= 1e-6) continue;
            const glm::dvec3 ndc = glm::dvec3(c) / c.w;
            if (std::abs(ndc.x) > 1.0 || std::abs(ndc.y) > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) continue;
            const float x = (float)((v.x + (ndc.x * 0.5 + 0.5) * v.w) / io.DisplayFramebufferScale.x);
            const float y = (float)((v.y + (ndc.y * 0.5 + 0.5) * v.h) / io.DisplayFramebufferScale.y);
            const std::string text = p.name + (p.play ? "" : " (baut)");
            const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
            const glm::vec3 col = glm::min(playerColor(p.id) * 1.4f, glm::vec3(1.0f));
            dl->AddRectFilled(ImVec2(x - ts.x / 2 - 6, y - ts.y - 4), ImVec2(x + ts.x / 2 + 6, y + 2), IM_COL32(0, 0, 0, 150), 4);
            dl->AddText(ImVec2(x - ts.x / 2, y - ts.y - 1), IM_COL32((int)(col.r * 255), (int)(col.g * 255), (int)(col.b * 255), 255), text.c_str());
        }
}

void App::drawNetMenu() {
    if (!ImGui::BeginMenu("Mehrspieler")) return;
    if (netRole_ == NetRole::Off) {
        if (ImGui::MenuItem("Im LAN öffnen (Gastgeber)...")) {
            std::string err;
            if (netHost(&err)) {
                showNet_ = true;
            } else {
                log("Error: " + err, 2);
                toast(err, false);
            }
        }
        if (ImGui::MenuItem("LAN-Spiel beitreten...")) showNet_ = true;
    } else if (netRole_ == NetRole::Host) {
        if (ImGui::MenuItem("Spieler einladen / verwalten...")) showNet_ = true;
        ImGui::MenuItem("Beitritt erst nach Bestätigung", nullptr, &netConfirm_);
        ImGui::Separator();
        if (ImGui::MenuItem("LAN-Spiel beenden")) netStop("");
    } else {
        ImGui::TextDisabled(netWelcomed_ ? "Verbunden mit %s" : "Verbinde mit %s ...", netHostName_.c_str());
        if (ImGui::MenuItem("Spieler...")) showNet_ = true;
        if (ImGui::MenuItem("Spiel verlassen")) netStop("");
    }
    ImGui::EndMenu();
}

void App::drawNetWindow() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 230, 90), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Mehrspieler (LAN)", &showNet_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const ImVec4 accent(1.0f, 0.62f, 0.15f, 1.0f), info(0.5f, 0.8f, 1.0f, 1.0f);
    ImGui::SetNextItemWidth(220);
    if (netRole_ != NetRole::Off) ImGui::BeginDisabled();
    ImGui::InputText("Dein Name", netName_, sizeof(netName_));
    if (netRole_ != NetRole::Off) ImGui::EndDisabled();

    if (netRole_ == NetRole::Off) {
        ImGui::SeparatorText("Spiel im LAN öffnen");
        ImGui::TextWrapped("Du bist der Gastgeber: Dein Rechner berechnet die ganze Physik. Mitspieler im selben Netz sehen deine "
                           "Werkstatt, bauen mit und laufen herum.");
        ImGui::Checkbox("Mitspieler erst nach meiner Bestätigung zulassen", &netConfirm_);
        if (ImGui::Button("Im LAN öffnen", ImVec2(-1, 0))) {
            std::string err;
            if (!netHost(&err)) {
                netStatus_ = "Fehler: " + err;
                log("Error: " + err, 2);
            }
        }

        ImGui::SeparatorText("Spiel im LAN beitreten");
        const auto& found = netSearch_.found();
        if (found.empty()) ImGui::TextDisabled("Suche im Netz ... (noch kein offenes Spiel gefunden)");
        for (size_t i = 0; i < found.size(); ++i) {
            const net::FoundGame& g = found[i];
            ImGui::PushID((int)i);
            ImGui::TextColored(accent, "%s", g.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s · %d Spieler", g.address.c_str(), g.players);
            ImGui::SameLine(ImGui::GetWindowWidth() - 100);
            if (ImGui::Button("Beitreten")) {
                std::string err;
                const std::string addr = g.address + (g.port != net::kGamePort ? ":" + std::to_string(g.port) : "");
                if (!netJoin(addr, &err)) netStatus_ = "Fehler: " + err;
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::SetNextItemWidth(220);
        bool go = ImGui::InputTextWithHint("##addr", "Adresse, z.B. 192.168.1.20", netAddress_, sizeof(netAddress_),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        go |= ImGui::Button("Verbinden");
        if (go) {
            std::string err;
            if (!netJoin(netAddress_, &err)) netStatus_ = "Fehler: " + err;
        }
        if (!netStatus_.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", netStatus_.c_str());
    } else if (netRole_ == NetRole::Host) {
        ImGui::SeparatorText("Spieler einladen");
        ImGui::TextWrapped("Dein Spiel ist im LAN offen. Mitspieler wählen „Mehrspieler > LAN-Spiel beitreten“ und finden es dort, "
                           "oder sie verbinden sich mit dieser Adresse:");
        if (netAddresses_.empty()) ImGui::BulletText("(keine Netzwerkverbindung gefunden)");
        for (const std::string& a : netAddresses_) ImGui::BulletText("%s", a.c_str());
        ImGui::Checkbox("Mitspieler erst nach meiner Bestätigung zulassen", &netConfirm_);
        ImGui::SeparatorText("Spieler");
        ImGui::TextColored(ImVec4(playerColor(1).r, playerColor(1).g, playerColor(1).b, 1), "%s", netName_);
        ImGui::SameLine();
        ImGui::TextDisabled("(du, Gastgeber, %s)", play_ ? "spielt" : "baut");
        for (auto& p : peers_) {
            if (!p->greeted || p->closeAt >= 0) continue;
            ImGui::PushID((int)p->id);
            const glm::vec3 c = playerColor(p->id);
            ImGui::TextColored(ImVec4(c.r, c.g, c.b, 1), "%s", p->name.c_str());
            ImGui::SameLine();
            if (p->accepted) {
                ImGui::TextDisabled("(%s, %s)", p->conn->address().c_str(), p->play ? "spielt" : "baut");
                ImGui::SameLine(ImGui::GetWindowWidth() - 100);
                if (ImGui::SmallButton("Entfernen")) netReject(*p, "The host removed you from the game");
            } else {
                ImGui::TextDisabled("(%s) möchte beitreten", p->conn->address().c_str());
                ImGui::SameLine(ImGui::GetWindowWidth() - 160);
                if (ImGui::SmallButton("Zulassen")) netAccept(*p);
                ImGui::SameLine();
                if (ImGui::SmallButton("Ablehnen")) netReject(*p, "The host declined");
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        if (world_->simulating() && !play_) ImGui::TextColored(info, "Physik läuft, weil Mitspieler im Spiel-Modus sind.");
        if (ImGui::Button("LAN-Spiel beenden", ImVec2(-1, 0))) netStop("");
    } else {
        ImGui::SeparatorText(netWelcomed_ ? ("Mitspieler bei " + netHostName_).c_str() : "Verbinden");
        if (!netWelcomed_) {
            ImGui::TextColored(info, "%s", netStatus_.c_str());
        } else {
            ImGui::TextWrapped("Der Gastgeber berechnet die Physik. Bauen und Werkzeuge funktionieren wie sonst, Ergebnisse kommen vom "
                               "Gastgeber. Tab wechselt zwischen Bauen und Spielen.");
            for (const NetPlayer& p : netPlayers_) {
                const glm::vec3 c = playerColor(p.id);
                ImGui::TextColored(ImVec4(c.r, c.g, c.b, 1), "%s", p.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s%s)", p.id == netSelfId_ ? "du, " : p.id == 1 ? "Gastgeber, " : "", p.play ? "spielt" : "baut");
            }
        }
        ImGui::Spacing();
        if (ImGui::Button(netWelcomed_ ? "Spiel verlassen" : "Abbrechen", ImVec2(-1, 0))) netStop("");
    }
    ImGui::End();
}

// Beitrittsanfragen: immer oben sichtbar, auch wenn das Mehrspieler-Fenster zu ist
void App::drawNetRequests() {
    if (netRole_ != NetRole::Host) return;
    std::vector<Peer*> waiting;
    for (auto& p : peers_)
        if (p->greeted && !p->accepted && p->closeAt < 0) waiting.push_back(p.get());
    if (waiting.empty()) return;
    ImGuiIO& io = ImGui::GetIO();  // unter den Meldungen oben in der Mitte
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.24f), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::Begin("Beitrittsanfrage", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    for (Peer* p : waiting) {
        ImGui::PushID((int)p->id);
        ImGui::Text("%s (%s) möchte deinem Spiel beitreten.", p->name.c_str(), p->conn->address().c_str());
        if (ImGui::Button("Zulassen")) netAccept(*p);
        ImGui::SameLine();
        if (ImGui::Button("Ablehnen")) netReject(*p, "The host declined");
        ImGui::PopID();
    }
    if (cursorCaptured_) ImGui::TextDisabled("Esc drücken, um die Maus freizugeben");
    ImGui::End();
}
