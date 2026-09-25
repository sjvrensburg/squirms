#include "terrain.h"
#include "../physics/world.h"
#include "support.h"
#include "generate.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace Terrain {

int TerrainSystem::nextAnchorId = 0;

TerrainSystem::TerrainSystem(World* w, const std::vector<std::vector<Chunk*>>& c)
    : world(w), chunks(c) {
    buildInitialFixtures();
    recomputeSupportAndThaw();
}

TerrainSystem::~TerrainSystem() {
    for (auto& row : chunks)
        for (auto* c : row) delete c;
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
    if (chunk->state == ChunkState::SOLID && !chunk->landed) dirty.push_back(chunk);
    if (chunk->state == ChunkState::SOLID && chunk->landed) rubbleRemoved.push_back(chunk);
    recentChanges.push_back({chunk->centroidX, chunk->centroidY});
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
    // Landed rubble no longer sits in its grid cell, so its old grid
    // neighbours aren't actually exposed by it going away.
    if (chunk->landed) return;
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
    // Rubble that was resting on anything that just vanished or started
    // falling has lost its footing too: wake it so it drops.
    if (!recentChanges.empty()) {
        auto around = std::move(recentChanges);
        recentChanges.clear();
        wakeLanded(around, CELL * 1.9f);
        recentChanges.clear();
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
// shares that origin so they fall and rotate as a unit. Works for grid chunks
// and for landed rubble alike (both keep world-pixel `verts`).
void* TerrainSystem::thawSlab(std::vector<Chunk*> slab) {
    slab.erase(std::remove_if(slab.begin(), slab.end(), [](Chunk* c) {
                   // Bedrock is indestructible and always grounded.
                   return c->material == Material::Bedrock || c->state != ChunkState::SOLID;
               }),
               slab.end());
    if (slab.empty()) return nullptr;

    float totalMass = 0, cx = 0, cy = 0;
    for (auto* c : slab) {
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
        if (!c->landed) dirty.push_back(c);
        else rubbleRemoved.push_back(c);
        recentChanges.push_back({c->centroidX, c->centroidY});
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
    // Grid chunks left behind next to the break may have been buried
    // (no fixture of their own); they're exposed now.
    for (auto* c : slab) exposeNeighbors(c);

    dynamicBodies.insert(body);
    bodyChunks[body] = (int)slab.size();
    bodyAge[body] = 0;
    return body;
}

void* TerrainSystem::breakOff(const std::vector<Chunk*>& group) {
    return thawSlab(group);
}

void TerrainSystem::shakeLoose(float x, float y, float radius) {
    wakeLanded({{x, y}}, radius);
}

// Thaw landed rubble within `reach` of any of the given points, together
// with every piece of rubble touching those (so a pile comes down as a pile).
void TerrainSystem::wakeLanded(const std::vector<std::pair<float, float>>& around, float reach) {
    // Spatial hash of landed rubble by centroid cell.
    std::unordered_map<long long, std::vector<Chunk*>> hash;
    auto key = [](int cx, int cy) { return ((long long)cx << 32) ^ (unsigned)cy; };
    for (auto& row : chunks)
        for (auto* c : row)
            if (c->state == ChunkState::SOLID && c->landed)
                hash[key((int)floorf(c->centroidX / CELL), (int)floorf(c->centroidY / CELL))].push_back(c);
    if (hash.empty()) return;

    auto near = [&](float x, float y, float r, std::vector<Chunk*>& out) {
        int span = (int)ceilf(r / CELL);
        int cx = (int)floorf(x / CELL), cy = (int)floorf(y / CELL);
        for (int dy = -span; dy <= span; dy++)
            for (int dx = -span; dx <= span; dx++) {
                auto it = hash.find(key(cx + dx, cy + dy));
                if (it == hash.end()) continue;
                for (auto* c : it->second) {
                    float ex = c->centroidX - x, ey = c->centroidY - y;
                    if (ex * ex + ey * ey < r * r) out.push_back(c);
                }
            }
    };

    std::vector<Chunk*> seeds;
    for (auto& p : around) near(p.first, p.second, reach, seeds);
    if (seeds.empty()) return;

    std::set<Chunk*> seen;
    for (auto* s : seeds) {
        if (seen.count(s)) continue;
        std::vector<Chunk*> cluster;
        std::queue<Chunk*> q;
        q.push(s);
        seen.insert(s);
        while (!q.empty()) {
            Chunk* c = q.front(); q.pop();
            cluster.push_back(c);
            std::vector<Chunk*> nb;
            near(c->centroidX, c->centroidY, CELL * 1.7f, nb);
            for (auto* n : nb) {
                if (seen.insert(n).second) q.push(n);
            }
        }
        thawComponent(cluster);
    }
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
            c->bodyDrawY = (WORLD_H - bp.y) * PPM;
            c->bodyAngle = world->getBodyAngle(c->body);
        }
    }
}

void TerrainSystem::update(float dt) {
    for (auto& a : bodyAge) a.second += dt;
    detectImpacts();
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
    sinkDrownedSlabs();
    // Anything newly thawed (shattering) or rubble whose support fell away.
    if (!recentChanges.empty()) recomputeSupportAndThaw();
}

// A sharp change in a falling slab's velocity means it just hit something.
// Report the thud, and break big slabs that land hard into smaller pieces.
void TerrainSystem::detectImpacts() {
    struct Hit { void* body; Vector2 vel; float ang; };
    std::vector<Hit> breakers;
    for (auto* body : dynamicBodies) {
        Vector2 v = world->getBodyVelocity(body);
        auto it = lastVel.find(body);
        if (it != lastVel.end()) {
            float dx = v.x - it->second.x, dy = v.y - it->second.y;
            float dv = sqrtf(dx * dx + dy * dy);
            if (dv > 2.5f) {
                Vector2 bp = world->getBodyPosition(body);
                int n = bodyChunks.count(body) ? bodyChunks[body] : 1;
                bool breaks = dv > SHATTER_DV && n >= SHATTER_MIN_CHUNKS && bodyAge[body] > 0.15f;
                impacts.push_back({bp.x * PPM, (WORLD_H - bp.y) * PPM, dv * sqrtf((float)n), breaks});
                if (breaks) breakers.push_back({body, v, world->getBodyAngularVelocity(body)});
            }
        }
        lastVel[body] = v;
    }
    for (auto& b : breakers) shatter(b.body, b.vel, b.ang);
}

// Bake a dynamic body's current transform into its chunks' pixel verts and
// centroids, and destroy the body. The chunks are left SOLID + landed with no
// collision body; the caller freezes or re-thaws them.
void TerrainSystem::rebakeToWorld(void* body, const std::vector<Chunk*>& list) {
    // Capture the transform *before* destroying the body: destroyBody
    // returns the b2Body* to Box2D's block allocator, so reading it
    // afterwards would be a use-after-free. This runs in update(), after
    // world->step(), so creating bodies afterwards is outside any callback.
    Vector2 bp = world->getBodyPosition(body);
    float angle = world->getBodyAngle(body);
    float ca = cosf(angle), sa = sinf(angle);

    dynamicBodies.erase(body);
    restFrames.erase(body);
    lastVel.erase(body);
    bodyAge.erase(body);
    bodyChunks.erase(body);
    world->destroyBody(body);

    for (auto* c : list) {
        c->body = nullptr;
        c->fixture = nullptr;
        // Local metres relative to the slab origin match how the dynamic
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
}

// Break a hard-landing slab into chunks of a few cells each, which carry on
// with (most of) its momentum and tumble apart.
void TerrainSystem::shatter(void* body, Vector2 vel, float angVel) {
    std::vector<Chunk*> list = getChunksForBody(body);
    if (list.empty()) return;
    rebakeToWorld(body, list);

    size_t pieceMax = std::clamp<size_t>(list.size() / 5, 3, 9);
    std::set<Chunk*> left(list.begin(), list.end());
    while (!left.empty()) {
        // Grow a piece from a random remaining chunk through touching ones.
        auto it = left.begin();
        std::advance(it, GetRandomValue(0, (int)left.size() - 1));
        std::vector<Chunk*> piece{*it};
        left.erase(it);
        for (size_t k = 0; k < piece.size() && piece.size() < pieceMax; k++) {
            for (auto jt = left.begin(); jt != left.end() && piece.size() < pieceMax;) {
                float dx = (*jt)->centroidX - piece[k]->centroidX, dy = (*jt)->centroidY - piece[k]->centroidY;
                if (dx * dx + dy * dy < CELL * CELL * 2.2f) {
                    piece.push_back(*jt);
                    jt = left.erase(jt);
                } else {
                    ++jt;
                }
            }
        }
        void* b = thawSlab(piece);
        if (!b) continue;
        float r1 = (GetRandomValue(-100, 100) / 100.0f), r2 = (GetRandomValue(-100, 100) / 100.0f);
        world->setBodyVelocity(b, vel.x * 0.7f + r1 * 1.4f, vel.y * 0.5f + fabsf(r2) * 1.5f);
        world->setAngularVelocity(b, angVel * 0.5f + r2 * 3.0f);
        lastVel[b] = world->getBodyVelocity(b);
    }
}

// A slab that has fallen well below sea level is gone for good: drop its body
// and chunks rather than letting it settle, invisibly, on the hidden floor.
void TerrainSystem::sinkDrownedSlabs() {
    std::vector<void*> drowned;
    for (auto* body : dynamicBodies) {
        Vector2 bp = world->getBodyPosition(body);
        float py = (WORLD_H - bp.y) * PPM;
        if (py > WATER_Y + 50.0f) drowned.push_back(body);
    }
    for (auto* body : drowned) {
        Vector2 bp = world->getBodyPosition(body);
        sunk.push_back({bp.x * PPM, WATER_Y});
        std::vector<Chunk*> list = getChunksForBody(body);
        dynamicBodies.erase(body);
        restFrames.erase(body);
        lastVel.erase(body);
        bodyAge.erase(body);
        bodyChunks.erase(body);
        world->destroyBody(body);
        for (auto* c : list) {
            c->body = nullptr;
            c->fixture = nullptr;
            c->state = ChunkState::GONE;
        }
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
// rebake every chunk's vertices into its current world position (the slab
// may have rotated while falling) and give each chunk a fresh static body +
// fixture there. The chunks are landed/SOLID: support.cpp leaves them out of
// the support model, and the renderer draws them from their new pixel verts.
void TerrainSystem::freezeBody(void* body) {
    std::vector<Chunk*> chunkList = getChunksForBody(body);
    if (chunkList.empty()) {
        dynamicBodies.erase(body);
        return;
    }
    rebakeToWorld(body, chunkList);
    // Each chunk gets its own static body at its landed centroid. createFixture
    // builds the fixture with verts local to that centroid (Box2D vertices are
    // body-relative, Y-flipped), matching the CLAUDE.md conventions.
    for (auto* c : chunkList) {
        createFixture(c);
        rubbleAdded.push_back(c);
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
    // Oldest first: they're the most likely to be nearly at rest anyway.
    std::vector<void*> bodies(dynamicBodies.begin(), dynamicBodies.end());
    std::sort(bodies.begin(), bodies.end(), [this](void* a, void* b) { return bodyAge[a] > bodyAge[b]; });
    for (size_t i = 0; i < bodies.size() && (int)dynamicBodies.size() > MAX_DYNAMIC_BODIES; i++) {
        freezeBody(bodies[i]);
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
