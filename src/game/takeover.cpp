// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/takeover.ts @ v1.7.7
#include "game/takeover.h"

#include <cmath>

#include "platform/plat.h"
#include "sim/emergency.h"
#include "sim/traffic.h"

namespace fs {

VehicleKind takeKindOf(int style) {
  if (style == BUS_STYLE) return "bus";
  const int em = emKind(style);
  if (em == EM_AMBULANCE) return "lightTruck";
  if (em == EM_FIRE) return "truckSolo";
  return "car";
}

std::optional<PickTarget> pickAlong(double ox, double oy, double oz, double dx, double dy, double dz, const std::vector<PickBox>& boxes, double reach) {
  std::optional<PickTarget> best;
  double bt = reach;
  for (const PickBox& b : boxes) {
    const double t = rayBox(ox, oy, oz, dx, dy, dz, b);
    if (t >= 0 && t < bt) {
      bt = t;
      best = b.what;
    }
  }
  return best;
}

namespace {
/** a tap: let go within this time (ms) and this distance (px) of where it came down */
const double TAP_MS = 450;
const double TAP_PX = 12;
}  // namespace

TapPicker::TapPicker(const dom::El& canvasIn, std::shared_ptr<Camera> camera, std::function<std::optional<std::vector<PickBox>>()> boxes, std::function<void(const PickTarget&)> pick)
    : canvas(canvasIn) {
  downListener = canvas.on("pointerdown", [this](dom::Event& e) {
    if (e.pointerType == "mouse" && e.button != 0) return;
    down = Down{.id = e.pointerId, .x = e.clientX, .y = e.clientY, .t = plat::nowMs()};
  });
  upListener = canvas.on("pointerup", [this, camera = std::move(camera), boxes = std::move(boxes), pick = std::move(pick)](dom::Event& e) {
    Down& d = down;
    if (e.pointerId != d.id) return;
    d.id = -1;
    if (plat::nowMs() - d.t > TAP_MS || std::hypot(e.clientX - d.x, e.clientY - d.y) > TAP_PX) return;
    const std::optional<std::vector<PickBox>> list = boxes();
    if (!list || list->empty()) return;
    const dom::Rect r = canvas.getBoundingClientRect();
    if (r.width == 0 || r.height == 0) return;
    ndc.set(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
    ray.setFromCamera(ndc, *camera);
    const Vector3& o = ray.ray.origin;
    const Vector3& v = ray.ray.direction;
    const std::optional<PickTarget> t = pickAlong(o.x, o.y, o.z, v.x, v.y, v.z, *list);
    if (t) pick(*t);
  });
}

TapPicker::~TapPicker() {
  canvas.off("pointerdown", downListener);
  canvas.off("pointerup", upListener);
}

}  // namespace fs
