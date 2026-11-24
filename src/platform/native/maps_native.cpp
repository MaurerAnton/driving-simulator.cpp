// SPDX-License-Identifier: AGPL-3.0-only
/**
 * platform/geolocation.h, frame.h and observers.h on this machine: a position a test has
 * set, pictures a test asks for, observers a test notifies (maps_native.h).
 */
#include "platform/native/maps_native.h"

#include <map>
#include <utility>
#include <vector>

#include "platform/frame.h"
#include "platform/observers.h"
#include "platform/plat.h"

namespace fs::plat {

namespace {

struct GeoState {
  bool there = false;
  std::optional<GeoCoords> coords;
  int error = 2;
  Geolocation api;
};
GeoState& geo() {
  static GeoState g;
  return g;
}

struct Frames {
  int next = 1;
  std::vector<std::pair<int, std::function<void(double)>>> waiting;
};
Frames& frames() {
  static Frames f;
  return f;
}

struct Observer {
  int kind = 0;
  std::function<void()> fn;
  /** element → the attributes watched (empty: all) */
  std::vector<std::pair<int, std::vector<std::string>>> targets;
};
std::map<int, Observer>& observers() {
  static std::map<int, Observer> o;
  return o;
}
int nextObserver = 1;
std::map<int, int>& offsetParents() {
  static std::map<int, int> m;
  return m;
}

void notify(int kind, int element, const std::string& attribute) {
  // (a callback may disconnect its observer: the list is copied first)
  std::vector<std::function<void()>> due;
  for (const auto& [h, o] : observers()) {
    if (o.kind != kind) continue;
    for (const auto& [el, filter] : o.targets) {
      if (el != element) continue;
      bool wanted = kind == 0 || filter.empty();
      for (const std::string& a : filter)
        if (a == attribute) wanted = true;
      if (wanted) {
        due.push_back(o.fn);
        break;
      }
    }
  }
  for (auto& fn : due) fn();
}

}  // namespace

const Geolocation* geolocation() {
  GeoState& g = geo();
  if (!g.there) return nullptr;
  if (!g.api.getCurrentPosition) {
    g.api.getCurrentPosition = [](std::function<void(const GeoPosition&)> ok, std::function<void(const GeoError&)> fail, const GeoOptions&) {
      // (later, as the browser answers: never inside the call)
      GeoState now = geo();
      soon([now, ok, fail] {
        if (now.coords) ok(GeoPosition{*now.coords});
        else fail(GeoError{now.error});
      });
    };
  }
  return &g.api;
}

int requestAnimationFrame(std::function<void(double now)> fn) {
  Frames& f = frames();
  int id = f.next++;
  f.waiting.emplace_back(id, std::move(fn));
  return id;
}

void cancelAnimationFrame(int id) {
  auto& w = frames().waiting;
  for (size_t i = 0; i < w.size(); i++)
    if (w[i].first == id) {
      w.erase(w.begin() + static_cast<long>(i));
      return;
    }
}

namespace observerBackend {

int create(int kind, std::function<void()> fn) {
  int h = nextObserver++;
  Observer o;
  o.kind = kind;
  o.fn = std::move(fn);
  observers()[h] = std::move(o);
  return h;
}

void observe(int observer, int element, const std::vector<std::string>& attributeFilter) {
  auto it = observers().find(observer);
  if (it == observers().end() || !element) return;
  for (auto& t : it->second.targets)
    if (t.first == element) {
      t.second = attributeFilter;
      return;
    }
  it->second.targets.emplace_back(element, attributeFilter);
}

void disconnect(int observer) { observers().erase(observer); }

int offsetParent(int element) {
  auto it = offsetParents().find(element);
  if (it != offsetParents().end()) return it->second;
  return dom::backend::parent(element);
}

}  // namespace observerBackend

namespace native {

void setGeolocation(const GeoCoords& coords) {
  geo().there = true;
  geo().coords = coords;
}
void setGeolocationError(int code) {
  geo().there = true;
  geo().coords.reset();
  geo().error = code;
}
void clearGeolocation() {
  geo().there = false;
  geo().coords.reset();
}

int runFrames() {
  std::vector<std::pair<int, std::function<void(double)>>> due;
  due.swap(frames().waiting);
  for (auto& [id, fn] : due) fn(nowMs());
  return static_cast<int>(due.size());
}
int framesWaiting() { return static_cast<int>(frames().waiting.size()); }

void notifyResize(const dom::El& e) { notify(0, e.handle(), ""); }
void notifyMutation(const dom::El& e, const std::string& attribute) { notify(1, e.handle(), attribute); }
int observersActive() {
  int n = 0;
  for (const auto& [h, o] : observers())
    if (!o.targets.empty()) n++;
  return n;
}
void setOffsetParent(const dom::El& e, const dom::El& parent) { offsetParents()[e.handle()] = parent.handle(); }

}  // namespace native

}  // namespace fs::plat
