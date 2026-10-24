# Aufbau der Engine

## Überblick

```
             Maus/Tastatur   Konsole   Skript   Fernsteuerung (TCP)
                   |            \        |        /
                   v             v       v       v
   App (Fenster, Kameras,     Script (Befehlssprache, Aufträge)
   Werkzeuge, Oberfläche)              |
                   \                   v
                    \------------> Commands (einzelne Befehle, englisch)
                                       |
                                       v
                                World (Teile, Bearbeitung, Rückgängig, Speichern, Inspektor)
                               /       |          \           \
                           Solid    Physics      Chunks     Collision
                       (Geometrie)  (Jolt)    (räumliche     (Kollisionsformen
                               \               Ordnung)      aus der Geometrie)
                             Renderer (Vulkan)      Drawing (Dreitafelprojektion)
```

| Datei | Aufgabe |
|---|---|
| `src/main.cpp` | Kommandozeile, Ablauf Hauptmenü → Spiel → Hauptmenü, Betrieb ohne Fenster, Fernsteuerungs-Client, `--bench [szene]` |
| `src/Menu.*`, `qml/` | Hauptmenü mit Qt Quick: Weltliste, Mehrspieler, Einstellungen (eigene Bibliothek `WerkstattMenu`, [15](15-Hauptmenue-und-Welten.md)) |
| `src/Worlds.*`, `src/Settings.*`, `src/AppWorld.cpp` | Welten als Ordner (`world.json`, Szene, Sicherungen), Einstellungen, im Spiel: Welt öffnen, speichern, automatisch speichern |
| `src/App.h`, `App.cpp` | Hauptschleife, Kameras (Perspektive/orthografisch), Sichtbarkeit und Detailstufen, Bild zusammenstellen |
| `src/AppInput.cpp` | Maus/Tastatur, Bewegen per Ziehen (mit Anstoßen), Vorschau von Säge, Bohrer, Abrunden, Fräsen, Hobeln, Spielfigur, Tragen |
| `src/AppUI.cpp` | Menüs, Werkzeug-/Eigenschaften-Fenster, Konsole, Inspektor, Zeichnungsfenster, Dateidialoge (Oberfläche deutsch) |
| `src/AppFire.cpp` | Wärme-Werkzeug (H, im Spiel F), Flammen, Rauch, Funken, Kältenebel, Feuerschein, Temperaturgitter für den Shader |
| `src/AppCommands.cpp` | Live-Befehle: Aufträge, `player`, `tool`, `window`, `display`, `status`, Werte für die Befehlssprache |
| `src/Commands.*` | alle einzelnen Befehle und die Hilfe (`help`) |
| `src/Script.*` | Befehlssprache: Parser, Ausdrücke, Ausführung mit Warten |
| `src/Remote.*` | TCP-Server (läuft mit Hauptmenü die ganze Zeit, Menü und Spiel teilen ihn) und `--send`-Client |
| `src/World.*` | Teile, Erzeugen, Sägen/Bohren, Physik-Körper, Luft je Fläche, Rettung bei tiefem Eindringen, Inspektor-Messung, Rückgängig, Beispiel-Werkstatt, Speichern/Laden |
| `src/Edit.cpp` | Bearbeitung: Abrunden/Fasen, Fräsen, Hobeln, Drehen, Biegen, Spiegeln, Skalieren, Vereinen, Ausschneiden, Schnittmenge, Verschieben mit Anstoßen |
| `src/Collision.*` | baut aus den konvexen Stücken eines Teils die Kollisionshüllen (siehe unten) |
| `src/Joints.cpp` | Verbindungen (Kleber, Nägel, Schrauben, Schweißnaht, Scharnier, Feder, Seil): Tragfähigkeit, Belastung, Bruch |
| `src/Impact.cpp` | Aufprall: Zerbrechen spröder Teile; Form eines härteren Werkzeugs vermessen (Spitze, Schneide, stumpf), Eindringen, Spalten, Abdruck, Steckenbleiben |
| `src/Heat.cpp` | Wärme: Temperaturgitter je Teil, Wärmeleitung, Luft, Berührung, Reibungswärme, Werkzeuge, Verkohlen und Brennen, Feuer (Heskestad), Schmelzen, Tropfen und Pfützen, Erweichen von Verbindungen, `heat`-Texte ([12](12-Waerme-und-Feuer.md)) |
| `src/Water.*`, `src/AppWater.cpp` | Wasser: Gewässer (Becken, Meer mit Grundkarte je 16-m-Kachel: Land und Meeresboden), Flüssigkeiten, Wellen, exakter Auftrieb je konvexem Stück, Hohlräume, die volllaufen, Widerstand und Dämpfung, auf allen Kernen; Gewässer für die Shader, Spritzer, Dampf, Schwimmen ([14](14-Wasser.md)) |
| `src/Noise.*`, `src/WorldGen.*`, `src/Terrain.*`, `assets/biomes.json` | Weltgenerierung: deterministisches Rauschen, Klima, Biome und Flüsse aus dem Seed, Gelände in Kacheln mit Detailstufen (Erzeugung im Hintergrund), Physik als Höhenfeld je 16-m-Chunk, geänderte Chunks in Regionsdateien, Flusswasser als Gewässer ([16](16-Weltgenerierung.md)) |
| `src/Trees.*`, `src/AppTrees.cpp`, `assets/trees.json` | Bäume: Arten, Formen (Stamm, Äste, Krone), Wald aus dem Seed, gebündelt gezeichnet, nahe Bäume als feste Teile ([17](17-Baeume.md)) |
| `src/Showcase.cpp` | Beispielwelt (`scene showcase`) und Feuerstelle (`scene fire`), aufgebaut als Befehlslisten |
| `src/Solid.*` | Festkörper-Geometrie (siehe unten), Kanten finden, Werkzeugformen, Berührfläche zweier Teile |
| `src/Chunks.*` | 3D-Chunks |
| `src/Jobs.*`, `src/Profile.*` | Hilfs-Threads (Hintergrund-Aufgaben, parallele Schleifen) und Zeitmessung (`profile`) |
| `src/Physics.*` | Hülle um Jolt: Körper (Grundformen und Hüllen), Reibung je Materialpaar, Rollreibung, Wegprüfung, Abfragen (Sweep, Überlappung), Achsen, Verbindungs-Constraints mit Kraftmessung, Spielfigur, Aufprall-Meldungen |
| `src/Bench.cpp` | `--bench`: misst Rechenzeit, Eindringtiefe und Rettungen alter und neuer Einstellungen in elf Szenen |
| `src/Renderer.*`, `shaders/` | Vulkan-Grafik |
| `src/Drawing.*` | Dreitafelprojektion, Blatt, SVG |
| `src/Materials.*`, `src/Json.*` | Materialliste aus `assets/materials.json` (eingebettete Kopie als Rückfall), Feldtabelle (Laden, Speichern, `materials set`, Editor), Reibungspaare, Festigkeit je Temperatur, Kälte-Sprödigkeit, Wärme-Werkzeuge, Schmierstoffe |
| `src/ObjLoader.*` | OBJ-Import |
| `src/Png.*` | PNG-Schreiber für Fotos |
| `src/SelfTest.cpp` | automatische Prüfungen |
| `cmake/patch_jolt.cmake` | Korrektur in Jolt, wird beim Herunterladen eingespielt |

**Sprache:** Befehle, Werte der Befehlssprache, Materialnamen und alle Ausgaben/Fehlermeldungen sind englisch
(Standard, eindeutig). Menüs, Fenster und diese Anleitung sind deutsch. Jedes Material hat dafür einen Befehlsnamen
(`name`, z. B. `spruce`) und einen deutschen Anzeigenamen (`label`, z. B. „Fichte“).

## Geometrie: Körper aus konvexen Stücken

Jedes Teil ist eine Liste **konvexer Polyeder** (Stücke). Jedes Stück besteht aus ebenen, konvexen Flächen.

| Operation | Umsetzung |
|---|---|
| Sägen | jedes Stück an der Ebene zerschneiden (Polygon-Clipping + neue Deckfläche). Konvex bleibt konvex. |
| Sägen mit Fuge | eine dünne Platte (zwei Ebenen) abziehen |
| Abziehen eines konvexen Werkzeugs | jedes getroffene Stück wird an den Werkzeugebenen in konvexe Reste zerlegt. Vorher wird nur der Bereich des Werkzeugs (seine Hüllbox) herausgeschnitten, damit nicht der ganze Körper zersplittert |
| Bohren, Senken | Bohrer = konvexes Prisma (16–48 Seiten, flächengleich zum Kreis), Senker = Kegelstumpf. Bei runden Werkzeugen wird der Rest **strahlenförmig** um die Werkzeugachse zerlegt (Tortenstücke): keine dünnen Splitter, die die Physik stören |
| Fräsen (Tasche, Nut, Falz, Langloch, Zapfen, Überblattung) | Abziehen von Quadern (Tasche mit Eckradius: Quader + Zylinder) |
| Hobeln | Sägen und das dünne Stück wegwerfen |
| Kanten finden | Paare von Außenflächen mit gemeinsamer Kante und mehr als 20° Knick. Konvex/konkav aus der Lage der Nachbarfläche, gekrümmt = gehört zu Zylinder/Kugel |
| Abrunden / Fasen | Pro Kante ein Bereich zwischen den beiden Flächen und dem Kreisbogen (bzw. der Fase), zerlegt in konvexe Keile. Außenkante: Bereich abziehen, Innenkante (Hohlkehle): Bereich hinzufügen |
| Drehen (Drehbank) | Stücke an der Achse in Scheiben teilen, jede Scheibe mit einem Kegelstumpf (vieleckig, flächengleich) schneiden |
| Biegen | Stücke vor, in und hinter der Biegezone trennen. Die Zone wird in dünne Scheiben geteilt und jeder Punkt um die Biegeachse gelegt, die neutrale Faser (Mitte der Dicke) behält ihre Länge; der Rest wird starr gedreht. Die Dicke kommt aus dem Querschnitt an der Biegestelle (auch nach einer ersten Biegung richtig) |
| Spiegeln, Skalieren | jede Ebene und jeder Punkt wird umgerechnet (affine Abbildung). Spiegeln dreht die Flächen-Reihenfolge um |
| Vereinen | Stücke beider Teile zusammenlegen (im Koordinatensystem des ersten Teils) |
| Ausschneiden, Schnittmenge | jedes konvexe Stück des Werkzeugteils wird nacheinander abgezogen bzw. geschnitten |
| Zerfall in Teile | Stücke, die sich mit Fläche berühren oder überlappen, gehören zusammen (Union-Find). Getrennte Gruppen werden eigene Teile |
| Innenflächen | Flächen, die von Nachbarstücken verdeckt sind, werden ganz oder teilweise (2D-Polygon-Differenz) ausgeblendet. Nur die echte Oberfläche wird gezeichnet und bekommt Kanten |
| Volumen, Schwerpunkt | Summe über Tetraeder |
| Weiche Normalen | Flächen merken sich, ob sie zu einem Zylinder/einer Kugel gehören (Achse, Radius). Daraus entstehen runde Schattierung und die Umrisse in Zeichnungen |
| Zahnrad | Ring aus Segmenten + ein konvexes Prisma pro Zahn mit Evolventenflanken |
| Propeller | Nabe + je Blatt 4 verdrehte, nach außen schmaler werdende Quader (Blattwinkel fällt nach außen, konstante Steigung) |
| Berührfläche | gegenüberliegende Außenflächen zweier Teile mit höchstens Spalt-Abstand werden aufeinander projiziert und geschnitten (für Kleber, Nägel, Schweißnaht) |

Vorteile: Schnitte und Bohrungen sind exakt (Volumen auf 10⁻⁹ genau), und die Physik bekommt direkt
eine konvexe Zerlegung.

### Kollisionsformen für die Physik (`Collision.cpp`)

| Fall | Form in Jolt |
|---|---|
| unbearbeiteter Quader / Zylinder / Kugel | `BoxShape` / `CylinderShape` / `SphereShape` (exakt, schnell, runde Teile rollen glatt) |
| bearbeitet | Stücke werden gierig zu größeren Hüllen zusammengefasst, solange die gemeinsame Hülle **keine Lücke tiefer als 0,5 mm** auffüllt (Fülltiefe, siehe unten). Danach werden Hüllen dünner als 1,5 mm mit einem Nachbarn verschmolzen oder weggelassen. Mehr als 64 Hüllen: Toleranz verdoppeln. Ergebnis: `StaticCompoundShape` |
| `collision exact` / `hull` | Toleranz 0,05 mm / eine einzige Hülle |
| importiertes Modell | eine Hülle je Objekt der OBJ-Datei |

**Fülltiefe:** Hülle der Kandidaten bauen, alle Stücke davon abziehen und im Rest den Punkt suchen, der am weitesten
vom Material entfernt ist (Abstand zu den Flächen, die nicht zur Hülle gehören). Nur wenn das unter der Toleranz liegt,
wird zusammengefasst. So bleiben Löcher, Nuten und Zahnlücken offen, glatte Flächen werden zu einer Hülle.

Die Ebenen jeder Hülle berechnet Jolts `ConvexHullBuilder`. Das Ergebnis wird im Teil gespeichert und bei Rückgängig,
Kopieren und unveränderter Form wiederverwendet. Jolt lehnt manche Hüllen mit vielen fast ebenen Punkten ab; dann wird
mit doppelten Punkten entfernt und größerer Toleranz neu gebaut.

### Physik-Einstellungen (`Physics.cpp`, `World.cpp`)

- 240 Schritte/s, 15 Geschwindigkeits-/4 Positionsdurchläufe, 2 cm vorausschauende Kontakte,
  `mPenetrationSlop` 1 mm, `mMaxPenetrationDistance` 0,5 mm (sanftes Herausdrücken).
- **Reibung je Materialpaar:** Der `ContactListener` setzt für jeden Kontakt die Reibung aus der Paartabelle
  (`frictionPair` in `Materials.cpp`): Mischung aus Haft- und Gleitreibung nach der Gleitgeschwindigkeit an der
  Kontaktstelle (Stribeck, 2 cm/s). Die Werte werden für den Inspektor mitgeschrieben.
- **Rollreibung** und Bohrreibung: nach jedem Schritt ein bremsender Drehimpuls aus der Normalkraft der Kontakte.
- **Wegprüfung** (`LinearCast`) nur für Teile über 3 m/s (inkl. Drehung × halbe Größe), aus unter 2 m/s.
- Für zusammengesetzte Formen ist die Kontaktpunkt-Reduktion (`mUseManifoldReduction`) aus.
- **Rettung** (`World::handleDeepContacts`): letzte freie Lage jedes Teils merken (höchstens 1,5 mm eingedrungen).
  Steckt ein Kontakt tiefer als max(4 mm, ¼ der dünneren Dicke), beide Teile dorthin zurück, wenn dort frei
  (`Physics::overlaps`), Geschwindigkeit in das Hindernis entfernen. Paare, die 5-mal in 1 s klemmen, 3 s lang nur sanft trennen.
- **Bau-Modus:** `move` und Ziehen mit der Maus prüfen den Weg mit `Physics::sweep` (`CastShape`) und gleiten an Hindernissen entlang.

### Korrektur in Jolt

`cmake/patch_jolt.cmake` ergänzt in `Jolt/Physics/Collision/Shape/CompoundShapeVisitors.h`
(`CollideCompoundVsShapeVisitor`) die fehlende Vergrößerung der Teilhüllen-Boxen um `mMaxSeparationDistance`.
Ohne sie bekommt eine zusammengesetzte Form, die als **erste** Form geprüft wird, für Teilhüllen knapp außerhalb keinen
vorausschauenden Kontakt (die Gegenrichtung `CollideShapeVsCompoundVisitor` macht es richtig). Das Skript ist
idempotent und läuft als `PATCH_COMMAND` von `FetchContent`.

Außerdem ist `FLOATING_POINT_EXCEPTIONS_ENABLED` aus: Jolt schaltet sonst im Release-Build Gleitkomma-Ausnahmen ein
und beendet das Programm bei harmlosen Überläufen in Zwischenrechnungen.

## Verbindungen und Bruch

- Jede Verbindung ist ein Jolt-Constraint: Kleber/Nägel/Schrauben/Schweißnaht = `FixedConstraint`,
  Scharnier = `HingeConstraint` (mit Motor), Feder = `DistanceConstraint` mit Federeinstellung, Seil = `DistanceConstraint`
  mit nur Höchstlänge. Ist ein Partner fixiert, hängt das andere Teil direkt an der Welt.
- Nach jedem Physikschritt wird aus den Lagrange-Multiplikatoren die übertragene Kraft und das Moment berechnet
  (λ / Δt), in Zug, Scherung und Moment zerlegt und mit der Tragfähigkeit verglichen. Bei 100 % (Mittel über
  zwei Schritte) wird das Constraint entfernt und eine Meldung erzeugt.
- Aufprall: Der Kontakt-Listener meldet Stöße mit Annäherungsgeschwindigkeit und reduzierter Masse.
  Übersteigt die Energie Bruchenergie × kleinster Querschnitt, wird das Teil mit den normalen Säge-Funktionen
  geteilt; die Stücke bekommen die Geschwindigkeit des Teils.
- Wird ein Teil gesägt oder neu aufgebaut, werden seine Verbindungen mit verschobenen Ankerpunkten neu angelegt.
- Formeln und Werte: [08-Verbindungen-und-Bruch.md](08-Verbindungen-und-Bruch.md).

## 3D-Chunks

- Die Welt ist in Würfel von **16 × 16 × 16 m** eingeteilt, in **x, y und z** (unbegrenzt in alle Richtungen, 64-Bit-Koordinaten).
- Ein Teil gehört zu dem Chunk, in dem sein Mittelpunkt liegt, und wechselt beim Bewegen automatisch. Teile bewegen sich frei, nicht im Raster.
- Jeder Chunk kennt die Hülle seines Inhalts. Beim Zeichnen werden ganze Chunks außerhalb des Blickfelds übersprungen.
- Leere Chunks werden freigegeben.
- Das Gelände (Welttyp normal) benutzt dasselbe 16-m-Raster: je Chunk ein Höhenfeld für die Physik und, wenn geändert, ein
  Eintrag in der Regionsdatei. Gezeichnet wird es in eigenen Kacheln mit Detailstufen ([16](16-Weltgenerierung.md)).

## Grafik (Vulkan 1.3)

| Technik | Zweck |
|---|---|
| Dynamic Rendering, Synchronization2 | moderne Vulkan-API ohne Renderpass-Objekte |
| **Kamerarelativ** (Floating Origin) | alle Positionen relativ zur Kamera in doppelter Genauigkeit berechnet: kein Zittern weit vom Ursprung |
| **Reversed-Z, unendliche Projektion** | sehr hohe Tiefengenauigkeit, Sichtweite bis zum Horizont |
| Frustum-Culling je Chunk und je Teil | nur Sichtbares wird gezeichnet (alle Ansichten der 4-fach-Ansicht berücksichtigt) |
| **Detailstufen (LOD)** nach Bildschirmgröße | > 28 px: volle Form + Kanten; 3–28 px: volle Form; < 3 px: Ersatzquader; < 0,4 px: weggelassen |
| Schattenkarte 4096² | Sonnenschatten um den Blickpunkt, mit weichen Rändern (PCF) |
| 4× MSAA | glatte Kanten |
| Physikalisch basiertes Licht | GGX-Glanz, Metall/Rauheit, Himmels- und Bodenlicht, ACES-Tonemapping |
| Prozedurale Materialien | Maserung, Sprenkel, Poren werden im Shader im Objektraum berechnet (durchgehend, auch an Schnittflächen). Feine Muster werden entfernungsabhängig ausgeblendet (kein Flimmern) |
| Wärme | Temperaturgitter je Teil als Speicherpuffer (trilinear gelesen): Glühen (Schwarzer Strahler), Verkohlung mit glühenden Rissen, Anlauffarben nach der Höchsttemperatur, Reif, Schmelzglanz |
| Feuer (`fire.vert`, `fire.frag`) | Flammen, Rauch, Funken, Kältenebel als kamerazugewandte Rechtecke; Form und Flackern aus aufsteigendem Rauschen im Shader; vormultipliziertes Alpha (Flammen leuchten additiv, Rauch deckt ab) |
| Wasser (`water.vert`, `water.frag`, `waterView`/`waterSun` in `common.glsl`) | Oberfläche als Gitter aus `gl_VertexIndex`, Wellen wie in der Physik, Fresnel-Spiegelung, Sonnenglanz, Schaum; Körper färben sich nach ihrem Weg durch das Wasser, Lichtflecken am Grund ([14](14-Wasser.md)) |
| Feuerschein | bis zu 8 Punktlichter im Bild-Uniform, flackernd |
| Linien | Kanten, Auswahl, Achsen, Werkzeugvorschau (Schnittfläche, Kante, Fräsfläche), Kraftpfeile, Seile, Federn (Zickzack), Verbindungs-Markierungen |
| Boden | genaues Mittelstück (für feine Rasterlinien) + Außenring bis zum Horizont, folgt der Kamera |

Die Shader liegen in `shaders/` und werden beim Bauen in SPIR-V übersetzt und ins Programm eingebettet.

## Befehle und Aufträge

1. Jede eingehende Zeile (Konsole, Skript, Fernsteuerung, Knöpfe der Oberfläche) wird von `Script` übersetzt.
2. Daraus entsteht ein **Auftrag** mit eigenem Ausführungsstapel.
3. Jedes Bild läuft jeder Auftrag weiter, bis er wartet (`wait`, `wait until`) oder fertig ist.
4. Einzelne Befehle führt `Commands` aus. Fenster-Befehle (`player`, `window` …) gibt es nur in der App.
5. Fertige Fernaufträge schicken ihre gesammelte Ausgabe zurück, danach eine Zeile `<<end>>`.

Ohne Fenster läuft ein Auftrag sofort komplett durch. Wartezeiten werden dabei als Physiksimulation abgearbeitet.
Die Beispielwelt (`Showcase.cpp`) ist selbst nur eine Liste solcher Befehle, die beim Laden synchron ausgeführt wird.

## Dateiformat `.werkstatt`

Text, eine Angabe pro Zeile, Zahlen mit voller Genauigkeit (Längen in Metern):

```
WERKSTATT-SCENE 2
next_id 26
object <id> <art 0=Körper 1=Import> <fixiert> <achse -1..2> <U/min> <Nm> <bearbeitungen> <zufallswert>
material spruce
name Board
shape box 80.0 x 10.0 x 2.0 cm
pos <x> <y> <z>                      (Meter, Schwerpunkt)
rot <w> <x> <y> <z>                  (Quaternion)
prim <1 Quader|2 Kugel|3 Zylinder> <maße xyz>   (nur Grundformen, für die exakte Kollisionsform)
collision <1 exact|2 hull>           (nur wenn nicht auto)
unbreakable 1                        (nur wenn gesetzt)
meltform <1 Tropfen|2 Pfütze>           (nur bei Schmelze)
heat <nx ny nz> <ecke xyz> <zelle xyz>  (nur wenn das Teil ein Temperaturgitter hat)
heatcells <je Zelle: °C höchste_°C verkohlt brennstoff schmelze brennt> ...
pieces <anzahl>
piece <anzahl flächen>
f <nx> <ny> <nz> <d> <glatt> <zentrum xyz> <achse xyz> <innen> <radius> <anzahl ecken> <ecken xyz ...>
...
end
joint <nr> <art 0..6> <a> <b> <anker a xyz> <anker b xyz> <normale xyz> <achse xyz> <fläche> <dicke a>
      <haftung> <scherung> <spalt> <anzahl> <länge> <durchmesser> <naht> <max kraft> <steifigkeit> <dämpfung>
      <ruhelänge> <U/min> <Nm> <unzerstörbar> <kleber>          (alles in einer Zeile)
next_joint <nr>
water <nr> <flüssigkeit> <meer 0/1> <lo xyz> <hi xyz> <wellen> <strömung xyz> <eigene temp 0/1> <°C>
                                     (Becken: Quader lo..hi, Spiegel hi.y; Meer: Spiegel hi.y)
waterground <nr> <standardgrund>     (nur Meer: Grund in m, wo keine Kachel eingetragen ist)
groundtile <nr> <kachel x> <kachel z> <32 x 32 Höhen in m>   (Kachel 16 m mit eigenem Grund, Zellen 50 cm, Zeile für Zeile)
next_water <nr>
```

Bei importierten Modellen stehen statt der Stücke `scale` und `file` (das Modell wird beim Laden neu eingelesen).
Arten: 0 Klebung, 1 Nägel, 2 Schrauben, 3 Schweißnaht, 4 Scharnier, 5 Feder, 6 Seil.
Luft, Wind, `breakage on/off`, Lufttemperatur und `heat on/off` gehören nicht zur Szene und werden nicht gespeichert (die Temperaturen der Teile schon).
Bei einer **Welt** aus der Weltliste stehen sie in deren `world.json` (Abschnitt `environment`), zusammen mit Seed,
Kamera und Spielfigur ([15](15-Hauptmenue-und-Welten.md#speichersystem)). Meer ohne `waterground` (ältere Dateien):
Grund beim Boden der Welt bzw. 10 m unter dem Spiegel ([14](14-Wasser.md)).

**Alte Dateien** (`WERKSTATT-SZENE 1` mit deutschen Wörtern wie `objekt`, `stueck`, `verbindung`, Materialnamen wie
`Fichte` und Kleber wie `holzleim`) werden weiter geladen und beim nächsten Speichern im neuen Format geschrieben.

Baum-Teile haben zusätzlich `tree <art> <form> <größe> <ursprung x y z> <krone> <wurzel> <platz> <zustand>`, entfernte
Bäume des Generators stehen als `tree_taken <platz>` am Ende ([17](17-Baeume.md)).

### Binär: `world.bin`

Welten aus der Weltliste speichern ihre Teile **binär** in `world.bin` (`src/WorldBin.cpp`, Teile wie im LAN mit
`src/ObjectIO.cpp`). Das Textformat bleibt zum Weitergeben und Ansehen (`save datei.werkstatt`); `save datei.bin`
schreibt binär, `load` erkennt beide an der Kennung.

| | Text (`.werkstatt`) | Binär (`world.bin`) |
|---|---|---|
| Inhalt | Teile, Verbindungen, Gewässer, Temperatur, Bäume | dasselbe, dazu die fertige **Kollisionsform** jedes Teils |
| Beispielwelt (161 Teile) | 5,4 MB, Laden 8,6 s | 2,9 MB, Laden 0,09 s |
| Zahlen | Text mit 17 Stellen | 32/64 Bit wie im Speicher |

Aufbau: Kennung `WKBIN` (8 Byte), Version, nächste Nummern; Teile (Zustand, Stücke mit allen Flächen, Kollisionsform nur
wenn fertig berechnet, Temperatur je Zelle, Baum-Zustand), Verbindungen, Gewässer (mit Grund des Meers), entfernte Bäume.
Unveränderte Bäume des Generators und die Flüsse stehen nicht darin (sie entstehen aus dem Seed), das Gelände steht in den
Regionsdateien. Ältere Welten mit `scene.werkstatt` werden gelesen; das nächste Speichern schreibt `world.bin` (die alte
Datei bleibt liegen und wird nicht mehr benutzt).

## Bauen

Benötigt: Visual Studio 2022 (C++), Vulkan SDK 1.3+, CMake 3.20+, Internet beim ersten Konfigurieren.
Für das Hauptmenü: Qt 6.5+ für MSVC 2022 (Module Qt Quick, Qt Quick Controls), wird unter `C:/Qt/6.*/msvc2022_64`
gefunden; ohne Qt startet das Spiel ohne Hauptmenü ([15](15-Hauptmenue-und-Welten.md)).

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\Werkstatt.exe --selftest
```

Abhängigkeiten (werden automatisch geladen): GLFW 3.4, GLM 1.0.1, Jolt Physics 5.3.0 (doppelte Genauigkeit,
mit der Korrektur oben), Dear ImGui 1.91.9.

**Welche Konfiguration?** Zum Spielen und Testen `Release` oder `RelWithDebInfo` (optimiert, in Visual Studio trotzdem
mit Haltepunkten und Aufrufliste). `Debug` nur zum Schritt-für-Schritt-Debuggen des eigenen Codes: Dort sind die
Abhängigkeiten (Jolt, GLFW) zwar optimiert und kleine `inline`-Funktionen (glm) werden eingesetzt (`/Ob1`), der Spielcode
selbst ist aber nicht optimiert und rechnet etwa 3- bis 10-mal langsamer. Der Fenstertitel zeigt dann „Debug-Version“.
Jolts eigener Profiler und Debug-Zeichner sind abgeschaltet (das Spiel misst selbst mit `profile`).

## Leistung und Threads

| Was | Wie |
|---|---|
| Physik (Jolt) | eigener Thread-Pool mit allen Kernen außer einem |
| Zeitbudget der Physik | höchstens ca. 20 ms je Bild (`Physics::step`): reicht die Zeit für 240 Schritte/s nicht, läuft die Physik in Zeitlupe, statt dass jedes Bild länger wird und das nächste noch mehr nachholen muss |
| Ruhende Teile | kosten je Physikschritt nichts: Lage übernehmen, Luftkräfte und „letzte freie Stelle“ nur für wache Körper (`Physics::activeBodies`, `World::syncAwake`) |
| Kollisionsform bearbeiteter Teile | im Fenster **im Hintergrund** (`src/Jobs.*`, Hilfs-Threads mit niedriger Priorität): nach Sägen, Bohren, Zerbrechen, Abdruck, Abbrennen bekommt das Teil sofort eine grobe Form (eine Hülle je Stück, dünne Splitter weggelassen), die genaue (Lücken bleiben offen, Bohrungen exakt) wird nachgereicht (`World::pollCollisionJobs`, jedes Bild). Ohne Fenster und im Selbsttest sofort genau |
| Hüllen zusammenfassen | die Paarbewertung in `clusterHulls` läuft auf allen Kernen |
| Wärme je Teil | Zellen, Verbrennen, Schmelzen, Wärmeleitung: jedes Teil auf einem eigenen Kern (`jobs::parallelFor`) |
| Flamme und Strahlung | je Teil parallel; nahe glühende Teile (30 cm) werden zu einer Strahlungsquelle zusammengefasst |
| Brennende Stellen | je Teil zu wenigen zusammengefasst (kleine Teile: eine, große: höchstens 48) |
| Abbrennen, Schmelzen | Formänderungen reihum mit 4 ms Budget je Wärmeschritt; durchgebrannte Teile behalten höchstens 8 Bruchstücke, Krümel werden Asche |
| Teil zu Physik-Körper | Tabelle nach Jolts Körpernummer (direkter Zugriff, `World::fromBody`) |
| Strahl (Maus, Blick) | erst die Weltbegrenzung jedes Teils prüfen, nur Treffer ins Teil umrechnen |
| Konsole | zeichnet nur die sichtbaren Zeilen (bis zu 3000 im Speicher) |

**Messen:** `profile` zeigt, wo die Zeit bleibt (seit dem letzten `profile reset`): Bilder pro Sekunde, langsamstes
Bild, Bilder über 50 ms und je Abschnitt Aufrufe, Summe, ms pro Sekunde, ms pro Aufruf und Höchstwert. Wichtige
Abschnitte: `physics.jolt`, `step.*` (Teile eines Physikschritts), `heat.*` (Wärmeschritt), `shapes.*` (Abbrennen,
Kohle), `geom.*` (neue Geometrie: Kollisionsform, Grafik, Wärmegitter), `impact.*`, `render.waitGpu` (die Grafikkarte
ist noch beschäftigt), `render.acquire (vsync)` (Warten auf den Bildschirm-Takt). Eigene Messpunkte: `PROFILE("name");`
am Anfang eines Blocks (`src/Profile.h`).

Gemessen (RX 6800 XT, 165-Hz-Bildschirm), langsamstes Bild / Bilder über 50 ms in 25 s:

| Szene | vorher | jetzt |
|---|---|---|
| Feuerstelle, großes Feuer (`heat speed 8`) | 81 ms / 19 | 12 ms / 0 |
| Holzstapel mit 30 Scheiten, voller Brand (110 brennende Teile) | 148 ms / 34 | 29 ms / 0 |
| Bohren eines Bretts (Kollisionsform) | 48 ms | 2 ms (genaue Form im Hintergrund) |

Rechenzeit je Physikschritt (Ryzen 7 7700X, ohne Fenster gemessen):

| Szene | vorher | jetzt |
|---|---|---|
| Demo-Werkstatt, `Debug`-Build: Jolt | 2,6 ms | 0,3 ms (Jolt auch in Debug optimiert) |
| 1600 ruhende Teile, Release: Spielcode je Schritt (Lage, Luft, Eindringen) | 0,08 ms | 0,00 ms |

600 gleichzeitig fallende Kisten im Spiel: Release 168 Bilder/s, `Debug` 39 Bilder/s (dort greift das Zeitbudget, die
Physik läuft leicht in Zeitlupe).

## Selbsttest

`Werkstatt.exe --selftest` prüft (173 Prüfungen):

- Geometrie: Volumen, Sägeschnitt, Bohrung (π r² h), Innenflächen, Schnittfuge, Zahnrad, Kugel, Kanten finden,
  Abrunden/Fasen (Volumen nach Formel), Hohlkehle, Drehbank
- Physik: Masse, Fallen und Liegen, freier Fall, Reibung auf der Rampe (Stahl rutscht, Gummi haftet, Stahl auf 20° kriecht nicht),
  Inspektor-Kraftbilanz
- Werkzeuge: Befehle `new`, `cut`, `drill`, Massenverlust, alte deutsche Befehle werden abgelehnt
- Bearbeitung: Abrunden, Tasche, Nut, Hobeln, Biegen (Volumen bleibt), Ziegel nicht biegbar, Drehen, Vereinen,
  Ausschneiden, Skalieren, Spiegeln
- Kollision: gebohrte Platte behält ihr Loch, Stift fällt durch, `move` hält an, Rollreibung, Ziegelturm steht still,
  ineinander steckende Teile trennen sich sanft
- Zeichnung: verdeckte Kanten, Ø-Beschriftung, SVG und Maßstab
- Befehlssprache: Folgen, `wait until`, `if/else`, `repeat`, `set`/`$`, Textvergleich, Fehler, Zeitlimit
- Verbindungen und Bruch: Kleber hält / reißt, Seilpendel, Ziegel zerbricht, Stahl nicht, Verbindungen speichern
- Luft: Propellerschub (×4 bei doppelter Drehzahl), Vakuum, Propeller-Wagen
- Wärme und Feuer: Brenner heizt ein Stabende (anderes Ende kühl), Abkühlen, glühender Stahl weich, kalter Stahl zerspringt
  (warmer nicht), Brett verkohlt, brennt und das Feuer breitet sich aus, das Brett wird nach und nach kleiner und Kohle bleibt liegen, Löschen, kein Feuer im Vakuum, Aluminium schmilzt
  zur Pfütze, Holzleim lässt bei 150 °C los, Temperatur bleibt beim Speichern und Laden
- Materialdatei (mit der eingebetteten Kopie, unabhängig von eigenen Änderungen): Laden, Aliasse, Speichern und Lesen
  ergibt dieselben Werte, kaputte Datei wird abgelehnt (alte Liste bleibt), unbekannte Schlüssel gemeldet, neues Material
  per Befehl sofort benutzbar, Dichte ändern ändert vorhandene Teile, Neuladen behält das Material über den Namen
- Wasser: Fichte schwimmt zu 47 % unter Wasser, schräg eingesetzt legt sie sich flach, Stahl sinkt, Stahlboot schwimmt und
  sinkt vollgelaufen, Becher läuft unter Wasser voll, umgedreht hält er Luft, volles Glas kopfüber läuft aus, Abschrecken,
  Wellen, Meer, Speichern und Rückgängig; Fluss: Strömung um einen Pfeiler (wie die Potentialströmung), Stau und Schaum davor,
  Treibgut fährt vorbei, ohne Pfeiler fließt es wieder gerade
- Gelände ([16](16-Weltgenerierung.md)): Rauschen ist reproduzierbar, gleicher Seed ergibt gleiches Gelände, Biome passen zu
  ihrem Klimabereich, Einzelbiom, weiche Übergänge an den Grenzen, nächste Stelle eines Bioms, Klotz liegt still am Hang, Kugel rollt den Hügel hinab und
  bleibt auf der Oberfläche, Strahl trifft die Oberfläche, Anheben, Regionsdatei speichern, Neuladen, Zurücksetzen löscht sie;
  Flüsse: nicht selten, nicht überall, nie in der Wüste, außerhalb der Flussbetten trocken, alte Welten unverändert, Wasser im
  Flussbett, ein Holzball schwimmt und treibt mit
- Bäume ([17](17-Baeume.md)): Formen (groß/klein, immer gleich), Wald je Biom aus dem Seed, nahe Bäume als feste Teile, Fällen
  (Stumpf fest, Stamm mit Krone liegt), Wegräumen, Speichern und Laden, Baumschule
- Speichern und Laden, Beispiel-Werkstatt, Getriebe-Übersetzung, Beispielwelt (132 Teile, läuft 5 s ohne Ausreißer)

`Werkstatt.exe --bench [szene]` misst Rechenzeit, Eindringtiefe und Rettungen in elf Physik-Szenen, jeweils mit den alten
und den neuen Einstellungen (Ergebnisse in [05-Physik-und-Materialien.md](05-Physik-und-Materialien.md#rechenzeit-und-genauigkeit)).
Mit einem Wort dahinter laufen nur die Szenen, deren Name es enthält (z. B. `--bench Beispiel`).

## Erweitern

| Vorhaben | Wo |
|---|---|
| neues Material | nur `assets/materials.json` (ein Eintrag in `"materials"`, siehe [13](13-Materialien.md)) oder im Spiel: Menü Ansicht → Materialien bearbeiten. Neue Werte für alle Materialien: Feld in `Material` (`Materials.h`) und eine Zeile in der Feldtabelle `fieldDefs()` (`Materials.cpp`) – Datei, Befehle und Editor kennen ihn dann automatisch. Neue Darstellung: `shaders/mesh.frag` |
| neuer Befehl | `src/Commands.cpp` (`runCommand`), Hilfe-Text in `commandHelp()` |
| neue Bearbeitung | `src/Edit.cpp` (Werkzeug als Liste konvexer Ebenen-Körper, dann `removeMaterial` oder `applyResult`) + Befehl in `Commands.cpp` |
| neuer Fenster-Befehl | `src/AppCommands.cpp` (`appCommand`) |
| neuer Wert für die Befehlssprache | Teile: `objectProp` in `src/Script.cpp`; App-Werte: `App::queryValue` |
| neue Grundform | `src/Solid.cpp` (Stücke aus Ebenen bauen) + `World::add...` + Befehl `new` |
| neuer Kleber | Liste `kGlues` in `src/Joints.cpp` |
| neue Verbindungsart | `JointType` in `src/World.h`, Tragfähigkeit in `World::computeCapacity`, Constraint in `World::buildConstraint` |
| neue Station in der Beispielwelt | Befehlszeilen in `kShowcase[]` in `src/Showcase.cpp` |
