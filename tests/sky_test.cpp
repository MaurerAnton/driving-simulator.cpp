// SPDX-License-Identifier: AGPL-3.0-only
/** Sky: sun direction, day/night colours, dome placement. */
#include "../src/render/sky.h"
#include <cassert>
#include <cstdio>

using namespace fs;

int main() {
  Sky sky;

  // the dome exists, uses the sky shader and is drawn behind everything
  assert(sky.dome != nullptr);
  assert(sky.dome->kind == MaterialKind::Sky);
  assert(sky.dome->renderOrder == -1000);
  assert(!sky.dome->frustumCulled);
  assert(sky.dome->name == "sky");

  // the default sun is high and the light levels are daylit
  assert(sky.uSunDir.y > 0.5);
  assert(std::fabs(sky.uSunDir.length() - 1.0) < 1e-9);
  assert(sky.sun.intensity > 3.0);
  assert(sky.hemi.intensity > 1.0);

  // the fog follows the horizon colour
  assert(std::fabs(sky.fog->color.b - sky.uHorizon.b) < 1e-9);
  assert(sky.fog->near == 300 && sky.fog->far == 2400);

  // a low sun dims the light and darkens the sky
  double dayIntensity = sky.sun.intensity;
  double dayZenithB = sky.uZenith.b;
  sky.setSun(-6, 180);   // just below the horizon
  assert(sky.sun.intensity < dayIntensity);
  assert(sky.sun.intensity < 0.1);
  assert(sky.uZenith.b < dayZenithB + 1e-9);
  assert(sky.hemi.intensity < 0.4);

  // north is -Z, east is +X: azimuth 90 points east
  sky.setSun(0, 90);
  assert(sky.uSunDir.x > 0.99 && std::fabs(sky.uSunDir.z) < 1e-6);
  sky.setSun(0, 0);
  assert(sky.uSunDir.z < -0.99);   // north
  sky.setSun(0, 180);
  assert(sky.uSunDir.z > 0.99);    // south

  // follow() parks the dome on the camera and aims the sun at the focus
  Camera cam;
  cam.position.set(100, 50, -20);
  Vector3 focus(7, 3, -11);
  sky.follow(cam, focus);
  assert(sky.dome->position.x == 100 && sky.dome->position.y == 50);
  assert(sky.sun.target.position.x == 7 && sky.sun.target.position.z == -11);
  sky.dome->updateMatrixWorld();
  Vector3 wp;
  sky.dome->getWorldPosition(wp);
  assert(std::fabs(wp.x - 100) < 1e-9);

  std::printf("sky: all checks passed\n");
  return 0;
}
