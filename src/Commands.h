// Textbefehle: steuern die ganze Werkstatt (Konsole im Spiel, Skripte, Fernsteuerung, ohne Fenster).
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

class World;

class CommandHost {
public:
    virtual ~CommandHost() = default;
    virtual World& world() = 0;
    virtual glm::dvec3 spawnReference() { return glm::dvec3(0); }
    virtual uint32_t selectedId() const { return 0; }
    virtual std::vector<uint32_t> selectedIds() const { return {}; }
    virtual void select(uint32_t) {}
    virtual void selectMany(const std::vector<uint32_t>&) {}
    virtual void showDrawing(const std::vector<uint32_t>&) {}
    virtual void showInspector(uint32_t) {}
    virtual bool hasWindow() const { return false; }
    virtual std::string setMode(bool play);
    virtual std::string setView(const std::string&) { return "Ansichten gibt es nur mit Fenster"; }
    virtual std::string screenshot(const std::string&) { return "Fotos gibt es nur mit Fenster"; }
    virtual std::string cameraLook(const glm::dvec3&, double) { return "Kamera gibt es nur mit Fenster"; }
    virtual void requestQuit() {}
    // Befehle, die nur das Spiel mit Fenster kennt (spieler, werkzeug, fenster, anzeige).
    // true = erledigt, Ausgabe in out.
    virtual bool appCommand(const std::string&, const std::vector<std::string>&, std::string&) { return false; }
    virtual std::string statusText() { return ""; }
    // Werte für die Befehlssprache (z.B. "spieler.x", "modus", "zeit"). str leer = Zahl.
    virtual bool queryValue(const std::string&, double&, std::string&) { return false; }
    // Zeit vergehen lassen. Ohne Fenster: Physik simulieren. Mit Fenster: Skript pausiert.
    virtual std::string wait(double seconds);
};

struct CommandResult {
    bool ok = true;
    std::string text;
};

CommandResult runCommand(CommandHost& host, const std::string& line);
std::string commandHelp();
// Kleinbuchstaben, Umlaute -> ae/oe/ue/ss
std::string normalizeWord(const std::string& s);
