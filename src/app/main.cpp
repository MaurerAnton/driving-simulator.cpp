// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Entry point of the C++ port of Fahrsim Baunatal.
 *
 * Origin: src/main.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0) —
 * the TypeScript boot path, translated to C++.  The game logic lives in
 * src/game/, the engine/renderer in src/engine/, the road network in
 * src/world/.
 *
 * The WASM module is built by Emscripten (CMakeLists.txt +
 * .github/workflows) and, at runtime, talks to the browser through
 * Emscripten's HTML5 library (canvas, input, timers) — no JavaScript game
 * code runs in the page.
 *
 * The original reads `?lang=` and the settings' language, checks WebGL 2 and
 * then boots the Game; here the same sequence is expressed with the pieces
 * that exist so far, and grows as the port does.
 */
#include <cstdio>
#include <string>

#include "game/i18n.h"
#include "game/settings.h"

int main() {
  // (how the original picks its language: ?lang=, else the saved setting)
  fs::Settings settings = fs::loadSettings();
  fs::Lang lang = settings.lang == fs::LangSetting::En ? fs::Lang::En : fs::Lang::De;

  std::printf("fahrsim-cpp: %s\n", fs::tr("title", lang).c_str());
  std::printf("%s\n", fs::tr("subtitle", lang).c_str());
  return 0;
}
