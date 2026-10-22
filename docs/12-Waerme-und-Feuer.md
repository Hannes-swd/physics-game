# Wärme, Feuer und Kälte

Jedes Teil hat eine Temperatur – nicht nur eine einzige, sondern an jeder Stelle eine eigene. Wärme fließt durch das
Material, in Teile, die es berühren, in den Boden und an die Luft. Metall wird warm weich und schmilzt, Holz wird erst
schwarz und brennt dann, Kunststoff schmilzt, tropft und brennt rußend, Kälte macht Stahl, Gummi und Kunststoff spröde.

Ausprobieren: Menü *Datei → Feuerstelle* (`scene fire`) baut ein Lagerfeuer mit einem Stahlstab darüber, Aluminium,
Kunststoffbecher, Gummiring, einem geleimten Brett und einem tiefgekühlten Stahlstab daneben und zündet es an.

## Bedienen

| Wo | Was |
|---|---|
| Bau-Modus, Werkzeug **H** (Wärme / Kälte) | Maustaste auf einem Teil **halten**: Flamme bzw. Kälte trifft genau diese Stelle. Auswahl: Feuerzeug, Gasbrenner, Schweißbrenner, Kältespray, Flüssigstickstoff. Das Werkzeugfenster zeigt die Temperatur unter der Maus. |
| Spiel-Modus, Taste **F** halten | dasselbe Werkzeug auf das Teil, das man anschaut (bis 3 m) |
| Eigenschaften-Fenster, Abschnitt *Temperatur* | mittlere, kälteste und heißeste Temperatur, Zustand (brennt, verkohlt, geschmolzen, weich, spröde), Kennwerte des Materials; **Temperatur setzen**, **Anzünden**, **Löschen** |
| Menü *Physik* | *Wärme und Feuer* an/aus, *Lufttemperatur* (−40 … 35 °C), *Wärme-Zeitraffer* (1×, 5×, 20×, 60×), *Alle Feuer löschen* |
| Physik-Inspektor (F3) | Abschnitt HEAT |
| Statusleiste / Spiel-Anzeige | Temperatur an der Stelle unter der Maus bzw. im Blick, wenn sie auffällt |

Wärme läuft auch im Bau-Modus weiter (ein Feuer brennt, Metall kühlt ab). Teile, die abbrennen oder abtropfen, verändern
ihre Form aber nur, wenn die Physik läuft (Spiel-Modus oder `sim`/`wait` ohne Fenster).

## Befehle

| Befehl | Wirkung |
|---|---|
| `heat` | Überblick: an/aus, Lufttemperatur, Zeitraffer, Feuer (Ort, Leistung, Flammenhöhe, Flammentemperatur), heißestes Teil |
| `heat <id>` | Temperatur und Zustand eines Teils, Kennwerte des Materials |
| `heat <id> <°C>` | ganzes Teil auf diese Temperatur setzen (`heat 5 1000`, `heat 5 -196`) |
| `heat <id> map` | **Temperaturgitter auslesen**: jede Zelle, Schicht für Schicht (`*` brennt, `~` flüssig, `#` verkohlt) |
| `heat on` / `heat off` | Wärme rechnen / alle Temperaturen einfrieren |
| `heat ambient <°C>` | Luft- und Bodentemperatur; unberührte Teile haben sie sofort |
| `heat speed <x>` | Zeitraffer nur für die Wärme (0,1 … 100). Ein echtes Holzfeuer braucht Viertelstunden |
| `ignite <id> [face]` | anzünden (Holz, Gummi, Kunststoff). Ohne Fläche: die unterste Stelle |
| `extinguish <id\|all>` | Wasser: Flammen aus, brennende Stellen auf 100 °C |
| `torch <id> [tool] [seconds] [face]` | Werkzeug auf die Mitte einer Fläche richten. tool = `lighter` `gas` `welding` `spray` `nitrogen`, Standard: Gasbrenner, 5 s, oben |
| `tool heat [tool]` | Wärme-Werkzeug wählen (Fenster) |
| `scene fire` | Beispiel-Feuerstelle |

Werte für die Befehlssprache: `<id>.temp` `.tempmax` `.tempmin` (°C), `.burning` (brennende Stellen), `.charred` und
`.molten` (%), dazu `ambient` und `fires`. Beispiel:

```
new box steel 50 2 2 ; torch last gas 60 right ; wait until last.tempmax > 600 ; print last.tempmax, time
```

## Das Modell

### Temperatur in jedem Teil

Sobald ein Teil Wärme bekommt (oder abgibt), legt die Engine ein Gitter aus Zellen über seine lokale Begrenzung: höchstens
10 Zellen entlang der längsten Achse und höchstens 400 insgesamt, mindestens 4 mm groß. Nur Zellen, in denen Material
liegt, zählen (eine Bohrung oder ein Rohr bleibt innen leer). Jede Zelle hat

| Wert | Bedeutung |
|---|---|
| Temperatur | °C |
| Höchsttemperatur | bleibt erhalten (Anlauffarben, Zunder) |
| Verkohlung | 0 … 1, Holz wird erst braun, dann schwarz |
| Brennstoff | was noch brennen kann (0 = abgebrannt) |
| Schmelze | 0 … 1 |
| Oberfläche | Fläche zur Luft (aus den offenen Seiten, auf die echte Oberfläche des Teils skaliert) |

Unberührte Teile haben kein Gitter und gelten als luftwarm. Ändert sich die Form (Sägen, Bohren, Abbrennen …), wird das
Gitter neu angelegt; jede Zelle übernimmt den Wert der Stelle, an der sie vorher war. Abgesägte Stücke behalten so ihre
Temperatur.

### Wärmeschritt (20 je Sekunde)

| Vorgang | Rechnung |
|---|---|
| Leitung im Teil | zwischen Nachbarzellen G = λ · A / Δx, explizit in so vielen Teilschritten, wie für Stabilität nötig |
| Luft | freie Konvektion h = 1,31 · ΔT^⅓ W/(m²·K), mit Fahrt- oder Wind 6 · v^0,8 dazu (beides mal Luftdichte / 1,204), plus Strahlung ε · σ · (T⁴ − T_Luft⁴) |
| Berührung | an jedem Kontaktpunkt der Physik: Übergang 3000 W/(m²·K) in Reihe mit einer halben Zelle in jedem Teil (Holz isoliert, Kupfer leitet). Der Boden ist Beton bei Lufttemperatur. Nur, wenn die Physik läuft |
| Reibung | Reibimpuls × Gleitgeschwindigkeit aus dem Löser, halb in jedes der beiden Teile |
| Werkzeug | Wärmeübergang h zur Flammen- bzw. Kältetemperatur auf einem Fleck mit dem Radius des Werkzeugs |
| glühende Teile | strahlen ab 300 °C auf ihre Nachbarn (Punktquelle, ohne Abschattung) |

Alle Übergänge zu einer Zieltemperatur werden exakt (exponentiell) gerechnet. Deshalb bleibt die Rechnung auch im
Zeitraffer stabil, und eine Flamme heizt nie über ihre eigene Temperatur.

| Werkzeug | Temperatur | h W/(m²·K) | Fleck |
|---|---|---|---|
| Feuerzeug | 1000 °C | 60 | Ø 2,4 cm, etwa 25 W |
| Gasbrenner (Propan) | 1900 °C | 250 | Ø 6 cm, etwa 1,3 kW |
| Schweißbrenner (Acetylen) | 3100 °C | 3000 | Ø 2,4 cm, etwa 4 kW auf kleiner Fläche: schmilzt Stahl |
| Kältespray | −55 °C | 150 | Ø 6 cm |
| Flüssigstickstoff | −196 °C | 400 | Ø 10 cm |

### Brennen

1. **Verkohlen**: ab der Zersetzungstemperatur (Fichte 200 °C) wird Holz braun, dann schwarz. Die Rate verdreifacht sich
   etwa alle 40 °C. Holzgas entweicht, Holzkohle bleibt (Brennstoff sinkt bis 35 %).
2. **Zünden**: ab der Zündtemperatur (Fichte 300 °C), wenn eine Flamme daneben ist (eigene brennende Nachbarzelle,
   ein Feuer, ein Brenner). Ohne Flamme erst etwa 150 °C später (Selbstentzündung). Ohne Luft (`air vacuum`) gar nicht.
3. **Abbrand**: Masse je Sekunde = Abbrandrate × freie Oberfläche, mehr bei großem Feuer (Strahlung der Flamme auf die
   Glut). Freigesetzte Wärme = Masse × Heizwert; 30 % bleiben in der Glut, der Rest geht in die Flamme.
4. **Ausbreiten**: Brennende Stellen in Reichweite ihrer Flammen bilden ein **Feuer**:
   - Leistung P = Summe aller Stellen
   - Flammenhöhe nach Heskestad L = 0,235 · P^⅖ − 1,02 · D (P in kW, D = Flammenfuß in m), über brennende Wände hinweg
     bis über die Oberkante
   - **Flammentemperatur steigt mit der Größe**: 750 °C + 320 °C · (1 − e^(−P / 30 kW)), also etwa 760 °C bei einer
     kleinen Flamme und gut 1000 °C bei einem großen Feuer
   - Strahlung der Flamme ε = 1 − e^(−1,2 · (D + 0,15 m)): dicke Flammen strahlen mehr
   - Wind drückt die Flamme zur Seite (Auftrieb 2,5 · √L m/s gegen den Wind)

   Was **in der Flamme** steckt, wird mit 40 W/(m²·K) plus Flammenstrahlung auf die Flammentemperatur (oben etwas
   kühler) geheizt und gilt als „Flamme daneben“: Es verkohlt und fängt Feuer. Die Flamme leckt etwas über den Rand der
   brennenden Fläche, so kriecht das Feuer über ein Brett (etwa 1 mm/s auf einem liegenden 2-cm-Brett, nach oben viel
   schneller). Die Umgebung bekommt 30 % der Leistung als Wärmestrahlung (Punktquelle, nimmt mit dem Quadrat der
   Entfernung ab).
5. **Abgebrannt**: Zellen ohne Brennstoff verschwinden aus der Form, das Teil zerfällt, wenn es durchgebrannt ist, Reste
   unter 3 cm³ werden zu Asche. Damit die Kollision schnell bleibt, wird ein stark angebranntes Teil aus den übrigen Zellen
   als wenige große Quader neu gebaut.
6. **Löschen**: `extinguish` (Wasser) oder abkühlen: Fällt eine Stelle 100 °C unter die Zündtemperatur, geht sie aus.

### Schmelzen

Über dem Schmelzpunkt nimmt eine Zelle erst die Schmelzwärme auf (die Temperatur bleibt am Schmelzpunkt stehen), dann ist
sie flüssig. Beim Abkühlen wird die Wärme wieder frei.

- Flüssiges an der Oberfläche, unter dem nichts mehr trägt (kein Material, kein Boden, kein anderes Teil), **tropft ab**:
  Die Zellen verschwinden, darunter entsteht ein **Tropfen** mit ihrem Volumen und ihrer Temperatur.
- Wird es von unten getragen, bleibt es als flüssiger See in der Mulde.
- Ist das ganze Teil flüssig, wird es selbst zum Tropfen (eine Verankerung hält dann nicht mehr).
- Ein Tropfen, der aufkommt, wird zur **Pfütze**: flacher Zylinder mit gleichem Volumen, 5 mm hoch (Metall), 6 mm
  (Kunststoff), 12 mm (Stein). Fällt er auf eine Pfütze gleichen Materials oder daneben, fließen beide zusammen. Liegt
  etwas im Weg, staut sich die Schmelze (kleiner, dafür höher).
- Erstarrt ein Tropfen im Flug, bleibt er eine Kugel. Brennender Kunststoff tropft brennend.

### Festigkeit, Härte und Kälte

Festigkeit und Härte sinken mit der Temperatur wie die Abminderungsbeiwerte des Eurocodes:

  f = 1 / (1 + x³),  x = (T − T_weich) / (T_halb − T_weich),  nahe am Schmelzpunkt gegen 0

Stahl hat bei 600 °C noch knapp die Hälfte, bei 1000 °C etwa 5 %. Das wirkt sich aus auf

| Wo | Wirkung |
|---|---|
| Aufprall | Härte an der Trefferstelle bei ihrer Temperatur: **glühender Stahl wird von kaltem Stahl verformt** (Schmieden), eine Spitze dringt tiefer ein. Verkohltes Holz ist weich und zerbröselt |
| Bruch | Bruchenergie an der Trefferstelle: **Kälte macht spröde**. Baustahl unter etwa −20 °C, Gummi unter −60 °C, Kunststoff (ABS) unter −40 °C. Ganz spröde (45 °C unter dem Übergang) brechen sie wie Ziegel. Aluminium, Kupfer und Messing bleiben auch in flüssigem Stickstoff zäh (wie in Wirklichkeit) |
| Verbindungen | Kleber erweichen (Holzleim ab 60 °C, bei 110 °C hält nichts mehr; Epoxid 80/180 °C, Sekundenkleber 80/150, PU 90/180, Heißkleber 50/90, Montagekleber 70/140). Nägel und Schrauben verlieren mit dem Stahl Festigkeit, und das Holz um sie verkohlt. Schweißnähte werden mit dem Metall weich. Bricht eine Verbindung so, steht in der Meldung „weakened by heat to … %“ |
| Biegen | warm lässt sich Metall und Kunststoff viel enger biegen (Biegeradius × Festigkeitsanteil). Spröde vor Kälte: Biegen wird abgelehnt |

### Materialwerte

Mitgeliefert in `assets/materials.txt` (dort ändern oder neue Materialien anlegen, siehe [13-Materialien.md](13-Materialien.md)):

| Material | λ W/(m·K) | c J/(kg·K) | ε | schmilzt | weich ab / halb | spröde unter | verkohlt / zündet | Heizwert | Abbrand kg/(m²·s) |
|---|---|---|---|---|---|---|---|---|---|
| Fichte | 0,12 | 1700 | 0,9 | – | 60 / 250 °C | – | 200 / 300 °C | 15 MJ/kg | 0,011 |
| Eiche | 0,17 | 1700 | 0,9 | – | 60 / 250 °C | – | 220 / 320 °C | 15 MJ/kg | 0,009 |
| Ziegel | 0,7 | 840 | 0,93 | 1600 °C | 1000 / 1300 °C | (immer spröde) | – | – | – |
| Beton | 1,7 | 880 | 0,94 | 1350 °C | 300 / 600 °C | (immer spröde) | – | – | – |
| Granit | 2,8 | 790 | 0,9 | 1240 °C | 500 / 800 °C | (immer spröde) | – | – | – |
| Stahl | 50 | 490 | 0,7 | 1510 °C | 350 / 600 °C | −20 °C | – | – | – |
| Aluminium | 200 | 900 | 0,2 | 660 °C | 120 / 260 °C | – | – | – | – |
| Messing | 110 | 380 | 0,3 | 920 °C | 250 / 450 °C | – | – | – | – |
| Kupfer | 390 | 385 | 0,5 | 1085 °C | 200 / 450 °C | – | – | – | – |
| Gummi | 0,16 | 2000 | 0,9 | – (zersetzt sich) | 120 / 250 °C | −60 °C | 250 / 350 °C | 32 MJ/kg | 0,015 |
| Kunststoff (ABS) | 0,17 | 1400 | 0,9 | 230 °C | 80 / 105 °C | −40 °C | 300 / 390 °C | 35 MJ/kg | 0,020 |

Schmelzwärme: Stahl 270, Aluminium 397, Messing 168, Kupfer 205, Stein 400, Kunststoff 100 kJ/kg.
Kunststoff ist neu: `plastic` (ABS), 1050 kg/m³, 40 MPa, E = 2,3 GPa, Härte 100 MPa, rot.

## Darstellung

| Effekt | Woher |
|---|---|
| Glühen | ab etwa 500 °C dunkelrot, 900 °C orange, über 1300 °C gelb-weiß (vereinfachter Schwarzer Strahler), Zelle für Zelle |
| Verkohlung | braun, dann schwarz mit Rissen wie Krokodilhaut; heiße Holzkohle glüht in den Rissen und flackert |
| Anlauffarben | Stahl nach der höchsten erreichten Temperatur: strohgelb (220 °C), braun, purpur, blau (290 °C), grau, Zunder (ab 600 °C). Kupfer und Messing laufen rötlich, dann schwarz an. Bleibt nach dem Abkühlen |
| Schmelze | spiegelnd glatt |
| Reif | unter 0 °C setzt sich weißer Reif an |
| Flammen | Flammenzungen je brennender Stelle, bei großen Feuern zusätzlich große Zungen; Form, Flackern und Farbe (dunkelrot → orange → gelb) im Shader aus aufsteigendem Rauschen. Wind neigt sie |
| Rauch | Holz hellgrau und wenig, Gummi und Kunststoff dicht und schwarz; steigt, wird größer, treibt mit dem Wind |
| Funken | aus größeren Feuern |
| Brenner, Kälte | blaue Brennerflamme bzw. weißer Kältenebel an der Auftreffstelle |
| Feuerschein | bis zu 8 flackernde Punktlichter (Feuer, Brenner) beleuchten die Umgebung |

## Grenzen

- Das Gitter ist grob (höchstens 400 Zellen je Teil). Feine Details (dünne Stege) haben die Temperatur ihrer Zelle.
- Wärmeausdehnung wird nicht gerechnet (Passungen bleiben gleich).
- Strahlung zwischen Teilen ohne Abschattung.
- Flüssigkeiten fließen nicht; sie tropfen, bilden Pfützen und vereinen sich.
- Im LAN-Mehrspieler rechnet der Gastgeber die Wärme; Mitspieler sehen Formänderungen (Abbrennen, Tropfen), aber nicht das
  Glühen und die Flammen.
