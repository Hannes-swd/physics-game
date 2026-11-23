# Tiere

In Welten mit Gelände leben Tiere: Rehe, Hirsche, Wölfe, Wildpferde, Gämsen, Eichhörnchen, Vögel und mehr – je Biom
andere. Sie sind nur Bild und Verhalten (keine Physik) und werden nicht gespeichert.

## Erscheinen und Verschwinden

- Tiere erscheinen **immer in der Nähe des Spielers** (im Bau-Modus um die Kamera), mit den Arten des Bioms, in dem
  die Stelle liegt. Im Blickfeld tauchen sie erst in der Ferne auf, nah nur hinter dem Spieler – man sieht sie also
  nicht plötzlich vor der Nase erscheinen.
- Entfernt man sich, **verschwinden sie ganz**, sobald sie weiter weg sind, als man sie noch sehen würde. Das hängt
  von der Art ab (`visible` in `animals.json`, mal Sichtweite aus den Einstellungen):

| Art | bleibt bis |
|---|---|
| Frosch, Spinne | 25 m |
| Eichhörnchen | 45 m |
| Fledermaus | 55 m |
| Waschbär, Taube | 80 m |
| Fuchs, Wildschwein | 130–150 m |
| Reh, Wolf | 220–230 m |
| Rothirsch, Wildpferd, Wisent, Mufflon, Lama | 300–380 m |
| Krähen, Möwen | 320–380 m |
| **Gämse** (am Berg weit zu sehen) | 750 m |
| **Adler, Geier** (am Himmel) | 950 m |

- Wie viele es gibt, steht je Biom als „Gruppen in Sichtweite“ (z. B. Wald: 1,2 Reh-Gruppen, 0,25 Wolfsrudel).
  Bruchteile bedeuten: in manchen Gegenden eine Gruppe, in anderen keine.
- Tagtiere (Vögel, Tauben) erscheinen nur am Tag, Fledermäuse nur nachts. Wird es Nacht, verschwinden die Tagtiere,
  sobald man nicht hinsieht.

## Verhalten

| Verhalten | Tiere | was sie tun |
|---|---|---|
| scheu (`shy`) | Reh, Rothirsch, Gämse, Fuchs, Mufflon, Wildschwein, Przewalski-Pferd, Wildesel, Taube | bleiben stehen und schauen her (`alert`), fliehen in vollem Lauf, wenn man näher kommt (`flee`); Gämsen schon auf 60 m |
| ruhig (`calm`) | Wildpferd, Wisent, Lama | grasen weiter und weichen erst dicht vor einem im Trab aus |
| neugierig (`curious`) | Wolf, Polarwolf | halten 16–32 m Abstand, umkreisen den Spieler, fliehen nur ganz nah |
| klein (`small`) | Eichhörnchen, Waschbär, Schlange, Frosch, Spinne, Krabbe | flitzen kurz weg, wenn man fast drauftritt |

- **Einzelgänger und Herden:** Ein Rothirsch oder Fuchs ist allein, Rehe zu ein bis drei, Wölfe im Rudel (3–6),
  Wildpferde, Wisente und Rentiere in Herden. Eine Herde zieht gemeinsam umher (die Tiere folgen dem ersten), und
  erschrickt eines, flieht die ganze Gruppe.
- **Wer rennt, erschreckt weiter** (1,5-fache Fluchtdistanz), wer still steht, kommt etwas näher heran (0,75-fach).
- **Schleichen** (linke Strg halten): Tiere bemerken einen erst auf **halbe Entfernung**, beim Hinschauen, Fliehen und
  auch beim Jagen (ein Wolfsrudel greift erst auf 7 m an statt 14 m). Dafür ist man langsam (1,8 m/s).
- Tiere laufen nicht ins Wasser (außer Frösche und Krabben ins flache) und nicht zu steile Hänge hinauf; Gämsen und
  Mufflons klettern auch im Steilen und erscheinen bevorzugt an Hängen – so sieht man sie von unten am Berg.
- Stehend wechseln sie zwischen Abläufen (stehen, fressen, Kopf senken). Beim Laufen folgen die Schritte der
  Geschwindigkeit, bergauf und bergab neigt sich der Körper mit dem Hang.
- **Tauben** fliegen auf, wenn man ihnen zu nah kommt, und landen ein Stück weiter wieder.
- **Vögel:** Adler, Geier und Möwen kreisen und gleiten mit seltenen Flügelschlägen und legen sich in die Kurve;
  Krähen, Stare und Papageien ziehen im Schwarm umher; Fledermäuse flattern im Zickzack.

## Angriffe

Manche Tiere greifen die eigene Spielfigur an (Verletzungen siehe [25](25-Leben.md)). Dabei spielen sie ihren
Angriffs-Ablauf aus dem Modell (Biss, Kopfstoß, Zuschnappen).

| Tier | Art (`attack.mode`) | was es tut |
|---|---|---|
| Wolf, Polarwolf | jagt (`hunt`) | das Rudel kommt ab 14 m (Polarwolf 16 m) heran, **nachts schon aus doppelter Entfernung**, beißt in Beine und Arme, weicht nach jedem Biss ein paar Sprünge zurück und kommt wieder. Nach 25 s (30 s) gibt es auf und lässt einen 90 s in Ruhe |
| Wildschwein | verteidigt (`defend`) | wer näher als 5 m kommt, wird umgestoßen (Prellung), danach zieht es sich zurück |
| Wisent | verteidigt | ab 6 m: Kopfstoß in den Rumpf, kann Rippen brechen |
| Schlangen | verteidigt | beißen in Fuß oder Schienbein, wer fast drauftritt (Sandotter fester) |
| Spinnen, Krabben | verteidigt | stechen bzw. zwicken in den Fuß |

- **Wehren:** ein Schlag mit der linken Maustaste vor sich (mit Werkzeug in der Hand weiter) vertreibt die Tiere dort;
  sie fliehen und lassen einen 45 s in Ruhe.
- Ein erneuter Biss in eine schon verletzte Stelle kann sie verschlimmern: ein langer Kampf mit einem Rudel wird
  gefährlich.
- Tot oder mit `health off` greifen sie nicht an. Im LAN greifen sie auch Mitspieler an (der Gastgeber rechnet es).

## Jagd

Tiere lassen sich mit Schlägen (linke Maustaste auf das Tier) verletzen und erlegen:

| in der Hand | Schaden je Schlag | Reichweite |
|---|---|---|
| nichts | gering (ein Fuchs braucht viele Schläge) | 1,9 m |
| Axt (2), Spitzhacke (7) | kräftig | 2,4 m |
| Laser (3) | sehr kräftig | 3 m |

- **Trefferbox:** Getroffen wird, was man wirklich anvisiert. Jede Art hat eine Box aus den Maßen ihres Modells
  (stehend; tot die liegende Haltung am Ende des Sterbe-Ablaufs), die sich mit dem Tier dreht, neigt und kippt: ein
  Pferd ist lang und schmal, ein Fuchs klein. `animals info <id>` zeigt sie.
- **Treffer sieht man:** das Tier zuckt zur Seite, von der der Schlag kam (Treffer-Ablauf des Modells), am Fadenkreuz
  blitzen vier kurze Striche auf (rot, wenn es dabei stirbt).
- Mit der bloßen **Hand** geht es auch, nur schwach: ein Fuchs verliert etwa 13 % je Schlag, ein Reh 8 %.
- Wie viel ein Tier aushält, hängt von seiner Größe ab (`toughness`, ohne Angabe 0,35 + Höhe): ein Reh braucht zwei,
  drei Axthiebe, ein Wisent viele.
- Getroffene Tiere fliehen; Wehrhafte (Wolf, Wildschwein, Wisent) greifen erst recht an, bis sie schwer verwundet sind.
- Unter halber Kraft **humpeln** sie und können nicht mehr rennen, man holt sie also ein. Wunden heilen in etwa 4 min.
- Erlegt spielt das Tier seinen Sterbe-Ablauf und bleibt liegen (Tiere ohne Ablauf kippen auf die Seite), 10 min lang.
- **F** auf ein erlegtes Tier **zerlegt** es: **Fleisch** (Stücke zu etwa 1,5 kg, zum Essen mit **V**, [25](25-Leben.md#hunger)) und das **Fell** kommen ins Inventar (Größe je Art, `hide` in m²: Reh 0,8,
  Wolf 0,9, Wisent 4; ein Reh-Fell wiegt etwa 3,3 kg). Kleine Tiere (Schlange, Spinne, Frosch, Krabbe) haben keins.
- Aus Fell wird ein **Verband** ([22](22-Herstellen.md), [25](25-Leben.md)).
- Vögel am Himmel lassen sich nicht treffen. Mitspieler können jagen, aber noch nicht zerlegen (kein Inventar).
- **Teile treffen Tiere:** Ein fallender Baum, ein geworfener Stein oder ein herabstürzender Block verletzt jedes Tier
  am Boden, dessen Trefferbox er durchfliegt (ab 2 m/s). Der Schaden folgt der Wucht ½·m·v²: ab etwa 20 J, 400 J mehr
  entsprechen einer ganzen Lebenskraft (Zähigkeit 1). Ein 1-kg-Stein mit 15 m/s nimmt etwa 20 %, ein kräftiger Wurf
  mit 30 m/s gut die Hälfte, ein fallender Stamm erschlägt. Danach gilt dasselbe wie beim Schlag (zucken, fliehen oder
  angreifen, sterben); erschlagene Tiere melden sich oben („Reh von Fichte erschlagen“). Dasselbe Tier wird höchstens
  alle 0,5 s getroffen. Das Tempo kommt aus der Bewegung des Teils und der Physik (versetzte Teile zählen nicht).
- `animals spawn` nennt den Ort des ersten Tiers (cm), etwa um etwas darauf fallen zu lassen.

## Arten je Biom

| Biom | Tiere |
|---|---|
| Ebene | Reh, Rothirsch, Fuchs, Wildpferd, Wisent, Tauben, Krähen, Stare |
| Blumenwiese | Reh, Fuchs, Tauben, Stare, Krähen |
| Wald | Reh, Rothirsch, Fuchs, Wildschwein, Wolf, Waschbär, Eichhörnchen, Fledermaus, Krähen |
| Sumpf | Frosch, Schlange, Wildschwein, Waschbär, Fledermaus |
| Dschungel | Schlange, Frosch, Vogelspinne, Papageien, Fledermaus |
| Hügellandschaft | Reh, Rothirsch, Mufflon, Fuchs, Wildpferd, Steinadler |
| Kirschblütenhain | Reh, Fuchs, Eichhörnchen, Tauben, Stare |
| Hochplateau | Lama, Wildesel, Mufflon, Steinadler |
| Wüste | Sandotter, Walzenspinne, Wildesel, Geier |
| Canyon | Sandotter, Walzenspinne, Mufflon, Geier, Steinadler, Fledermaus |
| Steppe | Przewalski-Pferd, Wisent, Wolf, Fuchs, Steinadler |
| Felsenmeer | Gämse, Mufflon, Sandotter, Steinadler |
| Tundra | Rentier, Polarfuchs, Polarwolf |
| Schneewald | Rothirsch, Rentier, Wolf, Polarfuchs, Eichhörnchen |
| Gletscher | Polarfuchs (selten) |
| Sandstrand, Felsküste, Inselsee | Krabben, Möwen (Felsküste auch Gämsen) |
| Gebirge | **Gämse**, Steinadler, Mufflon, Lama |

## Modelle

Die Tiere sind Low-Poly-Modelle von **Quaternius**, Lizenz **CC0** (gemeinfrei: frei nutzbar, auch in verkauften
Spielen, ohne Namensnennung), siehe `assets/animals/LICENSE.md`. Jedes hat ein Skelett und Abläufe (stehen, fressen,
gehen, galoppieren …), die das Spiel weich überblendet. Ein Modell dient oft mehreren Tieren: die Gämse ist das
umgefärbte Reh, das Rentier der umgefärbte Hirsch, das Eichhörnchen die umgefärbte Ratte (`tint`).
Die Vögel am Himmel sind eine eingebaute Low-Poly-Form mit schlagenden Flügeln (`"model": "bird"`).

Eigene Tiere: eine glTF-Binärdatei (.glb, Skelett mit Abläufen, Farben als Materialien, keine Texturen) nach
`assets/animals` legen und in `animals.json` eintragen. Modelle, die sich über eine Farbtafel (Textur) färben, wandelt
`python tools/bake_atlas.py ein.glb aus.glb` in Materialfarben um.

## `animals.json`

Alles steht in `assets/animals.json` (die Schlüssel sind dort unter `_keys` erklärt). Die wichtigsten:

| Schlüssel | Bedeutung |
|---|---|
| `model`, `height`, `tint` | Datei, ganze Höhe (m), Farben einzelner Materialien ersetzen |
| `clips` | Abläufe: `idle` (Liste, zufällig), `walk`, `run`, `fly` |
| `speed` | [gehen, rennen] m/s |
| `group`, `spread`, `wander` | Gruppengröße [von, bis], Abstand in der Gruppe, wie weit sie umherzieht (m) |
| `behavior`, `flee`, `alert`, `keep` | Verhalten (siehe oben) und seine Abstände (m) |
| `visible` | bis wohin es bleibt (m) |
| `active` | `always`, `day`, `night` |
| `motion` | `ground`, `soar`, `flock`, `flutter` |
| `slope`, `move_slope`, `altitude` | wo es erscheint (Steilheit, Höhe) und wie steil es laufen darf |
| `hide`, `toughness`, `clips.death` | Fell beim Zerlegen (m²), wie viel es aushält, Sterbe-Ablauf (ohne Angabe: „Death“) |
| `attack` | Angriff: `mode` (`hunt`, `defend`), `range`, `reach` (m), `cooldown` (s), `power` (Schwere 1..3), `chase` (s, Jäger geben auf), `kind` (`bite`, `gore`, `sting`, `pinch`), `clip` (Angriffs-Ablauf) |
| `biomes` | je Biom: Art → Gruppen in Sichtweite |

Nach einer Änderung übernimmt `animals reload` die Datei sofort.

## Befehle

| Befehl | Wirkung |
|---|---|
| `animals` | wie viele Tiere welcher Art gerade da sind, das nächste in m |
| `animals list` | alle Arten mit Modell und Sichtweite |
| `animals info <id>` | Modell, Dreiecke, Abläufe, Schrittweite |
| `animals spawn <id> [Anzahl] [Abstand m]` | eine Gruppe vor dem Spieler (Standard 15 m), ohne Rücksicht auf Biom |
| `animals hit [hand\|axe\|pickaxe\|laser]` | das nächste Tier bis 3 m schlagen (Antwort: Kraft in %, erlegt) |
| `animals harvest` | das nächste erlegte Tier bis 3 m zerlegen |
| `animals clear` | alle weg (neue erscheinen gleich wieder) |
| `animals reload` | `animals.json` neu lesen |
| `animals on` / `off` | Tiere ein- und ausschalten |

## Leistung

Die Haut der Tiere wird auf der CPU verformt (2000–4000 Dreiecke je Tier). Nahe Tiere jedes Bild, weiter entfernte
seltener (bis 5-mal je Sekunde), was kleiner als ein Pixel ist oder außerhalb des Bilds liegt, gar nicht. Höchstens
160 Tiere gleichzeitig (`spawn.max_animals`). Im LAN sieht jeder Spieler seine eigenen Tiere.

## Reiten

Pferde (**Wildpferd** in Ebenen und Hügeln, **Przewalski-Pferd** in der Steppe; in `animals.json` `"ride": true`) lassen
sich reiten. Man findet sie frei in der Wildnis; sie sind scheu, also leise heranschleichen (Strg).

- **F** auf ein lebendes Pferd (bis 3 m): aufsteigen. Man sitzt im Sattel (Kamera und Figur höher), das Pferd läuft
  unter der Figur mit: **Trab 7 m/s**, mit **Shift Galopp 15 m/s** (zu Fuß 4 bzw. 7 m/s). **F** nochmal: absteigen.
- Ein gerittenes Pferd verlässt seine Herde und ist **zahm**: es bleibt stehen, wo man abgestiegen ist, und
  verschwindet nicht, auch wenn man weit weggeht. Man kann also zurückkommen und wieder aufsteigen.
- **Mit Seil ziehen:** Werkzeug 9 (Seil) geht auch im Sattel. An einen gefällten Stamm binden, dann zieht das Pferd mit
  der vierfachen Kraft (bis 24 000 N) und wird dabei weniger gebremst als man zu Fuß: so kommen auch große Stämme mit.
- Stirbt das Pferd (Wolf, fallender Baum), fällt man herunter. Mitspieler im LAN können noch nicht reiten.
- Befehl: `player ride` (auf das nächste Pferd bis 6 m), `player ride off`.
