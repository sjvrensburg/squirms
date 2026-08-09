#include "math.h"

namespace Core {

float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float smoothstep(float lo, float hi, float x) {
    float t = clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float falloff(float x, float radius) {
    if (x >= radius) return 0.0f;
    float t = x / radius;
    return (1.0f - t) * (1.0f - t);
}

} // namespace Core
