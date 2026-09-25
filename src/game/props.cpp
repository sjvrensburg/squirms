// Barrels, mines, supply crates and gravestones.
#include "game.h"
#include "../physics/world.h"
#include "../terrain/grid.h"
#include "../terrain/terrain.h"
#include "../audio/sfx.h"
#include <algorithm>
#include <cmath>

using Entities::Prop;
using Entities::PropType;

namespace {
float frand() { return (float)GetRandomValue(0, 10000) / 10000.0f; }

// What a weapon crate can hold: the good stuff, never the basics.
const WeaponId CRATE_LOOT[] = {
    WeaponId::ClusterBomb, WeaponId::BananaBomb, WeaponId::HolyGrenade, WeaponId::Dynamite,
    WeaponId::Mine, WeaponId::Sheep, WeaponId::AirStrike, WeaponId::Teleport, WeaponId::BaseballBat,
};
} // namespace

void Game::dropCrate() {
    // Over land, somewhere in the middle of the map.
    for (int attempt = 0; attempt < 30; attempt++) {
        float x = 120 + frand() * (Terrain::WORLD_PX_W - 240);
        int col = (int)(x / Terrain::CELL);
        bool land = false;
        for (int row = 0; row < Terrain::ROWS - 2; row++) {
            if (terrain->chunks[row][col]->state == Terrain::ChunkState::SOLID) {
                land = row * Terrain::CELL < Terrain::WATER_Y - 40;
                break;
            }
        }
        if (!land) continue;
        Prop c;
        bool health = frand() < 0.5f;
        c.spawn(world, health ? PropType::HealthCrate : PropType::WeaponCrate, x, -30);
        c.contents = (int)CRATE_LOOT[GetRandomValue(0, (int)(sizeof(CRATE_LOOT) / sizeof(CRATE_LOOT[0])) - 1)];
        props.push_back(c);
        say(health ? "Health crate incoming!" : "Weapon crate incoming!", Color{255, 240, 180, 255}, 1.8f);
        return;
    }
}

void Game::updateProps(float dt) {
    for (size_t i = 0; i < props.size(); i++) {
        Prop& p = props[i];
        if (!p.alive || !p.body) continue;
        p.sync(world);

        // Sunk.
        if (p.pos.y > Terrain::WATER_Y + 10) {
            fx.splash(p.pos.x, Terrain::WATER_Y, 0.7f);
            Audio::playSound("splash", 0.5f, 1.1f);
            p.destroy(world);
            continue;
        }
        if (p.pos.x < -300 || p.pos.x > Terrain::WORLD_PX_W + 300) { p.destroy(world); continue; }

        if (p.parachute) {
            // Drift down under the canopy, pushed by the wind, until landing.
            Vector2 v = world->getBodyVelocity(p.body);
            if (v.y < -1.6f) world->setBodyVelocity(p.body, wind * 1.2f, -1.6f);
            b2Vec2 bp(p.pos.x / Terrain::PPM, Terrain::WORLD_H - p.pos.y / Terrain::PPM);
            World::RayCastResult hit;
            if (world->RayCast(bp, bp + b2Vec2(0, -0.5f), hit, static_cast<b2Body*>(p.body), World::CAT_TERRAIN)) {
                p.parachute = false;
                Audio::playSound("land");
            }
        }

        // Pick-ups: any worm that can act collects a crate by touching it.
        if (p.type == PropType::HealthCrate || p.type == PropType::WeaponCrate) {
            for (auto* w : worms) {
                if (!w->canAct()) continue;
                Vector2 wp = w->pixelPos();
                if (fabsf(wp.x - p.pos.x) < 20 && fabsf(wp.y - p.pos.y) < 24) {
                    Team& team = teams[w->teamIndex];
                    if (p.type == PropType::HealthCrate) {
                        w->hp += 25;
                        w->shownHp = w->hp;
                        fx.banner(wp.x, wp.y - 40, "+25", Color{120, 255, 120, 255}, 24, 1.6f);
                        say(w->name + " gets 25 health", team.color, 1.8f);
                    } else {
                        WeaponId id = (WeaponId)p.contents;
                        if (team.ammo[(int)id] >= 0) team.ammo[(int)id]++;
                        say(team.name + " get a " + weaponInfo(id).name + "!", team.color, 2.0f);
                    }
                    fx.sparkle(p.pos.x, p.pos.y, Color{255, 240, 140, 255}, 20);
                    Audio::playSound("crate");
                    p.destroy(world);
                    break;
                }
            }
            if (!p.alive) continue;
        }

        if (p.type == PropType::Mine) {
            if (p.armTimer > 0) p.armTimer -= dt;
            // Trips when a worm comes close, then beeps for a moment.
            if (p.armTimer <= 0 && p.fuse < 0) {
                for (auto* w : worms) {
                    if (!w->canAct()) continue;
                    Vector2 wp = w->pixelPos();
                    float dx = wp.x - p.pos.x, dy = wp.y - p.pos.y;
                    if (dx * dx + dy * dy < 38 * 38) {
                        p.fuse = 1.3f;
                        break;
                    }
                }
            }
            if (p.fuse >= 0) {
                float before = p.fuse;
                if (fmodf(before, 0.3f) < dt) Audio::playSound("beep", 0.7f);
            }
        }

        if (p.type == PropType::Barrel && p.hp < 20 && GetRandomValue(0, 6) == 0) {
            fx.smokePuff(p.pos.x + (frand() - 0.5f) * 8, p.pos.y - 12, 4);
        }

        if (p.fuse >= 0) {
            p.fuse -= dt;
            if (p.fuse < 0) {
                Vector2 at = p.pos;
                PropType type = p.type;
                p.destroy(world);
                switch (type) {
                    case PropType::Barrel:
                        explode(at.x, at.y, 58, 40);
                        for (int k = 0; k < 8; k++) fx.smokePuff(at.x + (frand() - 0.5f) * 40, at.y - frand() * 20, 10);
                        break;
                    case PropType::Mine:
                        explode(at.x, at.y, 48, 45);
                        break;
                    case PropType::WeaponCrate:
                        explode(at.x, at.y, 45, 30);
                        break;
                    default:
                        explode(at.x, at.y, 30, 10);
                        break;
                }
            }
        }
    }
    props.erase(std::remove_if(props.begin(), props.end(), [](const Prop& p) { return !p.alive; }),
                props.end());
}
