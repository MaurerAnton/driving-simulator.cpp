// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/waves.ts @ v1.7.7
/**
 * The periodic part of a vehicle's sound, as one wave period of partials –
 * what the Web Audio API's PeriodicWave takes. It is worked out once per
 * vehicle, so that while driving the oscillators only have to play it at the
 * right speed:
 *
 *  - an engine: the pressure pulses of its cylinders in the exhaust, one per
 *    firing, at the crank angles its layout fires at (an inline four every
 *    180°, a parallel twin at 0° and 270°, a single every other turn). One
 *    period is a whole cycle (two turns of the crank for a four-stroke), so
 *    that the small differences between the cylinders – drawn once, the same
 *    in every cycle, as in a real engine – give the half orders that make an
 *    engine sound like one and not like a buzzer;
 *  - a propeller or an electric motor: chosen orders of its turning.
 *
 * Here too is what the loudness of the vehicles is balanced with: the
 * responses of the filters the sound goes through and the A-weighting of
 * the ear. Nothing here needs an AudioContext.
 */
#pragma once

// (JavaScript never multiplies and adds in one step; a C++ compiler does where the machine can – AArch64 –
// unless it is told not to.  For every file of the sound that includes this header: the same numbers as the original's.)
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace fs {

/** The partials of one wave period as createPeriodicWave takes them: cosine (real) and sine (imag) amplitudes; index 0, the mean, stays 0. */
struct Partials {
  std::vector<float> real;
  std::vector<float> imag;
};

enum class WaveKind { Pulses, Orders };

/**
 * A wave: `PulseWave | OrderWave` of the original in one struct – `kind` says which of the fields count.
 *
 * 'pulses' – an engine's exhaust: one pressure pulse per firing.
 * 'orders' – a turning machine heard at chosen orders of its turning: [order, amplitude].
 */
struct WaveSpec {
  WaveKind kind = WaveKind::Pulses;
  /** the crank angles the cylinders fire at within the period (degrees) */
  std::vector<double> firing = {};
  /** how strongly each fires (1 if not said) – a two-stroke at idle fires properly only every other turn */
  std::vector<double> amps = {};
  /** the pulse: how fast its pressure rises and how slowly it dies away (time constants, crank degrees) – the shorter, the brighter */
  double rise = 0;
  double decay = 0;
  /** no two cylinders are quite alike: random differences of strength (share) and of timing (crank degrees), drawn once */
  double spread = 0;
  double skew = 0;
  /** ('orders') [order, amplitude] */
  std::vector<std::array<double, 2>> orders = {};
  /** random phases (separate sources, as a motor's whines and its gears) – else all in phase, a pulse once a period (a propeller's blades) */
  bool scatter = false;
};
using PulseWave = WaveSpec;
using OrderWave = WaveSpec;

/** A small seeded random generator (mulberry32): the same numbers for the same seed, so that a vehicle always sounds the same. */
class Seeded {
 public:
  explicit Seeded(uint32_t seed) : a_(seed) {}
  double operator()() {
    a_ += 0x6d2b79f5u;
    uint32_t t = a_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }

 private:
  uint32_t a_;
};
inline Seeded seeded(uint32_t seed) { return Seeded(seed); }

/**
 * The partials of a wave, `count` of them, scaled so that the wave peaks at
 * 1 (then it makes no difference whether a browser normalises it again).
 * `periodDeg` is the crank angle of one period (720 for a four-stroke's cycle).
 */
Partials wavePartials(const WaveSpec& spec, double periodDeg, int count, uint32_t seed);

/** The wave at n evenly spaced points of its period (n a power of two, more than twice the partials). */
std::vector<double> samples(const Partials& p, int n);

/** The highest excursion of a wave (sampled four times finer than its highest partial: within a few per cent). */
double peakOf(const Partials& p);

/** The RMS of a wave: each partial a·cos + b·sin carries (a² + b²) / 2. */
double rmsOf(const Partials& p);

/** A-weighting – the ear at moderate levels – as a factor on the amplitude, 1 at 1 kHz. */
double aWeight(double f);

// The magnitudes of the BiquadFilterNode's responses (their analogue prototypes – close enough
// below a few kHz). As in the Web Audio API, the Q of the low-pass and the high-pass is in dB,
// that of the band-pass and the peaking filter a plain number.

double lowpassGain(double f, double fc, double qDb);
double highpassGain(double f, double fc, double qDb);
double bandpassGain(double f, double f0, double q);
double peakingGain(double f, double f0, double q, double gainDb);

/** the response of a filter: its gain at a frequency (Hz) */
using Response = std::function<double(double f)>;

/**
 * How loud (A-weighted RMS) two waves sound played together on one
 * fundamental f0 – they are in phase, so their partials add – after a
 * filter of the given response.
 */
double waveLoudness(const Partials& a, double ga, const Partials& b, double gb, double f0, const Response& response);

/** How loud (A-weighted RMS) white noise of ±1 sounds after a filter of the given response. */
double noiseLoudness(const Response& response, double sampleRate = 48000);

/** How loud (A-weighted RMS) an oscillator of amplitude 1 and one of the standard shapes ('sine' | 'square' | 'sawtooth' | 'triangle') sounds after a filter. */
double oscLoudness(std::string_view type, double f0, const Response& response = [](double) { return 1.0; });

}  // namespace fs
