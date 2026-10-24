// Festkörper-Geometrie: Jeder Körper besteht aus konvexen Teilstücken (Pieces).
// Schneiden (Ebene) und Bohren (konvexes Werkzeug abziehen) bleiben dadurch exakt,
// und die Physik bekommt direkt eine konvexe Zerlegung für Kollisionen.
#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace solid {

using dvec2 = glm::dvec2;
using dvec3 = glm::dvec3;
using dmat4 = glm::dmat4;

// Bark: Mantel eines Baumstamms oder Asts (weiche Normalen wie ein Zylinder, im Bild Rinde; kein exakter Zylinder für die Physik)
enum class Smooth : uint8_t { None = 0, Sphere = 1, Cylinder = 2, Bark = 3 };

// Punkte x mit dot(n, x) - d <= 0 liegen "innen".
// smooth/sc/sa beschreiben die gekrümmte Fläche, zu der die Ebene gehört (für weiche Normalen
// auf Flächen, die beim Schneiden mit dieser Ebene entstehen, z.B. Bohrlochwände).
struct Plane {
    dvec3 n{0, 1, 0};
    double d = 0;
    Smooth smooth = Smooth::None;
    dvec3 sc{0};
    dvec3 sa{0, 1, 0};
    double sr = 0;   // Nennradius der gekrümmten Fläche (m)
};

struct Face {
    dvec3 n{0, 1, 0};          // nach außen zeigende Normale
    double d = 0;              // Ebene: dot(n, x) = d
    std::vector<dvec3> v;      // konvexes Polygon, gegen den Uhrzeigersinn um n
    Smooth smooth = Smooth::None;
    dvec3 sc{0};               // Kugel-/Zylinderzentrum (für weiche Normalen)
    dvec3 sa{0, 1, 0};         // Zylinderachse
    double sr = 0;             // Nennradius (Zylinder/Kugel), für Zeichnungen
    bool internal = false;     // komplett von einem Nachbarstück verdeckt (abgeleitet)
    bool partial = false;      // teilweise verdeckt: nur "vis" ist Oberfläche (abgeleitet)
    std::vector<std::vector<dvec3>> vis;  // sichtbare konvexe Teilpolygone (bei partial)
};

struct Piece {
    std::vector<Face> faces;
};

struct Solid {
    std::vector<Piece> pieces;
};

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    uint32_t color = 0xffffffffu;  // RGBA8 (r im untersten Byte); Gelände: Farbe der Oberfläche; Teile: a = 0 Rinde, sonst weiß
};

// ---------------------------------------------------------------- Erzeugen
Piece fromPlanes(const std::vector<Plane>& planes, double extent);
Piece makeBox(dvec3 halfExtents);
// Prisma entlang Y. poly: Punkte in (x, z), es wird die konvexe Hülle genommen.
Piece makePrism(const std::vector<dvec2>& poly, double y0, double y1);
Solid makeCylinder(double radius, double height, int segments);
Solid makeTube(double rOuter, double rInner, double height, int segments);
Solid makeSphere(double radius, int subdivisions);
// Stirnrad mit Evolventenverzahnung (Eingriffswinkel 20°). Achse = Y.
Solid makeGear(int teeth, double module, double width, double bore);
// Propeller: Nabe + Blätter (verdreht, verjüngt). Achse = Y, Drehung um +Y (rechte Hand) erzeugt Schub nach +Y.
// pitchDeg = Blattwinkel bei 75 % des Radius.
Solid makePropeller(double radius, int blades, double pitchDeg, double chord, double thickness);
// Werkzeugkörper (Bohrer): Zylinder ab 'from' in Richtung 'dir' mit Länge 'length'.
std::vector<Plane> drillTool(dvec3 from, dvec3 dir, double radius, double length, int segments);
// Werkzeugkörper (Säge mit Schnittfuge): Platte um Ebene (n, d) mit Dicke 'kerf'.
std::vector<Plane> slabTool(dvec3 n, double d, double kerf);
// Quader (Fräser) zwischen mn und mx
std::vector<Plane> boxTool(dvec3 mn, dvec3 mx);
// Kegelstumpf (Senker): ab 'from' in Richtung 'dir', Radius r0 am Anfang, r1 nach 'length'
std::vector<Plane> coneTool(dvec3 from, dvec3 dir, double r0, double r1, double length, int segments);

// Konvexes Stück aus einer Punktwolke (Hülle). Für kleine Punktmengen (Biegen, Ausrundungen).
Piece hullPiece(const std::vector<dvec3>& points);
// Ebenen eines Stücks (Normalen nach außen)
std::vector<Plane> planesOf(const Piece& p);

// ---------------------------------------------------------------- Transformation
void transform(Piece& p, const dmat4& m);   // nur starre Transformationen
void transform(Solid& s, const dmat4& m);
void translate(Solid& s, dvec3 t);
// Beliebige affine Abbildung (Skalieren, Spiegeln). Normalen, Umlaufsinn und Rundungsdaten werden angepasst.
void transformAffine(Solid& s, const glm::dmat3& a, dvec3 t = dvec3(0));

// ---------------------------------------------------------------- Abfragen
double volume(const Piece& p, dvec3* centroid = nullptr);
double volume(const Solid& s, dvec3* centroid = nullptr);
void bounds(const Piece& p, dvec3& mn, dvec3& mx);
void bounds(const Solid& s, dvec3& mn, dvec3& mx);
double faceArea(const Face& f);
// Strahl im lokalen Raum. Liefert true + t + Flächennormale beim Eintritt.
bool rayHit(const Piece& p, dvec3 o, dvec3 dir, double& t, dvec3& n);
bool rayHit(const Solid& s, dvec3 o, dvec3 dir, double& t, dvec3& n);
size_t faceCount(const Solid& s);

// ---------------------------------------------------------------- Boolesche Operationen
// Behält den Teil mit dot(n,x) <= d. false, wenn nichts übrig bleibt.
bool clip(const Piece& in, const Plane& pl, Piece& out);
void split(const Solid& s, const Plane& pl, Solid& below, Solid& above);
// Zieht das konvexe Werkzeug (Halbräume, Normalen nach außen) ab.
// presplit: erst den Hüllquader des Werkzeugs abtrennen (schöne Zerlegung um Bohrlöcher).
// Ohne: Ebenen in der günstigsten Reihenfolge, möglichst wenige neue Stücke (Abrunden, Fräsen).
Solid subtract(const Solid& s, const std::vector<Plane>& tool, double* removedVolume = nullptr, bool presplit = true);
// Nur den Teil behalten, der im konvexen Werkzeug liegt
Solid intersect(const Solid& s, const std::vector<Plane>& tool);
// Teilt in zusammenhängende Körper auf.
std::vector<Solid> connectedComponents(const Solid& s);
// Markiert Flächen, die komplett innen liegen (zwischen zwei Teilstücken).
void classifyInternalFaces(Solid& s);

// Berührfläche zweier Körper (Weltlage über ma/mb): gegenüberliegende Außenflächen mit höchstens "gap" Abstand.
// Liefert Fläche (m²), Flächenschwerpunkt und mittlere Normale (aus a heraus, Richtung b) in Weltkoordinaten.
double contactArea(const Solid& a, const dmat4& ma, const Solid& b, const dmat4& mb, double gap, dvec3* centroid, dvec3* normal);

// ---------------------------------------------------------------- Kanten (Abrunden, Fasen)
// Eine Kante der Außenfläche zwischen zwei Flächen mit verschiedener Richtung
struct Edge {
    dvec3 a{0}, b{0};       // Anfang und Ende (lokal)
    dvec3 n1{0}, n2{0};     // Normalen der beiden Flächen (nach außen)
    bool convex = true;     // außen (Material im Winkel < 180°) oder innen (Hohlkehle)
    bool curved = false;    // grenzt an eine gekrümmte Fläche (Bohrloch, Zylinder)
    double angle = 0;       // Winkel zwischen den Normalen (rad)
};
// Alle Kanten mit einem Knick von mindestens minAngleDeg (Innenflächen zählen nicht).
// Reihenfolge stabil (nach Lage), damit Nummern zwischen Anzeige und Befehl gleich bleiben.
std::vector<Edge> findEdges(const Solid& s, double minAngleDeg = 20.0);

// Abrunden (segments >= 2) oder Fasen (segments = 1) einer Kante: Außenkanten verlieren Material, Innenkanten
// (Hohlkehlen) bekommen welches. size = Radius (Abrunden) bzw. Schenkellänge (Fase), in m.
// Liefert für Außenkanten die abzuziehenden, für Innenkanten die hinzuzufügenden konvexen Bereiche.
std::vector<std::vector<Plane>> edgeBlendTools(const Edge& e, double size, int segments);
// Konvexe Bereiche (als Ebenen) zu Stücken machen; bestehendes Material wird ausgespart (keine Überlappung)
Solid addRegions(const Solid& s, const std::vector<std::vector<Plane>>& regions);

// Drehen (Drehbank): zwischen from und to entlang der Achse (Punkt c, Richtung axis) nur noch innerhalb eines
// Kegelstumpfs mit den Radien r0 (bei from) und r1 (bei to) Material lassen.
Solid turn(const Solid& s, dvec3 c, dvec3 axis, double from, double to, double r0, double r1, int segments);

// ---------------------------------------------------------------- Ausgabe
void buildTriangles(const Solid& s, std::vector<Vertex>& out);
void buildEdges(const Solid& s, std::vector<glm::vec3>& lines, double minAngleDeg = 25.0);

} // namespace solid
