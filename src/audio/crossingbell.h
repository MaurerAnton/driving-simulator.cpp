// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/crossingbell.ts @ v1.7.7
/**
 * The bell of a level crossing (src/sim/levelcrossing): the bright,
 * quickly repeated strokes of the electric bell that rings while the
 * lights come on and the barriers go down – louder the nearer the listener
 * is.  Each stroke is two partials of a struck bell, dying away; made in
 * the game's audio graph (audio/audio.h), nothing is downloaded.
 */
#pragma once

#include "audio/crowd.h"
#include "platform/audio.h"

namespace fs {

class CrossingBell {
 public:
  /** (`out` must outlive this) */
  explicit CrossingBell(AudioOut& out) : out_(out) {}

  /** Each frame: `dist` from the listener to the nearest bell that rings (m; Infinity: none). */
  void update(double dist);

 private:
  AudioOut& out_;
  double next_ = 0;

  void stroke(const AudioContext& ctx, const GainNode& master, double t0, double level);
};

}  // namespace fs
