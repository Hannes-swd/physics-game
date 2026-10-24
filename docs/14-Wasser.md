# Wasser

Teile schwimmen, sinken, schaukeln in Wellen und treiben in der Strömung. Boote aus Blech schwimmen, bis sie volllaufen;
ein Becher läuft voll, wenn man ihn unter Wasser drückt, umgedreht hält er Luft. Glühender Stahl zischt und ist schnell
kalt, Feuer geht unter Wasser aus. Die Spielfigur watet und schwimmt.

Ausprobieren: Menü *Datei → Wasserbecken* (`scene water`) baut ein Becken mit Holz, Stahl, Gummi, einem Floß, einem
Stahlboot, zwei Bechern und einem glühenden Stahlstab. Menü *Datei → Fluss* (`scene river`) baut einen Kanal mit
Strömung, Felsen und einem Brückenpfeiler, an denen sich das Wasser staut und schäumt; Stämme und Bretter treiben mit.
**Tab** drücken.

## Gewässer

| Art | Was | Befehl |
|---|---|---|
| **Becken** | Quader voll Wasser: Breite × Tiefe, Wasserspiegel. Mit `walls` gleich mit Betonrand | `water pool 200 150 50 walls` |
| **Meer** | in der Fläche unbegrenzt. Wasser ist zwischen dem **Grund** und dem Wasserspiegel: darunter ist Erde, und wo der Grund über dem Spiegel liegt, ist **Land** (siehe unten) | `water sea 40` |

### Grund des Meers und Land

Das Meer füllt nicht „alles unter dem Spiegel“, sondern nur den Raum über seinem Grund. Wer an Land ein Loch gräbt, das
tiefer als der Meeresspiegel reicht, bleibt im Trockenen, und unter dem Meeresboden ist kein Wasser.

- **Standardgrund:** gilt überall, wo nichts anderes eingetragen ist, und kostet nichts (auch in einer unbegrenzten Welt).
  Ohne Angabe ist es der Boden der Welt (0 cm); liegt der Spiegel darunter, 10 m unter dem Spiegel. `water sea 40 bottom -300`
  macht das Meer 3,4 m tief.
- **Grundkarte:** Höhe des Grunds je Zelle von 50 cm, in Kacheln von 16 m wie die Chunks (`GroundMap` in `Water.h`). Nur
  Kacheln mit eigenem Grund werden gespeichert. `water land` legt trockenes Land ins Meer, `water ground` setzt den Grund
  beliebig (Bucht, Graben, Sandbank). Später trägt der Weltgenerator hier das Gelände je Chunk ein (`GroundMap::set`).
- Zwischen den Zellmitten wird der Grund überblendet (bilinear): eine Küste steigt über 50 cm an.
- An Land schwimmt nichts, das Strömungsgitter behandelt Land als **Küste** (das Wasser fließt daran entlang), und die
  Shader zeichnen dort keine Wasseroberfläche und färben nichts unter dem Grund ein.
- Die Shader bekommen ein Fenster der Grundkarte von 128 × 128 m (1 m je Zelle) um die Kamera, neu berechnet nur, wenn die
  Kamera 16 m weiterwandert oder sich der Grund ändert. Land außerhalb des Fensters zeigen sie noch als Meer (die Physik
  rechnet überall richtig).
- Speichern (`waterground`, `groundtile` in der Szenendatei), Rückgängig und Mehrspieler (die Kacheln gehen nur bei einer
  Änderung übers Netz) sind dabei.

Jedes Gewässer hat eine **Flüssigkeit** (`water`, `seawater`, `muddy`, `oil`), **Wellen** (Höhe von Tal bis Kamm),
eine **Strömung** (z. B. ein Fluss) und eine **Temperatur** (ohne Angabe die Lufttemperatur).

| Flüssigkeit | Dichte | Wärmeübergang | Siedet | Licht |
|---|---|---|---|---|
| `water` (Wasser) | 998 kg/m³ | 500 W/(m²·K) | 100 °C | klar, blaugrün |
| `seawater` (Meerwasser) | 1025 kg/m³ | 500 W/(m²·K) | 100,5 °C | klar, grüner |
| `muddy` (trübes Wasser) | 1010 kg/m³ | 500 W/(m²·K) | 100 °C | nach wenigen cm undurchsichtig, braun |
| `oil` (Speiseöl) | 920 kg/m³ | 150 W/(m²·K) | – | gelb, löscht kein Feuer |

Im Meerwasser schwimmt alles etwas höher (Eiche: 7,0 statt 7,2 cm von 10 cm unter Wasser).

## Bedienen

| Wo | Was |
|---|---|
| Menü *Physik → Wasser* | Becken hier (2 × 1,5 m, 50 cm tief, mit Rand), Meer an/aus (Spiegel 40 cm), Wellen (ruhig, 10, 40, 150 cm), Strömung (keine, 0,4, 1,2, 2,5 m/s nach rechts), ausgewähltes Teil füllen, alles Wasser entfernen |
| Menü *Datei → Wasserbecken*, *Fluss* | Beispielszenen |
| Spiel-Modus | ins Wasser laufen: Waten bremst, ab der Brust schwimmt die Figur. **W** schwimmt in Blickrichtung (nach unten schauen = tauchen), **Leertaste** nach oben, **Strg** nach unten |
| Physik-Inspektor (F3) | Auftrieb im Wasser, Widerstand, Wasser im Teil (unter FORCES) |

## Befehle

| Befehl | Wirkung |
|---|---|
| `water` | Überblick: Gewässer, wie viele Teile schwimmen bzw. unter Wasser sind, Flüssigkeiten |
| `water pool <L> <W> <depth> [x y z] [liquid] [walls]` | Becken, Maße in cm. x y z = Mitte des Bodens (ohne: hier auf dem Boden) |
| `water sea <level> [liquid] [bottom <cm>]` / `water sea off` | Meer mit Wasserspiegel in cm (Standard: Meerwasser), `bottom`: Standardgrund |
| `water land <x> <z> <W> <L> [height]` | trockenes Land im Meer: Rechteck um x, z (cm), Grund 50 cm über dem Spiegel oder `height` |
| `water ground <x> <z> <W> <L> <height>` / `water ground reset` | Grund des Meers im Rechteck auf `height` (cm) setzen / alle eigenen Kacheln entfernen |
| `water level <n\|all> <cm>` | Wasserspiegel ändern (Teile darin werden geweckt und steigen oder fallen) |
| `water waves <n\|all> <cm>` | Wellenhöhe (0 = ruhig, 30 = bewegt, 150 = Sturm). Die Wellen laufen mit dem Wind |
| `water flow <n\|all> <x> <z>` | Strömung in m/s; das Wasser fließt um Hindernisse herum (siehe unten) |
| `water temp <n\|all> <°C\|air>` | eigene Wassertemperatur (kalter See) oder die der Luft |
| `water liquid <n\|all> <liquid>` | Flüssigkeit tauschen |
| `water remove <n\|all>` | Gewässer entfernen (was schwamm, fällt) |
| `water <id>` | wie ein Teil schwimmt: verdrängtes Volumen, Auftrieb, Widerstand, Hohlräume und das Wasser darin |
| `water fill <id> [Prozent]` / `water empty <id>` | Hohlräume eines Teils (Becher, Boot) füllen bzw. leeren |
| `scene water` / `scene river` | Beispielszenen: Becken, Fluss |

```
> water
water         1 body
  #1 pool 300 x 200 cm, 60 cm deep, bottom center 0 0 0 cm, water 20 °C (air)
parts         8 floating, 3 under water
liquids       water (998 kg/m3) seawater (1025 kg/m3) muddy (1010 kg/m3) oil (920 kg/m3)

> water 13
#13 Steel boat (3 mm sheet): 18.379 kg, 2341.3 cm3 (7850 kg/m3)
in water      #1, center of mass 0.8 cm below the surface
displaced     18429.1 cm3 = 18.392 kg of water (66 % of the material under water)
buoyancy      180.43 N, weight 180.30 N, water drag 0.24 N
hollow 1      57658.7 cm3, open 3922.4 cm2, water inside 0.0 cm3 (0 %)
```

Mit Strömung oder Wellen zeigt `water` auch das Strömungsgitter (`scene river`):

```
  #1 pool 800 x 160 cm, 40 cm deep, bottom center 0 0 0 cm, water 20 °C (air), current 1.20 0.00 m/s
     flow grid 96 x 20 cells of 8.3 cm, 157 blocked, fastest 2.17 m/s, water piles up 5.4 cm / drops 4.2 cm, foam 100 %
```

Werte für die Befehlssprache: `<id>.inwater` (0/1), `.submerged` (% des Materials unter Wasser), `.flooded` (% der
Hohlräume voll Wasser), dazu `waters` (Anzahl der Gewässer). Beispiel:

```
water fill 13 30 ; wait until 13.flooded > 90 max 30 ; print time, 13.bottom
```

Gewässer werden mit der Szene gespeichert und gehören zum Rückgängig-Verlauf.

## Das Modell

### Auftrieb (Archimedes), exakt

Jedes Teil besteht aus konvexen Stücken (siehe [07](07-Engine-Aufbau.md)). Jedes Stück wird an der Wasseroberfläche und an
den Wänden und dem Boden des Beckens geschnitten; das Volumen darunter und sein Schwerpunkt geben die Auftriebskraft
ρ · g · V und ihren Angriffspunkt. Deshalb

- schwimmt Fichte (470 kg/m³) genau zu 47 % unter Wasser, Eiche zu 72 %, Stahl sinkt,
- liegt ein Brett flach, richtet sich ein schräg eingesetzter Klotz auf, schwimmt ein Stamm quer,
- ragt ein Brett über den Beckenrand, trägt nur der Teil im Wasser.

Bei Wellen wird jedes Stück an der Tangentialebene der Wellen über ihm geschnitten: Ein langes Boot hebt sich vorne, wenn
dort ein Wellenberg ist. Schnitte und Volumen rechnen ohne neue Geometrie (Bezugspunkt auf der Schnittebene: die
Schnittfläche trägt nichts bei), ganz eingetauchte Stücke kosten nur einen Test mit ihrer umschließenden Kugel.

### Hohlräume: Boote, Becher, Taucherglocken

Beim ersten Kontakt mit Wasser wird einmal berechnet, wo ein Teil **Hohlräume** hat: seine konvexe Hülle minus das
Material (mit den normalen Booleschen Operationen der Geometrie), aufgeteilt in zusammenhängende Räume. Flächen eines
Raums, die auf der Hülle liegen, sind seine **Öffnungen**. Ein Stahlkasten mit ausgefräster Tasche ist so ein Boot, ein
Zylinder mit Bohrung ein Becher; Bohrungen und Nuten sind Räume, die sofort volllaufen. Ein Raum ohne Öffnung (dicht
verschweißter Tank) läuft nie voll.

| Vorgang | Rechnung |
|---|---|
| Luft im Raum trägt | der Teil des Raums unter der Wasseroberfläche verdrängt Wasser wie Material |
| Wasser läuft hinein | durch den Teil der Öffnungen unter der Oberfläche, Torricelli: Q = 0,6 · A · √(2 g Δh), Δh = Höhenunterschied der Wasserspiegel. Nur, wenn die Luft oben entweichen kann: die Öffnung reicht bis zum höchsten Punkt des Raums |
| Wasser läuft heraus | genauso, wenn der Spiegel innen höher ist, und nur, wenn Luft nachströmen kann: über dem Spiegel innen oder (gluckernd) durch eine weite Öffnung an der Luft |
| Wasserspiegel innen | waagrecht, so hoch, dass das Volumen darunter dem Wasser im Raum entspricht (Bisektion) |
| Wasser im Raum | zählt zur Masse des Physik-Körpers (Trägheit stimmt, auch im freien Fall); sein Schwerpunkt liegt tiefer bzw. seitlich: das Moment wird ergänzt |

Daraus folgt von selbst: Das Boot schwimmt, bis Wasser über den Rand schwappt oder man es füllt, dann sinkt es. Ein
Becher kippt beim Untertauchen nicht einfach um, sondern läuft voll. **Umgedreht unter Wasser** kann die Luft nicht nach
oben entweichen: Er hält seine Luft und steigt auf. Ein volles Glas, das man trägt, bleibt voll; **kopfüber** an der
Luft läuft es aus, mit dem Rand unter Wasser nicht.

### Strömung um Hindernisse

Hat ein Gewässer eine Strömung (oder Wellen), liegt über seiner Oberfläche ein 2D-Gitter (höchstens 96 Zellen je Seite,
mindestens 5 cm; beim Meer ein Fenster von 48 × 48 m um den Blickpunkt). 20-mal je Sekunde:

| Schritt | Rechnung |
|---|---|
| Hindernisse | Teile, die durch die Wasseroberfläche ragen und (fast) stillstehen: fest verankerte, ruhende oder hängengebliebene. Was mit dem Wasser treibt, zählt nicht. Neu gerastert wird nur, wenn sich an ihnen etwas geändert hat |
| Umströmung | die freie Strömung quellenfrei gemacht (Druck lösen, Gradient abziehen, wie in Rauch- und Wassersimulationen): Geschwindigkeiten auf den Zellkanten, Rot-Schwarz-Gauß-Seidel mit Überrelaxation, der Druck vom letzten Mal als Startwert. Ränder des Gitters offen (Wasser kommt herein und fließt hinaus), Hindernisse dicht. Ergebnis wie die Potentialströmung: vor einem Pfeiler (30 cm, 1 m/s) 0,26 m/s (Theorie 0,22), daneben 1,34 m/s (1,36) |
| Stau | Bernoulli: Δh = (U² − u²) / 2g. Vor dem Hindernis wölbt sich das Wasser auf, daneben, wo es schneller fließt, sinkt es (bei 1,2 m/s etwa +5 / −4 cm) |
| Schaum | entsteht, wo Wasser gegen ein Hindernis läuft (und wo Wellen anschlagen) und wo es stark verwirbelt (Kanten, Kielwasser); treibt mit der Strömung und zerfällt in etwa 4 s. Wo schnelles Wasser aufprallt (über 1 m/s), spritzt Gischt |

Die Physik benutzt dieselben Werte: Teile treiben mit der Strömung an ihrem Ort (also um Hindernisse herum, im Kehrwasser
langsamer), der Stau hebt sie mit an. Baut man einen Pfeiler oder Felsen ins Wasser, ändert sich die Strömung sofort,
nimmt man ihn weg, fließt es wieder gerade. Ein Stamm, der quer vor einen Felsen treibt, bleibt dort hängen.

### Widerstand und Dämpfung

| Kraft | Rechnung |
|---|---|
| Strömungswiderstand | Plattenmodell wie in der Luft ([08](08-Verbindungen-und-Bruch.md)), mit der Dichte der Flüssigkeit, je Außenfläche nach ihrem Anteil unter Wasser. Gegen die Bewegung des Wassers: Strömung plus Orbitalbewegung der Wellen (nimmt nach unten mit e^(−k·Tiefe) ab). Flächen unter Wasser bekommen keine Luftkraft |
| Auf und Ab | ein schwimmendes Teil strahlt Wellen ab: Kraft ∝ Änderung des verdrängten Volumens, 0,15 · ρ · √(g · L) · dV/dt. Ein Klotz schaukelt so ein paar Mal und liegt still |
| Rollen und Stampfen | dreht sich eine Fläche um eine Achse in ihrer Ebene, drückt eine Hälfte ins Wasser: Moment für eine quadratische Fläche (linear und quadratisch in der Drehgeschwindigkeit) |
| Stabilität | alles explizit gerechnet; bei leichten Teilen wird die Dämpfung so begrenzt, dass ein Schritt höchstens bis zur Geschwindigkeit des Wassers bremst |

Jolt legt langsame Teile schlafen. Im Wasser erst, wenn Auftrieb und Gewicht im Gleichgewicht sind und nichts das Teil
bewegt (Wellen oder Strömung an der Oberfläche, einlaufendes Wasser). Gesunkene und schwimmende ruhende Teile schlafen
und kosten dann nichts.

### Wellen

Vier Sinuswellen (Tiefwasser, ω² = g · k), die größte mit der Wellenlänge 14 × Wellenhöhe, die anderen 0,61, 0,37 und
0,23 davon, schräg dazu. Physik und Grafik rechnen **dieselbe Formel** (`waveHeight` in `Water.cpp`, `waterWave` in
`shaders/common.glsl`); die Phasen gibt die CPU in doppelter Genauigkeit an den Shader (mit Zeit und Ursprung): auch
Kilometer vom Weltursprung entfernt sitzen schwimmende Teile auf den Wellen, die man sieht. Die Wellen laufen mit dem
Wind (`wind x y z`), ohne Wind schräg.

### Wärme

Zellen des Temperaturgitters ([12](12-Waerme-und-Feuer.md)) unter Wasser geben ihre Wärme ans Wasser statt an die Luft:

| Oberfläche | Wärmeübergang |
|---|---|
| unter dem Siedepunkt | 500 W/(m²·K) + 1500 · v^0,8 bei Bewegung |
| bis ~ 300 °C (Blasensieden) | bis 9000 W/(m²·K): sehr schnell kalt |
| über ~ 450 °C (Dampffilm, Leidenfrost) | 1200 W/(m²·K) in kaltem Wasser |

Ein glühender Stahlstab (20 × 2 × 2 cm, 800 °C) ist nach 20 s im Wasser unter 100 °C, an der Luft noch über 700 °C.
Über dem Siedepunkt steigt Dampf auf. Unter Wasser brennt nichts: brennende Stellen gehen aus, `ignite` lehnt Stellen
unter Wasser ab. Das Wasser selbst erwärmt sich nicht (unbegrenzter Vorrat).

## Darstellung (Shader)

| Effekt | Wie |
|---|---|
| Oberfläche | eigenes Gitter ohne Vertex-Puffer: die Ecken entstehen aus `gl_VertexIndex` (`water.vert`). Meer: Gitter um die Kamera, nah 8 cm, außen immer gröber bis 25 km; Becken nur so fein, wie die Wellen es brauchen. Wellen im Vertex-Shader verschoben |
| Spiegelung | Fresnel (n = 1,33), Himmel in Spiegelrichtung, Sonnenglanz (GGX), Schatten |
| Kräuselung | feine Normalen aus Rauschen, auch auf ruhigem Wasser. Mit Strömung treibt das Muster mit (Flow-Map: zwei um eine halbe Periode versetzte Phasen, überblendet, damit es sich nicht endlos verzerrt); schnelles Wasser ist rauer |
| Fließen und Aufprall | Strömung, Schaum und Stau je Zelle als Puffer für die Shader (`waterFlow` in `common.glsl`): Schaum zieht sich in Strömungsrichtung zu Streifen, vor Hindernissen wölbt sich die Oberfläche auf (auch im Vertex-Shader), Gischt als Partikel |
| Flimmerfrei | Wellen, die kleiner als ein Pixel werden, werden glatt: das ferne Meer ist ein Spiegel des Himmels |
| Durchsicht und Farbe | jeder Körper färbt sich selbst nach seinem Weg durch das Wasser zur Kamera: Absorption je Farbe (rot zuerst) und Streulicht (`waterView` in `mesh.frag`). Kein zweiter Bildpass, kein Tiefenpuffer zum Lesen |
| Lichtflecken (Kaustik) | prozedural am Grund und an Wänden unter Wasser, wandernd; Sonnenlicht nimmt mit der Tiefe ab |
| Schaum | auf den Wellenkämmen, vor Hindernissen und in ihrem Kielwasser |
| Unter Wasser | Blick durch das Wasser in Wasserfarbe; Oberfläche von unten mit Totalreflexion außerhalb des Snellius-Fensters (48,6°) |
| Spritzer, Dampf | Partikel, wenn ein Teil schnell aufschlägt bzw. heißes Metall im Wasser liegt |

## Rechenzeit

| Situation | Kosten |
|---|---|
| ruhiges Wasser, Teile liegen still (gesunken oder schwimmend) | nichts (sie schlafen) |
| 200 schwimmende Klötze in 40-cm-Wellen | 0,04 ms je Physikschritt, auf alle Kerne verteilt (Jolt selbst: 0,06 ms) |
| Strömungsgitter (Fluss 8 m, 96 × 20 Zellen, drei Hindernisse) | 0,22 ms, 20-mal je Sekunde (etwa 0,4 % eines Kerns). Ruhiges Wasser ohne Strömung und Wellen hat kein Gitter |
| Grafik | ein Zeichenaufruf je Gewässer (höchstens 8 je Bild, die nächsten), ein paar Rechnungen je Pixel |

Die Wasserrechnung je Teil liest die Welt nur und schreibt nur in dieses Teil, deshalb läuft sie für alle Teile
gleichzeitig (`World::waterSteps`). Hohlräume werden einmal je Form berechnet (Teile mit mehr als 64 Stücken, z. B. mit
vielen Bohrungen, bekommen keine).

## Grenzen und Ausblick

- **Die Wassermenge fließt nicht um.** Die Strömung findet ihren Weg um Hindernisse, aber der Wasserspiegel bleibt (bis
  auf den Stau) überall gleich: Ein Damm quer über den ganzen Fluss hält die Strömung an, staut aber kein Wasser auf und
  wird nicht überspült; ein Becken läuft nicht über, und was aus einem Becher läuft, verschwindet. Dafür wäre der nächste
  Schritt ein **Höhenfeld (Flachwassergleichungen) je Chunk**, nur dort aktiv, wo sich etwas bewegt; das Strömungsgitter
  ist dafür schon die Grundlage.
- Hohlräume gehören zu einem Teil: Ein Boot aus mehreren geleimten Blechen hat (noch) keinen gemeinsamen Hohlraum. Holz
  schwimmt trotzdem, ein Boot aus Stahl muss aus einem Stück sein (z. B. Kasten mit Tasche).
- Keine mitbewegte Wassermasse („added mass“): Leichte Teile, die man unter Wasser drückt, schnellen etwas zu schnell hoch.
- Luft in Hohlräumen wird nicht zusammengedrückt (ein umgedrehter Becher steigt in der Tiefe nicht voll Wasser).
- Das Wasser hat eine feste Temperatur; Teile erwärmen es nicht.
- Im LAN-Mehrspieler rechnet der Gastgeber; die Mitspieler bekommen Gewässer, den Grund des Meers und die Uhr der Wellen.
- **Offene Welt:** Becken bleiben Quader. Seen und Flüsse der Welt sollen später Gewässer mit einer Grundkarte werden wie das
  Meer (Wasserspiegel je Gewässer, Grund aus dem Gelände); ein Fluss mit Gefälle braucht dazu noch einen Spiegel, der
  entlang des Flusses sinkt.
