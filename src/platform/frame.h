// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The browser's next picture: `requestAnimationFrame` / `cancelAnimationFrame` – for what
 * draws itself only when something changed (the big map), beside the game's own loop.
 *
 * Two hosts implement it: the browser (platform/web: window.requestAnimationFrame) and this
 * machine (platform/native/maps_native.cpp: the callbacks wait until a test or a tool calls
 * plat::native::runFrames() – maps_native.h).
 */
#pragma once

#include <functional>

namespace fs::plat {

/** window.requestAnimationFrame(fn): `fn(performance.now())` before the next picture, once.  Returns what cancelAnimationFrame takes (never 0). */
int requestAnimationFrame(std::function<void(double now)> fn);
/** window.cancelAnimationFrame(id) (0, or one that has run: nothing) */
void cancelAnimationFrame(int id);

}  // namespace fs::plat
