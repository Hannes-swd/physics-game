# Bauvorlagen (Hologramme)

Statt jedes Brett selbst zu sägen und zu verbinden, setzt man im **Spiel-Modus** eine Vorlage als blaues Hologramm in die
Welt und füllt sie mit echtem Material. Jedes Bauteil der Vorlage (z. B. ein Brett einer Wand) wird ein echtes Teil, sobald
genug passendes Material drin ist.

## Steuerung (Spiel-Modus)

| Eingabe | Wirkung |
|---|---|
| **B** | Auswahl der Baupläne: ein Kasten je Vorlage mit Zeichnung und Materialbedarf, Klick wählt; dann hängt die Vorlage als Hologramm vor der Figur (ohne Hauptmenü: gleich die letzte) |
| Mausrad / **1 … 0** | Vorlage wechseln |
| **R** (Shift+R) | um 90° drehen |
| Linke Maustaste | Vorlage hier setzen (mehrere hintereinander möglich) |
| **G** | neue Vorlagen *fest* (schweben, zerbrechen nicht) oder mit *Physik* |
| **Alt** halten | ohne Einrasten |
| Rechte Maustaste / **B** | Bauplan-Modus beenden |
| **E** (mit einem Teil in der Hand, auf ein Hologramm schauen) | Teil einsetzen (sonst steckt E es ins [Inventar](21-Inventar.md)) |
| **Q** halten (0,8 s) auf ein fertiges Bauteil | abbauen: es wird wieder ein loses Teil, das Hologramm ist wieder leer |
| **Q** halten auf ein Hologramm | die ganze Vorlage entfernen (fertige Bauteile bleiben stehen) |

**Einrasten:** In der Nähe (4 m) einer anderen Vorlage rastet die neue in deren Raster ein: 1 m waagrecht, 2,5 m je
Geschoss, 90°. So kommt eine Wand genau auf den Rand eines Bodens, die nächste Wand daneben, ein dünner Boden oben auf die
Wände (zweites Stockwerk). Eingerastet ist die Vorschau türkis.

## Material einsetzen

- Das Hologramm zeigt, was es braucht: **Holz** (Fichte, Eiche, …), **Stein** (Feldstein, Granit, Ziegel, …), **Metall** (Stahl, Kupfer, …) oder beliebig.
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

Durchgeblättert in dieser Reihenfolge (`order` in der Datei):

| Gruppe | Vorlagen (Name in Befehlen) |
|---|---|
| Wände | Holzwand `wall`, Fensterwand `window`, Türwand `door`, Brüstung 1 m `halfwall`, Steinwand `stonewall`, Steinwand mit Fenster `stonewindow`, Mauer 1 m `lowwall`, Stahlgitter `grate` |
| Böden | Boden dünn `floor` (auch 2. Stock), Fundament bis zum Boden `foundation`, Steinplatten `stonefloor`, Stahlblech `steelfloor`, Brücke 4 m `bridge` |
| Treppen | Holztreppe `stairs` und Steintreppe `stonestairs` (je ein Geschoss), Rampe 1 m hoch `ramp` |
| Stützen | Pfosten `pillar`, Steinsäule `stonepillar`, Balken `beam` und Stahlträger `steelbeam` (oben zwischen zwei Stützen), Steintor `stonegate` |
| Zäune | Geländer `railing`, Zaun `fence` |
| Dach | Dach 30° `roof`, Giebel 4 m `gable` (zwei Dächer gegeneinander gedreht darauf) |
| Feuer | Steinofen mit Brennkammer und Kamin `oven`, Feuerstelle `fireplace`, Grill mit Stahlrost `grill`, Esse (Feuer in Arbeitshöhe) `forge`, Amboss `anvil`, Schornstein `chimney` |
| Möbel | Tisch `table`, Bank `bench`, Stuhl `chair`, Bett `bed`, Regal `shelf`, Kiste (offen) `chest`, Werkbank `workbench` |
| Draußen | Brunnen `well` |

Die Feuer-Vorlagen sind echte Stein- und Stahlteile: Wärme und Feuer laufen darin wie überall ([12](12-Waerme-und-Feuer.md)).
Holz in die Brennkammer des Ofens legen und anzünden (Bau-Modus H oder `ignite`): Der Kammerboden wird über 700 °C heiß,
Wände und Decke speichern die Wärme. Mit Holz wird es aber nicht viel heißer als etwa 750 °C, zum Schmelzen von Metall
reicht das nicht.

## Eigene Vorlagen

Etwas bauen (im Bau-Modus oder im Spiel), im **Bau-Modus** die Teile auswählen (Shift+Klick, Strg+A), dann in der Konsole:

```
plan save gartenhaus Mein Gartenhaus
```

Jedes ausgewählte Teil wird ein Bauteil in der Größe seines Hüllquaders (runde Formen werden Quader), Holzteile brauchen
Holz, Steinteile Stein, Metallteile Metall, alles andere beliebiges Material. Der Ursprung ist die Mitte unten aller Teile.
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
| `material` | `wood`, `stone`, `metal` oder `any` |
| `order` | Reihenfolge beim Durchblättern (kleiner = früher; eigene ohne Angabe: 1000, ans Ende) |

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
