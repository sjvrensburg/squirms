#pragma once
#include <raylib.h>
#include <vector>

// Box2D headers for the ray-cast API exposed by World::RayCast().
#include <box2d/b2_world.h>
#include <box2d/b2_world_callbacks.h>

class World {
public:
    class Impl;
    
    World(float gravityX, float gravityY);
    ~World();
    
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

private:
    Impl* impl_;
};

void setupCategories(World* world);
