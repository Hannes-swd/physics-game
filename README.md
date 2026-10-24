# Werkstatt – Handwerker-Simulator

Eine vollständig funktionsfähige 3D-Engine für physikalisch korrekte Konstruktionen und Bearbeitungen im Browser und auf dem Desktop.

## Überblick

**Werkstatt** ist ein interaktiver Handwerker-Simulator mit einer custom 3D-Engine (C++17, Vulkan 1.3) und echter Physik (Jolt Physics mit doppelter Genauigkeit). 

### Kernfeatures

**Konstruktion & Bearbeitung:**
- Teile aus verschiedenen Materialien erstellen (Holz, Ziegel, Stein, Metall, Gummi, Kunststoff); alle Materialwerte stehen in
  einer JSON-Datei (`assets/materials.json`) und lassen sich im Spiel bearbeiten und erweitern
- Vollständiges Werkzeug-Set: Sägen, Bohren, Fräsen, Abrunden, Drehen, Biegen
- Präzise Bearbeitungen mit technischen Zeichnungen (Dreitafelprojektion)

**Physik & Verbindungen:**
- Realistische Physik mit Jolt Physics Engine
- Verschiedene Verbindungstypen: Kleber, Nägel, Schrauben, Schweißnähte, Scharniere, Federn, Seile
- Bruchverhalten: Verbindungen und spröde Teile brechen bei Überlast
- Aerodynamik: Propeller erzeugen Schub und Luftwiderstand
- Materialspezifische Reibung: Jedes Materialpaar hat eigene Haft- und Gleitreibung
- Wärme, Feuer und Kälte: Temperatur an jeder Stelle eines Teils, Wärmeleitung, Brenner und Kältespray; Holz verkohlt,
  brennt und das Feuer breitet sich aus; Metall glüht, wird weich und schmilzt; Kälte macht Stahl, Gummi und Kunststoff spröde
- Wasser: Becken und Meer (mit Meeresgrund und trockenem Land), exakter Auftrieb, Boote und Becher, die volllaufen,
  Wellen, Strömung um Hindernisse, Abschrecken, Schwimmen

**Welten & Hauptmenü:**
- Hauptmenü (Qt Quick / QML, noch ungestalteter Rohbau): Weltliste, neue Welt mit Seed, Mehrspieler, Einstellungen
- Jede Welt ein eigener Ordner mit sicherem Speichern (Sicherungskopie, nichts geht bei einem Absturz verloren),
  automatischem Speichern, Kamera, Spielfigur und Umgebung
- Ladebildschirm mit echtem Fortschritt beim Öffnen einer Welt und beim Beitreten zu einem LAN-Spiel

**Steuerung & Automatisierung:**
- Intuitive Maus-Steuerung **und** englische Textbefehle
- Live-Fernsteuerung von außen (TCP/Kommandozeile), auch das Hauptmenü: eine KI kann alles per Befehl bedienen und
  auslesen, ohne Bildschirmfotos
- Befehlssprache mit Scripting: Konditionale, Schleifen, Variablen, Funktionen
- Headless-Modus für automatisierte Tests und Simulationen

**Visualisierung:**
- Echtzeit 3D-Rendering mit Vulkan
- Physik-Inspektor (F3) für Debugging
- Technische Zeichnungen mit SVG-Export
- Verschiedene Ansicht-Modi

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
| [Mehrspieler](docs/11-Mehrspieler.md) | im LAN zusammen bauen und spielen: Gastgeber rechnet, Mitspieler treten bei |
| [Wärme & Feuer](docs/12-Waerme-und-Feuer.md) | Temperatur, Wärmeleitung, Feuer, Schmelzen, Kälte, Befehle `heat`, `ignite`, `torch` |
| [Materialien](docs/13-Materialien.md) | alle Materialwerte in `assets/materials.json`, neue Materialien anlegen, Material-Editor |
| [Wasser](docs/14-Wasser.md) | Becken, Meer mit Grund und Land, Auftrieb, Boote, Wellen, Strömung |
| [Hauptmenü & Welten](docs/15-Hauptmenue-und-Welten.md) | Hauptmenü (Qt Quick), Weltliste, Speichersystem, Einstellungen |

## Projektstruktur

```
Werkstatt/
├── CMakeLists.txt           # Build-Konfiguration (lädt GLFW, GLM, Jolt, ImGui; findet Qt für das Hauptmenü)
├── src/                     # Spiel und Engine (Aufgabe jeder Datei: docs/07-Engine-Aufbau.md)
│   ├── main.cpp             # Kommandozeile, Ablauf Hauptmenü -> Spiel -> Hauptmenü
│   ├── App*.cpp             # Spielfenster: Kameras, Eingabe, Oberfläche (ImGui), LAN, Wasser, Welt + Ladebildschirm
│   ├── World*, Solid*, ...  # Teile, Geometrie, Bearbeitung, Physik (Jolt), Wärme, Wasser, Verbindungen
│   ├── Worlds.*, Settings.* # Welten als Ordner (world.json, Szene, Sicherungen), Einstellungen
│   └── Menu.*               # Hauptmenü (Qt), eigene Bibliothek WerkstattMenu
├── qml/                     # Oberfläche des Hauptmenüs (QML, eingebettet)
├── shaders/                 # GLSL, wird beim Bauen eingebettet
├── assets/                  # Materialien (materials.json), Spielfigur
├── cmake/                   # Hilfsskripte (Einbetten, Korrektur für Jolt)
├── docs/                    # Ausführliche Dokumentation
└── build/                   # Build-Output (nach cmake --build)
```

Welten und Einstellungen liegen nicht im Projekt, sondern unter `%APPDATA%\Werkstatt\`
([15](docs/15-Hauptmenue-und-Welten.md#speichersystem)).

## Technologie-Stack

| Komponente | Technologie |
|---|---|
| **Sprache** | C++17 |
| **Grafik** | Vulkan 1.3, Fenster GLFW |
| **Physik** | Jolt Physics Engine (doppelte Genauigkeit) |
| **Oberfläche im Spiel** | Dear ImGui |
| **Hauptmenü** | Qt 6 Quick / QML (optional; ohne Qt startet das Spiel direkt) |
| **Build** | CMake 3.20+ |
| **Compiler** | MSVC (Visual Studio 2022) |

## API & Fernsteuerung

### TCP-Fernsteuerung

Das laufende Programm wartet auf Port **7777** (nur `127.0.0.1`) auf Befehle, schon im Hauptmenü:

```bash
# Hauptmenü: wo bin ich, Welt anlegen und öffnen
Werkstatt.exe --send "menu" "menu new Testwelt 4711" "menu play Testwelt"

# Im Spiel: alle Befehle
Werkstatt.exe --send "new box oak 100 50 20" "world save" "world menu"
```

Ohne Fenster (Tests, Simulationen): `Werkstatt.exe --headless --script befehle.txt`.
Alles dazu: [docs/04-Fernsteuerung.md](docs/04-Fernsteuerung.md).

### Befehlssprache

Die vollständige Dokumentation der Befehlssprache inkl. Variablen, Funktionen und Kontrollstrukturen finden Sie in [docs/03-Befehlssprache.md](docs/03-Befehlssprache.md).

## Debugging & Entwicklung

- **F3:** Physik-Inspektor öffnen (Visualisierung der Physik-Bodies, Verbindungen, Kollisionen)
- **F4:** Technische Zeichnung für Präzisions-Überprüfung
- `--selftest`: Interne Tests ausführen
- `--headless --demo`: ohne Fenster mit der Beispiel-Werkstatt (Befehle von der Tastatur oder einer Pipe)
- `--world <Name>`: eine Welt direkt öffnen, `--no-menu`: ohne Hauptmenü, `--menu-shot bild.png [seite]`: Bild des Hauptmenüs
- Alle Befehle werden in die Konsole (F1) protokolliert

## Häufig gestellte Fragen

**F: Kann ich Werkstatt ohne GPU nutzen?**  
A: Nein, es braucht eine Grafik mit Vulkan 1.3. Neuere integrierte Grafik (z.B. Intel Iris Xe) reicht für kleine Szenen; CPU-Rendering wird nicht unterstützt. Siehe [Systemanforderungen](#systemanforderungen).

**F: Welche Materialien sind verfügbar?**  
A: Holz, Ziegel, Stein, Metall, Aluminium und weitere. Jedes Material hat spezifische Eigenschaften wie Dichte, Festigkeit und Reibung.

**F: Wie speichere ich meine Konstruktionen?**  
A: In einer Welt aus der Weltliste automatisch (Standard alle 5 Minuten, beim Verlassen und Beenden) und mit Strg+S.
Einzelne Szenen lassen sich zusätzlich als `.werkstatt`-Datei speichern und laden (*Datei → Szene speichern unter*).
Siehe [docs/15-Hauptmenue-und-Welten.md](docs/15-Hauptmenue-und-Welten.md).

**F: Kann ich eigene Szenen erstellen?**  
A: Ja, über Skripte (`.txt` oder per Fernsteuerung) oder durch Editieren der Szenen-Dateien.

## Lizenz

Dieses Projekt ist proprietär. Alle Rechte vorbehalten.

## Kontakt & Beiträge

Für Fragen, Bug-Reports oder Feature-Requests öffnen Sie bitte ein Issue oder kontaktieren Sie den Entwickler.

---

**Version:** 1.0  
**Letztes Update:** 2026-10-24  
**Maintainer:** Hannes

## Systemanforderungen

Richtwerte (nicht auf jeder Hardware gemessen):

| | Minimum | Empfohlen |
|---|---|---|
| **Betriebssystem** | Windows 10 (64-Bit) | Windows 11 (64-Bit) |
| **Prozessor** | 4 Kerne, z.B. Intel Core i5 (6. Gen.) / AMD Ryzen 3 | 6–8 Kerne, z.B. Intel Core i5/i7 (10. Gen.+) / AMD Ryzen 5/7 (3000+) |
| **Grafik** | Vulkan 1.3 mit aktuellem Treiber: NVIDIA GTX 900, AMD RX 400, Intel Iris Xe oder neuer | NVIDIA GTX 1660 / RTX 2060, AMD RX 5600 oder besser |
| **Grafikspeicher** | 2 GB | 4 GB+ |
| **Arbeitsspeicher** | 8 GB | 16 GB |
| **Speicherplatz** | ca. 100 MB zum Spielen | ca. 3 GB zum Selbst-Bauen (Visual Studio-Build mit Abhängigkeiten) |
| **Bildschirm** | 1600 × 900 | 1920 × 1080 oder mehr |

Was wie stark belastet:

- **Prozessor (am wichtigsten):** Die Physik (Jolt) rechnet 240 Schritte pro Sekunde und verteilt die Arbeit auf alle Kerne
  bis auf einen. Viele Teile, die sich gleichzeitig bewegen (Dominos, Kugelbahn, große Bauten), Bohrungen mit Stiften und
  viele Verbindungen kosten vor allem CPU. Mehr Kerne und hoher Takt helfen hier am meisten.
  Bearbeitungen (Sägen, Bohren, Abrunden, Biegen) laufen auf einem Kern: Bei komplizierten Teilen dauert ein Schnitt dann
  einen Moment.
- **Grafik:** eher genügsam. Schatten mit 4096 × 4096-Schattenkarte und 4× Kantenglättung; weit entfernte Teile werden
  vereinfacht oder ausgeblendet. Integrierte Grafik reicht für kleine Werkstätten, für die große Beispiel-Welt und
  „Vier Ansichten“ ist eine eigene Grafikkarte angenehmer. Schatten und Kanten lassen sich unter *Ansicht* abschalten.
- **Arbeitsspeicher:** Jeder Rückgängig-Schritt speichert die ganze Szene (bis zu 50 Schritte). Bei sehr großen Szenen
  wächst der Speicherbedarf daher mit der Zeit.

Zum **Spielen** braucht es nur einen aktuellen Grafiktreiber (bringt Vulkan mit). Das
[LunarG Vulkan SDK](https://vulkan.lunarg.com/) ist nur zum **Selbst-Bauen** nötig (Shader-Compiler `glslc`), siehe unten.
Ob die Grafikkarte Vulkan 1.3 kann, zeigt z.B. `vulkaninfo --summary` (aus dem SDK) oder die
[GPU-Datenbank von gpuinfo.org](https://vulkan.gpuinfo.org/).

## Installation & Starten

### Schnellstart

```bash
build\Release\Werkstatt.exe
```

Im Hauptmenü **Spielen → Neue Welt**, dann die Welt doppelklicken.

### Build von Source

Voraussetzungen:
- Visual Studio 2022
- CMake 3.20+
- Vulkan SDK
- für das Hauptmenü: Qt 6.5+ für MSVC 2022 mit Qt Quick und Qt Quick Controls (wird unter `C:/Qt/6.*/msvc2022_64`
  gefunden; ohne Qt wird das Spiel ohne Hauptmenü gebaut). Nach dem Bauen legt `windeployqt` die Qt-Dateien neben
  `Werkstatt.exe`.

```bash
# Build konfigurieren
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Kompilieren
cmake --build build --config Release

# Tests ausführen
build\Release\Werkstatt.exe --selftest

# Starten
build\Release\Werkstatt.exe
```

In Visual Studio zum Spielen die Konfiguration **Release** oder **RelWithDebInfo** wählen (optimiert, Haltepunkte gehen
trotzdem). **Debug** ist nur zum Schritt-für-Schritt-Debuggen gedacht: Der Spielcode ist dort nicht optimiert und das Spiel
deutlich langsamer (Fenstertitel „Debug-Version“). Details: [Engine-Aufbau, Bauen](docs/07-Engine-Aufbau.md#bauen).

## Verwendung

### In der Anwendung

Starten Sie die Anwendung und nutzen Sie:
- **F1:** Konsolenfenster öffnen (Befehle eingeben)
- **F3:** Physik-Inspektor (Debugging von Physik und Verbindungen)
- **F4:** Technische Zeichnung / Dreitafelprojektion
- **Maus:** Objekte auswählen, verschieben, drehen
- **Zahlen 1-9:** Werkzeugprofil wechseln

### Befehlszeile

```bash
# Vollständige GUI mit Standard-Szene
Werkstatt.exe

# Mit spezifischer Szene starten
Werkstatt.exe --scene showcase

# Headless-Modus (kein Fenster, Fernsteuerung nur per TCP)
Werkstatt.exe --headless

# Mit Skript ausführen
Werkstatt.exe --script commands.txt

# Demo-Modus mit automatischen Tests
Werkstatt.exe --demo

# Status des laufenden Prozesses abfragen
Werkstatt.exe --send "status"

# Befehle an laufendes Spiel senden
Werkstatt.exe --send "new box spruce 100 50 20"
```

## Beispiele

### Konsole (F1) oder Fernsteuerung

```
# Neue Teile erstellen und bearbeiten
new box spruce 80 10 2
cut last x 30 45
drill last top 0 0 8
round last top 3

# Physik-Simulation: Warten bis Teil zur Ruhe kommt
mode play
wait until 17.resting max 5
print 17.x, 17.y

# Verbindungen erstellen
glue 13 14 construction
nail 1 6 4
joints

# Propeller mit Luftwiderstand
new propeller aluminium 30
axle last y 3000
wait 1
print last.airy

# Vordefinierte Szenen
scene showcase
```

### Skriptdatei (commands.txt)

```
# Komplexe Konstruktion mit Schleifen und Bedingungen
new box wood 100 50 20
repeat 3
  drill last top $(i*30) 0 8
end

if true
  glue 1 2 construction
end

mode play
wait until 1.resting max 10
```
