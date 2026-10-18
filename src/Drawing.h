// Technische Zeichnung: Dreitafelprojektion (DIN ISO 5456-2, Projektionsmethode 1)
// Vorderansicht oben links, Seitenansicht von links rechts daneben, Draufsicht darunter.
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

class World;

enum class LineStyle : int { Visible = 0, Hidden = 1, Center = 2, Thin = 3, Frame = 4 };

struct DLine {
    glm::dvec2 a, b;   // Modellkoordinaten der Ansicht (m)
    LineStyle style;
};

struct DDim {
    glm::dvec2 a, b;        // Messpunkte (m)
    glm::dvec2 normal;      // Richtung, in die die Maßlinie versetzt wird
    double offset;          // Abstand der Maßlinie in Papier-mm
    std::string text;
};

struct DLabel {
    glm::dvec2 p;           // Bezugspunkt (m)
    std::string text;
};

struct DView {
    std::string name;
    std::vector<DLine> lines;
    std::vector<DDim> dims;
    std::vector<DLabel> labels;
    glm::dvec2 min{0}, max{0};
    glm::dvec2 offset{0};   // Lage auf dem Blatt (m, y nach oben)
    int visibleCount = 0, hiddenCount = 0;
};

struct TechDrawing {
    DView views[3];         // 0 = Vorderansicht, 1 = Draufsicht, 2 = Seitenansicht von links
    std::vector<uint32_t> ids;
    std::string title, material;          // Textausgabe (englisch)
    std::string sheetTitle, sheetMaterial;  // Schriftfeld auf dem Blatt (deutsch, wie die Norm)
    double mass = 0;
    glm::dvec3 size{0};
    glm::dvec2 sheetMin{0}, sheetMax{0};
};

// Papier-Elemente in mm (y nach unten), gemeinsam für SVG und Bildschirm
struct SheetLine {
    glm::dvec2 a, b;
    LineStyle style;
};
struct SheetText {
    glm::dvec2 p;           // Mittelpunkt
    std::string text;
    double size;            // Schrifthöhe mm
    bool vertical;
};
struct SheetTri {
    glm::dvec2 a, b, c;
};
struct Sheet {
    double width = 297, height = 210;  // A4 quer
    std::vector<SheetLine> lines;
    std::vector<SheetText> texts;
    std::vector<SheetTri> tris;
    std::string scaleText;
    double scale = 1;       // Papier-mm pro Modell-mm
};

bool makeThreeView(World& w, const std::vector<uint32_t>& ids, TechDrawing& out, std::string* err);
Sheet layoutSheet(const TechDrawing& d);
std::string sheetToSvg(const Sheet& s);
std::string drawingSummary(const TechDrawing& d, const Sheet& s);
