// Bearbeitungen: Abrunden/Fasen, Fräsen, Hobeln, Spiegeln, Skalieren, Vereinen/Abziehen, Drehbank, Biegen
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <map>

#include "Materials.h"
#include "World.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

glm::dmat4 frameOf(const Object& o) { return glm::translate(glm::dmat4(1.0), o.pos) * glm::mat4_cast(o.rot); }

Object* solidPart(World& w, uint32_t id, std::string* err, const char* verb) {
    Object* o = w.find(id);
    if (!o) {
        if (err) *err = "part not found";
        return nullptr;
    }
    if (o->kind != ObjKind::Solid) {
        if (err) *err = std::string("imported models cannot be ") + verb + " (yet)";
        return nullptr;
    }
    return o;
}

std::vector<glm::dvec3> pointsOf(const solid::Piece& p) {
    std::vector<glm::dvec3> r;
    for (const auto& f : p.faces)
        for (const auto& v : f.v) {
            bool dup = false;
            for (const auto& q : r)
                if (glm::length(q - v) < 1e-10) {
                    dup = true;
                    break;
                }
            if (!dup) r.push_back(v);
        }
    return r;
}

bool boxesTouch(const solid::Piece& a, const solid::Solid& b) {
    glm::dvec3 amn, amx, bmn, bmx;
    solid::bounds(a, amn, amx);
    solid::bounds(b, bmn, bmx);
    return amn.x <= bmx.x && bmn.x <= amx.x && amn.y <= bmx.y && bmn.y <= amx.y && amn.z <= bmx.z && bmn.z <= amx.z;
}

} // namespace

const std::vector<solid::Edge>& World::edgesOf(Object& o) {
    if (!o.edgesValid) {
        o.featureEdges.clear();
        if (o.kind == ObjKind::Solid) o.featureEdges = solid::findEdges(o.solid);
        o.edgesValid = true;
    }
    return o.featureEdges;
}

// ============================================================================ Abrunden / Fasen

bool World::blendEdges(uint32_t id, const std::vector<int>& edges, double size, int segments, std::string* err) {
    Object* o = solidPart(*this, id, err, "rounded");
    if (!o) return false;
    if (size <= 0) {
        if (err) *err = "the radius must be > 0";
        return false;
    }
    const auto all = edgesOf(*o);  // Kopie: die Liste ändert sich mit der Form
    std::vector<std::vector<solid::Plane>> sub, add;
    for (int i : edges) {
        if (i < 0 || i >= (int)all.size()) {
            if (err) *err = "there is no edge " + std::to_string(i) + " (the part has " + std::to_string(all.size()) + " edges, see 'edges')";
            return false;
        }
        auto regs = solid::edgeBlendTools(all[i], size, segments);
        auto& dst = all[i].convex ? sub : add;
        dst.insert(dst.end(), regs.begin(), regs.end());
    }
    if (sub.empty() && add.empty()) {
        if (err) *err = "no edge to round";
        return false;
    }
    solid::Solid s = o->solid;
    double removed = 0;
    for (const auto& t : sub) {
        double r = 0;
        s = solid::subtract(s, t, &r, false);
        removed += r;
    }
    const double before = solid::volume(s);
    if (!add.empty()) s = solid::addRegions(s, add);
    const double added = solid::volume(s) - before;
    if (removed < 1e-13 && added < 1e-13) {
        if (err) *err = "nothing changed (radius too small?)";
        return false;
    }
    return applyResult(*o, std::move(s), nullptr, err);
}

// ============================================================================ Abtragen (Fräsen, Senken)

bool World::removeMaterial(uint32_t id, const std::vector<std::vector<solid::Plane>>& tools, double* removed,
                           std::vector<uint32_t>* resultIds, std::string* err) {
    Object* o = solidPart(*this, id, err, "milled");
    if (!o) return false;
    solid::Solid s = o->solid;
    double total = 0;
    for (const auto& t : tools) {
        double r = 0;
        s = solid::subtract(s, t, &r, true);
        total += r;
    }
    if (removed) *removed = total;
    if (total < 1e-12) {
        if (err) *err = "the tool does not hit any material";
        return false;
    }
    return applyResult(*o, std::move(s), resultIds, err);
}

bool World::planeOff(uint32_t id, int axis, int sign, double depth, std::string* err) {
    Object* o = solidPart(*this, id, err, "planed");
    if (!o) return false;
    if (axis < 0 || axis > 2 || depth <= 0) {
        if (err) *err = "depth must be > 0";
        return false;
    }
    const double size = o->bbMax[axis] - o->bbMin[axis];
    if (depth >= size - 1e-6) {
        if (err) *err = "the part is only " + fmtNum(size * 1000, 1) + " mm thick there";
        return false;
    }
    solid::Plane pl;
    pl.n = glm::dvec3(0);
    pl.n[axis] = sign > 0 ? 1.0 : -1.0;
    pl.d = sign > 0 ? o->bbMax[axis] - depth : -(o->bbMin[axis] + depth);
    solid::Solid keep, off;
    solid::split(o->solid, pl, keep, off);
    if (keep.pieces.empty() || off.pieces.empty()) {
        if (err) *err = "nothing to plane off";
        return false;
    }
    return applyResult(*o, std::move(keep), nullptr, err);
}

// ============================================================================ Spiegeln, Skalieren

bool World::mirrorPart(uint32_t id, int axis, std::string* err) {
    Object* o = solidPart(*this, id, err, "mirrored");
    if (!o) return false;
    glm::dmat3 a(1.0);
    a[axis][axis] = -1.0;
    solid::transformAffine(o->solid, a);
    o->hullCache.reset();
    for (auto& j : joints_) {
        if (j->a == id) {
            j->anchorA = a * j->anchorA;
            j->normalA = a * j->normalA;
        }
        if (j->b == id) {
            j->anchorB = a * j->anchorB;
            j->axisB = glm::abs(a * j->axisB);
        }
    }
    if (!o->prim) ++o->edits;  // Grundformen sind symmetrisch: bleiben exakt
    rebuildGeometry(*o);
    rebuildBody(*o);
    return true;
}

bool World::scalePart(uint32_t id, const glm::dvec3& f, std::string* err) {
    Object* o = find(id);
    if (!o) {
        if (err) *err = "part not found";
        return false;
    }
    if (f.x < 0.01 || f.y < 0.01 || f.z < 0.01 || f.x > 100 || f.y > 100 || f.z > 100) {
        if (err) *err = "scale factors must be between 0.01 and 100";
        return false;
    }
    const glm::dmat3 a(glm::dvec3(f.x, 0, 0), glm::dvec3(0, f.y, 0), glm::dvec3(0, 0, f.z));
    const glm::dmat3 n = glm::transpose(glm::inverse(a));
    if (o->kind == ObjKind::Mesh) {
        if (!o->imported) return false;
        auto im = std::make_shared<ImportedMesh>(*o->imported);
        for (auto& v : im->vertices) {
            glm::dvec3 p = a * glm::dvec3(v.px, v.py, v.pz);
            glm::dvec3 nn = glm::normalize(n * glm::dvec3(v.nx, v.ny, v.nz));
            v = {float(p.x), float(p.y), float(p.z), float(nn.x), float(nn.y), float(nn.z)};
        }
        for (auto& h : im->hulls)
            for (auto& p : h) p = glm::vec3(a * glm::dvec3(p));
        im->volume *= f.x * f.y * f.z;
        o->imported = im;
    } else {
        solid::transformAffine(o->solid, a);
        o->hullCache.reset();
        const bool uniform = std::abs(f.x - f.y) < 1e-9 && std::abs(f.y - f.z) < 1e-9;
        if (o->prim == 1) {
            o->primSize *= f;
            glm::dvec3 s = o->primSize * 200.0;
            o->shape = "box " + fmtNum(s.x) + " x " + fmtNum(s.z) + " x " + fmtNum(s.y) + " cm";
        } else if (o->prim == 2 && uniform) {
            o->primSize.x *= f.x;
            o->shape = "sphere D " + fmtNum(o->primSize.x * 200) + " cm";
        } else if (o->prim == 3 && std::abs(f.x - f.z) < 1e-9) {
            o->primSize.x *= f.x;
            o->primSize.y *= f.y;
            o->shape = "cylinder D " + fmtNum(o->primSize.x * 200) + " cm, H " + fmtNum(o->primSize.y * 200) + " cm";
        } else {
            if (o->prim) ++o->edits;  // keine exakte Grundform mehr (z.B. Kugel zum Ei)
            o->prim = 0;
            o->shape += uniform ? " scaled x" + fmtNum(f.x, 3) : " scaled " + fmtNum(f.x, 3) + "/" + fmtNum(f.y, 3) + "/" + fmtNum(f.z, 3);
        }
    }
    for (auto& j : joints_) {
        if (j->a == id) {
            j->anchorA = a * j->anchorA;
            j->normalA = glm::normalize(n * j->normalA);
        }
        if (j->b == id) j->anchorB = a * j->anchorB;
    }
    rebuildGeometry(*o);
    rebuildBody(*o);
    return true;
}

// ============================================================================ Vereinen, Abziehen, Schnittmenge

bool World::mergeParts(uint32_t ida, uint32_t idb, std::string* err) {
    Object* A = solidPart(*this, ida, err, "merged");
    Object* B = solidPart(*this, idb, err, "merged");
    if (!A || !B) return false;
    if (ida == idb) {
        if (err) *err = "give two different parts";
        return false;
    }
    if (A->material != B->material) {
        if (err) *err = std::string("the parts are made of different materials (") + materials()[A->material].name + ", " +
                        materials()[B->material].name + "). Use 'glue' or 'weld', or set the same material first.";
        return false;
    }
    const glm::dmat4 m = glm::inverse(frameOf(*A)) * frameOf(*B);
    const glm::dmat3 r(m);
    solid::Solid sb = B->solid;
    solid::transform(sb, m);
    for (const auto& p : A->solid.pieces) {  // überlappendes Material nur einmal zählen
        if (sb.pieces.empty()) break;
        if (boxesTouch(p, sb)) sb = solid::subtract(sb, solid::planesOf(p), nullptr, false);
    }
    solid::Solid s = A->solid;
    for (auto& p : sb.pieces) s.pieces.push_back(std::move(p));
    solid::classifyInternalFaces(s);
    if (solid::connectedComponents(s).size() > solid::connectedComponents(A->solid).size()) {
        if (err) *err = "the parts do not touch - move them together first";
        return false;
    }
    // Verbindungen: zwischen a und b entfallen, die von b gehen auf a über
    releaseJointsOf(idb);
    for (size_t k = joints_.size(); k-- > 0;) {
        Joint& j = *joints_[k];
        if ((j.a == ida && j.b == idb) || (j.a == idb && j.b == ida)) {
            removeJoint(j.id);
            continue;
        }
        if (j.a == idb) {
            j.a = ida;
            j.anchorA = glm::dvec3(m * glm::dvec4(j.anchorA, 1.0));
            j.normalA = glm::normalize(r * j.normalA);
        }
        if (j.b == idb) {
            j.b = ida;
            j.anchorB = glm::dvec3(m * glm::dvec4(j.anchorB, 1.0));
            j.axisB = glm::normalize(r * j.axisB);
        }
    }
    remove(idb);
    return applyResult(*A, std::move(s), nullptr, err);
}

bool World::booleanCut(uint32_t ida, uint32_t idt, bool inter, bool keepTool, std::vector<uint32_t>* resultIds, double* removed,
                       std::string* err) {
    Object* A = solidPart(*this, ida, err, "cut");
    Object* T = find(idt);
    if (!A) return false;
    if (!T || ida == idt) {
        if (err) *err = "tool part not found";
        return false;
    }
    if (T->kind != ObjKind::Solid) {
        if (err) *err = "imported models cannot be used as a tool (yet)";
        return false;
    }
    solid::Solid st = T->solid;
    solid::transform(st, glm::inverse(frameOf(*A)) * frameOf(*T));
    const double v0 = solid::volume(A->solid);
    solid::Solid s;
    if (!inter) {
        s = A->solid;
        for (const auto& p : st.pieces)
            if (boxesTouch(p, s)) s = solid::subtract(s, solid::planesOf(p), nullptr, true);
    } else {
        for (const auto& p : st.pieces) {
            solid::Solid part = solid::intersect(A->solid, solid::planesOf(p));
            for (auto& q : part.pieces) s.pieces.push_back(std::move(q));
        }
    }
    const double r = v0 - solid::volume(s);
    if (removed) *removed = r;
    if (r < 1e-12) {
        if (err) *err = inter ? "the part lies completely inside the tool - nothing to remove" : "the parts do not overlap";
        return false;
    }
    if (s.pieces.empty()) {
        if (err) *err = inter ? "the parts do not overlap" : "nothing would be left of the part";
        return false;
    }
    if (!applyResult(*A, std::move(s), resultIds, err)) return false;
    if (!keepTool) remove(idt);
    return true;
}

// ============================================================================ Drehbank

bool World::turnPart(uint32_t id, int axis, double from, double to, double d0, double d1, double* removed, std::string* err) {
    Object* o = solidPart(*this, id, err, "turned");
    if (!o) return false;
    if (axis < 0 || axis > 2 || to <= from || d0 <= 0 || d1 <= 0) {
        if (err) *err = "range or diameter invalid (from < to, diameters > 0)";
        return false;
    }
    glm::dvec3 c = (o->bbMin + o->bbMax) * 0.5;  // Drehachse durch die Mitte des Querschnitts
    c[axis] = o->bbMin[axis];                    // Maße ab dem Ende
    glm::dvec3 dir(0);
    dir[axis] = 1;
    const int seg = std::clamp(int(std::max(d0, d1) * 2000), 32, 96);
    const double v0 = o->volume;
    solid::Solid s = solid::turn(o->solid, c, dir, from, to, d0 / 2, d1 / 2, seg);
    const double r = v0 - solid::volume(s);
    if (removed) *removed = r;
    if (r < 1e-12) {
        if (err) *err = "the part is already thinner than " + fmtNum(std::max(d0, d1) * 1000, 1) + " mm there";
        return false;
    }
    return applyResult(*o, std::move(s), nullptr, err);
}

// ============================================================================ Biegen

bool World::bendPart(uint32_t id, int a, double at, double angleDeg, double radius, std::string* err, std::string* info) {
    Object* o = solidPart(*this, id, err, "bent");
    if (!o) return false;
    const Material& mat = materials()[o->material];
    if (mat.minBendRatio < 0) {
        if (err) *err = std::string(mat.name) + " is brittle and cannot be bent - it would break";
        return false;
    }
    // Temperatur: kalt spröde (Stahl unter etwa -20 °C, Kunststoff, Gummi) bricht, warm lässt es sich viel enger biegen
    const double tempC = o->heat.empty() ? ambient() : o->heatInfo.tMean;
    if (brittleness(o->material, tempC) > 0.5) {
        if (err) *err = std::string(mat.name) + " is brittle at " + fmtNum(tempC, 0) + " °C and would crack - warm it up first";
        return false;
    }
    const double bendRatio = mat.minBendRatio * std::clamp((double)strengthFactor(o->material, tempC), 0.05, 1.0);
    if (a < 0 || a > 2 || std::abs(angleDeg) < 0.01 || std::abs(angleDeg) > 180) {
        if (err) *err = "angle must be between -180 and 180 degrees (not 0)";
        return false;
    }
    const glm::dvec3 ext = o->bbMax - o->bbMin;
    const double s0 = o->bbMin[a] + at;
    // Querschnitt an der Biegestelle (nach einer ersten Biegung sagt der Hüllquader nichts mehr über die Dicke)
    glm::dvec3 cmn, cmx;
    {
        glm::dvec3 lo = o->bbMin - 1.0, hi = o->bbMax + 1.0;
        lo[a] = s0 - 0.0005;
        hi[a] = s0 + 0.0005;
        solid::Solid slice = solid::intersect(o->solid, solid::boxTool(lo, hi));
        if (slice.pieces.empty()) {
            if (err) *err = "there is no material at " + fmtNum(at * 100, 1) + " cm along " + "xyz"[a];
            return false;
        }
        solid::bounds(slice, cmn, cmx);
    }
    const glm::dvec3 cext = cmx - cmn;
    // Dicke = die dünnere der beiden anderen Richtungen, Breite = die dritte (Biegeachse)
    int t = (a + 1) % 3, w = (a + 2) % 3;
    if (cext[w] < cext[t]) std::swap(t, w);
    const double T = cext[t];
    const double minR = std::max(0.0, bendRatio * T);
    if (radius <= 0) radius = std::max(minR, 0.25 * T);
    if (radius + 1e-9 < minR) {
        if (err) *err = std::string(mat.name) + " " + fmtNum(T * 1000, 1) + " mm thick cracks below an inner radius of " + fmtNum(minR * 1000, 1) +
                        " mm (" + fmtNum(bendRatio, 2) + " x thickness at " + fmtNum(tempC, 0) + " °C)";
        return false;
    }
    radius = std::max(radius, 0.05 * T);  // ganz scharf geht nicht (Material muss irgendwo hin)
    const double sign = angleDeg > 0 ? 1.0 : -1.0;
    const double theta = std::abs(angleDeg) * kPi / 180.0;
    const double tIn = sign > 0 ? cmx[t] : cmn[t];  // Innenseite der Biegung (an der Biegestelle)
    const double Rin = radius, Rn = Rin + 0.5 * T;              // neutrale Faser in der Mitte
    const double L = Rn * theta;                                // Länge der Biegezone
    if (s0 <= o->bbMin[a] + 1e-6 || s0 + L >= o->bbMax[a] - 1e-6) {
        if (err) *err = "the bend (" + fmtNum(L * 1000, 1) + " mm long) must lie inside the part: 0 < at and at + bend < " +
                        fmtNum(ext[a] * 100, 1) + " cm";
        return false;
    }
    // Abbildung eines Punkts (lokal) beim Biegen
    auto bend = [&](const glm::dvec3& p) {
        const double sa = p[a];
        if (sa <= s0) return p;
        const double q = sign * (tIn - p[t]);  // Abstand von der Innenseite
        const double rho = Rin + q;
        glm::dvec3 r = p;
        if (sa <= s0 + L) {
            const double phi = (sa - s0) / Rn;
            r[a] = s0 + rho * std::sin(phi);
            r[t] = tIn + sign * Rin - sign * rho * std::cos(phi);
        } else {
            const double rest = sa - s0 - L;
            r[a] = s0 + rho * std::sin(theta) + rest * std::cos(theta);
            r[t] = tIn + sign * Rin - sign * rho * std::cos(theta) + sign * rest * std::sin(theta);
        }
        return r;
    };
    // Stücke vor, in und hinter der Biegezone
    solid::Plane p0, p1;
    p0.n = glm::dvec3(0);
    p0.n[a] = 1;
    p0.d = s0;
    p1 = p0;
    p1.d = s0 + L;
    solid::Solid before, rest, zone, after;
    solid::split(o->solid, p0, before, rest);
    solid::split(rest, p1, zone, after);
    solid::Solid out = before;
    // hinter der Biegung: starr gedreht und verschoben
    {
        glm::dvec3 P0 = (o->bbMin + o->bbMax) * 0.5;
        P0[a] = s0 + L;
        P0[t] = tIn;
        glm::dvec3 ea(0), et(0), ew(0);
        ea[a] = 1;
        et[t] = 1;
        ew[w] = 1;
        const glm::dvec3 axis = glm::normalize(glm::cross(ea, et * sign));
        const glm::dmat4 m = glm::translate(glm::dmat4(1.0), bend(P0)) * glm::mat4_cast(glm::angleAxis(theta, axis)) *
                             glm::translate(glm::dmat4(1.0), -P0);
        for (auto& p : after.pieces) {
            solid::transform(p, m);
            out.pieces.push_back(std::move(p));
        }
    }
    // Biegezone: in dünne Scheiben teilen, jede Scheibe gebogen als konvexe Hülle
    const int n = std::clamp((int)std::ceil(angleDeg * sign / 6.0), 2, 30);
    glm::dvec3 ew(0);
    ew[w] = 1;
    for (int k = 0; k < n; ++k) {
        solid::Plane lo, hi;
        lo.n = glm::dvec3(0);
        lo.n[a] = -1;
        lo.d = -(s0 + L * k / n);
        hi.n = glm::dvec3(0);
        hi.n[a] = 1;
        hi.d = s0 + L * (k + 1) / n;
        for (const auto& p : zone.pieces) {
            solid::Piece sl, tmp;
            if (!solid::clip(p, lo, tmp) || !solid::clip(tmp, hi, sl)) continue;
            std::vector<glm::dvec3> pts = pointsOf(sl);
            for (auto& q : pts) q = bend(q);
            solid::Piece bent = solid::hullPiece(pts);
            if (bent.faces.empty()) continue;
            for (auto& f : bent.faces) {  // gekrümmte Innen-/Außenseite weich darstellen
                if (std::abs(glm::dot(f.n, ew)) > 0.2) continue;
                glm::dvec3 fc(0);
                for (auto& v : f.v) fc += v;
                fc /= double(f.v.size());
                glm::dvec3 center = fc;
                center[a] = s0;
                center[t] = tIn + sign * Rin;
                glm::dvec3 radial = fc - center;
                radial[w] = 0;
                if (glm::length(radial) < 1e-9 || std::abs(glm::dot(glm::normalize(radial), f.n)) < 0.97) continue;
                f.smooth = solid::Smooth::Cylinder;
                f.sc = center;
                f.sa = ew;
                f.sr = glm::length(radial);
            }
            out.pieces.push_back(std::move(bent));
        }
    }
    // Ankerpunkte von Verbindungen mitbiegen
    for (auto& j : joints_) {
        if (j->a == id) j->anchorA = bend(j->anchorA);
        if (j->b == id) j->anchorB = bend(j->anchorB);
    }
    if (info)
        *info = fmtNum(angleDeg, 1) + "° around an inner radius of " + fmtNum(Rin * 1000, 1) + " mm, bend zone " +
                fmtNum(L * 1000, 1) + " mm (thickness " + fmtNum(T * 1000, 1) + " mm)";
    return applyResult(*o, std::move(out), nullptr, err);
}

// ============================================================================ Axt

bool World::chop(uint32_t id, const glm::dvec3& p, glm::dvec3 toward, double* removed, std::string* err) {
    Object* o = solidPart(*this, id, err, "chopped");
    if (!o) return false;
    const Material& m = materials()[(size_t)o->material];
    if (m.cls != MaterialClass::Wood) {
        if (err) *err = "the axe only bites into wood (" + std::string(m.name) + " is too hard or too soft)";
        return false;
    }
    toward.y = 0;
    toward = glm::length(toward) > 1e-6 ? glm::normalize(toward) : glm::dvec3(1, 0, 0);
    // die Klinge trifft dort, wo das Holz in Schlagrichtung jetzt ist (in einer Kerbe: ihr Grund)
    glm::dvec3 hitP = p;
    {
        RayHit h;
        const uint32_t self = o->id;
        if (raycast(p + toward * 0.6 + glm::dvec3(0, 1e-4, 0), -toward, h) && h.obj && h.obj->id == self && h.t < 1.2) hitP = h.point;
        o = find(self);
        if (!o) return false;
    }
    const glm::dvec3& pt = hitP;
    // Keil: Schneide innen, waagrecht quer zum Hieb; öffnet sich nach außen. Leichtes Holz gibt mehr nach
    const double depth = std::clamp(0.06 - m.density * 0.00003, 0.025, 0.06), half = 0.05, w = 0.3, out = 0.3;
    const glm::dvec3 up(0, 1, 0), side = glm::normalize(glm::cross(up, toward));
    const glm::dvec3 edge = pt - toward * depth, outer = pt + toward * out;
    const double hh = half * (depth + out) / depth;
    std::vector<glm::dvec3> pts = {edge + side * w, edge - side * w};
    for (double a : {-1.0, 1.0})
        for (double b : {-1.0, 1.0}) pts.push_back(outer + side * (w * a) + up * (hh * b));
    solid::Piece wedge = solid::hullPiece(pts);
    if (wedge.faces.empty()) return false;
    solid::transform(wedge, glm::inverse(frameOf(*o)));
    double rem = 0;
    solid::Solid res = solid::subtract(o->solid, solid::planesOf(wedge), &rem);
    if (rem < 1e-9) {
        if (err) *err = "the axe misses (aim at the part)";
        return false;
    }
    if (removed) *removed = rem;
    fellHint_ = -toward;  // fällt der Baum, dann vom Schlagenden weg
    const bool ok = applyResult(*o, std::move(res), nullptr, err);
    fellHint_ = glm::dvec3(0);
    return ok;
}
