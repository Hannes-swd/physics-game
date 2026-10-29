// Automatische Tests: Geometrie (Sägen/Bohren/Abrunden/Fräsen/Drehen/Biegen), Physik (Fallen/Reibung/Rollen/Stapeln),
// Kollision (Stift im Loch, Anhalten beim Verschieben), Befehlssprache, Verbindungen, Zeichnung, Speichern.
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "Avatar.h"
#include "Commands.h"
#include "Drawing.h"
#include "Materials.h"
#include "Noise.h"
#include "Physics.h"
#include "Script.h"
#include "Solid.h"
#include "Terrain.h"
#include "Stones.h"
#include "Trees.h"
#include "World.h"
#include "WorldGen.h"

namespace {

int failures = 0, checks = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("[%s] %s%s%s\n", ok ? " OK " : "FAIL", what.c_str(), detail.empty() ? "" : "  -> ", detail.c_str());
    std::fflush(stdout);  // stürzt etwas ab, steht trotzdem da, wie weit es kam
}

bool approx(double a, double b, double tol) { return std::abs(a - b) <= tol; }
std::string num(double v) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.6f", v);
    return b;
}

double triangleArea(const std::vector<solid::Vertex>& v) {
    double a = 0;
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
        glm::dvec3 p0(v[i].px, v[i].py, v[i].pz), p1(v[i + 1].px, v[i + 1].py, v[i + 1].pz), p2(v[i + 2].px, v[i + 2].py, v[i + 2].pz);
        a += 0.5 * glm::length(glm::cross(p1 - p0, p2 - p0));
    }
    return a;
}

class TestHost : public CommandHost {
public:
    explicit TestHost(World& w) : w_(w) {}
    World& world() override { return w_; }
    uint32_t selectedId() const override { return sel_; }
    void select(uint32_t id) override { sel_ = id; }

private:
    World& w_;
    uint32_t sel_ = 0;
};

} // namespace

int runSelfTest() {
    const double pi = 3.14159265358979323846;
    loadBuiltinMaterials();  // die mitgelieferten Werte, unabhängig von Änderungen an assets/materials.json
    std::printf("== Geometry ==\n");
    {
        solid::Solid box;
        box.pieces.push_back(solid::makeBox(glm::dvec3(0.5)));
        check(approx(solid::volume(box), 1.0, 1e-9), "cube 1 m3", num(solid::volume(box)));

        solid::Plane pl;
        pl.n = glm::dvec3(1, 0, 0);
        pl.d = 0.2;
        solid::Solid below, above;
        solid::split(box, pl, below, above);
        check(approx(solid::volume(below), 0.7, 1e-9) && approx(solid::volume(above), 0.3, 1e-9), "saw cut 70/30",
              num(solid::volume(below)) + " / " + num(solid::volume(above)));

        double removed = 0;
        solid::Solid drilled = solid::subtract(box, solid::drillTool({0, 0.6, 0}, {0, -1, 0}, 0.1, 2.0, 32), &removed);
        check(approx(removed, pi * 0.01, 1e-9), "drill removes pi*r^2*h", num(removed));
        check(approx(solid::volume(drilled), 1.0 - pi * 0.01, 1e-9), "volume after drilling", num(solid::volume(drilled)));
        check(solid::connectedComponents(drilled).size() == 1, "drilled cube stays one part");
        solid::classifyInternalFaces(drilled);
        std::vector<solid::Vertex> tris;
        solid::buildTriangles(drilled, tris);
        // Oberfläche: 6 m² - 2 Lochflächen + Lochwand (Umfang des Vielecks x 1 m)
        double polyR = 0.1 * std::sqrt(2.0 * pi / (32 * std::sin(2.0 * pi / 32)));
        double perim = 32 * 2 * polyR * std::sin(pi / 32);
        double expectArea = 6.0 - 2 * pi * 0.01 + perim;
        check(approx(triangleArea(tris), expectArea, 1e-6), "visible surface without inner faces", num(triangleArea(tris)) + " expected " + num(expectArea));

        solid::Solid kerfed = solid::subtract(box, solid::slabTool({1, 0, 0}, 0.0, 0.01));
        auto parts = solid::connectedComponents(kerfed);
        check(parts.size() == 2 && approx(solid::volume(parts[0]), 0.495, 1e-9), "cut with 1 cm kerf -> 2 parts", std::to_string(parts.size()));

        solid::Solid gear = solid::makeGear(20, 0.004, 0.015, 0.010);
        double gv = solid::volume(gear);
        double rf = 0.004 * (10 - 1.25), ra = 0.004 * 11;
        check(gv > pi * rf * rf * 0.015 * 0.9 && gv < pi * ra * ra * 0.015, "gear volume plausible", num(gv * 1e6) + " cm3");
        check(solid::connectedComponents(gear).size() == 1, "gear is one connected part");

        solid::Solid ball = solid::makeSphere(0.5, 2);
        check(approx(solid::volume(ball), 4.0 / 3.0 * pi * 0.125, 1e-9), "sphere volume", num(solid::volume(ball)));

        // Kanten eines Würfels, Abrunden und Fasen einer Kante
        auto edges = solid::findEdges(box);
        bool allConvex = true;
        for (auto& e : edges) allConvex &= e.convex && std::abs(e.angle - pi / 2) < 1e-6;
        check(edges.size() == 12 && allConvex, "cube has 12 convex 90 degree edges", std::to_string(edges.size()));
        const double r = 0.1;
        solid::Solid rounded = box;
        for (auto& t : solid::edgeBlendTools(edges[0], r, 8)) rounded = solid::subtract(rounded, t, nullptr, false);
        const double expectRound = r * r * (1.0 - 8 * 0.5 * std::sin(pi / 16)) * 1.0;  // Ecke minus 8-Eck-Viertel
        check(approx(1.0 - solid::volume(rounded), expectRound, 1e-9), "fillet r=10 cm removes corner minus arc",
              num(1.0 - solid::volume(rounded)) + " (circle would be " + num(r * r * (1 - pi / 4)) + ")");
        solid::Solid chamfered = box;
        for (auto& t : solid::edgeBlendTools(edges[0], 0.2, 1)) chamfered = solid::subtract(chamfered, t, nullptr, false);
        check(approx(1.0 - solid::volume(chamfered), 0.5 * 0.2 * 0.2, 1e-9), "chamfer 20 cm removes s^2/2 per m", num(1.0 - solid::volume(chamfered)));
        // L-Profil: Innenkante (Hohlkehle) füllt Material auf
        solid::Solid ell;
        ell.pieces.push_back(solid::makeBox({0.5, 0.1, 0.5}));
        solid::Piece leg = solid::makeBox({0.1, 0.4, 0.5});
        solid::transform(leg, glm::translate(glm::dmat4(1.0), glm::dvec3(-0.4, 0.5, 0)));
        ell.pieces.push_back(leg);
        solid::classifyInternalFaces(ell);
        int concave = -1;
        auto le = solid::findEdges(ell);
        for (size_t i = 0; i < le.size(); ++i)
            if (!le[i].convex) concave = (int)i;
        double v0 = solid::volume(ell);
        if (concave >= 0) ell = solid::addRegions(ell, solid::edgeBlendTools(le[concave], 0.1, 1));
        check(concave >= 0 && approx(solid::volume(ell) - v0, 0.5 * 0.01 * 1.0, 1e-9), "concave edge found, chamfer adds material",
              num(solid::volume(ell) - v0));
        // Drehbank: Zylinder D 40 mm auf 20 mm über 5 cm
        solid::Solid cyl = solid::makeCylinder(0.02, 0.1, 48);
        double cv = solid::volume(cyl);
        solid::Solid turned = solid::turn(cyl, {0, -0.05, 0}, {0, 1, 0}, 0.0, 0.05, 0.01, 0.01, 48);
        check(approx(cv - solid::volume(turned), pi * (0.02 * 0.02 - 0.01 * 0.01) * 0.05, 2e-8), "lathe: D40 -> D20 over 5 cm",
              num((cv - solid::volume(turned)) * 1e6) + " cm3");
    }

    std::printf("== Physics ==\n");
    Physics physics;
    physics.init();
    {
        World w(physics, nullptr);
        TestHost host(w);
        const int spruce = materialIndex("spruce");
        Object* cube = w.addBox(spruce, {0.1, 0.1, 0.1}, {0, 0.5, 0});
        double expectMass = 0.001 * 470;
        check(approx(cube->mass, expectMass, 1e-9), "mass of spruce cube 10 cm", num(cube->mass) + " kg");
        w.simulateSeconds(2.0);
        check(approx(cube->pos.y, 0.05, 0.003), "cube falls and lies on the floor", "y=" + num(cube->pos.y));

        // freier Fall: nach 0,3 s ca. 0,44 m gefallen (mit etwas Luftwiderstand)
        Object* ball = w.addSphere(materialIndex("steel"), 0.05, {1, 5, 0});
        double y0 = ball->pos.y;
        w.simulateSeconds(0.3);
        double fall = y0 - ball->pos.y;
        check(approx(fall, 0.5 * 9.81 * 0.09, 0.02), "free fall 0.3 s", num(fall) + " m (theory 0.441)");

        // Rampen: Haftreibung hält unterhalb des Reibungswinkels ohne zu kriechen, darüber rutscht es
        auto ramp = [&](double deg, const char* mat, double x) {
            const double a = deg * pi / 180.0;
            glm::dquat r = glm::angleAxis(a, glm::dvec3(0, 0, 1));
            glm::dvec3 c(x, 0.5 * std::sin(a) + 0.02, 0);
            Object* rp = w.addBox(materialIndex("oak"), {1.0, 0.02, 0.35}, c);
            w.setTransform(*rp, c, r);
            w.setFixed(*rp, true);
            glm::dvec3 along = r * glm::dvec3(1, 0, 0), up = r * glm::dvec3(0, 1, 0);
            Object* b = w.addBox(materialIndex(mat), {0.06, 0.06, 0.06}, c);
            w.setTransform(*b, c + along * 0.25 + up * 0.0401, r);
            return b->id;
        };
        uint32_t s30 = ramp(30, "steel", 5), g30 = ramp(30, "rubber", 7), s20 = ramp(20, "steel", 9);
        glm::dvec3 a0 = w.find(s30)->pos, b0 = w.find(g30)->pos, c0 = w.find(s20)->pos;
        w.simulateSeconds(1.5);
        double ds = glm::length(w.find(s30)->pos - a0), dg = glm::length(w.find(g30)->pos - b0), d20 = glm::length(w.find(s20)->pos - c0);
        check(ds > 0.1, "steel slides on 30 deg oak ramp (static 0.45 < tan30 = 0.58)", num(ds * 100) + " cm");
        check(dg < 0.005, "rubber sticks on the ramp (static 0.8)", num(dg * 100) + " cm");
        check(d20 < 0.002, "steel sticks on 20 deg ramp without creeping (tan20 = 0.36 < 0.45)", num(d20 * 1000) + " mm");

        // Inspektor: ruhender Würfel -> Kontaktkraft = Gewicht
        w.setInspect(cube->id);
        w.simulateSeconds(0.5);
        const InspectData& d = w.inspect();
        check(approx(d.fContact.y, cube->mass * 9.81 - 1.204 * cube->volume * 9.81, 0.05), "inspector: support force = weight - buoyancy",
              num(d.fContact.y) + " N");

        std::printf("== Tools ==\n");
        CommandResult res = runCommand(host, "new box spruce 80 10 2 0 1 200");
        check(res.ok, "command: new box", res.text);
        uint32_t board = host.selectedId();
        res = runCommand(host, "cut " + std::to_string(board) + " x 30");
        check(res.ok, "command: cut at 30 cm", res.text);
        Object* b = w.find(board);
        check(b && approx((b->bbMax - b->bbMin).x, 0.5, 1e-6), "larger piece is 50 cm long");
        double m0 = b ? b->mass : 0;
        res = runCommand(host, "drill " + std::to_string(board) + " top 0 0 10");
        check(res.ok, "command: drill D10 through", res.text);
        double expectDelta = pi * 0.005 * 0.005 * 0.02 * 470;
        check(b && approx(m0 - b->mass, expectDelta, 1e-7), "mass drops by the chips", num((m0 - b->mass) * 1000) + " g");
        check(!runCommand(host, "neu quader fichte 10 10 2").ok, "old German command is rejected");

        TechDrawing td;
        std::string err;
        bool ok = makeThreeView(w, {board}, td, &err);
        check(ok, "three-view drawing created", err);
        if (ok) {
            check(td.views[0].hiddenCount > 0, "front view shows the hidden hole", std::to_string(td.views[0].hiddenCount));
            bool hasDia = false;
            for (auto& l : td.views[1].labels) hasDia |= l.text == "Ø10";
            check(hasDia, "top view labels D10");
            Sheet sh = layoutSheet(td);
            std::string svg = sheetToSvg(sh);
            check(svg.find("<svg") != std::string::npos && sh.scaleText == "1:5", "SVG at scale 1:5", sh.scaleText);
        }

        auto val = [&](const std::string& expr) {
            std::string o = runScriptSync(host, "print " + expr);
            size_t p = o.rfind("= ");
            return p == std::string::npos ? -1e30 : std::atof(o.c_str() + p + 2);
        };

        std::printf("== Editing ==\n");
        {
            w.setSimulating(false);
            runScriptSync(host, "scene empty");
            runScriptSync(host, "new box oak 20 10 4 0 50 0 ; set k = last ; fix $k");
            double v0 = val("k.volume");
            res = runCommand(host, "round " + std::to_string((int)val("k.id")) + " +y 5");
            // 60 cm Kante x Querschnitt (0,5 cm)² x 0,2196, an den 4 Ecken etwas weniger (Überlappung)
            check(res.ok && approx(v0 - val("k.volume"), 60 * 0.25 * 0.21964, 0.1), "round 4 top edges r=5 mm",
                  num(v0 - val("k.volume")) + " cm3");
            v0 = val("k.volume");
            res = runCommand(host, "pocket " + std::to_string((int)val("k.id")) + " -y 0 0 60 30 15");
            check(res.ok && approx(v0 - val("k.volume"), 6 * 3 * 1.5, 1e-3), "pocket 60x30x15 mm", num(v0 - val("k.volume")) + " cm3");
            v0 = val("k.volume");
            res = runCommand(host, "groove " + std::to_string((int)val("k.id")) + " +z x 0 6 5");
            check(res.ok && v0 - val("k.volume") > 3.0, "groove 6x5 mm along x", res.text.substr(0, res.text.find('\n')));
            res = runCommand(host, "plane " + std::to_string((int)val("k.id")) + " -x 2");
            check(res.ok && approx(val("k.length"), 19.8, 1e-6), "plane 2 mm off the end", num(val("k.length")) + " cm");

            runScriptSync(host, "new box steel 30 5 0.2 0 20 100 ; set sh = last");
            double vs = val("sh.volume");
            res = runCommand(host, "bend " + std::to_string((int)val("sh.id")) + " x 10 90 2");
            check(res.ok && approx(val("sh.volume"), vs, vs * 0.02) && val("sh.height") > 15, "bend steel sheet 90 degrees (volume kept)",
                  num(val("sh.volume")) + " / " + num(vs) + " cm3, height " + num(val("sh.height")));
            runScriptSync(host, "new box brick 24 11.5 7.1 100 20 100");
            check(!runCommand(host, "bend last x 10 30").ok, "brick cannot be bent");

            runScriptSync(host, "new cylinder steel 4 10 -100 20 100 ; set rod = last");
            v0 = val("rod.volume");
            res = runCommand(host, "turn " + std::to_string((int)val("rod.id")) + " y 0 5 20");
            check(res.ok && approx(v0 - val("rod.volume"), pi * (4 - 1) * 5, 0.3), "turn D40 -> D20 on 5 cm", num(v0 - val("rod.volume")) + " cm3");

            runScriptSync(host, "new box spruce 10 10 2 -200 20 0 ; set m1 = last ; new box spruce 10 10 2 -190 20 0 ; set m2 = last");
            double vm = val("m1.volume") + val("m2.volume");
            res = runCommand(host, "merge " + std::to_string((int)val("m1.id")) + " " + std::to_string((int)val("m2.id")));
            check(res.ok && approx(val("m1.volume"), vm, 1e-6) && val("m2.exists") == 0, "merge two touching boards", num(val("m1.volume")) + " cm3");
            runScriptSync(host, "new box spruce 10 10 10 -300 20 0 ; set cb = last ; new sphere steel 6 -300 25 0 ; set sp = last");
            v0 = val("cb.volume");
            res = runCommand(host, "subtract " + std::to_string((int)val("cb.id")) + " " + std::to_string((int)val("sp.id")));
            check(res.ok && v0 - val("cb.volume") > 40 && val("sp.exists") == 0, "subtract a sphere from a block", num(v0 - val("cb.volume")) + " cm3");
            double mm = val("cb.mass");
            runCommand(host, "scale " + std::to_string((int)val("cb.id")) + " 2");
            check(approx(val("cb.mass"), mm * 8, 0.002), "scale x2 -> 8x mass", num(mm) + " -> " + num(val("cb.mass")) + " kg");
            v0 = val("cb.volume");
            runCommand(host, "mirror " + std::to_string((int)val("cb.id")) + " x");
            check(approx(val("cb.volume"), v0, 1e-6), "mirror keeps the volume");
        }

        std::printf("== Collision ==\n");
        {
            runScriptSync(host, "scene empty ; new box oak 30 10 2 0 50 0 ; set plate = last ; fix $plate ; drill $plate top 0 0 22");
            check(val("plate.hulls") > 1, "drilled plate keeps its hole in the collision shape", num(val("plate.hulls")) + " hulls");
            runScriptSync(host, "new cylinder steel 1.8 8 0 60 0 ; set peg = last");
            w.setSimulating(true);
            runScriptSync(host, "wait 1.5");
            check(val("peg.y") < 45, "peg D18 falls through hole D22", "peg.y = " + num(val("peg.y")));
            w.setSimulating(false);
            runScriptSync(host, "new box spruce 10 10 10 100 5 0 ; set a = last ; new box spruce 10 10 10 130 5 0 ; set bb = last");
            res = runCommand(host, "move " + std::to_string((int)val("a.id")) + " 40 0 0");
            check(res.text.find("stopped") != std::string::npos && approx(val("bb.x") - val("a.x"), 10, 0.05), "move stops at the next part",
                  num(val("bb.x") - val("a.x")) + " cm apart");
            w.setSimulating(true);
            // Rollreibung: Gummiball rollt mit 1 m/s und wird langsamer
            runScriptSync(host, "new sphere rubber 15 300 7.5 0 ; set rb = last ; wait 0.3");
            Object* rb = w.find((uint32_t)val("rb.id"));
            if (rb) physics.setVelocity(rb->body, glm::vec3(1, 0, 0), glm::vec3(0, 0, -1.0f / 0.075f));
            runScriptSync(host, "wait 5");
            double vr = val("rb.v");
            check(vr > 0.45 && vr < 0.8, "rolling resistance slows a rubber ball (1 m/s -> ~0.63 m/s in 5 s)", num(vr) + " m/s");
            // Ziegelturm steht ruhig
            std::vector<uint32_t> tower;
            for (int i = 0; i < 8; ++i) {
                Object* o = w.addBox(materialIndex("brick"), {0.24, 0.071, 0.115}, {-6.0, 0.0355 + i * 0.0711, 0.0});
                tower.push_back(o->id);
            }
            runScriptSync(host, "wait 2");
            glm::dvec3 top0 = w.find(tower.back())->pos;
            runScriptSync(host, "wait 3");
            double drift = glm::length(w.find(tower.back())->pos - top0);
            check(drift < 0.001, "tower of 8 bricks stands still", num(drift * 1000) + " mm drift in 3 s");
            // Teile, die ineinander gesetzt werden, kommen sanft frei (kein Herausschleudern)
            runScriptSync(host, "new box steel 10 10 10 800 5 0 ; set o1 = last ; new box steel 10 10 10 805 5 0 ; set o2 = last ; wait 1");
            check(val("abs(o2.x - o1.x)") > 9.5 && val("o1.v") < 0.3 && val("o2.v") < 0.3, "overlapping parts separate gently",
                  "distance " + num(val("abs(o2.x - o1.x)")) + " cm");
        }

        std::printf("== Pins and bores ==\n");
        {
            w.setSimulating(false);
            runScriptSync(host, "scene empty ; new box steel 4 4 4 0 10 0 ; set k = last ; fix $k ; drill $k top 0 0 10 30");
            check(val("k.bores") == 1, "drilled hole becomes an exact round bore", num(val("k.bores")));
            runScriptSync(host, "new cylinder steel 0.98 8 0 20 0 ; set p = last ; move $p 0 -6 0");
            w.setSimulating(true);
            runScriptSync(host, "wait 1");
            check(approx(val("p.bottom"), 9.0, 0.01) && val("p.overlap") < 0.05, "pin D9.8 slides into blind hole D10 down to the bottom",
                  "bottom " + num(val("p.bottom")) + " cm (hole bottom 9.0)");
            w.setSimulating(false);
            runScriptSync(host, "new box steel 4 4 4 20 10 0 ; set k2 = last ; fix $k2 ; drill $k2 top 0 0 10 ; new cylinder steel 1.05 8 20 20 0 ; "
                                "set p2 = last ; move $p2 0 -6 0");
            w.setSimulating(true);
            runScriptSync(host, "wait 1");
            check(val("p2.bottom") > 11.98, "pin D10.5 does not go into hole D10", "bottom " + num(val("p2.bottom")) + " cm (top of hole 12.0)");

            // Tür an selbstgebauten Stiftscharnieren (Lappen mit Stift gedreht, Lappen mit Bohrung abgerundet)
            auto door = [&](double x, double drop, const std::string& lube) {
                auto X = [&](double v) { return num(x + v); };
                auto Y = [&](double v) { return num(v + drop); };
                w.setSimulating(false);
                // wie in echt: Stahlplatte auf den Pfosten geklebt und geschraubt, Stiftlappen angeschweißt; Band mit
                // abgerundetem Auge auf die Türfläche geklebt und geschraubt
                std::string s = "new box oak 6 6 200 " + X(0) + " 100 0 ; set post = last ; fix $post";
                for (int i = 0; i < 2; ++i) {
                    const std::string p = i ? "plb" : "pla", f = i ? "fkb" : "fka", d = i ? "dkb" : "dka";
                    const double yk = i ? 164 : 24;
                    s += " ; new box steel 0.8 6 10 " + X(3.4) + " " + num(yk - 2) + " 0 ; set " + p + " = last ; glue $post $" + p + " epoxy ; screw $" + p +
                         " $post 2";
                    s += " ; new cylinder steel 3 8 " + X(5.1) + " " + num(yk) + " 2.4 ; set " + f + " = last ; turn $" + f + " y 4 8 10 ; turn $" + f +
                         " y 7.5 8 10 7 ; plane $" + f + " -x 2 ; weld $" + p + " $" + f + " 4";
                    s += " ; new box steel 14 2.4 4 " + X(10.89) + " " + Y(yk + 2) + " 2.4 ; set " + d + " = last ; round $" + d + " e4,e5 11.9 ; drill $" + d +
                         " top -5.79 0 10.4";
                    if (!lube.empty()) s += " ; lubricate $" + d + " " + lube;
                }
                s += " ; new box oak 80 2.4 190 " + X(47.1) + " " + Y(100) + " 0 ; set door = last ; glue $door $dka epoxy ; screw $dka $door 2 ; "
                     "glue $door $dkb epoxy ; screw $dkb $door 2";
                runScriptSync(host, s);
                w.setSimulating(true);
                runScriptSync(host, "wait 2");
            };
            const double r0 = w.rescueCount();
            door(100, 0, "");
            check(std::abs(val("door.rz")) < 0.5 && approx(val("dka.bottom"), 24.0, 0.15) && val("rescued") == r0,
                  "door hangs straight on two self-made pin hinges", "tilt " + num(val("door.rz")) + "°, strap bottom " + num(val("dka.bottom")) + " cm");
            check(val("dka.boreforce") > 20 && val("dkb.boreforce") > 20, "both pins carry the door (load in the bores)",
                  num(val("dka.boreforce")) + " N / " + num(val("dkb.boreforce")) + " N");
            runScriptSync(host, "impulse $door 0 0 5 ; wait 3");
            const double dry = std::abs(val("door.ry"));
            check(dry > 3 && dry < 40 && val("door.rpm") < 0.5, "a push opens it, friction in the hinges stops it", num(dry) + "°");
            door(400, 0, "oil");
            runScriptSync(host, "impulse $door 0 0 5 ; wait 3");
            const double oiled = std::abs(val("door.ry"));
            check(oiled > 2 * dry, "oiled hinges: the same push opens it much further", num(oiled) + "° instead of " + num(dry) + "°");
            door(700, 12, "");
            check(std::abs(val("door.rz")) < 0.5 && approx(val("dkb.bottom"), 164.0, 0.15), "door dropped 12 cm onto the pins hangs straight",
                  "tilt " + num(val("door.rz")) + "°, upper strap bottom " + num(val("dkb.bottom")) + " cm");

            // dünner Fichtenlappen reißt quer zur Faser aus
            w.setSimulating(false);
            runScriptSync(host, "new cylinder steel 1 10 1000 5 0 ; fix last ; new tube steel 3 1.1 0.5 1000 4.75 0 ; fix last ; "
                                "new box spruce 10 1.4 2 1004 6 0 ; set st = last ; drill $st top -4 0 10.2");
            const int broken0 = w.brokenCount();
            w.setSimulating(true);
            runScriptSync(host, "wait 0.5 ; impulse $st 0 0 0.3 ; wait 0.2");
            check(w.brokenCount() > broken0 && val("st.bores") == 0, "a thin spruce strap tears out across the grain");

            // Schmierstoff und Abrieb werden gespeichert
            const std::string lubFile = (std::filesystem::temp_directory_path() / "werkstatt_selftest_lube.werkstatt").generic_string();
            runScriptSync(host, "lubricate $dka grease ; save " + lubFile + " ; load " + lubFile);
            const std::string lubOut = runScriptSync(host, "print dka.lubricant, dka.worn");
            check(lubOut.find("grease") != std::string::npos, "lubricant and wear are saved and loaded", lubOut.substr(0, lubOut.find('\n')));
            std::filesystem::remove(lubFile);
        }

        std::printf("== Script language ==\n");
        {
            w.setSimulating(true);  // wie ohne Fenster: Zeit vergeht bei wait/sim
            auto has = [](const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; };
            std::string o = runScriptSync(host, "new sphere steel 5 300 150 0 ; wait until last.resting max 10 ; "
                                                "if last.bottom < 0.3 then print \"lying\" else print \"floating\"");
            check(has(o, "true after") && has(o, "lying"), "sequence: create, wait until resting, if/else", o.substr(o.rfind('\n', o.size() - 2) + 1));
            o = runScriptSync(host, "set n = parts ; repeat 3 times { new box spruce 10 10 2 } ; print parts - n");
            check(has(o, "parts - n = 3"), "repeat 3 times + set + math");
            o = runScriptSync(host, "set h = 42 ; new box oak 10 10 10 400 $h 0 ; print last.y, last.material");
            check(has(o, "last.y = 42") && has(o, "last.material = oak"), "variable in a command ($h) + text values");
            o = runScriptSync(host, "if last.material == \"oak\" and not last.fixed then print \"ok\"");
            check(has(o, "ok"), "text comparison, and, not");
            o = runScriptSync(host, "print 99999.exists, (1 + 2) * 3, max(1, 5, 3), round(2.6)");
            check(has(o, "99999.exists = 0") && has(o, "(1 + 2) * 3 = 9") && has(o, "max(1, 5, 3) = 5") && has(o, "round(2.6) = 3"),
                  "exists, brackets, functions");
            bool ok2 = true;
            o = runScriptSync(host, "if then { }", &ok2);
            check(!ok2 && has(o, "Error"), "syntax error is reported", o);
            o = runScriptSync(host, "wait until 1 > 2 max 0.5 ; print \"continue\"");
            check(has(o, "time limit") && has(o, "continue"), "wait until with time limit");
        }

        std::printf("== Joints, breaking, air ==\n");
        {
            runScriptSync(host, "scene empty ; new box oak 60 10 5 0 100 0 ; set beam = last ; fix $beam");
            runScriptSync(host, "new box spruce 30 10 2 0 96.5 0 ; set board = last ; glue $beam $board hotglue 0.01");
            runScriptSync(host, "new box steel 10 10 10 0 90.5 0 ; set block = last ; glue $board $block epoxy ; wait 1");
            check(approx(val("block.y"), 90.5, 0.3) && val("broken") == 0, "glue holds 8 kg (strength 300 N)", std::to_string(val("block.y")));
            runScriptSync(host, "joint 1 strength 0.002 ; wait 1.5");
            check(val("block.y") < 20 && val("broken") == 1, "glue tears when overloaded (60 N < 80 N)", std::to_string(val("block.y")));

            runScriptSync(host, "new propeller aluminium 30 2 20 4 3 ; set p = last ; position $p 300 100 0 ; axle $p y 3000 5 ; wait 2");
            double t3 = val("p.airy");
            runScriptSync(host, "axle $p y 6000 5 ; wait 2");
            double t6 = val("p.airy");
            check(t3 > 1 && approx(t6 / t3, 4.0, 0.3), "propeller: thrust forward, double speed = 4x thrust", num(t3) + " N / " + num(t6) + " N");
            runScriptSync(host, "air vacuum ; wait 0.5");
            check(approx(val("p.airy"), 0, 1e-9), "vacuum: no thrust");
            runScriptSync(host, "air normal");

            runScriptSync(host, "new box brick 24 11.5 7.1 -300 30 0 ; wait 2");
            double before = val("broken");
            runScriptSync(host, "new box brick 24 11.5 7.1 -500 200 0 ; wait 2");
            check(val("broken") == before + 1, "brick: intact from 30 cm, shattered from 2 m");
            runScriptSync(host, "new box steel 10 10 10 -700 300 0 ; wait 2");
            check(val("broken") == before + 1, "steel does not break when it falls");

            // Form des Werkzeugs: Keil entlang der Faser spaltet, quer dazu bleibt er stecken, runde Spitze macht nur ein Loch
            double b0 = val("broken"), p0 = val("parts"), j0 = val("joints");
            runScriptSync(host, "new box spruce 60 10 2 -300 1 -400 ; new wedge steel 10 8 25 -300 300 -400 ; wait 1.5");
            check(val("broken") == b0 + 1 && val("parts") == p0 + 3, "wedge along the grain splits a board (3 m)",
                  num(val("parts") - p0) + " new parts");
            runScriptSync(host, "new box spruce 60 10 2 -100 1 -400 ; new wedge steel 10 8 25 -100 300 -400 ; set wq = last ; "
                                "rotate $wq y 90 ; wait 1.5");
            check(val("broken") == b0 + 1 && val("joints") == j0 + 1 && val("wq.y") < 7.5 && val("wq.v") < 0.01,
                  "wedge across the grain cuts in and sticks", num(val("wq.y")) + " cm");
            runScriptSync(host, "wait 1");
            check(val("joints") == j0 + 1 && val("wq.v") < 0.01, "stuck wedge stays in its notch");
            runScriptSync(host, "new box spruce 60 10 2 100 1 -400 ; set bs = last ; new spike steel 1 8 5 100 200 -400 ; wait 1.5");
            check(val("bs.volume") < 1199.99 && val("bs.volume") > 1199 && val("joints") == j0 + 1 && val("broken") == b0 + 1,
                  "round tip: only a small hole, no split", num(val("bs.volume")) + " cm3");
            runScriptSync(host, "new box brick 24 11.5 7.1 300 3.55 -400 ; new wedge steel 10 8 25 300 300 -400 ; rotate last y 90 ; wait 1.5");
            check(val("broken") == b0 + 2, "chisel splits a brick");

            runScriptSync(host, "new box aluminium 40 20 4 0 8 400 ; set cart = last");
            for (const char* r : {"15 6 412", "-15 6 412", "15 6 388", "-15 6 388"})
                runScriptSync(host, std::string("new cylinder rubber 12 3 ") + r + " ; rotation last 90 0 0 ; hinge $cart last y");
            runScriptSync(host, "new propeller aluminium 30 2 20 4 3 ; set pr = last ; position $pr -15 28 400 ; rotation $pr 0 0 -90 ; hinge $cart $pr y 6000 3");
            runScriptSync(host, "set x0 = cart.x ; wait 3");
            check(val("cart.x - x0") > 150, "propeller cart drives (wheels on hinges)", num(val("cart.x - x0")) + " cm");

            runScriptSync(host, "new box oak 20 10 5 300 300 400 ; set hook = last ; fix $hook ; new sphere steel 10 400 250 400 ; set bob = last ; rope $hook $bob");
            double l0 = val("distance(hook, bob)");
            runScriptSync(host, "wait 1.5");
            check(val("distance(hook, bob)") <= l0 + 0.5 && val("bob.v") > 0.5, "rope: pendulum swings, rope does not stretch");

            std::string err2;
            auto f = std::filesystem::temp_directory_path() / "werkstatt_joints.werkstatt";
            size_t nj = w.joints().size();
            w.save(f, &err2);
            w.load(f, &err2);
            check(w.joints().size() == nj && nj >= 6, "joints are saved and loaded", std::to_string(nj));
            std::filesystem::remove(f);
        }

        auto file = std::filesystem::temp_directory_path() / "werkstatt_selftest.werkstatt";
        size_t count = w.objects().size();
        double vol = 0;
        for (auto& o : w.objects()) vol += o->volume;
        check(w.save(file, &err), "save scene", err);
        check(w.load(file, &err), "load scene", err);
        double vol2 = 0;
        for (auto& o : w.objects()) vol2 += o->volume;
        check(w.objects().size() == count && approx(vol, vol2, 1e-12), "saved scene is identical", std::to_string(w.objects().size()));
        {  // Weltdatei binär: dasselbe, kleiner, mit den fertigen Kollisionsformen
            auto bin = std::filesystem::temp_directory_path() / "werkstatt_selftest.bin";
            const size_t joints = w.joints().size();
            std::vector<std::string> before;
            for (auto& o : w.objects())
                before.push_back(std::to_string(o->id) + o->name + fmtNum(o->pos.x * 1e4, 0) + fmtNum(o->mass * 1e6, 0) + std::to_string(o->fixed));
            const bool saved = w.saveBinary(bin, &err);
            const auto textSize = std::filesystem::file_size(file), binSize = saved ? std::filesystem::file_size(bin) : 0;
            const bool loaded = World::isBinaryScene(bin) && w.loadBinary(bin, &err);
            std::vector<std::string> after;
            int cached = 0;
            for (auto& o : w.objects()) {
                after.push_back(std::to_string(o->id) + o->name + fmtNum(o->pos.x * 1e4, 0) + fmtNum(o->mass * 1e6, 0) + std::to_string(o->fixed));
                cached += o->hullCache != nullptr;
            }
            check(saved && loaded && before == after && w.joints().size() == joints && binSize < textSize && cached > 0,
                  "the binary world file holds the same parts and joints, smaller, with ready collision shapes",
                  std::to_string(binSize / 1024) + " KB instead of " + std::to_string(textSize / 1024) + " KB, " + std::to_string(cached) +
                      " shapes not recomputed, " + err);
            std::filesystem::remove(bin);
        }
        std::filesystem::remove(file);

        w.demoScene();
        check(w.objects().size() > 10, "demo workshop", std::to_string(w.objects().size()) + " parts in " + std::to_string(w.grid().chunkCount()) + " chunks");
        w.simulateSeconds(3.0);
        Object* small = nullptr;
        Object* big = nullptr;
        for (auto& o : w.objects()) {
            if (o->name == "Small gear (motor)") small = o.get();
            if (o->name == "Large gear") big = o.get();
        }
        if (small && big) {
            double ws = glm::length(physics.angularVelocity(small->body)), wb = glm::length(physics.angularVelocity(big->body));
            check(ws > 1.0 && approx(wb / ws, 20.0 / 30.0, 0.15), "gears: large gear turns at 2/3 the speed",
                  num(ws * 60 / (2 * pi)) + " / " + num(wb * 60 / (2 * pi)) + " rpm");
        }

        w.showcaseScene();
        check(w.objects().size() > 40 && w.joints().size() > 5, "showcase world", std::to_string(w.objects().size()) + " parts, " +
                                                                                    std::to_string(w.joints().size()) + " joints");
        w.setSimulating(true);
        double worst = 0;
        for (int i = 0; i < 20; ++i) {
            w.simulateSeconds(0.25);
            worst = std::max(worst, (double)physics.stats().maxPenetration);
        }
        double fastest = 0;
        for (auto& o : w.objects())
            if (o->body != kNoBody && !o->fixed && o->hingeAxis < 0) fastest = std::max(fastest, (double)glm::length(physics.linearVelocity(o->body)));
        check(fastest < 20, "showcase runs 5 s without anything exploding", "fastest " + num(fastest) + " m/s, deepest " + num(worst * 1000) + " mm");
    }

    std::printf("== Heat and fire ==\n");
    {
        World w(physics, nullptr);
        w.setSimulating(true);
        TestHost host(w);
        auto run = [&](const std::string& line) { return runScriptSync(host, line); };
        auto val = [&](const std::string& expr) {
            std::string o = runScriptSync(host, "print " + expr);
            size_t p = o.rfind("= ");
            return p == std::string::npos ? -1e30 : std::atof(o.c_str() + p + 2);
        };
        // Wärmeleitung: Gasbrenner 30 s auf ein Ende eines Stahlstabs
        run("new box steel 50 2 2 0 1 0 ; set bar = last ; torch $bar gas 30 right ; wait 30");
        const double hot = val("bar.tempmax"), cold = val("bar.tempmin");
        check(hot > 250 && hot < 900 && cold < 60, "gas torch heats the end of a steel bar, the other end stays cool",
              num(hot) + " / " + num(cold) + " °C");
        run("wait 120");
        check(val("bar.tempmax") < hot && val("bar.temp") > 25, "the bar cools in the air and the heat spreads", num(val("bar.tempmax")) + " °C");
        // Festigkeit: glühender Stahl ist weich, kalter spröde
        Object* bar = w.find((uint32_t)val("bar.id"));
        w.setTemperature(*bar, 1000);
        const double hHot = w.hardnessAt(*bar, bar->pos);
        w.setTemperature(*bar, 20);
        check(hHot < 0.1 * w.hardnessAt(*bar, bar->pos), "steel at 1000 °C has less than 10 % of its hardness", num(hHot) + " MPa");
        double b0 = val("broken");
        run("new box steel 30 3 3 100 200 0 ; heat last -80 ; wait 2");
        check(val("broken") >= b0 + 1, "steel at -80 °C shatters when it falls 2 m");
        b0 = val("broken");
        run("new box steel 30 3 3 200 200 0 ; wait 2");
        check(val("broken") == b0, "the same steel bar at 20 °C survives the fall");
        // Holz: verkohlt, brennt und das Feuer breitet sich aus
        run("new box spruce 60 10 2 0 1 100 ; set board = last ; ignite $board ; wait 240");
        check(val("board.burning") >= 3 && val("board.charred") > 5 && val("fires") >= 1, "a lit board chars, burns and the fire spreads",
              num(val("board.burning")) + " spots, " + num(val("board.charred")) + " % charred");
        run("extinguish all ; wait 5");
        check(val("board.burning") == 0, "water puts the fire out");
        // Abbrennen: das Holz wird nach und nach kleiner, Kohle bleibt liegen
        {
            World w2(physics, nullptr);
            w2.setSimulating(true);
            TestHost h2(w2);
            auto v2 = [&](const std::string& expr) {
                std::string o = runScriptSync(h2, "print " + expr);
                size_t p = o.rfind("= ");
                return p == std::string::npos ? -1e30 : std::atof(o.c_str() + p + 2);
            };
            runScriptSync(h2, "new box spruce 60 10 2 0 1 0 ; set b = last ; ignite $b ; heat speed 10 ; sim 30");
            const double vol = v2("b.volume");
            check(vol > 100 && vol < 1150, "a burning board gets smaller bit by bit", num(vol) + " of 1200 cm3 left after 5 min");
            int coalSeen = 0;
            for (int i = 0; i < 40 && v2("parts") > 0; ++i) {
                runScriptSync(h2, "sim 10");
                int coal = 0;
                for (const auto& o : w2.objects()) coal += materials()[o->material].name == std::string("charcoal");
                coalSeen = std::max(coalSeen, coal);
            }
            int coalLeft = 0;
            for (const auto& o : w2.objects()) coalLeft += materials()[o->material].name == std::string("charcoal");
            check(coalSeen >= 3 && coalLeft >= 1 && w2.burningParts() == 0, "charcoal appears where the wood burned out and stays when the fire is out",
                  std::to_string(coalSeen) + " pieces at most, " + std::to_string(coalLeft) + " left");
        }
        run("air vacuum");
        check(runScriptSync(host, "ignite $board").find("oxygen") != std::string::npos, "no fire in a vacuum");
        run("air normal");
        // Schmelzen: Aluminium über dem Schmelzpunkt wird zu einer Pfütze
        const double p0 = val("parts");
        run("new box aluminium 4 4 4 300 2 0 ; set alu = last ; heat $alu 700 ; wait 3");
        bool puddle = false;
        for (const auto& o : w.objects()) puddle |= o->meltForm == MeltForm::Puddle && materials()[o->material].name == std::string("aluminium");
        check(puddle && val("parts") == p0 + 1, "aluminium at 700 °C melts and lands as a puddle");
        // Leim wird warm weich: hängender Block reißt ab
        run("new box oak 40 10 4 0 100 -100 ; fix last ; set beam = last ; new box oak 10 10 10 0 93 -100 ; set blk = last ; "
            "glue $beam $blk woodglue ; wait 1");
        const double j0 = val("joints");
        run("heat $beam 150 ; heat $blk 150 ; wait 1");
        check(val("joints") == j0 - 1 && val("blk.y") < 50, "wood glue lets go at 150 °C");
        // Speichern und Laden behält die Temperatur
        run("new box copper 10 2 2 0 1 -200 ; set cu = last ; heat $cu 400");
        const std::string file = (std::filesystem::temp_directory_path() / "werkstatt_heat_test.werkstatt").string();
        run("save \"" + file + "\" ; load \"" + file + "\"");
        double tCu = -1;
        for (const auto& o : w.objects())
            if (materials()[o->material].name == std::string("copper") && !o->heat.empty()) tCu = o->heatInfo.tMax;
        check(approx(tCu, 400, 1), "save and load keep the temperature", num(tCu) + " °C");
        std::filesystem::remove(file);
    }

    std::printf("== Water ==\n");
    {
        World w(physics, nullptr);
        w.setSimulating(true);
        TestHost host(w);
        auto run = [&](const std::string& line) { return runScriptSync(host, line); };
        auto val = [&](const std::string& expr) {
            std::string o = runScriptSync(host, "print " + expr);
            size_t p = o.rfind("= ");
            return p == std::string::npos ? -1e30 : std::atof(o.c_str() + p + 2);
        };
        run("water pool 400 300 100 0 0 0");  // Wasserspiegel 100 cm
        check(val("waters") == 1 && w.inWater({0, 0.5, 0}) && !w.inWater({0, 1.05, 0}) && !w.inWater({2.1, 0.5, 0}), "a pool holds water up to its level");
        // Holz schwimmt: Tiefe = Höhe · Dichte Holz / Dichte Wasser
        run("new box spruce 40 40 10 -100 105 -80 ; set wb = last ; wait 6");
        const double draft = 100.0 - val("wb.bottom");
        check(approx(draft, 10.0 * 470.0 / 998.0, 0.3) && approx(val("wb.submerged"), 47.1, 3), "a spruce block floats with 47 % under water",
              num(draft) + " cm deep, " + num(val("wb.submerged")) + " %");
        // schräg ins Wasser gesetzt: dreht sich flach (stabile Lage) und kommt zur Ruhe, statt endlos zu schaukeln
        run("new box spruce 60 20 10 100 105 120 ; set tilt = last ; rotation $tilt 70 0 0 ; wait 4");
        check(std::abs(std::sin(val("tilt.rx") * pi / 180)) < 0.04 && val("tilt.resting") == 1, "a block put in tilted turns flat and comes to rest",
              num(val("tilt.rx")) + "°");
        run("new box steel 5 5 5 100 105 -80 ; set ws = last ; wait 4");
        check(val("ws.bottom") < 0.5, "a steel cube sinks to the bottom", num(val("ws.bottom")) + " cm");
        // Stahlboot: Wanne aus 3 mm Blech verdrängt mehr Wasser als sie wiegt (18,4 kg -> 4,6 cm tief)
        run("new box steel 80 50 15 0 108 60 ; set boat = last ; pocket $boat top 0 0 794 494 147 ; wait 6");
        const double boatDraft = 100.0 - val("boat.bottom");
        check(approx(boatDraft, 4.6, 0.6) && val("boat.flooded") < 2, "a boat of 3 mm steel sheet floats",
              num(boatDraft) + " cm deep, " + num(val("boat.flooded")) + " % flooded");
        run("water fill $boat 100 ; wait 6");
        check(val("boat.bottom") < 1.0, "the same boat full of water sinks", num(val("boat.bottom")) + " cm");
        // Becher (ABS, schwerer als Wasser): leer schwimmt er, unter Wasser gedrückt läuft er voll und sinkt
        run("new cylinder plastic 8 10 -100 107 60 ; set cup = last ; new cylinder plastic 7 10 -100 108 60 ; subtract $cup last ; wait 4");
        check(val("cup.bottom") > 94 && val("cup.flooded") < 5, "an empty plastic cup floats upright",
              "bottom " + num(val("cup.bottom")) + " cm, " + num(val("cup.flooded")) + " % flooded");
        run("position $cup -100 60 60 ; wait 10");  // ABS ist kaum schwerer als Wasser: sinkt langsam
        check(val("cup.flooded") > 90 && val("cup.bottom") < 2, "pushed under water, the cup fills and sinks",
              num(val("cup.flooded")) + " % flooded, bottom " + num(val("cup.bottom")) + " cm");
        // Kopfüber unter Wasser: die Luft kann nicht nach oben entweichen, der Becher steigt auf (Taucherglocke)
        run("new cylinder plastic 8 10 100 60 60 ; set bell = last ; new cylinder plastic 7 10 100 61 60 ; subtract $bell last ; "
            "rotate $bell x 180 ; wait 5");
        check(val("bell.flooded") < 30 && val("bell.top") > 96, "a cup upside down under water keeps its air and comes up",
              num(val("bell.flooded")) + " % flooded, top " + num(val("bell.top")) + " cm");
        // Ein volles Glas kopfüber in der Luft läuft aus
        run("new cylinder plastic 8 10 300 200 300 ; set glass = last ; new cylinder plastic 7 10 300 201 300 ; subtract $glass last ; "
            "water fill $glass ; rotate $glass x 180 ; wait 2");
        check(val("glass.flooded") < 5, "a full glass upside down in the air runs empty", num(val("glass.flooded")) + " % left");
        // Abschrecken: glühender Stahl kühlt im Wasser viel schneller als an der Luft
        run("new box steel 20 2 2 -150 120 -80 ; set hot = last ; heat $hot 800 ; new box steel 20 2 2 300 200 -300 ; set dry = last ; "
            "fix $dry ; heat $dry 800 ; wait 20");
        check(val("hot.tempmax") < 120 && val("dry.tempmin") > 500, "red-hot steel is quenched in water (in the air it stays hot)",
              num(val("hot.tempmax")) + " °C in water, " + num(val("dry.tempmin")) + " °C in the air");
        // Wellen heben und senken, was schwimmt
        run("water waves 1 30");
        double lo = 1e9, hi = -1e9, fastest = 0;
        for (int i = 0; i < 30; ++i) {
            run("wait 0.1");
            lo = std::min(lo, val("wb.y"));
            hi = std::max(hi, val("wb.y"));
            for (auto& o : w.objects())
                if (o->body != kNoBody && !o->fixed) fastest = std::max(fastest, (double)glm::length(physics.linearVelocity(o->body)));
        }
        check(hi - lo > 6 && hi - lo < 40 && fastest < 5, "waves rock what floats (nothing flies off)",
              "rises and falls " + num(hi - lo) + " cm, fastest " + num(fastest) + " m/s");
        // Speichern, Laden, Rückgängig
        const std::string file = (std::filesystem::temp_directory_path() / "werkstatt_water_test.werkstatt").string();
        run("save \"" + file + "\" ; load \"" + file + "\"");
        check(w.waters().size() == 1 && approx(w.waters()[0].waves, 0.3, 1e-9) && approx(w.waters()[0].level(), 1.0, 1e-9),
              "save and load keep the water");
        std::filesystem::remove(file);
        run("water remove all");
        const double gone = val("waters");
        run("undo");
        check(gone == 0 && val("waters") == 1, "undo brings removed water back");
        // Meer: überall unter dem Spiegel ist Wasser
        run("water sea 50 ; new box oak 30 30 10 5000 80 5000 ; set far = last ; wait 5");
        check(val("far.inwater") == 1 && approx(50.0 - val("far.bottom"), 10.0 * 720.0 / 1025.0, 0.6), "the sea carries an oak block far away",
              num(50.0 - val("far.bottom")) + " cm deep");
        // Grund des Meers: darunter ist Erde, kein Wasser (wer gräbt, stößt nicht auf das Meer)
        check(w.inWater({3, 0.2, 3}) && !w.inWater({3, -1.0, 3}) && !w.inWater({3, -50.0, 3}), "below the sea bottom there is no water");
        run("water sea 50 bottom -300");
        check(w.inWater({3, -1.0, 3}) && !w.inWater({3, -3.2, 3}), "a deeper sea bottom holds water down to it");
        run("water sea 50 bottom 0");
        // Land im Meer: trocken, auch unter dem Meeresspiegel
        run("water land 2000 0 1000 1000");
        check(!w.inWater({20, 0.2, 0}) && !w.inWater({20, -2.0, 0}) && w.inWater({10, 0.2, 0}) && w.inWater({20, 0.2, 10}),
              "land in the sea is dry, also below the sea level");
        run("new box oak 30 30 10 2000 80 0 ; set dry = last ; wait 3");
        check(val("dry.inwater") == 0 && val("dry.bottom") < 1, "a block on the land does not float", num(val("dry.bottom")) + " cm");
        run("new box oak 30 30 10 1450 80 0 ; set coast = last ; wait 3");
        check(val("coast.inwater") == 1 && approx(50.0 - val("coast.bottom"), 10.0 * 720.0 / 1025.0, 0.6), "next to the coast it floats",
              num(50.0 - val("coast.bottom")) + " cm deep");
        const std::string seaFile = (std::filesystem::temp_directory_path() / "werkstatt_sea_test.werkstatt").string();
        run("save \"" + seaFile + "\" ; load \"" + seaFile + "\"");
        check(!w.inWater({20, 0.2, 0}) && w.inWater({10, 0.2, 0}), "save and load keep the land in the sea");
        std::filesystem::remove(seaFile);
        run("water ground reset");
        const bool flooded = w.inWater({20, 0.2, 0});
        run("undo");
        check(flooded && !w.inWater({20, 0.2, 0}), "undo brings the land back");
    }

    std::printf("== River ==\n");
    {
        World w(physics, nullptr);
        w.setSimulating(true);
        TestHost host(w);
        auto run = [&](const std::string& line) { return runScriptSync(host, line); };
        auto val = [&](const std::string& expr) {
            std::string o = runScriptSync(host, "print " + expr);
            size_t p = o.rfind("= ");
            return p == std::string::npos ? -1e30 : std::atof(o.c_str() + p + 2);
        };
        // Kanal 6 x 1,2 m, 40 cm tief, 1 m/s nach +x
        run("water pool 600 120 40 0 0 0 ; water flow all 1 0 ; wait 0.2");
        const FlowGrid* g = w.flowGrid(w.waters()[0].id);
        auto at = [&](double x, double z) {
            glm::dvec2 u(0);
            double r = 0, f = 0;
            if (g = w.flowGrid(w.waters()[0].id); g) flowSample(*g, x, z, &u, &r, &f);
            return std::make_tuple(u, r, f);
        };
        const glm::dvec2 free0 = std::get<0>(at(0.0, 0.0));
        check(g && approx(free0.x, 1.0, 0.02) && approx(free0.y, 0.0, 0.02), "a channel without obstacles flows evenly", num(free0.x) + " m/s");
        // Umbauen: ein Pfeiler mitten im Fluss. Davor staut es sich (fast stehendes Wasser), daneben wird es schneller
        run("new cylinder concrete 30 60 0 30 0 ; fix last ; wait 1");
        // Umströmung eines Zylinders (Potentialströmung): vorn u = U·(1 − R²/r²), seitlich U·(1 + R²/r²)
        const auto [uFront, rFront, fFront] = at(-0.17, 0.0);
        const auto [uSide, rSide, fSide] = at(0.0, 0.25);
        check(glm::length(uFront) < 0.4 && glm::length(uSide) > 1.2, "after building a pier the water flows around it",
              "in front " + num(glm::length(uFront)) + " m/s, beside " + num(glm::length(uSide)) + " m/s (theory 0.22 / 1.36)");
        check(rFront > 0.02 && rSide < rFront && fFront > 0.2, "in front of the pier the water piles up and foams",
              "rises " + num(rFront * 100) + " cm, foam " + num(fFront * 100) + " %");
        // Treibgut fährt mit und um den Pfeiler herum bis ans Ende
        run("new box spruce 30 15 5 -250 41 -30 ; set drift = last ; wait 4");
        check(val("drift.x") > 0 && val("drift.inwater") == 1, "a floating board drifts downstream past the pier",
              "now at x = " + num(val("drift.x")) + " cm");
        // Pfeiler weg: das Wasser fließt wieder gerade
        run("delete 1 ; wait 1");
        check(approx(glm::length(std::get<0>(at(-0.22, 0.0))), 1.0, 0.05), "after removing the pier the river flows straight again");
    }

    std::printf("== Terrain ==\n");
    {
        // Rauschen: nur Seed und Ort zählen
        double a = 0, b = 0, maxAbs = 0;
        bool same = true;
        for (int i = 0; i < 2000; ++i) {
            const double x = i * 0.731 - 300.0, z = i * 1.917 + 50.0;
            a = noise::fbm(42, x, z, 4);
            b = noise::fbm(42, x, z, 4);
            same &= a == b;
            maxAbs = std::max(maxAbs, std::abs(a));
        }
        check(same && maxAbs <= 1.0 && maxAbs > 0.3 && noise::fbm(42, 3.3, 7.7, 4) != noise::fbm(43, 3.3, 7.7, 4),
              "noise depends only on seed and place", "largest " + num(maxAbs));

        auto cfg = std::make_shared<GenConfig>(builtinGenConfig());
        const int plains = cfg->findBiome("plains"), hills = cfg->findBiome("hills"), desert = cfg->findBiome("desert");
        const int forest = cfg->findBiome("forest");
        WorldGen g1(4711, cfg, -1), g2(4711, cfg, -1), g3(4712, cfg, -1);
        int equal = 0, differ = 0, plainsN = 0, hillsN = 0, desertN = 0, forestN = 0, wrong = 0;
        for (int i = 0; i < 900; ++i) {
            const double x = (i % 30) * 157.0 - 2300.0, z = (i / 30) * 163.0 - 2300.0;
            GenSample s1, s2, s3;
            g1.sample(x, z, s1);
            g2.sample(x, z, s2);
            g3.sample(x, z, s3);
            equal += s1.height == s2.height && s1.biome == s2.biome;
            differ += s1.height != s3.height;
            (s1.biome == plains ? plainsN : s1.biome == hills ? hillsN : s1.biome == forest ? forestN : desertN)++;
            // biomes.json: feucht -> wenig Zerklüftung Ebene (sehr feucht: Wald), viel Zerklüftung Hügel; heiß und trocken -> Wüste
            const Climate c = g1.climate(x, z);
            const double e = c.v[kErosion], hum = c.v[kHumidity], tmp = c.v[kTemperature];
            if (hum > 0.37 && hum < 0.6 && e < 0.48 && s1.biome != plains) ++wrong;
            if (hum > 0.64 && e < 0.48 && s1.biome != forest) ++wrong;
            if (hum > 0.37 && e > 0.52 && s1.biome != hills) ++wrong;
            if (hum < 0.33 && tmp > 0.57 && s1.biome != desert) ++wrong;
            if (s1.biome == desert && hum > 0.37) ++wrong;
        }
        check(plains >= 0 && hills >= 0 && desert >= 0 && equal == 900 && differ > 850, "same seed, same terrain and biomes; other seed, other terrain",
              std::to_string(equal) + " equal, " + std::to_string(differ) + " different");
        check(wrong == 0 && plainsN > 30 && hillsN > 60 && desertN > 20 && forestN > 30,
              "biomes sit where their climate fits (plains, forest, hills by humidity and erosion; desert hot and dry)",
              std::to_string(plainsN) + " plains, " + std::to_string(forestN) + " forest, " + std::to_string(hillsN) + " hills, " + std::to_string(desertN) + " desert, " +
                  std::to_string(wrong) + " wrong");
        WorldGen only(4711, cfg, hills);
        int onlyHills = 0;
        for (int i = 0; i < 200; ++i) {
            GenSample s;
            only.sample(i * 97.0 - 9000.0, i * 53.0, s);
            onlyHills += s.biome == hills;
        }
        check(onlyHills == 200, "a world with one biome has only that biome");
        // Grenze ohne Stufe: entlang einer Linie über mehrere Grenzen 0,5 m weit gehen
        double steepest = 0;
        int borders = 0, last = -1;
        for (int i = 0; i < 6000; ++i) {
            GenSample s0, s1;
            g1.sample(i * 0.5, 211.0, s0);
            g1.sample(i * 0.5 + 0.5, 211.0, s1);
            steepest = std::max(steepest, std::abs(s1.height - s0.height));
            if (last >= 0 && s0.biome != last) ++borders;
            last = s0.biome;
        }
        check(borders > 0 && steepest < 0.6, "biome borders blend smoothly (no cliffs)",
              std::to_string(borders) + " borders crossed, steepest step " + num(steepest * 100) + " cm per 50 cm");

        // Flüsse: nicht selten, nicht überall; nie in der Wüste; außerhalb der Flussbetten bleibt der Boden über dem Wasser
        {
            const GenConfig::Rivers& rv = cfg->rivers;
            int crossings = 0, lowDry = 0, desertSamples = 0, desertRiver = 0;
            bool inRiver = false;
            double lowestDry = 1e9;
            for (const double lineZ : {-6234.0, -2234.0, 1234.0, 5234.0})  // vier Linien je 20 km, Schritte von 0,5 m
                for (int i = 0; i < 40000; ++i) {
                    GenSample s;
                    g1.sample(-10000.0 + i * 0.5, lineZ, s);
                    const bool water = s.wet && s.height < rv.level;
                    if (water && !inRiver) ++crossings;
                    inRiver = water;
                    if (!s.wet) {
                        lowestDry = std::min(lowestDry, s.height);
                        lowDry += s.height < rv.level + rv.floor - 0.01;
                    }
                }
            for (int i = 0; i < 6400; ++i) {
                GenSample s;
                g1.sample((i % 80) * 75.0 - 3000.0, (i / 80) * 75.0 - 3000.0, s);
                if (s.biome != desert) continue;
                ++desertSamples;
                desertRiver += s.river.strength > 0 || (s.wet && s.height < rv.level);
            }
            check(rv.enabled && crossings >= 8 && crossings <= 60, "rivers: not rare, not everywhere",
                  std::to_string(crossings) + " rivers crossed on 80 km (about every " + num(80.0 / std::max(crossings, 1)) + " km)");
            check(desertSamples > 100 && desertRiver == 0, "no rivers in the desert",
                  std::to_string(desertSamples) + " desert spots, " + std::to_string(desertRiver) + " with a river");
            check(lowDry == 0 && lowestDry >= rv.level + rv.floor - 0.01, "outside the river beds the ground stays above the water",
                  "lowest dry ground " + num((lowestDry - rv.level) * 100) + " cm above the river level");
            GenConfig old;
            std::string perr;
            const bool parsed = parseGenConfig("{\"biomes\": [{\"id\": \"flat\", \"height\": {\"base\": -3, \"amplitude\": 0}}]}", old, &perr);
            GenSample s;
            WorldGen(1, std::make_shared<GenConfig>(old), -1).sample(10, 10, s);
            check(parsed && !old.rivers.enabled && approx(s.height, -3.0, 1e-9) && !s.wet,
                  "worlds made before rivers stay as they were (no rivers, ground not lifted)", perr);
        }

        // Gelände in einer Welt: Physik als Höhenfeld, Strahl, Änderungen
        World w(physics, nullptr);
        w.setSimulating(true);
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "werkstatt_terrain_test";
        std::filesystem::remove_all(dir);
        w.setTerrain(std::make_shared<WorldGen>(4711, cfg, -1), dir);
        Terrain& tr = *w.terrain();
        TestHost host(w);
        auto run = [&](const std::string& line) { return runScriptSync(host, line); };
        auto val = [&](const std::string& expr) {
            std::string o = runScriptSync(host, "print " + expr);
            size_t p = o.rfind("= ");
            return p == std::string::npos ? -1e30 : std::atof(o.c_str() + p + 2);
        };
        {  // Nächste Stelle eines Bioms (tp, terrain goto): ganz das Biom, auf Kreisen weiter innen liegt keine
            const glm::dvec2 from(37.0, -51.0);
            const int other = g1.classify(g1.climate(from.x, from.y), nullptr) == plains ? hills : plains;
            double sc[kMaxBiomes];
            auto pure = [&](const glm::dvec2& p) { return g1.classify(g1.climate(p.x, p.y), sc) == other && sc[other] >= cfg->blend; };
            glm::dvec2 f(0), again(0);
            const bool found = tr.findBiome(other, from, 20000.0, f);
            const double d = glm::length(f - from);
            int nearer = 0;
            for (int k = 0; k < 720; ++k)
                for (const double r : {d * 0.5, d - 1.0}) {
                    const double a = k * 3.14159265358979 / 360.0;
                    if (pure(from + r * glm::dvec2(std::cos(a), std::sin(a)))) ++nearer;
                }
            check(found && d > 2.0 && pure(f) && nearer == 0 && tr.findBiome(other, f, 100.0, again) && again == f,
                  "the nearest spot of a biome is found (fully that biome, nothing nearer)",
                  cfg->biomes[(size_t)other].id + " " + num(d) + " m away, " + std::to_string(nearer) + " nearer spots");
        }
        {  // Fluss in der Welt: Wasser im Flussbett (Gewässer des Geländes), ein Holzball schwimmt und treibt mit
            const double L = tr.riverLevel();
            glm::dvec2 f(0), mid(0);
            const bool found = tr.findRiver(glm::dvec2(0, 0), 5000.0, f);
            double deepest = 1e9;  // Mitte: tiefste Stelle in der Nähe
            for (int b = -16; b <= 16 && found; ++b)
                for (int a = -16; a <= 16; ++a) {
                    const glm::dvec2 q = f + glm::dvec2(a, b) * 0.5;
                    const double d = tr.waterBottom(q.x, q.y);
                    if (d < deepest) {
                        deepest = d;
                        mid = q;
                    }
                }
            double surface = 0;
            const WaterBody* wb = found ? w.waterColumn(glm::dvec3(mid.x, L - 0.1, mid.y), surface) : nullptr;
            check(found && wb && wb->generated() && deepest < L - 0.5 && approx(surface, L, 0.05),
                  "rivers hold water (the water body of the terrain)", "found " + num(glm::length(f)) + " m away, " + num((L - deepest) * 100) + " cm deep");
            const glm::dvec3 flow = tr.riverFlow(mid.x, mid.y);
            run("new sphere spruce 30 " + num(mid.x * 100) + " " + num((L + 0.3) * 100) + " " + num(mid.y * 100) + " ; set rb = last");
            w.setWaterFocus(glm::dvec3(mid.x, L, mid.y));
            run("sim 6");
            const glm::dvec3 rp(val("rb.x") / 100, val("rb.y") / 100, val("rb.z") / 100);
            const glm::dvec2 dir = glm::length(flow) > 0 ? glm::normalize(glm::dvec2(flow.x, flow.z)) : glm::dvec2(0);
            const double along = glm::dot(glm::dvec2(rp.x - mid.x, rp.z - mid.y), dir);
            check(found && rp.y > L - 0.15 && rp.y < L + 0.15 && along > 0.5, "a wooden ball floats on a river and drifts downstream",
                  "center " + num((rp.y - L) * 100) + " cm above the water, " + num(along) + " m downstream (current " + num(glm::length(flow)) + " m/s)");
            run("delete rb");
        }
        // Hang suchen (Steigung 10..20°)
        glm::dvec2 slope(0);
        for (int i = 0; i < 4000 && slope == glm::dvec2(0); ++i) {
            const double x = (i % 64) * 9.0, z = (i / 64) * 9.0;
            glm::dvec3 n;
            tr.surfaceHeight(x, z, &n);
            if (n.y < std::cos(10.0 * 3.14159265 / 180) && n.y > std::cos(20.0 * 3.14159265 / 180)) slope = glm::dvec2(x, z);
        }
        const double top = tr.surfaceHeight(slope.x, slope.y) + 1.0;
        run("new box oak 30 30 30 " + num(slope.x * 100) + " " + num(top * 100) + " " + num(slope.y * 100) + " ; set b = last ; sim 4");
        const glm::dvec3 bp(val("b.x") / 100, val("b.y") / 100, val("b.z") / 100);
        const double under = tr.surfaceHeight(bp.x, bp.z);
        check(slope != glm::dvec2(0) && val("b.v") < 0.05 && bp.y - under > 0.1 && bp.y - under < 0.26,
              "a block on a slope comes to rest on the terrain (height field)", "center " + num((bp.y - under) * 100) + " cm above the ground");
        // Kugel auf den Hang: rollt bergab und bleibt dabei auf der Oberfläche
        run("delete b ; new sphere oak 40 " + num(slope.x * 100) + " " + num((top + 0.5) * 100) + " " + num(slope.y * 100) + " ; set ball = last");
        const double startY = val("ball.y") / 100;
        run("sim 3");
        const glm::dvec3 kp(val("ball.x") / 100, val("ball.y") / 100, val("ball.z") / 100);
        const double gap = kp.y - 0.2 - tr.surfaceHeight(kp.x, kp.z);  // Unterkante der Kugel über dem Gelände (auf dem Hang etwas mehr)
        check(kp.y < startY - 1.0 && gap > -0.03 && gap < 0.08, "a ball on a hill rolls down and stays on the surface",
              "rolled " + num(glm::length(glm::dvec2(kp.x - slope.x, kp.z - slope.y))) + " m, " + num(startY - kp.y) + " m down, gap " +
                  num(gap * 100) + " cm");
        RayHit hit;
        const bool hitOk = w.raycast(glm::dvec3(slope.x + 3, top + 50, slope.y + 2), glm::dvec3(0, -1, 0), hit);
        check(hitOk && !hit.obj && approx(hit.point.y, tr.surfaceHeight(slope.x + 3, slope.y + 2), 0.002), "a ray finds the terrain surface");
        const double h0 = tr.surfaceHeight(40.0, 40.0);
        run("terrain raise 4000 4000 500 200");
        const double h1 = tr.surfaceHeight(40.0, 40.0), side = tr.surfaceHeight(46.0, 40.0) - tr.latticeHeight(92, 80);
        check(approx(h1 - h0, 2.0, 0.01) && approx(side, 0.0, 1e-9), "terrain raise lifts the ground (2 m in the middle, nothing outside)",
              num((h1 - h0) * 100) + " cm");
        std::string err;
        check(tr.save(&err) && std::filesystem::exists(dir / "r.0.0.bin"), "changed chunks are saved in a region file", err);
        {
            Terrain again(physics, nullptr, std::make_shared<WorldGen>(4711, cfg, -1), dir);
            check(approx(again.surfaceHeight(40.0, 40.0), h1, 1e-6) && again.editedChunks() == tr.editedChunks(),
                  "a reloaded world has the same changes", std::to_string(again.editedChunks()) + " chunk(s)");
        }
        run("terrain reset");
        check(approx(tr.surfaceHeight(40.0, 40.0), h0, 1e-6) && tr.save(&err) && !std::filesystem::exists(dir / "r.0.0.bin"),
              "terrain reset brings back the generated ground (and removes the region file)");
        std::filesystem::remove_all(dir);
    }

    std::printf("== Trees ==\n");
    {
        auto tcfg = std::make_shared<TreeConfig>(builtinTreeConfig());
        auto lib = std::make_shared<TreeLibrary>(nullptr, tcfg);
        const int oakBig = tcfg->find("oak_big"), oakYoung = tcfg->find("oak_young"), spruceBig = tcfg->find("spruce_big");
        const double oakRho = materials()[(size_t)materialIndex("oak")].density;
        const TreeModel& mb = lib->model(oakBig, 0);
        const TreeModel& my = lib->model(oakYoung, 0);
        const TreeModel again = buildTreeModel(tcfg->types[(size_t)oakBig], noise::subSeed(0x7EE5, "tree/oak_big/0"));
        check(oakBig >= 0 && oakYoung >= 0 && spruceBig >= 0 && mb.volume * oakRho > 2000 && my.volume * oakRho < 300 && mb.height > 15 &&
                  again.volume == mb.volume && again.leaves.size() == mb.leaves.size(),
              "tree types: big ones worth felling, small ones for small houses, same form every time",
              "big oak " + num(mb.volume * oakRho) + " kg, " + num(mb.height) + " m; young oak " + num(my.volume * oakRho) + " kg");

        // Wald aus dem Seed: dicht im Wald, fast keine in der Ebene, keine in der Wüste, keiner im Fluss
        auto bcfg = std::make_shared<GenConfig>(builtinGenConfig());
        auto gen = std::make_shared<WorldGen>(4711, bcfg, -1);
        Forest f1(gen, lib), f2(std::make_shared<WorldGen>(4711, bcfg, -1), lib);
        const int forestB = bcfg->findBiome("forest"), plainsB = bcfg->findBiome("plains"), desertB = bcfg->findBiome("desert");
        std::vector<int> cells(bcfg->biomes.size(), 0), trees(bcfg->biomes.size(), 0);
        int same = 0, total = 0, wet = 0;
        const double s = tcfg->spacing;
        for (int j = 0; j < 160; ++j)
            for (int i = 0; i < 160; ++i) {
                const int64_t cx = (int64_t)i * 3 - 240, cz = (int64_t)j * 3 - 240;  // jedes dritte Feld, 2,9 km
                GenSample g;
                gen->sample((cx + 0.5) * s, (cz + 0.5) * s, g);
                ++cells[(size_t)g.biome];
                TreeSpot a, b;
                const bool ha = f1.spotOf(cx, cz, a), hb = f2.spotOf(cx, cz, b);
                same += ha == hb && (!ha || (a.pos == b.pos && a.type == b.type && a.variant == b.variant && a.scale == b.scale));
                ++total;
                if (!ha) continue;
                ++trees[(size_t)g.biome];
                GenSample at;
                gen->sample(a.pos.x, a.pos.z, at);
                wet += at.wet && at.height < bcfg->rivers.level;
            }
        auto perHa = [&](int b) { return cells[(size_t)b] ? trees[(size_t)b] / (cells[(size_t)b] * s * s) * 10000.0 : -1.0; };
        check(same == total && perHa(forestB) > 80 && perHa(forestB) < 220 && perHa(plainsB) < 15 && trees[(size_t)desertB] == 0 &&
                  cells[(size_t)desertB] > 50 && wet == 0,
              "trees grow by biome from the seed (dense forest, few in the plains, none in the desert or in rivers)",
              "per hectare: forest " + num(perHa(forestB)) + ", plains " + num(perHa(plainsB)) + ", desert " + std::to_string(trees[(size_t)desertB]) +
                  "; " + std::to_string(same) + "/" + std::to_string(total) + " cells the same with the same seed");

        // In einer Welt: nahe Bäume werden feste Teile mit Krone und Wurzel
        World w(physics, nullptr);
        w.setSimulating(true);
        w.setTrees(tcfg);
        w.setTerrain(std::make_shared<WorldGen>(4711, bcfg, -1), {});
        TestHost host(w);
        auto run = [&](const std::string& line) { return runScriptSync(host, line); };
        TreeSpot spot;
        const bool found = w.forest()->nearest(glm::dvec2(0, 0), 5000.0, spruceBig, spot);
        w.updateTrees({spot.pos}, 10000);
        int parts = 0, good = 0;
        Object* tree = nullptr;
        for (const auto& op : w.objects()) {
            if (!op->treeKey) continue;
            ++parts;
            good += op->fixed && op->rooted && op->crown && w.untouchedTree(*op);
            if (op->treeKey == spot.key) tree = op.get();
        }
        check(found && tree && parts > 5 && good == parts && w.treeHidden(spot.key), "trees near a player become fixed parts (crown, root)",
              std::to_string(parts) + " trees within " + num(tcfg->solidDistance) + " m");

        // Fällen: Schnitt 1 m über dem Boden, der Stumpf bleibt fest, der Stamm mit der Krone fällt
        const uint32_t treeId = tree ? tree->id : 0;
        const double treeMass = tree ? tree->mass : 0;
        run("cut " + std::to_string(treeId) + " y " + num((0.4 * spot.scale + 1.0) * 100));
        run("sim 10");
        const Object *stump = nullptr, *trunk = nullptr;
        for (const auto& op : w.objects()) {
            if (op->treeType.empty() || glm::length(glm::dvec2(op->pos.x - spot.pos.x, op->pos.z - spot.pos.z)) > 35.0) continue;
            if (op->rooted && op->fixed && op->wMax.y < spot.pos.y + 2.0) stump = op.get();
            if (op->crown && !op->fixed && op->mass > treeMass * 0.3) trunk = op.get();
        }
        check(stump && trunk && trunk->wMax.y - trunk->wMin.y < 6.0 && !w.untouchedTree(*trunk),
              "a cut tree falls: the stump stays rooted, the trunk with its crown lies on the ground",
              trunk ? "trunk " + num(trunk->mass) + " kg, " + num(trunk->wMax.y - trunk->wMin.y) + " m high now" : std::string("no falling trunk"));

        // Weit weg: unveränderte Bäume verschwinden wieder, der gefällte bleibt; Speichern ohne die unveränderten
        w.updateTrees({spot.pos + glm::dvec3(3000, 0, 0)}, 0);
        int left = 0, cut = 0;
        for (const auto& op : w.objects()) {
            if (op->treeType.empty()) continue;
            ++left;
            cut += op->treeKey == spot.key;
        }
        // einen anderen Baum ganz entfernen: wächst nicht nach
        w.updateTrees({spot.pos}, 10000);
        uint32_t other = 0;
        uint64_t otherKey = 0;
        for (const auto& op : w.objects())
            if (op->treeKey && op->treeKey != spot.key && w.untouchedTree(*op)) {
                other = op->id;
                otherKey = op->treeKey;
            }
        run("delete " + std::to_string(other));
        const std::string file = (std::filesystem::temp_directory_path() / "werkstatt_trees_test.werkstatt").string();
        std::string err;
        const bool saved = w.save(std::filesystem::u8path(file), &err);
        World w2(physics, nullptr);
        w2.setTrees(tcfg);
        w2.setTerrain(std::make_shared<WorldGen>(4711, bcfg, -1), {});
        const bool loaded = w2.load(std::filesystem::u8path(file), &err);
        w2.updateTrees({}, 0);  // vorhandene Baum-Teile zählen
        int loadedTrees = 0;
        for (const auto& op : w2.objects()) loadedTrees += !op->treeType.empty();
        check(cut == 1 && saved && loaded && loadedTrees == left && w2.treeHidden(otherKey) && w2.treeHidden(spot.key),
              "far away untouched trees go away again; cut and removed trees are saved",
              std::to_string(left) + " tree part(s) kept, " + std::to_string(loadedTrees) + " loaded, " + err);
        std::filesystem::remove(file);

        // flache Welt: Baumschule
        World flat(physics, nullptr);
        flat.setTrees(tcfg);
        flat.treeScene();
        int nursery = 0;
        for (const auto& op : flat.objects()) nursery += op->fixed && op->crown && !op->treeKey;
        check(nursery == (int)tcfg->types.size() * 3, "the tree nursery shows every type in every form", std::to_string(nursery) + " trees");
    }

    std::printf("== Stones ==\n");
    {
        auto bcfg = std::make_shared<GenConfig>(builtinGenConfig());
        const int hills = bcfg->findBiome("hills");
        World w(physics, nullptr), w2(physics, nullptr);
        w.setSimulating(true);
        w.setTerrain(std::make_shared<WorldGen>(4711, bcfg, hills), {});
        w2.setTerrain(std::make_shared<WorldGen>(4711, bcfg, hills), {});
        std::vector<StoneSpot> spots, spots2;
        w.stones()->around(glm::dvec2(0, 0), 60.0, spots);
        w2.stones()->around(glm::dvec2(0, 0), 60.0, spots2);
        bool same = spots.size() == spots2.size();
        for (size_t i = 0; same && i < spots.size(); ++i) same = spots[i].key == spots2[i].key && spots[i].pos == spots2[i].pos && spots[i].size == spots2[i].size;
        int small = 0, big = 0;
        StoneSpot bigSpot, smallSpot;
        for (const StoneSpot& s : spots) {
            if (w.stones()->big(s)) {
                if (big++ == 0) bigSpot = s;
            } else if (small++ == 0) {
                smallSpot = s;
            }
        }
        check(same && small > 2 * big && big > 0, "stones lie by biome from the seed: many small, few big",
              std::to_string(small) + " small, " + std::to_string(big) + " big within 60 m in the hills");

        // groß und nah: festes Teil (kostet nichts); kleine bleiben Bild; anfassen macht den großen beweglich
        w.updateStones({bigSpot.pos}, 10000);
        Object* rock = nullptr;
        int smallParts = 0;
        for (const auto& op : w.objects())
            if (World::isGeneratorStone(*op)) {
                if (op->treeKey == bigSpot.key) rock = op.get();
                smallParts += op->wMax.y - op->wMin.y < 0.15;
            }
        const bool fixedFirst = rock && rock->fixed && w.untouchedTree(*rock) && w.stoneHidden(bigSpot.key);
        if (rock) w.wakeStone(*rock);
        check(fixedFirst && rock && !rock->fixed && !w.untouchedTree(*rock) && smallParts == 0,
              "big stones near a player are fixed parts, small ones only pictures; touching makes a big one movable",
              rock ? num(rock->mass) + " kg rock" : "no rock part");

        // klein: von oben anschauen und aufheben
        StoneSpot hit;
        double t = 0;
        const bool picked = w.pickStone(smallSpot.pos + glm::dvec3(0, 1.0, 0), glm::dvec3(0, -1, 0), 3.0, hit, t);
        Object* pebble = picked ? w.takeStone(hit) : nullptr;
        check(pebble && !pebble->fixed && w.stoneHidden(hit.key) && pebble->mass < 40.0 && !w.takeStone(hit),
              "a small stone becomes a movable part when picked up (only once)", pebble ? num(pebble->mass) + " kg" : "nothing hit");
    }

    std::printf("== Material file ==\n");
    {
        loadBuiltinMaterials();
        const std::string text = materialsText();
        const size_t count = materials().size();
        const Material steel = materials()[materialIndex("steel")];
        check(count >= 11 && materialFile().empty() && findMaterialIndex("stahl") == findMaterialIndex("steel"),
              "built-in materials load, aliases (old German names) work", std::to_string(count) + " materials");
        std::string report;
        const bool again = loadMaterialText(text, "saved", &report);
        const Material& s2 = materials()[materialIndex("steel")];
        float mu = 0, muK = 0;
        frictionPair(materialIndex("steel"), materialIndex("steel"), mu, muK);
        check(again && report.empty() && materials().size() == count && s2.density == steel.density && s2.heat.meltC == steel.heat.meltC &&
                  s2.temper == Temper::Steel && s2.cls == MaterialClass::Metal && approx(mu, 0.74, 1e-6),
              "saving and reading the material file gives the same values", report);
        check(!loadMaterialText("[material x]\ndensity = abc\n[material y\n", "broken", &report) && materials().size() == count,
              "a broken material file is rejected, the old list stays", report.substr(0, report.find('\n')));
        loadMaterialText("[material soft]\nclass = metal\ndensity = 1000\nwobble = 3\n", "partial", &report);
        check(materials().size() == 1 && report.find("unknown key 'wobble'") != std::string::npos && materials()[0].heat.meltC <= 0,
              "unknown keys are reported, missing values take the defaults", report.substr(0, report.find('\n')));
        // JSON: Werte, null = keins, Fehler mit Zeilenangabe
        const bool jsonOk = loadMaterialText("{\"materials\": [{\"name\": \"glass\", \"class\": \"stone\", \"density\": 2500,\n"
                                             "  \"color\": [0.8, 0.9, 1], \"melts_at\": 1400, \"ignites_at\": null, \"aliases\": [\"glas\"]}],\n"
                                             " \"pairs\": [{\"a\": \"glass\", \"b\": \"glass\", \"friction\": [0.9, 0.4]}]}",
                                             "json", &report);
        float gs = 0, gk = 0;
        if (jsonOk) frictionPair(0, 0, gs, gk);
        check(jsonOk && materials().size() == 1 && findMaterialIndex("glas") == 0 && materials()[0].density == 2500 &&
                  materials()[0].heat.meltC == 1400 && !combustible(0) && approx(gs, 0.9, 1e-6),
              "the JSON material file is read (lists, null, aliases, pairs)", report);
        check(!loadMaterialText("{\"materials\": [\n  {\"name\": \"x\" \"density\": 1}\n]}", "broken.json", &report) && materials().size() == 1 &&
                  report.find("line 2") != std::string::npos,
              "a broken JSON file names the line and keeps the old list", report.substr(0, report.find('\n')));
        loadBuiltinMaterials();

        World w(physics, nullptr);
        w.setSimulating(true);
        TestHost host(w);
        auto val = [&](const std::string& expr) {
            std::string o = runScriptSync(host, "print " + expr);
            size_t p = o.rfind("= ");
            return p == std::string::npos ? -1e30 : std::atof(o.c_str() + p + 2);
        };
        runScriptSync(host, "materials new pine spruce ; materials set pine density 520 ; materials set pine ignites_at 280");
        runScriptSync(host, "new box pine 10 10 10 0 5 0 ; set p = last");
        check(approx(val("p.mass"), 0.52, 1e-6) && runScriptSync(host, "print p.material").find("pine") != std::string::npos,
              "a new material can be made by command and used right away", num(val("p.mass")) + " kg");
        runScriptSync(host, "materials set pine density 600");
        check(approx(val("p.mass"), 0.6, 1e-6), "changing the density updates existing parts", num(val("p.mass")) + " kg");
        // neu laden ohne "pine": das Teil bekommt das erste Material, die anderen behalten ihres über den Namen
        runScriptSync(host, "new box steel 5 5 5 50 2.5 0 ; set s = last");
        const std::string file = (std::filesystem::temp_directory_path() / "werkstatt_materials_test.txt").string();
        {
            std::ofstream f(file);
            f << "[material concrete]\ndensity = 2400\n[material steel]\nclass = metal\ndensity = 7850\n";
        }
        runScriptSync(host, "materials reload \"" + file + "\"");
        check(materials().size() == 2 && runScriptSync(host, "print s.material").find("steel") != std::string::npos,
              "reloading keeps each part's material by name", runScriptSync(host, "print s.material"));
        std::filesystem::remove(file);
        loadBuiltinMaterials();
    }

    std::printf("== Player ==\n");
    {
        World w(physics, nullptr);
        w.setSimulating(true);
        const float dt = 1.0f / 60.0f;
        // läuft (oder steht) eine Weile und gibt die größte Fußhöhe zurück
        auto walk = [&](glm::vec3 wish, double seconds, const std::function<void(int)>& each = {}) {
            double top = -1e9;
            for (int i = 0; i < (int)(seconds / dt); ++i) {
                if (each) each(i);
                w.step(dt, [](float) {});
                physics.updateCharacter(dt, wish, false, kNoBody);
                top = std::max(top, physics.characterPosition().y);
            }
            return top;
        };
        // leichtes Brett (8 kg, 5 cm) und höherer Kasten (30 cm, Stufe) quer im Weg
        w.addBox(materialIndex("spruce"), {0.6, 0.05, 0.6}, {1.5, 0.025, 0});
        w.addBox(materialIndex("spruce"), {0.5, 0.30, 0.5}, {3.5, 0.15, 0});
        w.simulateSeconds(1.0);
        physics.createCharacter({0, 0, 0});
        double top = walk({4, 0, 0}, 0.55);
        check(top < 0.05 + 0.03, "player walks over a light board without being thrown up", "highest feet " + num(top * 100) + " cm");
        top = walk({4, 0, 0}, 0.35);
        double on = physics.characterPosition().y;
        top = std::max(top, walk({0, 0, 0}, 1.5));
        check(approx(on, 0.30, 0.02) && top < 0.30 + 0.03,"player steps onto a 30 cm box and stays on it",
              "on " + num(on * 100) + " cm, highest " + num(top * 100) + " cm");

        // das Brett unter den Füßen wird kräftig nach oben gestoßen (6 m/s): 8 kg können 75 kg kaum anheben
        physics.setCharacterPosition({1.5, 0.06, 0});
        walk({0, 0, 0}, 1.0);
        Object* board = nullptr;
        for (auto& o : w.objects())
            if (std::abs(o->pos.x - 1.5) < 0.3) board = o.get();
        double y0 = physics.characterPosition().y;
        top = walk({0, 0, 0}, 2.0, [&](int i) {
            if (board && i < 3) physics.setVelocity(board->body, glm::vec3(0, 6, 0), glm::vec3(0));
        });
        check(board && top - y0 < 0.4, "a light board jerked upward does not launch the player", "rises " + num((top - y0) * 100) + " cm");
        physics.destroyCharacter();
    }
    {
        // Spielfigur: eingebettetes Modell, Größe, Blickrichtung, Abläufe
        AvatarModel fig;
        std::string err;
        check(fig.loadBuiltin(1.8f, &err), "player model loads", err);
        const int idle = fig.clip("Idle"), walk = fig.clip("Walk"), run = fig.clip("Run");
        check(idle >= 0 && walk >= 0 && run >= 0, "player model has idle, walk and run");
        std::vector<solid::Vertex> v;
        AvatarModel::Layer still{idle, 0.3f, 1.0f};
        fig.pose(&still, 1, nullptr, 0, v);
        glm::vec3 lo(1e9f), hi(-1e9f), eye(0), all(0);
        int eyes = 0;
        for (const solid::Vertex& p : v) {
            lo = glm::min(lo, glm::vec3(p.px, p.py, p.pz));
            hi = glm::max(hi, glm::vec3(p.px, p.py, p.pz));
            all += glm::vec3(p.px, p.py, p.pz);
        }
        for (const AvatarModel::Part& part : fig.parts())
            if (part.material == "Eye")
                for (uint32_t i = part.first; i < part.first + part.count; ++i) eye += glm::vec3(v[i].px, v[i].py, v[i].pz), ++eyes;
        all /= (float)std::max<size_t>(1, v.size());
        check(!v.empty() && approx(lo.y, 0.0, 0.05) && hi.y > 1.6f && hi.y < 1.9f, "player model stands on the floor, 1.8 m tall",
              "feet " + num(lo.y * 100) + " cm, top " + num(hi.y * 100) + " cm");
        check(eyes > 0 && eye.z / eyes < all.z - 0.05f, "player model looks along -z like the camera", "eyes z " + num(eye.z / std::max(1, eyes)));
        const float vw = fig.clipGroundSpeed(walk), vr = fig.clipGroundSpeed(run);
        check(vw > 0.5f && vw < 3.0f && vr > vw, "step speed of walk and run read from the feet", num(vw) + " / " + num(vr) + " m/s");
        const int left = fig.clip("Run_Left"), right = fig.clip("Run_Right"), back = fig.clip("Run_Back");
        check(left >= 0 && right >= 0 && back >= 0 && fig.clipGroundSpeed(left) > 0.5f && fig.clipGroundSpeed(back) > 0.5f,
              "player model runs sideways and backwards", num(fig.clipGroundSpeed(left)) + " / " + num(fig.clipGroundSpeed(back)) + " m/s");

        // Landung: Knie um a beugen (Oberschenkel +a, Unterschenkel -2a). Die Schuhe hängen an Fuß-Zielen unter der
        // Wurzel und müssen mitgehen; die Füße heben sich um Beinlänge * (1 - cos a)
        const int thigh = fig.node("UpperLeg.L"), shin = fig.node("LowerLeg.L"), ankle = fig.node("LowerLeg.L_end");
        const int thighR = fig.node("UpperLeg.R"), shinR = fig.node("LowerLeg.R");
        fig.setFollow(fig.node("Foot.L"), shin);
        fig.setFollow(fig.node("Foot.R"), shinR);
        const float bendA = 0.6f;
        const AvatarModel::Bend crouch[4] = {{thigh, {1, 0, 0}, bendA}, {shin, {1, 0, 0}, -2 * bendA}, {thighR, {1, 0, 0}, bendA},
                                             {shinR, {1, 0, 0}, -2 * bendA}};
        std::vector<solid::Vertex> c;
        fig.pose(&still, 1, crouch, 4, c);
        float low = 1e9f;
        for (const solid::Vertex& p : c) low = std::min(low, p.py);
        const float leg = glm::length(fig.restPosition(thigh) - fig.restPosition(shin)) + glm::length(fig.restPosition(shin) - fig.restPosition(ankle));
        const float expect = leg * (1 - std::cos(bendA));
        check(thigh >= 0 && approx(low, expect, 0.05), "bent knees lift the shoes with the legs", num(low * 100) + " cm, expected " + num(expect * 100));
        check(fig.restPosition(fig.node("UpperArm.L")).x < 0, "left arm is on the left (-x)");
    }
    physics.shutdown();
    std::printf("\n%d of %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
