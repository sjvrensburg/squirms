#include "worm.h"
#include "../physics/world.h"
#include "../terrain/grid.h"
#include "../audio/sfx.h"
#include <algorithm>
#include <cmath>

namespace Entities {

namespace {
constexpr float WALK_SPEED = 1.9f;        // m/s (~60 px/s): a Worms amble
constexpr float STEP_UP = 0.07f;          // m lifted per tick while climbing a step
constexpr float KNEE_HEIGHT = 0.5f;       // m above the feet: taller ledges block
constexpr float FALL_SAFE_SPEED = 10.0f;  // m/s; landing faster than this hurts
constexpr float FALL_DAMAGE_PER_MS = 6.0f;
} // namespace

Worm::Worm(const std::string& n, int team, float startHp)
    : name(n), teamIndex(team), hp(startHp), shownHp(startHp), maxHp(startHp) {
    blink = 1.0f + (float)GetRandomValue(0, 300) / 100.0f;
}

void Worm::init(World* w, float pxX, float pxY) {
    world = w;
    // pxY is the feet; the body origin sits HALF_HEIGHT above them. Flip Y
    // for Box2D's Y-up convention.
    float cx = pxX / Terrain::PPM;
    float cy = Terrain::WORLD_H - (pxY / Terrain::PPM - HALF_HEIGHT);
    body = static_cast<b2Body*>(world->createBody(cx, cy, true));
    if (!body) return;
    world->registerBody(body, World::BodyKind::Worm);
    // Capsule: two stacked circles. Circles ride over the small steps of
    // the chunk grid far better than a box's corners.
    const float off = HALF_HEIGHT - RADIUS;
    world->createCircleFixture(body, 0, -off, RADIUS, 1.0f, 0.8f, 0.15f);
    world->createCircleFixture(body, 0, off, RADIUS, 1.0f, 0.8f, 0.15f);
    world->setBodyFilter(body, World::CAT_WORM, World::CAT_TERRAIN | World::CAT_PROJ);
}

void Worm::refreshGrounded() {
    // Three short rays down from just above the feet (centre and both
    // sides of the capsule). Rays start inside the lower circle but above
    // the ground surface, so a resting worm's ray begins in air and hits the
    // terrain top face (Box2D rejects rays that start inside a shape).
    // Only terrain counts as ground.
    b2Vec2 pos = body->GetPosition();
    const float lift = 0.1f, tolerance = 0.18f;
    const float offsets[3] = {-0.2f, 0.0f, 0.2f};
    grounded = false;
    for (float ox : offsets) {
        b2Vec2 a(pos.x + ox, pos.y - (HALF_HEIGHT - lift));
        b2Vec2 b(pos.x + ox, pos.y - (HALF_HEIGHT + tolerance));
        World::RayCastResult hit;
        if (world->RayCast(a, b, hit, body, World::CAT_TERRAIN) && hit.normal.y > 0.3f) {
            grounded = true;
            return;
        }
    }
}

void Worm::update(float dt) {
    if (!alive || !body) return;
    if (hurtTimer > 0) hurtTimer -= dt;
    if (crushCooldown > 0) crushCooldown -= dt;
    blink -= dt;
    if (blink < -0.14f) blink = 1.5f + (float)GetRandomValue(0, 400) / 100.0f;

    if (drowned) {
        sinkDepth += 35.0f * dt;
        if (sinkDepth > 90.0f) alive = false;
        return;
    }
    if (dying && !body->IsEnabled()) return;

    // Into the sea: glug.
    Vector2 p = pixelPos();
    if (p.y > Terrain::WATER_Y + 6.0f) {
        drown();
        return;
    }

    b2Vec2 vel = body->GetLinearVelocity();
    refreshGrounded();
    // Wedged in a narrow pit, the rays can miss the floor even though the
    // worm is plainly resting on something. Motionless for a moment counts
    // as standing, so it can still walk/jump and the turn can move on.
    if (vel.x * vel.x + vel.y * vel.y < 0.02f) stillTime_ += dt;
    else stillTime_ = 0;
    if (!grounded && stillTime_ > 0.25f) grounded = true;

    if (onRope) {
        // Swinging isn't falling: only a drop after letting go can hurt.
        fallSpeed_ = 0;
        wasGrounded_ = grounded;
        walkedThisTick = false;
        return;
    }
    if (!grounded) {
        fallSpeed_ = std::max(fallSpeed_, -vel.y);
    } else if (!wasGrounded_) {
        // Just landed. Fall damage for long drops (Worms-style: a big fall
        // hurts and ends your turn).
        if (fallSpeed_ > FALL_SAFE_SPEED && !dying) {
            float dmg = std::round((fallSpeed_ - FALL_SAFE_SPEED) * FALL_DAMAGE_PER_MS);
            if (dmg >= 1.0f) {
                hit(dmg, 0, 0);
                Audio::playSound("hurt");
            }
        }
        if (fallSpeed_ > 3.0f) Audio::playSound("land", std::min(1.0f, fallSpeed_ / 10.0f));
        fallSpeed_ = 0;
        if (!flying) { spin = 0; spinRate = 0; }
    }
    wasGrounded_ = grounded;

    if (flying) {
        spin += spinRate * dt;
        float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
        if (grounded && speed < 0.8f) {
            flying = false;
            spin = 0;
            spinRate = 0;
        }
    } else if (spinRate != 0) {
        spin += spinRate * dt;
    }

    // Worms don't slide around when you let go of the keys.
    if (grounded && !flying && !walkedThisTick) {
        vel = body->GetLinearVelocity();
        vel.x *= 0.5f;
        body->SetLinearVelocity(vel);
    }
    walkedThisTick = false;
}

void Worm::walk(int direction, float dt) {
    if (!canAct() || !body || !grounded || flying || direction == 0) return;
    facing = direction;
    walkedThisTick = true;
    walkPhase += dt * 11.0f;

    b2Vec2 vel = body->GetLinearVelocity();
    vel.x = direction * WALK_SPEED;
    body->SetLinearVelocity(vel);

    // Step-up assist: the chunk grid makes every hillside a staircase of
    // small ledges. If the foot is blocked but knee height is clear, lift
    // the worm a little each tick so it climbs the step instead of sticking.
    b2Vec2 pos = body->GetPosition();
    float reach = RADIUS + 0.14f;
    b2Vec2 foot(pos.x, pos.y - HALF_HEIGHT + 0.08f);
    b2Vec2 knee(pos.x, pos.y - HALF_HEIGHT + KNEE_HEIGHT);
    World::RayCastResult hit;
    bool footBlocked = world->RayCast(foot, foot + b2Vec2(direction * reach, 0), hit, body, World::CAT_TERRAIN);
    if (footBlocked) {
        World::RayCastResult hit2;
        bool kneeBlocked = world->RayCast(knee, knee + b2Vec2(direction * reach, 0), hit2, body, World::CAT_TERRAIN);
        if (!kneeBlocked) {
            body->SetTransform(pos + b2Vec2(direction * 0.02f, STEP_UP), 0);
            vel = body->GetLinearVelocity();
            if (vel.y < 0) vel.y = 0;
            body->SetLinearVelocity(vel);
        }
    }
}

void Worm::jump() {
    if (!canAct() || !body || !grounded || flying) return;
    body->SetLinearVelocity(b2Vec2(facing * 2.6f, 5.4f));
    body->SetAwake(true);
    grounded = false;
    Audio::playSound("jump");
}

void Worm::backflip() {
    if (!canAct() || !body || !grounded || flying) return;
    body->SetLinearVelocity(b2Vec2(-facing * 1.3f, 7.4f));
    body->SetAwake(true);
    grounded = false;
    spinRate = -facing * 12.5f;  // one full tumble backwards
    Audio::playSound("backflip");
}

void Worm::hit(float damage, float kickX, float kickY) {
    if (!alive || drowned || !body) return;
    if (damage > 0) {
        hp -= damage;
        damageThisTurn += damage;
        freshDamage += damage;
        hurtTimer = 0.6f;
        if (hp <= 0) { hp = 0; dying = true; }
    }
    if (kickX * kickX + kickY * kickY > 0.25f) {
        b2Vec2 v = body->GetLinearVelocity();
        body->SetLinearVelocity(b2Vec2(v.x + kickX, v.y + kickY));
        body->SetAwake(true);
        flying = true;
        grounded = false;
        wasGrounded_ = false;
        spinRate = (kickX >= 0 ? -1.0f : 1.0f) * (6.0f + std::min(10.0f, fabsf(kickX) * 0.8f));
    }
}

void Worm::teleportTo(float pxX, float pxY) {
    if (!body) return;
    float cx = pxX / Terrain::PPM;
    float cy = Terrain::WORLD_H - (pxY / Terrain::PPM - HALF_HEIGHT);
    body->SetTransform(b2Vec2(cx, cy), 0);
    body->SetLinearVelocity(b2Vec2(0, 0));
    body->SetAwake(true);
    fallSpeed_ = 0;
}

void Worm::drown() {
    if (drowned) return;
    drowned = true;
    dying = false;
    flying = false;
    hp = 0;
    if (body) body->SetEnabled(false);
    Audio::playSound("splash");
    Audio::playSound("drown", 0.8f);
}

void Worm::pop() {
    alive = false;
    dying = false;
    if (body) body->SetEnabled(false);
}

Vector2 Worm::getPosition() const {
    if (!body) return Vector2{0, 0};
    b2Vec2 pos = body->GetPosition();
    // Un-flip Y out of Box2D's Y-up convention so callers can multiply by PPM
    // and land in pixel space.
    return Vector2{pos.x, Terrain::WORLD_H - pos.y};
}

Vector2 Worm::pixelPos() const {
    Vector2 m = getPosition();
    return Vector2{m.x * Terrain::PPM, m.y * Terrain::PPM + (drowned ? sinkDepth : 0.0f)};
}

Vector2 Worm::getVelocity() const {
    if (!body) return Vector2{0, 0};
    b2Vec2 v = body->GetLinearVelocity();
    return Vector2{v.x, v.y};
}

bool Worm::isResting() const {
    if (!alive) return true;
    if (drowned) return false;  // still sinking
    if (!body || !body->IsEnabled()) return true;
    b2Vec2 v = body->GetLinearVelocity();
    return grounded && !flying && (v.x * v.x + v.y * v.y) < 0.09f;
}

} // namespace Entities
