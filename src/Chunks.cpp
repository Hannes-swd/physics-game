#include "Chunks.h"

#include <algorithm>
#include <cmath>

ChunkKey ChunkGrid::keyOf(const glm::dvec3& p) {
    return {(int64_t)std::floor(p.x / kChunkSize), (int64_t)std::floor(p.y / kChunkSize),
            (int64_t)std::floor(p.z / kChunkSize)};
}

glm::dvec3 ChunkGrid::chunkMin(const ChunkKey& k) {
    return glm::dvec3((double)k.x, (double)k.y, (double)k.z) * kChunkSize;
}

void ChunkGrid::place(uint32_t id, const glm::dvec3& mn, const glm::dvec3& mx) {
    ChunkKey key = keyOf((mn + mx) * 0.5);
    auto it = entries_.find(id);
    if (it != entries_.end()) {
        if (it->second.key == key) {
            it->second.mn = mn;
            it->second.mx = mx;
            chunks_[key].boundsDirty = true;
            return;
        }
        remove(id);
    }
    entries_[id] = {key, mn, mx};
    Chunk& c = chunks_[key];
    c.key = key;
    c.objects.push_back(id);
    c.boundsDirty = true;
}

void ChunkGrid::remove(uint32_t id) {
    auto it = entries_.find(id);
    if (it == entries_.end()) return;
    auto cit = chunks_.find(it->second.key);
    if (cit != chunks_.end()) {
        auto& v = cit->second.objects;
        v.erase(std::remove(v.begin(), v.end(), id), v.end());
        cit->second.boundsDirty = true;
        if (v.empty()) chunks_.erase(cit);  // leere Chunks freigeben
    }
    entries_.erase(it);
}

void ChunkGrid::clear() {
    chunks_.clear();
    entries_.clear();
}

bool ChunkGrid::keyOfObject(uint32_t id, ChunkKey& out) const {
    auto it = entries_.find(id);
    if (it == entries_.end()) return false;
    out = it->second.key;
    return true;
}

void ChunkGrid::contentBounds(const ChunkKey& k, glm::dvec3& mn, glm::dvec3& mx) {
    auto it = chunks_.find(k);
    if (it == chunks_.end()) {
        mn = chunkMin(k);
        mx = mn + kChunkSize;
        return;
    }
    Chunk& c = it->second;
    if (c.boundsDirty) {
        c.contentMin = glm::dvec3(1e300);
        c.contentMax = glm::dvec3(-1e300);
        for (uint32_t id : c.objects) {
            const Entry& e = entries_[id];
            c.contentMin = glm::min(c.contentMin, e.mn);
            c.contentMax = glm::max(c.contentMax, e.mx);
        }
        c.boundsDirty = false;
    }
    mn = c.contentMin;
    mx = c.contentMax;
}

void ChunkGrid::forEachNear(const glm::dvec3& center, double radius, const std::function<void(Chunk&)>& fn) {
    for (auto& [key, chunk] : chunks_) {
        glm::dvec3 mn, mx;
        contentBounds(key, mn, mx);
        glm::dvec3 q = glm::clamp(center, mn, mx);
        if (glm::length(q - center) <= radius) fn(chunk);
    }
}
