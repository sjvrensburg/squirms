#pragma once
#include <raylib.h>
#include <vector>

// Full World definition so we can name World::BodyContact in the contact
// resolver signatures.
#include "../physics/world.h"

namespace Terrain { struct Chunk; class TerrainSystem; }
namespace Entities { class Worm; class Explosion; }

namespace Entities {

enum class ProjectileType { Bazooka, Grenade };

// A fired shell: a real Box2D dynamic body that flies under gravity, reacts to
// wind (bazooka) or bounces (grenade), and explodes on terrain/worm contact or,
// for the grenade, when its fuse runs out. The body is owned by World; this
// object only holds a handle to it.
class Projectile {
public:
    void* body = nullptr;         // b2Body* (owned by World)
    ProjectileType type;
    float radius;                  // blast radius, in pixels
    float damage;
    float fuseTime;                // seconds before a grenade detonates (0 for bazooka)
    float age = 0.0f;              // seconds since launch
    bool alive = true;
    Worm* owner = nullptr;         // never explode against the worm that fired it
    void* ownerBody = nullptr;
    float wind = 0.0f;             // sideways wind acceleration (bazooka only)
    Terrain::TerrainSystem* terrain = nullptr;

    // Pixel-space position of the shell's centre, updated each step so the
    // renderer can draw it without needing World access.
    float drawX = 0.0f, drawY = 0.0f;

    Projectile(ProjectileType type, float radiusPx, float damage, float fuseTime);

    // Create the body at (px, py) [pixel space] heading in `angle` (radians,
    // pixel space, +y down) with `speed` [px/s]. Registers it as a Projectile.
    void launch(World* world, float px, float py, float angle, float speed);

    // Advance one fixed step. Applies wind, resolves contacts, checks the fuse
    // and world bounds, and explodes when due. Any resulting explosion is
    // appended to `fx` (position/size already in pixel space); the shell is
    // marked not alive. Out-of-bounds shells simply die without exploding.
    void update(World* world, float dt,
                const std::vector<std::vector<Worm*>>& worms,
                std::vector<Explosion>* fx);

    bool isAlive() const { return alive; }

private:
    // Detonate at a point (Box2D Y-up metres): damage the world/worms and
    // record an Explosion for the renderer.
    void explode(World* world, Vector2 pointMetersYup,
                 const std::vector<std::vector<Worm*>>& worms,
                 std::vector<Explosion>* fx);
};

} // namespace Entities
