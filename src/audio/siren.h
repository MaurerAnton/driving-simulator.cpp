// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/siren.ts @ v1.7.7
/**
 * The two-tone horn of the emergency vehicles – the German Einsatzhorn
 * ("Martinshorn"): two tones a pure fourth apart in turn, here a′ and d″
 * (440 and 587 Hz), within the range the standard (DIN 14610) allows.  Made
 * with the Web Audio API like every other sound of the game: two horns tuned
 * a little apart (they beat against each other), brassy, louder the nearer
 * it is, duller far away and through the walls of a closed cab, from the
 * side it comes from – and higher while it comes nearer, lower as it goes
 * away (the Doppler effect).
 *
 * It plays in the game's AudioContext (src/audio/audio.h hands it out);
 * every frame the game passes the traffic's cars – those of them on a call
 * sound (src/sim/emergency) – and where the listener is.
 *
 * The cars and the listener are taken as they are (the original's TrafficCar and
 * SirenEar, of which only a few fields are read): any car that has `id`, `x`, `z`,
 * `yaw`, `v` and `em` – its emergency state or none, with `call`, `phase`, `pitch` –
 * and any listener with a `position` (x, z) and a `matrixWorld` (its `elements`: the
 * camera of the engine is one).
 */
#pragma once

#include <cmath>
#include <map>
#include <unordered_set>
#include <vector>

#include "platform/audio.h"

namespace fs {

/** the two tones (Hz): a′ and d″, a pure fourth */
inline constexpr double SIREN_LOW = 440;
inline constexpr double SIREN_HIGH = 587.33;
/** how long each of the two tones lasts (s) */
inline constexpr double SIREN_TONE = 0.7;

/** The tone of a horn `t` seconds into its play (Hz, before the Doppler effect): the high one first, then the low one. */
double sirenTone(double t, double pitch = 1);

/**
 * How much higher a sound is heard than it is made (the Doppler effect):
 * the source comes towards the listener at `source` m/s, the listener goes
 * towards the source at `listener` m/s (negative: away).
 */
double doppler(double source, double listener);

/** How loud a horn is heard `d` metres away (1 within 12 m, then 6 dB less for each doubling of the distance). */
double sirenLevel(double d);

namespace sirenDetail {

/** a car of the traffic, however the traffic keeps its cars (as they are, or by pointers to them) */
template <class Item>
const auto& carOf(const Item& item) {
  if constexpr (requires { item.id; }) return item;
  else return *item;
}

/** `car.em`, however the car keeps it (an optional, a pointer): a pointer to it, or null */
template <class Em>
auto emOf(const Em& em) -> decltype(&*em) {
  return em ? &*em : nullptr;
}

}  // namespace sirenDetail

class Sirens {
 public:
  /**
   * Every frame: the cars of the traffic (those on a call sound), the
   * listener, whether the listener sits in a closed cab, and how the
   * listener moves (m/s, x and z).  Nothing happens before the game's sound
   * is started (`ctx` empty) or while it is suspended.
   */
  template <class Cars, class Ear>
  void update(const AudioContext& ctx, const AudioNode& out, const Cars& cars, const Ear& ear, bool inside, double vx = 0, double vz = 0) {
    if (!ctx || !out || ctx.state() != "running") return;
    const double ex = ear.position.x;
    const double ez = ear.position.z;
    std::vector<Near>& near = near_;
    near.clear();
    for (const auto& item : cars) {
      const auto& car = sirenDetail::carOf(item);
      const auto* em = sirenDetail::emOf(car.em);
      if (!em || !em->call) continue;
      const double d = std::hypot(car.x - ex, car.z - ez);
      if (d < 1200) near.push_back(Near{static_cast<int>(car.id), car.x, car.z, car.yaw, car.v, em->phase, em->pitch, d});
    }
    // the listener's right, along the ground (the camera's x axis)
    const auto& m = ear.matrixWorld.elements;
    sound(ctx, out, ex, ez, m[0], m[2], inside, vx, vz);
  }

  /** Silence every horn at once (the drive is over). */
  void stop();

 private:
  struct Voice {
    std::vector<OscillatorNode> osc;
    BiquadFilterNode body;
    BiquadFilterNode dull;
    GainNode level;
    /** (empty: the browser has no panner) */
    StereoPannerNode pan;
    /** its own place in the rhythm and its pitch (the vehicle's) */
    double phase = 0;
    double pitch = 1;
    /** seconds it has not been heard (it is taken down after a while) */
    double quiet = 0;
  };
  /** a car on a call within hearing: what is read of it, and how far it is */
  struct Near {
    int id;
    double x, z, yaw, v;
    double phase, pitch;
    double d;
  };

  std::map<int, Voice> voices_;
  std::vector<Near> near_;
  std::unordered_set<int> heard_;
  double last_ = 0;

  /** (the original's update() from where the cars within hearing are known) */
  void sound(const AudioContext& ctx, const AudioNode& out, double ex, double ez, double m0, double m2, bool inside, double vx, double vz);
  /** One horn: two oscillators tuned a little apart, a brassy peak, a low-pass for the distance, its loudness and its side. */
  Voice voice(const AudioContext& ctx, const AudioNode& out, double phase, double pitch);
};

}  // namespace fs
