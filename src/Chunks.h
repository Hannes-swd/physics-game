// Räumliche Aufteilung der Welt in Würfel-Chunks (x, y UND z) -> unendlich in alle Richtungen.
// Dient zum schnellen Finden sichtbarer/naher Dinge (Rendering, LOD, später Terrain und Nachladen).
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

constexpr double kChunkSize = 16.0;  // Kantenlänge eines Chunks in Metern

struct ChunkKey {
    int64_t x = 0, y = 0, z = 0;
    bool operator==(const ChunkKey& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const ChunkKey& o) const { return !(*this == o); }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        uint64_t h = (uint64_t)k.x * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t)k.y * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
        h ^= (uint64_t)k.z * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
        return (size_t)h;
    }
};

struct Chunk {
    ChunkKey key;
    std::vector<uint32_t> objects;      // Teile, deren Mittelpunkt in diesem Chunk liegt
    glm::dvec3 contentMin{0}, contentMax{0};  // Hülle aller Teile (können über den Rand ragen)
    bool boundsDirty = true;
    // Platz für später: Terrain-Daten und Netze je Detailstufe
};

class ChunkGrid {
public:
    static ChunkKey keyOf(const glm::dvec3& p);
    static glm::dvec3 chunkMin(const ChunkKey& k);

    // Teil eintragen/aktualisieren (Weltgrenzen des Teils)
    void place(uint32_t id, const glm::dvec3& mn, const glm::dvec3& mx);
    void remove(uint32_t id);
    void clear();

    // Hülle des Inhalts (wird bei Bedarf neu berechnet)
    void contentBounds(const ChunkKey& k, glm::dvec3& mn, glm::dvec3& mx);
    const std::unordered_map<ChunkKey, Chunk, ChunkKeyHash>& chunks() const { return chunks_; }
    size_t chunkCount() const { return chunks_.size(); }
    bool keyOfObject(uint32_t id, ChunkKey& out) const;

    // Alle belegten Chunks, deren Inhalt die Kugel berührt
    void forEachNear(const glm::dvec3& center, double radius, const std::function<void(Chunk&)>& fn);

private:
    struct Entry {
        ChunkKey key;
        glm::dvec3 mn, mx;
    };
    std::unordered_map<ChunkKey, Chunk, ChunkKeyHash> chunks_;
    std::unordered_map<uint32_t, Entry> entries_;
};
