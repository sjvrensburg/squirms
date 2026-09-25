# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Squirms is a Worms 2-style artillery game with destructible, collapsing physics terrain, written entirely in **C++** (raylib for rendering/input/audio, Box2D 2.4 for physics) and compiled to **WebAssembly** via Emscripten. It runs as a self-contained WASM module — raylib/GLFW3 own the canvas and drive the whole game loop directly; `index.html` and `web/` provide the browser shell, sound preferences and WebRTC remote play. Gameplay/physics remain in C++; the host streams its canvas to the guest and validates received input in `src/net/input.*`. Online rooms use `node server/server.mjs` (see README).

There are no image or sound assets: sprites are drawn procedurally (`render/sprites.cpp`) and every sound is synthesized at startup (`audio/sfx.cpp`). The one asset is the Montserrat font in `assets/fonts/` (SIL OFL), baked into the build with `--embed-file` (see `CMakeLists.txt`), and the `stage` build target also copies the browser shell into `dist/web/`.

## Commands

```bash
source emsdk/emsdk_env.sh          # activate the local Emscripten SDK (once per shell; not tracked in git)
emcmake cmake -B build              # configure (re-run if CMakeLists.txt changes)
cmake --build build                 # compile -> build/squirms_wasm.{js,wasm}
cmake --build build --target stage
PATH=$PATH:$(pwd)/emsdk/upstream/emscripten emrun dist/index.html   # serve + open in a browser
```

Run the server, input-boundary and two-browser integration tests documented in README. Also verify changes in foreground browser tabs (see "Headless browser testing" below).

`emcmake cmake -B build` must also be re-run after **adding a new `.cpp` file**: sources are collected with `file(GLOB_RECURSE ...)`, so a plain `cmake --build` won't see it (you'll get an undefined-symbol link error).

Gameplay setup is all in-engine (no URL params): the start menu (Up/Down pick a row, Left/Right change it: teams, Computer vs Hot-seat opponents, worms/team, worm energy, landscape theme; type digits for the seed, R rerolls; Enter/click starts). In-game: H shows the controls, F1 toggles the debug overlay (FPS, static/dynamic body counts, shells, props) and, while it's on, a left-click blows a hole at the cursor (handy for poking at terrain collapse).

## Architecture

```
src/
  main.cpp                 Window (tracks page size), start menu + map preview, fixed-timestep loop
  core/rng.*, math.*       Seeded RNG (mulberry32), small math helpers
  terrain/noise.*          Gradient noise + fBm
  terrain/generate.*       Island generator: fBm heightfield tapered into the sea, 2D warp for
                           overhangs, ridged-noise tunnels + caverns, bedrock floor; prunes anything
                           not connected to the floor so nothing collapses on turn one
  terrain/grid.*           Jittered convex quad chunks; CELL/PPM/WORLD_*/COLS/ROWS/WATER_Y constants
  terrain/support.*        Flood-fill support detection: BFS from bedrock/bottom-row chunks
                           through solid 4-adjacency: reachable = grounded, everything else
                           in a SOLID chunk is a floating cluster that gets thawed to dynamic
  terrain/terrain.*        TerrainSystem (fixture creation, thaw-to-dynamic, refreeze, sinking,
                           `dirty` list for the renderer)
  physics/world.*          Thin Box2D wrapper (bodies, polygon/circle fixtures, collision filters,
                           category-masked ray casts, contact records)
  entities/worm.*          Worm: capsule body, walk with step-up assist, jump/backflip, knockback
                           flight, fall damage, drowning, hp vs shownHp
  entities/projectile.*    Anything fired/thrown/dropped (bazooka ... sheep); behaviour in game/
  entities/prop.*          Barrels, mines, crates, gravestones
  weapons/weapons.*        Weapon table (name, ammo, FireMode, aims/fused, shots)
  game/game.*              Game: state, turn phases (Intro/Aiming/Retreat/Settling/Deaths/GameOver),
                           input, camera follow, world spawning
  game/weapons.cpp         Firing, projectile update (fuses, bounces, the sheep), Game::explode()
  game/props.cpp           Barrels/mines/crates update, crate drops and pick-ups
  game/rope.cpp            Ninja rope: hook flight, distance joint to a fixed point, wrap/unwrap
                           round corners, snaps if its anchor goes
  game/ai.cpp              CPU teams: brute-force trajectory search, then aim + charge visibly
  game/camera.*            Camera2D-based pan/zoom/shake, clamped to the map
  render/terrain_render.*  TerrainRenderer: cached painted terrain texture (see below)
  render/renderer.*        Sky, parallax hills, sea, weather, world sprites, worm labels
  render/sprites.*         Procedural worm / weapon icon / projectile / prop drawings
  render/fx.*              Particles, floating damage numbers, comic "POW!" bursts, screen shake
  render/hud.*             Timer, team bars, wind gauge, weapon card + panel, overlays
  render/text.*            Font loading (embedded Montserrat) and outlined text helpers
  render/theme.*           Landscape themes (Meadow/Desert/Arctic/Hell palettes + weather)
  render/rlgl_min.h        Hand-declared rlgl entry points (the vendored raylib ships no rlgl.h)
  audio/sfx.*              Procedurally synthesized sound effects (Audio::playSound(name, vol, pitch))
assets/fonts/              Montserrat (SIL OFL), embedded into the build
vendor/                    Prebuilt raylib + Box2D 2.4 static libs for WASM (headers + .a)
emsdk/                     Local Emscripten SDK install (gitignored — see Commands)
```

### Coordinate conventions (read this before touching physics/terrain/camera code)

Box2D bodies use a Y-up convention; everything else (terrain generation, chunk grid, rendering, camera) uses Y-down pixel space (`Terrain::PPM` px/meter, world size `Terrain::WORLD_W`/`WORLD_H` in meters). The two are bridged at exactly two points, and both must stay consistent:

- **Body placement**: `bodyY = WORLD_H - pixelY/PPM` (used for both terrain chunk bodies in `terrain.cpp` and worm bodies in `worm.cpp`'s `init()`).
- **Fixture vertices**: Box2D polygon vertices are *local to the body origin*, not world coordinates. Terrain fixtures are built as `((vert - centroid)/PPM, -(vert - centroid)/PPM)` (centroid = the chunk's own centroid for static chunks, or the shared slab centroid for thawed dynamic chunks) — getting this wrong (e.g. passing absolute world coordinates) silently produces fixtures nowhere near their visual position, so nothing collides even though everything renders normally. This exact bug is what made every worm fall through the entire map before it was fixed.
- **Pixel-space angles**: Y-down flips rotation too. A Box2D body angle becomes `-angle` in pixel space (see `Prop::sync`), and a pixel-space velocity `(vx, vy)` becomes `(vx/PPM, -vy/PPM)` in Box2D.
- **Reading positions back**: `Worm::getPosition()` un-flips (`Terrain::WORLD_H - body->GetPosition().y`) so callers (camera, renderer, weapons, explosions) can just multiply by `Terrain::PPM` and get pixel space directly. Code that reads `body->GetPosition()` directly (bypassing that accessor) is still in the raw flipped frame — that's fine for physics-internal logic (e.g. the grounded raycast in `Worm::update()`) but wrong for anything render/gameplay-facing.

### Fixed timestep

`main.cpp`'s `frame()` accumulates real `GetFrameTime()` into a `double accumulator` and drains it in fixed `TICK = 1/60` steps (capped at `MAX_STEPS_PER_FRAME` to avoid a spiral of death), calling `game->update(TICK)` each step. `world->step()`, the turn-phase clock, and `camera.update()` all receive that same fixed `dt`. Don't reintroduce a raw `GetFrameTime()` call into the simulation path — `requestAnimationFrame` isn't guaranteed to fire at 60Hz (high-refresh-rate monitors, unthrottled headless browsers), and physics/timers/camera would desync from each other and from real time if driven directly by it.

### Terrain support / collapse

`terrain/support.cpp` is a structural model, not just a connectivity check. `supportCost()` runs a bucket-queue Dijkstra from every bedrock/bottom-row cell. Moving up onto a cell (it rests on us) costs 0. A sideways step costs `sideCost(material, thickness)`: thin dirt ledges are expensive, thick terrain and rock cheap. Stepping down (hanging) costs `hangCost`. Cells whose cheapest load path exceeds `MAX_SPAN` are unsupported; `clusterUnsupported()` groups them (4-adjacency) and `TerrainSystem::recomputeSupportAndThaw()` thaws each group into a falling slab (capped at `SLAB_CHUNK_CAP` chunks per body). So cutting under an overhang makes the part beyond the span limit snap off, not just fully disconnected islands.

`generate.cpp` runs the *same* model on the fresh map. Over-long spans are propped with flared rock pillars under the middle of the span, and whatever still can't stand is removed, so a new map starts fully stable. If you change the cost tables, re-check a few seeds with a native preview (compile `generate.cpp`/`noise.cpp`/`grid.cpp`/`support.cpp` with g++ and dump the solid grid to an image). Too-strict costs gut the hills; too-lax ones make nothing ever fall.

Other ways terrain moves (all in `terrain.cpp`, driven from `Game::explode` in `game/weapons.cpp`):
- **Ejecta**: `breakOff(group)` thaws a few rim chunks just outside a crater; the game gives them a launch velocity.
- **Rubble wake-up**: `destroyChunk`/`thawSlab` record centroids in `recentChanges`. `recomputeSupportAndThaw()` then calls `wakeLanded()`, which thaws landed rubble near those points plus everything touching it (proximity-hashed). `shakeLoose()` does the same around a blast. So rubble never floats after its support is removed.
- **Shattering**: `detectImpacts()` watches each dynamic body's velocity change. A jolt above `SHATTER_DV` on a slab with ≥ `SHATTER_MIN_CHUNKS` chunks rebakes it (`rebakeToWorld`) and re-thaws it as several small pieces. Every jolt is reported in `impacts` for dust/shake/sound (`Game::terrainAftermath`, which also applies crush damage to worms touching fast-moving slabs).
- `thawSlab` calls `exposeNeighbors` on the chunks it takes, because a snapped-off overhang can leave buried grid chunks (no fixture of their own) newly exposed.

### Falling slabs come to rest as static terrain

A thawed slab falls as one Box2D `dynamic` body. `TerrainSystem::update()` runs `tryRefreeze()` once per `REFREEZE_THRESHOLD` (0.75s). A slab is frozen only once it has actually come to rest: Box2D reports it asleep, or its linear and angular speed stay below small thresholds for a full interval (tracked in `TerrainSystem::restFrames`, so a momentary near-stop at a bounce apex can't trap it mid-air). A slab is never frozen while still moving — except by the `MAX_DYNAMIC_BODIES` safety rail (`forceFreezeOldest()`), which now freezes bodies properly too.

`freezeBody()` destroys the shared dynamic body and gives every chunk in the slab a fresh **static** body at its resting transform. Each chunk's `verts`/`centroid` are rebaked into its current world position (the slab may have rotated while falling), so the chunk renders and collides where it actually landed. Landed chunks set `Chunk::landed = true` and become `SOLID`; their collision bodies are registered as `World::BodyKind::Terrain`, so worms stand on them and later blasts can damage/destroy them (blast distance is measured off the rebaked `centroid`).

Rendering: settled rubble is **baked into the terrain texture** (`TerrainRenderer::bakeRubble`, via `rubbleAdded`/`rubbleRemoved`), with a per-pixel rubble layer that maps each pixel back to the chunk's original spot in `base`. Only *falling* chunks are drawn as individual quads.

Landed chunks are **excluded from the support model**: `support.cpp` treats them as empty cells (skipped in both the seeding and the 4-adjacency steps), so the hole a chunk left behaves as a hole and a landed chunk can neither prop up nor be propped up by its old grid neighbours. `clusterUnsupported()` never returns landed chunks. Rubble only re-thaws through `wakeLanded()` (its support vanished nearby, or a blast shook it). The net effect is that a map with no active collapse settles back to a quiet state with zero dynamic bodies (`Dynamic:` in the F1 overlay returns to 0). The Y-flip and body-local fixture-vertex conventions above still apply when rebaking the verts; bodies are only created/destroyed in `update()` (after `world->step()`), never inside a Box2D callback or during `step()`.

### Ninja rope

`game/rope.cpp`. Once attached, the worm hangs from a Box2D distance joint with `minLength = 0`, `maxLength = freeLen` and `stiffness = 0`, which makes it a rope (slack below the limit, rigid at it). The joint is anchored on `World`'s fixture-less static `ground` body at the live pivot, never on a terrain chunk body, because chunk bodies get destroyed and would take the joint with them. Wrapping: each tick after the physics step, a ray from the worm to the live pivot that hits terrain adds a new pivot there. The joint is recreated with the remaining free length, and the turn direction is remembered in `wrapSide`. The pivot unwraps when that side flips and the previous pivot is visible again. Each pivot remembers its terrain body; if that body is destroyed or becomes dynamic, the rope snaps. `Worm::onRope` suppresses fall-damage bookkeeping and ground friction, and the game sets `Worm::spin` so the sprite hangs head-first toward the pivot.

### Input: presses per frame, held keys per tick

raylib latches `IsKeyPressed`/`IsMouseButtonPressed` for a whole rendered frame, but a frame runs zero or several fixed ticks. So `Game::frameInput()` (called once per frame from `main.cpp`, before the tick loop) handles every edge-triggered action: toggles, weapon selection, jump/backflip, fuse keys, starting a charge, instant/drop/targeted fire, sheep detonation, camera drag/zoom. `Game::tickInput(dt)` (per tick) only reads held state: walking, aiming, accumulating charge power and firing on release. Reading presses per tick is a real bug here: it once made a single Space both release and detonate the sheep, and a single Tab open and immediately close the weapon panel.

### Terrain rendering

`TerrainRenderer` never draws the static landscape chunk by chunk. At map build it rasterizes every chunk's quad into a per-pixel owner map, paints a pristine `base` texture (soil/rock/pebbles plus a crust on up-facing surfaces, using the theme palette), and builds a `display` texture: base pixels masked to still-standing grid chunks, **box-filter smoothed** (`SMOOTH_R`, via a summed-area table) so the 10 px staircase reads as organic ground, plus a dark outline. When chunks are destroyed or thawed, `TerrainSystem` pushes them onto `terrain->dirty`; `TerrainRenderer::sync()` clears their pixels and re-uploads only the affected rectangles (`UpdateTextureRec`). Falling chunks are drawn as textured quads sampling `base` at the chunk's *original* verts (`Chunk::uv`), slightly grown to hide seams. Settled rubble is baked into `display` (see the falling-slabs section above) and smoothed together with the standing terrain. The smoothing means the drawn edge can sit up to ~`SMOOTH_R` px off the physics edge; that's intentional.

Materials: explosions destroy SOLID chunks whose centroid is within the crater radius (rock only within 80% of it); bedrock never. The per-chunk `hp` field is no longer used for blasts.

### Headless browser testing

Verification is done by driving local headless Chrome over CDP. Node 22 has a global `WebSocket`, so a ~100-line script with no npm deps can launch `google-chrome --headless=new --remote-debugging-port=...`, navigate to the served `dist/`, dispatch DOM `KeyboardEvent`/`MouseEvent`/`WheelEvent`s on `window`/the canvas via `Runtime.evaluate`, and save `Page.captureScreenshot` PNGs. Gotchas:
- Hold synthetic keys ~200 ms. Headless SwiftShader frames can be slow, and a keydown+keyup inside one frame is invisible to raylib.
- Printable keys need a `keypress` event too, or `GetCharPressed()` (menu seed entry) won't see them.
- A Chrome-extension tab that isn't foregrounded reports `document.visibilityState === "hidden"`, so `requestAnimationFrame` (and the whole game loop) is paused and screenshots are stale.
- Check which process owns the port you serve on; a stale server from another session can silently serve an old build.
- CDP's `Input.dispatchKeyEvent`/`dispatchMouseEvent` were unreliable for reaching raylib/GLFW's handlers; real DOM events dispatched via `Runtime.evaluate` work.
- `requestAnimationFrame` pacing under headless/backgrounded tabs isn't representative of a real foregrounded tab. Don't trust timing-sensitive observations from a headless capture without cross-checking.
