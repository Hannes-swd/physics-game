# Steine

Im Gelände liegen **Steine** in allen Größen: viele kleine Kiesel, einige faustgroße und kopfgroße, wenige große Brocken.
Wie viele und wie groß, sagt jedes Biom: in den **Hügeln** viele, auch große Felsbrocken; in der **Wüste** Sandstein; in
**Ebene** und **Wald** eher wenige.

## Anfassen, aufheben, Physik

Solange man einen Stein nicht anfasst, kostet er in der Physik **nichts**:

| Stein | vorher | wenn man ihn anfasst |
|---|---|---|
| **klein** (unter `big`, Standard 30 cm) | nur Bild: keine Physik, kein Teil | **E** oder **linke Maustaste** im Spiel-Modus: er wird zu einem echten Teil und liegt in der Hand. Werfen mit der rechten Maustaste |
| **groß** (ab 30 cm) | in der Nähe der Spielfigur (`solid_distance`, 25 m) ein **festes** Teil wie ein Baum: man läuft nicht hindurch, es wird aber nichts gerechnet (statisch). Weiter weg nur Bild | anfassen (E, linke Maustaste) oder bearbeiten (Säge, Bohrer) macht ihn **beweglich**. Bis etwa 40 kg kann man ihn tragen, schwerere bewegen sich kaum |

Ein angefasster Stein ist danach ein ganz normales Teil aus Feldstein oder Sandstein: sägen, bohren, bauen, werfen.

Wie bei den Bäumen:

- Gleicher Seed, gleiche Steine an denselben Stellen, auf jedem Rechner (auch im LAN).
- **Unveränderte** Steine werden nicht gespeichert: sie entstehen beim Laden wieder aus dem Seed. Ein großer Stein, den
  keiner angefasst hat, wird wieder nur Bild, sobald niemand mehr in der Nähe ist.
- **Veränderte** (angefasst, bewegt, bearbeitet) werden mit der Welt gespeichert, **gelöschte** merkt sich die Welt (sie
  liegen nicht wieder da).
- Gräbt man das Gelände um, rutschen die Steine dort mit der neuen Höhe mit (die, die gerade Teile sind, bleiben liegen).

## Einstellen (`biomes.json`)

Je Biom:

```json
"stones": { "per_ha": 450, "size": [0.05, 2.2], "skew": 2.5, "material": "fieldstone" }
```

| Schlüssel | Bedeutung |
|---|---|
| `per_ha` | Steine je Hektar (100 × 100 m), alle Größen zusammen |
| `size` | größte Ausdehnung [von, bis] in m |
| `skew` | wie stark die kleinen überwiegen: 1 = alle Größen gleich oft, 3 = die meisten klein, wenige groß |
| `material` | Material aus `materials.json`: `fieldstone` (Feldstein), `sandstone` (Sandstein), oder jedes andere |

Für alle Biome, oben in der Datei:

```json
"stones": { "distance": 250, "solid_distance": 25, "big": 0.3 }
```

| Schlüssel | Bedeutung |
|---|---|
| `distance` | bis wohin Steine gezeichnet werden (m). Kleine nur näher: ein Stein ist höchstens bis zum 350-Fachen seiner Größe zu sehen (5 cm: 17 m, 30 cm: 105 m) |
| `solid_distance` | bis wohin große Steine um jede Spielfigur feste Teile sind (m) |
| `big` | ab diesem Durchmesser (m) ist ein Stein groß |

| Biom | je Hektar | Größe | Material |
|---|---|---|---|
| `plains` (Ebene) | 120 | 4 cm – 0,9 m, fast alle klein | Feldstein |
| `forest` (Wald) | 150 | 5 cm – 1,4 m | Feldstein |
| `hills` (Hügel) | 450 | 5 cm – 2,2 m, viele große | Feldstein |
| `desert` (Wüste) | 250 | 4 cm – 1,6 m | Sandstein |

Welten von vor den Steinen bekommen die Werte der eingebauten `biomes.json`. **`terrain reload`** übernimmt Änderungen in
die offene Welt. Die Sichtweite in *Einstellungen → Grafik* gilt auch für Steine.

## Für Entwickler

| Datei | Inhalt |
|---|---|
| `src/Stones.*` | `StoneField`: 16 Formen (konvexe Hülle zufälliger Punkte auf einem Ellipsoid, 1 m), Plätze je Kachel von 16 m aus Seed und Ort, Strahl gegen die Steine (`raycast`) |
| `src/World.cpp` | `updateStones` (große in der Nähe als feste Teile, unveränderte weit weg wieder weg), `takeStone` (kleiner Stein → bewegliches Teil), `wakeStone` (fest → beweglich; auch beim Bearbeiten). Steine benutzen die Schlüssel der Bäume (`Object::treeKey`, Bit 62 gesetzt): Speichern, „entfernt“ und LAN funktionieren dadurch genauso |
| `src/AppInput.cpp` | Greifen (`tryGrab`): erst Steine, die nur Bild sind, dann Teile; feste Steine werden beim Anfassen beweglich |
| `src/AppTrees.cpp` | `App::addStones`: Steine als Vorlagen zeichnen (gleiche Form = ein Aufruf) |
