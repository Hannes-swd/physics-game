# Steine und Holz am Boden

Im Gelände liegt Zeug herum, je nach Biom verschieden:

| Art | Was | wo vor allem |
|---|---|---|
| **Steine** (`stones`) | Kiesel bis Felsbrocken, viele kleine, wenige große; Feldstein, in der Wüste Sandstein | Hügel, Wüste |
| **Stöcke** (`sticks`) | Äste und Stöcke, gerade oder leicht gebogen, manche mit Zweig; Fichte oder Eiche | Wald (sehr viele) |
| **Stämme** (`logs`) | umgefallene Baumstämme mit Rinde und Aststummeln, folgen dem Hang | Wald |
| **Stümpfe** (`stumps`) | Baumstümpfe mit Wurzelanlauf und heller Schnittfläche | Wald |

Große Stücke liegen nie in einem Baum. Nichts liegt im Flussbett oder unter Wasser.

## Anfassen, aufheben, Physik

Solange man ein Stück nicht anfasst, kostet es in der Physik **nichts**:

| Stück | vorher | wenn man es anfasst (E, linke Maustaste, Säge, Bohrer, Axt) |
|---|---|---|
| **klein**: Steine unter `big_stone` (30 cm), alle Stöcke | nur Bild: keine Physik, kein Teil | wird ein echtes Teil und liegt in der Hand. Werfen mit der rechten Maustaste |
| **groß**: Steine ab 30 cm, Stämme | in der Nähe der Spielfigur (`solid_distance`, 25 m) ein **festes** Teil wie ein Baum: man läuft nicht hindurch, es wird aber nichts gerechnet. Weiter weg nur Bild | wird **beweglich**. Bis etwa 40 kg kann man es tragen; schwerere bewegen sich kaum (einen Stamm sägt man in Stücke) |
| **Stumpf** | wie groß | bleibt **fest** (er wurzelt). Sägt man ihn durch, bleibt das untere Stück stehen, das obere ist frei |

Danach ist jedes Stück ein ganz normales Teil aus Feldstein, Sandstein, Fichte oder Eiche: sägen, bohren, hacken, anzünden
(Stöcke brennen gut), bauen.

Wie bei den Bäumen:

- Gleicher Seed, gleiches Zeug an denselben Stellen, auf jedem Rechner (auch im LAN).
- **Unveränderte** Stücke werden nicht gespeichert: sie entstehen beim Laden wieder aus dem Seed. Ein großes Stück, das
  keiner angefasst hat, wird wieder nur Bild, sobald niemand mehr in der Nähe ist.
- **Veränderte** (angefasst, bewegt, bearbeitet) werden mit der Welt gespeichert, **gelöschte** merkt sich die Welt.
- Gräbt man das Gelände um, rutscht das Zeug dort mit der neuen Höhe mit (was gerade ein Teil ist, bleibt liegen).

Hinschauen: `tp stone`, `tp stick`, `tp log`, `tp stump` stellt einen neben das nächste Stück dieser Art.

## Einstellen (`biomes.json`)

Je Biom, jede Art für sich (fehlt eine, gibt es sie dort nicht):

```json
"stones": { "per_ha": 450, "size": [0.05, 2.2], "skew": 2.5, "material": "fieldstone" },
"sticks": { "per_ha": 700, "length": [0.3, 1.6], "thickness": [0.015, 0.06], "skew": 2, "material": ["spruce", "spruce", "oak"] },
"logs":   { "per_ha": 14, "length": [2.5, 9], "thickness": [0.15, 0.5], "skew": 1.5, "material": ["spruce", "spruce", "oak"] },
"stumps": { "per_ha": 14, "height": [0.25, 0.7], "thickness": [0.25, 0.75], "material": ["spruce", "oak"] }
```

| Schlüssel | Bedeutung |
|---|---|
| `per_ha` | Stücke je Hektar (100 × 100 m) |
| `size` (Steine) | größte Ausdehnung [von, bis] in m |
| `length` (Stöcke, Stämme), `height` (Stümpfe) | Länge bzw. Höhe [von, bis] in m |
| `thickness` (Stöcke, Stämme, Stümpfe) | Durchmesser [von, bis] in m; lange Stücke werden eher dick |
| `skew` | wie stark die kleinen überwiegen: 1 = alle Größen gleich oft, 3 = die meisten klein |
| `material` | ein Material aus `materials.json` oder eine Liste (je Stück zufällig; doppelt = häufiger) |

Für alle Biome, oben in der Datei:

```json
"scatter": { "distance": 250, "stick_distance": 45, "solid_distance": 25, "big_stone": 0.3 }
```

| Schlüssel | Bedeutung |
|---|---|
| `distance` | bis wohin Steine, Stämme und Stümpfe gezeichnet werden (m). Kleine Steine nur näher: höchstens bis zum 350-Fachen ihrer Größe (5 cm: 17 m) |
| `stick_distance` | bis wohin Stöcke gezeichnet werden (m) |
| `solid_distance` | bis wohin große Stücke um jede Spielfigur feste Teile sind (m) |
| `big_stone` | ab diesem Durchmesser (m) ist ein Stein groß |

| Biom | Steine/ha | Stöcke/ha | Stämme/ha | Stümpfe/ha |
|---|---|---|---|---|
| `plains` (Ebene) | 120 (Feldstein, fast alle klein) | 40 (Eiche) | 1 | 2 |
| `forest` (Wald) | 150 | 700 (Fichte, Eiche) | 14 | 14 |
| `hills` (Hügel) | 450 (viele große) | 120 (Fichte) | 4 | 5 |
| `desert` (Wüste) | 250 (Sandstein) | – | – | – |

Welten von vor diesem Zeug bekommen die Werte der eingebauten `biomes.json`. **`terrain reload`** übernimmt Änderungen in
die offene Welt. Die Sichtweite in *Einstellungen → Grafik* gilt auch hierfür.

## Für Entwickler

| Datei | Inhalt |
|---|---|
| `src/Scatter.*` | `ScatterField`: Vorlagen je Art (Steine: konvexe Hülle zufälliger Punkte auf einem Ellipsoid; Holz: Kegelstümpfe mit Rinde, `barkFrustum` aus `Trees.h`), Plätze je Kachel von 16 m aus Seed und Ort, Abstand zu den Bäumen, Strahl gegen die Stücke (`raycast`) |
| `src/World.cpp` | `updateScatter` (große in der Nähe als feste Teile, unveränderte weit weg wieder weg), `takeScatter` (Aufheben), `wakeScatter` (fest → beweglich; Stümpfe nicht), Bearbeiten weckt (`replaceWithParts`: beim Stumpf bleibt das unterste Stück fest). Die Stücke benutzen die Schlüssel der Bäume (`Object::treeKey`, Bit 62 gesetzt, Art in Bit 56..59): Speichern, „entfernt“ und LAN funktionieren dadurch genauso |
| `src/AppInput.cpp` | Greifen (`tryGrab`): erst Stücke, die nur Bild sind, dann Teile; feste werden beim Anfassen beweglich |
| `src/AppTrees.cpp` | `App::addScatter`: als Vorlagen zeichnen (gleiche Form = ein Aufruf) |
