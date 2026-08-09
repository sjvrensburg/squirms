#pragma once
#include <vector>

namespace Entities { class Worm; }

enum class TurnState { PLAYING, GAME_OVER };

struct TurnConfig {
    float timer = 60.0f;
    int currentTeamIndex = 0;
};

class TurnSystem {
public:
    TurnState state = TurnState::PLAYING;
    float timer = 60.0f;
    int currentTeamIndex = 0;
    int currentWormIndex = 0;
    
    TurnSystem(const std::vector<std::vector<Entities::Worm*>>& worms, int teamCount);
    void update(float dt);
    Entities::Worm* getCurrentWorm();
    float getWind();
    
private:
    std::vector<std::vector<Entities::Worm*>> worms_;
    int teamCount_;

    void advanceTurn();
};
