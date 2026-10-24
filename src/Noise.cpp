// Deterministisches Rauschen (siehe Noise.h)
#include "Noise.h"

#include <cmath>

namespace noise {

uint64_t subSeed(uint64_t seed, const std::string& purpose) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a des Zwecks, dann mit dem Seed mischen
    for (unsigned char c : purpose) h = (h ^ c) * 1099511628211ull;
    return mix64(seed ^ mix64(h));
}

namespace {

inline uint64_t hashPoint(uint64_t seed, int64_t x, int64_t z) {
    return mix64(seed ^ mix64((uint64_t)x * 0x9E3779B97F4A7C15ull ^ mix64((uint64_t)z + 0x632BE59BD9B4E019ull)));
}

// Gradient an einem Gitterpunkt: einer von 16 gleichmäßig verteilten Einheitsvektoren (keine Vorzugsrichtung)
inline double dotGrad(uint64_t h, double dx, double dz) {
    static const double kGx[16] = {1.0, 0.9238795325, 0.7071067812, 0.3826834324, 0.0, -0.3826834324, -0.7071067812, -0.9238795325,
                                   -1.0, -0.9238795325, -0.7071067812, -0.3826834324, 0.0, 0.3826834324, 0.7071067812, 0.9238795325};
    const int i = (int)(h >> 60);
    const double gx = kGx[i], gz = kGx[(i + 4) & 15];  // sin = cos um 90° verschoben
    return gx * dx + gz * dz;
}

inline double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

} // namespace

double hash01(uint64_t seed, int64_t x, int64_t z) { return (double)(hashPoint(seed, x, z) >> 11) * (1.0 / 9007199254740992.0); }

double gradient(uint64_t seed, double x, double z) {
    const double fx = std::floor(x), fz = std::floor(z);
    const int64_t ix = (int64_t)fx, iz = (int64_t)fz;
    const double dx = x - fx, dz = z - fz;
    const double n00 = dotGrad(hashPoint(seed, ix, iz), dx, dz);
    const double n10 = dotGrad(hashPoint(seed, ix + 1, iz), dx - 1.0, dz);
    const double n01 = dotGrad(hashPoint(seed, ix, iz + 1), dx, dz - 1.0);
    const double n11 = dotGrad(hashPoint(seed, ix + 1, iz + 1), dx - 1.0, dz - 1.0);
    const double u = fade(dx), v = fade(dz);
    const double a = n00 + u * (n10 - n00), b = n01 + u * (n11 - n01);
    return (a + v * (b - a)) * 1.41421356;  // etwa auf [-1, 1]
}

double fbm(uint64_t seed, double x, double z, int octaves, double persistence, double lacunarity) {
    double sum = 0, amp = 1, norm = 0, f = 1;
    for (int o = 0; o < octaves; ++o) {
        // jede Oktave eigener Seed und leicht verschoben: keine sichtbaren Gittermuster an den Ursprüngen
        sum += amp * gradient(mix64(seed + (uint64_t)o * 0x9E3779B97F4A7C15ull), x * f + o * 17.31, z * f - o * 11.73);
        norm += amp;
        amp *= persistence;
        f *= lacunarity;
    }
    return norm > 0 ? sum / norm : 0.0;
}

} // namespace noise
