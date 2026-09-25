#pragma once
#include <raylib.h>
#include "../weapons/weapons.h"

namespace Entities { class Worm; class Projectile; class Prop; }

// Procedurally drawn cartoon sprites (no image assets). Everything takes a
// pixel-space position; world-space calls go inside BeginMode2D.
namespace Sprites {

struct WormPose {
    float time;          // seconds, for idle animation
    bool holding;        // show the selected weapon in hand
    WeaponId weapon;
    bool showAim;        // crosshair + power cone
    float power;         // 0..1 charge
    Color teamColor;
    bool onRope = false; // hanging from the ninja rope (w.spin already points the head at the rope)
    bool active = false; // it's this worm's turn
};

void drawWorm(const Entities::Worm& w, const WormPose& pose);
void drawProjectile(const Entities::Projectile& p, float time);
void drawProp(const Entities::Prop& p, float time, Color graveColor);
void drawWeaponIcon(WeaponId id, Vector2 center, float size, float time);
void drawPlane(Vector2 pos, int dir, float time);

} // namespace Sprites
