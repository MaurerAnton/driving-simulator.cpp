# Fahrsim Baunatal — C++ port (Emscripten/WASM)

C++ translation of [MaurerAnton/driving-simulator](https://github.com/MaurerAnton/driving-simulator)
("Fahrsim Baunatal", a free 3D driving-school simulator for North Hesse). The game
logic, rendering (a three.js-equivalent engine in C++), audio, input, i18n and
networking are translated to **pure C++** and compiled to **WebAssembly** with
[Emscripten](https://emscripten.org). No JavaScript game code runs in the page.

**Upstream has moved on:** the source repo reached **1.7.2** (194,547 lines of
TypeScript) while this port was being written; the parity table below is
measured against every release, and the port follows the same order.

**Status: in progress.** The port is built incrementally, upstream version by
upstream version (v0.1.0 → v1.6.0), starting from the entry point
(`src/main.ts` at `5b6ddc7`). Each upstream commit's state is translated with
real code only — no stubs, no placeholders.

## Layout

```
src/main.cpp        entry point (origin: src/main.ts)
src/app/            entry point (origin: src/main.ts)
src/audio/          synthesised sound graph (origin: src/audio/audio.ts)
src/core/           2D ground-plane geometry (origin: src/core/geom.ts)
src/engine/         three.js-equivalent renderer: Scene, Mesh, materials,
                    geometries, math (Vector/Matrix/Quaternion), WebGL2 backend
src/game/           the simulator (origin: src/game/*.ts)
src/geo/            UTM32/world-frame conversion (origin: src/geo/utm.ts)
src/input/          driver input (origin: src/input/input.ts)
src/platform/       software 2D canvas, host interfaces (HudHost, TileSource, …)
src/render/         the scene: sky, tiles, props, signs, cars, mirrors, HUD
src/sim/            vehicle dynamics, traffic, rules, signals, collisions
src/ui/             menus and HUD (origin: src/ui/hud.ts)
src/world/          road network, lanes, tile container (origin: src/world/*.ts)
tools/parity.py     LOC parity report (TS original vs C++ port)
tools/versions.tsv  upstream version list + per-version TS line counts
.github/workflows/  CI: Emscripten build + native build + parity report
```

Every translated file carries an `// Origin: src/…` header naming the TypeScript
file it comes from.

## Parity

| version | upstream commit | TS LOC | C++ LOC | parity |
|---------|-----------------|-------:|--------:|-------:|
| 0.1.0   | `5b6ddc7`       | 10,819 | 14,635 | 1.35 |
| 0.2.0   | `0e593a7`       | 10,819 | — | no source change (sign graphics only) |
| 0.3.0   | `633e0ca`       | 11,026 | — | done |
| 0.4.0   | `578e397`       | 12,847 | 17,555 | 1.37 |
| 0.5.0   | `1c08919`       | 13,710 | — | — |
| 0.6.0   | `408014b`       | 22,817 | — | — |
| 0.7.0   | `ff37c5d`       | 32,933 | — | — |
| 0.8.0   | `3420ad0`       | 37,418 | — | — |
| 0.9.0   | `9d37870`       | 37,452 | — | — |
| 1.0.0   | `v1.0.0`        | 42,762 | — | — |
| 1.1.0   | `v1.1.0`        | 45,763 | — | — |
| 1.2.0   | `v1.2.0`        | 61,331 | — | — |
| 1.3.0   | `v1.3.0`        | 66,252 | — | — |
| 1.4.0   | `v1.4.0`        | 120,012 | — | — |
| 1.5.0   | `v1.5.0`        | 137,788 | — | — |
| 1.6.0   | `v1.6.0`        | 146,760 | — | — |
| 1.7.0   | `925e08c`       | 193,775 | — | — |
| 1.7.1   | `2648b83`       | 194,062 | — | — |
| 1.7.2   | `f68dd99`       | 194,547 | — | — |

Run `python3 tools/parity.py` for the live numbers (needs the read-only
`driving-simulator` checkout for exact per-commit counts, or falls back to the
recorded values in `tools/versions.tsv`).

The 0.1.0 translation is complete: all 35 upstream files are present as C++
counterparts. The C++ total is larger because the port also carries the
three.js-equivalent engine (the TypeScript game gets that from the `three`
package) and the platform layer that replaces the DOM and the canvas.

## Build

```bash
# WebAssembly (Emscripten)
emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# → build/fahrsim.js + build/fahrsim.wasm

# Native (console smoke build, also used by CI)
cmake -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native
ctest --test-dir build-native --output-on-failure
```

CI (`.github/workflows/build.yml`) sets up Emscripten, builds the WASM, runs
the native unit tests and uploads the artifact on every push.

## Licence

AGPL-3.0-only — same as the original (see [LICENSE](LICENSE)).
Map data attribution and other notices follow the original project's
`NOTICE.md`.
