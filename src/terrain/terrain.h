#pragma once
#include <vector>
#include <set>
#include "grid.h"

class World;

namespace Terrain {

constexpr int MAX_DYNAMIC_BODIES = 120;
constexpr float REFREEZE_THRESHOLD = 0.75f;
constexpr int SLAB_CHUNK_CAP = 150;

class TerrainSystem {
public:
    World* world;
    std::vector<std::vector<Chunk*>> chunks;
    std::set<void*> dynamicBodies;
    float refreezeTimer = 0;
    bool debugAllFixtures = false;
    static int nextAnchorId;

    TerrainSystem(World* w, const std::vector<std::vector<Chunk*>>& c);
    void buildInitialFixtures();
    void recomputeSupportAndThaw();
    void update(float dt);
    bool damageChunk(Chunk* chunk, float damage);
    int getDynamicCount() const;
    int getStaticCount() const;

private:
    bool isExposed(const Chunk* c) const;
    void createFixture(Chunk* chunk);
    void destroyChunk(Chunk* chunk);
    void exposeNeighbors(const Chunk* chunk);
    void thawComponent(std::vector<Chunk*> component);
    void thawSlab(std::vector<Chunk*> slab);
    void syncDynamicChunkPositions();
    void tryRefreeze();
    void freezeBody(void* body);
    std::vector<Chunk*> getChunksForBody(void* body) const;
    void forceFreezeOldest();
};

float computeQuadArea(const std::vector<std::pair<float, float>>& verts);

} // namespace Terrain
