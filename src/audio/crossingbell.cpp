// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/crossingbell.ts @ v1.7.7
#include "audio/crossingbell.h"

#include <algorithm>
#include <vector>

namespace fs {

namespace {

/** how far a bell is heard at all (m) */
const double HEARD = 180;
/** strokes a second */
const double RATE = 3.2;

}  // namespace

void CrossingBell::update(double dist) {
  const AudioContext ctx = out_.context();
  const GainNode master = out_.masterGain();
  if (!ctx || !master || ctx.state() != "running" || dist >= HEARD) return;
  const double level = 0.11 * std::min(1.0, 14 / std::max(dist, 4.0));
  const double now = ctx.currentTime();
  if (next_ < now) next_ = now + 0.02;
  // (the strokes due within the next quarter of a second, each at its time)
  while (next_ < now + 0.25) {
    stroke(ctx, master, next_, level);
    next_ += 1 / RATE;
  }
}

void CrossingBell::stroke(const AudioContext& ctx, const GainNode& master, double t0, double level) {
  const GainNode g = ctx.createGain();
  g.gain.setValueAtTime(0, t0);
  g.gain.linearRampToValueAtTime(level, t0 + 0.004);
  g.gain.exponentialRampToValueAtTime(0.0005, t0 + 0.28);
  g.connect(master);
  std::vector<OscillatorNode> parts;
  const double partials[2][2] = {
      {1480, 1},
      {3960, 0.35},
  };
  for (const double* partial : partials) {
    const double f = partial[0], a = partial[1];
    const OscillatorNode o = ctx.createOscillator();
    o.type("sine");
    o.frequency.value(f);
    const GainNode pg = ctx.createGain();
    pg.gain.value(a);
    o.connect(pg).connect(g);
    o.start(t0);
    o.stop(t0 + 0.3);
    parts.push_back(o);
  }
  parts[0].onended([parts, g] {
    for (const OscillatorNode& o : parts) o.disconnect();
    g.disconnect();
  });
}

}  // namespace fs
