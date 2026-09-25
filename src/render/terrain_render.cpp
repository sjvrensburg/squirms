#include "terrain_render.h"
#include "rlgl_min.h"
#include "../terrain/terrain.h"
#include "../terrain/noise.h"
#include "../core/rng.h"
#include <algorithm>
#include <cmath>

using namespace Terrain;

namespace {

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float smooth(float a, float b, float x) {
    float t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
inline Color mix(Color a, Color b, float t) {
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), 255};
}
inline Color shade(Color c, float f) {
    auto ch = [f](unsigned char v) { float r = v * f; return (unsigned char)(r > 255 ? 255 : (r < 0 ? 0 : r)); };
    return Color{ch(c.r), ch(c.g), ch(c.b), c.a};
}
inline uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float hashf(int x, int y) { return (hash2(x, y) & 0xffff) / 65535.0f; }

// Radius of the box filter that rounds the chunk grid's 10 px staircase into
// a smooth, organic silhouette. The drawn edge strays at most ~R px from the
// physics, which reads fine at worm scale.
constexpr int SMOOTH_R = 4;

// Box-filter `src` (a W x H 0/1 mask) with radius R and threshold at half,
// over [x0,x1) x [y0,y1), writing a row-major (x1-x0) wide result to `out`.
// Uses a summed-area table; samples outside the image clamp to the edge.
void smoothMask(const std::vector<uint8_t>& src, int W, int H, int x0, int y0, int x1, int y1, int R,
                std::vector<uint8_t>& out) {
    int ex0 = x0 - R, ey0 = y0 - R;
    int ew = (x1 - x0) + 2 * R, eh = (y1 - y0) + 2 * R;
    std::vector<int> sat((size_t)(ew + 1) * (eh + 1), 0);
    for (int y = 0; y < eh; y++) {
        int sy = std::clamp(ey0 + y, 0, H - 1);
        int row = 0;
        const uint8_t* line = &src[(size_t)sy * W];
        int* dst = &sat[(size_t)(y + 1) * (ew + 1) + 1];
        const int* above = &sat[(size_t)y * (ew + 1) + 1];
        for (int x = 0; x < ew; x++) {
            int sx = std::clamp(ex0 + x, 0, W - 1);
            row += line[sx] ? 1 : 0;
            dst[x] = above[x] + row;
        }
    }
    int k = 2 * R + 1, need = (k * k + 1) / 2;
    int ow = x1 - x0;
    out.resize((size_t)ow * (y1 - y0));
    for (int y = 0; y < y1 - y0; y++) {
        const int* ra = &sat[(size_t)y * (ew + 1)];
        const int* rb = &sat[(size_t)(y + k) * (ew + 1)];
        for (int x = 0; x < ow; x++) {
            int sum = rb[x + k] - ra[x + k] - rb[x] + ra[x];
            out[(size_t)y * ow + x] = sum >= need;
        }
    }
}

} // namespace

void TerrainRenderer::build(TerrainSystem& terrain, const Theme& theme, int seed) {
    unload();
    w_ = WORLD_PX_W;
    h_ = WORLD_PX_H;
    edge_ = theme.edge;
    rasterizeOwners(terrain);
    paintBase(terrain, theme, seed);

    Image baseImg{base_.data(), w_, h_, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    baseTex_ = LoadTextureFromImage(baseImg);
    SetTextureFilter(baseTex_, TEXTURE_FILTER_BILINEAR);

    refreshRegion(0, 0, w_, h_, false);
    Image dispImg{scratch_.data(), w_, h_, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    displayTex_ = LoadTextureFromImage(dispImg);
    SetTextureFilter(displayTex_, TEXTURE_FILTER_BILINEAR);
    scratch_.clear();
    scratch_.shrink_to_fit();

    terrain.dirty.clear();
    terrain.rubbleAdded.clear();
    terrain.rubbleRemoved.clear();
    built_ = true;
}

void TerrainRenderer::unload() {
    if (!built_) return;
    UnloadTexture(baseTex_);
    UnloadTexture(displayTex_);
    base_.clear(); base_.shrink_to_fit();
    owner_.clear(); owner_.shrink_to_fit();
    visible_.clear(); visible_.shrink_to_fit();
    rubble_.clear(); rubble_.shrink_to_fit();
    solidScratch_.clear(); solidScratch_.shrink_to_fit();
    rubbleXf_.clear();
    built_ = false;
}

// Point-in-convex-quad per pixel centre. Chunk verts run TL, TR, BR, BL in
// Y-down pixel space, so "inside" is every edge cross product >= 0.
static bool insideQuad(const std::vector<std::pair<float, float>>& v, float px, float py) {
    size_t n = v.size();
    for (size_t i = 0; i < n; i++) {
        const auto& a = v[i];
        const auto& b = v[(i + 1) % n];
        float cross = (b.first - a.first) * (py - a.second) - (b.second - a.second) * (px - a.first);
        if (cross < -1e-4f) return false;
    }
    return true;
}

void TerrainRenderer::rasterizeOwners(const TerrainSystem& terrain) {
    owner_.assign((size_t)w_ * h_, 0);
    visible_.assign((size_t)w_ * h_, 0);
    rubble_.assign((size_t)w_ * h_, 0);
    solidScratch_.assign((size_t)w_ * h_, 0);
    rubbleXf_.clear();
    for (const auto& row : terrain.chunks) {
        for (const Chunk* c : row) {
            if (!c || c->state != ChunkState::SOLID || c->uv.empty()) continue;
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            for (auto& p : c->uv) {
                minX = std::min(minX, p.first); maxX = std::max(maxX, p.first);
                minY = std::min(minY, p.second); maxY = std::max(maxY, p.second);
            }
            int x0 = std::max(0, (int)floorf(minX)), x1 = std::min(w_ - 1, (int)ceilf(maxX));
            int y0 = std::max(0, (int)floorf(minY)), y1 = std::min(h_ - 1, (int)ceilf(maxY));
            bool vis = !c->landed;
            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    size_t i = (size_t)y * w_ + x;
                    if (owner_[i]) continue;
                    if (!insideQuad(c->uv, x + 0.5f, y + 0.5f)) continue;
                    owner_[i] = (uint16_t)(c->id + 1);
                    visible_[i] = vis;
                }
            }
        }
    }
}

void TerrainRenderer::paintBase(const TerrainSystem& terrain, const Theme& theme, int seed) {
    base_.assign((size_t)w_ * h_, Color{edge_.r, edge_.g, edge_.b, 0});
    Core::Mulberry32 rng((unsigned int)seed * 2654435761u + 17u);
    const float ox = rng() * 100.0f, oy = rng() * 100.0f;

    // Paint against the smoothed silhouette (what the player will see), and
    // also every pixel a chunk actually covers (loose chunks sample those).
    std::vector<uint8_t> orig(owner_.size());
    for (size_t i = 0; i < owner_.size(); i++) orig[i] = owner_[i] != 0;
    std::vector<uint8_t> solid;
    smoothMask(orig, w_, h_, 0, 0, w_, h_, SMOOTH_R, solid);
    auto painted = [&](size_t i) { return solid[i] || orig[i]; };

    auto cellMaterial = [&](int col, int row) {
        col = std::clamp(col, 0, COLS - 1);
        row = std::clamp(row, 0, ROWS - 1);
        return terrain.chunks[row][col]->material;
    };
    // Rock-ness blended between cell centres so rock pockets have soft,
    // blobby borders instead of showing the square cell grid.
    auto rockiness = [&](int x, int y) {
        float fx = (x - CELL * 0.5f) / CELL, fy = (y - CELL * 0.5f) / CELL;
        int cx = (int)floorf(fx), cy = (int)floorf(fy);
        float tx = fx - cx, ty = fy - cy;
        auto r = [&](int c, int rr) { return cellMaterial(c, rr) != Terrain::Material::Dirt ? 1.0f : 0.0f; };
        float top = r(cx, cy) * (1 - tx) + r(cx + 1, cy) * tx;
        float bot = r(cx, cy + 1) * (1 - tx) + r(cx + 1, cy + 1) * tx;
        return top * (1 - ty) + bot * ty;
    };

    // Column tops, for a gentle darkening with depth below the surface.
    std::vector<int> top(w_, h_);
    for (int x = 0; x < w_; x++) {
        for (int y = 0; y < h_; y++) {
            if (solid[(size_t)y * w_ + x]) { top[x] = y; break; }
        }
    }

    // Soil / rock body.
    for (int y = 0; y < h_; y++) {
        for (int x = 0; x < w_; x++) {
            size_t i = (size_t)y * w_ + x;
            if (!painted(i)) continue;
            float n1 = noise2D(ox + x / 26.0f, oy + y / 26.0f);
            float n2 = noise2D(ox + x / 6.5f, oy + y / 6.5f);
            float rock = rockiness(x, y) + (noise2D(ox + x / 7.0f, oy + 30 + y / 7.0f) - 0.5f) * 0.45f;
            Color c;
            if (rock > 0.5f) {
                c = mix(theme.rockA, theme.rockB, smooth(0.3f, 0.7f, n1));
                // Hairline cracks along the ridges of a stretched noise field.
                float crack = fabsf(noise2D(ox + x / 20.0f, oy + y / 11.0f) - 0.5f);
                if (crack < 0.018f) c = shade(c, 0.62f);
                else if (crack < 0.03f) c = shade(c, 1.12f);
                // Darker rim where rock meets soil.
                if (rock < 0.58f) c = shade(c, 0.72f);
            } else {
                c = mix(theme.soilA, theme.soilB, smooth(0.32f, 0.68f, n1));
                // Faint wavy strata.
                float strata = sinf(y * 0.32f + n1 * 7.0f) * 0.5f + 0.5f;
                c = shade(c, 0.94f + strata * 0.1f);
            }
            float depth = (float)(y - top[x]);
            float f = (0.9f + n2 * 0.2f + (hashf(x, y) - 0.5f) * 0.06f) *
                      (1.0f - 0.32f * clamp01(depth / 420.0f));
            base_[i] = shade(c, f);
        }
    }

    // Pebbles: little shaded stones scattered through the soil.
    int pebbles = w_ * h_ / 1500;
    for (int k = 0; k < pebbles; k++) {
        int px = (int)(rng() * w_), py = (int)(rng() * h_);
        size_t pi = (size_t)py * w_ + px;
        if (!solid[pi] || py - top[px] < 14) continue;
        float r = 1.5f + rng() * rng() * 4.5f;
        bool rocky = rockiness(px, py) > 0.5f;
        Color pc = rocky ? shade(theme.soilA, 0.8f + rng() * 0.3f)
                         : shade(mix(theme.rockA, theme.soilA, rng() * 0.5f), 0.85f + rng() * 0.35f);
        int ir = (int)ceilf(r) + 1;
        for (int dy = -ir; dy <= ir; dy++) {
            for (int dx = -ir; dx <= ir; dx++) {
                int x = px + dx, y = py + dy;
                if (x < 0 || y < 0 || x >= w_ || y >= h_) continue;
                size_t i = (size_t)y * w_ + x;
                if (!painted(i)) continue;
                float d = sqrtf((float)(dx * dx + dy * dy));
                if (d > r + 0.5f) continue;
                if (d > r - 0.7f) { base_[i] = shade(base_[i], 0.6f); continue; } // rim shadow
                // Light from the top-left.
                float l = 1.0f + (-dx - dy) / (r * 3.2f);
                base_[i] = shade(pc, l);
            }
        }
    }

    // Crust (grass / sand / snow / lava) on every upward-facing surface of
    // the smoothed silhouette, with a ragged lower edge like blades and a
    // thin dark underside.
    for (int x = 0; x < w_; x++) {
        float colN = noise2D(ox + x / 9.0f, oy + 55.5f);
        int blade = (int)(hash2(x, 99) % 4);
        for (int y = 0; y < h_; y++) {
            size_t i = (size_t)y * w_ + x;
            if (!solid[i]) continue;
            if (y > 0 && solid[i - w_]) continue;
            if (y >= (ROWS - 2) * CELL) continue;  // bedrock floor
            int thick = 5 + (int)(colN * 5.0f) + blade;
            float stripe = (x % 3 == 0) ? 0.9f : 1.0f;
            int k = 0;
            for (; k < thick && y + k < h_; k++) {
                size_t j = (size_t)(y + k) * w_ + x;
                if (!painted(j)) break;
                float t = (float)k / thick;
                Color c = mix(theme.crustB, theme.crustA, t * t);
                if (k == 0) c = shade(theme.crustB, 1.12f);
                base_[j] = shade(c, stripe * (0.95f + hashf(x, y + k) * 0.1f));
            }
            for (int s = 0; s < 2 && y + k + s < h_; s++) {
                size_t j = (size_t)(y + k + s) * w_ + x;
                if (!painted(j)) break;
                base_[j] = shade(base_[j], s == 0 ? 0.55f : 0.78f);
            }
        }
    }
}

void TerrainRenderer::refreshRegion(int x0, int y0, int x1, int y1, bool upload) {
    x0 = std::max(0, x0); y0 = std::max(0, y0);
    x1 = std::min(w_, x1); y1 = std::min(h_, y1);
    if (x1 <= x0 || y1 <= y0) return;
    int rw = x1 - x0, rh = y1 - y0;
    scratch_.resize((size_t)rw * rh);

    // Smoothed visibility over the region plus the outline margin.
    const int M = 2;
    int sx0 = x0 - M, sy0 = y0 - M, sw = rw + 2 * M;
    // Smooth the union of standing terrain and baked rubble, so a pile of
    // rubble on the ground reads as one lump of land.
    int ux0 = std::max(0, sx0 - SMOOTH_R), uy0 = std::max(0, sy0 - SMOOTH_R);
    int ux1 = std::min(w_, x1 + M + SMOOTH_R), uy1 = std::min(h_, y1 + M + SMOOTH_R);
    for (int y = uy0; y < uy1; y++)
        for (int x = ux0; x < ux1; x++) {
            size_t i = (size_t)y * w_ + x;
            solidScratch_[i] = visible_[i] || rubble_[i];
        }
    std::vector<uint8_t> sm;
    smoothMask(solidScratch_, w_, h_, sx0, sy0, x1 + M, y1 + M, SMOOTH_R, sm);
    // Out-of-bounds counts as solid so the map border isn't outlined.
    auto vis = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return true;
        return sm[(size_t)(y - sy0) * sw + (x - sx0)] != 0;
    };
    const Color clear{edge_.r, edge_.g, edge_.b, 0};

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            size_t i = (size_t)y * w_ + x;
            Color& out = scratch_[(size_t)(y - y0) * rw + (x - x0)];
            if (!vis(x, y)) { out = clear; continue; }
            // Chebyshev distance to the nearest air pixel, capped at 3.
            int d = 3;
            for (int r = 1; r <= 2 && d == 3; r++) {
                for (int k = -r; k <= r; k++) {
                    if (!vis(x + k, y - r) || !vis(x + k, y + r) ||
                        !vis(x - r, y + k) || !vis(x + r, y + k)) { d = r; break; }
                }
            }
            Color c = base_[i];
            if (!visible_[i]) {
                // Rubble (or a smoothing-filled gap next to it): colour from
                // the rubble chunk's original patch of terrain.
                uint16_t r = rubble_[i];
                if (!r) {
                    for (int k = 1; k <= SMOOTH_R && !r; k++) {
                        if (x - k >= 0 && rubble_[i - k]) r = rubble_[i - k];
                        else if (x + k < w_ && rubble_[i + k]) r = rubble_[i + k];
                        else if (y - k >= 0 && rubble_[i - (size_t)k * w_]) r = rubble_[i - (size_t)k * w_];
                        else if (y + k < h_ && rubble_[i + (size_t)k * w_]) r = rubble_[i + (size_t)k * w_];
                    }
                }
                auto it = r ? rubbleXf_.find(r - 1) : rubbleXf_.end();
                if (it != rubbleXf_.end()) {
                    const RubbleXf& t = it->second;
                    float dx = x + 0.5f - t.cwx, dy = y + 0.5f - t.cwy;
                    int ux = (int)(t.cux + dx * t.c + dy * t.s), uy = (int)(t.cuy - dx * t.s + dy * t.c);
                    ux = std::clamp(ux, 0, w_ - 1);
                    uy = std::clamp(uy, 0, h_ - 1);
                    c = base_[(size_t)uy * w_ + ux];
                }
            }
            if (c.a == 0) c = edge_;
            if (d == 1) c = edge_;
            else if (d == 2) c = mix(c, edge_, 0.45f);
            out = c;
        }
    }
    if (upload) {
        UpdateTextureRec(displayTex_, Rectangle{(float)x0, (float)y0, (float)rw, (float)rh}, scratch_.data());
    }
}

bool TerrainRenderer::eraseRubble(const Chunk* c, int box[4]) {
    auto it = rubbleXf_.find(c->id);
    if (it == rubbleXf_.end()) return false;
    const RubbleXf t = it->second;
    rubbleXf_.erase(it);
    uint16_t tag = (uint16_t)(c->id + 1);
    for (int y = t.y0; y <= t.y1; y++)
        for (int x = t.x0; x <= t.x1; x++) {
            size_t i = (size_t)y * w_ + x;
            if (rubble_[i] == tag) rubble_[i] = 0;
        }
    box[0] = t.x0; box[1] = t.y0; box[2] = t.x1; box[3] = t.y1;
    return true;
}

bool TerrainRenderer::bakeRubble(const Chunk* c, int box[4]) {
    if (c->verts.size() != 4 || c->uv.size() != 4) return false;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    float cwx = 0, cwy = 0, cux = 0, cuy = 0;
    for (int k = 0; k < 4; k++) {
        const auto& p = c->verts[k];
        minX = std::min(minX, p.first); maxX = std::max(maxX, p.first);
        minY = std::min(minY, p.second); maxY = std::max(maxY, p.second);
        cwx += p.first * 0.25f; cwy += p.second * 0.25f;
        cux += c->uv[k].first * 0.25f; cuy += c->uv[k].second * 0.25f;
    }
    int x0 = std::max(0, (int)floorf(minX)), x1 = std::min(w_ - 1, (int)ceilf(maxX));
    int y0 = std::max(0, (int)floorf(minY)), y1 = std::min(h_ - 1, (int)ceilf(maxY));
    if (x1 < x0 || y1 < y0) return false;
    // Rotation from original to landed: compare one edge before and after.
    float aw = atan2f(c->verts[1].second - c->verts[0].second, c->verts[1].first - c->verts[0].first);
    float au = atan2f(c->uv[1].second - c->uv[0].second, c->uv[1].first - c->uv[0].first);
    float a = aw - au;
    // uv = cu + R(-a) (p - cw)
    RubbleXf t{cwx, cwy, cux, cuy, cosf(a), sinf(a), x0, y0, x1, y1};
    rubbleXf_[c->id] = t;
    uint16_t tag = (uint16_t)(c->id + 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (insideQuad(c->verts, x + 0.5f, y + 0.5f)) rubble_[(size_t)y * w_ + x] = tag;
    box[0] = x0; box[1] = y0; box[2] = x1; box[3] = y1;
    return true;
}

void TerrainRenderer::sync(TerrainSystem& terrain) {
    if (!built_ || (terrain.dirty.empty() && terrain.rubbleAdded.empty() && terrain.rubbleRemoved.empty())) {
        terrain.dirty.clear();
        return;
    }

    struct Box { int x0, y0, x1, y1; };
    std::vector<Box> boxes;
    auto addBox = [&](Box b) {
        for (auto& o : boxes) {
            if (b.x0 <= o.x1 + 24 && o.x0 <= b.x1 + 24 && b.y0 <= o.y1 + 24 && o.y0 <= b.y1 + 24) {
                o.x0 = std::min(o.x0, b.x0); o.y0 = std::min(o.y0, b.y0);
                o.x1 = std::max(o.x1, b.x1); o.y1 = std::max(o.y1, b.y1);
                return;
            }
        }
        boxes.push_back(b);
    };
    const int pad = SMOOTH_R + 3;
    int bb[4];
    for (Chunk* c : terrain.rubbleRemoved)
        if (eraseRubble(c, bb)) addBox({bb[0] - pad, bb[1] - pad, bb[2] + pad + 1, bb[3] + pad + 1});
    for (Chunk* c : terrain.rubbleAdded)
        if (c->state == ChunkState::SOLID && c->landed && bakeRubble(c, bb))
            addBox({bb[0] - pad, bb[1] - pad, bb[2] + pad + 1, bb[3] + pad + 1});
    terrain.rubbleRemoved.clear();
    terrain.rubbleAdded.clear();

    for (Chunk* c : terrain.dirty) {
        if (c->uv.empty()) continue;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (auto& p : c->uv) {
            minX = std::min(minX, p.first); maxX = std::max(maxX, p.first);
            minY = std::min(minY, p.second); maxY = std::max(maxY, p.second);
        }
        int x0 = std::max(0, (int)floorf(minX)), x1 = std::min(w_ - 1, (int)ceilf(maxX));
        int y0 = std::max(0, (int)floorf(minY)), y1 = std::min(h_ - 1, (int)ceilf(maxY));
        uint16_t tag = (uint16_t)(c->id + 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                size_t i = (size_t)y * w_ + x;
                if (owner_[i] == tag) visible_[i] = 0;
            }
        // Pad by the outline width so neighbours get re-edged too.
        addBox({x0 - pad, y0 - pad, x1 + pad + 1, y1 + pad + 1});
    }
    terrain.dirty.clear();
    for (auto& b : boxes) refreshRegion(b.x0, b.y0, b.x1, b.y1, true);
}

void TerrainRenderer::drawStatic() const {
    if (!built_) return;
    DrawTexture(displayTex_, 0, 0, WHITE);
}

void TerrainRenderer::drawLoose(const TerrainSystem& terrain) const {
    if (!built_) return;
    const float iw = 1.0f / w_, ih = 1.0f / h_;
    Vector2 pts[8];
    for (const auto& row : terrain.chunks) {
        for (const Chunk* c : row) {
            // Landed rubble is baked into `display`; only falling chunks here.
            bool dynamic = c->state == ChunkState::DYNAMIC && c->body;
            if (!dynamic) continue;
            size_t n = std::min<size_t>(c->verts.size(), 8);
            if (n != 4 || c->uv.size() != 4) continue;
            if (dynamic) {
                // Verts are stored in pixel space relative to the slab origin;
                // rotate about it by the body angle and offset to the body's
                // current pixel position. Y-down pixel space flips the angle.
                float ca = cosf(c->bodyAngle), sa = sinf(c->bodyAngle);
                for (size_t k = 0; k < n; k++) {
                    float lx = c->verts[k].first - c->bodyOriginX;
                    float ly = c->verts[k].second - c->bodyOriginY;
                    pts[k] = Vector2{c->bodyDrawX + lx * ca + ly * sa,
                                     c->bodyDrawY - lx * sa + ly * ca};
                }
            } else {
                for (size_t k = 0; k < n; k++) pts[k] = Vector2{c->verts[k].first, c->verts[k].second};
            }
            // Grow each quad ~0.6 px about its centre (positions and UVs alike)
            // so neighbouring chunks of a rotated slab overlap slightly instead
            // of leaving hairline seams where the background shows through.
            const float grow = 1.12f;
            Vector2 pc{0, 0}, uc{0, 0};
            for (int k = 0; k < 4; k++) {
                pc.x += pts[k].x * 0.25f; pc.y += pts[k].y * 0.25f;
                uc.x += c->uv[k].first * 0.25f; uc.y += c->uv[k].second * 0.25f;
            }
            rlCheckRenderBatchLimit(4);
            rlSetTexture(baseTex_.id);
            rlBegin(RL_QUADS);
            rlColor4ub(255, 255, 255, 255);
            // Reverse the TL,TR,BR,BL order so the quad winds counter-clockwise
            // on screen and survives raylib's default back-face culling.
            for (int k = 3; k >= 0; k--) {
                float u = uc.x + (c->uv[k].first - uc.x) * grow;
                float v = uc.y + (c->uv[k].second - uc.y) * grow;
                rlTexCoord2f(u * iw, v * ih);
                rlVertex2f(pc.x + (pts[k].x - pc.x) * grow, pc.y + (pts[k].y - pc.y) * grow);
            }
            rlEnd();
            rlSetTexture(0);
        }
    }
}

Color TerrainRenderer::sample(float x, float y) const {
    if (!built_) return GRAY;
    int ix = std::clamp((int)x, 0, w_ - 1), iy = std::clamp((int)y, 0, h_ - 1);
    Color c = base_[(size_t)iy * w_ + ix];
    if (c.a == 0) return shade(edge_, 2.5f);
    return c;
}
