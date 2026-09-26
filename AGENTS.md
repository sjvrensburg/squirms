# Squirms Agent Cheat Sheet

## Setup
- Emscripten SDK lives locally at `emsdk/` (not tracked in git — install from https://github.com/emscripten-core/emsdk if missing). Activate it once per shell: `source emsdk/emsdk_env.sh`.

## Build
- `emcmake cmake -B build` – configure (only needed after CMakeLists.txt changes or a fresh clone/deleted `build/`).
- `cmake --build build` – compile; produces `build/squirms_wasm.js` / `.wasm`.
- `cmake --build build --target stage` – stage the playable build.
- `PATH=$PATH:$(pwd)/emsdk/upstream/emscripten emrun dist/index.html` – serve and open in a browser.

## Gameplay shortcuts (in-engine menu, no URL params)
- Menu: Up/Down pick a row, Left/Right change it (teams, Computer/Hot-seat, worms, energy, landscape). Type digits: seed; R: reroll. Enter/click: start.
- In-game: arrows walk/aim, Space (hold) fires, Enter jumps, Backspace backflips, Tab/right-click weapon panel, H help, Esc quit. Ninja rope: Space shoots/lets go, Left/Right swing, Up/Down climb, Enter lets go.
- F1: debug overlay (FPS, body counts); while on, left-click blows a hole at the cursor.

## Architecture highlights
- Entry point: `src/main.cpp` owns the menu and the fixed-timestep loop (`frame()`, called via `emscripten_set_main_loop`). The match itself is `Game` in `src/game/` (turn phases, input, weapons, props, CPU AI).
- Terrain system: `src/terrain/terrain.cpp` (uses constants `CELL`, `WATER_Y`, `MAX_DYNAMIC_BODIES`, `REFREEZE_THRESHOLD`, `SLAB_CHUNK_CAP` from `terrain/grid.h` and `terrain/terrain.h`); drawn by `src/render/terrain_render.cpp` from a cached texture. Collapse is structural (`terrain/support.cpp`: load paths to bedrock with a span limit), plus explosion ejecta, slab shattering and rubble that re-falls when its support goes.
- Ninja rope: `src/game/rope.cpp` (distance joint to a fixed point, wraps round corners).
- Weapons: table in `src/weapons/weapons.cpp`, behaviour in `src/game/weapons.cpp`, picked from the Tab weapon panel.
- No image/sound assets: sprites are procedural (`render/sprites.cpp`), sounds are synthesized (`audio/sfx.cpp`); the Montserrat font in `assets/fonts` is embedded via `--embed-file`.
- New `.cpp` files need a re-run of `emcmake cmake -B build` (sources are globbed).
- raylib/GLFW3 own the local canvas under Emscripten. `web/online.js` handles room pairing and WebRTC remote play; simulation stays in C++. Use `node server/server.mjs` for online rooms (see README).

## Testing workflow
1. `cmake --build build` and confirm it exits 0 with no new warnings.
2. Serve `dist/` (see Build) and exercise the menu, a full turn, and firing each weapon, either in a real foregrounded browser tab or with scripted headless Chrome over CDP (see CLAUDE.md, "Headless browser testing", for the gotchas).

## Common gotchas
- **Coordinate conventions**: terrain body positions are Y-flipped (`WORLD_H - pixelY/PPM`) to fit Box2D's Y-up convention, and their fixture vertices must be *local* to that body origin (`(vert - centroid)/PPM`, Y-flipped) — Box2D vertices are body-relative, not world coordinates. `Worm::getPosition()` un-flips back to the pixel/PPM-meters convention that terrain rendering, camera, weapons, and explosions all assume; if you add new code that reads `body->GetPosition()` directly (bypassing that accessor), remember it's still in the raw flipped frame.
- `TerrainSystem::update()`'s `world->step()` uses whatever `dt` `main.cpp`'s fixed-timestep accumulator passes in (always `1/60`) — don't call `update()` with a raw `GetFrameTime()`, since `requestAnimationFrame` is not guaranteed to fire at 60Hz (high-refresh-rate displays, unthrottled headless browsers) and physics/camera/timers would desync from each other.
- Headless Chrome (used for automated verification) throttles/paces `requestAnimationFrame` in ways that don't reflect real browser behavior — synthetic input via CDP's `Input.dispatchKeyEvent`/`dispatchMouseEvent` was unreliable in this environment; dispatching a real DOM `KeyboardEvent`/click via `Runtime.evaluate` in-page worked reliably instead. Always sanity-check anything time-sensitive in a real foregrounded tab, not just headless captures.
- Input: key/mouse *presses* are handled once per rendered frame in `Game::frameInput()`, held keys per fixed tick in `Game::tickInput()`. Don't read `IsKeyPressed` from per-tick code: a frame can run 0 or several ticks, which drops or doubles presses.
