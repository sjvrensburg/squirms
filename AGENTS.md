# Squirms Agent Cheat Sheet

## Setup
- Emscripten SDK lives locally at `emsdk/` (not tracked in git — install from https://github.com/emscripten-core/emsdk if missing). Activate it once per shell: `source emsdk/emsdk_env.sh`.

## Build
- `emcmake cmake -B build` – configure (only needed after CMakeLists.txt changes or a fresh clone/deleted `build/`).
- `cmake --build build` – compile; produces `build/squirms_wasm.js` / `.wasm`.
- `cp build/squirms_wasm.js build/squirms_wasm.wasm dist/ && cp index.html dist/` – stage the playable build.
- `PATH=$PATH:$(pwd)/emsdk/upstream/emscripten emrun dist/index.html` – serve and open in a browser.

## Gameplay shortcuts (in-engine menu, no URL params anymore)
- Left/Right: team count. Up/Down: worms per team. Type digits: seed (blank = random). Enter/click: start.
- F1 (in-game): toggle debug overlay (FPS, static/dynamic body counts).

## Architecture highlights
- Entry point: `src/main.cpp` — owns the menu, the fixed-timestep game loop (`frame()`, called via `emscripten_set_main_loop`), and turn/input handling.
- Terrain system: `src/terrain/terrain.cpp` (uses constants `CELL`, `MAX_DYNAMIC_BODIES`, `REFREEZE_THRESHOLD`, `SLAB_CHUNK_CAP` from `terrain/grid.h` and `terrain/terrain.h`).
- Weapons: defined in `src/weapons/`, accessed via keys 1‑3.
- raylib/GLFW3 own the canvas and input directly under Emscripten — there is no JS/TS game logic, only the minimal `Module.canvas` bootstrap snippet in `index.html`.

## Testing workflow
1. `cmake --build build` and confirm it exits 0 with no new warnings.
2. Serve `dist/` (see Build) and manually exercise the menu, a full turn, and firing each weapon in a real browser tab — headless/automated browser testing is unreliable here (see gotcha below) and won't substitute for this.

## Common gotchas
- **Coordinate conventions**: terrain body positions are Y-flipped (`WORLD_H - pixelY/PPM`) to fit Box2D's Y-up convention, and their fixture vertices must be *local* to that body origin (`(vert - centroid)/PPM`, Y-flipped) — Box2D vertices are body-relative, not world coordinates. `Worm::getPosition()` un-flips back to the pixel/PPM-meters convention that terrain rendering, camera, weapons, and explosions all assume; if you add new code that reads `body->GetPosition()` directly (bypassing that accessor), remember it's still in the raw flipped frame.
- `TerrainSystem::update()`'s `world->step()` uses whatever `dt` `main.cpp`'s fixed-timestep accumulator passes in (always `1/60`) — don't call `update()` with a raw `GetFrameTime()`, since `requestAnimationFrame` is not guaranteed to fire at 60Hz (high-refresh-rate displays, unthrottled headless browsers) and physics/camera/timers would desync from each other.
- Headless Chrome (used for automated verification) throttles/paces `requestAnimationFrame` in ways that don't reflect real browser behavior — synthetic input via CDP's `Input.dispatchKeyEvent`/`dispatchMouseEvent` was unreliable in this environment; dispatching a real DOM `KeyboardEvent`/click via `Runtime.evaluate` in-page worked reliably instead. Always sanity-check anything time-sensitive in a real foregrounded tab, not just headless captures.
