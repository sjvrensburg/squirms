#include "noise.h"
#include "../core/rng.h"
#include <cmath>

namespace Terrain {

uint8_t PERM[512];

static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
static float lerp(float a, float b, float t) { return a + t * (b - a); }
static float grad(int hash, float x, float y) {
    int h = hash & 3;
    return ((h & 1) ? -x : x) + ((h & 2) ? -y : y);
}

void initNoise(int seed) {
    Core::Mulberry32 rng((unsigned int)seed);
    uint8_t p[256];
    for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;
    for (int i = 255; i > 0; i--) {
        int j = (int)(rng() * (i + 1));
        uint8_t temp = p[i]; p[i] = p[j]; p[j] = temp;
    }
    for (int i = 0; i < 512; i++) PERM[i] = p[i & 255];
}

float noise2D(float x, float y) {
    int xi = (int)floorf(x) & 255, yi = (int)floorf(y) & 255;
    float xf = x - floorf(x), yf = y - floorf(y);
    float u = fade(xf), v = fade(yf);
    int aa = PERM[PERM[xi] + yi], ab = PERM[PERM[xi] + yi + 1];
    int ba = PERM[PERM[xi + 1] + yi], bb = PERM[PERM[xi + 1] + yi + 1];
    return lerp(
        lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u),
        lerp(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u),
        v
    ) * 0.5f + 0.5f;
}

float fbm(float x, float y, int octaves, std::function<float()> rng) {
    float value = 0, amplitude = 1, frequency = 1, maxVal = 0;
    for (int i = 0; i < octaves; i++) {
        value += noise2D(x * frequency, y * frequency) * amplitude;
        maxVal += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return value / maxVal;
}

} // namespace Terrain
