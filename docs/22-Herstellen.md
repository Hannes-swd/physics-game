# Herstellen (Rezepte)

Herstellen funktioniert wie die [Bauvorlagen](20-Bauvorlagen.md), nur für kleine Dinge: Man wählt im **Spiel-Modus** ein
Rezept, legt es als **orangefarbenes Hologramm** hin und füllt es mit Material. Ist alles voll, wird aus allen Bauteilen
zusammen **ein einziges Teil**, das direkt ins [Inventar](21-Inventar.md) kommt.

## Steuerung (Spiel-Modus)

| Eingabe | Wirkung |
|---|---|
| **C** | Herstellbuch: ein Kasten je Rezept mit Zeichnung und was es braucht (und ob es im Inventar ist), Klick wählt; dann hängt das Rezept als Hologramm dort, wo man hinschaut (bis 4 m) |
| Mausrad / **1 … 0** | Rezept wechseln. Unten steht, was es braucht und ob es im Inventar ist |
| **R** (Shift+R) | um 90° drehen |
| Linke Maustaste | Rezept hierhin legen (danach ist man wieder im normalen Spiel) |
| Rechte Maustaste / **C** | Herstellen beenden |
| **E** mit leerer Hand, auf das Hologramm schauen | mit passendem Material **aus dem Inventar** füllen |
| **E** mit einem Teil in der Hand, auf das Hologramm schauen | dieses Teil einsetzen (wie bei den Bauvorlagen) |
| **Q** halten (0,8 s) auf das Hologramm | Rezept entfernen; schon eingesetztes Material kommt als Klotz je Bauteil ins Inventar zurück |

## Material

- Jedes Bauteil braucht **Holz**, **Stein**, **Metall** oder beliebiges Material, wie bei den Bauvorlagen. Grün = das
  getragene Teil passt, rot = passt nicht.
- Es zählt das **Volumen** (Anzeige in cm³). Zu große Stücke werden passend **abgesägt**; der Rest bleibt in der Hand bzw.
  geht ins Inventar zurück.
- Aus dem Inventar wird je Bauteil zuerst das kleinste Stück genommen, das allein reicht (wenig Abfall), sonst das größte.
  Fehlt etwas, sagt eine Meldung, wie viel wovon.
- Das fertige Teil ist aus **einem** Material: dem des Bauteils mit `"main": true`, sonst dem des größten Bauteils
  (eine Steinaxt mit großem Stiel ist also aus Holz, ein Steinmesser aus Stein). Daraus ergeben sich Gewicht, Farbe,
  Festigkeit und ob es brennt.
- Passt das fertige Teil nicht mehr ins Inventar (Gewicht, Platz), bleibt es dort liegen, wo das Hologramm war.

## Mitgelieferte Rezepte

| Gruppe | Rezepte (Name in Befehlen) | braucht etwa |
|---|---|---|
| Werkzeug | Steinaxt `axe`, Spitzhacke `pickaxe`, Steinhammer `hammer`, Steinmesser `knife`, Speer `spear`, Holzschaufel `shovel`, Fackel `torch` | ein Stock + ein Stein |
| Baumaterial | Brett 80 cm `plank`, Kantholz 1 m `squarebeam`, Mauerstein `brick`, Steinplatte `stoneslab` | mehrere Stöcke bzw. Steine |
| Dinge | Steinschale `bowl`, Holzkistchen `crate`, Hocker `stool` | |

Die Werkzeuge sind bisher **nur Gegenstände** (echte Teile mit Physik: tragen, werfen, anzünden, verbauen). Die Axt (X)
geht weiterhin auch ohne hergestellte Axt.

## Eigene Rezepte

Jedes Rezept ist eine JSON-Datei im **gleichen Format wie eine Bauvorlage** ([20](20-Bauvorlagen.md#dateien)):

- mitgeliefert: `assets/recipes/*.json`
- eigene: `%APPDATA%\Werkstatt\recipes\*.json` (gleicher Name wie ein mitgeliefertes: das eigene gilt)

```json
{
  "id": "axe",
  "label": "Steinaxt",
  "order": 10,
  "pieces": [
    { "from": [-0.3, 0, -0.0175], "to": [0.24, 0.035, 0.0175], "material": "wood" },
    { "from": [0.24, 0, -0.09], "to": [0.31, 0.05, 0.09], "material": "stone", "main": true }
  ]
}
```

Zusätzlich zu den Schlüsseln der Bauvorlagen:

| Schlüssel | Bedeutung |
|---|---|
| `main` (am Bauteil) | aus dem Material dieses Bauteils wird das fertige Teil (sonst: das größte Bauteil) |

Tipps: Gegenstände liegen flach (y = 0 ist der Boden), die Bauteile sollen sich **berühren, aber nicht überlappen**
(sonst zählt das Volumen doppelt und das Teil wird zu schwer). Der Name des fertigen Teils ist `label`.

## Befehle

```
craft                                     Rezepte, was sie brauchen, ob es im Inventar ist
craft fill <nr>                           Rezept-Hologramm aus dem Inventar füllen (Spiel-Modus)
plan place craft:<rezept> <x y z> [grad]  Rezept setzen (cm)
plan fill <nr> <bauteil|auto> <teil>      Teil einsetzen
plan info <nr> / plan remove <nr>         wie bei den Bauvorlagen
```

Gesetzte Rezepte werden wie Bauvorlagen mit der Welt gespeichert (Typ `craft:<id>`).

## Für Entwickler

| Datei | Inhalt |
|---|---|
| `src/Blueprints.*` | Rezepte lesen (`craftRecipes`, `planTemplateFor`), `World::fillPlan`: bei Rezepten werden Bauteile nur voll, am Ende ein Teil aus allen Quadern |
| `src/AppPlans.cpp` | C-Modus (dieselbe Bedienung wie B), orange Hologramme, Q halten gibt Material zurück |
| `src/AppCraft.cpp` | aus dem Inventar füllen, Ergebnis ins Inventar, Bedarf in der Statuszeile |
