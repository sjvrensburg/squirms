# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Squirms is a Worms-like artillery game with collapsing physics terrain, written entirely in **C++** (raylib for rendering/input/audio, Box2D 2.4 for physics) and compiled to **WebAssembly** via Emscripten. It runs as a self-contained WASM module — raylib/GLFW3 own the canvas and drive the whole game loop directly; `index.html` is just a `<canvas id="canvas">` plus a one-line `Module.canvas = ...` bootstrap snippet and the Emscripten-generated glue script (`squirms_wasm.js`). There is no JS/TS game logic anywhere in the project.

## Commands

```bash
source emsdk/emsdk_env.sh          # activate the local Emscripten SDK (once per shell; not tracked in git)
emcmake cmake -B build              # configure (re-run if CMakeLists.txt changes)
cmake --build build                 # compile -> build/squirms_wasm.{js,wasm}
cp build/squirms_wasm.js build/squirms_wasm.wasm dist/ && cp index.html dist/
PATH=$PATH:$(pwd)/emsdk/upstream/emscripten emrun dist/index.html   # serve + open in a browser
```

There is no test suite. Verify changes by building cleanly and manually exercising the game in a real (non-headless) browser tab — see the headless-testing gotcha below.

Gameplay setup previously lived behind URL query params (`?seed=`, `?debug=1`) in an HTML form; that's gone. It's all in-engine now: the start menu (Left/Right = team count, Up/Down = worms/team, type digits = seed, Enter/click = start) and an F1 toggle in-game for the debug overlay (FPS, static/dynamic body counts).

## Architecture

```
src/
  main.cpp                Entry point: menu, fixed-timestep loop (frame(), via
                           emscripten_set_main_loop), input, turn flow, weapon firing
  core/math.*, rng.*       Vector helpers, seeded RNG (mulberry32)
  core/input.*             raylib keyboard/mouse polling
  terrain/noise.*          Value noise + fBm
  terrain/generate.*       Heightfield + cave generation (bedrock fills the bottom 2 rows)
  terrain/grid.*           Jittered convex quad chunks; CELL/PPM/WORLD_W/WORLD_H/COLS/ROWS constants
  terrain/support.*        Flood-fill support detection: BFS from bedrock/bottom-row chunks
                           through solid 4-adjacency: reachable = grounded, everything else
                           in a SOLID chunk is a floating cluster that gets thawed to dynamic
  terrain/terrain.*        TerrainSystem (fixture creation, thaw-to-dynamic, refreeze)
  physics/world.*          Thin Box2D wrapper (World::createBody/createPolygonFixture/step)
  entities/worm.*          Worm controller (walk/jump/backflip/takeDamage)
  entities/explosion.*     Blast routine (damages chunks + worms in radius)
  weapons/ninjarope.*      Ninja rope joint
  weapons/index.*          Weapon registry (Bazooka, Grenade)
  game/turn.*              Turn state machine (timer, current team/worm rotation, wind)
  game/camera.*            Camera follow/clamp
  render/renderer.*        raylib-based renderer (chunk outlines, worms, HUD)
  render/hud.*             HUD stub (renderHUD lives in renderer.cpp)
  audio/sfx.*              raylib sound loading/playback
vendor/                    Prebuilt raylib + Box2D 2.4 static libs for WASM (headers + .a)
emsdk/                     Local Emscripten SDK install (gitignored — see Commands)
```

### Coordinate conventions (read this before touching physics/terrain/camera code)

Box2D bodies use a Y-up convention; everything else (terrain generation, chunk grid, rendering, camera) uses Y-down pixel space (`Terrain::PPM` px/meter, world size `Terrain::WORLD_W`/`WORLD_H` in meters). The two are bridged at exactly two points, and both must stay consistent:

- **Body placement**: `bodyY = WORLD_H - pixelY/PPM` (used for both terrain chunk bodies in `terrain.cpp` and worm bodies in `worm.cpp`'s `init()`).
- **Fixture vertices**: Box2D polygon vertices are *local to the body origin*, not world coordinates. Terrain fixtures are built as `((vert - centroid)/PPM, -(vert - centroid)/PPM)` (centroid = the chunk's own centroid for static chunks, or the shared slab centroid for thawed dynamic chunks) — getting this wrong (e.g. passing absolute world coordinates) silently produces fixtures nowhere near their visual position, so nothing collides even though everything renders normally. This exact bug is what made every worm fall through the entire map before it was fixed.
- **Reading positions back**: `Worm::getPosition()` un-flips (`Terrain::WORLD_H - body->GetPosition().y`) so callers (camera, renderer, weapons, explosions) can just multiply by `Terrain::PPM` and get pixel space directly. Code that reads `body->GetPosition()` directly (bypassing that accessor) is still in the raw flipped frame — that's fine for physics-internal logic (e.g. the grounded raycast in `Worm::update()`) but wrong for anything render/gameplay-facing.

### Fixed timestep

`main.cpp`'s `frame()` accumulates real `GetFrameTime()` into a `double accumulator` and drains it in fixed `TICK = 1/60` steps (capped at `MAX_STEPS_PER_FRAME` to avoid a spiral of death), calling `update(TICK)` each step. `world->step()`, `turn.update()`, and `camera.update()` all receive that same fixed `dt`. Don't reintroduce a raw `GetFrameTime()` call into the simulation path — `requestAnimationFrame` isn't guaranteed to fire at 60Hz (high-refresh-rate monitors, unthrottled headless browsers), and physics/timers/camera would desync from each other and from real time if driven directly by it.

### Terrain support / collapse

`terrain/support.cpp`'s `clusterUnsupported()` BFS-floods outward from every `Material::Bedrock` chunk (and any chunk in the bottom row) through solid 4-adjacent neighbors to find everything "grounded." Any `SOLID` chunk *not* reached by that flood is grouped into a connected component and returned; `TerrainSystem::buildInitialFixtures()` skips fixture creation for those (they're about to fall) and `TerrainSystem::recomputeSupportAndThaw()` immediately converts each component into a dynamic falling slab (capped at `SLAB_CHUNK_CAP` chunks per slab body). Only *grounded* solid chunks get real static fixtures. If you change terrain generation in a way that can disconnect large regions from the bedrock floor (e.g. very aggressive cave carving), expect large-scale immediate collapse — that's working as intended, not a bug, per the README's terrain-physics description.

### Headless browser testing gotcha

Verifying this game visually requires a browser (it's a raylib/WebGL canvas app, not something you can curl). Headless Chrome behaves unusually here: CDP's `Input.dispatchKeyEvent`/`dispatchMouseEvent` were unreliable for reaching the in-canvas raylib/GLFW input handlers in this environment; dispatching a real DOM `KeyboardEvent`/click via `Runtime.evaluate` (in-page JS) worked reliably instead. `requestAnimationFrame` pacing under headless/backgrounded tabs is also not representative of a real foregrounded tab — don't trust timing-sensitive observations (e.g. "the camera isn't converging") from a headless capture without cross-checking against a longer/differently-paced run first.
