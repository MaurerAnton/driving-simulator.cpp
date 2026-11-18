// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Entry point of the C++ port of Fahrsim Baunatal.
 *
 * Origin: src/main.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0) —
 * the TypeScript boot path, translated to C++. The game logic lives in
 * src/game/, the engine/renderer (three.js equivalent) in src/engine/.
 *
 * The WASM module is built by Emscripten (CMakeLists.txt + .github/workflows)
 * and, at runtime, talks to the browser through Emscripten's HTML5 library
 * (canvas, input, timers) — no JavaScript game code runs in the page.
 */

#include <cstdio>

int main() {
  std::printf("fahrsim-cpp: boot\n");
  return 0;
}
