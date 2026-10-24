// Welten: jede Welt ist ein Ordner unter %APPDATA%\Werkstatt\worlds\
//   world.json        Name, Seed, Generator, erstellt, zuletzt geöffnet, Spielzeit, Kamera/Spielfigur, Umgebung
//   world.bin         Teile, Verbindungen, Gewässer, entfernte Bäume (binär, WorldBin.cpp); ältere Welten: scene.werkstatt
//   generator.json, trees.json   Kopien von Biomen und Baumarten (die Welt sieht immer gleich aus)
//   regions/          geänderte Gelände-Chunks
//   *.bak             der Stand vor dem letzten Speichern (wird beim Laden genommen, wenn die Datei fehlt oder kaputt ist)
// Geschrieben wird immer erst in eine .tmp-Datei, die dann die alte ersetzt: ein Absturz beim Speichern zerstört nichts.
// Der Generator erzeugt den Rest aus dem Seed.
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
    std::string generator = "flat";     // "flat": Ebene bei y = 0 (wie früher), "normal": Gelände mit Biomen
    int generatorVersion = 1;           // ändert sich der Generator, bleiben alte Welten beim alten
    std::string biome = "all";          // "normal": alle Biome oder nur dieses (id aus biomes.json)
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
std::filesystem::path sceneFile(const WorldInfo& w);  // alt: Textformat (wird noch gelesen)
std::filesystem::path binFile(const WorldInfo& w);    // world.bin: Teile, Verbindungen, Gewässer (binär)
std::filesystem::path generatorFile(const WorldInfo& w);  // Kopie von biomes.json, mit der die Welt erzeugt wird
std::filesystem::path regionDir(const WorldInfo& w);      // Änderungen am Gelände (regions/)

// Alle Welten, zuletzt geöffnete zuerst
std::vector<WorldInfo> list();
bool readInfo(const std::filesystem::path& dir, WorldInfo& out, std::string* err);
bool writeInfo(const WorldInfo& w, std::string* err);
// Neue Welt: eigener Ordner (aus dem Namen), Seed aus dem Text (Zahl, sonst Hash des Textes, leer: zufällig).
// generator "flat" oder "normal"; biome "all" oder eine id. generatorText: Inhalt von biomes.json (für "normal",
// wird als generator.json in die Welt kopiert)
struct CreateOptions {
    std::string generator = "normal";
    std::string biome = "all";
    std::string generatorText;
};
bool create(const std::string& name, const std::string& seedText, const CreateOptions& options, WorldInfo& out, std::string* err);
std::string randomSeedText();  // zufälliger Seed als Zahl (für das Feld "Seed" beim Erstellen)
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
