// SPDX-License-Identifier: AGPL-3.0-only
// LINK: platform core
/** The page layer (platform/dom.h) on the native host, and the task type it is used with. */
#include <cassert>
#include <cstdio>

#include "core/task.h"
#include "platform/dom.h"
#include "platform/dom_internal.h"
#include "platform/native/dom_native.h"
#include "platform/native/native.h"
#include "platform/plat.h"

using namespace fs;
using namespace fs::dom;

static Task<int> twice(int v) {
  co_await plat::sleep(5);
  co_return v * 2;
}

static Task<int> sum() {
  int a = co_await twice(2);
  Task<int> kept = twice(10);
  int b = co_await kept;
  int c = co_await kept;   // a task may be awaited again
  co_return a + b + c;
}

int main() {
  // --- building, reading ---
  native::reset();
  El box = el("div", "panel wide");
  El b = el("button", "btn ghost", "Undo");
  b.type("button");
  El label = el("span", "", "Hello");
  box.append(label, b, " and text");
  body().append(box);
  assert(box.childElementCount() == 2);
  assert(native::text(box) == "HelloUndo and text");
  assert(box.hasClass("wide") && !box.hasClass("btn"));
  assert(b.parentElement() == box);
  assert(box.firstElementChild() == label && box.lastElementChild() == b);
  assert(label.nextElementSibling() == b && b.previousElementSibling() == label);
  assert(querySelector(".panel .btn.ghost") == b);
  assert(querySelector("div > button") == b);
  assert(!querySelector(".panel .missing"));
  assert(b.closest(".panel") == box);
  assert(box.isConnected() && b.isConnected());
  assert(native::html(b) == "<button class=\"btn ghost\" type=\"button\">Undo</button>");

  // --- properties, attributes, style, classes ---
  b.disabled(true);
  assert(b.disabled());
  b.disabled(false);
  b.setAttribute("aria-label", "Undo it");
  assert(b.getAttribute("aria-label") == "Undo it" && b.hasAttribute("aria-label") && !b.hasAttribute("title"));
  b.dataset("lessonId", "rbl");
  assert(b.getAttribute("data-lesson-id") == "rbl" && b.dataset("lessonId") == "rbl");
  b.style("fontSize", "12px").style("--x", "3");
  assert(b.style("font-size") == "12px" && b.style("--x") == "3");
  assert(b.toggleClass("on") && b.hasClass("on") && !b.toggleClass("on") && !b.toggleClass("on", false));
  label.textContent("Bye");
  assert(native::text(label) == "Bye" && label.childElementCount() == 0);

  // --- events: the on… property is replaced, listeners add up, clicks bubble ---
  int clicks = 0, outer = 0, first = 0;
  b.onclick([&] { first++; });
  b.onclick([&] { clicks++; });
  bool fromButton = false;
  box.on("click", [&](Event& e) {
    outer++;
    fromButton = e.target() == b && e.currentTarget() == box;
  });
  b.click();
  assert(first == 0 && clicks == 1 && outer == 1 && fromButton);
  b.onclick([&](Event& e) {
    clicks += 10;
    e.stopPropagation();
  });
  b.click();
  assert(clicks == 11 && outer == 1);
  b.disabled(true);
  b.click();
  assert(clicks == 11);
  // a listener taken away is gone, `once` runs once
  int n = 0;
  int id = box.on("ping", [&] { n++; });
  native::dispatch(box, "ping");
  box.off("ping", id);
  native::dispatch(box, "ping");
  assert(n == 1);
  El::Options once;
  once.once = true;
  box.on("pong", [&] { n += 5; }, once);
  native::dispatch(box, "pong");
  native::dispatch(box, "pong");
  assert(n == 6);

  // --- a field, a checkbox, a key ---
  El field = el("input");
  std::string typed;
  field.oninput([&, field] { typed = field.value(); });
  box.append(field);
  native::type(field, "Kassel");
  assert(typed == "Kassel");
  El check = el("input");
  check.type("checkbox");
  bool changed = false;
  check.onchange([&, check] { changed = check.checked(); });
  box.append(check);
  check.click();
  assert(changed && check.checked());
  std::string code;
  window().on("keydown", [&](Event& e) {
    code = e.code;
    e.preventDefault();
  });
  assert(!native::key("KeyM", "m"));
  assert(code == "KeyM");

  // --- moving and removing ---
  box.prepend(b);
  assert(box.firstElementChild() == b);
  label.after(b);
  assert(label.nextElementSibling() == b);
  b.remove();
  assert(!b.isConnected() && !b.parentElement());
  box.replaceChildren(label);
  assert(box.childElementCount() == 1 && native::text(box) == "Bye");
  box.replaceChildren();
  assert(native::text(box).empty());

  // --- handlers of elements that are gone are let go (dom::collect) ---
  {
    native::reset();
    int before = fs::dom::detail::listenerCount();
    El kept = el("div");   // named from here: stays, though it is not in the page
    kept.onclick([kept] { kept.toggleClass("x"); });
    {
      // a menu that is built, shown and thrown away: its buttons name each other and the panel
      El panel = el("div", "panel");
      El ok = el("button", "", "OK"), cancel = el("button", "", "Cancel");
      ok.onclick([panel, cancel] { cancel.disabled(true); panel.addClass("done"); });
      cancel.onclick([panel, ok] { ok.disabled(true); panel.remove(); });
      panel.on("pointerdown", [ok](Event&) { ok.focus(); });
      panel.append(ok, cancel);
      body().append(panel);
      assert(fs::dom::detail::listenerCount() == before + 4);
      assert(collect() == 0);   // it is in the page
      panel.remove();
      assert(collect() == 0);   // still named by `panel`, `ok`, `cancel`
    }
    assert(collect() == 3);     // nothing but its own handlers names it now
    assert(fs::dom::detail::listenerCount() == before + 1);
    // an element in the page keeps what its handlers name, also when that is detached
    El shown = el("button"), aside = el("div");
    int hits = 0;
    aside.onclick([&hits] { hits++; });
    shown.onclick([aside] { aside.click(); });
    body().append(shown);
    assert(collect() == 0);
    shown.click();
    assert(hits == 1);
  }

  // --- tasks: they start at once, wait for timers, can be awaited again ---
  plat::native::manualClock(true);
  Task<int> t = sum();
  assert(!t.done());
  int v = plat::native::wait(t);
  assert(v == 4 + 20 + 20);
  Resolver<std::string> r;
  std::string got;
  auto reader = [](Task<std::string> from, std::string* out) -> Task<void> { *out = co_await from; };
  reader(r.task(), &got).detach();
  assert(got.empty());
  r.resolve("done");
  assert(got == "done");

  // --- files: a mounted folder answers later, as a browser does ---
  plat::native::mount("map/", "tests/data/");
  bool answered = false;
  int status = -1;
  auto ask = [](std::string url, bool* answered, int* status) -> Task<void> {
    plat::Response res = co_await plat::fetch(url);
    *answered = true;
    *status = res.status;
  };
  ask("map/does-not-exist.json", &answered, &status).detach();
  assert(!answered);
  plat::native::drain();
  assert(answered && status == 404);
  ask("elsewhere/x", &answered, &status).detach();
  plat::native::drain();
  assert(status == 0);

  std::printf("dom_test: ok\n");
  return 0;
}
