# Squirms

A Worms 2-style artillery game with destructible, collapsing physics terrain. Teams of pink worms take turns lobbing bazookas, bananas and holy hand grenades across procedurally generated islands. Blasts carve round craters, and any chunk of land cut loose falls as a real rigid body and lands where it lands. Written in C++ with raylib and Box2D and compiled to WebAssembly with Emscripten for play in the browser.

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

The game is entirely self-contained C++. `index.html` is just a `<canvas id="canvas">` plus the Emscripten-generated glue script; raylib/GLFW3 own the canvas, input, audio and rendering directly, and the whole game loop runs in WASM. There are no image or sound files: every sprite is drawn procedurally and every sound effect is synthesized at startup. The only asset is the Montserrat font (`assets/fonts`, SIL OFL), which is embedded into the build.

## Setting up a match

The start menu (arrow keys or mouse) sets:

| Option | Values |
|--------|--------|
| Teams | 2–4 |
| Opponents | Computer (team 1 is you, the rest are CPU) or Hot-seat (everyone human) |
| Worms per team | 1–8 |
| Worm energy | 50 / 100 / 150 / 200 |
| Landscape | Meadow, Desert, Arctic, Hell, or random |
| Map seed | type digits for a reproducible map, R to reroll |

A live preview shows the island the seed will generate.

## Controls

| Key | Action |
|-----|--------|
| ← → / A D | Walk |
| ↑ ↓ / W S | Aim |
| Space (hold) | Charge and fire (Space again detonates the sheep; shoots/lets go of the rope) |
| Enter | Jump (lets go of the rope) |
| Backspace | Backflip |
| 1–5 | Grenade / cluster / banana fuse |
| Tab / right-click | Weapon panel |
| Left-click | Pick a target (air strike, teleport) |
| Drag / mouse wheel | Look around / zoom |
| H | Controls help |
| Esc | Quit to menu |
| F1 | Debug overlay; while it's on, click to blow a hole anywhere |

## Turns

Each turn gives the active worm 45 seconds to move, aim and take its shot. After firing there are 4 seconds to retreat. Then everything settles: shells land, slabs stop falling, flung worms come to rest, and health counters tick down. Worms reduced to 0 HP blow up one by one and leave gravestones. Taking damage ends your turn. Falling into the sea drowns a worm outright. The last team standing wins.

Supply crates parachute in from the second round on. Health crates give +25 HP; weapon crates add ammo for a special weapon. Oil barrels explode when hit (and set each other off), and mines trip when a worm wanders close.

## Weapons

| Weapon | Ammo | Notes |
|--------|------|-------|
| Bazooka | ∞ | Explodes on impact, pushed by the wind |
| Grenade | ∞ | Bouncy, 1–5 s fuse |
| Cluster Bomb | 4 | Bursts into five bomblets |
| Banana Bomb | 1 | Five more bananas, each a big bang |
| Holy Hand Grenade | 1 | Huge blast, with a hallelujah |
| Dynamite | 2 | Drop and run, 5 s fuse |
| Mine | 2 | Arms after 2 s, trips on worms |
| Sheep | 1 | Waddles and hops; Space to detonate |
| Shotgun | ∞ | Two hitscan shots per turn |
| Baseball Bat | 1 | Knocks worms flying |
| Ninja Rope | ∞ | Swing around the map; doesn't end your turn |
| Air Strike | 1 | Click a target: five missiles from the sky |
| Teleport | 2 | Click anywhere open; uses up the turn |
| Skip Go | ∞ | Pass |

## Terrain

The island is a grid of 10 px jittered convex chunks, each a Box2D polygon (only exposed chunks get a static body), and it behaves like material rather than scenery:

- **Craters.** A blast destroys every chunk whose centre is inside the crater. Rock is tougher and only breaks near the middle.
- **Ejecta.** Chunks around the rim are torn loose and flung out as real, tumbling pieces of terrain.
- **Structural support.** Every chunk needs a load path down to the bedrock floor. Resting on supported ground is free, each sideways step is a cantilever (cheap through thick terrain and rock, expensive through thin dirt ledges), and hanging from above costs more. Anything past the span limit breaks off as a slab. So long thin ledges snap, bridges fail when you cut them, cave roofs hold only while they're thick enough, and shooting out a rock pillar brings down whatever it was holding up. The map generator applies the same rule, propping over-long spans with natural rock pillars, so a fresh map starts stable.
- **Falling slabs.** Loose terrain falls as rigid bodies, crushes worms it lands on ("SQUASH!"), kicks up dust, and big slabs that land hard shatter into smaller pieces. Anything that drops into the sea sinks.
- **Rubble.** Once a piece comes to rest it freezes where it landed and becomes solid ground you can stand on, blow up or rope onto. It's baked into the terrain texture, so piles blend in with the landscape. Rubble falls again if what it's resting on is destroyed, and nearby blasts shake it loose.

The landscape is drawn from a cached texture rather than one polygon per chunk. It's painted once per map (soil, rock pockets, pebbles, a grass/sand/snow/lava crust), and its outline is smoothed so the 10 px grid reads as organic, Worms-like ground. Destroyed chunks are erased and landed rubble baked in with partial texture uploads. Falling chunks sample the same texture, so they carry their own patch of soil and grass with them.

## Ninja Rope

Select it from the weapon panel, aim, and press Space. The hook flies out along the crosshair and catches solid ground within reach. Left/Right swing, Up/Down climb, and Enter (or Space with the rope still selected) lets go. You can fire it again in mid-air. The rope wraps around corners and unwraps as you swing back. It snaps if the ground it's hooked to is blown away or starts to fall. Roping doesn't use up your turn: swing into position, then pick a weapon and fire, even while hanging.

## Tuning Constants

| Constant | File | Default | What it controls |
|----------|------|---------|------------------|
| `CELL` | terrain/grid.h | 10 | Chunk size in px |
| `WATER_Y` | terrain/grid.h | world height − 120 | Sea level (px) |
| `MAX_DYNAMIC_BODIES` | terrain/terrain.h | 120 | Slab freeze guard rail |
| `REFREEZE_THRESHOLD` | terrain/terrain.h | 0.75 | Seconds between refreeze checks |
| `SLAB_CHUNK_CAP` | terrain/terrain.h | 300 | Max chunks per dynamic body |
| `SMOOTH_R` | render/terrain_render.cpp | 4 | Terrain outline smoothing radius (px) |
| `MAX_SPAN` | terrain/support.h | 60 | Structural limit (see `sideCost`/`hangCost` in support.cpp) |
| `SHATTER_DV` | terrain/terrain.h | 5.5 | Landing jolt (m/s) that breaks a big slab apart |
| `TURN_SECONDS` etc. | game/game.cpp | 45 | Turn, retreat and settle timings |

## Architecture

```
src/
  main.cpp                 Window, start menu + map preview, fixed-timestep loop
  core/rng.*, math.*       Seeded RNG (mulberry32), small math helpers
  terrain/noise.*          Gradient noise + fBm
  terrain/generate.*       Island generator (heightfield, overhangs, tunnels, caverns)
  terrain/grid.*           Jittered convex quad chunks; world/sea constants
  terrain/support.*        Structural support model (load paths to bedrock)
  terrain/terrain.*        TerrainSystem (fixtures, thaw, ejecta, shattering, refreeze, rubble, sinking)
  physics/world.*          Box2D wrapper (bodies, fixtures, filters, ray casts, contacts)
  entities/worm.*          Worm: capsule body, walking/step-up, jumps, damage, drowning
  entities/projectile.*    Shells, grenades, bananas, dynamite, the sheep
  entities/prop.*          Barrels, mines, crates, gravestones
  weapons/weapons.*        Weapon table (names, ammo, fire modes)
  game/game.*              Game state, turn phases, input, camera follow
  game/weapons.cpp         Firing, projectile behaviour, explosions
  game/props.cpp           Barrels, mines, crate drops and pick-ups
  game/rope.cpp            Ninja rope (shoot, swing, climb, wrap round corners)
  game/ai.cpp              Computer opponents (trajectory search)
  game/camera.*            Pan/zoom/shake camera
  render/terrain_render.*  Cached terrain texture, crater updates, loose chunks
  render/renderer.*        Sky, parallax backdrop, sea, weather, world sprites, labels
  render/sprites.*         Procedural worm / weapon / prop drawings
  render/fx.*              Particles, damage numbers, comic "POW!" bursts, shake
  render/hud.*             Timer, team bars, wind, weapon panel, overlays
  render/text.*            Font loading and outlined text
  render/theme.*           Landscape themes
  audio/sfx.*              Procedurally synthesized sound effects
assets/fonts/              Montserrat (SIL OFL), embedded into the WASM build
vendor/                    Prebuilt raylib + Box2D 2.4 static libs (WASM)
```
