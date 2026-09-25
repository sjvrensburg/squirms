#pragma once
#include <raylib.h>
#include <string>
#include <vector>

#include "camera.h"
#include "../entities/worm.h"
#include "../entities/projectile.h"
#include "../entities/prop.h"
#include "../render/fx.h"
#include "../render/terrain_render.h"
#include "../render/theme.h"
#include "../weapons/weapons.h"

class World;
namespace Terrain { class TerrainSystem; }

struct GameConfig {
    int teams = 2;
    int wormsPerTeam = 4;
    int seed = 1;
    int theme = 0;
    int startHp = 100;
    bool vsCpu = false;      // team 1 is human, the rest are computer-controlled
};

struct Team {
    std::string name;
    Color color;
    std::vector<Entities::Worm*> worms;
    int ammo[WEAPON_COUNT];
    int nextWorm = 0;
    WeaponId weapon = WeaponId::Bazooka;
    bool cpu = false;

    float shownHp() const;
    bool hasActiveWorms() const;
};

// Phases of play, Worms style:
//   Intro    - "It's your go" banner, camera flies to the next worm.
//   Aiming   - the worm may walk, jump, aim and fire; the turn clock runs.
//   Retreat  - after the shot: a few seconds to scarper, no more firing.
//   Settling - hands off; wait for shells, collapsing terrain and flying
//              worms to come to rest, while health counters tick down.
//   Deaths   - worms reduced to 0 HP blow up one at a time.
//   GameOver - one team (or nobody) left standing.
enum class Phase { Intro, Aiming, Retreat, Settling, Deaths, GameOver };

// The ninja rope (see game/rope.cpp). While shooting, the hook flies out
// along the aim; once attached, the worm hangs from a max-length distance
// joint at the newest pivot. The rope wraps round terrain corners: each wrap
// adds a pivot (and remembers which side it wrapped on so it can unwrap).
struct NinjaRope {
    enum class State { Idle, Shooting, Attached } state = State::Idle;
    Vector2 dir{0, 0};                // shooting direction (pixel space)
    float shot = 0;                   // how far the hook has flown (px)
    std::vector<Vector2> pivots;      // pixel space; back() is the live pivot
    std::vector<int> wrapSide;        // per pivot: sign of the turn it wrapped
    std::vector<void*> pivotBodies;   // terrain body each pivot is fixed to
    float freeLen = 0;                // px from the live pivot to the worm (max)
    void* joint = nullptr;
};

// What a computer worm has decided to do this turn (see game/ai.cpp).
struct AiPlan {
    bool ready = false;
    WeaponId weapon = WeaponId::Bazooka;
    int facing = 1;
    float aim = 0;
    float power = 0;
    int fuse = 3;
};

class Game {
public:
    explicit Game(const GameConfig& cfg);
    ~Game();

    void frameInput();       // once per rendered frame: presses, clicks, toggles
    void update(float dt);   // one fixed simulation tick (held keys, physics, turn flow)
    void draw();             // world + HUD for this frame
    bool wantsExit() const { return exitRequested; }

    // ---- state (read by the renderer/HUD) ----
    GameConfig cfg;
    const Theme* theme;
    World* world = nullptr;
    Terrain::TerrainSystem* terrain = nullptr;
    TerrainRenderer terrainGfx;
    Fx fx;
    GameCamera camera;

    std::vector<Team> teams;
    std::vector<Entities::Worm*> worms;
    std::vector<Entities::Projectile> projectiles;
    std::vector<Entities::Prop> props;

    Phase phase = Phase::Intro;
    float phaseTime = 0;         // seconds in the current phase
    float turnTime = 45;         // Aiming clock
    float retreatTime = 0;
    int currentTeam = 0;
    Entities::Worm* current = nullptr;
    float wind = 0;              // -1 .. 1
    int turnNumber = 0;
    float matchTime = 0;

    WeaponId weapon = WeaponId::Bazooka;
    int fuse = 3;
    float power = 0;
    bool charging = false;
    int shotsLeft = 1;
    bool fired = false;
    bool panelOpen = false;
    bool helpOpen = false;
    bool confirmQuit = false;
    bool debug = false;
    float perfUpdateMs = 0, perfDrawMs = 0;   // shown in the F1 overlay
    int winner = -1;             // team index; -1 draw (valid in GameOver)

    std::string message;
    Color messageColor = WHITE;
    float messageTime = 0;

    // Air strike plane sweeping across the top of the screen.
    float planeX = -1e9f, planeY = 0;
    int planeDir = 1;

    NinjaRope rope;

    Vector2 mouseWorld{0, 0};
    bool exitRequested = false;

    // Helpers used by the renderer.
    Vector2 aimDir(const Entities::Worm* w) const;   // unit vector, pixel space
    bool isTargeting() const;

private:
    struct PendingBlast { float x, y, radius, damage, delay; };
    std::vector<PendingBlast> blasts;
    // Bomblets spawned mid-loop in updateProjectiles; appended afterwards so
    // the projectile vector isn't reallocated under the loop.
    std::vector<Entities::Projectile> newShells;
    Vector2 lastMouse{0, 0};
    bool dragging = false;
    float dragDist = 0;
    float settleCap = 0;
    float quietTime = 0;         // how long the world has been still while settling
    float deathTimer = 0;
    float fireworksTimer = 0;
    float tickSoundAt = 0;

    AiPlan ai;
    int aiStage = 0;
    float aiTimer = 0;
    void planCpuTurn();
    void updateCpu(float dt);
    bool cpuTurn() const { return teams[currentTeam].cpu; }

    void spawnWorldObjects(unsigned seed);
    std::vector<Vector2> surfaceSpots(float minGap, int count, unsigned seed,
                                      const std::vector<Vector2>& avoid);

    // Turn flow
    void startTurn();
    void endTurn();              // go to Settling
    void enterPhase(Phase p);
    void updatePhase(float dt);
    bool worldIsQuiet() const;
    void checkGameOver();
    void say(const std::string& text, Color color, float seconds = 2.5f);

    // Input
    void tickInput(float dt);
    void handleCamera(float dt);
    void selectWeapon(WeaponId id);

    // Weapons (game/weapons.cpp)
    void fire();
    void fireAt(Vector2 target);
    void launch(Entities::ProjectileType type, Vector2 from, Vector2 vel, float fuseSeconds);
    void updateProjectiles(float dt);
    void detonate(Entities::Projectile& p);
    void updateSheep(Entities::Projectile& p, float dt);
    void shotgunBlast();
    void batSwing();
    bool teleport(Vector2 target);

    // Ninja rope (game/rope.cpp)
    void shootRope();
    void releaseRope();
    void ropeControls(float dt);
    void updateRope(float dt);
    void attachRope(Vector2 at, void* body);
    void rehangRope();

    // Terrain physics hooks (game/weapons.cpp)
    void terrainAftermath();

    // Props (game/props.cpp)
    void updateProps(float dt);
    void dropCrate();

public:
    // Blow a crater, hurt and fling everything nearby.
    void explode(float x, float y, float radius, float damage);
};
