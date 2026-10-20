# Werkstatt – Handwerker-Simulator

Eine vollständig funktionsfähige 3D-Engine für physikalisch korrekte Konstruktionen und Bearbeitungen im Browser und auf dem Desktop.

## Überblick

**Werkstatt** ist ein interaktiver Handwerker-Simulator mit einer custom 3D-Engine (C++17, Vulkan 1.3) und echter Physik (Jolt Physics mit doppelter Genauigkeit). 

### Kernfeatures

**Konstruktion & Bearbeitung:**
- Teile aus verschiedenen Materialien erstellen (Holz, Ziegel, Stein, Metall)
- Vollständiges Werkzeug-Set: Sägen, Bohren, Fräsen, Abrunden, Drehen, Biegen
- Präzise Bearbeitungen mit technischen Zeichnungen (Dreitafelprojektion)

**Physik & Verbindungen:**
- Realistische Physik mit Jolt Physics Engine
- Verschiedene Verbindungstypen: Kleber, Nägel, Schrauben, Schweißnähte, Scharniere, Federn, Seile
- Bruchverhalten: Verbindungen und spröde Teile brechen bei Überlast
- Aerodynamik: Propeller erzeugen Schub und Luftwiderstand
- Materialspezifische Reibung: Jedes Materialpaar hat eigene Haft- und Gleitreibung

**Steuerung & Automatisierung:**
- Intuitive Maus-Steuerung **und** englische Textbefehle
- Live-Fernsteuerung von außen (TCP/Kommandozeile)
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

## Projektstruktur

```
Werkstatt/
├── CMakeLists.txt           # Build-Konfiguration
├── src/
│   ├── main.cpp             # Einstiegspunkt
│   ├── engine/              # 3D-Engine (Vulkan)
│   ├── physics/             # Physik-Integration (Jolt)
│   ├── commands/            # Befehlsparser und -ausführung
│   ├── geometry/            # Geometrie-Verarbeitung
│   └── ui/                  # Benutzeroberfläche
├── docs/                    # Ausführliche Dokumentation
├── assets/                  # Shaders, Texturen, Szenen
└── build/                   # Build-Output (nach cmake --build)
```

## Technologie-Stack

| Komponente | Technologie |
|---|---|
| **Sprache** | C++17 |
| **Grafik** | Vulkan 1.3 |
| **Physik** | Jolt Physics Engine |
| **Build** | CMake 3.20+ |
| **Compiler** | MSVC (Visual Studio 2022) |

## API & Fernsteuerung

### TCP-Fernsteuerung

Werkstatt kann im Headless-Modus laufen und per TCP gesteuert werden:

```bash
# Starten (lauscht auf Port 12345)
Werkstatt.exe --headless

# Von extern Befehle senden
Werkstatt.exe --send "new box wood 100 50 20"
```

### Befehlssprache

Die vollständige Dokumentation der Befehlssprache inkl. Variablen, Funktionen und Kontrollstrukturen finden Sie in [docs/03-Befehlssprache.md](docs/03-Befehlssprache.md).

## Debugging & Entwicklung

- **F3:** Physik-Inspektor öffnen (Visualisierung der Physik-Bodies, Verbindungen, Kollisionen)
- **F4:** Technische Zeichnung für Präzisions-Überprüfung
- `--selftest`: Interne Tests ausführen
- `--demo`: Demo-Modus mit automatischen Tests
- Alle Befehle werden in die Konsole (F1) protokolliert

## Häufig gestellte Fragen

**F: Kann ich Werkstatt ohne GPU nutzen?**  
A: Nein, Vulkan erfordert eine dedizierte GPU. CPU-Rendering wird nicht unterstützt.

**F: Welche Materialien sind verfügbar?**  
A: Holz, Ziegel, Stein, Metall, Aluminium und weitere. Jedes Material hat spezifische Eigenschaften wie Dichte, Festigkeit und Reibung.

**F: Wie speichere ich meine Konstruktionen?**  
A: Konstruktionen können per Export-Befehl gespeichert und später geladen werden. Siehe [docs/07-Engine-Aufbau.md](docs/07-Engine-Aufbau.md).

**F: Kann ich eigene Szenen erstellen?**  
A: Ja, über Skripte (`.txt` oder per Fernsteuerung) oder durch Editieren der Szenen-Dateien.

## Lizenz

Dieses Projekt ist proprietär. Alle Rechte vorbehalten.

## Kontakt & Beiträge

Für Fragen, Bug-Reports oder Feature-Requests öffnen Sie bitte ein Issue oder kontaktieren Sie den Entwickler.

---

**Version:** 1.0  
**Letztes Update:** 2026-10-20  
**Maintainer:** Hannes

## Systemanforderungen

- **OS:** Windows 10/11 (64-Bit)
- **GPU:** Vulkan 1.3-fähige GPU (NVIDIA RTX, AMD RDNA, Intel Arc oder ähnlich)
- **RAM:** Mindestens 4 GB (8+ GB empfohlen)
- **Vulkan SDK:** [LunarG Vulkan SDK](https://vulkan.lunarg.com/)

## Installation & Starten

### Schnellstart

```bash
build\Release\Werkstatt.exe
```

### Build von Source

Voraussetzungen:
- Visual Studio 2022
- CMake 3.20+
- Vulkan SDK

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
