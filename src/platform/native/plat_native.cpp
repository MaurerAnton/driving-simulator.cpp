// SPDX-License-Identifier: AGPL-3.0-only
/**
 * plat.h on this machine: files from mounted folders, stores in memory, a clock
 * that can be driven by hand, and a queue in place of the browser's event loop.
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unistd.h>

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
  /** addresses nothing is mounted for are fetched from the real network (tools; never in tests) */
  bool network = false;
  native::FileReader reader;
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
    auto it = items_.find(key);
    if (it != items_.end() && it->second == value) return true;
    items_[key] = value;
    changed();
    return true;
  }
  void removeItem(const std::string& key) override {
    if (items_.erase(key)) changed();
  }

  /** kept in a file (keepLocalStorage): "<key bytes> <value bytes>\n<key><value>" one after the other */
  void keep(const std::string& file) {
    file_ = file;
    std::ifstream f(file, std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t at = 0;
    while (at < all.size()) {
      const size_t nl = all.find('\n', at);
      if (nl == std::string::npos) break;
      unsigned long k = 0, v = 0;
      if (std::sscanf(all.c_str() + at, "%lu %lu", &k, &v) != 2 || nl + 1 + k + v > all.size()) break;
      items_[all.substr(nl + 1, k)] = all.substr(nl + 1 + k, v);
      at = nl + 1 + k + v;
    }
    dirty_ = false;
  }
  /** write the file now (when there is one and something changed) */
  void flush() {
    if (file_.empty() || !dirty_) return;
    dirty_ = false;
    lastWrite_ = nowMs();
    const std::string tmp = file_ + ".new";
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      if (!f) return;
      for (const auto& [k, v] : items_) f << k.size() << ' ' << v.size() << '\n' << k << v;
      if (!f) return;
    }
    std::rename(tmp.c_str(), file_.c_str());
  }
  /** written a moment after a change (not at every one: a setting may change every frame) */
  void flushSoon() {
    if (dirty_ && nowMs() - lastWrite_ > 2000) flush();
  }

 private:
  std::map<std::string, std::string> items_;
  std::string file_;
  bool dirty_ = false;
  double lastWrite_ = -1e9;
  void changed() {
    if (!file_.empty()) dirty_ = true;
  }
};

MemoryStorage& local() {
  static auto* s = new MemoryStorage();
  return *s;
}

/** The network's threads: what fetch asked for is fetched there, and its answer handed back to pump(). */
struct Net {
  std::mutex m;
  std::condition_variable wake;
  std::deque<std::pair<Request, std::function<void(Response)>>> jobs;
  std::deque<std::function<void()>> answered;
  int busy = 0;
  int threads = 0;
  native::NetworkFetch fetch;
};

Net& net() {
  static auto* n = new Net();   // (never destroyed: its threads outlive whatever exit destroys)
  return *n;
}

bool fileSystemRead(const std::string& path, Bytes& out) {
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

/** "bytes=a-b" of a Range header (false: there is none, or not of that plain shape) */
static bool rangeOf(const Request& request, double& a, double& b) {
  for (const auto& h : request.headers) {
    if (h.first != "Range" && h.first != "range") continue;
    unsigned long long x = 0, y = 0;
    if (std::sscanf(h.second.c_str(), "bytes=%llu-%llu", &x, &y) == 2 && y >= x) {
      a = static_cast<double>(x);
      b = static_cast<double>(y);
      return true;
    }
  }
  return false;
}

/** The real network, for the tools that look at the published maps: through the `curl` program. */
static Response curlFetch(const Request& request) {
  Response res;
  char tmp[] = "/tmp/fahrsim-fetch-XXXXXX";
  int fd = mkstemp(tmp);
  if (fd < 0) return res;
  close(fd);
  std::string cmd = "curl -s -L --max-time 120 -o '" + std::string(tmp) + "' -w '%{http_code} %{content_type}'";
  if (request.method != "GET") cmd += " -X " + request.method;
  for (const auto& h : request.headers) {
    // (only what a header may hold: nothing that could end the quoting)
    std::string v = h.first + ": " + h.second;
    bool plain = true;
    for (char c : v)
      if (c == '\'' || c == '\\' || c == '\n' || c == '\r') plain = false;
    if (plain) cmd += " -H '" + v + "'";
  }
  std::string url = request.url;
  for (char c : url)
    if (c == '\'' || c == '\\' || c == ' ' || c == '\n') return res;
  cmd += " '" + url + "' 2>/dev/null";
  if (FILE* p = popen(cmd.c_str(), "r")) {
    char buf[256] = {0};
    if (std::fgets(buf, sizeof buf, p)) {
      res.status = std::atoi(buf);
      const char* sp = std::strchr(buf, ' ');
      if (sp) res.contentType = sp + 1;
    }
    pclose(p);
  }
  if (res.status > 0) fileSystemRead(tmp, res.body);
  std::remove(tmp);
  return res;
}

/** hand a request to the network's threads (at most four of them); `done` runs in pump() with the answer */
static void askNetwork(Request request, std::function<void(Response)> done) {
  Net& n = net();
  request.progress = nullptr;   // (what it holds belongs to this thread: told in `done` instead)
  std::lock_guard<std::mutex> lock(n.m);
  n.jobs.emplace_back(std::move(request), std::move(done));
  n.busy++;
  if (n.threads < 4 && n.threads < n.busy) {
    n.threads++;
    std::thread([&n] {
      for (;;) {
        std::pair<Request, std::function<void(Response)>> job;
        {
          std::unique_lock<std::mutex> lock(n.m);
          n.wake.wait(lock, [&n] { return !n.jobs.empty(); });
          job = std::move(n.jobs.front());
          n.jobs.pop_front();
        }
        native::NetworkFetch fn;
        {
          std::lock_guard<std::mutex> lock(n.m);
          fn = n.fetch;
        }
        Response res = fn ? fn(job.first) : curlFetch(job.first);
        std::lock_guard<std::mutex> lock(n.m);
        n.answered.push_back([done = std::move(job.second), res = std::move(res)]() mutable { done(std::move(res)); });
      }
    }).detach();
  }
  n.wake.notify_one();
}

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
    if (path.compare(0, 7, "http://") == 0 || path.compare(0, 8, "https://") == 0) {
      // (a folder on the network: the address beside it, its query and all)
      if (!h.network) res.status = 0;
      else {
        Request there = request;
        there.url = best->second;
        const std::string after = request.url.substr(best->first.size());
        if (!there.url.empty() && there.url.back() != '/' && !after.empty() && after.front() != '/') there.url += '/';
        there.url += after;
        Resolver<Response> r2 = r;
        auto progress = request.progress;
        askNetwork(std::move(there), [r2, progress](Response got) mutable {
          if (progress && got.ok()) progress(static_cast<double>(got.body.size()), static_cast<double>(got.body.size()));
          r2.resolve(std::move(got));
        });
        return r.task();
      }
    } else if (native::readFile(path, res.body)) {
      res.status = 200;
      double a = 0, b = 0;
      if (rangeOf(request, a, b) && a < static_cast<double>(res.body.size())) {
        size_t end = std::min(res.body.size(), static_cast<size_t>(b) + 1);
        res.body = Bytes(res.body.begin() + static_cast<long>(a), res.body.begin() + static_cast<long>(end));
        res.status = 206;
      }
      if (path.size() > 5 && path.compare(path.size() - 5, 5, ".json") == 0) res.contentType = "application/json";
    } else res.status = 404;
  } else if (!best && h.network && (request.url.compare(0, 8, "https://") == 0 || request.url.compare(0, 7, "http://") == 0)) {
    auto progress = request.progress;
    askNetwork(std::move(request), [r, progress](Response got) mutable {
      if (progress && got.ok()) progress(static_cast<double>(got.body.size()), static_cast<double>(got.body.size()));
      r.resolve(std::move(got));
    });
    return r.task();
  }
  auto progress = request.progress;
  // (the answer comes later, as in a browser: whoever asked goes on first)
  h.ready.push_back([r, res = std::move(res), progress]() mutable {
    if (progress && res.ok()) progress(static_cast<double>(res.body.size()), static_cast<double>(res.body.size()));
    r.resolve(std::move(res));
  });
  return r.task();
}

Task<Response> fetch(std::string url) {
  Request q;
  q.url = std::move(url);
  return fetch(std::move(q));
}

Storage& localStorage() { return local(); }
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
void useNetwork(bool on) { host().network = on; }
void setNetwork(NetworkFetch fetch) {
  Net& n = net();
  std::lock_guard<std::mutex> lock(n.m);
  n.fetch = std::move(fetch);
}
void setFileReader(FileReader reader) { host().reader = std::move(reader); }
bool readFile(const std::string& path, Bytes& out) {
  const FileReader& reader = host().reader;
  return reader ? reader(path, out) : fileSystemRead(path, out);
}
void keepLocalStorage(const std::string& file) { local().keep(file); }
void flushLocalStorage() { local().flush(); }
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
  // the network's answers that have come
  {
    Net& n = net();
    std::deque<std::function<void()>> came;
    {
      std::lock_guard<std::mutex> lock(n.m);
      came.swap(n.answered);
      n.busy -= static_cast<int>(came.size());
    }
    for (auto& fn : came) {
      fn();
      ran++;
    }
  }
  local().flushSoon();
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

int networkBusy() {
  Net& n = net();
  std::lock_guard<std::mutex> lock(n.m);
  return n.busy;
}

int pending() { return static_cast<int>(host().ready.size() + host().timers.size()) + networkBusy(); }

}  // namespace native
}  // namespace fs::plat
