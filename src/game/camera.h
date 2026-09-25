#pragma once
#include <raylib.h>

// World camera in pixel space. Glides toward a target point (the active
// worm, a shell in flight, ...), can be dragged by the player, clamps to the
// map, and adds explosion shake.
class GameCamera {
public:
    float x = 0, y = 0, zoom = 1;
    float targetX = 0, targetY = 0, targetZoom = 1;
    float followSpeed = 5.0f;
    bool manual = false;        // player dragged the view; stop auto-follow
    float shakeX = 0, shakeY = 0;

    void snapTo(float px, float py);
    void follow(Vector2 p, float speed = 5.0f);   // no-op while manual
    void pan(float dxScreen, float dyScreen);     // drag by screen pixels
    void zoomBy(float factor);
    void update(float dt, float shake);

    Camera2D camera2D() const;
    Vector2 screenToWorld(Vector2 s) const;
    Vector2 worldToScreen(Vector2 w) const;

private:
    void clampTarget();
};
