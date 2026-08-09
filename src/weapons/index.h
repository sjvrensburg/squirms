#pragma once
#include <string>
#include <vector>

class World;
namespace Terrain { struct Chunk; }
using ChunkGrid = std::vector<std::vector<Terrain::Chunk*>>;
namespace Entities { class Worm; }

namespace Weapons {

class Bazooka {
public:
    std::string name = "bazooka";
    float radius = 1.9f;
    float damage = 45.0f;
    void fire(const Entities::Worm* worm, float angle, float power, World* world, 
              const ChunkGrid& chunks, const std::vector<std::vector<Entities::Worm*>>& worms);
};

class Grenade {
public:
    std::string name = "grenade";
    float radius = 1.7f;
    float damage = 50.0f;
    int fuseTime = 3;
    void fire(const Entities::Worm* worm, float angle, float power, World* world,
              const ChunkGrid& chunks, const std::vector<std::vector<Entities::Worm*>>& worms);
};

} // namespace Weapons

struct WeaponList {
    Weapons::Bazooka bazooka;
    Weapons::Grenade grenade;
};

extern WeaponList weapons;
