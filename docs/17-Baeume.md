# Bäume: Wald, fällen, Holz gewinnen

In Welten mit Gelände wachsen Bäume, vor allem im **Wald**. Jeder Baum ist aus echtem Holz (Eiche oder Fichte). Man kann
ihn **fällen** (den Stamm durchsägen), dann liegt er mit seiner Krone am Boden. Danach sägt man ihn in Stücke und gewinnt
Bretter und Balken für den normalen Bau. Große Bäume liefern viel Holz (eine große Eiche wiegt gut 5 t), kleine sind schnell
gefällt und reichen für kleine Hütten.

Zum Ansehen: Menü *Datei → Baumschule* (`scene trees`) stellt jede Art in jeder Form nebeneinander auf, in jeder Welt.
Im Wald: `tp forest`, oder `tp tree oak_big` stellt einen neben die nächste große Eiche.

## Arten

| Art (`id`) | Name | Höhe | Stamm unten | Holz | Gewicht | Krone |
|---|---|---|---|---|---|---|
| `oak_big` | Große Eiche | 19 m | 110 cm | Eiche | ~5,5 t | Büschel an 6 Ästen |
| `oak_young` | Junge Eiche | 7 m | 22 cm | Eiche | ~80 kg | kleine Büschel |
| `spruce_big` | Große Fichte | 27,5 m | 80 cm | Fichte | ~2,2 t | Kegel |
| `spruce_mid` | Mittlere Fichte | 16 m | 42 cm | Fichte | ~360 kg | Kegel |
| `spruce_young` | Junge Fichte | 6 m | 15 cm | Fichte | ~20 kg | Kegel |

Jede Art hat **3 Formen** (gleiche Werte, anders gewachsen: Krümmung, Äste, Büschel) und wächst 75–125 % groß.
`trees` zeigt die Arten mit Höhe, Holzmenge und Gewicht.

## Ein Baum = ein Teil

- **Holz:** Stamm und Äste sind **ein festes Teil** aus konvexen Stücken (Stamm in Abschnitten, Wurzelanlauf unten,
  0,4 m Wurzel im Boden, je Ast zwei Stücke). Die Mantelflächen sind **Rinde** (graubraun, Längsrillen); jede
  Schnittfläche zeigt das helle Holz mit Jahresringen.
- **Krone:** nur Bild, ohne Gewicht und ohne Kollision (man läuft hindurch, sie bremst keinen fallenden Stamm). Sie hängt
  am Teil und bewegt sich mit ihm.
- **Fest durch die Wurzel:** Ein stehender Baum bewegt sich nicht und kostet in der Physik nichts.
- **Fällen:** Wird das Teil zersägt (oder durchgebohrt), bleibt nur das Stück mit dem **Stammfuß** fest stehen (der
  Stumpf). Alles andere ist frei und fällt, mit einem kleinen Anstoß in Richtung seiner Neigung (ein gerader Stamm stünde
  sonst auf dem Stumpf). Die Krone bleibt am Stück, an dem sie hängt (dem oberen). Ein abgesägter Ast fällt ebenso.
  Schlägt ein langer Stamm hart auf, kann er brechen (wie jedes Teil, siehe [08](08-Verbindungen-und-Bruch.md)).
- Danach ist alles normales Holz: sägen, bohren, hobeln, anzünden. Ein großer Stamm ist zum Tragen zu schwer: erst in
  Stücke sägen.

```
tree plant oak_big                 Eiche vor der Kamera pflanzen
cut selected y 140                 1 m über dem Boden durchsägen (gemessen vom Wurzelende, 40 cm im Boden)
mode play ; wait 5                 er fällt
```

Mit der Maus: Säge (S) auf den Stamm. Der Schnitt geht quer zur gewählten Achse; Y = waagrecht durch den Stamm.

**Mit der Axt** (Spiel-Modus): **X** nimmt die Axt, dann schlägt jeder **Linksklick** auf Holz eine Kerbe (ein Keil, 3–5 cm
tief je Hieb, leichtes Holz gibt mehr nach; der nächste Hieb trifft den Grund der Kerbe). Eine junge Fichte ist nach etwa
3 Hieben durch, eine junge Eiche nach einigen mehr, eine große Eiche braucht Ausdauer. Ist der Stamm durch, fällt der Baum
vom Hackenden weg. Per Befehl: `chop <id> <x y z> [dx dz]` (Stelle des Hiebs in cm, Richtung zum Hackenden). Die Axt hackt
nur Holz.

## Wo Bäume wachsen

Die Lage jedes Baums kommt **nur aus dem Seed**: gleicher Seed, gleicher Wald, auf jedem Rechner.

- Die Welt ist in Felder von `spacing` (6 m) geteilt. In jedem Feld steht **höchstens ein Baum**, irgendwo darin (nicht am
  Rand, so stehen zwei nie direkt aneinander).
- Ob dort einer steht, sagt die **Dichte des Bioms** (`trees.per_ha` in `biomes.json`, Bäume je Hektar). An Biomgrenzen
  wird sie weich überblendet: ein Wald dünnt zum Rand hin aus.
- Welche Art, sagt das Biom an dieser Stelle (`trees.types`, mit Gewichten); dazu Form, Größe und Drehung.
- Nie im Flussbett und nicht direkt am Wasser.

| Biom | Bäume je Hektar | Arten |
|---|---|---|
| `forest` (Wald) | 150 | alle: viele junge, einige große |
| `hills` (Hügellandschaft) | 18 | Fichten |
| `plains` (Ebene) | 4 | einzelne Eichen |
| `desert` (Wüste) | 0 | – |

Der **Wald** ist ein eigenes Biom: sehr feucht (`humidity` 0.62–1) und eher flach (`erosion` 0–0.5), dunkler Boden.
Die Ebene liegt jetzt bei `humidity` 0.35–0.62. Siehe [16](16-Weltgenerierung.md).

## Teile in der Nähe, Bild in der Ferne

So bleibt auch ein großer Wald schnell:

| Bereich | Was | Kosten |
|---|---|---|
| bis `solid_distance` (40 m) um jeden Spieler | **echte Teile** (fest, mit Physik): sägen, bohren, anzünden, anstoßen | wie andere feste Teile |
| bis `detail_distance` (150 m) | Bild in voller Form, mit Schatten | ein Zeichenaufruf je Form |
| bis `draw_distance` (450 m) | Bild grob (Stamm sechseckig, Krone aus wenigen Flächen) | ein Zeichenaufruf je Form |

Die Bilder sind Vorlagen: dasselbe Netz für alle Bäume einer Form, nur Lage, Drehung und Größe sind verschieden. Der
Renderer zeichnet alle gleichen Netze in einem Aufruf (Instanzen). Die Plätze werden je Kachel von 64 m einmal berechnet
und behalten, neue Kacheln nach und nach (die nächsten zuerst).

In der Nähe werden Bäume zu Teilen, sobald jemand näher als 40 m kommt (Spielfigur; im Bau-Modus der Punkt, auf den die
Kamera schaut), höchstens 6 je Bild. Entfernt man sich wieder (60 m), verschwinden **unveränderte** Teile wieder und sind
nur noch Bild.

## Speichern

Gespeichert wird nur, was sich geändert hat:

- Ein unveränderter Baum wird **nicht** gespeichert. Er entsteht beim Laden wieder aus dem Seed.
- Ein veränderter Baum (gesägt, gefällt, angebrannt, bewegt) bleibt ein normales Teil und wird mit der Welt gespeichert,
  mit Art, Form, Krone und Wurzel (`tree`-Zeile in der Szenendatei). Der Generator zeichnet an seiner Stelle keinen mehr.
- Ein **entfernter** Baum (`delete`, ganz verbrannt) wird als Nummer gemerkt (`tree_taken`) und wächst nicht nach.
- Kopien eines Baums (`copy`) sind gepflanzte Bäume, keine des Generators.
- In der Baumschule und mit `tree plant` gepflanzte Bäume sind normale Teile und werden immer gespeichert.

## Mehrspieler

Der Gastgeber schickt die Baumarten (seine `trees.json`) und die entfernten Bäume. Den Wald erzeugt jeder Rechner selbst
aus dem Seed. Die Teile in der Nähe der Spieler legt der Gastgeber an (für alle Spieler, auch die Mitspieler), sie kommen
wie alle Teile zu den Mitspielern; dort wird an ihrer Stelle kein Bild-Baum mehr gezeichnet. Protokollversion 4.

## `assets/trees.json`

Jede Welt bekommt beim ersten Öffnen eine Kopie (`trees.json` im Weltordner) und sieht dann immer gleich aus. `trees reload`
übernimmt eine geänderte `assets/trees.json` in die offene Welt.

```json
{
  "spacing": 6.0, "solid_distance": 40, "detail_distance": 150, "draw_distance": 450,
  "types": [
    {
      "id": "oak_big", "label": "Große Eiche", "wood": "oak",
      "trunk":    { "height": 15.0, "diameter": 1.1, "top_diameter": 0.3, "flare": 1.35, "bend": 0.35, "sections": 6 },
      "branches": { "count": 6, "from": 0.45, "to": 0.85, "length": [4.0, 6.5], "diameter": [0.22, 0.34], "rise": 32 },
      "crown":    { "shape": "round", "bottom": 7.0, "top": 19.0, "radius": 7.0,
                    "color": [0.15, 0.27, 0.07], "color2": [0.08, 0.17, 0.04] },
      "scale": [0.85, 1.15], "variants": 3
    }
  ]
}
```

| Schlüssel | Bedeutung |
|---|---|
| `spacing` | Feldgröße (m): höchstens ein Baum je Feld. Kleiner = dichter möglich |
| `solid_distance` | so nah um Spieler werden Bäume zu Teilen (m) |
| `detail_distance`, `draw_distance` | bis hier volle Form mit Schatten / bis hier grob (m) |
| `types[].id`, `label` | Name für Befehle und `biomes.json`, Anzeigename |
| `types[].wood` | Material von Stamm und Ästen (aus `materials.json`) |
| `trunk.height` | Stammlänge über dem Boden (m) |
| `trunk.diameter`, `top_diameter` | Durchmesser unten / an der Spitze (m) |
| `trunk.flare` | Wurzelanlauf: so viel dicker ist der Stamm am Boden (Faktor, über 0,9 m) |
| `trunk.bend` | Krümmung (m seitlich), `sections`: Stücke des Stamms |
| `branches.count` | Äste (0 bei Nadelbäumen: die Krone verdeckt sie) |
| `branches.from`, `to` | Höhe der Äste als Anteil des Stamms |
| `branches.length`, `diameter` | `[von, bis]` (m) |
| `branches.rise` | Winkel nach oben (Grad); das äußere Stück biegt sich weiter nach oben |
| `crown.shape` | `round` (Büschel an den Astspitzen, oben und innen) oder `cone` (Kegelstufen) |
| `crown.bottom`, `top`, `radius` | wo die Krone beginnt und endet, halbe Breite (m) |
| `crown.color`, `color2` | zwei Grüntöne (linear) |
| `scale` | Größe je Baum `[von, bis]` |
| `variants` | Formen je Art (1–8) |

In `biomes.json` je Biom: `"trees": { "per_ha": 150, "types": { "oak_big": 1, "spruce_young": 3 } }`.
Unbekannte Arten werden übersprungen.

## Befehle

| Befehl | Wirkung |
|---|---|
| `trees` | Arten (Höhe, Holz, Gewicht), Baum-Teile (wie viele vom Generator, wie viele verändert), Wald |
| `tree plant <art> [form] [x z] [größe]` | einen Baum als festes Teil aufstellen (ohne Ort: vor der Kamera), wird ausgewählt |
| `tree nearest [art]` | nächster Baum des Generators: Ort, Größe, Form |
| `tp tree [art]` | neben den nächsten Baum (Spielfigur; im Bau-Modus die Kamera) |
| `scene trees` | Baumschule: jede Art in jeder Form nebeneinander (auch *Datei → Baumschule*) |
| `trees reload` | `assets/trees.json` neu lesen und in die Welt übernehmen |
| `info <id>` | zeigt bei Bäumen die Zeile `tree`: Art, Form, Größe, Krone, Wurzel, vom Generator (unverändert/verändert) oder gepflanzt |

## Für Entwickler

| Datei | Aufgabe |
|---|---|
| `src/Trees.*` | `TreeConfig` (trees.json), `buildTreeModel` (Stamm, Äste, Krone aus den Werten), `TreeLibrary` (alle Formen mit Netzen), `Forest` (Plätze aus dem Seed, je Kachel von 64 m) |
| `src/World.cpp` | `plantTree`, `updateTrees` (Teile in der Nähe, unveränderte wieder weg), `replaceWithParts` (Stumpf bleibt fest, Rest fällt, Krone wandert mit), Speichern (`tree`, `tree_taken`), `treeScene` |
| `src/AppTrees.cpp` | Bild: Bäume des Generators gebündelt je Netz, Kronen der Baum-Teile |
| `src/Renderer.cpp` | gleiche Netze hintereinander in einem Aufruf (Instanzen, `gl_InstanceIndex`) |
| `shaders/mesh.frag` | Rinde (Flächen mit `Smooth::Bark`, Eckpunkt-Alpha 0) und Laub (Schattierung 12) |

- Formen hängen nur von der Art ab, nicht vom Welt-Seed (dieselben Bäume in jeder Welt und in der Baumschule).
- `Smooth::Bark`: Mantelflächen von Stamm und Ästen. Weiche Normalen wie ein Zylinder, aber kein exakter Zylinder für die
  Kollision. Bleibt beim Sägen an den Außenflächen erhalten; Schnittflächen sind eben und zeigen Holz.
- Selbsttests (Abschnitt *Trees*): Formen (groß/klein, immer gleich), Wald je Biom und Seed (Dichte, keine in Wüste und
  Fluss), Teile in der Nähe (fest, Krone, Wurzel), Fällen (Stumpf fest, Stamm mit Krone liegt), Wegräumen, Speichern und
  Laden, Baumschule.

## Ausblick

- Mehr Arten (Birke, Kiefer, Büsche) und Pflanzen am Boden.
- Fallkerb: Fallrichtung über einen Keilschnitt bestimmen.
- Laub fällt beim Fällen ab bzw. verbrennt mit.
