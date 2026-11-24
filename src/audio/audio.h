// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/audio.ts @ v1.7.7
/**
 * All sound is synthesised with the Web Audio API – nothing is recorded,
 * nothing downloaded. Every kind of vehicle has a sound of its own
 * (profiles.h: a petrol four, diesels from the van to the truck, the
 * tractors, the motorcycles from the moped's two-stroke to the big four,
 * the tram's motors, the aeroplane's engine and propeller); here the voices
 * are built once, when the sound starts, and set up anew when the vehicle
 * changes – while driving, update() only moves their settings, so that it
 * stays cheap on a weak tablet or a phone:
 *
 *   main voice   two oscillators playing the vehicle's waves (soft, hard)
 *                → low-pass → resonance → unsteadiness → level
 *   engine noise noise → band-pass → in pulses with the hard wave → level
 *   whines       turbo, two tones (gears, chain, the tram's inverter, the stall horn)
 *   rolling, wind, skid (the tram's squeal), air (hisses), horn, starter
 *
 * All of it goes through one gain that silences it while the vehicle
 * changes and a high-pass that keeps the inaudible bottom of the engines'
 * waves off small speakers; then the master gain (the game's volume) and a
 * limiter. Short sounds (click) and the music (music.h) join at the master.
 */
#pragma once

#include <array>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "audio/crowd.h"
#include "audio/music.h"
#include "audio/profiles.h"
#include "platform/audio.h"

namespace fs {

/** The short sounds click() makes. */
enum class ClickKind {
  /** the indicator relay (also the game's small clicks) */
  Tick,
  Tock,
  Crash,
  Grind,
  Chime,
  Bump,
  /** a door's latch as it opens, a door shut */
  Door,
  DoorShut,
  /** a footstep – a crunch on rough ground */
  Step,
  /** air brakes: the parking brake set ("pssht"), a short puff, a passenger door working on air (hiss and thud) */
  Air,
  AirShort,
  AirDoor,
  /** the tram's bell, struck once */
  Bell,
  /** wheels over a rail joint or a switch: two knocks, an axle's distance apart at the speed going */
  Clack,
  /** one beep of a reversing alarm */
  Beep,
};
inline constexpr std::array<ClickKind, 15> CLICK_KINDS = {ClickKind::Tick,     ClickKind::Tock, ClickKind::Crash, ClickKind::Grind,    ClickKind::Chime,
                                                          ClickKind::Bump,     ClickKind::Door, ClickKind::DoorShut, ClickKind::Step,  ClickKind::Air,
                                                          ClickKind::AirShort, ClickKind::AirDoor, ClickKind::Bell, ClickKind::Clack, ClickKind::Beep};
/** the kind as the original writes it: 'tick', 'doorShut', 'airDoor' … – and back (nullopt: no such sound) */
const char* clickKindName(ClickKind kind);
std::optional<ClickKind> clickKindOf(std::string_view name);

class GameAudio : public AudioOut {
 public:
  double volume = 0.7;

  GameAudio();
  ~GameAudio() override;
  GameAudio(const GameAudio&) = delete;
  GameAudio& operator=(const GameAudio&) = delete;

  /** Must be called from a user gesture (click / key) – browsers require it. */
  void start();

  void suspend();

  void setVolume(double v);

  /** The game's AudioContext once start() has made it (the empty one before) – other sounds of the game (a siren) play in it. */
  AudioContext context() const override;

  /** The master gain – the game's volume, followed only by a limiter: where other sounds of the game connect (the empty one before start()). */
  GainNode masterGain() const override;

  /** Calls `f` with the context and the master gain once they are there – at once if they already are. */
  void onReady(std::function<void(const AudioContext& ctx, const GainNode& master)> f);

  /** the vehicle sound now set */
  SoundKind vehicleSound() const { return kind_; }

  /**
   * The vehicle whose sound is to be heard (soundKindOf gives it for a vehicle of the game). `engine`: the idle
   * and limiter speeds of the game's engine for it (a VehicleSpec will do) – its rpm are then mapped onto the
   * sound's own engine (a minibus's diesel revs higher than the truck engine the game drives it with); left out,
   * rpm are taken as they come. The tram and the aeroplane ignore it.
   */
  void setVehicle(SoundKind kind, const RpmScale* engine = nullptr);
  /** (… anything that has an `idleRpm` and a `limiterRpm`) */
  template <class Engine>
    requires requires(const Engine& e) {
      { e.idleRpm } -> std::convertible_to<double>;
      { e.limiterRpm } -> std::convertible_to<double>;
    }
  void setVehicle(SoundKind kind, const Engine& engine) {
    const RpmScale scale{static_cast<double>(engine.idleRpm), static_cast<double>(engine.limiterRpm)};
    setVehicle(kind, &scale);
  }

  /**
   * The music: 'off' (the default – nothing of it runs), 'quiet' (now and then a piece, softly, with pauses of a
   * few minutes) or 'on' (pieces with short pauses, a little louder); `volume` 0 … 1 on top of the game's volume
   * (left out: as it was).
   */
  void setMusic(MusicMode mode, double volume);
  void setMusic(MusicMode mode) { setMusic(mode, musicVolume_); }
  /** (… the mode as the settings keep it: 'off' | 'quiet' | 'on'; anything else is 'off') */
  void setMusic(std::string_view mode, double volume) { setMusic(musicModeOf(mode).value_or(MusicMode::Off), volume); }
  void setMusic(std::string_view mode) { setMusic(mode, musicVolume_); }

  /** Short sounds. */
  void click(ClickKind kind);
  /** (… by its name, as the original gives it: click('tick'); a name that is none makes no sound) */
  void click(std::string_view kind) {
    if (const std::optional<ClickKind> k = clickKindOf(kind)) click(*k);
  }

  void update(const AudioState& s);

 private:
  struct Nodes;

  /** the settings the main loop moves: how many there are */
  static constexpr int SLOTS = 23;

  std::unique_ptr<Nodes> n_;
  SoundKind kind_ = SoundKind::Car;
  const Voicing* voice_;
  std::optional<RpmScale> scale_;
  SoundTargets tg_ = newTargets();
  /** what was last sent to each setting (NaN: whatever comes next is sent) – unchanged settings are not sent again */
  std::array<double, SLOTS> sent_;
  double now_ = 0;
  /** RMS of the low-passed noise that makes the unsteadiness, and the gain that makes a hiss as loud as the reference */
  double jitRms_ = 0.0176;
  double hissUnit_ = 1;
  bool lastBlink_ = false;
  bool lastHorn_ = false;
  double nextBell_ = 0;
  std::optional<bool> lastPark_;
  double brakeHeld_ = 0;
  double nextPuff_ = 0;
  double purgeAt_ = 0;
  double nextBeep_ = 0;
  double lastRough_ = 1;
  double lastSpeed_ = 0;
  std::unique_ptr<MusicPlayer> music_;
  MusicMode musicMode_ = MusicMode::Off;
  double musicVolume_ = 1;
  std::vector<std::function<void(const AudioContext& ctx, const GainNode& master)>> ready_;

  /** Sets up the voices for the vehicle now set (the waves, the filters, the horn …) – while they are silenced. */
  void applyVehicle();
  /** Whether a setting has moved enough to be sent again (and remembers it if so). */
  bool changed(int i, double v);
  void glide(int i, const AudioParam& param, double v, double tc);
  /** Air brakes: "pssht" as the parking brake is set, a puff as the brake is let go, the air dryer now and then. */
  void airBrakes(const AudioState& s, double t);
  /** A hiss of air on the vehicle's own hiss voice: loudness (dB against the reference), held (s), dying away (s), its pitch sinking from → to (Hz). */
  void hiss(double db, double hold, double decay, double from, double to);
  /** A burst of filtered noise at time t. */
  void noiseHit(double t, const char* type, double freq, double q, double level, double dur);
  /** The tram's bell struck once: a dome bell's partials, the higher dying sooner, and the knock of the hammer. */
  void strike(double t);
  /** One beep of a reversing alarm, as heard from the cab. */
  void beep(double t);
};

}  // namespace fs
