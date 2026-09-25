#pragma once
#include <vector>
#include <set>
#include <map>
#include <raylib.h>
#include "grid.h"

class World;

namespace Terrain {

constexpr int MAX_DYNAMIC_BODIES = 120;
constexpr float REFREEZE_THRESHOLD = 0.75f;
// A dynamic slab is only frozen once its linear and angular speed drop below
// these small values (Box2D's own asleep flag also counts as "resting").
constexpr float REFREEZE_SPEED_THRESH = 0.15f;
constexpr float REFREEZE_ANG_THRESH = 0.30f;
constexpr int SLAB_CHUNK_CAP = 300;
// A falling slab of at least SHATTER_MIN_CHUNKS whose velocity changes by more
// than SHATTER_DV m/s in one tick (a hard landing) breaks into smaller pieces.
constexpr float SHATTER_DV = 5.5f;
constexpr int SHATTER_MIN_CHUNKS = 10;

// A thud from falling terrain, for dust/shake/sound (pixel space).
struct TerrainImpact { float x, y, strength; bool shattered; };

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
    // Grid chunks that stopped being part of the static landscape this tick
    // (destroyed, or thawed into a falling slab). The terrain renderer drains
    // this to erase them from its cached texture.
    std::vector<Chunk*> dirty;
    // Rubble that just came to rest (frozen where it landed), and rubble that
    // just left (destroyed or thawed again). The renderer bakes/erases these
    // in its terrain texture.
    std::vector<Chunk*> rubbleAdded, rubbleRemoved;
    // Pixel-space points where a falling slab sank into the sea this tick
    // (the game turns these into splashes).
    std::vector<std::pair<float, float>> sunk;
    // Hard landings of falling slabs this tick.
    std::vector<TerrainImpact> impacts;

    TerrainSystem(World* w, const std::vector<std::vector<Chunk*>>& c);
    // Frees the chunks (allocated by buildGrid). Their Box2D bodies belong to
    // the World, which the owner destroys separately.
    ~TerrainSystem();
    TerrainSystem(const TerrainSystem&) = delete;
    TerrainSystem& operator=(const TerrainSystem&) = delete;
    void buildInitialFixtures();
    void recomputeSupportAndThaw();
    void update(float dt);
    bool damageChunk(Chunk* chunk, float damage);
    // Knock a group of solid chunks (grid or landed rubble) loose as one
    // falling body and return it (nullptr if nothing was thawed). Used for
    // explosion ejecta; the caller gives it a velocity.
    void* breakOff(const std::vector<Chunk*>& group);
    // Wake landed rubble near a point (explosions shake it loose).
    void shakeLoose(float x, float y, float radius);
    bool isDynamicBody(void* body) const { return dynamicBodies.count(body) != 0; }
    int getDynamicCount() const;
    int getStaticCount() const;

private:
    bool isExposed(const Chunk* c) const;
    void createFixture(Chunk* chunk);
    void destroyChunk(Chunk* chunk);
    void exposeNeighbors(const Chunk* chunk);
    void thawComponent(std::vector<Chunk*> component);
    void* thawSlab(std::vector<Chunk*> slab);
    // Centroids of chunks that were destroyed or thawed since the last
    // support pass: rubble resting on them must be re-checked.
    std::vector<std::pair<float, float>> recentChanges;
    std::map<void*, Vector2> lastVel;       // per dynamic body, for impact detection
    std::map<void*, float> bodyAge;
    std::map<void*, int> bodyChunks;        // chunk count per dynamic body
    void wakeLanded(const std::vector<std::pair<float, float>>& around, float reach);
    void rebakeToWorld(void* body, const std::vector<Chunk*>& list);
    void detectImpacts();
    void shatter(void* body, Vector2 vel, float angVel);
    void syncDynamicChunkPositions();
    void tryRefreeze();
    bool isBodySlow(void* body);
    void freezeBody(void* body);
    std::vector<Chunk*> getChunksForBody(void* body) const;
    void forceFreezeOldest();
    void sinkDrownedSlabs();
};

float computeQuadArea(const std::vector<std::pair<float, float>>& verts);

} // namespace Terrain
