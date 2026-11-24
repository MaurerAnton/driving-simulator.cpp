// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/cockpit.ts @ v1.7.7
#include "game/cockpit.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/js.h"

namespace fs {

namespace {
bool includes(const std::vector<std::string>& list, const std::string& name) { return std::find(list.begin(), list.end(), name) != list.end(); }
}  // namespace

std::optional<std::string> MirrorLook::next(const std::vector<std::string>& have) {
  std::vector<std::string> order;
  for (const std::string& n : MIRROR_ORDER)
    if (includes(have, n)) order.push_back(n);
  int i = -1;
  if (shown) {
    auto at = std::find(order.begin(), order.end(), *shown);
    i = at == order.end() ? -1 : static_cast<int>(at - order.begin());
  }
  if (i + 1 < static_cast<int>(order.size())) shown = order[static_cast<size_t>(i + 1)];
  else shown.reset();
  return shown;
}

std::optional<std::string> MirrorLook::pick(const std::string& name, const std::vector<std::string>& have) {
  if (shown == name || !includes(have, name)) shown.reset();
  else shown = name;
  return shown;
}

void MirrorLook::close() { shown.reset(); }

std::optional<std::string> MirrorLook::keep(const std::vector<std::string>& have) {
  if (shown && !includes(have, *shown)) shown.reset();
  return shown;
}

std::optional<std::string> mirrorAt(double x, double y, const std::vector<MirrorOnScreen>& mirrors, double margin) {
  const MirrorOnScreen* best = nullptr;
  double bestOut = std::numeric_limits<double>::infinity();
  for (const MirrorOnScreen& m : mirrors) {
    // how far outside the box the tap lies (0: inside)
    const double out = std::hypot(std::max({0.0, m.x0 - x, x - m.x1}), std::max({0.0, m.y0 - y, y - m.y1}));
    if (out > margin) continue;
    if (out < bestOut - 1e-9 || (std::fabs(out - bestOut) <= 1e-9 && best && m.distance < best->distance)) {
      best = &m;
      bestOut = out;
    }
  }
  if (best) return best->name;
  return std::nullopt;
}

double zoomAspect(double glassWidth, double glassHeight) {
  const double a = glassHeight > 0 ? glassWidth / glassHeight : 1.6;
  return std::max(1.4, std::min(2.6, a));
}

PanelRect zoomPanel(double w, double h, double aspect, double top) {
  // (its foot at the middle of the screen – a little lower on a short one, where the top is taken by the controls)
  const double maxH = std::max({60.0, h * 0.5 + 8 - top, h * 0.42});
  const double maxW = std::max(80.0, w * 0.94);
  double ph = std::min(maxH, maxW / aspect);
  double pw = ph * aspect;
  if (pw > maxW) {
    pw = maxW;
    ph = pw / aspect;
  }
  pw = jsRound(pw);
  ph = jsRound(ph);
  return {.x = jsRound((w - pw) / 2), .y = jsRound(top), .w = pw, .h = ph};
}

}  // namespace fs
