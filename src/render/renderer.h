#pragma once
#include <raylib.h>
#include <vector>
#include <string>

class GameCamera;
namespace Entities { class Worm; class Explosion; class Projectile; }
namespace Terrain { struct Chunk; }

using ChunkGrid = std::vector<std::vector<Terrain::Chunk*>>;

void render(const GameCamera& camera, const ChunkGrid& chunks, 
            const std::vector<Entities::Worm*>& worms,
            const std::vector<Entities::Explosion>& explosions,
            const std::vector<Entities::Projectile>& projectiles,
            int screenWidth, int screenHeight,
            const Entities::Worm* currentWorm, bool debugMode, bool matchRunning);

struct HUDState {
    std::vector<Entities::Worm*> worms;
    int currentTeamIndex;
    float timer;
    float wind;
    std::string weapon;
    float chargeLevel;
    bool debugMode;
    int fps;
    int staticBodies;
    int dynamicBodies;
};

void renderHUD(const HUDState& state);
