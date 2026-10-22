// Große Beispielwelt mit allem, was die Engine kann ("scene showcase").
// Sie ist absichtlich als Befehlsfolge geschrieben: Jede Zeile ist auch ein Beispiel für die Befehle.
#include <cstdio>
#include <cstdlib>

#include "Commands.h"
#include "Script.h"
#include "World.h"

namespace {

class BuildHost : public CommandHost {
public:
    explicit BuildHost(World& w) : w_(w) {}
    World& world() override { return w_; }

private:
    World& w_;
};

// Maße in cm, Position = Schwerpunkt. "new box <mat> L W H x y z": L entlang x, W entlang z, H nach oben.
const char* kShowcase[] = {
    // ------------------------------------------------ Kugelbahn mit gefräster Rille und Dominosteinen
    "new box oak 240 12 4 -350 37 -200 ; set sc_ramp = last ; name $sc_ramp 'Ball run (milled groove)'",
    "groove $sc_ramp top x 0 30 15 ; round $sc_ramp x 3 ; rotate $sc_ramp z -12 ; fix $sc_ramp",
    "new box oak 8 8 58 -460 29 -200 ; fix last ; name last 'Ball run post'",
    "new sphere steel 2.5 -447.45 59.5 -200 ; name last 'Steel ball'",
    "new sphere rubber 2.5 -439.6 57.84 -200 ; name last 'Rubber ball'",
    "new sphere oak 2.5 -431.8 56.17 -200 ; name last 'Oak ball'",
    "set sc_x = -220 ; repeat 14 times { new box spruce 1 5 15 $sc_x 7.55 -200 ; name last Domino ; set sc_x = sc_x + 7 }",
    "new box brick 24 11.5 7.1 -110 3.6 -200 ; name last 'Domino target brick'",

    // ------------------------------------------------ Kugelstoßpendel (Newton): 5 Stahlkugeln an je zwei Seilen
    "new box oak 80 4 4 300 90 -262 ; fix last ; name last 'Cradle beam back'",
    "new box oak 80 4 4 300 90 -238 ; fix last ; name last 'Cradle beam front'",
    "set sc_x = 290 ; repeat 5 times { new box steel 2 2 2 $sc_x 87 -262 ; fix last ; name last Hook ; set sc_k1 = last ; "
    "new box steel 2 2 2 $sc_x 87 -238 ; fix last ; name last Hook ; set sc_k2 = last ; "
    "if sc_x < 291 then { new sphere steel 5 268.8 55.7 -250 ; name last 'Cradle ball (raised)' } else { new sphere steel 5 $sc_x 50 -250 ; name last 'Cradle ball' } ; "
    "rope $sc_k1 last ; rope $sc_k2 last ; set sc_x = sc_x + 5.02 }",

    // ------------------------------------------------ Propeller-Waage: der Propeller hebt seinen Hebelarm an (Auftrieb)
    "new box oak 6 6 60 300 30 150 ; set sc_post = last ; fix $sc_post ; name $sc_post 'Balance post'",
    "new box spruce 100 4 2 300 62 150 ; set sc_arm2 = last ; name $sc_arm2 'Balance arm' ; hinge $sc_post $sc_arm2 z",
    "new propeller aluminium 30 3 20 ; set sc_lift = last ; position $sc_lift 340 68 150 ; hinge $sc_arm2 $sc_lift y 3000 3 ; name $sc_lift 'Lifting propeller'",
    "new box steel 4 4 2 262 64 150 ; glue $sc_arm2 last epoxy ; name last 'Counterweight'",
    "new box oak 6 6 6 262 48 150 ; fix last ; name last 'Balance stop' ; new box oak 6 6 6 262 80 150 ; fix last ; name last 'Balance stop'",

    // ------------------------------------------------ Propeller-Karussell: der Schub treibt einen Arm im Kreis (Kraft nach vorne)
    "new box oak 8 8 80 300 40 330 ; set sc_cpost = last ; fix $sc_cpost ; name $sc_cpost 'Carousel post'",
    "new box spruce 120 4 3 300 81.6 330 ; set sc_carm = last ; name $sc_carm 'Carousel arm' ; hinge $sc_cpost $sc_carm y",
    "new propeller aluminium 30 2 20 ; set sc_push = last ; rotation $sc_push 90 0 0 ; position $sc_push 378 81.6 330 ; hinge $sc_carm $sc_push y 3000 3 ; name $sc_push 'Pushing propeller'",

    // ------------------------------------------------ Hängebrücke aus Brettern und Seilen, mit Kugel
    "new box oak 10 30 60 -95 30 250 ; fix last ; name last 'Bridge post' ; new box oak 10 30 60 95 30 250 ; fix last ; name last 'Bridge post'",
    "new box steel 4 30 4 -88 57 250 ; set sc_h1 = last ; fix $sc_h1 ; name $sc_h1 'Bridge hook'",
    "new box steel 4 30 4 88 57 250 ; set sc_h2 = last ; fix $sc_h2 ; name $sc_h2 'Bridge hook'",
    "set sc_prev = sc_h1 ; set sc_x = -71.6 ; repeat 8 times { new box oak 16 30 3 $sc_x 57 250 ; name last Plank ; rope $sc_prev last 6.5 ; set sc_prev = last ; set sc_x = sc_x + 20.46 }",
    "rope $sc_prev $sc_h2 6.5",
    "new sphere rubber 8 -50 63 250 ; name last 'Bridge ball'",

    // ------------------------------------------------ Feder mit Gewicht
    "new box oak 10 10 120 -300 60 200 ; fix last ; name last 'Spring post'",
    "new box oak 50 10 6 -280 117 200 ; set sc_arm = last ; fix $sc_arm ; name $sc_arm 'Spring arm'",
    "new box steel 10 10 10 -280 75 200 ; set sc_w = last ; name $sc_w 'Spring weight' ; spring $sc_arm $sc_w 1500 3",

    // ------------------------------------------------ Wippe mit Scharnier
    "new box oak 10 20 10 -150 5 100 ; set sc_base = last ; fix $sc_base ; name $sc_base 'Seesaw base'",
    "new box spruce 120 20 3 -150 11.6 100 ; set sc_plank = last ; name $sc_plank 'Seesaw plank' ; hinge $sc_base $sc_plank z",
    "new box oak 15 15 15 -200 40 100 ; name last 'Falling oak block'",
    "new sphere rubber 10 -100 18.2 100 ; name last 'Seesaw ball'",

    // ------------------------------------------------ Stift ins Loch: D18 passt durch D22, aber nicht durch D12
    "new box oak 10 10 40 115 20 -250 ; fix last ; name last 'Peg stand' ; new box oak 10 10 40 185 20 -250 ; fix last ; name last 'Peg stand'",
    "new box oak 80 20 2 150 41 -250 ; set sc_plate = last ; fix $sc_plate ; name $sc_plate 'Plate with holes D22 / D12'",
    "drill $sc_plate top -15 0 22 ; drill $sc_plate top 15 0 12 ; countersink $sc_plate top 15 0 18",
    "new cylinder steel 1.8 8 135 50 -250 ; name last 'Peg D18 (fits D22)' ; new cylinder steel 1.8 8 165 50 -250 ; name last 'Peg D18 (too big for D12)'",

    // ------------------------------------------------ Werkstücke auf einem Tisch: alle Bearbeitungen
    "new box oak 170 70 70 0 35 -265 ; fix last ; name last 'Display table'",
    "new box oak 16 10 6 -70 73.1 -248 ; round last all 10 ; name last 'Rounded block (round all 10)'",
    "new box steel 8 8 8 -50 74.1 -248 ; chamfer last all 6 ; name last 'Chamfered cube (chamfer all 6)'",
    "new box oak 14 14 7 -28 73.6 -248 ; set sc_bowl = last ; new sphere steel 12 -28 79 -248 ; subtract $sc_bowl last ; name $sc_bowl 'Bowl (subtract a sphere)'",
    "new cylinder oak 4 24 0 90 -248 ; set sc_rod = last ; turn $sc_rod y 0 4 25 ; turn $sc_rod y 4 9 30 20 ; turn $sc_rod y 14 15 28 ; turn $sc_rod y 20 24 32",
    "rotation $sc_rod 0 0 90 ; position $sc_rod 0 72.1 -248 ; name $sc_rod 'Turned rod (lathe)'",
    "new box oak 30 6 4 20 72.1 -270 ; tenon last +x 4 12 40 ; name last 'Tenon (tenon +x 4 12 40)'",
    "new box oak 12 8 6 45 73.1 -270 ; pocket last -x -1 0 12 40 40 ; name last 'Mortise (pocket)'",
    "new box steel 30 4 0.3 -60 70.25 -285 ; bend last x 15 90 3 ; name last 'Bent bracket (bend 90)'",
    "new box spruce 20 10 2 -28 71.1 -285 ; set sc_cs = last ; drill $sc_cs top -6 0 4 ; drill $sc_cs top 6 0 4 ; countersink $sc_cs top -6 0 9 ; countersink $sc_cs top 6 0 9 ; name $sc_cs 'Countersunk holes'",
    "new box spruce 20 4 2 0 71.1 -285 ; set sc_t = last ; new box spruce 2 4 12 0 78.1 -285 ; merge $sc_t last ; name $sc_t 'T-piece (merge)'",
    "new box aluminium 15 8 0.5 25 70.35 -285 ; slot last top -4 0 4 0 6 ; name last 'Slot plate'",
    "new box oak 20 4 2 52 71.1 -285 ; groove last top z 0 40 10 ; name last 'Cross lap A' ; new box oak 20 4 2 52 72.1 -285 ; groove last bottom z 0 40 10 ; rotate last y 90 ; name last 'Cross lap B'",
    "new box spruce 25 10 2 62 71.1 -250 ; sand last 2 ; name last 'Sanded board (sand 2)'",
    "new box oak 12 8 6 -40 73.1 -300 ; rebate last top +z 20 15 ; name last 'Rebate (rebate)'",
    "new box steel 10 4 2 -15 71.1 -300 ; set sc_m = last ; cut $sc_m x 7 30 ; delete last ; name $sc_m 'Mitred block' ; mirror $sc_m x copy ; name last 'Mitred block (mirror copy)'",

    // ------------------------------------------------ Regal: schwacher Heißkleber bricht, Epoxid hält
    "new box oak 10 40 100 -450 50 50 ; set sc_wall = last ; fix $sc_wall ; name $sc_wall 'Wall'",
    "new box spruce 30 30 2 -430 80 50 ; set sc_s1 = last ; name $sc_s1 'Shelf (weak hot glue)' ; glue $sc_wall $sc_s1 hotglue 0.02",
    "new box steel 10 10 10 -425 86.1 50 ; name last 'Heavy block (breaks the glue)'",
    "new box spruce 30 30 2 -430 50 50 ; set sc_s2 = last ; name $sc_s2 'Shelf (epoxy holds)' ; glue $sc_wall $sc_s2 epoxy",
    "new box steel 10 10 10 -425 56.1 50 ; name last 'Heavy block (held)'",

    // ------------------------------------------------ Abrissbirne gegen eine Ziegelmauer
    "new box oak 10 10 90 150 45 360 ; fix last ; name last 'Gantry post' ; new box oak 10 10 90 150 45 480 ; fix last ; name last 'Gantry post'",
    "new box oak 10 130 10 150 95 420 ; set sc_beam = last ; fix $sc_beam ; name $sc_beam 'Gantry beam'",
    "new sphere steel 15 90.8 43.7 420 ; set sc_wb = last ; name $sc_wb 'Wrecking ball' ; rope $sc_beam $sc_wb",
    "set sc_y = 3.56 ; set sc_off = 0 ; repeat 4 times { set sc_z = 383 + sc_off ; repeat 3 times { new box brick 11.5 24 7.1 162 $sc_y $sc_z ; "
    "name last 'Wall brick' ; set sc_z = sc_z + 24.5 } ; set sc_y = sc_y + 7.12 ; set sc_off = 12 - sc_off }",

    // ------------------------------------------------ Axt aus einem Teil (Keil-Kopf + Stahlrohr-Stiel vereint) und Spaltholz
    // Schneide parallel zum Stiel. Mit der Schneide entlang der Faser auf ein Brett geworfen, spaltet sie es, quer bleibt sie stecken.
    "new wedge steel 10 10 20 -230 50 400 ; set sc_axe = last ; new tube steel 3 2.4 60 -255 53.5 400 ; rotation last 0 0 90 ; "
    "merge $sc_axe last ; rotation $sc_axe 90 0 0 ; drop $sc_axe ; name $sc_axe 'Axe (one part: wedge head + tube handle, merged)'",
    "new box spruce 60 10 2 -240 1 440 ; name last 'Firewood board 2 cm' ; new box spruce 60 10 3 -240 1.5 470 ; name last 'Firewood board 3 cm'",

    // ------------------------------------------------ Zwei Tore an selbstgebauten Stiftscharnieren (trocken / geölt)
    // Stahlplatte auf den Pfosten geklebt und geschraubt, Stiftlappen (gedreht, Spitze angefast) angeschweißt.
    // Türband mit abgerundetem Auge und Bohrung D10,4 auf die Türfläche geklebt und geschraubt. Anschlagleiste am
    // zweiten Pfosten und Bodenstopper: die Anschläge liegen weit vom Scharnier (kein Hebel).
    "set sc_gz = -50 ; set sc_lube = 0 ; repeat 2 times { "
    "new box oak 6 6 200 480 100 $sc_gz ; set sc_gp = last ; fix $sc_gp ; name $sc_gp 'Gate post (hinge side)' ; "
    "new box steel 0.8 6 10 483.4 22 $sc_gz ; set sc_p1 = last ; name $sc_p1 'Pintle plate' ; glue $sc_gp $sc_p1 epoxy ; screw $sc_p1 $sc_gp 2 ; "
    "new box steel 0.8 6 10 483.4 162 $sc_gz ; set sc_p2 = last ; name $sc_p2 'Pintle plate' ; glue $sc_gp $sc_p2 epoxy ; screw $sc_p2 $sc_gp 2 ; "
    "new cylinder steel 3 8 485.1 24 $(sc_gz + 2.4) ; set sc_k1 = last ; turn $sc_k1 y 4 8 10 ; turn $sc_k1 y 7.5 8 10 7 ; plane $sc_k1 -x 2 ; "
    "name $sc_k1 'Pin knuckle (turned pin D10)' ; weld $sc_p1 $sc_k1 4 ; "
    "new cylinder steel 3 8 485.1 164 $(sc_gz + 2.4) ; set sc_k2 = last ; turn $sc_k2 y 4 8 10 ; turn $sc_k2 y 7.5 8 10 7 ; plane $sc_k2 -x 2 ; "
    "name $sc_k2 'Pin knuckle (turned pin D10)' ; weld $sc_p2 $sc_k2 4 ; "
    "new box steel 14 2.4 4 490.89 26 $(sc_gz + 2.4) ; set sc_s1 = last ; round $sc_s1 e4,e5 11.9 ; drill $sc_s1 top -5.79 0 10.4 ; "
    "name $sc_s1 'Door strap (eye D10.4)' ; "
    "new box steel 14 2.4 4 490.89 166 $(sc_gz + 2.4) ; set sc_s2 = last ; round $sc_s2 e4,e5 11.9 ; drill $sc_s2 top -5.79 0 10.4 ; "
    "name $sc_s2 'Door strap (eye D10.4)' ; "
    "new box oak 80 2.4 190 527.1 100 $sc_gz ; set sc_gd = last ; glue $sc_gd $sc_s1 epoxy ; screw $sc_s1 $sc_gd 2 ; glue $sc_gd $sc_s2 epoxy ; screw $sc_s2 $sc_gd 2 ; "
    "new box oak 6 6 200 573 100 $sc_gz ; fix last ; name last 'Gate post (lock side)' ; "
    "new box oak 4 2 180 566 100 $(sc_gz - 2.2) ; fix last ; name last 'Door stop lath' ; "
    "new box oak 10 10 8 491.4 4 $(sc_gz + 69) ; fix last ; name last 'Floor stop' ; "
    "if sc_lube > 0 then { name $sc_gd 'Gate (oiled hinges)' ; lubricate $sc_s1 oil ; lubricate $sc_s2 oil } else { name $sc_gd 'Gate (dry hinges)' } ; "
    "set sc_gz = sc_gz + 130 ; set sc_lube = 1 }",
    // Stift D9,8 fällt in ein Sackloch D10 und bleibt am Boden stehen; Stift D10,5 passt nicht hinein
    "new box steel 4 4 4 480 2 -150 ; fix last ; drill last top 0 0 10 30 ; name last 'Block, blind hole D10' ; "
    "new cylinder steel 0.98 8 480 6 -150 ; name last 'Pin D9.8 (fits, sinks to the bottom)' ; "
    "new box steel 4 4 4 500 2 -150 ; fix last ; drill last top 0 0 10 30 ; name last 'Block, blind hole D10' ; "
    "new cylinder steel 1.05 8 500 8.01 -150 ; name last 'Pin D10.5 (too big, stays on top)'",
};

// Feuerstelle ("scene fire"): Lagerfeuer mit Stahlstab darüber, Aluminium, Kunststoff, Gummi, ein geleimtes Holzgestell
// und ein tiefgekühlter Stahlstab daneben
const char* kFireScene[] = {
    // Lagerfeuer: zwei Scheite unten, zwei quer darüber, Anzündholz in der Mitte
    "new box spruce 50 8 8 0 4 -12 ; name last 'Log' ; new box spruce 50 8 8 0 4 12 ; name last 'Log'",
    "new box spruce 50 8 8 -12 12.01 0 ; rotate last y 90 ; name last 'Log' ; new box spruce 50 8 8 12 12.01 0 ; rotate last y 90 ; name last 'Log'",
    "new box spruce 30 3 3 -3 9.51 0 ; rotate last y 90 ; name last 'Kindling' ; set fs_k = last ; "
    "new box spruce 30 3 3 3 9.51 0 ; rotate last y 70 ; name last 'Kindling'",
    // Stahlstab auf zwei Betonsteinen über dem Feuer: wird heiß, glüht, läuft blau an
    "new box concrete 12 12 22 -45 11 0 ; new box concrete 12 12 22 45 11 0",
    "new box steel 110 2 2 0 23.01 0 ; name last 'Steel bar over the fire'",
    // Aluminiumblock und Kunststoffbecher am Rand: schmelzen, wenn das Feuer groß wird
    "new box aluminium 6 6 6 0 3 30 ; name last 'Aluminium block' ; new cylinder plastic 7 9 22 4.5 -24 ; name last 'Plastic cup'",
    "new tube rubber 30 18 8 40 4 -45 ; name last 'Rubber ring'",
    // geleimtes Brett an einem Pfosten nah am Feuer: der Leim wird weich
    "new box oak 4 4 40 -40 20 40 ; fix last ; set fs_post = last ; new box oak 30 2 10 -23 30 40 ; set fs_shelf = last ; "
    "name $fs_shelf 'Glued shelf' ; glue $fs_post $fs_shelf woodglue",
    // Kälte: Stahlstab auf -60 °C (spröde) und einer bei Raumtemperatur zum Vergleich
    "new box steel 40 3 3 80 40 30 ; heat last -60 ; name last 'Frozen steel bar (-60 °C)' ; new box steel 40 3 3 80 40 50 ; name last 'Steel bar (20 °C)'",
    "ignite $fs_k ; ignite 1 ; ignite 2",
};

} // namespace

void World::fireScene() {
    clear();
    undo_.clear();
    redo_.clear();
    BuildHost host(*this);
    for (const char* line : kFireScene) {
        bool ok = true;
        std::string out = runScriptSync(host, line, &ok);
        if (!ok) std::fprintf(stderr, "fire scene: %s\n%s\n", line, out.c_str());
    }
}

void World::showcaseScene() {
    demoScene();  // Werkbank, Rampe, Getriebe in der Mitte
    BuildHost host(*this);
    for (const char* line : kShowcase) {
        bool ok = true;
        const auto t0 = std::chrono::steady_clock::now();
        std::string out = runScriptSync(host, line, &ok);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (sec > 0.3) std::fprintf(stderr, "showcase slow (%.1f s): %.80s\n", sec, line);
        if (!ok) std::fprintf(stderr, "showcase: %s\n%s\n", line, out.c_str());
    }
    undo_.clear();
    redo_.clear();
}
