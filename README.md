# Fahrsim Baunatal — C++ port (Emscripten/WASM)

C++ translation of [MaurerAnton/driving-simulator](https://github.com/MaurerAnton/driving-simulator)
("Fahrsim Baunatal", a free 3D driving-school simulator for North Hesse). The game
logic, rendering (a three.js-equivalent engine in C++), audio, input, i18n and
networking are translated to **pure C++** and compiled to **WebAssembly** with
[Emscripten](https://emscripten.org). No JavaScript game code runs in the page.

**Status: in progress.** The port is built incrementally, upstream version by
upstream version (v0.1.0 → v1.6.0), starting from the entry point
(`src/main.ts` at `5b6ddc7`). Each upstream commit's state is translated with
real code only — no stubs, no placeholders.

## Layout

```
src/main.cpp        entry point (origin: src/main.ts)
src/engine/         three.js-equivalent renderer: Scene, Mesh, materials,
                    geometries, math (Vector/Matrix/Quaternion), WebGL2 backend
src/game/           the simulator (origin: src/game/*.ts)
src/i18n/           translation data (origin: src/i18n/*.ts)
tools/parity.py     LOC parity report (TS original vs C++ port)
tools/versions.tsv  upstream version list + per-version TS line counts
.github/workflows/  CI: Emscripten build + parity report
```

Every translated file carries an `// Origin: src/…` header naming the TypeScript
file it comes from.

## Parity

| version | upstream commit | TS LOC | C++ LOC | parity |
|---------|-----------------|-------:|--------:|-------:|
| 0.1.0   | `5b6ddc7`       | 10,819 | — | — |
| 0.2.0   | `0e593a7`       | 10,819 | — | — |
| 0.3.0   | `633e0ca`       | 11,026 | — | — |
| 0.4.0   | `578e397`       | 12,847 | — | — |
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

Run `python3 tools/parity.py` for the live numbers (needs the read-only
`driving-simulator` checkout for exact per-commit counts, or falls back to the
recorded values in `tools/versions.tsv`).

## Build

```bash
# WebAssembly (Emscripten)
emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# → build/fahrsim.js + build/fahrsim.wasm

# Native (console smoke build, also used by CI)
cmake -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native
```

CI (`.github/workflows/build.yml`) sets up Emscripten, builds the WASM and
uploads it as an artifact on every push.

## Licence

AGPL-3.0-only — same as the original (see [LICENSE](LICENSE)).
Map data attribution and other notices follow the original project's
`NOTICE.md`.
