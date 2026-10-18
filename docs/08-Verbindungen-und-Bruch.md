# Verbindungen, Bruch und Luft

## Grundidee

- Teile lassen sich **verbinden**: kleben, nageln, schrauben, schweißen, mit Scharnier, Feder oder Seil.
- Jede Verbindung hat eine **Tragfähigkeit** und **bricht bei Überlast**.
- Auch **Teile selbst zerbrechen**, wenn sie zu hart aufschlagen (z. B. ein fallender Ziegel).
- Mit `breakage off` hält alles (praktisch zum Bauen und Ausprobieren in der Engine).
  Mit `breakage on` (Standard) ist alles realistisch begrenzt.
- Einzelne Verbindungen oder Teile kann man als **unzerstörbar** markieren.

In Befehlen ist immer **a** das haltende Teil und **b** das gehaltene (bei Mausbedienung: zuerst gewähltes Teil = a).

## Verbindungen anlegen

| Befehl | Was entsteht |
|---|---|
| `glue <a> <b> [glue] [strength_MPa] [gap_mm]` | Klebung über die gesamte Berührfläche |
| `nail <a> <b> [count] [length_mm] [d_mm]` | Nägel durch a in b (nur Holz) |
| `screw <a> <b> [count] [length_mm] [d_mm]` | Schrauben (Holz, mit Dübel in Stein/Ziegel/Beton, Gewinde in Metall) |
| `weld <a> <b> [seam_mm]` | Schweißnaht rund um die Berührfläche (nur Metall) |
| `hinge <a> <b> <x\|y\|z> [rpm] [Nm]` | b dreht sich um seine lokale Achse durch seinen Schwerpunkt, gelagert in a. Mit Drehzahl: Motor |
| `spring <a> <b> [N/m] [N·s/m] [max_N]` | Feder zwischen den einander zugewandten Oberflächen, Ruhelänge = jetziger Abstand |
| `rope <a> <b> [length_cm] [max_N]` | Seil zwischen den Oberflächen, kann durchhängen, aber nicht länger werden |

Mit der Maus: zwei Teile markieren (Shift+Klick), im Eigenschaften-Fenster unter **Verbinden** Art und Werte wählen.

Kleben, Nageln, Schrauben und Schweißen brauchen eine **Berührfläche**: Die Teile müssen aneinanderliegen
(höchstens den Spalt des Klebers entfernt). Tipp: erst `drop` oder mit Raster genau zusammenschieben.

Beispiel (zwei Fichtenbretter aufeinander):

```
new box spruce 30 10 2 0 1 0 ; set a = last
new box spruce 30 10 2 0 3 0 ; set b = last
glue $a $b
```

Ausgabe:

```
Joined: J1  glue  #1 Board <-> #2 Board 2  load 0 %
  glue          wood glue (PVA, D3), strength 6.0 MPa, shear 10.0 MPa, max. gap 0.3 mm
  area          300.00 cm²
  holds         tension 180000 N, shear 300000 N, moment 5196.2 N·m
  load          0.0 %  (force 0.0 N, moment 0.00 N·m)
  breaks        at 100 % load
```

Mit denselben Brettern ergibt `nail $a $b` die Zeilen `nails 4 x 2.0 x 50 mm, part a 20.0 mm thick, penetration 30 mm`
und `holds tension 1060 N, shear 1700 N, moment 88.4 N·m`. `screw $a $b 2` ergibt `screws 2 x 3.7 x 44 mm` und
`holds tension 2333 N, shear 2669 N, moment 138.7 N·m`.

### Kleber

| Kleber (Wort im Befehl) | Haftung (Zug) | Scherung | größter Spalt |
|---|---|---|---|
| `woodglue` (Holzleim, Standard) | 6 MPa | 10 MPa | 0,3 mm |
| `epoxy` (2-Komponenten-Epoxid) | 25 MPa | 20 MPa | 3 mm |
| `superglue` (Sekundenkleber) | 15 MPa | 15 MPa | 0,1 mm |
| `pu` (PU-Kleber) | 8 MPa | 9 MPa | 1 mm |
| `hotglue` (Heißkleber) | 2 MPa | 2,5 MPa | 2 mm |
| `construction` (Montagekleber) | 1,5 MPa | 2 MPa | 5 mm |

1 MPa = 1 N/mm². Eine Klebung kann nie fester sein als das schwächere Material:
Holzleim zwischen Fichte und Ziegel reißt bei 3,5 MPa (Zugfestigkeit des Ziegels).
Mit eigener Haftung (`glue 3 7 hotglue 0.05`) wird die Scherfestigkeit im selben Verhältnis angepasst.
Alte Szenen mit deutschen Kleber-Namen (holzleim, epoxid …) werden beim Laden übersetzt.

### Nägel und Schrauben

Die Tragfähigkeit folgt vereinfachten Formeln des Holzbaus (Eurocode 5):

| Größe | Formel |
|---|---|
| Ausziehen (Nagel) | 20·10⁻⁶ · ρ² · d · Einschlagtiefe (ρ = Dichte des Holzes in kg/m³, Maße in mm → N) |
| Ausziehen (Schraube) | 3 × Nagel |
| Abscheren | kleinster Wert aus Lochleibung beider Teile und Biegung des Stifts (Johansen) |
| Höchstens | Stahlquerschnitt des Nagels/der Schraube |

Ohne Angaben: Länge ≈ 2,5 × Dicke von a, Durchmesser ≈ Länge/25 (Schraube /12), ein Nagel je 75 cm² Fläche (höchstens 8).
Der Nagel muss mindestens 4 × Durchmesser ins zweite Teil reichen.

### Schweißnaht

Naht rund um die Berührfläche, Tragfähigkeit = 60 % der Zugfestigkeit des schwächeren Metalls × Nahtfläche.

### Scharnier, Feder, Seil

| | Standard |
|---|---|
| Scharnier | trägt 3000 N, Motor-Moment 5 N·m |
| Feder | 2000 N/m, Dämpfung 20 N·s/m, bricht bei 1000 N |
| Seil | bricht bei 2000 N |

## Belastung und Bruch

Die Physik misst in jedem Schritt (240-mal pro Sekunde) die Kraft und das Moment, die jede Verbindung überträgt.

| Verbindung | Auslastung |
|---|---|
| Kleber, Nägel, Schrauben, Schweißnaht | √((Zug/Zugfestigkeit + Moment/Momentfestigkeit)² + (Scherung/Scherfestigkeit)²). Druck belastet nicht |
| Scharnier | Lagerkraft / Bruchkraft |
| Feder, Seil | Zugkraft / Bruchkraft |

Bei **100 %** (gemittelt über zwei Schritte, damit einzelne Rechenspitzen nichts zerstören) bricht die Verbindung.
Es erscheint eine Meldung. Beispiel: ein Brett mit fast keinem Heißkleber unter einem festen Balken, darunter ein
Stahlwürfel (in einer leeren Werkstatt):

```
new box spruce 100 10 10 0 120 0 ; fix last ; set beam = last
new box spruce 30 10 2 0 114 0 ; set board = last ; glue $beam $board hotglue 0.001
new box steel 10 10 10 0 108 0 ; glue $board last epoxy
mode play ; wait 1
```

```
!! glue J1 (Block - Board) broke: tension 71 N (holds 30), shear 0 N (holds 38), moment 0.0 N·m
```

Anzeige: im Eigenschaften-Fenster als Balken, im Physik-Inspektor (F3), mit `joints` / `joint <nr>` und
im Bau-Modus als farbige Markierung (grün = wenig, gelb = halb, rot = kurz vor dem Bruch).

### Einstellungen ändern

```
joint 3                          alles anzeigen
joint 3 strength 0.5             Kleber: Haftung (MPa), Scherung wird im selben Verhältnis angepasst
joint 3 shear 0.8                Kleber: Scherfestigkeit (MPa)
joint 3 gap 1                    Kleber: größter Spalt (mm)
joint 3 count 4                  Nägel/Schrauben
joint 3 length 60                Nägel/Schrauben (mm), Feder/Seil (cm)
joint 3 diameter 3               Nägel/Schrauben (mm)
joint 3 seam 5                   Schweißnaht (mm)
joint 3 force 500                Scharnier/Feder/Seil: Bruchkraft (N)
joint 3 stiffness 800            Feder (N/m)
joint 3 damping 5                Feder (N·s/m)
joint 3 motor 3000 2             Scharnier: Motor U/min und N·m
joint 3 unbreakable on           hält alles aus (off = wieder normal)
detach 3 | detach all            Verbindung entfernen
```

`joints` listet alle Verbindungen, `joints <id>` nur die eines Teils:

```
J1  glue  #1 Board <-> #2 Board 2  load 0 %
J2  nails  #1 Board <-> #2 Board 2  load 0 %
J3  screws  #1 Board <-> #2 Board 2  load 0 %
3 joint(s), breakage on, broken so far: 0
```

## Bruch von Teilen beim Aufprall

Bei jedem Aufprall berechnet die Physik die Aufprallenergie:

E = ½ · µ · v²  (v = Annäherungsgeschwindigkeit, µ = reduzierte Masse m₁·m₂/(m₁+m₂), Boden = unendlich schwer)

Ein Teil zerbricht, wenn **E > Bruchenergie × Bruchquerschnitt**. Der Bruchquerschnitt ist der kleinste Querschnitt
(die zwei kleineren Abmessungen), denn dort bricht z. B. ein Ziegel. Der Bruch geht quer zur längsten Seite durch die
Aufprallstelle (leicht schräg). Bei mehr als 3-facher Energie splittert es zusätzlich. Die Stücke fliegen mit der
Bewegung des Teils weiter. Teile kleiner als 2 cm³ oder dünner als 8 mm zerbrechen nicht weiter.

```
new box brick 24 7.1 11.5 0 30 0
new box brick 24 7.1 11.5 100 200 0
mode play ; wait 2
print broken, parts
```

```
!! Block 2 shattered (2 pieces): impact 66.5 J, breaking limit 24.5 J
broken = 1   parts = 3
```

Der Ziegel aus 30 cm bleibt ganz, der aus 2 m zerbricht in zwei Stücke.

| Material | Bruchenergie (J/m²) | Beispiel |
|---|---|---|
| Ziegel | 3 000 | Ziegel zerbricht ab ca. 0,7–1 m Fallhöhe auf festem Boden |
| Beton | 4 000 | |
| Granit | 6 000 | |
| Fichte / Eiche | 40 000 / 50 000 | Holz zerbricht nur bei sehr harten Schlägen |
| Stahl, Messing, Kupfer, Aluminium | 2·10⁷ – 5·10⁷ | praktisch nie (Metall verformt sich eher) |
| Gummi | 10⁹ | nie |

Die Werte sind auf Alltagserfahrung abgestimmt (echte Bruchmechanik hängt stark von Rissen, Auflage und Form ab).
Feste (fixierte) Teile zerbrechen nicht. `unbreakable <id>` schützt ein Teil (Antwort z. B. `Block never breaks`),
`breakage off` schaltet jeden Bruch ab.

**Noch nicht:** Durchbiegen und Brechen eines Bretts unter ruhender Last (z. B. Brett auf zwei Böcken mit Gewicht).
Dafür braucht es die genauen Kontaktkräfte der Auflagen. Verbindungen brechen dagegen auch unter ruhender Last.

## Luft

Die Luft wirkt auf **jede Außenfläche** eines bewegten Teils einzeln:

| Kraft | Formel |
|---|---|
| Auftrieb | ρ · V · g nach oben |
| Druck auf angeströmte Flächen | ½ · ρ · v² · A · C_N(α), C_N = 2π·sin α / (1 + 4,2·sin α) |
| Reibung entlang der Fläche | ½ · ρ · 0,004 · v² · A |

v ist die Geschwindigkeit der Fläche gegenüber der Luft (Bewegung + Drehung − Wind), α der Anstellwinkel.
Bei kleinen Winkeln verhält sich eine Fläche wie ein Flügel (C_N ≈ 2π·α), quer angeströmt wie eine Platte (C_N ≈ 1,2).
Daraus ergeben sich automatisch: Luftwiderstand, Trudeln, Auftrieb an schrägen Flächen und **Propellerschub**.

| Befehl | Wirkung |
|---|---|
| `air` | aktuelle Einstellung, z. B. `air: density 1.204 kg/m3, wind 0.00 0.00 0.00 m/s (0.00 m/s = 0.0 km/h)` |
| `air density 1.204` | Luftdichte (kg/m³) |
| `air vacuum` | keine Luft (alles fällt gleich schnell) |
| `air normal` | 1,204 kg/m³, kein Wind |
| `wind x y z` / `air wind x y z` | Wind in m/s (10 m/s = 36 km/h) |

### Propeller

`new propeller <material> <diameter_cm> [blades=2] [angle=20] [blade_width_cm] [thickness_mm=3]` (auch `new fan`)

Nabe + verdrehte, nach außen schmaler werdende Blätter. Der Winkel gilt bei 75 % des Radius (geometrische Steigung).
Achse = lokale y-Achse. Dreht er positiv um y (`axle <id> y 3000` oder `hinge <holder> <id> y 3000`),
schiebt er nach **+y** und bremst mit einem Luft-Drehmoment, das der Motor überwinden muss.

Gemessen mit `new propeller aluminium 30 ; axle last y 3000 ; mode play ; wait 1 ; print last.airy, last.airtorque`
(30 cm, 2 Blätter, 20°):

| Drehzahl | Schub (`airy`) | Luft-Moment (`airtorque`) |
|---|---|---|
| 3000 U/min | 3,10 N | 0,19 N·m |
| 6000 U/min | 12,42 N | 0,77 N·m |

Doppelte Drehzahl ergibt 4-fachen Schub, wie bei echten Propellern. Werte: `<id>.airy`, `<id>.airforce`, `<id>.airtorque`.

### Beispiel: Propeller-Wagen

```
new box aluminium 40 20 4 0 8 0 ; set cart = last
new cylinder rubber 12 3 15 6 12 ; rotation last 90 0 0 ; hinge $cart last y
new cylinder rubber 12 3 -15 6 12 ; rotation last 90 0 0 ; hinge $cart last y
new cylinder rubber 12 3 15 6 -12 ; rotation last 90 0 0 ; hinge $cart last y
new cylinder rubber 12 3 -15 6 -12 ; rotation last 90 0 0 ; hinge $cart last y
new propeller aluminium 30 2 20 4 3 ; set p = last ; position $p -15 28 0 ; rotation $p 0 0 -90 ; hinge $cart $p y 6000 3
mode play ; wait 3 ; print cart.x, cart.v
```

Ergebnis: `cart.x = 434.6607   cart.v = 2.9111`, also ca. 4,3 m in 3 s mit 2,9 m/s; die Räder rollen ohne zu rutschen.
