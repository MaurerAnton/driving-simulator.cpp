// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The native host (tests, tools, pictures without a browser): what a test or a
 * tool sets up and drives by hand.
 */
#pragma once

#include <chrono>
#include <functional>
#include <stdexcept>
#include <thread>
#include <string>
#include <vector>

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
/**
 * Addresses that nothing is mounted for are fetched from the real network (with the `curl`
 * program) – for the tools that look at the published maps; off by default, and never on in a test.
 * A folder mounted as an address (`mount(prefix, "https://…/")`) is fetched from there over the
 * network too (when it is on).  The network is asked on threads of its own: the answer comes at a
 * later pump(), and the loop goes on meanwhile.
 */
void useNetwork(bool on);
/** what asks the network (run on a worker thread; unset: the `curl` program – Android sets its own) */
using NetworkFetch = std::function<Response(const Request& request)>;
void setNetwork(NetworkFetch fetch);

/**
 * How a file is read (mounted folders, the canvas's fonts).  Unset: from the file system.  The SDL
 * host on Android reads its APK's assets this way (a relative path is an asset there).
 */
using FileReader = std::function<bool(const std::string& path, Bytes& out)>;
void setFileReader(FileReader reader);
/** read a file as the host does (the reader above, or the file system) */
bool readFile(const std::string& path, Bytes& out);

/**
 * Keep localStorage in this file: what it holds is read now, and written back a moment after it
 * changed (and at flushLocalStorage()).  Not called: localStorage lives in memory only.
 */
void keepLocalStorage(const std::string& file);
/** write localStorage to its file now, if it changed since it was last written */
void flushLocalStorage();
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
/** how many answers and timers are still waiting (also those of the future, and the network's) */
int pending();

/** how many requests the network's threads are still busy with */
int networkBusy();

/** Run the host until the task has finished, and hand back its value. */
template <class T>
T wait(Task<T> task) {
  while (!task.done()) {
    if (pump() == 0 && !task.done()) {
      if (pending() == 0) throw std::runtime_error("plat::native::wait: the task waits for something that never comes");
      if (networkBusy() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));   // (the network answers in its own time)
      else advance(1);   // (only timers of the future are left: let time pass)
    }
  }
  if constexpr (!std::is_void_v<T>) return task.get();
}

/** what window.confirm answers on this machine (OK unless a test says otherwise), and the questions asked */
inline bool confirmAnswer = true;
inline std::vector<std::string> confirmed;
/** what downloaded() says (bytes; a test sets it) */
inline double bytesDownloaded = 0;

}  // namespace fs::plat::native
