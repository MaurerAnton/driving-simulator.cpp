// SPDX-License-Identifier: AGPL-3.0-only
/** plat.h in the browser (platform/web/library_fahrsim.js does the browser's part). */
#include <emscripten.h>

#include <cstdlib>
#include <unordered_map>

#include "core/js.h"
#include "platform/plat.h"

extern "C" {
void fsp_fetch(int id, const char* url, const char* method, const char* headers, const uint8_t* body, int bodyLen, const char* cache, int wantProgress);
char* fsp_storage_get(int which, const char* key);
int fsp_storage_set(int which, const char* key, const char* value);
void fsp_storage_remove(int which, const char* key);
double fsp_now();
double fsp_date();
double fsp_timezone_offset(double ms);
void fsp_timeout(int id, double ms);
void fsp_soon(int id);
void fsp_console(int level, const char* text);
char* fsp_text(int what);
void fsp_replace_search(const char* search);
void fsp_reload();
void fsp_open_tab(const char* url);
void fsp_copy_text(const char* text);
int fsp_match_media(const char* query);
double fsp_number(int what);
void fsp_vibrate(double ms);
int fsp_gamepads(double* out, int max);
char* fsp_gamepad_id(int index);
}

namespace fs::plat {

namespace {

struct Pending {
  Resolver<Response> resolver;
  Response response;
  std::function<void(double, double)> progress;
};

std::unordered_map<int, Pending>& fetches() {
  static std::unordered_map<int, Pending> m;
  return m;
}
std::unordered_map<int, std::function<void()>>& timers() {
  static std::unordered_map<int, std::function<void()>> m;
  return m;
}
int nextId = 1;

std::string take(char* s) {
  if (!s) return std::string();
  std::string out(s);
  std::free(s);
  return out;
}

class WebStorage : public Storage {
 public:
  explicit WebStorage(int which) : which_(which) {}
  std::optional<std::string> getItem(const std::string& key) override {
    char* s = fsp_storage_get(which_, key.c_str());
    if (!s) return std::nullopt;
    return take(s);
  }
  bool setItem(const std::string& key, const std::string& value) override { return fsp_storage_set(which_, key.c_str(), value.c_str()) != 0; }
  void removeItem(const std::string& key) override { fsp_storage_remove(which_, key.c_str()); }

 private:
  int which_;
};

}  // namespace

double nowMs() { return fsp_now(); }
double dateMs() { return fsp_date(); }
double timezoneOffsetMin(double ms) { return fsp_timezone_offset(ms); }

void setTimeout(std::function<void()> fn, double ms) {
  int id = nextId++;
  timers().emplace(id, std::move(fn));
  fsp_timeout(id, ms);
}

Task<void> sleep(double ms) {
  Resolver<void> r;
  setTimeout([r] { r.resolve(); }, ms);
  return r.task();
}

void soon(std::function<void()> fn) {
  int id = nextId++;
  timers().emplace(id, std::move(fn));
  fsp_soon(id);
}

void log(const std::string& text) { fsp_console(0, text.c_str()); }
void warn(const std::string& text) { fsp_console(1, text.c_str()); }
void error(const std::string& text) { fsp_console(2, text.c_str()); }

Task<Response> fetch(Request request) {
  int id = nextId++;
  Pending& p = fetches()[id];
  p.progress = std::move(request.progress);
  std::string headers;
  for (const auto& h : request.headers) headers += h.first + ": " + h.second + "\n";
  fsp_fetch(id, request.url.c_str(), request.method.c_str(), headers.c_str(), request.body.data(), static_cast<int>(request.body.size()), request.cache.c_str(),
            p.progress ? 1 : 0);
  return p.resolver.task();
}

Task<Response> fetch(std::string url) {
  Request q;
  q.url = std::move(url);
  return fetch(std::move(q));
}

Storage& localStorage() {
  static WebStorage s(0);
  return s;
}
Storage& sessionStorage() {
  static WebStorage s(1);
  return s;
}

std::string locationSearch() { return take(fsp_text(0)); }
std::string locationBase() { return take(fsp_text(1)); }
void replaceSearch(const std::string& search) { fsp_replace_search(search.c_str()); }
void reload() { fsp_reload(); }
void openTab(const std::string& url) { fsp_open_tab(url.c_str()); }
void copyText(const std::string& text) { fsp_copy_text(text.c_str()); }

std::vector<std::string> languages() { return split(take(fsp_text(2)), ","); }
bool matchMedia(const std::string& query) { return fsp_match_media(query.c_str()) != 0; }
int hardwareConcurrency() { return static_cast<int>(fsp_number(0)); }
double deviceMemory() { return fsp_number(1); }
double devicePixelRatio() { return fsp_number(2); }
int innerWidth() { return static_cast<int>(fsp_number(3)); }
int innerHeight() { return static_cast<int>(fsp_number(4)); }
void vibrate(double ms) { fsp_vibrate(ms); }
bool online() { return fsp_number(5) != 0; }

std::vector<Gamepad> gamepads() {
  double buf[512];
  int n = fsp_gamepads(buf, 512);
  std::vector<Gamepad> out;
  int i = 0;
  while (i + 3 <= n) {
    Gamepad g;
    g.index = static_cast<int>(buf[i]);
    int axes = static_cast<int>(buf[i + 1]), buttons = static_cast<int>(buf[i + 2]);
    i += 3;
    if (i + axes + buttons > n) break;
    g.axes.assign(buf + i, buf + i + axes);
    i += axes;
    g.buttons.assign(buf + i, buf + i + buttons);
    i += buttons;
    g.id = take(fsp_gamepad_id(g.index));
    out.push_back(std::move(g));
  }
  return out;
}

}  // namespace fs::plat

extern "C" {

/** The page has `bytes` bytes of an answer: where to put them. */
EMSCRIPTEN_KEEPALIVE uint8_t* fsp_fetch_room(int id, int bytes) {
  auto it = fs::plat::fetches().find(id);
  if (it == fs::plat::fetches().end()) return nullptr;
  it->second.response.body.resize(static_cast<size_t>(bytes > 0 ? bytes : 0));
  return it->second.response.body.data();
}

EMSCRIPTEN_KEEPALIVE void fsp_fetch_progress(int id, double got, double total) {
  auto it = fs::plat::fetches().find(id);
  if (it != fs::plat::fetches().end() && it->second.progress) it->second.progress(got, total);
}

EMSCRIPTEN_KEEPALIVE void fsp_fetch_done(int id, int status, const char* contentType) {
  auto it = fs::plat::fetches().find(id);
  if (it == fs::plat::fetches().end()) return;
  fs::Resolver<fs::plat::Response> r = it->second.resolver;
  fs::plat::Response res = std::move(it->second.response);
  res.status = status;
  if (contentType) res.contentType = contentType;
  fs::plat::fetches().erase(it);
  r.resolve(std::move(res));
}

EMSCRIPTEN_KEEPALIVE void fsp_timer(int id) {
  auto it = fs::plat::timers().find(id);
  if (it == fs::plat::timers().end()) return;
  std::function<void()> fn = std::move(it->second);
  fs::plat::timers().erase(it);
  fn();
}

}  // extern "C"
