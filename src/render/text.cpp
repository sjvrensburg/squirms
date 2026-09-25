#include "text.h"
#include <cmath>

namespace Text {

namespace {
Font heavyFont{};
Font boldFont{};
bool loaded = false;

// Load at a generous atlas size and let bilinear filtering scale it down;
// that keeps small HUD text and big titles both reasonably crisp.
Font loadFace(const char* path) {
    Font f = LoadFontEx(path, 64, nullptr, 0);
    if (f.texture.id == 0) return GetFontDefault();
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return f;
}

const Font& face(bool heavy) { return heavy ? heavyFont : boldFont; }

float spacingFor(float size) { return size * 0.02f; }
} // namespace

void init() {
    if (loaded) return;
    heavyFont = loadFace("assets/fonts/Montserrat-Black.otf");
    boldFont = loadFace("assets/fonts/Montserrat-Bold.otf");
    loaded = true;
}

void unload() {
    if (!loaded) return;
    if (heavyFont.texture.id != GetFontDefault().texture.id) UnloadFont(heavyFont);
    if (boldFont.texture.id != GetFontDefault().texture.id) UnloadFont(boldFont);
    loaded = false;
}

Vector2 measure(const char* text, float size, bool heavy) {
    if (!loaded) return Vector2{(float)MeasureText(text, (int)size), size};
    return MeasureTextEx(face(heavy), text, size, spacingFor(size));
}

void draw(const char* text, Vector2 pos, float size, Color color, bool heavy) {
    if (!loaded) { DrawText(text, (int)pos.x, (int)pos.y, (int)size, color); return; }
    DrawTextEx(face(heavy), text, pos, size, spacingFor(size), color);
}

void drawOutlined(const char* text, Vector2 pos, float size, Color fill, Color outline,
                  float thick, bool heavy) {
    // Eight offset copies make a solid outline without shaders.
    const int dirs[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    for (auto& d : dirs) {
        draw(text, Vector2{pos.x + d[0] * thick, pos.y + d[1] * thick}, size, outline, heavy);
    }
    draw(text, pos, size, fill, heavy);
}

void drawCentered(const char* text, Vector2 center, float size, Color fill, Color outline,
                  float thick, bool heavy) {
    Vector2 m = measure(text, size, heavy);
    drawOutlined(text, Vector2{roundf(center.x - m.x / 2), roundf(center.y - m.y / 2)}, size, fill,
                 outline, thick, heavy);
}

} // namespace Text
