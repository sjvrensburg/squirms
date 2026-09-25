#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace Terrain {

// Chunk size in px. Small cells keep craters reading as round holes rather
// than blocky bites; the static terrain is drawn from one cached texture (see
// render/terrain_render.cpp), so the cell count doesn't drive draw calls.
constexpr int CELL = 10;
constexpr int PPM = 32;
constexpr int WORLD_W = 75;
constexpr int WORLD_H = 38;
constexpr int COLS = (WORLD_W * PPM + CELL - 1) / CELL;
constexpr int ROWS = (WORLD_H * PPM + CELL - 1) / CELL;
constexpr int WORLD_PX_W = WORLD_W * PPM;
constexpr int WORLD_PX_H = WORLD_H * PPM;
// Pixel-space sea level. Anything that sinks below it drowns; the bedrock
// floor that grounds the support flood-fill sits hidden underneath.
constexpr float WATER_Y = (float)(WORLD_PX_H - 120);

enum class Material { Dirt, Rock, Sand, Bedrock };

struct MaterialProps {
    float hp;
    float density;
    float friction;
    std::string color;
};

const MaterialProps MATERIAL_PROPS[] = {
    {30.0f, 1.6f, 0.55f, "#8B6914"},
    {70.0f, 2.4f, 0.9f,  "#7a7a7a"},
    {15.0f, 1.4f, 0.25f, "#c2b280"},
    {-1.0f, 999.0f, 1.0f, "#3a3a5a"}
};

struct CellData {
    bool solid;
    Material material;
};

struct ChunkVertex {
    float x, y;
};

enum class ChunkState { SOLID, DYNAMIC, GONE };

struct Chunk {
    int id;
    int col, row;
    std::vector<std::pair<float, float>> verts;
    float centroidX, centroidY;
    // The chunk's original pixel-space verts, fixed at build time. The
    // renderer samples the terrain texture with these as UVs, so a chunk that
    // fell and rotated still carries its own patch of soil/grass with it.
    std::vector<std::pair<float, float>> uv;
    Material material;
    float hp;
    ChunkState state;
    // Set once a falling slab has come to rest and been converted into static
    // terrain at where it landed (see TerrainSystem::freezeBody). Landed chunks
    // are SOLID with a static body, but they no longer sit in their grid cell,
    // so support.cpp treats them as empty rubble excluded from the flood-fill
    // (they never thaw again and can't prop up their old neighbours). Their
    // `verts`/`centroid` are rebaked to the landed position so the renderer and
    // explosions see them where they actually are.
    bool landed = false;
    void* body = nullptr;
    void* fixture = nullptr;
    // For a thawed dynamic slab these are kept fresh so the renderer can draw
    // the chunk where its Box2D body actually is (see renderer.cpp). The slab
    // centroid `bodyOrigin` is set once when the slab is created; `bodyDraw`
    // and `bodyAngle` are refreshed every tick from the body's state.
    float bodyOriginX = 0, bodyOriginY = 0;
    float bodyDrawX = 0, bodyDrawY = 0;
    float bodyAngle = 0;
};

struct GridData {
    std::vector<std::vector<Chunk*>> chunks;
    std::vector<std::vector<ChunkVertex>> vertices;
};

const MaterialProps& getMaterialProps(Material mat);
GridData buildGrid(const std::vector<std::vector<bool>>& solid, const std::vector<std::vector<Material>>& material, int seed);

} // namespace Terrain
