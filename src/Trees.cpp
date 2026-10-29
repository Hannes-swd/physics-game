// Bäume: Arten lesen (trees.json), Formen bauen, Plätze aus dem Seed (siehe Trees.h)
#include "Trees.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Json.h"
#include "Noise.h"

namespace {

static const unsigned char kBuiltinTrees[] = {
#include "trees.json.inc"
    0};

constexpr double kPi = 3.14159265358979323846;
constexpr double kRoot = 0.4;  // so tief reicht der Stamm in den Boden (m)

using glm::dvec3;

std::filesystem::path exeDir() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? std::filesystem::path(buf).parent_path() : std::filesystem::current_path();
}

bool readVec3(const Json& j, glm::vec3& out) {
    if (j.type != Json::Array || j.size() != 3) return false;
    for (int i = 0; i < 3; ++i)
        if (j[i].type != Json::Number) return false;
    out = glm::vec3((float)j[0].num, (float)j[1].num, (float)j[2].num);
    return true;
}

// Zahl lesen: fehlt sie, bleibt der Standard; ist sie da, aber falsch, ist das ein Fehler
bool num(const Json& obj, const char* key, double& out, std::string& err, const std::string& where) {
    const Json& j = obj[key];
    if (j.type == Json::Null) return true;
    if (j.type != Json::Number) {
        err = where + "." + key + ": number expected";
        return false;
    }
    out = j.num;
    return true;
}

bool range(const Json& obj, const char* key, double& lo, double& hi, std::string& err, const std::string& where) {
    const Json& a = obj[key];
    if (a.type == Json::Null) return true;
    if (a.type != Json::Array || a.size() != 2 || a[0].type != Json::Number || a[1].type != Json::Number) {
        err = where + "." + key + ": [from, to] expected";
        return false;
    }
    lo = a[0].num;
    hi = std::max(a[1].num, lo);
    return true;
}

// Zufallszahlen aus einem Seed (deterministisch auf jedem Rechner)
struct Rng {
    uint64_t s;
    double next() {
        s = noise::mix64(s + 0x9E3779B97F4A7C15ull);
        return double(s >> 11) * (1.0 / 9007199254740992.0);
    }
    double range(double a, double b) { return a + (b - a) * next(); }
};

// zwei Richtungen senkrecht zu a
void basis(const dvec3& a, dvec3& u, dvec3& v) {
    const dvec3 t = std::abs(a.y) < 0.9 ? dvec3(0, 1, 0) : dvec3(1, 0, 0);
    u = glm::normalize(glm::cross(a, t));
    v = glm::cross(a, u);
}

// Kegelstumpf als konvexes Stück: Ringe um c0 und c1 (senkrecht zu axis), Mantel = Rinde
solid::Piece frustum(const dvec3& c0, double r0, const dvec3& c1, double r1, const dvec3& axis, int seg) {
    dvec3 u, v;
    basis(axis, u, v);
    std::vector<dvec3> pts;
    for (int i = 0; i < seg; ++i) {
        const double a = 2.0 * kPi * i / seg;
        const dvec3 d = u * std::cos(a) + v * std::sin(a);
        pts.push_back(c0 + d * r0);
        pts.push_back(c1 + d * std::max(r1, 0.004));
    }
    solid::Piece p = solid::hullPiece(pts);
    const dvec3 along = glm::normalize(c1 - c0);
    for (solid::Face& f : p.faces)
        if (std::abs(glm::dot(f.n, axis)) < 0.85) {
            f.smooth = solid::Smooth::Bark;
            f.sc = c0;
            f.sa = along;
            f.sr = 0.5 * (r0 + r1);
        }
    return p;
}

// Ein Dreieck der Krone (gegen den Uhrzeigersinn von außen)
void tri(std::vector<solid::Vertex>& out, const dvec3& a, const dvec3& b, const dvec3& c, const dvec3& na, const dvec3& nb,
         const dvec3& nc) {
    out.push_back({float(a.x), float(a.y), float(a.z), float(na.x), float(na.y), float(na.z)});
    out.push_back({float(b.x), float(b.y), float(b.z), float(nb.x), float(nb.y), float(nb.z)});
    out.push_back({float(c.x), float(c.y), float(c.z), float(nc.x), float(nc.y), float(nc.z)});
}

// Büschel: Ikosaeder (subdiv-mal verfeinert), zum Ellipsoid gestreckt, die Oberfläche etwas wellig
void blob(std::vector<solid::Vertex>& out, const dvec3& c, const dvec3& radii, int subdiv, uint64_t seed) {
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<dvec3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                            {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (dvec3& p : v) p = glm::normalize(p);
    std::vector<std::array<dvec3, 3>> faces;
    const int f[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                          {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (const auto& k : f) faces.push_back({v[(size_t)k[0]], v[(size_t)k[1]], v[(size_t)k[2]]});
    for (int s = 0; s < subdiv; ++s) {
        std::vector<std::array<dvec3, 3>> next;
        for (const auto& q : faces) {
            const dvec3 ab = glm::normalize(q[0] + q[1]), bc = glm::normalize(q[1] + q[2]), ca = glm::normalize(q[2] + q[0]);
            next.push_back({q[0], ab, ca});
            next.push_back({ab, q[1], bc});
            next.push_back({ca, bc, q[2]});
            next.push_back({ab, bc, ca});
        }
        faces.swap(next);
    }
    Rng r{seed};
    dvec3 k[3];
    double ph[3];
    for (int i = 0; i < 3; ++i) {
        k[i] = glm::normalize(dvec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)) + dvec3(1e-3)) * r.range(2.5, 4.5);
        ph[i] = r.range(0, 2 * kPi);
    }
    // Welligkeit hängt nur von der Richtung ab: Nachbardreiecke passen zusammen
    auto bump = [&](const dvec3& d) {
        double s = 0;
        for (int i = 0; i < 3; ++i) s += std::sin(glm::dot(d, k[i]) + ph[i]);
        return 1.0 + 0.09 * s;
    };
    auto pos = [&](const dvec3& d) { return c + radii * d * bump(d); };
    auto nrm = [&](const dvec3& d) { return glm::normalize(d / radii); };
    for (const auto& q : faces) {
        // Ikosaeder oben ist im Uhrzeigersinn: umdrehen
        tri(out, pos(q[0]), pos(q[2]), pos(q[1]), nrm(q[0]), nrm(q[2]), nrm(q[1]));
    }
}

// Eine Stufe einer Nadelbaumkrone: Kegel von y0 (Rand, hängt etwas) bis zur Spitze y1, dazu die Unterseite
void coneTier(std::vector<solid::Vertex>& out, const dvec3& axis0, double y0, double y1, double r, int seg, uint64_t seed) {
    Rng rng{seed};
    std::vector<double> rr((size_t)seg);
    for (double& x : rr) x = r * rng.range(0.85, 1.12);
    const double h = y1 - y0, droop = 0.18 * h;
    const dvec3 apex = axis0 + dvec3(0, y1, 0), under = axis0 + dvec3(0, y0 + 0.25 * h, 0);
    for (int i = 0; i < seg; ++i) {
        const double a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        const dvec3 d0(std::cos(a0), 0, std::sin(a0)), d1(std::cos(a1), 0, std::sin(a1));
        const dvec3 p0 = axis0 + d0 * rr[(size_t)i] + dvec3(0, y0 - droop, 0);
        const dvec3 p1 = axis0 + d1 * rr[(size_t)((i + 1) % seg)] + dvec3(0, y0 - droop, 0);
        const dvec3 n0 = glm::normalize(d0 * (h + droop) + dvec3(0, r, 0)), n1 = glm::normalize(d1 * (h + droop) + dvec3(0, r, 0));
        const dvec3 na = glm::normalize(n0 + n1);
        tri(out, apex, p1, p0, na, n1, n0);
        const dvec3 down(0, -1, 0);
        tri(out, under, p0, p1, down, glm::normalize(down + d0 * 0.4), glm::normalize(down + d1 * 0.4));
    }
}

} // namespace

solid::Piece barkFrustum(const dvec3& c0, double r0, const dvec3& c1, double r1, const dvec3& axis, int seg) {
    return frustum(c0, r0, c1, r1, axis, seg);
}

// ============================================================================ trees.json

int TreeConfig::find(const std::string& id) const {
    for (size_t i = 0; i < types.size(); ++i)
        if (types[i].id == id) return (int)i;
    return -1;
}

bool parseTreeConfig(const std::string& text, TreeConfig& out, std::string* errOut) {
    std::string err;
    auto fail = [&](const std::string& e) {
        if (errOut) *errOut = e;
        return false;
    };
    Json j;
    if (!parseJson(text, j, &err)) return fail(err);
    if (j.type != Json::Object) return fail("the file must contain an object { ... }");
    TreeConfig c;
    c.text = text;
    if (!num(j, "spacing", c.spacing, err, "") || !num(j, "solid_distance", c.solidDistance, err, "") ||
        !num(j, "detail_distance", c.detailDistance, err, "") || !num(j, "draw_distance", c.drawDistance, err, ""))
        return fail(err.substr(1));
    c.spacing = std::clamp(c.spacing, 1.0, 100.0);
    c.solidDistance = std::clamp(c.solidDistance, 5.0, 200.0);
    c.drawDistance = std::clamp(c.drawDistance, 50.0, 3000.0);
    c.detailDistance = std::clamp(c.detailDistance, 10.0, c.drawDistance);
    const Json& tl = j["types"];
    if (tl.type != Json::Array) return fail("types: a list of tree types is needed");
    for (size_t i = 0; i < tl.size(); ++i) {
        const Json& t = tl[i];
        const std::string w = "types[" + std::to_string(i) + "]";
        if (t.type != Json::Object) return fail(w + ": object expected");
        TreeType d;
        if (t["id"].type != Json::String || t["id"].str.empty()) return fail(w + ".id: text expected (e.g. \"oak_big\")");
        d.id = t["id"].str;
        if (c.find(d.id) >= 0) return fail(w + ".id: '" + d.id + "' is used twice");
        d.label = t["label"].type == Json::String ? t["label"].str : d.id;
        if (t["wood"].type == Json::String) d.wood = t["wood"].str;
        double sections = d.sections, branches = 0, variants = d.variants;
        if (const Json& s = t["trunk"]; s.type == Json::Object) {
            const std::string ws = w + ".trunk";
            if (!num(s, "height", d.height, err, ws) || !num(s, "diameter", d.diameter, err, ws) ||
                !num(s, "top_diameter", d.topDiameter, err, ws) || !num(s, "flare", d.flare, err, ws) || !num(s, "bend", d.bend, err, ws) ||
                !num(s, "sections", sections, err, ws))
                return fail(err);
        }
        if (const Json& s = t["branches"]; s.type == Json::Object) {
            const std::string ws = w + ".branches";
            if (!num(s, "count", branches, err, ws) || !num(s, "from", d.branchFrom, err, ws) || !num(s, "to", d.branchTo, err, ws) ||
                !range(s, "length", d.branchLenMin, d.branchLenMax, err, ws) || !range(s, "diameter", d.branchDiaMin, d.branchDiaMax, err, ws) ||
                !num(s, "rise", d.branchRise, err, ws))
                return fail(err);
        }
        if (const Json& s = t["crown"]; s.type == Json::Object) {
            const std::string ws = w + ".crown";
            if (s["shape"].type == Json::String) {
                if (s["shape"].str != "round" && s["shape"].str != "cone") return fail(ws + ".shape: 'round' or 'cone'");
                d.cone = s["shape"].str == "cone";
            }
            if (!num(s, "bottom", d.crownBottom, err, ws) || !num(s, "top", d.crownTop, err, ws) || !num(s, "radius", d.crownRadius, err, ws))
                return fail(err);
            if (s.has("color") && !readVec3(s["color"], d.leafColor)) return fail(ws + ".color: [r, g, b] expected");
            if (s.has("color2") && !readVec3(s["color2"], d.leafColor2)) return fail(ws + ".color2: [r, g, b] expected");
        }
        if (!range(t, "scale", d.scaleMin, d.scaleMax, err, w) || !num(t, "variants", variants, err, w)) return fail(err);
        d.height = std::clamp(d.height, 0.5, 120.0);
        d.diameter = std::clamp(d.diameter, 0.02, 5.0);
        d.topDiameter = std::clamp(d.topDiameter, 0.01, d.diameter);
        d.flare = std::clamp(d.flare, 1.0, 3.0);
        d.bend = std::clamp(d.bend, 0.0, d.height * 0.1);
        d.sections = std::clamp((int)sections, 1, 16);
        d.branches = std::clamp((int)branches, 0, 24);
        d.branchFrom = std::clamp(d.branchFrom, 0.05, 1.0);
        d.branchTo = std::clamp(d.branchTo, d.branchFrom, 1.0);
        d.branchDiaMin = std::clamp(d.branchDiaMin, 0.01, d.diameter);
        d.branchDiaMax = std::clamp(d.branchDiaMax, d.branchDiaMin, d.diameter);
        d.branchRise = std::clamp(d.branchRise, -30.0, 85.0);
        d.crownRadius = std::clamp(d.crownRadius, 0.2, 40.0);
        d.crownTop = std::max(d.crownTop, d.crownBottom + 0.5);
        d.scaleMin = std::clamp(d.scaleMin, 0.2, 5.0);
        d.scaleMax = std::clamp(d.scaleMax, d.scaleMin, 5.0);
        d.variants = std::clamp((int)variants, 1, 8);
        c.types.push_back(std::move(d));
    }
    out = std::move(c);
    return true;
}

const TreeConfig& builtinTreeConfig() {
    static const TreeConfig cfg = [] {
        TreeConfig c;
        std::string err;
        if (!parseTreeConfig(reinterpret_cast<const char*>(kBuiltinTrees), c, &err)) std::fprintf(stderr, "built-in trees.json: %s\n", err.c_str());
        return c;
    }();
    return cfg;
}

std::string findTreesFile() {
    std::error_code ec;
    const std::filesystem::path dir = exeDir();
    for (const std::filesystem::path& p : {dir / "trees.json", std::filesystem::current_path(ec) / "assets" / "trees.json",
                                           dir / "assets" / "trees.json", dir.parent_path().parent_path() / "assets" / "trees.json"})
        if (std::filesystem::is_regular_file(p, ec)) return p.lexically_normal().u8string();
    return {};
}

std::shared_ptr<const TreeConfig> loadTreesFile(std::string* report) {
    const std::string path = findTreesFile();
    if (!path.empty()) {
        std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        auto c = std::make_shared<TreeConfig>();
        std::string err;
        if (parseTreeConfig(ss.str(), *c, &err)) return c;
        if (report) *report = path + ": " + err + " - using the built-in trees";
    }
    return std::make_shared<TreeConfig>(builtinTreeConfig());
}

// ============================================================================ Form

TreeModel buildTreeModel(const TreeType& t, uint64_t seed) {
    TreeModel m;
    Rng r{seed};
    const double H = t.height, R0 = t.diameter / 2, R1 = t.topDiameter / 2;
    // Krümmung: zwei sanfte Wellen quer, am Fuß 0
    const double ph1 = r.range(0, 2 * kPi), ph2 = r.range(0, 2 * kPi), dirA = r.range(0, 2 * kPi);
    const dvec3 bx(std::cos(dirA), 0, std::sin(dirA)), bz(-bx.z, 0, bx.x);
    auto center = [&](double y) {
        const double s = std::clamp(y / H, 0.0, 1.0);
        return bx * (t.bend * s * std::sin(s * 2.1 + ph1)) + bz * (t.bend * 0.6 * s * std::sin(s * 3.3 + ph2)) + dvec3(0, y, 0);
    };
    auto radius = [&](double y) {  // verjüngt, unten der Wurzelanlauf
        const double s = std::clamp(y / H, 0.0, 1.0);
        double rr = R0 + (R1 - R0) * std::pow(s, 0.85);
        if (y < 0.9) rr *= 1.0 + (t.flare - 1.0) * (1.0 - std::max(y, 0.0) / 0.9) * (1.0 - std::max(y, 0.0) / 0.9);
        return rr;
    };
    const int seg = std::clamp((int)std::round(R0 * 40.0), 8, 20);
    // Stamm: Wurzel (im Boden), Anlauf, dann gleich lange Stücke bis oben
    std::vector<double> ys = {-kRoot, 0.0, std::min(0.9, H * 0.3)};
    for (int i = 1; i <= t.sections; ++i) ys.push_back(ys[2] + (H - ys[2]) * i / t.sections);
    const dvec3 up(0, 1, 0);
    for (size_t i = 0; i + 1 < ys.size(); ++i) {
        const double ya = ys[i], yb = ys[i + 1];
        const double ra = ya < 0 ? radius(0) : radius(ya), rb = radius(yb);
        const dvec3 ca = ya < 0 ? center(0) + dvec3(0, ya, 0) : center(ya), cb = center(yb);
        m.wood.pieces.push_back(frustum(ca, ra, cb, rb, up, seg));
    }
    // Äste: verteilt um den Stamm (goldener Winkel), je zwei Stücke, das äußere biegt sich nach oben
    std::vector<std::pair<dvec3, double>> tips;  // Spitze, Länge
    for (int k = 0; k < t.branches; ++k) {
        const double f = t.branchFrom + (t.branchTo - t.branchFrom) * (k + r.range(0.2, 0.8)) / t.branches;
        const double y = H * f, az = k * 2.39996 + r.range(-0.4, 0.4), rise = glm::radians(t.branchRise + r.range(-8, 8));
        const double len = r.range(t.branchLenMin, t.branchLenMax), dia = r.range(t.branchDiaMin, t.branchDiaMax);
        const dvec3 dir = glm::normalize(dvec3(std::cos(az) * std::cos(rise), std::sin(rise), std::sin(az) * std::cos(rise)));
        const dvec3 p0 = center(y), p1 = p0 + dir * (len * 0.55);
        const dvec3 dir2 = glm::normalize(dir + dvec3(0, 0.45, 0)), p2 = p1 + dir2 * (len * 0.45);
        const double r0 = std::min(dia / 2, radius(y) * 0.8), r1 = r0 * 0.68, r2 = std::max(r0 * 0.3, 0.012);
        const int bs = std::clamp((int)std::round(r0 * 60.0), 6, 12);
        m.wood.pieces.push_back(frustum(p0, r0, p1, r1, dir, bs));
        m.wood.pieces.push_back(frustum(p1, r1, p2, r2, dir2, bs));
        tips.emplace_back(p2, len);
    }
    solid::classifyInternalFaces(m.wood);
    m.volume = solid::volume(m.wood);

    // Krone
    const double cr = t.crownRadius;
    if (t.cone) {
        // Nadelbaum: überlappende Kegelstufen, unten breit, oben spitz
        const int tiers = std::clamp((int)std::round((t.crownTop - t.crownBottom) / 2.2), 3, 12);
        const double step = (t.crownTop - t.crownBottom) / tiers;
        for (int i = 0; i < tiers; ++i) {
            const double y0 = t.crownBottom + i * step, y1 = std::min(y0 + step * 2.1, t.crownTop);
            const double s = 1.0 - (y0 - t.crownBottom) / (t.crownTop - t.crownBottom);
            const double rr = cr * (0.12 + 0.88 * s);
            const dvec3 axis = center(std::min(y0, H)) - dvec3(0, std::min(y0, H), 0);
            const uint64_t ts = noise::mix64(seed + 101 + (uint64_t)i);
            coneTier(m.leaves, axis, y0, y1, rr, 14, ts);
            coneTier(m.leavesLow, axis, y0 - step * 0.2, y1, rr, 7, ts);
        }
        m.crownAnchor = center(std::min(t.crownBottom + (t.crownTop - t.crownBottom) * 0.3, H));
    } else {
        // Laubbaum: Büschel an den Astspitzen, eines oben, einige innen um den Stamm
        std::vector<std::pair<dvec3, dvec3>> blobs;  // Mitte, Halbachsen
        for (const auto& [tip, len] : tips) {
            const double rb = cr * r.range(0.38, 0.5);
            blobs.push_back({tip + dvec3(0, rb * 0.35, 0), dvec3(rb, rb * 0.72, rb)});
        }
        const dvec3 top = center(H);
        const double rt = cr * 0.52;
        blobs.push_back({dvec3(top.x, std::max(H, t.crownTop - rt * 0.8), top.z), dvec3(rt, rt * 0.75, rt)});
        const int inner = t.branches > 0 ? 2 : 4;
        for (int i = 0; i < inner; ++i) {
            const double a = r.range(0, 2 * kPi), y = r.range(t.crownBottom + 0.3 * (t.crownTop - t.crownBottom), t.crownTop - rt);
            const double rb = cr * r.range(0.35, 0.48);
            blobs.push_back({center(std::min(y, H)) + dvec3(std::cos(a), 0, std::sin(a)) * (cr * 0.35) + dvec3(0, y - std::min(y, H), 0),
                             dvec3(rb, rb * 0.75, rb)});
        }
        for (size_t i = 0; i < blobs.size(); ++i) {
            const uint64_t bs = noise::mix64(seed + 501 + i);
            blob(m.leaves, blobs[i].first, blobs[i].second, 2, bs);
            blob(m.leavesLow, blobs[i].first, blobs[i].second * 1.03, 0, bs);
        }
        m.crownAnchor = center(std::min(t.branches > 0 ? H * t.branchFrom : t.crownBottom, H));
    }
    m.height = std::max(H, t.crownTop);
    m.radius = cr;
    // grob: ein Stück vom Fuß bis oben, sechseckig (Äste verschwinden in der Krone)
    solid::Solid low;
    low.pieces.push_back(frustum(dvec3(0), radius(0.5) * 1.05, center(H), std::max(R1, 0.01), up, 6));
    solid::buildTriangles(low, m.woodLow);
    return m;
}

// ============================================================================ Bibliothek

TreeLibrary::TreeLibrary(Renderer* renderer, std::shared_ptr<const TreeConfig> cfg) : renderer_(renderer), cfg_(std::move(cfg)) {
    if (!cfg_) cfg_ = std::make_shared<TreeConfig>(builtinTreeConfig());
    for (const TreeType& t : cfg_->types) {
        std::vector<std::unique_ptr<TreeModel>> list;
        for (int v = 0; v < t.variants; ++v) {
            // Die Form hängt nur von der Art ab (nicht vom Welt-Seed): dieselben Bäume in jeder Welt und in der Baumschule
            auto m = std::make_unique<TreeModel>(buildTreeModel(t, noise::subSeed(0x7EE5, "tree/" + t.id + "/" + std::to_string(v))));
            if (renderer_) {
                std::vector<solid::Vertex> verts;
                solid::buildTriangles(m->wood, verts);
                m->woodMesh = renderer_->createMesh(verts);
                m->leafMesh = renderer_->createMesh(m->leaves);
                m->woodLowMesh = renderer_->createMesh(m->woodLow);
                m->leafLowMesh = renderer_->createMesh(m->leavesLow);
            }
            list.push_back(std::move(m));
        }
        models_.push_back(std::move(list));
    }
}

TreeLibrary::~TreeLibrary() {
    if (!renderer_) return;
    for (auto& list : models_)
        for (auto& m : list)
            for (GpuMesh* g : {&m->woodMesh, &m->leafMesh, &m->woodLowMesh, &m->leafLowMesh}) renderer_->destroyMesh(*g);
}

const TreeModel& TreeLibrary::model(int type, int variant) const {
    const auto& list = models_[(size_t)std::clamp(type, 0, types() - 1)];
    return *list[(size_t)std::clamp(variant, 0, (int)list.size() - 1)];
}

// ============================================================================ Wald

Forest::Forest(std::shared_ptr<const WorldGen> gen, std::shared_ptr<const TreeLibrary> lib) : gen_(std::move(gen)), lib_(std::move(lib)) {
    seed_ = noise::subSeed(gen_->seed(), "trees");
    const GenConfig& cfg = gen_->config();
    for (const GenConfig::Biome& b : cfg.biomes) {
        std::vector<std::pair<int, double>> types;
        for (const auto& [id, w] : b.treeTypes) {
            const int k = lib_->find(id);
            if (k >= 0) types.emplace_back(k, w);  // unbekannte Arten (alte trees.json): weglassen
        }
        biomeDensity_.push_back(types.empty() ? 0.0 : b.treesPerHa / 10000.0);
        any_ |= !types.empty() && b.treesPerHa > 0;
        biomeTypes_.push_back(std::move(types));
    }
}

bool Forest::spotOf(int64_t cx, int64_t cz, TreeSpot& out) const {
    if (!any_) return false;
    const double s = lib_->config().spacing;
    Rng r{noise::mix64(seed_ ^ noise::mix64((uint64_t)cx * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)cz * 0xC2B2AE3D27D4EB4Full))};
    const double accept = r.next(), jx = r.next(), jz = r.next(), pick = r.next(), yaw = r.next(), sc = r.next(), var = r.next();
    // Dichte aus den Biomen: an Grenzen weich (wie Höhe und Farbe), darum dünnt ein Wald zum Rand hin aus
    const double x = (double(cx) + 0.15 + 0.7 * jx) * s, z = (double(cz) + 0.15 + 0.7 * jz) * s;
    GenSample g;
    gen_->sample(x, z, g);
    const GenConfig& cfg = gen_->config();
    double density = 0;
    if (gen_->singleBiome() >= 0) {
        density = biomeDensity_[(size_t)gen_->singleBiome()];
    } else {
        double sum = 0;
        for (size_t b = 0; b < cfg.biomes.size(); ++b) {
            double w = std::clamp(0.5 + g.score[b] / (2.0 * cfg.blend), 0.0, 1.0);
            w = w * w * (3.0 - 2.0 * w);
            sum += w;
            density += w * biomeDensity_[b];
        }
        density = sum > 1e-9 ? density / sum : biomeDensity_[(size_t)g.biome];
    }
    if (accept >= density * s * s) return false;
    // nicht im Flussbett, nicht direkt am Wasser
    if (g.wet || (cfg.rivers.enabled && g.height < cfg.rivers.level + cfg.rivers.floor + 0.05 && g.river.strength > 0)) return false;
    const auto& types = biomeTypes_[(size_t)g.biome];
    if (types.empty()) return false;
    double total = 0;
    for (const auto& [k, w] : types) total += w;
    double p = pick * total;
    int type = types.back().first;
    for (const auto& [k, w] : types) {
        if (p < w) {
            type = k;
            break;
        }
        p -= w;
    }
    const TreeType& tt = lib_->config().types[(size_t)type];
    out.key = spotKey(cx, cz);
    out.pos = dvec3(x, g.height, z);
    out.yaw = (float)(yaw * 2.0 * kPi);
    out.scale = (float)(tt.scaleMin + (tt.scaleMax - tt.scaleMin) * sc);
    out.type = type;
    out.variant = std::min((int)(var * lib_->variants(type)), lib_->variants(type) - 1);
    return true;
}

const std::vector<TreeSpot>& Forest::tile(int64_t tx, int64_t tz) {
    const uint64_t key = tileKey(tx, tz);
    auto it = tiles_.find(key);
    if (it != tiles_.end()) return it->second;
    if (tiles_.size() > 20000) tiles_.clear();  // weit gewandert: von vorn
    std::vector<TreeSpot> spots;
    const double s = lib_->config().spacing;
    // Felder, deren Ecke in der Kachel liegt
    const int64_t cx0 = (int64_t)std::ceil(tx * kTile / s), cx1 = (int64_t)std::ceil((tx + 1) * kTile / s);
    const int64_t cz0 = (int64_t)std::ceil(tz * kTile / s), cz1 = (int64_t)std::ceil((tz + 1) * kTile / s);
    for (int64_t cz = cz0; cz < cz1; ++cz)
        for (int64_t cx = cx0; cx < cx1; ++cx) {
            TreeSpot t;
            if (spotOf(cx, cz, t)) spots.push_back(t);
        }
    return tiles_.emplace(key, std::move(spots)).first->second;
}

size_t Forest::prepare(const glm::dvec2& c, double radius, int budget) {
    if (!any_) return 0;
    std::vector<std::pair<double, std::pair<int64_t, int64_t>>> missing;
    const int64_t t0x = (int64_t)std::floor((c.x - radius) / kTile), t1x = (int64_t)std::floor((c.x + radius) / kTile);
    const int64_t t0z = (int64_t)std::floor((c.y - radius) / kTile), t1z = (int64_t)std::floor((c.y + radius) / kTile);
    for (int64_t tz = t0z; tz <= t1z; ++tz)
        for (int64_t tx = t0x; tx <= t1x; ++tx) {
            if (tileReady(tx, tz)) continue;
            const glm::dvec2 mid((tx + 0.5) * kTile, (tz + 0.5) * kTile);
            const double d = glm::length(mid - c);
            if (d > radius + kTile) continue;
            missing.push_back({d, {tx, tz}});
        }
    std::sort(missing.begin(), missing.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    size_t done = 0;
    for (const auto& m : missing) {
        if ((int)done >= budget) break;
        tile(m.second.first, m.second.second);
        ++done;
    }
    return missing.size() - done;
}

void Forest::around(const glm::dvec2& c, double radius, std::vector<TreeSpot>& out) {
    out.clear();
    if (!any_) return;
    const int64_t t0x = (int64_t)std::floor((c.x - radius) / kTile), t1x = (int64_t)std::floor((c.x + radius) / kTile);
    const int64_t t0z = (int64_t)std::floor((c.y - radius) / kTile), t1z = (int64_t)std::floor((c.y + radius) / kTile);
    for (int64_t tz = t0z; tz <= t1z; ++tz)
        for (int64_t tx = t0x; tx <= t1x; ++tx)
            for (const TreeSpot& s : tile(tx, tz))
                if (glm::length(glm::dvec2(s.pos.x, s.pos.z) - c) <= radius) out.push_back(s);
}

bool Forest::nearest(const glm::dvec2& from, double maxDist, int type, TreeSpot& out) {
    if (!any_) return false;
    std::vector<TreeSpot> list;
    for (double r = 64.0;; r = std::min(r * 2.0, maxDist)) {
        around(from, r, list);
        double best = 1e300;
        for (const TreeSpot& s : list) {
            if (type >= 0 && s.type != type) continue;
            const double d = glm::length(glm::dvec2(s.pos.x, s.pos.z) - from);
            if (d < best) {
                best = d;
                out = s;
            }
        }
        if (best < 1e300) return true;
        if (r >= maxDist) return false;
    }
}
