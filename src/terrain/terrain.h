#pragma once
#include <vector>
#include <set>
#include <map>
#include "grid.h"

class World;

namespace Terrain {

constexpr int MAX_DYNAMIC_BODIES = 120;
constexpr float REFREEZE_THRESHOLD = 0.75f;
// A dynamic slab is only frozen once its linear and angular speed drop below
// these small values (Box2D's own asleep flag also counts as "resting").
constexpr float REFREEZE_SPEED_THRESH = 0.15f;
constexpr float REFREEZE_ANG_THRESH = 0.30f;
constexpr int SLAB_CHUNK_CAP = 150;

class TerrainSystem {
public:
    World* world;
    std::vector<std::vector<Chunk*>> chunks;
    std::set<void*> dynamicBodies;
    float refreezeTimer = 0;
    bool debugAllFixtures = false;
    static int nextAnchorId;
    // How many refreeze intervals each still-moving body has read below the
    // speed threshold. A body freezes once this has held for one interval
    // (i.e. it was slow both last tick and this one), so a momentary
    // near-stop at the apex of a bounce can't freeze it mid-air. The Box2D
    // asleep flag bypasses this and freezes immediately.
    std::map<void*, int> restFrames;

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
    bool isBodySlow(void* body);
    void freezeBody(void* body);
    std::vector<Chunk*> getChunksForBody(void* body) const;
    void forceFreezeOldest();
};

float computeQuadArea(const std::vector<std::pair<float, float>>& verts);

} // namespace Terrain
