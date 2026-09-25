#include "worm.h"
#include "../physics/world.h"
#include "../terrain/grid.h"
#include <cmath>

namespace Entities {

Worm::Worm(float x, float y, int team, const char* color)
    : body(nullptr), hp(100), maxHp(100), teamIndex(team), color(color), alive(true) {
    name = "Worm " + std::to_string(team);
}

void Worm::init(World* world, float px, float py) {
    this->world = world;
    // Flip Y for Box2D Y-up convention (grid uses Y-down)
    float bodyY = Terrain::WORLD_H - py;
    // fixedRotation keeps the worm upright so it never tips over when it lands
    // askew; a Worms avatar stands straight rather than flipping on its side.
    body = static_cast<b2Body*>(world->createBody(px, bodyY, true));
    
    if (!body) return;
    
    // Create worm shape (capsule-like polygon)
    std::vector<Vector2> verts;
    float w = 0.5f;  // half width
    float h = 1.0f;  // half height
    
    verts.push_back(Vector2{-w, -h});
    verts.push_back(Vector2{ w, -h});
    verts.push_back(Vector2{ w,  h});
    verts.push_back(Vector2{-w,  h});
    
    world->createPolygonFixture(body, verts, 1.0f, 0.5f);
}

void Worm::update(float dt) {
    if (!alive || !body) return;

    // Fell out of the world (Box2D Y-up: the bottom of the world is y = 0,
    // see init()); a worm that drops below it dies.
    if (body->GetPosition().y < -0.5f) {
        die();
        return;
    }

    // Real grounded check: ray-cast straight down from just above the worm's
    // feet. The ray must start above the terrain surface, not below the feet:
    // Box2D's polygon ray-cast reports a negative fraction for a ray that
    // begins *inside* a shape and rejects it, so a ray starting under the feet
    // (inside the surface chunk the resting worm sits on) would never hit.
    //
    // We cast three rays across the worm's width (left foot, centre, right
    // foot) and treat the worm as grounded if any of them hits a surface whose
    // normal points up. The spread matters at a ledge corner: if the worm's
    // centre has just past the edge, the centre ray falls off the ledge while
    // the outer foot still rests on it, so a single centre ray would wrongly
    // report "in the air". A hit on terrain, or on another worm/body, counts
    // (a sloped top face still has an up-normal; a vertical side or
    // under-surface does not).
    //
    // The tolerance below the feet is kept small so the worm must be
    // essentially in contact with the ground, not floating a cell or two
    // above it (which would let it walk/re-jump in mid air).
    b2Vec2 pos = body->GetPosition();
    const float halfHeight = 1.0f;  // fixture half-height; feet sit here
    const float lift = 0.1f;        // start this far above the feet (above surface)
    const float tolerance = 0.15f;  // ground may be this far below the feet
    const float offsets[3] = {-0.4f, 0.0f, 0.4f};  // inside the 0.5m half-width

    grounded = false;
    for (float ox : offsets) {
        b2Vec2 origin(pos.x + ox, pos.y - (halfHeight - lift));
        b2Vec2 target(pos.x + ox, pos.y - (halfHeight + tolerance));
        World::RayCastResult hit;
        if (world && world->RayCast(origin, target, hit, body) && hit.normal.y > 0.0f) {
            grounded = true;
            break;
        }
    }
}

void Worm::walk(int direction, float dt) {
    if (!alive || !body || !grounded) return;

    // Remember which way we're moving so a backflip can hop the opposite way.
    if (direction) facing = direction;

    float speed = 4.0f;
    b2Vec2 vel = body->GetLinearVelocity();
    vel.x = direction * speed;
    body->SetLinearVelocity(vel);
}

void Worm::jump() {
    if (!alive || !body || !grounded) return;

    // A short hop, not a giant arc: apex = v^2 / 2g = 49 / 20 ~= 2.45m (about
    // 4 terrain cells), enough to clear a small bump but nowhere near a hill.
    const float jumpVelocity = 7.0f;
    b2Vec2 vel = body->GetLinearVelocity();
    vel.y = jumpVelocity;
    vel.x = 0.0f;  // hop straight up
    body->SetLinearVelocity(vel);
    body->SetAwake(true);
}

void Worm::backflip() {
    if (!alive || !body || !grounded) return;

    // fixedRotation keeps the worm upright, so a backflip can't actually spin
    // the body (that's a renderer concern, out of scope here). It hops higher
    // and farther than a normal jump, and backward: opposite the worm's facing
    // (its last walk direction). Apex = 81 / 20 ~= 4.05m.
    const float backflipVelocity = 9.0f;
    const float backflipKick = 1.5f;
    b2Vec2 vel = body->GetLinearVelocity();
    vel.y = backflipVelocity;
    vel.x = -facing * backflipKick;  // backward relative to facing
    body->SetLinearVelocity(vel);
    body->SetAwake(true);
}

void Worm::takeDamage(float damage, float kx, float ky) {
    if (!alive || !body) return;
    
    hp -= damage;
    
    // Apply knockback impulse
    if (kx != 0 || ky != 0) {
        b2Vec2 impulse(kx * 10.0f, ky * 10.0f);
        body->ApplyLinearImpulse(impulse, body->GetWorldCenter(), true);
    }
    
    if (hp <= 0) die();
}

Vector2 Worm::getPosition() const {
    if (!body) return Vector2{0, 0};
    b2Vec2 pos = body->GetPosition();
    // Un-flip Y back out of Box2D's Y-up convention (see init()) so callers
    // get the same pixel/PPM-meters convention terrain and spawn code use.
    return Vector2{pos.x, Terrain::WORLD_H - pos.y};
}

void Worm::die() {
    alive = false;
    if (body) {
        // Stop colliding with anything. The renderer already skips dead worms
        // (isAlive()), so the body simply vanishes from the world.
        body->SetEnabled(false);
    }
}

} // namespace Entities
