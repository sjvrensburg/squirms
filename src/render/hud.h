#pragma once
#include <raylib.h>

class Game;

// Worms 2-style HUD: turn clock (bottom left), team health bars (bottom
// centre), wind gauge (bottom right), selected weapon, message banner, the
// weapon panel, and the help / quit / game-over overlays.
void renderHUD(Game& game);

// Weapon panel hit test (screen coords): the WeaponId index under the mouse,
// or -1. Shared with Game's input handling so layout lives in one place.
int weaponPanelHit(Vector2 mouse);
