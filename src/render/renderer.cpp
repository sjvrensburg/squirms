#include "renderer.h"
#include "sprites.h"
#include "text.h"
#include "../game/game.h"
#include "../terrain/grid.h"
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

const GameCamera* gCam = nullptr;
Vector2 toScreen(Vector2 w) { return gCam->worldToScreen(w); }

Color withAlpha(Color c, float a) {
    c.a = (unsigned char)std::clamp(a * 255.0f, 0.0f, 255.0f);
    return c;
}
Color mixC(Color a, Color b, float t) {
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}
float hash1(float n) {
    float s = sinf(n * 127.1f) * 43758.5453f;
    return s - floorf(s);
}

// ---- Sky & backdrop (screen space) ----

void drawSky(const Game& g, int sw, int sh, float t) {
    const Theme& th = *g.theme;
    DrawRectangleGradientV(0, 0, sw, sh, th.skyTop, th.skyBottom);

    float camX = g.camera.x, camY = g.camera.y;
    if (th.stars) {
        for (int i = 0; i < 140; i++) {
            float x = fmodf(hash1(i * 1.3f) * 4000 - camX * 0.04f, (float)sw + 40);
            if (x < 0) x += sw + 40;
            float y = hash1(i * 7.7f) * sh * 0.7f - camY * 0.02f;
            float tw = 0.5f + 0.5f * sinf(t * (1.5f + hash1(i) * 3) + i);
            DrawCircleV({x, y}, 0.8f + hash1(i * 3.1f) * 1.2f, withAlpha(WHITE, 0.3f + 0.6f * tw));
        }
    }

    // Sun / moon, barely moving so it reads as far away.
    Vector2 sun{sw * 0.78f - camX * 0.02f, sh * 0.2f - camY * 0.02f};
    Color disc, glow;
    float r;
    switch (th.weather) {
        case Weather::Leaves: disc = {255, 246, 200, 255}; glow = {255, 240, 170, 60}; r = 34; break;
        case Weather::Dust:   disc = {255, 214, 120, 255}; glow = {255, 180, 90, 70}; r = 70; sun.y += sh * 0.12f; break;
        case Weather::Snow:   disc = {236, 240, 255, 255}; glow = {200, 210, 255, 40}; r = 26; break;
        default:              disc = {230, 70, 40, 255}; glow = {255, 60, 20, 60}; r = 44; break;
    }
    DrawCircleGradient(sun, r * 4.2f, withAlpha(glow, 0.16f), BLANK);
    DrawCircleV(sun, r, disc);
    DrawCircleGradient({sun.x - r * 0.2f, sun.y - r * 0.2f}, r * 0.8f,
                       withAlpha(WHITE, 0.3f), BLANK);

    // Broad shafts of haze sit behind the clouds and hills.
    for (int i = 0; i < 5; ++i) {
        float end = sun.x + (i - 2) * sw * 0.3f + sinf(t * 0.08f) * 30;
        DrawTriangle(sun, {end - sw * 0.11f, (float)sh}, {end + sw * 0.11f, (float)sh},
                     withAlpha(disc, 0.025f));
    }

    // Clouds drifting with the wind.
    bool hell = th.weather == Weather::Embers;
    Color cloud = hell ? mixC(th.skyBottom, Color{60, 20, 20, 255}, 0.8f) : mixC(th.skyBottom, WHITE, 0.88f);
    float span = sw + 400.0f;
    for (int i = 0; i < 9; i++) {
        float base = hash1(i * 3.7f) * span;
        float x = fmodf(base + t * (6 + g.wind * 40) * (0.6f + hash1(i) * 0.8f) - camX * 0.18f, span);
        if (x < 0) x += span;
        x -= 200;
        float y = sh * (0.08f + hash1(i * 5.3f) * 0.3f) - camY * 0.08f;
        float s = 0.7f + hash1(i * 9.1f) * 0.9f;
        for (int k = 0; k < 5; k++) {
            float ox = (k - 2) * 26 * s, oy = -fabsf((float)(k - 2)) * -6 * s - (k % 2) * 10 * s;
            DrawEllipse((int)(x + ox), (int)(y + oy), 34 * s, 20 * s, cloud);
        }
    }

    // A few distant birds, deterministic and purely visual.
    if (th.weather == Weather::Leaves || th.weather == Weather::Dust) {
        for (int i = 0; i < 6; ++i) {
            float x = fmodf(sw * hash1(i + 23) + t * (9 + i) - camX * 0.08f, sw + 80.0f);
            if (x < 0) x += sw + 80;
            float y = sh * (0.18f + hash1(i + 42) * 0.18f) + sinf(t * 0.8f + i) * 9;
            float wing = sinf(t * 4 + i * 2) * 3;
            Color c = withAlpha(th.hillFar, 0.65f);
            DrawLineEx({x - 5, y - wing}, {x, y}, 1.5f, c);
            DrawLineEx({x, y}, {x + 5, y - wing}, 1.5f, c);
        }
    }

    // Two layers of distant hills with parallax.
    for (int layer = 0; layer < 2; layer++) {
        float par = layer == 0 ? 0.15f : 0.35f;
        Color col = layer == 0 ? mixC(th.hillFar, th.skyBottom, 0.25f) : th.hillNear;
        float z = g.camera.zoom;
        float baseY = sh / 2.0f + ((Terrain::WATER_Y - (layer == 0 ? 330 : 190)) - camY) * z * (0.35f + par);
        float amp = layer == 0 ? 150 : 110;
        for (int x = 0; x < sw; x += 4) {
            float wx = (x - sw / 2.0f) + camX * par;
            float h = sinf(wx * 0.0021f + layer * 3) * 0.5f + sinf(wx * 0.0057f + 1.3f + layer) * 0.3f +
                      sinf(wx * 0.013f + layer * 7) * 0.12f;
            // Mountain peaks on the far layer, softer rolling hills up close.
            if (layer == 0) h = fabsf(h) * 1.4f - 0.2f;
            float top = baseY - h * amp;
            if (top < sh) DrawRectangle(x, (int)top, 4, sh - (int)top, col);
        }
    }
}

// ---- Sea (world space, inside BeginMode2D) ----

void drawWater(const Game& g, float yBase, Color top, Color bottom, float alpha, float amp, float phase,
               float t) {
    Vector2 tl = g.camera.screenToWorld({0, 0});
    Vector2 br = g.camera.screenToWorld({(float)GetScreenWidth(), (float)GetScreenHeight()});
    float bottomY = std::max(br.y, yBase + 40) + 20;
    const float step = 6;
    for (float x = floorf(tl.x / step) * step - step; x < br.x + step; x += step) {
        float y = yBase + sinf(x * 0.018f + t * 1.6f + phase) * amp + sinf(x * 0.041f - t * 2.3f + phase) * amp * 0.45f;
        DrawRectangleGradientV((int)x, (int)y, (int)step, (int)(bottomY - y), withAlpha(top, alpha),
                               withAlpha(bottom, alpha));
        DrawRectangle((int)x, (int)y, (int)step, 2, withAlpha(mixC(top, WHITE, 0.5f), alpha));
    }
}

// ---- Weather (screen space) ----

struct Flake { float x, y, s, spin, rot; bool init = false; };
Flake flakes[110];

void drawWeather(const Game& g, int sw, int sh, float dt) {
    const Theme& th = *g.theme;
    for (int i = 0; i < 110; i++) {
        Flake& f = flakes[i];
        if (!f.init) {
            f = {(float)GetRandomValue(0, sw), (float)GetRandomValue(0, sh), 0.6f + GetRandomValue(0, 100) / 100.0f,
                 (float)GetRandomValue(-300, 300) / 100.0f, 0, true};
        }
        float windPush = g.wind * 160.0f;
        float vy = 0, vx = windPush * f.s;
        switch (th.weather) {
            case Weather::Leaves: vy = 38 * f.s; vx += sinf(f.rot) * 20; break;
            case Weather::Snow:   vy = 45 * f.s; vx += sinf(f.rot * 0.7f) * 12; break;
            case Weather::Dust:   vy = 8 * f.s;  vx = windPush * 2.2f * f.s + 30 * (g.wind >= 0 ? 1 : -1); break;
            case Weather::Embers: vy = -40 * f.s; vx += sinf(f.rot) * 15; break;
        }
        f.x += vx * dt;
        f.y += vy * dt;
        f.rot += f.spin * dt;
        if (f.x < -20) f.x += sw + 40;
        if (f.x > sw + 20) f.x -= sw + 40;
        if (f.y > sh + 20) f.y -= sh + 40;
        if (f.y < -20) f.y += sh + 40;

        switch (th.weather) {
            case Weather::Leaves: {
                Color c = i % 3 == 0 ? Color{200, 70, 30, 220} : (i % 3 == 1 ? th.weatherColor : Color{170, 150, 40, 220});
                float w = 4.5f * f.s, h = 2.2f * f.s * (0.4f + 0.6f * fabsf(cosf(f.rot)));
                DrawEllipse((int)f.x, (int)f.y, w, h, c);
                break;
            }
            case Weather::Snow:
                DrawCircleV({f.x, f.y}, 1.2f + f.s * 1.3f, withAlpha(WHITE, 0.85f));
                break;
            case Weather::Dust:
                DrawRectangle((int)f.x, (int)f.y, (int)(3 * f.s + 1), 1, withAlpha(th.weatherColor, 0.5f));
                break;
            case Weather::Embers: {
                float flick = 0.5f + 0.5f * sinf(f.rot * 5);
                DrawCircleV({f.x, f.y}, 1.0f + f.s, withAlpha(th.weatherColor, 0.5f + 0.5f * flick));
                break;
            }
        }
    }
}

// ---- Labels ----

void drawWormLabels(const Game& g) {
    for (auto* w : g.worms) {
        if (!w->alive || w->drowned) continue;
        Color team = g.teams[w->teamIndex].color;
        Vector2 p = w->pixelPos();
        Vector2 s = g.camera.worldToScreen({p.x, p.y - 22});
        float size = 14;
        char hp[16];
        snprintf(hp, sizeof hp, "%d", (int)std::ceil(std::max(0.0f, w->shownHp)));
        Vector2 hm = Text::measure(hp, size);
        Vector2 nm = Text::measure(w->name.c_str(), size);
        float y = s.y - hm.y - 4;
        Rectangle hb{roundf(s.x - hm.x / 2 - 5), roundf(y), hm.x + 10, hm.y + 1};
        DrawRectangleRounded(hb, 0.35f, 4, Color{12, 10, 16, 190});
        Text::draw(hp, {hb.x + 5, hb.y + 1}, size, team);
        Rectangle nb{roundf(s.x - nm.x / 2 - 5), roundf(y - nm.y - 3), nm.x + 10, nm.y + 1};
        DrawRectangleRounded(nb, 0.35f, 4, Color{12, 10, 16, 190});
        Text::draw(w->name.c_str(), {nb.x + 5, nb.y + 1}, size, team);

        // Bouncing arrow over whoever's turn it is until they get going.
        if (w == g.current && (g.phase == Phase::Intro || (g.phase == Phase::Aiming && g.phaseTime < 3.0f))) {
            float b = fabsf(sinf(g.matchTime * 6)) * 8;
            float ay = nb.y - 10 - b;
            Vector2 a{s.x, ay + 10}, l{s.x - 9, ay - 2}, r{s.x + 9, ay - 2};
            DrawTriangle(l, a, r, Color{20, 10, 10, 220});
            DrawTriangle({l.x + 2.5f, l.y + 1.5f}, {a.x, a.y - 3}, {r.x - 2.5f, r.y + 1.5f}, team);
        }
    }
}

} // namespace

void renderWorld(Game& g) {
    gCam = &g.camera;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    float t = g.matchTime;
    const Theme& th = *g.theme;

    drawSky(g, sw, sh, t);

    BeginMode2D(g.camera.camera2D());

    // Far swell of the sea, behind the land.
    drawWater(g, Terrain::WATER_Y - 14, mixC(th.waterTop, th.waterDeep, 0.35f), th.waterDeep, 1.0f, 3.0f, 1.7f, t);

    g.terrainGfx.drawStatic();
    g.terrainGfx.drawLoose(*g.terrain);

    for (auto& p : g.props) {
        Color gc = p.contents >= 0 && p.contents < (int)g.teams.size() ? g.teams[p.contents].color : GRAY;
        Sprites::drawProp(p, t, gc);
    }

    // Ninja rope: hook, pivots, and the line down to the worm.
    if (g.rope.state != NinjaRope::State::Idle && g.current) {
        Vector2 w = g.current->pixelPos();
        std::vector<Vector2> pts;
        if (g.rope.state == NinjaRope::State::Shooting) {
            pts = {{w.x + g.rope.dir.x * g.rope.shot, w.y + g.rope.dir.y * g.rope.shot}};
        } else {
            pts = g.rope.pivots;
        }
        // The rope ends at the worm's head (the sprite hangs rotated by spin).
        float sp = g.current->spin;
        pts.push_back({w.x + 11.0f * sinf(sp), w.y - 11.0f * cosf(sp)});
        for (size_t i = 0; i + 1 < pts.size(); i++) {
            DrawLineEx(pts[i], pts[i + 1], 3.2f, Color{60, 40, 24, 255});
            DrawLineEx(pts[i], pts[i + 1], 1.6f, Color{190, 150, 90, 255});
        }
        for (size_t i = 1; i + 1 < pts.size(); i++) DrawCircleV(pts[i], 2.0f, Color{60, 40, 24, 255});
        // The hook: a little grey grapnel at the far end.
        Vector2 h = pts[0];
        Vector2 d = pts.size() > 1 ? Vector2{pts[1].x - h.x, pts[1].y - h.y} : Vector2{0, 1};
        float dl = sqrtf(d.x * d.x + d.y * d.y);
        if (dl > 0.01f) { d.x /= dl; d.y /= dl; }
        Vector2 n{-d.y, d.x};
        Color steel{150, 156, 170, 255};
        DrawLineEx({h.x - n.x * 4, h.y - n.y * 4}, {h.x + n.x * 4, h.y + n.y * 4}, 2.2f, steel);
        DrawLineEx({h.x - n.x * 4, h.y - n.y * 4}, {h.x - n.x * 5 + d.x * 3, h.y - n.y * 5 + d.y * 3}, 2.0f, steel);
        DrawLineEx({h.x + n.x * 4, h.y + n.y * 4}, {h.x + n.x * 5 + d.x * 3, h.y + n.y * 5 + d.y * 3}, 2.0f, steel);
        DrawCircleV(h, 2.2f, Color{90, 94, 104, 255});
    }

    const WeaponInfo& info = weaponInfo(g.weapon);
    bool sheepOut = false;
    for (auto& p : g.projectiles) if (p.type == Entities::ProjectileType::Sheep) sheepOut = true;
    for (auto* w : g.worms) {
        if (!w->alive) continue;
        Sprites::WormPose pose{};
        pose.time = t + w->teamIndex * 0.37f;
        pose.teamColor = g.teams[w->teamIndex].color;
        bool roped = w == g.current && g.rope.state == NinjaRope::State::Attached;
        bool active = w == g.current && g.phase == Phase::Aiming && g.shotsLeft > 0 && !sheepOut &&
                      w->canAct() && (w->grounded || roped) && !w->flying;
        pose.onRope = roped;
        pose.active = w == g.current && (g.phase == Phase::Aiming || g.phase == Phase::Retreat);
        // Weapons are put away while walking so the inch-worm shuffle shows.
        Vector2 v = w->getVelocity();
        bool walking = w->grounded && fabsf(v.x) > 0.3f && !g.charging;
        pose.holding = active && !walking && info.mode != FireMode::Skip && info.mode != FireMode::Targeted &&
                       !(roped && info.mode == FireMode::Rope);
        pose.weapon = g.weapon;
        pose.showAim = active && !walking && info.aims && !g.panelOpen && !(roped && info.mode == FireMode::Rope) &&
                       g.rope.state != NinjaRope::State::Shooting;
        pose.power = g.charging ? g.power : 0;
        Vector2 wp = w->pixelPos();
        if (w->grounded && !w->drowned) {
            DrawEllipse((int)wp.x, (int)(wp.y + 6), 13, 3.5f, Color{8, 10, 20, 85});
            if (pose.active) DrawEllipseLines((int)wp.x, (int)(wp.y + 6), 17, 5, withAlpha(pose.teamColor, 0.65f));
        }
        Sprites::drawWorm(*w, pose);
    }

    for (auto& p : g.projectiles) Sprites::drawProjectile(p, t);
    if (g.planeX > -1e8f) Sprites::drawPlane({g.planeX, g.planeY}, g.planeDir, t);

    g.fx.drawWorld();

    // Near waves, in front of everything.
    drawWater(g, Terrain::WATER_Y, th.waterTop, th.waterDeep, 0.78f, 4.0f, 0.0f, t);
    drawWater(g, Terrain::WATER_Y + 12, mixC(th.waterTop, th.waterDeep, 0.3f), th.waterDeep, 0.85f, 3.0f, 2.9f, t);

    // Broken highlights and foam give the water depth without extra textures.
    Vector2 waterTL = g.camera.screenToWorld({0, 0});
    Vector2 waterBR = g.camera.screenToWorld({(float)sw, (float)sh});
    for (int i = 0; i < 90; ++i) {
        float span = waterBR.x - waterTL.x + 60;
        float x = waterTL.x + fmodf(hash1(i + 17) * span + t * (5 + g.wind * 8) + span * 100, span) - 30;
        float depth = hash1(i + 73) * 160;
        float y = Terrain::WATER_Y + 8 + depth + sinf(t * 1.8f + i) * 3;
        float shimmer = 0.06f + 0.14f * std::pow(0.5f + 0.5f * sinf(t * 2.2f + i * 3), 3);
        DrawLineEx({x, y}, {x + 5 + depth * 0.13f, y}, 1.3f,
                   withAlpha(mixC(th.waterTop, WHITE, 0.75f), shimmer));
    }

    // Targeting cursor for air strikes / teleport.
    if (g.isTargeting()) {
        Vector2 m = g.mouseWorld;
        float pulse = 1 + 0.15f * sinf(t * 8);
        if (g.weapon == WeaponId::AirStrike) {
            DrawRing(m, 12 * pulse, 15 * pulse, 0, 360, 32, Color{255, 40, 40, 230});
            DrawLineEx({m.x - 22, m.y}, {m.x + 22, m.y}, 2, Color{255, 40, 40, 230});
            DrawLineEx({m.x, m.y - 22}, {m.x, m.y + 22}, 2, Color{255, 40, 40, 230});
            float d = (float)(g.current ? g.current->facing : 1);
            for (int k = 0; k < 3; k++) {
                float ax = m.x - d * (60 - k * 14);
                DrawTriangle({ax, m.y - 70}, {ax + d * 10, m.y - 62}, {ax, m.y - 54}, Color{255, 255, 255, 200});
            }
        } else {
            DrawEllipseLines((int)m.x, (int)m.y, 10 * pulse, 15 * pulse, Color{140, 220, 255, 255});
            DrawEllipseLines((int)m.x, (int)m.y, 12 * pulse, 17 * pulse, Color{140, 220, 255, 140});
        }
    }

    EndMode2D();

    drawWeather(g, sw, sh, GetFrameTime());
    // Soft edge shading leaves the world legible and the HUD crisp.
    DrawRectangleGradientV(0, 0, sw, 80, Color{8, 12, 25, 45}, BLANK);
    DrawRectangleGradientV(0, sh - 100, sw, 100, BLANK, Color{8, 12, 25, 65});
    drawWormLabels(g);
    g.fx.drawText(toScreen, g.camera.zoom);
}
