// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/boom.ts @ v1.7.7
#include "audio/boom.h"

#include <cmath>
#include <vector>

#include "core/random.h"

namespace fs {

void playBoom(const AudioContext& ctx, const AudioNode& out, double level) {
  const double t0 = ctx.currentTime() + 0.01;
  const double len = 4;
  // white noise, once, for the crack and the roar
  const AudioBuffer buf = ctx.createBuffer(1, static_cast<int>(std::floor(ctx.sampleRate() * len)), ctx.sampleRate());
  std::vector<float> data(static_cast<size_t>(buf.length()));   // (buf.getChannelData(0): handed to the buffer below)
  for (size_t i = 0; i < data.size(); i++) data[i] = static_cast<float>(rnd() * 2 - 1);
  buf.copyToChannel(data, 0);

  // the crack: bright noise, gone in a tenth of a second
  const AudioBufferSourceNode crack = ctx.createBufferSource();
  crack.buffer(buf);
  const GainNode crackGain = ctx.createGain();
  const BiquadFilterNode crackHp = ctx.createBiquadFilter();
  crackHp.type("highpass");
  crackHp.frequency.value(900);
  crackGain.gain.setValueAtTime(0.9 * level, t0);
  crackGain.gain.exponentialRampToValueAtTime(0.001, t0 + 0.12);
  crack.connect(crackHp).connect(crackGain).connect(out);
  crack.start(t0);
  crack.stop(t0 + 0.2);

  // the thump: a sine falling from 90 to 30 Hz
  const OscillatorNode thump = ctx.createOscillator();
  thump.type("sine");
  thump.frequency.setValueAtTime(90, t0);
  thump.frequency.exponentialRampToValueAtTime(30, t0 + 0.9);
  const GainNode thumpGain = ctx.createGain();
  thumpGain.gain.setValueAtTime(0.0001, t0);
  thumpGain.gain.exponentialRampToValueAtTime(1.1 * level, t0 + 0.02);
  thumpGain.gain.exponentialRampToValueAtTime(0.001, t0 + 1.4);
  thump.connect(thumpGain).connect(out);
  thump.start(t0);
  thump.stop(t0 + 1.5);

  // the roar: low noise that swells and dies away, the fire crackling in it
  const AudioBufferSourceNode roar = ctx.createBufferSource();
  roar.buffer(buf);
  const BiquadFilterNode lp = ctx.createBiquadFilter();
  lp.type("lowpass");
  lp.frequency.setValueAtTime(1800, t0);
  lp.frequency.exponentialRampToValueAtTime(260, t0 + len - 0.2);
  const GainNode roarGain = ctx.createGain();
  roarGain.gain.setValueAtTime(0.0001, t0);
  roarGain.gain.exponentialRampToValueAtTime(0.75 * level, t0 + 0.05);
  roarGain.gain.exponentialRampToValueAtTime(0.002, t0 + len - 0.1);
  roar.connect(lp).connect(roarGain).connect(out);
  roar.start(t0);
  roar.stop(t0 + len);
}

}  // namespace fs
