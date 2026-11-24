// SPDX-License-Identifier: AGPL-3.0-only
/** platform/observers.h in the browser (library_observers.js). */
#include <emscripten.h>

#include <unordered_map>

#include "platform/observers.h"

extern "C" {
void fso_create(int kind, int id);
void fso_observe(int id, int element, const char* filter);
void fso_disconnect(int id);
int fso_offset_parent(int element);
}

namespace fs::plat::observerBackend {

namespace {
int nextId = 1;
std::unordered_map<int, std::function<void()>>& callbacks() {
  static std::unordered_map<int, std::function<void()>> m;
  return m;
}
}  // namespace

int create(int kind, std::function<void()> fn) {
  const int id = nextId++;
  callbacks()[id] = std::move(fn);
  fso_create(kind, id);
  return id;
}

void observe(int observer, int element, const std::vector<std::string>& attributeFilter) {
  if (!observer) return;
  std::string names;
  for (size_t i = 0; i < attributeFilter.size(); i++) names += (i ? "\n" : "") + attributeFilter[i];
  fso_observe(observer, element, names.c_str());
}

void disconnect(int observer) {
  if (!observer) return;
  fso_disconnect(observer);
  callbacks().erase(observer);
}

int offsetParent(int element) { return fso_offset_parent(element); }

void fire(int id) {
  auto it = callbacks().find(id);
  if (it == callbacks().end()) return;
  std::function<void()> fn = it->second;   // (the callback may disconnect its own observer)
  fn();
}

}  // namespace fs::plat::observerBackend

extern "C" EMSCRIPTEN_KEEPALIVE void fso_fire(int id) { fs::plat::observerBackend::fire(id); }
