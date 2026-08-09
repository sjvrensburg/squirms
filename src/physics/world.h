#pragma once
#include <raylib.h>
#include <vector>

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
    
private:
    Impl* impl_;
};

void setupCategories(World* world);
