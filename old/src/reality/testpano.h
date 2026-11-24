// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A drawn 360° panorama with a compass rose, for checking the panorama viewer
 * without any real picture (`?reality=test`): the letter in the middle of the
 * view must be the direction the car faces.
 *
 * Origin: src/reality/testpano.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/canvas2d.h"

namespace fs {

/** The drawn panorama as an encoded JPEG, named after where it pretends to be. */
struct TestPanorama {
  std::string name;
  std::vector<uint8_t> jpeg;
};

TestPanorama makeTestPanorama(double lat, double lon, double heading);

}  // namespace fs
