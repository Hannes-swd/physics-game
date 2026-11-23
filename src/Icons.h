#pragma once
// Symbole des Spiels (Stil „Gravur“): Strichzeichnungen im 24er-Raster, mit ImGui gezeichnet, ohne Bilddateien.
// Die Formen stehen als kurze SVG-Pfade in Icons.cpp und werden einmal in Linienzüge zerlegt.
#include <imgui.h>

namespace icons {

enum class Id { Hand, Axe, Laser, Drill, Shovel, Tractor, Pickaxe, Lighter, Rope, Burn, Frost, Cut, Bruise, Break, Count };

// Symbol mit Mitte c und Kantenlänge size (Pixel); stroke ist die Breite des Hauptstrichs in Pixeln
void draw(ImDrawList* dl, Id id, ImVec2 c, float size, ImU32 color, float stroke);
// Wie draw, mit dunklem Schatten darunter (lesbar auf hellem Hintergrund)
void drawShadowed(ImDrawList* dl, Id id, ImVec2 c, float size, ImU32 color, float stroke);
// Wie draw, aber ins Holz graviert: dunkle Kante oben, heller Schein unten
void drawEngraved(ImDrawList* dl, Id id, ImVec2 c, float size, ImU32 color, float stroke);

// Runde Holzmarke (Wald-Stil der Menüs) mit Radius r; gewählt: moosgrüner Ring mit Leuchten
void woodToken(ImDrawList* dl, ImVec2 c, float r, bool selected);
// Holzfarbe in der Mitte der Marke (zum Abdunkeln nicht gewählter Symbole)
ImU32 tokenColor(bool selected);

} // namespace icons
