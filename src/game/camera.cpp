#include "camera.h"
#include "../entities/worm.h"
#include "../terrain/grid.h"
#include <algorithm>

// The camera operates in the same pixel-space as terrain chunk coordinates
// (Terrain::CELL grid units), while Worm::getPosition() returns raw Box2D
// body coordinates in meters, so worm positions must be scaled by
// Terrain::PPM before being compared against camera/terrain coordinates.

void GameCamera::setFollow(const Entities::Worm* worm) {
    if (worm) {
        Vector2 pos = worm->getPosition();
        targetX = pos.x * Terrain::PPM;
        targetY = pos.y * Terrain::PPM;
    }
}

void GameCamera::update(float dt) {
    // Smooth follow
    x += (targetX - x) * 5.0f * dt;
    y += (targetY - y) * 5.0f * dt;

    // Clamp to world bounds (in pixel-space)
    constexpr float worldPxW = (float)Terrain::WORLD_W * Terrain::PPM;
    constexpr float worldPxH = (float)Terrain::WORLD_H * Terrain::PPM;
    x = std::max(0.0f, std::min(x, worldPxW));
    y = std::max(0.0f, std::min(y, worldPxH));
}

Vector2 GameCamera::screenToWorld(Vector2 screenPos) const {
    return Vector2{
        (screenPos.x - GetScreenWidth() / 2.0f) / zoom + x,
        (screenPos.y - GetScreenHeight() / 2.0f) / zoom + y
    };
}

Vector2 GameCamera::worldToScreen(Vector2 worldPos) const {
    return Vector2{
        (worldPos.x - x) * zoom + GetScreenWidth() / 2.0f,
        (worldPos.y - y) * zoom + GetScreenHeight() / 2.0f
    };
}
