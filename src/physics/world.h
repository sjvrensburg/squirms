#pragma once
#include <raylib.h>
#include <vector>
#include <cstdint>

// Box2D headers for the ray-cast API exposed by World::RayCast().
#include <box2d/b2_world.h>
#include <box2d/b2_world_callbacks.h>

class World {
public:
    class Impl;

    World(float gravityX, float gravityY);
    ~World();

    // What a body represents, so weapons can classify contact hits without
    // walking the chunk/worm lists themselves.
    enum class BodyKind { Unknown, Terrain, Worm, Projectile, Prop };

    // Collision categories. Worms pass through each other and through props
    // (crates/mines/barrels are triggered by proximity, as in Worms);
    // shells hit everything except other shells.
    static constexpr uint16_t CAT_TERRAIN = 0x0001;
    static constexpr uint16_t CAT_WORM    = 0x0002;
    static constexpr uint16_t CAT_PROJ    = 0x0004;
    static constexpr uint16_t CAT_PROP    = 0x0008;

    // A single contact reported for a body: the other body involved, the
    // contact normal (points from A to B; sign is irrelevant for reflection),
    // and the world-space contact point (Box2D Y-up metres).
    struct BodyContact {
        void* body = nullptr;
        b2Vec2 normal;
        b2Vec2 point;
    };

    void* createBody(float x, float y, bool fixedRotation,
                     float linearDamping = 0, float angularDamping = 0,
                     float sleepThreshold = 0.75f, bool isStatic = false);
    void destroyBody(void* body);
    void* createPolygonFixture(void* body, const std::vector<Vector2>& verts, float friction);
    void* createPolygonFixture(void* body, const std::vector<Vector2>& verts, float density, float friction);
    void step(float dt, int velocityIterations, int positionIterations);

    // Ray-cast through the world. Returns true and fills `out` with the
    // closest hit. `ignoreBody` (typically the querying body itself) is
    // skipped so a body never reports itself as ground.
    struct RayCastResult {
        void* body = nullptr;
        void* fixture = nullptr;
        float fraction = 1.0f;
        b2Vec2 point;
        b2Vec2 normal;
    };
    // `categoryMask` restricts which fixture categories can be hit (e.g.
    // CAT_TERRAIN for ground checks, so a grenade underfoot isn't "ground").
    bool RayCast(b2Vec2 p1, b2Vec2 p2, RayCastResult& out, const b2Body* ignoreBody = nullptr,
                 uint16_t categoryMask = 0xFFFF);

    // Add a circle fixture (Box2D metres, body-local centre).
    void createCircleFixture(void* body, float cx, float cy, float radius,
                             float density, float friction, float restitution);
    // Apply a collision category/mask to every fixture on a body.
    void setBodyFilter(void* body, uint16_t category, uint16_t mask);
    void setBullet(void* body, bool bullet);
    void setBodyTransform(void* body, float x, float y, float angle);
    void setAngularVelocity(void* body, float w);
    void setGravityScale(void* body, float scale);
    void applyForce(void* body, float fx, float fy);

    // Ninja rope: a max-length-only distance joint from a fixed world point
    // (Box2D metres) to `body`'s centre. Slack below maxLength, taut at it.
    void* createRopeJoint(void* body, float anchorX, float anchorY, float maxLength);
    void setRopeLength(void* joint, float maxLength);
    void destroyJoint(void* joint);

    // Body accessors. Positions/velocities are in Box2D's Y-up metre space
    // (see CLAUDE.md); callers multiply by Terrain::PPM and un-flip Y to get
    // the pixel convention used everywhere else.
    Vector2 getBodyPosition(void* body);
    Vector2 getBodyVelocity(void* body);
    float getBodyAngularVelocity(void* body);
    bool isBodyAsleep(void* body);
    float getBodyMass(void* body);
    float getBodyAngle(void* body);
    void setBodyVelocity(void* body, float vx, float vy);
    void applyLinearImpulse(void* body, float impulseX, float impulseY);

    // Classify a body for contact handling and query the bodies currently
    // touching `key` (with normals/points). Bodies must be registered before
    // the first step so contact callbacks can classify them.
    void registerBody(void* body, BodyKind kind);
    BodyKind getBodyKind(void* body);
    void getContacts(void* key, std::vector<BodyContact>& out);

    // Configure a body's first-fixture restitution. Projectiles use 0 so they
    // stop on first contact; grenades use a non-zero value so Box2D ricochets
    // them with the real approach velocity.
    void configureFixture(void* body, float restitution, float restitutionThreshold);
    // Tag a body's Box2D user data. Projectiles tag their owner's body pointer
    // so the contact filter can suppress collisions between a shell and the
    // worm that fired it (see OwnerExclusionFilter).

    void setBodyUserData(void* body, void* userData);

private:
    Impl* impl_;
};
