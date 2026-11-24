// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/mirrorzoom.ts @ v1.7.7
/**
 * A mirror looked into: its picture drawn large over a part of the screen.
 *
 * The mirror's own camera (render/mirrors.ts, the driver's eye reflected in
 * the glass) is pointed at what the glass shows – in the shape of the panel,
 * and for a glass far from the eye (the door mirror on the passenger's side
 * is more than a metre and a half away: it shows a few degrees) widened to
 * at least MIN_ANGLE up and down, still far narrower than the view through
 * the windscreen – and renders into a picture as sharp as the panel it
 * fills.  That picture is then laid onto the screen, after the scene,
 * flipped left to right as a mirror shows it.  For parking: the kerb, the
 * line of the bay or the edge of the garage door beside the rear wheel.
 *
 * Costs one more picture of the scene each frame while it is open (without a
 * new shadow map: the scene's of this frame is still right).
 *
 * (The picture is 8 bits a channel here, linear; the original renders it in half floats.)
 */
#pragma once

#include <optional>

#include "engine/engine.h"
#include "engine/renderer.h"
#include "render/mirrors.h"

namespace fs {

class MirrorZoom {
 public:
  /** a rectangle of the canvas, CSS pixels from its top left corner */
  struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
  };
  struct Options {
    /** how far up and down the picture reaches at least (the tangent; the inside mirror in the HUD: 0) – default MIN_TAN */
    std::optional<double> minTan;
    /** false: lay the last picture again without drawing the scene anew */
    bool refresh = true;
  };

  MirrorZoom();
  MirrorZoom(const MirrorZoom&) = delete;
  MirrorZoom& operator=(const MirrorZoom&) = delete;

  /**
   * Draw the picture of `mirror` into the rectangle `rect` of the canvas (CSS
   * pixels of the canvas, from its top left corner), after the scene was
   * drawn.  `frame`: the world matrix of what the mirror is fixed to, `eye`:
   * the driver's eye in the world.  False: nothing to show (the eye behind the glass).
   */
  bool draw(Renderer& renderer, Scene& scene, Mirror& mirror, const Matrix4& frame, const Vector3& eye, double far, const Rect& rect,
            const Options& opts);
  bool draw(Renderer& renderer, Scene& scene, Mirror& mirror, const Matrix4& frame, const Vector3& eye, double far, const Rect& rect) {
    return draw(renderer, scene, mirror, frame, eye, far, rect, Options());
  }
  void dispose();

 private:
  RenderTargetPtr target_;
  std::shared_ptr<Mesh> quad_;
  std::shared_ptr<Scene> screen_ = make<Scene>();
  std::shared_ptr<Camera> flat_ = orthographicCamera(-1, 1, 1, -1, 0, 1);
  Vector2 size_;

  /** The mirror's picture into the target. */
  void picture(Renderer& renderer, Scene& scene, Mirror& mirror);
  /** The picture laid onto the screen, over what is there. */
  void lay(Renderer& renderer, const Rect& rect);
};

}  // namespace fs
