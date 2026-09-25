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
    enum class BodyKind { Unknown, Terrain, Worm, Projectile };

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
    bool RayCast(b2Vec2 p1, b2Vec2 p2, RayCastResult& out, const b2Body* ignoreBody = nullptr);

    // Body accessors. Positions/velocities are in Box2D's Y-up metre space
    // (see CLAUDE.md); callers multiply by Terrain::PPM and un-flip Y to get
    // the pixel convention used everywhere else.
    Vector2 getBodyPosition(void* body);
    Vector2 getBodyVelocity(void* body);
    float getBodyMass(void* body);
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
