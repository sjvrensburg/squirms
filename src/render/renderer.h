#pragma once

class Game;

// Draw the battlefield (sky, backdrop, terrain, sea, worms, props, shells,
// particles, weather) and the in-world labels. HUD lives in hud.h.
void renderWorld(Game& game);
