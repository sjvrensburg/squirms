#include "grid.h"
#include <set>
#include <queue>

namespace Terrain {

// Chunks are "supported" if they're reachable from the map's bedrock floor
// by a path of solid 4-adjacent neighbors. Anything else is a floating
// cluster that should immediately collapse into a dynamic slab.
static std::set<int> floodGrounded(const std::vector<std::vector<Chunk*>>& chunks) {
    std::set<int> grounded;
    std::queue<std::pair<int, int>> q;

    for (int x = 0; x < COLS; x++) {
        for (int y = 0; y < ROWS; y++) {
            Chunk* c = chunks[y][x];
            if (c->state != ChunkState::SOLID) continue;
            if (c->material != Material::Bedrock && y != ROWS - 1) continue;
            if (grounded.insert(c->id).second) {
                q.push({x, y});
            }
        }
    }

    int dirs[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!q.empty()) {
        auto [cx, cy] = q.front(); q.pop();
        for (auto& d : dirs) {
            int nx = cx + d[0], ny = cy + d[1];
            if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
            Chunk* n = chunks[ny][nx];
            if (n->state != ChunkState::SOLID) continue;
            if (!grounded.insert(n->id).second) continue;
            q.push({nx, ny});
        }
    }

    return grounded;
}

std::vector<std::vector<Chunk*>> clusterUnsupported(const std::vector<std::vector<Chunk*>>& chunks) {
    std::set<int> grounded = floodGrounded(chunks);

    std::vector<std::vector<Chunk*>> components;
    std::set<int> visited;
    int dirs[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            Chunk* start = chunks[y][x];
            if (start->state != ChunkState::SOLID) continue;
            if (grounded.count(start->id)) continue;
            if (visited.count(start->id)) continue;

            std::vector<Chunk*> component;
            std::queue<std::pair<int, int>> q;
            q.push({x, y});
            visited.insert(start->id);

            while (!q.empty()) {
                auto [cx, cy] = q.front(); q.pop();
                component.push_back(chunks[cy][cx]);

                for (auto& d : dirs) {
                    int nx = cx + d[0], ny = cy + d[1];
                    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
                    Chunk* n = chunks[ny][nx];
                    if (n->state != ChunkState::SOLID) continue;
                    if (grounded.count(n->id)) continue;
                    if (visited.count(n->id)) continue;
                    visited.insert(n->id);
                    q.push({nx, ny});
                }
            }

            components.push_back(component);
        }
    }
    return components;
}

} // namespace Terrain
