// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/mirrorzoom.ts @ v1.7.7
/**
 * Looking into a mirror: one of the vehicle's mirrors shown large over the
 * upper half of the picture – to see the kerb, the line of a parking bay or
 * the edge of the garage door while reversing.
 *
 * The mirror key ([[KeyY]]), the round mirror button of the on-screen
 * controls and a tap on the panel itself go round the mirrors the vehicle
 * has (left → right → inside → off, game/cockpit.ts); a tap on a mirror in
 * the driver's seat shows that one, the × puts it away.  It works from the
 * driver's seat and from the camera behind the vehicle (the picture is the
 * one the driver sees in the glass), in the car, the truck, the bus, the
 * tractor and on the motorcycle.
 *
 * The panel is a frame of the HUD with nothing in it: the picture is drawn
 * into the canvas underneath, in the same place (render/mirrorzoom.ts).
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/renderer.h"
#include "game/cockpit.h"
#include "platform/dom.h"
#include "render/mirrors.h"
#include "render/mirrorzoom.h"

namespace fs {

class Input;

/** the vehicle's mirrors as it holds them (render/car.h) */
using MirrorList = std::vector<std::unique_ptr<Mirror>>;

/** What the game tells the panel each frame. */
struct MirrorView {
  /** the vehicle's mirrors, when the view lets one be looked into – null: it does not (on foot, a menu, the free camera …) */
  const MirrorList* mirrors = nullptr;
  /** seen from the driver's seat (a tap on a mirror then shows it) */
  bool cockpit = false;
  /** the driver's eye in the world */
  Vector3 eye;
  /** how far the picture reaches (m) */
  double far = 0;
  std::string lang;
};

/** The parts of the game's rendering the panel draws with. */
struct MirrorHost {
  Renderer& renderer;
  Scene& scene;
  Camera& camera;
  dom::El canvas;
};

class MirrorZoomPanel {
 public:
  MirrorZoomPanel(const dom::El& ui, Input& input, MirrorHost host);
  MirrorZoomPanel(const MirrorZoomPanel&) = delete;
  MirrorZoomPanel& operator=(const MirrorZoomPanel&) = delete;

  /** Each frame, after the scene was drawn: the button, the panel and the picture in it. */
  void frame(const MirrorView& v);

 private:
  MirrorLook look;
  MirrorZoom zoom;
  MirrorHost host;
  const dom::El ui;
  dom::El hud;
  dom::El frameEl;
  dom::El label;
  dom::El closeBtn;
  dom::El button;
  /** the mirrors offered in the last frame (for the key, the buttons and a tap between two frames) */
  std::vector<Mirror*> mirrors;
  bool cockpit = false;
  std::string lang;
  /** where the frame was put last (written again only when it moves) */
  std::string placed;
  struct Down {
    int id = -1;
    double x = 0;
    double y = 0;
    double t = 0;
  } down;
  Vector3 v;
  Vector2 size;

  std::vector<std::string> names() const;
  /** The mirror key, the button, a tap on the panel: the next mirror (after the last: none). */
  void next();
  /** A tap on the picture at (x, y) (px of the window): a mirror there is shown large – or put away. */
  void tap(double x, double y);
};

}  // namespace fs
