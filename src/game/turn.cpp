#include "turn.h"
#include "../entities/worm.h"
#include <algorithm>
#include <cstdlib>

TurnSystem::TurnSystem(const std::vector<std::vector<Entities::Worm*>>& worms, int teamCount)
    : worms_(worms), teamCount_(teamCount) {}

void TurnSystem::update(float dt) {
    if (state != TurnState::PLAYING) return;
    timer -= dt;
    if (timer <= 0.0f) {
        advanceTurn();
    }
}

void TurnSystem::advanceTurn() {
    timer = 60.0f;
    if (teamCount_ <= 0) return;

    for (int attempts = 0; attempts < teamCount_; attempts++) {
        currentTeamIndex = (currentTeamIndex + 1) % teamCount_;
        if (currentTeamIndex >= (int)worms_.size()) continue;
        auto& team = worms_[currentTeamIndex];
        int teamSize = (int)team.size();
        if (teamSize == 0) continue;
        for (int i = 0; i < teamSize; i++) {
            int idx = (currentWormIndex + 1 + i) % teamSize;
            if (team[idx] && team[idx]->isAlive()) {
                currentWormIndex = idx;
                return;
            }
        }
    }
}

Entities::Worm* TurnSystem::getCurrentWorm() {
    if (worms_.empty() || currentTeamIndex >= (int)worms_.size() || 
        worms_[currentTeamIndex].empty() || currentWormIndex >= (int)worms_[currentTeamIndex].size()) {
        return nullptr;
    }
    return worms_[currentTeamIndex][currentWormIndex];
}

float TurnSystem::getWind() {
    return ((float)((double)rand() / RAND_MAX) - 0.5f) * 4.0f;
}
