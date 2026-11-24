// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Working mirrors.
 *
 * Each mirror is rendered from the driver's eye reflected in the mirror
 * plane, through an off-axis frustum whose near rectangle is the mirror glass
 * itself.  The picture therefore changes correctly when the driver moves the
 * head, and the whole render target is spent on what the glass shows.
 *
 * Origin: src/render/mirrors.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <memory>
#include <string>

#include "engine/scene.h"
#include "engine/webgl.h"

namespace fs {

/** Where one mirror glass sits in the car. */
struct MirrorSpec {
  std::string name;
  /** centre of the glass in car coordinates */
  Vector3 centre;
  double width = 0.16;
  double height = 0.11;
  /** unit normal of the glass (car coordinates), pointing at the cabin side */
  Vector3 normal;
  /** > 1 widens the field of view like a convex mirror */
  double convex = 1;
  std::pair<int, int> resolution{128, 96};
};

class Mirror {
 public:
  explicit Mirror(const MirrorSpec& spec);

  MirrorSpec spec;
  std::unique_ptr<Mesh> mesh;
  std::shared_ptr<RenderTarget> target;
  Camera camera;
  bool enabled = true;

  /** A housing behind the glass, aligned with it (car coordinates). */
  std::unique_ptr<Mesh> makeHousing(double depth, uint32_t color, double border = 0.012);

  /** Show a neutral grey instead of a stale picture when switched off. */
  void setEnabled(bool on);

  /**
   * Render the mirror picture.  `carMatrix` is the car's world matrix,
   * `eyeWorld` the driver's eye in world space.
   */
  void render(WebGLRenderer& renderer, Scene& scene, const Matrix4& carMatrix,
              const Vector3& eyeWorld, double far);

  /** The glass basis in car coordinates (right, up, normal). */
  Vector3 localRight;
  Vector3 localUp;
};

/** Glass normal that shows the driver the direction `look` (car coordinates). */
Vector3 mirrorNormal(const Vector3& centre, const Vector3& eye, const Vector3& look);

}  // namespace fs
