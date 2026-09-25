#include "renderer.h"
#include "../game/camera.h"
#include "../entities/worm.h"
#include "../entities/explosion.h"
#include "../entities/projectile.h"
#include "../terrain/terrain.h"
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// Parse a "#rrggbb" (or "rrggbb") string into a raylib Color. Each channel is
// exactly two hex digits: strtol would otherwise consume the whole remaining
// string, so parse one byte at a time here.
static Color parseHexColor(const std::string& hex) {
    const char* p = hex.c_str();
    if (*p == '#') ++p;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    int r = (nibble(p[0]) << 4) | nibble(p[1]);
    int g = (nibble(p[2]) << 4) | nibble(p[3]);
    int b = (nibble(p[4]) << 4) | nibble(p[5]);
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
}

// Fill the whole screen with a vertical sky gradient (lighter at the horizon,
// deeper blue up high). This replaces the old flat DARKGRAY clear so the world
// has a backdrop that is clearly distinct from every terrain material.
static void drawSky(int screenWidth, int screenHeight) {
    const Color top   = (Color){ 107, 183, 227, 255 };  // #6FB7E3
    const Color bottom = (Color){ 209, 238, 247, 255 }; // #D1EEF7
    for (int y = 0; y < screenHeight; y += 3) {
        float t = (float)y / (float)(screenHeight > 3 ? screenHeight - 1 : 1);
        Color c;
        c.r = (unsigned char)(top.r + (bottom.r - top.r) * t);
        c.g = (unsigned char)(top.g + (bottom.g - top.g) * t);
        c.b = (unsigned char)(top.b + (bottom.b - top.b) * t);
        c.a = 255;
        DrawRectangle(0, y, screenWidth, 3, c);
    }
}

// True if every screen-space vertex of the quad lies outside the screen
// (allowing a small margin), so the chunk can be skipped entirely.
static bool chunkOffScreen(const std::vector<Vector2>& screenVerts,
                           int w, int h) {
    const int margin = 64;
    bool anyX = false, anyY = false;
    for (const auto& v : screenVerts) {
        if (v.x >= -margin && v.x <= w + margin) anyX = true;
        if (v.y >= -margin && v.y <= h + margin) anyY = true;
    }
    return !(anyX && anyY);
}

// Draw a convex quad filled, with a subtle darker seam so neighbouring chunks
// read as separate slabs rather than one flat slab. Verts are already in
// screen space and ordered around the perimeter (top-left, top-right,
// bottom-right, bottom-left, plus any jitter). We split the quad into two
// triangles in perimeter order; raylib's primitive path has no face culling
// enabled, so both windings render -- but keeping the natural order avoids any
// degenerate fan-centre artefact and guarantees every chunk appears.
static void drawFilledQuad(const std::vector<Vector2>& screenVerts, Color fill,
                           Color seam) {
    if (screenVerts.size() < 3) return;
    // Fan from the first vertex covers a convex quad without an extra centre
    // point; DrawTriangleFan tolerates any convex polygon.
    DrawTriangleFan(screenVerts.data(), (int)screenVerts.size(), fill);
    for (size_t i = 1; i < screenVerts.size(); ++i) {
        DrawLineEx(screenVerts[i - 1], screenVerts[i], 1.0f, seam);
    }
    DrawLineEx(screenVerts.back(), screenVerts.front(), 1.0f, seam);
}

static void drawTerrainChunk(const Terrain::Chunk* chunk, const GameCamera& camera,
                             int w, int h, bool debugMode) {
    if (chunk->state == Terrain::ChunkState::GONE) return;

    // Material colour, darkened a touch for bedrock so it reads as
    // indestructible (darker + a cross-hatch) rather than just "another gray".
    Color fill = parseHexColor(getMaterialProps(chunk->material).color);
    Color seam = (Color){ fill.r, fill.g, fill.b, 160 };
    bool isBedrock = chunk->material == Terrain::Material::Bedrock;
    if (isBedrock) {
        fill = (Color){ (unsigned char)(fill.r * 0.7f),
                        (unsigned char)(fill.g * 0.7f),
                        (unsigned char)(fill.b * 0.8f), 255 };
    }

    Vector2 screenCentroid = camera.worldToScreen((Vector2){chunk->centroidX, chunk->centroidY});

    std::vector<Vector2> screenVerts;
    if (chunk->state == Terrain::ChunkState::DYNAMIC && chunk->body) {
        // Verts are stored in pixel space relative to the slab origin;
        // rotate them about that origin, offset by the body's current
        // pixel position, and un-flip Box2D's Y-up metres back to
        // pixel space -- the same convention projectile.cpp uses.
        float angle = chunk->bodyAngle;
        float cxM = chunk->bodyDrawX / Terrain::PPM;
        // bodyDrawY is already pixel space, so un-flip to Box2D metres once here.
        float cyM = Terrain::WORLD_H - chunk->bodyDrawY / Terrain::PPM;
        for (auto& v : chunk->verts) {
            float lx = (v.first - chunk->bodyOriginX) / Terrain::PPM;
            float ly = -(v.second - chunk->bodyOriginY) / Terrain::PPM;
            float rx = lx * cosf(angle) - ly * sinf(angle);
            float ry = lx * sinf(angle) + ly * cosf(angle);
            float px = (cxM + rx) * Terrain::PPM;
            float py = (Terrain::WORLD_H - (cyM + ry)) * Terrain::PPM;
            screenVerts.push_back(camera.worldToScreen((Vector2){px, py}));
        }
    } else {
        for (auto& v : chunk->verts) {
            Vector2 worldPos = {(float)v.first, (float)v.second};
            screenVerts.push_back(camera.worldToScreen(worldPos));
        }
    }

    if (screenVerts.size() < 3) return;
    if (chunkOffScreen(screenVerts, w, h)) return;

    // Falling (DYNAMIC) chunks are drawn in their normal material colour now;
    // keep a subtle brighter rim only while the F1 debug overlay is on.
    drawFilledQuad(screenVerts, fill, seam);
    if (chunk->state == Terrain::ChunkState::DYNAMIC && debugMode) {
        float pulse = 160 + 80 * (0.5f + 0.5f * sinf(GetTime() * 6.0f));
        Color rim = (Color){ 255, 240, 140, (unsigned char)pulse };
        for (size_t i = 1; i < screenVerts.size(); ++i) {
            DrawLineEx(screenVerts[i - 1], screenVerts[i], 2.0f, rim);
        }
        DrawLineEx(screenVerts.back(), screenVerts.front(), 2.0f, rim);
    }

    // Bedrock cross-hatch: a few faint diagonal strokes so it is unmistakably
    // solid/indestructible at a glance.
    if (isBedrock) {
        float minX = screenVerts[0].x, minY = screenVerts[0].y;
        float maxX = screenVerts[0].x, maxY = screenVerts[0].y;
        for (const auto& v : screenVerts) {
            minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
            minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
        }
        Color hatch = (Color){ 20, 20, 40, 150 };
        for (float d = minX - (maxX - minX); d < maxX; d += 10.0f) {
            DrawLineEx((Vector2){d, minY + (d - minX) * 0.1f},
                       (Vector2){d + 8, minY + (d - minX) * 0.1f + 10}, 1.0f, hatch);
        }
    }
}

// Draw an upward-pointing arrow that bounces above the active worm, so the
// player can see at a glance whose turn it is.
static void drawTurnArrow(const Vector2& screenPos) {
    float bounce = sinf(GetTime() * 6.0f) * 8.0f;
    float baseY = screenPos.y - 46 - bounce;
    float alpha = 200 + 55 * (0.5f + 0.5f * sinf(GetTime() * 3.0f));
    Color c = (Color){ 255, 244, 150, (unsigned char)alpha };
    // Upward triangle (apex at top).
    Vector2 apex = (Vector2){ screenPos.x, baseY - 12 };
    Vector2 bl = (Vector2){ screenPos.x - 8, baseY + 2 };
    Vector2 br = (Vector2){ screenPos.x + 8, baseY + 2 };
    DrawTriangle(apex, bl, br, c);
    // Short stem.
    DrawLineEx((Vector2){ screenPos.x, baseY + 2 },
               (Vector2){ screenPos.x, baseY + 12 }, 3.0f, c);
}

void render(const GameCamera& camera, const ChunkGrid& chunks, 
            const std::vector<Entities::Worm*>& worms,
            const std::vector<Entities::Explosion>& explosions,
            const std::vector<Entities::Projectile>& projectiles,
            int screenWidth, int screenHeight,
            const Entities::Worm* currentWorm, bool debugMode, bool matchRunning) {
    (void)screenWidth; (void)screenHeight;

    // Sky backdrop (replaces the old flat DARKGRAY clear).
    drawSky(screenWidth, screenHeight);

    // Draw terrain chunks, filled in material colour.
    for (const auto& row : chunks) {
        for (auto* chunk : row) {
            if (!chunk || chunk->state == Terrain::ChunkState::GONE) continue;
            drawTerrainChunk(chunk, camera, screenWidth, screenHeight, debugMode);
        }
    }

    // Draw worms
    for (auto* worm : worms) {
        if (!worm || !worm->isAlive()) continue;

        Vector2 pos = worm->getPosition();
        Vector2 screenPos = camera.worldToScreen((Vector2){pos.x * Terrain::PPM, pos.y * Terrain::PPM});

        Color body = parseHexColor(worm->color);

        // Draw worm body (ellipse). The radii must match the physics box built
        // in Worm::init(): that body is 1.0m wide x 2.0m tall, i.e. 32px x 64px
        // at Terrain::PPM (half = 16px x 32px). Drawing a smaller ellipse here
        // left the visible bottom ~24px above the feet that actually rest on the
        // ground, so the worm looked like it was hovering.
        DrawEllipse(screenPos.x, screenPos.y, 16, 32, ColorAlpha(body, 0.95f));
        DrawEllipseLines(screenPos.x, screenPos.y, 16, 32, ColorAlpha((Color){0,0,0,120}, 1));

        // Eyes sit on the side it faces (facing is +1 right / -1 left).
        float ex = screenPos.x + (float)worm->facing * 6;
        float ey = screenPos.y - 10;
        DrawCircle(ex - 3, ey, 3, WHITE);
        DrawCircle(ex + 3, ey, 3, WHITE);
        DrawCircle(ex - 3 + worm->facing, ey, 1.5f, BLACK);
        DrawCircle(ex + 3 + worm->facing, ey, 1.5f, BLACK);

        // Turn marker: a bouncing arrow above the active worm, drawn only
        // while the match is actually running (not at game over / menu).
        if (matchRunning && currentWorm == worm) {
            drawTurnArrow(screenPos);
        }

        // Draw health bar + HP number.
        float hpPercent = worm->hp / worm->maxHp;
        int barWidth = 28;
        int barHeight = 5;
        int barX = (int)screenPos.x - barWidth/2;
        int barY = (int)screenPos.y - 44;

        DrawRectangle(barX, barY, barWidth, barHeight, (Color){0,0,0,160});
        DrawRectangle(barX + 1, barY + 1, barWidth - 2, barHeight - 2,
                      hpPercent > 0.5f ? GREEN : (hpPercent > 0.25f ? YELLOW : RED));

        char hpText[16];
        snprintf(hpText, sizeof(hpText), "%d/%d", (int)worm->hp, (int)worm->maxHp);
        int textWidth = MeasureText(hpText, 10);
        DrawText(hpText, (int)screenPos.x - textWidth/2, barY - 12, 10, WHITE);
    }

    // Draw projectiles in flight: a small dense shell with a dark rim so it
    // reads against both the sky and the terrain, coloured by type.
    for (const auto& p : projectiles) {
        if (!p.isAlive()) continue;
        Vector2 screenPos = camera.worldToScreen((Vector2){p.drawX, p.drawY});
        Color c = (p.type == Entities::ProjectileType::Bazooka) ? GOLD : (Color){120, 120, 140, 255};
        DrawCircle(screenPos.x, screenPos.y, 5.0f, c);
        DrawCircleLines(screenPos.x, screenPos.y, 5.0f, (Color){40, 40, 40, 200});
    }

    // Draw explosions: a filled flash that fades out at the real blast radius.
    for (const auto& exp : explosions) {
        if (!exp.isAlive()) continue;

        Vector2 screenPos = camera.worldToScreen((Vector2){exp.x, exp.y});
        float alpha = 1.0f - exp.age / exp.maxAge;

        // Outer warm flash, fully opaque core fading to transparent at the edge.
        Color core = ORANGE;
        core.a = (unsigned char)(255 * alpha);
        DrawCircle(screenPos.x, screenPos.y, exp.radius, core);

        // Bright inner flash (near-white while hot).
        Color inner = (Color){ 255, 244, 190, (unsigned char)(255 * alpha) };
        DrawCircle(screenPos.x, screenPos.y, exp.radius * 0.55f, inner);
    }
}

void renderHUD(const HUDState& state) {
    // Weapon name
    DrawText(state.weapon.c_str(), 20, 20, 20, WHITE);
    
    // Charge level
    if (state.chargeLevel > 0) {
        int barWidth = 100;
        int barHeight = 10;
        DrawRectangle(20, 50, barWidth, barHeight, DARKGRAY);
        DrawRectangle(20, 50, (int)(barWidth * state.chargeLevel), barHeight, YELLOW);
    }
    
    // Wind indicator
    char windText[32];
    snprintf(windText, sizeof(windText), "Wind: %.1f", state.wind);
    DrawText(windText, 20, 70, 16, LIGHTGRAY);
    
    // Timer
    char timerText[32];
    snprintf(timerText, sizeof(timerText), "Time: %.0f", state.timer);
    DrawText(timerText, GetScreenWidth() - 120, 20, 20, WHITE);
    
    // Debug info
    if (state.debugMode) {
        char debugText[128];
        snprintf(debugText, sizeof(debugText), 
                 "FPS: %d | Static: %d | Dynamic: %d",
                 state.fps, state.staticBodies, state.dynamicBodies);
        DrawText(debugText, 20, GetScreenHeight() - 40, 16, LIGHTGRAY);
    }
}
