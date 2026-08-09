#pragma once
#include <cstdint>
#include <functional>

namespace Terrain {

extern uint8_t PERM[512];

void initNoise(int seed);
float noise2D(float x, float y);
float fbm(float x, float y, int octaves, std::function<float()> rng);

} // namespace Terrain
