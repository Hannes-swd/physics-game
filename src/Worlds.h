// Welten: jede Welt ist ein Ordner unter %APPDATA%\Werkstatt\worlds\
//   world.json        Name, Seed, Generator, erstellt, zuletzt geöffnet, Spielzeit, Kamera/Spielfigur, Umgebung
//   scene.werkstatt   Teile, Verbindungen, Gewässer (Szenenformat, siehe docs/07-Engine-Aufbau.md)
//   *.bak             der Stand vor dem letzten Speichern (wird beim Laden genommen, wenn die Datei fehlt oder kaputt ist)
// Geschrieben wird immer erst in eine .tmp-Datei, die dann die alte ersetzt: ein Absturz beim Speichern zerstört nichts.
// Später kommen hier die Chunks dazu (regions/), der Generator erzeugt den Rest aus dem Seed.
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct WorldInfo {
    std::filesystem::path dir;          // Ordner der Welt
    int format = 1;                     // Aufbau von world.json
    std::string name;
    uint64_t seed = 0;
    std::string seedText;               // so wie eingegeben (leer: zufällig gewählt)
    std::string generator = "flat";     // bis es einen Generator gibt: flache Welt
    int generatorVersion = 1;           // ändert sich der Generator, bleiben alte Welten beim alten
    int64_t created = 0, lastPlayed = 0;  // Unix-Zeit (s)
    double playTime = 0;                // gespielte Zeit (s)
    bool broken = false;                // world.json unlesbar (die Welt lässt sich nur löschen)

    // Ansicht beim letzten Speichern
    struct View {
        bool valid = false;
        bool play = false;              // Spiel-Modus (sonst Bau-Modus)
        glm::dvec3 orbitTarget{0, 0.6, -0.5};
        double orbitYaw = 0.55, orbitPitch = 0.42, orbitDist = 3.4;
        int viewMode = 0;
        glm::dvec3 feet{0};             // Spielfigur
        double yaw = 0, pitch = 0;
    } view;
    // Umgebung (gehört nicht zur Szene, aber zur Welt)
    struct Environment {
        bool valid = false;
        double airDensity = 1.204;
        glm::dvec3 wind{0};
        bool breakage = true, buildCollision = true, heat = true;
        double ambient = 20.0, heatSpeed = 1.0;
    } env;
};

namespace worlds {

std::filesystem::path dataDir();    // %APPDATA%\Werkstatt (wird angelegt)
std::filesystem::path worldsDir();  // dataDir()\worlds
std::filesystem::path sceneFile(const WorldInfo& w);

// Alle Welten, zuletzt geöffnete zuerst
std::vector<WorldInfo> list();
bool readInfo(const std::filesystem::path& dir, WorldInfo& out, std::string* err);
bool writeInfo(const WorldInfo& w, std::string* err);
// Neue Welt: eigener Ordner (aus dem Namen), Seed aus dem Text (Zahl, sonst Hash des Textes, leer: zufällig)
bool create(const std::string& name, const std::string& seedText, WorldInfo& out, std::string* err);
bool rename(const std::filesystem::path& dir, const std::string& name, std::string* err);
bool remove(const std::filesystem::path& dir, std::string* err);  // in den Papierkorb

uint64_t seedFromText(const std::string& text);
std::string cleanName(const std::string& name);  // Leerzeichen am Rand weg, höchstens 64 Zeichen
int64_t now();
std::string formatTime(int64_t t);       // "heute, 14:03", "gestern, 09:12", "24.10.2026, 14:03"
std::string formatDuration(double s);    // "2 h 05 min"

// Datei sicher ersetzen: tmp ist schon geschrieben; die alte Datei wird zu .bak, tmp an ihre Stelle
bool replaceFile(const std::filesystem::path& tmp, const std::filesystem::path& file, std::string* err);
// Datei lesen, bei Fehlen die .bak-Datei
std::filesystem::path existingOrBackup(const std::filesystem::path& file);

} // namespace worlds
