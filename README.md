# Squirms

A Worms 2-style artillery game with destructible, collapsing physics terrain. Teams of pink worms take turns lobbing bazookas, bananas and holy hand grenades across procedurally generated islands. Blasts carve round craters, and any chunk of land cut loose falls as a real rigid body and lands where it lands. Written in C++ with raylib and Box2D and compiled to WebAssembly with Emscripten for play in the browser.

## Build & Run

Requires the Emscripten SDK installed locally at `emsdk/` (not tracked in git — clone it from https://github.com/emscripten-core/emsdk and run `./emsdk install latest && ./emsdk activate latest`).

```bash
source emsdk/emsdk_env.sh
emcmake cmake -B build
cmake --build build --target stage
node server/server.mjs  # Node 22+, then open http://localhost:8080
```

Gameplay, physics, procedural graphics and synthesized audio run in C++/WASM. The browser shell (`web/`) adds online pairing, video transport, sound preferences and fullscreen controls. There are no image or sound files; the Montserrat font (`assets/fonts`, SIL OFL) is embedded into the build. `stage` assembles the complete playable bundle in `dist/`. Static hosting still supports computer and hot-seat matches; online rooms need the included server.

## Online multiplayer

1. Both players open the same running Squirms server in a desktop browser.
2. The host chooses the map, worms and energy in the menu, then selects **Play online → Create a room**.
3. Send the invite link to a friend. They open it and select **Join**. The match starts automatically with two human teams: host is team 1, guest is team 2. The seed determines which team goes first.
4. Use the normal keyboard/mouse controls on your turn. **Play online → Leave online session** returns to the menu. Create a new room for a rematch.

Online mode is **host-authoritative remote play**: the host runs the entire simulation and streams the canvas at up to 30 FPS over WebRTC. The guest sends input over an ordered data channel and synthesizes received sound events locally. Both players see the same camera, terrain and explosions; physics cannot drift apart. Guest video quality and control latency depend on the connection and host machine. Keep the host tab visible. A lost heartbeat pauses the simulation; a closed connection requires a new room. There is no reconnect-to-match, host migration, spectator mode or matchmaking. Debug terrain editing is disabled online.

Sound settings provide separate effects and music/ambience sliders, plus mute, saved per browser. No microphone or camera access is requested.

### Hosting for friends

The server binds to localhost by default. For a LAN, use `HOST=0.0.0.0 PORT=8080 node server/server.mjs` and share your machine's LAN address. For Internet play, place the server behind an HTTPS reverse proxy that preserves the Host header. Both players must reach that public URL; localhost links only work on the same machine. Serve the page and `/api/` from the same origin.

The default ICE configuration uses Google's public STUN server. Some networks require TURN. Supply a JSON array of WebRTC ICE servers when starting the server, for example:

```bash
ICE_SERVERS='[{"urls":"stun:stun.example.com:3478"},{"urls":"turns:turn.example.com:5349","username":"temporary-user","credential":"temporary-password"}]' HOST=0.0.0.0 node server/server.mjs
```

ICE settings are sent to browsers, so use short-lived TURN credentials from your relay provider. The signaling service stores room offers/answers in memory for ten minutes, permits one guest per room, limits room creation and message size, and uses separate host-management secrets. Restarting it clears waiting rooms; established peer connections continue. Room links are bearer invitations: anyone with the link can take the guest slot. This small server is intended for private friend matches; it does not include user accounts or a public lobby.

The browser APIs used are [canvas captureStream](https://developer.mozilla.org/en-US/docs/Web/API/HTMLCanvasElement/captureStream) and [RTCPeerConnection.addTrack](https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/addTrack). Game exports use [EMSCRIPTEN_KEEPALIVE](https://emscripten.org/docs/api_reference/emscripten.h.html#c.EMSCRIPTEN_KEEPALIVE).

## Verification

```bash
cmake --build build --target stage
node --test tests/server.test.mjs
# Boundary tests run natively against minimal raylib stubs:
g++ -std=c++17 -Wall -Wextra -Isrc -Ivendor/raylib/include tests/input_test.cpp src/net/input.cpp -o /tmp/squirms-input-test
/tmp/squirms-input-test
# Requires google-chrome on PATH (or set CHROME); launches two isolated browsers:
node --test tests/browser.test.mjs
# Optional visible-browser pass (requires a desktop session):
HEADFUL=1 node --test --test-name-pattern='local weapons' tests/browser.test.mjs
```

The browser test uses actual WASM, direct local WebRTC, different window sizes, turn ownership, weapon selection/firing, remote sound events and disconnect cleanup. It writes screenshots to the system temporary directory. Check timing and audio in foreground browser tabs as well; headless rendering does not reproduce real display pacing.

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
  audio/sfx.*              Synthesized effects, ambient score, wind and audio mix
  net/input.*              Host-side input ownership and remote input validation
web/                       Browser shell, preferences and WebRTC transport
server/server.mjs          Static hosting and short-lived room signaling (Node)
tests/                     Server, input-boundary and two-browser integration checks
assets/fonts/              Montserrat (SIL OFL), embedded into the WASM build
vendor/                    Prebuilt raylib + Box2D 2.4 static libs (WASM)
```
