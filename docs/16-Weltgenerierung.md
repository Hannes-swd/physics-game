# Weltgenerierung: Gelände und Biome

Eine Welt vom Typ **Normal** hat Gelände statt der flachen Werkstattebene. Das Gelände entsteht vollständig aus dem
**Seed**: Gleicher Seed bedeutet gleiche Hügel und gleiche Biome an denselben Stellen, auf jedem Rechner. Gespeichert wird
nur, was man am Gelände verändert hat.

Zum Ausprobieren: Hauptmenü → *Spielen* → *Neue Welt*, Welttyp **Normal**, dann die Welt öffnen.
Per Befehl: `menu new Hügeltest 4711 normal ; menu play Hügeltest`.

## Welttypen (beim Erstellen)

| Welttyp | Was |
|---|---|
| **Normal** | Gelände mit Biomen. Unter *Biome*: **Alle Biome** oder **nur ein Biom** (die ganze Welt ist dann so) |
| **Flach** | die Ebene bei y = 0 wie bisher (Werkstatt, Beispielszenen) |

Der **Seed** steht beim Öffnen des Dialogs schon fest (eine Zufallszahl) und lässt sich ändern: eine Zahl oder ein
beliebiger Text (aus dem Text wird eine Zahl berechnet, gleicher Text = gleiche Welt). **Zufällig** würfelt neu.

## Wie die Biome entstehen

Biome werden nicht zufällig verteilt, sondern dort, wo ihr **Klima** passt. Aus dem Seed entstehen vier Klimafelder, je
mit Werten von 0 bis 1, die sich nur langsam über die Welt ändern (über einige hundert Meter bis Kilometer):

| Klimawert | Bedeutung | Größe der Gebiete (Standard) |
|---|---|---|
| `temperature` | Wärme (0 kalt, 1 heiß) | 4,5 km |
| `humidity` | Feuchtigkeit (0 trocken, 1 nass) | 3,8 km |
| `continentalness` | Kontinentalität (0 offenes Meer, 1 tief im Landesinneren) | 5 km |
| `erosion` | Zerklüftung (0 flach, 1 bergig) | 900 m |

Jedes Biom sagt in [`assets/biomes.json`](../assets/biomes.json), in welchen **Bereichen** dieser Werte es vorkommt. An
jedem Ort gewinnt das Biom, dessen Bereiche am besten passen:

- Liegt der Ort in den Bereichen eines Bioms, zählt, wie tief er darin liegt (Abstand zum nächsten Rand). Liegt er
  außerhalb, zählt, wie weit er weg ist.
- Weil sich das Klima stetig ändert, grenzen nur Biome mit **ähnlichem Klima** aneinander. Beispiel: Eine Wüste
  (`temperature` 0.75–1, `humidity` 0–0.25, `continentalness` 0.45–1) kann nie direkt an einem Ozean liegen
  (`continentalness` 0–0.3): Dazwischen liegt immer ein Gebiet mit Kontinentalität 0.3–0.45, etwa ein Strand.
- An der Grenze werden **Höhe und Farbe weich überblendet** (Breite `border_blend`), es gibt keine Kanten. Das Biom
  selbst wechselt an einer scharfen Linie.
- Fehlt ein Klimawert bei einem Biom, gilt der ganze Bereich 0–1.

Jeder Zweck hat seinen eigenen Seed, abgeleitet aus dem Welt-Seed und einem Namen (`climate/temperature`,
`height/hills` …). Ein neues Biom in der Datei verschiebt deshalb die Höhen der alten Biome nicht.

**Biome (Standard):**

| Biom | Klima | Gelände | Oberfläche | Flüsse |
|---|---|---|---|---|
| `plains` (Ebene) | feucht (`humidity` 0.35–0.62), `erosion` 0–0.5 | 1 m ± 1,2 m, Formen ~180 m | Gras, Hänge erdig | ja |
| `forest` (Wald) | sehr feucht (`humidity` ab 0.62), `erosion` 0–0.5 | 1,5 m ± 2,5 m, Formen ~150 m | dunkler Waldboden | ja |
| `hills` (Hügellandschaft) | feucht (`humidity` ab 0.35), `erosion` 0.5–1 | 4 m ± 16 m, Formen ~140 m, 5 Oktaven | dunkleres Gras, steile Hänge felsig | ja |
| `desert` (Wüste) | heiß und trocken: `temperature` 0.55–1, `humidity` 0–0.35 | 2 m ± 4 m, Dünen ~60 m | Sand (Material `sand`) | **keine** |

Kalte, trockene Gegenden (dafür gibt es noch kein eigenes Biom) werden Ebene oder Hügel, je nachdem, was näher liegt.
Im Wald stehen dicht Bäume, in den Hügeln einige Fichten, in der Ebene einzelne Eichen ([17](17-Baeume.md)).

## Flüsse

Flüsse entstehen ebenfalls nur aus dem Seed, mit dem Gelände zusammen:

- **Wo:** Die Flusslinien sind die Nulllinien eines eigenen, großen Rauschfelds (`spacing`, Standard 2,5 km). Zwei weitere
  Felder verschieben es ein wenig: so entstehen Schlingen (`meander`). Die Linien sind lang und durchgehend.
- **Wie viele:** Ein weiteres Rauschfeld entscheidet, welche Abschnitte der Linien Wasser führen (`presence`, 60 %).
  Wo ein Fluss endet, wird er schmaler und flacher, bis das Bett über dem Wasser liegt (wie an einer Quelle).
  Auf einer geraden Strecke trifft man etwa alle 5–6 km auf einen Fluss.
- **Biom:** Jedes Biom sagt mit `rivers` (0..1), wie viele Flüsse dort Wasser führen. Die Wüste hat 0: Dort gibt es nie
  einen Fluss; ein Fluss läuft schon vor ihrer Grenze aus. Gerechnet wird mit dem Abstand zum gewinnenden Biom, darum gilt
  das auch in Gegenden, deren Klima zu keinem Biom passt.
- **Form:** Entlang der Linie wird eine Rinne gegraben (5–14 m breit, 1–2,2 m tief, in der Mitte am tiefsten), dazu ein
  Tal: Das Gelände darf neben dem Fluss höchstens so hoch sein wie ein Hang mit `valley_slope` (19°) vom Ufer aus; nach
  `valley` m (90) gilt wieder das normale Gelände. In der Ebene liegt das Ufer flach, im Hügelland schneidet der Fluss
  ein Tal. Flussbett und Ufer haben eine eigene Farbe (`bed_color`).
- **Wasserspiegel:** Alle Flüsse haben denselben Spiegel (`level`, 0 m). Außerhalb der Flussbetten bleibt das Gelände
  mindestens `floor` (30 cm) darüber, tiefere Senken werden weich angehoben. So steht nirgends sonst Wasser, und wer
  irgendwo tiefer gräbt, stößt nicht auf Wasser (nur direkt am Fluss läuft die Grube voll).
- **Wasser:** Die Flüsse sind ein echtes Gewässer („rivers of the terrain“ in `water`): Teile schwimmen und treiben mit
  der Strömung (0,4–1,1 m/s, schmale Flüsse schneller, in der Mitte schneller als am Ufer), man kann schwimmen, das
  Strömungsgitter lenkt das Wasser um Steine und Teile. Das Gewässer entsteht mit dem Gelände, wird nicht gespeichert und
  im LAN nicht übertragen (jeder Rechner erzeugt es selbst); `water remove all` lässt es stehen.
- **Alte Welten** (ohne `rivers` in ihrer `generator.json`) bleiben genau, wie sie waren: keine Flüsse, kein Anheben.

`terrain at` zeigt in der Zeile `river`, wie weit die Flussmitte ist, Breite, Tiefe, Strömung und das Wasser an der Stelle.
`tp river` geht ans Ufer des nächsten Flusses. `terrain map bild.png` zeichnet eine Karte von oben mit allen Flüssen.

```json
"rivers": {
  "level": 0.0, "spacing": 2500, "presence": 0.6,
  "meander": 60, "meander_scale": 260,
  "width": [5, 14], "depth": [1.0, 2.2],
  "bank": 0.35, "floor": 0.3,
  "valley": 90, "valley_slope": 0.35,
  "speed": [0.4, 1.1],
  "bed_color": [0.30, 0.26, 0.18]
}
```

| Schlüssel | Bedeutung |
|---|---|
| `level` | Wasserspiegel aller Flüsse (m) |
| `spacing` | Größe des Linienmusters (m): größer = Flüsse weiter auseinander |
| `presence` | Anteil der Linien mit Wasser (0..1), in Biomen mit `rivers`: 1 |
| `meander`, `meander_scale` | Schlingen: wie weit (m) und wie groß die Bögen (m) |
| `width`, `depth` | Breite der Rinne und Tiefe in der Mitte, je `[von, bis]` (m); breite Abschnitte sind tiefer |
| `bank` | Ufer über dem Wasser (m) |
| `floor` | so weit bleibt das Gelände außerhalb der Flüsse mindestens über dem Wasser (m) |
| `valley`, `valley_slope` | Breite des Tals je Seite (m), höchste Steigung der Talhänge |
| `speed` | Strömung `[breiter Fluss, schmaler Fluss]` (m/s) |
| `bed_color` | Farbe von Flussbett und Ufer |
| `biomes[].rivers` | wie viele Flüsse in diesem Biom Wasser führen (0 = keine, 1 = alle) |

## `assets/biomes.json`

```json
{
  "version": 1,
  "climate": {
    "temperature":     { "label": "Wärme",           "scale": 4500, "octaves": 3, "contrast": 1.7 },
    "humidity":        { "label": "Feuchtigkeit",    "scale": 3800, "octaves": 3, "contrast": 1.7 },
    "continentalness": { "label": "Kontinentalität", "scale": 5000, "octaves": 4, "contrast": 1.5 },
    "erosion":         { "label": "Zerklüftung",     "scale": 900,  "octaves": 3, "contrast": 1.9 }
  },
  "border_blend": 0.04,
  "biomes": [
    {
      "id":      "hills",
      "label":   "Hügellandschaft",
      "climate": { "erosion": [0.5, 1.0] },
      "height":  { "base": 4.0, "amplitude": 16.0, "scale": 140, "octaves": 5, "persistence": 0.45 },
      "surface": { "color": [0.16, 0.25, 0.09], "slope_color": [0.33, 0.31, 0.27],
                   "slope_start": 0.3, "slope_end": 0.55, "material": "soil" },
      "rivers":       1.0,
      "trees":        { "per_ha": 18, "types": { "spruce_big": 1, "spruce_mid": 2, "spruce_young": 2 } },
      "border_color": [1.0, 0.45, 0.15]
    }
  ]
}
```

| Schlüssel | Bedeutung |
|---|---|
| `climate.<wert>.scale` | Größe der Klimagebiete in m (größer = Biome weiter auseinander) |
| `climate.<wert>.octaves` | Feinheit der Ränder (mehr = unruhigere Grenzen) |
| `climate.<wert>.contrast` | Streckung: größer = Werte nahe 0 und 1 kommen häufiger vor |
| `border_blend` | Übergangsbreite zwischen Biomen, in Klimaeinheiten (0.04 = einige Dutzend Meter) |
| `biomes[].id`, `label` | Name für Befehle (klein, eindeutig) und Anzeigename im Menü |
| `biomes[].climate` | Bereiche `[von, bis]` je Klimawert |
| `biomes[].height` | Höhe = `base` + `amplitude` · Rauschen (−1..1). `scale` = Größe der Formen (m), `octaves` = Detailstufen, `persistence` = wie stark jede feinere Stufe ist, `lacunarity` (2) = Frequenzschritt |
| `biomes[].surface.color` | Farbe der Oberfläche (linear, 0..1) |
| `biomes[].surface.slope_color`, `slope_start`, `slope_end` | Farbe an steilen Hängen; Übergang zwischen `slope_start` und `slope_end` (gemessen als 1 − Normale.y: 0 = eben, 0.3 ≈ 45°) |
| `biomes[].surface.material` | Material für Reibung und Stöße (aus `materials.json`, z. B. `soil` = Erde) |
| `biomes[].rivers` | wie viele Flüsse hier Wasser führen (0..1), siehe unten |
| `biomes[].trees` | Bäume: `per_ha` (je Hektar) und `types` (Arten aus `trees.json` mit Gewicht), siehe [17](17-Baeume.md) |
| `biomes[].grass`, `flowers`, `bushes` | Bewuchs: Dichte, Höhe, Flecken, Farben, siehe [18](18-Bewuchs.md) (dazu oben `flora`: Reichweiten) |
| `biomes[].stones`, `sticks`, `logs`, `stumps` | Zeug am Boden: je Hektar, Größe, Dicke, wie viele kleine, Material, siehe [19](19-Steine-und-Holz.md) (dazu oben `scatter`: Reichweiten, ab wann ein Stein groß ist) |
| `biomes[].border_color` | Farbe der Biomgrenze auf diesem Biom |

Fehler in der Datei werden mit Ort gemeldet (z. B. `biomes[1].climate: unknown value 'warmth'`); dann gilt die beim Bauen
eingebettete Kopie.

**Jede Welt hat ihre eigene Kopie:** Beim Erstellen wird `biomes.json` als `generator.json` in den Weltordner kopiert.
Änderungen an `assets/biomes.json` gelten deshalb nur für **neue** Welten; alte Welten sehen immer gleich aus. Um eine
offene Welt bewusst umzustellen (zum Ausprobieren von Werten): `terrain reload` liest `assets/biomes.json` neu, erzeugt das
Gelände neu und ersetzt die Kopie der Welt. Änderungen am Gelände bleiben dabei erhalten.

**Ein neues Biom anlegen:** einen Eintrag in `biomes` mit eigener `id`, Klimabereichen und Höhe. Beispiel einer Wüste:

```json
{
  "id": "desert", "label": "Wüste",
  "climate": { "temperature": [0.75, 1.0], "humidity": [0.0, 0.25], "continentalness": [0.45, 1.0] },
  "height":  { "base": 3.0, "amplitude": 6.0, "scale": 90, "octaves": 4, "persistence": 0.4 },
  "surface": { "color": [0.62, 0.52, 0.33], "slope_color": [0.55, 0.42, 0.28], "material": "soil" },
  "border_color": [1.0, 0.95, 0.4]
}
```

Wichtig: Die Bereiche der Biome sollten zusammen den ganzen Klimaraum abdecken, sonst gewinnt an manchen Orten ein Biom
von außerhalb seiner Bereiche (das mit dem kleinsten Abstand).

## Gelände: Kacheln, Detailstufen, Physik

| Teil | Wie |
|---|---|
| **Raster** | Höhenpunkte alle 0,5 m. Höhe = Generator + Änderung des Punkts |
| **Chunk** | 16 × 16 m (32 × 32 Punkte), wie die Chunks der Teile. Einheit für Physik und gespeicherte Änderungen |
| **Kacheln** | 32 × 32 Vierecke, in 8 Stufen: Stufe 0 hat 0,5-m-Vierecke (eine Kachel = ein Chunk), Stufe 7 hat 64-m-Vierecke (Kachel 2048 m). Nahe der Kamera fein, weiter weg grob (eine Kachel wird geteilt, wenn die Kamera näher als ihre doppelte Größe ist). Sichtweite 2,5 km |
| **Nur die Oberfläche** | jede Kachel ist ein Dreiecksnetz der Oberfläche, Farbe je Eckpunkt (Biom, Hang). Senkrechte „Schürzen“ an den Rändern verdecken Spalten zwischen feinen und groben Kacheln |
| **Im Hintergrund** | Kacheln werden auf den Hilfs-Threads erzeugt, die wichtigsten zuerst (nah im Verhältnis zur Größe). Bis eine feinere Kachel fertig ist, bleibt die gröbere sichtbar: keine Löcher. Nicht mehr gebrauchte Kacheln werden nach 4 s freigegeben |
| **Physik** | je Chunk ein Höhenfeld (Jolt `HeightFieldShape`, 34 × 34 Punkte), nur unter Teilen, die sich bewegen können, und um Spielfiguren und Baukamera. Nach 5 s ohne Bedarf wieder weg. Physik und Bild teilen jedes Viereck entlang derselben Diagonale: Teile liegen genau auf der sichtbaren Oberfläche |
| **Abfragen** | Höhe an einer Stelle, Strahl (Maus, Absetzen), höchster Punkt unter einem Teil (neue Teile stecken nicht im Hang) |

Gemessen (8 Hilfs-Threads): rund 400 Kacheln gezeichnet, 0,1–0,2 ms je Bild für die Auswahl, das Spiel läuft mit
etwa 165 Bildern/s. Beim Öffnen einer Welt wartet der Ladebildschirm, bis die Kacheln um die Kamera da sind
(„Gelände erzeugen: noch N Kacheln“).

**Immer auf der Oberfläche:** Neue Teile werden auf den höchsten Punkt darunter gesetzt. Die Spielfigur (eigene und die
von Mitspielern beim Gastgeber) erscheint auf dem Gelände, und die Physik dort wird sofort angelegt, bevor sie fällt.
Eine gespeicherte Kamera oder Spielfigur, die nach einer Änderung im Boden läge, wird beim Öffnen nach oben gesetzt.
Tritt man einem LAN-Spiel bei, kommt die eigene Kamera auf die Oberfläche.

## Gelände ändern und speichern

```
terrain raise 0 0 800 300       Hügel: um (0, 0), Radius 8 m, 3 m hoch (alle Angaben in cm)
terrain lower 2000 0 500 150    Mulde
terrain flatten 0 0 600         eben auf die Höhe in der Mitte (oder: terrain flatten 0 0 600 250)
terrain reset                   alles wieder wie vom Generator
```

Gespeichert werden **nur die geänderten Chunks** (je Punkt die Höhenänderung), zusammen mit der Welt (automatisch,
Strg+S, beim Verlassen). Alles andere entsteht beim Laden wieder aus dem Seed.

```
Meine Welt/
  world.json         generator "normal", biome "all" (oder eine id), Seed ...
  generator.json     die Kopie von biomes.json, mit der diese Welt erzeugt wird
  regions/
    r.0.0.bin        Regionsdatei: 32 × 32 Chunks = 512 m
    r.-1.0.bin
```

Regionsdatei: `"WTRE"`, Version 1 (uint32), Punkte je Chunk (32), Anzahl Chunks, dann je Chunk: x, z (int32) und
32 · 32 Höhenänderungen (float, m, Zeile für Zeile). Geschrieben wie alles andere: erst `.tmp`, die alte wird zu `.bak`.
Eine Region ohne Änderungen mehr wird gelöscht.

Mit Höhenänderungen lassen sich Hügel, Mulden und ebene Flächen machen, aber keine Höhlen oder Überhänge. Dafür kommt
später eine Voxel-Schicht in der Nähe dazu, gespeichert in denselben Regionsdateien (neue Version).

## Befehle

| Befehl | Wirkung |
|---|---|
| `terrain` | Seed, Biome, Kacheln (gezeichnet, fertig, fehlend, in Arbeit), Physik-Chunks, geänderte Chunks |
| `terrain at [x z]` | an dieser Stelle (ohne Angabe: vor der Kamera): Höhe (und ohne Änderungen), Steigung, Biom, alle vier Klimawerte, Scores aller Biome, die vier Rasterpunkte |
| `terrain biomes` | alle Biome mit Klimabereichen, Höhe, Material; Größe der Klimagebiete |
| `terrain find <biom>` | nächste Stelle, an der das Biom ganz es selbst ist (hinter dem Übergang; bis 20 km, auf 0,5 m genau) |
| `terrain goto <biom>` | dorthin schauen |
| `tp <biom>` | selbst dorthin (Spielfigur; im Bau-Modus die Kamera), auf die Oberfläche, Blick ins Biom ([02](02-Befehle.md#teleportieren-tp)) |
| `terrain raise\|lower <x> <z> <radius> <cm>` | Hügel bzw. Mulde, weich auslaufend |
| `terrain flatten <x> <z> <radius> [höhe]` | eben machen, zum Rand weich ins alte Gelände |
| `terrain reset` | alle Änderungen weg |
| `terrain reload` | `assets/biomes.json` neu lesen und in diese Welt übernehmen |
| `terrain borders on\|off` | Linien an den Biomgrenzen (auch *Ansicht → Biomgrenzen* und in den Einstellungen) |
| `terrain map <bild.png> [größe_m] [pixel]` | Karte von oben um die Kamera (Standard 8 km, 512 Pixel): Farbe der Biome, Höhe als Schatten, Flüsse blau, Raster je km, Mitte rot. Dazu der Anteil jedes Bioms und der Flüsse |

```
> terrain at 30000 30000
at            30000 30000 cm
height        180.8 cm (generator 180.8 cm), slope 16.7°
biome         hills (Hügellandschaft)
climate       temperature 0.46 humidity 0.67 continentalness 0.50 erosion 0.55
scores        plains -0.051 hills 0.051
lattice       ... die vier Rasterpunkte darum (0,5 m), so rechnen Physik und Bild
```

## Biomgrenzen sehen

Standardmäßig aus. Einschalten: *Einstellungen → Biomgrenzen (Linien)*, im Spiel *Ansicht → Biomgrenzen* (oder
`terrain borders on`). Dann liegt eine Linie genau dort, wo das Biom wechselt, knapp über dem
Boden, in der Farbe `border_color` der beiden Biome. Die Linie liegt dort, wo die Scores der beiden Biome gleich sind; sie
ist also so glatt wie das Klima selbst. Gezeichnet für Kacheln bis etwa 1 km Entfernung.

## Mehrspieler

Der Gastgeber schickt beim Beitreten nur **Seed, Biome (der Text von `generator.json`) und die Änderungen**. Der
Mitspieler erzeugt das Gelände daraus selbst, genau gleich wie der Gastgeber. Ändert der Gastgeber das Gelände, gehen die
Änderungen gleich mit. Lesende Geländebefehle (`terrain`, `at`, `biomes`, `find`, `goto`, `borders`) laufen beim
Mitspieler selbst, ändernde beim Gastgeber. Protokollversion 4 (beide brauchen diese Version). Die Flüsse erzeugt jeder Rechner selbst; ihr Gewässer wird nicht übertragen.
`tp <biom>` sucht ebenfalls beim Mitspieler; der Gastgeber setzt dessen Figur dann dort auf die Oberfläche.

## Für Entwickler

| Datei | Aufgabe |
|---|---|
| `src/Noise.*` | deterministisches Rauschen: eigener Ganzzahl-Hash (keine Zufallsverteilungen der Standardbibliothek, die sind je Compiler verschieden), Gradientenrauschen, Oktaven, abgeleitete Seeds |
| `src/WorldGen.*` | `GenConfig` (biomes.json lesen und prüfen), `WorldGen`: Klima, Scores, Biom, Höhe, Farbe an einem Ort. Unveränderlich, von mehreren Threads gleichzeitig benutzbar |
| `src/Terrain.*` | Kacheln und Detailstufen, Erzeugung im Hintergrund, Physik je Chunk, Änderungen, Regionsdateien, Strahl, Biomgrenzen |
| `src/World.*` | `setTerrain`/`clearTerrain` (statt der Ebene bei y = 0), `groundHeight`, `prepareGround`, Strahl und freie Stellen auf dem Gelände |
| `src/AppWorld.cpp` | Welt öffnen mit Generator (`makeWorldGen`, `makeTreeConfig`), Gelände je Bild (`updateTerrain`), Befehle `terrain borders/goto/reload`, `trees reload` |
| `src/Water.*` | `WaterShape`: Gewässer, dessen Grund und Strömung von außen kommen (die Flüsse, `Terrain::waterShape`) |
| `shaders/mesh.frag` | Schattierung 11 = Gelände: Farbe je Eckpunkt (`solid::Vertex::color`), etwas Fleckigkeit, Raster schwächer |

- Gleicher Seed muss überall dasselbe ergeben: kein `/fp:fast`, keine Zufallsverteilungen der Standardbibliothek, und
  Werte nur aus Seed und Ort (nie aus der Reihenfolge, in der Kacheln entstehen). Die Selbsttests prüfen das.
- Ändert sich der Generator so, dass alte Welten anders aussähen, `generatorVersion` erhöhen und den alten Weg behalten.
- Selbsttests (Abschnitt *Terrain*): Rauschen und Generator deterministisch, Biome passen zum Klima, eine Welt mit
  einem Biom, Grenzen ohne Stufen, nächste Stelle eines Bioms (nichts liegt näher), Block und Kugel auf einem Hang, Strahl,
  Ändern, Speichern, Laden, Zurücksetzen.
- Suche nach dem nächsten Biom (`Terrain::findBiome`): quadratische Ringe im 32-m-Raster, nur das Klima; nach dem ersten
  Fund weiter bis zu dem Ring, der nichts Näheres mehr enthalten kann, dann um den Fund im 4-m- und 0,5-m-Raster.
  „Im Biom“ heißt: es ist das Biom an dieser Stelle **und** sein Score ist mindestens `border_blend`, dort mischt sich
  kein anderes Biom mehr in Höhe und Farbe.

## Ausblick

- **Pflanzen** am Boden (Gras, Büsche) je Biom; Bäume gibt es schon ([17](17-Baeume.md)).
- **Meer:** Biome mit niedriger Kontinentalität als Ozean, die Flüsse münden hinein (gleicher Wasserspiegel).
- **Flüsse mit Gefälle:** heute haben alle Flüsse denselben Spiegel (sie fließen eben); Gefälle bräuchte einen Spiegel,
  der entlang des Flusses sinkt, und Wasserfälle.
- **Höhlen und Überhänge:** Voxel-Schicht nahe der Spielfigur über dem Höhenfeld.
- Regionsdateien bei Bedarf nachladen statt alle beim Öffnen (heute: alle, sie sind klein).
