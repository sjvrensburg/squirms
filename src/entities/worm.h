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
    float hp, maxHp;
    int teamIndex;
    std::string color;
    bool grounded = false;
    bool alive = true;
    std::string name;
    
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
