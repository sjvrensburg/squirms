#pragma once
#include <raylib.h>

// Text drawing with the bundled Montserrat faces (assets/fonts, embedded
// into the WASM build). Falls back to raylib's default font if loading fails.
namespace Text {

void init();
void unload();

// `heavy` selects Montserrat Black (titles, labels) vs Bold (body text).
Vector2 measure(const char* text, float size, bool heavy = true);
void draw(const char* text, Vector2 pos, float size, Color color, bool heavy = true);
// Draw with a solid outline `thick` px wide, the chunky cartoon look.
void drawOutlined(const char* text, Vector2 pos, float size, Color fill, Color outline,
                  float thick = 2.0f, bool heavy = true);
// Centered on `center` (both axes).
void drawCentered(const char* text, Vector2 center, float size, Color fill, Color outline,
                  float thick = 2.0f, bool heavy = true);

} // namespace Text
