// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The screen in the cockpit of the truck, the bus, the tractor and on the
 * tram's desk: in the driver's seat it shows the live map – the one of the
 * HUD's corner, which is hidden meanwhile (ui/hud.ts `screen`) – and
 * otherwise a still picture of a map.  (The car's centre display is built
 * with its dashboard, render/car.ts.)
 *
 * A dark bezel with the lit picture in it, on a short arm to whatever it is
 * fixed to, and turned so that the driver looks at it square on.  It stays
 * solid when the cab is drawn see-through, like the instruments.
 *
 * Origin: src/render/navscreen.ts @ v1.7.7
 */
#pragma once

#include <memory>

#include "engine/engine.h"
#include "platform/canvas.h"

namespace fs {

/** A vehicle's screen as the game hands it to the HUD: what to draw the map on, and the switch between the map and the still picture. */
class VehicleScreen {
 public:
  const Canvas canvas;

  virtual ~VehicleScreen() = default;
  /** told after each picture the HUD drew into the canvas */
  virtual void drawn() = 0;
  /** the live map (true) or the still picture */
  virtual void setLive(bool on) = 0;
  /** the picture in the scene – a tap on it in the driver's seat opens the big map or brings back the corner map (ui/screentap.ts); null: none */
  virtual Object3D* hitMesh() { return nullptr; }

 protected:
  explicit VehicleScreen(const Canvas& c) : canvas(c) {}
};

class NavScreen : public VehicleScreen {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  /** `width`: of the picture (m); the height follows from its 16 : 9. */
  explicit NavScreen(double width);

  /**
   * Fix it in `parent` with its middle at `at`, turned to face `eye` (both in
   * the parent's coordinates), on an arm back to `mount` (the dashboard, a
   * post of the cab).
   */
  NavScreen& place(Object3D& parent, const Vector3& at, const Vector3& eye, const Vector3* mount = nullptr);

  Object3D* hitMesh() override;

  void setLive(bool on) override;

  void drawn() override;

 private:
  const TexturePtr texture_ = canvasTexture(canvas);
  std::shared_ptr<Mesh> face_;
};

}  // namespace fs
