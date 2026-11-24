// SPDX-License-Identifier: AGPL-3.0-only
#include "audio/timers.h"

#include <memory>
#include <unordered_map>

#include "platform/plat.h"

namespace fs::timers {

namespace {

struct Timer {
  /** (shared: it may clear itself, or set others, while it runs) */
  std::shared_ptr<std::function<void()>> fn;
  /** setInterval's period (ms); below 0: a timeout, which runs once */
  double every = -1;
};

std::unordered_map<int, Timer>& table() {
  static std::unordered_map<int, Timer>* t = new std::unordered_map<int, Timer>();
  return *t;
}
int nextId = 1;

void fire(int id) {
  auto it = table().find(id);
  // (cleared in the meantime)
  if (it == table().end()) return;
  std::shared_ptr<std::function<void()>> fn = it->second.fn;
  if (it->second.every < 0) table().erase(it);
  else {
    double every = it->second.every;
    plat::setTimeout([id] { fire(id); }, every);
  }
  (*fn)();
}

int add(std::function<void()> fn, double ms, double every) {
  int id = nextId++;
  Timer t;
  t.fn = std::make_shared<std::function<void()>>(std::move(fn));
  t.every = every;
  table().emplace(id, std::move(t));
  plat::setTimeout([id] { fire(id); }, ms > 0 ? ms : 0);
  return id;
}

}  // namespace

int setTimeout(std::function<void()> fn, double ms) { return add(std::move(fn), ms, -1); }

void clearTimeout(int id) { table().erase(id); }

int setInterval(std::function<void()> fn, double ms) {
  // (never less than a millisecond, as in a browser: an interval of none would never let anything else run)
  double every = ms > 1 ? ms : 1;
  return add(std::move(fn), every, every);
}

void clearInterval(int id) { table().erase(id); }

int pending() { return static_cast<int>(table().size()); }

}  // namespace fs::timers
