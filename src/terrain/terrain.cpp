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
    int cap = std::min((int)component.size(), SLAB_CHUNK_CAP);
    if (cap == 0) return;

    float totalMass = 0, cx = 0, cy = 0;
    for (int i = 0; i < cap; i++) {
        Chunk* c = component[i];
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

    for (int i = 0; i < cap; i++) {
        Chunk* c = component[i];
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
    }

    dynamicBodies.insert(body);
}

void TerrainSystem::update(float dt) {
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
        // In a real implementation, check if body is awake
        toFreeze.push_back(body);
    }
    for (auto* body : toFreeze) {
        freezeBody(body);
    }
}

void TerrainSystem::freezeBody(void* body) {
    dynamicBodies.erase(body);
    std::vector<Chunk*> chunkList = getChunksForBody(body);
    for (auto* c : chunkList) {
        c->state = ChunkState::SOLID;
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
