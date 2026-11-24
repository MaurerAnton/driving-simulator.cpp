// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Reading the position, heading and time out of a picture's own metadata
 * (EXIF, XMP in JPEG/PNG/WebP).
 *
 * Origin: src/reality/imagemeta.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace fs {

struct ImageMeta {
  std::string kind;   // 'jpeg' | 'png' | 'webp' | ''
  int width = 0, height = 0;
  double lat = NAN, lon = NAN, heading = NAN, time = NAN;
  /** true/false if the metadata says so, -1 if it is silent */
  int pano = -1;
};

/** Read whatever the file carries (EXIF/XMP), leaving unknown fields as NaN. */
ImageMeta readImageMeta(const std::vector<uint8_t>& bytes);

/** Does this look like a full panorama rather than a flat photograph? */
bool looksLikePanorama(const ImageMeta& meta);

}  // namespace fs
