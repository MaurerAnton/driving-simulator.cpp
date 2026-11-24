// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Sky dome, sun and ambient light.  Colours are derived from the sun height.
 *
 * Origin: src/render/sky.ts @ v1.7.7
 */
#pragma once

#include "engine/engine.h"

namespace fs {

class Sky {
 public:
  std::shared_ptr<Mesh> dome;
  const LightPtr sun = directionalLight(Color(0xfff4e0), 3.0);
  const LightPtr hemi = hemisphereLight(Color(0xbfd6ff), Color(0x6b6a55), 1.1);
  const std::shared_ptr<Fog> fog = std::make_shared<Fog>(Color(0xc4d4e4), 300, 2400);

  Sky();
  /** Sun elevation above the horizon and compass azimuth (degrees, 180 = south). */
  void setSun(double elevationDeg, double azimuthDeg);
  /** Keep the dome centred on the camera and the shadow frustum around the focus point. */
  void follow(const Camera& camera, const Vector3& focus);

  /** the sky's colours and the sun's direction (the weather and the night reach in here) */
  const UniformPtr uSunDir = uniform(Vector3(0.4, 0.7, 0.3).normalize());
  const UniformPtr uZenith = uniform(Color(0x3f78c8));
  const UniformPtr uHorizon = uniform(Color(0xc4d4e4));
  const UniformPtr uSunColor = uniform(Color(0xfff2d0));

 private:
  Vector3 lightRight_, lightUp_;
};

}  // namespace fs
