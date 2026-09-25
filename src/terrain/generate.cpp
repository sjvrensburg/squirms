#include "generate.h"
#include "noise.h"
#include "support.h"
#include "../core/rng.h"
#include <algorithm>
#include <cmath>
#include <queue>

namespace Terrain {

namespace {

// Ridged noise: 0 on the "ridge" lines of the underlying noise, rising away
// from them. Thresholding it near 0 yields long, winding worm-hole tunnels.
float ridged(float x, float y) {
    return fabsf(noise2D(x, y) - 0.5f) * 2.0f;
}

// Keep only solid cells connected (4-adjacency) to the bottom rows, so the
// map starts fully supported: nothing thaws into a falling slab on turn one.
void keepGrounded(std::vector<std::vector<bool>>& solid) {
    std::vector<std::vector<bool>> seen(ROWS, std::vector<bool>(COLS, false));
    std::queue<std::pair<int, int>> q;
    for (int x = 0; x < COLS; x++) {
        if (solid[ROWS - 1][x]) { seen[ROWS - 1][x] = true; q.push({x, ROWS - 1}); }
    }
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop();
        for (auto& d : dirs) {
            int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
            if (!solid[ny][nx] || seen[ny][nx]) continue;
            seen[ny][nx] = true;
            q.push({nx, ny});
        }
    }
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            if (solid[y][x] && !seen[y][x]) solid[y][x] = false;
}

// Fill enclosed air pockets smaller than `minCells` (not reachable from the
// map edge/sky): single-cell pinholes read as noise, not caves.
void fillPinholes(std::vector<std::vector<bool>>& solid, int minCells) {
    std::vector<std::vector<bool>> seen(ROWS, std::vector<bool>(COLS, false));
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int sy = 0; sy < ROWS; sy++) {
        for (int sx = 0; sx < COLS; sx++) {
            if (solid[sy][sx] || seen[sy][sx]) continue;
            std::vector<std::pair<int, int>> cells;
            bool touchesEdge = false;
            std::queue<std::pair<int, int>> q;
            q.push({sx, sy}); seen[sy][sx] = true;
            while (!q.empty()) {
                auto [x, y] = q.front(); q.pop();
                cells.push_back({x, y});
                if (x == 0 || y == 0 || x == COLS - 1) touchesEdge = true;
                for (auto& d : dirs) {
                    int nx = x + d[0], ny = y + d[1];
                    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
                    if (solid[ny][nx] || seen[ny][nx]) continue;
                    seen[ny][nx] = true;
                    q.push({nx, ny});
                }
            }
            if (!touchesEdge && (int)cells.size() < minCells) {
                for (auto& c : cells) solid[c.second][c.first] = true;
            }
        }
    }
}

} // namespace

GenerateResult generateTerrain(int seed) {
    initNoise(seed);
    Core::Mulberry32 rng((unsigned int)seed);
    auto rnd = [&rng]() { return rng(); };

    std::vector<std::vector<bool>> solid(ROWS, std::vector<bool>(COLS, false));
    std::vector<std::vector<Material>> material(ROWS, std::vector<Material>(COLS, Material::Dirt));

    const float waterRow = WATER_Y / CELL;
    // Per-map offsets into the noise field so each seed samples a new region.
    const float ox = rng() * 200.0f, oy = rng() * 200.0f;
    // A few maps are one big island, most have a sea channel or two cutting
    // the land into islands (Worms-style archipelagos).
    const float channelFreq = 0.004f + rng() * 0.006f;

    // Surface height per column (in rows): rolling fBm hills, with the ends
    // of the map dipping below sea level so the land reads as an island.
    std::vector<float> surface(COLS);
    for (int x = 0; x < COLS; x++) {
        float h = fbm(ox + x * 0.011f, oy, 4, rnd);          // broad hills
        h = std::clamp((h - 0.5f) * 2.4f + 0.5f, 0.0f, 1.0f); // stretch contrast
        float d = fbm(ox + x * 0.045f, oy + 9.1f, 2, rnd);   // small bumps
        float row = ROWS * (0.22f + 0.50f * (1.0f - h)) + (d - 0.5f) * 10.0f;

        // Taper both ends into the sea with a smooth shoulder, not a cliff.
        float edge = std::min(x, COLS - 1 - x) / (COLS * 0.11f);
        if (edge < 1.0f) {
            float t = 1.0f - edge;
            row += t * t * (3.0f - 2.0f * t) * ROWS * 0.75f;
        }

        // Occasional channels: where this low-frequency noise dips, sink the
        // land under the sea to split it into islands.
        float ch = noise2D(ox * 0.3f + x * channelFreq, 77.7f);
        if (ch < 0.34f) row += (0.34f - ch) * ROWS * 6.0f;

        surface[x] = row;
    }

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            // Warp the surface with 2D noise so hills grow overhangs and
            // lips instead of being a pure heightfield.
            float warp = (fbm(ox + x * 0.04f, oy + y * 0.04f, 3, rnd) - 0.5f) * 22.0f;
            if (y < surface[x] + warp) continue;
            solid[y][x] = true;
        }
    }

    // Caves: long winding tunnels (thin ridges of noise) plus a few round
    // caverns. Kept a few rows under the surface and above the waterline so
    // they are reachable and visible.
    for (int y = 0; y < ROWS; y++) {
        for (int x = 1; x < COLS - 1; x++) {
            if (!solid[y][x]) continue;
            float depth = y - surface[x];
            if (depth < 7.0f || y > waterRow - 3) continue;
            float tunnel = ridged(ox + x * 0.022f, oy + 40.0f + y * 0.035f);
            float cavern = fbm(ox + 50.0f + x * 0.03f, oy + y * 0.045f, 3, rnd);
            if (tunnel < 0.075f || cavern > 0.72f) solid[y][x] = false;
        }
    }

    // Bedrock floor across the whole map (hidden under the sea); the support
    // flood-fill is seeded from it.
    for (int y = ROWS - 2; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) solid[y][x] = true;

    keepGrounded(solid);
    fillPinholes(solid, 12);

    // Materials: tougher rock pockets deeper down, dirt elsewhere.
    auto assignMaterials = [&]() {
        for (int x = 0; x < COLS; x++) {
            int top = 0;
            while (top < ROWS && !solid[top][x]) top++;
            for (int y = 0; y < ROWS; y++) {
                if (!solid[y][x]) continue;
                if (y >= ROWS - 2) { material[y][x] = Material::Bedrock; continue; }
                float n = fbm(ox + x * 0.06f, oy + 20.0f + y * 0.06f, 3, rnd);
                material[y][x] = (y - top > 10 && n > 0.60f) ? Material::Rock : Material::Dirt;
            }
        }
    };
    assignMaterials();

    // Structural pass: the same support model the game uses (support.cpp).
    // Where a roof or ledge spans too far to hold itself up, prop it with a
    // natural pillar under the middle of the span (cave columns); whatever
    // still can't stand is crumbled away. The map starts stable instead of
    // collapsing on turn one.
    auto costs = [&]() {
        std::vector<uint8_t> cells(ROWS * COLS, CELL_EMPTY);
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                if (solid[y][x])
                    cells[y * COLS + x] = material[y][x] == Material::Bedrock ? CELL_BEDROCK
                                        : material[y][x] == Material::Rock    ? CELL_ROCK
                                                                              : CELL_DIRT;
        return supportCost(cells);
    };
    for (int pass = 0; pass < 12; pass++) {
        std::vector<int> cost = costs();
        // Lowest unsupported cell with air beneath it, per column.
        std::vector<int> roof(COLS, -1);
        for (int x = 0; x < COLS; x++) {
            for (int y = ROWS - 2; y >= 0; y--) {
                if (solid[y][x] && cost[y * COLS + x] > MAX_SPAN && !solid[y + 1][x]) { roof[x] = y; break; }
            }
        }
        bool any = false;
        for (int x = 0; x < COLS;) {
            if (roof[x] < 0) { x++; continue; }
            int start = x;
            while (x < COLS && roof[x] >= 0) x++;
            int mid = (start + x - 1) / 2;
            // A rock pillar from the roof down to whatever is below, flared
            // where it meets roof and floor so it reads as a natural column.
            int yTop = roof[mid] + 1, yBot = yTop;
            while (yBot < ROWS && !solid[yBot][mid]) yBot++;
            for (int y = yTop; y < yBot; y++) {
                int half = (y - yTop < 3 || yBot - y <= 3) ? 3 : 2;
                for (int px = std::max(0, mid - half); px <= std::min(COLS - 1, mid + half); px++) {
                    if (solid[y][px]) continue;
                    solid[y][px] = true;
                    material[y][px] = Material::Rock;
                }
            }
            any = true;
        }
        if (!any) break;
    }
    {
        std::vector<int> cost = costs();
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                if (solid[y][x] && cost[y * COLS + x] > MAX_SPAN) solid[y][x] = false;
        keepGrounded(solid);
    }

    return {solid, material};
}

} // namespace Terrain
