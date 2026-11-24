// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the host gives the game besides the page (dom.h), drawing (canvas.h, the
 * engine) and sound (audio.h): the clock, files over the network, a small store
 * that survives a reload, the address of the page, what kind of device this is.
 *
 * Two hosts implement it: the browser (platform/web) and this machine
 * (platform/native: files from disk, a store in memory, a clock that can be set –
 * for tests and tools).  The game never calls a browser API directly.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/task.h"

namespace fs {

using Bytes = std::vector<uint8_t>;

namespace plat {

// --- the clock ---------------------------------------------------------------

/** performance.now(): milliseconds since the page was opened */
double nowMs();
/** Date.now(): milliseconds since 1970 (UTC) */
double dateMs();
/** minutes the local time is behind UTC at that moment (`new Date(ms).getTimezoneOffset()`: -60 in a German winter) */
double timezoneOffsetMin(double ms);
/** setTimeout(fn, ms) */
void setTimeout(std::function<void()> fn, double ms);
/** `await new Promise(r => setTimeout(r, ms))` */
Task<void> sleep(double ms);
/** queueMicrotask(fn) / `await Promise.resolve()`: soon, after what is running now */
void soon(std::function<void()> fn);

// --- the console -------------------------------------------------------------

void log(const std::string& text);
void warn(const std::string& text);
void error(const std::string& text);

// --- files over the network ---------------------------------------------------

struct Response {
  /** the HTTP status; 0: there was no answer at all (no connection, refused) */
  int status = 0;
  Bytes body;
  bool ok() const { return status >= 200 && status < 300; }
  std::string text() const { return std::string(body.begin(), body.end()); }
};

struct Request {
  std::string url;
  std::string method = "GET";
  std::vector<std::pair<std::string, std::string>> headers;
  Bytes body;
  /** fetch's `cache` ('no-store', 'no-cache' …; '': the browser's default) */
  std::string cache;
  /** called while the answer arrives: bytes so far, and of how many (-1: the server did not say) */
  std::function<void(double got, double total)> progress;
};

/** fetch(url) and `await response.arrayBuffer()` in one: never throws, see Response::status */
Task<Response> fetch(Request request);
Task<Response> fetch(std::string url);

/** gunzip (the map's files are stored gzipped); empty when the bytes are not gzip */
Bytes gunzip(const Bytes& gz);
/** is it gzip (the magic number)? */
inline bool isGzip(const Bytes& b) { return b.size() > 2 && b[0] == 0x1f && b[1] == 0x8b; }

// --- the store that survives a reload ------------------------------------------

class Storage {
 public:
  virtual ~Storage() = default;
  /** localStorage.getItem (nullopt: nothing stored under the key) */
  virtual std::optional<std::string> getItem(const std::string& key) = 0;
  /** localStorage.setItem; false: it could not be stored (the store is full or shut) */
  virtual bool setItem(const std::string& key, const std::string& value) = 0;
  virtual void removeItem(const std::string& key) = 0;
};
Storage& localStorage();
Storage& sessionStorage();

// --- the page's address ---------------------------------------------------------

/** location.search ("?start=free&lang=de", or "") */
std::string locationSearch();
/** location.href without the query and the fragment, ending in "/" or the file's name */
std::string locationBase();
/** history.replaceState: the address bar shows this query from now on ("" or "?a=b") */
void replaceSearch(const std::string& search);
void reload();
/** window.open(url, '_blank', 'noopener') */
void openTab(const std::string& url);
/** navigator.clipboard.writeText */
void copyText(const std::string& text);

// --- the device ------------------------------------------------------------------

/** navigator.languages (e.g. "de-DE", "de", "en") */
std::vector<std::string> languages();
/** matchMedia(query).matches, e.g. "(pointer: coarse)" */
bool matchMedia(const std::string& query);
/** navigator.hardwareConcurrency (8 when the browser does not say) */
int hardwareConcurrency();
/** navigator.deviceMemory in GB (8 when the browser does not say) */
double deviceMemory();
double devicePixelRatio();
/** window.innerWidth / innerHeight (CSS pixels) */
int innerWidth();
int innerHeight();
/** navigator.vibrate(ms) */
void vibrate(double ms);
/** navigator.onLine */
bool online();

/** One gamepad as navigator.getGamepads() has it (index: its slot). */
struct Gamepad {
  int index = 0;
  std::string id;
  std::vector<double> axes;
  /** how far each button is pressed (0 … 1) */
  std::vector<double> buttons;
};
std::vector<Gamepad> gamepads();

}  // namespace plat
}  // namespace fs
