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
    // Flip Y for Box2D Y-up convention (grid uses Y-down)
    float bodyY = Terrain::WORLD_H - py;
    body = static_cast<b2Body*>(world->createBody(px, bodyY, false));
    
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
    
    // Check if grounded via raycast (simplified)
    b2Vec2 pos = body->GetPosition();
    b2Vec2 origin(pos.x, pos.y - 1.1f);
    b2Vec2 target(pos.x, pos.y - 1.5f);
    
    b2RayCastInput input;
    input.p1 = origin;
    input.p2 = target;
    input.maxFraction = 1.0f;
    
    b2RayCastOutput output;
    b2Fixture* fixture = body->GetFixtureList();
    if (fixture) {
        // Simple grounded check - in real impl would query world
        grounded = false;
    }
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
    
    // Apply angular impulse for backflip
    body->ApplyAngularImpulse(5.0f, true);
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
        // Mark as dead but keep physics body for a moment
        body->SetAwake(false);
    }
}

} // namespace Entities
