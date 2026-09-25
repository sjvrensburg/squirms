#include "world.h"
#include <box2d/b2_body.h>
#include <box2d/b2_fixture.h>
#include <box2d/b2_polygon_shape.h>
#include <box2d/b2_circle_shape.h>
#include <box2d/b2_distance_joint.h>
#include <box2d/b2_world.h>
#include <box2d/b2_world_callbacks.h>
#include <box2d/b2_contact.h>
#include <box2d/b2_collision.h>
#include <vector>
#include <memory>
#include <map>
#include <cstdint>

namespace {
// Collects the closest non-ignored fixture hit from a world ray-cast. Returning
// the hit fraction makes Box2D clip to it, so the first (closest) fixture wins;
// returning -1 skips a fixture (used to ignore the querying body itself).
class RayCastResultCallback : public b2RayCastCallback {
public:
    const b2Body* ignore;
    uint16_t mask;
    World::RayCastResult result;
    RayCastResultCallback(const b2Body* ignoreBody, uint16_t m) : ignore(ignoreBody), mask(m) {}
    float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override {
        if (fixture->GetBody() == ignore) return -1.0f;
        if ((fixture->GetFilterData().categoryBits & mask) == 0) return -1.0f;
        if (fixture->IsSensor()) return -1.0f;
        result.body = fixture->GetBody();
        result.fixture = fixture;
        result.point = point;
        result.normal = normal;
        result.fraction = fraction;
        return fraction;
    }
};
} // namespace

// A body registry + contact recorder so weapons can classify contact hits
// (worm vs terrain vs projectile) and resolve them *outside* Box2D callbacks,
// which Box2D forbids destroying bodies from inside.
class World::Impl {
public:
    b2World* world = nullptr;
    std::vector<b2Body*> bodies;
    b2ContactListener* listener = nullptr;
    b2ContactFilter* filter = nullptr;
    std::map<void*, BodyKind> bodyKinds;
    std::map<void*, std::map<void*, BodyContact>> contacts;
    b2Body* ground = nullptr;   // fixture-less static body that rope joints hang from

    Impl(float gx, float gy);
    ~Impl();
};

namespace {
// Records every contact so weapons can resolve them *outside* the callback
// (Box2D forbids destroying bodies from inside a contact callback). We keep,
// per body, the set of bodies it is currently touching along with the normal
// and contact point of the most recent BeginContact with each.
class WorldContactListener : public b2ContactListener {
public:
    World::Impl* owner;
    explicit WorldContactListener(World::Impl* o) : owner(o) {}
    void BeginContact(b2Contact* contact) override {
        b2Body* a = contact->GetFixtureA()->GetBody();
        b2Body* b = contact->GetFixtureB()->GetBody();
        b2WorldManifold mf;
        contact->GetWorldManifold(&mf);
        World::BodyContact ca;
        ca.body = b;
        ca.normal = mf.normal;
        ca.point = mf.points[0];
        owner->contacts[a][b] = ca;
        World::BodyContact cb;
        cb.body = a;
        cb.normal = mf.normal;
        cb.point = mf.points[0];
        owner->contacts[b][a] = cb;
    }
    void EndContact(b2Contact* contact) override {
        b2Body* a = contact->GetFixtureA()->GetBody();
        b2Body* b = contact->GetFixtureB()->GetBody();
        owner->contacts[a].erase(b);
        owner->contacts[b].erase(a);
    }
};

// Suppresses contacts between a shell and the worm that fired it. Each shell
// tags its body with the owner's b2Body* in Box2D user data; every other body
// (the owner, worms, terrain) leaves that pointer null. This is evaluated live
// at contact time, so it never mutates persistent collision masks: it is
// correct regardless of how many shells have been fired, by whom, or in what
// order, and regardless of how the category counter cycles.
class OwnerExclusionFilter : public b2ContactFilter {
public:
    bool ShouldCollide(b2Fixture* a, b2Fixture* b) override {
        // Default category/mask + groupIndex behavior for everything else.
        if (!b2ContactFilter::ShouldCollide(a, b)) return false;
        b2Body* bodyA = a->GetBody();
        b2Body* bodyB = b->GetBody();
        void* ownerA = reinterpret_cast<void*>(bodyA->GetUserData().pointer);
        void* ownerB = reinterpret_cast<void*>(bodyB->GetUserData().pointer);
        if (ownerA && bodyB == static_cast<b2Body*>(ownerA)) return false;
        if (ownerB && bodyA == static_cast<b2Body*>(ownerB)) return false;
        return true;
    }
};
} // namespace

World::Impl::Impl(float gx, float gy) {
    b2Vec2 gravity(gx, gy);
    world = new b2World(gravity);
    world->SetAllowSleeping(true);
    world->SetWarmStarting(true);
    listener = new WorldContactListener(this);
    world->SetContactListener(listener);
    filter = new OwnerExclusionFilter();
    world->SetContactFilter(filter);
    b2BodyDef gd;
    gd.type = b2_staticBody;
    ground = world->CreateBody(&gd);
}

World::Impl::~Impl() {
    delete filter;
    delete world;
}

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

    // Drop any stale contact/kind records for the destroyed body so a hit can
    // never be classified against a body that no longer exists.
    impl->contacts.erase(b);
    for (auto& kv : impl->contacts) kv.second.erase(b);
    impl->bodyKinds.erase(b);
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

bool World::RayCast(b2Vec2 p1, b2Vec2 p2, RayCastResult& out, const b2Body* ignoreBody,
                    uint16_t categoryMask) {
    auto* impl = static_cast<Impl*>(impl_);
    RayCastResultCallback cb(ignoreBody, categoryMask);
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

Vector2 World::getBodyPosition(void* body) {
    b2Body* b = static_cast<b2Body*>(body);
    b2Vec2 p = b->GetPosition();
    return Vector2{p.x, p.y};
}

Vector2 World::getBodyVelocity(void* body) {
    b2Body* b = static_cast<b2Body*>(body);
    b2Vec2 v = b->GetLinearVelocity();
    return Vector2{v.x, v.y};
}

float World::getBodyAngularVelocity(void* body) {
    b2Body* b = static_cast<b2Body*>(body);
    return b->GetAngularVelocity();
}

bool World::isBodyAsleep(void* body) {
    b2Body* b = static_cast<b2Body*>(body);
    return !b->IsAwake();
}

float World::getBodyMass(void* body) {
    b2Body* b = static_cast<b2Body*>(body);
    return b->GetMass();
}

float World::getBodyAngle(void* body) {
    b2Body* b = static_cast<b2Body*>(body);
    return b->GetAngle();
}

void World::setBodyVelocity(void* body, float vx, float vy) {
    b2Body* b = static_cast<b2Body*>(body);
    b->SetLinearVelocity(b2Vec2(vx, vy));
    b->SetAwake(true);
}

void World::applyLinearImpulse(void* body, float impulseX, float impulseY) {
    b2Body* b = static_cast<b2Body*>(body);
    b->ApplyLinearImpulse(b2Vec2(impulseX, impulseY), b->GetWorldCenter(), true);
}

void World::registerBody(void* body, BodyKind kind) {
    if (!body) return;
    static_cast<Impl*>(impl_)->bodyKinds[body] = kind;
}

World::BodyKind World::getBodyKind(void* body) {
    auto* impl = static_cast<Impl*>(impl_);
    auto it = impl->bodyKinds.find(body);
    return it == impl->bodyKinds.end() ? BodyKind::Unknown : it->second;
}

void World::getContacts(void* key, std::vector<BodyContact>& out) {
    auto* impl = static_cast<Impl*>(impl_);
    auto it = impl->contacts.find(key);
    if (it == impl->contacts.end()) return;
    for (auto& kv : it->second) {
        out.push_back(kv.second);
    }
}

void World::configureFixture(void* body, float restitution, float restitutionThreshold) {
    if (!body) return;
    b2Body* b = static_cast<b2Body*>(body);
    b2Fixture* fx = b->GetFixtureList();
    if (!fx) return;
    fx->SetRestitution(restitution);
    fx->SetRestitutionThreshold(restitutionThreshold);
}

void World::setBodyUserData(void* body, void* userData) {
    if (!body) return;
    b2Body* b = static_cast<b2Body*>(body);
    b->GetUserData().pointer = reinterpret_cast<uintptr_t>(userData);
}

void World::createCircleFixture(void* body, float cx, float cy, float radius,
                                float density, float friction, float restitution) {
    if (!body) return;
    b2CircleShape shape;
    shape.m_p.Set(cx, cy);
    shape.m_radius = radius;
    b2FixtureDef def;
    def.shape = &shape;
    def.density = density;
    def.friction = friction;
    def.restitution = restitution;
    def.restitutionThreshold = 0.5f;
    static_cast<b2Body*>(body)->CreateFixture(&def);
}

void World::setBodyFilter(void* body, uint16_t category, uint16_t mask) {
    if (!body) return;
    b2Filter f;
    f.categoryBits = category;
    f.maskBits = mask;
    for (b2Fixture* fx = static_cast<b2Body*>(body)->GetFixtureList(); fx; fx = fx->GetNext()) {
        fx->SetFilterData(f);
    }
}

void World::setBullet(void* body, bool bullet) {
    if (body) static_cast<b2Body*>(body)->SetBullet(bullet);
}

void World::setBodyTransform(void* body, float x, float y, float angle) {
    if (!body) return;
    b2Body* b = static_cast<b2Body*>(body);
    b->SetTransform(b2Vec2(x, y), angle);
    b->SetAwake(true);
}

void World::setAngularVelocity(void* body, float w) {
    if (body) static_cast<b2Body*>(body)->SetAngularVelocity(w);
}

void World::setGravityScale(void* body, float scale) {
    if (body) static_cast<b2Body*>(body)->SetGravityScale(scale);
}

void World::applyForce(void* body, float fx, float fy) {
    if (!body) return;
    b2Body* b = static_cast<b2Body*>(body);
    b->ApplyForceToCenter(b2Vec2(fx, fy), true);
}

void* World::createRopeJoint(void* body, float anchorX, float anchorY, float maxLength) {
    auto* impl = static_cast<Impl*>(impl_);
    b2Body* b = static_cast<b2Body*>(body);
    b2DistanceJointDef def;
    def.Initialize(impl->ground, b, b2Vec2(anchorX, anchorY), b->GetWorldCenter());
    def.collideConnected = false;
    def.minLength = 0.0f;
    def.maxLength = maxLength;
    def.length = maxLength;
    def.stiffness = 0.0f;   // rigid at the limit: a rope, not a spring
    def.damping = 0.0f;
    return impl->world->CreateJoint(&def);
}

void World::setRopeLength(void* joint, float maxLength) {
    if (!joint) return;
    auto* j = static_cast<b2DistanceJoint*>(joint);
    j->SetLength(maxLength);
    j->SetMaxLength(maxLength);
    j->GetBodyB()->SetAwake(true);
}

void World::destroyJoint(void* joint) {
    if (!joint) return;
    static_cast<Impl*>(impl_)->world->DestroyJoint(static_cast<b2Joint*>(joint));
}
