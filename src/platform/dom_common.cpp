// SPDX-License-Identifier: AGPL-3.0-only
/** The parts of dom.h that are the same on every host: the handlers and the small helpers. */
#include <unordered_map>
#include <vector>

#include "platform/dom.h"
#include "platform/dom_internal.h"

namespace fs::dom {

namespace detail {

std::vector<int>* census = nullptr;

namespace {
struct Listener {
  Handler fn;
  /** the element it is on */
  int owner = 0;
  /** the elements the handler names (one entry for each El it holds) */
  std::vector<int> names;
};
std::unordered_map<int, Listener>& table() {
  static std::unordered_map<int, Listener> t;
  return t;
}
int nextListener = 1;
}  // namespace

int addListener(Handler fn, int owner) {
  int id = nextListener++;
  Listener l;
  l.owner = owner;
  // which elements does it name?  Copying the handler copies the Els it holds, and each copy is noted.
  {
    std::vector<int> seen;
    census = &seen;
    {
      Handler copy = fn;
    }
    census = nullptr;
    l.names = std::move(seen);
  }
  l.fn = std::move(fn);
  table().emplace(id, std::move(l));
  return id;
}

void dropListener(int id) { table().erase(id); }

Handler wrap(std::function<void()> fn) {
  return [fn = std::move(fn)](Event&) { fn(); };
}

void dispatch(int listener, Event& e) {
  auto it = table().find(listener);
  if (it == table().end()) return;
  // (a copy: the handler may take itself away while it runs)
  Handler fn = it->second.fn;
  fn(e);
}

int listenerCount() { return static_cast<int>(table().size()); }

}  // namespace detail

El el(const char* tag, const std::string& cls, const std::string& text) {
  El e = El::of(backend::create(tag));
  if (!cls.empty()) e.className(cls);
  if (!text.empty()) e.textContent(text);
  return e;
}

El window() { return El::of(backend::special(0)); }
El document() { return El::of(backend::special(1)); }
El body() { return El::of(backend::special(2)); }
El root() { return El::of(backend::special(3)); }
El getElementById(const std::string& id) { return El::of(backend::byId(id.c_str())); }
El querySelector(const std::string& selector) { return El::of(backend::query(backend::special(1), selector.c_str())); }
El fullscreenElement() { return El::of(backend::documentElementOf("fullscreenElement")); }
El activeElement() { return El::of(backend::documentElementOf("activeElement")); }
El pointerLockElement() { return El::of(backend::documentElementOf("pointerLockElement")); }
void exitFullscreen() { backend::exitFullscreen(); }

int collect() {
  auto& t = detail::table();
  const int limit = backend::handleLimit();
  if (limit <= 0) return 0;
  std::vector<uint8_t> connected;
  std::vector<int> tree;
  backend::liveness(limit, connected, tree);
  // how often each element is named by handlers, and the handlers on each element
  std::vector<int> byHandlers(static_cast<size_t>(limit), 0);
  std::unordered_map<int, std::vector<int>> on;
  for (const auto& kv : t) {
    on[kv.second.owner].push_back(kv.first);
    for (int h : kv.second.names)
      if (h > 0 && h < limit) byHandlers[static_cast<size_t>(h)]++;
  }
  std::unordered_map<int, std::vector<int>> inTree;
  for (int h = 1; h < limit; h++)
    if (tree[static_cast<size_t>(h)]) inTree[tree[static_cast<size_t>(h)]].push_back(h);
  // alive: in the page, or named by something that is not a handler; and from there on what those reach
  std::vector<uint8_t> alive(static_cast<size_t>(limit), 0);
  std::vector<int> todo;
  auto reach = [&](int h) {
    if (h <= 0 || h >= limit || alive[static_cast<size_t>(h)]) return;
    alive[static_cast<size_t>(h)] = 1;
    todo.push_back(h);
  };
  for (int h = 1; h < limit; h++)
    if (connected[static_cast<size_t>(h)] || backend::refCount(h) > byHandlers[static_cast<size_t>(h)]) reach(h);
  std::unordered_map<int, bool> treeDone;
  while (!todo.empty()) {
    int h = todo.back();
    todo.pop_back();
    // (an element that is kept keeps the whole detached tree it hangs in: it may be put back into the page)
    int tr = tree[static_cast<size_t>(h)];
    if (tr && !treeDone[tr]) {
      treeDone[tr] = true;
      for (int o : inTree[tr]) reach(o);
    }
    auto it = on.find(h);
    if (it == on.end()) continue;
    for (int id : it->second)
      for (int named : t[id].names) reach(named);
  }
  std::vector<int> gone;
  for (const auto& kv : t)
    if (kv.second.owner > 0 && kv.second.owner < limit && !alive[static_cast<size_t>(kv.second.owner)]) gone.push_back(kv.first);
  // (the handlers are moved out before they are destroyed: destroying one lets go of elements, which may free more)
  std::vector<Handler> dying;
  dying.reserve(gone.size());
  for (int id : gone) {
    auto it = t.find(id);
    dying.push_back(std::move(it->second.fn));
    t.erase(it);
    backend::forgetListener(id);
  }
  dying.clear();
  return static_cast<int>(gone.size());
}

/** dataset.fooBar ↔ the attribute data-foo-bar */
static std::string dataName(const std::string& name) {
  std::string out = "data-";
  for (char c : name) {
    if (c >= 'A' && c <= 'Z') {
      out += '-';
      out += static_cast<char>(c - 'A' + 'a');
    } else out += c;
  }
  return out;
}

const El& El::dataset(const std::string& name, const std::string& value) const { return setAttribute(dataName(name), value); }
std::string El::dataset(const std::string& name) const { return getAttribute(dataName(name)); }

}  // namespace fs::dom
