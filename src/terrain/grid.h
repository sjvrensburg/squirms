#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace Terrain {

constexpr int CELL = 20;
constexpr int PPM = 32;
constexpr int WORLD_W = 75;
constexpr int WORLD_H = 38;
constexpr int COLS = (WORLD_W * PPM + CELL - 1) / CELL;
constexpr int ROWS = (WORLD_H * PPM + CELL - 1) / CELL;

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
    Material material;
    float hp;
    ChunkState state;
    void* body = nullptr;
    void* fixture = nullptr;
};

struct GridData {
    std::vector<std::vector<Chunk*>> chunks;
    std::vector<std::vector<ChunkVertex>> vertices;
};

const MaterialProps& getMaterialProps(Material mat);
GridData buildGrid(const std::vector<std::vector<bool>>& solid, const std::vector<std::vector<Material>>& material, int seed);

} // namespace Terrain
