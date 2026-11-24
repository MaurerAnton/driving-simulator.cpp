// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/compose.ts @ v1.7.7
/**
 * Music for the drive, made up anew for every piece – nothing recorded,
 * nothing borrowed: calm pieces of a minute or two in the manner of light
 * background music, each drawn from a seed –
 *
 *  - a key, a mode (major, Lydian, Mixolydian, Dorian or minor), a calm
 *    tempo, four or three beats to the bar, a set of instruments;
 *  - a form of eight-bar sections: a theme (A), a second theme (B), the
 *    theme again – with a short intro and an ending on the home chord;
 *  - chords from progressions known to sound pleasant (no diminished
 *    chords), every A closing with a cadence, B with a half close that
 *    leads back into A;
 *  - the theme: two phrases that begin alike and end apart – the first
 *    open, the second home –, built from a two-bar rhythmic motif whose
 *    first bar comes back on the next chord (a sequence), moving mostly by
 *    step and leaning on the notes of the chord; it returns later on
 *    another instrument;
 *  - an accompaniment of broken or held chords, and a bass.
 *
 * Pure numbers – what is played when; music.cpp plays it.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio/waves.h"

namespace fs {

enum class Instrument { Bell, Pluck, Marimba, Flute, Pad, Bass };
inline constexpr size_t INSTRUMENT_COUNT = 6;
/** the instrument as the original writes it: 'bell', 'pluck' … */
const char* instrumentName(Instrument i);

/** what a note belongs to: the tune, the chords (broken or held), the bass */
enum class NoteRole { Melody, Chords, Bass };

struct MusicNote {
  /** when it starts (s from the start of the piece) and how long it is held (s) */
  double t = 0;
  double d = 0;
  /** pitch (MIDI number: 60 = c′, 69 = a′ at 440 Hz) and strength (0 … 1) */
  int p = 0;
  double v = 0;
  Instrument i = Instrument::Bell;
  /** what it belongs to: the tune, the chords (broken or held), the bass */
  NoteRole role = NoteRole::Melody;

  bool operator==(const MusicNote& o) const = default;
};

enum class ModeName { Major, Lydian, Mixolydian, Dorian, Minor };
inline constexpr std::array<ModeName, 5> MODE_NAMES = {ModeName::Major, ModeName::Lydian, ModeName::Mixolydian, ModeName::Dorian, ModeName::Minor};
/** the mode as the original writes it: 'major', 'lydian' … */
const char* modeName(ModeName mode);

struct Piece {
  uint32_t seed = 0;
  double bpm = 0;
  int beatsPerBar = 4;
  /** the key's tonic (pitch class: 0 = C … 11 = B) and its mode */
  int key = 0;
  ModeName mode = ModeName::Major;
  /** the chord of every bar, as a degree of the mode (0: on the tonic) */
  std::vector<int> chords;
  /** seconds from the start until the last note has died away */
  double length = 0;
  /** in the order they start */
  std::vector<MusicNote> notes;

  bool operator==(const Piece& o) const = default;
};

/** `MODES[mode]`: the steps of each mode above its tonic (semitones) */
struct Modes {
  std::array<std::array<int, 7>, 5> steps;
  constexpr const std::array<int, 7>& operator[](ModeName mode) const { return steps[static_cast<size_t>(mode)]; }
};
inline constexpr Modes MODES = {{{
    {0, 2, 4, 5, 7, 9, 11},   // major
    {0, 2, 4, 6, 7, 9, 11},   // lydian
    {0, 2, 4, 5, 7, 9, 10},   // mixolydian
    {0, 2, 3, 5, 7, 9, 10},   // dorian
    {0, 2, 3, 5, 7, 8, 10},   // minor
}}};

/** Makes up a piece from a seed – the same piece for the same seed. */
Piece composePiece(uint32_t seed);

/** The scale steps of a pitch in a piece's key and mode (−1: not in the scale). */
int scaleDegree(const Piece& piece, int p);

}  // namespace fs
