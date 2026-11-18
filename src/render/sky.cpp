// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Sky dome, sun and ambient light.
 *
 * Origin: src/render/sky.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The shader source itself lives in engine/materials.h (fragmentShaderFor
 * with MaterialKind::Sky), so this file carries the scene setup and the
 * colour maths.
 */
#include "render/sky.h"

#include <cmath>

namespace fs {

Sky::Sky() : sun(0xfff4e0, 3.0), hemi(0xbfd6ff, 0x6b6a55, 1.1) {
  fog = std::make_shared<Fog>(0xc4d4e4, 300, 2400);
  uSunDir.set(0.4, 0.7, 0.3).normalize();
  uZenith.setHex(0x3f78c8);
  uHorizon.setHex(0xc4d4e4);
  uSunColor.setHex(0xfff2d0);

  dome = std::make_unique<Mesh>(makeSphereGeometry(1, 32, 16), MaterialKind::Sky);
  dome->frustumCulled = false;
  dome->renderOrder = -1000;
  dome->name = "sky";
  setSun(52, 215);
}

void Sky::setSun(double elevationDeg, double azimuthDeg) {
  double el = (elevationDeg * PI) / 180.0;
  double az = (azimuthDeg * PI) / 180.0;
  // compass azimuth: 0 = north (-Z), 90 = east (+X)
  uSunDir.set(std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el));
  sun.position.copy(uSunDir).multiplyScalar(400);
  double day = MathUtils::smoothstep(elevationDeg, -4, 18);
  double warm = 1 - MathUtils::smoothstep(elevationDeg, 2, 30);
  sun.color.set(1, 0.96 - 0.3 * warm, 0.88 - 0.5 * warm);
  sun.intensity = 3.2 * day;
  hemi.intensity = 0.25 + 0.95 * day;
  Color zenith;
  zenith.setHex(0x0a1020).lerp(Color().setHex(0x3a74c6), day);
  Color horizon;
  horizon.setHex(0x1a2030).lerp(Color().setHex(0xc2d3e4), day);
  Color warmC;
  warmC.setHex(0xf0b890);
  horizon.lerp(warmC, warm * day * 0.45);
  uZenith.copy(zenith);
  uHorizon.copy(horizon);
  fog->color.copy(horizon);
}

void Sky::follow(const Camera& camera, const Vector3& focus) {
  dome->position.copy(camera.position);
  dome->scale.set(1, 1, 1);
  sun.position.set(focus.x + uSunDir.x * 300, focus.y + uSunDir.y * 300, focus.z + uSunDir.z * 300);
  sun.target.position.copy(focus);
  sun.target.updateMatrixWorld();
}

}  // namespace fs
