// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/profiles.ts @ v1.7.7
/**
 * What each kind of vehicle sounds like, and how its sound follows what the
 * game tells it every frame – pure numbers, so that they can be tested
 * without a browser (audio.h plays them).
 *
 * Every vehicle is made of the same voices, each set up for it:
 *
 *  - the main voice: the engine – its firing pulses at the crank angles of
 *    its layout (waves.h), one wave for idle and overrun and a harder one
 *    for full load, blended with the load, through a low-pass that opens
 *    with revs and load and a resonance of its exhaust or body; for the
 *    aeroplane the propeller's blade passing as well, for the tram its
 *    traction motors and gears, whose whine rises with the speed;
 *  - combustion noise: a diesel's clatter, a petrol engine's intake roar,
 *    a two-stroke's rasp – noise that comes in pulses with the firings; the
 *    tram's fans, the propeller's swish;
 *  - the turbocharger's whistle, gear and chain whines, the tram's inverter;
 *  - the rolling of the tyres (the tram's wheels on the rails), the wind,
 *    a skidding tyre (the tram's flanges squealing in a tight curve);
 *  - the horn (the tram's bell), the starter, the indicator relay; air
 *    brakes and a reversing alarm on trucks and buses, the aeroplane's stall
 *    warning.
 *
 * All levels are given in dB(A) against one reference – the car's engine at
 * half load and middle revs – and turned into gains by measuring each
 * voice's loudness (A-weighted, after its filters) once per vehicle: so the
 * vehicles stay balanced against each other whatever their waves are like.
 */
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "audio/waves.h"

namespace fs {

/**
 * What the game tells the sound once a frame. The first fields are those of
 * every vehicle; the optional ones make the sound of particular vehicles –
 * left out, they change nothing.
 */
struct AudioState {
  /** engine speed (rpm) as the game's engine has it (the tachometer's); the aeroplane: its own rpm, engine and propeller; not used for the tram */
  double rpm = 0;
  /** 0 … 1: the throttle (engine load); the aeroplane: the throttle lever; the tram: how hard its motors pull (the controller forward) */
  double load = 0;
  /** the engine runs; the tram: its motors have power (pantograph up at the wire, or a regional tram's engine) */
  bool engineRunning = false;
  /** the starter turns the engine */
  bool starter = false;
  /** m/s; the aeroplane: its airspeed */
  double speed = 0;
  /** 0 … 1 tyre squeal */
  double skid = 0;
  bool indicatorOn = false;
  bool blinkOn = false;
  /** the horn; the tram: its bell, which rings for as long as this is held; nothing for the aeroplane or on foot */
  bool horn = false;
  /** surface roughness for road noise (1 asphalt … 3 gravel) – also what footsteps sound like */
  double rough = 1;
  /** 0 … 1 the service brake (the pedal); the tram: the controller backwards (its motors brake – and whine as they do), 1 for the emergency brake. Air brakes let out a puff when it is let go. */
  std::optional<double> braking = std::nullopt;
  /** the parking brake is on: a truck or a bus lets the air out of its spring brakes as it is set – "pssht" */
  std::optional<bool> parkingBrake = std::nullopt;
  /** in reverse gear: trucks and buses beep */
  std::optional<bool> reversing = std::nullopt;
  /** how sharply the way curves under the vehicle: 1 / radius (1/m, either side) – the tram's wheels squeal in tight curves */
  std::optional<double> curve = std::nullopt;
  /** the aeroplane is off the ground: its wheels no longer rumble */
  std::optional<bool> airborne = std::nullopt;
  /** the aeroplane's stall warning: its horn sounds */
  std::optional<bool> stallWarning = std::nullopt;
};

/** The kinds of vehicle sound – one per kind of vehicle of the game (soundKindOf). */
enum class SoundKind { Car, Moped, Motorcycle125, Motorcycle35, Motorcycle, Minibus, LightTruck, Truck, Semi, Bus, TractorSlow, Tractor, Tram, Plane, OnFoot };

inline constexpr std::array<SoundKind, 15> SOUND_KINDS = {
    SoundKind::Car,     SoundKind::Moped, SoundKind::Motorcycle125, SoundKind::Motorcycle35, SoundKind::Motorcycle, SoundKind::Minibus, SoundKind::LightTruck, SoundKind::Truck,
    SoundKind::Semi,    SoundKind::Bus,   SoundKind::TractorSlow,   SoundKind::Tractor,      SoundKind::Tram,       SoundKind::Plane,   SoundKind::OnFoot};

/** the kind as the original writes it: 'car', 'motorcycle125', 'onFoot' … */
const char* soundKindName(SoundKind kind);
/** … and back (nullopt: no such kind) */
std::optional<SoundKind> soundKindOfName(std::string_view name);

/**
 * The sound of a vehicle of the game.  `kind`: its VehicleKind (game/vehicles.h – a text in the port, as in
 * the original: 'car', 'busTrailer', 'pedestrian' …); a kind nothing is known of sounds like the car.
 */
SoundKind soundKindOf(std::string_view kind);

enum class VoiceKind { Engine, Prop, Motor };

/**
 * The main voice: `EngineVoice | MotorVoice` of the original in one struct – `kind` says which of the fields count.
 *
 * 'engine' | 'prop' – a piston engine (and the aeroplane's, with its propeller): turns … harden, prop.
 * 'motor' – the tram's traction: motors and gears whose orders rise with the speed: perMps, coast, effort.
 */
struct MainVoice {
  VoiceKind kind = VoiceKind::Engine;
  /** crank turns per wave period: 2 for a four-stroke's cycle – and for the two-stroke moped, whose firings differ at idle */
  double turns = 0;
  /** its own idle and highest engine speeds (rpm) – the game's engine speeds are mapped onto them (setVehicle) */
  double idleRpm = 0;
  double maxRpm = 0;
  /** (motor) the motors' turning (Hz) per m/s: gear ratio over the wheels' circumference */
  double perMps = 0;
  /** the two waves the voice blends: soft (idle, overrun – coasting for the motor, the propeller for the aeroplane) and hard (full load, pulling – the aeroplane's exhaust) */
  WaveSpec soft = {};
  WaveSpec hard = {};
  /** how many partials of a pulse wave are worked out (enough to reach a few kHz at idle) */
  int partials = 0;
  /** loudness at the reference – half load at 45 % of the revs; the motor: 10 m/s at half effort – in dB(A) against the car's */
  double db = 0;
  /** its level made up of a part at idle, one with the load and one with the revs */
  double idle = 0;
  double load = 0;
  double revs = 0;
  /** how quickly the hard wave takes over with the load (exponent: lower is sooner) */
  double harden = 0;
  /** (motor) level of the gears while coasting, and of the motors with full effort (pulling or braking) */
  double coast = 0;
  double effort = 0;
  /** low-pass (Hz): at idle, at the highest revs (the motor: per m/s), and what full load adds; and its resonance (dB) */
  std::array<double, 3> lp = {0, 0, 0};
  double lpQ = 0;
  /** a resonance of the exhaust, the intake or the body: centre (Hz), Q, gain (dB) */
  std::array<double, 3> body = {0, 0, 0};
  /** unsteadiness at idle (less under load): of the speed (cents) and of the strength (share) */
  std::array<double, 2> jitter = {0, 0};
  /** the aeroplane: the propeller's loudness (the soft wave) against the exhaust's at the highest rpm */
  double prop = 0;
};
using EngineVoice = MainVoice;
using MotorVoice = MainVoice;

/** Noise of the engine: clatter, intake roar, rasp – or of fans and a propeller. */
struct NoiseVoice {
  /** band-pass centre at idle and at the highest revs (Hz), and its Q */
  std::array<double, 2> f = {0, 0};
  double q = 0;
  /** loudness at the reference against the main voice (dB) */
  double db = 0;
  /** the share of it that comes in pulses with the firings (0: steady, as fans) */
  double am = 0;
  /** what it is made up of: a part always there, one with the load, one with the revs (negative: less at high revs, as a diesel's clatter) */
  double base = 0;
  double load = 0;
  double revs = 0;
  /** only while the engine fires – else while it turns (a windmilling propeller), or for the tram while it has power */
  bool firing = false;
};

struct TurboVoice {
  /** its whistle (Hz) from spooling up to full boost, its loudness at full boost (dB), and how long it takes to spool up (s) */
  std::array<double, 2> f = {0, 0};
  double db = 0;
  double spool = 0;
};

enum class ToneBy { Speed, Rpm, Fixed };

/** A whine: of gears, a chain, a pump, the tram's inverter. */
struct ToneVoice {
  /** 'sine' | 'triangle' | 'square' | 'sawtooth' */
  std::string wave = "sine";
  /** what its pitch follows: the speed (hz per m/s), the revs (hz per rpm) or nothing (hz) */
  ToneBy by = ToneBy::Speed;
  double hz = 0;
  /** its loudness at full effort, once faded in, at 15 m/s or middle revs (dB) */
  double db = 0;
  /** the share of it that comes with effort – pulling or braking (0: steady) */
  double effort = 0;
  /** speeds (m/s) over which it comes in, and – if given – over which it goes again */
  std::array<double, 2> fade = {0, 0};
  std::optional<std::array<double, 2>> out = std::nullopt;
};

struct SoundProfile {
  /** rolling: loudness at 30 m/s on asphalt (dB), low-pass (Hz: at a standstill, per m/s) and its resonance (dB) */
  struct Road {
    double db = 0;
    std::array<double, 2> lp = {0, 0};
    double q = 0;
  };
  /** wind: loudness at 30 m/s (dB), band-pass centre (Hz: at a standstill, per m/s) and its Q */
  struct Wind {
    double db = 0;
    std::array<double, 2> bp = {0, 0};
    double q = 0;
  };
  /** a skidding tyre: band-pass centre (Hz), Q, loudness at a full skid (dB) */
  struct Skid {
    double f = 0, q = 0, db = 0;
  };
  /** the tram's flanges in a tight curve: a narrow squeal (Hz, Q), its loudness (dB) and how far it wanders (cents) */
  struct Squeal {
    double f = 0, q = 0, db = 0, wobble = 0;
  };
  struct Horn {
    std::array<double, 2> f = {0, 0};
    /** 'sawtooth' | 'square' */
    std::string wave = "sawtooth";
    double lp = 0;
    double db = 0;
  };
  /** the starter: its whirr (Hz), the compression pulses it labours through (Hz) and how far they pull its pitch (Hz), its loudness (dB) */
  struct Starter {
    double f = 0, lfo = 0, depth = 0, db = 0;
  };
  /** the indicator relay: tick and tock (Hz) and how loud (as a gain) */
  struct Relay {
    double tick = 0, tock = 0, level = 0;
  };
  /** the aeroplane's stall warning horn (a reed): pitch (Hz) and loudness (dB) */
  struct StallHorn {
    double f = 0, db = 0;
  };

  std::optional<MainVoice> main = std::nullopt;
  /** a road vehicle: the engine speeds of the game's engine are mapped onto the profile's (not the aeroplane, whose rpm are its own) */
  bool mapRpm = false;
  std::optional<NoiseVoice> noise = std::nullopt;
  std::optional<TurboVoice> turbo = std::nullopt;
  std::vector<ToneVoice> tones = {};
  std::optional<Road> road = std::nullopt;
  std::optional<Wind> wind = std::nullopt;
  std::optional<Skid> skid = std::nullopt;
  std::optional<Squeal> squeal = std::nullopt;
  std::optional<Horn> horn = std::nullopt;
  /** a tram's bell instead of a horn */
  bool bell = false;
  std::optional<Starter> starter = std::nullopt;
  std::optional<Relay> relay = std::nullopt;
  /** air brakes: a puff when the brake is let go, "pssht" as the parking brake is set, the air dryer now and then */
  bool air = false;
  /** a reversing alarm */
  bool beeper = false;
  std::optional<StallHorn> stallHorn = std::nullopt;
};

/**
 * The scale of everything: the A-weighted RMS of the car's engine at its reference (half load, 45 % of its revs) –
 * a little above what the single engine sound before was, so that a volume set then still suits.
 */
inline constexpr double REF_LOUDNESS = 0.03;

/**
 * `PROFILES[kind]`: the profile of every kind of sound.  (The table itself is made when it is
 * first asked for, so it may be asked for at any time – also while the program starts.)
 */
struct Profiles {
  const SoundProfile& operator[](SoundKind kind) const;
};
inline constexpr Profiles PROFILES = {};

/** The game's engine speeds that are mapped onto a profile's: its engine's idle and its limiter. */
struct RpmScale {
  double idleRpm = 0;
  double limiterRpm = 0;
};

/** The engine speed the profile plays at for the game's `rpm`: the game's idle onto its idle, the limiter onto its highest. */
double soundRpm(const SoundProfile& p, double rpm, const RpmScale* scale);

/** How often the engine fires (Hz) at an engine speed: a four-stroke's cylinders once every two turns, the two-stroke's every turn. */
double firingHz(const SoundProfile& p, double rpm);

/** A profile ready to be played: its waves and the gains that make each voice as loud as it says. */
struct Voicing {
  struct Gain {
    double main = 1;
    double noise = 1;
    double turbo = 0;
    std::array<double, 2> tones = {0, 0};
    double road = 0;
    double wind = 0;
    double skid = 0;
    double squeal = 0;
    double horn = 0;
    double starter = 0;
    double stall = 0;
  };
  SoundKind kind;
  const SoundProfile& profile;
  std::optional<Partials> soft;
  std::optional<Partials> hard;
  Gain gain;
};

/** Where every voice is to go this frame (audio.cpp glides there). */
struct SoundTargets {
  /** the main voice: fundamental (Hz – a whole wave period, the engine's cycle), the shares of its soft and hard wave, its level, its low-pass (Hz), unsteadiness (cents, share) */
  double freq = 10;
  double soft = 0;
  double hard = 0;
  double level = 0;
  double cutoff = 1000;
  double jitCents = 0;
  double jitAmp = 0;
  double noiseFreq = 1000;
  double noiseLevel = 0;
  double turboFreq = 3000;
  double turboLevel = 0;
  double toneAFreq = 440;
  double toneALevel = 0;
  double toneBFreq = 440;
  double toneBLevel = 0;
  double roadCutoff = 500;
  double roadLevel = 0;
  double windFreq = 500;
  double windLevel = 0;
  /** the skidding tyre – the tram's squeal: band-pass centre (Hz) and level */
  double skidFreq = 1000;
  double skidLevel = 0;
  double hornLevel = 0;
  double starterLevel = 0;
};

SoundTargets newTargets();

/**
 * Where every voice of a vehicle is to go for what the game says (its
 * engine speeds mapped by `scale`). Writes into `out` – nothing is made anew
 * per frame.
 */
SoundTargets& soundTargets(const Voicing& v, const AudioState& s, const RpmScale* scale, SoundTargets& out);
/** (for `soundTargets(v, s, null, newTargets())`: into targets just made, which are handed back) */
inline SoundTargets soundTargets(const Voicing& v, const AudioState& s, const RpmScale* scale, SoundTargets&& out) { return soundTargets(v, s, scale, out); }

/** How loud (A-weighted RMS) the main voice is at some targets – with the waves, the filters it goes through. */
double mainLoudness(const Voicing& v, const SoundTargets& t);

/** The state the main voice's level is given at: half load at 45 % of the revs – for the tram 10 m/s at half effort. */
AudioState referenceState(const SoundProfile& p);

/** A profile ready to be played (worked out on first use, then kept). */
const Voicing& voicing(SoundKind kind);

}  // namespace fs
