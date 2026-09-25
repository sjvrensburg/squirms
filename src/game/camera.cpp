#include "camera.h"
#include "../terrain/grid.h"
#include <algorithm>
#include <cmath>

// How far past the map edges the view may go: sideways over the open sea,
// and up into the sky so high lobs stay in shot.
constexpr float SIDE_MARGIN = 260.0f;
constexpr float SKY_MARGIN = 520.0f;

void GameCamera::snapTo(float px, float py) {
    targetX = px;
    targetY = py;
    clampTarget();
    x = targetX;
    y = targetY;
}

void GameCamera::follow(Vector2 p, float speed) {
    if (manual) return;
    targetX = p.x;
    targetY = p.y;
    followSpeed = speed;
}

void GameCamera::pan(float dxScreen, float dyScreen) {
    manual = true;
    targetX -= dxScreen / zoom;
    targetY -= dyScreen / zoom;
    clampTarget();
    x = targetX;
    y = targetY;
}

void GameCamera::zoomBy(float factor) {
    targetZoom = std::clamp(targetZoom * factor, 0.5f, 1.6f);
}

void GameCamera::clampTarget() {
    float halfW = GetScreenWidth() / (2.0f * zoom);
    float halfH = GetScreenHeight() / (2.0f * zoom);
    float minX = halfW - SIDE_MARGIN, maxX = Terrain::WORLD_PX_W - halfW + SIDE_MARGIN;
    if (minX > maxX) minX = maxX = Terrain::WORLD_PX_W / 2.0f;
    float minY = halfH - SKY_MARGIN, maxY = Terrain::WORLD_PX_H - halfH;
    if (minY > maxY) minY = maxY;
    targetX = std::clamp(targetX, minX, maxX);
    targetY = std::clamp(targetY, minY, maxY);
}

void GameCamera::update(float dt, float shake) {
    zoom += (targetZoom - zoom) * std::min(1.0f, 8.0f * dt);
    clampTarget();
    float k = std::min(1.0f, followSpeed * dt);
    x += (targetX - x) * k;
    y += (targetY - y) * k;
    if (shake > 0.1f) {
        shakeX = ((float)GetRandomValue(-100, 100) / 100.0f) * shake;
        shakeY = ((float)GetRandomValue(-100, 100) / 100.0f) * shake;
    } else {
        shakeX = shakeY = 0;
    }
}

Camera2D GameCamera::camera2D() const {
    Camera2D c{};
    c.offset = Vector2{GetScreenWidth() / 2.0f + shakeX, GetScreenHeight() / 2.0f + shakeY};
    c.target = Vector2{roundf(x * zoom) / zoom, roundf(y * zoom) / zoom};
    c.rotation = 0;
    c.zoom = zoom;
    return c;
}

Vector2 GameCamera::screenToWorld(Vector2 s) const {
    return GetScreenToWorld2D(s, camera2D());
}

Vector2 GameCamera::worldToScreen(Vector2 w) const {
    return GetWorldToScreen2D(w, camera2D());
}
