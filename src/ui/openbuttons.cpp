// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/openbuttons.ts @ v1.7.7
#include "ui/openbuttons.h"

#include <utility>

namespace fs {

OpenButtons::OpenButtons(const dom::El& parent, std::function<void(const std::string& id)> onTap) {
  el.className("openbuttons");
  el.style("position", "absolute");
  el.style("left", "50%");
  el.style("bottom", "calc(5vh + 108px)");
  el.style("transform", "translateX(-50%)");
  el.style("display", "none");
  el.style("gap", "12px");
  el.style("pointerEvents", "auto");
  el.style("maxWidth", "94vw");
  el.style("flexWrap", "wrap");
  el.style("justifyContent", "center");
  el.on("pointerdown", [onTap = std::move(onTap)](dom::Event& e) {
    const dom::El b = e.target().closest("button");
    // (not a look round with the finger, not the canvas's)
    e.stopPropagation();
    if (!b) return;
    e.preventDefault();
    onTap(b.dataset("id"));
  });
  parent.append(el);
}

void OpenButtons::show(const std::vector<OpenButton>& list) {
  std::string key;
  for (size_t i = 0; i < list.size(); i++) key += (i ? "|" : "") + list[i].id + ":" + list[i].label;
  if (key == shown) return;
  shown = key;
  el.replaceChildren();
  for (const OpenButton& b : list) {
    dom::El e = dom::el("button");
    e.type("button");
    e.dataset("id", b.id);
    e.textContent(b.label);
    e.style("minHeight", "48px");
    e.style("minWidth", "104px");
    e.style("padding", "0 18px");
    e.style("borderRadius", "24px");
    e.style("border", "none");
    e.style("background", "rgba(255, 204, 0, 0.94)");
    e.style("color", "#111");
    e.style("font", "600 15px system-ui, sans-serif");
    e.style("boxShadow", "0 2px 8px rgba(0,0,0,.35)");
    e.style("cursor", "pointer");
    e.style("touchAction", "none");
    el.append(e);
  }
  el.style("display", !list.empty() ? "flex" : "none");
}

}  // namespace fs
