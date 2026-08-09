#pragma once
#include <raylib.h>

namespace Entities { class Worm; }

class GameCamera {
public:
    float x = 0, y = 0, zoom = 1;
    float targetX = 0, targetY = 0, targetZoom = 1;
    
    void setFollow(const Entities::Worm* worm);
    void update(float dt);
    Vector2 screenToWorld(Vector2 screenPos) const;
    Vector2 worldToScreen(Vector2 worldPos) const;
};
