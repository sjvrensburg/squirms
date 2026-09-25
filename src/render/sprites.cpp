#include "sprites.h"
#include "../entities/worm.h"
#include "../entities/projectile.h"
#include "../entities/prop.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

using Entities::ProjectileType;
using Entities::PropType;

namespace Sprites {

namespace {

// Local-to-world transform: rotate by `a` (radians, Y-down so positive is
// clockwise on screen), scale, and optionally mirror X (facing left).
struct Xf {
    Vector2 o;
    float a = 0, s = 1;
    float fx = 1;
    Vector2 p(float x, float y) const {
        x *= fx * s;
        y *= s;
        float c = cosf(a), sn = sinf(a);
        return Vector2{o.x + x * c - y * sn, o.y + x * sn + y * c};
    }
    float r(float v) const { return v * s; }
};

Color shade(Color c, float f) {
    auto ch = [f](unsigned char v) { float r = v * f; return (unsigned char)(r > 255 ? 255 : (r < 0 ? 0 : r)); };
    return Color{ch(c.r), ch(c.g), ch(c.b), c.a};
}
Color lerpC(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

// raylib wants counter-clockwise (on screen) triangles; accept any order.
void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) std::swap(b, c);
    DrawTriangle(a, b, c, col);
}
void quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    tri(a, b, c, col);
    tri(a, c, d, col);
}
void box(const Xf& X, float x0, float y0, float x1, float y1, Color fill, Color outline, float ow = 1.2f) {
    Vector2 a = X.p(x0, y0), b = X.p(x1, y0), c = X.p(x1, y1), d = X.p(x0, y1);
    quad(a, b, c, d, fill);
    if (ow > 0) {
        DrawLineEx(a, b, ow, outline); DrawLineEx(b, c, ow, outline);
        DrawLineEx(c, d, ow, outline); DrawLineEx(d, a, ow, outline);
    }
}
void line(const Xf& X, float x0, float y0, float x1, float y1, float thick, Color c) {
    DrawLineEx(X.p(x0, y0), X.p(x1, y1), X.r(thick), c);
}
void circle(const Xf& X, float x, float y, float r, Color c) { DrawCircleV(X.p(x, y), X.r(r), c); }
void circleOutlined(const Xf& X, float x, float y, float r, Color fill, Color outline) {
    DrawCircleV(X.p(x, y), X.r(r + 1.2f), outline);
    DrawCircleV(X.p(x, y), X.r(r), fill);
}

const Color OUTLINE{40, 24, 30, 255};

// ---- item drawings shared by world sprites and panel icons ----

void drawRocket(const Xf& X) {
    // Pointing along +x.
    tri(X.p(-8, -3), X.p(-12, -6), X.p(-6, -3), Color{70, 80, 60, 255});
    tri(X.p(-8, 3), X.p(-12, 6), X.p(-6, 3), Color{70, 80, 60, 255});
    box(X, -9, -3, 5, 3, Color{110, 128, 80, 255}, OUTLINE);
    tri(X.p(5, -3), X.p(11, 0), X.p(5, 3), Color{220, 50, 40, 255});
    line(X, 5, -3, 11, 0, 1.0f, OUTLINE);
    line(X, 5, 3, 11, 0, 1.0f, OUTLINE);
}

void drawGrenadeBody(const Xf& X, Color body, bool lever) {
    circleOutlined(X, 0, 0, 5, body, OUTLINE);
    circle(X, -1.6f, -1.6f, 1.6f, shade(body, 1.5f));
    if (lever) {
        box(X, -1.5f, -8, 1.5f, -5, Color{160, 160, 170, 255}, OUTLINE, 0.8f);
        line(X, 1.5f, -7, 4.5f, -3, 1.4f, Color{160, 160, 170, 255});
    }
}

void drawBanana(const Xf& X, float scale) {
    // A fat crescent: a chain of circles along an arc, thicker in the middle.
    const int n = 10;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i <= n; i++) {
            float t = (float)i / n;
            float a = PI * (0.15f + 0.7f * t);
            float x = cosf(a) * 7 * scale, y = (sinf(a) * 7 - 5) * scale;
            float r = (1.2f + 2.6f * sinf(t * PI)) * scale;
            if (pass == 0) circle(X, x, y, r + 1.1f, OUTLINE);
            else circle(X, x, y, r, Color{250, 220, 60, 255});
        }
    }
    circle(X, cosf(PI * 0.15f) * 7 * scale, (sinf(PI * 0.15f) * 7 - 5) * scale, 1.3f * scale, Color{110, 70, 30, 255});
    circle(X, cosf(PI * 0.85f) * 7 * scale, (sinf(PI * 0.85f) * 7 - 5) * scale, 1.3f * scale, Color{110, 70, 30, 255});
}

void drawHoly(const Xf& X, float time) {
    circleOutlined(X, 0, 0, 7, Color{240, 196, 60, 255}, OUTLINE);
    circle(X, -2.4f, -2.4f, 2.2f, Color{255, 240, 170, 255});
    line(X, 0, -7, 0, -14, 2.4f, Color{240, 196, 60, 255});
    line(X, -3, -11, 3, -11, 2.4f, Color{240, 196, 60, 255});
    float tw = 0.5f + 0.5f * sinf(time * 8);
    circle(X, 4, -5, 1.0f + tw, Color{255, 255, 255, (unsigned char)(120 + 120 * tw)});
}

void drawDynamite(const Xf& X, float time, bool lit) {
    box(X, -3.5f, -7, 3.5f, 7, Color{210, 40, 40, 255}, OUTLINE);
    box(X, -3.5f, -2, 3.5f, 1, Color{240, 220, 200, 255}, OUTLINE, 0.8f);
    line(X, 0, -7, 2, -11, 1.3f, Color{60, 50, 40, 255});
    if (lit) {
        float f = 1.5f + 1.2f * (0.5f + 0.5f * sinf(time * 40));
        circle(X, 2, -11.5f, f + 1, Color{255, 140, 30, 200});
        circle(X, 2, -11.5f, f, Color{255, 250, 180, 255});
    }
}

void drawMine(const Xf& X, float time, bool active, bool armed) {
    for (int i = 0; i < 6; i++) {
        float a = i * PI / 3 + 0.3f;
        line(X, cosf(a) * 4, sinf(a) * 4, cosf(a) * 7.5f, sinf(a) * 7.5f, 1.6f, Color{50, 50, 58, 255});
    }
    circleOutlined(X, 0, 0, 5, Color{72, 74, 84, 255}, OUTLINE);
    circle(X, -1.5f, -1.5f, 1.5f, Color{130, 132, 146, 255});
    bool on = active ? fmodf(time * 6, 1) < 0.5f : fmodf(time * 1.2f, 1) < 0.25f;
    Color light = !armed ? Color{80, 220, 80, 255} : Color{255, 50, 40, 255};
    circle(X, 0, -4.5f, 1.7f, on ? light : shade(light, 0.4f));
}

void drawSheep(const Xf& X, float time, bool walking) {
    // Legs (animated), wool, face.
    float k = walking ? sinf(time * 18) * 2.0f : 0;
    line(X, -4, 4, -4 - k, 9, 1.8f, Color{30, 30, 30, 255});
    line(X, 4, 4, 4 + k, 9, 1.8f, Color{30, 30, 30, 255});
    const float wool[][3] = {{-5, -1, 4.5f}, {0, -3, 5}, {5, -1, 4.5f}, {-2, 2, 4.5f}, {3, 2, 4.5f}};
    for (auto& w : wool) circle(X, w[0], w[1], w[2] + 1.1f, Color{90, 90, 90, 255});
    for (auto& w : wool) circle(X, w[0], w[1], w[2], Color{250, 250, 244, 255});
    for (auto& w : wool) circle(X, w[0] - 1.2f, w[1] - 1.2f, w[2] * 0.4f, WHITE);
    circle(X, 9, -2, 3.8f, Color{36, 32, 34, 255});
    circle(X, 10.2f, -3.2f, 1.2f, WHITE);
    circle(X, 10.6f, -3.2f, 0.6f, BLACK);
    circle(X, 6.8f, -4.8f, 1.5f, Color{36, 32, 34, 255});
}

void drawShotgun(const Xf& X) {
    box(X, -2, -1.6f, 16, 1.2f, Color{60, 62, 70, 255}, OUTLINE, 0.8f);
    box(X, -9, -1.2f, -1, 3.4f, Color{140, 90, 50, 255}, OUTLINE, 0.8f);
    box(X, 3, 1.2f, 8, 3.2f, Color{140, 90, 50, 255}, OUTLINE, 0.8f);
}

void drawBat(const Xf& X) {
    Vector2 a = X.p(-6, 0), b = X.p(16, 0);
    DrawLineEx(a, b, X.r(3.6f), OUTLINE);
    DrawLineEx(X.p(-5, 0), X.p(4, 0), X.r(2.2f), Color{200, 150, 90, 255});
    DrawLineEx(X.p(4, 0), X.p(15, 0), X.r(3.4f), Color{220, 170, 100, 255});
    circle(X, 15, 0, 2.2f, Color{220, 170, 100, 255});
}

// A coiled rope with a grappling hook poking out of it.
void drawRopeCoil(const Xf& X) {
    const Color ropeDark{70, 44, 22, 255}, rope{206, 160, 92, 255}, ropeLight{240, 206, 140, 255};
    const Color steel{160, 166, 180, 255}, steelDark{50, 52, 62, 255};
    // Spiral coil, squashed as if lying at an angle, plus the free end.
    const int n = 26;
    Vector2 pts[n + 2];
    for (int i = 0; i <= n; i++) {
        float a = 3.5f * PI * i / n;
        float r = 1.3f + 6.6f * i / n;
        pts[i] = Vector2{-2.5f + cosf(a) * r, 3.0f + sinf(a) * r * 0.78f};
    }
    pts[n + 1] = Vector2{5.5f, -5.5f};
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i <= n; i++) {
            Vector2 a = pts[i], b = pts[i + 1];
            if (pass == 0) line(X, a.x, a.y, b.x, b.y, 4.0f, ropeDark);
            else if (pass == 1) line(X, a.x, a.y, b.x, b.y, 2.4f, rope);
            else if (i % 3 == 0) line(X, a.x, a.y, (a.x + b.x) / 2, (a.y + b.y) / 2, 1.0f, ropeLight);
        }
    }
    // Grapnel: shaft, crossbar and barbs curling back.
    line(X, 4.5f, -4.5f, 10.5f, -10.5f, 3.6f, steelDark);
    line(X, 7.4f, -13.6f, 13.6f, -7.4f, 3.6f, steelDark);
    line(X, 7.4f, -13.6f, 5.2f, -10.4f, 3.2f, steelDark);
    line(X, 13.6f, -7.4f, 10.4f, -5.2f, 3.2f, steelDark);
    line(X, 4.5f, -4.5f, 10.5f, -10.5f, 1.8f, steel);
    line(X, 7.4f, -13.6f, 13.6f, -7.4f, 1.8f, steel);
    line(X, 7.4f, -13.6f, 5.2f, -10.4f, 1.6f, steel);
    line(X, 13.6f, -7.4f, 10.4f, -5.2f, 1.6f, steel);
    circle(X, 10.5f, -10.5f, 1.4f, Color{220, 224, 232, 255});
}

void drawPlaneShape(const Xf& X) {
    Color body{150, 160, 176, 255}, dark{90, 98, 112, 255};
    tri(X.p(-16, -2), X.p(-22, -10), X.p(-12, -2), dark);         // tail fin
    box(X, -18, -3, 14, 3, body, OUTLINE, 1.0f);                   // fuselage
    tri(X.p(14, -3), X.p(20, 0), X.p(14, 3), body);                // nose
    quad(X.p(-4, 1), X.p(4, 1), X.p(-2, 12), X.p(-8, 12), dark);    // wing
    circle(X, 8, -1, 1.6f, Color{160, 220, 255, 255});             // cockpit
}

// ---- worms ----------------------------------------------------------------
//
// A worm is drawn in a local frame (x forward, y down, origin at the physics
// capsule centre, feet at y = +14.4) as a tube of overlapping circles swept
// along a cubic Bezier spine, tail on the ground behind, head on top. The
// tube is shaded in passes (outline, shadow, body, back highlight) so it
// reads as round, then gets a team headband, eyes with lids and a mouth.

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float smooth01(float v) { v = clamp01(v); return v * v * (3 - 2 * v); }
float hash01(float x) { float s = sinf(x * 127.1f + 311.7f) * 43758.5453f; return s - floorf(s); }
Vector2 lerpV(Vector2 a, Vector2 b, float t) { return Vector2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
Vector2 norm2(Vector2 v) {
    float l = sqrtf(v.x * v.x + v.y * v.y);
    return l > 1e-5f ? Vector2{v.x / l, v.y / l} : Vector2{1, 0};
}
Vector2 XP(const Xf& X, Vector2 q) { return X.p(q.x, q.y); }
// A filled circle with a segment count to suit its size (worm bodies draw
// ~90 of these each, so the default 36 segments is wasteful).
void blob(const Xf& X, float x, float y, float r, Color c) {
    float rs = X.r(r);
    int segs = std::clamp((int)(rs * 2.4f), 10, 36);
    DrawCircleSector(X.p(x, y), rs, 0, 360, segs, c);
}

// A rotated sub-frame (head, eye) inside the worm's local frame.
struct Fr {
    Vector2 o;
    float a, c, s;
    Fr(Vector2 o_, float a_) : o(o_), a(a_), c(cosf(a_)), s(sinf(a_)) {}
    Vector2 p(float u, float v) const { return Vector2{o.x + c * u - s * v, o.y + s * u + c * v}; }
    Vector2 p(Vector2 q) const { return p(q.x, q.y); }
    Fr sub(float u, float v, float da) const { return Fr(p(u, v), a + da); }
};

constexpr int POLY_MAX = 40;

int ellipsePoly(float ax, float ay, Vector2* out, int segs) {
    for (int i = 0; i < segs; i++) {
        float a = 2 * PI * i / segs;
        out[i] = Vector2{cosf(a) * ax, sinf(a) * ay};
    }
    return segs;
}

// Clip a convex polygon to the half-plane dot(p - o, n) <= 0 (Sutherland-
// Hodgman). `cut` (if given) receives the two ends of the new edge.
int clipHalf(const Vector2* in, int n, Vector2 o, Vector2 nrm, Vector2* out, Vector2* cut = nullptr) {
    int m = 0, ne = 0;
    for (int i = 0; i < n && m < POLY_MAX - 1; i++) {
        Vector2 a = in[i], b = in[(i + 1) % n];
        float da = (a.x - o.x) * nrm.x + (a.y - o.y) * nrm.y;
        float db = (b.x - o.x) * nrm.x + (b.y - o.y) * nrm.y;
        if (da <= 0) out[m++] = a;
        if ((da <= 0) != (db <= 0)) {
            float k = da / (da - db);
            Vector2 q{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k};
            out[m++] = q;
            if (cut && ne < 2) cut[ne] = q;
            ne++;
        }
    }
    return m;
}

// Fill a convex polygon given in frame F.
void fillFr(const Xf& X, const Fr& F, const Vector2* pts, int n, Color col) {
    if (n < 3) return;
    Vector2 p0 = XP(X, F.p(pts[0]));
    Vector2 prev = XP(X, F.p(pts[1]));
    for (int i = 2; i < n; i++) {
        Vector2 cur = XP(X, F.p(pts[i]));
        tri(p0, prev, cur, col);
        prev = cur;
    }
}
void ellipseFr(const Xf& X, const Fr& F, float ax, float ay, Color col, int segs = 18) {
    Vector2 poly[POLY_MAX];
    fillFr(X, F, poly, ellipsePoly(ax, ay, poly, segs), col);
}
void lineFr(const Xf& X, const Fr& F, float u0, float v0, float u1, float v1, float thick, Color c) {
    DrawLineEx(XP(X, F.p(u0, v0)), XP(X, F.p(u1, v1)), X.r(thick), c);
}

// Everything drawWorm needs, decoupled from Entities::Worm so a pose can be
// drawn without a live physics body (handy for previews / pose sheets).
struct WormLook {
    Vector2 pos;
    float scale = 1, facing = 1, aim = 0, spin = 0, walkPhase = 0;
    Vector2 vel{0, 0};         // m/s, Box2D Y-up
    bool blinking = false, flash = false, hurt = false, flying = false;
    bool dying = false, grounded = true, drowned = false;
    float seed = 0;            // per-worm 0..1, desyncs idle animation
};

struct EyeStyle {
    float open;       // 1 = wide open, 0 = shut
    float lidTilt;    // slope of the lid edge (+: lower towards +u)
    Vector2 look;     // pupil direction, eye frame
    float pupilR;
    bool xEyes;
};

void drawEye(const Xf& X, const Fr& F, float ax, float ay, const EyeStyle& st, Color outline, Color lid) {
    const float ow = 1.05f;
    ellipseFr(X, F, ax + ow, ay + ow, outline);
    ellipseFr(X, F, ax, ay, Color{206, 208, 226, 255});
    ellipseFr(X, F.sub(-0.12f * ax, -0.16f * ay, 0), ax * 0.84f, ay * 0.82f, Color{255, 255, 255, 255});
    if (st.xEyes) {
        float k = 0.62f;
        lineFr(X, F, -ax * k, -ay * k, ax * k, ay * k, 1.1f, outline);
        lineFr(X, F, -ax * k, ay * k, ax * k, -ay * k, 1.1f, outline);
        return;
    }
    float lidV = -ay + (1 - st.open) * 2 * ay;
    if (st.open > 0.08f) {
        float pr = st.pupilR;
        Vector2 lk = st.look;
        float l = sqrtf(lk.x * lk.x + lk.y * lk.y);
        if (l > 1) lk = Vector2{lk.x / l, lk.y / l};
        float pu = lk.x * (ax - pr - 0.35f);
        float pv = lk.y * (ay - pr - 0.35f);
        // Keep the pupil under the lid: fully when alert, peeking when sleepy.
        float lidAt = lidV + st.lidTilt * pu;
        pv = std::clamp(pv, std::min(lidAt + pr * (st.open > 0.7f ? 0.8f : 0.35f), ay - pr - 0.35f), ay - pr - 0.35f);
        Vector2 pc = F.p(pu, pv);
        DrawCircleV(XP(X, pc), X.r(pr), Color{26, 14, 22, 255});
        DrawCircleV(X.p(pc.x - pr * 0.4f, pc.y - pr * 0.45f), X.r(pr * 0.4f), WHITE);
    }
    if (st.open < 0.97f) {
        Vector2 poly[POLY_MAX], clip[POLY_MAX], cut[2];
        int n = ellipsePoly(ax + 0.15f, ay + 0.15f, poly, 18);
        n = clipHalf(poly, n, Vector2{0, lidV}, Vector2{-st.lidTilt, 1}, clip, cut);
        fillFr(X, F, clip, n, lid);
        if (st.open > 0.08f && n >= 3) {
            DrawLineEx(XP(X, F.p(cut[0])), XP(X, F.p(cut[1])), X.r(1.1f), outline);
        } else {
            // Shut: a lash line curving down.
            lineFr(X, F, -ax * 0.8f, ay * 0.05f, 0, ay * 0.4f, 1.0f, outline);
            lineFr(X, F, 0, ay * 0.4f, ax * 0.8f, ay * 0.05f, 1.0f, outline);
        }
    }
}

// Team headband tail: a tapering ribbon that flutters.
void bandTail(const Xf& X, Vector2 base, Vector2 dir0, float t, float phase, float flutter, float droop,
              Color fill, Color dark) {
    const int SEG = 5;
    Vector2 pts[SEG + 1];
    pts[0] = base;
    Vector2 d = norm2(dir0);
    for (int j = 1; j <= SEG; j++) {
        float wv = sinf(t * (7.0f + 6.0f * flutter) - j * 1.1f + phase) * (0.25f + 0.45f * flutter) * (0.4f + 0.3f * j);
        Vector2 perp{-d.y, d.x};
        Vector2 dj = norm2(Vector2{d.x + perp.x * wv, d.y + perp.y * wv + droop * j * 0.14f});
        pts[j] = Vector2{pts[j - 1].x + dj.x * 1.7f, pts[j - 1].y + dj.y * 1.7f};
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int j = 0; j < SEG; j++) {
            float th = 1.8f - 0.2f * j;
            if (pass == 0) line(X, pts[j].x, pts[j].y, pts[j + 1].x, pts[j + 1].y, th + 1.3f, dark);
            else line(X, pts[j].x, pts[j].y, pts[j + 1].x, pts[j + 1].y, th, fill);
        }
        if (pass == 0) circle(X, pts[SEG].x, pts[SEG].y, 0.85f, dark);
    }
}

void drawWormLook(const WormLook& L, const WormPose& pose) {
    const float t = pose.time;
    const float ph = L.seed * 2 * PI;
    Xf X{L.pos, L.spin, L.scale, L.facing};

    const bool charging = pose.showAim && pose.power > 0;
    const bool panic = !L.dying && (L.flying || L.drowned || L.hurt);
    const bool rope = pose.onRope && !L.flying;
    const float vx = L.vel.x, vy = L.vel.y;
    const float walkAmt = (L.grounded && !L.flying && !rope) ? smooth01((fabsf(vx) - 0.15f) / 0.5f) : 0.0f;
    const float airAmt = (!L.grounded && !L.flying && !rope && !L.drowned) ? smooth01((fabsf(vy) - 0.5f) / 2.0f) : 0.0f;
    const Vector2 dirL{cosf(L.aim), -sinf(L.aim)};   // local (unmirrored) aim direction

    // ---- colours ----
    Color skin{255, 150, 172, 255};
    Color shadow{220, 96, 132, 255};
    Color light{255, 198, 212, 255};
    Color spec{255, 240, 244, 255};
    Color ringC{236, 124, 154, 255};
    Color outline{92, 20, 50, 255};
    if (L.dying) {
        Color grey{196, 170, 178, 255};
        skin = lerpC(skin, grey, 0.35f); shadow = lerpC(shadow, grey, 0.35f);
        light = lerpC(light, grey, 0.35f); ringC = lerpC(ringC, grey, 0.35f);
    }
    if (L.flash) {
        skin = lerpC(skin, WHITE, 0.6f); shadow = lerpC(shadow, WHITE, 0.5f);
        light = lerpC(light, WHITE, 0.6f); ringC = lerpC(ringC, WHITE, 0.5f);
    }
    Color lidC = lerpC(skin, shadow, 0.25f);
    Color team = pose.teamColor;
    team.a = 255;
    Color teamDark = lerpC(team, Color{20, 10, 20, 255}, 0.6f);
    Color teamShade = lerpC(team, Color{0, 0, 0, 255}, 0.25f);
    Color teamLight = lerpC(team, WHITE, 0.45f);

    // ---- spine ----
    Vector2 P[4] = {{-11.8f, 12.4f}, {6.5f, 11.0f}, {2.5f, 0.0f}, {2.4f, -8.0f}};
    float br = sinf(t * (pose.active ? 3.1f : 2.2f) + ph);
    float breathAmp = pose.active ? 0.85f : 0.5f;
    P[3].y += br * breathAmp;
    P[2].x += br * 0.35f;
    if (pose.active && !pose.showAim) P[3].x += sinf(t * 1.6f + ph) * 0.7f;
    if (walkAmt > 0) {
        // Inch-worm: hunch up to drag the tail in, then stretch the head on.
        const Vector2 hunch[4] = {{-7.0f, 12.4f}, {-3.5f, 2.5f}, {8.0f, 11.0f}, {3.2f, -6.9f}};
        const Vector2 reach[4] = {{-12.5f, 12.6f}, {7.5f, 11.5f}, {5.0f, 0.0f}, {4.8f, -8.2f}};
        float sn = sinf(L.walkPhase);
        float wh = std::max(0.0f, sn) * walkAmt, wr = std::max(0.0f, -sn) * walkAmt;
        for (int i = 0; i < 4; i++) {
            P[i].x += (hunch[i].x - P[i].x) * wh + (reach[i].x - P[i].x) * wr;
            P[i].y += (hunch[i].y - P[i].y) * wh + (reach[i].y - P[i].y) * wr;
        }
    }
    if (pose.showAim) {
        P[3].x += dirL.x * 0.6f;
        P[3].y += dirL.y * 1.0f;
        P[3].y += pose.power * 1.2f;
        P[3].x += sinf(t * 57) * 0.35f * pose.power;
    }
    if (airAmt > 0) {
        const Vector2 riseP[4] = {{-4.2f, 13.6f}, {-1.8f, 7.5f}, {4.6f, 0.5f}, {2.4f, -10.2f}};
        const Vector2 fallP[4] = {{-10.8f, 6.5f}, {-1.5f, 14.0f}, {7.0f, 3.5f}, {3.0f, -7.4f}};
        float rise = smooth01(0.5f + vy / 4.0f);
        for (int i = 0; i < 4; i++) P[i] = lerpV(P[i], lerpV(fallP[i], riseP[i], rise), airAmt);
    }
    if (L.flying || L.drowned) {
        // Flung: a flailing noodle (the tumble itself is w.spin).
        float f = t * (L.drowned ? 14.0f : 22.0f);
        Vector2 fl[4] = {{-2.5f + sinf(f + 1) * 3.0f, 13.5f}, {-5.0f + sinf(f) * 2.2f, 5.0f},
                         {-1.5f, -2.5f}, {2.2f, -9.6f}};
        for (int i = 0; i < 4; i++) P[i] = fl[i];
    } else if (rope) {
        float s = t * 2.6f + ph;
        Vector2 rp[4] = {{sinf(s) * 2.8f, 14.8f}, {sinf(s - 0.9f) * 1.7f, 7.0f}, {0.5f, -1.0f}, {0.8f, -10.4f}};
        for (int i = 0; i < 4; i++) P[i] = rp[i];
    }
    if (L.dying) {
        P[3].x += 1.0f + sinf(t * 2.5f) * 0.6f;
        P[3].y += 1.6f;
        P[2].x += 0.8f;
    }

    const int N = 22;
    Vector2 pts[N + 1];
    float rad[N + 1];
    float len = 0;
    for (int i = 0; i <= N; i++) {
        float s = (float)i / N, u = 1 - s;
        float b0 = u * u * u, b1 = 3 * u * u * s, b2 = 3 * u * s * s, b3 = s * s * s;
        pts[i] = Vector2{b0 * P[0].x + b1 * P[1].x + b2 * P[2].x + b3 * P[3].x,
                         b0 * P[0].y + b1 * P[1].y + b2 * P[2].y + b3 * P[3].y};
        if (i > 0) len += hypotf(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    // Keep the volume: a stretched worm gets thinner, a squashed one fatter.
    const float REST_LEN = 29.8f;
    float thick = std::clamp(sqrtf(REST_LEN / std::max(len, 1.0f)), 0.86f, 1.1f);
    for (int i = 0; i <= N; i++) {
        float s = (float)i / N;
        float r = 1.7f + 3.5f * smooth01(s / 0.3f) + 1.0f * smooth01((s - 0.55f) / 0.45f);
        if (s < 0.75f) r *= 1.0f + 0.03f * br;
        rad[i] = r * thick;
    }
    Vector2 T[N + 1], NB[N + 1];   // tangent (tail -> head) and back-side normal
    for (int i = 0; i <= N; i++) {
        Vector2 a = pts[std::max(i - 1, 0)], b = pts[std::min(i + 1, N)];
        T[i] = norm2(Vector2{b.x - a.x, b.y - a.y});
        NB[i] = Vector2{T[i].y, -T[i].x};
    }

    // ---- head frame and headband geometry ----
    const Vector2 H = pts[N];
    const float rh = rad[N];
    float headRot = 0.08f;
    if (pose.showAim) headRot += -dirL.y * 0.12f;
    if (walkAmt > 0) headRot += -sinf(L.walkPhase) * 0.1f * walkAmt;
    if (L.dying) headRot += 0.25f;
    if (rope) headRot = -0.05f;
    const Fr head(H, headRot);
    const float bandV = -2.7f, bandM = -0.3f, bandW = 3.0f;
    const float knotU = -rh * 0.9f;
    const Vector2 knot = head.p(knotU, bandV + bandM * knotU);

    // Band tails, behind the body.
    {
        float speed = sqrtf(vx * vx + vy * vy);
        float flutter = clamp01(speed / 4.0f + (L.flying ? 1.0f : 0.0f) + (pose.active ? 0.25f : 0.0f));
        float droop = 1.0f - 0.8f * flutter;
        if (rope) droop = 0.4f;
        Vector2 back{-head.c, -head.s};                         // head frame -u
        Vector2 down{-head.s, head.c};                          // head frame +v
        Vector2 d1{back.x + down.x * 0.2f, back.y + down.y * 0.2f};
        Vector2 d2{back.x + down.x * 0.75f, back.y + down.y * 0.75f};
        bandTail(X, knot, d2, t, ph + 1.3f, flutter, droop, teamShade, teamDark);
        bandTail(X, knot, d1, t, ph, flutter, droop, team, teamDark);
    }

    // ---- body tube ----
    const float OW = 1.15f;
    for (int i = 0; i <= N; i++) blob(X, pts[i].x, pts[i].y, rad[i] + OW, outline);
    for (int i = 0; i <= N; i++) blob(X, pts[i].x, pts[i].y, rad[i], shadow);
    for (int i = 0; i <= N; i++)
        blob(X, pts[i].x + NB[i].x * rad[i] * 0.2f, pts[i].y + NB[i].y * rad[i] * 0.2f, rad[i] * 0.78f, skin);
    // Earthworm segment rings across the lower body.
    for (int i = 5; i <= 13; i += 2) {
        float r = rad[i];
        Vector2 a{pts[i].x - NB[i].x * r * 0.5f, pts[i].y - NB[i].y * r * 0.5f};
        Vector2 m{pts[i].x + NB[i].x * r * 0.15f + T[i].x * r * 0.2f, pts[i].y + NB[i].y * r * 0.15f + T[i].y * r * 0.2f};
        Vector2 b{pts[i].x + NB[i].x * r * 0.8f, pts[i].y + NB[i].y * r * 0.8f};
        line(X, a.x, a.y, m.x, m.y, 0.75f, ringC);
        line(X, m.x, m.y, b.x, b.y, 0.75f, ringC);
    }
    for (int i = 2; i <= N; i++)
        blob(X, pts[i].x + NB[i].x * rad[i] * 0.46f, pts[i].y + NB[i].y * rad[i] * 0.46f, rad[i] * 0.3f, light);
    // A glossy glint on the crown.
    circle(X, pts[N].x + NB[N].x * rad[N] * 0.5f + T[N].x * rad[N] * 0.1f,
           pts[N].y + NB[N].y * rad[N] * 0.5f + T[N].y * rad[N] * 0.1f, rad[N] * 0.16f, spec);

    // ---- headband ----
    {
        Vector2 disk[POLY_MAX], a[POLY_MAX], b[POLY_MAX];
        int nd = ellipsePoly(rh + 0.3f, rh + 0.3f, disk, 24);
        auto slab = [&](float v0, float v1, Color col) {
            int n = clipHalf(disk, nd, Vector2{0, v1}, Vector2{-bandM, 1}, a);
            n = clipHalf(a, n, Vector2{0, v0}, Vector2{bandM, -1}, b);
            fillFr(X, head, b, n, col);
        };
        slab(bandV - bandW / 2 - 0.8f, bandV + bandW / 2 + 0.8f, teamDark);
        slab(bandV - bandW / 2, bandV + bandW / 2, team);
        slab(bandV + bandW * 0.2f, bandV + bandW / 2, teamShade);
        slab(bandV - bandW / 2 + 0.35f, bandV - bandW / 2 + 1.1f, teamLight);
        // The knot.
        circle(X, knot.x, knot.y, 1.8f, teamDark);
        circle(X, knot.x, knot.y, 1.1f, team);
    }

    // ---- mouth (under the held weapon, the eyes go on top of it) ----
    {
        Fr m = head.sub(4.0f, 3.3f, 0);
        if (L.dying) {
            lineFr(X, m, -1.6f, 0.3f, -0.5f, -0.2f, 1.0f, outline);
            lineFr(X, m, -0.5f, -0.2f, 0.5f, 0.3f, 1.0f, outline);
            lineFr(X, m, 0.5f, 0.3f, 1.5f, -0.2f, 1.0f, outline);
        } else if (panic) {
            ellipseFr(X, m.sub(0.2f, 0.4f, 0), 1.9f, 2.3f, outline, 14);
            ellipseFr(X, m.sub(0.2f, 0.4f, 0), 1.1f, 1.5f, Color{120, 16, 40, 255}, 12);
            ellipseFr(X, m.sub(0.2f, 1.2f, 0), 0.8f, 0.5f, Color{240, 110, 130, 255}, 10);
        } else if (charging) {
            // Gritted teeth.
            Vector2 q[4] = {{-1.9f, -0.9f}, {1.7f, -1.3f}, {1.8f, 0.9f}, {-1.7f, 1.0f}};
            Vector2 qo[4] = {{-2.7f, -1.7f}, {2.5f, -2.1f}, {2.6f, 1.7f}, {-2.5f, 1.8f}};
            fillFr(X, m, qo, 4, outline);
            fillFr(X, m, q, 4, WHITE);
            lineFr(X, m, -1.8f, 0.0f, 1.75f, -0.2f, 0.6f, Color{150, 120, 130, 255});
            lineFr(X, m, 0.0f, -1.1f, 0.0f, 0.95f, 0.6f, Color{150, 120, 130, 255});
        } else {
            float big = (pose.active || pose.showAim) ? 1.0f : 0.0f;
            lineFr(X, m, -1.9f - big * 0.3f, -0.7f - big * 0.4f, -0.4f, 0.5f + big * 0.2f, 1.2f, outline);
            lineFr(X, m, -0.4f, 0.5f + big * 0.2f, 1.6f, -0.1f - big * 0.3f, 1.2f, outline);
        }
    }

    // ---- held weapon ----
    float aim = L.aim;
    if (pose.holding) {
        // Weapon frame: worm-local, rotated to the aim. Mirroring commutes
        // with rotation as R(spin) M R(-aim) = R(spin - facing*aim) M, so a
        // left-facing worm holds a mirrored (not upside-down) weapon.
        Xf Hx{X.p(4, 0), L.spin - L.facing * aim, L.scale, L.facing};
        switch (pose.weapon) {
            case WeaponId::Bazooka: {
                Xf B = Hx;
                line(B, -8, 0, 15, 0, 7.5f, OUTLINE);
                line(B, -7, 0, 14, 0, 5.5f, Color{110, 128, 80, 255});
                line(B, -7, -1.3f, 12, -1.3f, 1.2f, Color{150, 170, 110, 255});
                line(B, 12, 0, 15, 0, 6.5f, Color{70, 80, 55, 255});
                circle(B, -7, 0, 3, Color{70, 80, 55, 255});
                break;
            }
            case WeaponId::Shotgun: { Xf B = Hx; B.o = Hx.p(-2, 0); drawShotgun(B); break; }
            case WeaponId::BaseballBat: { Xf B = Hx; drawBat(B); break; }
            default: {
                Vector2 hand = Hx.p(7, 0);
                drawWeaponIcon(pose.weapon, hand, 13 * L.scale, t);
                break;
            }
        }
    }

    // ---- eyes ----
    {
        EyeStyle st{};
        st.xEyes = L.dying;
        st.pupilR = 1.55f;
        st.lidTilt = 0;
        if (L.blinking && !panic) st.open = 0;
        else if (panic) { st.open = 1; st.pupilR = 1.0f; }
        else if (charging) { st.open = 0.78f; st.pupilR = 1.6f; }
        else if (pose.showAim || rope) st.open = 0.92f;
        else if (pose.active) st.open = 0.82f;
        else if (walkAmt > 0.5f || !L.grounded) st.open = 0.88f;
        else st.open = 0.56f + 0.06f * sinf(t * 0.7f + ph);

        Vector2 look;
        if (pose.showAim) look = dirL;
        else if (rope) look = Vector2{0.25f, -1};
        else if (panic) {
            float k = floorf(t * 9);
            float a = hash01(k + L.seed * 31) * 2 * PI;
            look = Vector2{cosf(a) * 0.7f, sinf(a) * 0.7f};
        } else if (walkAmt > 0.3f) look = Vector2{1, 0.25f};
        else if (airAmt > 0.3f) look = Vector2{0.7f, vy > 0 ? -0.7f : 0.8f};
        else {
            // Idle: glance around every couple of seconds.
            static const Vector2 dirs[] = {{1, 0.3f}, {0.8f, 0.9f}, {0.3f, -0.9f}, {-0.9f, 0.3f},
                                           {0.1f, 0.5f}, {1, -0.2f}, {0.6f, 0.4f}};
            float slot = floorf(t * 0.42f + L.seed * 5.0f);
            look = dirs[(int)(hash01(slot + L.seed * 17) * 7) % 7];
        }
        // Look is in the worm frame; eyes are rotated slightly within it.
        auto toEye = [&](float rot) {
            float c = cosf(-(headRot + rot)), s = sinf(-(headRot + rot));
            return Vector2{look.x * c - look.y * s, look.x * s + look.y * c};
        };
        float wide = panic ? 1.08f : 1.0f;
        const float farRot = -0.14f, nearRot = 0.14f;
        Fr eFar = head.sub(-0.9f, -4.7f, farRot);
        Fr eNear = head.sub(3.1f, -4.1f, nearRot);
        EyeStyle sFar = st, sNear = st;
        sFar.look = toEye(farRot);
        sNear.look = toEye(nearRot);
        if (charging) { sFar.lidTilt = 0.4f; sNear.lidTilt = -0.4f; }   // determined frown
        drawEye(X, eFar, 2.8f * wide, 3.8f * wide, sFar, outline, lidC);
        drawEye(X, eNear, 3.1f * wide, 4.1f * wide, sNear, outline, lidC);
    }

    // ---- crosshair and power cone ----
    if (pose.showAim) {
        Vector2 c = L.pos;
        Vector2 dirW{L.facing * cosf(aim), -sinf(aim)};
        if (pose.power > 0) {
            float maxD = 14 + pose.power * 78;
            for (float d = 14; d <= maxD; d += 3.5f) {
                float k = (d - 14) / 78.0f;
                Color col = k < 0.5f ? lerpC(Color{255, 240, 60, 255}, Color{255, 150, 30, 255}, k * 2)
                                     : lerpC(Color{255, 150, 30, 255}, Color{230, 30, 30, 255}, (k - 0.5f) * 2);
                DrawCircleV(Vector2{c.x + dirW.x * d, c.y + dirW.y * d}, 1.8f + k * 5.5f, col);
            }
        }
        Vector2 ch{c.x + dirW.x * 62, c.y + dirW.y * 62};
        float pulse = 1.0f + 0.12f * sinf(t * 8);
        DrawRing(ch, 5.0f * pulse, 7.6f * pulse, 0, 360, 24, Color{20, 10, 10, 200});
        DrawRing(ch, 5.6f * pulse, 7.0f * pulse, 0, 360, 24, Color{255, 40, 40, 255});
        DrawLineEx({ch.x - 10, ch.y}, {ch.x - 4, ch.y}, 2, Color{255, 40, 40, 255});
        DrawLineEx({ch.x + 4, ch.y}, {ch.x + 10, ch.y}, 2, Color{255, 40, 40, 255});
        DrawLineEx({ch.x, ch.y - 10}, {ch.x, ch.y - 4}, 2, Color{255, 40, 40, 255});
        DrawLineEx({ch.x, ch.y + 4}, {ch.x, ch.y + 10}, 2, Color{255, 40, 40, 255});
        DrawCircleV(ch, 1.4f, WHITE);
    }
}

} // namespace

// ---------------------------------------------------------------------------

void drawWorm(const Entities::Worm& w, const WormPose& pose) {
    WormLook L;
    L.pos = w.pixelPos();
    L.facing = (float)w.facing;
    L.aim = w.aim;
    L.spin = w.spin;
    L.walkPhase = w.walkPhase;
    L.vel = w.getVelocity();
    L.blinking = w.blink < 0;
    L.hurt = w.hurtTimer > 0;
    L.flash = w.hurtTimer > 0 && fmodf(w.hurtTimer * 10, 1) < 0.5f;
    L.flying = w.flying;
    L.dying = w.dying;
    L.grounded = w.grounded;
    L.drowned = w.drowned;
    L.seed = hash01((float)(((uintptr_t)&w >> 4) & 4095));
    drawWormLook(L, pose);
}

void drawProjectile(const Entities::Projectile& p, float time) {
    Xf X{p.pos, p.angle, 1, 1};
    switch (p.type) {
        case ProjectileType::Bazooka:
        case ProjectileType::AirMissile:
            drawRocket(X);
            break;
        case ProjectileType::Grenade:
            drawGrenadeBody(X, Color{70, 120, 50, 255}, true);
            break;
        case ProjectileType::Cluster:
            drawGrenadeBody(X, Color{190, 70, 50, 255}, true);
            circle(X, 1.5f, 1.5f, 1.1f, Color{255, 220, 120, 255});
            break;
        case ProjectileType::Clusterlet:
            circleOutlined(X, 0, 0, 3, Color{60, 60, 64, 255}, OUTLINE);
            break;
        case ProjectileType::Banana:
            drawBanana(X, 1.1f);
            break;
        case ProjectileType::Bananalet:
            drawBanana(X, 0.85f);
            break;
        case ProjectileType::Holy:
            drawHoly(X, time);
            break;
        case ProjectileType::Dynamite:
            drawDynamite(X, time, true);
            break;
        case ProjectileType::Sheep: {
            Xf S{p.pos, 0, 1, (float)p.facing};
            drawSheep(S, time, true);
            break;
        }
    }
}

void drawProp(const Entities::Prop& p, float time, Color graveColor) {
    Xf X{p.pos, p.angle, 1, 1};
    switch (p.type) {
        case PropType::Barrel: {
            Color red{200, 44, 34, 255};
            box(X, -8.3f, -10.9f, 8.3f, 10.9f, red, OUTLINE, 1.4f);
            box(X, -8.3f, -10.9f, -4.5f, 10.9f, shade(red, 1.25f), red, 0);
            box(X, 4.5f, -10.9f, 8.3f, 10.9f, shade(red, 0.75f), red, 0);
            line(X, -8.3f, -5.5f, 8.3f, -5.5f, 1.6f, shade(red, 0.55f));
            line(X, -8.3f, 5.5f, 8.3f, 5.5f, 1.6f, shade(red, 0.55f));
            circle(X, 0, 0, 3.4f, Color{255, 210, 40, 255});
            circle(X, 0, 0, 1.3f, OUTLINE);
            break;
        }
        case PropType::Mine:
            drawMine(X, time, p.fuse >= 0, p.armTimer <= 0);
            break;
        case PropType::HealthCrate:
        case PropType::WeaponCrate: {
            if (p.parachute) {
                Vector2 top = X.p(0, -36);
                for (int i = 0; i < 6; i++) {
                    float a0 = PI + i * PI / 6, a1 = PI + (i + 1) * PI / 6;
                    tri(top, {top.x + cosf(a0) * 22, top.y + sinf(a0) * 16},
                        {top.x + cosf(a1) * 22, top.y + sinf(a1) * 16},
                        i % 2 ? Color{240, 240, 240, 255} : Color{220, 50, 50, 255});
                }
                DrawLineEx(X.p(-22, -36), X.p(-10, -11), 1, Color{60, 60, 60, 255});
                DrawLineEx(X.p(22, -36), X.p(10, -11), 1, Color{60, 60, 60, 255});
                DrawLineEx(X.p(0, -36), X.p(0, -11), 1, Color{60, 60, 60, 255});
            }
            if (p.type == PropType::HealthCrate) {
                box(X, -10.9f, -10.9f, 10.9f, 10.9f, Color{245, 245, 240, 255}, OUTLINE, 1.4f);
                box(X, -2.5f, -7.5f, 2.5f, 7.5f, Color{220, 30, 40, 255}, Color{0, 0, 0, 0}, 0);
                box(X, -7.5f, -2.5f, 7.5f, 2.5f, Color{220, 30, 40, 255}, Color{0, 0, 0, 0}, 0);
            } else {
                Color wood{190, 128, 66, 255};
                box(X, -10.9f, -10.9f, 10.9f, 10.9f, wood, OUTLINE, 1.4f);
                line(X, -10.9f, -3.6f, 10.9f, -3.6f, 1, shade(wood, 0.6f));
                line(X, -10.9f, 3.6f, 10.9f, 3.6f, 1, shade(wood, 0.6f));
                line(X, -9, -9, 9, 9, 2.2f, shade(wood, 0.7f));
                box(X, -4.5f, -4.5f, 4.5f, 4.5f, Color{250, 220, 70, 255}, OUTLINE, 1.0f);
                circle(X, 0, 0, 1.6f, Color{200, 60, 30, 255});
            }
            break;
        }
        case PropType::Grave: {
            Color stone{176, 176, 186, 255};
            circleOutlined(X, 0, -3, 6, stone, OUTLINE);
            box(X, -6, -3, 6, 9, stone, OUTLINE, 1.2f);
            box(X, -5, -3, 5, 8, stone, stone, 0);
            line(X, 0, -6, 0, 4, 1.8f, Color{90, 90, 100, 255});
            line(X, -3, -3, 3, -3, 1.8f, Color{90, 90, 100, 255});
            box(X, -7, 7.5f, 7, 9.6f, graveColor, OUTLINE, 0.8f);
            break;
        }
    }
}

void drawWeaponIcon(WeaponId id, Vector2 c, float size, float time) {
    float s = size / 26.0f;
    Xf X{c, 0, s, 1};
    switch (id) {
        case WeaponId::Bazooka: {
            Xf B{c, -0.5f, s, 1};
            line(B, -11, 0, 11, 0, 8.5f, OUTLINE);
            line(B, -10, 0, 10, 0, 6.5f, Color{110, 128, 80, 255});
            line(B, 8, 0, 11, 0, 7.5f, Color{70, 80, 55, 255});
            break;
        }
        case WeaponId::Grenade: { Xf G{c, 0.3f, s * 1.6f, 1}; drawGrenadeBody(G, Color{70, 120, 50, 255}, true); break; }
        case WeaponId::ClusterBomb: {
            Xf G{{c.x - 2 * s, c.y}, 0.3f, s * 1.4f, 1};
            drawGrenadeBody(G, Color{190, 70, 50, 255}, true);
            circle(X, 8, 5, 2.2f, Color{60, 60, 64, 255});
            circle(X, 10, -1, 1.8f, Color{60, 60, 64, 255});
            break;
        }
        case WeaponId::BananaBomb: { Xf B{{c.x, c.y + 2 * s}, -0.2f, s * 1.3f, 1}; drawBanana(B, 1.1f); break; }
        case WeaponId::HolyGrenade: { Xf H{{c.x, c.y + 3 * s}, 0, s * 1.2f, 1}; drawHoly(H, time); break; }
        case WeaponId::Dynamite: { Xf D{{c.x, c.y + 2 * s}, 0.25f, s * 1.3f, 1}; drawDynamite(D, time, true); break; }
        case WeaponId::Mine: { Xf M{c, 0, s * 1.5f, 1}; drawMine(M, time, false, true); break; }
        case WeaponId::Sheep: { Xf S{{c.x - 1 * s, c.y - 1 * s}, 0, s * 1.2f, 1}; drawSheep(S, time, false); break; }
        case WeaponId::Shotgun: { Xf G{{c.x - 4 * s, c.y}, -0.35f, s * 1.1f, 1}; drawShotgun(G); break; }
        case WeaponId::BaseballBat: { Xf B{{c.x - 3 * s, c.y + 3 * s}, -0.7f, s * 1.1f, 1}; drawBat(B); break; }
        case WeaponId::NinjaRope: { Xf R{{c.x - 1.5f * s, c.y + 1.5f * s}, 0, s * 1.15f, 1}; drawRopeCoil(R); break; }
        case WeaponId::AirStrike: {
            Xf P{{c.x, c.y - 4 * s}, 0, s * 0.8f, 1};
            drawPlaneShape(P);
            circle(X, -4, 8, 1.8f, Color{50, 50, 50, 255});
            circle(X, 2, 10, 1.8f, Color{50, 50, 50, 255});
            circle(X, 8, 8, 1.8f, Color{50, 50, 50, 255});
            break;
        }
        case WeaponId::Teleport: {
            for (int i = 0; i < 3; i++) {
                float r = 4.0f + i * 3.5f;
                float a = time * 3 + i;
                DrawRing(c, (r - 1.2f) * s, r * s, a * RAD2DEG, a * RAD2DEG + 250, 16,
                         Color{(unsigned char)(120 + i * 40), 210, 255, 255});
            }
            circle(X, 0, 0, 2.2f, WHITE);
            break;
        }
        case WeaponId::SkipGo: {
            for (int i = 0; i < 2; i++) {
                float ox = -5 + i * 8;
                line(X, ox - 3, -7, ox + 3, 0, 3, Color{240, 240, 240, 255});
                line(X, ox + 3, 0, ox - 3, 7, 3, Color{240, 240, 240, 255});
            }
            break;
        }
        default: break;
    }
}

void drawPlane(Vector2 pos, int dir, float time) {
    (void)time;
    Xf X{pos, 0, 2.2f, (float)dir};
    drawPlaneShape(X);
}

} // namespace Sprites
