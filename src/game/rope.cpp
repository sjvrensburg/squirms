#include "../net/input.h"
// The ninja rope: shoot it along the aim, swing with Left/Right, climb with
// Up/Down, let go with Space (or Enter), and shoot it again mid-air. Using
// the rope doesn't end the turn.
#include "game.h"
#include "../physics/world.h"
#include "../terrain/grid.h"
#include "../terrain/terrain.h"
#include "../audio/sfx.h"
#include <algorithm>
#include <cmath>

using Entities::Worm;

namespace {
constexpr float ROPE_REACH = 380.0f;     // px the hook can fly
constexpr float ROPE_SPEED = 1500.0f;    // px/s hook speed
constexpr float ROPE_MIN = 16.0f;        // px shortest free rope
constexpr float ROPE_MAX = 460.0f;       // px longest free rope
constexpr float CLIMB_SPEED = 140.0f;    // px/s
constexpr float SWING_FORCE = 6.5f;      // N sideways

inline float len(Vector2 a, Vector2 b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
inline float cross(Vector2 a, Vector2 b, Vector2 c) {  // (b - a) x (c - b)
    return (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
}
inline b2Vec2 toB2(Vector2 p) { return b2Vec2(p.x / Terrain::PPM, Terrain::WORLD_H - p.y / Terrain::PPM); }
inline Vector2 fromB2(b2Vec2 p) { return Vector2{p.x * Terrain::PPM, (Terrain::WORLD_H - p.y) * Terrain::PPM}; }
} // namespace

void Game::shootRope() {
    if (!current || rope.state != NinjaRope::State::Idle) return;
    rope.state = NinjaRope::State::Shooting;
    rope.dir = aimDir(current);
    rope.shot = 0;
    Audio::playSound("throw", 0.7f, 1.4f);
}

void Game::releaseRope() {
    if (rope.joint) world->destroyJoint(rope.joint);
    rope = NinjaRope{};
    if (current) {
        current->onRope = false;
        current->spin = 0;
    }
}

// (Re)create the joint at the live pivot with the current free length.
void Game::rehangRope() {
    if (rope.joint) world->destroyJoint(rope.joint);
    b2Vec2 a = toB2(rope.pivots.back());
    rope.joint = world->createRopeJoint(current->body, a.x, a.y, rope.freeLen / Terrain::PPM);
}

void Game::attachRope(Vector2 at, void* body) {
    rope.state = NinjaRope::State::Attached;
    rope.pivots = {at};
    rope.wrapSide = {0};
    rope.pivotBodies = {body};
    rope.freeLen = std::clamp(len(current->pixelPos(), at), ROPE_MIN, ROPE_MAX);
    rehangRope();
    current->onRope = true;
    current->flying = false;
    camera.manual = false;
    Audio::playSound("bounce", 0.8f, 1.6f);
}

// Held controls while attached (per tick): swing and climb.
void Game::ropeControls(float dt) {
    if (rope.state != NinjaRope::State::Attached || !current) return;
    int dir = 0;
    if (Input::keyDown(KEY_LEFT) || Input::keyDown(KEY_A)) dir -= 1;
    if (Input::keyDown(KEY_RIGHT) || Input::keyDown(KEY_D)) dir += 1;
    if (dir) {
        world->applyForce(current->body, dir * SWING_FORCE, 0);
        current->facing = dir;
    }
    float climb = 0;
    if (Input::keyDown(KEY_UP) || Input::keyDown(KEY_W)) climb -= CLIMB_SPEED * dt;
    if (Input::keyDown(KEY_DOWN) || Input::keyDown(KEY_S)) climb += CLIMB_SPEED * dt;
    if (climb != 0) {
        rope.freeLen = std::clamp(rope.freeLen + climb, ROPE_MIN, ROPE_MAX);
        world->setRopeLength(rope.joint, rope.freeLen / Terrain::PPM);
    }
}

// After the physics step: fly the hook, wrap/unwrap round corners, snap the
// rope if its anchor is blown away, and point the worm up the rope.
void Game::updateRope(float dt) {
    if (rope.state == NinjaRope::State::Idle) return;
    bool turnOver = !(phase == Phase::Aiming || phase == Phase::Retreat);
    if (!current || !current->canAct() || turnOver) {
        releaseRope();
        return;
    }
    Vector2 w = current->pixelPos();

    if (rope.state == NinjaRope::State::Shooting) {
        float from = rope.shot;
        rope.shot = std::min(ROPE_REACH, rope.shot + ROPE_SPEED * dt);
        Vector2 a{w.x + rope.dir.x * std::max(0.0f, from - 4), w.y + rope.dir.y * std::max(0.0f, from - 4)};
        Vector2 b{w.x + rope.dir.x * rope.shot, w.y + rope.dir.y * rope.shot};
        World::RayCastResult hit;
        if (world->RayCast(toB2(a), toB2(b), hit, current->body, World::CAT_TERRAIN)) {
            // Only solid, static ground holds a hook; falling slabs don't.
            if (!terrain->isDynamicBody(hit.body)) {
                Vector2 p = fromB2(hit.point);
                Vector2 n{hit.normal.x, -hit.normal.y};
                attachRope({p.x + n.x * 2, p.y + n.y * 2}, hit.body);
                return;
            }
            rope = NinjaRope{};
            return;
        }
        if (rope.shot >= ROPE_REACH) {
            rope = NinjaRope{};  // missed: reel back in
            Audio::playSound("select", 0.4f, 0.7f);
        }
        return;
    }

    // Attached. The rope snaps if the ground its live pivot hangs on is
    // destroyed or starts falling.
    // (World forgets destroyed bodies; thawed ones become dynamic.)
    void* pb = rope.pivotBodies.back();
    bool anchored = world->getBodyKind(pb) == World::BodyKind::Terrain && !terrain->isDynamicBody(pb);
    if (!anchored) {
        Audio::playSound("select", 0.6f, 0.5f);
        releaseRope();
        return;
    }

    // Wrap: something now blocks the straight line to the live pivot.
    Vector2 pivot = rope.pivots.back();
    World::RayCastResult hit;
    if (world->RayCast(toB2(w), toB2(pivot), hit, current->body, World::CAT_TERRAIN) &&
        !terrain->isDynamicBody(hit.body)) {
        Vector2 p = fromB2(hit.point);
        Vector2 n{hit.normal.x, -hit.normal.y};
        Vector2 np{p.x + n.x * 2.5f, p.y + n.y * 2.5f};
        float seg = len(np, pivot);
        if (seg > 4.0f && rope.freeLen - seg > ROPE_MIN) {
            rope.wrapSide.push_back(cross(pivot, np, w) >= 0 ? 1 : -1);
            rope.pivots.push_back(np);
            rope.pivotBodies.push_back(hit.body);
            rope.freeLen -= seg;
            rehangRope();
            pivot = np;
        }
    } else if (rope.pivots.size() >= 2) {
        // Unwrap: the worm has swung back past the corner and can see the
        // previous pivot again.
        Vector2 prev = rope.pivots[rope.pivots.size() - 2];
        int side = cross(prev, pivot, w) >= 0 ? 1 : -1;
        World::RayCastResult h2;
        bool clear = !world->RayCast(toB2(w), toB2(prev), h2, current->body, World::CAT_TERRAIN);
        if (side != rope.wrapSide.back() && clear) {
            rope.freeLen = std::min(ROPE_MAX, rope.freeLen + len(prev, pivot));
            rope.pivots.pop_back();
            rope.wrapSide.pop_back();
            rope.pivotBodies.pop_back();
            rehangRope();
            pivot = prev;
        }
    }

    // Hang head-first towards the pivot.
    current->spin = atan2f(pivot.x - w.x, -(pivot.y - w.y));
    current->onRope = true;
}
