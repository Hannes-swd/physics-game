// Weltdatei binär (world.bin im Weltordner): klein und schnell zu laden. Teile mit fertiger Kollisionsform (die teure
// Zerlegung entfällt beim Laden), Verbindungen, Gewässer, entfernte Bäume. Unveränderte Bäume des Generators stehen nicht
// darin (sie entstehen aus dem Seed), das Gelände auch nicht (Regionsdateien). Siehe docs/07-Engine-Aufbau.md.
//
//   "WKBIN" 0 0 0, Version (u32), nächste Nummern (Teil, Verbindung, Gewässer)
//   Teile:       Anzahl, je Teil writeObject(file = true)
//   Verbindungen: Anzahl, je writeJoint
//   Gewässer:    Anzahl, je Werte und Grund (Meer)
//   Bäume:       Anzahl entfernter, je Platz (u64)
//   ab Version 2: nächste Vorlagen-Nummer, Bauvorlagen: Anzahl, je Vorlage Werte und Bauteile
#include "World.h"

#include <cstring>
#include <fstream>
#include <sstream>

#include "Materials.h"
#include "ObjectIO.h"
#include "Profile.h"

namespace {

constexpr char kMagic[8] = {'W', 'K', 'B', 'I', 'N', 0, 0, 0};
constexpr uint32_t kVersion = 2;

void u64(net::Writer& w, uint64_t v) {
    w.u32((uint32_t)v);
    w.u32((uint32_t)(v >> 32));
}
uint64_t u64(net::Reader& r) {
    const uint64_t lo = r.u32(), hi = r.u32();
    return lo | (hi << 32);
}

} // namespace

bool World::isBinaryScene(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    char m[8] = {};
    return in.read(m, 8) && std::memcmp(m, kMagic, 8) == 0;
}

bool World::saveBinary(const std::filesystem::path& file, std::string* err) const {
    PROFILE("world.saveBinary");
    net::Writer w;
    w.u32(0);  // Platzhalter, unten durch die Kennung ersetzt
    w.u32(0);
    w.u32(kVersion);
    w.u32(nextId_);
    w.u32(nextJointId_);
    w.u32(nextWaterId_);
    std::vector<const Object*> list;
    for (const auto& op : objects_)
        if (!untouchedTree(*op)) list.push_back(op.get());
    w.u32((uint32_t)list.size());
    for (const Object* o : list) writeObject(w, *o, true);
    w.u32((uint32_t)joints_.size());
    for (const auto& j : joints_) writeJoint(w, *j);
    std::vector<const WaterBody*> waters;
    for (const WaterBody& b : waters_)
        if (!b.generated()) waters.push_back(&b);
    w.u32((uint32_t)waters.size());
    for (const WaterBody* b : waters) {
        w.u32(b->id);
        w.u32((uint32_t)b->liquid);
        w.u8(b->sea);
        w.dvec3(b->lo);
        w.dvec3(b->hi);
        w.f64(b->waves);
        w.dvec3(b->flow);
        w.u8(b->ownTemp);
        w.f64(b->tempC);
        const bool ground = b->sea && b->ground;
        w.u8(ground);
        if (!ground) continue;
        w.f64(b->ground->base);
        w.u32((uint32_t)b->ground->tiles.size());
        for (const auto& [key, tile] : b->ground->tiles) {
            u64(w, key);
            for (float h : tile.h) w.f32(h);
        }
    }
    w.u32((uint32_t)treesTaken_.size());
    for (uint64_t k : treesTaken_) u64(w, k);
    w.u32(nextPlanId_);
    w.u32((uint32_t)plans_.size());
    for (const BuildPlan& p : plans_) {
        w.u32(p.id);
        w.str(p.type);
        w.dvec3(p.pos);
        w.f64(p.yaw);
        w.u8(p.fixed);
        w.u32((uint32_t)p.pieces.size());
        for (const PlanPiece& c : p.pieces) {
            w.dvec3(c.center);
            w.dvec3(c.half);
            w.dquat(c.rot);
            w.u32((uint32_t)c.mat);
            w.f64(c.filled);
            w.u32((uint32_t)(c.material + 1));
            w.u32(c.built);
        }
    }

    std::string data = w.data();
    std::memcpy(data.data(), kMagic, 8);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(data.data(), (std::streamsize)data.size())) {
        if (err) *err = "cannot write the file";
        return false;
    }
    return true;
}

bool World::loadBinary(const std::filesystem::path& file, std::string* err, const std::function<void(size_t, size_t)>& progress) {
    PROFILE("world.loadBinary");
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (err) *err = "file not found";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string data = ss.str();
    auto fail = [&](const std::string& e) {
        if (err) *err = e;
        return false;
    };
    if (data.size() < 12 || std::memcmp(data.data(), kMagic, 8) != 0) return fail("not a world file");
    net::Reader r(data);
    r.u32(), r.u32();
    const uint32_t version = r.u32();
    if (version > kVersion) return fail("the world was saved by a newer version of the game");
    const uint32_t nextId = r.u32(), nextJointId = r.u32(), nextWaterId = r.u32();
    std::vector<ObjectState> states(readCount(r, 64));
    for (ObjectState& s : states)
        if (!readObject(r, s, true)) return fail("damaged (parts)");
    std::vector<JointState> joints(readCount(r, 64));
    for (JointState& j : joints) j = readJoint(r);
    std::vector<WaterBody> waters(readCount(r, 64));
    for (WaterBody& b : waters) {
        b.id = r.u32();
        b.liquid = std::clamp((int)r.u32(), 0, (int)liquids().size() - 1);
        b.sea = r.u8() != 0;
        b.lo = r.dvec3();
        b.hi = r.dvec3();
        b.waves = r.f64();
        b.flow = r.dvec3();
        b.ownTemp = r.u8() != 0;
        b.tempC = r.f64();
        if (r.u8() == 0) continue;
        auto m = std::make_shared<GroundMap>();
        m->base = r.f64();
        const uint32_t n = readCount(r, 8 + 4 * GroundMap::kCells * GroundMap::kCells);
        for (uint32_t i = 0; i < n && r.ok(); ++i) {
            const uint64_t key = u64(r);
            GroundMap::Tile t;
            for (float& h : t.h) h = r.f32();
            t.lo = *std::min_element(t.h.begin(), t.h.end());
            t.hi = *std::max_element(t.h.begin(), t.h.end());
            m->tiles[key] = std::move(t);
        }
        m->updateBounds();
        m->rev = newGroundRev();
        b.ground = m;
    }
    std::unordered_set<uint64_t> taken;
    const uint32_t nt = readCount(r, 8);
    for (uint32_t i = 0; i < nt && r.ok(); ++i) taken.insert(u64(r));
    uint32_t nextPlanId = 1;
    std::vector<BuildPlan> plans;
    if (version >= 2) {
        nextPlanId = r.u32();
        plans.resize(readCount(r, 16));
        for (BuildPlan& p : plans) {
            p.id = r.u32();
            p.type = r.str();
            p.pos = r.dvec3();
            p.yaw = r.f64();
            p.fixed = r.u8() != 0;
            p.pieces.resize(readCount(r, 64));
            for (PlanPiece& c : p.pieces) {
                c.center = r.dvec3();
                c.half = r.dvec3();
                c.rot = r.dquat();
                c.mat = (int)r.u32();
                c.filled = r.f64();
                c.material = std::min((int)r.u32() - 1, (int)materials().size() - 1);
                c.built = r.u32();
            }
        }
    }
    if (!r.ok()) return fail("damaged (cut short)");

    clear();
    undo_.clear();
    redo_.clear();
    setWaters(waters);
    treesTaken_ = taken;
    nextWaterId_ = std::max(nextWaterId_, nextWaterId);
    for (size_t i = 0; i < states.size(); ++i) {
        if (progress) progress(i, states.size());
        auto o = std::make_unique<Object>();
        static_cast<ObjectState&>(*o) = std::move(states[i]);
        insert(std::move(o));  // mit der gespeicherten Kollisionsform: keine neue Zerlegung
    }
    nextId_ = std::max(nextId_, nextId);
    for (auto& o : objects_) nextId_ = std::max(nextId_, o->id + 1);
    for (const JointState& j : joints) addJoint(j, nullptr);
    nextJointId_ = std::max(nextJointId_, nextJointId);
    plans_ = std::move(plans);
    nextPlanId_ = nextPlanId;
    for (const BuildPlan& p : plans_) nextPlanId_ = std::max(nextPlanId_, p.id + 1);
    return true;
}
