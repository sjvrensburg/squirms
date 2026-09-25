#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Core includes
#include "core/math.h"
#include "core/rng.h"
#include "core/input.h"

// Terrain includes
#include "terrain/generate.h"
#include "terrain/grid.h"
#include "terrain/terrain.h"
#include "terrain/support.h"

// Physics includes
#include "physics/world.h"

// Entity includes
#include "entities/worm.h"
#include "entities/explosion.h"
#include "entities/projectile.h"

// Weapon includes
#include "weapons/index.h"
#include "weapons/ninjarope.h"

// Game includes
#include "game/camera.h"
#include "game/turn.h"

// Render includes
#include "render/renderer.h"
#include "render/hud.h"

// Audio includes
#include "audio/sfx.h"

// Constants
constexpr int TEAM_COLORS_COUNT = 4;
const char* TEAM_COLORS[] = {"#ff6b6b", "#4ecdc4", "#ffe66d", "#a8e6cf"};

struct GameState {
    World* world = nullptr;
    Terrain::TerrainSystem* terrain = nullptr;
    std::vector<std::vector<Entities::Worm*>> worms;
    std::vector<Entities::Explosion> explosions;
    std::vector<Entities::Projectile> projectiles;
    TurnSystem turn = TurnSystem({}, 0);
    GameCamera camera;
    std::string weapon = "bazooka";
    float chargeLevel = 0.0f;
    bool charging = false;
    NinjaRope* ninjaRope = nullptr;
    int fps = 60;
    int frameCount = 0;
    double lastFpsTime = 0;

    int getTeamCount() const { return (int)worms.size(); }
    Entities::Worm* getWorm(int team, int idx) const {
        if (team < 0 || team >= (int)worms.size() || idx < 0 || idx >= (int)worms[team].size()) return nullptr;
        return worms[team][idx];
    }
    const std::vector<std::vector<Entities::Worm*>>& getAllWorms() const { return worms; }
    const std::vector<Entities::Explosion>& getExplosions() const { return explosions; }
    Terrain::TerrainSystem* getTerrain() const { return terrain; }
};

static GameState* state = nullptr;
static bool debugMode = false;

// Menu state (replaces the old HTML start-screen form)
static int menuTeamCount = 2;
static int menuWormsPerTeam = 2;
static char menuSeedText[16] = "";
static int menuSeedLen = 0;

void initGame(int teamCount, int wormsPerTeam, int seed) {
    // Create physics world
    state->world = new World(0, -10);
    
    // Generate terrain
    auto result = Terrain::generateTerrain(seed);
    auto gridData = Terrain::buildGrid(result.solid, result.material, seed);
    
    // Create terrain system
    state->terrain = new Terrain::TerrainSystem(state->world, gridData.chunks);
    
    // Initialize worms
    Core::Mulberry32 rng((unsigned int)seed);
    int totalWorms = teamCount * wormsPerTeam;
    
    for (int t = 0; t < teamCount; t++) {
        std::vector<Entities::Worm*> teamWorms;
        for (int w = 0; w < wormsPerTeam; w++) {
            float x = 0, y = 0;
            int wormGlobalIndex = t * wormsPerTeam + w;
            int colStep = std::max(1, (Terrain::COLS - 10) / totalWorms);
            int targetCol = 5 + wormGlobalIndex * colStep + (int)(rng() * colStep);
            
            // Find ground position (place worm on top of terrain). Check the
            // chunk's actual post-TerrainSystem state, not the pre-physics
            // `result.solid` heightfield: TerrainSystem's constructor already
            // collapsed any unsupported clumps to DYNAMIC/GONE by this point,
            // and spawning on top of one of those drops the worm into freefall.
            for (int col = targetCol; col < Terrain::COLS - 5 && col >= 5; col++) {
                for (int row = 0; row < Terrain::ROWS; row++) {
                    Terrain::Chunk* c = gridData.chunks[row][col];
                    if (c && c->state == Terrain::ChunkState::SOLID) {
                        x = col * Terrain::CELL + Terrain::CELL / 2.0f + ((float)rng() - 0.5f) * 10.0f;
                        y = (row - 1) * Terrain::CELL;  // Place on top of terrain
                        goto foundPosition;
                    }
                }
            }
            foundPosition:;

            // Adjust spawn if chunk was thawed to dynamic or destroyed
            int spawnCol = (int)(x / Terrain::CELL);
            int spawnRow = (int)(y / Terrain::CELL);
            if (spawnCol >= 0 && spawnCol < Terrain::COLS && spawnRow >= 0 && spawnRow < (int)gridData.chunks.size()) {
                Terrain::Chunk* spawnChunk = gridData.chunks[spawnRow][spawnCol];
                if (spawnChunk->state == Terrain::ChunkState::DYNAMIC) {
                    y -= (float)Terrain::CELL * 2.0f;
                } else if (spawnChunk->state == Terrain::ChunkState::GONE) {
                    for (int searchRow = spawnRow - 1; searchRow >= 0; searchRow--) {
                        if (gridData.chunks[searchRow][spawnCol]->state != Terrain::ChunkState::GONE) {
                            y = (float)searchRow * (float)Terrain::CELL;
                            break;
                        }
                    }
                }
            }
            
            Entities::Worm* worm = new Entities::Worm(x, y, t, TEAM_COLORS[t]);
            worm->init(state->world, x / Terrain::PPM, y / Terrain::PPM);
            teamWorms.push_back(worm);
        }
        state->worms.push_back(teamWorms);
    }
    
    // Initialize turn system
    state->turn = TurnSystem(state->worms, teamCount);
    state->camera.setFollow(state->turn.getCurrentWorm());
}

void fireWeapon();
std::vector<Entities::Worm*> flattenWorms();

void update(float dt) {
    if (!state) return;
    
    // FPS counter
    state->frameCount++;
    double now = (double)GetTime();
    if (now - state->lastFpsTime >= 1.0) {
        state->fps = state->frameCount;
        state->frameCount = 0;
        state->lastFpsTime = now;
    }
    
    // Update input from raylib
    Input::updateInput();
    
    Entities::Worm* currentWorm = state->turn.getCurrentWorm();
    if (currentWorm && currentWorm->isAlive()) {
        state->camera.setFollow(currentWorm);
    }
    
    // Handle input. Movement is only accepted while it's the current worm's
    // turn (AIMING or the post-fire RETREAT window); while the world settles
    // or the match is over, movement and firing input is ignored.
    if (state->turn.canMove()) {
        if (Input::isKey("w") || Input::isKey("arrowup")) {
            if (state->ninjaRope) state->ninjaRope->retract(50.0f * dt);
        }
        if (Input::isKey("s") || Input::isKey("arrowdown")) {
            if (state->ninjaRope) state->ninjaRope->extend(50.0f * dt);
        }

        if (currentWorm) {
            if (Input::isKey("a") || Input::isKey("arrowleft")) {
                currentWorm->walk(-1, dt);
            }
            if (Input::isKey("d") || Input::isKey("arrowright")) {
                currentWorm->walk(1, dt);
            }
            if (Input::isKey(" ") || Input::isKey("enter")) {
                currentWorm->jump();
            }
            if (Input::isKey("backspace")) {
                currentWorm->backflip();
            }
        }
    }
    
    // Weapon selection
    if (Input::isKey("1")) state->weapon = "bazooka";
    if (Input::isKey("2")) state->weapon = "grenade";
    if (Input::isKey("3") && !state->ninjaRope && currentWorm) {
        Vector2 mouseWorld = state->camera.screenToWorld((Vector2){(float)Input::getMouseScreenX(), (float)Input::getMouseScreenY()});
        Vector2 wormPos = currentWorm->getPosition();
        float angle = atan2f(mouseWorld.y - wormPos.y * Terrain::PPM, mouseWorld.x - wormPos.x * Terrain::PPM);
        state->ninjaRope = new NinjaRope(currentWorm, state->world);
        state->ninjaRope->fire(angle);
        Audio::playSound("rope");
    }
    if (Input::isKey("escape") && state->ninjaRope) {
        delete state->ninjaRope;
        state->ninjaRope = nullptr;
    }
    
    // Charging weapon
    if (Input::isKey("q")) {
        if (!state->charging) {
            state->charging = true;
            state->chargeLevel = 0.0f;
        }
    }
    if (state->charging && !Input::isKey("q")) {
        // One shot per turn: only fire during AIMING and before the current
        // worm has already fired. noteFired() ends the firing window for this
        // turn (the worm may still move during the retreat window).
        if (state->turn.canFire()) {
            fireWeapon();
            state->turn.noteFired();
        }
        state->charging = false;
    }
    
    if (state->charging && state->chargeLevel < 1.0f) {
        state->chargeLevel += dt / 1.2f;
    }
    
    // Update physics world
    state->world->step(dt, 8, 3);
    
    // Update projectiles: fly, react to wind/bounces, and detonate when due.
    // Any resulting explosions are collected and appended to the render list.
    std::vector<Entities::Explosion> projExplosions;
    for (auto& p : state->projectiles) {
        p.update(state->world, dt, state->worms, &projExplosions);
    }
    for (auto& e : projExplosions) {
        state->explosions.push_back(e);
    }
    // Dead shells' Box2D bodies are owned by World, so free them before the
    // erase below drops the Projectile objects (avoids leaking b2Bodies).
    for (auto& p : state->projectiles) {
        if (!p.isAlive() && p.body) {
            state->world->destroyBody(p.body);
            p.body = nullptr;
        }
    }
    state->projectiles.erase(
        std::remove_if(state->projectiles.begin(), state->projectiles.end(),
            [](const Entities::Projectile& p) { return !p.isAlive(); }),
        state->projectiles.end());
    
    // Update terrain
    state->terrain->update(dt);
    
    // Update worms
    for (auto& team : state->worms) {
        for (auto* worm : team) {
            worm->update(dt);
        }
    }
    
    // Update explosions
    for (auto& exp : state->explosions) {
        exp.update(dt);
    }
    state->explosions.erase(
        std::remove_if(state->explosions.begin(), state->explosions.end(),
            [](const Entities::Explosion& e) { return !e.isAlive(); }),
        state->explosions.end());
    
    // Check game over before advancing the turn: if at most one team now has a
    // living worm the match ends (a draw if none are left). Done first so a
    // finished match never advances to the next turn.
    int winningTeam = -1;
    if (state->turn.evaluateResult(&winningTeam) != MatchResult::IN_PROGRESS) {
        state->turn.state = TurnState::GAME_OVER;
    }

    // Is the world quiet enough to hand off to the next team? No shells in
    // flight, no active explosions, and no terrain slabs still falling.
    bool worldSettled = state->projectiles.empty()
                        && state->explosions.empty()
                        && state->terrain->getDynamicCount() == 0;

    // Update turn system
    state->turn.update(dt, worldSettled);

    // Update camera
    state->camera.update(dt);
    
    // Debug mode - click to explode
    if (debugMode && Input::wasClicked()) {
        Vector2 mouse = {(float)Input::getMouseScreenX(), (float)Input::getMouseScreenY()};
        Vector2 worldPos = state->camera.screenToWorld(mouse);
        createExplosion(state->world, worldPos.x * Terrain::PPM, worldPos.y * Terrain::PPM, 2.0f, 50.0f, 
                       state->terrain, state->worms);
    }
}

void fireWeapon() {
    if (!state || !state->turn.getCurrentWorm()) return;
    
    const Entities::Worm* worm = state->turn.getCurrentWorm();
    Vector2 pos = worm->getPosition();
    Vector2 mouseWorld = state->camera.screenToWorld((Vector2){(float)Input::getMouseScreenX(), (float)Input::getMouseScreenY()});
    float angle = atan2f(mouseWorld.y - pos.y * Terrain::PPM, mouseWorld.x - pos.x * Terrain::PPM);

    // The turn rolls its wind once at the start of the turn and holds it, so
    // this snapshot is steady across the shell's whole flight and matches what
    // the HUD shows for the rest of the turn.
    float wind = state->turn.getWind();
    
    if (state->weapon == "bazooka") {
        Entities::Projectile p = weapons.bazooka.fire(worm, angle, state->chargeLevel,
                                                       state->world, state->terrain, wind);
        if (p.isAlive() && p.body) state->projectiles.push_back(std::move(p));
    } else if (state->weapon == "grenade") {
        Entities::Projectile p = weapons.grenade.fire(worm, angle, state->chargeLevel,
                                                       state->world, state->terrain, wind);
        if (p.isAlive() && p.body) state->projectiles.push_back(std::move(p));
    }
    
    Audio::playSound("fire");
}

// Draw the match-result overlay shown while the simulation keeps running after
// the game ends: names the winning team in its colour, or says it's a draw.
static void drawGameResult() {
    int winner = -1;
    if (state->turn.evaluateResult(&winner) != MatchResult::IN_PROGRESS) {
        const char* text;
        Color col = WHITE;
        char buf[128];  // declared before the branch so it outlives the DrawText calls below
        if (winner < 0) {
            // No worms left anywhere: it's a draw.
            text = "It's a draw!";
        } else {
            snprintf(buf, sizeof(buf), "Team %d wins!", winner + 1);
            text = buf;
            // TEAM_COLORS are "#rrggbb" strings; parse them into a Color.
            const char* hex = TEAM_COLORS[winner % TEAM_COLORS_COUNT];
            col = (Color){ (unsigned char)strtol(hex + 1, nullptr, 16),
                           (unsigned char)strtol(hex + 3, nullptr, 16),
                           (unsigned char)strtol(hex + 5, nullptr, 16),
                           255 };
        }
        int fontSize = 48;
        int x = GetScreenWidth() / 2 - MeasureText(text, fontSize) / 2;
        int y = GetScreenHeight() / 2 - fontSize / 2;
        DrawText(text, x, y, fontSize, col);

        const char* hint = "Press Enter to return to the menu";
        DrawText(hint,
                 GetScreenWidth() / 2 - MeasureText(hint, 20) / 2,
                 y + fontSize + 30, 20, LIGHTGRAY);
    }
}

void renderFrame(float dt) {
    if (!state) return;
    
    // Clear background
    ClearBackground(DARKGRAY);
    
    // Render terrain and game objects
    render(state->camera, 
           state->terrain ? state->terrain->chunks : std::vector<std::vector<Terrain::Chunk*>>{},
           flattenWorms(),
           state->explosions,
           state->projectiles,
           GetScreenWidth(), GetScreenHeight());
    
    // Render HUD
    renderHUD({
        .worms = flattenWorms(),
        .currentTeamIndex = state->turn.currentTeamIndex,
        .timer = state->turn.timer,
        .wind = state->turn.getWind(),
        .weapon = state->weapon,
        .chargeLevel = state->chargeLevel,
        .debugMode = debugMode,
        .fps = state->fps,
        .staticBodies = state->terrain ? state->terrain->getStaticCount() : 0,
        .dynamicBodies = state->terrain ? state->terrain->getDynamicCount() : 0
    });

    // While it's not the player's move (settling, or the match is over) the
    // simulation still renders; show the result overlay once it's over.
    if (state->turn.state == TurnState::GAME_OVER) {
        drawGameResult();
    }
}

std::vector<Entities::Worm*> flattenWorms() {
    std::vector<Entities::Worm*> result;
    for (auto& team : state->worms) {
        for (auto* worm : team) {
            result.push_back(worm);
        }
    }
    return result;
}

// ---- Start menu (in-engine replacement for the old HTML start form) ----

static void startGame() {
    int seed = menuSeedLen > 0 ? atoi(menuSeedText) : GetRandomValue(0, 999999);
    state = new GameState();
    initGame(menuTeamCount, menuWormsPerTeam, seed);
}

static void updateMenu() {
    if (IsKeyPressed(KEY_LEFT))  menuTeamCount = std::max(2, menuTeamCount - 1);
    if (IsKeyPressed(KEY_RIGHT)) menuTeamCount = std::min(4, menuTeamCount + 1);
    if (IsKeyPressed(KEY_UP))    menuWormsPerTeam = std::min(6, menuWormsPerTeam + 1);
    if (IsKeyPressed(KEY_DOWN))  menuWormsPerTeam = std::max(1, menuWormsPerTeam - 1);

    for (int ch = GetCharPressed(); ch != 0; ch = GetCharPressed()) {
        if (ch >= '0' && ch <= '9' && menuSeedLen < (int)sizeof(menuSeedText) - 1) {
            menuSeedText[menuSeedLen++] = (char)ch;
            menuSeedText[menuSeedLen] = '\0';
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && menuSeedLen > 0) {
        menuSeedText[--menuSeedLen] = '\0';
    }

    if (IsKeyPressed(KEY_F1)) debugMode = !debugMode;

    if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        startGame();
    }
}

static void drawMenu() {
    ClearBackground((Color){26, 26, 46, 255});
    const Color teal = (Color){127, 219, 202, 255};

    const char* title = "SQUIRMS";
    DrawText(title, GetScreenWidth() / 2 - MeasureText(title, 40) / 2, 140, 40, teal);
    const char* subtitle = "A physics-terrain Worms-like";
    DrawText(subtitle, GetScreenWidth() / 2 - MeasureText(subtitle, 16) / 2, 190, 16, GRAY);

    char line[128];
    snprintf(line, sizeof(line), "Teams: %d   (Left/Right)", menuTeamCount);
    DrawText(line, GetScreenWidth() / 2 - 130, 260, 20, LIGHTGRAY);
    snprintf(line, sizeof(line), "Worms/team: %d   (Up/Down)", menuWormsPerTeam);
    DrawText(line, GetScreenWidth() / 2 - 130, 290, 20, LIGHTGRAY);
    snprintf(line, sizeof(line), "Seed: %s   (type digits)", menuSeedLen > 0 ? menuSeedText : "random");
    DrawText(line, GetScreenWidth() / 2 - 130, 320, 20, LIGHTGRAY);

    const char* start = "Press ENTER or click to start";
    DrawText(start, GetScreenWidth() / 2 - MeasureText(start, 20) / 2, 380, 20, teal);
    DrawText("F1: toggle debug overlay", 20, GetScreenHeight() - 30, 14, DARKGRAY);
}

// ---- Main loop: one frame, driven by raylib natively or by the browser's
// requestAnimationFrame via emscripten_set_main_loop under Emscripten. ----

// Fixed-timestep accumulator: requestAnimationFrame's rate is neither
// guaranteed to be 60Hz (high-refresh-rate displays, unthrottled headless
// browsers) nor perfectly steady, but world->step() and the gameplay code
// assume a fixed 1/60s tick. Decoupling simulation from render rate here
// keeps physics/camera/timers correct regardless of how often frame() runs.
constexpr double TICK = 1.0 / 60.0;
constexpr int MAX_STEPS_PER_FRAME = 5;
static double accumulator = 0.0;

static void frame() {
    if (!state) {
        updateMenu();
        BeginDrawing();
        drawMenu();
        EndDrawing();
        return;
    }

    double dt = (double)GetFrameTime();
    if (dt > 0.25) dt = 0.25; // clamp to avoid a spiral of death after a stall
    accumulator += dt;
    int steps = 0;
    while (accumulator >= TICK && steps < MAX_STEPS_PER_FRAME) {
        update((float)TICK);
        accumulator -= TICK;
        steps++;
    }

    BeginDrawing();
    renderFrame((float)TICK);
    EndDrawing();

    if (state->turn.state == TurnState::GAME_OVER &&
        (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        delete state;
        state = nullptr;
    }
}

int main() {
    InitWindow(1280, 720, "Squirms");
    SetTargetFPS(60);

    Input::init();
    Audio::init();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose()) {
        frame();
    }
    delete state;
    CloseWindow();
#endif

    return 0;
}
