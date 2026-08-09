#include "index.h"
#include "../entities/worm.h"
#include "../entities/explosion.h"
#include "../terrain/terrain.h"
#include "../physics/world.h"
#include <raylib.h>
#include <algorithm>
#include <cmath>

namespace Weapons {

void Bazooka::fire(const Entities::Worm* worm, float angle, float power, World* world,
                   const ChunkGrid& chunks, const std::vector<std::vector<Entities::Worm*>>& worms) {
    if (!worm || !world) return;
    
    Vector2 pos = worm->getPosition();
    float px = pos.x * Terrain::PPM;
    float py = pos.y * Terrain::PPM;
    
    // Create explosion at target location
    float distance = 10.0f + power * 10.0f;
    float tx = px + cosf(angle) * distance;
    float ty = py + sinf(angle) * distance;
    
    Entities::Explosion exp(tx, ty, radius * Terrain::PPM, damage);
    createExplosion(world, tx, ty, radius * Terrain::PPM, damage, nullptr, worms);
}

void Grenade::fire(const Entities::Worm* worm, float angle, float power, World* world,
                   const ChunkGrid& chunks, const std::vector<std::vector<Entities::Worm*>>& worms) {
    if (!worm || !world) return;
    
    Vector2 pos = worm->getPosition();
    float px = pos.x * Terrain::PPM;
    float py = pos.y * Terrain::PPM;
    
    // Create explosion at target location
    float distance = 8.0f + power * 8.0f;
    float tx = px + cosf(angle) * distance;
    float ty = py + sinf(angle) * distance;
    
    Entities::Explosion exp(tx, ty, radius * Terrain::PPM, damage);
    createExplosion(world, tx, ty, radius * Terrain::PPM, damage, nullptr, worms);
}

} // namespace Weapons

WeaponList weapons;
