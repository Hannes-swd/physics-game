// Kollisionsform eines Teils aus seinen konvexen Stücken.
// Nachbarstücke werden zu größeren Hüllen zusammengefasst (schneller für die Physik), aber nur, wenn die Hülle
// keine Lücke "zuschüttet", die tiefer ist als die Toleranz: Bohrlöcher, Nuten und Taschen bleiben erhalten,
// damit z.B. ein Stift ins Loch passt.
#pragma once
#include <glm/glm.hpp>

#include <vector>

#include "Solid.h"

struct CollisionBuildInfo {
    size_t pieces = 0;        // konvexe Stücke der Geometrie
    size_t hulls = 0;         // Hüllen der Kollisionsform
    double tolerance = 0;     // verwendete Fülltiefe (m)
    size_t bores = 0;         // exakt runde Bohrungen
    size_t cylinders = 0;     // exakte Zylinder
};

// Runde Bohrung (lokal im Teil). Ihre Wand rechnet die Physik exakt rund (BoreShape), die Hüllen ringsum weichen um
// "band" zurück.
struct BoreGeom {
    glm::dvec3 center{0};                 // Mitte der Bohrung auf der Achse
    glm::dvec3 axis{0, 1, 0};             // Achsrichtung (Einheitsvektor)
    glm::dvec3 u{1, 0, 0}, w{0, 0, 1};    // quer zur Achse; edge[k] gilt für die Richtung cos(k·45°)·u + sin(k·45°)·w
    double radius = 0, halfLength = 0;    // Nennradius und halbe Länge (m)
    double band = 0;                      // Breite der Ringzone hinter der Wand (m)
    bool openTop = true, openBottom = true;  // Ende bei +axis / -axis offen (sonst Sackloch-Boden)
    double edge[8] = {};                  // Materialstärke von der Wand bis nach außen je Richtung (m)
};

// Exakter Zylinder (z.B. gedrehter Stift): bekommt in der Physik eine echte Zylinderform statt einer Vieleck-Hülle
struct CylGeom {
    glm::dvec3 center{0};
    glm::dvec3 axis{0, 1, 0};
    double radius = 0, halfLength = 0;
};

struct CollisionShape {
    std::vector<std::vector<glm::vec3>> hulls;
    std::vector<BoreGeom> bores;
    std::vector<CylGeom> cylinders;
};

// mode: 0 automatisch (0,5 mm), 1 genau (0,05 mm), 2 eine einzige Hülle (dann ohne Bohrungen und Zylinder)
CollisionShape buildCollisionShape(const solid::Solid& s, int mode, CollisionBuildInfo* info = nullptr);
std::vector<std::vector<glm::vec3>> buildCollisionHulls(const solid::Solid& s, int mode, CollisionBuildInfo* info = nullptr);

// Runde Bohrungen eines Teils (volle 360°, mindestens Ø 2 mm)
std::vector<BoreGeom> findBores(const solid::Solid& s);

// Wie tief würde die konvexe Hülle der Punkte Lücken zwischen den Stücken auffüllen? (m)
double hullFillDepth(const std::vector<glm::dvec3>& points, const std::vector<solid::Piece>& pieces, double stopAbove);
