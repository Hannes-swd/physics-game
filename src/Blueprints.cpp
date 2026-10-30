// Bauvorlagen (siehe Blueprints.h, docs/20-Bauvorlagen.md): Vorlagen, Hologramme setzen, mit Material füllen, abbauen.
#include "Blueprints.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <windows.h>

#include "Json.h"
#include "Materials.h"
#include "World.h"
#include "Worlds.h"

namespace {

// Vorlagen stehen in Dateien (JSON, eine je Vorlage): mitgeliefert in assets/blueprints, eigene in
// %APPDATA%\Werkstatt\blueprints. Raster der mitgelieferten: Breite 2 m, Geschosshöhe 2,5 m; Wände stehen mittig auf ihrer
// Linie (z = 0), Böden liegen mit der Oberseite auf y = 0. Format siehe docs/20-Bauvorlagen.md.
std::filesystem::path exeDirectory() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? std::filesystem::path(buf).parent_path() : std::filesystem::current_path();
}

// sub: "blueprints" (Bauvorlagen) oder "recipes" (Rezepte zum Herstellen)
std::filesystem::path builtinDir(const char* sub) {
    std::error_code ec;
    const std::filesystem::path dir = exeDirectory();
    for (const std::filesystem::path& p : {dir / sub, std::filesystem::current_path(ec) / "assets" / sub, dir / "assets" / sub,
                                           dir.parent_path().parent_path() / "assets" / sub})
        if (std::filesystem::is_directory(p, ec)) return p;
    return {};
}

std::filesystem::path userDir(const char* sub = "blueprints") { return worlds::dataDir() / sub; }

bool vec3(const Json& j, glm::dvec3& v) {
    if (j.type != Json::Array || j.size() != 3) return false;
    for (size_t k = 0; k < 3; ++k) v[(int)k] = j[k].num;
    return true;
}

PlanMat matOf(const std::string& s) {
    return s == "stone" ? PlanMat::Stone : s == "metal" ? PlanMat::Metal : s == "any" ? PlanMat::Any : PlanMat::Wood;
}
const char* matKey(PlanMat m) {
    return m == PlanMat::Stone ? "stone" : m == PlanMat::Metal ? "metal" : m == PlanMat::Any ? "any" : "wood";
}

bool readTemplate(const std::filesystem::path& file, PlanTemplate& t, std::string& err) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    Json j;
    if (!parseJson(ss.str(), j, &err)) return false;
    t = PlanTemplate();
    t.id = j["id"].type == Json::String ? j["id"].str : file.stem().u8string();
    t.label = j["label"].type == Json::String ? j["label"].str : t.id;
    t.toGround = j["to_ground"].type == Json::Bool && j["to_ground"].num != 0;
    t.order = j["order"].integer(1000);
    const Json& ps = j["pieces"];
    for (size_t i = 0; i < ps.size(); ++i) {
        const Json& p = ps[i];
        PlanPieceDef d;
        glm::dvec3 a, b;
        if (vec3(p["from"], a) && vec3(p["to"], b)) {
            d.center = (a + b) * 0.5;
            d.half = glm::abs(b - a) * 0.5;
        } else if (vec3(p["center"], a) && vec3(p["size"], b)) {
            d.center = a;
            d.half = glm::abs(b) * 0.5;
        } else {
            err = "piece " + std::to_string(i + 1) + ": needs \"from\"/\"to\" or \"center\"/\"size\"";
            return false;
        }
        d.pitchDeg = p["pitch"].number(0);
        const Json& q = p["rotation"];  // w x y z
        if (q.type == Json::Array && q.size() == 4) d.rot = glm::normalize(glm::dquat(q[0].num, q[1].num, q[2].num, q[3].num));
        d.mat = matOf(p["material"].str);
        if (p["main"].type == Json::Bool && p["main"].num != 0) t.mainPiece = (int)t.pieces.size();
        t.pieces.push_back(d);
    }
    if (t.pieces.empty()) {
        err = "no pieces";
        return false;
    }
    return true;
}

void loadTemplates(const char* sub, bool craft, std::vector<PlanTemplate>& t) {
    std::error_code ec;
    for (int custom = 0; custom < 2; ++custom) {
        const std::filesystem::path dir = custom ? userDir(sub) : builtinDir(sub);
        if (dir.empty() || !std::filesystem::is_directory(dir, ec)) continue;
        std::vector<std::filesystem::path> files;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".json") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            PlanTemplate p;
            std::string err;
            if (!readTemplate(f, p, err)) {
                std::fprintf(stderr, "%s %s: %s\n", craft ? "recipe" : "blueprint", f.u8string().c_str(), err.c_str());
                continue;
            }
            p.custom = custom != 0;
            p.craft = craft;
            auto it = std::find_if(t.begin(), t.end(), [&](const PlanTemplate& o) { return o.id == p.id; });
            if (it != t.end()) *it = p;  // gleicher Name: die eigene ersetzt die mitgelieferte
            else t.push_back(p);
        }
    }
    // Reihenfolge beim Durchblättern: "order" aus der Datei, dann nach Name
    std::stable_sort(t.begin(), t.end(), [](const PlanTemplate& a, const PlanTemplate& b) { return a.order != b.order ? a.order < b.order : a.id < b.id; });
}

std::vector<PlanTemplate>& templates() {
    static std::vector<PlanTemplate> t;
    static bool loaded = false;
    if (!loaded) loadTemplates("blueprints", false, t);
    loaded = true;
    return t;
}

std::vector<PlanTemplate>& recipes() {
    static std::vector<PlanTemplate> t;
    static bool loaded = false;
    if (!loaded) loadTemplates("recipes", true, t);
    loaded = true;
    return t;
}

// Strahl gegen einen gedrehten Quader (Mitte c, Drehung r, Halbmaße h): Eintrittsstelle t
bool rayBox(const glm::dvec3& o, const glm::dvec3& d, const glm::dvec3& c, const glm::dquat& r, const glm::dvec3& h, double& t) {
    const glm::dquat inv = glm::conjugate(r);
    const glm::dvec3 lo = inv * (o - c), ld = inv * d;
    double t0 = -1e30, t1 = 1e30;
    for (int k = 0; k < 3; ++k) {
        if (std::abs(ld[k]) < 1e-12) {
            if (lo[k] < -h[k] || lo[k] > h[k]) return false;
            continue;
        }
        double a = (-h[k] - lo[k]) / ld[k], b = (h[k] - lo[k]) / ld[k];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    if (t1 < 0) return false;
    t = std::max(t0, 0.0);
    return true;
}

bool fitsMat(PlanMat m, int material) {
    const MaterialClass c = materialClass(material);
    switch (m) {
    case PlanMat::Wood: return c == MaterialClass::Wood;
    case PlanMat::Stone: return c == MaterialClass::Stone;
    case PlanMat::Metal: return c == MaterialClass::Metal;
    default: return true;
    }
}

} // namespace

const std::vector<PlanTemplate>& planTemplates() { return templates(); }
const std::vector<PlanTemplate>& craftRecipes() { return recipes(); }

const PlanTemplate* planTemplateFor(const std::string& type) {
    const size_t n = std::char_traits<char>::length(kCraftPrefix);
    const bool craft = type.rfind(kCraftPrefix, 0) == 0;
    const std::string id = craft ? type.substr(n) : type;
    for (const PlanTemplate& t : craft ? recipes() : templates())
        if (t.id == id) return &t;
    return nullptr;
}

bool savePlanTemplate(const PlanTemplate& t, std::string* err) {
    auto& list = templates();
    if (t.id.empty() || t.id.find_first_of("/\\:*?\"<>| ") != std::string::npos) {
        if (err) *err = "the name must be one word without / \\ : * ? \" < > |";
        return false;
    }
    for (const PlanTemplate& o : list)
        if (o.id == t.id && !o.custom) {
            if (err) *err = "'" + t.id + "' is a built-in template, choose another name";
            return false;
        }
    Json j = Json::makeObject();
    j.set("id", Json::makeString(t.id));
    j.set("label", Json::makeString(t.label));
    j.set("order", Json::makeNumber(t.order));
    if (t.toGround) {
        Json b;
        b.type = Json::Bool;
        b.num = 1;
        j.set("to_ground", b);
    }
    Json& ps = j.set("pieces", Json::makeArray());
    auto arr = [](std::initializer_list<double> v) {
        Json a = Json::makeArray();
        for (double x : v) a.push(Json::makeNumber(std::round(x * 10000.0) / 10000.0));
        return a;
    };
    for (const PlanPieceDef& d : t.pieces) {
        Json p = Json::makeObject();
        p.set("center", arr({d.center.x, d.center.y, d.center.z}));
        p.set("size", arr({d.half.x * 2, d.half.y * 2, d.half.z * 2}));
        if (d.pitchDeg != 0) p.set("pitch", Json::makeNumber(d.pitchDeg));
        if (std::abs(d.rot.w) < 0.999999) p.set("rotation", arr({d.rot.w, d.rot.x, d.rot.y, d.rot.z}));
        p.set("material", Json::makeString(matKey(d.mat)));
        ps.push(std::move(p));
    }
    std::error_code ec;
    std::filesystem::create_directories(userDir(), ec);
    const std::filesystem::path file = userDir() / std::filesystem::u8path(t.id + ".json");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out || !(out << writeJson(j))) {
        if (err) *err = "cannot write " + file.u8string();
        return false;
    }
    PlanTemplate c = t;
    c.custom = true;
    auto it = std::find_if(list.begin(), list.end(), [&](const PlanTemplate& o) { return o.id == t.id; });
    if (it != list.end()) *it = c;
    else list.push_back(c);
    if (err) *err = file.u8string();
    return true;
}

bool deletePlanTemplate(const std::string& id, std::string* err) {
    auto& list = templates();
    auto it = std::find_if(list.begin(), list.end(), [&](const PlanTemplate& o) { return o.id == id; });
    if (it == list.end() || !it->custom) {
        if (err) *err = it == list.end() ? "no template '" + id + "'" : "built-in templates cannot be deleted";
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(userDir() / std::filesystem::u8path(id + ".json"), ec);
    list.erase(it);
    return true;
}

int findPlanTemplate(const std::string& name) {
    const auto& t = planTemplates();
    for (size_t i = 0; i < t.size(); ++i)
        if (name == t[i].id) return (int)i;
    return -1;
}

const char* planMatLabel(PlanMat m) {
    switch (m) {
    case PlanMat::Wood: return "Holz";
    case PlanMat::Stone: return "Stein";
    case PlanMat::Metal: return "Metall";
    default: return "Material";
    }
}

bool World::makePlan(const std::string& type, const glm::dvec3& pos, double yaw, bool fixed, BuildPlan& out) const {
    const PlanTemplate* found = planTemplateFor(type);
    if (!found) return false;
    const PlanTemplate& tp = *found;
    out = BuildPlan();
    out.type = type;
    out.pos = pos;
    out.yaw = yaw;
    out.fixed = fixed;
    const glm::dquat r = out.rot();
    for (const PlanPieceDef& d : tp.pieces) {
        PlanPiece p;
        p.center = d.center;
        p.half = d.half;
        p.rot = d.rot * glm::angleAxis(glm::radians(d.pitchDeg), glm::dvec3(1, 0, 0));
        p.mat = (int)d.mat;
        if (tp.toGround) {  // bis unter den tiefsten Punkt des Geländes darunter (mindestens 10 cm dick)
            const double top = d.center.y + d.half.y;
            double low = 1e30;
            for (int c = 0; c < 5; ++c) {
                glm::dvec3 l = d.center;
                if (c < 4) {
                    l.x += (c & 1) ? d.half.x : -d.half.x;
                    l.z += (c & 2) ? d.half.z : -d.half.z;
                }
                const glm::dvec3 w = pos + r * l;
                low = std::min(low, groundHeight(w.x, w.z) - pos.y);
            }
            const double bottom = std::min(low - 0.05, top - 0.1);
            p.center.y = (top + bottom) * 0.5;
            p.half.y = (top - bottom) * 0.5;
        }
        out.pieces.push_back(p);
    }
    return true;
}

BuildPlan* World::addPlan(const std::string& type, const glm::dvec3& pos, double yaw, bool fixed, std::string* err) {
    BuildPlan p;
    if (!makePlan(type, pos, yaw, fixed, p)) {
        if (err) {
            const bool craft = type.rfind(kCraftPrefix, 0) == 0;
            const auto& list = craft ? craftRecipes() : planTemplates();
            *err = std::string(craft ? "unknown recipe '" : "unknown template '") + type + "' (";
            for (const PlanTemplate& t : list) *err += (craft ? kCraftPrefix : "") + t.id + (&t == &list.back() ? ")" : ", ");
        }
        return nullptr;
    }
    p.id = nextPlanId_++;
    plans_.push_back(std::move(p));
    return &plans_.back();
}

bool World::removePlan(uint32_t id) {
    for (size_t i = 0; i < plans_.size(); ++i)
        if (plans_[i].id == id) {
            plans_.erase(plans_.begin() + (long)i);
            return true;
        }
    return false;
}

BuildPlan* World::findPlan(uint32_t id) {
    for (BuildPlan& p : plans_)
        if (p.id == id) return &p;
    return nullptr;
}

bool World::pickPlan(const glm::dvec3& o, const glm::dvec3& dir, double maxT, uint32_t& plan, int& piece, double& t, bool built) const {
    bool found = false;
    t = maxT;
    for (const BuildPlan& bp : plans_)
        for (size_t i = 0; i < bp.pieces.size(); ++i) {
            const PlanPiece& p = bp.pieces[i];
            if (bp.pieceDone(p) && !built) continue;
            double ti;
            if (rayBox(o, dir, bp.pieceCenter(p), bp.pieceRot(p), p.half, ti) && ti < t) {
                t = ti;
                plan = bp.id;
                piece = (int)i;
                found = true;
            }
        }
    return found;
}

bool World::planPieceOf(uint32_t objId, uint32_t& plan, int& piece) const {
    if (!objId) return false;
    for (const BuildPlan& bp : plans_)
        for (size_t i = 0; i < bp.pieces.size(); ++i)
            if (bp.pieces[i].built == objId) {
                plan = bp.id;
                piece = (int)i;
                return true;
            }
    return false;
}

bool World::fillPlan(uint32_t planId, int pieceIdx, uint32_t objId, std::string& text, bool* done, uint32_t* rest, uint32_t* crafted) {
    if (done) *done = false;
    if (rest) *rest = 0;
    if (crafted) *crafted = 0;
    BuildPlan* bp = findPlan(planId);
    Object* o = find(objId);
    if (!bp || pieceIdx < 0 || pieceIdx >= (int)bp->pieces.size()) {
        text = "no such hologram";
        return false;
    }
    if (!o) {
        text = "part not found";
        return false;
    }
    PlanPiece& pc = bp->pieces[(size_t)pieceIdx];
    const PlanMat want = (PlanMat)pc.mat;
    uint32_t dummyPlan;
    int dummyPiece;
    if (bp->pieceDone(pc)) {
        text = bp->craft() ? "this piece is already full" : "this piece is already built";
        return false;
    }
    if (planPieceOf(objId, dummyPlan, dummyPiece)) {
        text = "this part is already built into a template (Q dismantles it)";
        return false;
    }
    if (o->fixed) {
        text = "this part is fixed in place";
        return false;
    }
    if (!fitsMat(want, o->material) || (pc.material >= 0 && materialClass(pc.material) != materialClass(o->material))) {
        text = std::string("needs ") + (pc.material >= 0 ? materials()[(size_t)pc.material].label : planMatLabel(want)) + ", this is " +
               materials()[(size_t)o->material].label;
        return false;
    }
    const int material = o->material;
    const double need = std::max(0.0, pc.volume() - pc.filled);
    double taken = 0;
    // Zu großes Stück: nur so viel absägen, wie fehlt (quer zur längsten Seite), der Rest bleibt übrig
    if (o->kind == ObjKind::Solid && o->volume > need * 1.15 && need > 1e-6) {
        const glm::dvec3 ext = o->bbMax - o->bbMin;
        const int ax = ext.x >= ext.y && ext.x >= ext.z ? 0 : (ext.y >= ext.z ? 1 : 2);
        const double frac = std::clamp(need * 1.03 / o->volume, 0.02, 0.98);
        glm::dvec3 n(0), q = (o->bbMin + o->bbMax) * 0.5;
        n[ax] = 1;
        q[ax] = o->bbMin[ax] + ext[ax] * frac;
        solid::Plane pl;
        pl.n = n;
        pl.d = q[ax];
        const glm::dvec3 wp = o->pos + o->rot * q, wn = o->rot * n;
        std::vector<uint32_t> ids;
        std::string err;
        if (cut(objId, pl, 0.0, &ids, &err)) {
            double restVol = 0;
            for (uint32_t id : ids) {
                Object* r = find(id);
                if (!r) continue;
                if (glm::dot((r->wMin + r->wMax) * 0.5 - wp, wn) < 0) {
                    taken += r->volume;
                    remove(id);
                } else if (r->volume > restVol) {
                    restVol = r->volume;
                    if (rest) *rest = id;
                }
            }
        }
    }
    if (taken <= 0) {  // ganz einsetzen
        taken = o->volume;
        remove(objId);
        if (rest) *rest = 0;
    }
    if (pc.material < 0) pc.material = material;
    pc.filled += taken;
    const PlanTemplate* tp = planTemplateFor(bp->type);
    const std::string label = tp ? tp->label : bp->type;
    if (bp->craft()) {
        // Rezept: Bauteile werden nur voll; sind alle voll, wird aus allen zusammen ein einziges Teil
        auto cm3 = [](double v) { return fmtNum(v * 1e6, 0) + " cm3"; };
        if (pc.filled < pc.volume() * 0.97) {
            text = label + ": " + cm3(taken) + " inserted, " + cm3(pc.filled) + " of " + cm3(pc.volume());
            return true;
        }
        pc.filled = pc.volume();
        if (done) *done = true;
        int left = 0;
        for (const PlanPiece& p : bp->pieces) left += bp->pieceDone(p) ? 0 : 1;
        if (left) {
            text = label + ": piece " + std::to_string(pieceIdx + 1) + " full (" + std::to_string(left) + " to go)";
            return true;
        }
        solid::Solid s;
        // Material des Ergebnisses: das des Hauptteils ("main"), sonst das des größten Bauteils
        int main = tp && tp->mainPiece >= 0 && tp->mainPiece < (int)bp->pieces.size() ? tp->mainPiece : -1;
        const bool mainGiven = main >= 0;
        for (size_t i = 0; i < bp->pieces.size(); ++i) {
            const PlanPiece& p = bp->pieces[i];
            solid::Piece box = solid::makeBox(p.half);
            solid::transform(box, glm::translate(glm::dmat4(1.0), p.center) * glm::mat4_cast(p.rot));
            s.pieces.push_back(std::move(box));
            if (!mainGiven && (main < 0 || p.volume() > bp->pieces[(size_t)main].volume())) main = (int)i;
        }
        solid::classifyInternalFaces(s);
        const int mat = bp->pieces[(size_t)main].material;
        Object* n = addSolid(std::move(s), mat, bp->pos, bp->rot(), label, "crafted " + (tp ? tp->id : bp->type));
        if (!n) {
            text = "could not create " + label;
            return false;
        }
        if (crafted) *crafted = n->id;
        text = label + " made (" + materials()[(size_t)n->material].label + ", " + fmtNum(n->mass, 2) + " kg)";
        removePlan(planId);
        return true;
    }
    if (pc.filled < pc.volume() * 0.97) {
        text = label + ": " + fmtNum(taken * 1000, 1) + " dm3 inserted, " + fmtNum(pc.filled * 1000, 0) + " of " +
               fmtNum(pc.volume() * 1000, 0) + " dm3";
        return true;
    }
    // fertig: ein echtes Teil genau in der Form des Bauteils
    pc.filled = pc.volume();
    Object* n = addBox(pc.material, pc.half * 2.0, bp->pieceCenter(pc), label + " " + std::to_string(pieceIdx + 1));
    if (!n) {
        text = "could not create the part";
        return false;
    }
    setTransform(*n, bp->pieceCenter(pc), bp->pieceRot(pc));
    n->unbreakable = bp->fixed;
    setFixed(*n, true);  // auch mit Physik: fest, bis die ganze Vorlage fertig ist (sonst fällt das erste Brett um)
    pc.built = n->id;
    if (done) *done = true;
    int left = 0;
    for (const PlanPiece& p : bp->pieces) left += p.built ? 0 : 1;
    text = label + ": piece " + std::to_string(pieceIdx + 1) + " built" +
           (left ? " (" + std::to_string(left) + " to go)" : " - the template is complete");
    if (!left && !bp->fixed) {
        // Physik: alle Bauteile lose und dort verleimt, wo sie sich berühren
        std::vector<Object*> parts;
        for (const PlanPiece& p : bp->pieces)
            if (Object* q = find(p.built)) parts.push_back(q);
        for (Object* q : parts) setFixed(*q, false);
        int glued = 0;
        for (size_t i = 0; i < parts.size(); ++i)
            for (size_t j = i + 1; j < parts.size(); ++j) {
                const glm::dvec3 pad(0.005);
                const Object &a = *parts[i], &b = *parts[j];
                if (glm::any(glm::lessThan(a.wMax + pad, b.wMin)) || glm::any(glm::lessThan(b.wMax + pad, a.wMin))) continue;
                const bool wood = materialClass(a.material) == MaterialClass::Wood && materialClass(b.material) == MaterialClass::Wood;
                std::string err;
                if (glue(a.id, b.id, wood ? "woodglue" : "construction", 0, 0, &err)) ++glued;
            }
        text += ", physics on (" + std::to_string(glued) + " glue joints)";
    }
    return true;
}

bool World::dismantle(uint32_t objId, std::string* err) {
    uint32_t planId;
    int idx;
    Object* o = find(objId);
    if (!o || !planPieceOf(objId, planId, idx)) {
        if (err) *err = "this part is not built from a template";
        return false;
    }
    BuildPlan* bp = findPlan(planId);
    PlanPiece& pc = bp->pieces[(size_t)idx];
    for (size_t k = joints_.size(); k-- > 0;)
        if (joints_[k]->a == objId || joints_[k]->b == objId) removeJoint(joints_[k]->id);
    o->unbreakable = false;
    setFixed(*o, false);
    pc.built = 0;
    pc.filled = 0;
    pc.material = -1;
    return true;
}

void World::updatePlans() {
    for (BuildPlan& bp : plans_)
        for (PlanPiece& p : bp.pieces)
            if (p.built && !find(p.built)) {  // Teil weg (gelöscht, verbrannt): wieder Hologramm
                p.built = 0;
                p.filled = 0;
                p.material = -1;
            }
}

bool templateFromParts(World& w, const std::vector<uint32_t>& ids, PlanTemplate& out) {
    std::vector<Object*> parts;
    glm::dvec3 mn(1e30), mx(-1e30);
    for (uint32_t id : ids)
        if (Object* o = w.find(id)) {
            parts.push_back(o);
            mn = glm::min(mn, o->wMin);
            mx = glm::max(mx, o->wMax);
        }
    if (parts.empty()) return false;
    const glm::dvec3 origin((mn.x + mx.x) * 0.5, mn.y, (mn.z + mx.z) * 0.5);
    out.pieces.clear();
    out.toGround = false;
    for (Object* o : parts) {
        PlanPieceDef d;
        d.center = o->pos + o->rot * ((o->bbMin + o->bbMax) * 0.5) - origin;
        d.half = (o->bbMax - o->bbMin) * 0.5;
        d.rot = o->rot;
        const MaterialClass c = materialClass(o->material);
        d.mat = c == MaterialClass::Wood ? PlanMat::Wood : c == MaterialClass::Stone ? PlanMat::Stone : c == MaterialClass::Metal ? PlanMat::Metal : PlanMat::Any;
        out.pieces.push_back(d);
    }
    return true;
}

bool planMatFits(PlanMat m, int material) { return fitsMat(m, material); }

bool planPieceFits(const PlanPiece& p, int material) {
    if (p.material >= 0) return materialClass(material) == materialClass(p.material);
    return fitsMat((PlanMat)p.mat, material);
}
