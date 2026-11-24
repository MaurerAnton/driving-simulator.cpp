// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/closex.ts @ v1.7.7
#include "ui/closex.h"

#include <utility>

namespace fs {

dom::El closeX(const std::string& label, std::function<void()> act) {
  dom::El b = dom::el("button");
  b.type("button");
  b.className("x-close panel-x");
  b.textContent("✕");
  b.title(label);
  b.setAttribute("aria-label", label);
  b.onclick([act = std::move(act)](dom::Event& e) {
    e.stopPropagation();
    act();
  });
  return b;
}

}  // namespace fs
