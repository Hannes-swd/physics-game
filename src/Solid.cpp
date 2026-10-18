#include "Solid.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <tuple>

#include <glm/gtc/matrix_transform.hpp>

namespace solid {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-9;      // Abstandstoleranz (m)
constexpr double kVolEps = 1e-13;  // kleinstes erlaubtes Teilstück (m³)
constexpr double kAreaEps = 1e-13; // kleinste erlaubte Fläche (m²)

void planeBasis(const dvec3& n, dvec3& u, dvec3& w) {
    dvec3 a = std::abs(n.x) < 0.9 ? dvec3(1, 0, 0) : dvec3(0, 1, 0);
    u = glm::normalize(glm::cross(a, n));
    w = glm::cross(n, u);  // (u, w, n) ist rechtshändig
}

double cross2(const dvec2& o, const dvec2& a, const dvec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Konvexe Hülle (Andrew Monotone Chain). Liefert Indizes gegen den Uhrzeigersinn.
std::vector<int> hull2D(const std::vector<dvec2>& p) {
    std::vector<int> idx(p.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return p[a].x < p[b].x || (p[a].x == p[b].x && p[a].y < p[b].y);
    });
    double scale = 0;
    for (const auto& q : p) scale = std::max({scale, std::abs(q.x), std::abs(q.y)});
    const double tolA = kEps * std::max(scale, 1e-3) * 0.5;

    std::vector<int> u;
    for (int i : idx)
        if (u.empty() || glm::length(p[i] - p[u.back()]) > kEps) u.push_back(i);
    if (u.size() < 3) return u;

    std::vector<int> h(2 * u.size());
    int k = 0;
    for (size_t i = 0; i < u.size(); ++i) {
        while (k >= 2 && cross2(p[h[k - 2]], p[h[k - 1]], p[u[i]]) <= tolA) --k;
        h[k++] = u[i];
    }
    for (int i = (int)u.size() - 2, t = k + 1; i >= 0; --i) {
        while (k >= t && cross2(p[h[k - 2]], p[h[k - 1]], p[u[i]]) <= tolA) --k;
        h[k++] = u[i];
    }
    h.resize(std::max(0, k - 1));
    return h;
}

// Ordnet koplanare Punkte als konvexes Polygon gegen den Uhrzeigersinn um n.
std::vector<dvec3> orderConvex(const dvec3& n, const std::vector<dvec3>& pts) {
    dvec3 u, w;
    planeBasis(n, u, w);
    std::vector<dvec2> p2(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) p2[i] = dvec2(glm::dot(pts[i], u), glm::dot(pts[i], w));
    std::vector<int> h = hull2D(p2);
    std::vector<dvec3> out;
    out.reserve(h.size());
    for (int i : h) out.push_back(pts[i]);
    return out;
}

Face makeFace(const dvec3& n, const std::vector<dvec3>& pts) {
    Face f;
    f.n = n;
    f.v = orderConvex(n, pts);
    f.d = f.v.empty() ? 0.0 : glm::dot(n, f.v[0]);
    return f;
}

bool lexLess(const dvec3& a, const dvec3& b) {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}

// Schnittpunkt einer Kante mit der Ebene, unabhängig von der Kantenrichtung (bitgleich).
dvec3 edgeCut(dvec3 a, dvec3 b, double sa, double sb) {
    if (lexLess(b, a)) {
        std::swap(a, b);
        std::swap(sa, sb);
    }
    double t = sa / (sa - sb);
    return a + (b - a) * t;
}

void removeDuplicates(std::vector<dvec3>& v) {
    std::vector<dvec3> out;
    out.reserve(v.size());
    for (const auto& p : v)
        if (out.empty() || glm::length(p - out.back()) > kEps) out.push_back(p);
    while (out.size() > 1 && glm::length(out.front() - out.back()) <= kEps) out.pop_back();
    v.swap(out);
}

std::vector<dvec2> project(const Face& f, const dvec3& u, const dvec3& w, bool reverse) {
    std::vector<dvec2> r(f.v.size());
    for (size_t i = 0; i < f.v.size(); ++i) {
        const dvec3& p = f.v[reverse ? f.v.size() - 1 - i : i];
        r[i] = dvec2(glm::dot(p, u), glm::dot(p, w));
    }
    return r;
}

double polyArea(const std::vector<dvec2>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const dvec2& q = p[i];
        const dvec2& r = p[(i + 1) % p.size()];
        a += q.x * r.y - r.x * q.y;
    }
    return 0.5 * a;
}

// Schnittfläche zweier konvexer CCW-Polygone (Sutherland-Hodgman).
double convexOverlapArea(std::vector<dvec2> a, const std::vector<dvec2>& b) {
    for (size_t i = 0; i < b.size() && !a.empty(); ++i) {
        const dvec2& e0 = b[i];
        const dvec2& e1 = b[(i + 1) % b.size()];
        std::vector<dvec2> out;
        for (size_t j = 0; j < a.size(); ++j) {
            const dvec2& p = a[j];
            const dvec2& q = a[(j + 1) % a.size()];
            double sp = cross2(e0, e1, p);
            double sq = cross2(e0, e1, q);
            if (sp >= 0) out.push_back(p);
            if ((sp >= 0) != (sq >= 0)) {
                double t = sp / (sp - sq);
                out.push_back(p + (q - p) * t);
            }
        }
        a.swap(out);
    }
    return a.size() >= 3 ? polyArea(a) : 0.0;
}

bool oppositeCoplanar(const Face& a, const Face& b) {
    return glm::dot(a.n, b.n) < -1.0 + 1e-9 && std::abs(a.d + b.d) < 1e-8;
}

double faceOverlap(const Face& a, const Face& b) {
    dvec3 u, w;
    planeBasis(a.n, u, w);
    return convexOverlapArea(project(a, u, w, false), project(b, u, w, true));
}

bool boxesOverlap(const dvec3& amn, const dvec3& amx, const dvec3& bmn, const dvec3& bmx, double tol) {
    return amn.x <= bmx.x + tol && bmn.x <= amx.x + tol && amn.y <= bmx.y + tol && bmn.y <= amx.y + tol &&
           amn.z <= bmx.z + tol && bmn.z <= amx.z + tol;
}

bool piecesTouch(const Piece& a, const Piece& b) {
    for (const Face& fa : a.faces)
        for (const Face& fb : b.faces)
            if (oppositeCoplanar(fa, fb) && faceOverlap(fa, fb) > 1e-10) return true;
    // Volumenüberlappung (z.B. Zahnradzähne, die in den Radkörper ragen)
    Piece inter = a, tmp;
    for (const Face& fb : b.faces) {
        Plane pl;
        pl.n = fb.n;
        pl.d = fb.d;
        if (!clip(inter, pl, tmp)) return false;
        inter = std::move(tmp);
    }
    return volume(inter) > 1e-12;
}

dvec3 smoothNormal(const Face& f, const dvec3& p) {
    dvec3 r;
    if (f.smooth == Smooth::Sphere) {
        r = p - f.sc;
    } else {
        dvec3 q = p - f.sc;
        r = q - f.sa * glm::dot(q, f.sa);
    }
    double len = glm::length(r);
    if (len < 1e-12) return f.n;
    r /= len;
    if (glm::dot(r, f.n) < 0) r = -r;
    // Nicht zu stark von der Flächennormale abweichen (Schutz bei groben Facetten)
    if (glm::dot(r, f.n) < 0.5) return f.n;
    return r;
}

std::vector<dvec3> regularPolygon(double radius, int seg) {
    std::vector<dvec3> r;
    for (int i = 0; i < seg; ++i) {
        double a = 2.0 * kPi * i / seg;
        r.emplace_back(radius * std::cos(a), 0.0, radius * std::sin(a));
    }
    return r;
}

// Radius eines regelmäßigen n-Ecks mit gleicher Fläche wie der Kreis
double areaEquivalentRadius(double r, int seg) {
    return r * std::sqrt(2.0 * kPi / (seg * std::sin(2.0 * kPi / seg)));
}

void markCylinderFaces(Piece& p, double minAbsRadialDot, double rA, double rB) {
    for (Face& f : p.faces) {
        if (std::abs(f.n.y) > 0.5) continue;
        dvec3 c(0);
        for (const auto& v : f.v) c += v;
        c /= double(f.v.size());
        dvec3 radial(c.x, 0, c.z);
        double len = glm::length(radial);
        if (len < 1e-12) continue;
        if (std::abs(glm::dot(radial / len, f.n)) < minAbsRadialDot) continue;
        f.smooth = Smooth::Cylinder;
        f.sc = dvec3(0);
        f.sa = dvec3(0, 1, 0);
        double apothem = std::abs(f.d);  // Abstand der Fläche von der Achse
        f.sr = std::abs(apothem - rA) <= std::abs(apothem - rB) ? rA : rB;
    }
}

} // namespace

// ============================================================================ Erzeugen

Piece makeBox(dvec3 h) {
    Piece p;
    const dvec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const dvec3& n : dirs) {
        std::vector<dvec3> pts;
        for (int i = 0; i < 8; ++i) {
            dvec3 c((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
            if (glm::dot(c, n) > 0) pts.push_back(c);
        }
        p.faces.push_back(makeFace(n, pts));
    }
    return p;
}

Piece fromPlanes(const std::vector<Plane>& planes, double extent) {
    Piece p = makeBox(dvec3(extent)), tmp;
    for (const Plane& pl : planes) {
        if (!clip(p, pl, tmp)) return Piece{};
        p = std::move(tmp);
    }
    return p;
}

Piece makePrism(const std::vector<dvec2>& polyIn, double y0, double y1) {
    std::vector<int> h = hull2D(polyIn);
    std::vector<dvec2> poly;
    for (int i : h) poly.push_back(polyIn[i]);
    if (poly.size() < 3) return Piece{};

    double extent = std::max(std::abs(y0), std::abs(y1));
    dvec2 c(0);
    for (const auto& q : poly) {
        c += q;
        extent = std::max({extent, std::abs(q.x), std::abs(q.y)});
    }
    c /= double(poly.size());

    std::vector<Plane> planes;
    Plane bottom;
    bottom.n = dvec3(0, -1, 0);
    bottom.d = -y0;
    Plane top;
    top.n = dvec3(0, 1, 0);
    top.d = y1;
    planes.push_back(bottom);
    planes.push_back(top);
    for (size_t i = 0; i < poly.size(); ++i) {
        dvec2 a = poly[i], b = poly[(i + 1) % poly.size()];
        dvec2 e = b - a;
        dvec2 n2 = glm::normalize(dvec2(e.y, -e.x));
        if (glm::dot(n2, (a + b) * 0.5 - c) < 0) n2 = -n2;
        Plane pl;
        pl.n = dvec3(n2.x, 0, n2.y);
        pl.d = glm::dot(pl.n, dvec3(a.x, 0, a.y));
        planes.push_back(pl);
    }
    return fromPlanes(planes, extent * 2 + 1.0);
}

Solid makeCylinder(double radius, double height, int seg) {
    seg = std::max(seg, 6);
    std::vector<dvec2> poly;
    double r = areaEquivalentRadius(radius, seg);
    for (int i = 0; i < seg; ++i) {
        double a = 2.0 * kPi * i / seg;
        poly.emplace_back(r * std::cos(a), r * std::sin(a));
    }
    Solid s;
    s.pieces.push_back(makePrism(poly, -height / 2, height / 2));
    markCylinderFaces(s.pieces[0], 0.95, radius, radius);
    return s;
}

Solid makeTube(double rOuter, double rInner, double height, int seg) {
    seg = std::max(seg, 8);
    Solid s;
    double ro = areaEquivalentRadius(rOuter, seg), ri = areaEquivalentRadius(rInner, seg);
    for (int k = 0; k < seg; ++k) {
        double a0 = 2.0 * kPi * k / seg, a1 = 2.0 * kPi * (k + 1) / seg;
        std::vector<dvec2> q = {{ri * std::cos(a0), ri * std::sin(a0)},
                                {ri * std::cos(a1), ri * std::sin(a1)},
                                {ro * std::cos(a1), ro * std::sin(a1)},
                                {ro * std::cos(a0), ro * std::sin(a0)}};
        Piece p = makePrism(q, -height / 2, height / 2);
        markCylinderFaces(p, 0.99, rOuter, rInner);
        s.pieces.push_back(std::move(p));
    }
    classifyInternalFaces(s);
    return s;
}

Solid makeSphere(double radius, int subdiv) {
    // Ikosaeder unterteilen, dann Tangentialebenen schneiden -> konvexes Polyeder
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<dvec3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                            {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& p : v) p = glm::normalize(p);
    std::vector<glm::ivec3> tri = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
                                   {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                   {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
                                   {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
    for (int s = 0; s < subdiv; ++s) {
        std::vector<glm::ivec3> nt;
        auto mid = [&](int a, int b) {
            v.push_back(glm::normalize(v[a] + v[b]));
            return int(v.size() - 1);
        };
        for (auto& f : tri) {
            int a = mid(f.x, f.y), b = mid(f.y, f.z), c = mid(f.z, f.x);
            nt.push_back({f.x, a, c});
            nt.push_back({f.y, b, a});
            nt.push_back({f.z, c, b});
            nt.push_back({a, b, c});
        }
        tri.swap(nt);
    }
    std::vector<Plane> planes;
    for (auto& f : tri) {
        dvec3 a = v[f.x], b = v[f.y], c = v[f.z];
        Plane pl;
        pl.n = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(pl.n, a) < 0) pl.n = -pl.n;
        pl.d = glm::dot(pl.n, a);
        planes.push_back(pl);
    }
    Piece p = fromPlanes(planes, 2.0);
    // auf gleiches Volumen wie die echte Kugel skalieren
    double vol = volume(p);
    double k = radius * std::cbrt((4.0 / 3.0 * kPi) / vol);
    for (Face& f : p.faces) {
        for (auto& q : f.v) q *= k;
        f.d *= k;
        f.smooth = Smooth::Sphere;
        f.sc = dvec3(0);
        f.sr = radius;
    }
    Solid s;
    s.pieces.push_back(std::move(p));
    return s;
}

Solid makeGear(int z, double m, double width, double bore) {
    z = std::clamp(z, 6, 200);
    const double alpha = 20.0 * kPi / 180.0;
    const double r = m * z / 2.0;
    const double ra = r + m;
    const double rf = r - 1.25 * m;
    const double rb = r * std::cos(alpha);
    const int ringSeg = std::max(24, 2 * z);
    bore = std::min(bore, rf * 1.6);

    Solid s;
    const double h0 = -width / 2, h1 = width / 2;
    if (bore > 1e-5) {
        double ri = areaEquivalentRadius(bore / 2, ringSeg);
        for (int k = 0; k < ringSeg; ++k) {
            double a0 = 2.0 * kPi * (k + 0.5) / ringSeg, a1 = 2.0 * kPi * (k + 1.5) / ringSeg;
            std::vector<dvec2> q = {{ri * std::cos(a0), ri * std::sin(a0)},
                                    {ri * std::cos(a1), ri * std::sin(a1)},
                                    {rf * std::cos(a1), rf * std::sin(a1)},
                                    {rf * std::cos(a0), rf * std::sin(a0)}};
            Piece p = makePrism(q, h0, h1);
            markCylinderFaces(p, 0.99, rf, bore / 2);
            s.pieces.push_back(std::move(p));
        }
    } else {
        std::vector<dvec2> q;
        for (int k = 0; k < ringSeg; ++k) {
            double a = 2.0 * kPi * (k + 0.5) / ringSeg;
            q.emplace_back(rf * std::cos(a), rf * std::sin(a));
        }
        Piece p = makePrism(q, h0, h1);
        markCylinderFaces(p, 0.99, rf, rf);
        s.pieces.push_back(std::move(p));
    }

    // Zähne: Evolventenflanken
    const double sag = rf * (1.0 - std::cos(kPi / ringSeg));
    const double rBase = rf - sag - 0.05 * m;
    const double psiP = kPi / (2.0 * z);
    auto inv = [](double a) { return std::tan(a) - a; };
    const double invA = inv(alpha);
    auto halfAngle = [&](double rho) {
        rho = std::max(rho, rb);
        double phi = std::acos(std::min(1.0, rb / rho));
        return psiP + invA - inv(phi);
    };
    double raEff = ra;
    while (halfAngle(raEff) < 0.15 * psiP && raEff > r) raEff -= 0.02 * m;
    const double r0 = std::max(rb, rBase);
    const int K = 6;
    for (int k = 0; k < z; ++k) {
        double th = 2.0 * kPi * k / z;
        std::vector<dvec2> pts;
        auto polar = [&](double rho, double a) { pts.emplace_back(rho * std::cos(a), rho * std::sin(a)); };
        double hb = halfAngle(r0);
        polar(rBase, th - hb);
        polar(rBase, th + hb);
        for (int j = 0; j <= K; ++j) {
            double rho = r0 + (raEff - r0) * j / K;
            double ha = halfAngle(rho);
            polar(rho, th - ha);
            polar(rho, th + ha);
        }
        Piece p = makePrism(pts, h0, h1);
        if (!p.faces.empty()) s.pieces.push_back(std::move(p));
    }
    classifyInternalFaces(s);
    return s;
}

Solid makePropeller(double radius, int blades, double pitchDeg, double chord, double thickness) {
    blades = std::clamp(blades, 1, 12);
    radius = std::max(radius, 0.01);
    chord = std::clamp(chord, 0.002, radius);
    thickness = std::clamp(thickness, 0.0005, chord);
    const double hubR = std::max(chord * 0.45, radius * 0.1);
    const double hubH = std::max(chord * 0.9, thickness * 3);
    Solid s = makeCylinder(hubR, hubH, 24);
    // Geometrische Steigung aus dem Blattwinkel bei 75 % Radius: P = 2·π·0,75·R·tan(β)
    const double pitch = 2.0 * kPi * 0.75 * radius * std::tan(glm::radians(std::clamp(pitchDeg, 1.0, 60.0)));
    const double r0 = hubR * 0.7;
    const int segs = 4;
    for (int b = 0; b < blades; ++b) {
        const double phi = 2.0 * kPi * b / blades;
        for (int k = 0; k < segs; ++k) {
            double ra = r0 + (radius - r0) * k / segs, rb = r0 + (radius - r0) * (k + 1) / segs;
            const double overlap = (k + 1 < segs) ? 0.12 * (rb - ra) : 0.0;  // Stücke überlappen -> zusammenhängend
            rb += overlap;
            const double rm = 0.5 * (ra + rb);
            const double beta = std::atan(pitch / (2.0 * kPi * rm));         // Blattwinkel nimmt nach außen ab
            const double c = chord * (1.0 - 0.35 * (rm - r0) / (radius - r0));  // Blatt verjüngt sich
            Piece p = makeBox(dvec3(0.5 * (rb - ra), 0.5 * thickness, 0.5 * c));
            dmat4 m = glm::rotate(dmat4(1.0), phi, dvec3(0, 1, 0)) * glm::translate(dmat4(1.0), dvec3(rm, 0, 0)) *
                      glm::rotate(dmat4(1.0), beta, dvec3(1, 0, 0));  // Vorderkante (-z) hebt sich
            transform(p, m);
            s.pieces.push_back(std::move(p));
        }
    }
    classifyInternalFaces(s);
    return s;
}

std::vector<Plane> drillTool(dvec3 from, dvec3 dir, double radius, double length, int seg) {
    seg = std::max(seg, 8);
    dir = glm::normalize(dir);
    dvec3 u, w;
    planeBasis(dir, u, w);
    double rp = areaEquivalentRadius(radius, seg);
    std::vector<Plane> planes;
    for (int k = 0; k < seg; ++k) {
        double a = 2.0 * kPi * (k + 0.5) / seg;
        Plane pl;
        pl.n = std::cos(a) * u + std::sin(a) * w;
        pl.d = glm::dot(pl.n, from) + rp * std::cos(kPi / seg);
        pl.smooth = Smooth::Cylinder;
        pl.sc = from;
        pl.sa = dir;
        pl.sr = radius;
        planes.push_back(pl);
    }
    Plane start;
    start.n = -dir;
    start.d = glm::dot(start.n, from);
    Plane end;
    end.n = dir;
    end.d = glm::dot(dir, from + dir * length);
    planes.push_back(start);
    planes.push_back(end);
    return planes;
}

std::vector<Plane> boxTool(dvec3 mn, dvec3 mx) {
    std::vector<Plane> r;
    for (int k = 0; k < 3; ++k) {
        Plane hi, lo;
        hi.n = dvec3(0);
        hi.n[k] = 1;
        hi.d = mx[k];
        lo.n = dvec3(0);
        lo.n[k] = -1;
        lo.d = -mn[k];
        r.push_back(hi);
        r.push_back(lo);
    }
    return r;
}

std::vector<Plane> coneTool(dvec3 from, dvec3 dir, double r0, double r1, double length, int seg) {
    seg = std::max(seg, 8);
    dir = glm::normalize(dir);
    dvec3 u, w;
    planeBasis(dir, u, w);
    const double k = std::sqrt(kPi / (seg * std::tan(kPi / seg)));  // Vieleck mit Kreisfläche
    const double a0 = r0 * k, a1 = r1 * k, slope = (a1 - a0) / std::max(1e-12, length);
    std::vector<Plane> planes;
    for (int i = 0; i < seg; ++i) {
        double a = 2.0 * kPi * (i + 0.5) / seg;
        dvec3 radial = std::cos(a) * u + std::sin(a) * w;
        Plane pl;
        pl.n = glm::normalize(radial - dir * slope);
        pl.d = glm::dot(pl.n, from + radial * a0);
        pl.smooth = Smooth::Cylinder;
        pl.sc = from;
        pl.sa = dir;
        pl.sr = 0.5 * (r0 + r1);
        planes.push_back(pl);
    }
    Plane start, end;
    start.n = -dir;
    start.d = glm::dot(start.n, from);
    end.n = dir;
    end.d = glm::dot(dir, from + dir * length);
    planes.push_back(start);
    planes.push_back(end);
    return planes;
}

std::vector<Plane> slabTool(dvec3 n, double d, double kerf) {
    n = glm::normalize(n);
    Plane a;
    a.n = n;
    a.d = d + kerf / 2;
    Plane b;
    b.n = -n;
    b.d = -(d - kerf / 2);
    return {a, b};
}

Piece hullPiece(const std::vector<dvec3>& in) {
    std::vector<dvec3> pts;
    for (const auto& p : in) {
        bool dup = false;
        for (const auto& q : pts)
            if (glm::length(p - q) < 1e-9) {
                dup = true;
                break;
            }
        if (!dup) pts.push_back(p);
    }
    if (pts.size() < 4) return Piece{};
    dvec3 c(0);
    for (const auto& p : pts) c += p;
    c /= double(pts.size());
    double scale = 0, far = 0;
    for (const auto& p : pts) {
        scale = std::max(scale, glm::length(p - c));
        far = std::max({far, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
    }
    const double tol = 1e-10 + scale * 1e-9;
    std::vector<Plane> planes;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            for (size_t k = j + 1; k < n; ++k) {
                dvec3 nn = glm::cross(pts[j] - pts[i], pts[k] - pts[i]);
                double len = glm::length(nn);
                if (len < 1e-12 * scale * scale + 1e-24) continue;
                nn /= len;
                double d = glm::dot(nn, pts[i]);
                bool pos = false, neg = false;
                for (const auto& p : pts) {
                    double s = glm::dot(nn, p) - d;
                    if (s > tol) pos = true;
                    else if (s < -tol) neg = true;
                    if (pos && neg) break;
                }
                if (pos && neg) continue;
                if (!pos && !neg) return Piece{};  // alle Punkte in einer Ebene
                if (pos) {
                    nn = -nn;
                    d = -d;
                }
                // Ebene durch die äußersten Punkte legen (Rundungsfehler)
                for (const auto& p : pts) d = std::max(d, glm::dot(nn, p));
                bool dup = false;
                for (const Plane& q : planes)
                    if (glm::dot(q.n, nn) > 1.0 - 1e-9 && std::abs(q.d - d) < tol * 10) {
                        dup = true;
                        break;
                    }
                if (dup) continue;
                Plane pl;
                pl.n = nn;
                pl.d = d;
                planes.push_back(pl);
            }
    return fromPlanes(planes, far * 2 + 1.0);
}

std::vector<Plane> planesOf(const Piece& p) {
    std::vector<Plane> r;
    for (const Face& f : p.faces) {
        Plane pl;
        pl.n = f.n;
        pl.d = f.d;
        pl.smooth = f.smooth;
        pl.sc = f.sc;
        pl.sa = f.sa;
        pl.sr = f.sr;
        r.push_back(pl);
    }
    return r;
}

// ============================================================================ Transformation

void transformAffine(Solid& s, const glm::dmat3& a, dvec3 t) {
    const glm::dmat3 nrm = glm::transpose(glm::inverse(a));
    const bool flip = glm::determinant(a) < 0;
    const double k = std::cbrt(std::abs(glm::determinant(a)));
    for (Piece& p : s.pieces)
        for (Face& f : p.faces) {
            for (auto& v : f.v) v = a * v + t;
            if (flip) std::reverse(f.v.begin(), f.v.end());
            for (auto& poly : f.vis) {
                for (auto& v : poly) v = a * v + t;
                if (flip) std::reverse(poly.begin(), poly.end());
            }
            f.n = glm::normalize(nrm * f.n);
            f.d = f.v.empty() ? 0.0 : glm::dot(f.n, f.v[0]);
            f.sc = a * f.sc + t;
            f.sa = glm::normalize(a * f.sa);
            f.sr *= k;
        }
}

void transform(Piece& p, const dmat4& m) {
    glm::dmat3 r(m);
    for (Face& f : p.faces) {
        f.n = glm::normalize(r * f.n);
        for (auto& v : f.v) v = dvec3(m * glm::dvec4(v, 1.0));
        for (auto& poly : f.vis)
            for (auto& v : poly) v = dvec3(m * glm::dvec4(v, 1.0));
        f.d = f.v.empty() ? 0.0 : glm::dot(f.n, f.v[0]);
        f.sc = dvec3(m * glm::dvec4(f.sc, 1.0));
        f.sa = glm::normalize(r * f.sa);
    }
}

void transform(Solid& s, const dmat4& m) {
    for (Piece& p : s.pieces) transform(p, m);
}

void translate(Solid& s, dvec3 t) {
    for (Piece& p : s.pieces)
        for (Face& f : p.faces) {
            for (auto& v : f.v) v += t;
            for (auto& poly : f.vis)
                for (auto& v : poly) v += t;
            f.d += glm::dot(f.n, t);
            f.sc += t;
        }
}

// ============================================================================ Abfragen

double faceArea(const Face& f) {
    dvec3 acc(0);
    for (size_t i = 1; i + 1 < f.v.size(); ++i) acc += glm::cross(f.v[i] - f.v[0], f.v[i + 1] - f.v[0]);
    return 0.5 * glm::dot(acc, f.n);
}

double volume(const Piece& p, dvec3* centroid) {
    if (p.faces.empty() || p.faces[0].v.empty()) {
        if (centroid) *centroid = dvec3(0);
        return 0;
    }
    const dvec3 ref = p.faces[0].v[0];
    double vol = 0;
    dvec3 c(0);
    for (const Face& f : p.faces) {
        for (size_t i = 1; i + 1 < f.v.size(); ++i) {
            dvec3 a = f.v[0] - ref, b = f.v[i] - ref, d = f.v[i + 1] - ref;
            double vt = glm::dot(a, glm::cross(b, d)) / 6.0;
            vol += vt;
            c += vt * (a + b + d) / 4.0;
        }
    }
    if (centroid) *centroid = std::abs(vol) > 1e-30 ? ref + c / vol : ref;
    return vol;
}

double volume(const Solid& s, dvec3* centroid) {
    double vol = 0;
    dvec3 c(0);
    for (const Piece& p : s.pieces) {
        dvec3 pc;
        double v = volume(p, &pc);
        vol += v;
        c += v * pc;
    }
    if (centroid) *centroid = vol > 1e-30 ? c / vol : dvec3(0);
    return vol;
}

void bounds(const Piece& p, dvec3& mn, dvec3& mx) {
    mn = dvec3(1e30);
    mx = dvec3(-1e30);
    for (const Face& f : p.faces)
        for (const auto& v : f.v) {
            mn = glm::min(mn, v);
            mx = glm::max(mx, v);
        }
}

void bounds(const Solid& s, dvec3& mn, dvec3& mx) {
    mn = dvec3(1e30);
    mx = dvec3(-1e30);
    for (const Piece& p : s.pieces) {
        dvec3 a, b;
        bounds(p, a, b);
        mn = glm::min(mn, a);
        mx = glm::max(mx, b);
    }
}

size_t faceCount(const Solid& s) {
    size_t n = 0;
    for (const Piece& p : s.pieces) n += p.faces.size();
    return n;
}

bool rayHit(const Piece& p, dvec3 o, dvec3 dir, double& tOut, dvec3& nOut) {
    double tEnter = -1e30, tExit = 1e30;
    dvec3 nEnter(0);
    for (const Face& f : p.faces) {
        double denom = glm::dot(f.n, dir);
        double dist = glm::dot(f.n, o) - f.d;
        if (std::abs(denom) < 1e-15) {
            if (dist > 0) return false;
            continue;
        }
        double t = -dist / denom;
        if (denom < 0) {
            if (t > tEnter) {
                tEnter = t;
                nEnter = f.n;
            }
        } else {
            tExit = std::min(tExit, t);
        }
        if (tEnter > tExit) return false;
    }
    if (tExit < 0) return false;
    tOut = std::max(tEnter, 0.0);
    nOut = nEnter;
    return true;
}

bool rayHit(const Solid& s, dvec3 o, dvec3 dir, double& tOut, dvec3& nOut) {
    bool hit = false;
    double best = 1e30;
    for (const Piece& p : s.pieces) {
        double t;
        dvec3 n;
        if (rayHit(p, o, dir, t, n) && t < best) {
            best = t;
            nOut = n;
            hit = true;
        }
    }
    if (hit) tOut = best;
    return hit;
}

// ============================================================================ Boolesche Operationen

bool clip(const Piece& in, const Plane& pl, Piece& out) {
    bool anyAbove = false, anyBelow = false;
    for (const Face& f : in.faces)
        for (const auto& v : f.v) {
            double s = glm::dot(pl.n, v) - pl.d;
            if (s > kEps) anyAbove = true;
            else if (s < -kEps) anyBelow = true;
        }
    if (!anyAbove) {
        out = in;
        return !in.faces.empty();
    }
    if (!anyBelow) {
        out.faces.clear();
        return false;
    }

    Piece res;
    std::vector<dvec3> cap;
    for (const Face& f : in.faces) {
        Face nf;
        nf.n = f.n;
        nf.d = f.d;
        nf.smooth = f.smooth;
        nf.sc = f.sc;
        nf.sa = f.sa;
        nf.sr = f.sr;
        const size_t n = f.v.size();
        for (size_t i = 0; i < n; ++i) {
            const dvec3& a = f.v[i];
            const dvec3& b = f.v[(i + 1) % n];
            double sa = glm::dot(pl.n, a) - pl.d;
            double sb = glm::dot(pl.n, b) - pl.d;
            if (sa <= kEps) {
                nf.v.push_back(a);
                if (sa >= -kEps) cap.push_back(a);
            }
            if ((sa < -kEps && sb > kEps) || (sa > kEps && sb < -kEps)) {
                dvec3 p = edgeCut(a, b, sa, sb);
                nf.v.push_back(p);
                cap.push_back(p);
            }
        }
        removeDuplicates(nf.v);
        if (nf.v.size() >= 3 && faceArea(nf) > kAreaEps) res.faces.push_back(std::move(nf));
    }

    if (cap.size() >= 3) {
        Face c = makeFace(pl.n, cap);
        c.d = pl.d;
        c.smooth = pl.smooth;
        c.sc = pl.sc;
        c.sa = pl.sa;
        c.sr = pl.sr;
        if (c.v.size() >= 3 && faceArea(c) > kAreaEps) res.faces.push_back(std::move(c));
    }
    if (res.faces.size() < 4) return false;
    if (volume(res) < kVolEps) return false;
    out = std::move(res);
    return true;
}

void split(const Solid& s, const Plane& pl, Solid& below, Solid& above) {
    Plane flipped = pl;
    flipped.n = -pl.n;
    flipped.d = -pl.d;
    for (const Piece& p : s.pieces) {
        Piece a, b;
        if (clip(p, pl, a)) below.pieces.push_back(std::move(a));
        if (clip(p, flipped, b)) above.pieces.push_back(std::move(b));
    }
}

// Werkzeug mit runder Mantelfläche (Bohrer, Senker): die Umgebung in Tortenstücke um die Achse zerlegen.
// Jedes Stück liegt zwischen zwei Halbierenden benachbarter Vieleckecken und außerhalb genau einer Mantelebene.
// So entstehen kräftige konvexe Stücke statt dünner Scheiben (an hauchdünnen Splittern findet die Physik oft die
// falsche Richtung zum Trennen: Zittern und Ineinanderstecken).
static bool radialSubtract(const Piece& remaining, const std::vector<Plane>& tool, Solid& result) {
    std::vector<const Plane*> sides, caps;
    for (const Plane& pl : tool) (pl.smooth == Smooth::Cylinder ? sides : caps).push_back(&pl);
    if (sides.size() < 6) return false;
    const dvec3 axis = glm::normalize(sides[0]->sa), c = sides[0]->sc;
    for (const Plane* s : sides)
        if (glm::length(glm::cross(glm::normalize(s->sa), axis)) > 1e-6 || glm::length(glm::cross(s->sc - c, axis)) > 1e-9) return false;
    dvec3 u, w;
    planeBasis(axis, u, w);
    auto radial = [&](const dvec3& n) { return glm::normalize(n - axis * glm::dot(n, axis)); };
    auto angle = [&](const Plane* p) {
        dvec3 r = radial(p->n);
        return std::atan2(glm::dot(r, w), glm::dot(r, u));
    };
    std::sort(sides.begin(), sides.end(), [&](const Plane* a, const Plane* b) { return angle(a) < angle(b); });
    // Material jenseits der Deckel (z.B. unter einem Sackloch) bleibt ein ganzes Stück
    Piece rest = remaining;
    for (const Plane* cp : caps) {
        Plane outside = *cp;
        outside.n = -cp->n;
        outside.d = -cp->d;
        Piece o;
        if (clip(rest, outside, o)) result.pieces.push_back(std::move(o));
        Piece in;
        if (!clip(rest, *cp, in)) return true;
        rest = std::move(in);
    }
    const size_t n = sides.size();
    for (size_t i = 0; i < n; ++i) {
        const Plane& side = *sides[i];
        const dvec3 ni = radial(side.n), np = radial(sides[(i + n - 1) % n]->n), nn = radial(sides[(i + 1) % n]->n);
        auto wedge = [&](const dvec3& bisector) {
            Plane h;
            h.n = glm::normalize(glm::cross(axis, bisector));
            if (glm::dot(h.n, ni) > 0) h.n = -h.n;
            h.d = glm::dot(h.n, c);
            return h;
        };
        Plane outside = side;
        outside.n = -side.n;
        outside.d = -side.d;
        Piece a, b, sec;
        if (!clip(rest, wedge(glm::normalize(np + ni)), a) || !clip(a, wedge(glm::normalize(ni + nn)), b) || !clip(b, outside, sec)) continue;
        result.pieces.push_back(std::move(sec));
    }
    return true;
}

Solid subtract(const Solid& s, const std::vector<Plane>& tool, double* removedVolume, bool presplit) {
    Solid result;
    double removed = 0;
    if (!presplit) {
        // Jeweils die Ebene zuerst, außerhalb derer das meiste Material liegt: Liegt das Stück z.B. ganz hinter
        // zwei Werkzeugflächen (Ecke beim Abrunden), wird es nur von der dritten beschnitten -> ein Stück statt vier.
        for (const Piece& p : s.pieces) {
            Piece inter = p, tmp;
            bool hits = true;
            for (const Plane& pl : tool) {
                if (!clip(inter, pl, tmp)) {
                    hits = false;
                    break;
                }
                inter = std::move(tmp);
            }
            double vInter = hits ? volume(inter) : 0.0;
            if (!hits || vInter < kVolEps) {
                result.pieces.push_back(p);
                continue;
            }
            removed += vInter;
            Piece remaining = p;
            std::vector<bool> used(tool.size(), false);
            for (size_t step = 0; step < tool.size(); ++step) {
                size_t best = SIZE_MAX;
                double bestVol = -1;
                Piece bestOut;
                for (size_t i = 0; i < tool.size(); ++i) {
                    if (used[i]) continue;
                    Plane outside = tool[i];
                    outside.n = -tool[i].n;
                    outside.d = -tool[i].d;
                    Piece o;
                    double v = clip(remaining, outside, o) ? volume(o) : 0.0;
                    if (v > bestVol) {
                        bestVol = v;
                        best = i;
                        bestOut = std::move(o);
                    }
                }
                if (best == SIZE_MAX) break;
                used[best] = true;
                if (bestVol > kVolEps) result.pieces.push_back(std::move(bestOut));
                Piece in;
                if (!clip(remaining, tool[best], in)) break;
                remaining = std::move(in);
            }
        }
        if (removedVolume) *removedVolume = removed;
        return result;
    }
    // Hüllquader des Werkzeugs: Erst diesen Bereich abtrennen, damit nur ein kleines Stück um das Loch
    // in Keile zerlegt wird. Der Rest bleibt aus wenigen großen Stücken (schöner und schneller für die Physik).
    std::vector<Plane> boxPlanes;
    {
        Piece toolPiece = fromPlanes(tool, 1e4);
        if (!toolPiece.faces.empty()) {
            dvec3 tmn, tmx;
            bounds(toolPiece, tmn, tmx);
            for (int k = 0; k < 3; ++k) {
                if (tmx[k] - tmn[k] > 1e3) continue;  // in dieser Richtung unbegrenzt (z.B. Sägefuge)
                Plane hi, lo;
                hi.n = dvec3(0);
                hi.n[k] = 1;
                hi.d = tmx[k] + 1e-6;
                lo.n = dvec3(0);
                lo.n[k] = -1;
                lo.d = -(tmn[k] - 1e-6);
                boxPlanes.push_back(hi);
                boxPlanes.push_back(lo);
            }
        }
    }
    for (const Piece& p : s.pieces) {
        // Schneidet das Werkzeug dieses Teilstück überhaupt?
        Piece inter = p, tmp;
        bool hits = true;
        for (const Plane& pl : tool) {
            if (!clip(inter, pl, tmp)) {
                hits = false;
                break;
            }
            inter = std::move(tmp);
        }
        double vInter = hits ? volume(inter) : 0.0;
        if (!hits || vInter < kVolEps) {
            result.pieces.push_back(p);
            continue;
        }
        removed += vInter;
        Piece remaining = p;
        bool empty = false;
        for (const Plane& bp : boxPlanes) {  // Teile außerhalb des Werkzeug-Quaders bleiben ganz
            Plane outside = bp;
            outside.n = -bp.n;
            outside.d = -bp.d;
            Piece o;
            if (clip(remaining, outside, o)) result.pieces.push_back(std::move(o));
            Piece in;
            if (!clip(remaining, bp, in)) {
                empty = true;
                break;
            }
            remaining = std::move(in);
        }
        if (empty) continue;
        if (radialSubtract(remaining, tool, result)) continue;
        for (const Plane& pl : tool) {
            Plane outside = pl;
            outside.n = -pl.n;
            outside.d = -pl.d;
            Piece o;
            if (clip(remaining, outside, o)) result.pieces.push_back(std::move(o));
            Piece in;
            if (!clip(remaining, pl, in)) break;
            remaining = std::move(in);
        }
    }
    if (removedVolume) *removedVolume = removed;
    return result;
}

Solid intersect(const Solid& s, const std::vector<Plane>& tool) {
    Solid result;
    for (const Piece& p : s.pieces) {
        Piece inter = p, tmp;
        bool ok = true;
        for (const Plane& pl : tool) {
            if (!clip(inter, pl, tmp)) {
                ok = false;
                break;
            }
            inter = std::move(tmp);
        }
        if (ok && volume(inter) > kVolEps) result.pieces.push_back(std::move(inter));
    }
    return result;
}

std::vector<Solid> connectedComponents(const Solid& s) {
    const size_t n = s.pieces.size();
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };

    std::vector<dvec3> mn(n), mx(n);
    for (size_t i = 0; i < n; ++i) bounds(s.pieces[i], mn[i], mx[i]);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            if (find((int)i) == find((int)j)) continue;
            if (!boxesOverlap(mn[i], mx[i], mn[j], mx[j], 1e-7)) continue;
            if (piecesTouch(s.pieces[i], s.pieces[j])) parent[find((int)i)] = find((int)j);
        }

    std::vector<int> compIndex(n, -1);
    std::vector<Solid> comps;
    for (size_t i = 0; i < n; ++i) {
        int r = find((int)i);
        if (compIndex[r] < 0) {
            compIndex[r] = (int)comps.size();
            comps.emplace_back();
        }
        comps[compIndex[r]].pieces.push_back(s.pieces[i]);
    }
    return comps;
}

namespace {

// Konvexes 2D-Polygon auf eine Seite einer Geraden beschneiden (keepLeft: links von e0->e1 behalten)
std::vector<dvec2> clipHalf(const std::vector<dvec2>& poly, const dvec2& e0, const dvec2& e1, bool keepLeft) {
    std::vector<dvec2> out;
    const double sgn = keepLeft ? 1.0 : -1.0;
    for (size_t j = 0; j < poly.size(); ++j) {
        const dvec2& p = poly[j];
        const dvec2& q = poly[(j + 1) % poly.size()];
        double sp = sgn * cross2(e0, e1, p), sq = sgn * cross2(e0, e1, q);
        if (sp >= 0) out.push_back(p);
        if ((sp >= 0) != (sq >= 0)) out.push_back(p + (q - p) * (sp / (sp - sq)));
    }
    return out;
}

// Konvexes Polygon minus konvexes Polygon -> Liste konvexer Teile
void subtractConvex2D(const std::vector<dvec2>& p, const std::vector<dvec2>& c, std::vector<std::vector<dvec2>>& out) {
    std::vector<dvec2> rest = p;
    for (size_t i = 0; i < c.size() && rest.size() >= 3; ++i) {
        const dvec2& e0 = c[i];
        const dvec2& e1 = c[(i + 1) % c.size()];
        std::vector<dvec2> outside = clipHalf(rest, e0, e1, false);
        if (outside.size() >= 3 && polyArea(outside) > 1e-14) out.push_back(std::move(outside));
        rest = clipHalf(rest, e0, e1, true);
    }
}

} // namespace

void classifyInternalFaces(Solid& s) {
    const size_t n = s.pieces.size();
    std::vector<dvec3> mn(n), mx(n);
    for (size_t i = 0; i < n; ++i) bounds(s.pieces[i], mn[i], mx[i]);
    for (size_t i = 0; i < n; ++i) {
        for (Face& f : s.pieces[i].faces) {
            f.internal = false;
            f.partial = false;
            f.vis.clear();
            if (n < 2 || f.v.size() < 3) continue;
            dvec3 u, w;
            planeBasis(f.n, u, w);
            std::vector<std::vector<dvec2>> parts = {project(f, u, w, false)};
            const double area = polyArea(parts[0]);
            bool touched = false;
            for (size_t j = 0; j < n && !parts.empty(); ++j) {
                if (j == i || !boxesOverlap(mn[i], mx[i], mn[j], mx[j], 1e-7)) continue;
                for (const Face& g : s.pieces[j].faces) {
                    if (!oppositeCoplanar(f, g)) continue;
                    std::vector<dvec2> cover = project(g, u, w, true);
                    std::vector<std::vector<dvec2>> next;
                    for (auto& part : parts) {
                        if (convexOverlapArea(part, cover) <= 1e-14) {
                            next.push_back(std::move(part));
                            continue;
                        }
                        touched = true;
                        subtractConvex2D(part, cover, next);
                    }
                    parts.swap(next);
                }
            }
            if (!touched) continue;
            double rest = 0;
            for (auto& part : parts) rest += polyArea(part);
            if (rest <= area * 1e-6 + 1e-14) {
                f.internal = true;
            } else if (rest < area * (1.0 - 1e-9)) {
                f.partial = true;
                for (auto& part : parts) {
                    std::vector<dvec3> p3;
                    for (auto& q : part) p3.push_back(u * q.x + w * q.y + f.n * f.d);
                    f.vis.push_back(std::move(p3));
                }
            }
        }
    }
}

double contactArea(const Solid& a, const dmat4& ma, const Solid& b, const dmat4& mb, double gap, dvec3* centroid, dvec3* normal) {
    Solid sa = a, sb = b;
    transform(sa, ma);
    transform(sb, mb);
    auto polys = [](const Face& f) {
        std::vector<std::vector<dvec3>> r;
        if (f.partial) r = f.vis;
        else r.push_back(f.v);
        return r;
    };
    double total = 0;
    dvec3 cSum(0), nSum(0);
    for (const Piece& pa : sa.pieces)
        for (const Face& fa : pa.faces) {
            if (fa.internal) continue;
            dvec3 u, w;
            planeBasis(fa.n, u, w);
            for (const Piece& pb : sb.pieces)
                for (const Face& fb : pb.faces) {
                    if (fb.internal || fb.v.empty() || glm::dot(fa.n, fb.n) > -0.97) continue;
                    const double s = glm::dot(fa.n, fb.v[0]) - fa.d;  // Abstand der Ebenen entlang der Normalen von a
                    if (s < -0.002 || s > gap + 1e-6) continue;
                    for (const auto& qa : polys(fa))
                        for (const auto& qb : polys(fb)) {
                            std::vector<dvec2> p(qa.size()), c(qb.size());
                            for (size_t i = 0; i < qa.size(); ++i) p[i] = dvec2(glm::dot(qa[i], u), glm::dot(qa[i], w));
                            for (size_t i = 0; i < qb.size(); ++i) {
                                const dvec3& v = qb[qb.size() - 1 - i];  // b liegt gegenüber -> umdrehen
                                c[i] = dvec2(glm::dot(v, u), glm::dot(v, w));
                            }
                            for (size_t i = 0; i < c.size() && p.size() >= 3; ++i) p = clipHalf(p, c[i], c[(i + 1) % c.size()], true);
                            if (p.size() < 3) continue;
                            double ar = polyArea(p);
                            if (ar <= 1e-12) continue;
                            dvec2 cc(0);
                            for (size_t i = 1; i + 1 < p.size(); ++i) {
                                double t = 0.5 * cross2(p[0], p[i], p[i + 1]);
                                cc += t * (p[0] + p[i] + p[i + 1]) / 3.0;
                            }
                            cc /= ar;
                            total += ar;
                            cSum += ar * (u * cc.x + w * cc.y + fa.n * (fa.d + 0.5 * s));
                            nSum += ar * fa.n;
                        }
                }
        }
    if (total > 0) {
        if (centroid) *centroid = cSum / total;
        if (normal) *normal = glm::normalize(nSum);
    }
    return total;
}

// ============================================================================ Kanten

std::vector<Edge> findEdges(const Solid& s, double minAngleDeg) {
    // Alle Ränder der sichtbaren Flächenstücke sammeln
    struct Seg {
        dvec3 a, b, n, in;  // Normale der Fläche, Richtung in die Fläche hinein (quer zur Kante)
        bool curved;
    };
    std::vector<Seg> segs;
    for (const Piece& p : s.pieces)
        for (const Face& f : p.faces) {
            if (f.internal) continue;
            std::vector<const std::vector<dvec3>*> polys;
            if (f.partial)
                for (const auto& v : f.vis) polys.push_back(&v);
            else
                polys.push_back(&f.v);
            for (const auto* poly : polys) {
                if (poly->size() < 3) continue;
                dvec3 c(0);
                for (const auto& v : *poly) c += v;
                c /= double(poly->size());
                for (size_t i = 0; i < poly->size(); ++i) {
                    const dvec3& a = (*poly)[i];
                    const dvec3& b = (*poly)[(i + 1) % poly->size()];
                    dvec3 e = b - a;
                    double len = glm::length(e);
                    if (len < 1e-8) continue;
                    dvec3 in = glm::normalize(glm::cross(f.n, e / len));
                    if (glm::dot(in, c - a) < 0) in = -in;
                    segs.push_back({a, b, f.n, in, f.smooth != Smooth::None});
                }
            }
        }
    // Nach Geraden gruppieren
    std::map<std::tuple<long long, long long, long long, long long, long long, long long>, std::vector<int>> lines;
    for (size_t i = 0; i < segs.size(); ++i) {
        dvec3 d = glm::normalize(segs[i].b - segs[i].a);
        if (d.x < -1e-9 || (std::abs(d.x) <= 1e-9 && (d.y < -1e-9 || (std::abs(d.y) <= 1e-9 && d.z < 0)))) d = -d;
        dvec3 q = segs[i].a - d * glm::dot(segs[i].a, d);
        auto key = std::make_tuple(std::llround(d.x * 1e5), std::llround(d.y * 1e5), std::llround(d.z * 1e5), std::llround(q.x * 1e6),
                                   std::llround(q.y * 1e6), std::llround(q.z * 1e6));
        lines[key].push_back((int)i);
    }
    const double minAngle = minAngleDeg * kPi / 180.0;
    std::vector<Edge> out;
    for (auto& [key, idx] : lines) {
        if (idx.size() < 2) continue;
        const dvec3 d = glm::normalize(dvec3(std::get<0>(key), std::get<1>(key), std::get<2>(key)));
        // Bruchstellen entlang der Geraden
        std::vector<double> ts;
        for (int i : idx) {
            ts.push_back(glm::dot(segs[i].a, d));
            ts.push_back(glm::dot(segs[i].b, d));
        }
        std::sort(ts.begin(), ts.end());
        std::vector<double> u;
        for (double t : ts)
            if (u.empty() || t - u.back() > 1e-9) u.push_back(t);
        Edge cur;
        bool open = false;
        double curT0 = 0, curT1 = 0;
        const dvec3 q = segs[idx[0]].a - d * glm::dot(segs[idx[0]].a, d);
        auto flush = [&]() {
            if (!open) return;
            cur.a = q + d * curT0;
            cur.b = q + d * curT1;
            if (curT1 - curT0 > 1e-7) out.push_back(cur);
            open = false;
        };
        for (size_t k = 0; k + 1 < u.size(); ++k) {
            const double tm = 0.5 * (u[k] + u[k + 1]);
            // Flächen, die an diesem Abschnitt anliegen (verschiedene Richtungen)
            std::vector<int> cover;
            for (int i : idx) {
                double ta = glm::dot(segs[i].a, d), tb = glm::dot(segs[i].b, d);
                if (std::min(ta, tb) <= tm && tm <= std::max(ta, tb)) cover.push_back(i);
            }
            std::vector<int> dirs;
            for (int i : cover) {
                bool same = false;
                for (int j : dirs)
                    if (glm::dot(segs[i].n, segs[j].n) > 1.0 - 1e-6) same = true;
                if (!same) dirs.push_back(i);
            }
            bool ok = dirs.size() == 2;
            Edge e;
            if (ok) {
                const Seg* s1 = &segs[dirs[0]];
                const Seg* s2 = &segs[dirs[1]];
                if (lexLess(s2->n, s1->n)) std::swap(s1, s2);
                e.n1 = s1->n;
                e.n2 = s2->n;
                e.angle = std::acos(std::clamp(glm::dot(e.n1, e.n2), -1.0, 1.0));
                e.convex = glm::dot(s1->in, e.n2) < 0;
                e.curved = false;
                for (int i : cover) e.curved |= segs[i].curved;
                ok = e.angle >= minAngle && e.angle < kPi - 1e-3;
            }
            if (!ok) {
                flush();
                continue;
            }
            if (open && glm::dot(cur.n1, e.n1) > 1.0 - 1e-9 && glm::dot(cur.n2, e.n2) > 1.0 - 1e-9 && cur.convex == e.convex &&
                u[k] - curT1 < 1e-9) {
                curT1 = u[k + 1];
                cur.curved |= e.curved;
                continue;
            }
            flush();
            cur = e;
            curT0 = u[k];
            curT1 = u[k + 1];
            open = true;
        }
        flush();
    }
    // stabile Reihenfolge: gerade Kanten zuerst, dann nach Lage der Mitte
    std::sort(out.begin(), out.end(), [](const Edge& x, const Edge& y) {
        if (x.curved != y.curved) return !x.curved;
        dvec3 mx = glm::round((x.a + x.b) * 0.5e6), my = glm::round((y.a + y.b) * 0.5e6);
        if (mx.y != my.y) return mx.y > my.y;
        if (mx.x != my.x) return mx.x < my.x;
        if (mx.z != my.z) return mx.z < my.z;
        return glm::length(x.b - x.a) > glm::length(y.b - y.a);
    });
    return out;
}

std::vector<std::vector<Plane>> edgeBlendTools(const Edge& e, double size, int segments) {
    std::vector<std::vector<Plane>> regions;
    const dvec3 ed = glm::normalize(e.b - e.a);
    const dvec3 n1 = e.n1, n2 = e.n2;
    const double c12 = std::clamp(glm::dot(n1, n2), -1.0, 1.0);
    const double phi = std::acos(c12);
    if (phi < 1e-3 || phi > kPi - 1e-3 || size <= 0) return regions;
    segments = std::clamp(segments, 1, 64);
    const dvec3 m = e.a;
    Plane cap0, cap1;
    cap0.n = -ed;
    cap0.d = -glm::dot(ed, e.a);
    cap1.n = ed;
    cap1.d = glm::dot(ed, e.b);
    // Richtungen auf den beiden Flächen vom Kantenpunkt weg (in die Fläche hinein)
    auto onFace = [&](const dvec3& n, const dvec3& other) {
        dvec3 t = glm::normalize(glm::cross(n, ed));
        // Außenkante: die Fläche liegt auf der Seite, von der die andere Normale wegzeigt
        bool away = glm::dot(t, other) < 0;
        if (e.convex != away) t = -t;
        return t;
    };
    const dvec3 t1 = onFace(n1, n2), t2 = onFace(n2, n1);
    // Bogen: Mittelpunkt im gleichen Abstand r zu beiden Flächen
    double r = size;
    std::vector<dvec3> arc;
    dvec3 center(0);
    if (segments == 1) {
        // Fase: Schenkel gleicher Länge auf beiden Flächen
        arc = {m + t1 * size, m + t2 * size};
    } else {
        const double sgn = e.convex ? -1.0 : 1.0;
        center = m + sgn * r * (n1 + n2) / (1.0 + c12);
        for (int k = 0; k <= segments; ++k) {
            const double s = double(k) / segments;
            dvec3 u = (std::sin((1 - s) * phi) * n1 + std::sin(s * phi) * n2) / std::sin(phi);
            arc.push_back(center - sgn * r * u);
        }
    }
    for (size_t k = 0; k + 1 < arc.size(); ++k) {
        const dvec3& p0 = arc[k];
        const dvec3& p1 = arc[k + 1];
        // neue Fläche: Normale quer zur Sehne, zeigt vom Material weg
        dvec3 nc = glm::normalize(glm::cross(ed, p1 - p0));
        std::vector<Plane> reg;
        if (e.convex) {
            if (glm::dot(nc, m - p0) < 0) nc = -nc;  // zur Ecke hin (dort wird abgetragen)
            Plane chord;
            chord.n = -nc;
            chord.d = -glm::dot(nc, p0);
            if (segments > 1) {
                chord.smooth = Smooth::Cylinder;
                chord.sc = center;
                chord.sa = ed;
                chord.sr = r;
            }
            Plane f1, f2;
            f1.n = n1;
            f1.d = glm::dot(n1, m);
            f2.n = n2;
            f2.d = glm::dot(n2, m);
            reg = {chord, f1, f2, cap0, cap1};
        } else {
            // Hohlkehle: Dreieck (Ecke, p0, p1) wird aufgefüllt
            auto side = [&](const dvec3& a, const dvec3& b, const dvec3& away) {
                Plane pl;
                pl.n = glm::normalize(glm::cross(ed, b - a));
                if (glm::dot(pl.n, away - a) > 0) pl.n = -pl.n;
                pl.d = glm::dot(pl.n, a);
                return pl;
            };
            Plane chord = side(p0, p1, m);
            if (segments > 1) {
                chord.smooth = Smooth::Cylinder;
                chord.sc = center;
                chord.sa = ed;
                chord.sr = r;
            }
            reg = {chord, side(m, p0, p1), side(m, p1, p0), cap0, cap1};
        }
        regions.push_back(std::move(reg));
    }
    return regions;
}

Solid addRegions(const Solid& s, const std::vector<std::vector<Plane>>& regions) {
    Solid out = s;
    dvec3 mn, mx;
    bounds(s, mn, mx);
    double extent = std::max({std::abs(mn.x), std::abs(mn.y), std::abs(mn.z), std::abs(mx.x), std::abs(mx.y), std::abs(mx.z)}) * 4 + 1.0;
    for (const auto& reg : regions) {
        Piece p = fromPlanes(reg, extent);
        if (p.faces.empty() || volume(p) < kVolEps) continue;
        Solid add;
        add.pieces.push_back(std::move(p));
        dvec3 amn, amx;
        bounds(add, amn, amx);
        for (const Piece& q : out.pieces) {  // vorhandenes Material aussparen
            dvec3 qmn, qmx;
            bounds(q, qmn, qmx);
            if (!boxesOverlap(amn, amx, qmn, qmx, 1e-9)) continue;
            add = subtract(add, planesOf(q), nullptr, false);
            if (add.pieces.empty()) break;
        }
        for (auto& q : add.pieces) out.pieces.push_back(std::move(q));
    }
    return out;
}

Solid turn(const Solid& s, dvec3 c, dvec3 axis, double from, double to, double r0, double r1, int segments) {
    axis = glm::normalize(axis);
    segments = std::clamp(segments, 8, 128);
    dvec3 u, w;
    planeBasis(axis, u, w);
    const double s0 = glm::dot(axis, c) + from, s1 = glm::dot(axis, c) + to;
    // Vieleck mit gleicher Fläche wie der Kreis (Inkreisradius = Apothem)
    const double k = std::sqrt(kPi / (segments * std::tan(kPi / segments)));
    const double a0 = r0 * k, a1 = r1 * k;
    const double slope = (a1 - a0) / std::max(1e-12, to - from);
    std::vector<Plane> cone;
    for (int i = 0; i < segments; ++i) {
        const double ang = 2.0 * kPi * (i + 0.5) / segments;
        const dvec3 dir = std::cos(ang) * u + std::sin(ang) * w;
        Plane pl;
        pl.n = glm::normalize(dir - axis * slope);
        pl.d = glm::dot(pl.n, c + axis * from + dir * a0);
        pl.smooth = Smooth::Cylinder;
        pl.sc = c;
        pl.sa = axis;
        pl.sr = 0.5 * (r0 + r1);
        cone.push_back(pl);
    }
    Plane below, above, inLo, inHi;
    below.n = axis;
    below.d = s0;  // Teil vor dem Bereich
    above.n = -axis;
    above.d = -s1;  // Teil nach dem Bereich
    inLo.n = -axis;
    inLo.d = -s0;
    inHi.n = axis;
    inHi.d = s1;
    Solid out;
    for (const Piece& p : s.pieces) {
        Piece a, b, mid, tmp;
        if (clip(p, below, a)) out.pieces.push_back(std::move(a));
        if (clip(p, above, b)) out.pieces.push_back(std::move(b));
        if (!clip(p, inLo, mid) || !clip(mid, inHi, tmp)) continue;
        mid = std::move(tmp);
        bool ok = true;
        for (const Plane& pl : cone) {
            if (!clip(mid, pl, tmp)) {
                ok = false;
                break;
            }
            mid = std::move(tmp);
        }
        if (ok) out.pieces.push_back(std::move(mid));
    }
    return out;
}

// ============================================================================ Ausgabe

void buildTriangles(const Solid& s, std::vector<Vertex>& out) {
    out.clear();
    for (const Piece& p : s.pieces)
        for (const Face& f : p.faces) {
            if (f.internal || f.v.size() < 3) continue;
            auto vert = [&](const dvec3& v) {
                dvec3 n = f.smooth == Smooth::None ? f.n : smoothNormal(f, v);
                out.push_back({float(v.x), float(v.y), float(v.z), float(n.x), float(n.y), float(n.z)});
            };
            auto fan = [&](const std::vector<dvec3>& poly) {
                for (size_t i = 1; i + 1 < poly.size(); ++i) {
                    vert(poly[0]);
                    vert(poly[i]);
                    vert(poly[i + 1]);
                }
            };
            if (f.partial) {
                for (const auto& part : f.vis) fan(part);
            } else {
                fan(f.v);
            }
        }
}

namespace {

// Abschnitte [t0,t1] der Strecke a->b, die in den sichtbaren Teilen der Fläche liegen
std::vector<std::pair<double, double>> visibleIntervals(const Face& f, const dvec3& a, const dvec3& b) {
    std::vector<std::pair<double, double>> iv;
    if (!f.partial) {
        iv.emplace_back(0.0, 1.0);
        return iv;
    }
    const dvec3 d = b - a;
    for (const auto& poly : f.vis) {
        double t0 = 0, t1 = 1;
        bool ok = true;
        for (size_t i = 0; i < poly.size() && ok; ++i) {
            const dvec3& p = poly[i];
            const dvec3& q = poly[(i + 1) % poly.size()];
            dvec3 m = glm::cross(f.n, q - p);  // zeigt nach innen
            double len = glm::length(m);
            if (len < 1e-15) continue;
            m /= len;
            double s0 = glm::dot(m, a - p) + 1e-7, ds = glm::dot(m, d);
            if (std::abs(ds) < 1e-15) {
                if (s0 < 0) ok = false;
                continue;
            }
            double t = -s0 / ds;
            if (ds > 0) t0 = std::max(t0, t);
            else t1 = std::min(t1, t);
            if (t0 > t1) ok = false;
        }
        if (ok && t1 - t0 > 1e-9) iv.emplace_back(t0, t1);
    }
    std::sort(iv.begin(), iv.end());
    std::vector<std::pair<double, double>> merged;
    for (auto& x : iv) {
        if (!merged.empty() && x.first <= merged.back().second + 1e-9) merged.back().second = std::max(merged.back().second, x.second);
        else merged.push_back(x);
    }
    return merged;
}

std::vector<std::pair<double, double>> intersectIntervals(const std::vector<std::pair<double, double>>& a,
                                                          const std::vector<std::pair<double, double>>& b) {
    std::vector<std::pair<double, double>> r;
    for (auto& x : a)
        for (auto& y : b) {
            double lo = std::max(x.first, y.first), hi = std::min(x.second, y.second);
            if (hi - lo > 1e-9) r.emplace_back(lo, hi);
        }
    return r;
}

} // namespace

void buildEdges(const Solid& s, std::vector<glm::vec3>& lines, double minAngleDeg) {
    lines.clear();
    const double cosT = std::cos(minAngleDeg * kPi / 180.0);
    for (const Piece& p : s.pieces) {
        for (size_t fi = 0; fi < p.faces.size(); ++fi) {
            const Face& f = p.faces[fi];
            if (f.internal) continue;
            for (size_t i = 0; i < f.v.size(); ++i) {
                const dvec3& a = f.v[i];
                const dvec3& b = f.v[(i + 1) % f.v.size()];
                int gi = -1;
                for (size_t k = 0; k < p.faces.size(); ++k) {
                    if (k == fi) continue;
                    const Face& g = p.faces[k];
                    if (std::abs(glm::dot(g.n, a) - g.d) < 1e-7 && std::abs(glm::dot(g.n, b) - g.d) < 1e-7) {
                        gi = (int)k;
                        break;
                    }
                }
                auto iv = visibleIntervals(f, a, b);
                if (gi >= 0) {
                    const Face& g = p.faces[gi];
                    if (g.internal || gi < (int)fi) continue;
                    if (glm::dot(f.n, g.n) > cosT) continue;
                    iv = intersectIntervals(iv, visibleIntervals(g, a, b));
                }
                for (auto& [t0, t1] : iv) {
                    lines.emplace_back(a + (b - a) * t0);
                    lines.emplace_back(a + (b - a) * t1);
                }
            }
        }
    }
}

} // namespace solid
