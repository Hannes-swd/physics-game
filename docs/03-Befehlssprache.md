# Befehlssprache – Befehle kombinieren

Mehrere Befehle, Wartezeiten, Bedingungen und Schleifen passen in **eine Zeile**.
Das funktioniert in der Konsole, in Skripten und per Fernsteuerung.
Alle Schlüsselwörter sind englisch (wie die Befehle selbst).

```
mode play ; new sphere steel 5 0 150 0 ; wait until last.resting max 5 ; print last.y, last.v ;
if last.y < 3 then { screenshot ball.png ; print 'lying on the floor' } else print 'stuck somewhere'
```

Im Spiel mit Fenster läuft eine solche Folge **neben dem Spiel her**: Das Bild bleibt flüssig, die Physik läuft weiter.
Per Fernsteuerung kommt die Antwort, sobald die ganze Folge fertig ist.

## Bausteine

| Baustein | Bedeutung |
|---|---|
| `a ; b ; c` | Befehle nacheinander ausführen |
| `{ a ; b }` | Block: mehrere Befehle als Einheit (für `if` und `repeat`) |
| `wait 2` | 2 Sekunden warten (auch `wait 0.5`, `wait 2 s`) |
| `wait until <condition>` | warten, bis die Bedingung wahr ist (höchstens 60 s) |
| `wait until <condition> max 10` | … höchstens 10 s |
| `if <condition> then <command>` | nur ausführen, wenn die Bedingung wahr ist |
| `if <condition> then { … } else { … }` | mit Alternative (`then` darf fehlen) |
| `repeat 5 times { … }` | Block 5-mal ausführen (`times` darf fehlen) |
| `print <expression>, <expression>, …` | Werte ausgeben. Anderer Name: `show` |
| `set <name> = <expression>` | Wert unter einem Namen speichern. Anderer Name: `let` |
| `$name` | gespeicherten Wert in einen normalen Befehl einsetzen |
| `$(ausdruck)` | Ausdruck ausrechnen und in einen normalen Befehl einsetzen, z. B. `position 5 $(7.x + 30) 10 0` |

Ohne geschweifte Klammern gehört nach `then`, `else` und `times` nur **ein** Befehl dazu:

```
if 17.v > 1 then screenshot fast.png ; print 17.v     <- print läuft immer
if 17.v > 1 then { screenshot fast.png ; print 17.v }  <- beides nur, wenn schnell
```

### Ausgabe von `print`

```
> mode play ; wait 1 ; print 17.y, 17.v, 'text', (1 + 2) * 3
17.y = 4.1611   17.v = 0.5654   text   (1 + 2) * 3 = 9
```

### Ausgabe von `wait until`

```
wait until: true after 0.63 s
wait until: time limit 0.5 s reached, condition not met
```
Ein überschrittenes Zeitlimit ist kein Fehler, die Folge läuft weiter. Mit `if` kann man danach prüfen, was los ist.

In einer Folge wird vor jedem Befehl `> befehl` ausgegeben, damit man sieht, welche Ausgabe wozu gehört.

## Ausdrücke

| Art | Beispiele |
|---|---|
| Zahlen | `3`, `-2.5`, `0.01` (in Ausdrücken nur mit **Punkt**) |
| Text | `"Text"` oder `'Text'` |
| Wahr / falsch | `true`, `false` (auch `yes`/`no`, `on`/`off`); jede Zahl ≠ 0 ist wahr |
| Rechnen | `+ - * /` und Klammern `( )`; `+` hängt Texte aneinander |
| Vergleichen | `<  >  <=  >=  ==  !=` (auch `=` statt `==`). Texte werden ohne Groß-/Kleinschreibung verglichen |
| Verknüpfen | `and`, `or`, `not` |
| Werte von Teilen | `17.y`, `selected.mass`, `last.v`, `n.x` (wenn `n` eine gespeicherte Teilnummer ist) |
| Gespeicherte Werte | `h` oder `$h` |
| Funktionen | siehe unten, z. B. `distance(3, 7)`, `sqrt(2)`, `max(a, b, c)` |

### Funktionen

| Funktion | Ergebnis |
|---|---|
| `abs(x)` | Betrag |
| `sqrt(x)` | Quadratwurzel |
| `round(x)` | auf ganze Zahl runden |
| `sin(w)` `cos(w)` `tan(w)` | Winkelfunktionen, Winkel in **Grad** |
| `min(a, b, …)` `max(a, b, …)` | kleinster / größter Wert |
| `distance(id1, id2)` | Abstand der Schwerpunkte zweier Teile in cm (auch `dist`) |

```
> print distance(a, b), sqrt(16), round(2.6), max(1, 5, 3), sin(30), abs(-4)
distance(a, b) = 2   sqrt(16) = 4   round(2.6) = 3   max(1, 5, 3) = 5   sin(30) = 0.5   abs(-4) = 4
```

### Werte eines Teils: `<part>.<property>`

`<part>` ist eine Nummer, `selected` (auch `sel`), `last` oder ein gespeicherter Name mit einer Teilnummer.

| Eigenschaft | Bedeutung | Einheit |
|---|---|---|
| `x` `y` `z` | Position des Schwerpunkts | cm |
| `bottom` / `top` | tiefster / höchster Punkt | cm |
| `length` `height` `width` | Größe lokal x / y / z (auch `sizex` `sizey` `sizez`) | cm |
| `rx` `ry` `rz` | Drehwinkel (Euler) | Grad |
| `v` | Geschwindigkeit (auch `speed`) | m/s |
| `vx` `vy` `vz` | Geschwindigkeit je Richtung | m/s |
| `kmh` | Geschwindigkeit | km/h |
| `rpm` | Drehgeschwindigkeit | U/min |
| `mass` | Masse | kg |
| `weight` | Gewichtskraft | N |
| `volume` | Volumen | cm³ |
| `energy` | Bewegungs- + Lageenergie | J |
| `resting` | 1, wenn die Physik das Teil schlafen gelegt hat (wirklich zur Ruhe gekommen). Im Bau-Modus ruht alles | 0/1 |
| `still` | 1, wenn es sich gerade fast nicht bewegt (< 1 cm/s) | 0/1 |
| `moving` | Gegenteil von `still` | 0/1 |
| `active` | 1, wenn die Physik das Teil gerade berechnet | 0/1 |
| `fixed` | 1, wenn fest verankert | 0/1 |
| `overlap` | wie tief das Teil gerade in einem anderen steckt | mm |
| `pieces` | Anzahl konvexer Teilstücke der Geometrie | |
| `hulls` | Anzahl Hüllen der Kollisionsform | |
| `edges` | Anzahl Kanten (für `round`, `chamfer`) | |
| `airforce` | Betrag der Luftkraft (Widerstand, Kraft auf Flächen, Propellerschub; ohne statischen Auftrieb) | N |
| `airx` `airy` `airz` | Luftkraft je Richtung (Propeller nach oben: `airy` = Schub) | N |
| `airtorque` | Drehmoment der Luft (bremst z. B. einen Propeller) | N·m |
| `joints` | Anzahl Verbindungen an diesem Teil | |
| `load` | höchste Auslastung seiner Verbindungen (100 = bricht) | % |
| `exists` | 1, wenn es das Teil gibt (auch für gelöschte Nummern erlaubt) | 0/1 |
| `id` `name` `material` | Nummer, Name, Material | Text |

Tipp: Direkt nach dem Erstellen ist ein Teil noch nicht „ruhend“. `wait until last.resting` wartet also,
bis es heruntergefallen ist und still liegt.

### Weitere Werte

| Wert | Bedeutung |
|---|---|
| `parts` | Anzahl Teile |
| `chunks` | Anzahl belegter Chunks |
| `time` | Sekunden seit Programmstart (ohne Fenster: simulierte Zeit) |
| `selected`, `last` | Nummer des ausgewählten bzw. zuletzt erstellten Teils |
| `pi` | 3.14159… |
| `joints` | Anzahl aller Verbindungen |
| `broken` | wie oft bisher etwas gebrochen ist (Verbindungen und Teile) |
| `rescued` | wie oft ein Teil, das tief in ein anderes geraten war, an seine letzte freie Stelle gesetzt wurde |
| `mode` | `"build"` oder `"play"` (nur mit Fenster) |
| `view` | `"perspective"`, `"top"`, `"front"`, `"side"`, `"quad"`, `"firstperson"` (nur mit Fenster) |
| `tool` | `"select"`, `"saw"`, `"drill"`, `"round"`, `"mill"`, `"plane"` (nur mit Fenster) |
| `fps` | Bilder pro Sekunde (nur mit Fenster) |
| `player.x` `.y` `.z` | Position der Füße in cm (nur mit Fenster) |
| `player.onground` | 1, wenn die Figur auf dem Boden steht |
| `player.holding` | Nummer des getragenen Teils (0 = nichts) |
| `player.lookingat` | Nummer des angeschauten Teils (0 = nichts) |
| `player.heading` / `player.pitch` | Blickrichtung / Neigung in Grad |
| `camera.x` `.y` `.z` / `camera.distance` | Zielpunkt der Baukamera (cm) / Abstand (m) |

## Gespeicherte Werte

`set` speichert einen Wert. Er bleibt erhalten, bis das Programm beendet wird
(auch über mehrere Befehle und Fernsteuerungs-Aufrufe hinweg).

```
set board = last
cut $board x 30 ; drill $board top 0 0 8
set start = time ; mode play ; wait until 17.resting ; print time - start
```

In normalen Befehlen wird `$name` durch den Wert ersetzt. In Ausdrücken genügt `name`.

## Beispiele

**Fallversuch: Fallzeit und Rücksprung eines Gummiballs aus 2 m**
```
new sphere rubber 10 0 200 300 ; mode play ; wait until last.vy > 0 max 3 ; wait until last.vy < 0 max 3 ; print last.bottom
```
```
wait until: true after 0.63 s          <- Fallzeit (Theorie: √(2·1,95 m / 9,81 m/s²) = 0,63 s)
wait until: true after 0.50 s          <- Steigzeit nach dem Aufprall
last.bottom = 120.3055                 <- springt auf 120 cm zurück (Stoßzahl ≈ 0,78)
```

**Reibungsversuch: Rutscht der Klotz?**
```
mode play ; wait 2 ; if 17.x < -180 then print 'steel slides' else print 'steel sticks'
```

**Zehn Bretter stapeln**
```
repeat 10 times { new box spruce 60 10 2 0 150 300 ; wait 0.4 } ; mode play
```
(Erst im Spiel-Modus fallen sie, im Bau-Modus schweben sie.)

**Turm bauen, bis er umfällt**
```
mode play ; set n = 0 ;
repeat 15 times { new box brick 24 11.5 7.1 200 100 0 ; wait 1 ; set n = n + 1 } ;
print n, last.y
```

**Wie viel hält eine Heißkleber-Klebung?** (Gewicht stufenweise erhöhen, bis sie bricht)
```
scene empty ; new box oak 20 10 3 0 150 0 ; set d = last ; fix $d ;
new box spruce 20 10 2 0 147.5 0 ; set b = last ; glue $d $b hotglue 0.002 ;
set o = b ; set kg = b.mass ; mode play ;
repeat 40 times { if broken == 0 then { new box steel 5 5 5 0 $(o.bottom - 2.5) 0 ; glue $o last epoxy ;
  set o = last ; set kg = kg + last.mass ; wait 0.5 } } ;
print kg, broken
```
Jeder neue Stahlwürfel (0,98 kg) wird unten an den vorigen geklebt. Die Klebung trägt 0,002 MPa × 200 cm² = 40 N:
```
kg = 4.113   broken = 1
!! glue J1 (Block - Block 2) broke: tension 43 N (holds 40), shear 0 N (holds 50), moment 0.0 N·m
```

**Propellerschub messen**
```
new propeller aluminium 30 2 20 ; set p = last ; position $p 0 100 0 ; axle $p y 3000 ; mode play ; wait 1 ;
print p.airy, p.airtorque, p.rpm
```
```
p.airy = 3.1038   p.airtorque = 0.1916   p.rpm = 3000.0001
```

**Brett vorbereiten, abrunden und zeichnen**
```
new box oak 40 10 3 ; set b = last ; drill $b top 8 0 10 ; drill $b top -8 0 10 ; round $b +y 4 ; cut $b x 34 45 ; drawing $b board.svg
```

**Stift-im-Loch-Versuch: Passt der Dübel?**
```
new box oak 30 10 2 0 50 0 ; set plate = last ; fix $plate ; drill $plate top 0 0 22 ;
new cylinder steel 1.8 8 0 60 0 ; set peg = last ; mode play ; wait 1.5 ;
if peg.y < 45 then print 'fits through' else print 'stuck on top'
```

## Grenzen

- Höchstens 1 000 000 Wiederholungen. Sehr lange Schleifen ohne `wait` werden automatisch auf mehrere Bilder verteilt.
- `wait` höchstens 3600 s.
- Ein Fehler in der Befehlssprache selbst (Schreibfehler im Ausdruck, unbekannte Eigenschaft, fehlende Klammer …)
  bricht die Folge ab und wird mit `Error: …` gemeldet.
- Scheitert dagegen ein einzelner Befehl (unbekannter Befehl, `glue` bei Teilen, die sich nicht berühren …), steht
  `Error: …` in der Ausgabe und die Folge läuft weiter. Ein unbekanntes `$name` bleibt als Text stehen.
  Prüfen lässt sich das danach mit `if`, z. B. `if last.joints == 0 then …`.
- `;` und `}` innerhalb von Anführungszeichen trennen nicht.
- `wait until` prüft die Bedingung etwa 60-mal pro Sekunde. Sehr kurze Ereignisse (ein Aufprall dauert nur
  Millisekunden) erkennt man besser an der Geschwindigkeit (`vy > 0` = springt zurück) als an der Höhe.
