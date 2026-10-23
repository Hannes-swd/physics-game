#include "World.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <tuple>

#include "Collision.h"
#include "Materials.h"
#include "ObjLoader.h"
#include "Jobs.h"
#include "Profile.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
// Körpernummer in Jolts BodyID; die oberen Bits zählen, wie oft die Nummer schon vergeben war
constexpr BodyHandle kBodyIndexMask = 0x7fffff;

float randomSeed() {
    static std::mt19937 rng(12345);
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

glm::dmat4 toMatrix(const glm::dvec3& p, const glm::dquat& q) {
    return glm::translate(glm::dmat4(1.0), p) * glm::mat4_cast(q);
}

bool rayAabb(const glm::dvec3& o, const glm::dvec3& d, const glm::dvec3& mn, const glm::dvec3& mx) {
    double t0 = -1e30, t1 = 1e30;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-15) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
            continue;
        }
        double a = (mn[i] - o[i]) / d[i], b = (mx[i] - o[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    return t1 >= 0;
}

// Strahl gegen ein Teil im lokalen Raum
bool rayObjectLocal(const Object& o, const glm::dvec3& lo, const glm::dvec3& ld, double& t, glm::dvec3& n) {
    glm::dvec3 pad(1e-4);
    if (!rayAabb(lo, ld, o.bbMin - pad, o.bbMax + pad)) return false;
    if (o.kind == ObjKind::Solid) return solid::rayHit(o.solid, lo, ld, t, n);
    if (!o.imported) return false;
    bool hit = false;
    double best = 1e30;
    const auto& v = o.imported->vertices;
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
        glm::dvec3 a(v[i].px, v[i].py, v[i].pz), b(v[i + 1].px, v[i + 1].py, v[i + 1].pz),
            c(v[i + 2].px, v[i + 2].py, v[i + 2].pz);
        glm::dvec3 e1 = b - a, e2 = c - a;
        glm::dvec3 p = glm::cross(ld, e2);
        double det = glm::dot(e1, p);
        if (std::abs(det) < 1e-18) continue;
        double inv = 1.0 / det;
        glm::dvec3 s = lo - a;
        double u = glm::dot(s, p) * inv;
        if (u < 0 || u > 1) continue;
        glm::dvec3 q = glm::cross(s, e1);
        double w = glm::dot(ld, q) * inv;
        if (w < 0 || u + w > 1) continue;
        double tt = glm::dot(e2, q) * inv;
        if (tt >= 0 && tt < best) {
            best = tt;
            n = glm::normalize(glm::cross(e1, e2));
            if (glm::dot(n, ld) > 0) n = -n;
            hit = true;
        }
    }
    if (hit) t = best;
    return hit;
}

bool rayObject(const Object& o, const glm::dvec3& origin, const glm::dvec3& dir, double& t, glm::dvec3& n,
               glm::dvec3* lp = nullptr, glm::dvec3* ln = nullptr) {
    glm::dquat inv = glm::conjugate(o.rot);
    glm::dvec3 lo = inv * (origin - o.pos);
    glm::dvec3 ld = inv * dir;
    glm::dvec3 nl;
    if (!rayObjectLocal(o, lo, ld, t, nl)) return false;
    n = o.rot * nl;
    if (lp) *lp = lo + ld * t;
    if (ln) *ln = nl;
    return true;
}

std::vector<glm::vec3> uniquePoints(const solid::Piece& p) {
    std::map<std::tuple<long long, long long, long long>, glm::vec3> m;
    for (const auto& f : p.faces)
        for (const auto& v : f.v) {
            auto key = std::make_tuple(std::llround(v.x * 1e7), std::llround(v.y * 1e7), std::llround(v.z * 1e7));
            m.emplace(key, glm::vec3(v));
        }
    std::vector<glm::vec3> r;
    r.reserve(m.size());
    for (auto& [k, v] : m) r.push_back(v);
    return r;
}

std::string quoteFree(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

} // namespace

std::string fmtNum(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

// ============================================================================ Aufbau

World::World(Physics& physics, Renderer* renderer) : physics_(physics), renderer_(renderer) {
    // Boden: unendliche Ebene bei y = 0 aus Beton (Werkstattboden; später durch Terrain-Chunks ersetzt)
    groundBody_ = physics_.createGroundPlane(0.0, materialIndex("concrete"), 0.2f);
    if (renderer_) {
        // Bodenfläche wird jedes Bild unter die Kamera gelegt. Kleines, genaues Mittelstück (feine Rasterlinien)
        // plus Außenring bis zum Horizont (dort reicht grobe Genauigkeit).
        std::vector<solid::Vertex> v;
        auto quad = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d) {  // gegen den Uhrzeigersinn von oben
            for (glm::vec2 p : {a, b, c, a, c, d}) v.push_back({p.x, 0, p.y, 0, 1, 0});
        };
        const float r = 150.0f, e = 30000.0f;
        quad({-r, -r}, {-r, r}, {r, r}, {r, -r});
        quad({-e, -e}, {-r, -r}, {r, -r}, {e, -e});
        quad({e, -e}, {r, -r}, {r, r}, {e, e});
        quad({e, e}, {r, r}, {-r, r}, {-e, e});
        quad({-e, e}, {-r, r}, {-r, -r}, {-e, -e});
        groundMesh_ = renderer_->createMesh(v);
        solid::Solid s;
        s.pieces.push_back(solid::makeBox(glm::dvec3(0.5)));
        std::vector<solid::Vertex> b;
        solid::buildTriangles(s, b);
        boxMesh_ = renderer_->createMesh(b);
    }
}

World::~World() {
    for (auto& o : objects_) releaseAll(*o);
    objects_.clear();
    physics_.destroyBody(groundBody_);
    if (renderer_) {
        renderer_->destroyMesh(groundMesh_);
        renderer_->destroyMesh(boxMesh_);
    }
}

void World::refreshPlacement(Object& o) {
    worldBounds(o, o.wMin, o.wMax);
    grid_.place(o.id, o.wMin, o.wMax);
}

std::string World::nextName(const std::string& base) const {
    int n = 1;
    for (auto& o : objects_)
        if (o->name.rfind(base, 0) == 0) ++n;
    return n == 1 ? base : base + " " + std::to_string(n);
}

Object* World::insert(std::unique_ptr<Object> o) {
    Object* p = o.get();
    byId_[p->id] = p;
    objects_.push_back(std::move(o));
    rebuildGeometry(*p);
    rebuildBody(*p);
    return p;
}

void World::rebuildGeometry(Object& o) {
    PROFILE("geom.rebuild (all)");
    std::vector<solid::Vertex> verts;
    o.edges.clear();
    if (o.kind == ObjKind::Solid) {
        solid::buildTriangles(o.solid, verts);
        solid::buildEdges(o.solid, o.edges);
        o.volume = solid::volume(o.solid);
        solid::bounds(o.solid, o.bbMin, o.bbMax);
        buildAeroFaces(o, &o.solid, nullptr);
    } else if (o.imported) {
        verts = o.imported->vertices;
        o.volume = o.imported->volume;
        buildAeroFaces(o, nullptr, &verts);
        o.bbMin = glm::dvec3(1e30);
        o.bbMax = glm::dvec3(-1e30);
        for (auto& v : verts) {
            o.bbMin = glm::min(o.bbMin, glm::dvec3(v.px, v.py, v.pz));
            o.bbMax = glm::max(o.bbMax, glm::dvec3(v.px, v.py, v.pz));
        }
    }
    o.mass = o.volume * materials()[o.material].density;
    o.edgesValid = false;
    o.netRev = ++netRevs_;
    if (o.kind == ObjKind::Solid) {
        // Bohrungen vor der Änderung (läuft schon eine Berechnung im Hintergrund, die von davor)
        const std::vector<BoreGeom> oldBores = o.collisionPending ? o.collisionOldBores : o.coll.bores;
        o.collisionPending = false;
        if (legacyCollision_) {  // nur für Messungen: jedes Stück eine Hülle
            o.coll = CollisionShape();
            for (const auto& p : o.solid.pieces) o.coll.hulls.push_back(uniquePoints(p));
        } else if (o.hullCache && o.hullCacheMode == o.collision) {
            o.coll = *o.hullCache;  // Form unverändert (Rückgängig, Kopie, Laden mit Cache)
        } else if (asyncCollision_ && o.solid.pieces.size() > 6) {
            // Die genaue Form kann bei vielen Stücken Sekunden dauern: im Hintergrund rechnen, bis dahin eine grobe
            PROFILE("geom.collisionQuick");
            o.coll = quickCollisionShape(o.solid);
            o.collisionPending = true;
            o.collisionOldBores = oldBores;
            CollisionJob job;
            job.id = o.id;
            job.rev = o.netRev;
            job.mode = o.collision;
            job.task = std::make_shared<CollisionTask>();
            auto solid = std::make_shared<const solid::Solid>(o.solid);
            std::shared_ptr<CollisionTask> task = job.task;
            const int mode = o.collision;
            jobs::submit([solid, task, mode] {
                task->result = buildCollisionShape(*solid, mode);
                task->done = true;
            });
            collisionJobs_.push_back(std::move(job));
        } else {
            // Nachbarstücke zusammenfassen, solange keine Lücke (Loch, Nut) zugeschüttet wird; runde Bohrungen und
            // Zylinder exakt
            PROFILE("geom.collisionShape");
            o.coll = buildCollisionShape(o.solid, o.collision);
            o.hullCache = std::make_shared<const CollisionShape>(o.coll);
            o.hullCacheMode = o.collision;
        }
        if (!o.collisionPending) remapBoreWear(o, oldBores);
        o.boreRt.assign(o.coll.bores.size(), Object::BoreRuntime());
    }
    if (renderer_) {
        renderer_->destroyMesh(o.mesh);
        PROFILE("geom.gpuMesh");
        o.mesh = renderer_->createMesh(verts);
    }
    if (!o.heat.empty()) {
        PROFILE("geom.fitHeat");
        fitHeat(o);  // Temperaturgitter an die neue Form anpassen
    }
    refreshPlacement(o);
}

// Abrieb gehört zur Bohrung: nach einer Bearbeitung der gleichen Bohrung wieder zuordnen
void World::remapBoreWear(Object& o, const std::vector<BoreGeom>& oldBores) {
    if (oldBores.empty()) {  // neu, geladen oder aus Rückgängig: Werte gelten, wenn die Anzahl passt
        o.boreWear.resize(o.coll.bores.size(), 0.0);
        return;
    }
    std::vector<double> wear(o.coll.bores.size(), 0.0);
    for (size_t i = 0; i < o.coll.bores.size(); ++i)
        for (size_t k = 0; k < oldBores.size() && k < o.boreWear.size(); ++k) {
            const BoreGeom& a = o.coll.bores[i];
            const BoreGeom& b = oldBores[k];
            if (glm::length(a.center - b.center) < 0.001 && std::abs(glm::dot(a.axis, b.axis)) > 0.999 && std::abs(a.radius - b.radius) < 1e-4)
                wear[i] = o.boreWear[k];
        }
    o.boreWear = wear;
}

// Fertige Kollisionsformen aus dem Hintergrund übernehmen: Physik-Körper mit der genauen Form, die Bewegung bleibt
void World::pollCollisionJobs() {
    for (size_t k = 0; k < collisionJobs_.size();) {
        CollisionJob& job = collisionJobs_[k];
        if (!job.task->done) {
            ++k;
            continue;
        }
        Object* o = find(job.id);
        if (o && o->collisionPending && o->netRev == job.rev && o->collision == job.mode) {
            PROFILE("geom.collisionApply");
            o->coll = std::move(job.task->result);
            o->hullCache = std::make_shared<const CollisionShape>(o->coll);
            o->hullCacheMode = job.mode;
            o->collisionPending = false;
            remapBoreWear(*o, o->collisionOldBores);
            o->collisionOldBores.clear();
            o->boreRt.assign(o->coll.bores.size(), Object::BoreRuntime());
            const bool moving = o->body != kNoBody && !o->fixed && physics_.isActive(o->body);
            const glm::vec3 v = moving ? physics_.linearVelocity(o->body) : glm::vec3(0);
            const glm::vec3 w = moving ? physics_.angularVelocity(o->body) : glm::vec3(0);
            rebuildBody(*o);
            if (moving && simulating_ && o->body != kNoBody) {
                physics_.activate(o->body);
                physics_.setVelocity(o->body, v, w);
            }
        }
        if (k + 1 < collisionJobs_.size()) collisionJobs_[k] = std::move(collisionJobs_.back());
        collisionJobs_.pop_back();
    }
}

// Außenflächen für die Luftkräfte sammeln. Flächen in derselben Ebene werden zusammengefasst.
void World::buildAeroFaces(Object& o, const solid::Solid* s, const std::vector<solid::Vertex>* tris) {
    o.aero.clear();
    std::map<std::tuple<long long, long long, long long, long long>, size_t> bins;
    auto addPoly = [&](const glm::dvec3& n, double d, const glm::dvec3* poly, size_t count, double quant) {
        glm::dvec3 c(0);
        double area = 0;
        for (size_t i = 1; i + 1 < count; ++i) {
            double a = 0.5 * glm::length(glm::cross(poly[i] - poly[0], poly[i + 1] - poly[0]));
            area += a;
            c += a * (poly[0] + poly[i] + poly[i + 1]) / 3.0;
        }
        if (area < 1e-10) return;
        auto key = std::make_tuple(std::llround(n.x * quant), std::llround(n.y * quant), std::llround(n.z * quant), std::llround(d * 1000));
        auto it = bins.find(key);
        if (it == bins.end()) {
            bins[key] = o.aero.size();
            o.aero.push_back({glm::vec3(c / area), glm::vec3(n), (float)area});
        } else {
            Object::AeroFace& f = o.aero[it->second];
            double total = f.area + area;
            f.c = glm::vec3((glm::dvec3(f.c) * (double)f.area + c) / total);
            f.area = (float)total;
        }
    };
    if (s) {
        for (const auto& p : s->pieces)
            for (const auto& f : p.faces) {
                if (f.internal) continue;
                if (f.partial)
                    for (const auto& poly : f.vis) addPoly(f.n, f.d, poly.data(), poly.size(), 500.0);
                else
                    addPoly(f.n, f.d, f.v.data(), f.v.size(), 500.0);
            }
    } else if (tris) {  // importierte Netze: grob nach Richtung zusammenfassen
        for (size_t i = 0; i + 2 < tris->size(); i += 3) {
            glm::dvec3 t[3];
            for (int k = 0; k < 3; ++k) t[k] = glm::dvec3((*tris)[i + k].px, (*tris)[i + k].py, (*tris)[i + k].pz);
            glm::dvec3 n = glm::cross(t[1] - t[0], t[2] - t[0]);
            if (glm::length(n) < 1e-14) continue;
            n = glm::normalize(n);
            addPoly(glm::normalize(glm::round(n * 4.0) / 4.0 + n * 1e-6), 0.0, t, 3, 4.0);
        }
    }
}

void World::releaseBody(Object& o) {
    releaseJointsOf(o.id);  // Verbindungen zuerst lösen, dann den Körper
    if (o.hinge) physics_.destroyConstraint(o.hinge);
    o.hinge = 0;
    if (o.body != kNoBody) {
        const size_t i = o.body & kBodyIndexMask;
        if (i < byBody_.size() && byBody_[i] == &o) byBody_[i] = nullptr;
        physics_.destroyBody(o.body);
    }
    o.body = kNoBody;
}

void World::releaseAll(Object& o) {
    releaseBody(o);
    if (renderer_) renderer_->destroyMesh(o.mesh);
}

void World::rebuildBody(Object& o) {
    PROFILE("body.rebuild");
    releaseBody(o);
    o.boreInUse = false;  // neuer Körper: ohne Extra-Durchläufe, bis wieder etwas anliegt
    const Material& m = materials()[o.material];
    BodyDesc d;
    if (o.kind == ObjKind::Solid && !legacyCollision_ && o.prim != 0 && o.edits == 0) {
        d.primitive = o.prim;  // unbearbeitete Grundform: exakte Kugel/Quader/Zylinder
        d.primSize = o.primSize;
    } else if (o.kind == ObjKind::Solid) {
        d.hulls = o.coll.hulls;
        for (const CylGeom& c : o.coll.cylinders) d.cylinders.push_back({c.center, c.axis, c.radius, c.halfLength});
        for (size_t i = 0; i < o.coll.bores.size(); ++i) {
            const BoreGeom& b = o.coll.bores[i];
            const double wear = i < o.boreWear.size() ? std::min(o.boreWear[i], 0.8 * b.band) : 0.0;
            d.bores.push_back({b.center, b.axis, b.radius + wear, b.halfLength, b.band, b.openTop, b.openBottom});
            if (i < o.boreRt.size()) o.boreRt[i].applied = wear;
        }
        d.massOverride = (float)o.mass;  // exakte Masse, auch wenn die Kollisionsform vereinfacht ist
    } else if (o.imported) {
        d.hulls = o.imported->hulls;
        d.massOverride = (float)o.mass;
    }
    d.density = m.density;
    d.position = o.pos;
    d.rotation = o.rot;
    d.isStatic = o.fixed;
    d.material = o.material;
    d.lubricant = o.lubricant;
    d.restitution = m.restitution;
    glm::dvec3 ext = o.bbMax - o.bbMin;
    (void)ext;
    // CCD (Weg prüfen) schaltet beforeStep nur für schnelle Teile ein: Langsame fangen die vorausschauenden Kontakte
    // sicher ab, und Jolts CCD stößt zusammengesetzte Formen nur an einem Punkt an (falscher Drall).
    d.ccd = legacyCollision_ && std::min({ext.x, ext.y, ext.z}) < 0.03;
    o.ccd = false;
    d.userData = o.id;
    o.body = physics_.createBody(d);
    if (o.body != kNoBody) {
        const size_t i = o.body & kBodyIndexMask;
        if (i >= byBody_.size()) byBody_.resize(i + 1, nullptr);
        byBody_[i] = &o;
    }
    if (o.body == kNoBody) {
        std::fprintf(stderr, "Warning: no collision shape for '%s'\n", o.name.c_str());
        return;
    }
    if (!simulating_) {
        physics_.setVelocity(o.body, glm::vec3(0), glm::vec3(0));
        physics_.deactivate(o.body);  // im Bau-Modus ruht alles
    }
    if (o.id == inspectId_) physics_.watchContacts(o.body);
    if (o.hingeAxis >= 0 && !o.fixed) {
        glm::dvec3 axis(0);
        axis[o.hingeAxis] = 1.0;
        HingeDesc h;
        h.point = o.pos;
        h.axis = o.rot * axis;
        h.motor = std::abs(o.motorRpm) > 1e-3f;
        h.motorRpm = o.motorRpm;
        h.motorTorque = o.motorTorque;
        o.hinge = physics_.createHinge(o.body, h);
    }
    rebuildJointsOf(o.id);
}

Object* World::addSolid(solid::Solid s, int material, const glm::dvec3& framePos, const glm::dquat& frameRot,
                        const std::string& name, const std::string& shape) {
    glm::dvec3 com;
    double vol = solid::volume(s, &com);
    if (vol <= 1e-12 || s.pieces.empty()) return nullptr;
    solid::translate(s, -com);
    auto o = std::make_unique<Object>();
    o->id = nextId_++;
    o->name = name.empty() ? nextName("Part") : name;
    o->material = std::clamp(material, 0, (int)materials().size() - 1);
    o->kind = ObjKind::Solid;
    o->solid = std::move(s);
    o->pos = framePos + frameRot * com;
    o->rot = glm::normalize(frameRot);
    o->shape = shape;
    o->seed = randomSeed();
    lastCreated_ = o->id;
    return insert(std::move(o));
}

Object* World::addBox(int material, glm::dvec3 size, const glm::dvec3& pos, const std::string& name) {
    solid::Solid s;
    s.pieces.push_back(solid::makeBox(size * 0.5));
    std::string shape = "box " + fmtNum(size.x * 100) + " x " + fmtNum(size.z * 100) + " x " + fmtNum(size.y * 100) + " cm";
    const bool thin = size.y < 0.06 && std::max(size.x, size.z) > 0.2;
    const bool metal = materialClass(material) == MaterialClass::Metal;
    Object* o = addSolid(std::move(s), material, pos, glm::dquat(1, 0, 0, 0),
                         name.empty() ? nextName(thin ? (metal ? "Sheet" : "Board") : "Block") : name, shape);
    return setPrimitive(o, 1, size * 0.5);
}

Object* World::addCylinder(int material, double d, double h, const glm::dvec3& pos, const std::string& name) {
    int seg = std::clamp(int(d * 400), 24, 64);
    std::string shape = "cylinder D " + fmtNum(d * 100) + " cm, H " + fmtNum(h * 100) + " cm";
    Object* o = addSolid(solid::makeCylinder(d / 2, h, seg), material, pos, glm::dquat(1, 0, 0, 0),
                         name.empty() ? nextName(d < 0.03 && h > 0.1 ? "Rod" : "Cylinder") : name, shape);
    return setPrimitive(o, 3, glm::dvec3(d / 2, h / 2, 0));
}

Object* World::addTube(int material, double dOut, double dIn, double h, const glm::dvec3& pos, const std::string& name) {
    dIn = std::min(dIn, dOut * 0.98);
    int seg = std::clamp(int(dOut * 300), 24, 48);
    std::string shape = "tube D " + fmtNum(dOut * 100) + "/" + fmtNum(dIn * 100) + " cm, L " + fmtNum(h * 100) + " cm";
    return addSolid(solid::makeTube(dOut / 2, dIn / 2, h, seg), material, pos, glm::dquat(1, 0, 0, 0),
                    name.empty() ? nextName("Tube") : name, shape);
}

Object* World::addSphere(int material, double d, const glm::dvec3& pos, const std::string& name) {
    std::string shape = "sphere D " + fmtNum(d * 100) + " cm";
    Object* o = addSolid(solid::makeSphere(d / 2, 2), material, pos, glm::dquat(1, 0, 0, 0), name.empty() ? nextName("Sphere") : name, shape);
    return setPrimitive(o, 2, glm::dvec3(d / 2, 0, 0));
}

Object* World::setPrimitive(Object* o, int prim, const glm::dvec3& size) {
    if (!o) return o;
    o->prim = prim;
    o->primSize = size;
    if (!legacyCollision_) rebuildBody(*o);
    return o;
}

Object* World::addGear(int material, int teeth, double module, double width, double bore, const glm::dvec3& pos,
                       const std::string& name) {
    std::string shape = "gear z=" + std::to_string(teeth) + ", m=" + fmtNum(module * 1000, 1) + " mm, b=" +
                        fmtNum(width * 100) + " cm, D(pitch)=" + fmtNum(module * teeth * 100) + " cm";
    return addSolid(solid::makeGear(teeth, module, width, bore), material, pos, glm::dquat(1, 0, 0, 0),
                    name.empty() ? nextName("Gear") : name, shape);
}

Object* World::addPropeller(int material, double diameter, int blades, double pitchDeg, double chord, double thickness,
                            const glm::dvec3& pos, const std::string& name) {
    std::string shape = "propeller D " + fmtNum(diameter * 100) + " cm, " + std::to_string(blades) + " blades, " + fmtNum(pitchDeg, 0) +
                        "° at 75 %, blade " + fmtNum(chord * 100) + " cm";
    return addSolid(solid::makePropeller(diameter / 2, blades, pitchDeg, chord, thickness), material, pos, glm::dquat(1, 0, 0, 0),
                    name.empty() ? nextName("Propeller") : name, shape);
}

Object* World::addWedge(int material, double length, double height, double angleDeg, const glm::dvec3& pos, const std::string& name) {
    const double back = 2.0 * height * std::tan(glm::radians(std::clamp(angleDeg, 2.0, 120.0)) / 2.0);
    const double hx = length / 2, hy = height / 2, hz = back / 2;
    std::vector<glm::dvec3> pts = {{-hx, -hy, 0}, {hx, -hy, 0}, {-hx, hy, -hz}, {hx, hy, -hz}, {-hx, hy, hz}, {hx, hy, hz}};
    solid::Solid s;
    s.pieces.push_back(solid::hullPiece(pts));
    std::string shape = "wedge " + fmtNum(length * 100) + " cm edge, " + fmtNum(height * 100) + " cm high, " + fmtNum(angleDeg, 0) + "°";
    return addSolid(std::move(s), material, pos, glm::dquat(1, 0, 0, 0), name.empty() ? nextName("Wedge") : name, shape);
}

Object* World::addSpike(int material, double diameter, double length, double tipRadius, const glm::dvec3& pos, const std::string& name) {
    const double r = diameter / 2, rt = std::clamp(tipRadius, 0.0, r), hy = length / 2;
    const int seg = 16;  // hullPiece prüft alle Punkt-Dreiergruppen: wenige Punkte halten das schnell
    std::vector<glm::dvec3> pts;
    for (int i = 0; i < seg; ++i) {
        const double a = 2.0 * 3.14159265358979323846 * i / seg;
        pts.push_back({r * std::cos(a), hy, r * std::sin(a)});
    }
    if (rt < 1e-5) {
        pts.push_back({0, -hy, 0});
    } else {  // runde Spitze: untere Halbkugel
        const glm::dvec3 c(0, -hy + rt, 0);
        pts.push_back(c - glm::dvec3(0, rt, 0));
        for (int ring = 1; ring <= 3; ++ring) {
            const double b = 1.5707963267948966 * ring / 3.0;
            for (int i = 0; i < seg; ++i) {
                const double a = 2.0 * 3.14159265358979323846 * i / seg;
                pts.push_back(c + glm::dvec3(rt * std::sin(b) * std::cos(a), -rt * std::cos(b), rt * std::sin(b) * std::sin(a)));
            }
        }
    }
    solid::Solid s;
    s.pieces.push_back(solid::hullPiece(pts));
    std::string shape = "spike D " + fmtNum(diameter * 100) + " cm, " + fmtNum(length * 100) + " cm long, tip radius " + fmtNum(rt * 1000) + " mm";
    return addSolid(std::move(s), material, pos, glm::dquat(1, 0, 0, 0), name.empty() ? nextName("Spike") : name, shape);
}

Object* World::addImported(const std::filesystem::path& file, int material, double scale, const glm::dvec3& pos,
                           std::string* error) {
    ObjModel m;
    if (!loadObj(file, m)) {
        if (error) *error = m.error;
        return nullptr;
    }
    auto im = std::make_shared<ImportedMesh>();
    im->file = file.u8string();
    im->scale = scale;
    // Volumen und Schwerpunkt über Tetraeder (funktioniert für geschlossene Netze)
    double vol = 0;
    glm::dvec3 c(0), vc(0);
    for (size_t i = 0; i + 2 < m.vertices.size(); i += 3) {
        glm::dvec3 a(m.vertices[i].px, m.vertices[i].py, m.vertices[i].pz);
        glm::dvec3 b(m.vertices[i + 1].px, m.vertices[i + 1].py, m.vertices[i + 1].pz);
        glm::dvec3 d(m.vertices[i + 2].px, m.vertices[i + 2].py, m.vertices[i + 2].pz);
        a *= scale;
        b *= scale;
        d *= scale;
        double vt = glm::dot(a, glm::cross(b, d)) / 6.0;
        vol += vt;
        c += vt * (a + b + d) / 4.0;
        vc += a + b + d;
    }
    glm::dvec3 mn(1e30), mx(-1e30);
    for (auto& v : m.vertices) {
        mn = glm::min(mn, glm::dvec3(v.px, v.py, v.pz) * scale);
        mx = glm::max(mx, glm::dvec3(v.px, v.py, v.pz) * scale);
    }
    glm::dvec3 com;
    if (std::abs(vol) > 1e-9) {
        com = c / vol;
        vol = std::abs(vol);
    } else {
        // offenes Netz: grobe Schätzung
        com = vc / double(std::max<size_t>(1, m.vertices.size()));
        glm::dvec3 e = mx - mn;
        vol = std::max(1e-9, e.x * e.y * e.z * 0.5);
    }
    im->volume = vol;
    for (auto& v : m.vertices) {
        glm::dvec3 p = glm::dvec3(v.px, v.py, v.pz) * scale - com;
        v.px = (float)p.x;
        v.py = (float)p.y;
        v.pz = (float)p.z;
    }
    im->vertices = std::move(m.vertices);
    for (auto& g : m.groups) {
        std::vector<glm::vec3> pts;
        for (auto& p : g) pts.push_back(glm::vec3(glm::dvec3(p) * scale - com));
        im->hulls.push_back(std::move(pts));
    }
    if (im->hulls.empty()) {
        std::vector<glm::vec3> pts;
        for (auto& v : im->vertices) pts.emplace_back(v.px, v.py, v.pz);
        im->hulls.push_back(std::move(pts));
    }

    auto o = std::make_unique<Object>();
    o->id = nextId_++;
    o->name = nextName(m.name.empty() ? "Model" : m.name);
    o->material = material;
    o->kind = ObjKind::Mesh;
    o->imported = im;
    glm::dvec3 e = mx - mn;
    o->pos = pos.y < -1e8 ? findFreeSpot(e * 0.5, glm::dvec3(0)) + (com - (mn + mx) * 0.5) : pos;
    o->shape = "import " + file.filename().u8string() + " (" + fmtNum(e.x * 100) + " x " + fmtNum(e.z * 100) + " x " +
               fmtNum(e.y * 100) + " cm)";
    o->seed = randomSeed();
    lastCreated_ = o->id;
    return insert(std::move(o));
}

glm::dvec3 World::findFreeSpot(const glm::dvec3& half, const glm::dvec3& ref) const {
    const double step = std::max(0.05, std::max(half.x, half.z) * 2 + 0.05);
    for (int ring = 0; ring < 30; ++ring) {
        for (int i = -ring; i <= ring; ++i)
            for (int j = -ring; j <= ring; ++j) {
                if (std::max(std::abs(i), std::abs(j)) != ring) continue;
                glm::dvec3 c(ref.x + i * step, half.y, ref.z + j * step);
                bool free = true;
                for (auto& o : objects_) {
                    glm::dvec3 mn, mx;
                    worldBounds(*o, mn, mx);
                    if (mn.y > half.y * 2 + 0.01) continue;
                    if (c.x + half.x + 0.02 > mn.x && c.x - half.x - 0.02 < mx.x && c.z + half.z + 0.02 > mn.z &&
                        c.z - half.z - 0.02 < mx.z) {
                        free = false;
                        break;
                    }
                }
                if (free) return c;
            }
    }
    return glm::dvec3(ref.x, half.y, ref.z);
}

// ============================================================================ Zugriff

Object* World::find(uint32_t id) {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : it->second;
}

Object* World::fromBody(BodyHandle b) {
    if (b == kNoBody) return nullptr;
    const size_t i = b & kBodyIndexMask;
    Object* o = i < byBody_.size() ? byBody_[i] : nullptr;
    return o && o->body == b ? o : nullptr;  // gleicher Index, aber ein neuerer Körper: nicht dieses Teil
}

void World::worldBounds(const Object& o, glm::dvec3& mn, glm::dvec3& mx) const {
    mn = glm::dvec3(1e30);
    mx = glm::dvec3(-1e30);
    for (int i = 0; i < 8; ++i) {
        glm::dvec3 c((i & 1) ? o.bbMax.x : o.bbMin.x, (i & 2) ? o.bbMax.y : o.bbMin.y, (i & 4) ? o.bbMax.z : o.bbMin.z);
        glm::dvec3 w = o.pos + o.rot * c;
        mn = glm::min(mn, w);
        mx = glm::max(mx, w);
    }
}

// ============================================================================ Ändern

void World::remove(uint32_t id) {
    for (size_t k = joints_.size(); k-- > 0;)
        if (joints_[k]->a == id || joints_[k]->b == id) removeJoint(joints_[k]->id);
    for (size_t i = 0; i < objects_.size(); ++i)
        if (objects_[i]->id == id) {
            releaseAll(*objects_[i]);
            objects_.erase(objects_.begin() + i);
            grid_.remove(id);
            byId_.erase(id);
            return;
        }
}

void World::clear() {
    for (auto& j : joints_)
        if (j->constraint) physics_.destroyConstraint(j->constraint);
    joints_.clear();
    for (auto& o : objects_) releaseAll(*o);
    objects_.clear();
    grid_.clear();
    byId_.clear();
    byBody_.clear();
    nextId_ = 1;  // neue Szene: Nummern wieder ab 1 (Laden/Rückgängig setzen sie danach selbst)
    nextJointId_ = 1;
    lastCreated_ = 0;
    brokenCount_ = 0;  // Zähler gelten für die Szene
    rescues_ = 0;
    rescueLog_.clear();
    stuckPairs_.clear();
    fires_.clear();
    heatSources_.clear();
    frictionHeat_.clear();
    waters_.clear();
    nextWaterId_ = 1;
    flowGrids_.clear();
    splashes_.clear();
    steam_.clear();
}

Object* World::duplicate(uint32_t id, const glm::dvec3& offset) {
    Object* src = find(id);
    if (!src) return nullptr;
    auto o = std::make_unique<Object>();
    static_cast<ObjectState&>(*o) = static_cast<const ObjectState&>(*src);
    o->id = nextId_++;
    o->name = nextName(src->name + " copy");
    o->pos += offset;
    o->seed = randomSeed();
    lastCreated_ = o->id;
    return insert(std::move(o));
}

void World::setTransform(Object& o, const glm::dvec3& pos, const glm::dquat& rot) {
    o.pos = pos;
    o.rot = glm::normalize(rot);
    o.hasSafe = false;  // neue Lage: erst prüfen, ob sie frei ist
    if (o.hinge) {
        rebuildBody(o);  // Achse hängt an der Weltposition
    } else {
        physics_.setTransform(o.body, o.pos, o.rot, simulating_);
        if (!simulating_) {
            physics_.setVelocity(o.body, glm::vec3(0), glm::vec3(0));
            rebuildJointsOf(o.id);  // im Bau-Modus verschoben: Verbindung in der neuen Lage festhalten
        }
    }
    refreshPlacement(o);
}

void World::setMaterial(Object& o, int material) {
    o.material = std::clamp(material, 0, (int)materials().size() - 1);
    o.mass = o.volume * materials()[o.material].density;
    if (!o.heat.empty()) {  // neues Material: unverbrannt, Wärmekapazität neu
        for (HeatCell& c : o.heat.cells) {
            c.fuel = combustible(o.material) ? 1.0f : 0.0f;
            c.chr = c.melt = 0.0f;
            c.burning = 0;
        }
        fitHeat(o);
    }
    rebuildBody(o);
}

void World::applyMaterials(const std::vector<int>& remap) {
    const int n = (int)materials().size();
    auto map = [&](int m) { return std::clamp(m >= 0 && m < (int)remap.size() ? remap[m] : m, 0, n - 1); };
    for (auto& op : objects_) {
        Object& o = *op;
        o.material = map(o.material);
        o.mass = o.volume * materials()[o.material].density;
        if (!o.heat.empty()) fitHeat(o);  // Masse je Zelle
        rebuildBody(o);                   // Dichte, Reibung, Stoßzahl; Verbindungen neu berechnet
    }
    for (std::vector<Snapshot>* list : {&undo_, &redo_})
        for (Snapshot& s : *list)
            for (ObjectState& st : s.objects) st.material = map(st.material);
}

void World::setFixed(Object& o, bool fixed) {
    o.fixed = fixed;
    rebuildBody(o);
}

void World::setHinge(Object& o, int axis, float rpm, float torque) {
    o.hingeAxis = axis;
    o.motorRpm = rpm;
    o.motorTorque = torque;
    rebuildBody(o);
}

void World::setCollisionMode(Object& o, int mode) {
    o.collision = std::clamp(mode, 0, 2);
    rebuildGeometry(o);
    rebuildBody(o);
}

void World::dropToSurface(Object& o) {
    glm::dvec3 mn, mx;
    worldBounds(o, mn, mx);
    double best = mn.y;  // Abstand bis zum Boden (y = 0)
    const int N = 9;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            double x = mn.x + (mx.x - mn.x) * (i + 0.5) / N;
            double z = mn.z + (mx.z - mn.z) * (j + 0.5) / N;
            // Unterseite des Teils an dieser Stelle finden
            double t;
            glm::dvec3 n;
            glm::dvec3 from(x, mn.y - 1.0, z);
            if (!rayObject(o, from, glm::dvec3(0, 1, 0), t, n)) continue;
            double yBottom = from.y + t;
            best = std::min(best, yBottom);
            RayHit hit;
            if (raycast(glm::dvec3(x, yBottom - 1e-6, z), glm::dvec3(0, -1, 0), hit, o.id))
                best = std::min(best, hit.t);
        }
    if (best > 1e-7) setTransform(o, o.pos - glm::dvec3(0, best, 0), o.rot);
}

// ============================================================================ Werkzeuge

bool World::replaceWithParts(Object& o, std::vector<solid::Solid>& parts, std::vector<uint32_t>* resultIds) {
    if (parts.empty()) return false;
    std::sort(parts.begin(), parts.end(),
              [](const solid::Solid& a, const solid::Solid& b) { return solid::volume(a) > solid::volume(b); });
    {
        PROFILE("geom.internalFaces");
        for (auto& p : parts) solid::classifyInternalFaces(p);
    }
    const glm::dvec3 framePos = o.pos;
    const glm::dquat frameRot = o.rot;
    const uint32_t id = o.id;
    const std::string baseName = o.name;
    const int mat = o.material;
    const bool fixed = o.fixed;
    const std::string shape = o.shape;
    const int edits = o.edits + 1;
    const float seed = o.seed;
    const HeatGrid heat = o.heat;  // Temperatur gilt im alten Koordinatensystem des Teils
    const MeltForm meltForm = o.meltForm;

    // größtes Stück bleibt das ursprüngliche Teil
    glm::dvec3 com;
    solid::volume(parts[0], &com);
    solid::translate(parts[0], -com);
    o.solid = std::move(parts[0]);
    o.hullCache.reset();
    o.pos = framePos + frameRot * com;
    o.edits = edits;
    o.heat.origin -= com;
    for (auto& j : joints_) {  // Anker liegen relativ zum (verschobenen) Schwerpunkt
        if (j->a == id) j->anchorA -= com;
        if (j->b == id) j->anchorB -= com;
    }
    for (auto& b : o.coll.bores) b.center -= com;  // damit der Abrieb seiner Bohrung zugeordnet bleibt
    const int lub = o.lubricant;
    rebuildGeometry(o);
    rebuildBody(o);
    if (resultIds) resultIds->push_back(id);

    for (size_t i = 1; i < parts.size(); ++i) {
        Object* n = addSolid(std::move(parts[i]), mat, framePos, frameRot, baseName + " (piece " + std::to_string(i + 1) + ")", shape);
        if (!n) continue;
        n->edits = edits;
        n->seed = seed;  // gleiche Maserung wie das Original
        n->meltForm = meltForm;
        if (!heat.empty()) {  // Stücke behalten die Temperatur der Stelle, an der sie waren
            n->heat = heat;
            n->heat.origin -= glm::conjugate(frameRot) * (n->pos - framePos);
            fitHeat(*n);
        }
        if (fixed) setFixed(*n, true);
        if (lub) setLubricant(*n, lub);
        if (resultIds) resultIds->push_back(n->id);
    }
    return true;
}

bool World::cut(uint32_t id, const solid::Plane& pl, double kerf, std::vector<uint32_t>* resultIds, std::string* err) {
    Object* o = find(id);
    if (!o) {
        if (err) *err = "part not found";
        return false;
    }
    if (o->kind != ObjKind::Solid) {
        if (err) *err = "imported models cannot be cut (yet)";
        return false;
    }
    std::vector<solid::Solid> parts;
    if (kerf <= 1e-6) {
        solid::Solid below, above;
        solid::split(o->solid, pl, below, above);
        if (below.pieces.empty() || above.pieces.empty()) {
            if (err) *err = "the cut does not hit the part";
            return false;
        }
        for (auto& c : solid::connectedComponents(below)) parts.push_back(std::move(c));
        for (auto& c : solid::connectedComponents(above)) parts.push_back(std::move(c));
    } else {
        double removed = 0;
        solid::Solid res = solid::subtract(o->solid, solid::slabTool(pl.n, pl.d, kerf), &removed);
        if (removed < 1e-12) {
            if (err) *err = "the cut does not hit the part";
            return false;
        }
        parts = solid::connectedComponents(res);
    }
    return replaceWithParts(*o, parts, resultIds);
}

bool World::drill(uint32_t id, const glm::dvec3& from, const glm::dvec3& dir, double diameter, double depth,
                  std::vector<uint32_t>* resultIds, double* removedVolume, std::string* err) {
    Object* o = find(id);
    if (!o) {
        if (err) *err = "part not found";
        return false;
    }
    if (o->kind != ObjKind::Solid) {
        if (err) *err = "imported models cannot be drilled (yet)";
        return false;
    }
    glm::dvec3 ext = o->bbMax - o->bbMin;
    double length = depth > 0 ? depth : glm::length(ext) * 2 + 0.01;
    int seg = std::clamp(int(diameter * 2000), 16, 48);
    auto tool = solid::drillTool(from, dir, diameter / 2, length, seg);
    double removed = 0;
    solid::Solid res = solid::subtract(o->solid, tool, &removed);
    if (removed < 1e-12) {
        if (err) *err = "the drill does not hit any material";
        return false;
    }
    if (removedVolume) *removedVolume = removed;
    auto parts = solid::connectedComponents(res);
    if (parts.empty()) {
        if (err) *err = "the part was drilled away completely";
        remove(id);
        return true;
    }
    return replaceWithParts(*o, parts, resultIds);
}

bool World::applyResult(Object& o, solid::Solid s, std::vector<uint32_t>* resultIds, std::string* err) {
    std::vector<solid::Solid> parts;
    {
        PROFILE("geom.connectedParts");
        parts = solid::connectedComponents(s);
    }
    if (parts.empty()) {
        if (err) *err = "nothing would be left of the part";
        return false;
    }
    return replaceWithParts(o, parts, resultIds);
}

// ============================================================================ Bau-Modus: nicht ineinander

std::vector<std::pair<uint32_t, double>> World::overlapsOf(const Object& o, double minDepth) {
    std::vector<std::pair<uint32_t, double>> r;
    if (o.body == kNoBody) return r;
    for (const OverlapHit& h : physics_.overlaps(o.body, minDepth, {})) {
        Object* p = fromBody(h.body);
        r.emplace_back(p ? p->id : 0u, h.depth);
    }
    std::sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.second > b.second; });
    return r;
}

glm::dvec3 World::sweepMove(const std::vector<uint32_t>& ids, const glm::dvec3& delta, uint32_t* blocker, bool* blocked) {
    std::vector<Object*> objs;
    std::vector<BodyHandle> group;
    for (uint32_t id : ids)
        if (Object* o = find(id)) {
            objs.push_back(o);
            if (o->body != kNoBody) group.push_back(o->body);
        }
    if (blocker) *blocker = 0;
    if (blocked) *blocked = false;
    glm::dvec3 moved(0), rest = delta;
    const double gap = 2e-5;  // 0,02 mm Luft lassen, damit das Teil nicht schon beim nächsten Zug klemmt
    for (int iter = 0; iter < 3 && glm::length(rest) > 1e-7; ++iter) {
        double frac = 1.0;
        glm::dvec3 normal(0);
        BodyHandle hitBody = kNoBody;
        for (Object* o : objs) {
            if (o->body == kNoBody) continue;
            SweepHit h = physics_.sweep(o->body, rest, group);
            if (h.hit && h.fraction < frac) {
                frac = h.fraction;
                normal = h.normal;
                hitBody = h.body;
            }
        }
        const double len = glm::length(rest);
        glm::dvec3 step = rest * std::max(0.0, frac - (frac < 1.0 ? gap / len : 0.0));
        if (glm::length(step) > 1e-9)
            for (Object* o : objs) setTransform(*o, o->pos + step, o->rot);
        moved += step;
        if (frac >= 1.0) break;
        if (blocked) *blocked = true;
        if (blocker) {
            Object* b = fromBody(hitBody);
            *blocker = b ? b->id : 0u;
        }
        // am Hindernis entlanggleiten: Anteil in das Hindernis hinein wegnehmen
        glm::dvec3 remain = rest - step;
        double into = glm::dot(remain, normal);
        if (into < 0) remain -= normal * into;
        rest = remain;
    }
    return moved;
}

// ============================================================================ Physik

void World::setSimulating(bool on) {
    if (on == simulating_) return;
    simulating_ = on;
    for (auto& o : objects_) {
        if (o->body == kNoBody || o->fixed) continue;
        physics_.setVelocity(o->body, glm::vec3(0), glm::vec3(0));
        o->hasSafe = false;
        if (on) physics_.activate(o->body);
        else physics_.deactivate(o->body);
    }
    if (!on) return;
    // Teile, die im Bau-Modus ineinander geschoben wurden: melden (die Physik schiebt sie langsam auseinander)
    int reported = 0;
    for (auto& o : objects_) {
        if (o->body == kNoBody || o->fixed || reported >= 5) continue;
        for (auto& [other, depth] : overlapsOf(*o, 0.002)) {
            if (other != 0 && other < o->id && find(other) && !find(other)->fixed) continue;  // Paar nur einmal
            bool joined = false;
            for (auto& j : joints_) joined |= (j->a == o->id && j->b == other) || (j->b == o->id && j->a == other);
            if (joined) continue;
            Object* p = find(other);
            events_.push_back("note: " + o->name + " sticks " + fmtNum(depth * 1000, 1) + " mm into " + (p ? p->name : std::string("the floor")) +
                              " - pushing them apart slowly");
            if (++reported >= 5) break;
        }
    }
}

void World::syncObject(Object& o) {
    if (o.body == kNoBody || o.fixed) return;
    glm::dvec3 p;
    glm::dquat q;
    physics_.getTransform(o.body, p, q);
    bool moved = glm::length(p - o.pos) > 1e-7 || std::abs(glm::dot(q, o.rot)) < 1.0 - 1e-12;
    o.pos = p;
    o.rot = q;
    if (moved) refreshPlacement(o);  // ggf. in einen anderen Chunk wechseln
}

void World::syncFromPhysics() {
    for (auto& o : objects_) syncObject(*o);
}

// Wie syncFromPhysics, aber nur die Teile, die sich bewegt haben können: wach nach dem Schritt oder im Schritt
// eingeschlafen (awake_ aus beforeStep). Schlafende Teile liegen still; Lageänderungen von außen setzen o.pos selbst.
void World::syncAwake() {
    physics_.activeBodies(awakeNow_);
    std::sort(awakeNow_.begin(), awakeNow_.end());
    for (BodyHandle b : awakeNow_)
        if (Object* o = fromBody(b)) syncObject(*o);
    for (BodyHandle b : awake_)
        if (!std::binary_search(awakeNow_.begin(), awakeNow_.end(), b))
            if (Object* o = fromBody(b)) syncObject(*o);
}

void World::step(float dt, const std::function<void(float)>& preStep) {
    physics_.step(
        dt,
        [&](float h) {
            extForce_ = glm::dvec3(0);
            if (preStep) preStep(h);
            beforeStep(h);
        },
        [&](float h) { afterStep(h); });
    syncAwake();
}

void World::simulateSeconds(double seconds, bool wakeAll) {
    if (wakeAll)
        for (auto& o : objects_)
            if (o->body != kNoBody && !o->fixed) physics_.activate(o->body);
    int steps = (int)std::ceil(seconds / physics_.fixedDt());
    for (int i = 0; i < steps; ++i)
        physics_.step(
            physics_.fixedDt(),
            [&](float h) {
                extForce_ = glm::dvec3(0);
                beforeStep(h);
            },
            [&](float h) { afterStep(h); });
    syncAwake();
}

// Luft: Auftrieb (Archimedes) und Strömungswiderstand für alle bewegten Teile
// Luftdichte bei 20 °C: 1,204 kg/m³ (einstellbar mit setAir)
static constexpr double kG = 9.81;

// Luft- bzw. Wasserkraft auf eine Fläche (Plattenmodell): Normalkraft je nach Anstellwinkel + Reibung entlang der Fläche.
// C_N = 2π·sinα / (1 + 4,2·sinα): bei kleinen Winkeln wie ein Flügel (2π·α), quer angeströmt ≈ 1,2.
void plateForce(const glm::dvec3& vRel, const glm::dvec3& n, double area, double rho, glm::dvec3& out) {
    const double sp2 = glm::dot(vRel, vRel);
    if (sp2 < 1e-10) return;
    const double vn = glm::dot(vRel, n);
    if (vn > 0) {  // Fläche bewegt sich gegen die Luft (Luv-Seite)
        const double sp = std::sqrt(sp2), sa = vn / sp;
        const double cn = 2.0 * kPi * sa / (1.0 + 4.2 * sa);
        out -= 0.5 * rho * sp2 * area * cn * n;
    }
    const glm::dvec3 vt = vRel - vn * n;  // Reibung der Luft an der Oberfläche (c_f ≈ 0,004)
    out -= 0.5 * rho * 0.004 * glm::length(vt) * vt * area;
}

void World::setAir(double density, const glm::dvec3& wind) {
    airDensity_ = std::max(0.0, density);
    wind_ = wind;
    if (glm::length(wind_) > 0.01)  // Wind erfasst auch ruhende Teile
        for (auto& o : objects_)
            if (o->body != kNoBody && !o->fixed && simulating_) physics_.activate(o->body);
}

void World::beforeStep(float h) {
    PROFILE("step.before (air, water)");
    std::vector<uint64_t> noContact;  // steckengebliebene Werkzeuge berühren ihren Abdruck nicht
    for (const auto& j : joints_)
        if (j->type == JointType::Glue && j->preset == kStuckPreset)
            noContact.push_back(j->a < j->b ? (uint64_t(j->a) << 32) | j->b : (uint64_t(j->b) << 32) | j->a);
    physics_.setNoContactPairs(std::move(noContact));
    aeroDrag_ = glm::dvec3(0);
    aeroTorque_ = glm::dvec3(0);
    const double rho = airDensity_;
    // Nur wache Teile (ruhende bekommen keine Luftkraft). Wer im letzten Schritt wach war, startet bei 0: schläft es
    // jetzt, bleibt seine Luftkraft 0.
    for (BodyHandle b : awake_)
        if (Object* o = fromBody(b)) o->aeroForce = o->aeroTorque = glm::dvec3(0);
    physics_.activeBodies(awake_);
    {
        PROFILE("step.water");
        waterSteps(h);  // Wasser: Auftrieb, Widerstand, Hohlräume (Water.cpp), auf allen Kernen
    }
    for (BodyHandle b : awake_) {
        Object* op = fromBody(b);
        if (!op || op->fixed) continue;
        Object& o = *op;
        o.aeroForce = o.aeroTorque = glm::dvec3(0);
        const glm::dvec3 v = glm::dvec3(physics_.linearVelocity(o.body));
        const glm::dvec3 w = glm::dvec3(physics_.angularVelocity(o.body));
        // Teil mit Bohrung in Bewegung: Kontakte an der runden Wand jeden Schritt neu (Jolt würde sie sonst bei kleinen
        // Drehungen weiterverwenden, die Punkte wandern aber auf der Wand)
        if (o.boreInUse && (glm::length(v) > 1e-4 || glm::length(w) > 1e-3)) physics_.invalidateContacts(o.body);
        // Was unter Wasser ist, hat keinen Auftrieb und Widerstand in der Luft
        const bool wet = o.water.step == waterSteps_;
        const glm::dvec3 buoy(0, rho * (o.volume - (wet ? o.water.solidSub : 0.0)) * kG, 0);  // Auftrieb in der Luft (Archimedes)
        glm::dvec3 force(0), torque(0);
        const double reach = 0.5 * glm::length(o.bbMax - o.bbMin);
        if (!legacyCollision_) {
            // Ab ca. 3 m/s (2 cm Vorausschau je Schritt reichen bis 4,8 m/s) zusätzlich den Weg prüfen
            const double speed = glm::length(v) + glm::length(w) * reach;
            // Teile mit Bohrungen nie: der Weg-Test schiebt sie geradlinig und bleibt am eigenen Stift hängen. Ebenso der
            // Stift selbst, solange er an einer Lochwand anliegt: der Weg-Test trifft die Wand direkt vor ihm, der Stift
            // bleibt stehen und das Teil mit der Bohrung rutscht über ihn hinweg (Achse fällt aus dem gezogenen Wagen)
            const bool fast = o.coll.bores.empty() && simTime_ >= o.inBoreUntil && (o.ccd ? speed > 2.0 : speed > 3.0);
            if (fast != o.ccd) {
                o.ccd = fast;
                physics_.setContinuous(o.body, fast);
            }
        }
        if (rho > 0 && glm::length(v - wind_) + glm::length(w) * reach > 0.02) {
            const glm::dmat3 rot = glm::mat3_cast(o.rot);
            const bool faceWet = wet && o.water.wet.size() == o.aero.size();
            for (size_t i = 0; i < o.aero.size(); ++i) {
                const Object::AeroFace& f = o.aero[i];
                const double dry = faceWet ? 1.0 - o.water.wet[i] : 1.0;
                if (dry <= 0) continue;
                const glm::dvec3 r = rot * glm::dvec3(f.c), n = rot * glm::dvec3(f.n);
                glm::dvec3 df(0);
                plateForce(v + glm::cross(w, r) - wind_, n, f.area * dry, rho, df);
                force += df;
                torque += glm::cross(r, df);
            }
        }
        o.aeroForce = force;
        o.aeroTorque = torque;
        physics_.addForceNoWake(o.body, glm::vec3(force + buoy));
        if (glm::length(torque) > 0) physics_.addTorqueNoWake(o.body, glm::vec3(torque));
        if (o.id == inspectId_) {
            aeroDrag_ = force + o.water.drag;
            aeroTorque_ = torque;
        }
    }
    if (Object* o = find(inspectId_)) {
        BodyState s;
        if (physics_.bodyState(o->body, s)) velBefore_ = s.vel;
    }
}

// Steckt ein Teil tief in einem anderen (z.B. vom Spieler hineingedrückt, sehr schnell gedreht), wird es an seine
// letzte freie Stelle zurückgesetzt und verliert den Schwung in das Hindernis hinein. Kein Zittern, kein Herausschleudern.
void World::handleDeepContacts() {
    if (legacyCollision_) return;
    const auto& cs = physics_.stepContacts();
    std::unordered_map<BodyHandle, float> pen;
    bool anyDeep = false;
    for (const StepContact& c : cs) {
        if (c.depth <= 0) continue;
        float& a = pen[c.a];
        a = std::max(a, c.depth);
        float& b = pen[c.b];
        b = std::max(b, c.depth);
        anyDeep |= c.depth > 0.004f;
    }
    if (anyDeep) {
        std::vector<uint32_t> moved;
        for (const StepContact& c : cs) {
            if (c.depth <= 0.004f) continue;
            Object* A = fromBody(c.a);
            Object* B = fromBody(c.b);
            if (!A && !B) continue;
            auto minExt = [](const Object* o) {
                glm::dvec3 e = o->bbMax - o->bbMin;
                return std::min({e.x, e.y, e.z});
            };
            double thr = 1e30;
            if (A) thr = std::min(thr, minExt(A));
            if (B) thr = std::min(thr, minExt(B));
            thr = std::max(0.004, 0.25 * thr);
            if (c.depth < thr) continue;
            const uint32_t ia = A ? A->id : 0, ib = B ? B->id : 0;
            const uint64_t key = (uint64_t(std::min(ia, ib)) << 32) | std::max(ia, ib);
            auto st = stuckPairs_.find(key);
            if (st != stuckPairs_.end() && simTime_ < st->second) continue;
            bool joined = false;
            for (auto& j : joints_) joined |= (j->a == ia && j->b == ib) || (j->a == ib && j->b == ia);
            if (joined) continue;
            // Beide beweglichen Teile einen Schritt zurück an ihre letzte freie Stelle (gemeinsam, sonst schiebt man das
            // eine in den Platz, den das andere inzwischen einnimmt). Nur wenn diese Stellen wirklich frei sind.
            // Teile mit Verbindungen nicht einzeln versetzen: das verbiegt die verklebte/verschraubte Baugruppe
            auto hasJoints = [&](const Object* o) {
                for (const auto& j : joints_)
                    if (j->a == o->id || j->b == o->id) return true;
                return false;
            };
            auto usable = [&](Object* o) {
                return o && !o->fixed && o->hasSafe && !hasJoints(o) && std::find(moved.begin(), moved.end(), o->id) == moved.end();
            };
            std::vector<std::pair<Object*, glm::dvec3>> victims;  // Teil, Richtung in das Hindernis
            if (usable(A)) victims.emplace_back(A, glm::dvec3(c.normal));
            if (usable(B)) victims.emplace_back(B, -glm::dvec3(c.normal));
            if (victims.empty()) continue;
            struct Pose {
                glm::dvec3 p;
                glm::dquat q;
            };
            std::vector<Pose> before;
            std::vector<BodyHandle> group;
            for (auto& [o, into] : victims) {
                before.push_back({o->pos, o->rot});
                group.push_back(o->body);
                physics_.setTransform(o->body, o->safePos, o->safeRot, false);
            }
            bool free = true;
            for (auto& [o, into] : victims)
                if (!physics_.overlaps(o->body, 0.002, group).empty()) free = false;
            if (!free) {  // dort ist inzwischen etwas anderes: lieber sanft trennen lassen
                for (size_t i = 0; i < victims.size(); ++i) physics_.setTransform(victims[i].first->body, before[i].p, before[i].q, false);
                continue;
            }
            for (auto& [o, into] : victims) {
                glm::dvec3 v = glm::dvec3(physics_.linearVelocity(o->body));
                if (glm::dot(v, into) > 0) v -= into * glm::dot(v, into);  // Schwung in das Hindernis hinein wegnehmen
                physics_.activate(o->body);
                physics_.setVelocity(o->body, glm::vec3(v), glm::vec3(physics_.angularVelocity(o->body)) * 0.5f);
                o->pos = o->safePos;
                o->rot = o->safeRot;
                refreshPlacement(*o);
                moved.push_back(o->id);
                pen[o->body] = 0;
            }
            ++rescues_;
            auto& log = rescueLog_[key];
            if (simTime_ - log.first > 1.0) log = {simTime_, 0};
            if (++log.second > 4) {  // klemmt immer wieder: dann nur noch sanft auseinanderschieben
                stuckPairs_[key] = simTime_ + 3.0;
                log = {simTime_, 0};
                const bool melt = (A && A->meltForm != MeltForm::None) || (B && B->meltForm != MeltForm::None);  // Schmelze fließt an
                if (!melt)
                    events_.push_back("note: " + (A ? A->name : std::string("floor")) + " and " + (B ? B->name : std::string("floor")) +
                                      " keep getting stuck - pushing them apart slowly");
            }
        }
    }
    // freie Stellen merken (Teil steckt höchstens 1,5 mm in etwas). Nur wache Teile: ruhende liegen still.
    for (BodyHandle b : awakeNow_) {
        Object* op = fromBody(b);
        if (!op || op->fixed) continue;
        Object& o = *op;
        auto it = pen.find(o.body);
        if (it != pen.end() && it->second > 0.0015f) continue;
        o.safePos = o.pos;
        o.safeRot = o.rot;
        o.hasSafe = true;
    }
}

void World::afterStep(float h) {
    simTime_ += h;
    waterTime_ += h;
    flowAccum_ += h;
    if (flowAccum_ >= 0.05) {  // Strömung 20-mal je Sekunde
        PROFILE("step.flow");
        updateFlow(flowAccum_);
        flowAccum_ = 0;
    }
    {
        PROFILE("step.sync+deepContacts");
        syncAwake();  // aktuelle Lage, bevor Verbindungen und Brüche ausgewertet werden
        handleDeepContacts();
    }
    {
        PROFILE("step.joints");
        checkJoints(h);
    }
    {
        PROFILE("step.impacts");
        processImpacts();
    }
    {
        PROFILE("step.bores");
        processBores(h);
    }
    if (heatOn_) {  // Wärme: 20 Schritte je Sekunde (im Zeitraffer jeweils länger)
        PROFILE("step.heat");
        collectFrictionHeat(h);
        heatAccum_ += h;
        if (heatAccum_ >= 0.05) {
            heatAccum_ -= 0.05;
            stepHeat(0.05 * heatSpeed_);
        }
    }
    Object* o = find(inspectId_);
    if (!o || o->body == kNoBody) return;
    BodyState s;
    if (!physics_.bodyState(o->body, s)) return;
    const double m = s.dynamic ? s.mass : o->mass;
    const double k = inspect_.valid ? 0.12 : 1.0;  // Glättung der Anzeige
    glm::dvec3 acc = s.dynamic ? (s.vel - velBefore_) / (double)h : glm::dvec3(0);
    inspect_.acc += (acc - inspect_.acc) * k;
    inspect_.fGravity = glm::dvec3(0, -m * kG, 0);
    inspect_.fBuoyancy = glm::dvec3(0, airDensity_ * (o->volume - o->water.solidSub) * kG, 0) + o->water.buoyancy;
    inspect_.fDrag += (aeroDrag_ - inspect_.fDrag) * k;
    inspect_.tAero += (aeroTorque_ - inspect_.tAero) * k;
    inspect_.fExternal += (extForce_ - inspect_.fExternal) * k;
    inspect_.fNet = m * inspect_.acc;
    inspect_.fContact = inspect_.fNet - inspect_.fGravity - inspect_.fBuoyancy - inspect_.fDrag - inspect_.fExternal;
    if (s.active || inspect_.contacts.empty()) inspect_.contacts = physics_.watchedContacts();
    inspect_.valid = true;
}

void World::setInspect(uint32_t id) {
    if (id == inspectId_) return;
    inspectId_ = id;
    inspect_ = InspectData{};
    inspect_.id = id;
    Object* o = find(id);
    physics_.watchContacts(o ? o->body : kNoBody);
}

void World::addExternalForce(uint32_t id, const glm::dvec3& f) {
    if (id == inspectId_) extForce_ += f;
}

std::string World::inspectText(uint32_t id) {
    Object* o = find(id);
    if (!o) return "part not found";
    const Material& mat = materials()[o->material];
    BodyState s;
    bool hasBody = physics_.bodyState(o->body, s);
    const bool live = inspect_.valid && inspectId_ == id;
    const double m = (hasBody && s.dynamic) ? s.mass : o->mass;
    auto v3 = [](const glm::dvec3& v, int d) { return "(" + fmtNum(v.x, d) + ", " + fmtNum(v.y, d) + ", " + fmtNum(v.z, d) + ")"; };
    auto line = [](std::ostringstream& ss, const char* label, const std::string& val) {
        ss << "  " << label;
        size_t chars = 0;  // UTF-8: nur Zeichenanfänge zählen
        for (const char* c = label; *c; ++c) chars += ((unsigned char)*c & 0xC0) != 0x80;
        for (size_t i = chars; i < 24; ++i) ss << ' ';
        ss << val << "\n";
    };
    std::ostringstream ss;
    glm::dvec3 vel = hasBody ? s.vel : glm::dvec3(0), w = hasBody ? s.angVel : glm::dvec3(0);
    glm::dvec3 com = hasBody ? s.com : o->pos;

    ss << "GENERAL\n";
    line(ss, "Part", "#" + std::to_string(o->id) + " " + o->name);
    line(ss, "Material", std::string(mat.name) + " (friction static " + fmtNum(mat.frictionStatic, 2) + " / sliding " +
                             fmtNum(mat.frictionKinetic, 2) + ", restitution " + fmtNum(mat.restitution, 2) + ")");
    std::string state;
    if (o->fixed) state = "fixed (immovable)";
    else if (!simulating_) state = "build mode (physics paused)";
    else if (!s.active) state = "resting (asleep)";
    else if (glm::length(vel) < 0.01 && glm::length(w) < 0.05) state = "active, almost at rest";
    else state = "moving";
    line(ss, "State", state);
    ChunkKey ck;
    if (grid_.keyOfObject(o->id, ck)) line(ss, "Chunk", std::to_string(ck.x) + " " + std::to_string(ck.y) + " " + std::to_string(ck.z));

    ss << "MASS & GEOMETRY\n";
    line(ss, "Mass", fmtNum(m, 4) + " kg");
    line(ss, "Volume", fmtNum(o->volume * 1e6, 2) + " cm³");
    line(ss, "Density", fmtNum(mat.density, 0) + " kg/m³");
    glm::dvec3 size = (o->bbMax - o->bbMin) * 100.0;
    line(ss, "Size (local)", fmtNum(size.x, 2) + " x " + fmtNum(size.y, 2) + " x " + fmtNum(size.z, 2) + " cm");
    line(ss, "Center of mass", v3(com * 100.0, 2) + " cm");
    line(ss, "Height of COM", fmtNum(com.y * 100, 2) + " cm above floor");
    if (hasBody && s.dynamic)
        line(ss, "Moments of inertia", v3(s.inertia, 6) + " kg·m²");
    if (o->kind == ObjKind::Solid)
        line(ss, "Collision shape", o->prim && !o->edits ? std::string("exact primitive")
                                                         : std::to_string(o->coll.hulls.size()) + " hull(s) from " +
                                                               std::to_string(o->solid.pieces.size()) + " pieces" +
                                                               (o->coll.bores.empty() ? "" : ", " + std::to_string(o->coll.bores.size()) + " exact round bore(s)") +
                                                               (o->coll.cylinders.empty() ? "" : ", " + std::to_string(o->coll.cylinders.size()) + " exact pin(s)"));

    ss << "MOTION\n";
    line(ss, "Speed", fmtNum(glm::length(vel), 3) + " m/s = " + fmtNum(glm::length(vel) * 3.6, 2) + " km/h");
    line(ss, "  vector", v3(vel, 3) + " m/s");
    if (live) line(ss, "Acceleration", fmtNum(glm::length(inspect_.acc), 3) + " m/s²  " + v3(inspect_.acc, 2));
    line(ss, "Momentum", fmtNum(m * glm::length(vel), 3) + " kg·m/s");
    line(ss, "Bottom edge", fmtNum(o->wMin.y * 100, 2) + " cm above floor");

    ss << "ROTATION\n";
    line(ss, "Orientation (Euler)", v3(glm::degrees(glm::eulerAngles(o->rot)), 1) + " °");
    line(ss, "Angular velocity", fmtNum(glm::length(w), 3) + " rad/s = " + fmtNum(glm::length(w) * 60.0 / (2 * kPi), 2) + " rpm");
    glm::dvec3 L(0);
    if (hasBody && s.dynamic && std::abs(glm::determinant(s.invInertiaWorld)) > 1e-30) L = glm::inverse(s.invInertiaWorld) * w;
    line(ss, "Angular momentum", fmtNum(glm::length(L), 5) + " kg·m²/s");
    if (o->hinge) line(ss, "Motor torque", fmtNum(physics_.hingeTorque(o->hinge), 3) + " N·m");

    ss << "FORCES\n";
    glm::dvec3 fg(0, -m * kG, 0), fb(0, airDensity_ * (o->volume - o->water.solidSub) * kG, 0);
    line(ss, "Weight", fmtNum(glm::length(fg), 3) + " N (down)" + (o->water.extraMass > 0 ? " incl. " + fmtNum(o->water.extraMass, 3) + " kg water inside" : ""));
    line(ss, "Buoyancy (air)", fmtNum(fb.y, 4) + " N (up)");
    if (o->water.body) {
        line(ss, "Buoyancy (water)", fmtNum(o->water.buoyancy.y, 3) + " N (up), displaced " + fmtNum(o->water.displaced * 1e6, 1) + " cm3");
        line(ss, "Water drag", fmtNum(glm::length(o->water.drag), 3) + " N  " + v3(o->water.drag, 3));
    }
    if (live) {
        line(ss, "Air + water force", fmtNum(glm::length(inspect_.fDrag), 4) + " N  " + v3(inspect_.fDrag, 3) + "  (drag/thrust)");
        if (glm::length(inspect_.tAero) > 1e-6) line(ss, "Air torque", fmtNum(glm::length(inspect_.tAero), 4) + " N·m  " + v3(inspect_.tAero, 4));
        if (glm::length(inspect_.fExternal) > 1e-6) line(ss, "Holding force (hand)", fmtNum(glm::length(inspect_.fExternal), 3) + " N  " + v3(inspect_.fExternal, 2));
        glm::dvec3 fc = inspect_.fContact;
        glm::dvec3 n(0, 1, 0);
        if (!inspect_.contacts.empty()) {
            glm::dvec3 sum(0);
            for (auto& c : inspect_.contacts) sum += c.normal;
            if (glm::length(sum) > 1e-9) n = glm::normalize(sum);
        }
        double fn = glm::dot(fc, n);
        glm::dvec3 ft = fc - n * fn;
        line(ss, "Contact/support force", fmtNum(glm::length(fc), 3) + " N  " + v3(fc, 2));
        line(ss, "  normal force", fmtNum(fn, 3) + " N");
        std::string mu;
        if (!inspect_.contacts.empty()) {
            float muMin = 1e9f, muMax = 0, slip = 0;
            for (auto& c : inspect_.contacts) {
                muMin = std::min(muMin, c.friction);
                muMax = std::max(muMax, c.friction);
                slip = std::max(slip, c.slip);
            }
            mu = ", µ " + fmtNum(muMin, 2) + (muMax - muMin > 0.005f ? "-" + fmtNum(muMax, 2) : std::string("")) +
                 (slip > 0.02f ? " sliding at " + fmtNum(slip, 3) + " m/s" : " sticking");
        }
        line(ss, "  friction", fmtNum(glm::length(ft), 3) + " N" + (std::abs(fn) > 1e-6 ? "  (ratio " + fmtNum(glm::length(ft) / std::abs(fn), 3) + mu + ")" : ""));
        line(ss, "Net force", fmtNum(glm::length(inspect_.fNet), 3) + " N  " + v3(inspect_.fNet, 2));
    } else {
        line(ss, "Contact forces", "measurable once physics runs (Tab / sim)");
    }

    ss << "ENERGY\n";
    double ekin = 0.5 * m * glm::dot(vel, vel), erot = 0.5 * glm::dot(w, L), epot = m * kG * com.y;
    line(ss, "Kinetic (motion)", fmtNum(ekin, 4) + " J");
    line(ss, "Kinetic (rotation)", fmtNum(erot, 4) + " J");
    line(ss, "Potential (height)", fmtNum(epot, 4) + " J");
    line(ss, "Total", fmtNum(ekin + erot + epot, 4) + " J");

    ss << "SUPPORT & TIPPING\n";
    const auto& cs = inspect_.contacts;
    if (inspectId_ != id || cs.empty()) {
        line(ss, "Contacts", o->fixed ? "fixed" : "none known");
    } else {
        std::map<BodyHandle, int> partners;
        for (auto& c : cs) partners[c.other]++;
        std::string who;
        for (auto& [b, n] : partners) {
            Object* p = fromBody(b);
            who += (who.empty() ? "" : ", ") + (p ? p->name : std::string("floor")) + " (" + std::to_string(n) + ")";
        }
        line(ss, "Contact points", std::to_string(cs.size()) + ": " + who);
        // Stützfläche aus tragenden Kontakten (Normale zeigt nach oben)
        std::vector<glm::dvec2> pts;
        double baseY = 0;
        for (auto& c : cs)
            if (c.normal.y > 0.3) {
                pts.emplace_back(c.point.x, c.point.z);
                baseY += c.point.y;
            }
        if (pts.empty()) {
            line(ss, "Support area", "no supporting contacts");
        } else {
            baseY /= pts.size();
            glm::dvec2 g(com.x, com.z);
            // konvexe Hülle (Monotone Chain)
            std::sort(pts.begin(), pts.end(), [](auto& a, auto& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
            std::vector<glm::dvec2> hull;
            auto cross = [](glm::dvec2 o, glm::dvec2 a, glm::dvec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
            for (int pass = 0; pass < 2; ++pass) {
                size_t start = hull.size();
                for (size_t i = 0; i < pts.size(); ++i) {
                    glm::dvec2 p = pass == 0 ? pts[i] : pts[pts.size() - 1 - i];
                    while (hull.size() >= start + 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 1e-12) hull.pop_back();
                    hull.push_back(p);
                }
                hull.pop_back();
            }
            double margin;
            bool inside = hull.size() >= 3;
            if (hull.size() >= 3) {
                margin = 1e30;
                for (size_t i = 0; i < hull.size(); ++i) {
                    glm::dvec2 a = hull[i], b = hull[(i + 1) % hull.size()];
                    glm::dvec2 e = b - a;
                    double len = glm::length(e);
                    if (len < 1e-9) continue;
                    double d = cross(a, b, g) / len;  // > 0 = innen (CCW)
                    margin = std::min(margin, d);
                    if (d < 0) inside = false;
                }
            } else {
                glm::dvec2 a = hull.empty() ? pts[0] : hull[0], b = hull.size() > 1 ? hull[1] : a;
                glm::dvec2 e = b - a;
                double t = glm::dot(e, e) > 1e-12 ? std::clamp(glm::dot(g - a, e) / glm::dot(e, e), 0.0, 1.0) : 0.0;
                margin = -glm::length(g - (a + e * t));
            }
            line(ss, "Support area", std::to_string(hull.size()) + "-gon of supporting contacts");
            line(ss, "COM above it", inside ? "yes (stands stable)" : "no (tips / is held)");
            line(ss, "Distance to tipping edge", fmtNum(margin * 100, 2) + " cm");
            double hgt = com.y - baseY;
            if (inside && hgt > 1e-6) line(ss, "Tips at about", fmtNum(glm::degrees(std::atan(margin / hgt)), 1) + " ° tilt");
        }
    }

    if (!o->coll.bores.empty() || !o->coll.cylinders.empty() || o->lubricant) {
        ss << "BORES & PINS\n";
        std::istringstream bt(boresText(*o));
        std::string l;
        std::getline(bt, l);  // Kopfzeile (Teilname) steht schon oben
        while (std::getline(bt, l)) ss << l << "\n";
    }

    ss << "JOINTS\n";
    int nj = 0;
    for (const auto& j : joints_) {
        if (j->a != id && j->b != id) continue;
        ++nj;
        const Object* p = find(j->a == id ? j->b : j->a);
        std::string label = "J" + std::to_string(j->id) + " " + jointName(*j);
        line(ss, label.c_str(), "with " + (p ? p->name : std::string("?")) + ": " + fmtNum(j->load * 100, 0) + " % loaded (force " +
                                    fmtNum(glm::length(j->lastForce), 1) + " N)" + (j->unbreakable ? ", holds everything" : ""));
    }
    if (!nj) line(ss, "Joints", "none");
    line(ss, "Breaking", o->unbreakable ? "never (unbreakable)" : (breakage_ ? "possible (breakage on)" : "off (breakage off)"));
    ss << "HEAT\n";
    std::istringstream heat(heatText(*o));
    for (std::string l; std::getline(heat, l);) {
        const size_t sp = l.find("  ");
        if (sp == std::string::npos) continue;
        std::string key = l.substr(0, sp), val = l.substr(l.find_first_not_of(' ', sp));
        key[0] = (char)std::toupper((unsigned char)key[0]);
        line(ss, key.c_str(), val);
    }
    return ss.str();
}

void World::applyImpulse(Object& o, const glm::vec3& j) {
    if (o.body == kNoBody || o.fixed) return;
    physics_.addImpulseAt(o.body, j, physics_.centerOfMass(o.body));
}

// ============================================================================ Strahl

bool World::raycast(const glm::dvec3& origin, const glm::dvec3& dir, RayHit& hit, uint32_t ignoreId) {
    double best = 1e30;
    bool found = false;
    const glm::dvec3 pad(1e-4);
    for (auto& o : objects_) {
        if (o->id == ignoreId) continue;
        // schnelle Vorprüfung mit der Weltbegrenzung (ohne Drehen): die meisten Teile liegen nicht auf dem Strahl
        if (!rayAabb(origin, dir, o->wMin - pad, o->wMax + pad)) continue;
        double t;
        glm::dvec3 n, lp, ln;
        if (rayObject(*o, origin, dir, t, n, &lp, &ln) && t < best) {
            best = t;
            hit.obj = o.get();
            hit.t = t;
            hit.normal = n;
            hit.localPoint = lp;
            hit.localNormal = ln;
            found = true;
        }
    }
    // Boden
    if (dir.y < -1e-9) {
        double t = -origin.y / dir.y;
        if (t >= 0 && t < best) {
            best = t;
            hit.obj = nullptr;
            hit.t = t;
            hit.normal = glm::dvec3(0, 1, 0);
            found = true;
        }
    }
    if (found) hit.point = origin + dir * hit.t;
    return found;
}

// ============================================================================ Rückgängig

World::Snapshot World::capture() const {
    Snapshot s;
    s.nextId = nextId_;
    s.nextJointId = nextJointId_;
    s.nextWaterId = nextWaterId_;
    for (auto& o : objects_) s.objects.push_back(static_cast<const ObjectState&>(*o));
    for (auto& j : joints_) s.joints.push_back(static_cast<const JointState&>(*j));
    s.waters = waters_;
    return s;
}

void World::restore(const Snapshot& s) {
    clear();
    nextId_ = s.nextId;
    waters_ = s.waters;
    nextWaterId_ = s.nextWaterId;
    for (const ObjectState& st : s.objects) {
        auto o = std::make_unique<Object>();
        static_cast<ObjectState&>(*o) = st;
        insert(std::move(o));
    }
    for (const JointState& js : s.joints) addJoint(js, nullptr);
    nextJointId_ = std::max(nextJointId_, s.nextJointId);
}

void World::pushUndo() {
    PROFILE("world.pushUndo");
    undo_.push_back(capture());
    if (undo_.size() > 50) undo_.erase(undo_.begin());
    redo_.clear();
}

bool World::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(capture());
    restore(undo_.back());
    undo_.pop_back();
    return true;
}

bool World::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(capture());
    restore(redo_.back());
    redo_.pop_back();
    return true;
}

// ============================================================================ LAN-Mehrspieler

Object* World::netPut(const ObjectState& s) {
    nextId_ = std::max(nextId_, s.id + 1);
    Object* o = find(s.id);
    if (!o) {
        auto n = std::make_unique<Object>();
        static_cast<ObjectState&>(*n) = s;
        lastCreated_ = s.id;  // "last" in Befehlen
        return insert(std::move(n));
    }
    static_cast<ObjectState&>(*o) = s;
    o->coll.bores.clear();  // Abrieb kommt vom Gastgeber, nicht neu zuordnen
    rebuildGeometry(*o);
    rebuildBody(*o);
    rebuildJointsOf(o->id);
    return o;
}

void World::netMove(Object& o, const glm::dvec3& pos, const glm::dquat& rot) {
    o.pos = pos;
    o.rot = rot;
    if (o.body != kNoBody) physics_.setTransform(o.body, pos, rot, false);
    refreshPlacement(o);
}

void World::netSetJoints(const std::vector<JointState>& joints) {
    for (auto& j : joints_)
        if (j->constraint) physics_.destroyConstraint(j->constraint);
    joints_.clear();
    for (const JointState& js : joints) addJoint(js, nullptr);
}

// ============================================================================ Szene

void World::demoScene() {
    clear();
    const int oak = materialIndex("oak"), spruce = materialIndex("spruce"), steel = materialIndex("steel");
    const glm::dquat id(1, 0, 0, 0);

    // Werkbank (fest)
    const double topY = 0.90;
    Object* top = addBox(oak, {1.5, 0.04, 0.7}, {0, topY - 0.02, -1.0}, "Workbench top");
    if (top) setFixed(*top, true);
    for (int i = 0; i < 4; ++i) {
        double x = (i & 1) ? 0.66 : -0.66, z = -1.0 + ((i & 2) ? 0.28 : -0.28);
        Object* leg = addBox(oak, {0.08, topY - 0.04, 0.08}, {x, (topY - 0.04) / 2, z}, "Workbench leg " + std::to_string(i + 1));
        if (leg) setFixed(*leg, true);
    }
    // Material auf der Werkbank
    addBox(spruce, {0.80, 0.02, 0.10}, {-0.25, topY + 0.01, -1.1}, "Spruce board");
    addBox(spruce, {0.60, 0.02, 0.10}, {-0.25, topY + 0.01, -0.95}, "Spruce board short");
    addBox(materialIndex("brick"), {0.24, 0.071, 0.115}, {0.4, topY + 0.0355, -1.1}, "Brick");
    addBox(steel, {0.05, 0.05, 0.05}, {0.45, topY + 0.025, -0.85}, "Steel cube");
    addCylinder(materialIndex("aluminium"), 0.02, 0.5, {0.15, topY + 0.25, -1.25}, "Aluminium rod");

    // Auf dem Boden
    addBox(materialIndex("granite"), {0.3, 0.3, 0.3}, {1.3, 0.15, 0.4}, "Granite block");
    addSphere(materialIndex("rubber"), 0.15, {0.8, 0.075, 0.6}, "Rubber ball");
    for (int i = 0; i < 3; ++i)
        addBox(materialIndex("brick"), {0.24, 0.071, 0.115}, {-0.9, 0.0355 + i * 0.0715, 0.5}, "Brick " + std::to_string(i + 1));

    // Rampe (30°): Stahl rutscht, Gummi haftet
    {
        const double a = 30.0 * kPi / 180.0;
        glm::dquat r = glm::angleAxis(a, glm::dvec3(0, 0, 1));
        glm::dvec3 c(-1.9, 0.5 * std::sin(a) + 0.02, -0.6);
        Object* ramp = addBox(oak, {1.0, 0.02, 0.35}, c, "Ramp 30°");
        if (ramp) {
            setTransform(*ramp, c, r);
            setFixed(*ramp, true);
        }
        glm::dvec3 along = r * glm::dvec3(1, 0, 0), up = r * glm::dvec3(0, 1, 0);
        Object* s = addBox(steel, {0.06, 0.06, 0.06}, c, "Steel block (slides)");
        if (s) setTransform(*s, c + along * 0.25 + up * 0.0405 + glm::dvec3(0, 0, -0.08), r);
        Object* g = addBox(materialIndex("rubber"), {0.06, 0.06, 0.06}, c, "Rubber block (sticks)");
        if (g) setTransform(*g, c + along * 0.25 + up * 0.0405 + glm::dvec3(0, 0, 0.08), r);
    }

    // Getriebe: zwei Zahnräder auf Achsen, eines mit Motor
    {
        const double m = 0.004, z1 = 20, z2 = 30;
        const double dist = m * (z1 + z2) / 2 + 0.0004;  // Achsabstand + etwas Flankenspiel
        const glm::dvec3 c1(1.4, 0.40, -1.0), c2 = c1 + glm::dvec3(dist, 0, 0);
        glm::dquat toZ = glm::angleAxis(kPi / 2, glm::dvec3(1, 0, 0));  // lokale Y-Achse -> Welt Z
        Object* g1 = addGear(materialIndex("brass"), (int)z1, m, 0.015, 0.010, c1, "Small gear (motor)");
        Object* g2 = addGear(steel, (int)z2, m, 0.015, 0.010, c2, "Large gear");
        if (g1) {
            setTransform(*g1, c1, toZ);
            setHinge(*g1, 1, 20.0f, 3.0f);
        }
        if (g2) {
            setTransform(*g2, c2, toZ * glm::angleAxis(kPi / z2, glm::dvec3(0, 1, 0)));
            setHinge(*g2, 1, 0.0f, 0.0f);
        }
        for (const glm::dvec3& c : {c1, c2}) {
            Object* shaft = addCylinder(steel, 0.009, 0.08, c, "Shaft");
            if (shaft) {
                setTransform(*shaft, c, toZ);
                setFixed(*shaft, true);
            }
        }
        Object* post = addBox(steel, {0.03, 0.40 - 0.0, 0.03}, {c1.x + dist / 2, 0.2, c1.z - 0.06}, "Bearing post");
        if (post) setFixed(*post, true);
    }
    undo_.clear();
    redo_.clear();
}

// Szenendatei: Zeilen "schluessel werte...". Version 2 mit englischen Schlüsseln; Version 1 (deutsch) wird weiter gelesen.
bool World::save(const std::filesystem::path& file, std::string* err) const {
    std::ofstream out(file);
    if (!out) {
        if (err) *err = "cannot write the file";
        return false;
    }
    out.precision(17);
    out << "WERKSTATT-SCENE 2\n";
    out << "next_id " << nextId_ << "\n";
    for (auto& op : objects_) {
        const Object& o = *op;
        out << "object " << o.id << " " << (int)o.kind << " " << o.fixed << " " << o.hingeAxis << " " << o.motorRpm << " "
            << o.motorTorque << " " << o.edits << " " << o.seed << "\n";
        out << "material " << materials()[o.material].name << "\n";
        out << "name " << quoteFree(o.name) << "\n";
        out << "shape " << quoteFree(o.shape) << "\n";
        out << "pos " << o.pos.x << " " << o.pos.y << " " << o.pos.z << "\n";
        out << "rot " << o.rot.w << " " << o.rot.x << " " << o.rot.y << " " << o.rot.z << "\n";
        if (o.prim) out << "prim " << o.prim << " " << o.primSize.x << " " << o.primSize.y << " " << o.primSize.z << "\n";
        if (o.collision) out << "collision " << o.collision << "\n";
        if (o.unbreakable) out << "unbreakable 1\n";
        if (o.lubricant > 0) out << "lubricant " << lubricants()[std::clamp(o.lubricant, 0, (int)lubricants().size() - 1)].name << "\n";
        for (size_t i = 0; i < o.boreWear.size(); ++i)
            if (o.boreWear[i] > 0) out << "wear " << i << " " << o.boreWear[i] << "\n";
        if (o.meltForm != MeltForm::None) out << "meltform " << (int)o.meltForm << "\n";
        if (!o.heat.empty()) {  // Temperaturgitter: Maße, dann je Zelle Temperatur, Höchstwert, Verkohlung, Brennstoff, Schmelze, brennt
            const HeatGrid& g = o.heat;
            out << "heat " << g.dim.x << " " << g.dim.y << " " << g.dim.z << " " << g.origin.x << " " << g.origin.y << " " << g.origin.z << " "
                << g.cell.x << " " << g.cell.y << " " << g.cell.z << "\n";
            out << "heatcells";
            for (const HeatCell& c : g.cells)
                out << " " << c.temp << " " << c.peak << " " << c.chr << " " << c.fuel << " " << c.melt << " " << (int)c.burning;
            out << "\n";
            bool shrunk = false;  // abgebranntes Holz: wie weit jede Zelle schon geschrumpft ist
            for (const HeatCell& c : g.cells) shrunk |= c.shape < 1.0f;
            if (shrunk) {
                out << "heatshape";
                for (const HeatCell& c : g.cells) out << " " << c.shape;
                out << "\n";
            }
        }
        if (o.kind == ObjKind::Mesh && o.imported) {
            out << "scale " << o.imported->scale << "\n";
            out << "file " << o.imported->file << "\n";
        } else {
            out << "pieces " << o.solid.pieces.size() << "\n";
            for (auto& p : o.solid.pieces) {
                out << "piece " << p.faces.size() << "\n";
                for (auto& f : p.faces) {
                    out << "f " << f.n.x << " " << f.n.y << " " << f.n.z << " " << f.d << " " << (int)f.smooth << " " << f.sc.x
                        << " " << f.sc.y << " " << f.sc.z << " " << f.sa.x << " " << f.sa.y << " " << f.sa.z << " "
                        << f.internal << " " << f.sr << " " << f.v.size();
                    for (auto& v : f.v) out << " " << v.x << " " << v.y << " " << v.z;
                    out << "\n";
                }
            }
        }
        out << "end\n";
    }
    for (const auto& jp : joints_) {  // Verbindungen
        const Joint& j = *jp;
        auto v3 = [&](const glm::dvec3& v) { out << " " << v.x << " " << v.y << " " << v.z; };
        out << "joint " << j.id << " " << (int)j.type << " " << j.a << " " << j.b;
        v3(j.anchorA);
        v3(j.anchorB);
        v3(j.normalA);
        v3(j.axisB);
        out << " " << j.area << " " << j.thickA << " " << j.ft << " " << j.fs << " " << j.gapMm << " " << j.count << " " << j.lengthMm << " "
            << j.diameterMm << " " << j.seamMm << " " << j.maxForce << " " << j.stiffness << " " << j.damping << " " << j.restLength << " "
            << j.motorRpm << " " << j.motorTorque << " " << j.unbreakable << " " << (j.preset.empty() ? "-" : j.preset) << "\n";
    }
    out << "next_joint " << nextJointId_ << "\n";
    for (const WaterBody& b : waters_)  // Gewässer
        out << "water " << b.id << " " << liquids()[b.liquid].name << " " << b.sea << " " << b.lo.x << " " << b.lo.y << " " << b.lo.z << " "
            << b.hi.x << " " << b.hi.y << " " << b.hi.z << " " << b.waves << " " << b.flow.x << " " << b.flow.y << " " << b.flow.z << " "
            << b.ownTemp << " " << b.tempC << "\n";
    if (!waters_.empty()) out << "next_water " << nextWaterId_ << "\n";
    return true;
}

bool World::load(const std::filesystem::path& file, std::string* err) {
    std::ifstream in(file);
    if (!in) {
        if (err) *err = "file not found";
        return false;
    }
    std::string line;
    std::getline(in, line);
    if (line.rfind("WERKSTATT-SZENE", 0) != 0 && line.rfind("WERKSTATT-SCENE", 0) != 0) {
        if (err) *err = "not a Werkstatt scene";
        return false;
    }
    // alte (deutsche) Schlüssel auf die neuen abbilden
    auto key = [](const std::string& t) -> std::string {
        static const char* map[][2] = {{"naechste_id", "next_id"}, {"objekt", "object"},        {"form", "shape"},
                                       {"kollision", "collision"}, {"unzerbrechlich", "unbreakable"}, {"skala", "scale"},
                                       {"datei", "file"},          {"stuecke", "pieces"},       {"stueck", "piece"},
                                       {"ende", "end"},            {"verbindung", "joint"},     {"naechste_verbindung", "next_joint"}};
        for (auto& m : map)
            if (t == m[0]) return m[1];
        return t;
    };
    std::vector<ObjectState> states;
    std::vector<JointState> loadedJoints;
    uint32_t nextJointIdLoaded = 1;
    std::vector<WaterBody> loadedWaters;
    uint32_t nextWaterIdLoaded = 1;
    std::vector<std::pair<std::string, double>> meshFiles;  // pro Zustand (leer = Festkörper)
    uint32_t nextId = 1;
    ObjectState cur;
    std::string curFile;
    double curScale = 1.0;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        tag = key(tag);
        auto rest = [&]() {
            std::string r;
            std::getline(ss, r);
            if (!r.empty() && r[0] == ' ') r.erase(0, 1);
            return r;
        };
        if (tag == "next_id") {
            ss >> nextId;
        } else if (tag == "object") {
            cur = ObjectState{};
            curFile.clear();
            curScale = 1.0;
            int kind = 0;
            ss >> cur.id >> kind >> cur.fixed >> cur.hingeAxis >> cur.motorRpm >> cur.motorTorque >> cur.edits >> cur.seed;
            cur.kind = (ObjKind)kind;
        } else if (tag == "material") {
            cur.material = materialIndex(rest().c_str());
        } else if (tag == "name") {
            cur.name = rest();
        } else if (tag == "shape") {
            cur.shape = rest();
        } else if (tag == "pos") {
            ss >> cur.pos.x >> cur.pos.y >> cur.pos.z;
        } else if (tag == "rot") {
            ss >> cur.rot.w >> cur.rot.x >> cur.rot.y >> cur.rot.z;
        } else if (tag == "unbreakable") {
            ss >> cur.unbreakable;
        } else if (tag == "lubricant") {
            std::string n;
            ss >> n;
            cur.lubricant = std::max(0, findLubricant(n.c_str()));
        } else if (tag == "wear") {
            size_t i = 0;
            double v = 0;
            ss >> i >> v;
            if (i < 1000) {
                if (cur.boreWear.size() <= i) cur.boreWear.resize(i + 1, 0.0);
                cur.boreWear[i] = v;
            }
        } else if (tag == "meltform") {
            int f = 0;
            ss >> f;
            cur.meltForm = (MeltForm)std::clamp(f, 0, 2);
        } else if (tag == "heat") {
            HeatGrid& g = cur.heat;
            ss >> g.dim.x >> g.dim.y >> g.dim.z >> g.origin.x >> g.origin.y >> g.origin.z >> g.cell.x >> g.cell.y >> g.cell.z;
            if (!ss || g.dim.x <= 0 || g.dim.y <= 0 || g.dim.z <= 0 || (size_t)g.dim.x * g.dim.y * g.dim.z > 100000) cur.heat = HeatGrid();
        } else if (tag == "heatcells") {
            HeatGrid& g = cur.heat;
            if (g.empty()) continue;
            g.cells.resize((size_t)g.dim.x * g.dim.y * g.dim.z);
            for (HeatCell& c : g.cells) {
                int b = 0;
                ss >> c.temp >> c.peak >> c.chr >> c.fuel >> c.melt >> b;
                c.burning = b != 0;
                c.inside = 1;  // wird beim Einfügen aus der Form bestimmt
            }
            if (!ss) cur.heat = HeatGrid();
        } else if (tag == "heatshape") {
            for (HeatCell& c : cur.heat.cells) ss >> c.shape;
            if (!ss)
                for (HeatCell& c : cur.heat.cells) c.shape = 1.0f;
        } else if (tag == "collision") {
            ss >> cur.collision;
        } else if (tag == "prim") {
            ss >> cur.prim >> cur.primSize.x >> cur.primSize.y >> cur.primSize.z;
        } else if (tag == "scale") {
            ss >> curScale;
        } else if (tag == "file") {
            curFile = rest();
        } else if (tag == "piece") {
            cur.solid.pieces.emplace_back();
        } else if (tag == "f") {
            if (cur.solid.pieces.empty()) continue;
            solid::Face f;
            int smooth = 0, internal = 0;
            size_t nv = 0;
            ss >> f.n.x >> f.n.y >> f.n.z >> f.d >> smooth >> f.sc.x >> f.sc.y >> f.sc.z >> f.sa.x >> f.sa.y >> f.sa.z >>
                internal >> f.sr >> nv;
            f.smooth = (solid::Smooth)smooth;
            f.internal = internal != 0;
            f.v.resize(nv);
            for (auto& v : f.v) ss >> v.x >> v.y >> v.z;
            cur.solid.pieces.back().faces.push_back(std::move(f));
        } else if (tag == "end") {
            states.push_back(cur);
            meshFiles.emplace_back(curFile, curScale);
        } else if (tag == "joint") {
            JointState j;
            int type = 0;
            std::string preset;
            ss >> j.id >> type >> j.a >> j.b >> j.anchorA.x >> j.anchorA.y >> j.anchorA.z >> j.anchorB.x >> j.anchorB.y >> j.anchorB.z >>
                j.normalA.x >> j.normalA.y >> j.normalA.z >> j.axisB.x >> j.axisB.y >> j.axisB.z >> j.area >> j.thickA >> j.ft >> j.fs >>
                j.gapMm >> j.count >> j.lengthMm >> j.diameterMm >> j.seamMm >> j.maxForce >> j.stiffness >> j.damping >> j.restLength >>
                j.motorRpm >> j.motorTorque >> j.unbreakable >> preset;
            j.type = (JointType)type;
            j.preset = preset == "-" ? "" : gluePresetKey(preset);
            loadedJoints.push_back(j);
        } else if (tag == "next_joint") {
            ss >> nextJointIdLoaded;
        } else if (tag == "water") {
            WaterBody b;
            std::string liquid;
            ss >> b.id >> liquid >> b.sea >> b.lo.x >> b.lo.y >> b.lo.z >> b.hi.x >> b.hi.y >> b.hi.z >> b.waves >> b.flow.x >> b.flow.y >>
                b.flow.z >> b.ownTemp >> b.tempC;
            b.liquid = std::max(0, findLiquid(liquid.c_str()));
            if (ss) loadedWaters.push_back(b);
        } else if (tag == "next_water") {
            ss >> nextWaterIdLoaded;
        }
    }
    clear();
    undo_.clear();
    redo_.clear();
    setWaters(loadedWaters);
    nextWaterId_ = std::max(nextWaterId_, nextWaterIdLoaded);
    nextId_ = nextId;
    for (size_t i = 0; i < states.size(); ++i) {
        if (states[i].kind == ObjKind::Mesh) {
            std::string e;
            Object* o = addImported(std::filesystem::u8path(meshFiles[i].first), states[i].material, meshFiles[i].second,
                                    states[i].pos, &e);
            if (!o) {
                std::fprintf(stderr, "model %s is missing: %s\n", meshFiles[i].first.c_str(), e.c_str());
                continue;
            }
            std::shared_ptr<const ImportedMesh> im = o->imported;
            static_cast<ObjectState&>(*o) = states[i];
            o->imported = im;
            rebuildGeometry(*o);
            rebuildBody(*o);
        } else {
            auto o = std::make_unique<Object>();
            static_cast<ObjectState&>(*o) = states[i];
            solid::classifyInternalFaces(o->solid);
            insert(std::move(o));
        }
    }
    nextId_ = std::max(nextId_, nextId);
    for (auto& o : objects_) nextId_ = std::max(nextId_, o->id + 1);
    for (const JointState& j : loadedJoints) addJoint(j, nullptr);
    nextJointId_ = std::max(nextJointId_, nextJointIdLoaded);
    return true;
}
