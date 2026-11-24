// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Timers that can be taken back: window.setTimeout / clearTimeout and
 * setInterval / clearInterval of the original, as the music's scheduler and the
 * spoken instructions use them (no file of the original: these are the browser's).
 *
 * The host's clock (platform/plat.h) only has a timer that cannot be stopped;
 * these are built on it – a timer that was cleared still comes due there and then
 * finds nothing to do.  Natively they run from plat::native::pump().
 */
#pragma once

#include <functional>

namespace fs::timers {

/** setTimeout(fn, ms): the number clearTimeout takes (never 0) */
int setTimeout(std::function<void()> fn, double ms);
/** clearTimeout(id) – 0, or a timer that has run already: nothing happens */
void clearTimeout(int id);
/** setInterval(fn, ms): again and again, until clearInterval(id) */
int setInterval(std::function<void()> fn, double ms);
void clearInterval(int id);
/** how many timers wait (tests) */
int pending();

}  // namespace fs::timers
