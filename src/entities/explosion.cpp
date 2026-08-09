#include "explosion.h"
#include "../physics/world.h"
#include "../terrain/terrain.h"
#include "../entities/worm.h"
#include <raylib.h>
#include <algorithm>
#include <cmath>

namespace Entities {

Explosion::Explosion(float x, float y, float radius, float damage)
    : x(x), y(y), radius(radius), damage(damage), age(0), maxAge(0.5f), alive(true) {}

void Explosion::update(float dt) {
    age += dt;
    if (age >= maxAge) alive = false;
}

} // namespace Entities

void createExplosion(World* world, float x, float y, float radius, float damage,
                     Terrain::TerrainSystem* terrain, const std::vector<std::vector<Entities::Worm*>>& worms) {
    // Damage chunks in radius
    if (terrain) {
        for (auto& row : terrain->chunks) {
            for (auto* chunk : row) {
                if (!chunk) continue;
                float dx = chunk->centroidX - x;
                float dy = chunk->centroidY - y;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < radius) {
                    float falloff = 1.0f - dist / radius;
                    terrain->damageChunk(chunk, damage * falloff);
                }
            }
        }
    }
    
    // Damage worms in radius
    for (auto& team : worms) {
        for (auto* worm : team) {
            if (!worm || !worm->isAlive()) continue;
            Vector2 pos = worm->getPosition();
            float dx = pos.x * 32.0f - x;
            float dy = pos.y * 32.0f - y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < radius) {
                float falloff = 1.0f - dist / radius;
                worm->takeDamage(damage * falloff);
            }
        }
    }
}
