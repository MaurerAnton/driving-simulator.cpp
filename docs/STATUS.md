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
| main (lead) | foundation, renderer, hosts, regions' tiles, deploy; then `game.cpp`, the world layer of the big map | running |
| w/engine | maths ✔ merged · scene graph ✔ merged · geometries, curves | finishing |
| w/world | map layer at v1.7.7 with typed tile sections, loader, differential test | running |
| w/sim | vehicle ✔ (matches the original) · signals, traffic, rules, autodriver | running |
| w/i18n | 28 languages as JSON, `tr()`, Intl hooks | running |
| w/audio | Web Audio layer, engine sounds, speech | running |
| w/game | settings, vehicles, nav, address, tour … | running |
| w/outline | country outlines for the world map (`web/data/outline.bin`) | running |
| w/render-scene | props, parked, signs, signart, pedlights, far, facadedetail, headlights | running |
| w/render-cars | car, carBody, instruments, mirrors, actors, navscreen | running |
| (next) | input · ui/hud · ui maps (minimap, bigmap …) · game hub translation | to start when their headers are merged |

## Order of bringing it to the screen

1. ✔ Renderer and hosts proven (native GPU and headless browser), test site deployed.
2. Merge w/world → `pending/render/tiles`, `pending/render/sky`, `pending/game/app` and `pending/tools/view.cpp`
   move back into `src/` → `tools/t.sh view out.png` shows the real map; frame time and draw calls measured.
3. The same in the browser (main_web with App); deploy.
4. The car: vehicle + ground + chase camera + keys; then touch controls; then the display (hud).
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
- The native picture loop is much quicker: `tools/t.sh render_probe out.png`, later `tools/t.sh view out.png`.
