# Bearbeitung – Abrunden, Fräsen, Drehen, Biegen, Formen kombinieren

Alle Bearbeitungen gibt es als **Befehl** (Kurzreferenz in [02-Befehle.md](02-Befehle.md)) und die wichtigsten
auch mit der **Maus** (Werkzeug-Fenster und Bereich „Bearbeiten“ im Eigenschaften-Fenster). Was man mit der Maus
macht, steht danach als Befehl in der Konsole (F1), lässt sich also abschreiben und in Skripten wiederholen.
Jede Bearbeitung lässt sich mit `undo` (Strg+Z) zurücknehmen.

| Bearbeitung | Befehl | Maus |
|---|---|---|
| Sägen, auch auf Gehrung | `cut` | Werkzeug **Säge (S)** |
| Bohren, Senken | `drill`, `drillat`, `countersink` | Werkzeug **Bohrer (B)** |
| Kanten abrunden / fasen / schleifen | `round`, `chamfer`, `sand`, `edges` | Werkzeug **Abrunden / Fasen (E)**, „Bearbeiten“ |
| Tasche, Zapfenloch | `pocket` | Werkzeug **Fräsen (M)** |
| Nut, Falz, Langloch, Zapfen, Überblattung | `groove`, `rebate`, `slot`, `tenon`, `halflap` | – |
| Hobeln | `plane` | Werkzeug **Hobeln (P)** |
| Drehbank | `turn` | „Bearbeiten“ → Drehbank |
| Biegen | `bend` | „Bearbeiten“ → Biegen |
| Spiegeln, Skalieren | `mirror`, `scale` | „Bearbeiten“ |
| Vereinen, Ausschneiden, Schnittmenge | `merge`, `subtract`, `intersect` | zwei Teile wählen → „Formen kombinieren“ |

## Wie die Geometrie rechnet

Jedes Teil besteht aus **konvexen Stücken** (siehe [07-Engine-Aufbau.md](07-Engine-Aufbau.md)). Jede Bearbeitung
ist eine von drei exakten Grundoperationen:

1. **Schneiden mit einer Ebene** (Sägen, Hobeln, Biegezone abtrennen),
2. **ein konvexes Werkzeug abziehen** (Bohrer, Senker, Fräser-Quader, Abrund-Keile),
3. **konvexe Bereiche hinzufügen** (Hohlkehle an einer Innenkante, Vereinen).

Volumen, Masse und Schwerpunkt bleiben dadurch genau (der Selbsttest prüft das, z. B. Bohrung = π·r²·h).
Rund ist intern ein Vieleck mit gleicher Fläche wie der Kreis (16–96 Ecken). Für die Darstellung bekommen
runde Flächen weiche Normalen, sie sehen also glatt aus.

Um ein Bohrloch herum wird das Material in **Tortenstücke um die Lochachse** zerlegt (je ein Stück pro Vieleckseite).
So entstehen kräftige Stücke ohne hauchdünne Splitter, was für die Kollision wichtig ist
(siehe [05](05-Physik-und-Materialien.md#kollisionsformen)).

## Kanten: abrunden, fasen, schleifen

`edges <id>` listet die Kanten eines Teils. Eine Kante ist eine Linie, an der zwei Außenflächen in verschiedener
Richtung (mindestens 20° Knick) zusammentreffen:

- **convex** (Außenkante): das Material liegt im Winkel unter 180°, z. B. jede Kante eines Quaders.
- **concave** (Innenkante, Hohlkehle): z. B. die Innenecke eines L-Profils oder der Grund einer Nut.

Innenflächen zwischen zwei konvexen Stücken desselben Teils zählen nicht.

**Abrunden** (`round <id> <edges> <radius_mm> [segments]`): Der Rundungsbogen berührt beide Flächen.
Er wird aus `segmente` geraden Sehnen gebildet (Standard 8). Bei einer Außenkante wird je Sehne der keilförmige
Bereich zwischen Kante und Sehne abgezogen. Bei einer Innenkante wird derselbe Keil aufgefüllt.
Die Rundung liegt innerhalb der Kantenlänge. An Ecken, an denen drei gerundete Kanten zusammentreffen,
schneiden sich die Rundungen (keine Kugelecke wie bei einer echten Fräse, für das Aussehen meist egal).

**Fasen** (`chamfer`): eine einzige Sehne, die auf beiden Flächen `fase_mm` von der Kante entfernt ansetzt.

**Schleifen** (`sand <id> [mm]`): bricht alle geraden Kanten mit kleinem Radius (Standard 1 mm, 3 Segmente).

Kantenauswahl:

| Auswahl | Bedeutung |
|---|---|
| `all` | alle geraden Kanten |
| `convex` / `concave` | nur Außen- / Innenkanten |
| `x` `y` `z` | alle geraden Kanten entlang dieser lokalen Achse (z. B. die 4 langen Kanten eines Bretts: `x`) |
| `+y` / `top` | alle geraden Kanten einer Fläche |
| `+y+x` / `top+right` | die Kante zwischen zwei Flächen |
| `e3`, `e1,e4,e7` | Kanten nach Nummer (aus `edges`) |
| `rims` | Kanten an runden Flächen, z. B. Lochränder (jedes Vieleck-Stück einzeln) |

Der Abrund-Radius sollte kleiner als die halbe Dicke bleiben. Sonst schneiden sich die Rundungen gegenüberliegender
Kanten und das Teil wird dünner als gedacht (das ist dann aber geometrisch richtig).

```
> round 9 +y 3
Rounded 4 edge(s) with 3.0 mm radius, volume -0.385 cm3 (-0.0030 kg)
> chamfer 9 e0 2
Chamfered 1 edge(s) with 2.0 mm, volume -0.094 cm3 (-0.0007 kg)
```

Prüfung im Selbsttest: Eine Rundung mit r = 10 cm an einer 1 m langen Würfelkante nimmt 2,196 dm³ ab
(Kreis: 2,146 dm³; das 8-Eck-Viertel liegt etwas innen). Eine Fase von 20 cm nimmt genau s²/2 = 20 dm³ ab.

### Mit der Maus

Werkzeug **Abrunden / Fasen (E)**. Fährt die Maus über ein Teil, leuchtet die nächste Kante
(gelb = Außenkante, blau = Innenkante), dazu dünnere Linien, wo die Rundung auf den Flächen beginnt.
Ein Klick rundet ab (Radius im Werkzeug-Fenster, Haken „Fase statt Rundung“ für Fasen).

Im Eigenschaften-Fenster unter **Bearbeiten**: „Alle Kanten abrunden“, „fasen“, „schleifen“ mit eingestelltem Radius.

## Fräsen

| Befehl | Form |
|---|---|
| `pocket <id> <face> <a> <b> <length_mm> <width_mm> <depth_mm> [corner_mm]` | rechteckige Tasche, mittig bei a b. Länge entlang der ersten, Breite entlang der zweiten Flächenachse (±y: x, z; ±x: y, z; ±z: x, y). Mit Eckradius sind die Ecken rund wie bei einem echten Fräser |
| `groove <id> <face> <along> <offset_cm> <width_mm> <depth_mm>` | Nut, läuft entlang der Achse über die ganze Fläche, Versatz quer dazu von der Mitte |
| `rebate <id> <face> <side_face> <width_mm> <depth_mm>` | Falz: Stufe an der Kante zwischen Fläche und Seitenfläche |
| `slot <id> <face> <a1> <b1> <a2> <b2> <width_mm> [depth_mm]` | Langloch mit runden Enden (Tiefe 0 = durch) |
| `tenon <id> <end_face> <length_cm> <thickness_mm> <width_mm>` | Zapfen: an der Stirnseite bleibt ein mittiger Zapfen stehen, Dicke entlang der dünneren Richtung |
| `halflap <id> <end_face> <length_cm> [side_face]` | Überblattung: am Ende die halbe Dicke abnehmen (Standard: von der dünneren Seite, oben) |
| `countersink <id> <face> <a> <b> <D_mm> [angle]` | Senkung (Kegel) für Senkkopfschrauben, Standard 90° |

Die Werkzeuge ragen 1 mm über die Oberfläche hinaus, damit die Fläche sauber offen ist.

### Beispiel: Zapfenverbindung, verleimt

```
new box oak 30 6 4 0 50 0 ; set rail = last ; tenon $rail +x 3 12 30
new box oak 10 8 6 17 50 0 ; set post = last ; pocket $post -x 0 0 12 30 31
overlaps $rail
glue $post $rail woodglue
```
```
Milled tenon 3.0 cm long, 12.0 x 30.0 mm on +x, removed 61.200 cm3
Milled pocket 12.0 x 30.0 mm, 31.0 mm deep on -x, removed 11.160 cm3
  touches nothing deeper than 0.1 mm
Joined: J1  glue  #2 Block <-> #1 Board  load 0 %
  glue          wood glue (PVA, D3), strength 6.0 MPa, shear 10.0 MPa, max. gap 0.3 mm
  area          45.60 cm²
```
Der Zapfen (3 cm lang, 12 × 30 mm) steckt im Zapfenloch (31 mm tief, 1 mm Luft am Grund). Verleimt werden die
Schultern und die Zapfenwände: 45,6 cm². Fest verankert trägt die Verbindung einen 7,85-kg-Stahlklotz
mit 6 % Auslastung.

### Mit der Maus

Werkzeug **Fräsen (M)**: Länge, Breite, Tiefe und Eckradius einstellen, mit der Maus auf eine ebene Fläche zeigen
(grünes Rechteck, rastet am Raster ein), klicken = Tasche fräsen.

## Hobeln

`plane <id> <face> <mm>` nimmt eine Schicht von der Fläche ab (z. B. ein Brett auf Maß bringen).
Werkzeug **Hobeln (P)**: auf eine Fläche klicken nimmt die eingestellte Dicke ab.

```
> plane 26 -x 2
Planed 2.00 mm off -x: 200.0 -> 198.0 mm
```

## Drehbank

`turn <id> <x|y|z> <from_cm> <to_cm> <D_mm> [D2_mm]` dreht das Teil zwischen `von` und `bis` (vom Ende gemessen)
auf den Durchmesser D ab. Mit D2 entsteht ein Kegel. Die Drehachse geht durch die Mitte des Querschnitts.
Alles außerhalb des (Kegel-)Zylinders wird entfernt, auch die Ecken eines Kantholzes, das so rund wird.

```
new cylinder oak 4 24 0 90 0 ; set rod = last
turn $rod y 0 4 25          Zapfen D25 auf 4 cm
turn $rod y 4 9 30 20       Kegel von D30 auf D20
turn $rod y 14 15 28        Einstich D28
turn $rod y 20 24 32        Absatz D32
```
Ist das Teil an der Stelle schon dünner, meldet `turn` das (`the part is already thinner than …`).

## Biegen

`bend <id> <axis> <at_cm> <angle> [inner_radius_mm]` biegt quer zur angegebenen Achse.

- Die **Dicke** T ist die dünnere der beiden anderen Richtungen, um die breitere wird gebogen.
- Positiver Winkel biegt zur positiven Dicken-Seite (bei einem liegenden Blech: nach oben).
- Die **neutrale Faser** (Mitte der Dicke) behält ihre Länge: Die Biegezone ist L = (r + T/2) · Winkel lang,
  das Volumen bleibt gleich. Innen wird das Material gestaucht, außen gedehnt.
- Die Biegezone wird in Scheiben von höchstens 6° geteilt, jede Scheibe wird gebogen (die Außen- und Innenseite
  sieht rund aus). Löcher und Nuten in der Biegezone werden mitgebogen.

Was sich biegen lässt, hängt vom Material ab (kleinster Innenradius als Vielfaches der Dicke):

| Material | kleinster Innenradius |
|---|---|
| Kupfer | 0,25 × Dicke |
| Messing | 0,5 × Dicke |
| Stahl, Aluminium | 1 × Dicke |
| Gummi | beliebig |
| Fichte, Eiche | 50 × Dicke (ohne Dämpfen bricht Holz bei engeren Radien) |
| Ziegel, Beton, Granit | gar nicht (spröde, bricht) |

```
> new box steel 30 4 0.3 100 50 300
> bend last x 10 90 3
Bent 90.0° around an inner radius of 3.0 mm, bend zone 7.1 mm (thickness 3.0 mm)
  #24  Sheet  [steel]  10.6 x 19.9 x 4.0 cm  0.283 kg  pos 93.6 56.5 300.0 cm  free
> bend 24 x 10 90 1
Error: steel 3.0 mm thick cracks below an inner radius of 3.0 mm (1.00 x thickness)
```
Für eine zweite Biegung am selben Teil gilt `bei` wieder vom (neuen) Ende der lokalen Achse aus.

## Spiegeln und Skalieren

`mirror <id> <axis> [copy]` spiegelt an der lokalen Ebene durch den Schwerpunkt, z. B. für ein linkes und rechtes
Seitenteil (`copy` legt die gespiegelte Kopie daneben). Ein gespiegelter Propeller dreht seinen Schub um.

`scale <id> <f>` oder `scale <id> <fx> <fy> <fz>` ändert die Größe (0,01 … 100). Masse und Trägheit werden neu
berechnet. Quader bleiben exakte Quader, Kugeln nur bei gleichem Faktor in alle Richtungen, Zylinder wenn x und z
gleich skaliert werden. Auch importierte Modelle lassen sich skalieren.

## Formen kombinieren

| Befehl | Wirkung |
|---|---|
| `merge <a> <b> [c ...]` | zu einem Teil verschmelzen. Die Teile müssen sich berühren und aus demselben Material sein. Überlappendes Material zählt nur einmal. Verbindungen von b gehen auf a über, Verbindungen zwischen a und b entfallen |
| `subtract <a> <tool> [keep]` | Form des Werkzeug-Teils aus a herausschneiden. Das Werkzeug verschwindet, außer mit `keep` |
| `intersect <a> <tool> [keep]` | nur das gemeinsame Volumen von a und dem Werkzeug behalten |

Beispiele: Eine Kugel aus einem Klotz ausschneiden ergibt eine Schale. Würfel ∩ Kugel ergibt einen „Würfel mit
abgerundeten Ecken“. Zwei Bretter vereinen ergibt ein T-Stück aus einem Stück.

```
new box oak 14 14 7 0 3.6 0 ; set bowl = last ; new sphere steel 12 0 9 0 ; subtract $bowl last
```

Mit der Maus: zwei Teile auswählen (Shift+Klick), im Eigenschaften-Fenster unter „Formen kombinieren“
**Vereinen**, **#2 aus #1 ausschneiden** oder **Schnittmenge**.

## Bau-Modus: Teile stoßen aneinander

Im Bau-Modus (ohne Schwerkraft) halten verschobene Teile am ersten Teil in ihrem Weg an und gleiten daran entlang,
statt hineinzurutschen:

- **Maus**: Teil ziehen. Es bleibt an anderen Teilen hängen (Statuszeile: „touching …“).
  **Strg** halten = durch alles hindurch, **Alt** = ohne Raster.
- **Befehl** `move <id> dx dy dz` hält ebenso an (`stopped after … at …`), `move … through` geht hindurch.
- `position` setzt ein Teil direkt und warnt, wenn es danach in einem anderen steckt.
- `overlaps <id>` zeigt, wie tief ein Teil in anderen steckt.
- Ein- und ausschalten: `buildcollision on|off` oder Menü **Physik → Bau-Modus: Teile stoßen an**.

Teile, die schon ineinander stecken (z. B. mit `position` so gesetzt), dürfen frei herausbewegt werden.
Beim Start des Spiel-Modus meldet das Programm solche Teile und schiebt sie langsam auseinander.

## Was die Physik daraus macht

Nach jeder Bearbeitung wird die Kollisionsform neu berechnet. Löcher, Nuten, Taschen und Zapfenlöcher bleiben
für die Physik offen (Toleranz 0,5 mm), ein Stift fällt also durch ein passendes Loch und ein Zapfen passt ins
Zapfenloch. Abgerundete und gefaste Kanten rollen und kippen entsprechend. Details:
[05-Physik-und-Materialien.md](05-Physik-und-Materialien.md#kollisionsformen).

## Grenzen

- Importierte Modelle (OBJ) lassen sich skalieren, aber nicht sägen, bohren, fräsen, abrunden, biegen oder vereinen.
- Abrundungen an Ecken sind keine Kugelflächen (siehe oben).
- Lochränder (`rims`) werden Stück für Stück abgerundet; zwischen den Vieleckseiten bleiben winzige Spitzen.
- Biegen ist eine rein geometrische Umformung; Rückfederung und Kaltverfestigung werden nicht berechnet.
- Sehr viele Bearbeitungen an einem Teil erzeugen viele konvexe Stücke (siehe `info`, Zeile „geometry“).
  Die Physik fasst sie zusammen, das Zeichnen bleibt aber aufwendiger.
