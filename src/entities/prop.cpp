#include "prop.h"
#include "../physics/world.h"
#include "../terrain/grid.h"

namespace Entities {

void Prop::spawn(World* world, PropType t, float x, float y) {
    type = t;
    pos = {x, y};
    float bx = x / Terrain::PPM;
    float by = Terrain::WORLD_H - y / Terrain::PPM;
    bool upright = (t == PropType::HealthCrate || t == PropType::WeaponCrate);
    body = world->createBody(bx, by, upright, upright ? 0.0f : 0.1f, 0.8f);
    if (!body) { alive = false; return; }

    auto box = [&](float hw, float hh, float density, float friction) {
        std::vector<Vector2> v = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        world->createPolygonFixture(body, v, density, friction);
    };
    switch (t) {
        case PropType::Barrel:      box(0.26f, 0.34f, 1.0f, 0.7f); hp = 20; break;
        case PropType::Mine: {
            // Flat-bottomed so it sits where it's put instead of rolling downhill.
            std::vector<Vector2> v = {{-0.17f, -0.1f}, {0.17f, -0.1f}, {0.12f, 0.12f}, {-0.12f, 0.12f}};
            world->createPolygonFixture(body, v, 3.0f, 1.0f);
            hp = 1;
            armTimer = 0;
            break;
        }
        case PropType::HealthCrate: box(0.34f, 0.34f, 1.0f, 0.9f); hp = 10; parachute = true; break;
        case PropType::WeaponCrate: box(0.34f, 0.34f, 1.0f, 0.9f); hp = 10; parachute = true; break;
        case PropType::Grave:       box(0.2f, 0.3f, 2.0f, 0.9f); hp = 1; break;
    }
    world->setBodyFilter(body, World::CAT_PROP, World::CAT_TERRAIN | World::CAT_PROJ | World::CAT_PROP);
    world->registerBody(body, World::BodyKind::Prop);
}

void Prop::sync(World* world) {
    if (!body) return;
    Vector2 bp = world->getBodyPosition(body);
    pos = {bp.x * Terrain::PPM, (Terrain::WORLD_H - bp.y) * Terrain::PPM};
    angle = -world->getBodyAngle(body);
}

void Prop::destroy(World* world) {
    if (body) world->destroyBody(body);
    body = nullptr;
    alive = false;
}

bool Prop::isResting(World* world) const {
    if (!alive || !body) return true;
    if (world->isBodyAsleep(body)) return true;
    Vector2 v = world->getBodyVelocity(body);
    return v.x * v.x + v.y * v.y < 0.04f;
}

} // namespace Entities
