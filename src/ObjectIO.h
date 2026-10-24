// Teile und Verbindungen binär (LAN und Weltdatei world.bin), siehe ObjectIO.cpp
#pragma once
#include <string>
#include <vector>

#include "Net.h"
#include "World.h"

// Anzahl lesen und prüfen, dass die Daten dafür reichen (each = Mindestgröße je Eintrag)
uint32_t readCount(net::Reader& r, size_t each);
void writeHulls(net::Writer& w, const std::vector<std::vector<glm::vec3>>& hulls);
std::vector<std::vector<glm::vec3>> readHulls(net::Reader& r);
void writePoints(net::Writer& w, const std::vector<glm::dvec3>& pts);
std::vector<glm::dvec3> readPoints(net::Reader& r);
// file: für die Weltdatei. Dann auch Temperatur, Schmelze und Baum-Zustand, und die Kollisionsform nur, wenn sie fertig ist
void writeObject(net::Writer& w, const Object& o, bool file = false);
void writeCollision(net::Writer& w, const CollisionShape& c);
bool readObject(net::Reader& r, ObjectState& s, bool file = false);
void writeJoint(net::Writer& w, const JointState& j);
JointState readJoint(net::Reader& r);
