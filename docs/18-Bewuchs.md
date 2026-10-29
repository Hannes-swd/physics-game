# Bewuchs: Gras, Blumen, Büsche

Auf dem Gelände wachsen **Gras**, **Blumen** und **Büsche**. Sie sind nur Bild: keine Physik, keine Kollision, man läuft
hindurch. Dafür kosten sie kaum Rechenzeit. Gras und Blumen wiegen sich im Wind.

Wie viel wo wächst und in welchen Farben, sagt jedes Biom in `assets/biomes.json`:

| Biom | Gras | Blumen | Büsche |
|---|---|---|---|
| `plains` (Ebene) | etwas, in lockeren Flecken | einige, in Gruppen, bunt | einzelne |
| `forest` (Wald) | dicht und höher | wenige | viele |
| `hills` (Hügellandschaft) | kurz, fleckig | einige | einige |
| `desert` (Wüste) | selten, aber wo, dann dichte Büschel; gelb-braun | keine | wenige, trocken |

## Einstellen (`biomes.json`)

Je Biom drei Einträge (fehlt einer, wächst davon nichts):

```json
"grass":   { "density": 5, "height": [0.15, 0.45], "patches": 0.35,
             "colors": [[0.17, 0.28, 0.08], [0.21, 0.32, 0.09], [0.25, 0.31, 0.10]] },
"flowers": { "density": 0.25, "colors": [[0.95, 0.95, 0.92], [0.95, 0.82, 0.15], [0.75, 0.15, 0.12]] },
"bushes":  { "per_ha": 25, "size": [0.6, 1.5], "colors": [[0.15, 0.26, 0.07], [0.19, 0.29, 0.09]] }
```

| Schlüssel | Bedeutung |
|---|---|
| `grass.density` | Grasbüschel je m² (ein Büschel = 3 Halme). 0 = kein Gras |
| `grass.height` | Höhe [von, bis] in m |
| `grass.patches` | 0 = gleichmäßig verteilt, 1 = nur in Flecken (dort dafür bis 3,5-mal dichter). Wüste: 0.9 |
| `grass.colors` | Farben, zufällig gemischt. Am besten nahe der Bodenfarbe (`surface.color`), sonst wirkt es fremd |
| `flowers.density` | Blumen je m²; sie wachsen immer in Gruppen |
| `flowers.colors` | Farben der Blüten (je Blume eine) |
| `bushes.per_ha` | Büsche je Hektar (100 × 100 m) |
| `bushes.size` | Durchmesser [von, bis] in m |
| `bushes.colors` | Laubfarben |

Dazu oben in der Datei, für alle Biome:

```json
"flora": { "grass_distance": 45, "bush_distance": 130 }
```

`grass_distance`: bis wohin Gras und Blumen gezeichnet werden (m), `bush_distance`: bis wohin Büsche. An Biomgrenzen gehen
die Werte weich ineinander über (wie Höhe und Farbe). Im Flussbett, unter Wasser und an steilen Hängen wächst nichts.

Wie bei den Bäumen kommt alles **nur aus dem Seed**: gleicher Seed, gleiche Wiese. Jede Welt hat ihre Kopie der Datei
(`generator.json`); **`terrain reload`** übernimmt Änderungen in die offene Welt. Welten von vor dem Bewuchs haben in ihrer
Kopie noch keine Angaben: sie bekommen die Werte der eingebauten `biomes.json` (gleiche Biom-`id`).

Gräbt man das Gelände um, wird der Bewuchs dort neu gesetzt. Auf Teilen (z. B. dem Boden einer Hütte) wächst er nicht,
aber Gras unter einem flachen Boden kann durchschauen.

## Leistung

- Je Kachel von 16 × 16 m ein Netz für Gras, eins für Blumen und eins für Büsche: wenige Zeichenaufrufe.
- Volles Gras gibt es nur in der Nähe (`grass_distance`). Nach außen wird es **ausgedünnt** (die Büschel liegen in
  zufälliger Reihenfolge im Netz, gezeichnet wird nur ein Anfangsstück) und am Rand in den Boden **abgesenkt**.
- Dahinter, bis zum **Dreifachen** von `grass_distance`, eine **ferne Stufe**: nur etwa jedes sechste Büschel, dafür mit
  dreimal so breiten Halmen, an denselben Flecken. Sie wächst am Übergang aus dem Boden, während das nahe Gras einsinkt:
  kein sichtbarer Rand, und in der Ferne sieht die Wiese trotzdem bewachsen aus.
- Halme werfen keinen Schatten, Büsche in der Nähe schon.
- Einstellungen → Grafik: **Gras und Blumen** (0 = aus bis 150 %: Dichte und Reichweite), **Sichtweite** (Reichweite der
  Büsche), siehe [15](15-Hauptmenue-und-Welten.md).

## Befehle

| Befehl | Wirkung |
|---|---|
| `terrain flora on\|off` | Bewuchs zeigen oder nicht; zeigt die Zahl der Kacheln und der gezeichneten Grasbüschel |
| `terrain reload` | `assets/biomes.json` neu lesen (auch die Bewuchs-Werte) |

Menü: *Ansicht → Gras und Büsche*.

## Für Entwickler

| Datei | Inhalt |
|---|---|
| `src/Flora.*` | Kacheln bauen (Plätze aus Seed und Ort, Höhe aus dem Gelände), ausdünnen, zeichnen |
| `src/WorldGen.*` | `GenConfig::Biome::Flora`, `GenConfig::FloraView`: Werte aus `biomes.json` |
| `src/AppTrees.cpp` | `App::addFlora`: je Bild aktualisieren und zeichnen |
| `shaders/mesh.vert` | Shading 13: Wind und Absenken am Rand. Alpha der Eckpunktfarbe = Höhe der Spitze / 2 m (0: steht fest) |
| `shaders/mesh.frag` | `plant()`: Farbe je Eckpunkt, Büsche (Alpha 0) mit Blattmuster und unruhigen Normalen |
