# Bauvorlagen (Hologramme)

Statt jedes Brett selbst zu sägen und zu verbinden, setzt man im **Spiel-Modus** eine Vorlage als blaues Hologramm in die
Welt und füllt sie mit echtem Material. Jedes Bauteil der Vorlage (z. B. ein Brett einer Wand) wird ein echtes Teil, sobald
genug passendes Material drin ist.

## Steuerung (Spiel-Modus)

| Eingabe | Wirkung |
|---|---|
| **B** | Bauplan-Modus an/aus. Die gewählte Vorlage hängt als Hologramm vor der Figur |
| Mausrad / **1 … 0** | Vorlage wechseln |
| **R** (Shift+R) | um 90° drehen |
| Linke Maustaste | Vorlage hier setzen (mehrere hintereinander möglich) |
| **G** | neue Vorlagen *fest* (schweben, zerbrechen nicht) oder mit *Physik* |
| **Alt** halten | ohne Einrasten |
| Rechte Maustaste / **B** | Bauplan-Modus beenden |
| **E** (mit einem Teil in der Hand, auf ein Hologramm schauen) | Teil einsetzen |
| **Q** halten (0,8 s) auf ein fertiges Bauteil | abbauen: es wird wieder ein loses Teil, das Hologramm ist wieder leer |
| **Q** halten auf ein Hologramm | die ganze Vorlage entfernen (fertige Bauteile bleiben stehen) |

**Einrasten:** In der Nähe (4 m) einer anderen Vorlage rastet die neue in deren Raster ein: 1 m waagrecht, 2,5 m je
Geschoss, 90°. So kommt eine Wand genau auf den Rand eines Bodens, die nächste Wand daneben, ein dünner Boden oben auf die
Wände (zweites Stockwerk). Eingerastet ist die Vorschau türkis.

## Material einsetzen

- Das Hologramm zeigt, was es braucht: **Holz** (Fichte, Eiche, …), **Stein** (Feldstein, Granit, Ziegel, …) oder beliebig.
  Grün = das getragene Teil passt, rot = passt nicht.
- Es zählt das **Volumen**: Kleinere Stücke (Stöcke, Steine) füllen das Bauteil nach und nach, man sieht es von unten
  volllaufen. Ein zu großes Stück (Stamm, Balken) wird beim Einsetzen passend **abgesägt**, der Rest bleibt in der Hand.
- Ein Bauteil bleibt bei der Holz- bzw. Steinart, mit der man angefangen hat; fertig ist es aus genau diesem Material.
- Schwere Stücke (über ca. 43 kg) kann man nicht heben, aber schleifen: nah heranziehen, aufs Hologramm schauen, **E**.

## Fest oder Physik

| | fest (Standard) | Physik |
|---|---|---|
| fertige Bauteile | schweben, wo sie sind, ohne Schwerkraft, zerbrechen nie | stehen fest, bis die **ganze** Vorlage fertig ist |
| danach | bleiben so | werden lose und dort **verleimt**, wo sie sich berühren (Holz: Holzleim, Stein: Montagekleber), echte Physik und Bruch |

## Mitgelieferte Vorlagen

| Name | Vorlage | Bauteile |
|---|---|---|
| `wall` | Holzwand 2 × 2,5 m | 4 Bretter 50 × 250 × 10 cm |
| `window` | Fensterwand, Öffnung 1 × 1 m | 4 Holzteile |
| `door` | Türwand, Öffnung 1 × 2,1 m | 3 Holzteile |
| `stonewall` | Steinwand 2 × 2,5 m, 20 cm dick | 22 Steine im Verband |
| `floor` | Boden (dünn) 2 × 2 m, 10 cm, Oberseite auf der Höhe des Zielpunkts; auch fürs zweite Stockwerk | 4 Bretter |
| `foundation` | Fundament 2 × 2 m, reicht bis in den Boden (am Hang unterschiedlich tief) | 4 Steinblöcke |
| `stairs` | Treppe, ein Geschoss (2,5 m) auf 3 m, 1 m breit | 2 Wangen, 10 Stufen |
| `pillar` | Pfosten 20 × 20 cm, 2,5 m | 1 |
| `railing` | Geländer 2 m, 1 m hoch | 3 Pfosten, Handlauf |
| `roof` | Dach 2 × 2 m, 30° | 4 Bretter |

## Eigene Vorlagen

Etwas bauen (im Bau-Modus oder im Spiel), im **Bau-Modus** die Teile auswählen (Shift+Klick, Strg+A), dann in der Konsole:

```
plan save gartenhaus Mein Gartenhaus
```

Jedes ausgewählte Teil wird ein Bauteil in der Größe seines Hüllquaders (runde Formen werden Quader), Holzteile brauchen
Holz, Steinteile Stein, alles andere beliebiges Material. Der Ursprung ist die Mitte unten aller Teile.
`plan delete gartenhaus` löscht sie wieder.

## Dateien

Jede Vorlage ist eine eigene JSON-Datei:

- mitgeliefert: `assets/blueprints/*.json`
- eigene: `%APPDATA%\Werkstatt\blueprints\*.json` (gleicher Name wie eine mitgelieferte: die eigene gilt)

Gelesen werden sie beim ersten Gebrauch (nach Änderungen das Spiel neu starten).

```json
{
  "id": "wall",
  "label": "Holzwand",
  "to_ground": false,
  "pieces": [
    { "from": [-1, 0, -0.05], "to": [-0.5, 2.5, 0.05], "material": "wood" },
    { "center": [0.55, 1.36, 0], "size": [0.1, 0.28, 3.9], "pitch": 39.8, "rotation": [1, 0, 0, 0], "material": "stone" }
  ]
}
```

| Schlüssel | Bedeutung |
|---|---|
| `id` | Name in Befehlen (ein Wort) |
| `label` | Anzeige im Spiel |
| `to_ground` | `true`: alle Bauteile reichen von ihrer Oberseite bis in den Boden (Fundament) |
| `from` / `to` | gegenüberliegende Ecken des Quaders (m), **oder** |
| `center` / `size` | Mitte und Kantenlängen (m) |
| `pitch` | gekippt um die Querachse (°), positiv: steigt nach −z (vom Setzenden weg) |
| `rotation` | weitere Drehung als Quaternion `[w, x, y, z]` (eigene Vorlagen) |
| `material` | `wood`, `stone` oder `any` |

Koordinaten: x nach rechts, y nach oben, −z vom Setzenden weg; Ursprung = Zielpunkt beim Setzen. Die mitgelieferten
benutzen ein Raster von 2 m Breite und 2,5 m Geschosshöhe (Wände mittig auf ihrer Linie, Böden mit der Oberseite auf 0).

## Befehle

```
plan                                      Vorlagen und gesetzte Hologramme
plan place <vorlage> <x y z> [grad] [fixed|physics]     setzen (cm)
plan info <nr>                            Bauteile, Füllstand
plan fill <nr> <bauteil|auto> <teil>      Teil einsetzen
plan dismantle <teil>                     abbauen
plan remove <nr>                          Hologramm entfernen
plan save <name> [anzeige]  /  plan delete <name>
```

Gesetzte Vorlagen und ihr Füllstand werden mit der Welt gespeichert (`world.bin` ab Version 2) und gehören zu
Rückgängig/Wiederholen. Noch nicht: im LAN sehen Mitspieler die Hologramme nicht; in `.werkstatt`-Szenendateien
(Szene speichern unter) werden sie nicht mitgespeichert.

## Für Entwickler

| Datei | Inhalt |
|---|---|
| `src/Blueprints.*` | Vorlagen aus den Dateien lesen/schreiben, `World::makePlan/addPlan/fillPlan/dismantle/pickPlan` |
| `src/AppPlans.cpp` | Bauplan-Modus, Einrasten, Hologramme zeichnen (durchsichtige Dreiecke), Q halten |
| `src/AppInput.cpp` | E: Einsetzen statt Ablegen (`handActions`) |
| `src/WorldBin.cpp` | Speichern (Version 2) |
