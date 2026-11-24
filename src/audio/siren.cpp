// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/siren.ts @ v1.7.7
#include "audio/siren.h"

#include <algorithm>

namespace fs {

namespace {

const double SOUND_SPEED = 343;
/** at most this many horns are heard at once (the nearest) */
const size_t VOICES = 3;

}  // namespace

double sirenTone(double t, double pitch) {
  const double round = 2 * SIREN_TONE;
  const double ph = std::fmod(std::fmod(t, round) + round, round);
  return (ph < SIREN_TONE ? SIREN_HIGH : SIREN_LOW) * pitch;
}

double doppler(double source, double listener) {
  const double s = std::max(-80.0, std::min(80.0, source));
  const double l = std::max(-80.0, std::min(80.0, listener));
  return (SOUND_SPEED + l) / (SOUND_SPEED - s);
}

double sirenLevel(double d) { return 12 / std::max(12.0, d); }

void Sirens::sound(const AudioContext& ctx, const AudioNode& out, double ex, double ez, double m0, double m2, bool inside, double vx, double vz) {
  const double now = ctx.currentTime();
  const double dt = std::min(0.25, std::max(0.0, now - last_));
  last_ = now;
  std::vector<Near>& near = near_;
  // (JavaScript's sort keeps cars equally far in the order they came)
  std::stable_sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.d < b.d; });
  if (near.size() > VOICES) near.resize(VOICES);
  // the listener's right, along the ground (the camera's x axis)
  const double hyp = std::hypot(m0, m2);
  const double rl = hyp != 0 && hyp == hyp ? hyp : 1;
  const double rx = m0 / rl;
  const double rz = m2 / rl;
  std::unordered_set<int>& heard = heard_;
  heard.clear();
  for (const Near& car : near) {
    const double d = car.d;
    heard.insert(car.id);
    auto found = voices_.find(car.id);
    if (found == voices_.end()) found = voices_.emplace(car.id, voice(ctx, out, car.phase, car.pitch)).first;
    Voice& v = found->second;
    v.quiet = 0;
    const double dist = std::max(0.5, d);
    // from the horn to the listener
    const double ux = (ex - car.x) / dist;
    const double uz = (ez - car.z) / dist;
    // (the horn coming towards the listener, the listener towards the horn: m/s)
    const double source = (-std::sin(car.yaw) * ux - std::cos(car.yaw) * uz) * car.v;
    const double listener = -(vx * ux + vz * uz);
    const double f = sirenTone(now + v.phase, v.pitch) * doppler(source, listener);
    v.osc[0].frequency.setTargetAtTime(f, now, 0.01);
    v.osc[1].frequency.setTargetAtTime(f * 1.007, now, 0.01);
    v.level.gain.setTargetAtTime(sirenLevel(dist) * (inside ? 0.12 : 0.22), now, 0.06);
    // far away the high notes are lost; through the walls of a closed cab more so
    v.dull.frequency.setTargetAtTime((inside ? 2400 : 9000) / (1 + dist / 220), now, 0.1);
    if (v.pan) v.pan.pan.setTargetAtTime(std::max(-1.0, std::min(1.0, ((car.x - ex) * rx + (car.z - ez) * rz) / dist)) * 0.8, now, 0.05);
  }
  // the horns no longer heard: quiet, and after a while taken down
  for (auto it = voices_.begin(); it != voices_.end();) {
    Voice& v = it->second;
    if (heard.count(it->first)) {
      ++it;
      continue;
    }
    v.level.gain.setTargetAtTime(0, now, 0.15);
    v.quiet += dt;
    if (v.quiet > 1.5) {
      for (const OscillatorNode& o : v.osc) {
        o.stop();
        o.disconnect();
      }
      (v.pan ? AudioNode(v.pan) : AudioNode(v.level)).disconnect();
      it = voices_.erase(it);
    } else ++it;
  }
}

Sirens::Voice Sirens::voice(const AudioContext& ctx, const AudioNode& out, double phase, double pitch) {
  const GainNode mix = ctx.createGain();
  mix.gain.value(0.5);
  std::vector<OscillatorNode> osc;
  for (const char* type : {"sawtooth", "square"}) {
    const OscillatorNode o = ctx.createOscillator();
    o.type(type);
    o.frequency.value(SIREN_HIGH * pitch);
    const GainNode g = ctx.createGain();
    g.gain.value(std::string_view(type) == "square" ? 0.35 : 0.65);
    o.connect(g).connect(mix);
    o.start();
    osc.push_back(o);
  }
  const BiquadFilterNode body = ctx.createBiquadFilter();
  body.type("peaking");
  body.frequency.value(1350);
  body.Q.value(1.1);
  body.gain.value(7);
  const BiquadFilterNode dull = ctx.createBiquadFilter();
  dull.type("lowpass");
  dull.frequency.value(6000);
  dull.Q.value(0.7);
  const GainNode level = ctx.createGain();
  level.gain.value(0);
  mix.connect(body).connect(dull).connect(level);
  StereoPannerNode pan;
  if (ctx.hasStereoPanner()) {
    pan = ctx.createStereoPanner();
    level.connect(pan).connect(out);
  } else level.connect(out);
  return Voice{osc, body, dull, level, pan, phase, pitch, 0};
}

void Sirens::stop() {
  for (auto& [id, v] : voices_) {
    for (const OscillatorNode& o : v.osc) {
      o.stop();
      o.disconnect();
    }
    (v.pan ? AudioNode(v.pan) : AudioNode(v.level)).disconnect();
  }
  voices_.clear();
}

}  // namespace fs
