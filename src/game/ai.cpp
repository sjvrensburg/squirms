// Computer-controlled teams. On its turn a CPU worm "thinks" for a moment,
// brute-forces a spread of bazooka and grenade throws through a simple
// ballistic model (gravity, wind, terrain grid), picks the one expected to
// hurt enemies most without hurting its own side, then visibly turns, aims
// and winds up the shot like a player would.
#include "game.h"
#include "../terrain/grid.h"
#include "../terrain/terrain.h"
#include "../audio/sfx.h"
#include <algorithm>
#include <cmath>

using Entities::Worm;

namespace {

constexpr float GRAVITY_PX = 10.0f * Terrain::PPM;   // matches World(0, -10)
constexpr float WIND_PX = 4.0f * Terrain::PPM;       // matches Game::updateProjectiles
constexpr float SIM_DT = 1.0f / 60.0f;

float frand() { return (float)GetRandomValue(0, 10000) / 10000.0f; }

} // namespace

// Where a shell launched from `from` at `vel` first hits terrain or a worm.
// Returns false if it leaves the map or drops into the sea.
static bool simulateShot(const Game& g, Vector2 from, Vector2 vel, bool wind, const Worm* self,
                         float maxTime, Vector2& hit) {
    Vector2 p = from;
    float windAcc = wind ? g.wind * WIND_PX : 0;
    for (float t = 0; t < maxTime; t += SIM_DT) {
        vel.y += GRAVITY_PX * SIM_DT;
        vel.x += windAcc * SIM_DT;
        p.x += vel.x * SIM_DT;
        p.y += vel.y * SIM_DT;
        if (p.x < -200 || p.x > Terrain::WORLD_PX_W + 200 || p.y > Terrain::WATER_Y) return false;
        if (p.y >= 0) {
            int col = (int)(p.x / Terrain::CELL), row = (int)(p.y / Terrain::CELL);
            if (col >= 0 && col < Terrain::COLS && row >= 0 && row < Terrain::ROWS &&
                g.terrain->chunks[row][col]->state == Terrain::ChunkState::SOLID) {
                hit = p;
                return true;
            }
        }
        if (t > 0.05f) {
            for (auto* w : g.worms) {
                if (w == self || !w->canAct()) continue;
                Vector2 wp = w->pixelPos();
                if (fabsf(wp.x - p.x) < 10 && fabsf(wp.y - p.y) < 15) {
                    hit = p;
                    return true;
                }
            }
        }
    }
    return false;
}

// Expected value of a blast at `at`: damage to enemies, minus (weighted)
// damage to our own team, with a bonus for enemies near the sea.
static float scoreBlast(const Game& g, Vector2 at, float radius, float damage, int team) {
    float score = 0;
    for (auto* w : g.worms) {
        if (!w->canAct()) continue;
        Vector2 p = w->pixelPos();
        float d = std::max(0.0f, sqrtf((p.x - at.x) * (p.x - at.x) + (p.y - at.y) * (p.y - at.y)) - 10.0f);
        float reach = radius * 1.1f;
        if (d >= reach) continue;
        float dmg = std::min(w->hp, damage * (1 - d / reach));
        bool kill = dmg >= w->hp;
        if (w->teamIndex == team) score -= dmg * 1.6f + (kill ? 60 : 0);
        else score += dmg + (kill ? 40 : 0) + (p.y > Terrain::WATER_Y - 120 ? 10 : 0);
    }
    return score;
}

void Game::planCpuTurn() {
    ai = AiPlan{};
    Worm* me = current;
    if (!me) return;
    Team& team = teams[currentTeam];
    Vector2 pos = me->pixelPos();

    // Closest enemy, used as a fallback aim point when nothing scores.
    Vector2 nearest = pos;
    float nearestD = 1e9f;
    for (auto* w : worms) {
        if (!w->canAct() || w->teamIndex == currentTeam) continue;
        Vector2 p = w->pixelPos();
        float d = fabsf(p.x - pos.x) + fabsf(p.y - pos.y);
        if (d < nearestD) { nearestD = d; nearest = p; }
    }

    struct Option { WeaponId weapon; float maxSpeed, radius, damage; bool wind; float fuse; };
    std::vector<Option> options = {{WeaponId::Bazooka, 950, 50, 50, true, 0}};
    options.push_back({WeaponId::Grenade, 720, 50, 50, false, 3});
    if (team.ammo[(int)WeaponId::BananaBomb] != 0) options.push_back({WeaponId::BananaBomb, 720, 62, 70, false, 3});
    if (team.ammo[(int)WeaponId::HolyGrenade] != 0) options.push_back({WeaponId::HolyGrenade, 620, 115, 100, false, 3});

    float best = 0.5f, bestMiss = 1e9f;
    AiPlan missPlan{};
    for (const auto& o : options) {
        // Specials are saved for shots that really pay off.
        float bar = (o.weapon == WeaponId::BananaBomb || o.weapon == WeaponId::HolyGrenade) ? 70.0f : 0.0f;
        for (int facing : {-1, 1}) {
            for (float aim = -1.2f; aim <= 1.45f; aim += 0.06f) {
                Vector2 dir{facing * cosf(aim), -sinf(aim)};
                Vector2 from{pos.x + dir.x * 16, pos.y + dir.y * 16};
                for (float power = 0.15f; power <= 1.0f; power += 0.06f) {
                    float speed = 60.0f + power * o.maxSpeed;
                    Vector2 hit;
                    float maxT = o.fuse > 0 ? o.fuse : 6.0f;
                    if (!simulateShot(*this, from, {dir.x * speed, dir.y * speed}, o.wind, me, maxT, hit)) continue;
                    float s = scoreBlast(*this, hit, o.radius, o.damage, currentTeam);
                    if (s > best && s > bar) {
                        best = s;
                        ai = AiPlan{true, o.weapon, facing, aim, power, 3};
                    }
                    // Remember the closest safe near-miss as a fallback.
                    float self = sqrtf((hit.x - pos.x) * (hit.x - pos.x) + (hit.y - pos.y) * (hit.y - pos.y));
                    float miss = fabsf(hit.x - nearest.x) + fabsf(hit.y - nearest.y);
                    if (o.weapon == WeaponId::Bazooka && self > o.radius * 1.3f && miss < bestMiss) {
                        bestMiss = miss;
                        missPlan = AiPlan{true, o.weapon, facing, aim, power, 3};
                    }
                }
            }
        }
    }
    if (!ai.ready) {
        if (missPlan.ready) ai = missPlan;
        else ai = AiPlan{true, WeaponId::SkipGo, me->facing, me->aim, 0, 3};
    }
    // Not a perfect shot every time.
    ai.aim += (frand() - 0.5f) * 0.06f;
    ai.power = std::clamp(ai.power + (frand() - 0.5f) * 0.05f, 0.05f, 1.0f);
}

void Game::updateCpu(float dt) {
    if (!current || !current->canAct() || fired || phase != Phase::Aiming) return;
    aiTimer += dt;
    switch (aiStage) {
        case 0:  // thinking
            if (aiTimer > 0.9f) {
                planCpuTurn();
                aiStage = 1;
                aiTimer = 0;
                if (ai.weapon != weapon) selectWeapon(ai.weapon);
            }
            break;
        case 1: {  // turn round and sweep the crosshair onto target
            if (current->facing != ai.facing && aiTimer > 0.2f) {
                current->facing = ai.facing;
                aiTimer = 0;
            }
            float d = ai.aim - current->aim;
            float step = 1.5f * dt;
            if (fabsf(d) <= step) {
                current->aim = ai.aim;
                if (aiTimer > 0.5f) {
                    aiStage = 2;
                    aiTimer = 0;
                    if (weaponInfo(weapon).mode == FireMode::Charged) {
                        charging = true;
                        power = 0;
                        Audio::playSound("charge", 0.6f);
                    } else {
                        fire();
                        aiStage = 3;
                    }
                }
            } else {
                current->aim += d > 0 ? step : -step;
            }
            break;
        }
        case 2:  // wind up
            power = std::min(1.0f, power + dt / 1.5f);
            if (power >= ai.power) {
                fire();
                charging = false;
                aiStage = 3;
            }
            break;
        default:
            break;
    }
}
