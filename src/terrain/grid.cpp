#include "grid.h"
#include "noise.h"
#include "../core/rng.h"

namespace Terrain {

const MaterialProps& getMaterialProps(Material mat) {
    return MATERIAL_PROPS[(int)mat];
}

GridData buildGrid(const std::vector<std::vector<bool>>& solid, const std::vector<std::vector<Material>>& material, int seed) {
    Core::Mulberry32 rng((unsigned int)seed);
    int vCols = COLS + 1;
    int vRows = ROWS + 1;

    GridData data;
    data.vertices.resize(vRows);
    for (int j = 0; j < vRows; j++) {
        data.vertices[j].resize(vCols);
        for (int i = 0; i < vCols; i++) {
            float jitter = ((float)rng() - 0.5f) * CELL * 0.28f;
            data.vertices[j][i] = {
                (float)(i * CELL + jitter),
                (float)(j * CELL + jitter)
            };
        }
    }

    data.chunks.resize(ROWS);
    for (int row = 0; row < ROWS; row++) {
        data.chunks[row].resize(COLS);
        for (int col = 0; col < COLS; col++) {
            if (!solid[row][col]) {
                Material mat = material[row][col];
                data.chunks[row][col] = new Chunk{
                    row * COLS + col, col, row, {}, 0, 0,
                    mat, getMaterialProps(mat).hp, ChunkState::GONE, nullptr, nullptr
                };
                continue;
            }
            const auto& v00 = data.vertices[row][col];
            const auto& v10 = data.vertices[row][col + 1];
            const auto& v01 = data.vertices[row + 1][col];
            const auto& v11 = data.vertices[row + 1][col + 1];
            std::vector<std::pair<float, float>> verts = {{v00.x, v00.y}, {v10.x, v10.y}, {v11.x, v11.y}, {v01.x, v01.y}};
            float cx = (v00.x + v10.x + v11.x + v01.x) / 4.0f;
            float cy = (v00.y + v10.y + v11.y + v01.y) / 4.0f;
            Material mat = material[row][col];
            data.chunks[row][col] = new Chunk{
                row * COLS + col, col, row, verts, cx, cy,
                mat, getMaterialProps(mat).hp, ChunkState::SOLID, nullptr, nullptr
            };
        }
    }

    return data;
}

} // namespace Terrain
