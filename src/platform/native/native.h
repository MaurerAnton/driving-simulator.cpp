// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The native host (tests, tools, pictures without a browser): what a test or a
 * tool sets up and drives by hand.
 */
#pragma once

#include <string>

#include "core/task.h"
#include "platform/plat.h"

namespace fs::plat::native {

/**
 * Addresses that begin with `prefix` are read from the folder `dir` (the rest of
 * the address is the path below it).  The longest matching prefix wins; an
 * address nothing is mounted for answers with status 0.  A file `x.gz` is served
 * as it is (the game gunzips what it asks for by that name).
 */
void mount(const std::string& prefix, const std::string& dir);
void unmountAll();
/** what locationSearch() answers ("?start=free") */
void setSearch(const std::string& search);
/** the clock: `true` – nowMs()/dateMs() only move when advance() is called (tests); false – the real clock */
void manualClock(bool on, double dateMs = 1767268800000.0);
void advance(double ms);
/** what languages() and matchMedia("(pointer: coarse)") answer */
void setLanguages(const std::vector<std::string>& langs);
void setCoarsePointer(bool on);
void setViewport(int width, int height, double pixelRatio = 1);

/**
 * Run what is due: answers of fetch(), timers whose time has come, soon().
 * Returns how many things were run (0: nothing is waiting but timers of the future).
 */
int pump();
/** pump() until nothing is due any more (timers of the future stay) */
void drain();
/** how many answers and timers are still waiting (also those of the future) */
int pending();

/** Run the host until the task has finished, and hand back its value. */
template <class T>
T wait(Task<T> task) {
  while (!task.done()) {
    if (pump() == 0 && !task.done()) {
      if (pending() == 0) throw std::runtime_error("plat::native::wait: the task waits for something that never comes");
      advance(1);   // (only timers of the future are left: let time pass)
    }
  }
  if constexpr (!std::is_void_v<T>) return task.get();
}

}  // namespace fs::plat::native
