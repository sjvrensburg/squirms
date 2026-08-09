#include "renderer.h"
#include "../game/camera.h"
#include "../entities/worm.h"
#include "../entities/explosion.h"
#include "../terrain/terrain.h"
#include <raylib.h>
#include <algorithm>
#include <cmath>

void render(const GameCamera& camera, const ChunkGrid& chunks, 
            const std::vector<Entities::Worm*>& worms,
            const std::vector<Entities::Explosion>& explosions,
            int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;

    // Draw terrain chunks
    for (const auto& row : chunks) {
        for (auto* chunk : row) {
            if (!chunk || chunk->state == Terrain::ChunkState::GONE) continue;
            
            Vector2 screenPos = camera.worldToScreen((Vector2){chunk->centroidX, chunk->centroidY});
            
            // Color based on material and state
            Color c;
            switch (chunk->material) {
                case Terrain::Material::Dirt: c = BROWN; break;
                case Terrain::Material::Rock: c = DARKGRAY; break;
                case Terrain::Material::Sand: c = GOLD; break;
                case Terrain::Material::Bedrock: c = DARKGRAY; break;
                default: c = LIGHTGRAY; break;
            }
            
            if (chunk->state == Terrain::ChunkState::DYNAMIC) {
                c = GOLD; // Dynamic chunks are highlighted
            }
            
            // Draw chunk as filled polygon
            std::vector<Vector2> screenVerts;
            for (auto& v : chunk->verts) {
                Vector2 worldPos = {(float)v.first, (float)v.second};
                Vector2 screen = camera.worldToScreen(worldPos);
                screenVerts.push_back(screen);
            }
            
            if (screenVerts.size() >= 3) {
                for (size_t i = 1; i < screenVerts.size(); ++i) {
                    DrawLineEx(screenVerts[i-1], screenVerts[i], 2.0f, c);
                }
                DrawLineEx(screenVerts.back(), screenVerts.front(), 2.0f, c);
            }
        }
    }

    // Draw worms
    for (auto* worm : worms) {
        if (!worm || !worm->isAlive()) continue;

        Vector2 pos = worm->getPosition();
        Vector2 screenPos = camera.worldToScreen((Vector2){pos.x * Terrain::PPM, pos.y * Terrain::PPM});

        // Get body rotation for visual orientation
        float angle = 0.0f;
        
        // Draw worm body (ellipse)
        DrawEllipse(screenPos.x, screenPos.y, 12, 8, ColorAlpha(ORANGE, 0.9f));
        
        // Draw eyes
        float eyeOffsetX = cosf(angle) * 6;
        float eyeOffsetY = sinf(angle) * 6;
        DrawCircle(screenPos.x + eyeOffsetX - 3, screenPos.y + eyeOffsetY - 2, 2, WHITE);
        DrawCircle(screenPos.x + eyeOffsetX + 3, screenPos.y + eyeOffsetY - 2, 2, WHITE);
        DrawCircle(screenPos.x + eyeOffsetX - 3, screenPos.y + eyeOffsetY - 2, 1, BLACK);
        DrawCircle(screenPos.x + eyeOffsetX + 3, screenPos.y + eyeOffsetY - 2, 1, BLACK);
        
        // Draw health bar
        float hpPercent = worm->hp / worm->maxHp;
        int barWidth = 24;
        int barHeight = 4;
        int barX = (int)screenPos.x - barWidth/2;
        int barY = (int)screenPos.y - 16;
        
        DrawRectangle(barX, barY, barWidth, barHeight, DARKGRAY);
        DrawRectangle(barX, barY, (int)(barWidth * hpPercent), barHeight,
                      hpPercent > 0.5f ? GREEN : (hpPercent > 0.25f ? YELLOW : RED));
    }

    // Draw explosions
    for (const auto& exp : explosions) {
        if (!exp.isAlive()) continue;
        
        Vector2 screenPos = camera.worldToScreen((Vector2){exp.x, exp.y});
        float alpha = 1.0f - exp.age / exp.maxAge;
        
        // Draw explosion ring
        Color c = ORANGE;
        c.a = (unsigned char)(255 * alpha);
        DrawCircleLines(screenPos.x, screenPos.y, exp.radius * 0.1f, c);
        
        // Draw inner glow
        c.a = (unsigned char)(128 * alpha);
        DrawCircle(screenPos.x, screenPos.y, exp.radius * 0.05f, c);
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
