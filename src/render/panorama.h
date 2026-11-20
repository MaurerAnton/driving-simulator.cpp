// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Viewer for 360° panoramas (equirectangular pictures): the picture is put on
 * the inside of a sphere and looked at from its centre.  It is drawn with the
 * game's own renderer into a rectangle of the window, so no second 3D context
 * is needed.
 *
 * Origin: src/render/panorama.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).
 */
#pragma once

#include <memory>
#include <string>

#include "engine/scene.h"
#include "engine/webgl.h"
#include "platform/canvas2d.h"

namespace fs {

class PanoramaView {
 public:
  PanoramaView();
  ~PanoramaView();   // the mesh is added to the scene by pointer

  /** viewing direction relative to the middle of the picture, radians: positive = to the right / up */
  double yaw = 0;
  double pitch = 0;
  /** vertical field of view in degrees */
  double fov = 75;

  bool active() const { return hasTexture_; }

  /**
   * Load a panorama from encoded bytes (the platform fetches and decodes the
   * picture; the scaling to `maxWidth` happens on the pixels it hands in).
   */
  void show(const std::vector<uint8_t>& jpeg, int width, int height, int maxWidth);

  void dispose();
  /** Stop showing (and forget a load that is still under way). */
  void close();

  /** The pointer dragged the picture by (dx, dy) pixels in a view `viewHeight` pixels high. */
  void drag(double dx, double dy, double viewHeight);
  void zoom(double delta);

  /**
   * Draw into the rectangle `rect` (CSS pixels, relative to the top left of
   * the canvas); the rest of the canvas is cleared.
   */
  void render(WebGLRenderer& renderer, double left, double top, double width, double height);

  /** The picture's pixels while it is loaded (for the renderer's texture). */
  const std::vector<uint8_t>& pixels() const { return pixels_; }
  int width() const { return width_; }
  int height() const { return height_; }

 private:
  std::vector<uint8_t> pixels_;
  int width_ = 0, height_ = 0;
  bool hasTexture_ = false;
  Scene scene_;
  Camera camera_;
  Mesh* mesh_ = nullptr;
};

}  // namespace fs
