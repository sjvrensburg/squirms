#include "generate.h"
#include "noise.h"
#include "../core/rng.h"
#include <algorithm>

namespace Terrain {

static int getSurfaceY(const std::vector<std::vector<bool>>& solid, int x) {
    for (int y = 0; y < ROWS; y++) {
        if (solid[y][x]) return y;
    }
    return ROWS;
}

static void floodFill(const std::vector<std::vector<bool>>& solid, std::vector<std::vector<bool>>& visited, int x, int y, int& count) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return;
    if (!solid[y][x] || visited[y][x]) return;
    visited[y][x] = true;
    count++;
    floodFill(solid, visited, x + 1, y, count);
    floodFill(solid, visited, x - 1, y, count);
    floodFill(solid, visited, x, y + 1, count);
    floodFill(solid, visited, x, y - 1, count);
}

static void fillIsland(std::vector<std::vector<bool>>& solid, int x, int y, bool val) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return;
    if (!solid[y][x]) return;
    solid[y][x] = val;
    fillIsland(solid, x + 1, y, val);
    fillIsland(solid, x - 1, y, val);
    fillIsland(solid, x, y + 1, val);
    fillIsland(solid, x, y - 1, val);
}

GenerateResult generateTerrain(int seed) {
    initNoise(seed);
    Core::Mulberry32 rng((unsigned int)seed);

    std::vector<std::vector<bool>> solid(ROWS, std::vector<bool>(COLS, false));
    std::vector<std::vector<Material>> material(ROWS, std::vector<Material>(COLS, Material::Dirt));

    float scale = 0.04f;
    int surfaceY = (int)(ROWS * 0.35f);

    for (int x = 0; x < COLS; x++) {
        float h = fbm((float)x * scale, 0.5f, 4, [&rng]() { return rng(); }) * 0.6f + 0.2f;
        int height = surfaceY + (int)(h * ROWS * 0.3f);
        for (int y = height; y < ROWS; y++) {
            solid[y][x] = true;
        }
    }

    float caveScale = 0.06f;
    float caveThreshold = 0.48f;
    for (int y = surfaceY + 3; y < ROWS - 2; y++) {
        for (int x = 1; x < COLS - 1; x++) {
            float v = fbm((float)x * caveScale, (float)y * caveScale, 3, [&rng]() { return rng(); });
            if (v > caveThreshold && solid[y][x]) {
                solid[y][x] = false;
            }
        }
    }

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (!solid[y][x]) continue;
            int depth = y - getSurfaceY(solid, x);
            float noiseVal = fbm((float)x * 0.05f, (float)y * 0.05f, 2, [&rng]() { return rng(); });
            if (y >= ROWS - 2) {
                material[y][x] = Material::Bedrock;
            } else if (depth > 18 && noiseVal > 0.55f) {
                material[y][x] = Material::Rock;
            } else if (depth < 5 && noiseVal < 0.35f) {
                material[y][x] = Material::Sand;
            } else {
                material[y][x] = Material::Dirt;
            }
        }
    }

    // Remove small islands
    std::vector<std::vector<bool>> visited(ROWS, std::vector<bool>(COLS, false));
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (!solid[y][x] || visited[y][x]) continue;
            int count = 0;
            floodFill(solid, visited, x, y, count);
            if (count < 8) {
                fillIsland(solid, x, y, false);
            }
        }
    }

    return {solid, material};
}

} // namespace Terrain
