# Befehle – vollständige Referenz

Befehle funktionieren überall gleich: in der **Konsole** (F1), in **Skripten** (`--script`), per **Fernsteuerung**
(`--send`) und **ohne Fenster** (`--headless`). Mehrere Befehle lassen sich kombinieren, siehe
[03-Befehlssprache.md](03-Befehlssprache.md).

**Alle Befehle, Schlüsselwörter, Materialien und Ausgaben sind englisch.** Die Menüs im Spiel und diese
Dokumentation sind deutsch.

## Allgemeine Regeln

- Groß-/Kleinschreibung ist egal.
- Zahlen mit Punkt oder Komma: `2.5` oder `2,5`.
- Text mit Leerzeichen in Anführungszeichen: `"My board"` oder `'My board'`.
- **`<id>`** ist die Teilnummer (`#17` in Listen, `17` und `#17` gehen beide). Statt einer Nummer geht auch:
  - `selected` (oder `sel`): das ausgewählte Teil (bei mehreren das zuletzt angeklickte)
  - `last`: das zuletzt erstellte Teil
- `[...]` = optional, `a|b` = eins davon.
- **Maße:** Längen und Positionen in cm, kleine Werkzeugmaße (Bohrer, Fräser, Radien, Fasen) in mm, Winkel in Grad.
- **Flächen** eines Teils (lokal): `+x -x +y -y +z -z` oder `right left top bottom front back`.
- **Lage auf einer Fläche** `a b` (cm) wird von der Flächenmitte aus gemessen: bei ±x entlang y und z,
  bei ±y entlang x und z, bei ±z entlang x und y.
- Befehle, die etwas verändern, lassen sich mit `undo` zurücknehmen.
- Fehler beginnen immer mit `Error:`.

## Übersicht

| Bereich | Befehle |
|---|---|
| Hilfe & Status | `help`, `status`, `profile`, `materials`, `chunks` |
| Erstellen | `new box`, `new cylinder`, `new tube`, `new sphere`, `new gear`, `new propeller`, `new wedge`, `new spike`, `import`, `copy` |
| Auslesen | `list`, `info`, `inspect`, `select`, `overlaps`, `edges` |
| Sägen & Bohren | `cut`, `drill`, `drillat`, `countersink` |
| Fräsen | `pocket`, `groove`, `rebate`, `slot`, `tenon`, `halflap` |
| Form | `round`, `chamfer`, `sand`, `plane`, `turn`, `bend`, `mirror`, `scale` |
| Formen kombinieren | `merge`, `subtract`, `intersect` |
| Lage | `position`, `move`, `rotation`, `rotate`, `drop`, `buildcollision` |
| Eigenschaften | `material`, `name`, `fix`, `axle`, `collision`, `unbreakable` |
| Verbindungen | `glue`, `nail`, `screw`, `weld`, `hinge`, `spring`, `rope`, `joints`, `joint`, `detach` |
| Löschen & Verlauf | `delete`, `undo`, `redo` |
| Physik | `sim`, `impulse`, `mode`, `breakage`, `air`, `wind` |
| Wärme & Feuer | `heat`, `ignite`, `extinguish`, `torch`, `tool heat` (siehe [12](12-Waerme-und-Feuer.md)) |
| Wasser | `water`, `water pool`, `water sea`, `water waves`, `water fill` … (siehe [14](14-Wasser.md)) |
| Ansicht & Bild | `view`, `camera`, `screenshot`, `window`, `display`, `tool` |
| Spielfigur | `player ...` |
| Zeichnung | `drawing` |
| Szene & Dateien | `scene`, `save`, `load`, `quit` |
| Befehlssprache | `wait`, `wait until`, `if`, `repeat`, `print`, `set` (siehe [03](03-Befehlssprache.md)) |

Ausführliche Erklärungen zu allen Bearbeitungen (wie sie rechnen, was realistisch erlaubt ist, Mauswerkzeuge):
[09-Bearbeitung.md](09-Bearbeitung.md).

---

## Hilfe & Status

### `help`
Zeigt alle Befehle und die Befehlssprache. Anderer Name: `?`.

### `status`
Zeigt den aktuellen Zustand: Physik an/aus, Anzahl Teile und Chunks, Auswahl, Rückgängig-Schritte und wie oft
ein Teil gerettet wurde (siehe [05](05-Physik-und-Materialien.md#ineinanderstecken-und-rettung)).
Mit Fenster zusätzlich: Modus, Ansicht, Werkzeug, Kamera, Spielfigur, Fenster, laufende Befehle, FPS, Physik, Fernsteuerung.

### `profile [reset|on|off]`
Wo bleibt die Rechenzeit? Zeigt seit dem letzten `profile reset` die Bilder pro Sekunde, das langsamste Bild, wie viele
Bilder über 50 ms dauerten (Ruckler) und je Engine-Abschnitt: Aufrufe, Summe, ms pro Sekunde, ms pro Aufruf, Höchstwert.
Beispiel: `profile reset ; wait 10 ; profile`. Die Abschnitte sind in [07](07-Engine-Aufbau.md#leistung-und-threads) erklärt.

```
> status
physics       paused (build mode)
parts         23 in 4 chunks, 0 active
selected      none
undo          0 steps
rescued       0 times a part was put back to its last free spot
mode          build
view          perspective
tool          select
camera        target 0.0 60.0 -50.0 cm, distance 3.40 m
windows       inspector off, drawing off, console off
commands      1 running
fps           166
physics       0.00 ms/frame, 0 steps (240/s), 0 contacts, 0 active, max penetration 0.0 mm
remote        active, port 7777
```

### `materials`
Listet alle Materialien mit Dichte, Haft- und Gleitreibung, Rollreibung, Stoßzahl, Zugfestigkeit, E-Modul und Biegbarkeit.
Verfügbar: **spruce** (Fichte), **oak** (Eiche), **brick** (Ziegel), **concrete** (Beton), **granite** (Granit),
**steel** (Stahl), **aluminium**, **brass** (Messing), **copper** (Kupfer), **rubber** (Gummi), **plastic** (Kunststoff ABS), **charcoal** (Holzkohle, bleibt übrig, wenn Holz abbrennt).
Bei Befehlen reichen die ersten drei Buchstaben (`spr`, `ste`, `alu`).

Alle Werte stehen in `assets/materials.json`. Ändern, neu anlegen, speichern und neu laden:
`materials keys | show <m> | set <m> <key> <value...> | new <name> [copy_of] | save [file] | reload [file] | file`
(siehe [13-Materialien.md](13-Materialien.md#befehle)).

```
> materials
spruce: 470 kg/m3, friction static 0.50 sliding 0.35, rolling 0.0050, restitution 0.35, tension 80 MPa, E 11 GPa, bend radius >= 50.00 x thickness
...
brick: 1800 kg/m3, friction static 0.65 sliding 0.55, rolling 0.0080, restitution 0.10, tension 3.5 MPa, E 15 GPa, not bendable
steel: 7850 kg/m3, friction static 0.60 sliding 0.45, rolling 0.0005, restitution 0.40, tension 360 MPa, E 210 GPa, bend radius >= 1.00 x thickness
```

### `chunks`
Zeigt die belegten 3D-Chunks (16 × 16 × 16 m) und wie viele Teile darin liegen.

---

## Erstellen

Ohne Position sucht das Programm automatisch einen freien Platz auf dem Boden
(mit Fenster: dort, wo die Kamera hinschaut). Mit Position `x y z` (cm) liegt der **Schwerpunkt** genau dort.
Das neue Teil wird ausgewählt und ist danach `last`.

### `new box <material> <L> <W> <H> [x y z]`
Quader bzw. Brett. **L** = Länge entlang x, **W** = Breite entlang z, **H** = Höhe/Dicke entlang y (alle cm).
Andere Namen statt `box`: `board`, `block`. Dünne Teile heißen „Board“, dünne Metallteile „Sheet“.

```
> new box spruce 80 10 2
Created: #24  Board  [spruce]  80.0 x 2.0 x 10.0 cm  0.752 kg  pos 0.0 1.0 0.0 cm  free
```
(Die Größe in Listen ist immer lokal x × y × z.)

### `new cylinder <material> <D> <H> [x y z]`
Stehender Zylinder (Achse = y), Durchmesser D und Höhe H in cm. Anderer Name: `rod`.

### `new tube <material> <D_out> <D_in> <L> [x y z]`
Rohr mit Achse y. Innendurchmesser muss kleiner als der Außendurchmesser sein. Anderer Name: `pipe`.

### `new sphere <material> <D> [x y z]`
Kugel mit Durchmesser D (cm). Anderer Name: `ball`.

### `new gear <material> <teeth> <module_mm> <width_cm> [bore_mm] [x y z]`
Stirnrad mit echter Evolventenverzahnung (Eingriffswinkel 20°), Achse = y.
Teilkreisdurchmesser = Zähne × Modul. Zwei Zahnräder greifen ineinander, wenn sie dasselbe Modul haben
und der Achsabstand (Zähne₁ + Zähne₂) × Modul / 2 beträgt.

```
new gear brass 20 4 1.5 10
```

### `new propeller <material> <D> [blades] [angle] [blade_width] [thickness_mm]`
Propeller mit Durchmesser D (cm), Standard 2 Blätter, 20° Blattwinkel (bei 75 % des Radius), Blattbreite D/8 (cm),
Blattdicke 3 mm. Achse = lokale y-Achse; dreht er positiv um y, entsteht Schub nach +y und ein bremsendes Luft-Moment.
Eine negative Drehzahl kehrt den Schub um, ein gespiegelter Propeller (`mirror … x`) ebenso.
Der Propeller wird ohne Positionsangabe erstellt, danach mit `position` und `rotation` an die richtige Stelle setzen.
Anderer Name: `fan`.

```
new propeller aluminium 30 ; axle last y 3000       dreht mit 3000 U/min, schiebt nach oben
```
Wichtig: Zwischen Nabe und Halter einige Zentimeter Luft lassen. Schleift die Nabe, bremst die Reibung den Motor.
Mehr in [08-Verbindungen-und-Bruch.md](08-Verbindungen-und-Bruch.md#propeller).

### `new wedge <material> <edge_L> <H> [angle=25] [x y z]`
Keil wie eine Axt- oder Meißelschneide: Schneide unten entlang der lokalen x-Achse, Länge `edge_L` (cm), Höhe H (cm),
ganzer Keilwinkel in Grad (Standard 25°, Axt ca. 25°, Spaltkeil 30–35°, Meißel 25–30°). Die Rückseite ist
2 · H · tan(Winkel/2) dick. Andere Namen: `axe`, `chisel`. Was ein Keil beim Aufprall anrichtet:
[08-Verbindungen-und-Bruch.md](08-Verbindungen-und-Bruch.md#form-des-werkzeugs-spitze-schneide-stumpf).

```
new wedge steel 10 8 25 0 300 0                    Axtkopf 10 cm Schneide, fällt aus 3 m
```

### `new spike <material> <D> <L> [tip_radius_mm=0] [x y z]`
Kegel mit der Spitze nach unten (Nagel, Dorn, Pfeilspitze): oben Durchmesser D (cm), Länge L (cm). Mit Spitzenradius
(mm) ist die Spitze rund (0 = spitz). Andere Namen: `nail`, `point`.

```
new spike steel 1 8 0 0 200 0                      spitz: bleibt in Holz stecken
new spike steel 1 8 5 0 200 0                      runde Spitze: nur ein kleines Loch
```

### `import <file.obj> [material] [scale]`
Lädt ein 3D-Modell im OBJ-Format (z. B. aus Blender: *Datei → Exportieren → Wavefront (.obj)*, Y oben, Meter).
Material Standard spruce, Skala Standard 1. Jedes Blender-Objekt bekommt eine eigene konvexe Kollisionshülle.
Masse = Volumen des Netzes × Dichte. Importierte Modelle lassen sich skalieren, aber (noch) nicht sägen, bohren oder fräsen.

```
import "C:\Models\chair.obj" oak
```

### `copy <id> [dx dy dz]`
Kopiert ein Teil. Ohne Versatz wird die Kopie direkt rechts daneben (x) gelegt.

---

## Auslesen

### `list`
Eine Zeile pro Teil: Nummer, Name, Material, Größe (lokal x × y × z), Masse, Position, Lagerung. Anderer Name: `ls`.

```
#17  Steel block (slides)  [steel]  6.0 x 6.0 x 6.0 cm  1.696 kg  pos -170.4 43.0 -68.0 cm  free
```

### `info <id>`
Alle festen Eigenschaften eines Teils:

```
> info 24
object        #24 "Board"
shape         box 80.0 x 10.0 x 2.0 cm (edited 2x)
material      spruce  density=470 kg/m3  friction static=0.50 sliding=0.35  rolling=0.0050  restitution=0.35
strength      tension=80.0 MPa  compression=40.0 MPa  E=11.00 GPa
size_cm       50.00 x 2.00 x 10.00  (local x/y/z)
volume        998.99 cm3
mass          0.4695 kg   weight 4.606 N
position_cm   14.99 1.00 -0.00  (center of mass)
rotation_deg  0.00 -0.00 0.00
world_bounds  -10.0 0.0 -5.0  to  40.0 2.0 5.0 cm
chunk         0 0 0
support       free
velocity_m/s  0.000 0.000 0.000  (|v|=0.000)
spin          0.00 rpm
geometry      20 convex pieces, 120 faces, 60 edges
collision     6 convex hull(s) (auto: holes and grooves are kept)
```

### `inspect <id>`
Live-Physikwerte (Kräfte, Reibung, Energie, Kippen …) wie im Physik-Inspektor. Öffnet mit Fenster auch den Inspektor (F3).
Kontaktkräfte sind erst messbar, wenn die Physik läuft (Spiel-Modus oder `sim`).
Siehe [05-Physik-und-Materialien.md](05-Physik-und-Materialien.md). Anderer Name: `physics`.

### `select [id ...] | select all`
Ohne Angabe: zeigt die aktuelle Auswahl. Mit Nummern: wählt diese Teile aus (mehrere möglich).

```
select 6 8 17
select all
```

### `overlaps <id>`
Zeigt, ob und wie tief ein Teil in anderen Teilen (oder im Boden) steckt.

```
> overlaps 9
#9  Steel cube  [steel]  5.0 x 5.0 x 5.0 cm  0.977 kg  pos 0.0 50.0 0.0 cm  free
  touches nothing deeper than 0.1 mm
```

### `edges <id> [selection]`
Listet die Kanten eines Teils mit Nummer (`e0`, `e1` …), Art (außen = `convex`, Innenecke/Hohlkehle = `concave`),
Länge, Lage, angrenzenden Flächen und Winkel. Die Nummern braucht man für `round` und `chamfer`.
Mit Auswahl nur einen Teil (gleiche Schreibweise wie bei `round`, z. B. `edges 9 +y`).

```
> edges 9
#9  Steel cube  [steel]  5.0 x 5.0 x 5.0 cm  0.981 kg  pos 45.0 92.5 -85.0 cm  free
  e0  convex   5.00 cm along z  from -2.50 2.50 -2.50 to -2.50 2.50 2.50 cm  faces -x / +y  90°
  e1  convex   5.00 cm along x  from -2.50 2.50 -2.50 to 2.50 2.50 -2.50 cm  faces -z / +y  90°
  ...
  e11  convex   5.00 cm along z  from 2.50 -2.50 -2.50 to 2.50 -2.50 2.50 cm  faces -y / +x  90°
12 of 12 edges
```

---

## Sägen & Bohren

### `cut <id> <x|y|z> <distance> [angle] [kerf_mm]`
Sägt quer zur **lokalen** Achse x, y oder z. Der Schnitt liegt `abstand` cm vom Ende entfernt
(gemessen vom kleinsten Wert der Achse). Anderer Name: `saw`.

| Parameter | Bedeutung |
|---|---|
| `winkel` | Gehrung in Grad (−60 … 60). Dreht die Schnittebene um die lokale y-Achse (bei Achse y: um x) |
| `fuge_mm` | Breite des Sägeblatts. 0 = verlustfreier Schnitt; sonst wird ein Spalt herausgesägt |

Das größere Stück behält die Nummer, die anderen bekommen neue Nummern („… (piece 2)“).
Zerfällt ein Teil in mehr als zwei Stücke (z. B. U-Form), entstehen entsprechend mehr Teile.

```
> cut 24 x 30
Cut -> 2 parts:
  #24  Board  [spruce]  50.0 x 2.0 x 10.0 cm  0.470 kg  pos 15.0 1.0 -0.0 cm  free
  #25  Board (piece 2)  [spruce]  30.0 x 2.0 x 10.0 cm  0.282 kg  pos -25.0 1.0 -0.0 cm  free

cut 24 x 30 45        45°-Gehrung
cut 24 x 30 0 3       mit 3 mm Schnittfuge
```

### `drill <id> <face> <a> <b> <D_mm> [depth_mm]`
Bohrt senkrecht in eine Fläche des Teils, `a b` = Lage auf der Fläche (cm von der Mitte).
Tiefe weglassen oder 0 = durchbohren.

```
> drill 24 top 10 0 8
Drilled: D 8.0 mm, removed 1.005 cm3 (0.0005 kg of chips)
  #24  Board  [spruce]  50.0 x 2.0 x 10.0 cm  0.470 kg  pos 15.0 1.0 -0.0 cm  free

drill 24 top 0 0 12 10      Sackloch Ø12, 10 mm tief
drill 24 +z 0 0 6 40        von vorne 40 mm tief
```

### `drillat <id> <x y z> <dx dy dz> <D_mm> [depth_mm]`
Bohren in beliebiger Richtung: Startpunkt (lokale cm, vom Schwerpunkt aus) und Richtung (lokal, beliebige Länge).

```
drillat 24 0 5 0 0 -1 1 8        schräg von oben
```

### `countersink <id> <face> <a> <b> <D_mm> [angle]`
Senkung für Senkkopfschrauben: ein Kegel mit Durchmesser D an der Oberfläche, Standard 90°.

```
> drill 35 top 0 0 5 ; countersink 35 top 0 0 10
Milled countersink D 10.0 mm, 90° on +y, removed 0.065 cm3
```

---

## Fräsen

Alle Fräsbefehle tragen Material in Quaderform ab (Ecken optional gerundet) und melden das abgetragene Volumen.

| Befehl | Wirkung |
|---|---|
| `pocket <id> <face> <a> <b> <length_mm> <width_mm> <depth_mm> [corner_mm]` | rechteckige Tasche (auch Zapfenloch). Länge entlang der ersten, Breite entlang der zweiten Flächenachse |
| `groove <id> <face> <entlang x\|y\|z> <offset_cm> <width_mm> <depth_mm>` | Nut quer über die ganze Fläche, Versatz von der Mitte |
| `rebate <id> <face> <side_face> <width_mm> <depth_mm>` | Falz: Stufe entlang der Kante zur Seitenfläche |
| `slot <id> <face> <a1> <b1> <a2> <b2> <width_mm> [depth_mm]` | Langloch von Punkt 1 nach Punkt 2 (Tiefe 0 = durch) |
| `tenon <id> <end_face> <length_cm> <thickness_mm> <width_mm>` | Zapfen: an der Stirnseite bleibt ein mittiger Zapfen stehen |
| `halflap <id> <end_face> <length_cm> [side_face]` | Überblattung: am Ende die halbe Dicke abnehmen (Standard: von oben) |

```
> new box oak 20 8 4 0 50 300
> pocket last top 0 0 40 12 15 3
Milled pocket 40.0 x 12.0 mm, 15.0 mm deep on +y, removed 7.084 cm3
> groove last -y x 0 6 4
Milled groove 6.0 x 4.0 mm along x on -y, removed 4.800 cm3
> rebate last top +z 10 5
Milled rebate 10.0 x 5.0 mm on +y, removed 10.000 cm3
> tenon last +x 3 12 30
Milled tenon 3.0 cm long, 12.0 x 30.0 mm on +x, removed 82.980 cm3
```

---

## Form

### `round <id> <edges> <radius_mm> [segments]` und `chamfer <id> <edges> <size_mm>`
Kanten abrunden bzw. fasen. Außenkanten verlieren Material, Innenkanten (Hohlkehlen) bekommen welches.
Rundungen bestehen aus kleinen Flächen (Standard 8 Segmente) und werden weich dargestellt.

| Kantenauswahl | Bedeutung |
|---|---|
| `all` | alle geraden Kanten |
| `convex` / `concave` | nur Außen- bzw. Innenkanten |
| `x` `y` `z` | Kanten entlang dieser lokalen Achse |
| `+y` oder `top` | alle Kanten der Fläche |
| `+y+x` oder `top+right` | die Kante zwischen zwei Flächen |
| `e3` oder `e1,e4,e7` | Kanten mit diesen Nummern (siehe `edges`) |
| `rims` | Kanten an runden Flächen, z. B. Lochränder (die anderen Auswahlen nehmen nur gerade Kanten) |

```
> round 9 +y 3
Rounded 4 edge(s) with 3.0 mm radius, volume -0.385 cm3 (-0.0030 kg)
> chamfer 9 e0 2
Chamfered 1 edge(s) with 2.0 mm, volume -0.094 cm3 (-0.0007 kg)
```

### `sand <id> [radius_mm]`
Schleifen: bricht alle geraden Kanten mit kleinem Radius (Standard 1 mm, 3 Segmente).

### `plane <id> <face> <mm>`
Hobeln: nimmt auf einer Fläche eine Schicht ab.

```
> plane 26 -x 2
Planed 2.00 mm off -x: 200.0 -> 198.0 mm
```

### `turn <id> <x|y|z> <from_cm> <to_cm> <D_mm> [D2_mm]`
Drehbank: zwischen `von` und `bis` (gemessen vom Ende, entlang der lokalen Achse) auf Durchmesser D abdrehen.
Mit D2 entsteht ein Kegel (D am Anfang, D2 am Ende). Die Drehachse geht durch die Mitte des Querschnitts.
So entstehen Wellen, Absätze, Einstiche und Kegel; auch eckige Teile lassen sich rund drehen.

```
> new cylinder steel 4 20 50 50 300
> turn last y 0 5 20
Turned 0.0-5.0 cm along y to D 20.0 mm, removed 47.124 cm3
> turn last y 5 12 30 20
Turned 5.0-12.0 cm along y to D 30.0-20.0 mm, removed 53.145 cm3
```

### `bend <id> <x|y|z> <at_cm> <angle> [inner_radius_mm]`
Biegen quer zur angegebenen Achse, `bei` cm vom Ende. Gebogen wird um die breitere der beiden anderen Richtungen,
zur dickeren Seite hin bei positivem Winkel. Die Länge der neutralen Faser bleibt erhalten (Volumen gleich).
Ohne Radius wird der kleinste erlaubte Radius genommen. Spröde Materialien (Ziegel, Beton, Granit) lassen sich nicht
biegen, Holz nur mit großem Radius (≥ 50 × Dicke), Metalle eng (Stahl, Aluminium ≥ 1 × Dicke, Messing 0,5, Kupfer 0,25).

```
> new box steel 30 4 0.3 100 50 300
Created: #24  Sheet  [steel]  30.0 x 0.3 x 4.0 cm  0.283 kg  pos 100.0 50.0 300.0 cm  free
> bend last x 10 90 3
Bent 90.0° around an inner radius of 3.0 mm, bend zone 7.1 mm (thickness 3.0 mm)
  #24  Sheet  [steel]  10.6 x 19.9 x 4.0 cm  0.283 kg  pos 93.6 56.5 300.0 cm  free
> new box brick 24 11.5 7.1 ; bend last x 10 30
Error: brick is brittle and cannot be bent - it would break
```

### `mirror <id> <x|y|z> [copy]`
Spiegelt die Form an der lokalen Ebene durch den Schwerpunkt. Mit `copy` entsteht eine gespiegelte Kopie daneben
(z. B. linkes und rechtes Seitenteil).

### `scale <id> <factor>` oder `scale <id> <fx> <fy> <fz>`
Vergrößert oder verkleinert ein Teil (0,01 … 100, je Achse möglich). Die Masse wird neu berechnet.

```
> scale 27 2
Scaled (mass 0.6235 -> 4.9879 kg): #27  Block 2  [oak]  20.0 x 20.0 x 20.0 cm  4.988 kg  pos 250.0 4.5 300.0 cm  free
```

---

## Formen kombinieren

| Befehl | Wirkung |
|---|---|
| `merge <a> <b> [c ...]` | Teile zu einem verschmelzen. Sie müssen sich berühren und aus demselben Material sein (sonst: `glue` oder `weld`). Verbindungen von b gehen auf a über |
| `subtract <a> <tool> [keep]` | die Form des Werkzeug-Teils aus a herausschneiden (z. B. eine Kugel als Mulde). Das Werkzeug verschwindet, außer mit `keep` |
| `intersect <a> <tool> [keep]` | nur das gemeinsame Volumen behalten |

```
> merge 25 26
Merged into one part: #25  Block  [spruce]  20.0 x 2.0 x 10.0 cm  0.188 kg  pos 205.0 1.0 300.0 cm  free
> subtract 27 28
Cut the tool shape out, removed 134.041 cm3 (tool part removed):
  #27  Block 2  [oak]  10.0 x 10.0 x 10.0 cm  0.623 kg  pos 250.0 4.5 300.0 cm  free
> intersect 33 34
Kept the common volume, removed 202.119 cm3 (tool part removed):
  #33  Block 6  [oak]  10.0 x 10.0 x 10.0 cm  0.574 kg  pos 500.0 50.0 0.0 cm  free
```

---

## Lage

| Befehl | Wirkung |
|---|---|
| `position <id> x y z` | Schwerpunkt auf die Weltposition (cm) setzen. Steckt das Teil danach in einem anderen, kommt eine Warnung. Anderer Name: `pos` |
| `move <id> dx dy dz [through]` | um dx dy dz (cm) verschieben. Das Teil **hält am ersten Hindernis an** und gleitet daran entlang; mit `through` geht es hindurch |
| `rotation <id> rx ry rz` | Drehung absolut setzen (Euler-Winkel in Grad, Reihenfolge x-y-z) |
| `rotate <id> <x\|y\|z> <deg>` | um eine **Welt**-Achse durch den Schwerpunkt weiterdrehen |
| `drop <id>` | senkrecht fallen lassen, bis es auf etwas aufliegt (Boden oder anderes Teil) |
| `buildcollision [on\|off]` | im Bau-Modus stoßen verschobene Teile an anderen an (Standard `on`, gilt für Maus und `move`) |

```
> move 30 50 0 0
#30  Block 4  [spruce]  10.0 x 10.0 x 10.0 cm  0.470 kg  pos 420.0 5.0 0.0 cm  free
  stopped after 20.00 of 50.00 cm at #31 Block 5 (add 'through' to ignore)
```

---

## Eigenschaften

| Befehl | Wirkung |
|---|---|
| `material <id> <material>` | Material ändern (Masse wird neu berechnet) |
| `name <id> <text>` | umbenennen (Text darf Leerzeichen enthalten) |
| `fix <id> [on\|off]` | fest verankern (unbeweglich, z. B. Werkbank). Ohne Angabe: on |
| `axle <id> <x\|y\|z\|off> [rpm] [Nm]` | Drehlager um die lokale Achse durch den Schwerpunkt, optional mit Motor (Drehzahl, maximales Moment, Standard 5 N·m) |
| `collision <id> auto\|exact\|hull` | Kollisionsform für die Physik (siehe unten) |
| `unbreakable <id> [on\|off]` | Teil zerbricht nie beim Aufprall. Ohne Angabe: on |

```
axle 19 y 20 3        Zahnrad dreht sich mit 20 U/min, max. 3 N·m
axle 20 y             frei drehbar
axle 20 off
```

**Kollisionsform**: Die Physik rechnet nicht mit dem Anzeigenetz, sondern mit konvexen Hüllen.

| Form | Bedeutung |
|---|---|
| `auto` (Standard) | Unbearbeitete Quader, Zylinder und Kugeln: exakte Grundform. Bearbeitete Teile: benachbarte Stücke werden zusammengefasst, solange dabei **keine Lücke tiefer als 0,5 mm zugeschüttet** wird. Bohrlöcher, Nuten, Taschen und Zahnlücken bleiben also offen (ein Stift passt ins Loch), glatte Bereiche werden zu wenigen großen Hüllen |
| `exact` | Toleranz 0,05 mm (fast jedes Stück einzeln, für sehr enge Passungen) |
| `hull` | eine einzige Hülle um das ganze Teil (am schnellsten, Löcher sind für die Physik zu) |

`info <id>` zeigt in der Zeile „collision“, wie viele Hüllen ein Teil gerade hat. Genaueres:
[05-Physik-und-Materialien.md](05-Physik-und-Materialien.md#kollisionsformen).

---

## Verbindungen

Alle Verbindungen haben eine Tragfähigkeit und brechen bei Überlast (außer mit `breakage off` oder „unbreakable“).
Ausführlich mit Formeln und Tabellen: [08-Verbindungen-und-Bruch.md](08-Verbindungen-und-Bruch.md).
**a** ist das haltende Teil, **b** das gehaltene. Ist a fixiert, wird b direkt an der Welt befestigt.

| Befehl | Wirkung |
|---|---|
| `glue <a> <b> [glue] [strength_MPa] [gap_mm]` | über die Berührfläche kleben. Kleber: `woodglue` (Standard), `epoxy`, `superglue`, `pu`, `hotglue`, `construction` |
| `nail <a> <b> [count] [length_mm] [d_mm]` | nageln (Holz) |
| `screw <a> <b> [count] [length_mm] [d_mm]` | schrauben |
| `weld <a> <b> [seam_mm]` | schweißen (Metall) |
| `hinge <a> <b> <x\|y\|z> [rpm] [Nm]` | b dreht sich um seine lokale Achse, gelagert in a; mit Drehzahl als Motor |
| `spring <a> <b> [N/m] [N·s/m] [max_N]` | Feder (Ruhelänge = jetziger Abstand der Oberflächen) |
| `rope <a> <b> [length_cm] [max_N]` | Seil (Standardlänge = jetziger Abstand) |
| `joints [id]` | alle Verbindungen (oder nur die eines Teils) mit Belastung |
| `joint <nr>` | alle Werte einer Verbindung |
| `joint <nr> <setting> <value>` | ändern: `strength shear gap count length diameter seam force stiffness damping motor unbreakable` |
| `detach <nr>` / `detach all` | Verbindung entfernen |

Verbindungen haben eigene Nummern (`J1`, `J2` …); bei `joint` und `detach` geht `3` oder `J3`.

```
> glue 13 14 construction
Joined: J1  glue  #13 Brick 1 <-> #14 Brick 2  load 0 %
  glue          construction adhesive, strength 1.5 MPa, shear 2.0 MPa, max. gap 5.0 mm
  area          276.00 cm²
  holds         tension 41400 N, shear 55200 N, moment 1146.3 N·m
  load          0.0 %  (force 0.0 N, moment 0.00 N·m)
  breaks        at 100 % load
> nail 1 6 4
Joined: J2  nails  #1 Workbench top <-> #6 Spruce board  load 0 %
  nails         4 x 4.0 x 100 mm, part a 40.0 mm thick, penetration 60 mm
  holds         tension 4241 N, shear 5337 N, moment 452.8 N·m
  load          0.0 %  (force 0.0 N, moment 0.00 N·m)
  breaks        at 100 % load
> joint 1 strength 0.5
> joints
J1  glue  #13 Brick 1 <-> #14 Brick 2  load 0 %
J2  nails  #1 Workbench top <-> #6 Spruce board  load 0 %
2 joint(s), breakage on, broken so far: 0
```

Bricht eine Verbindung oder ein Teil, erscheint eine Meldung mit `!!` (in der Konsole, als Hinweis im Bild,
bei `sim` und in Befehlsfolgen).

---

## Löschen & Verlauf

| Befehl | Wirkung |
|---|---|
| `delete <id>` | Teil löschen. Andere Namen: `remove`, `del` |
| `delete all` | alle Teile löschen |
| `undo` | letzten Schritt zurücknehmen (bis zu 50) |
| `redo` | zurückgenommenen Schritt wiederherstellen |

---

## Physik

### `mode build | play`
Wechselt den Modus (wie Tab). `play` startet die Physik, `build` hält sie an.
Stecken beim Start Teile ineinander, kommt ein Hinweis; die Physik schiebt sie langsam auseinander.

### `sim <seconds>`
Lässt die Physik genau so lange laufen (auch im Bau-Modus) und meldet, welche Teile sich bewegt haben.
Ruhende Teile werden dafür angestoßen. Anderer Name: `simulate`.

```
> sim 1
simulated 1.00 s. Moved parts:
  #14 Brick 2: moved 0.05 cm, now -90.0 10.6 50.0 cm, v=0.000 m/s (at rest)
  #15 Brick 3: moved 0.10 cm, now -90.0 17.7 50.0 cm, v=0.000 m/s (at rest)
  #17 Steel block (slides): moved 84.90 cm, now -245.9 4.2 -68.0 cm, v=0.565 m/s
  #18 Rubber block (sticks): moved 0.06 cm, now -170.4 43.0 -52.0 cm, v=0.000 m/s (at rest)

> new box brick 24 11.5 7.1 0 150 0 ; sim 1.5
simulated 1.50 s. Moved parts:
  #24 Block: moved 146.46 cm, now 2.5 3.6 -0.2 cm, v=0.000 m/s (at rest)
!! Block shattered (2 pieces): impact 50.1 J, breaking limit 24.5 J
```
(Der Stahlklotz rutscht mit Gleitreibung Holz/Metall 0,35 die 30°-Rampe hinunter, der Gummiklotz haftet mit 0,8.)

### `impulse <id> jx jy jz`
Gibt dem Teil einen Kraftstoß in N·s (Impuls) am Schwerpunkt. Geschwindigkeitsänderung = Kraftstoß / Masse.
Sichtbar wird er, sobald die Physik läuft. Anderer Name: `push`.

### `breakage [on|off]`
`on` (Standard): Verbindungen brechen bei Überlast, spröde Teile zerbrechen beim harten Aufprall.
`off`: alles hält (zum Bauen und Ausprobieren). Ohne Angabe: zeigt die Einstellung. Auch im Menü **Physik**.

### `air [density <kg/m³> | vacuum | normal | wind x y z]`, `wind x y z`
Luft für Auftrieb, Luftwiderstand und Propeller. Standard: 1,204 kg/m³ (20 °C), kein Wind.
Wind in m/s als Richtung x y z. Ohne Angabe: zeigt die Einstellung. Auch im Menü **Physik**.

```
> wind 5 0 0
air: density 1.204 kg/m3, wind 5.00 0.00 0.00 m/s (5.00 m/s = 18.0 km/h)
> air vacuum
air: density 0.000 kg/m3 (vacuum), wind 5.00 0.00 0.00 m/s (5.00 m/s = 18.0 km/h)
```
`air vacuum` behält den eingestellten Wind (ohne Luft wirkt er aber nicht), `air normal` setzt beides zurück.

---

## Wärme & Feuer

Das Modell (Wärmeleitung, Brennen, Schmelzen, Kälte) steht in [12-Waerme-und-Feuer.md](12-Waerme-und-Feuer.md).

### `heat`
Überblick:
```
heat          on, air 20.0 °C, time x5.0
parts         18 warmer or colder than the air, 6 burning
fires         1 (70.2 kW)
  at -4.6 -0.0 1.1 cm: 70.2 kW, flames 95 cm high, 1039 °C
hottest       #6 Kindling 821 °C
```

### `heat <id> [°C]`
Ohne Zahl: Temperatur, Zustand und Kennwerte. Mit Zahl: das ganze Teil auf diese Temperatur setzen (über dem
Schmelzpunkt: flüssig).
```
> heat 9
#9 Steel bar over the fire
temperature   mean 220 °C, min 46 °C, max 452 °C  (air 20 °C)
state         softened: 94 % strength at the hottest spot
material      λ 50.00 W/mK, c 490 J/kgK, ε 0.70, melts at 1510 °C, soft from 350 °C (half strength at 600 °C), brittle below -20 °C
```
`info <id>` zeigt dieselben Zeilen am Ende.

### `heat <id> map`
Das Temperaturgitter des Teils Schicht für Schicht (y von oben nach unten), °C je Zelle. `*` brennt, `~` flüssig,
`#` verkohlt, `.` kein Material:
```
Board: 10 x 1 x 2 cells of 6.0 x 2.0 x 5.0 cm (local x right, z down per layer)
layer y0
     .#   636*   438*   162     43     35     31     28     26     25
   641*   604*   435*   161     43     35     31     28     26     25
```

### `heat on|off`, `heat ambient <°C>`, `heat speed <x>`
Wärme rechnen oder alle Temperaturen einfrieren; Luft- und Bodentemperatur; Zeitraffer nur für die Wärme (0,1 … 100).

### `ignite <id> [face]`
Holz, Gummi oder Kunststoff anzünden, ohne Fläche an der untersten Stelle. Stahl, Stein und Co. brennen nicht
(Fehlermeldung), ohne Luft (`air vacuum`) auch nichts.

### `extinguish <id|all>`
Wasser: brennende Stellen gehen aus und haben danach höchstens 100 °C.

### `torch <id> [tool] [seconds=5] [face=top]`
Eine Flamme oder Kälte auf die Mitte einer Fläche richten. `tool`: `lighter` (Feuerzeug, 1000 °C), `gas` (Gasbrenner,
1900 °C), `welding` (Schweißbrenner, 3100 °C), `spray` (Kältespray, −55 °C), `nitrogen` (Flüssigstickstoff, −196 °C).
```
torch 3 welding 60 right ; wait 60 ; heat 3
```

## Wasser

Alle Befehle mit Beispielen und das Modell (Auftrieb, Hohlräume, Wellen, Abschrecken): [14-Wasser.md](14-Wasser.md).

```
water pool 200 150 50 walls      # Becken 2 x 1,5 m, 50 cm tief, mit Betonrand (hier auf dem Boden)
water waves all 30               # Wellen, 30 cm
new box spruce 40 40 10 0 60 0 ; wait 3 ; water last
```

---

## Ansicht & Bild (nur mit Fenster)

| Befehl | Wirkung |
|---|---|
| `view perspective\|top\|front\|side\|quad` | Kameraansicht (auch `1`–`5`) |
| `camera x y z [distance]` | Baukamera schaut auf den Punkt (cm), Abstand in m (Werte über 10 werden als cm gelesen) |
| `screenshot <file.png>` | Bild des nächsten Frames speichern. Anderer Name: `photo` |
| `window inspector\|drawing\|console\|help [on\|off]` | Fenster öffnen/schließen (ohne on/off: umschalten) |
| `display shadows\|edges\|grid [on\|off]` | Darstellung umschalten |
| `tool select` | Werkzeug Auswahl/Bewegen |
| `tool saw [x\|y\|z] [angle] [kerf_mm]` | Säge mit Einstellungen |
| `tool drill [D_mm] [depth_mm]` | Bohrer mit Einstellungen |
| `tool round [radius_mm] [chamfer]` | Abrunden (mit `chamfer`: Fasen) per Mausklick auf eine Kante. `tool chamfer [mm]` geht auch |
| `tool mill [L W depth]` | Tasche fräsen per Mausklick auf eine Fläche (mm) |
| `tool plane [mm]` | Hobeln per Mausklick auf eine Fläche |
| `tool heat [lighter\|gas\|welding\|spray\|nitrogen]` | Wärme-Werkzeug: im Bau-Modus Maustaste auf einem Teil halten, im Spiel-Modus F halten (der Modus bleibt) |

---

## Spielfigur (nur mit Fenster)

Alle `player`-Befehle (außer der Abfrage) schalten bei Bedarf automatisch in den Spiel-Modus.

| Befehl | Wirkung |
|---|---|
| `player` | Position (Füße), Blickrichtung, am Boden, was getragen wird, was angeschaut wird |
| `player position x y z` | Figur dorthin versetzen (cm, Füße). Anderer Name: `teleport` |
| `player look <id>` | zum Mittelpunkt eines Teils schauen |
| `player look x y z` | zu einem Punkt (cm) schauen |
| `player turn <deg> [pitch]` | nach rechts (+) / links (−) drehen; optional Neigung absolut (Grad, + = hoch) |
| `player walk <forward\|back\|left\|right> <seconds> [run]` | eine Zeit lang laufen (4 m/s, rennen 7 m/s). `player run …` rennt. Schräg wie zwei Tasten: `forward-right`, `back-left` … |
| `player stop` | stehen bleiben |
| `player jump` | springen |
| `player view [first\|third]` | durch die Augen der Figur oder von hinten über die Schulter (wie **F5**); ohne Angabe umschalten |
| `player grab [id]` | Teil aufnehmen (mit id: vorher hinschauen). Reichweite 3,5 m, nicht durch andere Teile hindurch |
| `player release` | Teil fallen lassen. Anderer Name: `drop` |
| `player throw [N·s]` | getragenes Teil in Blickrichtung werfen (Standard: 9 m/s, höchstens 45 N·s) |

```
> player
position_cm   -230.0 0.0 -160.0  (feet)
view          heading -147.1°, pitch -48.1°
on_ground     yes
holding       nothing
looking_at    #16 Ramp 30° (1.68 m)
mode          play
```

---

## Zeichnung

### `drawing [id ...] [file.svg]`
Erstellt die **Dreitafelprojektion** (Vorderansicht, Draufsicht, Seitenansicht von links) der angegebenen Teile,
ohne Nummern der Auswahl. Mit Dateiname wird sie als SVG (A4 quer) gespeichert, mit Fenster öffnet sie sich (F4).
Anderer Name: `threeview`. Details: [06-Dreitafelprojektion.md](06-Dreitafelprojektion.md).

```
> new box spruce 80 10 2 ; cut last x 30 ; drill selected top 10 0 8 ; drawing selected board.svg
...
three-view drawing: Board
scale 1:5 on A4 landscape, first-angle projection
overall size W x H x D: 500 x 20 x 100 mm
material: spruce, mass 0.4695 kg
front view: 4 visible, 2 hidden line segments, 1 center lines
top view: 20 visible, 0 hidden line segments, 2 center lines, holes/cylinders: Ø8
left side view: 4 visible, 2 hidden line segments, 1 center lines
SVG saved: board.svg
```
(In der Vorderansicht ist die Bohrung als zwei verdeckte Linien zu sehen, in der Draufsicht als Kreis mit Ø8.)

---

## Szene & Dateien

| Befehl | Wirkung |
|---|---|
| `scene empty` | alle Teile entfernen (Nummern beginnen wieder bei 1) |
| `scene demo` | Beispiel-Werkstatt laden (Werkbank, Bretter, Rampe, Getriebe …) |
| `scene showcase` | große Beispielwelt mit allem (siehe unten) |
| `scene water` | Wasserbecken: Holz, Stahl, Gummi, Floß, Stahlboot, Becher, glühender Stahl ([14](14-Wasser.md)) |
| `scene river` | Fluss: Kanal mit Strömung, Felsen, Brückenpfeiler, Treibgut ([14](14-Wasser.md)) |
| `scene fire` | Feuerstelle: Lagerfeuer, Stahlstab darüber, Aluminium, Kunststoff, Gummi, geleimtes Brett, gefrorener Stahl ([12](12-Waerme-und-Feuer.md)) |
| `save <file>` | Szene speichern (Textformat `.werkstatt`) |
| `load <file>` | Szene laden (auch Dateien aus der Zeit vor den englischen Befehlen) |
| `quit` | Programm beenden. Anderer Name: `exit` |

### Teile der Beispiel-Werkstatt

Nach `scene demo` (oder beim Start) haben die Teile immer diese Nummern:

| # | Teil | # | Teil |
|---|---|---|---|
| 1 | Workbench top (fest) | 13–15 | Brick 1–3 (Stapel) |
| 2–5 | Workbench leg 1–4 (fest) | 16 | Ramp 30° (fest) |
| 6 | Spruce board 80 cm | 17 | Steel block (slides): rutscht die Rampe herunter |
| 7 | Spruce board short | 18 | Rubber block (sticks): haftet auf der Rampe |
| 8 | Brick | 19 | Small gear (motor), 20 U/min |
| 9 | Steel cube | 20 | Large gear |
| 10 | Aluminium rod | 21–22 | Shaft (fest) |
| 11 | Granite block | 23 | Bearing post (fest) |
| 12 | Rubber ball | | |

### Die Beispielwelt (`scene showcase`)

Enthält die Beispiel-Werkstatt und rundherum (Menü **Datei → Beispiel-Welt (alles)**):

| Station | Was passiert (Tab drücken) |
|---|---|
| Kugelbahn | Drei Kugeln (Stahl, Gummi, Eiche) rollen in einer gefrästen Rille herunter und stoßen 14 Dominosteine um |
| Kugelstoßpendel | Fünf Stahlkugeln an je zwei Seilen, die erste ist angehoben |
| Propeller-Waage | Ein Propeller (3000 U/min) hebt seinen Hebelarm gegen ein Gegengewicht an: Auftrieb |
| Propeller-Karussell | Der Schub eines Propellers treibt einen Arm im Kreis: Kraft nach vorne |
| Hängebrücke | Acht Eichenbretter an Seilen, ein Gummiball rollt in die Mitte |
| Feder | Ein 7,85-kg-Stahlklotz schwingt an einer Feder |
| Wippe | Ein Eichenklotz fällt auf eine Wippe (Scharnier), ein Gummiball wird hochgeworfen |
| Stift im Loch | Stift Ø18 fällt durch das Loch Ø22, bleibt auf dem Loch Ø12 (mit Senkung) liegen |
| Werkstücke-Tisch | Alle Bearbeitungen: abgerundet, gefast, Schale, gedrehter Stab, Zapfen und Zapfenloch, gebogener Winkel, Senkungen, T-Stück, Langloch, Kreuzüberblattung, geschliffen, Falz, Gehrung mit Spiegelkopie |
| Regal | Ein Brett mit schwachem Heißkleber bricht unter dem Stahlklotz, das mit Epoxid hält |
| Abrissbirne | Eine 14-kg-Stahlkugel am Seil schwingt in eine Ziegelmauer |
| Axt und Spaltholz | Axt aus **einem** Teil (Keil-Kopf und Stahlrohr-Stiel mit `merge` vereint, 2,4 kg) neben zwei Fichtenbrettern (2 und 3 cm). Mit der Schneide entlang der Faser geworfen spaltet sie ein Brett, quer zur Faser bleibt sie stecken |

Die ganze Welt ist eine Befehlsfolge in `src/Showcase.cpp`. Dort kann man nachlesen, wie jede Station gebaut ist.
