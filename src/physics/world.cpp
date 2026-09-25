#include "world.h"
#include <box2d/b2_body.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_polygon_shape.h>
#include <box2d/b2_world.h>
#include <box2d/b2_world_callbacks.h>
#include <vector>
#include <memory>

namespace {
// Collects the closest non-ignored fixture hit from a world ray-cast. Returning
// the hit fraction makes Box2D clip to it, so the first (closest) fixture wins;
// returning -1 skips a fixture (used to ignore the querying body itself).
class RayCastResultCallback : public b2RayCastCallback {
public:
    const b2Body* ignore;
    World::RayCastResult result;
    explicit RayCastResultCallback(const b2Body* ignoreBody) : ignore(ignoreBody) {}
    float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override {
        if (fixture->GetBody() == ignore) return -1.0f;
        result.body = fixture->GetBody();
        result.fixture = fixture;
        result.point = point;
        result.normal = normal;
        result.fraction = fraction;
        return fraction;
    }
};
} // namespace

class World::Impl {
public:
    b2World* world = nullptr;
    std::vector<b2Body*> bodies;
    
    Impl(float gx, float gy) {
        b2Vec2 gravity(gx, gy);
        world = new b2World(gravity);
        world->SetAllowSleeping(true);
        world->SetWarmStarting(true);
    }
    
    ~Impl() {
        delete world;
    }
};

World::World(float gravityX, float gravityY) : impl_(new Impl(gravityX, gravityY)) {}

World::~World() {
    delete static_cast<Impl*>(impl_);
}

void* World::createBody(float x, float y, bool fixedRotation, 
                        float linearDamping, float angularDamping, 
                        float sleepThreshold, bool isStatic) {
    auto* impl = static_cast<Impl*>(impl_);
    b2BodyDef def;
    def.type = isStatic ? b2_staticBody : b2_dynamicBody;
    def.position.Set(x, y);
    def.fixedRotation = fixedRotation;
    def.linearDamping = linearDamping;
    def.angularDamping = angularDamping;
    (void)sleepThreshold;
    
    b2Body* body = impl->world->CreateBody(&def);
    impl->bodies.push_back(body);
    return static_cast<void*>(body);
}

void World::destroyBody(void* body) {
    if (!body) return;
    auto* impl = static_cast<Impl*>(impl_);
    b2Body* b = static_cast<b2Body*>(body);
    
    for (auto it = impl->bodies.begin(); it != impl->bodies.end(); ++it) {
        if (*it == b) {
            impl->world->DestroyBody(b);
            impl->bodies.erase(it);
            break;
        }
    }
}

void* World::createPolygonFixture(void* body, const std::vector<Vector2>& verts, float friction) {
    return createPolygonFixture(body, verts, 1.0f, friction);
}

void* World::createPolygonFixture(void* body, const std::vector<Vector2>& verts, float density, float friction) {
    if (!body || verts.size() < 3) return nullptr;
    
    auto* impl = static_cast<Impl*>(impl_);
    b2Body* b = static_cast<b2Body*>(body);
    
    b2FixtureDef def;
    def.density = density;
    def.friction = friction;
    
    std::vector<b2Vec2> points(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        points[i].Set(verts[i].x, verts[i].y);
    }
    
    b2PolygonShape shape;
    shape.Set(points.data(), (int)points.size());
    def.shape = &shape;
    
    b->CreateFixture(&def);
    return nullptr;
}

bool World::RayCast(b2Vec2 p1, b2Vec2 p2, RayCastResult& out, const b2Body* ignoreBody) {
    auto* impl = static_cast<Impl*>(impl_);
    RayCastResultCallback cb(ignoreBody);
    // b2World::RayCast() reports hits through the callback and returns void,
    // so a populated result body means we found ground.
    impl->world->RayCast(&cb, p1, p2);
    if (cb.result.body) out = cb.result;
    return cb.result.body != nullptr;
}

void World::step(float dt, int velocityIterations, int positionIterations) {
    auto* impl = static_cast<Impl*>(impl_);
    impl->world->Step(dt, velocityIterations, positionIterations);
}

void setupCategories(World* world) {
    (void)world;
}
