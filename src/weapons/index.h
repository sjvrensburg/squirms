#pragma once
#include <string>
#include <vector>

class World;
namespace Terrain { struct Chunk; class TerrainSystem; }
using ChunkGrid = std::vector<std::vector<Terrain::Chunk*>>;
namespace Entities { class Worm; class Projectile; }

namespace Weapons {

class Bazooka {
public:
    std::string name = "bazooka";
    float radius = 1.9f;
    float damage = 45.0f;
    // Launch speed [px/s] scales with charge: a tap is a weak lob, a full
    // charge a hard shot.
    float minSpeed = 150.0f;
    float maxSpeed = 520.0f;
    Entities::Projectile fire(const Entities::Worm* worm, float angle, float power, World* world,
              Terrain::TerrainSystem* terrain, float wind);
};

class Grenade {
public:
    std::string name = "grenade";
    float radius = 1.7f;
    float damage = 50.0f;
    int fuseTime = 3;
    float minSpeed = 120.0f;
    float maxSpeed = 420.0f;
    Entities::Projectile fire(const Entities::Worm* worm, float angle, float power, World* world,
              Terrain::TerrainSystem* terrain, float wind);
};

} // namespace Weapons

struct WeaponList {
    Weapons::Bazooka bazooka;
    Weapons::Grenade grenade;
};

extern WeaponList weapons;
