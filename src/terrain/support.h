#pragma once
#include <cstdint>
#include <vector>
#include "grid.h"

namespace Terrain {

// Structural support model. Every solid cell gets the cost of its cheapest
// load path down to the bedrock floor:
//   - resting on a supported cell directly below is free,
//   - each sideways step is a cantilever: cheap through thick terrain and
//     rock, expensive through thin dirt ledges,
//   - hanging from the cell above costs more still.
// Cells whose cost exceeds MAX_SPAN (or that can't reach the floor at all)
// are unsupported and break off. So a long thin ledge snaps, a thick cave
// roof holds, and anything cut loose falls.
enum CellKind : uint8_t { CELL_EMPTY = 0, CELL_DIRT = 1, CELL_ROCK = 2, CELL_BEDROCK = 3 };
constexpr int MAX_SPAN = 60;
constexpr int SPAN_UNSUPPORTED = 1 << 20;

// `cells` is ROWS x COLS row-major CellKind. Returns a cost per cell
// (SPAN_UNSUPPORTED for empty cells and anything beyond MAX_SPAN).
std::vector<int> supportCost(const std::vector<uint8_t>& cells);

// Connected groups (4-adjacency) of SOLID, non-landed chunks that the model
// says can't hold themselves up.
std::vector<std::vector<Chunk*>> clusterUnsupported(const std::vector<std::vector<Chunk*>>& chunks);

} // namespace Terrain
