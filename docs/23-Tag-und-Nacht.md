# Tag und Nacht

Jede Welt hat eine Uhrzeit. Ein ganzer Tag dauert **20 Minuten**. Die Sonne geht im Osten auf, steht mittags im Süden
und geht im Westen unter. Nachts zieht der Mond über den Himmel, und man sieht die echten **Sternbilder** und die
Milchstraße. Der Himmel dreht sich um den Polarstern.

Die Nacht ist dunkel, aber nicht schwarz, so wie früher, als es noch keine Lampen gab: Das Auge gewöhnt sich an die
Dunkelheit, der fast volle Mond wirft Schatten, und ein Feuer leuchtet nachts viel weiter.

## Ablauf eines Tags

Die Welt liegt auf 50° nördlicher Breite (wie Mitteleuropa), das Datum steht fest auf Ende April:

| Uhrzeit | Was passiert |
|---|---|
| ab 3:45 | Morgendämmerung, die Sterne verblassen |
| 5:00 | Sonnenaufgang im Osten (Morgenrot) |
| 12:00 | Mittag: Sonne im Süden, 52° hoch |
| 19:00 | Sonnenuntergang im Westen (Abendrot) |
| ab 19:40 | Mondaufgang im Osten, die Sterne kommen heraus |
| um 1:40 | Mond im Süden, etwa 40° hoch |

In 20 Minuten ist es also gut 11 Minuten hell und gut 8 Minuten dunkel.

## Sonne, Mond, Sterne

- **Sonne:** Je tiefer sie steht, desto mehr Luft liegt dazwischen. Dann wird ihr Licht gelb, orange und rot, und die
  Schatten werden lang. Das Abend- bzw. Morgenrot leuchtet um die Sonne am Horizont.
- **Mond:** fast voll, etwas größer gezeichnet als in echt, mit dunklen Meeren. Die Phase ergibt sich aus dem Stand der
  Sonne. Nachts gibt er das Licht und wirft Schatten (bläulich, schwächer als die Sonne). Er geht etwa 40 Minuten nach
  der Sonne auf und steht morgens nach Sonnenaufgang noch blass am Himmel.
- **Sterne:** Die etwa 170 hellsten Sterne der bekannten Sternbilder stehen an ihrem echten Ort und haben ihre echte
  Helligkeit und Farbe: Großer und Kleiner Wagen mit dem Polarstern, Kassiopeia, Orion, Stier mit den Plejaden,
  Zwillinge, Fuhrmann, Löwe, Bärenhüter, Jungfrau, Leier, Schwan, Adler, Skorpion, Schütze, Pegasus, Perseus, Drache,
  Herkules und weitere. Dazu kommen einige tausend schwache Sterne und die Milchstraße. Alle Sterne sind scharfe
  Punkte (je ein bis zwei Pixel, unabhängig von der Auflösung). Tief am Horizont werden sie schwächer und funkeln ein
  wenig.
- **Sternbildlinien:** `sky lines on` verbindet die Sterne jedes Sternbilds mit dünnen Linien (Voreinstellung: aus).

Was man wann sieht: Abends gehen im Westen Orion, Stier, Zwillinge und Fuhrmann unter. Um Mitternacht steht der Große
Wagen fast im Zenit, im Süden stehen Löwe, Jungfrau und Bärenhüter mit Arcturus. Gegen Morgen gehen im Osten Leier,
Schwan und Adler auf (das Sommerdreieck) und tief im Süden der Skorpion mit Antares.

## Bau-Modus

Die perspektivische Ansicht des Bau-Modus zeigt dieselbe Tageszeit. Die technischen Ansichten (Oben, Vorne, Seite)
bleiben immer im festen Tageslicht ohne Schatten, damit man nachts weiter genau bauen kann.

## Befehle

| Befehl | Wirkung |
|---|---|
| `time` | Uhrzeit, Länge eines Tags, Stand von Sonne und Mond |
| `time <hh:mm>` (auch `time set 6:30`, `time 18.5`) | Uhrzeit setzen |
| `time dawn` / `sunrise` / `morning` / `day` / `noon` / `afternoon` / `sunset` / `dusk` / `night` / `midnight` | zu dieser Tageszeit springen (Dämmerung, Sonnenaufgang, 8:00, 10:00, 12:00, 15:00, Sonnenuntergang, Abenddämmerung, 22:30, 0:00) |
| `time add <Stunden>` | vor- oder zurückdrehen (auch negativ) |
| `time stop` / `time run` | Uhr anhalten / weiterlaufen lassen |
| `time length <Minuten>` | Länge eines Tags (Voreinstellung 20; 1440 = Echtzeit) |
| `sky` | wie `time` |
| `sky lines [on\|off]` | Sternbildlinien ein- oder ausschalten (ohne Wert: umschalten) |

In der Befehlssprache liefert `$(daytime)` die Uhrzeit in Stunden (z. B. `wait until $(daytime) > 19`).
`status` zeigt die Uhrzeit ebenfalls.

## Speichern und Mehrspieler

Die Uhrzeit, die Länge eines Tags, ob die Uhr läuft und die Sternbildlinien werden mit der Welt gespeichert (`world.json`,
Abschnitt `environment`). Eine neue Welt beginnt um 8:00. Im Pausenfenster steht die Uhr still, wenn man allein spielt.

Im LAN gibt der Gastgeber die Uhrzeit vor. Mitspieler bekommen sie mit jedem Weltzustand und lassen sie dazwischen
selbst weiterlaufen. Nur der Gastgeber kann sie mit `time` ändern. Die Sternbildlinien schaltet jeder für sich.

## Technik

- `src/Sky.cpp` rechnet aus der Uhrzeit die Richtung von Sonne und Mond (Stundenwinkel, Deklination, Breite), die
  Farben des Himmels, des Lichts und des Umgebungslichts (Stützstellen nach der Sonnenhöhe), die Belichtung (nachts bis
  2,6-fach) und die Drehung des Sternenhimmels (Sternzeit). Dort steht auch der Sternkatalog (Rektaszension,
  Deklination, Größenklasse, Farbe) mit den Sternbildlinien.
- Das Licht für Körper, Wasser und Schatten ist am Tag die Sonne und nachts der Mond. Umgeschaltet wird kurz unter dem
  Horizont, wo beide fast dunkel sind.
- `shaders/common.glsl` zeichnet Himmelsverlauf, Abendrot, Sonne und Mondscheibe (auch in den Spiegelungen auf dem
  Wasser). `shaders/sky.frag` zeichnet Sterne, Milchstraße und Linien. Die schwachen Sterne kommen aus einem Würfelgitter
  (höchstens ein Stern je Zelle, in der Milchstraße dichter, je Größenklasse dreimal so viele), die hellen aus dem
  Katalog.
- Der Nebel hat die Farbe des Himmels am Horizont, nachts ist er also dunkel. Er ist dünn (am Boden nach 2 km etwa ein
  Fünftel) und nimmt mit der Höhe ab (Skalenhöhe 700 m, `fogAmount` in `common.glsl`): vom Berg aus sieht man klar weit.
- **Wolken** (`cloudLayer` in `sky.frag`): Haufenwolken auf einer Schicht in 2 km Höhe, fest über der Welt, treiben
  langsam. Mal mehr, mal weniger Wolken je Gegend. Von Himmel und Sonne beleuchtet, unten grauer, zur Sonne mit hellem
  Rand, abends und morgens rötlich. Nachts sind sie weg, der Sternenhimmel bleibt frei.
- Tagsüber ist der Himmel tiefblau, mit höherer Sonne noch tiefer (Stützstelle bei 35°), der Horizont nur etwas heller.
