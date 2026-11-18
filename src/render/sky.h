// SPDX-License-Identifier: AGPL-3.0-only
/** Sky dome, sun and ambient light.  Colours are derived from the sun height. */
#pragma once

#include <memory>
#include <string>

#include "engine/materials.h"
#include "engine/scene.h"

namespace fs {

class Sky {
 public:
  Sky();

  std::unique_ptr<Mesh> dome;   // custom shader, drawn first
  DirectionalLight sun;
  HemisphereLight hemi;
  std::shared_ptr<Fog> fog;

  /** Sun elevation above the horizon and compass azimuth (degrees, 180 = south). */
  void setSun(double elevationDeg, double azimuthDeg);
  /** Keep the dome centred on the camera and the shadow frustum around the focus point. */
  void follow(const Camera& camera, const Vector3& focus);

  /** The dome's shader uniforms, as the renderer uploads them. */
  Vector3 uSunDir;
  Color uZenith;
  Color uHorizon;
  Color uSunColor;
};

}  // namespace fs
