#include "projectile.h"
#include "../physics/world.h"
#include "../terrain/grid.h"

namespace Entities {

void Projectile::launch(World* world, ProjectileType t, float x, float y, float vx, float vy) {
    type = t;
    pos = {x, y};
    vel = {vx, vy};
    float bx = x / Terrain::PPM;
    float by = Terrain::WORLD_H - y / Terrain::PPM;

    bool rolls = (t == ProjectileType::Grenade || t == ProjectileType::Cluster ||
                  t == ProjectileType::Banana || t == ProjectileType::Holy ||
                  t == ProjectileType::Bananalet);
    body = world->createBody(bx, by, !rolls, 0.0f, rolls ? 0.6f : 0.0f);
    if (!body) { alive = false; return; }

    // Bouncy things get restitution; missiles none; dynamite and the sheep
    // are heavy and grippy so they stay where they land.
    float r = 0.16f, restitution = 0.0f, friction = 0.4f, density = 2.0f;
    switch (t) {
        case ProjectileType::Grenade:   r = 0.16f; restitution = 0.5f; break;
        case ProjectileType::Cluster:   r = 0.16f; restitution = 0.45f; break;
        case ProjectileType::Banana:    r = 0.2f;  restitution = 0.55f; break;
        case ProjectileType::Bananalet: r = 0.16f; restitution = 0.4f; break;
        case ProjectileType::Holy:      r = 0.22f; restitution = 0.25f; density = 4.0f; friction = 0.8f; break;
        case ProjectileType::Dynamite:  r = 0.18f; restitution = 0.05f; friction = 0.9f; break;
        case ProjectileType::Sheep:     r = 0.3f;  restitution = 0.0f; friction = 0.1f; density = 1.0f; break;
        default: break;
    }
    world->createCircleFixture(body, 0, 0, r, density, friction, restitution);
    uint16_t mask = World::CAT_TERRAIN | World::CAT_WORM | World::CAT_PROP;
    // The sheep waddles through worms rather than bumping them.
    if (t == ProjectileType::Sheep) mask = World::CAT_TERRAIN | World::CAT_PROP;
    world->setBodyFilter(body, World::CAT_PROJ, mask);
    world->setBullet(body, true);
    if (ownerBody) world->setBodyUserData(body, ownerBody);
    world->registerBody(body, World::BodyKind::Projectile);
    world->setBodyVelocity(body, vx / Terrain::PPM, -vy / Terrain::PPM);
}

void Projectile::destroy(World* world) {
    if (body) world->destroyBody(body);
    body = nullptr;
    alive = false;
}

} // namespace Entities
