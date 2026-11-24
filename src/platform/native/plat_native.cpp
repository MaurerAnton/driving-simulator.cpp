// SPDX-License-Identifier: AGPL-3.0-only
/**
 * plat.h on this machine: files from mounted folders, stores in memory, a clock
 * that can be driven by hand, and a queue in place of the browser's event loop.
 */
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <stdexcept>

#include "platform/native/native.h"
#include "platform/plat.h"

namespace fs::plat {

namespace {

struct Timer {
  double at;
  long seq;
  std::function<void()> fn;
};

struct Host {
  std::vector<std::pair<std::string, std::string>> mounts;
  std::string search;
  bool manual = false;
  double manualNow = 0;
  double manualDate = 1767268800000.0;
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
  std::deque<std::function<void()>> ready;
  std::vector<Timer> timers;
  long seq = 0;
  std::vector<std::string> langs = {"de-DE", "de", "en"};
  bool coarse = false;
  int width = 1280, height = 720;
  double pixelRatio = 1;
};

Host& host() {
  static Host h;
  return h;
}

class MemoryStorage : public Storage {
 public:
  std::optional<std::string> getItem(const std::string& key) override {
    auto it = items_.find(key);
    if (it == items_.end()) return std::nullopt;
    return it->second;
  }
  bool setItem(const std::string& key, const std::string& value) override {
    items_[key] = value;
    return true;
  }
  void removeItem(const std::string& key) override { items_.erase(key); }

 private:
  std::map<std::string, std::string> items_;
};

bool readFile(const std::string& path, Bytes& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  std::streamoff n = f.tellg();
  if (n < 0) return false;
  f.seekg(0);
  out.resize(static_cast<size_t>(n));
  if (n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
  return static_cast<bool>(f) || n == 0;
}

}  // namespace

double nowMs() {
  Host& h = host();
  if (h.manual) return h.manualNow;
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - h.t0).count();
}

double dateMs() {
  Host& h = host();
  if (h.manual) return h.manualDate + h.manualNow;
  return std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
}

double timezoneOffsetMin(double ms) {
  // Central European time by the rule of the EU (the game's maps are there; a test must not
  // depend on the machine's zone): summer time from the last Sunday of March 01:00 UTC to
  // the last Sunday of October 01:00 UTC.
  auto daysFromCivil = [](long y, unsigned m, unsigned d) -> long {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
  };
  long days = static_cast<long>(std::floor(ms / 86400000.0));
  // the year of that day
  long z = days + 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = static_cast<unsigned>(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = static_cast<long>(yoe) + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y++;
  auto lastSunday = [&](unsigned month) -> double {
    long last = daysFromCivil(y, month, 31);
    long wd = ((last % 7) + 11) % 7;   // 0 = Sunday (1970-01-01 was a Thursday)
    return (static_cast<double>(last - wd) * 86400000.0) + 3600000.0;
  };
  bool summer = ms >= lastSunday(3) && ms < lastSunday(10);
  return summer ? -120 : -60;
}

void setTimeout(std::function<void()> fn, double ms) {
  Host& h = host();
  h.timers.push_back(Timer{nowMs() + (ms > 0 ? ms : 0), h.seq++, std::move(fn)});
}

Task<void> sleep(double ms) {
  Resolver<void> r;
  setTimeout([r] { r.resolve(); }, ms);
  return r.task();
}

void soon(std::function<void()> fn) { host().ready.push_back(std::move(fn)); }

void log(const std::string& text) { std::fprintf(stdout, "%s\n", text.c_str()); }
void warn(const std::string& text) { std::fprintf(stderr, "warn: %s\n", text.c_str()); }
void error(const std::string& text) { std::fprintf(stderr, "error: %s\n", text.c_str()); }

Task<Response> fetch(Request request) {
  Resolver<Response> r;
  Host& h = host();
  // the folder the address is mounted on (the longest prefix)
  const std::pair<std::string, std::string>* best = nullptr;
  for (const auto& m : h.mounts)
    if (request.url.compare(0, m.first.size(), m.first) == 0 && (!best || m.first.size() > best->first.size())) best = &m;
  Response res;
  if (best && request.method == "GET") {
    std::string rest = request.url.substr(best->first.size());
    size_t q = rest.find_first_of("?#");
    if (q != std::string::npos) rest = rest.substr(0, q);
    std::string path = best->second;
    if (!path.empty() && path.back() != '/' && !rest.empty() && rest.front() != '/') path += '/';
    path += rest;
    if (readFile(path, res.body)) res.status = 200;
    else res.status = 404;
  }
  auto progress = request.progress;
  // (the answer comes later, as in a browser: whoever asked goes on first)
  h.ready.push_back([r, res = std::move(res), progress]() mutable {
    if (progress && res.status == 200) progress(static_cast<double>(res.body.size()), static_cast<double>(res.body.size()));
    r.resolve(std::move(res));
  });
  return r.task();
}

Task<Response> fetch(std::string url) {
  Request q;
  q.url = std::move(url);
  return fetch(std::move(q));
}

Storage& localStorage() {
  static MemoryStorage s;
  return s;
}
Storage& sessionStorage() {
  static MemoryStorage s;
  return s;
}

std::string locationSearch() { return host().search; }
std::string locationBase() { return "http://localhost/"; }
void replaceSearch(const std::string& search) { host().search = search; }
void reload() {}
void openTab(const std::string&) {}
void copyText(const std::string&) {}

std::vector<std::string> languages() { return host().langs; }
bool matchMedia(const std::string& query) {
  if (query.find("pointer: coarse") != std::string::npos) return host().coarse;
  return false;
}
int hardwareConcurrency() { return 8; }
double deviceMemory() { return 8; }
double devicePixelRatio() { return host().pixelRatio; }
int innerWidth() { return host().width; }
int innerHeight() { return host().height; }
void vibrate(double) {}
bool online() { return true; }
std::vector<Gamepad> gamepads() { return {}; }

namespace native {

void mount(const std::string& prefix, const std::string& dir) { host().mounts.emplace_back(prefix, dir); }
void unmountAll() { host().mounts.clear(); }
void setSearch(const std::string& search) { host().search = search; }
void manualClock(bool on, double date) {
  Host& h = host();
  h.manual = on;
  h.manualNow = 0;
  h.manualDate = date;
}
void advance(double ms) {
  Host& h = host();
  if (!h.manual) {
    // (the real clock cannot be pushed: from here on it is driven by hand, starting at now)
    h.manualNow = nowMs();
    h.manualDate = dateMs() - h.manualNow;
    h.manual = true;
  }
  h.manualNow += ms;
}
void setLanguages(const std::vector<std::string>& langs) { host().langs = langs; }
void setCoarsePointer(bool on) { host().coarse = on; }
void setViewport(int width, int height, double pixelRatio) {
  Host& h = host();
  h.width = width;
  h.height = height;
  h.pixelRatio = pixelRatio;
}

int pump() {
  Host& h = host();
  int ran = 0;
  // what is ready now (not what these add: that is the next round's)
  size_t n = h.ready.size();
  for (size_t i = 0; i < n && !h.ready.empty(); i++) {
    auto fn = std::move(h.ready.front());
    h.ready.pop_front();
    fn();
    ran++;
  }
  // the timers whose time has come, in the order they were set for
  double now = nowMs();
  for (;;) {
    int pick = -1;
    for (size_t i = 0; i < h.timers.size(); i++) {
      if (h.timers[i].at > now) continue;
      if (pick < 0 || h.timers[i].at < h.timers[pick].at || (h.timers[i].at == h.timers[pick].at && h.timers[i].seq < h.timers[pick].seq)) pick = static_cast<int>(i);
    }
    if (pick < 0) break;
    auto fn = std::move(h.timers[pick].fn);
    h.timers.erase(h.timers.begin() + pick);
    fn();
    ran++;
  }
  return ran;
}

void drain() {
  while (pump() > 0) {
  }
}

int pending() { return static_cast<int>(host().ready.size() + host().timers.size()); }

}  // namespace native
}  // namespace fs::plat
