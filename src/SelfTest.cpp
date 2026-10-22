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
#include "Physics.h"
#include "Script.h"
#include "Solid.h"
#include "World.h"

namespace {

int failures = 0, checks = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("[%s] %s%s%s\n", ok ? " OK " : "FAIL", what.c_str(), detail.empty() ? "" : "  -> ", detail.c_str());
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
