#include "Commands.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "Drawing.h"
#include "Jobs.h"
#include "Materials.h"
#include "Png.h"
#include "Profile.h"
#include "Script.h"
#include "Terrain.h"
#include "Trees.h"
#include "World.h"
#include "WorldGen.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMargin = 0.001;  // Werkzeuge ragen 1 mm über die Oberfläche hinaus

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> t;
    std::string cur;
    char quote = 0;  // " oder '
    bool has = false;
    for (char c : line) {
        if ((c == '"' || c == '\x27') && (quote == 0 || quote == c)) {
            quote = quote ? 0 : c;
            has = true;
            continue;
        }
        if (!quote && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
            if (has || !cur.empty()) t.push_back(cur);
            cur.clear();
            has = false;
            continue;
        }
        cur += c;
    }
    if (has || !cur.empty()) t.push_back(cur);
    return t;
}

bool toNum(const std::string& s, double& v) {
    std::string t = s;
    std::replace(t.begin(), t.end(), ',', '.');
    if (t.empty()) return false;
    char* end = nullptr;
    v = std::strtod(t.c_str(), &end);
    return end && *end == 0;
}

int findMaterial(const std::string& tok) {
    const auto& m = materials();
    std::string n = normalizeWord(tok);
    int i = findMaterialIndex(n.c_str());
    if (i >= 0) return i;
    if (n.size() >= 3)
        for (size_t k = 0; k < m.size(); ++k)
            if (std::string(m[k].name).rfind(n, 0) == 0) return (int)k;
    return -1;
}

std::string materialNames() {
    std::string s;
    for (auto& m : materials()) s += std::string(s.empty() ? "" : ", ") + m.name;
    return s;
}

std::string v3(const glm::dvec3& v, double scale = 100.0, int dec = 1) {
    return fmtNum(v.x * scale, dec) + " " + fmtNum(v.y * scale, dec) + " " + fmtNum(v.z * scale, dec);
}

glm::dvec3 eulerDeg(const glm::dquat& q) { return glm::degrees(glm::eulerAngles(q)); }

int axisIndex(const std::string& s) {
    std::string n = normalizeWord(s);
    if (n == "x") return 0;
    if (n == "y") return 1;
    if (n == "z") return 2;
    return -1;
}

// Fläche: +x -x +y -y +z -z oder top bottom right left front back. k = Achse, s = Richtung
bool parseFace(const std::string& tok, int& k, int& s) {
    std::string n = normalizeWord(tok);
    if (n == "top") n = "+y";
    else if (n == "bottom") n = "-y";
    else if (n == "right") n = "+x";
    else if (n == "left") n = "-x";
    else if (n == "front") n = "+z";
    else if (n == "back") n = "-z";
    if (n.size() == 1) n = "+" + n;
    if (n.size() != 2 || (n[0] != '+' && n[0] != '-')) return false;
    k = axisIndex(n.substr(1));
    s = n[0] == '+' ? 1 : -1;
    return k >= 0;
}

const char* faceName(int k, int s) {
    static const char* names[3][2] = {{"-x", "+x"}, {"-y", "+y"}, {"-z", "+z"}};
    return names[k][s > 0 ? 1 : 0];
}

// Richtung als Name (+x ...), sonst als Vektor
std::string dirName(const glm::dvec3& n) {
    for (int k = 0; k < 3; ++k)
        for (int s : {1, -1})
            if (n[k] * s > 0.999) return faceName(k, s);
    return "(" + fmtNum(n.x, 2) + " " + fmtNum(n.y, 2) + " " + fmtNum(n.z, 2) + ")";
}

// Achsen auf einer Fläche: u = erste, v = zweite andere Achse (wie beim Bohren)
void faceAxes(int k, int& u, int& v) {
    u = k == 0 ? 1 : 0;
    v = k == 2 ? 1 : 2;
}

bool yes(const std::string& s) {
    std::string n = normalizeWord(s);
    return n == "on" || n == "yes" || n == "true" || n == "1";
}

std::string supportText(const Object& o) {
    if (o.fixed) return "fixed";
    if (o.hingeAxis >= 0) {
        std::string s = std::string("axle ") + "xyz"[o.hingeAxis];
        if (std::abs(o.motorRpm) > 1e-3f) s += " motor " + fmtNum(o.motorRpm, 1) + " rpm, max " + fmtNum(o.motorTorque, 1) + " Nm";
        return s;
    }
    return "free";
}

std::string brief(const Object& o) {
    glm::dvec3 s = (o.bbMax - o.bbMin) * 100.0;
    return "#" + std::to_string(o.id) + "  " + o.name + "  [" + materials()[o.material].name + "]  " + fmtNum(s.x) + " x " +
           fmtNum(s.y) + " x " + fmtNum(s.z) + " cm  " + fmtNum(o.mass, 3) + " kg  pos " + v3(o.pos) + " cm  " + supportText(o);
}

std::string details(World& w, Object& o) {
    const Material& m = materials()[o.material];
    glm::dvec3 s = (o.bbMax - o.bbMin) * 100.0;
    std::ostringstream ss;
    ss << "object        #" << o.id << " \"" << o.name << "\"\n";
    ss << "shape         " << o.shape << (o.edits ? " (edited " + std::to_string(o.edits) + "x)" : "") << "\n";
    ss << "material      " << m.name << "  density=" << m.density << " kg/m3  friction static=" << fmtNum(m.frictionStatic, 2)
       << " sliding=" << fmtNum(m.frictionKinetic, 2) << "  rolling=" << fmtNum(m.rolling, 4) << "  restitution=" << fmtNum(m.restitution, 2)
       << "\n";
    ss << "strength      tension=" << fmtNum(m.tensileMPa, 1) << " MPa  compression=" << fmtNum(m.compressiveMPa, 1)
       << " MPa  E=" << fmtNum(m.youngGPa, 2) << " GPa\n";
    ss << "size_cm       " << fmtNum(s.x, 2) << " x " << fmtNum(s.y, 2) << " x " << fmtNum(s.z, 2) << "  (local x/y/z)\n";
    ss << "volume        " << fmtNum(o.volume * 1e6, 2) << " cm3\n";
    ss << "mass          " << fmtNum(o.mass, 4) << " kg   weight " << fmtNum(o.mass * 9.81, 3) << " N\n";
    ss << "position_cm   " << v3(o.pos, 100.0, 2) << "  (center of mass)\n";
    ss << "rotation_deg  " << v3(eulerDeg(o.rot), 1.0, 2) << "\n";
    ss << "world_bounds  " << v3(o.wMin) << "  to  " << v3(o.wMax) << " cm\n";
    ChunkKey k;
    if (w.grid().keyOfObject(o.id, k)) ss << "chunk         " << k.x << " " << k.y << " " << k.z << "\n";
    ss << "support       " << supportText(o) << "\n";
    if (o.body != kNoBody && !o.fixed) {
        glm::vec3 v = w.physics().linearVelocity(o.body), a = w.physics().angularVelocity(o.body);
        ss << "velocity_m/s  " << fmtNum(v.x, 3) << " " << fmtNum(v.y, 3) << " " << fmtNum(v.z, 3) << "  (|v|=" << fmtNum(glm::length(v), 3)
           << ")\n";
        ss << "spin          " << fmtNum(glm::length(a) * 60.0 / (2 * kPi), 2) << " rpm\n";
    }
    if (o.kind == ObjKind::Solid) {
        ss << "geometry      " << o.solid.pieces.size() << " convex pieces, " << solid::faceCount(o.solid) << " faces, "
           << w.edgesOf(o).size() << " edges\n";
        ss << "collision     "
           << (o.prim && !o.edits ? std::string("exact primitive") : std::to_string(o.coll.hulls.size()) + " convex hull(s)")
           << (o.collision == 1 ? " (exact)" : o.collision == 2 ? " (single hull)" : " (auto: holes and grooves are kept)") << "\n";
    } else {
        ss << "geometry      imported mesh, " << (o.imported ? o.imported->vertices.size() / 3 : 0) << " triangles\n";
    }
    if (o.body == kNoBody) ss << "warning       no collision shape\n";
    ss << w.heatText(o);
    return ss.str();
}

// Stelle auf einer Fläche (Mitte, lokal) für Werkzeuge: +y = oben ...
glm::dvec3 faceCenter(const Object& o, int k, int s) {
    glm::dvec3 p = (o.bbMin + o.bbMax) * 0.5;
    p[k] = s > 0 ? o.bbMax[k] : o.bbMin[k];
    return p;
}

// Optionale Position (cm) ab Index i, sonst freie Stelle
glm::dvec3 placeAt(CommandHost& h, const std::vector<std::string>& t, size_t i, const glm::dvec3& half) {
    double x, y, z;
    if (t.size() >= i + 3 && toNum(t[i], x) && toNum(t[i + 1], y) && toNum(t[i + 2], z)) return glm::dvec3(x, y, z) / 100.0;
    return h.world().findFreeSpot(half, h.spawnReference());
}

// Kanten auswählen: all | rims | convex | concave | +y | top | +y+x | top+front | x | y | z | e3 | 3 | e1,e4
bool selectEdges(const std::vector<solid::Edge>& edges, const std::string& tokIn, std::vector<int>& out, std::string& err) {
    std::string tok = normalizeWord(tokIn);
    out.clear();
    auto add = [&](int i) {
        if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
    };
    if (tok == "all" || tok == "rims" || tok == "convex" || tok == "concave") {
        for (size_t i = 0; i < edges.size(); ++i) {
            const auto& e = edges[i];
            if (tok == "all" && !e.curved) add((int)i);
            if (tok == "rims" && e.curved) add((int)i);
            if (tok == "convex" && e.convex && !e.curved) add((int)i);
            if (tok == "concave" && !e.convex && !e.curved) add((int)i);
        }
        if (out.empty()) err = "no such edges";
        return !out.empty();
    }
    int ax = axisIndex(tok);
    if (ax >= 0) {  // gerade Kanten entlang einer Achse
        for (size_t i = 0; i < edges.size(); ++i) {
            glm::dvec3 d = glm::normalize(edges[i].b - edges[i].a);
            if (std::abs(d[ax]) > 0.999 && !edges[i].curved) add((int)i);
        }
        if (out.empty()) err = std::string("no edges along ") + "xyz"[ax];
        return !out.empty();
    }
    // Nummern: e3, 3, e1,e4
    {
        std::vector<int> nums;
        std::stringstream ss(tok);
        std::string part;
        bool ok = true;
        while (std::getline(ss, part, ',')) {
            if (!part.empty() && part[0] == 'e') part = part.substr(1);
            double v;
            if (!toNum(part, v)) {
                ok = false;
                break;
            }
            nums.push_back((int)v);
        }
        if (ok && !nums.empty()) {
            for (int n : nums) {
                if (n < 0 || n >= (int)edges.size()) {
                    err = "there is no edge e" + std::to_string(n) + " (" + std::to_string(edges.size()) + " edges, see 'edges')";
                    return false;
                }
                add(n);
            }
            return true;
        }
    }
    // Flächen: +y, top, +y+x, top+front, top,front
    std::vector<glm::dvec3> faces;
    {
        std::string cur;
        auto flush = [&]() {
            if (cur.empty()) return true;
            int k, s;
            if (!parseFace(cur, k, s)) return false;
            glm::dvec3 n(0);
            n[k] = s;
            faces.push_back(n);
            cur.clear();
            return true;
        };
        for (size_t i = 0; i < tok.size(); ++i) {
            char c = tok[i];
            bool sep = c == ',' || ((c == '+' || c == '-') && !cur.empty() && cur != "+" && cur != "-");
            if (sep) {
                if (!flush()) {
                    err = "unknown edge selection '" + tokIn + "'";
                    return false;
                }
                if (c == ',') continue;
            }
            cur += c;
        }
        if (!flush() || faces.empty() || faces.size() > 2) {
            err = "unknown edge selection '" + tokIn + "' (all, rims, x, +y, top+front, e3, ...)";
            return false;
        }
    }
    for (size_t i = 0; i < edges.size(); ++i) {
        const auto& e = edges[i];
        if (e.curved) continue;  // Lochränder gehören nicht dazu (dafür: rims)
        auto has = [&](const glm::dvec3& n) { return glm::dot(e.n1, n) > 0.999 || glm::dot(e.n2, n) > 0.999; };
        if (faces.size() == 1 ? has(faces[0]) : (has(faces[0]) && has(faces[1]))) add((int)i);
    }
    if (out.empty()) err = "no edges on " + tokIn;
    return !out.empty();
}

std::string edgeLine(size_t i, const solid::Edge& e) {
    glm::dvec3 d = glm::normalize(e.b - e.a);
    std::string along = dirName(d);
    if (along[0] == '-' || along[0] == '+') along = along.substr(1);
    return "e" + std::to_string(i) + "  " + (e.convex ? "convex " : "concave") + "  " + fmtNum(glm::length(e.b - e.a) * 100, 2) + " cm along " +
           along + "  from " + v3(e.a, 100, 2) + " to " + v3(e.b, 100, 2) + " cm  faces " + dirName(e.n1) + " / " + dirName(e.n2) + "  " +
           fmtNum(180.0 - glm::degrees(e.angle), 0) + "°" + (e.curved ? "  (curved face)" : "");
}

} // namespace

std::string normalizeWord(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = (unsigned char)s[i + 1];
            ++i;
            if (d == 0xA4 || d == 0x84) r += "ae";
            else if (d == 0xB6 || d == 0x96) r += "oe";
            else if (d == 0xBC || d == 0x9C) r += "ue";
            else if (d == 0x9F) r += "ss";
            continue;
        }
        r += (char)std::tolower(c);
    }
    return r;
}

std::string CommandHost::wait(double seconds) {
    world().simulateSeconds(seconds);
    return fmtNum(seconds, 2) + " s passed (physics simulated)";
}

std::string CommandHost::setMode(bool play) {
    world().setSimulating(play);
    return play ? "physics running (play mode)" : "physics paused (build mode)";
}

std::string commandHelp() {
    return
        "Sizes in cm, small tool sizes in mm, angles in degrees. <id> = part number, 'selected' or 'last'.\n"
        "Faces: +x -x +y -y +z -z or right left top bottom front back. a b = position on the face from its center (cm).\n"
        "CREATE\n"
        "  new box <mat> <L> <W> <H> [x y z]       box (L = x, H = y, W = z)\n"
        "  new cylinder <mat> <D> <H> [x y z] | new tube <mat> <D_out> <D_in> <L> [x y z] | new sphere <mat> <D> [x y z]\n"
        "  new gear <mat> <teeth> <module_mm> <width> [bore_mm] [x y z]\n"
        "  new propeller <mat> <D> [blades] [angle] [blade_width] [thickness_mm]   axis y, +rotation = thrust to +y\n"
        "  new wedge <mat> <edge_L> <H> [angle=25] [x y z]   axe/chisel, edge along x at the bottom\n"
        "  new spike <mat> <D> <L> [tip_radius_mm=0] [x y z]   cone pointing down (0 = sharp, larger = round tip)\n"
        "  import <file.obj> [mat] [scale] | copy <id> [dx dy dz] | delete <id ...|all>\n"
        "  list | info <id> | inspect <id> | materials | chunks | overlaps <id>\n"
        "MATERIALS (all values in assets/materials.json, see docs/13-Materialien.md)\n"
        "  materials keys | materials show <m> | materials set <m> <key> <value...> | materials new <name> [copy_of]\n"
        "  materials save [file] | materials reload [file] | materials file\n"
        "PLACE\n"
        "  position <id> x y z                       teleport (warns when it overlaps something)\n"
        "  move <id> dx dy dz [through]              stops at the first part in the way (through = ignore)\n"
        "  rotation <id> rx ry rz | rotate <id> <x|y|z> <deg> | drop <id>\n"
        "  material <id> <mat> | name <id> <text> | fix <id> [on|off] | axle <id> <x|y|z|off> [rpm] [Nm]\n"
        "  collision <id> auto|exact|hull | unbreakable <id> [on|off] | buildcollision [on|off]\n"
        "SAW, DRILL, MILL\n"
        "  cut <id> <x|y|z> <distance> [angle] [kerf_mm]     distance from the end along the local axis\n"
        "  drill <id> <face> <a> <b> <D_mm> [depth_mm] | drillat <id> x y z dx dy dz <D_mm> [depth_mm]\n"
        "  countersink <id> <face> <a> <b> <D_mm> [angle]    cone for screw heads (default 90°)\n"
        "  pocket <id> <face> <a> <b> <length_mm> <width_mm> <depth_mm> [corner_mm]   also a mortise\n"
        "  groove <id> <face> <x|y|z> <offset> <width_mm> <depth_mm>     along an axis across the whole face\n"
        "  rebate <id> <face> <side_face> <width_mm> <depth_mm>          step along an edge\n"
        "  slot <id> <face> <a1> <b1> <a2> <b2> <width_mm> [depth_mm]    long hole (0 = through)\n"
        "  tenon <id> <end_face> <length> <thickness_mm> <width_mm> | halflap <id> <end_face> <length> [side_face]\n"
        "SHAPE\n"
        "  edges <id> [selection]                    list edges (e0, e1 ...)\n"
        "  round <id> <edges> <radius_mm> [segments] | chamfer <id> <edges> <size_mm> | sand <id> [radius_mm]\n"
        "      edges: all | rims | convex | concave | x|y|z | +y | top+front | e3 | e1,e4\n"
        "  plane <id> <face> <mm>                    plane off a layer\n"
        "  turn <id> <x|y|z> <from> <to> <D_mm> [D2_mm]   lathe: turn down to a diameter (D2 = taper)\n"
        "  bend <id> <x|y|z> <at> <angle> [radius_mm]     bend across the axis (sheet metal, rods)\n"
        "  mirror <id> <x|y|z> [copy] | scale <id> <factor> | scale <id> <fx> <fy> <fz>\n"
        "  merge <a> <b> [c ...] | subtract <a> <tool> [keep] | intersect <a> <tool> [keep]\n"
        "JOIN (a holds, b is held)\n"
        "  glue <a> <b> [glue] [strength_MPa] [gap_mm]   woodglue epoxy superglue pu hotglue construction\n"
        "  nail <a> <b> [count] [length_mm] [d_mm] | screw <a> <b> [count] [length_mm] [d_mm]\n"
        "  weld <a> <b> [seam_mm] | hinge <a> <b> <x|y|z> [rpm] [Nm]\n"
        "  spring <a> <b> [N/m] [N·s/m] [max_N] | rope <a> <b> [length] [max_N]\n"
        "  joints [id] | joint <nr> [setting value] | detach <nr|all>\n"
        "  bores <id>                                round holes: size, pin and play, load, pressure, wear\n"
        "  lubricate <id> [oil|grease|wax|soap|off]  lower friction and wear (pin hinges, axles, drawers)\n"
        "  breakage [on|off]                         on: joints and parts break when overloaded (default)\n"
        "HEAT & FIRE\n"
        "  heat                                      overview: air temperature, fires, hottest part\n"
        "  heat <id> [°C]                            read temperature/state of a part, or set it throughout\n"
        "  heat on|off | heat ambient <°C> | heat speed <x>   simulate heat / air temperature / heat time-lapse\n"
        "  ignite <id> [face]                        light wood, rubber or plastic (default: lowest spot)\n"
        "  extinguish <id|all>                       pour water on the flames\n"
        "  torch <id> [lighter|gas|welding|spray|nitrogen] [seconds=5] [face=top]   heat or cool a spot\n"
        "WATER (lengths in cm)\n"
        "  water                                     water bodies, liquids, what floats\n"
        "  water pool <L> <W> <depth> [x y z] [liquid] [walls]   water L x W, bottom middle at x y z (default: here on the floor)\n"
        "  water sea <level> [liquid] [bottom <cm>] | water sea off   endless sea: water between its bottom (default 0) and the level\n"
        "  water land <x> <z> <W> <L> [height] | water ground <x> <z> <W> <L> <height> | water ground reset   sea bottom / dry land\n"
        "  water level|waves <n|all> <cm> | water temp <n|all> <°C|air> | water flow <n|all> <x> <z> (m/s) | water liquid <n|all> <l>\n"
        "  water remove <n|all> | water <id>         how a part floats: displaced volume, buoyancy, water in its hollows\n"
        "  water fill <id> [percent] | water empty <id>   fill the hollows of a cup or boat\n"
        "      liquids: water seawater muddy oil\n"
        "PHYSICS & WORLD\n"
        "  air [density <kg/m3> | vacuum | normal | wind x y z] | wind x y z\n"
        "  impulse <id> jx jy jz                     impulse in N·s | sim <seconds>\n"
        "  mode build|play | view perspective|top|front|side|quad\n"
        "  pause [on|off|settings|quit]               pause window of play mode (like Esc): continue, switch mode, settings, quit\n"
        "  pause set <key> <value> | pause shot <f.png>  setting like 'menu set' (applies at once) | picture of the pause window\n"
        "  camera x y z [distance] | select <id ...|all> | screenshot <file.png>\n"
        "  drawing [id ...] [file.svg]               three-view drawing (no id: selection)\n"
        "  undo | redo | scene empty|demo|showcase|fire|water|river|trees | save <file> | load <file> | quit\n"
        "  status | wait <seconds> | profile [reset|on|off]   time spent per engine section (find slow parts)\n"
        "ONLY IN THE GAME WINDOW\n"
        "  player                                    position, view, what is carried\n"
        "  player position x y z | player look <id | x y z> | player turn <deg> [pitch]\n"
        "  player walk <forward|back|left|right> <seconds> [run] | player jump | player stop | player view [first|third]\n"
        "  player grab [id] | player release | player throw [N·s] | player rotate <deg> [tilt]   (carried part, like R)\n"
        "  tp | tp <biome> | tp river | tp water | tp tree [type] | tp stone|stick|log|stump | tp <player> | tp spawn | tp <x> <z> | tp <x> <y> <z>   teleport to the nearest such\n"
        "      spot, always onto the surface (build mode: moves the camera; 'tp' alone lists the targets)\n"
        "  tool select|saw [x|y|z] [angle] [kerf_mm]|drill [D_mm] [depth_mm]|round [r_mm] [chamfer]|mill [L W depth]|plane [mm]\n"
        "  tool heat [lighter|gas|welding|spray|nitrogen]   hold the mouse on a part (build) or F (play) to heat/cool it\n"
        "  window inspector|drawing|console|help|materials [on|off] | display shadows|edges|grid [on|off] | log [n]  (last messages)\n"
        "TERRAIN (world type normal, lengths in cm)\n"
        "  terrain | terrain at [x z] | terrain biomes   state, height, biome and climate at a spot, all biomes\n"
        "  terrain find <biome> | terrain goto <biome>   nearest spot of a biome (goto: the camera looks there)\n"
        "  terrain raise|lower <x> <z> <radius> <cm> | terrain flatten <x> <z> <radius> [height] | terrain reset\n"
        "  terrain reload | terrain borders on|off    biomes.json again (regenerates), biome border lines\n"
        "  terrain flora on|off                       grass, flowers, bushes (only drawn, no physics)\n"
        "  terrain map <file.png> [size_m] [pixels]   map from above around here: biomes, height, rivers (blue)\n"
        "TREES (trees.json; forests come from the seed, near players they are real fixed parts: saw the trunk to fell)\n"
        "  trees | tree plant <type> [form] [x z] [scale] | tree nearest [type] | scene trees (all types side by side)\n"
        "  chop <id> <x y z> [dx dz]                  axe: one blow cuts a notch into wood (play mode: X takes the axe, left mouse hits)\n"
        "  trees reload                               trees.json again (this world takes it over)\n"
        "BLUEPRINTS (holograms you fill with real material; files in assets/blueprints and %APPDATA%\\Werkstatt\\blueprints)\n"
        "  plan [list] | plan place <template> <x y z> [yaw] [fixed|physics] | plan info <plan> | plan remove <plan>\n"
        "  plan fill <plan> <piece|auto> <part> | plan dismantle <part>   (play mode: B places, E inserts, hold Q dismantles)\n"
        "  plan save <name> [label] | plan delete <name>   own template from the selected parts (build mode)\n"
        "  lan [status] | lan host | lan join <address> | lan leave       multiplayer in the local network\n"
        "  lan accept [name|all] | lan decline <name> | lan kick <name> | lan confirm on|off | lan name <text>\n"
        "  world | world save | world menu         the open world (from the world list): info, save now, back to the main menu\n"
        "  menu | menu <page|command>              where you are (game or main menu); 'menu worlds', 'menu play <world>' ... go to the\n"
        "                                          main menu and run there ('menu help' in the main menu lists all menu commands)\n";
}

CommandResult runCommand(CommandHost& host, const std::string& lineIn) {
    CommandResult res;
    std::string line = lineIn;
    if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // UTF-8-BOM (z.B. aus dem Windows-Editor)
    std::vector<std::string> t = tokenize(line);
    if (t.empty() || t[0][0] == '#') return res;
    World& w = host.world();
    const std::string cmd = normalizeWord(t[0]);
    auto fail = [&](const std::string& msg) {
        res.ok = false;
        res.text = "Error: " + msg;
        return res;
    };
    auto num = [&](size_t i, double& v) { return i < t.size() && toNum(t[i], v); };
    auto word = [&](size_t i) { return i < t.size() ? normalizeWord(t[i]) : std::string(); };
    auto obj = [&](size_t i) -> Object* {
        if (i >= t.size()) return nullptr;
        std::string n = normalizeWord(t[i]);
        double id = 0;
        if (n == "selected" || n == "sel") return w.find(host.selectedId());
        if (n == "last") return w.find(w.lastCreatedId());
        if (!n.empty() && n[0] == '#') n = n.substr(1);
        if (!toNum(n, id)) return nullptr;
        return w.find((uint32_t)id);
    };
    auto jointNr = [&](size_t i, double& v) {
        if (i >= t.size()) return false;
        std::string s = normalizeWord(t[i]);
        if (!s.empty() && s[0] == 'j') s = s.substr(1);
        return toNum(s, v);
    };
    std::ostringstream out;
    // Ergebnis einer Bearbeitung: alle entstandenen Teile auflisten
    auto partsText = [&](const std::string& head, const std::vector<uint32_t>& ids, uint32_t fallback) {
        out << head << "\n";
        std::vector<uint32_t> list = ids;
        if (list.empty()) list.push_back(fallback);
        for (uint32_t i : list)
            if (Object* p = w.find(i)) out << "  " << brief(*p) << "\n";
        res.text = out.str();
        return res;
    };

    {
        std::string appOut;
        if (host.appCommand(cmd, t, appOut)) {
            res.ok = appOut.rfind("Error", 0) != 0;
            res.text = appOut;
            return res;
        }
    }

    if (cmd == "help" || cmd == "?") {
        res.text = commandHelp() + "\n" + scriptHelp();
    } else if (cmd == "status") {
        size_t moving = 0;
        for (auto& o : w.objects())
            if (o->body != kNoBody && !o->fixed && w.physics().isActive(o->body)) ++moving;
        out << "physics       " << (w.simulating() ? "running (play mode)" : "paused (build mode)") << "\n";
        out << "parts         " << w.objects().size() << " in " << w.grid().chunkCount() << " chunks, " << moving << " active\n";
        auto sel = host.selectedIds();
        out << "selected      ";
        if (sel.empty()) out << "none";
        for (uint32_t id : sel)
            if (Object* o = w.find(id)) out << "#" << id << " " << o->name << "  ";
        out << "\n";
        out << "undo          " << w.undoCount() << " steps\n";
        out << "rescued       " << w.rescueCount() << " times a part was put back to its last free spot\n";
        double firePower = 0;
        for (const Fire& f : w.fires()) firePower += f.power;
        out << "heat          " << (w.heat() ? "on" : "off") << ", air " << fmtNum(w.ambient(), 0) << " °C, " << w.fires().size() << " fire(s) "
            << fmtNum(firePower / 1000.0, 1) << " kW, " << w.burningParts() << " part(s) burning\n";
        out << host.statusText();
        res.text = out.str();
    } else if (cmd == "wait") {
        double s = 1.0;
        num(1, s);
        res.text = host.wait(std::clamp(s, 0.0, 600.0));
    } else if (cmd == "profile") {
        // Rechenzeit je Abschnitt (Engine): wo bleibt die Zeit?
        if (word(1) == "reset") {
            prof::reset();
            res.text = "profile reset";
        } else if (word(1) == "off" || word(1) == "on") {
            prof::setEnabled(word(1) == "on");
            res.text = std::string("profile ") + (prof::enabled() ? "on" : "off");
        } else {
            res.text = prof::report();
        }
    } else if (cmd == "materials" && t.size() > 1) {
        // Materialdatei: anzeigen, ändern, neu, speichern, neu laden
        const std::string sub = word(1);
        const std::string file = materialFile().empty() ? std::string("(built-in list)") : materialFile();
        if (sub == "file") {
            res.text = "materials file: " + file + (materialFile().empty() ? "\nsaving writes " + defaultMaterialFile() : std::string());
        } else if (sub == "keys") {
            const char* group = "";
            for (const MaterialField& f : materialFields()) {
                if (std::strcmp(group, f.group) != 0) out << (*group ? "\n" : "") << f.group << "\n";
                group = f.group;
                std::string key = f.key;
                key.resize(std::max<size_t>(key.size(), 20), ' ');
                out << "  " << key << f.label << (*f.unit ? std::string(" (") + f.unit + ")" : std::string());
                if (f.type == MaterialField::Choice) {
                    out << ":";
                    for (int i = 0; f.choices[i]; ++i) out << " " << f.choices[i];
                }
                if (f.type == MaterialField::Optional) out << ", or none";
                out << "\n";
            }
            res.text = out.str();
        } else if (sub == "show") {
            const int m = t.size() > 2 ? findMaterial(t[2]) : -1;
            if (m < 0) return fail("materials show <material>   (" + materialNames() + ")");
            res.text = materialText(m);
        } else if (sub == "set") {
            const int m = t.size() > 2 ? findMaterial(t[2]) : -1;
            if (m < 0 || t.size() < 5) return fail("materials set <material> <key> <value...>   ('materials keys' lists the keys)");
            const std::string key = normalizeWord(t[3]);
            std::string value;
            for (size_t i = 4; i < t.size(); ++i) value += (i > 4 ? " " : "") + t[i];
            std::string err;
            if (key == "name") {
                if (!renameMaterial(m, value, &err)) return fail(err);
            } else {
                const MaterialField* f = findMaterialField(key);
                if (!f) return fail("unknown key '" + key + "' ('materials keys' lists them)");
                Material copy = materials()[m];
                if (!setMaterialValue(copy, *f, value, &err)) return fail(err);
                editableMaterials()[m] = copy;
            }
            w.applyMaterials({});
            res.text = std::string(materials()[m].name) + ": " + key + " = " +
                       (key == "name" ? std::string(materials()[m].name) : materialValue(materials()[m], *findMaterialField(key))) +
                       "   ('materials save' writes the file)";
        } else if (sub == "new") {
            const int base = t.size() > 3 ? findMaterial(t[3]) : (int)materials().size() - 1;
            if (t.size() < 3 || base < 0) return fail("materials new <name> [copy_of]   e.g. materials new pine spruce");
            std::string err;
            const int m = addMaterial(t[2], base, &err);
            if (m < 0) return fail(err);
            res.text = "new material '" + std::string(materials()[m].name) + "' (copy of " + materials()[base].name +
                       "). Change it with 'materials set " + materials()[m].name + " <key> <value>' or in the window, then 'materials save'.\n" +
                       materialText(m);
        } else if (sub == "save") {
            const std::string path = t.size() > 2 ? t[2] : materialSavePath();
            std::string err;
            if (!saveMaterialFile(path, &err)) return fail(err);
            res.text = std::to_string(materials().size()) + " materials saved to " + path;
        } else if (sub == "reload" || sub == "load") {
            const std::string path = t.size() > 2 ? t[2] : (materialFile().empty() ? defaultMaterialFile() : materialFile());
            std::vector<std::string> before;
            for (const Material& m : materials()) before.push_back(m.name);
            std::string report;
            if (!loadMaterialFile(path, &report)) return fail("not loaded, the old list stays:\n" + report);
            std::vector<int> remap;
            std::string lost;
            for (const std::string& n : before) {
                const int i = findMaterialIndex(n.c_str());
                remap.push_back(std::max(i, 0));
                if (i < 0) lost += " " + n;
            }
            w.applyMaterials(remap);
            res.text = std::to_string(materials().size()) + " materials loaded from " + path + (report.empty() ? "" : "\n" + report) +
                       (lost.empty() ? "" : "\nmissing now (parts use " + std::string(materials()[0].name) + "):" + lost);
        } else {
            return fail("materials [file | keys | show <m> | set <m> <key> <value> | new <name> [copy_of] | save [file] | reload [file]]");
        }
    } else if (cmd == "materials") {
        out << "file: " << (materialFile().empty() ? std::string("(built-in list)") : materialFile()) << "\n";
        for (size_t i = 0; i < materials().size(); ++i) {
            const Material& m = materials()[i];
            out << m.name << ": " << m.density << " kg/m3, friction static " << fmtNum(m.frictionStatic, 2) << " sliding "
                << fmtNum(m.frictionKinetic, 2) << ", rolling " << fmtNum(m.rolling, 4) << ", restitution " << fmtNum(m.restitution, 2)
                << ", tension " << m.tensileMPa << " MPa, E " << m.youngGPa << " GPa, "
                << (m.minBendRatio < 0 ? std::string("not bendable") : "bend radius >= " + fmtNum(m.minBendRatio, 2) + " x thickness");
            const Thermal& th = thermal((int)i);
            out << ", λ " << fmtNum(th.conductivity, 2) << " W/mK";
            if (th.meltC > 0) out << ", melts " << fmtNum(th.meltC, 0) << " °C";
            if (th.ignitionC > 0) out << ", ignites " << fmtNum(th.ignitionC, 0) << " °C";
            if (th.brittleC > -400) out << ", brittle below " << fmtNum(th.brittleC, 0) << " °C";
            out << "\n";
        }
        res.text = out.str();
    } else if (cmd == "new") {
        if (t.size() < 3) return fail("new <shape> <material> ...");
        std::string form = normalizeWord(t[1]);
        int mat = findMaterial(t[2]);
        if (mat < 0) return fail("unknown material '" + t[2] + "'. Available: " + materialNames());
        Object* o = nullptr;
        double a, b, c, d;
        if (form == "box" || form == "board" || form == "block") {
            if (!num(3, a) || !num(4, b) || !num(5, c) || a <= 0 || b <= 0 || c <= 0) return fail("new box <mat> <L> <W> <H>");
            glm::dvec3 size(a / 100, c / 100, b / 100);
            w.pushUndo();
            o = w.addBox(mat, size, placeAt(host, t, 6, size * 0.5));
        } else if (form == "cylinder" || form == "rod") {
            if (!num(3, a) || !num(4, b) || a <= 0 || b <= 0) return fail("new cylinder <mat> <D> <H>");
            w.pushUndo();
            o = w.addCylinder(mat, a / 100, b / 100, placeAt(host, t, 5, glm::dvec3(a, b, a) / 200.0));
        } else if (form == "tube" || form == "pipe") {
            if (!num(3, a) || !num(4, b) || !num(5, c) || a <= 0 || b <= 0 || b >= a || c <= 0)
                return fail("new tube <mat> <D_out> <D_in> <L>  (inner < outer)");
            w.pushUndo();
            o = w.addTube(mat, a / 100, b / 100, c / 100, placeAt(host, t, 6, glm::dvec3(a, c, a) / 200.0));
        } else if (form == "sphere" || form == "ball") {
            if (!num(3, a) || a <= 0) return fail("new sphere <mat> <D>");
            w.pushUndo();
            o = w.addSphere(mat, a / 100, placeAt(host, t, 4, glm::dvec3(a) / 200.0));
        } else if (form == "gear") {
            if (!num(3, a) || !num(4, b) || !num(5, c) || a < 6 || b <= 0 || c <= 0)
                return fail("new gear <mat> <teeth(>=6)> <module_mm> <width_cm> [bore_mm]");
            size_t posIdx = 6;
            d = 0;
            if (t.size() == 7 || t.size() == 10) {
                num(6, d);
                posIdx = 7;
            }
            double ra = b / 1000 * (a / 2 + 1);
            w.pushUndo();
            o = w.addGear(mat, (int)a, b / 1000, c / 100, d / 1000, placeAt(host, t, posIdx, glm::dvec3(ra, c / 200, ra)));
        } else if (form == "propeller" || form == "fan") {
            if (!num(3, a) || a <= 0) return fail("new propeller <mat> <D_cm> [blades] [angle_deg] [blade_width_cm] [thickness_mm]");
            double blades = 2, pitch = 20, chord = a / 8, thick = 3;
            num(4, blades);
            num(5, pitch);
            num(6, chord);
            num(7, thick);
            w.pushUndo();
            o = w.addPropeller(mat, a / 100, (int)blades, pitch, chord / 100, thick / 1000,
                               w.findFreeSpot(glm::dvec3(a / 200, chord / 100, a / 200), host.spawnReference()));
        } else if (form == "wedge" || form == "axe" || form == "chisel") {
            if (!num(3, a) || !num(4, b) || a <= 0 || b <= 0) return fail("new wedge <mat> <edge_length> <height> [angle_deg] [x y z]");
            size_t posIdx = 5;
            c = 25;
            if (t.size() == 6 || t.size() == 9) {
                if (!num(5, c) || c < 2 || c > 120) return fail("wedge angle must be between 2 and 120 degrees");
                posIdx = 6;
            }
            const double back = 2 * b * std::tan(c * 3.14159265358979 / 360.0);
            w.pushUndo();
            o = w.addWedge(mat, a / 100, b / 100, c, placeAt(host, t, posIdx, glm::dvec3(a / 100, b / 100, back / 100) * 0.5));
        } else if (form == "spike" || form == "nail" || form == "point") {
            if (!num(3, a) || !num(4, b) || a <= 0 || b <= 0) return fail("new spike <mat> <D> <length> [tip_radius_mm] [x y z]");
            size_t posIdx = 5;
            c = 0;
            if (t.size() == 6 || t.size() == 9) {
                if (!num(5, c) || c < 0) return fail("tip radius must be >= 0 mm");
                posIdx = 6;
            }
            w.pushUndo();
            o = w.addSpike(mat, a / 100, b / 100, c / 1000, placeAt(host, t, posIdx, glm::dvec3(a, b, a) / 200.0));
        } else {
            return fail("unknown shape '" + t[1] + "' (box, cylinder, tube, sphere, gear, propeller, wedge, spike)");
        }
        if (!o) return fail("the part could not be created");
        host.select(o->id);
        res.text = "Created: " + brief(*o);
    } else if (cmd == "import") {
        if (t.size() < 2) return fail("import <file.obj> [material] [scale]");
        int mat = t.size() > 2 ? findMaterial(t[2]) : materialIndex("spruce");
        if (mat < 0) return fail("unknown material");
        double scale = 1.0;
        if (t.size() > 3 && !num(3, scale)) return fail("scale is not a number");
        std::string err;
        w.pushUndo();
        Object* o = w.addImported(std::filesystem::u8path(t[1]), mat, scale, glm::dvec3(0, -1e9, 0), &err);
        if (!o) return fail(err);
        host.select(o->id);
        res.text = "Imported: " + brief(*o);
    } else if (cmd == "list" || cmd == "ls") {
        for (auto& o : w.objects()) out << brief(*o) << "\n";
        out << w.objects().size() << " parts in " << w.grid().chunkCount() << " chunks";
        res.text = out.str();
    } else if (cmd == "chunks") {
        out << "chunk size " << kChunkSize << " m, " << w.grid().chunkCount() << " chunks in use\n";
        for (auto& [k, c] : w.grid().chunks()) out << "  (" << k.x << ", " << k.y << ", " << k.z << "): " << c.objects.size() << " parts\n";
        res.text = out.str();
    } else if (cmd == "info") {
        Object* o = obj(1);
        if (!o) return fail("info <id>");
        res.text = details(w, *o);
        if (!o->treeType.empty())  // Baum: Art, Krone, Wurzel, vom Generator (unverändert oder verändert)
            res.text += "\ntree          " + o->treeType + " form " + std::to_string(o->treeVariant) + ", size " + fmtNum(o->treeScale, 2) +
                        (o->crown ? ", with crown" : "") + (o->rooted ? ", rooted" : "") +
                        (o->treeKey ? (w.untouchedTree(*o) ? ", from the generator (as grown, not saved)" : ", from the generator (changed, saved)")
                                    : ", planted");
    } else if (cmd == "inspect" || cmd == "physics") {
        Object* o = obj(1);
        if (!o) return fail("inspect <id>   (live: forces, energy, tipping; run 'sim' or play mode first)");
        w.setInspect(o->id);
        host.showInspector(o->id);
        res.text = w.inspectText(o->id);
    } else if (cmd == "overlaps") {
        Object* o = obj(1);
        if (!o) return fail("overlaps <id>");
        auto ov = w.overlapsOf(*o, 0.0001);
        if (ov.empty()) out << brief(*o) << "\n  touches nothing deeper than 0.1 mm";
        else out << brief(*o) << "\n";
        for (auto& [id, depth] : ov) {
            Object* p = w.find(id);
            out << "  sticks " << fmtNum(depth * 1000, 2) << " mm into " << (p ? "#" + std::to_string(id) + " " + p->name : std::string("the floor")) << "\n";
        }
        res.text = out.str();
    // ---------------------------------------------------------------- Sägen, Bohren
    } else if (cmd == "cut" || cmd == "saw") {
        Object* o = obj(1);
        int ax = t.size() > 2 ? axisIndex(t[2]) : -1;
        double dist, ang = 0, kerf = 0;
        if (!o || ax < 0 || !num(3, dist)) return fail("cut <id> <x|y|z> <distance_cm> [angle] [kerf_mm]");
        num(4, ang);
        num(5, kerf);
        glm::dvec3 q = (o->bbMin + o->bbMax) * 0.5;
        q[ax] = o->bbMin[ax] + dist / 100.0;
        glm::dvec3 n(0);
        n[ax] = 1;
        if (std::abs(ang) > 1e-9) n = glm::angleAxis(glm::radians(ang), ax == 1 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0)) * n;
        solid::Plane pl;
        pl.n = glm::normalize(n);
        pl.d = glm::dot(pl.n, q);
        std::vector<uint32_t> ids;
        std::string err;
        uint32_t id = o->id;
        w.pushUndo();
        if (!w.cut(id, pl, kerf / 1000.0, &ids, &err)) {
            w.discardUndo();
            return fail(err);
        }
        return partsText("Cut -> " + std::to_string(ids.size()) + " parts:", ids, id);
    } else if (cmd == "drill" || cmd == "drillat") {
        Object* o = obj(1);
        if (!o) return fail("part missing");
        glm::dvec3 from, dir;
        double dmm = 0, depth = 0;
        if (cmd == "drillat") {
            double v[6];
            for (int i = 0; i < 6; ++i)
                if (!num(2 + i, v[i])) return fail("drillat <id> <x y z> <dx dy dz> <D_mm> [depth_mm]");
            if (!num(8, dmm)) return fail("diameter missing");
            num(9, depth);
            from = glm::dvec3(v[0], v[1], v[2]) / 100.0;
            dir = glm::dvec3(v[3], v[4], v[5]);
            if (glm::length(dir) < 1e-9) return fail("direction is zero");
            dir = glm::normalize(dir);
            depth = depth > 0 ? depth / 1000.0 : 0.0;
        } else {
            int k, s;
            double a, b;
            if (t.size() < 3 || !parseFace(t[2], k, s) || !num(3, a) || !num(4, b) || !num(5, dmm))
                return fail("drill <id> <face> <a_cm> <b_cm> <D_mm> [depth_mm]");
            num(6, depth);
            int u, v;
            faceAxes(k, u, v);
            from = (o->bbMin + o->bbMax) * 0.5;
            from[k] = (s > 0 ? o->bbMax[k] : o->bbMin[k]) + s * 0.001;
            from[u] += a / 100.0;
            from[v] += b / 100.0;
            dir = glm::dvec3(0);
            dir[k] = -s;
            depth = depth > 0 ? depth / 1000.0 + 0.001 : 0.0;
        }
        if (dmm <= 0) return fail("diameter must be > 0");
        std::vector<uint32_t> ids;
        std::string err;
        double removed = 0, massBefore = o->mass;
        uint32_t id = o->id;
        w.pushUndo();
        if (!w.drill(id, from, dir, dmm / 1000.0, depth, &ids, &removed, &err)) {
            w.discardUndo();
            return fail(err);
        }
        double after = 0;
        for (uint32_t i : ids)
            if (Object* p = w.find(i)) after += p->mass;
        return partsText("Drilled: D " + fmtNum(dmm, 1) + " mm, removed " + fmtNum(removed * 1e6, 3) + " cm3 (" + fmtNum(massBefore - after, 4) +
                             " kg of chips)",
                         ids, id);
    } else if (cmd == "countersink" || cmd == "pocket" || cmd == "groove" || cmd == "rebate" || cmd == "slot" || cmd == "tenon" ||
               cmd == "halflap") {
        Object* o = obj(1);
        if (!o) return fail(cmd + " <id> ...   ('help' shows the parameters)");
        if (o->kind != ObjKind::Solid) return fail("imported models cannot be milled (yet)");
        const glm::dvec3 mn = o->bbMin, mx = o->bbMax, c = (mn + mx) * 0.5, ext = mx - mn;
        std::vector<std::vector<solid::Plane>> tools;
        std::string what;
        int k = 1, s = 1;
        if (t.size() < 3 || !parseFace(t[2], k, s)) return fail(cmd + ": face missing (+x -x +y -y +z -z / top bottom ...)");
        int u, v;
        faceAxes(k, u, v);
        const double face = s > 0 ? mx[k] : mn[k];
        // Bereich entlang der Flächennormalen: von der Oberfläche "depth" tief ins Teil (plus etwas Luft außen)
        auto depthRange = [&](double depth, double& lo, double& hi) {
            if (s > 0) {
                lo = face - depth;
                hi = face + kMargin;
            } else {
                lo = face - kMargin;
                hi = face + depth;
            }
        };
        if (cmd == "countersink") {
            double a, b, dmm, angle = 90;
            if (!num(3, a) || !num(4, b) || !num(5, dmm) || dmm <= 0) return fail("countersink <id> <face> <a> <b> <D_mm> [angle_deg]");
            num(6, angle);
            angle = std::clamp(angle, 30.0, 150.0);
            const double r = dmm / 2000.0, tanHalf = std::tan(glm::radians(angle / 2));
            glm::dvec3 from = c;
            from[k] = face + s * kMargin;
            from[u] += a / 100;
            from[v] += b / 100;
            glm::dvec3 dir(0);
            dir[k] = -s;
            const double r0 = r + kMargin * tanHalf, length = kMargin + r / tanHalf;
            tools.push_back(solid::coneTool(from, dir, r0, 0.0002, length - 0.0002 / tanHalf, 48));
            what = "countersink D " + fmtNum(dmm, 1) + " mm, " + fmtNum(angle, 0) + "°";
        } else if (cmd == "pocket") {
            double a, b, L, W, D, R = 0;
            if (!num(3, a) || !num(4, b) || !num(5, L) || !num(6, W) || !num(7, D) || L <= 0 || W <= 0 || D <= 0)
                return fail("pocket <id> <face> <a> <b> <length_mm> <width_mm> <depth_mm> [corner_radius_mm]");
            num(8, R);
            L /= 1000;
            W /= 1000;
            D /= 1000;
            R = std::clamp(R / 1000, 0.0, std::min(L, W) / 2);
            const double cu = c[u] + a / 100, cv = c[v] + b / 100;
            double lo, hi;
            depthRange(D, lo, hi);
            auto box = [&](double u0, double u1, double v0, double v1) {
                glm::dvec3 bmn, bmx;
                bmn[k] = lo;
                bmx[k] = hi;
                bmn[u] = u0;
                bmx[u] = u1;
                bmn[v] = v0;
                bmx[v] = v1;
                tools.push_back(solid::boxTool(bmn, bmx));
            };
            if (R < 1e-5) {
                box(cu - L / 2, cu + L / 2, cv - W / 2, cv + W / 2);
            } else {
                box(cu - L / 2 + R, cu + L / 2 - R, cv - W / 2, cv + W / 2);
                box(cu - L / 2, cu + L / 2, cv - W / 2 + R, cv + W / 2 - R);
                for (int i = 0; i < 4; ++i) {
                    glm::dvec3 from(0);
                    from[k] = face + s * kMargin;
                    from[u] = cu + ((i & 1) ? 1 : -1) * (L / 2 - R);
                    from[v] = cv + ((i & 2) ? 1 : -1) * (W / 2 - R);
                    glm::dvec3 dir(0);
                    dir[k] = -s;
                    tools.push_back(solid::drillTool(from, dir, R, D + kMargin, 32));
                }
            }
            what = "pocket " + fmtNum(L * 1000, 1) + " x " + fmtNum(W * 1000, 1) + " mm, " + fmtNum(D * 1000, 1) + " mm deep";
        } else if (cmd == "groove") {
            int j = t.size() > 3 ? axisIndex(t[3]) : -1;
            double off, W, D;
            if (j < 0 || j == k || !num(4, off) || !num(5, W) || !num(6, D) || W <= 0 || D <= 0)
                return fail("groove <id> <face> <along x|y|z (not the face axis)> <offset_cm> <width_mm> <depth_mm>");
            const int m = 3 - k - j;
            double lo, hi;
            depthRange(D / 1000, lo, hi);
            glm::dvec3 bmn, bmx;
            bmn[k] = lo;
            bmx[k] = hi;
            bmn[j] = mn[j] - 0.01;
            bmx[j] = mx[j] + 0.01;
            bmn[m] = c[m] + off / 100 - W / 2000;
            bmx[m] = c[m] + off / 100 + W / 2000;
            tools.push_back(solid::boxTool(bmn, bmx));
            what = "groove " + fmtNum(W, 1) + " x " + fmtNum(D, 1) + " mm along " + "xyz"[j];
        } else if (cmd == "rebate") {
            int k2, s2;
            double W, D;
            if (t.size() < 4 || !parseFace(t[3], k2, s2) || k2 == k || !num(4, W) || !num(5, D) || W <= 0 || D <= 0)
                return fail("rebate <id> <face> <side_face> <width_mm> <depth_mm>   e.g. rebate 5 top front 10 5");
            const int m = 3 - k - k2;
            double lo, hi;
            depthRange(D / 1000, lo, hi);
            glm::dvec3 bmn, bmx;
            bmn[k] = lo;
            bmx[k] = hi;
            bmn[k2] = s2 > 0 ? mx[k2] - W / 1000 : mn[k2] - kMargin;
            bmx[k2] = s2 > 0 ? mx[k2] + kMargin : mn[k2] + W / 1000;
            bmn[m] = mn[m] - 0.01;
            bmx[m] = mx[m] + 0.01;
            tools.push_back(solid::boxTool(bmn, bmx));
            what = "rebate " + fmtNum(W, 1) + " x " + fmtNum(D, 1) + " mm";
        } else if (cmd == "slot") {
            double a1, b1, a2, b2, W, D = 0;
            if (!num(3, a1) || !num(4, b1) || !num(5, a2) || !num(6, b2) || !num(7, W) || W <= 0)
                return fail("slot <id> <face> <a1> <b1> <a2> <b2> <width_mm> [depth_mm, 0 = through]");
            num(8, D);
            const double depth = D > 0 ? D / 1000 : ext[k] + 0.01;
            glm::dvec3 p1 = c, p2 = c;
            p1[k] = p2[k] = face + s * kMargin;
            p1[u] += a1 / 100;
            p1[v] += b1 / 100;
            p2[u] += a2 / 100;
            p2[v] += b2 / 100;
            glm::dvec3 dir(0);
            dir[k] = -s;
            const double r = W / 2000;
            tools.push_back(solid::drillTool(p1, dir, r, depth + kMargin, 32));
            if (glm::length(p2 - p1) > 1e-6) {
                tools.push_back(solid::drillTool(p2, dir, r, depth + kMargin, 32));
                const glm::dvec3 along = glm::normalize(p2 - p1), side = glm::cross(dir, along);
                std::vector<solid::Plane> bx;
                auto plane = [&](const glm::dvec3& n, const glm::dvec3& p) {
                    solid::Plane pl;
                    pl.n = n;
                    pl.d = glm::dot(n, p);
                    bx.push_back(pl);
                };
                plane(along, p2);
                plane(-along, p1);
                plane(side, p1 + side * r);
                plane(-side, p1 - side * r);
                plane(-dir, p1);
                plane(dir, p1 + dir * (depth + kMargin));
                tools.push_back(bx);
            }
            what = "slot " + fmtNum(glm::length(p2 - p1) * 1000 + W, 1) + " x " + fmtNum(W, 1) + " mm" + (D > 0 ? ", " + fmtNum(D, 1) + " mm deep" : ", through");
        } else if (cmd == "tenon") {
            double len, th, wd;
            if (!num(3, len) || !num(4, th) || !num(5, wd) || len <= 0 || th <= 0 || wd <= 0)
                return fail("tenon <id> <end_face> <length_cm> <thickness_mm> <width_mm>");
            int ta = u, wa = v;
            if (ext[wa] < ext[ta]) std::swap(ta, wa);  // Dicke entlang der dünneren Richtung
            th /= 1000;
            wd /= 1000;
            len /= 100;
            if (th >= ext[ta] - 1e-6 && wd >= ext[wa] - 1e-6) return fail("the tenon is as big as the part - nothing to remove");
            if (th > ext[ta] + 1e-6 || wd > ext[wa] + 1e-6 || len >= ext[k]) return fail("the tenon is larger than the part");
            double lo = s > 0 ? face - len : face - kMargin, hi = s > 0 ? face + kMargin : face + len;
            auto box = [&](double t0, double t1, double w0, double w1) {
                if (t1 - t0 < 1e-7 || w1 - w0 < 1e-7) return;
                glm::dvec3 bmn, bmx;
                bmn[k] = lo;
                bmx[k] = hi;
                bmn[ta] = t0;
                bmx[ta] = t1;
                bmn[wa] = w0;
                bmx[wa] = w1;
                tools.push_back(solid::boxTool(bmn, bmx));
            };
            const double e = 0.005;
            box(mn[ta] - e, c[ta] - th / 2, mn[wa] - e, mx[wa] + e);
            box(c[ta] + th / 2, mx[ta] + e, mn[wa] - e, mx[wa] + e);
            box(c[ta] - th / 2, c[ta] + th / 2, mn[wa] - e, c[wa] - wd / 2);
            box(c[ta] - th / 2, c[ta] + th / 2, c[wa] + wd / 2, mx[wa] + e);
            what = "tenon " + fmtNum(len * 100, 1) + " cm long, " + fmtNum(th * 1000, 1) + " x " + fmtNum(wd * 1000, 1) + " mm";
        } else {  // halflap
            double len;
            if (!num(3, len) || len <= 0) return fail("halflap <id> <end_face> <length_cm> [side_face]");
            len /= 100;
            if (len >= ext[k]) return fail("the half lap is longer than the part");
            int k2 = ext[u] < ext[v] ? u : v, s2 = 1;
            if (t.size() > 4 && (!parseFace(t[4], k2, s2) || k2 == k)) return fail("side face must lie across the end face");
            const int m = 3 - k - k2;
            glm::dvec3 bmn, bmx;
            bmn[k] = s > 0 ? face - len : face - kMargin;
            bmx[k] = s > 0 ? face + kMargin : face + len;
            bmn[k2] = s2 > 0 ? c[k2] : mn[k2] - kMargin;
            bmx[k2] = s2 > 0 ? mx[k2] + kMargin : c[k2];
            bmn[m] = mn[m] - 0.01;
            bmx[m] = mx[m] + 0.01;
            tools.push_back(solid::boxTool(bmn, bmx));
            what = "half lap " + fmtNum(len * 100, 1) + " cm, half the thickness from " + faceName(k2, s2);
        }
        std::vector<uint32_t> ids;
        std::string err;
        double removed = 0;
        uint32_t id = o->id;
        w.pushUndo();
        if (!w.removeMaterial(id, tools, &removed, &ids, &err)) {
            w.discardUndo();
            return fail(err);
        }
        return partsText("Milled " + what + " on " + faceName(k, s) + ", removed " + fmtNum(removed * 1e6, 3) + " cm3", ids, id);
    // ---------------------------------------------------------------- Kanten, Form
    } else if (cmd == "edges") {
        Object* o = obj(1);
        if (!o) return fail("edges <id> [selection]");
        const auto& edges = w.edgesOf(*o);
        std::vector<int> sel;
        std::string err;
        if (t.size() > 2) {
            if (!selectEdges(edges, t[2], sel, err)) return fail(err);
        } else {
            for (size_t i = 0; i < edges.size(); ++i) sel.push_back((int)i);
        }
        out << brief(*o) << "\n";
        for (int i : sel) out << "  " << edgeLine(i, edges[i]) << "\n";
        out << sel.size() << " of " << edges.size() << " edges";
        res.text = out.str();
    } else if (cmd == "round" || cmd == "chamfer" || cmd == "sand") {
        Object* o = obj(1);
        if (!o) return fail(cmd == "sand" ? "sand <id> [radius_mm]" : cmd + " <id> <edges> <size_mm>" + (cmd == "round" ? " [segments]" : ""));
        double size = 1, segs = 0;
        std::string selTok = "all";
        if (cmd == "sand") {
            num(2, size);
            segs = 3;
        } else {
            if (t.size() < 4 || !num(3, size)) return fail(cmd + " <id> <edges> <size_mm>   edges: all | x | +y | top+front | e3 ... (see 'edges')");
            selTok = t[2];
            num(4, segs);
        }
        if (size <= 0) return fail("size must be > 0");
        std::vector<int> sel;
        std::string err;
        if (!selectEdges(w.edgesOf(*o), selTok, sel, err)) return fail(err);
        int n = cmd == "chamfer" ? 1 : (segs >= 2 ? (int)segs : 8);
        uint32_t id = o->id;
        const double v0 = o->volume, m0 = o->mass;
        w.pushUndo();
        if (!w.blendEdges(id, sel, size / 1000.0, n, &err)) {
            w.discardUndo();
            return fail(err);
        }
        Object* p = w.find(id);
        out << (cmd == "chamfer" ? "Chamfered " : cmd == "sand" ? "Sanded " : "Rounded ") << sel.size() << " edge(s) with " << fmtNum(size, 1)
            << " mm" << (cmd == "round" ? " radius" : "") << ", volume " << fmtNum((p ? p->volume - v0 : 0) * 1e6, 3) << " cm3 ("
            << fmtNum(p ? p->mass - m0 : 0, 4) << " kg)\n";
        if (p) out << "  " << brief(*p) << "\n";
        res.text = out.str();
    } else if (cmd == "plane") {
        Object* o = obj(1);
        int k, s;
        double mm;
        if (!o || t.size() < 4 || !parseFace(t[2], k, s) || !num(3, mm)) return fail("plane <id> <face> <mm>");
        std::string err;
        uint32_t id = o->id;
        const double before = (o->bbMax[k] - o->bbMin[k]) * 1000;
        w.pushUndo();
        if (!w.planeOff(id, k, s, mm / 1000.0, &err)) {
            w.discardUndo();
            return fail(err);
        }
        Object* p = w.find(id);
        res.text = "Planed " + fmtNum(mm, 2) + " mm off " + faceName(k, s) + ": " + fmtNum(before, 1) + " -> " +
                   fmtNum(p ? (p->bbMax[k] - p->bbMin[k]) * 1000 : 0, 1) + " mm\n  " + (p ? brief(*p) : std::string());
    } else if (cmd == "turn") {
        Object* o = obj(1);
        int ax = t.size() > 2 ? axisIndex(t[2]) : -1;
        double from, to, d0, d1;
        if (!o || ax < 0 || !num(3, from) || !num(4, to) || !num(5, d0)) return fail("turn <id> <x|y|z> <from_cm> <to_cm> <D_mm> [D2_mm]");
        d1 = d0;
        num(6, d1);
        std::string err;
        double removed = 0;
        uint32_t id = o->id;
        w.pushUndo();
        if (!w.turnPart(id, ax, from / 100, to / 100, d0 / 1000, d1 / 1000, &removed, &err)) {
            w.discardUndo();
            return fail(err);
        }
        Object* p = w.find(id);
        res.text = "Turned " + fmtNum(from, 1) + "-" + fmtNum(to, 1) + " cm along " + "xyz"[ax] + " to D " + fmtNum(d0, 1) +
                   (std::abs(d1 - d0) > 1e-9 ? "-" + fmtNum(d1, 1) : std::string("")) + " mm, removed " + fmtNum(removed * 1e6, 3) + " cm3\n  " +
                   (p ? brief(*p) : std::string());
    } else if (cmd == "bend") {
        Object* o = obj(1);
        int ax = t.size() > 2 ? axisIndex(t[2]) : -1;
        double at, ang, rad = 0;
        if (!o || ax < 0 || !num(3, at) || !num(4, ang)) return fail("bend <id> <x|y|z> <at_cm> <angle_deg> [inner_radius_mm]");
        num(5, rad);
        std::string err, info;
        uint32_t id = o->id;
        w.pushUndo();
        if (!w.bendPart(id, ax, at / 100, ang, rad / 1000, &err, &info)) {
            w.discardUndo();
            return fail(err);
        }
        Object* p = w.find(id);
        res.text = "Bent " + info + "\n  " + (p ? brief(*p) : std::string());
    } else if (cmd == "mirror") {
        Object* o = obj(1);
        int ax = t.size() > 2 ? axisIndex(t[2]) : -1;
        if (!o || ax < 0) return fail("mirror <id> <x|y|z> [copy]");
        std::string err;
        w.pushUndo();
        uint32_t id = o->id;
        if (word(3) == "copy") {  // gespiegelte Kopie daneben (z.B. linkes und rechtes Teil)
            Object* c = w.duplicate(id, glm::dvec3(o->wMax.x - o->wMin.x + 0.02, 0, 0));
            if (!c) return fail("copy failed");
            id = c->id;
            host.select(id);
        }
        if (!w.mirrorPart(id, ax, &err)) {
            w.discardUndo();
            return fail(err);
        }
        res.text = std::string("Mirrored across local ") + "xyz"[ax] + ": " + brief(*w.find(id));
    } else if (cmd == "scale") {
        Object* o = obj(1);
        double fx, fy, fz;
        if (!o || !num(2, fx)) return fail("scale <id> <factor> | scale <id> <fx> <fy> <fz>");
        fy = fz = fx;
        if (t.size() >= 5 && (!num(3, fy) || !num(4, fz))) return fail("scale <id> <fx> <fy> <fz>");
        std::string err;
        const double m0 = o->mass;
        w.pushUndo();
        if (!w.scalePart(o->id, glm::dvec3(fx, fy, fz), &err)) {
            w.discardUndo();
            return fail(err);
        }
        res.text = "Scaled (mass " + fmtNum(m0, 4) + " -> " + fmtNum(o->mass, 4) + " kg): " + brief(*o);
    } else if (cmd == "merge") {
        Object* a = obj(1);
        if (!a || t.size() < 3) return fail("merge <a> <b> [c ...]   (same material, touching)");
        uint32_t id = a->id;
        w.pushUndo();
        for (size_t i = 2; i < t.size(); ++i) {
            Object* b = obj(i);
            if (!b) {
                w.undo();
                return fail("part " + t[i] + " not found");
            }
            std::string err;
            if (!w.mergeParts(id, b->id, &err)) {
                w.undo();
                return fail(err);
            }
        }
        res.text = "Merged into one part: " + brief(*w.find(id));
    } else if (cmd == "subtract" || cmd == "intersect") {
        Object* a = obj(1);
        Object* b = obj(2);
        if (!a || !b) return fail(cmd + " <part> <tool_part> [keep]");
        bool keep = word(3) == "keep";
        std::vector<uint32_t> ids;
        std::string err;
        double removed = 0;
        uint32_t id = a->id;
        w.pushUndo();
        if (!w.booleanCut(id, b->id, cmd == "intersect", keep, &ids, &removed, &err)) {
            w.discardUndo();
            return fail(err);
        }
        return partsText(std::string(cmd == "intersect" ? "Kept the common volume" : "Cut the tool shape out") + ", removed " +
                             fmtNum(removed * 1e6, 3) + " cm3" + (keep ? " (tool kept)" : " (tool part removed)") + ":",
                         ids, id);
    // ---------------------------------------------------------------- Lage
    } else if (cmd == "position" || cmd == "pos") {
        Object* o = obj(1);
        double x, y, z;
        if (!o || !num(2, x) || !num(3, y) || !num(4, z)) return fail("position <id> x y z (cm)");
        w.pushUndo();
        w.setTransform(*o, glm::dvec3(x, y, z) / 100.0, o->rot);
        res.text = brief(*o);
        for (auto& [id, depth] : w.overlapsOf(*o, 0.0005)) {
            Object* p = w.find(id);
            res.text += "\n  warning: sticks " + fmtNum(depth * 1000, 1) + " mm into " + (p ? "#" + std::to_string(id) + " " + p->name : std::string("the floor"));
        }
    } else if (cmd == "move") {
        Object* o = obj(1);
        double x, y, z;
        if (!o || !num(2, x) || !num(3, y) || !num(4, z)) return fail("move <id> dx dy dz [through]   (cm, relative)");
        const glm::dvec3 d = glm::dvec3(x, y, z) / 100.0;
        w.pushUndo();
        if (word(5) == "through" || !w.buildCollision()) {
            w.setTransform(*o, o->pos + d, o->rot);
            res.text = brief(*o);
        } else {
            uint32_t blocker = 0;
            bool blocked = false;
            glm::dvec3 moved = w.sweepMove({o->id}, d, &blocker, &blocked);
            res.text = brief(*o);
            if (blocked) {
                Object* b = w.find(blocker);
                res.text += "\n  stopped after " + fmtNum(glm::length(moved) * 100, 2) + " of " + fmtNum(glm::length(d) * 100, 2) + " cm at " +
                            (b ? "#" + std::to_string(blocker) + " " + b->name : std::string("the floor")) + " (add 'through' to ignore)";
            }
        }
    } else if (cmd == "rotation") {
        Object* o = obj(1);
        double x, y, z;
        if (!o || !num(2, x) || !num(3, y) || !num(4, z)) return fail("rotation <id> rx ry rz (degrees)");
        w.pushUndo();
        w.setTransform(*o, o->pos, glm::dquat(glm::radians(glm::dvec3(x, y, z))));
        res.text = brief(*o) + "  rotation " + v3(eulerDeg(o->rot), 1.0, 1);
    } else if (cmd == "rotate") {
        Object* o = obj(1);
        int ax = t.size() > 2 ? axisIndex(t[2]) : -1;
        double deg;
        if (!o || ax < 0 || !num(3, deg)) return fail("rotate <id> <x|y|z> <degrees>");
        glm::dvec3 axis(0);
        axis[ax] = 1;
        w.pushUndo();
        w.setTransform(*o, o->pos, glm::angleAxis(glm::radians(deg), axis) * o->rot);
        res.text = brief(*o) + "  rotation " + v3(eulerDeg(o->rot), 1.0, 1);
    } else if (cmd == "drop") {
        Object* o = obj(1);
        if (!o) return fail("drop <id>");
        w.pushUndo();
        w.dropToSurface(*o);
        res.text = brief(*o);
    } else if (cmd == "material") {
        Object* o = obj(1);
        int mat = t.size() > 2 ? findMaterial(t[2]) : -1;
        if (!o || mat < 0) return fail("material <id> <" + materialNames() + ">");
        w.pushUndo();
        w.setMaterial(*o, mat);
        res.text = brief(*o);
    } else if (cmd == "name") {
        Object* o = obj(1);
        if (!o || t.size() < 3) return fail("name <id> <text>");
        std::string n;
        for (size_t i = 2; i < t.size(); ++i) n += (i > 2 ? " " : "") + t[i];
        w.pushUndo();
        o->name = n;
        res.text = brief(*o);
    } else if (cmd == "fix") {
        Object* o = obj(1);
        if (!o) return fail("fix <id> [on|off]");
        bool on = t.size() < 3 || yes(t[2]);
        w.pushUndo();
        w.setFixed(*o, on);
        res.text = brief(*o);
    } else if (cmd == "buildcollision") {
        if (t.size() > 1) w.setBuildCollision(yes(t[1]));
        res.text = std::string("build collision ") + (w.buildCollision() ? "on: parts stop at other parts when moved" : "off: parts move through everything");
    // ---------------------------------------------------------------- Verbindungen
    } else if (cmd == "glue" || cmd == "nail" || cmd == "screw" || cmd == "weld" || cmd == "hinge" || cmd == "spring" || cmd == "rope") {
        Object* a = obj(1);
        Object* b = obj(2);
        if (!a || !b) return fail(cmd + " <part_a> <part_b> ...   ('help' shows the parameters)");
        std::string err;
        Joint* j = nullptr;
        w.pushUndo();
        if (cmd == "glue") {
            std::string preset = t.size() > 3 ? t[3] : "";
            double ft = 0, gap = 0;
            num(4, ft);
            num(5, gap);
            j = w.glue(a->id, b->id, preset, ft, gap, &err);
        } else if (cmd == "nail" || cmd == "screw") {
            double n = 0, len = 0, d = 0;
            num(3, n);
            num(4, len);
            num(5, d);
            j = w.nail(a->id, b->id, cmd == "screw", (int)n, len, d, &err);
        } else if (cmd == "weld") {
            double seam = 0;
            num(3, seam);
            j = w.weld(a->id, b->id, seam, &err);
        } else if (cmd == "hinge") {
            int ax = t.size() > 3 ? axisIndex(t[3]) : -1;
            double rpm = 0, tq = 0;
            num(4, rpm);
            num(5, tq);
            j = w.hinge(a->id, b->id, ax, (float)rpm, (float)tq, &err);
        } else if (cmd == "spring") {
            double k = 0, c = 0, mx = 0;
            num(3, k);
            num(4, c);
            num(5, mx);
            j = w.spring(a->id, b->id, k, c, mx, &err);
        } else {
            double len = 0, mx = 0;
            num(3, len);
            num(4, mx);
            j = w.rope(a->id, b->id, len / 100.0, mx, &err);
        }
        if (!j) {
            w.discardUndo();
            return fail(err);
        }
        res.text = "Joined: " + w.jointText(*j, true);
    } else if (cmd == "joints") {
        Object* o = t.size() > 1 ? obj(1) : nullptr;
        int n = 0;
        for (auto& j : w.joints())
            if (!o || j->a == o->id || j->b == o->id) {
                out << w.jointText(*j, false) << "\n";
                ++n;
            }
        out << n << " joint(s), breakage " << (w.breakage() ? "on" : "off") << ", broken so far: " << w.brokenCount();
        res.text = out.str();
    } else if (cmd == "joint") {
        double idv;
        if (!jointNr(1, idv))
            return fail("joint <nr> [strength|shear|gap|count|length|diameter|seam|force|stiffness|damping|motor|unbreakable] [value]");
        Joint* j = w.findJoint((uint32_t)idv);
        if (!j) return fail("there is no joint J" + fmtNum(idv, 0));
        if (t.size() >= 3) {
            std::string p = normalizeWord(t[2]);
            double v = 0, v2 = 0;
            bool has = num(3, v);
            num(4, v2);
            w.pushUndo();
            if (p == "unbreakable") j->unbreakable = t.size() < 4 || yes(t[3]);
            else if (!has) return fail("value missing");
            else if (p == "strength") {
                double ratio = j->ft > 0 ? j->fs / j->ft : 1.0;
                j->ft = v;
                j->fs = v * ratio;
            } else if (p == "shear") j->fs = v;
            else if (p == "gap") j->gapMm = v;
            else if (p == "count") j->count = std::max(1, (int)v);
            else if (p == "length") {
                if (j->type == JointType::Spring || j->type == JointType::Rope) j->restLength = v / 100.0;
                else j->lengthMm = v;
            } else if (p == "diameter") j->diameterMm = v;
            else if (p == "seam") j->seamMm = v;
            else if (p == "force") j->maxForce = v;
            else if (p == "stiffness") j->stiffness = v;
            else if (p == "damping") j->damping = v;
            else if (p == "motor") {
                j->motorRpm = (float)v;
                if (v2 > 0) j->motorTorque = (float)v2;
            } else return fail("unknown setting '" + t[2] + "'");
            w.updateJoint(*j);
        }
        res.text = w.jointText(*j, true);
    } else if (cmd == "detach") {
        if (word(1) == "all") {
            w.pushUndo();
            size_t n = w.joints().size();
            while (!w.joints().empty()) w.removeJoint(w.joints().back()->id);
            res.text = std::to_string(n) + " joints removed";
        } else {
            double idv;
            if (!jointNr(1, idv)) return fail("detach <nr|all>");
            if (!w.findJoint((uint32_t)idv)) return fail("there is no such joint");
            w.pushUndo();
            w.removeJoint((uint32_t)idv);
            res.text = "Joint J" + fmtNum(idv, 0) + " removed";
        }
    } else if (cmd == "unbreakable") {
        Object* o = obj(1);
        if (!o) return fail("unbreakable <id> [on|off]");
        bool on = t.size() < 3 || yes(t[2]);
        w.pushUndo();
        o->unbreakable = on;
        res.text = o->name + (on ? " never breaks" : " can break (when 'breakage on')");
    } else if (cmd == "bores" || cmd == "holes") {
        Object* o = obj(1);
        if (!o) return fail("bores <id>");
        res.text = w.boresText(*o);
    } else if (cmd == "lubricate" || cmd == "lube") {
        Object* o = obj(1);
        const int lub = t.size() > 2 ? findLubricant(t[2].c_str()) : 1;
        if (!o || lub < 0) {
            std::string names;
            for (const auto& l : lubricants()) names += std::string(names.empty() ? "" : " ") + l.name;
            return fail("lubricate <id> [oil|grease|wax|soap|off]   (" + names + ")");
        }
        w.pushUndo();
        w.setLubricant(*o, lub);
        const Lubricant& l = lubricants()[lub];
        res.text = lub == 0 ? o->name + " is dry again (friction of the materials)"
                            : o->name + " lubricated with " + l.name + ": friction at most " + fmtNum(l.muS, 2) + " static / " +
                                  fmtNum(l.muK, 2) + " sliding, wear x" + fmtNum(l.wear, 2);
    // ---------------------------------------------------------------- Wärme und Feuer
    } else if (cmd == "heat" || cmd == "temp" || cmd == "temperature") {
        const std::string sub = word(1);
        double v = 0;
        if (sub.empty()) {
            int hot = 0;
            const Object* hottest = nullptr;
            for (auto& o : w.objects())
                if (!o->heat.empty()) {
                    hot += std::abs(o->heatInfo.tMax - w.ambient()) > 5 || o->heatInfo.tMin < w.ambient() - 5;
                    if (!hottest || o->heatInfo.tMax > hottest->heatInfo.tMax) hottest = o.get();
                }
            double power = 0;
            for (const Fire& f : w.fires()) power += f.power;
            out << "heat          " << (w.heat() ? "on" : "off (temperatures frozen)") << ", air " << fmtNum(w.ambient(), 1) << " °C, time x"
                << fmtNum(w.heatSpeed(), 1) << "\n";
            out << "parts         " << hot << " warmer or colder than the air, " << w.burningParts() << " burning\n";
            out << "fires         " << w.fires().size() << " (" << fmtNum(power / 1000.0, 1) << " kW)";
            for (const Fire& f : w.fires())
                out << "\n  at " << v3(f.base) << " cm: " << fmtNum(f.power / 1000.0, 1) << " kW, flames " << fmtNum(f.height * 100, 0)
                    << " cm high, " << fmtNum(f.tempC, 0) << " °C";
            if (hottest) out << "\nhottest       #" << hottest->id << " " << hottest->name << " " << fmtNum(hottest->heatInfo.tMax, 0) << " °C";
            res.text = out.str();
        } else if (sub == "on" || sub == "off") {
            w.setHeat(sub == "on");
            res.text = std::string("heat ") + (w.heat() ? "on: conduction, cooling, fire and melting are simulated"
                                                         : "off: every part keeps its temperature");
        } else if (sub == "ambient" || sub == "air") {
            if (!num(2, v)) return fail("heat ambient <°C>   (e.g. 20, -20 winter, 35 summer)");
            w.setAmbient(v);
            res.text = "air and floor now " + fmtNum(w.ambient(), 1) + " °C (untouched parts too)";
        } else if (sub == "speed" || sub == "timelapse") {
            if (!num(2, v) || v <= 0) return fail("heat speed <factor>   (1 = real time, up to 100)");
            w.setHeatSpeed(v);
            res.text = "heat runs " + fmtNum(w.heatSpeed(), 1) + "x faster than real time (motion is not affected)";
        } else {
            Object* o = obj(1);
            if (!o) return fail("heat [on|off] | heat ambient <°C> | heat speed <x> | heat <id> [°C]");
            if (word(2) == "map") {  // Zellgitter Schicht für Schicht (Engine: Temperaturverteilung auslesen)
                const HeatGrid& g = o->heat;
                if (g.empty()) {
                    res.text = o->name + ": no heat grid yet (everything at air temperature " + fmtNum(w.ambient(), 0) + " °C)";
                    return res;
                }
                out << o->name << ": " << g.dim.x << " x " << g.dim.y << " x " << g.dim.z << " cells of " << fmtNum(g.cell.x * 100, 1) << " x "
                    << fmtNum(g.cell.y * 100, 1) << " x " << fmtNum(g.cell.z * 100, 1) << " cm (local x right, z down per layer)\n"
                    << "°C per cell, * = burning, ~ = molten, # = charred, . = no material\n";
                for (int y = g.dim.y - 1; y >= 0; --y) {
                    out << "layer y" << y << "\n";
                    for (int z = 0; z < g.dim.z; ++z) {
                        for (int x = 0; x < g.dim.x; ++x) {
                            const HeatCell& c = g.cells[g.index(x, y, z)];
                            std::string s = c.inside ? fmtNum(c.temp, 0) : ".";
                            s += c.burning ? "*" : c.melt > 0.5f ? "~" : c.chr > 0.5f ? "#" : " ";
                            out << std::string(s.size() < 7 ? 7 - s.size() : 0, ' ') << s;
                        }
                        out << "\n";
                    }
                }
                res.text = out.str();
            } else if (t.size() > 2) {
                if (!num(2, v)) return fail("heat <id> <°C> | heat <id> map");
                w.pushUndo();
                w.setTemperature(*o, v);
                res.text = o->name + " is now " + fmtNum(v, 0) + " °C throughout\n" + w.heatText(*o);
            } else {
                res.text = "#" + std::to_string(o->id) + " " + o->name + "\n" + w.heatText(*o);
            }
        }
    } else if (cmd == "ignite" || cmd == "fire" || cmd == "light") {
        Object* o = obj(1);
        if (!o) return fail("ignite <id> [face]   (default: lowest spot)");
        int k = 1, s = -1;
        glm::dvec3 p;
        const bool at = t.size() > 2 && parseFace(t[2], k, s);
        if (t.size() > 2 && !at) return fail("ignite <id> [face]   faces: top bottom left right front back, +x ...");
        if (at) p = faceCenter(*o, k, s);
        std::string err;
        if (!w.ignite(*o, at ? &p : nullptr, &err)) return fail(err);
        res.text = o->name + " is alight (" + materials()[o->material].name + " ignites at " + fmtNum(thermal(o->material).ignitionC, 0) +
                   " °C) - it chars, then burns and spreads";
    } else if (cmd == "extinguish" || cmd == "douse") {
        Object* o = word(1) == "all" || t.size() < 2 ? nullptr : obj(1);
        if (t.size() > 1 && word(1) != "all" && !o) return fail("extinguish <id|all>");
        const int n = w.extinguish(o);
        res.text = "water: " + std::to_string(n) + " burning spot(s) put out" + (o ? " on " + o->name : std::string());
    } else if (cmd == "torch") {
        Object* o = obj(1);
        const int tool = t.size() > 2 ? findHeatTool(word(2).c_str()) : 1;
        double secs = 5;
        if (t.size() > 3 && !num(3, secs)) return fail("torch <id> [tool] [seconds] [face]");
        int k = 1, s = 1;
        if (t.size() > 4 && !parseFace(t[4], k, s)) return fail("torch <id> [tool] [seconds] [face]");
        if (!o || tool < 0) {
            std::string names;
            for (const auto& h : heatTools()) names += std::string(names.empty() ? "" : " ") + h.name;
            return fail("torch <id> [tool] [seconds] [face]   tools: " + names);
        }
        w.addHeatSource(o->id, faceCenter(*o, k, s), tool, std::clamp(secs, 0.0, 3600.0));
        const HeatTool& h = heatTools()[tool];
        res.text = std::string(h.name) + " (" + fmtNum(h.tempC, 0) + " °C) on the " + faceName(k, s) + " face of " + o->name + " for " +
                   fmtNum(secs, 1) + " s" + (w.heat() ? "" : " (heat is off)");
    } else if (cmd == "breakage") {
        if (t.size() > 1) w.setBreakage(yes(t[1]));
        res.text = std::string("breakage ") + (w.breakage() ? "on: joints and parts break when overloaded (realistic)"
                                                            : "off: everything holds (engine/editor mode)");
    // ---------------------------------------------------------------- Physik, Welt
    } else if (cmd == "air" || cmd == "wind") {
        std::string sub = word(1);
        double rho = w.airDensity();
        glm::dvec3 wind = w.wind();
        double x, y, z;
        if (cmd == "wind") {
            if (!num(1, x) || !num(2, y) || !num(3, z)) return fail("wind x y z (m/s)");
            wind = glm::dvec3(x, y, z);
        } else if (sub == "density") {
            if (!num(2, rho) || rho < 0) return fail("air density <kg/m3>  (air 1.204, vacuum 0)");
        } else if (sub == "vacuum") {
            rho = 0;
        } else if (sub == "normal") {
            rho = 1.204;
            wind = glm::dvec3(0);
        } else if (sub == "wind") {
            if (!num(2, x) || !num(3, y) || !num(4, z)) return fail("air wind x y z (m/s)");
            wind = glm::dvec3(x, y, z);
        } else if (!sub.empty()) {
            return fail("air [density <kg/m3> | vacuum | normal | wind x y z]");
        }
        w.setAir(rho, wind);
        res.text = "air: density " + fmtNum(w.airDensity(), 3) + " kg/m3" + (w.airDensity() == 0 ? " (vacuum)" : "") + ", wind " +
                   fmtNum(w.wind().x, 2) + " " + fmtNum(w.wind().y, 2) + " " + fmtNum(w.wind().z, 2) + " m/s (" +
                   fmtNum(glm::length(w.wind()), 2) + " m/s = " + fmtNum(glm::length(w.wind()) * 3.6, 1) + " km/h)";
    } else if (cmd == "plan" || cmd == "blueprint" || cmd == "bauplan" || cmd == "vorlage") {
        // Bauvorlagen (docs/20-Bauvorlagen.md): Hologramme setzen, mit Teilen füllen, abbauen, eigene Vorlagen speichern
        const std::string sub = word(1);
        auto planText = [&](const BuildPlan& p) {
            int built = 0;
            for (const PlanPiece& c : p.pieces) built += c.built ? 1 : 0;
            return "plan " + std::to_string(p.id) + ": " + p.type + " at " + v3(p.pos) + " cm, " + fmtNum(glm::degrees(p.yaw), 0) + " deg, " +
                   (p.fixed ? "fixed" : "physics") + ", " + std::to_string(built) + "/" + std::to_string(p.pieces.size()) + " built";
        };
        if (sub.empty() || sub == "list") {
            out << "templates:";
            for (const PlanTemplate& t : planTemplates())
                out << "\n  " << t.id << "  " << t.label << " (" << t.pieces.size() << " pieces" << (t.custom ? ", own" : "") << ")";
            out << "\nplaced: " << w.plans().size();
            for (const BuildPlan& p : w.plans()) out << "\n  " << planText(p);
            res.text = out.str();
        } else if (sub == "place" || sub == "set") {
            double x, y, z, yaw = 0;
            if (t.size() < 3 || !num(3, x) || !num(4, y) || !num(5, z))
                return fail("plan place <template> <x y z cm> [yaw_deg] [fixed|physics]");
            num(6, yaw);
            const bool phys = word(6) == "physics" || word(7) == "physics" || word(6) == "physik" || word(7) == "physik";
            w.pushUndo();
            std::string err;
            BuildPlan* p = w.addPlan(t[2], glm::dvec3(x, y, z) / 100.0, glm::radians(yaw), !phys, &err);
            if (!p) {
                w.dropUndo();
                return fail(err);
            }
            res.text = planText(*p);
        } else if (sub == "info") {
            double id;
            const BuildPlan* p = num(2, id) ? w.findPlan((uint32_t)id) : nullptr;
            if (!p) return fail("plan info <plan>");
            out << planText(*p);
            for (size_t i = 0; i < p->pieces.size(); ++i) {
                const PlanPiece& c = p->pieces[i];
                out << "\n  " << i + 1 << ": " << planMatLabel((PlanMat)c.mat) << " " << fmtNum(c.half.x * 200, 0) << " x " << fmtNum(c.half.y * 200, 0)
                    << " x " << fmtNum(c.half.z * 200, 0) << " cm, ";
                if (c.built) out << "built (part " << c.built << ")";
                else out << fmtNum(c.filled * 1000, 1) << " of " << fmtNum(c.volume() * 1000, 1) << " dm3";
            }
            res.text = out.str();
        } else if (sub == "fill") {
            // plan fill <plan> <piece|auto> <part>
            double id, pi = 0;
            BuildPlan* p = num(2, id) ? w.findPlan((uint32_t)id) : nullptr;
            Object* o = obj(4);
            if (!p || !o || (!num(3, pi) && word(3) != "auto")) return fail("plan fill <plan> <piece|auto> <part>");
            int piece = (int)pi - 1;
            if (word(3) == "auto")
                for (size_t i = 0; i < p->pieces.size() && piece < 0; ++i)
                    if (!p->pieces[i].built) piece = (int)i;
            w.pushUndo();
            std::string msg;
            if (!w.fillPlan(p->id, piece, o->id, msg)) {
                w.dropUndo();
                return fail(msg);
            }
            res.text = msg;
        } else if (sub == "dismantle" || sub == "abbauen") {
            Object* o = obj(2);
            if (!o) return fail("plan dismantle <part>");
            w.pushUndo();
            std::string err;
            if (!w.dismantle(o->id, &err)) {
                w.dropUndo();
                return fail(err);
            }
            res.text = "dismantled: " + o->name + " is a loose part again";
        } else if (sub == "remove" || sub == "delete" && t.size() > 2 && std::isdigit((unsigned char)t[2][0])) {
            double id;
            if (!num(2, id)) return fail("plan remove <plan>");
            w.pushUndo();
            if (!w.removePlan((uint32_t)id)) {
                w.dropUndo();
                return fail("no plan " + t[2]);
            }
            res.text = "plan " + t[2] + " removed (built pieces stay)";
        } else if (sub == "save") {
            // eigene Vorlage aus den ausgewählten Teilen: plan save <name> [label ...]
            if (t.size() < 3) return fail("plan save <name> [label]   (select the parts first)");
            PlanTemplate tp;
            if (!templateFromParts(w, host.selectedIds(), tp)) return fail("select the parts first (build mode)");
            tp.id = normalizeWord(t[2]);
            tp.label = t[2];
            for (size_t i = 3; i < t.size(); ++i) tp.label = (i == 3 ? "" : tp.label + " ") + t[i];
            std::string info;
            if (!savePlanTemplate(tp, &info)) return fail(info);
            res.text = "template '" + tp.id + "' saved with " + std::to_string(tp.pieces.size()) + " pieces: " + info;
        } else if (sub == "delete" || sub == "forget") {
            std::string err;
            if (!deletePlanTemplate(word(2), &err)) return fail(err);
            res.text = "template '" + word(2) + "' deleted";
        } else {
            return fail("plan [list] | place <template> <x y z> [yaw] [fixed|physics] | info <plan> | fill <plan> <piece|auto> <part> | "
                        "dismantle <part> | remove <plan> | save <name> [label] | delete <name>");
        }
    } else if (cmd == "chop" || cmd == "axe") {
        // Axt: chop <id> <x y z> [dx dz]  Stelle des Hiebs (cm), Richtung zum Schlagenden (ohne: zur Kamera)
        Object* o = obj(1);
        double x, y, z, dx = 0, dz = 0;
        if (!o || !num(2, x) || !num(3, y) || !num(4, z)) return fail("chop <id> <x y z> [dx dz]   (cm: where the axe hits; direction back to you)");
        const glm::dvec3 p = glm::dvec3(x, y, z) / 100.0;
        glm::dvec3 toward = host.spawnReference() - p;
        if (num(5, dx) && num(6, dz)) toward = glm::dvec3(dx, 0, dz);
        w.pushUndo();
        double removed = 0;
        std::string err;
        const size_t before = w.objects().size();
        if (!w.chop(o->id, p, toward, &removed, &err)) {
            w.dropUndo();
            return fail(err);
        }
        res.text = "chop: " + fmtNum(removed * 1e6, 0) + " cm3 of " + materials()[(size_t)o->material].name + " chipped off" +
                   (w.objects().size() > before ? " - it is through, the part falls" : "");
    } else if (cmd == "trees" || cmd == "tree") {
        // Bäume: Arten, Wald des Generators, pflanzen, nächster Baum. Positionen in cm
        const std::string sub = word(1);
        const TreeLibrary& lib = w.trees();
        auto typeOf = [&](const std::string& s) {
            for (int k = 0; k < lib.types(); ++k)
                if (normalizeWord(lib.config().types[(size_t)k].id) == s || normalizeWord(lib.config().types[(size_t)k].label) == s) return k;
            return -1;
        };
        std::string names;
        for (const TreeType& tt : lib.config().types) names += (names.empty() ? "" : " ") + tt.id;
        if (sub.empty() || sub == "status" || sub == "types") {
            std::ostringstream o;
            o << lib.types() << " tree type(s):\n";
            for (int k = 0; k < lib.types(); ++k) {
                const TreeType& tt = lib.config().types[(size_t)k];
                const TreeModel& m = lib.model(k, 0);
                const int mat = findMaterialIndex(tt.wood.c_str());
                const double dens = mat >= 0 ? materials()[(size_t)mat].density : 700.0;
                o << "  " << tt.id << " (" << tt.label << "): " << fmtNum(m.height, 1) << " m tall, trunk " << fmtNum(tt.diameter * 100, 0)
                  << " cm, " << tt.wood << " " << fmtNum(m.volume, 2) << " m3 = " << fmtNum(m.volume * dens, 0) << " kg, "
                  << (tt.cone ? "cone" : "round") << " crown, " << lib.variants(k) << " forms\n";
            }
            size_t parts = 0, generated = 0, changed = 0;
            for (const auto& op : w.objects()) {
                if (op->treeType.empty()) continue;
                ++parts;
                if (op->treeKey) (w.untouchedTree(*op) ? generated : changed)++;
            }
            o << "tree parts    " << parts << " (" << generated << " standing from the generator, " << changed << " cut or changed)\n";
            if (Forest* f = w.forest())
                o << "forest        " << (f->any() ? "on" : "no biome has trees") << ", " << f->cachedTiles() << " tiles of 64 m known, "
                  << w.treesTaken().size() << " tree(s) removed, real parts within " << fmtNum(lib.config().solidDistance, 0) << " m, drawn up to "
                  << fmtNum(lib.config().drawDistance, 0) << " m";
            else
                o << "forest        none (flat world; 'scene trees' shows all types, 'tree plant <type>' plants one)";
            res.text = o.str();
        } else if (sub == "plant") {
            const int k = typeOf(word(2));
            if (k < 0) return fail("tree plant <type> [form] [x z] [scale]   (types: " + names + ")");
            double v = 0, x = 0, z = 0, sc = 1;
            num(3, v);
            glm::dvec3 p = host.spawnReference();
            if (num(4, x) && num(5, z)) p = glm::dvec3(x / 100.0, 0, z / 100.0);
            num(6, sc);
            w.pushUndo();
            Object* o = w.plantTree(k, std::clamp((int)v, 0, lib.variants(k) - 1), std::clamp(sc, 0.2, 5.0),
                                    glm::dvec3(p.x, w.groundHeight(p.x, p.z), p.z), 0.0);
            if (!o) return fail("could not plant the tree");
            host.select(o->id);
            res.text = "Planted: #" + std::to_string(o->id) + " " + o->name + ", " + fmtNum(o->mass, 0) + " kg, " +
                       fmtNum(lib.model(k, 0).height * sc, 1) + " m tall (fixed by its root; saw through the trunk to fell it)";
        } else if (sub == "nearest" || sub == "find") {
            Forest* f = w.forest();
            if (!f) return fail("this world has no forest (flat world)");
            const int k = word(2).empty() ? -1 : typeOf(word(2));
            if (!word(2).empty() && k < 0) return fail("tree nearest [type]   (types: " + names + ")");
            const glm::dvec3 c = host.spawnReference();
            TreeSpot s;
            if (!f->nearest(glm::dvec2(c.x, c.z), 5000.0, k, s)) return fail("no tree within 5 km");
            res.text = lib.config().types[(size_t)s.type].id + " at " + fmtNum(s.pos.x * 100, 0) + " " + fmtNum(s.pos.y * 100, 0) + " " +
                       fmtNum(s.pos.z * 100, 0) + " cm, " + fmtNum(glm::length(glm::dvec2(s.pos.x - c.x, s.pos.z - c.z)), 0) + " m away, size " +
                       fmtNum(s.scale, 2) + ", form " + std::to_string(s.variant) + (w.treeHidden(s.key) ? " (a part now, or removed)" : "");
        } else {
            return fail("trees | tree plant <type> [form] [x z] [scale] | tree nearest [type] | scene trees   (types: " + names + ")");
        }
    } else if (cmd == "terrain") {
        // Gelände (Welt "normal"): Zustand, was an einer Stelle ist, Biome, ändern. Positionen in cm
        Terrain* tr = w.terrain();
        const std::string sub = word(1);
        if (!tr) return fail("this world is flat (no terrain). Create a world of type 'normal' in the main menu");
        const WorldGen& g = tr->gen();
        const GenConfig& cfg = g.config();
        auto pct = [](double v) { return fmtNum(v, 2); };
        if (sub.empty() || sub == "status") {
            res.text = tr->statusText();
        } else if (sub == "at" || sub == "here") {
            glm::dvec3 p = host.spawnReference();
            double x = 0, z = 0;
            if (num(2, x) && num(3, z)) p = glm::dvec3(x, 0, z) / 100.0;
            const Climate c = g.climate(p.x, p.z);
            GenSample s;
            g.sample(p.x, p.z, s);
            glm::dvec3 n;
            const double h = tr->surfaceHeight(p.x, p.z, &n);
            std::ostringstream o;
            o << "at            " << fmtNum(p.x * 100, 0) << " " << fmtNum(p.z * 100, 0) << " cm\n";
            o << "height        " << fmtNum(h * 100, 1) << " cm (generator " << fmtNum(s.height * 100, 1) << " cm), slope "
              << fmtNum(std::acos(std::clamp(n.y, -1.0, 1.0)) * 180.0 / 3.14159265358979, 1) << "°\n";
            o << "biome         " << cfg.biomes[(size_t)s.biome].id << " (" << cfg.biomes[(size_t)s.biome].label << ")\n";
            o << "climate      ";
            for (int k = 0; k < kClimateCount; ++k) o << " " << kClimateNames[k] << " " << pct(c.v[k]);
            o << "\nscores       ";
            for (size_t b = 0; b < cfg.biomes.size(); ++b) o << " " << cfg.biomes[b].id << " " << fmtNum(s.score[b], 3);
            if (cfg.rivers.enabled) {  // Fluss: wie weit, wie breit, Wasser hier
                const RiverSample& r = s.river;
                const double water = cfg.rivers.level - tr->waterBottom(p.x, p.z);
                o << "\nriver        ";
                if (r.strength <= 0)
                    o << " none here (the biomes allow " << fmtNum(g.riverFactor(s.score) * 100, 0) << " % of the rivers)";
                else
                    o << " " << fmtNum(r.dist, 1) << " m from the middle of a river " << fmtNum(2 * r.half, 1) << " m wide, "
                      << fmtNum(r.depth * 100, 0) << " cm deep, current " << fmtNum(glm::length(r.flow), 2) << " m/s"
                      << (r.strength < 0.99 ? " (fading out: " + fmtNum(r.strength * 100, 0) + " %)" : "");
                if (water > 0) o << "; water here " << fmtNum(water * 100, 0) << " cm deep";
            }
            // die vier Rasterpunkte darum (0,5 m): so rechnen Physik und Bild
            const int64_t i = (int64_t)std::floor(p.x / kTerrainCell), j = (int64_t)std::floor(p.z / kTerrainCell);
            o << "\nlattice      ";
            for (int k = 0; k < 4; ++k) {
                const int64_t a = i + (k & 1), b = j + (k >> 1);
                const double h = tr->latticeHeight(a, b), f = tr->latticeHeightFresh(a, b);
                o << " (" << a << "," << b << ") " << fmtNum(h * 100, 1) << (std::abs(h - f) > 1e-4 ? " [fresh " + fmtNum(f * 100, 1) + "!]" : "");
            }
            res.text = o.str();
        } else if (sub == "biomes") {
            std::ostringstream o;
            o << cfg.biomes.size() << " biome(s)" << (g.singleBiome() >= 0 ? ", this world uses only " + cfg.biomes[(size_t)g.singleBiome()].id : "") << "\n";
            for (const GenConfig::Biome& b : cfg.biomes) {
                o << "  " << b.id << " (" << b.label << "):";
                for (int k = 0; k < kClimateCount; ++k)
                    if (b.lo[k] > 0 || b.hi[k] < 1) o << " " << kClimateNames[k] << " " << pct(b.lo[k]) << "-" << pct(b.hi[k]);
                o << ", height " << fmtNum(b.height.base, 1) << " m +- " << fmtNum(b.height.amplitude, 1) << " m (scale " << fmtNum(b.height.scale, 0)
                  << " m), material " << b.material << (cfg.rivers.enabled ? ", rivers " + fmtNum(b.rivers * 100, 0) + " %" : "") << "\n";
            }
            if (cfg.rivers.enabled)
                o << "rivers: level " << fmtNum(cfg.rivers.level * 100, 0) << " cm, lines every ~" << fmtNum(cfg.rivers.spacing, 0) << " m, "
                  << fmtNum(cfg.rivers.presence * 100, 0) << " % with water, " << fmtNum(cfg.rivers.widthMin, 0) << "-" << fmtNum(cfg.rivers.widthMax, 0)
                  << " m wide, " << fmtNum(cfg.rivers.depthMin, 1) << "-" << fmtNum(cfg.rivers.depthMax, 1) << " m deep\n";
            else
                o << "rivers: none (this world was made before rivers, or 'rivers' is missing in its generator.json)\n";
            o << "climate scales:";
            for (int k = 0; k < kClimateCount; ++k) o << " " << kClimateNames[k] << " " << fmtNum(cfg.climate[k].scale, 0) << " m";
            o << ", border blend " << pct(cfg.blend);
            res.text = o.str();
        } else if (sub == "raise" || sub == "lower" || sub == "flatten") {
            double x = 0, z = 0, r = 0, a = 0;
            const bool flat = sub == "flatten";
            if (!num(2, x) || !num(3, z) || !num(4, r) || r <= 0 || (!flat && !num(5, a)))
                return fail("terrain raise|lower <x> <z> <radius> <cm> | terrain flatten <x> <z> <radius> [height]   (all in cm)");
            x /= 100.0, z /= 100.0, r /= 100.0, a /= 100.0;
            if (r > 200.0) return fail("radius at most 20000 cm (200 m)");
            const double before = tr->surfaceHeight(x, z);
            if (flat) tr->flatten(x, z, r, num(5, a) ? a / 100.0 : before);
            else tr->raise(x, z, r, sub == "lower" ? -std::abs(a) : a);
            res.text = "terrain changed: height at the center " + fmtNum(before * 100, 1) + " -> " + fmtNum(tr->surfaceHeight(x, z) * 100, 1) +
                       " cm (" + std::to_string(tr->editedChunks()) + " chunk(s) changed in total; saved with the world)";
        } else if (sub == "map") {
            // Karte von oben um die Kamera: Farbe der Oberfläche, Höhe als Schatten, Flüsse blau, Raster je km, Mitte rot.
            // Nur der Generator (ohne Änderungen); dünne Flüsse werden mindestens ein Pixel breit gezeichnet
            if (t.size() < 3) return fail("terrain map <file.png> [size_m=8000] [pixels=512]   (x to the right, z down, here in the middle)");
            double size = 8000, pixels = 512;
            if (t.size() > 3 && !num(3, size)) return fail("terrain map <file.png> [size_m] [pixels]");
            if (t.size() > 4 && !num(4, pixels)) return fail("terrain map <file.png> [size_m] [pixels]");
            const int P = std::clamp((int)pixels, 64, 2048);
            size = std::clamp(size, 100.0, 200000.0);
            const glm::dvec3 c = host.spawnReference();
            const double cell = size / P;
            const size_t n = (size_t)P * P;
            std::vector<float> hgt(n);
            std::vector<glm::vec3> col(n);
            std::vector<uint8_t> wet(n), biome(n);
            jobs::parallelFor((size_t)P, [&](size_t row) {
                for (int x = 0; x < P; ++x) {
                    GenSample s;
                    g.sample(c.x + (x + 0.5 - P / 2.0) * cell, c.z + (double(row) + 0.5 - P / 2.0) * cell, s);
                    const size_t i = row * (size_t)P + (size_t)x;
                    hgt[i] = (float)s.height;
                    col[i] = s.color;
                    biome[i] = (uint8_t)s.biome;
                    const RiverSample& r = s.river;
                    wet[i] = r.strength > 0.3 && r.dist < std::max(r.half * 0.9, 0.6 * cell);
                }
            });
            std::vector<uint8_t> rgb(n * 3);
            std::vector<size_t> biomeCount(cfg.biomes.size(), 0);
            size_t wetCount = 0;
            const int grid = (int)std::round(1000.0 / cell);  // Linien je km
            for (int z = 0; z < P; ++z)
                for (int x = 0; x < P; ++x) {
                    const size_t i = (size_t)z * P + x;
                    auto h = [&](int a, int b) { return hgt[(size_t)std::clamp(b, 0, P - 1) * P + std::clamp(a, 0, P - 1)]; };
                    const double dx = (h(x + 1, z) - h(x - 1, z)) / (2 * cell), dz = (h(x, z + 1) - h(x, z - 1)) / (2 * cell);
                    const double shade = std::clamp(1.0 - 1.5 * (dx + dz), 0.45, 1.5);  // Licht von links oben
                    glm::dvec3 v = glm::pow(glm::dvec3(col[i]), glm::dvec3(1 / 2.2)) * 1.25 * shade;
                    if (wet[i]) v = glm::dvec3(0.15, 0.40, 0.85);
                    const int cx = x - P / 2, cz = z - P / 2;
                    if (grid >= 4 && (((cx % grid) + grid) % grid == 0 || ((cz % grid) + grid) % grid == 0)) v *= 0.8;
                    if ((std::abs(cx) <= 6 && std::abs(cz) <= 1) || (std::abs(cz) <= 6 && std::abs(cx) <= 1)) v = glm::dvec3(1, 0.1, 0.1);
                    for (int k = 0; k < 3; ++k) rgb[i * 3 + k] = (uint8_t)std::clamp(v[k] * 255.0, 0.0, 255.0);
                    ++biomeCount[biome[i]];
                    wetCount += wet[i];
                }
            if (!writePng(t[2], (uint32_t)P, (uint32_t)P, rgb)) return fail("could not write " + t[2]);
            std::ostringstream o;
            o << "map " << t[2] << ": " << fmtNum(size, 0) << " m around " << fmtNum(c.x * 100, 0) << " " << fmtNum(c.z * 100, 0) << " cm, "
              << fmtNum(cell, 1) << " m per pixel (x right, z down, grid 1 km)\n";
            for (size_t b = 0; b < cfg.biomes.size(); ++b)
                o << "  " << cfg.biomes[b].id << " " << fmtNum(100.0 * biomeCount[b] / n, 1) << " %";
            o << "\n  river water (drawn) " << fmtNum(100.0 * wetCount / n, 2) << " % of the area";
            res.text = o.str();
        } else if (sub == "reset") {
            const size_t n = tr->editedChunks();
            tr->clearEdits();
            res.text = std::to_string(n) + " changed chunk(s) reset to the generator";
        } else if (sub == "find") {
            // nächste Stelle (von hier aus), an der das Biom ganz es selbst ist (Terrain::findBiome)
            const int b = cfg.findBiome(word(2));
            if (b < 0) return fail("terrain find <biome>   (biomes: 'terrain biomes')");
            const glm::dvec3 c = host.spawnReference();
            glm::dvec2 f;
            if (!tr->findBiome(b, glm::dvec2(c.x, c.z), 20000.0, f)) return fail("no " + cfg.biomes[(size_t)b].id + " within 20 km");
            const double fx = f.x, fz = f.y;
            res.text = cfg.biomes[(size_t)b].id + " at " + fmtNum(fx * 100, 0) + " " + fmtNum(fz * 100, 0) + " cm, " +
                       fmtNum(glm::length(glm::dvec2(fx - c.x, fz - c.z)), 0) + " m away ('camera " + fmtNum(fx * 100, 0) + " " +
                       fmtNum(tr->surfaceHeight(fx, fz) * 100, 0) + " " + fmtNum(fz * 100, 0) + "' looks there)";
        } else {
            return fail("terrain | terrain at [x z] | terrain biomes | terrain find <biome> | terrain raise|lower <x> <z> <r> <cm> | "
                        "terrain flatten <x> <z> <r> [h] | terrain reset | terrain reload | terrain borders on|off | terrain map <file.png>");
        }
    } else if (cmd == "water") {
        const std::string sub = word(1);
        double x = 0, y = 0, z = 0, v = 0;
        // Gewässer per Nummer ("all" = alle)
        auto bodies = [&](size_t i) {
            std::vector<WaterBody*> r;
            if (word(i) == "all")
                for (const WaterBody& b : w.waters()) {
                    if (!b.generated()) r.push_back(w.findWater(b.id));  // die Flüsse gehören zum Gelände
                }
            else if (num(i, v))
                if (WaterBody* b = w.findWater((uint32_t)v); b && !b->generated()) r.push_back(b);
            return r;
        };
        if (sub.empty()) {
            res.text = w.waterText();
        } else if (sub == "pool") {
            // water pool <L> <W> <depth> [x y z] [liquid] [walls]: Wasser L x W, Boden in Höhe y (ohne Angabe: hier auf dem Boden)
            double L = 0, W = 0, D = 0;
            if (!num(2, L) || !num(3, W) || !num(4, D) || L <= 0 || W <= 0 || D <= 0)
                return fail("water pool <L> <W> <depth> [x y z] [liquid] [walls]   (cm; x y z = middle of the bottom)");
            glm::dvec3 c = host.spawnReference();
            size_t next = 5;
            if (num(5, x) && num(6, y) && num(7, z)) {
                c = glm::dvec3(x, y, z) / 100.0;
                next = 8;
            }
            WaterBody b;
            bool walls = false;
            for (size_t i = next; i < t.size(); ++i) {
                if (word(i) == "walls") walls = true;
                else if (findLiquid(word(i).c_str()) >= 0) b.liquid = findLiquid(word(i).c_str());
                else return fail("unknown liquid or option '" + t[i] + "' (" + std::string(liquids()[0].name) + ", seawater, muddy, oil, walls)");
            }
            b.lo = c - glm::dvec3(L / 200.0, 0, W / 200.0);
            b.hi = c + glm::dvec3(L / 200.0, D / 100.0, W / 200.0);
            w.pushUndo();
            if (walls) {  // Beckenrand aus Beton, 10 cm dick, 5 cm höher als das Wasser
                const double th = 0.10, H = D / 100.0 + 0.05, l = L / 100.0, wd = W / 100.0;
                const glm::dvec3 sizes[4] = {{l + 2 * th, H, th}, {l + 2 * th, H, th}, {th, H, wd}, {th, H, wd}};
                const glm::dvec3 offs[4] = {{0, H / 2, -(wd + th) / 2}, {0, H / 2, (wd + th) / 2}, {-(l + th) / 2, H / 2, 0}, {(l + th) / 2, H / 2, 0}};
                for (int i = 0; i < 4; ++i)
                    if (Object* o = w.addBox(materialIndex("concrete"), sizes[i], c + offs[i], "Pool wall")) w.setFixed(*o, true);
            }
            const uint32_t id = w.addWater(b);
            res.text = "water: " + w.waterText(*w.findWater(id));
        } else if (sub == "sea") {
            if (word(2) == "off") {
                int n = 0;
                std::vector<uint32_t> ids;
                for (const WaterBody& b : w.waters())
                    if (b.sea && !b.generated()) ids.push_back(b.id);
                if (!ids.empty()) w.pushUndo();
                for (uint32_t id : ids) n += w.removeWater(id);
                res.text = n ? "the sea is gone" : "there is no sea";
                return res;
            }
            const char* usage = "water sea <level_cm> [liquid] [bottom <cm>] | water sea off   (endless, water between the bottom and the level)";
            if (!num(2, v)) return fail(usage);
            int l = -1;
            double bottom = 0;
            bool haveBottom = false;
            for (size_t i = 3; i < t.size(); ++i) {
                if (word(i) == "bottom" || word(i) == "floor") {
                    if (!num(i + 1, bottom)) return fail(usage);
                    haveBottom = true;
                    ++i;
                } else if ((l = findLiquid(word(i).c_str())) < 0) {
                    return fail("unknown liquid or option '" + t[i] + "' (water, seawater, muddy, oil, bottom <cm>)");
                }
            }
            WaterBody* sea = nullptr;
            for (const WaterBody& b : w.waters())
                if (b.sea && !b.generated()) sea = w.findWater(b.id);
            w.pushUndo();
            if (!sea) {
                WaterBody b;
                b.sea = true;
                b.liquid = l >= 0 ? l : findLiquid("seawater");
                b.hi.y = v / 100.0;
                sea = w.findWater(w.addWater(b));
            }
            sea->hi.y = v / 100.0;
            if (l >= 0) sea->liquid = l;
            if (haveBottom) {  // Standardgrund (wo nichts anderes eingetragen ist)
                auto m = sea->ground ? sea->ground->edit() : std::make_shared<GroundMap>();
                m->base = bottom / 100.0;
                m->updateBounds();
                if (!sea->ground) m->rev = newGroundRev();
                sea->ground = m;
            }
            w.changedWater(*sea);
            res.text = "water: " + w.waterText(*sea);
        } else if (sub == "ground" || sub == "land") {
            // Grund des Meers in einem Rechteck: Land (über dem Spiegel), Bucht, Graben. Später kommt er aus dem Gelände
            const char* usage = sub == "land" ? "water land <x> <z> <width> <length> [height_cm]   (cm, dry land in the sea; default 50 cm above the level)"
                                              : "water ground <x> <z> <width> <length> <height_cm> | water ground reset   (cm, bottom of the sea)";
            WaterBody* sea = nullptr;
            for (const WaterBody& b : w.waters())
                if (b.sea && !b.generated()) sea = w.findWater(b.id);
            if (!sea) return fail("there is no sea ('water sea <level>')");
            auto m = sea->ground ? sea->ground->edit() : std::make_shared<GroundMap>();
            if (word(2) == "reset" || word(2) == "clear") {
                m->clearTiles();
            } else {
                double cx, cz, wd, ln, hgt = sea->level() * 100.0 + 50.0;
                if (!num(2, cx) || !num(3, cz) || !num(4, wd) || !num(5, ln) || wd <= 0 || ln <= 0) return fail(usage);
                if ((sub == "ground" || t.size() > 6) && !num(6, hgt)) return fail(usage);
                if (wd * ln > 2.5e9) return fail("at most 500 x 500 m at once");
                m->set((cx - wd / 2) / 100.0, (cz - ln / 2) / 100.0, (cx + wd / 2) / 100.0, (cz + ln / 2) / 100.0, hgt / 100.0);
            }
            w.pushUndo();
            sea->ground = m;
            w.changedWater(*sea);
            res.text = "water: " + w.waterText(*sea);
        } else if (sub == "remove" || sub == "delete") {
            auto list = bodies(2);
            if (list.empty()) return fail("water remove <n|all>   ('water' lists them)");
            w.pushUndo();
            std::vector<uint32_t> ids;
            for (WaterBody* b : list) ids.push_back(b->id);
            for (uint32_t id : ids) w.removeWater(id);
            res.text = std::to_string(ids.size()) + " water bod" + (ids.size() == 1 ? "y" : "ies") + " removed";
        } else if (sub == "level" || sub == "waves" || sub == "temp" || sub == "liquid" || sub == "flow" || sub == "current") {
            auto list = bodies(2);
            if (list.empty()) return fail("water " + sub + " <n|all> ...   ('water' lists them)");
            const int l = findLiquid(word(3).c_str());
            if (sub == "level" && !num(3, v)) return fail("water level <n|all> <height_cm>");
            if (sub == "waves" && (!num(3, v) || v < 0)) return fail("water waves <n|all> <height_cm>   (0 = calm, 30 = choppy, 150 = storm)");
            if (sub == "temp" && word(3) != "air" && !num(3, v)) return fail("water temp <n|all> <°C|air>");
            if (sub == "liquid" && l < 0) return fail("water liquid <n|all> <water|seawater|muddy|oil>");
            if ((sub == "flow" || sub == "current") && (!num(3, x) || !num(4, z))) return fail("water flow <n|all> <x> <z>   (m/s, e.g. a river)");
            w.pushUndo();
            for (WaterBody* b : list) {
                if (sub == "level") {
                    b->hi.y = std::max(v / 100.0, b->sea ? -1e9 : b->lo.y + 0.001);
                } else if (sub == "waves") {
                    b->waves = std::min(v / 100.0, 10.0);
                } else if (sub == "temp") {
                    b->ownTemp = word(3) != "air";
                    if (b->ownTemp) b->tempC = std::clamp(v, -2.0, 99.0);
                } else if (sub == "liquid") {
                    b->liquid = l;
                } else {
                    b->flow = glm::dvec3(x, 0, z);
                }
                w.changedWater(*b);
                out << "water: " << w.waterText(*b) << "\n";
            }
            res.text = out.str();
        } else if (sub == "fill" || sub == "empty") {
            Object* o = obj(2);
            if (!o) return fail("water fill <id> [percent] | water empty <id>   (fill the hollows of a cup or boat)");
            double pct = sub == "empty" ? 0 : 100;
            if (sub == "fill" && t.size() > 3 && !num(3, pct)) return fail("water fill <id> [percent]");
            std::string err;
            if (!w.fillWater(*o, pct / 100.0, &err)) return fail(err);
            res.text = w.waterTextOf(*o);
        } else if (Object* o = obj(1)) {
            res.text = w.waterTextOf(*o);
        } else {
            return fail("water | water pool <L> <W> <depth> [x y z] [liquid] [walls] | water sea <level>|off | water <id> ...  ('help')");
        }
    } else if (cmd == "collision") {
        Object* o = obj(1);
        std::string m = word(2);
        int mode = m == "auto" ? 0 : m == "exact" ? 1 : m == "hull" ? 2 : -1;
        if (!o || mode < 0) return fail("collision <id> auto|exact|hull");
        w.pushUndo();
        w.setCollisionMode(*o, mode);
        res.text = brief(*o) + "\ncollision: " +
                   (o->prim && !o->edits ? std::string("exact primitive") : std::to_string(o->coll.hulls.size()) + " hull(s)");
    } else if (cmd == "axle") {
        Object* o = obj(1);
        if (!o || t.size() < 3) return fail("axle <id> <x|y|z|off> [rpm] [Nm]");
        int ax = axisIndex(t[2]);
        double rpm = 0, torque = 5;
        num(3, rpm);
        num(4, torque);
        w.pushUndo();
        if (o->fixed && ax >= 0) w.setFixed(*o, false);
        w.setHinge(*o, ax, (float)rpm, (float)torque);
        res.text = brief(*o);
    } else if (cmd == "copy") {
        Object* o = obj(1);
        if (!o) return fail("copy <id> [dx dy dz]");
        double x, y, z;
        glm::dvec3 off(o->wMax.x - o->wMin.x + 0.02, 0, 0);
        if (num(2, x) && num(3, y) && num(4, z)) off = glm::dvec3(x, y, z) / 100.0;
        w.pushUndo();
        Object* c = w.duplicate(o->id, off);
        if (!c) return fail("copy failed");
        host.select(c->id);
        res.text = "Copy: " + brief(*c);
    } else if (cmd == "delete" || cmd == "remove" || cmd == "del") {
        if (word(1) == "all") {
            w.pushUndo();
            size_t n = w.objects().size();
            w.clear();
            res.text = std::to_string(n) + " parts deleted";
        } else {
            std::vector<uint32_t> ids;  // mehrere Teile: ein Rückgängig-Schritt
            for (size_t i = 1; i < t.size(); ++i) {
                Object* o = obj(i);
                if (!o) return fail("delete <id> [id ...] | delete all");
                if (std::find(ids.begin(), ids.end(), o->id) == ids.end()) ids.push_back(o->id);
            }
            if (ids.empty()) return fail("delete <id> [id ...] | delete all");
            std::string b = brief(*w.find(ids[0]));
            w.pushUndo();
            for (uint32_t id : ids) w.remove(id);
            res.text = ids.size() == 1 ? "Deleted: " + b : std::to_string(ids.size()) + " parts deleted";
        }
    } else if (cmd == "impulse" || cmd == "push") {
        Object* o = obj(1);
        double x, y, z;
        if (!o || !num(2, x) || !num(3, y) || !num(4, z)) return fail("impulse <id> jx jy jz (N·s)");
        w.applyImpulse(*o, glm::vec3((float)x, (float)y, (float)z));
        glm::vec3 v = w.physics().linearVelocity(o->body);
        res.text = "Impulse on #" + std::to_string(o->id) + ": speed now " + fmtNum(glm::length(v), 3) + " m/s";
    } else if (cmd == "sim" || cmd == "simulate") {
        double s = 1.0;
        num(1, s);
        s = std::clamp(s, 0.0, 600.0);
        std::vector<std::pair<uint32_t, glm::dvec3>> before;
        for (auto& o : w.objects()) before.emplace_back(o->id, o->pos);
        w.simulateSeconds(s);
        out << "simulated " << fmtNum(s, 2) << " s. Moved parts:\n";
        int moved = 0;
        for (auto& [id, p0] : before) {
            Object* o = w.find(id);
            if (!o) continue;
            double d = glm::length(o->pos - p0);
            if (d < 0.0005) continue;
            ++moved;
            glm::vec3 v = o->body != kNoBody ? w.physics().linearVelocity(o->body) : glm::vec3(0);
            out << "  #" << id << " " << o->name << ": moved " << fmtNum(d * 100, 2) << " cm, now " << v3(o->pos) << " cm, v="
                << fmtNum(glm::length(v), 3) << " m/s" << (glm::length(v) < 0.01f ? " (at rest)" : "") << "\n";
        }
        if (!moved) out << "  (none)\n";
        for (const std::string& e : w.takeEvents()) out << "!! " << e << "\n";
        res.text = out.str();
    } else if (cmd == "mode") {
        if (t.size() < 2) return fail("mode build|play");
        std::string m = word(1);
        if (m != "play" && m != "build") return fail("mode build|play");
        res.text = host.setMode(m == "play");
        for (const std::string& e : w.takeEvents()) res.text += "\n!! " + e;
    } else if (cmd == "view") {
        if (t.size() < 2) return fail("view perspective|top|front|side|quad");
        res.text = host.setView(word(1));
    } else if (cmd == "camera") {
        double x, y, z, d = 3;
        if (!num(1, x) || !num(2, y) || !num(3, z)) return fail("camera x y z [distance] (cm)");
        num(4, d);
        res.text = host.cameraLook(glm::dvec3(x, y, z) / 100.0, d > 10 ? d / 100.0 : d);
    } else if (cmd == "screenshot" || cmd == "photo") {
        if (t.size() < 2) return fail("screenshot <file.png>");
        res.text = host.screenshot(t[1]);
    } else if (cmd == "select") {
        std::vector<uint32_t> ids;
        for (size_t i = 1; i < t.size(); ++i) {
            if (word(i) == "all") {
                for (auto& o : w.objects()) ids.push_back(o->id);
                continue;
            }
            Object* o = obj(i);
            if (!o) return fail("select <id> [id ...] | select all");
            ids.push_back(o->id);
        }
        if (ids.empty()) {
            auto cur = host.selectedIds();
            out << cur.size() << " selected:\n";
            for (uint32_t id : cur)
                if (Object* o = w.find(id)) out << "  " << brief(*o) << "\n";
            res.text = out.str();
            return res;
        }
        host.selectMany(ids);
        out << ids.size() << " selected";
        if (ids.size() == 1) out << ": " << brief(*w.find(ids[0]));
        res.text = out.str();
    } else if (cmd == "drawing" || cmd == "threeview") {
        std::vector<uint32_t> ids;
        std::string file;
        for (size_t i = 1; i < t.size(); ++i) {
            std::string n = word(i);
            if (n.size() > 4 && n.substr(n.size() - 4) == ".svg") {
                file = t[i];
            } else if (n == "selected" || n == "sel") {
                for (uint32_t id : host.selectedIds()) ids.push_back(id);
            } else {
                Object* o = obj(i);
                if (!o) return fail("drawing [id ...] [file.svg]");
                ids.push_back(o->id);
            }
        }
        if (ids.empty()) ids = host.selectedIds();
        TechDrawing d;
        std::string err;
        if (!makeThreeView(w, ids, d, &err)) return fail(err + " (drawing <id ...> [file.svg])");
        Sheet s = layoutSheet(d);
        out << drawingSummary(d, s);
        if (!file.empty()) {
            std::ofstream f(std::filesystem::u8path(file), std::ios::binary);
            if (!f) return fail("cannot write the SVG: " + file);
            f << sheetToSvg(s);
            out << "SVG saved: " << file << "\n";
        }
        host.showDrawing(ids);
        res.text = out.str();
    } else if (cmd == "undo") {
        res.text = w.undo() ? "Undone" : "Nothing to undo";
    } else if (cmd == "redo") {
        res.text = w.redo() ? "Redone" : "Nothing to redo";
    } else if (cmd == "scene") {
        std::string m = word(1);
        w.pushUndo();
        if (m == "empty") w.clear();
        else if (m == "demo") w.demoScene();
        else if (m == "showcase") w.showcaseScene();
        else if (m == "fire") w.fireScene();
        else if (m == "water" || m == "pool") w.waterScene();
        else if (m == "river") w.riverScene();
        else if (m == "trees" || m == "baumschule") w.treeScene();
        else return fail("scene empty|demo|showcase|fire|water|river|trees");
        res.text = "Scene: " + std::to_string(w.objects().size()) + " parts, " + std::to_string(w.joints().size()) + " joints";
    } else if (cmd == "save") {
        if (t.size() < 2) return fail("save <file>");
        std::string err;
        // .bin: binär wie die Weltdatei (klein, schnell), sonst Text (lesbar, zum Weitergeben)
        const std::filesystem::path f = std::filesystem::u8path(t[1]);
        const bool bin = f.extension() == ".bin";
        if (!(bin ? w.saveBinary(f, &err) : w.save(f, &err))) return fail(err);
        res.text = "Saved: " + t[1];
    } else if (cmd == "load") {
        if (t.size() < 2) return fail("load <file>");
        std::string err;
        w.pushUndo();
        const std::filesystem::path f = std::filesystem::u8path(t[1]);
        if (!(World::isBinaryScene(f) ? w.loadBinary(f, &err) : w.load(f, &err))) return fail(err);
        res.text = "Loaded: " + std::to_string(w.objects().size()) + " parts";
    } else if (cmd == "quit" || cmd == "exit") {
        host.requestQuit();
        res.text = "Bye";
    } else if (cmd == "player" || cmd == "tp" || cmd == "teleport" || cmd == "tool" || cmd == "window" || cmd == "display" || cmd == "lan") {
        return fail("'" + t[0] + "' only works in the game window");
    } else {
        return fail("unknown command '" + t[0] + "'. 'help' shows all commands.");
    }
    return res;
}
