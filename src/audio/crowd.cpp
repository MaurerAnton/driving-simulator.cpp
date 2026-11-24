// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/crowd.ts @ v1.7.7
#include "audio/crowd.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/random.h"

namespace fs {

namespace {

/** how far a march is heard at all (m) */
const double HEARD = 320;

}  // namespace

void CrowdNoise::update(double dt, double dist, double size, bool walking) {
  const AudioContext ctx = out_.context();
  const GainNode master = out_.masterGain();
  if (!ctx || !master || ctx.state() != "running") return;
  const double near = dist < HEARD ? std::min(1.0, 22 / std::max(dist, 6.0)) : 0;
  const double loud = near * std::min(1.4, std::sqrt(size / 100));
  // the murmur: started when a crowd comes within hearing, faded out and stopped a while after it is gone
  if (loud > 0 && !murmur_) murmur_ = startMurmur(ctx, master);
  if (murmur_) {
    const Murmur& m = *murmur_;
    m.gain.gain.setTargetAtTime(0.09 * loud, ctx.currentTime(), 0.4);
    quiet_ = loud > 0 ? 0 : quiet_ + dt;
    if (quiet_ > 3) {
      m.src.stop();
      m.src.disconnect();
      m.gain.disconnect();
      murmur_.reset();
    }
  }
  // the whistles: about one a second from a crowd of a hundred on the march, fewer at the rally
  if (loud > 0 && rnd() < dt * (size / 110) * (walking ? 1 : 0.55)) whistle(ctx, master, 0.05 * loud);
}

CrowdNoise::Murmur CrowdNoise::startMurmur(const AudioContext& ctx, const GainNode& master) {
  // brown-ish noise: many voices far apart blur into it
  const int len = static_cast<int>(ctx.sampleRate() * 2);
  const AudioBuffer buf = ctx.createBuffer(1, len, ctx.sampleRate());
  std::vector<float> d(static_cast<size_t>(len));   // (buf.getChannelData(0): handed to the buffer below)
  double last = 0;
  for (int i = 0; i < len; i++) {
    last = (last + 0.06 * (rnd() * 2 - 1)) / 1.06;
    d[static_cast<size_t>(i)] = static_cast<float>(last * 3.2);
  }
  buf.copyToChannel(d, 0);
  const AudioBufferSourceNode src = ctx.createBufferSource();
  src.buffer(buf);
  src.loop(true);
  const BiquadFilterNode band = ctx.createBiquadFilter();
  band.type("bandpass");
  band.frequency.value(420);
  band.Q.value(0.6);
  const GainNode gain = ctx.createGain();
  gain.gain.value(0);
  src.connect(band).connect(gain).connect(master);
  src.start();
  return Murmur{src, gain};
}

void CrowdNoise::whistle(const AudioContext& ctx, const GainNode& master, double level) {
  const double t = ctx.currentTime() + 0.02;
  const int blasts = rnd() < 0.3 ? 2 : 1;
  for (int b = 0; b < blasts; b++) {
    const double t0 = t + b * 0.42;
    const double dur = 0.18 + rnd() * 0.45;
    const OscillatorNode o = ctx.createOscillator();
    o.type("sine");
    o.frequency.value(2700 + rnd() * 700);
    // (the pea rattles: the pitch trills some thirty times a second)
    const OscillatorNode lfo = ctx.createOscillator();
    lfo.frequency.value(26 + rnd() * 10);
    const GainNode depth = ctx.createGain();
    depth.gain.value(120 + rnd() * 90);
    lfo.connect(depth).connect(o.frequency);
    const GainNode g = ctx.createGain();
    g.gain.setValueAtTime(0, t0);
    g.gain.linearRampToValueAtTime(level, t0 + 0.02);
    g.gain.setValueAtTime(level, t0 + dur - 0.05);
    g.gain.linearRampToValueAtTime(0, t0 + dur);
    o.connect(g).connect(master);
    o.start(t0);
    lfo.start(t0);
    o.stop(t0 + dur + 0.02);
    lfo.stop(t0 + dur + 0.02);
    o.onended([o, lfo, depth, g] {
      o.disconnect();
      lfo.disconnect();
      depth.disconnect();
      g.disconnect();
    });
  }
}

}  // namespace fs
