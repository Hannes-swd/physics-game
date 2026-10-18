// Einfacher Wavefront-OBJ-Import (z.B. Export aus Blender: Y oben, Einheit Meter).
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "Solid.h"

struct ObjModel {
    std::vector<solid::Vertex> vertices;          // Dreiecke
    std::vector<std::vector<glm::vec3>> groups;   // Punkte je Objekt/Gruppe (für konvexe Kollisionshüllen)
    std::string name;
    std::string error;
};

bool loadObj(const std::filesystem::path& path, ObjModel& out);
