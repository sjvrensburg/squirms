# Squirms

A Worms-like artillery game with collapsing physics terrain. Written in C++ with raylib and Box2D, compiled to WebAssembly with Emscripten for play in the browser.

## Build & Run

Requires the Emscripten SDK installed locally at `emsdk/` (not tracked in git — clone it from https://github.com/emscripten-core/emsdk and run `./emsdk install latest && ./emsdk activate latest`).

```bash
source emsdk/emsdk_env.sh
emcmake cmake -B build
cmake --build build
cp build/squirms_wasm.js build/squirms_wasm.wasm dist/
cp index.html dist/
PATH=$PATH:$(pwd)/emsdk/upstream/emscripten emrun dist/index.html
```

The game is entirely self-contained C++ — `index.html` is just a `<canvas id="canvas">` plus the Emscripten-generated glue script; raylib/GLFW3 own the canvas, input, and rendering directly, and the whole game loop runs in WASM.

In-engine start menu: Left/Right adjusts team count, Up/Down adjusts worms per team, type digits to set a reproducible seed (blank = random), Enter or click to start. F1 toggles the debug overlay (FPS, static/dynamic body counts).

## Controls

| Key | Action |
|-----|--------|
| A / D or ← → | Walk left/right |
| W / S or ↑ ↓ | Ninja rope retract/extend |
| Space / Enter | Jump |
| Backspace | Backflip |
| 1 | Bazooka |
| 2 | Grenade |
| 3 | Ninja rope |
| Escape | Detach rope |
| Q (hold) + release | Fire weapon (charge level) |
| F1 | Debug overlay |

## Weapons

- **Bazooka** — charged rocket, explodes on contact (radius 1.9 m, 45 dmg)
- **Grenade** — bouncy projectile with fuse (radius 1.7 m, 50 dmg)
- **Ninja rope** — raycast anchor, swing/extend/retract, detach mid-air

## Terrain Physics

Terrain is a grid of convex polygon chunks in a rigid-body world. Chunks start as static bodies; when support is blown out (i.e. they're no longer connected to the bedrock floor by a path of solid neighbors) they promote to dynamic slabs and topple together. Slabs that sleep for 0.75 s refreeze as static, keeping the dynamic body count near zero between turns.

## Tuning Constants

| Constant | File | Default | What it controls |
|----------|------|---------|------------------|
| `CELL` | terrain/grid.h | 20 | Chunk size in px (larger = fewer bodies) |
| `MAX_DYNAMIC_BODIES` | terrain/terrain.h | 120 | Slab freeze guard rail |
| `REFREEZE_THRESHOLD` | terrain/terrain.h | 0.75 | Seconds of sleep before refreeze |
| `SLAB_CHUNK_CAP` | terrain/terrain.h | 150 | Max chunks per dynamic body |

## Architecture

```
src/
  main.cpp                Entry point: menu, fixed-timestep loop, input, turn flow
  core/math.*, rng.*       Vector helpers, seeded RNG (mulberry32)
  core/input.*             raylib keyboard/mouse polling
  terrain/noise.*          Value noise + fBm
  terrain/generate.*       Heightfield + cave generation
  terrain/grid.*           Jittered convex quad chunks
  terrain/support.*        Flood-fill support detection (grounded vs. floating)
  terrain/terrain.*        TerrainSystem (fixtures, thaw, refreeze)
  physics/world.*          Box2D wrappers
  entities/worm.*          Worm controller
  entities/explosion.*     Blast routine
  weapons/ninjarope.*      Ninja rope joint
  weapons/index.*          Weapon registry
  game/turn.*              Turn state machine
  game/camera.*            Camera follow/clamp
  render/renderer.*        raylib-based renderer
  render/hud.*             HUD overlay
  audio/sfx.*              raylib sound playback
vendor/                    Prebuilt raylib + Box2D 2.4 static libs (WASM)
```
