// Leistungs- und Stabilitätsmessung der Physik (Werkstatt.exe --bench)
// Jede Szene läuft zweimal: "vorher" = alte Kollision (jedes Stück eine Hülle, keine Grundformen, eine Reibungszahl,
// schnelles Herausdrücken) und "neu" = aktuelle Einstellungen.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "Collision.h"
#include "Commands.h"
#include "Materials.h"
#include "Physics.h"
#include "World.h"

namespace {

struct Result {
    double avgMs = 0, maxMs = 0;
    double avgContacts = 0;
    double maxPen = 0;      // m
    double meanPen = 0, p99Pen = 0;
    double maxSpeed = 0;    // m/s (Explosionen erkennen)
    double check = 0;       // szenenabhängiger Messwert
    int rescues = 0;
    int bodies = 0;
};

Result measure(World& w, Physics& ph, double seconds, const std::function<double(World&)>& extra) {
    Result r;
    int steps = 0;
    std::vector<double> pens;
    const double dt = ph.fixedDt();
    for (double t = 0; t < seconds; t += dt) {
        w.simulateSeconds(dt, false);
        const PhysicsStats& s = ph.stats();
        if (s.steps == 0) continue;
        double ms = s.frameMs / s.steps;
        r.avgMs += ms;
        r.maxMs = std::max(r.maxMs, ms);
        r.avgContacts += s.contacts;
        r.maxPen = std::max(r.maxPen, (double)s.maxPenetration);
        pens.push_back(std::max(0.0, (double)s.maxPenetration));
        for (auto& o : w.objects())
            if (o->body != kNoBody && !o->fixed && o->hingeAxis < 0)
                r.maxSpeed = std::max(r.maxSpeed, (double)glm::length(ph.linearVelocity(o->body)));
        ++steps;
    }
    if (steps) {
        r.avgMs /= steps;
        r.avgContacts /= steps;
        for (double p : pens) r.meanPen += p;
        r.meanPen /= pens.size();
        std::sort(pens.begin(), pens.end());
        r.p99Pen = pens[(size_t)(pens.size() * 0.99)];
    }
    r.bodies = (int)w.objects().size();
    r.rescues = w.rescueCount();
    if (extra) r.check = extra(w);
    return r;
}

void throwAt(World& w, Object* o, const glm::dvec3& v) {
    if (o) w.applyImpulse(*o, glm::vec3(v * o->mass));
}

// Wie sieht die Kollisionsform eines gebohrten Bretts aus? (Anzahl und Dicke der Hüllen)
void hullReport() {
    solid::Solid s;
    s.pieces.push_back(solid::makeBox({0.3, 0.01, 0.05}));
    for (double x : {-0.2, 0.0, 0.2}) s = solid::subtract(s, solid::drillTool({x, 0.011, 0}, {0, -1, 0}, 0.005, 0.1, 20));
    CollisionBuildInfo info;
    auto hulls = buildCollisionHulls(s, 0, &info);
    std::vector<double> thick;
    for (auto& h : hulls) {
        std::vector<glm::dvec3> pts(h.begin(), h.end());
        std::vector<glm::dvec4> pl;
        double t = 0;
        if (Physics::hullPlanes(pts, pl)) {
            t = 1e30;
            for (auto& p : pl) {
                double lo = 1e30, hi = -1e30;
                for (auto& q : pts) {
                    double d = glm::dot(glm::dvec3(p), q);
                    lo = std::min(lo, d);
                    hi = std::max(hi, d);
                }
                t = std::min(t, hi - lo);
            }
        }
        thick.push_back(t * 1000);
    }
    std::sort(thick.begin(), thick.end());
    std::printf("Brett 60x2x10 mit 3 Bohrungen D10: %zu Stuecke -> %zu Huellen, Dicke mm:", info.pieces, info.hulls);
    for (double t : thick) std::printf(" %.2f", t);
    std::printf("\n");
}

} // namespace

int runBench(const std::string& filter) {
    Physics physics;
    physics.init();
    hullReport();
    const int rubber = materialIndex("rubber"), steel = materialIndex("steel"), spruce = materialIndex("spruce"),
              brick = materialIndex("brick"), oak = materialIndex("oak"), alu = materialIndex("aluminium");
    struct Scene {
        const char* name;
        std::function<void(World&)> build;
        const char* checkName;
        std::function<double(World&)> check;
    };
    static uint32_t tagA = 0, tagB = 0;
    const Scene scenes[] = {
        {"Kugelhaufen (40 Kugeln)",
         [&](World& w) {
             for (int i = 0; i < 40; ++i)
                 w.addSphere(i % 2 ? rubber : steel, 0.10, {(i % 4) * 0.11 - 0.16, 0.3 + (i / 4) * 0.25, ((i / 2) % 2) * 0.05});
         },
         "", nullptr},
        {"Kugeln prallen zusammen",
         [&](World& w) {
             Object* a = w.addSphere(steel, 0.15, {-1.0, 0.075, 0});
             Object* b = w.addSphere(rubber, 0.15, {1.0, 0.075, 0.03});
             throwAt(w, a, {6, 0, 0});
             throwAt(w, b, {-6, 0, 0});
             for (int i = 0; i < 10; ++i) w.addSphere(rubber, 0.08, {-0.5 + i * 0.1, 0.04, 0.4});
         },
         "", nullptr},
        {"Zylinderhaufen (25)",
         [&](World& w) {
             for (int i = 0; i < 25; ++i) {
                 Object* c = w.addCylinder(i % 2 ? spruce : steel, 0.08, 0.12, {(i % 3) * 0.09, 0.3 + i * 0.13, (i % 2) * 0.03});
                 if (c && i % 3 == 1) w.setTransform(*c, c->pos, glm::angleAxis(1.2, glm::dvec3(1, 0, 0)));
             }
         },
         "", nullptr},
        {"Gebohrte Bretter (12)",
         [&](World& w) {
             for (int i = 0; i < 12; ++i) {
                 Object* b = w.addBox(spruce, {0.6, 0.02, 0.1}, {0, 0.1 + i * 0.05, 0});
                 if (!b) continue;
                 uint32_t id = b->id;
                 for (double x : {-0.2, 0.0, 0.2}) w.drill(id, {x, 0.011, 0}, {0, -1, 0}, 0.01, 0, nullptr, nullptr, nullptr);
                 if (Object* o = w.find(id)) w.setTransform(*o, o->pos, glm::angleAxis(i * 0.9, glm::dvec3(0, 1, 0)));
             }
         },
         "", nullptr},
        {"Ziegelwand + Wurf",
         [&](World& w) {
             for (int row = 0; row < 6; ++row)
                 for (int c = 0; c < 6; ++c)
                     w.addBox(brick, {0.24, 0.071, 0.115}, {(c - 2.5) * 0.245 + (row % 2) * 0.12, 0.0355 + row * 0.0712, 0});
             Object* s = w.addBox(steel, {0.1, 0.1, 0.1}, {0, 0.2, 2.0});
             throwAt(w, s, {0, 1.5, -10});
         },
         "", nullptr},
        {"Ziegelturm (10)",
         [&](World& w) {
             for (int i = 0; i < 10; ++i) {
                 Object* o = w.addBox(brick, {0.24, 0.071, 0.115}, {0, 0.0355 + i * 0.0711, 0});
                 if (o && i == 9) tagA = o->id;
             }
         },
         "Turmspitze verschoben mm",
         [&](World& w) {
             Object* o = w.find(tagA);
             return o ? glm::length(glm::dvec2(o->pos.x, o->pos.z)) * 1000 : -1;
         }},
        {"Ineinander gesteckt (10 Paare)",
         [&](World& w) {
             for (int i = 0; i < 10; ++i) {
                 w.addBox(steel, {0.1, 0.1, 0.1}, {i * 0.4, 0.05, 0});
                 w.addBox(i % 2 ? oak : steel, {0.1, 0.1, 0.1}, {i * 0.4 + 0.05, 0.08, 0.02});
             }
         },
         "noch ineinander (Paare)",
         [&](World& w) {
             int stuck = 0;
             for (auto& o : w.objects())
                 for (auto& [id, depth] : w.overlapsOf(*o, 0.002))
                     if (id > o->id) ++stuck;
             return (double)stuck;
         }},
        {"Fluegel 3000 U/min gegen Block",
         [&](World& w) {
             Object* bar = w.addBox(alu, {0.6, 0.02, 0.005}, {0, 0.5, 0});
             Object* blk = w.addBox(steel, {0.08, 0.3, 0.08}, {0.25, 0.5, 0.0});
             if (blk) w.setFixed(*blk, true);
             if (bar) {
                 w.setTransform(*bar, bar->pos, glm::angleAxis(0.6, glm::dvec3(0, 0, 1)) * glm::angleAxis(1.5708, glm::dvec3(1, 0, 0)));
                 w.setHinge(*bar, 2, 3000.0f, 20.0f);
                 tagB = bar->id;
             }
         },
         "Fluegel im Block mm",
         [&](World& w) {
             Object* o = w.find(tagB);
             double d = 0;
             if (o)
                 for (auto& [id, depth] : w.overlapsOf(*o, 0.0)) d = std::max(d, depth);
             return d * 1000;
         }},
        {"Stift D18 ins Loch D22",
         [&](World& w) {
             Object* plate = w.addBox(oak, {0.3, 0.02, 0.1}, {0, 0.5, 0});
             if (plate) {
                 w.setFixed(*plate, true);
                 w.drill(plate->id, {0, 0.011, 0}, {0, -1, 0}, 0.022, 0, nullptr, nullptr, nullptr);
             }
             Object* peg = w.addCylinder(steel, 0.018, 0.08, {0.0, 0.6, 0.0});
             tagA = peg ? peg->id : 0;
         },
         "Stift Hoehe cm (durch = < 45)",
         [&](World& w) {
             Object* o = w.find(tagA);
             return o ? o->pos.y * 100 : -1;
         }},
        {"Beispiel-Werkstatt (Getriebe)", [&](World& w) { w.demoScene(); }, "", nullptr},
        {"Beispiel-Welt (alles)", [&](World& w) { w.showcaseScene(); }, "", nullptr},
    };

    std::printf("%-32s %-6s %9s %8s %8s   %-22s %8s %7s  %s\n", "Szene", "Form", "ms/Schr.", "max ms", "Kontakte", "Eindringen mm Ø/99%/max",
                "max v", "Rettung", "Messwert");
    for (const Scene& sc : scenes) {
        if (!filter.empty() && std::string(sc.name).find(filter) == std::string::npos) continue;
        for (int mode = 0; mode < 2; ++mode) {
            World w(physics, nullptr);
            w.setLegacyCollision(mode == 0);
            physics.setLegacySolver(mode == 0);
            physics.setSpeculativeDistance(mode == 0 ? 0.01f : 0.02f);
            w.setSimulating(true);
            sc.build(w);
            Result r = measure(w, physics, 4.0, sc.check);
            std::printf("%-32s %-6s %9.3f %8.2f %8.0f   %6.2f %6.1f %7.1f   %6.1f m/s %6d", mode == 0 ? sc.name : "", mode == 0 ? "vorher" : "neu",
                        r.avgMs, r.maxMs, r.avgContacts, r.meanPen * 1000.0, r.p99Pen * 1000.0, r.maxPen * 1000.0, r.maxSpeed, r.rescues);
            if (sc.check) std::printf("  %s: %.1f", sc.checkName, r.check);
            std::printf("\n");
            w.takeEvents();
        }
    }
    physics.setLegacySolver(false);
    physics.shutdown();
    return 0;
}
