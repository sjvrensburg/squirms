#pragma once
#include <set>
#include <vector>
#include "grid.h"

namespace Terrain {

std::vector<std::vector<Chunk*>> clusterUnsupported(const std::vector<std::vector<Chunk*>>& chunks);

} // namespace Terrain
