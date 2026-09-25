#pragma once
#include <vector>
#include <string>
#include <raylib.h>
#include <box2d/box2d.h>

class World;
class Chunk;

namespace Entities {

struct WormConfig {
    float x, y;
    int teamIndex;
    std::string color;
};

class Worm {
public:
    b2Body* body = nullptr;
    World* world = nullptr;
    float hp, maxHp;
    int teamIndex;
    std::string color;
    bool grounded = false;
    bool alive = true;
    std::string name;
    // Logical facing for backflips (the body is fixed-rotation and never
    // actually turns). Defaults to +1 (right); walk() updates it, so a
    // backflip hops backward relative to the direction the worm last walked.
    int facing = 1;
    
    Worm(float x, float y, int team, const char* color);
    void init(World* world, float px, float py);
    void update(float dt);
    void walk(int direction, float dt);
    void jump();
    void backflip();
    void takeDamage(float damage, float kx = 0, float ky = 0);
    bool isAlive() const { return alive; }
    int getTeamIndex() const { return teamIndex; }
    Vector2 getPosition() const;
    
private:
    void die();
};

} // namespace Entities
