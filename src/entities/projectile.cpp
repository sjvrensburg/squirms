#include "projectile.h"
#include "physics/world.h"
#include "terrain/terrain.h"
#include "entities/worm.h"
#include "entities/explosion.h"
#include <cmath>

namespace {
// Tunables (feel only — the game is build-verified, not tuned in-browser here).
constexpr float windAccel = 1.5f;        // m/s² sideways accel per unit of wind (bazooka)
constexpr float graceTime = 0.1f;        // s before a shell can hit terrain/worms (clears spawn)
constexpr float bounceRestitution = 0.5f;// fraction of approach speed a grenade keeps on a bounce
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

    // Bazookas never bounce (they detonate on first contact); grenades bounce
    // with the configured restitution. The low threshold lets even a moderate
    // hit ricochet instead of being swallowed by Box2D's default threshold.
    float restitution = (type == ProjectileType::Grenade) ? bounceRestitution : 0.0f;
    world->configureFixture(body, restitution, 0.5f);
    // Tag the shell with its owner's body pointer so the contact filter can
    // suppress collisions between this shell and the worm that fired it. This
    // is evaluated live at contact time, so it never corrupts any persistent
    // collision mask and stays correct no matter how many shells have been
    // fired, by whom, or in what order.
    if (ownerBody) world->setBodyUserData(body, ownerBody);
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

    // Keep the renderer's position fresh (pixel space). Un-flip Box2D's Y-up
    // metres back to pixel space: (WORLD_H - bodyY) * PPM, matching
    // Worm::getPosition().
    Vector2 bp = world->getBodyPosition(body);
    drawX = bp.x * Terrain::PPM;
    drawY = (Terrain::WORLD_H - bp.y) * Terrain::PPM;

    // Bazooka shells are pushed sideways by wind. Applied as an impulse so
    // Box2D keeps integrating gravity correctly for the rest of the step;
    // multiplying by dt turns the per-second acceleration into a proper impulse
    // (impulse = mass * delta-v = mass * acceleration * dt).
    if (type == ProjectileType::Bazooka && wind != 0.0f) {
        float mass = world->getBodyMass(body);
        world->applyLinearImpulse(body, wind * windAccel * mass * dt, 0.0f);
    }

    // Grenade fuse: detonate wherever it is once the time is up.
    if (type == ProjectileType::Grenade && age >= fuseTime) {
        explode(world, bp, worms, fx);
        alive = false;
        return;
    }

    // Out of bounds: only the sides or the bottom remove a shell. A high lob
    // that leaves the top of the world arcs back down, so don't kill it there.
    if (bp.x < -0.5f || bp.x > (float)Terrain::WORLD_W + 0.5f ||
        bp.y < -0.5f) {
        alive = false;
        return;
    }

    // Resolve contacts outside any Box2D callback. The shooter is excluded from
    // collision by the contact filter (shell/user-data tag), so it never
    // appears here.
    std::vector<World::BodyContact> contacts;
    world->getContacts(body, contacts);
    for (const auto& c : contacts) {
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
            // Grenade: Box2D restitution ricochets it off terrain and worms.
        }
        // Projectile / Unknown contacts are ignored.
    }
}

void Projectile::explode(World* world, Vector2 pointMetersYup,
                         const std::vector<std::vector<Worm*>>& worms,
                         std::vector<Explosion>* fx) {
    // Convert the contact point back to pixel space for the blast:
    // (WORLD_H - bodyY) * PPM, matching Worm::getPosition().
    float px = pointMetersYup.x * Terrain::PPM;
    float py = (Terrain::WORLD_H - pointMetersYup.y) * Terrain::PPM;
    createExplosion(world, px, py, radius, damage, terrain, worms);
    if (fx) fx->emplace_back(px, py, radius, damage);
}

} // namespace Entities
