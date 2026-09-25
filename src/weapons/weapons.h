#pragma once

// The Worms 2-style arsenal. Behaviour lives in game/weapons.cpp; this table
// is the single source for names, default ammo and how each one is fired.
enum class WeaponId {
    Bazooka, Grenade, ClusterBomb, BananaBomb, HolyGrenade,
    Dynamite, Mine, Sheep,
    Shotgun, BaseballBat, NinjaRope,
    AirStrike, Teleport, SkipGo,
    COUNT
};
constexpr int WEAPON_COUNT = (int)WeaponId::COUNT;

// How the fire key behaves for a weapon.
enum class FireMode {
    Charged,   // hold Space to build power, release to launch along the aim
    Instant,   // press Space to fire along the aim (no power)
    Drop,      // press Space to place/release at the worm's feet
    Targeted,  // click a spot on the map
    Skip,      // press Space to end the turn
    Rope,      // press Space to shoot the ninja rope; doesn't use up the turn
};

struct WeaponInfo {
    const char* name;
    int ammo;          // starting ammo per team; -1 = unlimited
    FireMode mode;
    bool aims;         // shows the crosshair
    bool fused;        // fuse adjustable with keys 1-5
    int shots;         // shots per turn
    const char* blurb; // one-line description for the weapon panel
};

const WeaponInfo& weaponInfo(WeaponId id);
