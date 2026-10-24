// Einstellungen des Spiels: %APPDATA%\Werkstatt\settings.json. Das Hauptmenü bearbeitet sie, das Spiel liest sie beim
// Öffnen einer Welt (und schreibt beim Verlassen zurück, was man im Spiel umgestellt hat: Ansicht, Name im LAN).
#pragma once
#include <string>

struct GameSettings {
    std::string playerName;          // Name im LAN (leer: Windows-Benutzer)
    double mouseSensitivity = 1.0;   // Faktor auf die Mausbewegung im Spiel-Modus
    double fovDeg = 55.0;            // Sichtfeld (senkrecht)
    bool shadows = true, edges = true, grid = true;
    int autosaveMinutes = 5;         // Welt automatisch speichern (0 = nie)
};

GameSettings loadSettings();
bool saveSettings(const GameSettings& s, std::string* err = nullptr);
std::string defaultPlayerName();     // Windows-Benutzer
