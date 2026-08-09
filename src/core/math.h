#pragma once
#include <stdint.h>

inline float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float smoothstep(float lo, float hi, float x) {
    float t = clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float falloff(float x, float radius) {
    if (x >= radius) return 0.0f;
    float t = x / radius;
    return (1.0f - t) * (1.0f - t);
}

struct Vec2 {
    float x, y;
    Vec2(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
};

inline Vec2 add(const Vec2& a, const Vec2& b) { return Vec2(a.x + b.x, a.y + b.y); }
inline Vec2 sub(const Vec2& a, const Vec2& b) { return Vec2(a.x - b.x, a.y - b.y); }
inline Vec2 scale(const Vec2& v, float s) { return Vec2(v.x * s, v.y * s); }

inline float len(const Vec2& v) { 
    float sum = v.x * v.x + v.y * v.y;
    if (sum == 0.0f) return 0.0f;
    // Simple Newton's method for sqrt
    float x = sum;
    float y = 1.0f;
    for (int i = 0; i < 10; i++) {
        y = (y + x / y) * 0.5f;
    }
    return y;
}

inline Vec2 normalize(const Vec2& v) {
    float l = len(v);
    return l > 0.0f ? Vec2(v.x / l, v.y / l) : Vec2(0, 0);
}

inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline Vec2 rotate(const Vec2& v, float angle) {
    // Approximate sin/cos using small-angle Taylor series for degrees
    double rad = (double)angle * 0.017453292519943295;
    double c = 1.0 - rad*rad/2.0 + rad*rad*rad*rad/24.0;
    double s = rad - rad*rad*rad/6.0 + rad*rad*rad*rad*rad*rad/120.0;
    return Vec2((float)(v.x * c - v.y * s), (float)(v.x * s + v.y * c));
}
