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
    // Damage chunks in radius. Bedrock is indestructible, so skip it outright
    // (its hp is negative, so a naive hp check would destroy it).
    bool anyDestroyed = false;
    if (terrain) {
        for (auto& row : terrain->chunks) {
            for (auto* chunk : row) {
                if (!chunk || chunk->material == Terrain::Material::Bedrock) continue;
                float dx = chunk->centroidX - x;
                float dy = chunk->centroidY - y;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < radius) {
                    float falloff = 1.0f - dist / radius;
                    // damageChunk only reports a real removal (a SOLID chunk
                    // whose hp dropped to zero), so this counts once per
                    // destroyed chunk, not once per explosion.
                    if (terrain->damageChunk(chunk, damage * falloff)) anyDestroyed = true;
                }
            }
        }
        // Once per explosion: recompute which chunks lost their support and
        // thaw every now-floating component into a falling slab. Doing this
        // here (rather than per destroyed chunk or every tick) means a blast
        // that removes several chunks produces exactly one thaw pass.
        if (anyDestroyed) terrain->recomputeSupportAndThaw();
    }

    // Damage worms in radius, with distance falloff and a knockaway from the
    // blast centre scaled by proximity.
    for (auto& team : worms) {
        for (auto* worm : team) {
            if (!worm || !worm->isAlive()) continue;
            Vector2 pos = worm->getPosition();
            float wx = pos.x * Terrain::PPM;
            float wy = pos.y * Terrain::PPM;
            float dx = wx - x;
            float dy = wy - y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < radius) {
                float falloff = 1.0f - dist / radius;
                float nx = dist > 1e-4f ? dx / dist : 0.0f;
                // ny is in pixel space (Y-down); takeDamage applies the impulse
                // in Box2D's Y-up space, so flip the vertical component.
                float ny = dist > 1e-4f ? -(dy / dist) : 0.0f;
                float kick = 1.5f * falloff; // stronger nearer the centre
                worm->takeDamage(damage * falloff, nx * kick, ny * kick);
            }
        }
    }
}
