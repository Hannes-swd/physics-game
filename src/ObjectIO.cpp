// Teile und Verbindungen binär: für das LAN und für die Weltdatei (world.bin). Ein Teil komplett: Zustand, Geometrie und
// die fertige Kollisionsform (spart beim Empfangen und Laden die teure Zerlegung)
#include "ObjectIO.h"

#include <algorithm>

#include "Materials.h"

// Anzahl lesen und prüfen, dass die Daten dafür reichen (each = Mindestgröße je Eintrag)
uint32_t readCount(net::Reader& r, size_t each) {
    uint32_t n = r.u32();
    if (!r.ok() || (size_t)n * each > r.left()) {
        r.fail();
        return 0;
    }
    return n;
}

void writeHulls(net::Writer& w, const std::vector<std::vector<glm::vec3>>& hulls) {
    w.u32((uint32_t)hulls.size());
    for (const auto& h : hulls) {
        w.u32((uint32_t)h.size());
        for (const glm::vec3& p : h) w.vec3(p);
    }
}

std::vector<std::vector<glm::vec3>> readHulls(net::Reader& r) {
    std::vector<std::vector<glm::vec3>> hulls(readCount(r, 4));
    for (auto& h : hulls) {
        h.resize(readCount(r, 12));
        for (glm::vec3& p : h) p = r.vec3();
    }
    return hulls;
}

void writePoints(net::Writer& w, const std::vector<glm::dvec3>& pts) {
    w.u32((uint32_t)pts.size());
    for (const glm::dvec3& p : pts) w.dvec3(p);
}

std::vector<glm::dvec3> readPoints(net::Reader& r) {
    std::vector<glm::dvec3> pts(readCount(r, 24));
    for (glm::dvec3& p : pts) p = r.dvec3();
    return pts;
}

// Teil komplett: Zustand, Geometrie und Kollisionsform (spart dem Mitspieler die teure Zerlegung)
void writeObject(net::Writer& w, const Object& o, bool file) {
    w.u32(o.id);
    w.str(o.name);
    w.u32((uint32_t)o.material);
    w.u8((uint8_t)o.kind);
    w.u8(o.fixed);
    w.u32((uint32_t)(o.hingeAxis + 1));
    w.f32(o.motorRpm);
    w.f32(o.motorTorque);
    w.str(o.shape);
    w.u32((uint32_t)o.prim);
    w.dvec3(o.primSize);
    w.u32((uint32_t)o.collision);
    w.u8(o.unbreakable);
    w.u32((uint32_t)o.edits);
    w.f32(o.seed);
    w.u32((uint32_t)o.lubricant);
    w.u32((uint32_t)o.boreWear.size());
    for (double v : o.boreWear) w.f64(v);
    w.dvec3(o.pos);
    w.dquat(o.rot);
    // Baum: Art, Form, Größe, Stammfuß, Krone, Wurzel, Platz des Generators (der Mitspieler zeichnet ihn dann nicht doppelt)
    w.str(o.treeType);
    w.u32((uint32_t)o.treeVariant);
    w.f32(o.treeScale);
    w.dvec3(o.treeOrigin);
    w.u8((uint8_t)((o.crown ? 1 : 0) | (o.rooted ? 2 : 0)));
    w.u32((uint32_t)o.treeKey);
    w.u32((uint32_t)(o.treeKey >> 32));
    w.u8(o.imported ? 1 : 0);
    if (o.imported) {
        const ImportedMesh& m = *o.imported;
        w.u32((uint32_t)m.vertices.size());
        for (const solid::Vertex& v : m.vertices) {
            w.f32(v.px), w.f32(v.py), w.f32(v.pz);
            w.f32(v.nx), w.f32(v.ny), w.f32(v.nz);
        }
        writeHulls(w, m.hulls);
        w.f64(m.volume);
        w.str(m.file);
        w.f64(m.scale);
    }
    w.u32((uint32_t)o.solid.pieces.size());
    for (const solid::Piece& p : o.solid.pieces) {
        w.u32((uint32_t)p.faces.size());
        for (const solid::Face& f : p.faces) {
            w.dvec3(f.n);
            w.f64(f.d);
            w.u8((uint8_t)f.smooth);
            w.dvec3(f.sc);
            w.dvec3(f.sa);
            w.f64(f.sr);
            w.u8(f.internal);
            w.u8(f.partial);
            writePoints(w, f.v);
            w.u32((uint32_t)f.vis.size());
            for (const auto& poly : f.vis) writePoints(w, poly);
        }
    }
    // Kollisionsform: im LAN die aktuelle; in der Datei nur die fertige (sonst rechnet das Laden sie neu, genauer)
    const CollisionShape* coll = &o.coll;
    if (file) coll = o.hullCache && o.hullCacheMode == o.collision ? o.hullCache.get() : nullptr;
    w.u8(o.kind == ObjKind::Solid && coll);
    if (o.kind == ObjKind::Solid && coll) writeCollision(w, *coll);
    if (!file) return;
    // nur in der Datei: Schmelze, Temperatur (je Zelle), Zustand des Baums beim Entstehen
    w.u32((uint32_t)o.meltForm);
    w.u32((uint32_t)o.heat.dim.x), w.u32((uint32_t)o.heat.dim.y), w.u32((uint32_t)o.heat.dim.z);
    if (!o.heat.empty()) {
        w.dvec3(o.heat.origin);
        w.dvec3(o.heat.cell);
        for (const HeatCell& c : o.heat.cells) {
            w.f32(c.temp), w.f32(c.peak), w.f32(c.chr), w.f32(c.fuel), w.f32(c.melt), w.f32(c.shape);
            w.u8(c.burning);
        }
    }
    w.u32((uint32_t)o.treeSig);
    w.u32((uint32_t)(o.treeSig >> 32));
}

void writeCollision(net::Writer& w, const CollisionShape& cs) {
    const CollisionShape* coll = &cs;
    writeHulls(w, coll->hulls);
    w.u32((uint32_t)coll->bores.size());
    for (const BoreGeom& b : coll->bores) {
        w.dvec3(b.center);
        w.dvec3(b.axis);
        w.dvec3(b.u);
        w.dvec3(b.w);
        w.f64(b.radius);
        w.f64(b.halfLength);
        w.f64(b.band);
        w.u8(b.openTop);
        w.u8(b.openBottom);
        for (double e : b.edge) w.f64(e);
    }
    w.u32((uint32_t)coll->cylinders.size());
    for (const CylGeom& c : coll->cylinders) {
        w.dvec3(c.center);
        w.dvec3(c.axis);
        w.f64(c.radius);
        w.f64(c.halfLength);
    }
}

bool readObject(net::Reader& r, ObjectState& s, bool file) {
    s.id = r.u32();
    s.name = r.str();
    s.material = std::clamp((int)r.u32(), 0, (int)materials().size() - 1);
    s.kind = (ObjKind)r.u8();
    s.fixed = r.u8() != 0;
    s.hingeAxis = (int)r.u32() - 1;
    s.motorRpm = r.f32();
    s.motorTorque = r.f32();
    s.shape = r.str();
    s.prim = (int)r.u32();
    s.primSize = r.dvec3();
    s.collision = (int)r.u32();
    s.unbreakable = r.u8() != 0;
    s.edits = (int)r.u32();
    s.seed = r.f32();
    s.lubricant = std::clamp((int)r.u32(), 0, (int)lubricants().size() - 1);
    s.boreWear.resize(readCount(r, 8));
    for (double& v : s.boreWear) v = r.f64();
    s.pos = r.dvec3();
    s.rot = r.dquat();
    s.treeType = r.str();
    s.treeVariant = (int)r.u32();
    s.treeScale = r.f32();
    s.treeOrigin = r.dvec3();
    const uint8_t treeFlags = r.u8();
    s.crown = (treeFlags & 1) != 0;
    s.rooted = (treeFlags & 2) != 0;
    s.treeKey = r.u32();
    s.treeKey |= (uint64_t)r.u32() << 32;
    if (r.u8()) {
        auto m = std::make_shared<ImportedMesh>();
        m->vertices.resize(readCount(r, 24));
        for (solid::Vertex& v : m->vertices) {
            v.px = r.f32(), v.py = r.f32(), v.pz = r.f32();
            v.nx = r.f32(), v.ny = r.f32(), v.nz = r.f32();
        }
        m->hulls = readHulls(r);
        m->volume = r.f64();
        m->file = r.str();
        m->scale = r.f64();
        s.imported = m;
    }
    s.solid.pieces.resize(readCount(r, 4));
    for (solid::Piece& p : s.solid.pieces) {
        p.faces.resize(readCount(r, 90));
        for (solid::Face& f : p.faces) {
            f.n = r.dvec3();
            f.d = r.f64();
            f.smooth = (solid::Smooth)r.u8();
            f.sc = r.dvec3();
            f.sa = r.dvec3();
            f.sr = r.f64();
            f.internal = r.u8() != 0;
            f.partial = r.u8() != 0;
            f.v = readPoints(r);
            f.vis.resize(readCount(r, 4));
            for (auto& poly : f.vis) poly = readPoints(r);
        }
    }
    if (r.u8()) {
        auto c = std::make_shared<CollisionShape>();
        c->hulls = readHulls(r);
        c->bores.resize(readCount(r, 186));
        for (BoreGeom& b : c->bores) {
            b.center = r.dvec3();
            b.axis = r.dvec3();
            b.u = r.dvec3();
            b.w = r.dvec3();
            b.radius = r.f64();
            b.halfLength = r.f64();
            b.band = r.f64();
            b.openTop = r.u8() != 0;
            b.openBottom = r.u8() != 0;
            for (double& e : b.edge) e = r.f64();
        }
        c->cylinders.resize(readCount(r, 64));
        for (CylGeom& cy : c->cylinders) {
            cy.center = r.dvec3();
            cy.axis = r.dvec3();
            cy.radius = r.f64();
            cy.halfLength = r.f64();
        }
        s.hullCache = c;
        s.hullCacheMode = s.collision;
    }
    if (!file) return r.ok();
    s.meltForm = (MeltForm)std::clamp((int)r.u32(), 0, 2);
    glm::ivec3 dim;
    dim.x = (int)r.u32(), dim.y = (int)r.u32(), dim.z = (int)r.u32();
    if (dim.x > 0 && dim.y > 0 && dim.z > 0) {
        const size_t n = (size_t)dim.x * dim.y * dim.z;
        if (n > r.left() / 25) {  // je Zelle 25 Byte
            r.fail();
            return false;
        }
        s.heat.dim = dim;
        s.heat.origin = r.dvec3();
        s.heat.cell = r.dvec3();
        s.heat.cells.resize(n);
        for (HeatCell& c : s.heat.cells) {
            c.temp = r.f32(), c.peak = r.f32(), c.chr = r.f32(), c.fuel = r.f32(), c.melt = r.f32(), c.shape = r.f32();
            c.burning = r.u8();
        }
    }
    s.treeSig = r.u32();
    s.treeSig |= (uint64_t)r.u32() << 32;
    return r.ok();
}

void writeJoint(net::Writer& w, const JointState& j) {
    w.u32(j.id);
    w.u32((uint32_t)j.type);
    w.u32(j.a);
    w.u32(j.b);
    w.dvec3(j.anchorA);
    w.dvec3(j.anchorB);
    w.dvec3(j.normalA);
    w.dvec3(j.axisB);
    w.str(j.preset);
    for (double v : {j.area, j.thickA, j.ft, j.fs, j.gapMm}) w.f64(v);
    w.u32((uint32_t)j.count);
    for (double v : {j.lengthMm, j.diameterMm, j.seamMm, j.maxForce, j.stiffness, j.damping, j.restLength}) w.f64(v);
    w.f32(j.motorRpm);
    w.f32(j.motorTorque);
    w.u8(j.unbreakable);
}

JointState readJoint(net::Reader& r) {
    JointState j;
    j.id = r.u32();
    j.type = (JointType)r.u32();
    j.a = r.u32();
    j.b = r.u32();
    j.anchorA = r.dvec3();
    j.anchorB = r.dvec3();
    j.normalA = r.dvec3();
    j.axisB = r.dvec3();
    j.preset = r.str();
    for (double* v : {&j.area, &j.thickA, &j.ft, &j.fs, &j.gapMm}) *v = r.f64();
    j.count = (int)r.u32();
    for (double* v : {&j.lengthMm, &j.diameterMm, &j.seamMm, &j.maxForce, &j.stiffness, &j.damping, &j.restLength}) *v = r.f64();
    j.motorRpm = r.f32();
    j.motorTorque = r.f32();
    j.unbreakable = r.u8() != 0;
    return j;
}
