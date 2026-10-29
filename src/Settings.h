// Einstellungen des Spiels: %APPDATA%\Werkstatt\settings.json. Das Hauptmenü bearbeitet sie, das Spiel liest sie beim
// Öffnen einer Welt (und schreibt beim Verlassen zurück, was man im Spiel umgestellt hat: Ansicht, Name im LAN).
#pragma once
#include <string>

struct GameSettings {
    std::string playerName;          // Name im LAN (leer: Windows-Benutzer)
    double mouseSensitivity = 1.0;   // Faktor auf die Mausbewegung im Spiel-Modus
    double fovDeg = 55.0;            // Sichtfeld (senkrecht)
    bool shadows = true, edges = true, grid = true;
    bool biomeBorders = false;       // Gelände: Linien an den Biomgrenzen (Standard aus, seit Version 2 der Datei)
    int autosaveMinutes = 5;         // Welt automatisch speichern (0 = nie)

    // Grafik (gilt beim nächsten Öffnen einer Welt). Die Stufen Niedrig..Ultra setzen alle Werte auf einmal
    int msaa = 4;                    // Kantenglättung: Abtastungen je Pixel (1 = aus, 2, 4, 8)
    bool supersampling = false;      // Flächen je Abtastung berechnen: feine Muster scharf statt flimmernd (teuer)
    int shadowQuality = 2;           // 0 Niedrig .. 3 Ultra: Auflösung und Reichweite der Schatten
    double viewDistance = 1.0;       // Sichtweite (Faktor 0,5..2): Gelände-Detail, Bäume, Büsche, Nebel
    double plants = 1.0;             // Gras und Blumen (Faktor 0 = aus .. 1,5): Dichte und Reichweite
    bool vsync = true;               // Bildrate an den Bildschirm binden (kein Zerreißen)
};

// Grafikstufen: 0 Niedrig, 1 Mittel, 2 Hoch, 3 Ultra
constexpr int kGraphicsPresets = 4;
extern const char* const kGraphicsPresetNames[kGraphicsPresets];  // "low", "medium", "high", "ultra"
void applyGraphicsPreset(GameSettings& s, int preset);
int graphicsPresetOf(const GameSettings& s);  // die Stufe, zu der alle Werte passen; -1: eigene Werte
// Schatten je Stufe: Kantenlänge der Schattenkarte (Pixel) und Reichweite um die Spielfigur (m)
int shadowMapSize(int shadowQuality);
double shadowRange(int shadowQuality);

GameSettings loadSettings();
bool saveSettings(const GameSettings& s, std::string* err = nullptr);
std::string defaultPlayerName();     // Windows-Benutzer
