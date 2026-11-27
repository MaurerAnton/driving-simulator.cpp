# Where the port stands

The working list of the 2.0.0 push (started 2026-11-24, target ≈ 2026-11-28). Kept current by the lead;
`README.md` has the short version for readers.

## The goal, in the owner's words

A working game with as many of 1.7.7's features as fit, faster than 1.7.7. What players care about most:

1. **Regions:** one map – opened from the display, the in-car mini map or the M key – that shows every region
   there is and lets them pick a region or a place. No trip to the settings. Regions outside Hesse were never
   tested in 1.7.7: test them here.
2. **The autopilot and everything around it** (drive to a place, tours, watching it drive).
3. The rarely used vehicles (forklift, cranes, aeroplane …) can wait.

## What 2.0.0 contains (released 2026-11-24)

**From 1.7.7:** the map layer (every region), vehicle physics, traffic, signals and rules, the autopilot with trips,
tours, "drive there" and "turn around", lessons, the HUD with menus and settings, the maps (mini map, big map,
search, path drawing, transit-free layers), controls (keys, gamepad, touch stick, tilt), sound and speech, 28
languages; the scenery (trees, parked cars, signs, far terrain, facades, headlights); cars, trucks, buses,
articulated lorries, tractors with their trailers and the CE lessons; motorcycles; the finer car at "very high";
the cranes; the aeroplane with its autopilot; pedestrians on the pavements and crossings.

**New in 2.0:** one map for all regions (world layer, region cards, the region's schema before it is loaded, "Put the
car here" across maps); a light map style; the whole road network and every town name at every zoom; a search over
every town and loaded street plus online Photon search; aerial photographs for all of Germany (14 states' open DOP,
Sentinel-2 cloudless 2016 for Europe); regions' tiles fetched one by one by byte range; all 308 regions checked
(307 open – Duisburg's tiles are missing from map-assets).

**For 2.1 (not ported yet):** theory lessons and the theory exam (game/theory*, exam/*, ui/learn*), weather and seasons
(game/weather, render/weather, windscreen, season, dirt, snowtracks, grass, wipers), trams and buses by timetable
(sim/timetable, tramdrive, world/stops, buslines, render/trams, transit, stopmodels, ui/transitmap), roadworks,
narrowings, level crossings and signs with times (sim/roadevents, narrowings, levelcrossing, signtime, world/extrasigns),
the forklift and the practice-yard drills (sim/forklift, game/forklift*, drills, beams), replays and watching
(game/replay, spectate, showdrive, pointwatch), the stage and carousel menus, cyclists, emergency vehicles and
wildlife, water, wading, fuel and services, riding trams and buses as a passenger (render/passenger), the finer
trucks (fleetdetail), rear openings, multiplayer, the reality view, map packs, the version picker; people's shadows (needs a custom depth
material in the engine).  For 2.1.0 (on main): the weather and seasons, trams and buses by timetable, roadworks, narrowings, level crossings and
signs with times, the forklift, the class B drills and the headlights, the 3D theory lessons (game/theory*, render/theory*,
ui/learnplan), an experimental SDL3 build with a native canvas painter and HUD (docs/SDL.md); the theory exam, sign trainer, "spot the danger" and lesson log (game/exam, ui/learn; the 3D
theory scenes are hooks in ui/learnplan.h), getting out and walking, the doors, bonnet and boot in play, cars put down on the map
and getting into them (game_foot.cpp, spawncar; game_onfoot_test).  Known-failing tests at 2.0.0: `render_actors_test` (its reference dump of the original was
made without the people renderer; to be made anew), `game_bike_test` (the motorcycle starts in a car park and stands
after ~29 m, so the bend that should show the lean is ridden at walking pace – the scenario needs a street start).
`crane_play_test` (since the crash at exit is fixed – engine/gpu.h – it runs to its end: the keys A and W do not
reach the tower crane's controls – the jib does not slew, the hook does not rise; the crane panel reads the keys
itself, as in 1.7.7: to be found in 2.1).

## What 2.1.1 is to contain (started 2026-11-26)

A small patch: what the player sees first comparing 2.1.0 with 1.7.7, and the three known-failing tests. Nothing
from the 2.2 list comes into it – no replays, no riding as a passenger, no cyclists, emergency vehicles or wildlife,
no water, fuel or services, no finer trucks, no multiplayer, reality view, map packs or version picker.

(The checkout in `/home/user/ds2` stood at Release 2.0.0 and was 87 commits behind; pulled to `1de7455`, 2.1.0
published, before this list was made.)

### The four overlays of the driving frame: translated, never driven – wired since `ee95e3e`

In 1.7.7 they stand in one block of the frame method (`src/game/game.ts:6759-6787`); the port's frame
(`src/game/game_frame.cpp:567`) stops after drawing the vehicle's own glass. At v2.1.0 no file includes the three
headers – `src/ui/hudmirror.h`, `src/ui/queuelabels.h`, `src/ui/signaltimers.h` have no includer at all – and no
test names them. They compile in both builds only because CMake globs `src/*.cpp`.

| Piece | Lines | What the game layer has to call |
| --- | --- | --- |
| `ui/signaltimers.h` (`TimerHead`, `SignalTimers`) | 278 | `update(app.camera, w, h, uiScale, signalHeads->spots, walker ? pedPoles : null, signals)` while driving, no lesson, `settings.signalTimers` – else `hide()` (game.ts:6760) |
| `ui/queuelabels.h` (`QueueLabels`) | 178 | `update(app.camera, w, h, uiScale, traffic->cars, signals, settings.lang)` while driving with traffic, no lesson, `settings.queueLabels` – else `hide()` (game.ts:6766) |
| `ui/hudmirror.h` (`HudMirror`) | 221 | `frame({mirrors, cockpit, eye, eyeLocal, far, refresh, lang})` (game.ts:6769) |
| `ui/mirrorzoom.h` (`MirrorZoomPanel`) with `render/mirrorzoom.h` and `MirrorLook` of `game/cockpit.h` | 281 + 165 | `frame({mirrors, cockpit, eye, far, lang})` whenever there is input (game.ts:6780) |

Everything they need is already in the port: `app.canvas/renderer/scene/camera`, `eyeWorld`, `model.eye`,
`signalHeads->spots` (render/actors.h:128), `pedPoles` (game.h:330), `settings.uiScale`, `Input`.

**Done 27.11.** All four are called from the frame method now (`src/game/game_frame.cpp:620-655`), each made on its
first call and `hide()`n when its setting or the state says so, as in 1.7.7. Two things came back with the wiring and
were dead before:

- **The mirror key `Y` and the round mirror button.** `input.cpp:455` fires `mirror`, `ui/hudlayout.cpp:35` puts the
  button on the screen and `ui/mirrorzoom.cpp:67` answers the action – and now that the panel is built, both go round
  the mirrors (left → right → inside → none) as they do in 1.7.7.
- **Putting the inside mirror where the player wants it.** `Mirror::place()` (the rail shift and the size of
  `insideMirror`) is called again: mode `"hud"` takes the glass out of the car and draws the frame, dragged along the
  top of the picture and resized by its grip (0.6…2.0).

### The known-failing tests of 2.0.0 (checked again at `1de7455` on 26.11.)

All three are gone – two were failures of the November binaries, the third a stale test, not the port:

| Test | State at 2.1.0 | What had to be done |
| --- | --- | --- |
| `game_bike_test` | **passes** (leans 10.8° into the bend at 6.9 km/h) | nothing |
| `crane_play_test` | **passes** (jib slews 0.000 → 0.189, rope 12.00 → 9.23) | nothing |
| `render_actors_test` | **passes** (930 lines bit for bit, 26.11.) | nothing |

`render_actors_test` counted the people's and the emergency vehicles' groups as cars and still asserted a traffic
group without them; `src/render/actors.ts:56` makes both in the constructor. Its dump now filters them the way
`tools/diff/render_cars.ts` does, and the fixture was current all along – the dumper re-run against 1.7.7 gives it
byte for byte.

### Tests for what gets wired – `tests/ui_overlays_test.cpp` (new)

One file for all four (`LINK: ui render engine game input sim world platform core i18n geo audio`): the seconds a
light still shows what it shows and the colour that comes next, nothing over a light that is out of order, and the
pedestrians' light that only counts once somebody has pressed; the wording of a queue's label in English and German
(`120 m · 18 cars · wait about 45 s`) and four cars one after the other on a made-up road; the inside mirror's frame
in the HUD – put by its fractions of the picture, dragged, made larger by its grip, clamped to 0.6…2.0, and the glass
back in the car again; and looking into a mirror large: the round button and the mirror key going round the mirrors,
the × and a second tap putting the panel away, a tap on the glass.  Everything that needs a picture is asked only
when `renderer.available()`, and both mirror parts are left out when no GL context can be made at all.

### Trams (checked 27.11.)

`tests/tram_test.cpp` (origin `tests/game/tram.test.ts @ v1.7.7`) passes at `ee95e3e`: power and brake, the stop with
its doors and passengers, the way at a switch as the indicator asks, and what the instructor says.  Trams drive –
there is no stub left anywhere in `sim/tramdrive.cpp`, `sim/rail.cpp`, `render/trams.cpp` or `render/fleet.cpp`
(`TramCab`), and `game/vehicles.cpp` gives the tram its own kind out of `BUS_DEF`.

### Order

1. ✔ `signalTimers` and `queueLabels`, wired in `game_frame.cpp` (`7cf1ac6`).
2. ✔ `HudMirror` with `Mirror::place()` for shift and size, the glass out of the car for mode `"hud"`, and
   `MirrorZoomPanel` bringing the mirror key and the round button back.
3. ✔ The known-failing tests: `tests/known-failing.txt` is empty again; `render_actors_test` mends its own filter
   (`ee95e3e`), `game_bike_test` and `crane_play_test` passed all along.
4. ✔ The new stage test `tests/ui_overlays_test.cpp` – five checks pass.
5. ✔ The 2.1.1 entry in the release notes: an intro and four items in all 28 languages (`i18n-extra/<code>.json`,
   then `node tools/i18n-export.mjs`), stamp `a6a94bfc9283`.

## Who does what (branches `w/<name>`, worktrees `/home/user/ds2w/<name>`)

| Branch | Part | State |
| --- | --- | --- |
| main (lead) | foundation, renderer, hosts, the App with regions (files from map-assets, tiles by byte range), deploy, the browser's entry page | running |
| w/engine | maths, scene graph, geometries, curves – all matching three.js r187 | ✔ merged |
| w/i18n | 28 languages as JSON (`web/lang`), `tr()`, Intl hooks | ✔ merged |
| w/outline | country outlines for the world map (`web/data/outline.bin`, Natural Earth) | ✔ merged |
| w/world | map layer at v1.7.7 with typed tile sections, loader | ✔ merged · differential test finishing |
| w/sim | vehicle ✔ (matches the original) · signals, traffic, rules, autodriver | adapting to the merged map layer |
| w/audio | Web Audio layer, engine sounds, music, speech | finishing |
| w/game | settings, vehicles, nav, address, tour … | running |
| w/render-scene | props, parked, signs, signart, pedlights, far, facadedetail, headlights | finishing |
| w/render-cars | car, carBody, instruments, mirrors, actors, navscreen | running |
| w/input | `input/*`, touch stick, tilt steering, screen and world taps | started 24.11. |
| w/hud | `ui/hud` and the small pieces of the display (layout, mirrors, notices, lesson start …) | started 24.11. |
| w/maps | mini map, big map, search, path drawing … and **one map for all regions** (`docs/WORLD-MAP.md`) | started 24.11. |
| w/hub-a | `game/game.ts`: the class, boot, menu, driving loop, frame, cameras (`game.h`, `game.cpp`, `game_drive.cpp`, `game_frame.cpp`) | started 24.11. |
| w/hub-b | `game/game.ts`: the map and the autopilot's side – trips, tours, teleport, drive there, turn around (`game_trips.cpp`, `game_map.cpp`) | started 24.11. |

## Order of bringing it to the screen

1. ✔ Renderer and hosts proven (native GPU and headless browser), test site deployed.
2. ✔ The real map drawn natively (`tools/t.sh view out.png`): 36 tiles, 8326 roads; 71 draw calls and 625 000
   triangles at 1280×720 with shadows – 4–7 frames a second on the tablet's graphics chip *while a dozen builds
   ran beside it*; 1.7.7 made 1 in the same place. Looking down (16 calls, 237 000 triangles): 34.
3. ✔ The same in the browser, deployed (https://maureranton.github.io/driving-simulator.cpp/): the map under a free
   camera, any region by `?region=<id>` or the list on the page – tiles fetched one by one out of the regions' parts
   (Feldkirch, Austria: open in 0.9 s). `tools/t.sh regions_check` opens every region natively.
4. ✔ 24.11.: every helper branch merged; the whole game builds natively and in the browser, all 66 tests pass in CI, and
   the test site runs the C++ game: menu, free drive with traffic, HUD, mini map.  `autopilot_drive_test` drives
   1 km of a trip to Elgershausen on the real map by itself (30 km/h on average, never off its lane).
5. Traffic, signals, signs, rules; menu and lessons; sound.
6. Autopilot trips, big map, the world layer with all regions; region switching; a tool that opens every region.
7. Speed work on real phones; README progress table; release notes; 2.0.0.

## Regions (checked 24.11. with `tools/t.sh regions_check`, every region opened natively)

307 of 308 regions open and draw.  Köln's entry in 1.7.7's `regions.json` names the wrong part (62, its tiles are in
32) – corrected on the port's side by `web/parts/regions.json` (`node tools/build-region-parts.mjs`, run it again when
map-assets changes).  **Duisburg (`nrw-duisburg`): its tiles are in no part of map-assets at all** – its roads load,
the ground does not; only a new map build can mend it.  (The check tool itself crashes after ~150 regions in one
process – something of the App is not given back; run it in batches with `--from N --count 20`.)

## Notes for whoever continues

- Merge helper branches with `git merge` (never `pull --rebase` over a merge: it replays the branch).
- After merging render branches: `node tools/gen-game-glsl.mjs` and commit the regenerated `src/render/glsl_gen.cpp`.
- `FAHRSIM_LAYERS` in `.github/workflows/build.yml` names the folders of `src/` the browser build takes while the
  port is brought up: add a layer when it compiles as a whole, remove the option when all do.
- The browser test loop: `node tools/dev/serve.mjs` (leave it running) and `node tools/dev/shot.mjs "" out.png`;
  headless Chromium needs about a minute per picture on the tablet.
- Builds take turns on this machine (a lock in `tools/env.sh`); the compiler's results are shared by all
  worktrees (ccache, paths relative to the copy's root – `CMakeLists.txt`).
- New texts of 2.0 (not in 1.7.7's dictionaries) live in `i18n-extra/<code>.json`, all 28 languages.
- The native picture loop is much quicker: `tools/t.sh render_probe out.png`, later `tools/t.sh view out.png`.
