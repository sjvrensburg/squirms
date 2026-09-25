#pragma once
#include <string>
#include <raylib.h>
#include <box2d/box2d.h>

class World;

namespace Entities {

// A worm: a fixed-rotation capsule (two stacked circles) that walks, hops,
// backflips and gets blasted around. Worms don't collide with each other.
//
// Health works the Worms way: `hp` drops the moment damage lands, while
// `shownHp` (what the label and team bars show) counts down afterwards. A
// worm whose hp hits zero isn't removed at once; it is `dying` and blows up
// in its own little explosion when the turn wraps up.
class Worm {
public:
    // Physics size in metres: 0.6 m wide, 0.9 m tall (19 x 29 px).
    static constexpr float RADIUS = 0.3f;
    static constexpr float HALF_HEIGHT = 0.45f;

    b2Body* body = nullptr;
    World* world = nullptr;
    std::string name;
    int teamIndex = 0;
    float hp = 100, shownHp = 100, maxHp = 100;

    bool alive = true;       // still in play (false once drowned or popped)
    bool dying = false;      // hp <= 0: will explode at end of turn
    bool drowned = false;    // sank into the sea
    bool grounded = false;
    bool flying = false;     // knocked back: no control until it lands and settles
    bool onRope = false;     // hanging from the ninja rope (Game drives spin)
    float crushCooldown = 0; // after being hit by falling terrain
    int facing = 1;          // +1 right, -1 left
    float aim = 0.35f;       // radians above horizontal, in [-pi/2, pi/2]

    // Animation / feel.
    float walkPhase = 0;     // advances while walking (inch-worm wiggle)
    float spin = 0;          // visual rotation while flying / backflipping
    float spinRate = 0;
    float blink = 0;         // seconds until next blink
    float hurtTimer = 0;     // flashes after taking damage
    float sinkDepth = 0;     // drowning: px below the surface
    bool walkedThisTick = false;
    float damageThisTurn = 0;
    float freshDamage = 0;   // taken since the game last showed a damage number

    Worm(const std::string& name, int team, float hp);
    void init(World* world, float pxX, float pxY);   // spawn at pixel coords (feet on ground)
    void update(float dt);
    void walk(int direction, float dt);
    void jump();
    void backflip();
    // Apply damage and a knockback velocity kick (Box2D m/s, Y-up).
    void hit(float damage, float kickX, float kickY);
    void teleportTo(float pxX, float pxY);
    void drown();
    void pop();   // remove from play after the death explosion

    bool isAlive() const { return alive; }
    bool canAct() const { return alive && !dying && !drowned; }
    int getTeamIndex() const { return teamIndex; }
    // Metres, Y-down (un-flipped): multiply by PPM for pixel space.
    Vector2 getPosition() const;
    Vector2 pixelPos() const;
    Vector2 getVelocity() const;
    bool isResting() const;

private:
    bool wasGrounded_ = false;
    float stillTime_ = 0;  // seconds spent (nearly) motionless
    float fallSpeed_ = 0;  // largest downward speed since leaving the ground
    void refreshGrounded();
};

} // namespace Entities
