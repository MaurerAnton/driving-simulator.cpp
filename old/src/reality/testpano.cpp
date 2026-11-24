// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A drawn 360° panorama with a compass rose, for checking the panorama
 * viewer without any real picture.
 *
 * Origin: src/reality/testpano.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).  The original draws with the canvas 2D API and hands the result
 * to the browser's JPEG encoder; here the software canvas draws it and
 * Canvas2D::toJpeg encodes it.
 */
#include "reality/testpano.h"

#include <cmath>
#include <cstdio>

namespace fs {

TestPanorama makeTestPanorama(double lat, double lon, double heading) {
  const int W = 2048;
  const int H = 1024;
  Canvas2D c(W, H);
  // sky
  c.fillGradientV(parseColor("#245fb5"), parseColor("#c4def7"));
  // ground: only the lower half keeps its own gradient
  Canvas2D ground(H / 2, H / 2);
  ground.fillGradientV(parseColor("#7d8a66"), parseColor("#3f4a36"));
  c.drawImage(ground, 0, H / 2);

  // the middle of the picture looks towards `heading`
  for (int d = 0; d < 360; d += 15) {
    double x = std::fmod(std::fmod(d - heading + 180, 360.0) + 360.0, 360.0) / 360.0 * W;
    bool main = d % 90 == 0;
    uint32_t col = main ? parseColor("#ffcc00") : parseColor("#ffffff");
    c.fillRect(x - (main ? 3 : 1), H / 2 - (main ? 60 : 24), main ? 6 : 2,
               main ? 120 : 48, col);
    const char* label = main ? "NOSW"[d / 90] == 'N'   ? "N"
                            : "NOSW"[d / 90] == 'O'   ? "E"
                            : "NOSW"[d / 90] == 'S'   ? "S"
                                                      : "W"
                          : std::to_string(d).c_str();
    std::string text = main ? std::string(1, "NOSW"[d / 90]) : std::to_string(d);
    (void)label;
    c.fillText(text, x, H / 2 - (main ? 130 : 48), main ? 110 : 28, col);
  }
  c.fillText("Test panorama - no real picture", W / 2, H * 0.78, 34, parseColor("#ffffff"));

  TestPanorama out;
  char buf[64];
  std::snprintf(buf, sizeof buf, "testpano_%.5f_%.5f_h%d.jpg", lat, lon,
                static_cast<int>(std::lround(heading)));
  out.name = buf;
  out.jpeg = c.toJpeg(90);
  return out;
}

}  // namespace fs
