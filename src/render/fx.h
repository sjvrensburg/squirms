#pragma once
#include <raylib.h>
#include <string>
#include <vector>

class TerrainRenderer;

// Particles, floating text and screen shake. Simulated on the fixed tick,
// drawn in world space (inside BeginMode2D) except for text, which is drawn
// in screen space so it stays crisp at any zoom.
class Fx {
public:
    enum class Kind { Smoke, Fire, Spark, Debris, Bubble, Droplet, Flash, Ring, Star, Feather };

    struct Particle {
        Vector2 p, v;
        float life, maxLife;
        float size, grow;
        float rot, spin;
        Color color;
        Kind kind;
        float gravity;   // px/s^2
        float drag;      // fraction of velocity lost per second
    };

    struct Label {
        Vector2 p;
        std::string text;
        Color color;
        float life, maxLife;
        float size;
        bool comic;      // starburst "POW!" style
        float rise;      // px/s upward drift
    };

    void clear();
    void update(float dt, float wind);
    void drawWorld() const;
    void drawText(Vector2 (*toScreen)(Vector2), float zoom) const;

    // Canned effects.
    void explosion(float x, float y, float radius, const TerrainRenderer* terrain);
    void splash(float x, float y, float strength);
    void smokePuff(float x, float y, float size);
    void sparkle(float x, float y, Color color, int count);
    void bubbles(float x, float y, int count);
    void feathers(float x, float y);    // sheep went pop
    void trail(float x, float y, bool fiery);
    void dust(float x, float y, Color color, int count);     // cloud where terrain lands
    void crumble(float x, float y, Color color, int count);  // bits breaking off a slab
    void damageNumber(float x, float y, int amount, Color teamColor);
    void comicWord(float x, float y, float radius);
    void banner(float x, float y, const std::string& text, Color color, float size, float life);
    void addShake(float amount) { if (amount > shake) shake = amount; }

    float shake = 0;
    std::vector<Particle> particles;
    std::vector<Label> texts;

private:
    void add(const Particle& p);
};
