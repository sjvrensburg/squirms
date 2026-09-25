#include "game.h"
#include "../physics/world.h"
#include "../terrain/generate.h"
#include "../terrain/grid.h"
#include "../terrain/terrain.h"
#include "../core/rng.h"
#include "../audio/sfx.h"
#include "../render/renderer.h"
#include "../render/hud.h"
#include <algorithm>
#include <cmath>

using Entities::Worm;
using Entities::Projectile;
using Entities::ProjectileType;
using Entities::Prop;
using Entities::PropType;

namespace {

constexpr float TURN_SECONDS = 45.0f;
constexpr float RETREAT_SECONDS = 4.0f;
constexpr float INTRO_SECONDS = 1.3f;
constexpr float SETTLE_CAP_SECONDS = 12.0f;
constexpr float HP_COUNT_RATE = 40.0f;     // shown HP ticks down this fast (hp/s)
constexpr float AIM_SPEED = 1.5f;          // rad/s
constexpr float CHARGE_SECONDS = 1.5f;     // empty to full power

const char* TEAM_NAMES[4] = {"The Wrigglers", "Mud Monkeys", "Soil Rebels", "Worm Force"};
const Color TEAM_COLORS[4] = {
    {236, 64, 58, 255}, {58, 128, 246, 255}, {60, 196, 70, 255}, {246, 196, 36, 255},
};
const char* WORM_NAMES[4][8] = {
    {"Boggy B", "Spadge", "Clagnut", "Chuck", "Nobby", "Squelch", "Gonzo", "Tiddles"},
    {"Mudpie", "Grubby", "Sludge", "Pongo", "Dribbles", "Wormald", "Gunge", "Burble"},
    {"Rocky", "Digger", "Crumble", "Pebbles", "Loamy", "Tunnel", "Scoop", "Grit"},
    {"Major", "Sarge", "Private", "Ace", "Rambo", "Bomber", "Gunner", "Chief"},
};

float frand() { return (float)GetRandomValue(0, 10000) / 10000.0f; }

} // namespace

float Team::shownHp() const {
    float t = 0;
    for (auto* w : worms) if (w->alive && !w->drowned) t += std::max(0.0f, w->shownHp);
    return t;
}

bool Team::hasActiveWorms() const {
    for (auto* w : worms) if (w->canAct()) return true;
    return false;
}

Game::Game(const GameConfig& c) : cfg(c), theme(&getTheme(c.theme)) {
    world = new World(0, -10);
    auto gen = Terrain::generateTerrain(cfg.seed);
    auto grid = Terrain::buildGrid(gen.solid, gen.material, cfg.seed);
    terrain = new Terrain::TerrainSystem(world, grid.chunks);
    terrainGfx.build(*terrain, *theme, cfg.seed);

    int teamCount = std::clamp(cfg.teams, 2, 4);
    for (int t = 0; t < teamCount; t++) {
        Team team;
        team.name = TEAM_NAMES[t];
        team.color = TEAM_COLORS[t];
        team.cpu = cfg.vsCpu && t > 0;
        if (team.cpu) team.name += " (CPU)";
        for (int i = 0; i < WEAPON_COUNT; i++) team.ammo[i] = weaponInfo((WeaponId)i).ammo;
        teams.push_back(team);
    }
    spawnWorldObjects((unsigned)cfg.seed);

    currentTeam = (int)((unsigned)cfg.seed % teams.size()) - 1;
    camera.snapTo(Terrain::WORLD_PX_W / 2.0f, Terrain::WATER_Y - 300);
    startTurn();
}

Game::~Game() {
    terrainGfx.unload();
    for (auto* w : worms) delete w;
    // Bodies belong to the Box2D world, which goes with it.
    delete terrain;
    delete world;
}

// Spots where something can stand: the top of a solid chunk with open air
// above it (hilltops and cave floors alike), above the sea and not on a
// steep slope. Picked in a seeded shuffle, at least `minGap` px apart.
std::vector<Vector2> Game::surfaceSpots(float minGap, int count, unsigned seed,
                                        const std::vector<Vector2>& avoid) {
    using namespace Terrain;
    auto solidAt = [&](int col, int row) {
        if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return false;
        return terrain->chunks[row][col]->state == ChunkState::SOLID;
    };
    std::vector<Vector2> candidates;
    for (int col = 8; col < COLS - 8; col++) {
        for (int row = 5; row < ROWS; row++) {
            if (!solidAt(col, row)) continue;
            bool open = true;
            for (int k = 1; k <= 5 && open; k++) {
                if (solidAt(col, row - k) || solidAt(col - 1, row - k) || solidAt(col + 1, row - k)) open = false;
            }
            if (!open) continue;
            float y = (float)(row * CELL);
            if (y > WATER_Y - 50) continue;
            // Flat enough: neighbours two cells out are within two rows.
            bool flat = true;
            for (int dc : {-2, 2}) {
                bool any = false;
                for (int dr = -2; dr <= 2; dr++) if (solidAt(col + dc, row + dr) && !solidAt(col + dc, row + dr - 1)) any = true;
                if (!any) flat = false;
            }
            if (flat) candidates.push_back({(float)(col * CELL + CELL / 2), y});
        }
    }
    Core::Mulberry32 rng(seed * 7919u + 13u);
    for (int i = (int)candidates.size() - 1; i > 0; i--) {
        int j = (int)(rng() * (i + 1));
        std::swap(candidates[i], candidates[j]);
    }
    std::vector<Vector2> out;
    for (float gap = minGap; gap >= 20 && (int)out.size() < count; gap *= 0.7f) {
        for (auto& c : candidates) {
            if ((int)out.size() >= count) break;
            bool ok = true;
            for (auto& o : out) if (fabsf(o.x - c.x) + fabsf(o.y - c.y) * 0.5f < gap) ok = false;
            for (auto& a : avoid) if (fabsf(a.x - c.x) + fabsf(a.y - c.y) * 0.5f < gap * 0.8f) ok = false;
            if (ok) out.push_back(c);
        }
    }
    return out;
}

void Game::spawnWorldObjects(unsigned seed) {
    int perTeam = std::clamp(cfg.wormsPerTeam, 1, 8);
    int total = perTeam * (int)teams.size();
    std::vector<Vector2> spots = surfaceSpots(180, total, seed, {});
    for (int i = 0; i < total && i < (int)spots.size(); i++) {
        int t = i % (int)teams.size();
        int n = i / (int)teams.size();
        Worm* w = new Worm(WORM_NAMES[t][n % 8], t, (float)cfg.startHp);
        w->facing = spots[i].x < Terrain::WORLD_PX_W / 2 ? 1 : -1;
        w->init(world, spots[i].x, spots[i].y - 2);
        teams[t].worms.push_back(w);
        worms.push_back(w);
    }

    Core::Mulberry32 rng(seed + 99u);
    int barrels = 3 + (int)(rng() * 4);
    int mines = 4 + (int)(rng() * 5);
    std::vector<Vector2> taken = spots;
    auto objSpots = surfaceSpots(110, barrels + mines, seed + 1u, taken);
    for (int i = 0; i < (int)objSpots.size(); i++) {
        Prop p;
        if (i < barrels) p.spawn(world, PropType::Barrel, objSpots[i].x, objSpots[i].y - 12);
        else p.spawn(world, PropType::Mine, objSpots[i].x, objSpots[i].y - 6);
        props.push_back(p);
    }
}

// ---------------------------------------------------------------------------
// Turn flow

void Game::say(const std::string& text, Color color, float seconds) {
    message = text;
    messageColor = color;
    messageTime = seconds;
}

void Game::enterPhase(Phase p) {
    phase = p;
    phaseTime = 0;
}

void Game::startTurn() {
    // Next team with anyone left standing.
    int n = (int)teams.size();
    for (int k = 0; k < n; k++) {
        currentTeam = (currentTeam + 1 + n) % n;
        if (teams[currentTeam].hasActiveWorms()) break;
    }
    Team& team = teams[currentTeam];
    current = nullptr;
    for (size_t k = 0; k < team.worms.size(); k++) {
        int idx = (team.nextWorm + (int)k) % (int)team.worms.size();
        if (team.worms[idx]->canAct()) {
            current = team.worms[idx];
            team.nextWorm = idx + 1;
            break;
        }
    }
    for (auto* w : worms) w->damageThisTurn = 0;

    turnNumber++;
    wind = (frand() * 2.0f - 1.0f);
    if (fabsf(wind) < 0.05f) wind = 0;
    // Remember the team's last weapon (Worms-style), unless it's used up or
    // was Skip Go, which nobody wants pre-selected.
    if (team.ammo[(int)team.weapon] == 0 || team.weapon == WeaponId::SkipGo) team.weapon = WeaponId::Bazooka;
    weapon = team.weapon;
    fuse = 3;
    power = 0;
    charging = false;
    shotsLeft = weaponInfo(weapon).shots;
    fired = false;
    panelOpen = false;
    turnTime = TURN_SECONDS;
    tickSoundAt = 5.0f;
    aiStage = 0;
    aiTimer = 0;
    camera.manual = false;
    blasts.clear();

    // Supply drops from the second round on.
    if (turnNumber > (int)teams.size() && frand() < 0.4f) dropCrate();

    if (current) say(current->name + " - " + team.name, team.color, 2.2f);
    Audio::playSound("turn", 0.8f);
    enterPhase(Phase::Intro);
}

void Game::endTurn() {
    releaseRope();
    quietTime = 0;
    charging = false;
    power = 0;
    panelOpen = false;
    settleCap = SETTLE_CAP_SECONDS;
    enterPhase(Phase::Settling);
}

bool Game::worldIsQuiet() const {
    if (!projectiles.empty() || !blasts.empty()) return false;
    if (terrain->getDynamicCount() > 0) return false;
    for (auto* w : worms) if (!w->isResting()) return false;
    for (auto& p : props) {
        if (!p.alive) continue;
        if (p.fuse >= 0) return false;
        if (!p.isResting(world)) return false;
    }
    return true;
}

void Game::checkGameOver() {
    int alive = 0, last = -1;
    for (size_t t = 0; t < teams.size(); t++) {
        if (teams[t].hasActiveWorms()) { alive++; last = (int)t; }
    }
    if (alive <= 1) {
        winner = alive == 1 ? last : -1;
        enterPhase(Phase::GameOver);
        if (winner >= 0) say(teams[winner].name + " win!", teams[winner].color, 1e9f);
        else say("It's a draw!", WHITE, 1e9f);
        Audio::playSound("victory");
    }
}

void Game::updatePhase(float dt) {
    // Health counters tick down once the dust settles, like Worms.
    if (phase == Phase::Settling || phase == Phase::Deaths || phase == Phase::GameOver) {
        for (auto* w : worms) {
            if (w->shownHp > w->hp) w->shownHp = std::max(w->hp, w->shownHp - HP_COUNT_RATE * dt);
            else if (w->shownHp < w->hp) w->shownHp = w->hp;
        }
    }

    auto sheepActive = [&]() {
        for (auto& p : projectiles) if (p.alive && p.type == ProjectileType::Sheep) return true;
        return false;
    };
    bool hurt = current && (current->damageThisTurn > 0 || !current->canAct());

    switch (phase) {
        case Phase::Intro:
            if (phaseTime >= INTRO_SECONDS) enterPhase(Phase::Aiming);
            break;

        case Phase::Aiming:
            turnTime -= dt;
            if (turnTime <= tickSoundAt && tickSoundAt > 0) {
                Audio::playSound("tick");
                tickSoundAt -= 1.0f;
            }
            if (turnTime <= 0) {
                turnTime = 0;
                for (auto& p : projectiles) if (p.type == ProjectileType::Sheep) p.detonateNow = true;
                if (!fired && current) say("Out of time!", WHITE, 1.5f);
                endTurn();
            } else if (hurt) {
                endTurn();
            } else if (fired && shotsLeft <= 0 && !sheepActive()) {
                retreatTime = RETREAT_SECONDS;
                enterPhase(Phase::Retreat);
            }
            break;

        case Phase::Retreat:
            retreatTime -= dt;
            if (retreatTime <= 0 || hurt) endTurn();
            break;

        case Phase::Settling: {
            settleCap -= dt;
            bool countersDone = true;
            for (auto* w : worms) if (w->shownHp != w->hp) countersDone = false;
            // Hold a beat once everything is still, so the aftermath can be
            // seen before the camera whisks off to the next worm.
            quietTime = (worldIsQuiet() && countersDone) ? quietTime + dt : 0;
            if (quietTime > 1.0f || settleCap <= 0) {
                for (auto* w : worms) w->shownHp = w->hp;
                bool anyDying = false;
                for (auto* w : worms) if (w->alive && w->dying) anyDying = true;
                if (anyDying) {
                    deathTimer = 0.7f;
                    enterPhase(Phase::Deaths);
                } else {
                    checkGameOver();
                    if (phase != Phase::GameOver) startTurn();
                }
            }
            break;
        }

        case Phase::Deaths: {
            Worm* next = nullptr;
            for (auto* w : worms) if (w->alive && w->dying) { next = w; break; }
            if (!next) {
                settleCap = SETTLE_CAP_SECONDS;
                quietTime = 0;
                enterPhase(Phase::Settling);
                break;
            }
            camera.follow(next->pixelPos(), 4.0f);
            deathTimer -= dt;
            if (deathTimer <= 0) {
                Vector2 p = next->pixelPos();
                next->pop();
                Audio::playSound("die");
                explode(p.x, p.y, 30, 20);
                Prop grave;
                grave.contents = next->teamIndex;
                grave.spawn(world, PropType::Grave, p.x, p.y - 4);
                props.push_back(grave);
                deathTimer = 1.0f;
            }
            break;
        }

        case Phase::GameOver:
            fireworksTimer -= dt;
            if (fireworksTimer <= 0) {
                fireworksTimer = 0.35f + frand() * 0.4f;
                Vector2 c = camera.screenToWorld({GetScreenWidth() * (0.15f + frand() * 0.7f),
                                                  GetScreenHeight() * (0.1f + frand() * 0.4f)});
                Color col = winner >= 0 ? teams[winner].color : Color{255, 255, 255, 255};
                fx.sparkle(c.x, c.y, col, 30);
                fx.sparkle(c.x, c.y, WHITE, 12);
                Audio::playSound("explode_small", 0.35f, 1.4f + frand() * 0.4f);
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Input

bool Game::isTargeting() const {
    return (phase == Phase::Aiming) && !fired && !panelOpen &&
           weaponInfo(weapon).mode == FireMode::Targeted && current && current->canAct();
}

Vector2 Game::aimDir(const Worm* w) const {
    return Vector2{w->facing * cosf(w->aim), -sinf(w->aim)};
}

void Game::selectWeapon(WeaponId id) {
    Team& team = teams[currentTeam];
    if (team.ammo[(int)id] == 0) {
        Audio::playSound("select", 0.5f, 0.6f);
        return;
    }
    weapon = id;
    team.weapon = id;
    shotsLeft = weaponInfo(id).shots;
    charging = false;
    power = 0;
    panelOpen = false;
    Audio::playSound("select");
    say(weaponInfo(id).name, WHITE, 1.2f);
}

// One-shot input (key presses, clicks, toggles) is read once per rendered
// frame. raylib latches IsKeyPressed for a whole frame, and a frame can run
// zero or several fixed ticks: reading presses per tick would drop them on
// fast displays and double them on slow ones (e.g. one Space both releasing
// and detonating the sheep, one Tab opening and closing the panel).
void Game::frameInput() {
    if (IsKeyPressed(KEY_F1)) debug = !debug;
    if (IsKeyPressed(KEY_H)) helpOpen = !helpOpen;

    if (phase == Phase::GameOver) {
        if (phaseTime > 1.5f && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
                                 IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
            exitRequested = true;
    }

    if (confirmQuit) {
        if (IsKeyPressed(KEY_Y) || IsKeyPressed(KEY_ENTER)) exitRequested = true;
        if (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE)) confirmQuit = false;
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (panelOpen) panelOpen = false;
        else if (helpOpen) helpOpen = false;
        else if (phase != Phase::GameOver) confirmQuit = true;
        return;
    }

    // Camera: wheel zoom, drag to look around.
    float wheel = GetMouseWheelMove();
    if (wheel != 0) camera.zoomBy(wheel > 0 ? 1.12f : 1.0f / 1.12f);
    Vector2 mouse = GetMousePosition();
    mouseWorld = camera.screenToWorld(mouse);
    bool leftDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        dragging = !panelOpen;
        dragDist = 0;
        lastMouse = mouse;  // measure the drag from where the button went down
    }
    if (dragging && leftDown) {
        float dx = mouse.x - lastMouse.x, dy = mouse.y - lastMouse.y;
        dragDist += fabsf(dx) + fabsf(dy);
        if (dragDist > 6) camera.pan(dx, dy);
    }
    bool clicked = false;
    if (dragging && !leftDown) {
        dragging = false;
        if (dragDist <= 6) clicked = true;
    }
    lastMouse = mouse;

    // Debug overlay sandbox: click anywhere to blow a hole (handy for
    // poking at terrain collapse).
    if (debug && clicked && !isTargeting() && !panelOpen) {
        explode(mouseWorld.x, mouseWorld.y, 50, 30);
        clicked = false;
    }

    if (cpuTurn() && phase != Phase::GameOver) { panelOpen = false; return; }

    bool myTurn = (phase == Phase::Aiming || phase == Phase::Retreat) && current && current->canAct();
    Team& team = teams[currentTeam];

    // Weapon panel.
    if ((IsKeyPressed(KEY_TAB) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) && phase != Phase::GameOver) {
        panelOpen = !panelOpen;
        if (panelOpen) Audio::playSound("menu");
    }
    if (panelOpen) {
        int hover = weaponPanelHit(mouse);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover >= 0) {
            if (phase == Phase::Aiming && !fired) selectWeapon((WeaponId)hover);
            else panelOpen = false;
        }
        return;
    }

    if (!myTurn) return;

    bool sheepOut = false;
    for (auto& p : projectiles) if (p.alive && p.type == ProjectileType::Sheep) sheepOut = true;
    if (sheepOut) {
        // Remote detonation.
        if (IsKeyPressed(KEY_SPACE)) {
            for (auto& p : projectiles) if (p.type == ProjectileType::Sheep) p.detonateNow = true;
        }
        return;
    }

    bool roped = rope.state == NinjaRope::State::Attached;
    if (roped) {
        // Let go: Enter always, Space when the rope is what's in hand.
        if (IsKeyPressed(KEY_ENTER) || (IsKeyPressed(KEY_SPACE) && weapon == WeaponId::NinjaRope)) {
            releaseRope();
            return;
        }
    } else if (!charging) {
        if (IsKeyPressed(KEY_ENTER)) { current->jump(); camera.manual = false; }
        if (IsKeyPressed(KEY_BACKSPACE)) { current->backflip(); camera.manual = false; }
    }

    if (phase != Phase::Aiming || shotsLeft <= 0) return;
    const WeaponInfo& info = weaponInfo(weapon);
    if (info.fused) {
        for (int k = 1; k <= 5; k++) {
            if (IsKeyPressed(KEY_ZERO + k)) {
                fuse = k;
                say(TextFormat("Fuse: %d sec", k), WHITE, 1.0f);
                Audio::playSound("select", 0.6f, 0.8f + k * 0.1f);
            }
        }
    }
    if (team.ammo[(int)weapon] == 0) return;

    switch (info.mode) {
        case FireMode::Rope:
            if (IsKeyPressed(KEY_SPACE) && rope.state == NinjaRope::State::Idle) shootRope();
            break;
        case FireMode::Charged:
            if (IsKeyPressed(KEY_SPACE) && !charging && (current->grounded || roped)) {
                charging = true;
                power = 0;
                Audio::playSound("charge", 0.6f);
            }
            break;
        case FireMode::Instant:
        case FireMode::Drop:
        case FireMode::Skip:
            if (IsKeyPressed(KEY_SPACE)) fire();
            break;
        case FireMode::Targeted:
            if (clicked) fireAt(mouseWorld);
            break;
    }
}

// Held input (walking, aiming, winding up a shot), applied every tick.
void Game::tickInput(float dt) {
    if (cpuTurn()) { updateCpu(dt); return; }
    if (confirmQuit || panelOpen) return;
    bool myTurn = (phase == Phase::Aiming || phase == Phase::Retreat) && current && current->canAct();
    if (!myTurn) { charging = false; return; }
    for (auto& p : projectiles) if (p.alive && p.type == ProjectileType::Sheep) return;

    bool roped = rope.state == NinjaRope::State::Attached;
    if (roped) {
        ropeControls(dt);
    } else if (!charging) {
        int dir = 0;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) dir -= 1;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) dir += 1;
        if (dir != 0) {
            current->walk(dir, dt);
            camera.manual = false;
        }
    }

    if (phase != Phase::Aiming || shotsLeft <= 0) { charging = false; return; }
    const WeaponInfo& info = weaponInfo(weapon);
    if (info.aims && !roped) {
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) current->aim += AIM_SPEED * dt;
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) current->aim -= AIM_SPEED * dt;
        current->aim = std::clamp(current->aim, -1.5f, 1.5f);
    }
    if (charging) {
        power = std::min(1.0f, power + dt / CHARGE_SECONDS);
        if (!IsKeyDown(KEY_SPACE) || power >= 1.0f) {
            fire();
            charging = false;
        }
    }
}

void Game::handleCamera(float dt) {
    (void)dt;
    // Most recent live shell takes the camera.
    for (int i = (int)projectiles.size() - 1; i >= 0; i--) {
        if (projectiles[i].alive) {
            camera.manual = false;
            camera.follow(projectiles[i].pos, 6.0f);
            return;
        }
    }
    if (phase == Phase::Deaths) return;  // updatePhase aims at the dying worm
    if (phase == Phase::GameOver) {
        if (winner >= 0) {
            for (auto* w : teams[winner].worms) if (w->canAct()) { camera.follow(w->pixelPos(), 2.0f); break; }
        }
        return;
    }
    if (phase == Phase::Intro) {
        // A crate on its way down steals the intro shot.
        for (auto& p : props) {
            if (p.alive && p.parachute) { camera.follow(p.pos, 3.0f); return; }
        }
    }
    if (phase == Phase::Settling) {
        for (auto* w : worms) {
            if (w->alive && (w->flying || w->drowned)) { camera.follow(w->pixelPos(), 4.0f); return; }
        }
    }
    if (current && (current->alive)) camera.follow(current->pixelPos(), phase == Phase::Intro ? 3.0f : 5.0f);
}

// ---------------------------------------------------------------------------
// Tick

void Game::update(float dt) {
    matchTime += dt;
    phaseTime += dt;
    if (messageTime > 0) messageTime -= dt;
    tickInput(dt);

    world->step(dt, 8, 3);
    updateProjectiles(dt);
    terrain->update(dt);
    terrainAftermath();
    for (auto& s : terrain->sunk) {
        fx.splash(s.first, s.second, 1.2f);
        Audio::playSound("splash", 0.7f, 0.8f);
    }
    terrain->sunk.clear();

    for (auto* w : worms) {
        bool wasDrowned = w->drowned;
        w->update(dt);
        if (w->drowned && !wasDrowned) {
            Vector2 p = w->pixelPos();
            fx.splash(p.x, Terrain::WATER_Y, 1.0f);
            if (w->alive) fx.banner(p.x, Terrain::WATER_Y - 40, "Glug!", teams[w->teamIndex].color, 22, 1.6f);
        }
        if (w->drowned && w->alive && GetRandomValue(0, 5) == 0) {
            Vector2 p = w->pixelPos();
            fx.bubbles(p.x, p.y, 1);
        }
        if (w->freshDamage >= 0.5f) {
            Vector2 p = w->pixelPos();
            fx.damageNumber(p.x, p.y - 40, (int)std::round(w->freshDamage), teams[w->teamIndex].color);
            w->freshDamage = 0;
        }
    }

    updateRope(dt);
    updateProps(dt);

    for (auto& b : blasts) b.delay -= dt;
    std::vector<PendingBlast> due;
    for (auto& b : blasts) if (b.delay <= 0) due.push_back(b);
    blasts.erase(std::remove_if(blasts.begin(), blasts.end(), [](const PendingBlast& b) { return b.delay <= 0; }),
                 blasts.end());
    for (auto& b : due) explode(b.x, b.y, b.radius, b.damage);

    terrainGfx.sync(*terrain);
    fx.update(dt, wind);
    updatePhase(dt);

    if (planeX > -1e8f) {
        planeX += planeDir * 700.0f * dt;
        if (planeX < -600 || planeX > Terrain::WORLD_PX_W + 600) planeX = -1e9f;
    }

    handleCamera(dt);
    camera.update(dt, fx.shake);
}

void Game::draw() {
    renderWorld(*this);
    renderHUD(*this);
}
