// Bauvorlagen: Hologramme aus Bauteilen (Quadern), die man im Spiel mit echtem Material füllt. Ist genug Material eines
// Bauteils drin, wird es ein echtes Teil. Siehe docs/20-Bauvorlagen.md.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

class World;

// Welches Material ein Bauteil annimmt
enum class PlanMat : int { Wood = 0, Stone = 1, Any = 2, Metal = 3 };

// Ein Bauteil der Vorlage (lokal zur Vorlage: Ursprung = Mitte unten, y oben, Breite entlang x)
struct PlanPieceDef {
    glm::dvec3 center{0}, half{0};
    double pitchDeg = 0;         // gekippt um die x-Achse (Treppenwangen, Dach)
    glm::dquat rot{1, 0, 0, 0};  // weitere Drehung (eigene Vorlagen), vor dem Kippen
    PlanMat mat = PlanMat::Wood;
};

struct PlanTemplate {
    std::string id;              // in Befehlen (englisch, klein)
    std::string label;           // Anzeige
    std::vector<PlanPieceDef> pieces;
    bool toGround = false;       // Fundament: Bauteile reichen von oben (y = 0) bis in den Boden
    int order = 1000;            // Reihenfolge beim Durchblättern (kleiner = früher), dann nach Name
    bool custom = false;         // selbst erstellt (gespeichert in %APPDATA%\Werkstatt\blueprints.txt)
    // Rezept (Herstellen, docs/22-Herstellen.md): fertig gefüllt wird daraus ein einziges Teil, das ins Inventar kommt
    bool craft = false;
    int mainPiece = -1;          // aus dem Material dieses Bauteils ist das Ergebnis ("main": true; -1 = das größte)
};

// eingebaute und eigene Vorlagen (eigene werden beim ersten Aufruf aus der Datei gelesen)
const std::vector<PlanTemplate>& planTemplates();
// eigene Vorlage anlegen oder ersetzen und in die Datei schreiben. false: Name ist eine eingebaute Vorlage oder Datei-Fehler
bool savePlanTemplate(const PlanTemplate& t, std::string* err);
bool deletePlanTemplate(const std::string& id, std::string* err);
int findPlanTemplate(const std::string& name);  // -1: unbekannt
// Rezepte zum Herstellen: assets/recipes/*.json und %APPDATA%\Werkstatt\recipes\*.json, gleiches Format wie Vorlagen
const std::vector<PlanTemplate>& craftRecipes();
// Gesetzte Rezepte heißen "craft:<id>" (BuildPlan::type), Bauvorlagen nur "<id>"
constexpr const char* kCraftPrefix = "craft:";
const PlanTemplate* planTemplateFor(const std::string& type);  // Vorlage oder Rezept zu BuildPlan::type, nullptr: unbekannt
const char* planMatLabel(PlanMat m);
bool planMatFits(PlanMat m, int material);  // passt das Material (Index in materials()) zu dieser Art?

// Ein Bauteil einer gesetzten Vorlage
struct PlanPiece {
    glm::dvec3 center{0}, half{0};  // lokal zur Vorlage
    glm::dquat rot{1, 0, 0, 0};     // lokal zur Vorlage
    int mat = 0;                    // PlanMat
    double filled = 0;              // schon eingesetztes Material (m³)
    int material = -1;              // Material des ersten eingesetzten Stücks (das Bauteil wird daraus)
    uint32_t built = 0;             // fertig: Nummer des echten Teils (0 = noch Hologramm)
    double volume() const { return 8.0 * half.x * half.y * half.z; }
};
// passt das Material in dieses Bauteil? (Art des Bauteils, und hat es schon Material: dieselbe Art wie das)
bool planPieceFits(const PlanPiece& p, int material);

// Eine gesetzte Vorlage
struct BuildPlan {
    uint32_t id = 0;
    std::string type;                // PlanTemplate::id
    glm::dvec3 pos{0};               // Ursprung der Vorlage (Welt)
    double yaw = 0;                  // Drehung um die Hochachse (rad)
    bool fixed = true;               // true: fertige Bauteile schweben fest und zerbrechen nicht; false: echte Physik
    std::vector<PlanPiece> pieces;
    glm::dquat rot() const { return glm::angleAxis(yaw, glm::dvec3(0, 1, 0)); }
    glm::dvec3 pieceCenter(const PlanPiece& p) const { return pos + rot() * p.center; }
    glm::dquat pieceRot(const PlanPiece& p) const { return rot() * p.rot; }
    bool craft() const { return type.rfind(kCraftPrefix, 0) == 0; }  // Rezept: Bauteile werden nur gefüllt, nicht gebaut
    bool pieceDone(const PlanPiece& p) const { return p.built || (craft() && p.filled >= p.volume() * 0.97); }
    bool complete() const {
        for (const PlanPiece& p : pieces)
            if (!pieceDone(p)) return false;
        return !pieces.empty();
    }
};

// Eigene Vorlage aus Teilen der Welt (jedes Teil wird ein Bauteil in der Größe seines Hüllquaders, Material nach Art:
// Holz, Stein, sonst beliebig). Ursprung = Mitte unten aller Teile. false: keine Teile
bool templateFromParts(World& w, const std::vector<uint32_t>& ids, PlanTemplate& out);
