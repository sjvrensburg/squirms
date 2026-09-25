#include "turn.h"
#include "../entities/worm.h"
#include <algorithm>
#include <cstdlib>

// How long the just-fired worm gets to move without being able to fire, and
// the hard cap on how long we wait for the world to settle before forcing the
// next turn (so a stuck world can never stall the game).
constexpr float RETREAT_SECONDS = 4.0f;
constexpr float SETTLE_CAP_SECONDS = 10.0f;

TurnSystem::TurnSystem(const std::vector<std::vector<Entities::Worm*>>& worms, int teamCount)
    : worms_(worms), teamCount_(teamCount) {
    rollWind();
}

void TurnSystem::update(float dt, bool worldSettled) {
    if (state != TurnState::PLAYING) return;

    switch (phase) {
        case TurnPhase::AIMING:
            timer -= dt;
            // Leave AIMING as soon as the worm has fired, the timer expires, or
            // the current worm has died (its own blast, falling out of the map).
            // The death check also covers a worm that vanished mid-flight.
            if (firedThisTurn_ || timer <= 0.0f ||
                !getCurrentWorm() || !getCurrentWorm()->isAlive()) {
                phase = TurnPhase::RETREAT;
                retreatTimer_ = RETREAT_SECONDS;
            }
            break;

        case TurnPhase::RETREAT:
            retreatTimer_ -= dt;
            if (retreatTimer_ <= 0.0f) {
                phase = TurnPhase::SETTLING;
                settleTimer_ = SETTLE_CAP_SECONDS;
            }
            break;

        case TurnPhase::SETTLING:
            settleTimer_ -= dt;
            // Move on once the world is quiet, or once the safety cap elapses.
            if (worldSettled || settleTimer_ <= 0.0f) {
                advanceTurn();
            }
            break;

        default:
            break;
    }
}

void TurnSystem::noteFired() {
    firedThisTurn_ = true;
}

bool TurnSystem::canMove() const {
    return phase == TurnPhase::AIMING || phase == TurnPhase::RETREAT;
}

bool TurnSystem::canFire() const {
    return phase == TurnPhase::AIMING && !firedThisTurn_;
}

Entities::Worm* TurnSystem::getCurrentWorm() {
    if (worms_.empty() || currentTeamIndex >= (int)worms_.size() ||
        worms_[currentTeamIndex].empty() || currentWormIndex >= (int)worms_[currentTeamIndex].size()) {
        return nullptr;
    }
    return worms_[currentTeamIndex][currentWormIndex];
}

void TurnSystem::rollWind() {
    wind_ = ((float)((double)rand() / RAND_MAX) - 0.5f) * 4.0f;
}

void TurnSystem::advanceTurn() {
    // Roll a fresh wind and reset per-turn flags before handing control over.
    rollWind();
    timer = 60.0f;
    firedThisTurn_ = false;
    phase = TurnPhase::AIMING;

    if (teamCount_ <= 0) return;

    // Skip teams with no living worms, and within a team skip dead worms,
    // rotating through each team's worms in order.
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

    // No living worms remain on any team: the match is a draw.
    state = TurnState::GAME_OVER;
}

MatchResult TurnSystem::evaluateResult(int* winner) const {
    int livingTeams = 0;
    int lastLivingTeam = -1;
    for (size_t t = 0; t < worms_.size(); t++) {
        for (auto* worm : worms_[t]) {
            if (worm && worm->isAlive()) {
                livingTeams++;
                lastLivingTeam = (int)t;
                break;
            }
        }
    }

    if (livingTeams <= 1) {
        if (winner) *winner = lastLivingTeam;  // -1 when no worms are left (draw)
        return MatchResult::TEAM_WON;
    }
    return MatchResult::IN_PROGRESS;
}
