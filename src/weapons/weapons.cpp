#include "weapons.h"

static const WeaponInfo WEAPONS[WEAPON_COUNT] = {
    {"Bazooka",           -1, FireMode::Charged,  true,  false, 1, "Rocket. Explodes on impact. Blown about by the wind."},
    {"Grenade",           -1, FireMode::Charged,  true,  true,  1, "Bouncy. Fuse 1-5 seconds."},
    {"Cluster Bomb",       4, FireMode::Charged,  true,  true,  1, "Bursts into a shower of bomblets."},
    {"Banana Bomb",        1, FireMode::Charged,  true,  true,  1, "Five more bananas. Devastating."},
    {"Holy Hand Grenade",  1, FireMode::Charged,  true,  false, 1, "Count to three. Hallelujah!"},
    {"Dynamite",           2, FireMode::Drop,     false, false, 1, "Drop it and run. Five second fuse."},
    {"Mine",               2, FireMode::Drop,     false, false, 1, "Arms after a moment. Trips on worms."},
    {"Sheep",              1, FireMode::Drop,     false, false, 1, "Baa. Press Space again to detonate."},
    {"Shotgun",           -1, FireMode::Instant,  true,  false, 2, "Two shots per turn."},
    {"Baseball Bat",       1, FireMode::Instant,  true,  false, 1, "Home run! Knocks worms flying."},
    {"Ninja Rope",        -1, FireMode::Rope,     true,  false, 1, "Space: shoot/let go. Left/Right swing, Up/Down climb. Doesn't end your turn."},
    {"Air Strike",         1, FireMode::Targeted, false, false, 1, "Click a target. Five missiles."},
    {"Teleport",           2, FireMode::Targeted, false, false, 1, "Click anywhere to beam there."},
    {"Skip Go",           -1, FireMode::Skip,     false, false, 1, "Pass the turn."},
};

const WeaponInfo& weaponInfo(WeaponId id) {
    return WEAPONS[(int)id];
}
