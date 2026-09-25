#include "projectile.h"
#include "physics/world.h"
#include "terrain/terrain.h"
#include "entities/worm.h"
#include "entities/explosion.h"
#include <cmath>

namespace {
// Tunables (feel only — the game is build-verified, not tuned in-browser here).
constexpr float windAccel = 1.5f;        // m/s² sideways accel per unit of wind (bazooka)
constexpr float ownerKick = 2.0f;        // impulse to separate from the shooter as we leave it
constexpr float graceTime = 0.1f;        // s before a shell can hit terrain/worms (clears spawn)
constexpr float bounceRestitution = 0.5f;// fraction of approach speed preserved on a bounce
constexpr float bounceThreshold = 0.5f;  // m/s approach speed below which a shell rests, not bounces
} // namespace

namespace Entities {

Projectile::Projectile(ProjectileType type, float radiusPx, float damage, float fuseTime)
    : type(type), radius(radiusPx), damage(damage), fuseTime(fuseTime) {}

void Projectile::launch(World* world, float px, float py, float angle, float speed) {
    // Spawn at the given pixel position, flipped into Box2D's Y-up metre space.
    float bodyX = px / Terrain::PPM;
    float bodyY = Terrain::WORLD_H - py / Terrain::PPM;
    // fixedRotation keeps the shell from tumbling; light linear damping tames
    // long drift so a mis-fired shot doesn't float forever.
    body = world->createBody(bodyX, bodyY, true, 0.05f, 0.0f, 0.75f, false);
    if (!body) { alive = false; return; }

    // A small, dense shell. Kept small so it threads gaps rather than snagging
    // on a single-cell ledge.
    const float half = 0.25f;
    std::vector<Vector2> verts = {
        {-half, -half}, {half, -half}, {half, half}, {-half, half}
    };
    world->createPolygonFixture(body, verts, 1.5f, 0.3f); // density, friction
    world->registerBody(body, World::BodyKind::Projectile);

    // `angle` is in pixel space (+y down); Box2D is Y-up, so negate the y component.
    float speedMs = speed / Terrain::PPM;
    world->setBodyVelocity(body, speedMs * cosf(angle), -speedMs * sinf(angle));
}

void Projectile::update(World* world, float dt,
                        const std::vector<std::vector<Worm*>>& worms,
                        std::vector<Explosion>* fx) {
    if (!alive || !body) return;
    age += dt;

    // Keep the renderer's position fresh (pixel space).
    Vector2 bp = world->getBodyPosition(body);
    drawX = bp.x * Terrain::PPM;
    drawY = Terrain::WORLD_H - bp.y * Terrain::PPM;

    // Bazooka shells are pushed sideways by wind. Applied as an impulse so
    // Box2D keeps integrating gravity correctly for the rest of the step.
    if (type == ProjectileType::Bazooka && wind != 0.0f) {
        float mass = world->getBodyMass(body);
        world->applyLinearImpulse(body, wind * windAccel * mass, 0.0f);
    }

    // Grenade fuse: detonate wherever it is once the time is up.
    if (type == ProjectileType::Grenade && age >= fuseTime) {
        explode(world, bp, worms, fx);
        alive = false;
        return;
    }

    // Out of bounds: leave the world without exploding. `bp` is in Box2D
    // metres (world spans 0..WORLD_W x 0..WORLD_H); drawX/drawY are pixels, so
    // compare against the meter bounds here.
    if (bp.x < -0.5f || bp.x > (float)Terrain::WORLD_W + 0.5f ||
        bp.y < -0.5f || bp.y > (float)Terrain::WORLD_H + 0.5f) {
        alive = false;
        return;
    }

    // Resolve contacts outside any Box2D callback.
    std::vector<World::BodyContact> contacts;
    world->getContacts(body, contacts);
    for (const auto& c : contacts) {
        if (c.body == ownerBody) {
            // Separate from the shooter: nudge away along the owner->shell
            // line so we don't detonate against the worm we just left.
            Vector2 op = world->getBodyPosition(ownerBody);
            float ox = bp.x - op.x, oy = bp.y - op.y;
            float len = sqrtf(ox * ox + oy * oy);
            if (len < 1e-4f) {      // exact overlap (spawns inside the worm): push up
                oy = -1.0f; len = 1.0f;
            } else {
                ox /= len; oy /= len;
            }
            world->applyLinearImpulse(body, ox * ownerKick, oy * ownerKick);
            continue;
        }

        // While clearing the spawn point, ignore terrain/worm contact so the
        // shell can't be instantly detonated where it was launched.
        if (age < graceTime) continue;

        World::BodyKind kind = world->getBodyKind(c.body);
        if (kind == World::BodyKind::Worm || kind == World::BodyKind::Terrain) {
            if (type == ProjectileType::Bazooka) {
                // Bazooka detonates on first contact with terrain or a worm.
                explode(world, Vector2{c.point.x, c.point.y}, worms, fx);
                alive = false;
                return;
            }
            // Grenade ricochets off terrain and worms.
            bounce(world, c);
        }
        // Projectile / Unknown contacts are ignored.
    }
}

void Projectile::bounce(World* world, const World::BodyContact& c) {
    Vector2 vel = world->getBodyVelocity(body);
    // vn < 0 means the shell is moving toward the surface. If it's barely
    // approaching, let it rest (and wait for the fuse) instead of jitter-bouncing.
    float vn = vel.x * c.normal.x + vel.y * c.normal.y;
    if (vn > -bounceThreshold) return;
    Vector2 reflected;
    reflected.x = vel.x - (1.0f + bounceRestitution) * vn * c.normal.x;
    reflected.y = vel.y - (1.0f + bounceRestitution) * vn * c.normal.y;
    world->setBodyVelocity(body, reflected.x, reflected.y);
}

void Projectile::explode(World* world, Vector2 pointMetersYup,
                         const std::vector<std::vector<Worm*>>& worms,
                         std::vector<Explosion>* fx) {
    // Convert the contact point back to pixel space for the blast.
    float px = pointMetersYup.x * Terrain::PPM;
    float py = Terrain::WORLD_H - pointMetersYup.y * Terrain::PPM;
    createExplosion(world, px, py, radius, damage, terrain, worms);
    if (fx) fx->emplace_back(px, py, radius, damage);
}

} // namespace Entities
