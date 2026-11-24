// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/music.ts @ v1.7.7
/**
 * Plays the music that compose.cpp makes up – off unless the player asks
 * for it; then now and then a piece, quietly, with pauses of a few minutes
 * between ('quiet'), or more often and a little louder ('on').
 *
 * Kept cheap for a phone: a handful of shared nodes (a bus per instrument,
 * a soft low-pass, an echo of two delays that bounce between left and right
 * and grow darker – far cheaper than a reverb), and per note one or two
 * oscillators with an envelope, made just before the note is due and
 * stopped as soon as it has died away. A timer looks a second or so ahead
 * four times a second and schedules what is due on the audio clock, so a
 * busy frame does not shift the beat. With the music off nothing of it
 * exists and nothing runs.
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audio/compose.h"
#include "platform/audio.h"

namespace fs {

enum class MusicMode { Off, Quiet, On };
/** the mode as the original (and the settings) write it: 'off' | 'quiet' | 'on' – and back (nullopt: no such mode) */
const char* musicModeName(MusicMode mode);
std::optional<MusicMode> musicModeOf(std::string_view name);

/** The pause before the next piece (s): a few minutes now and then ('quiet'), under a minute 'on'. `r` is a random number 0 … 1. */
double pauseAfter(MusicMode mode, double r);

/** `MUSIC_DB[mode]`: how loud the music is, against the vehicles' reference (the car's engine at half load, dB(A)): well below it – 'quiet' still more. */
struct MusicDb {
  constexpr double operator[](MusicMode mode) const { return mode == MusicMode::Quiet ? -10 : -5; }
};
inline constexpr MusicDb MUSIC_DB = {};

/**
 * The gain that plays a piece as loud as the mode asks, at the player's music volume (0 … 1): every piece
 * equally loud, whether it has a busy accompaniment or only held chords.
 */
double pieceGain(const Piece& piece, MusicMode mode, double volume);

/**
 * How loud a piece is on average (A-weighted RMS) at a level – roughly, from
 * each note's energy (its envelope, its wave, its pitch) – for balancing the
 * music against the vehicles' sounds.
 */
double pieceLoudness(const Piece& piece, double level);

/** the player's options: `timer: false` leaves calling tick() to the caller (the tests); `seed`: of its first piece */
struct MusicOptions {
  std::optional<bool> timer = std::nullopt;
  std::optional<uint32_t> seed = std::nullopt;
};

class MusicPlayer {
 public:
  /** pieces begun (for the tests) */
  int played = 0;

  /** `timer: false` leaves calling tick() to the caller (the tests). */
  MusicPlayer(const BaseAudioContext& ctx, const AudioNode& dest, MusicOptions opts = MusicOptions());
  ~MusicPlayer();
  MusicPlayer(const MusicPlayer&) = delete;
  MusicPlayer& operator=(const MusicPlayer&) = delete;

  bool playing() const { return piece_.has_value(); }

  /** Off, now and then, or more often – and how loud (0 … 1). */
  void set(MusicMode mode, double volume);

  /** Schedules the notes due within the next moment (the timer calls it). */
  void tick();

  /** Stops for good (the game is closed). */
  void dispose();

 private:
  struct MusicGraph {
    GainNode level;
    std::array<GainNode, INSTRUMENT_COUNT> inst;
    DelayNode echoA;
    DelayNode echoB;
    OscillatorNode vibrato;
    GainNode vibratoDepth;
    std::vector<AudioNode> nodes;
  };

  const BaseAudioContext ctx_;
  const AudioNode dest_;
  const bool useTimer_;
  MusicMode mode_ = MusicMode::Off;
  double volume_ = 1;
  std::shared_ptr<MusicGraph> g_;
  /** the timer that calls tick() (0: none) */
  int timer_ = 0;
  std::optional<Piece> piece_;
  /** when the piece began (audio clock), the next of its notes to schedule, when the next piece is due */
  double begun_ = 0;
  size_t next_ = 0;
  double nextPieceAt_ = 0;
  uint32_t seed_;
  std::string lastKey_;

  void begin(double at);
  std::shared_ptr<MusicGraph> build();
  void stop();
  /** One note: one or two oscillators and an envelope, stopped once it has died away. */
  void play(const MusicGraph& g, const MusicNote& n, double t);
};

}  // namespace fs
