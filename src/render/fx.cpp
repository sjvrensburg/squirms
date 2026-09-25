#include "fx.h"
#include "text.h"
#include "terrain_render.h"
#include "../terrain/grid.h"
#include <algorithm>
#include <cmath>

namespace {
float frand() { return (float)GetRandomValue(0, 10000) / 10000.0f; }
float frange(float a, float b) { return a + (b - a) * frand(); }
Color lerpColor(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}
constexpr size_t MAX_PARTICLES = 2500;
} // namespace

void Fx::clear() {
    particles.clear();
    texts.clear();
    shake = 0;
}

void Fx::add(const Particle& p) {
    if (particles.size() >= MAX_PARTICLES) return;
    particles.push_back(p);
}

void Fx::update(float dt, float wind) {
    for (auto& p : particles) {
        p.life -= dt;
        p.v.y += p.gravity * dt;
        float d = std::max(0.0f, 1.0f - p.drag * dt);
        p.v.x *= d;
        p.v.y *= d;
        // Smoke and feathers drift with the wind.
        if (p.kind == Kind::Smoke || p.kind == Kind::Feather) p.v.x += wind * 18.0f * dt;
        p.p.x += p.v.x * dt;
        p.p.y += p.v.y * dt;
        p.size += p.grow * dt;
        p.rot += p.spin * dt;
        // Debris and droplets vanish in the sea.
        if ((p.kind == Kind::Debris || p.kind == Kind::Droplet || p.kind == Kind::Spark) &&
            p.p.y > Terrain::WATER_Y + 4) p.life = 0;
        if (p.kind == Kind::Bubble && p.p.y < Terrain::WATER_Y) p.life = 0;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(),
                                   [](const Particle& p) { return p.life <= 0 || p.size <= 0; }),
                    particles.end());

    for (auto& t : texts) {
        t.life -= dt;
        t.p.y -= t.rise * dt;
    }
    texts.erase(std::remove_if(texts.begin(), texts.end(), [](const Label& t) { return t.life <= 0; }),
                texts.end());

    shake = std::max(0.0f, shake - dt * 30.0f);
}

void Fx::drawWorld() const {
    for (const auto& p : particles) {
        float t = 1.0f - p.life / p.maxLife;  // 0 -> 1 over its life
        switch (p.kind) {
            case Kind::Smoke: {
                Color c = p.color;
                c.a = (unsigned char)(c.a * (1.0f - t) * (t < 0.1f ? t * 10.0f : 1.0f));
                DrawCircleV(p.p, p.size, c);
                break;
            }
            case Kind::Fire: {
                Color c = t < 0.35f ? lerpColor(Color{255, 250, 210, 255}, Color{255, 180, 40, 255}, t / 0.35f)
                                    : lerpColor(Color{255, 150, 30, 255}, Color{120, 30, 20, 0}, (t - 0.35f) / 0.65f);
                DrawCircleV(p.p, p.size, c);
                break;
            }
            case Kind::Spark: {
                Color c = lerpColor(Color{255, 255, 200, 255}, Color{255, 120, 20, 0}, t);
                Vector2 tail{p.p.x - p.v.x * 0.03f, p.p.y - p.v.y * 0.03f};
                DrawLineEx(tail, p.p, p.size, c);
                break;
            }
            case Kind::Debris: {
                Color c = p.color;
                if (t > 0.8f) c.a = (unsigned char)(255 * (1.0f - t) / 0.2f);
                Rectangle r{p.p.x, p.p.y, p.size, p.size};
                DrawRectanglePro(r, Vector2{p.size / 2, p.size / 2}, p.rot * RAD2DEG, c);
                break;
            }
            case Kind::Bubble:
                DrawCircleLinesV(p.p, p.size, Color{230, 245, 255, (unsigned char)(200 * (1 - t))});
                break;
            case Kind::Droplet: {
                Color c = p.color;
                c.a = (unsigned char)(c.a * (1 - t));
                DrawCircleV(p.p, p.size, c);
                break;
            }
            case Kind::Flash: {
                Color c{255, 255, 240, (unsigned char)(255 * (1 - t) * (1 - t))};
                DrawCircleV(p.p, p.size * (0.6f + t * 0.6f), c);
                break;
            }
            case Kind::Ring: {
                float r = p.size * (0.3f + t * 1.1f);
                DrawRing(p.p, r - 3.0f * (1 - t) - 1, r, 0, 360, 48,
                         Color{255, 255, 255, (unsigned char)(170 * (1 - t))});
                break;
            }
            case Kind::Star: {
                Color c = p.color;
                c.a = (unsigned char)(255 * (1 - t));
                float s = p.size * (1 - t * 0.5f);
                DrawLineEx({p.p.x - s, p.p.y}, {p.p.x + s, p.p.y}, 2, c);
                DrawLineEx({p.p.x, p.p.y - s}, {p.p.x, p.p.y + s}, 2, c);
                break;
            }
            case Kind::Feather: {
                Color c = p.color;
                c.a = (unsigned char)(255 * std::min(1.0f, (1 - t) * 3));
                DrawEllipse((int)p.p.x, (int)p.p.y, p.size, p.size * 0.45f, c);
                break;
            }
        }
    }
}

void Fx::drawText(Vector2 (*toScreen)(Vector2), float zoom) const {
    for (const auto& t : texts) {
        float age = t.maxLife - t.life;
        float alpha = std::min(1.0f, t.life / 0.35f);
        Vector2 s = toScreen(t.p);
        if (t.comic) {
            // Pop in with a little overshoot, then hold, then fade.
            float k = age < 0.18f ? (age / 0.18f) * 1.25f : (age < 0.3f ? 1.25f - (age - 0.18f) / 0.12f * 0.25f : 1.0f);
            float size = t.size * k * std::max(0.6f, zoom);
            Vector2 m = Text::measure(t.text.c_str(), size);
            float rx = m.x * 0.62f + 16, ry = m.y * 0.75f + 10;
            // Starburst balloon: alternating long/short spikes.
            const int spikes = 14;
            Vector2 pts[spikes * 2 + 2];
            pts[0] = s;
            for (int i = 0; i <= spikes * 2; i++) {
                float a = (float)i / (spikes * 2) * 2 * PI + 0.2f;
                float r = (i % 2 == 0) ? 1.0f : 0.72f;
                pts[i + 1] = Vector2{s.x + cosf(a) * rx * r, s.y + sinf(a) * ry * r};
            }
            Color outline{190, 20, 20, (unsigned char)(255 * alpha)};
            Color fill{255, 226, 60, (unsigned char)(255 * alpha)};
            for (int i = 1; i <= spikes * 2; i++) {
                Vector2 a = pts[i], b = pts[i + 1];
                Vector2 ao{s.x + (a.x - s.x) * 1.12f, s.y + (a.y - s.y) * 1.12f};
                Vector2 bo{s.x + (b.x - s.x) * 1.12f, s.y + (b.y - s.y) * 1.12f};
                DrawTriangle(s, bo, ao, outline);
            }
            for (int i = 1; i <= spikes * 2; i++) DrawTriangle(s, pts[i + 1], pts[i], fill);
            Text::drawOutlined(t.text.c_str(), {s.x - m.x / 2, s.y - m.y / 2}, size,
                               Color{230, 30, 30, (unsigned char)(255 * alpha)},
                               Color{255, 255, 255, (unsigned char)(255 * alpha)}, 2);
        } else {
            float size = t.size;
            Vector2 m = Text::measure(t.text.c_str(), size);
            Color c = t.color;
            c.a = (unsigned char)(255 * alpha);
            Text::drawOutlined(t.text.c_str(), {s.x - m.x / 2, s.y - m.y / 2}, size, c,
                               Color{0, 0, 0, (unsigned char)(230 * alpha)}, 2);
        }
    }
}

void Fx::explosion(float x, float y, float radius, const TerrainRenderer* terrain) {
    float r = radius;
    add({{x, y}, {0, 0}, 0.22f, 0.22f, r * 1.1f, 0, 0, 0, WHITE, Kind::Flash, 0, 0});
    add({{x, y}, {0, 0}, 0.45f, 0.45f, r * 1.25f, 0, 0, 0, WHITE, Kind::Ring, 0, 0});

    int fire = (int)(10 + r * 0.35f);
    for (int i = 0; i < fire; i++) {
        float a = frand() * 2 * PI, d = frand() * r * 0.55f, sp = frange(20, 90) * (r / 50.0f);
        float life = frange(0.35f, 0.75f);
        add({{x + cosf(a) * d, y + sinf(a) * d}, {cosf(a) * sp, sinf(a) * sp - 30},
             life, life, frange(r * 0.18f, r * 0.34f), frange(-10, 10), 0, 0,
             WHITE, Kind::Fire, -40, 1.5f});
    }

    int smoke = (int)(6 + r * 0.2f);
    for (int i = 0; i < smoke; i++) {
        float a = frand() * 2 * PI, d = frand() * r * 0.6f;
        float life = frange(1.2f, 2.4f);
        unsigned char g = (unsigned char)frange(50, 110);
        add({{x + cosf(a) * d, y + sinf(a) * d}, {frange(-20, 20), frange(-50, -15)}, life, life,
             frange(r * 0.2f, r * 0.35f), frange(8, 20), 0, 0, Color{g, g, g, 170}, Kind::Smoke, -8, 0.8f});
    }

    int sparks = (int)(8 + r * 0.25f);
    for (int i = 0; i < sparks; i++) {
        float a = frand() * 2 * PI, sp = frange(150, 420);
        float life = frange(0.3f, 0.8f);
        add({{x, y}, {cosf(a) * sp, sinf(a) * sp - 80}, life, life, frange(1.5f, 2.5f), 0, 0, 0,
             WHITE, Kind::Spark, 500, 0.6f});
    }

    // Clods of the actual terrain that was there.
    int debris = (int)(6 + r * 0.35f);
    for (int i = 0; i < debris; i++) {
        float a = frand() * 2 * PI, d = frand() * r * 0.8f;
        float px = x + cosf(a) * d, py = y + sinf(a) * d;
        Color c = terrain ? terrain->sample(px, py) : Color{120, 80, 40, 255};
        float sp = frange(120, 360);
        float life = frange(0.9f, 1.8f);
        add({{px, py}, {cosf(a) * sp * 0.7f, -fabsf(sinf(a)) * sp - 60}, life, life, frange(2.5f, 6.0f), 0,
             frand() * 6, frange(-12, 12), c, Kind::Debris, 700, 0.2f});
    }
    addShake(std::min(22.0f, r * 0.22f));
}

void Fx::splash(float x, float y, float strength) {
    int n = (int)(10 + strength * 20);
    for (int i = 0; i < n; i++) {
        float life = frange(0.5f, 1.1f);
        add({{x + frange(-10, 10), y}, {frange(-90, 90) * strength, -frange(150, 360) * strength}, life, life,
             frange(2, 4.5f), 0, 0, 0, Color{210, 235, 255, 230}, Kind::Droplet, 700, 0.3f});
    }
    add({{x, y}, {0, 0}, 0.5f, 0.5f, 18 + strength * 16, 0, 0, 0, WHITE, Kind::Ring, 0, 0});
}

void Fx::smokePuff(float x, float y, float size) {
    float life = frange(0.6f, 1.1f);
    add({{x, y}, {frange(-8, 8), frange(-25, -10)}, life, life, size, size * 1.2f, 0, 0,
         Color{200, 200, 200, 140}, Kind::Smoke, 0, 0.8f});
}

void Fx::sparkle(float x, float y, Color color, int count) {
    for (int i = 0; i < count; i++) {
        float a = frand() * 2 * PI, sp = frange(40, 160);
        float life = frange(0.5f, 1.0f);
        add({{x, y}, {cosf(a) * sp, sinf(a) * sp}, life, life, frange(3, 6), 0, 0, 0, color, Kind::Star, 60, 1.5f});
    }
}

void Fx::bubbles(float x, float y, int count) {
    for (int i = 0; i < count; i++) {
        float life = frange(0.6f, 1.4f);
        add({{x + frange(-8, 8), y + frange(0, 20)}, {frange(-10, 10), -frange(30, 70)}, life, life,
             frange(1.5f, 4), 0, 0, 0, WHITE, Kind::Bubble, 0, 0});
    }
}

void Fx::feathers(float x, float y) {
    for (int i = 0; i < 18; i++) {
        float a = frand() * 2 * PI, sp = frange(60, 220);
        float life = frange(1.5f, 2.8f);
        add({{x, y}, {cosf(a) * sp, sinf(a) * sp - 60}, life, life, frange(3, 5), 0, 0, 0,
             Color{250, 250, 245, 255}, Kind::Feather, 40, 1.8f});
    }
}

void Fx::trail(float x, float y, bool fiery) {
    if (fiery) {
        add({{x, y}, {frange(-10, 10), frange(-10, 10)}, 0.25f, 0.25f, frange(2.5f, 4.0f), -4, 0, 0,
             WHITE, Kind::Fire, 0, 0});
    }
    float life = frange(0.5f, 0.9f);
    add({{x, y}, {frange(-6, 6), frange(-12, -4)}, life, life, frange(2, 3.5f), 9, 0, 0,
         Color{215, 215, 215, 150}, Kind::Smoke, 0, 0.5f});
}

void Fx::dust(float x, float y, Color color, int count) {
    Color c{(unsigned char)std::min(255, color.r + 40), (unsigned char)std::min(255, color.g + 35),
            (unsigned char)std::min(255, color.b + 30), 150};
    for (int i = 0; i < count; i++) {
        float life = frange(0.7f, 1.4f);
        add({{x + frange(-24, 24), y + frange(-6, 10)}, {frange(-60, 60), frange(-40, -8)}, life, life,
             frange(4, 9), frange(8, 16), 0, 0, c, Kind::Smoke, 0, 1.6f});
    }
}

void Fx::crumble(float x, float y, Color color, int count) {
    for (int i = 0; i < count; i++) {
        float a = frand() * 2 * PI, sp = frange(60, 200);
        float life = frange(0.6f, 1.3f);
        add({{x + frange(-15, 15), y + frange(-10, 10)}, {cosf(a) * sp, -fabsf(sinf(a)) * sp}, life, life,
             frange(2, 4.5f), 0, frand() * 6, frange(-10, 10), color, Kind::Debris, 700, 0.2f});
    }
}

void Fx::damageNumber(float x, float y, int amount, Color teamColor) {
    texts.push_back({{x, y}, std::to_string(amount), teamColor, 1.8f, 1.8f, 22, false, 28});
}

void Fx::comicWord(float x, float y, float radius) {
    static const char* WORDS[] = {"POW!", "BOOM!", "BIFF!", "WHAM!", "KAPOW!", "BLAM!", "KABOOM!", "THWACK!"};
    const char* w = WORDS[GetRandomValue(0, 7)];
    if (radius >= 90) w = "KABOOM!";
    texts.push_back({{x, y - radius * 0.3f}, w, WHITE, 1.1f, 1.1f, 20 + std::min(radius, 110.0f) * 0.14f, true, 14});
}

void Fx::banner(float x, float y, const std::string& text, Color color, float size, float life) {
    texts.push_back({{x, y}, text, color, life, life, size, false, 12});
}
