#include "Collision.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <tuple>
#include <atomic>
#include <thread>

#include "Physics.h"

namespace {

struct Box {
    glm::dvec3 mn{1e30}, mx{-1e30};
    void add(const glm::dvec3& p) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    void add(const Box& b) {
        mn = glm::min(mn, b.mn);
        mx = glm::max(mx, b.mx);
    }
    bool overlaps(const Box& b, double tol) const {
        return mn.x <= b.mx.x + tol && b.mn.x <= mx.x + tol && mn.y <= b.mx.y + tol && b.mn.y <= mx.y + tol && mn.z <= b.mx.z + tol &&
               b.mn.z <= mx.z + tol;
    }
    double distance(const glm::dvec3& p) const { return glm::length(glm::max(glm::dvec3(0), glm::max(mn - p, p - mx))); }
};

std::vector<glm::dvec3> uniquePoints(const solid::Piece& p) {
    std::map<std::tuple<long long, long long, long long>, glm::dvec3> m;
    for (const auto& f : p.faces)
        for (const auto& v : f.v) m.emplace(std::make_tuple(std::llround(v.x * 1e7), std::llround(v.y * 1e7), std::llround(v.z * 1e7)), v);
    std::vector<glm::dvec3> r;
    r.reserve(m.size());
    for (auto& [k, v] : m) r.push_back(v);
    return r;
}

Box boxOf(const solid::Piece& p) {
    Box b;
    for (const auto& f : p.faces)
        for (const auto& v : f.v) b.add(v);
    return b;
}

// Abstand eines Punkts zum Material (untere Schranke über die Flächenebenen, genau auf Flächen)
double distanceToMaterial(const glm::dvec3& p, const std::vector<solid::Piece>& pieces, const std::vector<Box>& boxes) {
    double best = 1e30;
    for (size_t i = 0; i < pieces.size(); ++i) {
        if (boxes[i].distance(p) >= best) continue;
        double d = 0;
        for (const auto& f : pieces[i].faces) d = std::max(d, glm::dot(f.n, p) - f.d);
        best = std::min(best, d);
        if (best <= 0) return 0;
    }
    return best;
}

solid::Piece hullOf(const std::vector<glm::dvec3>& pts, double* vol) {
    std::vector<glm::dvec4> pl;
    if (!Physics::hullPlanes(pts, pl)) return {};
    std::vector<solid::Plane> planes;
    double far = 0;
    for (const auto& p : pts) far = std::max({far, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
    for (const auto& q : pl) {
        solid::Plane p;
        p.n = glm::dvec3(q);
        p.d = q.w;
        planes.push_back(p);
    }
    solid::Piece h = solid::fromPlanes(planes, far * 2 + 1.0);
    if (vol) *vol = h.faces.empty() ? 0.0 : solid::volume(h);
    return h;
}

double fillDepthOf(const solid::Piece& hull, const std::vector<solid::Piece>& pieces, const std::vector<Box>& boxes, double stopAbove) {
    if (hull.faces.empty()) return 1e30;
    Box hb = boxOf(hull);
    solid::Solid voids;
    voids.pieces.push_back(hull);
    for (size_t i = 0; i < pieces.size() && !voids.pieces.empty(); ++i) {
        if (!boxes[i].overlaps(hb, 1e-9)) continue;
        voids = solid::subtract(voids, solid::planesOf(pieces[i]), nullptr, false);
    }
    double depth = 0;
    for (const auto& v : voids.pieces) {
        glm::dvec3 c;
        if (solid::volume(v, &c) < 1e-15) continue;
        std::vector<glm::dvec3> samples = {c};
        for (const auto& f : v.faces)
            for (const auto& p : f.v) {
                samples.push_back(p);
                samples.push_back(0.5 * (p + c));
            }
        for (const auto& s : samples) {
            depth = std::max(depth, distanceToMaterial(s, pieces, boxes));
            if (depth > stopAbove) return depth;
        }
    }
    return depth;
}

// kleinste Ausdehnung einer konvexen Punktwolke (über alle Flächenrichtungen der Hülle)
double thicknessOf(const std::vector<glm::dvec3>& pts) {
    std::vector<glm::dvec4> pl;
    if (!Physics::hullPlanes(pts, pl)) return 0;
    double t = 1e30;
    for (const auto& p : pl) {
        const glm::dvec3 n(p);
        double lo = 1e30, hi = -1e30;
        for (const auto& q : pts) {
            double d = glm::dot(n, q);
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
        t = std::min(t, hi - lo);
    }
    return t;
}

constexpr double kPi = 3.14159265358979323846;

void basisOf(const glm::dvec3& a, glm::dvec3& u, glm::dvec3& w) {
    const glm::dvec3 t = std::abs(a.x) < 0.9 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
    u = glm::normalize(t - a * glm::dot(a, t));
    w = glm::cross(a, u);
}

bool insidePieces(const glm::dvec3& p, const std::vector<solid::Piece>& pieces, double eps) {
    for (const auto& pc : pieces) {
        bool in = !pc.faces.empty();
        for (const auto& f : pc.faces)
            if (glm::dot(f.n, p) - f.d > eps) {
                in = false;
                break;
            }
        if (in) return true;
    }
    return false;
}

// Wie weit liegt ab Abstand r0 entlang des Strahls durchgehend Material? (m)
double materialRun(const glm::dvec3& o, const glm::dvec3& dir, double r0, const std::vector<solid::Piece>& pieces) {
    std::vector<std::pair<double, double>> iv;
    for (const auto& pc : pieces) {
        double t0 = 0, t1 = 1e30;
        bool ok = !pc.faces.empty();
        for (const auto& f : pc.faces) {
            const double dn = glm::dot(f.n, dir), dist = f.d - glm::dot(f.n, o);
            if (std::abs(dn) < 1e-15) {
                if (dist < -1e-9) ok = false;
            } else if (dn > 0) {
                t1 = std::min(t1, dist / dn);
            } else {
                t0 = std::max(t0, dist / dn);
            }
            if (!ok || t0 > t1) {
                ok = false;
                break;
            }
        }
        if (ok && t1 > r0) iv.push_back({t0, t1});
    }
    std::sort(iv.begin(), iv.end());
    double end = r0;
    bool any = false;
    for (const auto& [a, b] : iv) {
        if (a > end + (any ? 1e-6 : 0.03 * r0)) break;  // Bohrung ist ein Vieleck: Ecken liegen etwas außerhalb
        end = std::max(end, b);
        any = true;
    }
    return any ? end - r0 : 0.0;
}

// Winkelabdeckung (Anteil von 360°) einer Menge von Zylinder-Teilflächen um die Achse
struct AngleCover {
    std::vector<char> bins = std::vector<char>(720, 0);
    void add(double a0, double a1) {
        const double step = 2.0 * kPi / bins.size();
        for (double a = a0; a <= a1 + 1e-12; a += step * 0.5) {
            double x = std::fmod(a, 2.0 * kPi);
            if (x < 0) x += 2.0 * kPi;
            bins[std::min(bins.size() - 1, size_t(x / step))] = 1;
        }
    }
    double fraction() const {
        size_t n = 0;
        for (char c : bins) n += c;
        return double(n) / bins.size();
    }
};

// Winkelbereich einer Fläche um die Achse (line + t·a)
void faceAngles(const solid::Face& f, const glm::dvec3& line, const glm::dvec3& a, const glm::dvec3& u, const glm::dvec3& w, double& a0,
                double& a1, double& t0, double& t1) {
    glm::dvec3 c(0);
    for (const auto& v : f.v) c += v;
    c /= double(f.v.size());
    auto ang = [&](const glm::dvec3& p) {
        glm::dvec3 r = p - line;
        r -= a * glm::dot(r, a);
        return std::atan2(glm::dot(r, w), glm::dot(r, u));
    };
    const double ac = ang(c);
    double lo = 0, hi = 0;
    t0 = 1e30;
    t1 = -1e30;
    for (const auto& v : f.v) {
        double d = ang(v) - ac;
        while (d > kPi) d -= 2 * kPi;
        while (d < -kPi) d += 2 * kPi;
        lo = std::min(lo, d);
        hi = std::max(hi, d);
        const double t = glm::dot(v - line, a);
        t0 = std::min(t0, t);
        t1 = std::max(t1, t);
    }
    a0 = ac + lo;
    a1 = ac + hi;
}

glm::dvec3 canonicalAxis(glm::dvec3 a) {
    a = glm::normalize(a);
    if (a.x < -1e-9 || (std::abs(a.x) <= 1e-9 && (a.y < -1e-9 || (std::abs(a.y) <= 1e-9 && a.z < 0)))) a = -a;
    return a;
}

using CylKey = std::tuple<long long, long long, long long, long long, long long, long long, long long>;
CylKey cylKey(const glm::dvec3& a, const glm::dvec3& line, double r) {
    return {std::llround(a.x * 1e4), std::llround(a.y * 1e4), std::llround(a.z * 1e4), std::llround(line.x * 1e5),
            std::llround(line.y * 1e5),   std::llround(line.z * 1e5), std::llround(r * 1e5)};
}

// Ist das Stück ein voller Zylinder (Mantel aus Zylinderflächen + zwei ebene Deckel quer zur Achse)?
bool cylinderPiece(const solid::Piece& p, CylGeom& out) {
    const solid::Face* side0 = nullptr;
    glm::dvec3 a(0), line(0);
    double r = 0;
    for (const auto& f : p.faces) {
        if (f.smooth != solid::Smooth::Cylinder || f.sr <= 0) continue;
        a = canonicalAxis(f.sa);
        line = f.sc - a * glm::dot(f.sc, a);
        r = f.sr;
        side0 = &f;
        break;
    }
    if (!side0) return false;
    glm::dvec3 u, w;
    basisOf(a, u, w);
    AngleCover cover;
    int sides = 0, capPos = 0, capNeg = 0;
    double t0 = 1e30, t1 = -1e30;
    for (const auto& f : p.faces) {
        const double na = glm::dot(f.n, a);
        if (std::abs(na) > 0.999) {
            (na > 0 ? capPos : capNeg)++;
            continue;
        }
        if (f.smooth != solid::Smooth::Cylinder || std::abs(na) > 0.02) return false;
        const glm::dvec3 fa = canonicalAxis(f.sa), fl = f.sc - fa * glm::dot(f.sc, fa);
        if (glm::length(fa - a) > 1e-6 || glm::length(fl - line) > 1e-7 || std::abs(f.sr - r) > 1e-7) return false;
        glm::dvec3 c(0);
        for (const auto& v : f.v) c += v;
        c /= double(f.v.size());
        glm::dvec3 rv = c - line;
        rv -= a * glm::dot(rv, a);
        if (glm::dot(f.n, rv) <= 0) return false;  // nach innen gewölbt: Bohrung
        double a0, a1, ft0, ft1;
        faceAngles(f, line, a, u, w, a0, a1, ft0, ft1);
        cover.add(a0, a1);
        ++sides;
    }
    if (sides < 6 || capPos != 1 || capNeg != 1 || cover.fraction() < 0.98) return false;
    for (const auto& f : p.faces)
        for (const auto& v : f.v) {
            const double t = glm::dot(v - line, a);
            t0 = std::min(t0, t);
            t1 = std::max(t1, t);
        }
    out.axis = a;
    out.radius = r;
    out.center = line + a * (0.5 * (t0 + t1));
    out.halfLength = 0.5 * (t1 - t0);
    return out.halfLength > 1e-5;
}

// Ragt eine (zusammengefasste) Hülle in eine Bohrung hinein? Dann darf sie so nicht gebaut werden.
bool intrudesBore(const solid::Piece& hull, const Box& hb, const std::vector<BoreGeom>& bores) {
    for (const BoreGeom& b : bores) {
        const double ext = b.radius + b.halfLength;
        Box bb;
        bb.add(b.center - glm::dvec3(ext));
        bb.add(b.center + glm::dvec3(ext));
        if (!bb.overlaps(hb, 1e-6)) continue;
        for (int k = 0; k < 5; ++k) {
            const double t = b.halfLength * (-0.9 + 1.8 * k / 4.0);
            for (int i = 0; i < 24; ++i) {
                const double ang = 2 * kPi * i / 24;
                const glm::dvec3 p = b.center + b.axis * t + (std::cos(ang) * b.u + std::sin(ang) * b.w) * (b.radius * 0.999);
                bool in = true;
                for (const auto& f : hull.faces)
                    if (glm::dot(f.n, p) - f.d > -1e-7) {
                        in = false;
                        break;
                    }
                if (in) return true;
            }
        }
    }
    return false;
}

std::vector<std::vector<glm::vec3>> clusterHulls(const std::vector<solid::Piece>& clusterPieces, const std::vector<solid::Piece>& pieces,
                                                 int mode, const std::vector<BoreGeom>& bores, CollisionBuildInfo* info);

} // namespace

double hullFillDepth(const std::vector<glm::dvec3>& points, const std::vector<solid::Piece>& pieces, double stopAbove) {
    std::vector<Box> boxes;
    for (const auto& p : pieces) boxes.push_back(boxOf(p));
    return fillDepthOf(hullOf(points, nullptr), pieces, boxes, stopAbove);
}

std::vector<BoreGeom> findBores(const solid::Solid& s) {
    struct Group {
        glm::dvec3 axis, line;
        double r;
        std::vector<const solid::Face*> faces;
    };
    std::map<CylKey, Group> groups;
    for (const auto& pc : s.pieces)
        for (const auto& f : pc.faces) {
            if (f.smooth != solid::Smooth::Cylinder || f.sr < 0.001 || f.v.size() < 3 || f.internal) continue;
            const glm::dvec3 a = canonicalAxis(f.sa);
            if (std::abs(glm::dot(f.n, a)) > 0.02) continue;  // Kegel (Senkung)
            glm::dvec3 c(0);
            for (const auto& v : f.v) c += v;
            c /= double(f.v.size());
            const glm::dvec3 line = f.sc - a * glm::dot(f.sc, a);
            glm::dvec3 rv = c - line;
            rv -= a * glm::dot(rv, a);
            const double rl = glm::length(rv);
            if (rl < 0.8 * f.sr || rl > 1.2 * f.sr) continue;
            if (glm::dot(f.n, rv) >= 0) continue;  // nach außen gewölbt: Stift, keine Bohrung
            Group& g = groups[cylKey(a, line, f.sr)];
            g.axis = a;
            g.line = line;
            g.r = f.sr;
            g.faces.push_back(&f);
        }
    std::vector<BoreGeom> out;
    for (auto& [key, g] : groups) {
        glm::dvec3 u, w;
        basisOf(g.axis, u, w);
        struct FI {
            double t0, t1, a0, a1;
        };
        std::vector<FI> fi;
        for (const solid::Face* f : g.faces) {
            FI x;
            faceAngles(*f, g.line, g.axis, u, w, x.a0, x.a1, x.t0, x.t1);
            fi.push_back(x);
        }
        std::sort(fi.begin(), fi.end(), [](const FI& a, const FI& b) { return a.t0 < b.t0; });
        // zusammenhängende Abschnitte entlang der Achse (eine unterbrochene Bohrung = mehrere)
        size_t i = 0;
        while (i < fi.size()) {
            double T0 = fi[i].t0, T1 = fi[i].t1;
            AngleCover cover;
            size_t j = i;
            while (j < fi.size() && fi[j].t0 <= T1 + 5e-5) {
                T1 = std::max(T1, fi[j].t1);
                cover.add(fi[j].a0, fi[j].a1);
                ++j;
            }
            i = j;
            if (cover.fraction() < 0.98) continue;
            BoreGeom b;
            b.axis = g.axis;
            b.u = u;
            b.w = w;
            b.radius = g.r;
            b.center = g.line + g.axis * (0.5 * (T0 + T1));
            b.halfLength = 0.5 * (T1 - T0);
            if (b.radius < 0.001 || b.halfLength < 0.0005) continue;
            b.openTop = !insidePieces(b.center + b.axis * (b.halfLength + 3e-4) + u * (0.5 * b.radius), s.pieces, 1e-9);
            b.openBottom = !insidePieces(b.center - b.axis * (b.halfLength + 3e-4) + u * (0.5 * b.radius), s.pieces, 1e-9);
            double minEdge = 1e30;
            for (int k = 0; k < 8; ++k) {
                const double ang = kPi * k / 4.0 + 0.013;  // nicht genau auf den Trennebenen der Stücke
                b.edge[k] = materialRun(b.center, std::cos(ang) * u + std::sin(ang) * w, b.radius, s.pieces);
                minEdge = std::min(minEdge, b.edge[k]);
            }
            b.band = std::clamp(0.15 * b.radius, 0.0004, 0.002);
            if (minEdge < 1e29) b.band = std::min(b.band, 0.4 * minEdge);
            if (b.band < 0.0001) continue;  // Wand zu dünn: normale Hüllen
            out.push_back(b);
        }
    }
    return out;
}

CollisionShape buildCollisionShape(const solid::Solid& s, int mode, CollisionBuildInfo* info) {
    CollisionShape out;
    if (mode == 2) {
        out.hulls = buildCollisionHulls(s, 2, info);
        return out;
    }
    out.bores = findBores(s);
    // Kollisionsgeometrie: um jede Bohrung ein 12-Eck mit Abstand radius + band wegnehmen (die Ringzone gehört der
    // exakten Lochwand). Die Tortenstück-Zerlegung um das 12-Eck ergibt kräftige Hüllen.
    solid::Solid cs = s;
    for (const BoreGeom& b : out.bores) {
        std::vector<solid::Plane> tool;
        for (int k = 0; k < 12; ++k) {
            const double ang = 2 * kPi * (k + 0.5) / 12;
            solid::Plane pl;
            pl.n = std::cos(ang) * b.u + std::sin(ang) * b.w;
            pl.d = glm::dot(pl.n, b.center) + b.radius + b.band;
            pl.smooth = solid::Smooth::Cylinder;
            pl.sc = b.center;
            pl.sa = b.axis;
            pl.sr = b.radius + b.band;
            tool.push_back(pl);
        }
        solid::Plane top, bottom;
        top.n = b.axis;
        top.d = glm::dot(b.axis, b.center) + b.halfLength + 1e-5;
        bottom.n = -b.axis;
        bottom.d = -glm::dot(b.axis, b.center) + b.halfLength + 1e-5;
        tool.push_back(top);
        tool.push_back(bottom);
        cs = solid::subtract(cs, tool);
    }
    std::vector<solid::Piece> pieces, rest;
    for (const auto& p : cs.pieces)
        if (!p.faces.empty() && solid::volume(p) > 1e-14) pieces.push_back(p);
    // Volle Zylinderstücke (gedrehte Stifte, Achsen) bekommen eine exakte Zylinderform
    std::vector<CylGeom> cyl;
    for (const auto& p : pieces) {
        CylGeom c;
        if (cylinderPiece(p, c)) cyl.push_back(c);
        else rest.push_back(p);
    }
    // aufeinanderliegende Zylinder gleicher Achse und gleichen Radius zusammenfassen
    std::sort(cyl.begin(), cyl.end(), [](const CylGeom& a, const CylGeom& b) {
        const CylKey ka = cylKey(a.axis, a.center - a.axis * glm::dot(a.center, a.axis), a.radius);
        const CylKey kb = cylKey(b.axis, b.center - b.axis * glm::dot(b.center, b.axis), b.radius);
        if (ka != kb) return ka < kb;
        return glm::dot(a.center, a.axis) < glm::dot(b.center, b.axis);
    });
    for (const CylGeom& c : cyl) {
        if (!out.cylinders.empty()) {
            CylGeom& p = out.cylinders.back();
            const glm::dvec3 lp = p.center - p.axis * glm::dot(p.center, p.axis), lc = c.center - c.axis * glm::dot(c.center, c.axis);
            const double pe = glm::dot(p.center, p.axis) + p.halfLength, cs0 = glm::dot(c.center, c.axis) - c.halfLength;
            if (glm::length(p.axis - c.axis) < 1e-6 && glm::length(lp - lc) < 1e-7 && std::abs(p.radius - c.radius) < 1e-7 && cs0 <= pe + 1e-6) {
                const double s0 = glm::dot(p.center, p.axis) - p.halfLength, e1 = std::max(pe, glm::dot(c.center, c.axis) + c.halfLength);
                p.center = lp + p.axis * (0.5 * (s0 + e1));
                p.halfLength = 0.5 * (e1 - s0);
                continue;
            }
        }
        out.cylinders.push_back(c);
    }
    if (!rest.empty()) out.hulls = clusterHulls(rest, pieces, mode, out.bores, info);
    if (info) {
        info->pieces = s.pieces.size();
        info->hulls = out.hulls.size();
        info->bores = out.bores.size();
        info->cylinders = out.cylinders.size();
    }
    return out;
}

std::vector<std::vector<glm::vec3>> buildCollisionHulls(const solid::Solid& s, int mode, CollisionBuildInfo* info) {
    std::vector<solid::Piece> pieces;
    for (const auto& p : s.pieces)
        if (!p.faces.empty() && solid::volume(p) > 1e-14) pieces.push_back(p);
    return clusterHulls(pieces, pieces, mode, {}, info);
}

namespace {

std::vector<std::vector<glm::vec3>> clusterHulls(const std::vector<solid::Piece>& clusterPieces, const std::vector<solid::Piece>& pieces,
                                                 int mode, const std::vector<BoreGeom>& bores, CollisionBuildInfo* info) {
    auto toFloat = [](const std::vector<glm::dvec3>& v) {
        std::vector<glm::vec3> r;
        r.reserve(v.size());
        for (auto& p : v) r.emplace_back(p);
        return r;
    };
    std::vector<glm::dvec3> all;
    for (const auto& p : clusterPieces) {
        auto u = uniquePoints(p);
        all.insert(all.end(), u.begin(), u.end());
    }
    if (info) {
        info->pieces = pieces.size();
        info->tolerance = 0;
    }
    auto intrudes = [&](const solid::Piece& h) { return !bores.empty() && !h.faces.empty() && intrudesBore(h, boxOf(h), bores); };
    if (clusterPieces.size() <= 1 || mode == 2) {
        if (info) info->hulls = 1;
        return {toFloat(all)};
    }
    double tol = mode == 1 ? 5e-5 : 5e-4;
    std::vector<Box> boxes;
    for (const auto& p : pieces) boxes.push_back(boxOf(p));

    // Schnellster Fall: eine Hülle für alles (fast konvexe Teile, z.B. schräg gesägt oder abgerundet)
    const solid::Piece hullAll = hullOf(all, nullptr);
    if (fillDepthOf(hullAll, pieces, boxes, tol) <= tol && !intrudes(hullAll)) {
        if (info) {
            info->hulls = 1;
            info->tolerance = tol;
        }
        return {toFloat(all)};
    }

    struct Cluster {
        std::vector<glm::dvec3> pts;
        double vol = 0;
        Box box;
        bool alive = true;
    };
    std::vector<Cluster> cl(clusterPieces.size());
    for (size_t i = 0; i < clusterPieces.size(); ++i) {
        cl[i].pts = uniquePoints(clusterPieces[i]);
        cl[i].vol = solid::volume(clusterPieces[i]);
        cl[i].box = boxOf(clusterPieces[i]);
    }
    struct Pair {
        size_t a, b;
        double cost;    // zusätzliches Volumen der gemeinsamen Hülle
        bool blocked;   // füllt eine Lücke -> nicht verschmelzen
    };
    auto evaluate = [&](size_t a, size_t b) {
        std::vector<glm::dvec3> u = cl[a].pts;
        u.insert(u.end(), cl[b].pts.begin(), cl[b].pts.end());
        double v = 0;
        solid::Piece h = hullOf(u, &v);
        Pair p{std::min(a, b), std::max(a, b), h.faces.empty() ? 1e30 : v - cl[a].vol - cl[b].vol, h.faces.empty() || intrudes(h)};
        return p;
    };
    // Alle Nachbarpaare bewerten (Hülle bauen): unabhängig voneinander, daher auf alle Kerne verteilt
    std::vector<std::pair<size_t, size_t>> todo;
    for (size_t a = 0; a < cl.size(); ++a)
        for (size_t b = a + 1; b < cl.size(); ++b)
            if (cl[a].box.overlaps(cl[b].box, 1e-6)) todo.emplace_back(a, b);
    std::vector<Pair> pairs(todo.size());
    {
        const size_t nThreads = std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 16);
        std::atomic<size_t> next{0};
        auto work = [&]() {
            for (size_t i; (i = next.fetch_add(1)) < todo.size();) pairs[i] = evaluate(todo[i].first, todo[i].second);
        };
        std::vector<std::thread> pool;
        for (size_t t = 1; t < nThreads && todo.size() > 32; ++t) pool.emplace_back(work);
        work();
        for (auto& th : pool) th.join();
    }

    size_t alive = cl.size();
    for (int round = 0; round < 8; ++round) {
        for (;;) {
            size_t best = SIZE_MAX;
            for (size_t i = 0; i < pairs.size(); ++i)
                if (!pairs[i].blocked && (best == SIZE_MAX || pairs[i].cost < pairs[best].cost)) best = i;
            if (best == SIZE_MAX) break;
            const size_t a = pairs[best].a, b = pairs[best].b;
            std::vector<glm::dvec3> u = cl[a].pts;
            u.insert(u.end(), cl[b].pts.begin(), cl[b].pts.end());
            // fast nichts dazu (z.B. zwei Hälften eines Quaders): sofort; sonst prüfen, wie tief die Hülle Lücken füllt
            if (pairs[best].cost > 1e-13 && fillDepthOf(hullOf(u, nullptr), pieces, boxes, tol) > tol) {
                pairs[best].blocked = true;
                continue;
            }
            cl[a].pts = std::move(u);
            cl[a].vol += cl[b].vol;
            cl[a].box.add(cl[b].box);
            cl[b].alive = false;
            cl[b].pts.clear();
            --alive;
            std::vector<Pair> next;
            next.reserve(pairs.size());
            for (const Pair& p : pairs)
                if (p.a != a && p.b != a && p.a != b && p.b != b) next.push_back(p);
            for (size_t k = 0; k < cl.size(); ++k)
                if (cl[k].alive && k != a && cl[a].box.overlaps(cl[k].box, 1e-6)) next.push_back(evaluate(a, k));
            pairs.swap(next);
        }
        // Sehr viele Hüllen (z.B. Lochblech): Toleranz verdoppeln, damit die Physik schnell bleibt
        if (alive <= 64 || mode == 1) break;
        tol *= 2;
        for (auto& p : pairs) p.blocked = false;
    }
    // Hauchdünne Hüllen (z.B. Splitter neben schrägen Schnitten) mit dem besten Nachbarn verschmelzen oder weglassen:
    // An ihnen findet die Physik oft die falsche Richtung zum Trennen. Bei einem dünnen Teil (Blech) ist dünn normal.
    glm::dvec3 smn(1e30), smx(-1e30);
    for (const auto& b : boxes) {
        smn = glm::min(smn, b.mn);
        smx = glm::max(smx, b.mx);
    }
    const glm::dvec3 sext = smx - smn;
    const double thinLimit = std::min(0.0015, 0.4 * std::min({sext.x, sext.y, sext.z}));
    double totalVol = 0;
    for (auto& c : cl) totalVol += c.alive ? c.vol : 0.0;
    for (bool changed = true; changed && mode != 1;) {
        changed = false;
        for (size_t i = 0; i < cl.size(); ++i) {
            if (!cl[i].alive || alive <= 1 || thicknessOf(cl[i].pts) >= thinLimit) continue;
            size_t best = SIZE_MAX;
            double bestCost = 1e30;
            for (size_t k = 0; k < cl.size(); ++k) {
                if (k == i || !cl[k].alive || !cl[i].box.overlaps(cl[k].box, 1e-6)) continue;
                std::vector<glm::dvec3> u = cl[i].pts;
                u.insert(u.end(), cl[k].pts.begin(), cl[k].pts.end());
                double v = 0;
                solid::Piece h = hullOf(u, &v);
                if (h.faces.empty()) continue;
                double cost = v - cl[i].vol - cl[k].vol;
                if (cost < bestCost && fillDepthOf(h, pieces, boxes, 4 * tol) <= 4 * tol && !intrudes(h)) {
                    bestCost = cost;
                    best = k;
                }
            }
            if (best != SIZE_MAX) {
                cl[best].pts.insert(cl[best].pts.end(), cl[i].pts.begin(), cl[i].pts.end());
                cl[best].vol += cl[i].vol;
                cl[best].box.add(cl[i].box);
            } else if (cl[i].vol > 0.02 * totalVol) {
                continue;  // ein großes dünnes Stück gehört zur Form
            }
            cl[i].alive = false;
            --alive;
            changed = true;
        }
    }
    std::vector<std::vector<glm::vec3>> out;
    for (auto& c : cl)
        if (c.alive) out.push_back(toFloat(c.pts));
    if (info) {
        info->hulls = out.size();
        info->tolerance = tol;
    }
    return out;
}

} // namespace
