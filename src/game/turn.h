#pragma once
#include <vector>

namespace Entities { class Worm; }

enum class TurnState { PLAYING, GAME_OVER };

// Phases of a single turn.
//   AIMING   - current worm may move and take its one shot; turn timer runs.
//   RETREAT  - current worm has fired (or timed out): it may still move, but
//              not fire, for a short window so it can escape the muzzle flash.
//   SETTLING - no input allowed; we wait for the world to settle (shells gone,
//              explosions gone, terrain slabs at rest) before the next turn.
enum class TurnPhase { AIMING, RETREAT, SETTLING };

// How the match resolves once the game-over condition is met.
enum class MatchResult { IN_PROGRESS, TEAM_WON, DRAW };

struct TurnConfig {
    float timer = 60.0f;
    int currentTeamIndex = 0;
};

class TurnSystem {
public:
    TurnSystem(const std::vector<std::vector<Entities::Worm*>>& worms, int teamCount);
    void update(float dt, bool worldSettled);
    Entities::Worm* getCurrentWorm();

    // Wind is rolled once per turn and held here, so both the HUD and every
    // shot during the turn see the same value.
    float getWind() const { return wind_; }

    // Called by main.cpp when the current worm fires. After this the current
    // worm may still move (retreat) but can no longer fire this turn.
    void noteFired();

    // Input gating: movement is allowed in AIMING and RETREAT; firing is only
    // allowed once, during AIMING.
    bool canMove() const;
    bool canFire() const;

    // Match resolution. Returns the current result and, on TEAM_WON, fills
    // *winner with the winning team index (0-based). DRAW sets *winner = -1.
    MatchResult evaluateResult(int* winner = nullptr) const;

    TurnState state = TurnState::PLAYING;
    TurnPhase phase = TurnPhase::AIMING;
    float timer = 60.0f;
    int currentTeamIndex = 0;
    int currentWormIndex = 0;

private:
    std::vector<std::vector<Entities::Worm*>> worms_;
    int teamCount_;
    float wind_ = 0.0f;
    float retreatTimer_ = 0.0f;
    float settleTimer_ = 0.0f;
    bool firedThisTurn_ = false;

    void advanceTurn();
    void rollWind();
};
