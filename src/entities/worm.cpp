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

    // Real grounded check: ray-cast straight down from the worm's feet and see
    // whether anything other than itself is there. A hit on terrain, or on
    // another worm/body, means the worm is actually standing on something.
    b2Vec2 pos = body->GetPosition();
    b2Vec2 origin(pos.x, pos.y - 1.1f);  // just below the feet
    b2Vec2 target(pos.x, pos.y - 1.6f);

    World::RayCastResult hit;
    grounded = world && world->RayCast(origin, target, hit, body);
}

void Worm::walk(int direction, float dt) {
    if (!alive || !body || !grounded) return;
    
    float speed = 4.0f;
    b2Vec2 vel = body->GetLinearVelocity();
    vel.x = direction * speed;
    body->SetLinearVelocity(vel);
}

void Worm::jump() {
    if (!alive || !body || !grounded) return;
    
    float jumpForce = 12.0f;
    b2Vec2 vel = body->GetLinearVelocity();
    vel.y = jumpForce;
    body->SetLinearVelocity(vel);
}

void Worm::backflip() {
    if (!alive || !body || !grounded) return;

    // fixedRotation keeps the worm upright, so a backflip can't actually spin
    // the body (that's a renderer concern, out of scope here). It still hops
    // straight up off the ground and lands again, as a Worms backflip does.
    b2Vec2 vel = body->GetLinearVelocity();
    vel.y = 10.0f;
    vel.x = 0.0f;  // spring up in place
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
