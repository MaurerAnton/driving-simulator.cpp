// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/viewbuttons.ts @ v1.7.7
#include "ui/viewbuttons.h"

#include <utility>

#include "platform/plat.h"

namespace fs {

static const char* const HIDE_KEY = "fahrsim.hudHidden";

ViewButtons viewButtons(const dom::El& root, std::function<std::string(const std::string& k)> t) {
  using dom::El;
  El box = dom::el("div");
  box.className("view-btns");
  El fs = dom::el("button");
  fs.type("button");
  fs.className("view-btn");
  El hide = dom::el("button");
  hide.type("button");
  hide.className("view-btn");
  const bool canFull = dom::root().flag("requestFullscreen");
  auto fsIcon = [fs, t] {
    const bool on = static_cast<bool>(dom::fullscreenElement());
    fs.textContent(on ? "🗗" : "⛶");
    const std::string title = t(on ? "view.fullscreenOff" : "view.fullscreen");
    fs.title(title);
    fs.setAttribute("aria-label", title);
  };
  auto hidden = [root] { return root.hasClass("hud-off"); };
  auto hideIcon = [hide, hidden, t] {
    hide.textContent(hidden() ? "👁" : "◌");
    const std::string title = t(hidden() ? "view.hudShow" : "view.hudHide");
    hide.title(title);
    hide.setAttribute("aria-label", title);
  };
  std::function<void()> toggleFullscreen = [] {
    // (the original asks for `navigationUI: 'hide'`; the host's call takes no options)
    if (dom::fullscreenElement()) dom::exitFullscreen();
    else dom::root().call("requestFullscreen");
  };
  auto setHidden = [root, hideIcon](bool on) {
    root.toggleClass("hud-off", on);
    // (a convenience: a store that keeps nothing says so by its answer, which is not asked for)
    plat::sessionStorage().setItem(HIDE_KEY, on ? "1" : "");
    hideIcon();
  };
  std::function<void()> toggleHud = [setHidden, hidden] { setHidden(!hidden()); };
  fs.onclick([toggleFullscreen](dom::Event& e) {
    e.stopPropagation();
    toggleFullscreen();
  });
  hide.onclick([toggleHud](dom::Event& e) {
    e.stopPropagation();
    toggleHud();
  });
  dom::document().on("fullscreenchange", std::function<void()>(fsIcon));
  fs.hidden(!canFull);
  box.append(fs, hide);
  fsIcon();
  const bool was = plat::sessionStorage().getItem(HIDE_KEY) == std::optional<std::string>("1");
  setHidden(was);
  return ViewButtons{box, toggleHud, toggleFullscreen};
}

}  // namespace fs
