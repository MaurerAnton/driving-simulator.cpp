// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The browser's side of platform/frame.h, sensors.h and geolocation.h, and of
 * window.confirm and the bytes downloaded (ui/hud_parts.h) – library_host.js.
 */
#include <emscripten.h>

#include <cstdlib>
#include <functional>
#include <unordered_map>

#include "core/js.h"
#include "platform/frame.h"
#include "platform/geolocation.h"
#include "platform/sensors.h"
#include "ui/hud_parts.h"

extern "C" {
int fsx_raf(int id);
void fsx_caf(int handle);
void fsx_debug(const char* text);
int fsx_orientation_known();
int fsx_orientation_asks();
void fsx_orientation_permission(int id);
char* fsx_gamepads();
int fsx_has_geolocation();
void fsx_geolocate(int id, int high, double timeout, double maximumAge);
int fsx_confirm(const char* text);
double fsx_downloaded();
}

namespace fs::plat {

namespace {

int nextId = 1;

struct Frame {
  std::function<void(double)> fn;
  int handle = 0;
};
std::unordered_map<int, Frame>& frames() {
  static std::unordered_map<int, Frame> m;
  return m;
}

std::unordered_map<int, Resolver<std::string>>& texts() {
  static std::unordered_map<int, Resolver<std::string>> m;
  return m;
}

struct GeoAsk {
  std::function<void(const GeoPosition&)> ok;
  std::function<void(const GeoError&)> fail;
};
std::unordered_map<int, GeoAsk>& geoAsks() {
  static std::unordered_map<int, GeoAsk> m;
  return m;
}

}  // namespace

int requestAnimationFrame(std::function<void(double now)> fn) {
  const int id = nextId++;
  Frame& f = frames()[id];
  f.fn = std::move(fn);
  f.handle = fsx_raf(id);
  return id;
}

void cancelAnimationFrame(int id) {
  auto it = frames().find(id);
  if (it == frames().end()) return;
  fsx_caf(it->second.handle);
  frames().erase(it);
}

void debug(const std::string& text) { fsx_debug(text.c_str()); }

bool isTrusted(const dom::Event& e) { return e.isTrusted; }

bool hasDeviceOrientation() { return fsx_orientation_known() != 0; }

bool deviceOrientationAsks() { return fsx_orientation_asks() != 0; }

Task<std::string> requestDeviceOrientationPermission() {
  const int id = nextId++;
  Resolver<std::string> r;
  texts().emplace(id, r);
  fsx_orientation_permission(id);
  return r.task();
}

std::vector<GamepadState> getGamepads() {
  std::vector<GamepadState> out;
  char* raw = fsx_gamepads();
  const std::string text = raw ? raw : "";
  std::free(raw);
  for (const std::string& line : split(text, "\n")) {
    if (line.empty()) continue;
    const std::vector<std::string> f = split(line, "\t");
    if (f.size() < 5) continue;
    GamepadState g;
    g.index = static_cast<int>(jsNumber(f[0]));
    g.id = f[1];
    g.mapping = f[2];
    if (!f[3].empty())
      for (const std::string& a : split(f[3], ",")) g.axes.push_back(jsNumber(a));
    if (!f[4].empty())
      for (const std::string& b : split(f[4], ",")) {
        const size_t colon = b.find(':');
        GamepadButton btn;
        btn.pressed = b.substr(0, colon) == "1";
        btn.value = colon == std::string::npos ? 0 : jsNumber(b.substr(colon + 1));
        g.buttons.push_back(btn);
      }
    out.push_back(std::move(g));
  }
  return out;
}

const Geolocation* geolocation() {
  if (!fsx_has_geolocation()) return nullptr;
  static Geolocation g{[](std::function<void(const GeoPosition&)> ok, std::function<void(const GeoError&)> fail, const GeoOptions& o) {
    const int id = nextId++;
    geoAsks()[id] = GeoAsk{std::move(ok), std::move(fail)};
    fsx_geolocate(id, o.enableHighAccuracy ? 1 : 0, o.timeout, o.maximumAge);
  }};
  return &g;
}

bool confirm(const std::string& text) { return fsx_confirm(text.c_str()) != 0; }

}  // namespace fs::plat

namespace fs {
double downloaded() { return fsx_downloaded(); }
}  // namespace fs

extern "C" EMSCRIPTEN_KEEPALIVE void fsx_frame(int id, double now) {
  auto it = fs::plat::frames().find(id);
  if (it == fs::plat::frames().end()) return;
  std::function<void(double)> fn = std::move(it->second.fn);
  fs::plat::frames().erase(it);
  if (fn) fn(now);
}

extern "C" EMSCRIPTEN_KEEPALIVE void fsx_text_done(int id, const char* text) {
  auto it = fs::plat::texts().find(id);
  if (it == fs::plat::texts().end()) return;
  fs::Resolver<std::string> r = it->second;
  fs::plat::texts().erase(it);
  r.resolve(std::string(text ? text : ""));
}

extern "C" EMSCRIPTEN_KEEPALIVE void fsx_geo_done(int id, int code, double latitude, double longitude, double accuracy) {
  auto it = fs::plat::geoAsks().find(id);
  if (it == fs::plat::geoAsks().end()) return;
  fs::plat::GeoAsk ask = std::move(it->second);
  fs::plat::geoAsks().erase(it);
  if (code == 0) {
    fs::plat::GeoPosition p;
    p.coords.latitude = latitude;
    p.coords.longitude = longitude;
    p.coords.accuracy = accuracy;
    if (ask.ok) ask.ok(p);
  } else {
    fs::plat::GeoError e;
    e.code = code;
    if (ask.fail) ask.fail(e);
  }
}
