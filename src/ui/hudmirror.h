// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hudmirror.ts @ v1.7.7
/**
 * The inside rear-view mirror where the player wants it.
 *
 * The settings (Settings.insideMirror) choose between two ways:
 *
 *  - "in the car": the real glass stays in the car, moved to the left or the
 *    right along the top of the windscreen as if on a rail, and larger or
 *    smaller – turned as it moves, so that it still shows straight back
 *    (render/mirrors.ts Mirror.place; the housing goes with it, the stem
 *    stays);
 *  - "in the HUD": the glass is taken out of the car (in the driver's seat)
 *    and its picture drawn into a frame of the HUD instead – the same
 *    picture, from the driver's eye reflected in the glass where it was
 *    built (render/mirrorzoom.ts).  The frame is dragged where it suits the
 *    player, its corner grip makes it larger or smaller; both are kept in
 *    this browser with the other settings.
 *
 * The frame costs one picture of the scene every other frame (each frame
 * with "all mirrors every frame"), at the frame's own size: no more than the
 * glass in the car, whose picture is then not drawn.
 */
#pragma once

#include <optional>
#include <string>

#include "engine/engine.h"
#include "game/settings.h"
#include "platform/dom.h"
#include "render/mirrors.h"
#include "render/mirrorzoom.h"
#include "ui/mirrorzoom.h"

namespace fs {

/** What the game tells the mirror each frame. */
struct HudMirrorView {
  /** the mirrors of the vehicle shown – null: none (the menu, a replay …) */
  const MirrorList* mirrors = nullptr;
  /** in the driver's seat (the HUD frame shows only there) */
  bool cockpit = false;
  /** the driver's eye in the world, and in the vehicle's coordinates (null: not known) */
  Vector3 eye;
  std::optional<Vector3> eyeLocal = std::nullopt;
  /** how far the picture reaches (m) */
  double far = 0;
  /** draw the scene anew this frame (else the last picture is laid again) */
  bool refresh = false;
  std::string lang;
};

class HudMirror {
 public:
  HudMirror(const dom::El& ui, MirrorHost host, Settings& settings);
  HudMirror(const HudMirror&) = delete;
  HudMirror& operator=(const HudMirror&) = delete;

  /** Each frame, after the scene was drawn: the glass in the car where the settings put it – or its picture in the HUD. */
  void frame(const HudMirrorView& v);

 private:
  MirrorZoom zoom;
  MirrorHost host;
  const dom::El ui;
  Settings& settings;
  dom::El frameEl;
  dom::El grip;
  Vector2 size;
  std::string lang;
  std::string placed;
  /** the inside mirror of the last frame, and whether its glass is shown in the car */
  Mirror* last = nullptr;
  struct Drag {
    int id = 0;
    bool resize = false;
    double x = 0;
    double y = 0;
    double fx = 0;
    double fy = 0;
    double w = 0;
    bool moved = false;
  };
  std::optional<Drag> drag;

  double zoomNow() const;
  /** The glass and its housing in the car shown or not. */
  void show(Mirror& mirror, bool on);
};

}  // namespace fs
