// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Working mirrors.
 *
 * Each mirror is rendered from the driver's eye reflected in the mirror
 * plane, through an off-axis frustum whose near rectangle is the mirror glass
 * itself.  The picture therefore changes correctly when the driver moves the
 * head, and the whole render target is spent on what the glass shows.
 *
 * Origin: src/render/mirrors.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/renderer.h"

namespace fs {

struct MirrorSpec {
  std::string name;
  /** centre of the glass in car coordinates */
  Vector3 centre;
  double width = 0;
  double height = 0;
  /** unit normal of the glass (car coordinates), pointing at the cabin side */
  Vector3 normal;
  /** > 1 widens the field of view like a convex mirror */
  double convex = 1;
  std::array<int, 2> resolution = {0, 0};
};

class Mirror {
 public:
  /** (the mirror's own: `place` moves the centre, the size and the normal in it) */
  MirrorSpec spec;
  std::shared_ptr<Mesh> mesh;
  RenderTargetPtr target;
  const std::shared_ptr<Camera> camera = perspectiveCamera();
  bool enabled = true;
  /** what moves with the glass when it is put elsewhere (place): its housing */
  std::vector<ObjectPtr> attached;

  explicit Mirror(const MirrorSpec& spec);
  Mirror(const Mirror&) = delete;
  Mirror& operator=(const Mirror&) = delete;

  /** A housing behind the glass, aligned with it (car coordinates). */
  std::shared_ptr<Mesh> makeHousing(double depth, const MaterialPtr& material, double border = 0.012);

  /** Show a neutral grey instead of a stale picture when the mirror is switched off. */
  void setEnabled(bool on);

  /**
   * Render the mirror picture.  `carMatrix` is the car's world matrix,
   * `eyeWorld` the driver's eye in world space.
   */
  void render(Renderer& renderer, Scene& scene, const Matrix4& carMatrix, const Vector3& eyeWorld, double far);

  /**
   * Point the mirror's camera at what the glass shows the eye.  `zoom` > 1
   * shows less of it, larger; `aspect` (width : height, 0: the glass's own)
   * widens the picture in one direction to that shape; `minTan` widens it
   * until it reaches at least that far up and down (the tangent of the half
   * angle) – for looking into a mirror far from the eye (render/mirrorzoom.ts).
   * False: the eye is behind the glass.
   */
  bool aim(const Matrix4& carMatrix, const Vector3& eyeWorld, double far, double zoom = 1, double aspect = 0, double minTan = 0);

  /**
   * Put the glass elsewhere, as on a rail: `shift` (m) along the vehicle's
   * x axis and `scale` times its size – with what is `attached` to it.  With
   * `eye` (the driver's eye, vehicle coordinates) it is turned so that it
   * still shows the eye the same direction as where it was built.
   */
  void place(double shift, double scale, const Vector3* eye);

  void dispose();

 private:
  Vector3 localRight_;
  Vector3 localUp_;
  /** the glass as it was built (place moves it from there) */
  struct Base {
    Vector3 centre;
    Vector3 normal;
    Vector3 right;
    Vector3 up;
    double width = 0;
    double height = 0;
  };
  std::optional<Base> base_;
  std::string placed_;
};

/** Glass normal that shows the driver the direction `look` (car coordinates). */
Vector3 mirrorNormal(const Vector3& centre, const Vector3& eye, const Vector3& look);

}  // namespace fs
