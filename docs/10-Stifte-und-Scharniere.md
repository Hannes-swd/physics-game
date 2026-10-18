# Stifte, Bohrungen und selbstgebaute Scharniere

Statt des fertigen Gelenks (`hinge`) kann man Scharniere, Achsen und Lager selbst bauen: ein Teil mit Bohrung, ein
Stift, der hineinpasst. Die Physik rechnet das als **echte Kollision**: Stift und Lochwand berühren sich, drücken
gegeneinander und reiben aneinander. Spiel, Reibung, Abrieb und Ausreißen ergeben sich daraus.

In der **Beispielwelt** (`scene showcase`, Menü *Datei → Beispiel-Welt*) stehen bei x ≈ 5 m zwei Tore an solchen
Scharnieren (trocken und geölt) und zwei Stifte in Sacklöchern (Ø9,8 passt, Ø10,5 nicht).

## Wie die Physik das rechnet

- Jede **runde Bohrung** (volle 360°, ab Ø2 mm) wird erkannt. Ihre Wand ist für die Physik ein exakter Kreiszylinder
  (eigene Kollisionsform `BoreShape`), kein Vieleck: Ein Stift rattert beim Drehen nicht und passt auch bei
  Zehntelmillimeter-Spiel. Die Kontakte (bis zu 4 Querschnitte × 4 Richtungen) werden genau berechnet, mit
  vorausschauenden Kontakten auf allen Seiten.
- **Gedrehte Stifte** (Drehbank) und unbearbeitete Zylinder sind für die Physik exakte Zylinder.
- **Spiel** wird spürbar: Ein Arm auf einem Bolzen hängt so schräg, wie das Spiel es erlaubt.
- **Übermaß passt nicht**: Ein Stift Ø10,5 bleibt auf einem Loch Ø10 stehen. Eine Fase an der Stiftspitze hilft beim
  Einführen wie in echt.
- Liegt etwas in einer Bohrung an, rechnet die Physik diese Gruppe mit mehr Durchläufen (40 statt 15) und berechnet die
  Kontakte jeden Schritt neu. Ohne Stift kostet eine Bohrung keine zusätzliche Rechenzeit.

## Befehle

| Befehl | Wirkung |
|---|---|
| `bores <id>` | Bohrungen und Stifte eines Teils: Maße, was darin steckt, Spiel und Passung, Last, Pressung, Abrieb |
| `lubricate <id> [oil\|grease\|wax\|soap\|off]` | schmieren (Öl, Fett, Wachs, Seife) oder wieder trocken |
| `player rotate <grad> [kippen]` | getragenes Teil drehen (wie Taste R / Shift+R) |
| `log [n]` | letzte Meldungen der Konsole (z. B. Brüche), auch per Fernsteuerung |

Werte für die Befehlssprache: `.bores` `.pins` `.boreload` (%) `.boreforce` (N) `.pressure` (MPa) `.wear` (mm)
`.worn` (mm³) `.lubricant`. Im Eigenschaften-Fenster stehen die Bohrungen mit Lastbalken und ein Feld **Schmieren**,
im Physik-Inspektor (F3) der Abschnitt *Bores & pins*.

Beispiel (Stahlblock mit Sackloch, Stift Ø9,8):

```
new box steel 4 4 4 0 10 0 ; set k = last ; fix $k ; drill $k top 0 0 10 30
new cylinder steel 0.98 8 0 20 0 ; set p = last ; move $p 0 -6 0
mode play ; wait 1 ; print p.bottom
```
Ausgabe: `p.bottom = 8.999` (Lochboden bei 9,0 cm).

## Passung, Reibung, Schmieren

`bores` nennt das Spiel (Durchmesser) und die Passung: unter 0,05 % des Durchmessers *push fit*, bis 0,2 % *sliding fit*,
bis 0,6 % *running fit*, darüber *loose fit*. Die Reibung ist die des Materialpaars (Stahl/Stahl trocken 0,74/0,57).
Geschmiert gilt höchstens: Öl 0,16/0,10, Fett 0,12/0,08, Wachs 0,20/0,14, Seife 0,25/0,18.
Gemessen am Tor (26 kg, Stoß 5 N·s am Türblatt): trocken öffnet es 12°, geölt 40°.

## Abrieb (Archard)

Abgetragenes Volumen = k · Normalkraft · Gleitweg / Härte (k trocken 5·10⁻⁵ bis 10⁻⁴, geschmiert × 0,03 … 0,3).
Die Wand weicht entsprechend zurück (wird gespeichert). Die Werte sind realistisch klein: Eine Stahltür, 30° geöffnet,
trägt etwa 0,0009 mm³ ab (2 Nanometer Wand). Holz ist rund 100-mal weicher als Stahl.

## Ausreißen

Die Querlast jeder Bohrungshälfte wird mit der Tragfähigkeit verglichen: Lochleibung (Holz nach EC5, quer zur Faser
schwächer; Metall 2,5 × Zugfestigkeit) und Ausreißen zum Rand (2 · Randabstand · Länge · Scherfestigkeit, Holz quer
zur Faser ein Viertel). Hält die Überlast etwa 15 ms an, reißt die Bohrung zum Rand aus oder wird zum Langloch gedrückt:
`!! Block: hole D10.2 mm torn out to the edge (bearing load 373 N, holds 140 N, spruce)`.

## Ein haltbares Scharnier bauen

So sind die Tore in der Beispielwelt gebaut (wie echte Ladenbänder):

1. **Rahmen:** Stahlplatte 0,8 × 6 × 10 cm auf den Pfosten kleben **und** schrauben (`glue`, `screw … 2`).
2. **Stiftlappen:** Stahlzylinder Ø30, auf der Drehbank oben auf Ø10 abdrehen, Spitze anfasen
   (`turn … y 4 8 10`, `turn … y 7.5 8 10 7`), hinten 2 mm abhobeln und an die Platte schweißen (`weld`).
3. **Türband:** Flachstahl 14 × 2,4 × 4 cm, ein Ende rund (`round … e4,e5 11.9`), Bohrung Ø10,4 im Mittelpunkt der Rundung,
   auf die **Fläche** der Tür kleben und schrauben.
4. Tür im Bau-Modus von oben über die Stifte schieben (`move … 0 -10 0`): sie gleitet hinein und bleibt auf den
   Stiftlappen stehen.
5. **Anschläge weit weg vom Scharnier** (Anschlagleiste am zweiten Pfosten, Bodenstopper).

Warum so: Ein nur an der schmalen Seite geklebter Lappen hält gegen Abschälen nur etwa 60 N·m. Drückt man eine Tür gegen
einen Anschlag dicht am Scharnier, wirkt sie als Hebel (80 cm zu 2 cm): Aus 420 N Handkraft werden über 2500 N am
Scharnier. Mit Platte, Schrauben und Anschlägen weit draußen hält es (getestet: Figur zerrt, hebt und dreht die Tür).

## Tragen im Spiel

Getragene Teile behalten ihre Lage (sie drehen mit dem Blick mit) statt zu pendeln. **R** dreht sie um 15°,
**Shift+R** kippt sie. Das Haltemoment ist auf 60 N·m begrenzt: Lange, schwere Teile lassen sich nur langsam drehen.
So kann man eine ausgehängte Tür wieder auf die Stifte setzen.

## Weitere Änderungen an der Physik

- Eindring-Toleranz 0,1 mm statt 1 mm (die Lochwand gleicht sie aus, damit Stifte unter Last nicht einsinken).
- Jolt-Patch 2: Die echten Kontaktkräfte jedes Kontaktpunkts werden an das Spiel gemeldet (Druck, Abrieb, Ausreißen).
- Ein Stoß oder eine Kraft weckt ein fast ruhendes Teil jetzt zuverlässig (vorher schlief es nach einem Schritt wieder ein).
- Die Rettung versetzt keine Teile mehr, die mit anderen verbunden sind (das verbog verklebte Baugruppen).
- Verbindungen brechen erst, wenn die Überlast etwa 15 ms anhält (keine Brüche durch Rechenspitzen).
- `scene empty` setzt die Zähler `broken` und `rescued` zurück.

## Grenzen

- Nur volle runde Bohrungen werden exakt gerechnet; Langlöcher, halbe Bohrungen und Kegel (Senkungen) wie bisher.
- Abrieb verändert nur den Bohrungsradius in der Physik, nicht die sichtbare Form (Nanometer bis Mikrometer).
- Eine Presspassung (Stift mit Gewalt eintreiben) gibt es nicht: Übermaß passt nicht hinein.
