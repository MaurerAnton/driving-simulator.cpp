// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/screentap.ts @ v1.7.7
#include "ui/screentap.h"

#include <cmath>

#include "platform/plat.h"

namespace fs {

namespace {

/** a tap: let go within this time (ms) and this distance (px) of where it came down */
const double TAP_MS = 450;
const double TAP_PX = 12;

}  // namespace

ScreenTap::ScreenTap(dom::El canvas_, std::shared_ptr<Camera> camera_, std::function<std::string()> mode, std::function<void()> openMap)
    : canvas(canvas_), camera(std::move(camera_)) {
  canvas.on("pointerdown", [this](dom::Event& e) {
    if (e.pointerType == "mouse" && e.button != 0) return;
    down = Down{e.pointerId, e.clientX, e.clientY, plat::nowMs()};
  });
  canvas.on("pointerup", [this, mode = std::move(mode), openMap = std::move(openMap)](dom::Event& e) {
    Down& d = down;
    if (e.pointerId != d.id) return;
    d.id = -1;
    if (plat::nowMs() - d.t > TAP_MS || std::hypot(e.clientX - d.x, e.clientY - d.y) > TAP_PX) return;
    // (only a screen the game named a moment ago: not after the drive has ended)
    VehicleScreen* const screen_ = plat::nowMs() - setAt < 500 ? screen : nullptr;
    Object3D* const mesh = screen_ ? screen_->hitMesh() : nullptr;
    if (!mesh || !hit(*mesh, *camera, canvas, e.clientX, e.clientY)) return;
    if (mode() == "minimap") peek = !peek;
    else openMap();
  });
}

bool ScreenTap::covers(double x, double y) {
  VehicleScreen* const screen_ = plat::nowMs() - setAt < 500 ? screen : nullptr;
  Object3D* const mesh = screen_ ? screen_->hitMesh() : nullptr;
  return mesh && hit(*mesh, *camera, canvas, x, y);
}

void ScreenTap::setScreen(VehicleScreen* screen_) {
  screen = screen_;
  setAt = plat::nowMs();
}

bool ScreenTap::hit(Object3D& mesh, const Camera& camera_, const dom::El& canvas_, double x, double y) {
  const dom::Rect r = canvas_.getBoundingClientRect();
  ndc.set(((x - r.left) / r.width) * 2 - 1, -((y - r.top) / r.height) * 2 + 1);
  ray.setFromCamera(ndc, camera_);
  hits = ray.intersectObject(mesh, false);
  return hits.size() > 0;
}

}  // namespace fs
