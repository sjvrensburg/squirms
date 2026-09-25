#include "support.h"
#include <algorithm>
#include <queue>

namespace Terrain {

namespace {

// Cost of stepping sideways into a cell, by material and by how thick the
// terrain is at that point (contiguous solid cells from here upwards, capped
// at 8): a 1-cell dirt ledge can reach ~7 cells, a thick dirt roof ~30,
// rock twice that.
constexpr int THICK_CAP = 8;
int sideCost(uint8_t kind, int thick) {
    static const int dirt[THICK_CAP + 1] = {0, 8, 6, 5, 4, 3, 3, 2, 2};
    int c = dirt[std::min(thick, THICK_CAP)];
    return kind == CELL_ROCK ? std::max(1, c / 2) : c;
}
int hangCost(uint8_t kind) { return kind == CELL_ROCK ? 3 : 6; }

} // namespace

std::vector<int> supportCost(const std::vector<uint8_t>& cells) {
    const int N = ROWS * COLS;
    std::vector<int> cost(N, SPAN_UNSUPPORTED);

    // Column thickness above each cell.
    std::vector<uint8_t> thick(N, 0);
    for (int x = 0; x < COLS; x++) {
        int run = 0;
        for (int y = 0; y < ROWS; y++) {
            int i = y * COLS + x;
            run = cells[i] ? std::min(run + 1, THICK_CAP) : 0;
            thick[i] = (uint8_t)run;
        }
    }

    // Dijkstra with a bucket queue (edge costs are small integers, and
    // anything past MAX_SPAN is pruned).
    std::vector<std::vector<int>> buckets(MAX_SPAN + 1);
    for (int x = 0; x < COLS; x++) {
        for (int y = 0; y < ROWS; y++) {
            int i = y * COLS + x;
            if (cells[i] == CELL_BEDROCK || (y == ROWS - 1 && cells[i])) {
                cost[i] = 0;
                buckets[0].push_back(i);
            }
        }
    }
    for (int c = 0; c <= MAX_SPAN; c++) {
        for (size_t k = 0; k < buckets[c].size(); k++) {
            int i = buckets[c][k];
            if (cost[i] != c) continue;  // stale entry
            int x = i % COLS, y = i / COLS;
            // dy = -1: the neighbour rests on us (free); dy = 0: it's
            // cantilevered off our side; dy = +1: it hangs from us.
            auto relax = [&](int nx, int ny, int dy) {
                if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) return;
                int j = ny * COLS + nx;
                if (!cells[j]) return;
                int step = dy < 0 ? 0 : (dy == 0 ? sideCost(cells[j], thick[j]) : hangCost(cells[j]));
                int nc = c + step;
                if (nc > MAX_SPAN || nc >= cost[j]) return;
                cost[j] = nc;
                buckets[nc].push_back(j);
            };
            relax(x, y - 1, -1);
            relax(x - 1, y, 0);
            relax(x + 1, y, 0);
            relax(x, y + 1, 1);
        }
    }
    return cost;
}

std::vector<std::vector<Chunk*>> clusterUnsupported(const std::vector<std::vector<Chunk*>>& chunks) {
    // Landed rubble sits outside its old grid cell, so it's excluded from the
    // model entirely (treated as empty): it can neither hold up nor be held
    // up by its old neighbours, and is never returned here.
    std::vector<uint8_t> cells(ROWS * COLS, CELL_EMPTY);
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            const Chunk* c = chunks[y][x];
            if (c->state != ChunkState::SOLID || c->landed) continue;
            cells[y * COLS + x] = c->material == Material::Bedrock ? CELL_BEDROCK
                                : c->material == Material::Rock    ? CELL_ROCK
                                                                   : CELL_DIRT;
        }
    }
    std::vector<int> cost = supportCost(cells);

    std::vector<std::vector<Chunk*>> components;
    std::vector<char> visited(ROWS * COLS, 0);
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    auto unsupported = [&](int i) { return cells[i] != CELL_EMPTY && cells[i] != CELL_BEDROCK && cost[i] > MAX_SPAN; };

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            int start = y * COLS + x;
            if (!unsupported(start) || visited[start]) continue;
            std::vector<Chunk*> component;
            std::queue<int> q;
            q.push(start);
            visited[start] = 1;
            while (!q.empty()) {
                int i = q.front(); q.pop();
                int cx = i % COLS, cy = i / COLS;
                component.push_back(chunks[cy][cx]);
                for (auto& d : dirs) {
                    int nx = cx + d[0], ny = cy + d[1];
                    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
                    int j = ny * COLS + nx;
                    if (!unsupported(j) || visited[j]) continue;
                    visited[j] = 1;
                    q.push(j);
                }
            }
            components.push_back(std::move(component));
        }
    }
    return components;
}

} // namespace Terrain
