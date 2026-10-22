# Physik und Materialien

## Physikmodell

| Eigenschaft | Wert |
|---|---|
| Engine | Jolt Physics 5.3, **doppelte Genauigkeit** (genau auch Kilometer vom Ursprung entfernt), mit einer Korrektur (siehe unten) |
| Zeitschritt | fest 1/240 s, höchstens 10 Schritte pro Bild (ist der PC zu langsam, läuft die Physik in Zeitlupe statt ungenau) |
| Löser | 15 Geschwindigkeits- und 4 Positionsdurchläufe |
| Kontaktgenauigkeit | 1 mm Eindringtoleranz, 2 cm vorausschauende Kontakte (Kontakte entstehen, bevor sich Teile berühren) |
| Herausdrücken | Teile, die ineinander stecken, werden mit höchstens 0,5 mm pro Durchlauf (ca. 0,5 m/s) getrennt, nicht herausgeschleudert |
| Schnelle Teile | ab 3 m/s (inkl. Drehung) prüft die Physik zusätzlich den ganzen Weg (kein Durchtunneln). Langsamere fängt die 2-cm-Vorausschau sicher ab |
| Drehzahl | bis 1500 rad/s (ca. 14 000 U/min, wichtig für Propeller) |
| Schwerkraft | 9,81 m/s² nach unten |
| Boden | unendliche Ebene bei y = 0 aus Beton (Reibung wie Beton, Stoßzahl 0,2) |
| Ruhezustand | Teile, die 0,5 s fast still liegen, „schlafen“ (sparen Rechenzeit) und wachen bei Berührung auf |
| Dämpfung | sehr gering (0,02 linear, 0,03 Drehung) |

### Masse und Form

- **Masse = Volumen × Dichte**, exakt aus der Geometrie (auch nach jeder Bearbeitung).
- Schwerpunkt und Trägheitsmomente berechnet die Physik-Engine aus der Kollisionsform, die Masse kommt aus der exakten Geometrie.

### Kollisionsformen

Die Physik rechnet nicht mit dem Anzeigenetz, sondern mit konvexen Formen (einstellbar mit
`collision <id> auto|exact|hull`, siehe [02-Befehle.md](02-Befehle.md#eigenschaften)):

- **Unbearbeitete Quader, Zylinder und Kugeln**: exakte Grundform. Eine Kugel ist für die Physik eine echte Kugel
  (kein Vieleck), dadurch rollen runde Teile glatt.
- **Bearbeitete Teile**: Die Geometrie besteht aus konvexen Stücken (siehe [07-Engine-Aufbau.md](07-Engine-Aufbau.md)).
  Benachbarte Stücke werden zu größeren Hüllen zusammengefasst (weniger Hüllen = schneller), aber **nur, wenn die gemeinsame
  Hülle keine Lücke auffüllt, die tiefer als 0,5 mm ist**. Gemessen wird das so: Hülle bauen, alle Stücke davon abziehen,
  im Rest den Punkt suchen, der am weitesten vom Material entfernt ist.
  - Bohrlöcher, Nuten, Taschen, Zapfenlöcher und Zahnlücken bleiben dadurch offen: Ein Stift Ø18 fällt durch ein Loch Ø22,
    ein Zapfen passt ins Zapfenloch, Zahnräder greifen.
  - Glatte Bereiche (ein abgerundeter Quader, ein schräg gesägtes Brett) werden zu einer einzigen Hülle.
  - Hauchdünne Hüllen (unter 1,5 mm) werden mit einem Nachbarn verschmolzen oder weggelassen. An solchen Splittern
    findet die Physik sonst oft die falsche Richtung zum Trennen.
  - Hat ein Teil mehr als 64 Hüllen (z. B. ein Lochblech), wird die Toleranz schrittweise verdoppelt, damit es schnell bleibt.
  - `exact`: Toleranz 0,05 mm, `hull`: eine Hülle um alles (Löcher zu).
- **Importierte Modelle**: eine konvexe Hülle pro Blender-Objekt.

Die Berechnung kostet bei stark bearbeiteten Teilen einige Millisekunden. Das Ergebnis wird deshalb gespeichert und bei
Rückgängig, Kopieren und unveränderter Form wiederverwendet.

### Reibung

Jedes Materialpaar hat eine eigene **Haftreibung** µₕ (solange nichts rutscht) und **Gleitreibung** µ_g (beim Rutschen).
Zwischen beiden gibt es einen weichen Übergang (Stribeck-Kurve):

  µ = µ_g + (µₕ − µ_g) · e^(−v / 2 cm/s),  v = Gleitgeschwindigkeit an der Kontaktstelle

| Paar (trocken) | Haftreibung | Gleitreibung |
|---|---|---|
| Stahl / Stahl | 0,74 | 0,57 |
| Aluminium / Stahl | 0,61 | 0,47 |
| Messing / Stahl | 0,51 | 0,44 |
| Kupfer / Stahl | 0,53 | 0,36 |
| Aluminium / Aluminium | 1,05 | 1,00 |
| Kupfer / Kupfer | 1,00 | 0,80 |
| Holz / Holz | 0,50 | 0,30 |
| Holz / Metall | 0,45 | 0,35 |
| Holz / Stein (Ziegel, Beton, Granit) | 0,60 | 0,50 |
| Metall / Stein | 0,57 | 0,45 |
| Stein / Stein | 0,65 | 0,55 |
| Gummi / Stein | 1,00 | 0,80 |
| Gummi / Holz, Gummi / Metall | 0,80 | 0,60 |
| Gummi / Gummi | 1,15 | 1,00 |
| andere | √(µ₁ · µ₂) der Materialwerte | |

Werte nach Tabellenbuch (gerundet). Auf einer 30°-Eichenrampe rutscht Stahl, weil µₕ = 0,45 < tan 30° ≈ 0,58.
Gummi haftet. Auf 20° haftet auch Stahl (tan 20° ≈ 0,36) und kriecht nicht: Im Selbsttest bewegt er sich in 1,5 s
weniger als 0,2 mm. Der Physik-Inspektor zeigt im Abschnitt „Forces“ die gerade benutzte Reibungszahl und ob das Teil
haftet oder rutscht.

### Rollreibung

Rollende Teile verformen sich und ihre Unterlage ein wenig. Das bremst mit dem Moment

  M = c_rr · N · r  (c_rr = Rollreibungszahl, N = Normalkraft, r = Höhe des Schwerpunkts über der Auflage)

Für ein Materialpaar gilt der Mittelwert beider Zahlen. Dazu kommt ein kleines Moment gegen das Drehen auf der Stelle
(Bohrreibung). Ohne Rollreibung würde eine Kugel auf ebenem Boden ewig weiterrollen.
Beispiel: Ein Gummiball (Ø15 cm, c_rr Gummi/Beton = 0,0105) wird beim Rollen um c_rr · g / 1,4 ≈ 0,07 m/s² langsamer:
aus 1 m/s nach 5 s noch 0,53 m/s (gemessen, mit Luftwiderstand).

### Ineinanderstecken und Rettung

Teile sollen sich berühren, aber nicht ineinander stecken. Dafür sorgen mehrere Stufen:

1. **Vorausschau**: Kontakte entstehen schon 2 cm vor der Berührung, die Physik bremst rechtzeitig ab.
2. **Wegprüfung** bei schnellen Teilen (ab 3 m/s): der ganze Weg im Schritt wird geprüft.
3. **Sanftes Trennen**: Stecken Teile doch ineinander (z. B. im Bau-Modus so hingestellt), schiebt die Physik sie mit
   höchstens ca. 0,5 m/s auseinander. Es gibt keinen Ruck und kein Herausschleudern. Beim Wechsel in den Spiel-Modus
   meldet das Programm solche Teile.
4. **Rettung**: Gerät ein Teil trotzdem tief in ein anderes (mehr als ein Viertel der Dicke des dünneren Teils,
   mindestens 4 mm, z. B. weil die Figur es hineindrückt oder ein schnell drehender Flügel einschlägt), werden **beide Teile einen
   Schritt zurück an ihre letzte freie Stelle** gesetzt. Dabei verlieren sie den Schwung in das Hindernis hinein.
   Zurückgesetzt wird nur, wenn diese Stellen wirklich frei sind. Klemmt dasselbe Paar immer wieder (5-mal in 1 s),
   wird es 3 s lang nur noch sanft getrennt. `status` und der Wert `rescued` zeigen, wie oft das passiert ist.

Verbundene Teile (Kleber, Scharnier …) werden dabei nicht getrennt.

### Korrekturen an Jolt

Bei der Suche nach dem Ruckeln und Ineinanderstecken sind drei Ursachen in der Physik-Engine aufgefallen:

| Ursache | Folge | Lösung |
|---|---|---|
| Prüft Jolt eine zusammengesetzte Form gegen eine andere Form, fehlt der Vorausschau-Abstand (nur in dieser Richtung) | Teilhüllen knapp über dem Boden bekommen keinen vorausschauenden Kontakt. Ein gebohrtes Brett landet auf einer Hälfte, bekommt Drall und taucht 13 mm in den Boden | Korrektur in Jolt (`cmake/patch_jolt.cmake`, wird beim Herunterladen automatisch eingespielt) |
| Jolt fasst die Kontaktpunkte aller Teilhüllen auf 4 Punkte zusammen | dabei kann eine Ecke verloren gehen; das Teil kippt langsam über diese Ecke und taucht ein | für Teile aus mehreren Hüllen abgeschaltet |
| Jolts Wegprüfung stößt eine zusammengesetzte Form nur an **einem** Punkt an und richtet sich nach der kleinsten Teilhülle | falscher Drall schon bei langsamem Fallen | Wegprüfung nur noch für schnelle Teile (ab 3 m/s) |

Außerdem: Jolt lehnte manche Hüllen mit vielen fast ebenen Punkten ab (runde Senkungen). Sie werden jetzt mit etwas
größerer Toleranz neu gebaut, statt in der Kollision zu fehlen. Und Jolts Prüfhilfe, die bei Gleitkomma-Überlauf das
Programm beendet, ist ausgeschaltet.

### Rechenzeit und Genauigkeit

Gemessen mit `Werkstatt.exe --bench` (je Physikschritt; bei 240 Schritten/s sind 0,05 ms/Schritt = 1,2 % eines Bildes
bei 100 FPS). „vorher“ = alte Einstellungen: jedes konvexe Stück eine Hülle, keine Grundformen, eine gemittelte
Reibungszahl, Herausdrücken bis 20 cm pro Durchlauf.

| Szene | | ms/Schritt | Spitze ms | Eindringen Ø / 99 % / max (mm) | Messwert |
|---|---|---|---|---|---|
| 40 Kugeln fallen auf einen Haufen | vorher | 0,070 | 0,27 | 0,09 / 3,6 / 12,3 | |
| | neu | 0,066 | 0,12 | 0,92 / 1,4 / 3,8 | |
| 2 Kugeln prallen mit 10 m/s zusammen | vorher | 0,042 | 0,12 | 0,03 / 0,8 / 19,2 | |
| | neu | 0,042 | 0,14 | 0,01 / 0,0 / 6,7 | |
| 25 Zylinder | vorher | 0,060 | 0,20 | 0,33 / 9,3 / 23,0 | |
| | neu | 0,061 | 0,18 | 1,10 / 4,4 / 15,0 | |
| 12 gebohrte Bretter | vorher | 1,530 | 13,92 | 15,67 / 34,6 / 54,0 | |
| | neu | 0,227 | 1,56 | 0,01 / 0,1 / 0,1 | |
| Ziegelwand + Wurf | vorher | 0,059 | 0,32 | 0,39 / 2,5 / 18,7 | |
| | neu | 0,078 | 0,34 | 0,62 / 6,8 / 8,2 | |
| Ziegelturm (10) | vorher | 0,009 | 0,08 | 0,03 / 0,3 / 0,4 | Spitze verschoben 0,1 mm |
| | neu | 0,010 | 0,13 | 0,03 / 0,3 / 0,4 | 1,9 mm (beim Setzen am Anfang, danach still) |
| 10 Paare ineinander gesteckt | vorher | 0,009 | 0,10 | 0,09 / 1,1 / 50 | sofort herausgedrückt |
| | neu | 0,016 | 0,13 | 2,72 / 45 / 50 | sanft getrennt (ca. 0,1 s), danach 0 Paare |
| Flügel 3000 U/min gegen Block | vorher | 0,049 | 0,15 | 1,28 / 7,7 / 7,7 | |
| | neu | 0,048 | 0,15 | 1,29 / 7,7 / 7,7 | Flügel steckt am Ende 0 mm im Block |
| Stift Ø18 ins Loch Ø22 | vorher | 0,022 | 0,18 | 0,00 / 0,0 / 0,1 | fällt durch (0,9 cm) |
| | neu | 0,050 | 0,14 | 0,54 / 1,0 / 1,0 | fällt durch (0,8 cm) |
| Beispiel-Werkstatt (Getriebe) | vorher | 0,306 | 0,78 | 0,11 / 0,9 / 0,9 | |
| | neu | 0,181 | 0,34 | 0,00 / 0,0 / 0,0 | |
| Beispiel-Welt (132 Teile) | vorher | 0,327 | 0,57 | 0,37 / 1,6 / 24,8 | |
| | neu | 0,210 | 0,40 | 1,00 / 5,8 / 7,2 | 0 Rettungen nötig |

Die Zeiten schwanken je nach PC und Lauf um etwa 10 %. Bei gebohrten Brettern ist die neue Kollision fast 7-mal
schneller als die alte Einzelstück-Form, und die Bretter stecken nicht mehr bis zu 5 cm ineinander. Der Stift fällt
auch mit der alten Form durch, weil dort jedes Stück eine eigene Hülle war (dafür ist sie viel langsamer). Im Spiel-Modus zeigt die Statusleiste die Physikzeit pro Bild, Kontakte,
aktive Teile und das größte Eindringen.

### Stoßzahl

Für zwei Materialien gilt die größere der beiden Stoßzahlen. Ein Gummiball (0,8) auf Beton springt aus 1,95 m Fallhöhe
auf 1,20 m zurück (gemessen, Stoßzahl also etwa 0,78).

### Luft

Alle bewegten Teile spüren die Luft (Standard: Dichte 1,204 kg/m³ bei 20 °C, kein Wind; einstellbar mit `air` und `wind`):

| Kraft | Formel |
|---|---|
| Auftrieb (Archimedes) | ρ_Luft · V · g, nach oben |
| Druck auf jede angeströmte Außenfläche | ½ · ρ_Luft · v² · A · C_N(α), C_N = 2π·sin α / (1 + 4,2·sin α) |
| Reibung entlang jeder Fläche | ½ · ρ_Luft · 0,004 · v² · A |

v ist die Geschwindigkeit der einzelnen Fläche gegenüber der Luft (aus Bewegung **und Drehung**, minus Wind).
Weil jede Fläche einzeln an ihrer Stelle wirkt, entstehen automatisch: Luftwiderstand je nach Lage (ein Brett fällt flach
langsamer als hochkant), Drehmomente (Trudeln, Ausrichten), Kräfte an schrägen Flächen, Schub von Propellern
und ein bremsendes Moment auf drehende Teile. Der statische Auftrieb ist winzig (bei Holz ca. 0,25 % des Gewichts), aber vorhanden.
Die Luft selbst strömt nicht: Ein Propeller schiebt sich selbst an, bläst aber andere Teile nicht weg.
Details und Propeller-Messwerte: [08-Verbindungen-und-Bruch.md](08-Verbindungen-und-Bruch.md#luft).

### Verbindungen und Bruch

Kleben, Nageln, Schrauben, Schweißen, Scharniere, Federn und Seile mit Tragfähigkeit, Bruch bei Überlast
und Zerbrechen spröder Teile beim Aufprall: [08-Verbindungen-und-Bruch.md](08-Verbindungen-und-Bruch.md).

### Achsen und Motoren

`axle <id> <x|y|z>` (oder im Eigenschaften-Fenster) lagert ein Teil drehbar um seine lokale Achse durch den Schwerpunkt,
fest mit der Welt verbunden. Lagerreibung 0,02 N·m. Soll die Achse an einem beweglichen Teil sitzen (Rad am Wagen),
nimmt man `hinge <holder> <part> <axis>`.
Mit Motor dreht die Achse mit der eingestellten Drehzahl, solange das Moment unter dem Maximum bleibt.
Zahnräder treiben sich **über echte Zahnkontakte** an. In der Beispiel-Werkstatt dreht das Motorrad (20 Zähne) mit 20 U/min
und das große Rad (30 Zähne) mit 13,34 U/min (Theorie 13,33).

## Materialien

| Material (Befehl) | Menü | Dichte kg/m³ | Haft-/Gleitreibung | Rollreibung | Stoßzahl | Zug MPa | Druck MPa | E-Modul GPa | Bruchenergie J/m² | biegbar ab Radius |
|---|---|---|---|---|---|---|---|---|---|---|
| `spruce` | Fichte | 470 | 0,50 / 0,35 | 0,005 | 0,35 | 80 | 40 | 11 | 40 000 | 50 × Dicke |
| `oak` | Eiche | 720 | 0,55 / 0,40 | 0,004 | 0,30 | 110 | 55 | 12,5 | 50 000 | 50 × Dicke |
| `brick` | Ziegel | 1800 | 0,65 / 0,55 | 0,008 | 0,10 | 3,5 | 20 | 15 | 3 000 | nicht |
| `concrete` | Beton | 2400 | 0,65 / 0,55 | 0,006 | 0,10 | 2,6 | 30 | 31 | 4 000 | nicht |
| `granite` | Granit | 2700 | 0,60 / 0,50 | 0,003 | 0,15 | 10 | 180 | 50 | 6 000 | nicht |
| `steel` | Stahl | 7850 | 0,60 / 0,45 | 0,0005 | 0,40 | 360 | 360 | 210 | 5·10⁷ | 1 × Dicke |
| `aluminium` | Aluminium | 2700 | 0,60 / 0,45 | 0,001 | 0,35 | 150 | 150 | 70 | 2·10⁷ | 1 × Dicke |
| `brass` | Messing | 8500 | 0,50 / 0,40 | 0,0008 | 0,35 | 350 | 350 | 100 | 5·10⁷ | 0,5 × Dicke |
| `copper` | Kupfer | 8960 | 0,55 / 0,40 | 0,0008 | 0,30 | 220 | 220 | 120 | 5·10⁷ | 0,25 × Dicke |
| `rubber` | Gummi | 1100 | 1,00 / 0,80 | 0,015 | 0,80 | 15 | 15 | 0,01 | 10⁹ | beliebig |
| `plastic` | Kunststoff (ABS) | 1050 | 0,50 / 0,35 | 0,006 | 0,45 | 40 | 65 | 2,3 | 20 000 | 3 × Dicke |

Haft- und Gleitreibung in der Tabelle sind die Werte des Materials; für ein Paar gilt die Paartabelle oben.
Holzwerte gelten entlang der Faser. Die Zugfestigkeit begrenzt Klebungen und Schweißnähte, die Bruchenergie bestimmt,
wann ein Teil beim Aufprall zerbricht (siehe [08](08-Verbindungen-und-Bruch.md#bruch-von-teilen-beim-aufprall)). Die Spaltenergie
(Fichte 600, Eiche 900, Ziegel 900, Beton 1 200, Granit 1 800 J/m², Metall und Gummi spalten nicht) und die Härte entscheiden,
was ein Keil oder eine Spitze anrichtet ([08](08-Verbindungen-und-Bruch.md#form-des-werkzeugs-spitze-schneide-stumpf)).
Festigkeit, Härte und Bruchenergie hängen von der Temperatur ab: warm weicher, kalt spröde (Wärmewerte und Modell in
[12-Waerme-und-Feuer.md](12-Waerme-und-Feuer.md)).
Verbiegen und Brechen unter ruhender Last sind noch nicht eingebaut (Biegen mit `bend` ist eine Bearbeitung, siehe
[09](09-Bearbeitung.md#biegen)).
Die Holzmaserung liegt entlang der lokalen x-Achse und geht durch das ganze Teil: Schnittflächen zeigen Jahresringe.
Gespeicherte Szenen mit den alten deutschen Materialnamen (Fichte, Stahl …) werden beim Laden übersetzt.

Neues Material: in `src/Materials.cpp` eine Zeile in der Liste ergänzen (Paarwerte in `frictionPair`).

## Spielfigur

| Eigenschaft | Wert |
|---|---|
| Form | Kapsel 1,80 m hoch, Radius 0,30 m, Augenhöhe 1,65 m |
| Masse | 75 kg |
| Gehen / Rennen | 4 m/s / 7 m/s |
| Sprung | Startgeschwindigkeit 4,6 m/s (ca. 1,1 m hoch) |
| Stufen / Hänge | steigt bis 35 cm hohe Stufen, geht bis 50° steile Hänge |
| Schieben | drückt beim Laufen mit bis zu 350 N gegen Teile |
| Tragen | Teil hängt an einer gedämpften Feder (ω = 12 rad/s) vor der Figur; höchstens **420 N** Kraft |
| Zu schwer | über ca. 43 kg (Gewicht > 420 N) kann nichts angehoben werden: „too heavy to lift“ |
| Loslassen | automatisch, wenn das Teil festhängt (mehr als 2 m vom Haltepunkt) |
| Werfen | Kraftstoß für 9 m/s, höchstens 45 N·s |
| Reichweite | 3,5 m |

Drückt die Figur ein getragenes Teil in ein anderes, greift die Rettung (siehe oben): Das Teil bleibt an seiner letzten
freien Stelle, statt hineinzurutschen.

## Physik-Inspektor (F3)

Zeigt live, nach Themen sortiert, alles über ein Teil: im Bau-Modus das ausgewählte, im Spiel-Modus das angeschaute
oder getragene. Dieselben Werte liefert der Befehl `inspect <id>`. Die Texte sind englisch (wie alle Ausgaben).

| Abschnitt | Inhalt |
|---|---|
| **General** | Name, Material mit Haft-/Gleitreibung und Stoßzahl, Zustand (fixed, build mode, resting, moving), Chunk |
| **Mass & Geometry** | Masse, Volumen, Dichte, Größe, Schwerpunkt, Höhe des Schwerpunkts, Hauptträgheitsmomente, Kollisionsform (Hüllen/Stücke) |
| **Motion** | Geschwindigkeit (m/s, km/h, Vektor), Beschleunigung, Impuls, Höhe der Unterkante |
| **Rotation** | Lage (Euler-Winkel), Winkelgeschwindigkeit (rad/s, U/min), Drehimpuls, Motormoment |
| **Forces** | Gewichtskraft, Auftrieb, Luftkraft (Widerstand/Schub) und Luft-Drehmoment, Haltekraft der Hand, Kontakt-/Lagerkraft mit Normalkraft und Reibung (mit benutzter Reibungszahl, haftet/rutscht mit Geschwindigkeit), resultierende Kraft |
| **Energy** | kinetisch (Bewegung und Drehung), potentiell (Höhe), gesamt |
| **Support & Tipping** | Kontaktpunkte mit Partnern, Stützfläche, ob der Schwerpunkt darüber liegt, Abstand zur Kippkante, Neigung, ab der es kippt |
| **Joints** | jede Verbindung des Teils mit Art, Partner, Kraft und Auslastung in %; ob das Teil zerbrechen kann |

### Wie die Werte entstehen

- **Beschleunigung** a = Änderung der Geschwindigkeit pro Physikschritt (für die Anzeige leicht geglättet).
- **Resultierende Kraft** F = m · a (Newton).
- **Kontakt-/Lagerkraft** = F − Gewicht − Auftrieb − Luftwiderstand − Hand. Das ist die Summe aller Kräfte von Boden,
  anderen Teilen und Achsen, exakt nach dem 2. Newtonschen Gesetz.
- **Normalkraft / Reibung**: Zerlegung der Kontaktkraft entlang der mittleren Kontaktnormalen. Daneben steht das
  Verhältnis Reibung/Normalkraft und die gerade benutzte Reibungszahl µ: Liegt das Verhältnis nahe an µ, rutscht das Teil gleich.
- **Kippen**: Aus den tragenden Kontaktpunkten wird die Stützfläche (konvexe Hülle) gebildet. Liegt der Schwerpunkt
  senkrecht darüber, steht das Teil stabil. Der Kippwinkel ist arctan(Abstand zur Kippkante / Schwerpunkthöhe).
- Im Bau-Modus läuft keine Physik, dort gibt es nur Gewicht und Auftrieb.

### Pfeile im Bild

| Farbe | Bedeutung |
|---|---|
| rot | Gewichtskraft |
| grün | Kontakt-/Lagerkraft |
| lila | Haltekraft der Hand |
| cyan | Luft (Auftrieb + Widerstand + Schub) |
| weiß | resultierende Kraft |
| blau | Geschwindigkeit (0,25 m Pfeil pro m/s) |
| weißes Kreuz | Schwerpunkt |
| gelbe Kreuze | Kontaktpunkte mit Normalenrichtung |

Kraftpfeile sind so skaliert, dass die Gewichtskraft 0,6 × Teilgröße lang ist.

## Geprüft

Der Selbsttest (`Werkstatt.exe --selftest`) misst unter anderem:

| Versuch | Ergebnis |
|---|---|
| 10-cm-Fichtenwürfel fällt aus 50 cm | liegt danach exakt bei y = 5 cm |
| Freier Fall 0,3 s | 0,45 m (Theorie 0,44 m; der kleine Unterschied kommt von den festen Zeitschritten) |
| Stahl auf 30°-Eichenrampe | rutscht (90 cm in 1,5 s) |
| Gummi auf 30°-Eichenrampe | haftet (0,11 mm) |
| Stahl auf 20°-Eichenrampe | haftet ohne zu kriechen (0,1 mm in 1,5 s) |
| Ruhender Würfel | Kontaktkraft = Gewicht − Auftrieb (auf 0,05 N genau) |
| Ø10-Bohrung in 2 cm Fichte | Masse sinkt um genau π · r² · h · ρ = 0,738 g |
| Gebohrte Platte, Stift Ø18, Loch Ø22 | das Loch bleibt in der Kollision offen, der Stift fällt durch |
| `move` gegen ein anderes Teil | hält an der Berührung an (Abstand 0,02 mm) |
| Gummiball rollt mit 1 m/s | wird durch Rollreibung langsamer (0,53 m/s nach 5 s) |
| Turm aus 8 Ziegeln | steht still (0 mm in 3 s) |
| Zwei Stahlwürfel 5 cm ineinander | werden sanft getrennt, ohne wegzufliegen |
| Getriebe 20:30 Zähne | Drehzahlverhältnis 0,667 (Theorie 0,667) |
| Klebung, trägt 300 N, mit 8 kg belastet | hält |
| Klebung, trägt 60 N, mit 80 N belastet | reißt |
| Seilpendel | schwingt, Seil dehnt sich nicht |
| Propeller 30 cm, 3000 → 6000 U/min | Schub 3,3 → 13,3 N (×4 bei doppelter Drehzahl), im Vakuum 0 |
| Propeller-Wagen | fährt (4,3 m in 3 s) |
| Ziegel fällt aus 30 cm / 2 m | bleibt ganz / zerbricht; Stahl zerbricht nicht |
| Stahlkeil aus 3 m auf ein Fichtenbrett | entlang der Faser: spaltet; quer: Kerbe, bleibt stecken und hält still; runde Spitze: nur kleines Loch; Keil auf Ziegel: spaltet |
| Beispielwelt (132 Teile, 29 Verbindungen) | läuft 5 s, ohne dass etwas wegfliegt (schnellstes Teil 10,9 m/s), tiefstes Eindringen unter 4 mm (schwankt je Lauf zwischen 2,9 und 3,8 mm) |

Dazu Geometrie und Bearbeitung (Abrunden, Fasen, Hohlkehle, Drehen, Fräsen, Hobeln, Biegen mit gleichem Volumen,
Ziegel nicht biegbar, Vereinen, Ausschneiden, Skalieren, Spiegeln), Befehlssprache, Zeichnung und Speichern.
Insgesamt 98 Prüfungen, alle bestanden.
