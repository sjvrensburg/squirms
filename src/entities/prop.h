#pragma once
#include <raylib.h>

class World;

namespace Entities {

enum class PropType { Barrel, Mine, HealthCrate, WeaponCrate, Grave };

// Scenery that reacts to the battle: oil barrels (explode when hurt), mines
// (trip when a worm comes near), supply crates (collected by walking into
// them, drift down on a parachute) and gravestones left by fallen worms.
class Prop {
public:
    PropType type = PropType::Barrel;
    void* body = nullptr;
    bool alive = true;
    float hp = 1;
    float fuse = -1;        // mines/barrels: counting down to detonation when >= 0
    float armTimer = 0;     // mines: seconds until armed
    bool parachute = false; // crates: drifting down
    int contents = 0;       // weapon crate: WeaponId; grave: team index
    Vector2 pos{0, 0};      // pixel centre
    float angle = 0;        // radians, pixel space

    void spawn(World* world, PropType type, float x, float y);
    void sync(World* world);   // refresh pos/angle from the body
    void destroy(World* world);
    bool isResting(World* world) const;
};

} // namespace Entities
