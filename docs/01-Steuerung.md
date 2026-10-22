# Steuerung

Es gibt zwei Modi. **Tab** wechselt zwischen ihnen.

| Modus | Was passiert |
|---|---|
| **Bau-Modus** (CAD) | Keine Schwerkraft, alles steht still. Teile erstellen, genau platzieren, sägen, bohren, abrunden, fräsen, hobeln, biegen, drehen. Verschobene Teile stoßen an anderen an. |
| **Spiel-Modus** | Physik läuft: Schwerkraft, Reibung, Kollisionen, Luft, Verbindungen und Bruch. Man läuft als Handwerker herum. |

Beim Wechsel in den Spiel-Modus wird die Figur dort abgesetzt, wo die Baukamera hinschaut.
Beim Zurückwechseln übernimmt die Baukamera den Blick der Figur. Teile bleiben dort, wo die Physik sie hingebracht hat.
Mit **Strg+Z** kann man das wieder rückgängig machen.

## Bau-Modus

### Kamera

| Eingabe | Wirkung |
|---|---|
| Rechte Maustaste ziehen | Kamera um den Zielpunkt drehen (in Seitenansichten: verschieben) |
| Mittlere Maustaste ziehen / Shift + rechte Maustaste | Kamera verschieben |
| Mausrad | zoomen |
| **F** | auf die Auswahl zoomen |
| **1** | Perspektive |
| **2** | Oben (Draufsicht, X/Z) |
| **3** | Vorne (X/Y) |
| **4** | Seite rechts (Z/Y) |
| **5** | Vier Ansichten gleichzeitig (Oben, Perspektive, Vorne, Seite) |

Die Ansichten 2–4 sind orthografisch (ohne Perspektive), wie in CAD-Programmen.

### Werkzeuge

| Taste | Werkzeug | Bedienung |
|---|---|---|
| **V** (oder Q) | Auswählen / Bewegen | Klick wählt aus, Ziehen bewegt |
| **S** | Säge | Über ein Teil fahren (rote Schnittfläche erscheint), klicken = sägen. **X / Y / Z** wählt die Achse, quer zu der gesägt wird. Gehrungswinkel und Schnittfuge im Werkzeug-Fenster. |
| **B** | Bohrer | Auf eine Fläche klicken: Es wird senkrecht hineingebohrt. Durchmesser und Tiefe (0 = durch) im Werkzeug-Fenster. |
| **E** | Abrunden / Fasen | Die Kante unter der Maus leuchtet (gelb = außen, blau = Innenkante), klicken = abrunden. Radius und „Fase statt Rundung“ im Werkzeug-Fenster. |
| **M** | Fräsen | Auf eine ebene Fläche zeigen (grünes Rechteck), klicken = Tasche fräsen. Länge, Breite, Tiefe, Eckradius im Werkzeug-Fenster. |
| **P** | Hobeln | Auf eine Fläche klicken = die eingestellte Dicke abnehmen. |
| **Esc** | | Werkzeug zurück auf Auswahl, nochmal: Auswahl aufheben |

Die neuen Werkzeuge (Abrunden, Fräsen, Hobeln) führen einen Befehl aus. Er steht danach in der Konsole (F1),
z. B. `round 9 e3 3.00`. Mehr dazu in [09-Bearbeitung.md](09-Bearbeitung.md).

### Auswählen und Bewegen

| Eingabe | Wirkung |
|---|---|
| Klick auf Teil | auswählen |
| **Shift** oder **Strg** + Klick | weitere Teile zur Auswahl hinzufügen/entfernen |
| **Strg+A** | alles auswählen |
| Ziehen | bewegen (waagerecht; in Seitenansichten in der Bildebene). Das Teil **stößt an anderen Teilen an** und gleitet an ihnen entlang |
| **Shift** beim Ziehen | hoch/runter bewegen |
| **Alt** beim Ziehen | ohne Einrasten |
| **Strg** beim Ziehen | durch andere Teile hindurch (ohne Anstoßen) |
| **R** | 90° drehen (um die Hochachse; in Vorne/Seite um die Blickachse). Shift+R: andere Achse |
| **G** | absetzen: Teil fällt senkrecht, bis es aufliegt |
| **Entf** | löschen |
| **Strg+D** | duplizieren |
| **Strg+Z / Strg+Y** | rückgängig / wiederholen |
| **Strg+S / Strg+O** | Szene speichern / laden |

Das Raster zum Einrasten (1 mm bis 10 cm) stellt man im Werkzeug-Fenster ein.
Mehrere ausgewählte Teile werden gemeinsam bewegt, gedreht, gelöscht und kopiert.

## Spiel-Modus

| Eingabe | Wirkung |
|---|---|
| **W A S D** | laufen |
| **Shift** | rennen |
| **Leertaste** | springen (ca. 1,1 m hoch) |
| Maus | umschauen |
| Linke Maustaste **halten** | angeschautes Teil tragen (loslassen = fallen lassen) |
| **E** | Teil aufnehmen / ablegen, ohne die Taste zu halten |
| Rechte Maustaste | getragenes Teil werfen |
| Mausrad | Abstand des getragenen Teils ändern |
| **F5** | Ansicht wechseln: durch die Augen der Figur / von hinten über die Schulter |
| **Esc** | Maus freigeben (Pause). Klick ins Bild: weiterspielen |

Die Spielfigur ist ein Bauarbeiter (Modell von Quaternius, CC0, siehe `assets/characters`). Sie steht, geht und rennt
je nach Tempo, macht seitwärts und rückwärts eigene Schritte (auch schräg gemischt) und neigt den Kopf mit dem Blick.
Beim Springen zieht sie die Knie an und balanciert mit den Armen, beim Fallen streckt sie die Beine, und nach der
Landung federt sie in die Knie – umso tiefer, je höher der Fall war. Mitspieler im LAN
tragen Helm und Warnstreifen in ihrer Spielerfarbe. Steigt die Figur auf eine Stufe oder ein Teil (bis 35 cm),
gleitet die Kamera weich nach. Steht sie auf einem beweglichen Teil, fährt sie mit; Stöße und Zittern eines leichten
Teils federn ihre Beine ab, statt sie in die Luft zu werfen.

Die Figur kann höchstens ca. **420 N** aufbringen. Teile über ca. 43 kg lassen sich nicht heben, nur schieben oder schleifen.
Die Figur schiebt beim Laufen auch lose Teile weg (mit bis zu 350 N).

## Überall

| Taste | Fenster / Funktion |
|---|---|
| **F1** oder **^** | Konsole: Befehle eintippen. Pfeil hoch/runter = frühere Befehle |
| **F3** | Physik-Inspektor für das ausgewählte (Bau) bzw. angeschaute/getragene (Spiel) Teil |
| **F4** | Dreitafelprojektion der Auswahl |
| **F12** | Foto (PNG) im Programmordner speichern |
| **Tab** | Modus wechseln |

## Fenster

| Fenster | Inhalt |
|---|---|
| **Werkzeuge** (links, Bau-Modus) | Werkzeugwahl, Einstellungen von Säge, Bohrer, Abrunden, Fräsen, Hobeln, Einrasten, **Neues Teil** (Quader, Zylinder, Rohr, Kugel, Zahnrad, Propeller; Material, Maße), Blender-Import |
| **Eigenschaften** (rechts, Bau-Modus) | Name, Material, Größe, Volumen, Masse, Haft-/Gleitreibung, Position und Drehung zum Eintippen, Lagerung (fest / Drehachse mit Motor), **Bearbeiten** (alle Kanten abrunden/fasen/schleifen, Drehbank, Biegen, Spiegeln, Skalieren), Verbindungen, Unzerbrechlich. Bei zwei gewählten Teilen: **Verbinden** und **Formen kombinieren** (Vereinen, Ausschneiden, Schnittmenge) |
| **Konsole** (F1) | Befehle und alle Meldungen |
| **Physik-Inspektor** (F3) | Live-Werte, siehe [05-Physik-und-Materialien.md](05-Physik-und-Materialien.md) |
| **Dreitafelprojektion** (F4) | Technische Zeichnung, siehe [06-Dreitafelprojektion.md](06-Dreitafelprojektion.md) |
| **Statusleiste** (unten) | Hinweise, Teil unter der Maus, Chunks, gezeichnete Teile je Detailstufe, FPS; im Spiel-Modus auch Physikzeit, Kontakte, aktive Teile und größtes Eindringen |

Oben in der Mitte werden Befehle eingeblendet, die per Fernsteuerung ankommen, zusammen mit ihrem Ergebnis.
Bricht etwas (Verbindung oder Teil), erscheint dort ebenfalls ein Hinweis.

## Verbinden mit der Maus

1. Im Bau-Modus das **haltende** Teil anklicken, dann mit **Shift+Klick** das **gehaltene** Teil dazunehmen.
2. Im Eigenschaften-Fenster erscheint **Verbinden**: Art wählen (Kleben, Nageln, Schrauben, Schweißen, Scharnier, Feder, Seil)
   und die Werte einstellen (0 = typischer Wert).
3. **Verbinden** drücken.

Unter **Verbindungen** im Eigenschaften-Fenster steht jede Verbindung des Teils mit einem Balken für die Belastung
(live im Spiel-Modus), einem Haken **hält alles** (unzerstörbar), dem Knopf **lösen** und den wichtigsten Werten
(Haftung, Anzahl Nägel, Bruchkraft, Motordrehzahl, Federsteifigkeit) zum Ändern.

Im Bau-Modus sind Verbindungen als kleine Markierungen zu sehen (grün = wenig belastet, gelb = halb, rot = kurz vor dem Bruch),
Seile als Linie, Federn als Zickzack.

Zwei gewählte Teile lassen sich darunter auch zu einem Teil **vereinen**, das zweite aus dem ersten
**ausschneiden** oder auf ihre **Schnittmenge** beschneiden (siehe [09-Bearbeitung.md](09-Bearbeitung.md#formen-kombinieren)).

## Menü Datei

| Eintrag | Wirkung |
|---|---|
| **Leere Werkstatt** | alles entfernen (`scene empty`) |
| **Beispiel-Werkstatt** | Werkbank, Rampe, Getriebe (`scene demo`) |
| **Beispiel-Welt (alles)** | große Welt mit Kugelbahn, Dominos, Pendel, Propellern, Hängebrücke, Feder, Wippe, Werkstücken, Regal, Abrissbirne, Axt mit Spaltholz (`scene showcase`, siehe [02](02-Befehle.md#die-beispielwelt-scene-showcase)) |
| **Speichern / Laden** | Szene als Datei (Strg+S / Strg+O) |
| **Modell importieren** | Blender-Modell (.obj) |
| **Foto speichern** | Bild als PNG (F12) |

## Menü Physik

| Eintrag | Wirkung |
|---|---|
| **Bruch (Verbindungen und Teile)** | an: Verbindungen und spröde Teile brechen bei Überlast; aus: alles hält (wie `breakage on/off`) |
| **Vakuum (keine Luft)** | ohne Luft: kein Luftwiderstand, kein Propellerschub (wie `air vacuum`) |
| **Bau-Modus: Teile stoßen an** | an: beim Verschieben im Bau-Modus halten Teile an anderen an (wie `buildcollision on/off`) |
| **Wind** | kein / leicht 3 m/s / stark 10 m/s / Sturm 25 m/s (wie `wind x y z`) |
