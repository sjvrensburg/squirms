#pragma once
#include <raylib.h>

class World;

namespace Entities {

enum class ProjectileType {
    Bazooka, Grenade, Cluster, Clusterlet, Banana, Bananalet,
    Holy, Dynamite, AirMissile, Sheep,
};

// Anything thrown, fired, dropped or released that later goes bang. A real
// Box2D body owned by World; this object only holds the handle. Behaviour
// (fuses, bounces, the sheep's waddle) is driven by Game::updateProjectiles.
class Projectile {
public:
    void* body = nullptr;
    ProjectileType type = ProjectileType::Bazooka;
    float radius = 40;           // crater radius, px
    float damage = 40;
    float fuse = 0;              // seconds left; <= 0 means no fuse
    bool impact = false;         // detonates on first contact
    bool windAffected = false;
    bool alive = true;
    bool detonateNow = false;    // set by the player (sheep) or game logic
    float age = 0;
    void* ownerBody = nullptr;   // never collides with this worm's body
    float angle = 0;             // draw angle, radians (pixel space)
    int facing = 1;              // sheep walking direction
    float hopTimer = 0;          // sheep
    float lastBounceSound = 0;

    // Pixel-space centre and velocity, refreshed every tick.
    Vector2 pos{0, 0};
    Vector2 vel{0, 0};

    // Create the body at pixel (x, y) moving at pixel velocity (vx, vy).
    void launch(World* world, ProjectileType type, float x, float y, float vx, float vy);
    void destroy(World* world);
    bool isAlive() const { return alive; }
};

} // namespace Entities
