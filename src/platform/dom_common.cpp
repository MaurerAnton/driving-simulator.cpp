// SPDX-License-Identifier: AGPL-3.0-only
/** The parts of dom.h that are the same on every host: the handlers and the small helpers. */
#include <unordered_map>

#include "platform/dom.h"
#include "platform/dom_internal.h"

namespace fs::dom {

namespace detail {

std::vector<int>* census = nullptr;

namespace {
std::unordered_map<int, Handler>& table() {
  static std::unordered_map<int, Handler> t;
  return t;
}
int nextListener = 1;
}  // namespace

int addListener(Handler fn) {
  int id = nextListener++;
  table().emplace(id, std::move(fn));
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
  Handler fn = it->second;
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
