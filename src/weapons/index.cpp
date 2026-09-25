#include "index.h"
#include "../entities/worm.h"
#include "../entities/projectile.h"
#include "../terrain/terrain.h"
#include "../physics/world.h"
#include <raylib.h>
#include <algorithm>
#include <cmath>

namespace Weapons {

// Fire a real projectile toward `angle` (radians, pixel space) whose launch
// speed scales with `power` (the charge level, 0..1). The shell is returned so
// the caller can hand it to the game loop to simulate.
Entities::Projectile Bazooka::fire(const Entities::Worm* worm, float angle, float power, World* world,
                         Terrain::TerrainSystem* terrain, float wind) {
    if (!worm || !world) return Entities::Projectile(Entities::ProjectileType::Bazooka, 0.0f, 0.0f, 0.0f);

    Entities::Projectile p(Entities::ProjectileType::Bazooka, radius * Terrain::PPM, damage, 0.0f);
    p.owner = const_cast<Entities::Worm*>(worm);
    p.ownerBody = p.owner ? p.owner->body : nullptr;
    p.wind = wind;
    p.terrain = terrain;

    Vector2 pos = worm->getPosition(); // metres
    float speed = minSpeed + power * (maxSpeed - minSpeed);
    p.launch(world, pos.x * Terrain::PPM, pos.y * Terrain::PPM, angle, speed);
    return p;
}

Entities::Projectile Grenade::fire(const Entities::Worm* worm, float angle, float power, World* world,
                         Terrain::TerrainSystem* terrain, float wind) {
    if (!worm || !world) return Entities::Projectile(Entities::ProjectileType::Grenade, 0.0f, 0.0f, 0.0f);

    Entities::Projectile p(Entities::ProjectileType::Grenade, radius * Terrain::PPM, damage, (float)fuseTime);
    p.owner = const_cast<Entities::Worm*>(worm);
    p.ownerBody = p.owner ? p.owner->body : nullptr;
    p.wind = wind;
    p.terrain = terrain;

    Vector2 pos = worm->getPosition(); // metres
    float speed = minSpeed + power * (maxSpeed - minSpeed);
    p.launch(world, pos.x * Terrain::PPM, pos.y * Terrain::PPM, angle, speed);
    return p;
}

} // namespace Weapons

WeaponList weapons;
