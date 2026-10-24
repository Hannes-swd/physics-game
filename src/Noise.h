// Deterministisches Rauschen für die Weltgenerierung: gleicher Seed -> überall (jeder Rechner, jeder Compiler) dieselben
// Werte. Darum keine Zufallsverteilungen der Standardbibliothek (die sind je nach Compiler verschieden), sondern ein
// eigener Ganzzahl-Hash und daraus Gradientenrauschen. Jeder Wert hängt nur von Seed und Ort ab, nicht von der
// Reihenfolge, in der Chunks erzeugt werden.
#pragma once
#include <cstdint>
#include <string>

namespace noise {

// 64-Bit-Mischfunktion (SplitMix64-Finalisierer)
inline uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
// Seed für einen Zweck ableiten ("temperature", "height/hills" ...): neue Zwecke verschieben die alten nicht
uint64_t subSeed(uint64_t seed, const std::string& purpose);
// Gleichverteilter Wert in [0, 1) für einen Gitterpunkt
double hash01(uint64_t seed, int64_t x, int64_t z);

// Gradientenrauschen (Perlin) in 2D, Wert etwa in [-1, 1], 0 an jedem Gitterpunkt
double gradient(uint64_t seed, double x, double z);
// Summe mehrerer Oktaven, auf [-1, 1] normiert. lacunarity: Frequenz je Oktave, persistence: Amplitude je Oktave
double fbm(uint64_t seed, double x, double z, int octaves, double persistence = 0.5, double lacunarity = 2.0);

} // namespace noise
