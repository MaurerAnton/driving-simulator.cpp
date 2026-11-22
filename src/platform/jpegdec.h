// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Read a baseline JPEG into pixels: the pictures of the aerial photographs
 * (and any other baseline JPEG the platform is handed) are drawn into the
 * map's own canvas, so they have to be decoded here – the page has no canvas
 * under the software renderer.
 *
 * What is read: SOF0 (sequential, 8-bit), DQT, DHT, SOS, up to three
 * components with any sampling (4:4:4 … 4:2:0), restart intervals.  What is
 * not read: progressive (SOF2), arithmetic coding, 12-bit – the services
 * serve baseline.
 *
 * Origin: the JPEG decoding the browser does for <img> in the TypeScript
 * game (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fs {

/**
 * Decode a baseline JPEG.  `out` gets `width * height * 4` RGBA bytes
 * (row-major) on success; false when it is not a picture this reader knows.
 */
bool decodeJpeg(const std::vector<uint8_t>& data, std::vector<uint8_t>& out, int& width,
                int& height);

}  // namespace fs
