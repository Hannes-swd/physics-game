# Werkstatt – Dokumentation

Ein Handwerker-Simulator mit eigener 3D-Engine (C++17, Vulkan 1.3) und echter Physik (Jolt Physics).
Man erstellt Teile aus echten Materialien, sägt, bohrt, fräst, rundet, dreht und biegt sie, verbindet sie (Kleber, Nägel, Schrauben, Schweißnaht,
Scharnier, Feder, Seil) und testet sie physikalisch – bis etwas bricht.
Alles lässt sich mit der Maus **und** komplett per Textbefehl steuern, auch live von außen.
Die Befehle, ihre Werte, Materialnamen und alle Ausgaben sind **englisch** (eindeutig und Standard); Menüs und diese
Anleitung sind deutsch.

| Datei | Inhalt |
|---|---|
| [01-Steuerung.md](01-Steuerung.md) | Maus, Tastatur, Modi, Werkzeuge, Fenster |
| [02-Befehle.md](02-Befehle.md) | **Alle Befehle** mit Parametern, Beispielen und Ausgaben |
| [03-Befehlssprache.md](03-Befehlssprache.md) | Befehle kombinieren: `;`, `wait`, `wait until`, `if`, `repeat`, `print`, `set`, Werte |
| [04-Fernsteuerung.md](04-Fernsteuerung.md) | Das laufende Spiel von außen steuern, Skripte, Betrieb ohne Fenster, Kommandozeile |
| [05-Physik-und-Materialien.md](05-Physik-und-Materialien.md) | Physikmodell, Reibung je Materialpaar, Ineinanderstecken und Rettung, Kollisionsformen, Rechenzeit, Materialwerte, Physik-Inspektor (F3), Spielfigur |
| [06-Dreitafelprojektion.md](06-Dreitafelprojektion.md) | Technische Zeichnungen nach DIN (F4), SVG-Export |
| [07-Engine-Aufbau.md](07-Engine-Aufbau.md) | Aufbau des Programms, Geometrie, Chunks, Grafik, Dateiformat, Tests |
| [08-Verbindungen-und-Bruch.md](08-Verbindungen-und-Bruch.md) | Kleben, Nageln, Schrauben, Schweißen, Scharnier, Feder, Seil; Belastung und Bruch; Luft, Wind, Propeller |
| [09-Bearbeitung.md](09-Bearbeitung.md) | Abrunden, Fasen, Fräsen (Tasche, Nut, Falz, Langloch, Zapfen), Hobeln, Drehbank, Biegen, Spiegeln, Skalieren, Vereinen, Ausschneiden |
| [10-Stifte-und-Scharniere.md](10-Stifte-und-Scharniere.md) | Stift in Bohrung: selbstgebaute Scharniere und Achsen, Passung, Schmieren, Abrieb, Ausreißen, Tragen mit R |
| [11-Mehrspieler.md](11-Mehrspieler.md) | Mehrspieler im LAN: Spiel öffnen, Mitspieler zulassen, beitreten, wer was rechnet, Ports |
| [12-Waerme-und-Feuer.md](12-Waerme-und-Feuer.md) | Wärme, Feuer und Kälte: Temperatur in jedem Teil, Wärmeleitung, Brenner und Kältespray, Holz verkohlt und brennt, Feuer breitet sich aus, Metall wird weich und schmilzt, Kälte macht spröde, Darstellung |
| [13-Materialien.md](13-Materialien.md) | Alle Materialien in der Datei `assets/materials.json`: jeder Wert erklärt, neue Materialien anlegen (Datei, Fenster, Befehle), Reibungspaare |
| [14-Wasser.md](14-Wasser.md) | Wasser: Becken und Meer, Auftrieb, Boote und Becher, die volllaufen, Wellen, Strömung, Abschrecken, Schwimmen, Darstellung im Shader, Rechenzeit |
| [15-Hauptmenue-und-Welten.md](15-Hauptmenue-und-Welten.md) | Hauptmenü (Qt Quick): Weltliste, neue Welt, Seed, Mehrspieler, Einstellungen; Ladebildschirm; Speichersystem der Welten, Sicherungen, automatisch speichern; das Menü per Befehl bedienen und auslesen (Fernsteuerung, KI) |

## Schnellstart

```
build\Release\Werkstatt.exe
```

0. Im **Hauptmenü** auf **Spielen → Neue Welt**, Namen eingeben (Seed leer lassen = zufällig) → **Erstellen**, dann die
   Welt doppelklicken. Ein Ladebildschirm zeigt den Fortschritt ([15](15-Hauptmenue-und-Welten.md)).
1. Im **Bau-Modus** links unter „Neues Teil“ Form, Material und Maße wählen → **Erstellen**.
2. Mit **S** (Säge) oder **B** (Bohrer) auf das Teil klicken.
3. **Tab** wechselt in den **Spiel-Modus**: Jetzt wirkt die Schwerkraft, und man kann herumlaufen und Teile tragen.
4. **F1** öffnet die Konsole. `help` zeigt alle Befehle.
5. Menü *Datei → Beispiel-Welt (alles)* lädt eine große Welt mit Kugelbahn, Dominos, Propellern, Hängebrücke und mehr.
6. Menü *Datei → Feuerstelle* zündet ein Lagerfeuer an: Holz verkohlt und brennt, Stahl glüht, Kunststoff schmilzt, gefrorener Stahl zerspringt.
7. Menü *Datei → Wasserbecken*: Holz schwimmt, Stahl sinkt, ein Stahlboot schwimmt, bis es vollläuft, ein umgedrehter Becher hält Luft.
8. Menü *Datei → Fluss*: Strömung um Felsen und einen Brückenpfeiler, das Wasser staut sich und schäumt, Treibgut fährt mit.

Dasselbe als Befehle:

```
new box spruce 80 10 2 ; cut last x 30 ; drill last +y 0 0 8 ; mode play
```

## Einheiten

| Größe | Einheit |
|---|---|
| Positionen, Maße | cm |
| Bohrer-Durchmesser, Bohrtiefe, Schnittfuge, Zahnrad-Modul, Nägel, Schrauben, Kleber-Spalt, Schweißnaht | mm |
| Festigkeit, Haftung | MPa (= N/mm²) |
| Winkel | Grad |
| Geschwindigkeit | m/s |
| Masse | kg |
| Kraft | N, Kraftstoß N·s, Drehmoment N·m |
| Zeit | Sekunden |

## Koordinaten

Y zeigt nach oben, der Boden liegt bei y = 0. X geht nach rechts, Z nach vorne (zur Kamera der Vorderansicht).
Die rote Linie am Boden ist die X-Achse, die blaue die Z-Achse.
Die Position eines Teils ist immer sein **Schwerpunkt**.
