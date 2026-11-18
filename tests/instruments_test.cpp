// SPDX-License-Identifier: AGPL-3.0-only
/** Instrument cluster: dials, needles, telltales, throttled redraw. */
#include "../src/render/instruments.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
/** Count the pixels that are not the dial background. */
int litPixels(const std::vector<uint8_t>& px) {
  int n = 0;
  for (size_t i = 0; i < px.size(); i += 4)
    if (px[i] > 0x12 || px[i + 1] > 0x12 || px[i + 2] > 0x16) n++;
  return n;
}
}  // namespace

int main() {
  InstrumentPanel p;
  assert(p.width() == 768 && p.height() == 256);
  assert(p.pixels().size() == 768 * 256 * 4);

  // the static face is drawn: the two dials light up a good part of the panel
  int face = litPixels(p.pixels());
  std::printf("face pixels=%d\n", face);
  fflush(stdout);
  assert(face > 20000);

  DashState s;
  s.ignition = true;
  s.engineRunning = true;
  s.speedKmh = 83;
  s.rpm = 2400;
  s.gear = 3;
  s.fuel = 0.7;
  s.odometerKm = 123.4;
  s.lowBeam = true;
  s.blinkOn = true;
  s.indicator = -1;
  p.update(s, 0.0);
  assert(p.dirty());
  int withState = litPixels(p.pixels());
  assert(withState > face);   // needles and lamps add ink

  // the redraw is throttled to ~20 Hz: a second call 10 ms on changes nothing
  std::vector<uint8_t> before = p.pixels();
  p.clearDirty();
  p.update(s, 0.01);          // 10 ms after the first frame: too soon
  assert(!p.dirty());
  assert(p.pixels() == before);
  p.update(s, 0.05);          // 50 ms later: redraws
  assert(p.dirty());
  // a different state must produce a different image (needles moved)
  DashState s2 = s;
  s2.speedKmh = 120;
  s2.rpm = 4500;
  p.update(s2, 0.10);
  assert(p.pixels() != before);

  // without ignition the centre display is dark (only the face shows)
  DashState off;
  off.ignition = false;
  InstrumentPanel q;
  q.update(off, 1.0);
  int dark = litPixels(q.pixels());
  assert(dark < withState);

  // a red-line rev counter draws in the warning colour
  DashState red;
  red.ignition = true;
  red.rpm = 6900;
  InstrumentPanel r;
  r.update(red, 2.0);
  bool sawRed = false;
  const auto& px = r.pixels();
  for (size_t i = 0; i < px.size(); i += 4)
    if (px[i] > 0xc0 && px[i + 1] < 0x60 && px[i + 2] < 0x60) sawRed = true;
  assert(sawRed);

  std::printf("instruments: all checks passed (%d face pixels, %d with state)\n", face, withState);
  return 0;
}
