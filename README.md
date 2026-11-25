# Fahrsim Baunatal 2 — the C++ rewrite

A rewrite of [Fahrsim Baunatal](https://github.com/MaurerAnton/driving-simulator) – the free 3D driving-school
simulator on real road networks – in C++, compiled to WebAssembly. The aim is speed on weak devices: the same
game, the same maps, the same picture, with less work for the processor and no pauses for garbage collection.

**Test builds:** <https://maureranton.github.io/driving-simulator.cpp/> (every push to `main`).
**The current game (1.7.7, TypeScript):** <https://maureranton.github.io/driving-simulator/>.

> **Status: being brought up.** The foundation is in place (see below); the game itself is being moved onto it.
> What works and what is still missing is listed under [Progress](#progress), updated as it changes.

## How it is built

- **The original at v1.7.7 is the specification.** Every C++ file is a translation of the TypeScript file of the
  same path and name (`src/world/lanes.ts` → `src/world/lanes.{h,cpp}`), with the same names, numbers and rules,
  and is checked against the original where it computes something: the same inputs must give the same numbers.
- **A renderer of our own** (`src/engine`) draws with WebGL 2 – with three.js's own shaders, so the picture is the
  original's – but uploads every array once, leaves out what is outside the picture before anything reaches the
  graphics card, and sorts its draws so that programs and materials change rarely.
- **The page is still the page.** Menus and the display are real HTML elements with the original's style sheets,
  built from C++ through a thin layer (`src/platform/dom.h`); texts stay in the original's 28 languages.
- **One program, two hosts:** the browser (Emscripten), and this machine (tests, tools, pictures drawn off screen
  with OpenGL ES) – nothing in the game calls a browser API directly.
- **Regional maps without the big download:** 1.7.7 fetches a whole archive of 20–100 MB before it can show a
  region; here only the tiles that are needed are fetched, each with a byte-range request (`src/world/regiontiles`).

[docs/PORT.md](docs/PORT.md) is the working guide: layout, the TypeScript → C++ rules, how to build, test and
compare with the original.

## Building

```bash
# tests and tools on this machine
tools/check.sh                       # build everything, run every test
tools/t.sh lanes_test                # build and run one test or tool

# the browser build
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -G Ninja && cmake --build build-web     # → build-web/site/
node tools/dev/serve.mjs             # serves it on http://127.0.0.1:5180/ with the map of the original's checkout
node tools/dev/shot.mjs "" shot.png  # a picture of it from a headless browser
```

Needs: a C++26 compiler (GCC ≥ 14, Clang ≥ 17, Emscripten ≥ 3.1.74), CMake ≥ 3.25, Ninja, zlib, libpng and OpenGL ES 3 with EGL for the native side;
Emscripten 3.1.74 for the browser build; Node ≥ 24 for the tools.

## Progress

| Part | State |
| --- | --- |
| Host layer: page, files, storage, clock, 2D canvas, graphics context – in the browser and natively | works, tested in a headless browser |
| Renderer with three.js's shaders, shadow map, culling | written; every material's program compiles on the test GPU |
| Deploy to GitHub Pages on every push | works |
| Regional maps: index of where each tile lies in the 376 archives | done (`web/parts`) |
| Map: road network in pieces, lanes, routes, tiles (v1.7.7) | in progress |
| Driving: vehicle dynamics, traffic, signals, rules, the autopilot's driver (v1.7.7) | in progress |
| Scene graph, geometries, maths (three.js r187) | maths done and checked against three.js; the rest in progress |
| The 28 languages as data | in progress |
| Sound | in progress |
| Settings, navigation, lessons, vehicle definitions | in progress |
| The picture: tiles, sky, vehicles, signs | tiles and sky translated; the rest to do |
| The game: menu, driving, cameras, display, big map with all regions, autopilot trips | to do |

## Licence

AGPL-3.0-only, as the original (see [LICENSE](LICENSE)). Notices for the map data, three.js and other material:
[NOTICE.md](NOTICE.md).
