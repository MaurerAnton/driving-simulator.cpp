// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/crowd.ts @ v1.7.7
/**
 * The sound of a march (sim/roadevents): the murmur of the crowd and,
 * now and then, a whistle – a trilling pea whistle, as at any
 * demonstration – louder the nearer the listener is.  Made from noise and
 * an oscillator in the game's audio graph (audio/audio.h); nothing is
 * downloaded.
 */
#pragma once

#include <optional>

#include "platform/audio.h"

namespace fs {

/** What of the game's audio is used: its context and the gain everything goes through (GameAudio is one). */
class AudioOut {
 public:
  virtual ~AudioOut() = default;
  /** (the empty one: there is none yet – `null`) */
  virtual AudioContext context() const = 0;
  virtual GainNode masterGain() const = 0;
};

class CrowdNoise {
 public:
  /** (`out` must outlive this) */
  explicit CrowdNoise(AudioOut& out) : out_(out) {}

  /**
   * Each frame: `dist` from the listener to the nearest crowd (m; Infinity:
   * none), how many walk in it, whether it is marching.
   */
  void update(double dt, double dist, double size, bool walking);

 private:
  struct Murmur {
    AudioBufferSourceNode src;
    GainNode gain;
  };

  AudioOut& out_;
  std::optional<Murmur> murmur_;
  double quiet_ = 0;

  Murmur startMurmur(const AudioContext& ctx, const GainNode& master);
  /** One blast of a pea whistle: a high tone trilling fast, short – sometimes two. */
  void whistle(const AudioContext& ctx, const GainNode& master, double level);
};

}  // namespace fs
