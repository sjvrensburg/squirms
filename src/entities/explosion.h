#pragma once
#include <vector>
#include <raylib.h>
#include "../terrain/terrain.h"
#include "../entities/worm.h"

class World;

namespace Entities {

class Explosion {
public:
    float x, y, radius, damage;
    float age, maxAge;
    bool alive;
    
    Explosion(float x, float y, float radius, float damage);
    void update(float dt);
    bool isAlive() const { return alive; }
};

} // namespace Entities

void createExplosion(World* world, float x, float y, float radius, float damage,
                     Terrain::TerrainSystem* terrain, const std::vector<std::vector<Entities::Worm*>>& worms);
