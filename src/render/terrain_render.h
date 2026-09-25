#pragma once
#include <raylib.h>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "theme.h"

namespace Terrain { class TerrainSystem; struct Chunk; }

// Draws the landscape Worms 2 style from two cached textures instead of one
// polygon per chunk:
//
//  - `base`: the pristine painted terrain (soil, rock, pebbles, grass crust)
//    for every originally solid pixel. Never changes after build().
//  - `display`: what the static landscape looks like right now: base pixels
//    masked to the grid chunks that are still in place, plus a dark outline
//    wherever solid meets air. Erased piecemeal as chunks are destroyed or
//    thawed (TerrainSystem::dirty), via partial texture uploads.
//
// Falling chunks are drawn individually as textured quads sampling `base` at
// their original position (Chunk::uv), so a chunk carries its own patch of
// soil with it. Once rubble comes to rest it is baked into `display` too (a
// per-pixel rubble layer mapping back into `base`), so landed piles get the
// same smoothed outline as the landscape instead of looking like tiles.
class TerrainRenderer {
public:
    void build(Terrain::TerrainSystem& terrain, const Theme& theme, int seed);
    void unload();

    // Drain terrain.dirty and refresh the affected regions of `display`.
    void sync(Terrain::TerrainSystem& terrain);

    // World-space drawing (call inside BeginMode2D).
    void drawStatic() const;
    void drawLoose(const Terrain::TerrainSystem& terrain) const;

    // Painted colour at a world pixel (for debris particles).
    Color sample(float x, float y) const;
    bool isBuilt() const { return built_; }

private:
    int w_ = 0, h_ = 0;
    bool built_ = false;
    std::vector<Color> base_;       // pristine painted pixels
    std::vector<uint16_t> owner_;   // chunk id + 1 covering each pixel (0 = air)
    std::vector<uint8_t> visible_;  // pixel belongs to a still-standing grid chunk
    std::vector<uint16_t> rubble_;  // landed chunk id + 1 covering each pixel (0 = none)
    // Per landed chunk: how to map a world pixel back to its original spot in
    // `base` (a rigid transform), and the box it was baked into.
    struct RubbleXf { float cwx, cwy, cux, cuy, c, s; int x0, y0, x1, y1; };
    std::unordered_map<int, RubbleXf> rubbleXf_;
    Texture2D baseTex_{};
    Texture2D displayTex_{};
    Color edge_{};

    void paintBase(const Terrain::TerrainSystem& terrain, const Theme& theme, int seed);
    void rasterizeOwners(const Terrain::TerrainSystem& terrain);
    // Recompute display pixels inside [x0,x1)x[y0,y1) and upload them.
    void refreshRegion(int x0, int y0, int x1, int y1, bool upload);
    bool bakeRubble(const Terrain::Chunk* c, int box[4]);
    bool eraseRubble(const Terrain::Chunk* c, int box[4]);
    std::vector<Color> scratch_;
    std::vector<uint8_t> solidScratch_;  // W x H, union mask for smoothing
};
