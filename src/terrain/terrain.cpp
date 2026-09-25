#include "terrain.h"
#include "../physics/world.h"
#include "support.h"
#include "generate.h"
#include <algorithm>

namespace Terrain {

int TerrainSystem::nextAnchorId = 0;

TerrainSystem::TerrainSystem(World* w, const std::vector<std::vector<Chunk*>>& c)
    : world(w), chunks(c) {
    buildInitialFixtures();
    recomputeSupportAndThaw();
}

bool TerrainSystem::isExposed(const Chunk* c) const {
    int dirs[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (auto& d : dirs) {
        int nx = c->col + d[0], ny = c->row + d[1];
        if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) return true;
        if (chunks[ny][nx]->state != ChunkState::SOLID) return true;
    }
    return false;
}

void TerrainSystem::createFixture(Chunk* chunk) {
    if (chunk->body) return;
    const auto& props = getMaterialProps(chunk->material);
    
    // Create static body at chunk centroid (flip Y for Box2D Y-up convention)
    float bodyY = WORLD_H - chunk->centroidY / PPM;
    chunk->body = world->createBody(chunk->centroidX / PPM, bodyY, true, 0, 0, 0.75f, true);
    world->registerBody(chunk->body, World::BodyKind::Terrain);
    
    // Create polygon fixture from vertices, local to the body origin (Box2D
    // fixture vertices are body-relative, not world coordinates) and with
    // the same Y-flip applied to the body position above.
    std::vector<Vector2> verts;
    for (auto& v : chunk->verts) {
        verts.push_back((Vector2){
            (float)((v.first - chunk->centroidX) / PPM),
            (float)(-(v.second - chunk->centroidY) / PPM)
        });
    }
    chunk->fixture = world->createPolygonFixture(chunk->body, verts, props.friction);
}

void TerrainSystem::destroyChunk(Chunk* chunk) {
    if (chunk->body) {
        world->destroyBody(chunk->body);
        chunk->body = nullptr;
        chunk->fixture = nullptr;
    }
    chunk->state = ChunkState::GONE;
}

bool TerrainSystem::damageChunk(Chunk* chunk, float damage) {
    if (chunk->state != ChunkState::SOLID) return false;
    chunk->hp -= damage;
    if (chunk->hp <= 0) {
        destroyChunk(chunk);
        exposeNeighbors(chunk);
        return true;
    }
    return false;
}

void TerrainSystem::exposeNeighbors(const Chunk* chunk) {
    int dirs[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (auto& d : dirs) {
        int nx = chunk->col + d[0], ny = chunk->row + d[1];
        if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
        Chunk* n = chunks[ny][nx];
        if (n->state == ChunkState::SOLID && !n->body) {
            createFixture(n);
        }
    }
}

void TerrainSystem::buildInitialFixtures() {
    auto unsupportedIds = std::set<int>();
    for (auto& component : clusterUnsupported(chunks)) {
        for (auto* c : component) {
            unsupportedIds.insert(c->id);
        }
    }
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            Chunk* c = chunks[row][col];
            if (c->state != ChunkState::SOLID) continue;
            if (unsupportedIds.count(c->id)) continue;
            if (isExposed(c)) {
                createFixture(c);
            }
        }
    }
}

void TerrainSystem::recomputeSupportAndThaw() {
    auto components = clusterUnsupported(chunks);
    for (auto& component : components) {
        if (!component.empty()) {
            thawComponent(component);
        }
    }
}

void TerrainSystem::thawComponent(std::vector<Chunk*> component) {
    // A floating component may be larger than SLAB_CHUNK_CAP. Split it into
    // successive slabs of at most that many chunks so every chunk falls
    // instead of the tail being silently left hanging as static.
    for (size_t start = 0; start < component.size(); start += SLAB_CHUNK_CAP) {
        size_t end = std::min(start + SLAB_CHUNK_CAP, component.size());
        thawSlab(std::vector<Chunk*>(component.begin() + start, component.begin() + end));
    }
}

// Turn one slab (<= SLAB_CHUNK_CAP solid chunks) into a single dynamic body.
// The body origin is the mass-weighted centroid of the slab; every chunk in it
// shares that origin so they fall and rotate as a unit.
void TerrainSystem::thawSlab(std::vector<Chunk*> slab) {
    if (slab.empty()) return;

    float totalMass = 0, cx = 0, cy = 0;
    for (auto* c : slab) {
        // Bedrock is indestructible and always grounded, so it can never be a
        // floating chunk -- but guard here too so it is never thawed.
        if (c->material == Material::Bedrock) continue;
        const auto& props = getMaterialProps(c->material);
        float area = computeQuadArea(c->verts);
        float mass = props.density * area / ((float)PPM * PPM);
        totalMass += mass;
        cx += c->centroidX * mass;
        cy += c->centroidY * mass;
    }
    if (totalMass == 0) totalMass = 1;
    cx /= totalMass; cy /= totalMass;

    // Flip Y for Box2D Y-up convention
    float bodyY = WORLD_H - cy / PPM;
    void* body = world->createBody(cx / PPM, bodyY, false, 0.02f, 0.05f, REFREEZE_THRESHOLD);
    world->registerBody(body, World::BodyKind::Terrain);

    for (auto* c : slab) {
        if (c->material == Material::Bedrock) continue;
        if (c->body) {
            world->destroyBody(c->body);
            c->body = nullptr;
            c->fixture = nullptr;
        }
        const auto& props = getMaterialProps(c->material);
        std::vector<Vector2> verts;
        for (auto& v : c->verts) {
            verts.push_back((Vector2){
                (float)((v.first - cx) / PPM),
                (float)(-(v.second - cy) / PPM)
            });
        }
        world->createPolygonFixture(body, verts, props.density, props.friction);
        c->body = body;
        c->state = ChunkState::DYNAMIC;
        // Record the slab origin (pixel space) and initial body position so the
        // renderer can draw the chunk where its Box2D body actually is.
        c->bodyOriginX = cx; c->bodyOriginY = cy;
        c->bodyDrawX = cx; c->bodyDrawY = cy;
        c->bodyAngle = 0;
    }

    dynamicBodies.insert(body);
}

// Refresh each dynamic chunk's render position/angle from its Box2D body so the
// renderer can draw falling slabs where the physics actually puts them.
void TerrainSystem::syncDynamicChunkPositions() {
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            Chunk* c = chunks[row][col];
            if (c->state != ChunkState::DYNAMIC || !c->body) continue;
            Vector2 bp = world->getBodyPosition(c->body);
            // Un-flip Box2D's Y-up metres back to pixel space, matching
            // Worm::getPosition() / projectile.cpp.
            c->bodyDrawX = bp.x * PPM;
            // Un-flip Box2D's Y-up metres back to pixel space: (WORLD_H - bodyY) * PPM.
            c->bodyDrawY = (WORLD_H - bp.y) * PPM;
            c->bodyAngle = world->getBodyAngle(c->body);
        }
    }
}

void TerrainSystem::update(float dt) {
    // Keep falling slabs' render positions in step with the physics body.
    syncDynamicChunkPositions();
    refreezeTimer += dt;
    if (refreezeTimer >= REFREEZE_THRESHOLD) {
        refreezeTimer = 0;
        tryRefreeze();
    }
    if ((int)dynamicBodies.size() > MAX_DYNAMIC_BODIES) {
        forceFreezeOldest();
    }
}

void TerrainSystem::tryRefreeze() {
    std::vector<void*> toFreeze;
    for (auto* body : dynamicBodies) {
        // A resting body (asleep, or slow for a full refreeze interval) becomes
        // static terrain where it landed. A body still falling/sliding is left
        // to keep falling.
        if (world->isBodyAsleep(body)) {
            toFreeze.push_back(body);
            restFrames.erase(body);
            continue;
        }
        if (isBodySlow(body)) {
            // Freeze only once it has read slow on the previous tick too, so a
            // momentary near-stop at a bounce apex can't trap it mid-air.
            if (restFrames.count(body)) toFreeze.push_back(body);
            restFrames[body]++;
        } else {
            restFrames.erase(body);
        }
    }
    for (auto* body : toFreeze) {
        restFrames.erase(body);
        freezeBody(body);
    }
}

// A body is "slow" when both its linear and angular speed have fallen below
// small thresholds. This is the fallback for bodies Box2D never puts to sleep
// (e.g. a very light slab); asleep is the primary resting signal.
bool TerrainSystem::isBodySlow(void* body) {
    Vector2 v = world->getBodyVelocity(body);
    float speed = sqrtf(v.x * v.x + v.y * v.y);
    float angVel = fabsf(world->getBodyAngularVelocity(body));
    return speed < REFREEZE_SPEED_THRESH && angVel < REFREEZE_ANG_THRESH;
}

// Turn a resting dynamic slab into static terrain at exactly where it landed:
// destroy the shared dynamic body, rebake every chunk's vertices into its
// current world position (the slab may have rotated while falling), and give
// each chunk a fresh static body + fixture at that position. The chunks are
// marked landed/SOLID so support.cpp treats them as permanent ground and the
// renderer draws them from their new pixel verts.
void TerrainSystem::freezeBody(void* body) {
    dynamicBodies.erase(body);
    std::vector<Chunk*> chunkList = getChunksForBody(body);
    if (chunkList.empty()) return;

    // Destroy the shared dynamic body once (outside step()/a callback -- this
    // runs in update(), after world->step()).
    world->destroyBody(body);
    for (auto* c : chunkList) c->body = nullptr;

    // Current resting transform of the slab in Box2D's Y-up metre space.
    Vector2 bp = world->getBodyPosition(body);
    float angle = world->getBodyAngle(body);
    float ca = cosf(angle), sa = sinf(angle);

    for (auto* c : chunkList) {
        // The chunk's verts are still in original pixel space; transform each
        // one by the body's current position + rotation to find where it is
        // now. Local metres relative to the slab origin match how the dynamic
        // fixture was built, so rotating by the body angle reproduces the
        // current world position.
        std::vector<std::pair<float, float>> newVerts;
        float sx = 0, sy = 0;
        for (auto& v : c->verts) {
            float lx = (v.first - c->bodyOriginX) / PPM;
            float ly = -(v.second - c->bodyOriginY) / PPM;
            float wx = ca * lx - sa * ly + bp.x;
            float wy = sa * lx + ca * ly + bp.y;
            // Box2D metres (Y-up) back to pixel space (Y-down).
            float px = wx * PPM;
            float py = (WORLD_H - wy) * PPM;
            newVerts.push_back({px, py});
            sx += px; sy += py;
        }
        float ncx = sx / newVerts.size();
        float ncy = sy / newVerts.size();
        c->verts = std::move(newVerts);
        c->centroidX = ncx; c->centroidY = ncy;
        c->bodyOriginX = ncx; c->bodyOriginY = ncy;
        c->bodyDrawX = ncx; c->bodyDrawY = ncy;
        c->bodyAngle = 0;
        c->landed = true;
        c->state = ChunkState::SOLID;
    }

    // Each chunk gets its own static body at its landed centroid. createFixture
    // builds the fixture with verts local to that centroid (Box2D vertices are
    // body-relative, Y-flipped), matching the CLAUDE.md conventions.
    for (auto* c : chunkList) {
        createFixture(c);
    }
}

std::vector<Chunk*> TerrainSystem::getChunksForBody(void* body) const {
    std::vector<Chunk*> result;
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            if (chunks[row][col]->body == body) {
                result.push_back(chunks[row][col]);
            }
        }
    }
    return result;
}

void TerrainSystem::forceFreezeOldest() {
    std::vector<void*> bodies(dynamicBodies.begin(), dynamicBodies.end());
    // Sort by anchor ID (simplified)
    while ((int)dynamicBodies.size() > MAX_DYNAMIC_BODIES && !bodies.empty()) {
        freezeBody(bodies.front());
        bodies.erase(bodies.begin());
    }
}

int TerrainSystem::getDynamicCount() const { return (int)dynamicBodies.size(); }

int TerrainSystem::getStaticCount() const {
    int count = 0;
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            if (chunks[row][col]->state == ChunkState::SOLID && chunks[row][col]->body) count++;
        }
    }
    return count;
}

float computeQuadArea(const std::vector<std::pair<float, float>>& verts) {
    float area = 0;
    for (size_t i = 0; i < verts.size(); i++) {
        size_t j = (i + 1) % verts.size();
        area += verts[i].first * verts[j].second;
        area -= verts[j].first * verts[i].second;
    }
    return fabsf(area) / 2.0f;
}

} // namespace Terrain
