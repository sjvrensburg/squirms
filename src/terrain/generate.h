#pragma once
#include <vector>
#include "grid.h"

namespace Terrain {

struct GenerateResult {
    std::vector<std::vector<bool>> solid;
    std::vector<std::vector<Material>> material;
};

GenerateResult generateTerrain(int seed);

} // namespace Terrain
