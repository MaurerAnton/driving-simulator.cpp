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

## Scope of 2.0.0 (the original's files, by state)

**In:** `world/*` (map, lanes, routes, tiles, regions) · `sim/` vehicle, ground, collision, signals, traffic,
rules, autodriver, follow, clearlanes, pastblock, smoothpose, queues, arrows, trailer · `game/` game (core),
app, settings, vehicles, nav, address, tour, tourlog, spawnplace, lessonplaces, routepresets, track, faultlog,
clock, history, turnaround, arcadedrive, cockpit, takeover, flycam, releases · `render/` tiles, materials, sky,
props, parked, signs, signart, pedlights, far, facadedetail, headlights, car, carBody, instruments, mirrors,
navscreen, actors · `ui/` hud, minimap, bigmap (+ the new world layer with all regions), mapwindow, maplayers,
placesearch, addresspicker, pathdraw, regions, geoip, geolocate, notices, viewbuttons, closex, screentap,
worldtap, touchstick, tiltsteer, lessonstart, lessonroutes · `input/*` · `audio/` audio, waves, profiles,
compose, music, speech, voicepick · `i18n` (all 28 languages, as data).

**Later (hooks left in the code):** pedestrians, cyclists, trams and buses with timetables, emergency vehicles,
roadworks, level crossings, wildlife, weather and seasons, water, wipers, fuel and car wash, the other vehicles'
models (truck, bus, motorcycles, tractor), trailers' lessons (CE), cranes, forklift, aeroplane, walking, theory
lessons and the theory exam, replays, watching other vehicles, the stage and carousel menus, the reality view,
multiplayer and the server, map packs, developer map modes, old-version switch.

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
