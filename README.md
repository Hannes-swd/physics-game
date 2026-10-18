# Werkstatt – Handwerker-Simulator

Eigene 3D-Engine (C++17, Vulkan 1.3) mit echter Physik (Jolt Physics, doppelte Genauigkeit).
Teile aus Holz, Ziegel, Stein und Metall erstellen, sägen, bohren, fräsen, abrunden, drehen, biegen, kleben, nageln,
schrauben, schweißen und physikalisch testen – Verbindungen und spröde Teile brechen bei Überlast, Propeller erzeugen
Schub und Luftwiderstand, jedes Materialpaar hat eigene Haft- und Gleitreibung.
Alles geht mit der Maus **und** per Textbefehl (englisch), auch live von außen (Fernsteuerung).

**Vollständige Dokumentation: [docs/README.md](docs/README.md)**

| | |
|---|---|
| [Steuerung](docs/01-Steuerung.md) | Maus, Tastatur, Modi, Werkzeuge |
| [Befehle](docs/02-Befehle.md) | alle Befehle mit Beispielen |
| [Befehlssprache](docs/03-Befehlssprache.md) | `;`, `wait`, `wait until`, `if`, `repeat`, `print`, `set`, `$(…)`, Funktionen |
| [Fernsteuerung](docs/04-Fernsteuerung.md) | laufendes Spiel steuern, Skripte, ohne Fenster, Kommandozeile |
| [Physik & Materialien](docs/05-Physik-und-Materialien.md) | Physikmodell, Reibung, Kollisionsformen, Rechenzeit, Materialwerte, Physik-Inspektor (F3) |
| [Dreitafelprojektion](docs/06-Dreitafelprojektion.md) | technische Zeichnungen (F4), SVG |
| [Engine-Aufbau](docs/07-Engine-Aufbau.md) | Programmaufbau, Geometrie, Chunks, Grafik, Dateiformat |
| [Verbindungen & Bruch](docs/08-Verbindungen-und-Bruch.md) | Kleber, Nägel, Schrauben, Schweißnaht, Scharnier, Feder, Seil, Bruch, Luft, Propeller |
| [Bearbeitung](docs/09-Bearbeitung.md) | Abrunden, Fasen, Fräsen, Hobeln, Drehbank, Biegen, Spiegeln, Skalieren, Formen kombinieren |
| [Stifte & Scharniere](docs/10-Stifte-und-Scharniere.md) | selbstgebaute Scharniere: Stift in Bohrung, Schmieren, Abrieb, Ausreißen |

## Starten

```
build\Release\Werkstatt.exe
```

## Bauen

Visual Studio 2022, Vulkan SDK, CMake:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\Werkstatt.exe --selftest
```

## Kurz-Beispiele

In der Konsole (F1) oder per Fernsteuerung, in der Beispiel-Werkstatt:

```
new box spruce 80 10 2 ; cut last x 30 45 ; drill last top 0 0 8 ; round last top 3
mode play ; wait until 17.resting max 5 ; print 17.x, 17.y
glue 13 14 construction ; nail 1 6 4 ; joints
new propeller aluminium 30 ; axle last y 3000 ; wait 1 ; print last.airy
scene showcase                                  (große Beispielwelt mit allem)
Werkstatt.exe --send "status"                   (an das laufende Spiel)
Werkstatt.exe --headless --demo --script commands.txt
```
