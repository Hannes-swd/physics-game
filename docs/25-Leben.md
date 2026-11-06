# 25 – Leben und Verletzungen

Die Spielfigur hat **keine Lebenspunkte**. Stattdessen kann sich jeder ihrer 16 Körperteile verletzen: Kopf, Hals,
Brust, Bauch und je Seite Oberarm, Unterarm, Hand, Oberschenkel, Schienbein und Fuß.

## Körperkarte

Links unten erscheint eine kleine Figur, **aber nur, wenn man verletzt ist**. Man sieht sie von hinten, links ist also
links. Verletzte Stellen leuchten in der Farbe ihrer Art. Daneben stehen die Verletzungen, die schwerste zuoberst,
jeweils mit Zustand und drei Strichen für die Schwere.

| Farbe | Art | entsteht durch |
|---|---|---|
| Orange | Verbrennung | Feuer (Strahlung, in der Flamme sehr schnell), heiße Teile in der Hand, auf Heißem stehen, heißes Wasser |
| Blau | Erfrierung | kalte Luft (kaltes Klima, Schnee, Nacht), kaltes Wasser, sehr kaltes Metall in der Hand |
| Rot | Schnitt | spitze oder scharfe Teile, die gegen die Figur fliegen oder auf sie fallen; Bisse |
| Violett | Prellung | stumpfe Teile, Stürze ab ca. 3 m, Stöße von Tieren |
| Gelb | Bruch | schwere Treffer und Stürze ab ca. 6 m |

- Ein **frisch verletzter** Körperteil blitzt kurz weiß auf. **Blutende** Stellen pulsieren. **Verbundene** Stellen
  haben zwei helle Streifen.
- Die Karte ist voll sichtbar, solange etwas blutet, schwer ist (Stufe 2 oder 3), man verbindet oder gerade verletzt
  wurde. Leichte Verletzungen blenden nach 6 s auf halbe Deckkraft ab. Gesund: nichts.
- Bei Blutverlust steht ganz oben die Zeile **Blutverlust**.

## Schwere und Heilung

Jede Verletzung hat die Stufe 1 bis 3 (zum Beispiel Schnitt: oberflächlich, blutet, blutet stark). Ein Körperteil hat
höchstens eine Verletzung; die schwerere bleibt (ein Bruch zählt eine Stufe mehr als eine Prellung). Trifft derselbe
schwere Schaden noch einmal dieselbe Stelle, wird sie eine Stufe schlimmer.

Verletzungen heilen mit der Zeit Stufe um Stufe: Prellungen schnell (45 s je Stufe), Schnitte 90 s, Verbrennungen und
Erfrierungen 150 s, Brüche 300 s. Erfrierungen heilen nur im Warmen, Verbrennungen erst, wenn es nicht mehr heiß ist.

## Blut

Schnitte ab Stufe 2 und offene Brüche **bluten**. Ein Schnitt der Stufe 2 gerinnt langsam von selbst, Stufe 3 und
offene Brüche nicht: sie müssen verbunden werden. Ohne Blutung kommt das Blut langsam wieder.

**H** verbindet die schlimmste offene Stelle (dauert 3 s, man kann dabei nur gehen). Dafür braucht man einen
**Verband** im Inventar, der dabei verbraucht wird. Verbundene Schnitte bluten nicht mehr und heilen normal; Brüche
werden geschient. Einen Verband stellt man aus **Fell** her (Rezept *Verband*, **C**, 60 cm³ Fell); Fell bekommt man
von erlegten Tieren ([24](24-Tiere.md#jagd)).

Ohne Verband drückt **H** 4 s auf die Wunde: aus „blutet stark“ wird „blutet“. Das rettet einen erst einmal, die
Wunde blutet aber weiter, bis sie gerinnt oder verbunden wird.

## Auswirkungen

- **Beine** (Oberschenkel, Schienbein, Fuß): langsamer; ab Stufe 2 kein Sprinten, mit gebrochenem Bein kein Springen.
- **Wenig Blut:** schwach und langsam, ab 65 % kein Sprinten.

## Tod

Man stirbt, wenn zu viel Blut verloren ist (unter 40 %), wenn Kopf, Hals, Brust oder Bauch schwer verletzt sind
(Stufe 3, außer Erfrierung) oder bei fünf schweren Verletzungen zugleich. Der Bildschirm wird dunkel und nennt die
Ursache; nach 4 s steht man gesund am Startpunkt (`tp spawn`). Das Inventar bleibt.

## Tiere

Manche Tiere greifen an ([24](24-Tiere.md#angriffe)). Wölfe jagen im Rudel, Wildschweine und Wisente stoßen, wer
ihnen zu nahe kommt, Schlangen, Spinnen und Krabben beißen, stechen oder zwicken, wenn man fast drauftritt. Wo es trifft,
hängt von der Größe des Tiers ab: Schlangen die Füße, Wölfe Beine und Arme, Wisente den Rumpf. Ein **Schlag** (linke
Maustaste, mit Werkzeug weiter) vertreibt sie.

## Wie gerechnet wird

- **Sturz:** aus der Fallgeschwindigkeit beim Aufsetzen. Ab 8 m/s (ca. 3,3 m) Prellungen an den Füßen, ab 11 m/s
  (ca. 6 m) Brüche, ab 18 m/s (ca. 16,5 m) tödlich. Ins Wasser fallen tut nicht weh.
- **Treffer:** nur die Bewegung des Teils auf die Figur zu zählt (wer gegen eine ruhende Kiste läuft, verletzt sich
  nicht). Energie ½·m·v²: ab 25 J Prellung, ab 200 J Bruch, ab 1200 J schwerer Bruch; am Kopf doppelt. Spitze und
  Schneiden (dieselbe Formerkennung wie bei Werkzeugen) schneiden schon ab 15 J. Was man selbst trägt, zählt nicht.
- **Hitze und Kälte** wirken als Dosis je Körperteil, die sich ansammelt und langsam abklingt; bei 1, 2,5 und 5 wird
  daraus eine Verletzung der Stufe 1, 2, 3. Feuer strahlt als Punktquelle (ca. 30 % seiner Leistung), Schmerzgrenze
  2 kW/m²; ein heißes Teil in der Hand verbrennt ab 50 °C, unter den Schuhen ab 60 °C. Kalte Luft unter −5 °C lässt
  zuerst Hände, Füße und Kopf erfrieren, Wasser unter 12 °C alles, was darin ist. Die Lufttemperatur kommt aus der
  Umgebungstemperatur der Welt, dem Klima des Geländes, dem Schnee und der Tageszeit.

Verletzungen werden mit der Welt gespeichert.

## Mehrspieler

Im LAN rechnet der **Gastgeber** die Verletzungen aller Figuren (Stürze, Treffer, Feuer, Kälte, Tierbisse) und schickt
jedem Mitspieler seinen Zustand mit der Spielerliste; der Mitspieler sieht seine eigene Körperkarte und den
Todesbildschirm, Meldungen („… verbunden“) kommen vom Gastgeber. Verletzte Mitspieler sind genauso langsamer, tote
stehen nach 4 s am Startpunkt. Mitspieler haben noch kein Inventar: bei ihnen verbindet **H** ohne Verband.

## Befehle

| Befehl | Wirkung |
|---|---|
| `health` | Blut, Blutung, alle Verletzungen, Tempo |
| `health hurt <teil> <art> [1-3]` | Verletzung zufügen. Teile: `head neck chest belly upperarm_l lowerarm_l hand_l upperarm_r lowerarm_r hand_r upperleg_l lowerleg_l foot_l upperleg_r lowerleg_r foot_r`; Arten: `burn frost cut bruise break` |
| `health use` | wie **H**: Verband aus dem Inventar, sonst Druck auf die Wunde (geht auch als Mitspieler) |
| `health heal` | alles geheilt |
| `health bandage` | sofort verbinden, ohne Verband (zum Testen) |
| `health blood <0..1>` | Blut setzen |
| `health on` / `off` | Verletzungen ein- und ausschalten (z. B. zum Bauen) |
