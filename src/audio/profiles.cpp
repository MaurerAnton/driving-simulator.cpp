// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/profiles.ts @ v1.7.7
#include "audio/profiles.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace fs {

namespace {

const char* const KIND_NAMES[] = {"car", "moped", "motorcycle125", "motorcycle35", "motorcycle", "minibus", "lightTruck", "truck", "semi", "bus", "tractorSlow", "tractor", "tram", "plane", "onFoot"};

/** the speed road and wind levels are given at (m/s) */
const double AT_SPEED = 30;
/** a cranked engine (turning, not firing) against its idle */
const double CRANK = 0.35;

std::array<SoundProfile, SOUND_KINDS.size()> makeProfiles() {
  using P = SoundProfile;
  const std::vector<double> I4 = {0, 180, 360, 540};
  const std::vector<double> I6 = {0, 120, 240, 360, 480, 600};

  /** what the motorcycles have in common but their engines */
  const SoundProfile BIKE = {
      .mapRpm = true,
      .turbo = std::nullopt,
      .road = P::Road{.db = -10, .lp = {380, 30}, .q = 0.5},
      .wind = P::Wind{.db = 0, .bp = {480, 18}, .q = 0.5},
      .skid = P::Skid{.f = 1350, .q = 6, .db = -5},
      .squeal = std::nullopt,
      .bell = false,
      .relay = P::Relay{.tick = 3300, .tock = 2900, .level = 0.16},
      .air = false,
      .beeper = false,
      .stallHorn = std::nullopt,
  };

  /** what the trucks and buses have in common but their engines */
  const SoundProfile HEAVY = {
      .mapRpm = true,
      .squeal = std::nullopt,
      .bell = false,
      .relay = P::Relay{.tick = 1700, .tock = 1300, .level = 0.5},
      .air = true,
      .beeper = true,
      .stallHorn = std::nullopt,
  };

  std::array<SoundProfile, SOUND_KINDS.size()> all;
  auto at = [&all](SoundKind kind) -> SoundProfile& { return all[static_cast<size_t>(kind)]; };

  // A 1.5 l turbo petrol four: smooth, even firing (second order), intake roar under load, a faint turbo.
  at(SoundKind::Car) = SoundProfile{
      .main =
          MainVoice{
              .kind = VoiceKind::Engine,
              .turns = 2,
              .idleRpm = 800,
              .maxRpm = 6500,
              .soft = {.kind = WaveKind::Pulses, .firing = I4, .rise = 14, .decay = 50, .spread = 0.1, .skew = 2},
              .hard = {.kind = WaveKind::Pulses, .firing = I4, .rise = 6, .decay = 34, .spread = 0.05, .skew = 1},
              .partials = 720,
              .db = 0,
              .idle = 0.6,
              .load = 0.45,
              .revs = 0.4,
              .harden = 0.8,
              .lp = {600, 2200, 1100},
              .lpQ = 1,
              .body = {160, 1.2, 4},
              .jitter = {5, 0.05},
              .prop = 0,
          },
      .mapRpm = true,
      .noise = NoiseVoice{.f = {500, 1800}, .q = 0.9, .db = -9, .am = 0.6, .base = 0.3, .load = 0.85, .revs = 0.4, .firing = true},
      .turbo = TurboVoice{.f = {2800, 6500}, .db = -22, .spool = 0.5},
      .tones = {ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 38, .db = -30, .effort = 0.6, .fade = {3, 12}}},
      .road = P::Road{.db = -6, .lp = {300, 28}, .q = 0.7},
      .wind = P::Wind{.db = -9, .bp = {380, 14}, .q = 0.6},
      .skid = P::Skid{.f = 1150, .q = 6, .db = -4},
      .squeal = std::nullopt,
      .horn = P::Horn{.f = {415, 520}, .wave = "sawtooth", .lp = 2400, .db = 6},
      .bell = false,
      .starter = P::Starter{.f = 88, .lfo = 9, .depth = 22, .db = -5},
      .relay = P::Relay{.tick = 2600, .tock = 1900, .level = 0.35},
      .air = false,
      .beeper = false,
      .stallHorn = std::nullopt,
  };
  // A moped's 50 cm³ two-stroke single: a buzz through its expansion chamber – at idle it fires
  // properly only every other turn ("ring-ding-ding"), which the soft wave's weak second firing is.
  {
    SoundProfile p = BIKE;
    p.main = MainVoice{
        .kind = VoiceKind::Engine,
        .turns = 2,
        .idleRpm = 1800,
        .maxRpm = 9500,
        .soft = {.kind = WaveKind::Pulses, .firing = {0, 360}, .amps = {1, 0.4}, .rise = 4, .decay = 30, .spread = 0.25, .skew = 8},
        .hard = {.kind = WaveKind::Pulses, .firing = {0, 360}, .amps = {1, 0.95}, .rise = 2, .decay = 18, .spread = 0.05, .skew = 2},
        .partials = 360,
        .db = 0,
        .idle = 0.45,
        .load = 0.4,
        .revs = 0.45,
        .harden = 0.6,
        .lp = {900, 4500, 2000},
        .lpQ = 3,
        .body = {1300, 2.5, 8},
        .jitter = {15, 0.18},
        .prop = 0,
    };
    p.noise = NoiseVoice{.f = {2500, 4000}, .q = 1.2, .db = -6, .am = 0.9, .base = 0.2, .load = 0.6, .revs = 0.4, .firing = true};
    p.tones = {};
    p.road = P::Road{.db = -12, .lp = {400, 30}, .q = 0.5};
    p.wind = P::Wind{.db = -3, .bp = {480, 18}, .q = 0.5};
    p.horn = P::Horn{.f = {480, 482}, .wave = "square", .lp = 3500, .db = 0};
    p.starter = P::Starter{.f = 190, .lfo = 15, .depth = 40, .db = -10};
    at(SoundKind::Moped) = p;
  }
  // A 125 cm³ four-stroke single: one bang every other turn – a thump at idle, a buzz when revved.
  {
    SoundProfile p = BIKE;
    p.main = MainVoice{
        .kind = VoiceKind::Engine,
        .turns = 2,
        .idleRpm = 1500,
        .maxRpm = 10000,
        .soft = {.kind = WaveKind::Pulses, .firing = {0}, .rise = 8, .decay = 40, .spread = 0, .skew = 0},
        .hard = {.kind = WaveKind::Pulses, .firing = {0}, .rise = 3, .decay = 26, .spread = 0, .skew = 0},
        .partials = 400,
        .db = 1.5,
        .idle = 0.45,
        .load = 0.4,
        .revs = 0.4,
        .harden = 0.7,
        .lp = {700, 3800, 1500},
        .lpQ = 2,
        .body = {260, 1.5, 6},
        .jitter = {10, 0.14},
        .prop = 0,
    };
    p.noise = NoiseVoice{.f = {1200, 3000}, .q = 1, .db = -6, .am = 0.85, .base = 0.2, .load = 0.6, .revs = 0.4, .firing = true};
    p.tones = {ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 70, .db = -32, .effort = 0.3, .fade = {3, 15}}};
    p.horn = P::Horn{.f = {440, 442}, .wave = "square", .lp = 3000, .db = 2};
    p.starter = P::Starter{.f = 150, .lfo = 12, .depth = 30, .db = -8};
    at(SoundKind::Motorcycle125) = p;
  }
  // A parallel twin with a 270° crank (class A2): it fires after 270° and again after 450° – the lumpy beat of a V-twin.
  {
    SoundProfile p = BIKE;
    p.main = MainVoice{
        .kind = VoiceKind::Engine,
        .turns = 2,
        .idleRpm = 1250,
        .maxRpm = 9000,
        .soft = {.kind = WaveKind::Pulses, .firing = {0, 270}, .rise = 7, .decay = 38, .spread = 0.08, .skew = 2},
        .hard = {.kind = WaveKind::Pulses, .firing = {0, 270}, .rise = 3, .decay = 24, .spread = 0.04, .skew = 1},
        .partials = 480,
        .db = 2.5,
        .idle = 0.45,
        .load = 0.42,
        .revs = 0.4,
        .harden = 0.7,
        .lp = {600, 3500, 1500},
        .lpQ = 2,
        .body = {200, 1.3, 6},
        .jitter = {7, 0.1},
        .prop = 0,
    };
    p.noise = NoiseVoice{.f = {900, 2500}, .q = 1, .db = -7, .am = 0.8, .base = 0.2, .load = 0.6, .revs = 0.4, .firing = true};
    p.tones = {ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 60, .db = -32, .effort = 0.3, .fade = {3, 15}}};
    p.horn = P::Horn{.f = {430, 432}, .wave = "sawtooth", .lp = 2800, .db = 2};
    p.starter = P::Starter{.f = 130, .lfo = 10, .depth = 26, .db = -7};
    at(SoundKind::Motorcycle35) = p;
  }
  // A big inline four: even firing every 180°, sharp pulses, high revs – a howl, and a deep intake roar.
  {
    SoundProfile p = BIKE;
    p.main = MainVoice{
        .kind = VoiceKind::Engine,
        .turns = 2,
        .idleRpm = 1200,
        .maxRpm = 11000,
        .soft = {.kind = WaveKind::Pulses, .firing = I4, .rise = 6, .decay = 32, .spread = 0.06, .skew = 1.5},
        .hard = {.kind = WaveKind::Pulses, .firing = I4, .rise = 2.5, .decay = 20, .spread = 0.03, .skew = 1},
        .partials = 500,
        .db = 3,
        .idle = 0.4,
        .load = 0.42,
        .revs = 0.5,
        .harden = 0.7,
        .lp = {700, 5000, 2000},
        .lpQ = 2,
        .body = {420, 1.2, 5},
        .jitter = {5, 0.06},
        .prop = 0,
    };
    p.noise = NoiseVoice{.f = {700, 2400}, .q = 0.9, .db = -6, .am = 0.6, .base = 0.15, .load = 0.75, .revs = 0.5, .firing = true};
    p.tones = {ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 55, .db = -32, .effort = 0.3, .fade = {3, 15}}};
    p.horn = P::Horn{.f = {420, 528}, .wave = "sawtooth", .lp = 2600, .db = 3};
    p.starter = P::Starter{.f = 120, .lfo = 10, .depth = 26, .db = -7};
    at(SoundKind::Motorcycle) = p;
  }
  // A minibus's 2.3 l turbo diesel four, revving higher than a truck's (the game's engine is mapped onto it):
  // clatter at idle, a turbo whistle, hydraulic brakes.
  at(SoundKind::Minibus) = SoundProfile{
      .main =
          MainVoice{
              .kind = VoiceKind::Engine,
              .turns = 2,
              .idleRpm = 750,
              .maxRpm = 4000,
              .soft = {.kind = WaveKind::Pulses, .firing = I4, .rise = 12, .decay = 46, .spread = 0.12, .skew = 2},
              .hard = {.kind = WaveKind::Pulses, .firing = I4, .rise = 5, .decay = 30, .spread = 0.08, .skew = 1.5},
              .partials = 800,
              .db = 1,
              .idle = 0.45,
              .load = 0.4,
              .revs = 0.35,
              .harden = 0.7,
              .lp = {380, 1800, 900},
              .lpQ = 1,
              .body = {120, 1, 4},
              .jitter = {7, 0.08},
              .prop = 0,
          },
      .mapRpm = true,
      .noise = NoiseVoice{.f = {1700, 2400}, .q = 1.3, .db = -9, .am = 0.85, .base = 1, .load = 0.35, .revs = -0.45, .firing = true},
      .turbo = TurboVoice{.f = {2200, 5200}, .db = -18, .spool = 0.7},
      .tones = {ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 30, .db = -28, .effort = 0.5, .fade = {3, 12}}},
      .road = P::Road{.db = -5, .lp = {280, 25}, .q = 0.7},
      .wind = P::Wind{.db = -8, .bp = {350, 13}, .q = 0.6},
      .skid = P::Skid{.f = 1000, .q = 6, .db = -4},
      .squeal = std::nullopt,
      .horn = P::Horn{.f = {349, 440}, .wave = "sawtooth", .lp = 2200, .db = 6},
      .bell = false,
      .starter = P::Starter{.f = 70, .lfo = 7, .depth = 16, .db = -4},
      .relay = P::Relay{.tick = 2400, .tock = 1800, .level = 0.35},
      .air = false,
      .beeper = false,
      .stallHorn = std::nullopt,
  };
  // A 7.5 t truck's 5 l diesel four: a slow, shaking idle, turbo, air brakes.
  {
    SoundProfile p = HEAVY;
    p.main = MainVoice{
        .kind = VoiceKind::Engine,
        .turns = 2,
        .idleRpm = 600,
        .maxRpm = 2400,
        .soft = {.kind = WaveKind::Pulses, .firing = I4, .rise = 14, .decay = 52, .spread = 0.14, .skew = 3},
        .hard = {.kind = WaveKind::Pulses, .firing = I4, .rise = 6, .decay = 34, .spread = 0.09, .skew = 2},
        .partials = 1000,
        .db = 2.5,
        .idle = 0.5,
        .load = 0.4,
        .revs = 0.3,
        .harden = 0.7,
        .lp = {320, 1600, 800},
        .lpQ = 1.5,
        .body = {95, 1, 5},
        .jitter = {8, 0.1},
        .prop = 0,
    };
    p.noise = NoiseVoice{.f = {1400, 2000}, .q = 1.2, .db = -8, .am = 0.85, .base = 1, .load = 0.35, .revs = -0.4, .firing = true};
    p.turbo = TurboVoice{.f = {1800, 4200}, .db = -15, .spool = 0.9};
    p.tones = {ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 26, .db = -26, .effort = 0.5, .fade = {3, 12}}};
    p.road = P::Road{.db = -4, .lp = {220, 20}, .q = 1};
    p.wind = P::Wind{.db = -8, .bp = {320, 12}, .q = 0.6};
    p.skid = P::Skid{.f = 800, .q = 5, .db = -3};
    p.horn = P::Horn{.f = {311, 392}, .wave = "sawtooth", .lp = 2000, .db = 7};
    p.starter = P::Starter{.f = 55, .lfo = 6, .depth = 14, .db = -3};
    at(SoundKind::LightTruck) = p;
  }
  // A heavy truck's 10–13 l diesel six under the cab: an even rumble (third order), a loud turbo, air brakes.
  {
    SoundProfile p = HEAVY;
    p.main = MainVoice{
        .kind = VoiceKind::Engine,
        .turns = 2,
        .idleRpm = 600,
        .maxRpm = 2400,
        .soft = {.kind = WaveKind::Pulses, .firing = I6, .rise = 12, .decay = 44, .spread = 0.1, .skew = 2},
        .hard = {.kind = WaveKind::Pulses, .firing = I6, .rise = 5, .decay = 28, .spread = 0.06, .skew = 1.5},
        .partials = 1000,
        .db = 3,
        .idle = 0.5,
        .load = 0.45,
        .revs = 0.3,
        .harden = 0.7,
        .lp = {300, 1700, 900},
        .lpQ = 2,
        .body = {85, 1, 5},
        .jitter = {6, 0.07},
        .prop = 0,
    };
    p.noise = NoiseVoice{.f = {1200, 1800}, .q = 1.1, .db = -9, .am = 0.8, .base = 1, .load = 0.4, .revs = -0.4, .firing = true};
    p.turbo = TurboVoice{.f = {1600, 3800}, .db = -12, .spool = 1.1};
    p.tones = {ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 22, .db = -26, .effort = 0.5, .fade = {3, 12}}};
    p.road = P::Road{.db = -4, .lp = {200, 18}, .q = 1};
    p.wind = P::Wind{.db = -7, .bp = {300, 12}, .q = 0.6};
    p.skid = P::Skid{.f = 700, .q = 5, .db = -3};
    p.horn = P::Horn{.f = {247, 311}, .wave = "sawtooth", .lp = 1800, .db = 8};
    p.starter = P::Starter{.f = 50, .lfo = 8, .depth = 14, .db = -3};
    at(SoundKind::Truck) = p;
  }
  // The heavy trucks' 13 l six (the tractor unit of the articulated lorry, the three-axle rigid): deeper and slower
  // than the 12-tonner's, a big turbo that whistles and blows off, the exhaust brake's bark; air brakes, an air horn.
  {
    SoundProfile p = HEAVY;
    p.main = MainVoice{
        .kind = VoiceKind::Engine,
        .turns = 2,
        .idleRpm = 550,
        .maxRpm = 2100,
        .soft = {.kind = WaveKind::Pulses, .firing = I6, .rise = 13, .decay = 48, .spread = 0.12, .skew = 2.2},
        .hard = {.kind = WaveKind::Pulses, .firing = I6, .rise = 5, .decay = 26, .spread = 0.07, .skew = 1.6},
        .partials = 1000,
        .db = 4,
        .idle = 0.5,
        .load = 0.5,
        .revs = 0.28,
        .harden = 0.65,
        .lp = {260, 1500, 1000},
        .lpQ = 2.4,
        .body = {72, 1.1, 6},
        .jitter = {7, 0.08},
        .prop = 0,
    };
    p.noise = NoiseVoice{.f = {1000, 1600}, .q = 1.1, .db = -9, .am = 0.8, .base = 1, .load = 0.45, .revs = -0.4, .firing = true};
    p.turbo = TurboVoice{.f = {1400, 3400}, .db = -10, .spool = 1.3};
    p.tones = {ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 19, .db = -25, .effort = 0.5, .fade = {3, 12}}};
    p.road = P::Road{.db = -3, .lp = {180, 17}, .q = 1.1};
    p.wind = P::Wind{.db = -6, .bp = {280, 11}, .q = 0.6};
    p.skid = P::Skid{.f = 650, .q = 5, .db = -2};
    p.horn = P::Horn{.f = {185, 233}, .wave = "sawtooth", .lp = 1500, .db = 9};
    p.starter = P::Starter{.f = 46, .lfo = 7, .depth = 15, .db = -2};
    at(SoundKind::Semi) = p;
  }
  // A city bus: the truck's six, but at the back – heard muffled through the long body, which booms; the
  // gearbox and its retarder whine (louder as it brakes); air brakes and air doors.
  {
    SoundProfile p = HEAVY;
    p.main = MainVoice{
        .kind = VoiceKind::Engine,
        .turns = 2,
        .idleRpm = 600,
        .maxRpm = 2400,
        .soft = {.kind = WaveKind::Pulses, .firing = I6, .rise = 12, .decay = 44, .spread = 0.1, .skew = 2},
        .hard = {.kind = WaveKind::Pulses, .firing = I6, .rise = 5, .decay = 28, .spread = 0.06, .skew = 1.5},
        .partials = 1000,
        .db = 1,
        .idle = 0.45,
        .load = 0.45,
        .revs = 0.3,
        .harden = 0.7,
        .lp = {240, 1300, 600},
        .lpQ = 2,
        .body = {70, 0.9, 6},
        .jitter = {6, 0.07},
        .prop = 0,
    };
    p.noise = NoiseVoice{.f = {1100, 1600}, .q = 1.1, .db = -13, .am = 0.8, .base = 1, .load = 0.4, .revs = -0.4, .firing = true};
    p.turbo = TurboVoice{.f = {1700, 4000}, .db = -16, .spool = 1.1};
    p.tones = {ToneVoice{.wave = "triangle", .by = ToneBy::Speed, .hz = 34, .db = -21, .effort = 0.8, .fade = {2, 10}}};
    p.road = P::Road{.db = -5, .lp = {210, 18}, .q = 1};
    p.wind = P::Wind{.db = -10, .bp = {300, 12}, .q = 0.6};
    p.skid = P::Skid{.f = 700, .q = 5, .db = -3};
    p.horn = P::Horn{.f = {294, 370}, .wave = "sawtooth", .lp = 2000, .db = 7};
    p.starter = P::Starter{.f = 50, .lfo = 8, .depth = 14, .db = -4};
    at(SoundKind::Bus) = p;
  }
  // A 40 km/h tractor's diesel four: a slow, heavy knock through an open bonnet and a straight stack,
  // mechanical clatter, the gearbox whining with the speed, the lugs of its tyres humming.
  at(SoundKind::TractorSlow) = SoundProfile{
      .main =
          MainVoice{
              .kind = VoiceKind::Engine,
              .turns = 2,
              .idleRpm = 800,
              .maxRpm = 2200,
              .soft = {.kind = WaveKind::Pulses, .firing = I4, .rise = 10, .decay = 40, .spread = 0.2, .skew = 4},
              .hard = {.kind = WaveKind::Pulses, .firing = I4, .rise = 4, .decay = 26, .spread = 0.14, .skew = 3},
              .partials = 750,
              .db = 4,
              .idle = 0.6,
              .load = 0.35,
              .revs = 0.25,
              .harden = 0.6,
              .lp = {450, 2000, 900},
              .lpQ = 3,
              .body = {140, 1.5, 6},
              .jitter = {12, 0.16},
              .prop = 0,
          },
      .mapRpm = true,
      .noise = NoiseVoice{.f = {1000, 1500}, .q = 0.9, .db = -5, .am = 0.9, .base = 1, .load = 0.4, .revs = -0.3, .firing = true},
      .turbo = std::nullopt,
      .tones =
          {
              ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 58, .db = -20, .effort = 0.4, .fade = {1, 5}},
              ToneVoice{.wave = "triangle", .by = ToneBy::Speed, .hz = 7.5, .db = -18, .effort = 0, .fade = {1, 4}},
          },
      .road = P::Road{.db = -5, .lp = {180, 25}, .q = 1},
      .wind = P::Wind{.db = -12, .bp = {320, 12}, .q = 0.6},
      .skid = P::Skid{.f = 600, .q = 3, .db = -6},
      .squeal = std::nullopt,
      .horn = P::Horn{.f = {370, 371.5}, .wave = "square", .lp = 2500, .db = 5},
      .bell = false,
      .starter = P::Starter{.f = 48, .lfo = 6, .depth = 12, .db = -3},
      .relay = P::Relay{.tick = 1500, .tock = 1200, .level = 0.5},
      .air = false,
      .beeper = false,
      .stallHorn = std::nullopt,
  };
  // A 60 km/h tractor's turbo diesel six: smoother than the four, louder, with a turbo whistle.
  at(SoundKind::Tractor) = SoundProfile{
      .main =
          MainVoice{
              .kind = VoiceKind::Engine,
              .turns = 2,
              .idleRpm = 800,
              .maxRpm = 2200,
              .soft = {.kind = WaveKind::Pulses, .firing = I6, .rise = 10, .decay = 38, .spread = 0.15, .skew = 3},
              .hard = {.kind = WaveKind::Pulses, .firing = I6, .rise = 4, .decay = 24, .spread = 0.1, .skew = 2},
              .partials = 750,
              .db = 4,
              .idle = 0.55,
              .load = 0.4,
              .revs = 0.3,
              .harden = 0.6,
              .lp = {420, 2100, 1000},
              .lpQ = 2.5,
              .body = {150, 1.4, 5},
              .jitter = {9, 0.12},
              .prop = 0,
          },
      .mapRpm = true,
      .noise = NoiseVoice{.f = {1100, 1600}, .q = 1, .db = -6, .am = 0.85, .base = 1, .load = 0.4, .revs = -0.3, .firing = true},
      .turbo = TurboVoice{.f = {2000, 4500}, .db = -16, .spool = 0.9},
      .tones =
          {
              ToneVoice{.wave = "sine", .by = ToneBy::Speed, .hz = 46, .db = -21, .effort = 0.4, .fade = {1, 5}},
              ToneVoice{.wave = "triangle", .by = ToneBy::Speed, .hz = 6, .db = -18, .effort = 0, .fade = {1, 4}},
          },
      .road = P::Road{.db = -5, .lp = {180, 22}, .q = 1},
      .wind = P::Wind{.db = -12, .bp = {320, 12}, .q = 0.6},
      .skid = P::Skid{.f = 600, .q = 3, .db = -6},
      .squeal = std::nullopt,
      .horn = P::Horn{.f = {392, 394}, .wave = "square", .lp = 2500, .db = 5},
      .bell = false,
      .starter = P::Starter{.f = 48, .lfo = 7, .depth = 12, .db = -3},
      .relay = P::Relay{.tick = 1500, .tock = 1200, .level = 0.5},
      .air = false,
      .beeper = false,
      .stallHorn = std::nullopt,
  };
  // The tram: no engine. Its traction motors and gears whine in orders of their turning – a chord that
  // rises with the speed, the gears alone while it coasts, the motors' magnetic orders when they pull or
  // brake; the inverter's carrier sings while it pulls away; fans, the rails' roar, the flanges squealing
  // in tight curves, the bell.
  at(SoundKind::Tram) = SoundProfile{
      .main =
          MainVoice{
              .kind = VoiceKind::Motor,
              .perMps = 3.45,
              .soft = {.kind = WaveKind::Orders, .orders = {{4, 0.12}, {17, 1}, {34, 0.35}, {51, 0.12}}, .scatter = true},
              .hard = {.kind = WaveKind::Orders,
                       .orders = {{2, 0.2}, {4, 0.55}, {6, 0.18}, {12, 0.3}, {17, 0.8}, {26, 0.25}, {28, 0.5}, {30, 0.25}, {34, 0.3}, {56, 0.12}},
                       .scatter = true},
              .partials = 64,
              .db = -3,
              .coast = 0.25,
              .effort = 0.9,
              .lp = {900, 100, 1500},
              .lpQ = 2,
              .body = {900, 1, 3},
              .jitter = {3, 0.04},
          },
      .mapRpm = false,
      .noise = NoiseVoice{.f = {650, 650}, .q = 0.8, .db = -14, .am = 0, .base = 1, .load = 0, .revs = 0, .firing = false},
      .turbo = std::nullopt,
      .tones = {ToneVoice{.wave = "triangle", .by = ToneBy::Fixed, .hz = 1150, .db = -16, .effort = 1, .fade = {0, 0}, .out = std::array<double, 2>{4, 12}}},
      .road = P::Road{.db = 5, .lp = {180, 32}, .q = 4},
      .wind = P::Wind{.db = -14, .bp = {350, 12}, .q = 0.6},
      .skid = std::nullopt,
      .squeal = P::Squeal{.f = 2900, .q = 28, .db = -4, .wobble = 50},
      .horn = std::nullopt,
      .bell = true,
      .starter = std::nullopt,
      .relay = P::Relay{.tick = 2000, .tock = 1500, .level = 0.3},
      .air = false,
      .beeper = false,
      .stallHorn = std::nullopt,
  };
  // A trainer's flat four driving a two-blade propeller directly: the blades pass twice a turn (the
  // propeller's drone, the soft wave, with the square of the rpm) over the raw firing of four short
  // stacks (the hard wave) – which windmilling with a dead engine leaves out. The wind of the airspeed,
  // the wheels on the runway, the stall warning's reed horn.
  at(SoundKind::Plane) = SoundProfile{
      .main =
          MainVoice{
              .kind = VoiceKind::Prop,
              .turns = 2,
              .idleRpm = 650,
              .maxRpm = 2700,
              .soft = {.kind = WaveKind::Orders,
                       .orders = {{2, 0.12}, {4, 1}, {8, 0.5}, {12, 0.33}, {16, 0.24}, {20, 0.18}, {24, 0.13}, {28, 0.1}, {32, 0.075}, {36, 0.055}, {40, 0.04}, {44, 0.03}, {48, 0.022}, {52, 0.016}, {56, 0.012}, {60, 0.009}}},
              .hard = {.kind = WaveKind::Pulses, .firing = I4, .rise = 3, .decay = 22, .spread = 0.16, .skew = 4},
              .partials = 860,
              .db = 1,
              .idle = 0,
              .load = 0,
              .revs = 0,
              .harden = 1,
              .lp = {800, 3500, 1000},
              .lpQ = 1,
              .body = {180, 1, 4},
              .jitter = {6, 0.08},
              .prop = 1.2,
          },
      .mapRpm = false,
      .noise = NoiseVoice{.f = {350, 900}, .q = 0.7, .db = -8, .am = 0.5, .base = 0.4, .load = 0.3, .revs = 0.6, .firing = false},
      .turbo = std::nullopt,
      .tones = {},
      .road = P::Road{.db = -8, .lp = {250, 25}, .q = 1},
      .wind = P::Wind{.db = -4, .bp = {350, 10}, .q = 0.5},
      .skid = P::Skid{.f = 1100, .q = 6, .db = -6},
      .squeal = std::nullopt,
      .horn = std::nullopt,
      .bell = false,
      .starter = P::Starter{.f = 60, .lfo = 5, .depth = 14, .db = -4},
      .relay = std::nullopt,
      .air = false,
      .beeper = false,
      .stallHorn = P::StallHorn{.f = 1650, .db = -2},
  };
  // On foot there is nothing to hear but one's steps (click('step')).
  at(SoundKind::OnFoot) = SoundProfile{
      .main = std::nullopt,
      .mapRpm = false,
      .noise = std::nullopt,
      .turbo = std::nullopt,
      .tones = {},
      .road = std::nullopt,
      .wind = std::nullopt,
      .skid = std::nullopt,
      .squeal = std::nullopt,
      .horn = std::nullopt,
      .bell = false,
      .starter = std::nullopt,
      .relay = P::Relay{.tick = 2600, .tock = 1900, .level = 0.35},
      .air = false,
      .beeper = false,
      .stallHorn = std::nullopt,
  };
  return all;
}

double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double clamp01(double v) { return v > 0 ? (v < 1 ? v : 1) : 0; }
double smoothstep(double a, double b, double x) {
  const double t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
/** 0 → 1 over a range of speeds; a range that is no range: always 1 */
double ramp(const std::array<double, 2>& r, double v) { return r[1] > r[0] ? smoothstep(r[0], r[1], v) : 1; }

/** `x || 0` (and `x || 1`) of a number: what it is unless it is 0 or no number */
double orElse(double x, double otherwise) { return x != 0 && x == x ? x : otherwise; }

double engineShape(const EngineVoice& m, double load, double n) { return m.idle + m.load * std::pow(load, 0.8) + m.revs * std::pow(std::max(0.0, n), 1.3); }

double dbGain(double db) { return std::pow(10, db / 20) * REF_LOUDNESS; }

/** a seed per kind of vehicle, so that its cylinders differ the same way every time */
uint32_t seedOf(std::string_view kind) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < kind.size(); i++) h = (h ^ static_cast<unsigned char>(kind[i])) * 16777619u;
  return h;
}

}  // namespace

const char* soundKindName(SoundKind kind) { return KIND_NAMES[static_cast<size_t>(kind)]; }

std::optional<SoundKind> soundKindOfName(std::string_view name) {
  for (SoundKind k : SOUND_KINDS)
    if (name == soundKindName(k)) return k;
  return std::nullopt;
}

/** Each vehicle of the game and the sound it has (with a trailer the same as without). */
SoundKind soundKindOf(std::string_view kind) {
  struct Of {
    const char* vehicle;
    SoundKind sound;
  };
  static const Of OF_VEHICLE[] = {
      {"car", SoundKind::Car},
      {"carTrailer", SoundKind::Car},
      {"moped", SoundKind::Moped},
      {"motorcycle125", SoundKind::Motorcycle125},
      {"motorcycle35", SoundKind::Motorcycle35},
      {"motorcycle", SoundKind::Motorcycle},
      {"lightTruck", SoundKind::LightTruck},
      {"lightTruckTrailer", SoundKind::LightTruck},
      {"truckSolo", SoundKind::Semi},
      {"truck", SoundKind::Truck},
      {"semi", SoundKind::Semi},
      {"minibus", SoundKind::Minibus},
      {"minibusTrailer", SoundKind::Minibus},
      {"bus", SoundKind::Bus},
      {"busTrailer", SoundKind::Bus},
      {"tractorSlow", SoundKind::TractorSlow},
      {"tractor", SoundKind::Tractor},
      {"tram", SoundKind::Tram},
      {"plane", SoundKind::Plane},
      // (the cranes: the mobile and the loader crane's diesels; the tower crane's drives are electric)
      {"mobileCrane", SoundKind::Truck},
      {"loaderCrane", SoundKind::Truck},
      {"towerCrane", SoundKind::Tram},
      // (the forklift: a small diesel)
      {"forklift", SoundKind::TractorSlow},
      {"pedestrian", SoundKind::OnFoot},
  };
  for (const Of& of : OF_VEHICLE)
    if (kind == of.vehicle) return of.sound;
  return SoundKind::Car;
}

const SoundProfile& Profiles::operator[](SoundKind kind) const {
  static const std::array<SoundProfile, SOUND_KINDS.size()>* all = new std::array<SoundProfile, SOUND_KINDS.size()>(makeProfiles());
  return (*all)[static_cast<size_t>(kind)];
}

double soundRpm(const SoundProfile& p, double rpm, const RpmScale* scale) {
  const double r = std::max(0.0, orElse(rpm, 0));
  if (!p.main) return r;
  const MainVoice& m = *p.main;
  if (m.kind == VoiceKind::Motor || !p.mapRpm || !scale || !(scale->idleRpm > 0) || !(scale->limiterRpm > scale->idleRpm)) return r;
  if (r <= scale->idleRpm) return (r * m.idleRpm) / scale->idleRpm;
  return m.idleRpm + ((r - scale->idleRpm) * (m.maxRpm - m.idleRpm)) / (scale->limiterRpm - scale->idleRpm);
}

double firingHz(const SoundProfile& p, double rpm) {
  if (!p.main) return 0;
  const MainVoice& m = *p.main;
  if (m.kind == VoiceKind::Motor || m.hard.kind != WaveKind::Pulses) return 0;
  return (rpm / 60) * (static_cast<double>(m.hard.firing.size()) / m.turns);
}

SoundTargets newTargets() { return SoundTargets(); }

SoundTargets& soundTargets(const Voicing& v, const AudioState& s, const RpmScale* scale, SoundTargets& out) {
  const SoundProfile& p = v.profile;
  const Voicing::Gain& g = v.gain;
  const double speed = orElse(std::abs(s.speed), 0);
  const double vv = std::min(speed, 90.0);
  const double load = clamp01(orElse(s.load, 0));
  const double braking = clamp01(s.braking.value_or(0));
  const MainVoice* m = p.main ? &*p.main : nullptr;
  double n = 0;
  double rpmS = 0;
  double effort = std::max(load, braking);
  bool firing = false;
  bool turning = false;
  out.level = 0;
  out.soft = 0;
  out.hard = 0;
  out.jitCents = 0;
  out.jitAmp = 0;
  if (m && m->kind != VoiceKind::Motor) {
    rpmS = soundRpm(p, s.rpm, scale);
    n = clamp((rpmS - m->idleRpm) / (m->maxRpm - m->idleRpm), 0, 1.2);
    turning = rpmS > 40;
    firing = s.engineRunning && turning;
    out.freq = std::max(1.0, rpmS / 60 / m->turns);
    const double h = firing ? std::pow(load, m->harden) : 0;
    if (m->kind == VoiceKind::Prop) {
      // the propeller with the square of its speed (windmilling too), the exhaust only while it fires
      const double x = std::min(1.15, rpmS / m->maxRpm);
      out.soft = m->prop * x * x;
      out.hard = firing ? 0.45 + 0.55 * load : 0;
      out.level = turning ? g.main : 0;
    } else {
      out.soft = 1 - h;
      out.hard = h;
      // (turned by the starter: compression without combustion, softer)
      if (firing) out.level = g.main * engineShape(*m, load, n);
      else if (turning) out.level = g.main * CRANK * engineShape(*m, 0, 0) * std::min(1.0, rpmS / m->idleRpm);
    }
    out.cutoff = m->lp[0] + (m->lp[1] - m->lp[0]) * std::min(1.0, n) + m->lp[2] * (firing ? load : 0);
    out.jitCents = m->jitter[0] * (1 - 0.6 * h);
    out.jitAmp = m->jitter[1] * (1 - 0.5 * h);
  } else if (m) {
    // the motors pull or brake only with power; the gears turn whenever the tram rolls
    effort = s.engineRunning ? clamp01(std::max(load, 0.8 * braking)) : 0;
    turning = speed > 0.05;
    firing = s.engineRunning;
    out.freq = std::max(0.5, m->perMps * speed);
    out.soft = 1 - effort;
    out.hard = effort;
    out.level = g.main * (m->coast * smoothstep(0.3, 6, speed) + m->effort * effort * (0.35 + 0.65 * smoothstep(0, 5, speed)));
    out.cutoff = m->lp[0] + m->lp[1] * speed + m->lp[2] * effort;
    out.jitCents = m->jitter[0];
    out.jitAmp = m->jitter[1];
  }

  out.noiseLevel = 0;
  if (p.noise) {
    const NoiseVoice& nz = *p.noise;
    const bool on = nz.firing ? firing : m && m->kind == VoiceKind::Motor ? s.engineRunning : turning;
    out.noiseFreq = nz.f[0] + (nz.f[1] - nz.f[0]) * std::min(1.0, n);
    if (on) out.noiseLevel = g.noise * std::max(0.0, nz.base + nz.load * load + nz.revs * n);
  }

  out.turboLevel = 0;
  if (p.turbo) {
    const TurboVoice& tb = *p.turbo;
    // the turbine runs on the exhaust's energy – load and revs – and takes its time (audio.cpp glides with tb.spool)
    const double spool = firing ? clamp01(load * std::min(1.0, n * 1.1)) : 0;
    out.turboFreq = tb.f[0] + (tb.f[1] - tb.f[0]) * std::pow(spool, 0.7);
    out.turboLevel = g.turbo * std::pow(spool, 1.2);
  }

  for (size_t i = 0; i < 2; i++) {
    const ToneVoice* tn = i < p.tones.size() ? &p.tones[i] : nullptr;
    double f = 440;
    double lv = 0;
    if (tn) {
      const double src = tn->by == ToneBy::Speed ? speed : tn->by == ToneBy::Rpm ? rpmS : 1;
      f = std::max(1.0, tn->hz * src);
      lv = g.tones[i] * (1 - tn->effort + tn->effort * effort) * ramp(tn->fade, speed) * (tn->out ? 1 - ramp(*tn->out, speed) : 1);
      if (tn->by == ToneBy::Rpm && !firing) lv = 0;
    }
    if (i == 0) {
      out.toneAFreq = f;
      out.toneALevel = lv;
    } else {
      out.toneBFreq = f;
      out.toneBLevel = lv;
    }
  }
  if (p.stallHorn) {
    out.toneAFreq = p.stallHorn->f;
    out.toneALevel = s.stallWarning.value_or(false) ? g.stall : 0;
  }

  const double rough = clamp(orElse(s.rough, 1), 1, 3);
  out.roadLevel = 0;
  if (p.road) {
    out.roadCutoff = (p.road->lp[0] + p.road->lp[1] * std::min(vv, 45.0)) * (0.85 + 0.15 * rough);
    if (!s.airborne.value_or(false) && speed > 0.05) out.roadLevel = g.road * std::min(1.6, std::pow(vv / AT_SPEED, 1.25)) * (0.4 + 0.6 * rough);
  }
  out.windLevel = 0;
  if (p.wind) {
    out.windFreq = p.wind->bp[0] + p.wind->bp[1] * std::min(vv, 60.0);
    out.windLevel = g.wind * std::min(3.0, std::pow(vv / AT_SPEED, 2));
  }

  out.skidLevel = 0;
  if (p.squeal) {
    // flanges in a curve tighter than a few hundred metres, at a fair pace; the brakes the moment before it stands
    const double c = std::abs(s.curve.value_or(0));
    const double curve = smoothstep(1.0 / 300, 1.0 / 40, c) * smoothstep(0.8, 3, speed) * (1 - 0.5 * smoothstep(8, 16, speed));
    const double stopping = smoothstep(0.15, 0.6, braking) * smoothstep(0.03, 0.3, speed) * (1 - smoothstep(1.2, 2.5, speed));
    out.skidFreq = p.squeal->f;
    out.skidLevel = g.squeal * std::max({curve, 0.45 * stopping, clamp01(orElse(s.skid, 0))});
  } else if (p.skid) {
    out.skidFreq = p.skid->f;
    out.skidLevel = g.skid * clamp01(orElse(s.skid, 0));
  }

  out.hornLevel = p.horn && s.horn ? g.horn : 0;
  out.starterLevel = p.starter && s.starter && !s.engineRunning ? g.starter : 0;
  return out;
}

double mainLoudness(const Voicing& v, const SoundTargets& t) {
  if (!v.profile.main || !v.soft || !v.hard) return 0;
  const MainVoice& m = *v.profile.main;
  const double bf = m.body[0], bq = m.body[1], bg = m.body[2];
  return t.level * waveLoudness(*v.soft, t.soft, *v.hard, t.hard, t.freq, [&](double f) { return lowpassGain(f, t.cutoff, m.lpQ) * peakingGain(f, bf, bq, bg); });
}

AudioState referenceState(const SoundProfile& p) {
  const MainVoice* m = p.main ? &*p.main : nullptr;
  const double rpm = m && m->kind != VoiceKind::Motor ? m->idleRpm + 0.45 * (m->maxRpm - m->idleRpm) : 0;
  return AudioState{.rpm = rpm, .load = 0.5, .engineRunning = true, .starter = false, .speed = 10, .skid = 0, .indicatorOn = false, .blinkOn = false, .horn = false, .rough = 1};
}

const Voicing& voicing(SoundKind kind) {
  static std::array<std::unique_ptr<Voicing>, SOUND_KINDS.size()>* voicings = new std::array<std::unique_ptr<Voicing>, SOUND_KINDS.size()>();
  std::unique_ptr<Voicing>& have = (*voicings)[static_cast<size_t>(kind)];
  if (have) return *have;
  const SoundProfile& p = PROFILES[kind];
  const MainVoice* m = p.main ? &*p.main : nullptr;
  std::optional<Partials> soft;
  std::optional<Partials> hard;
  if (m) {
    const uint32_t seed = seedOf(soundKindName(kind));
    const double period = m->kind == VoiceKind::Motor ? 360 : 360 * m->turns;
    auto count = [m](const WaveSpec& w) -> int {
      if (w.kind != WaveKind::Orders) return m->partials;
      // (the highest of its orders)
      double most = 0;
      for (const std::array<double, 2>& o : w.orders) most = std::max(most, o[0]);
      return static_cast<int>(most);
    };
    soft = wavePartials(m->soft, period, count(m->soft), seed);
    hard = wavePartials(m->hard, period, count(m->hard), seed);
  }
  have.reset(new Voicing{kind, p, std::move(soft), std::move(hard), Voicing::Gain{}});
  Voicing& v = *have;
  Voicing::Gain& g = v.gain;
  // the main voice and the engine's noise, measured at the reference with unit gains
  if (m && v.soft && v.hard) {
    const SoundTargets t = soundTargets(v, referenceState(p), nullptr, newTargets());
    g.main = dbGain(m->db) / std::max(1e-9, mainLoudness(v, t));
    if (p.noise) {
      const NoiseVoice& nz = *p.noise;
      const double pulses = std::sqrt(std::pow(1 - nz.am, 2) + std::pow(nz.am * rmsOf(*v.hard), 2));
      const double raw = noiseLoudness([&](double f) { return bandpassGain(f, t.noiseFreq, nz.q); }) * pulses * t.noiseLevel;
      g.noise = dbGain(m->db + nz.db) / std::max(1e-9, raw);
    }
  } else g.noise = 0;
  if (p.turbo) g.turbo = dbGain(p.turbo->db) / oscLoudness("sine", p.turbo->f[1]);
  for (size_t i = 0; i < p.tones.size(); i++) {
    const ToneVoice& tn = p.tones[i];
    const double mid = m && m->kind != VoiceKind::Motor ? (m->idleRpm + m->maxRpm) / 2 : 1000;
    const double f = tn.by == ToneBy::Speed ? tn.hz * 15 : tn.by == ToneBy::Rpm ? tn.hz * mid : tn.hz;
    if (i < g.tones.size()) g.tones[i] = dbGain(tn.db) / oscLoudness(tn.wave, f);
  }
  if (p.road) {
    const SoundProfile::Road& r = *p.road;
    g.road = dbGain(r.db) / noiseLoudness([&](double f) { return lowpassGain(f, r.lp[0] + r.lp[1] * AT_SPEED, r.q); });
  }
  if (p.wind) {
    const SoundProfile::Wind& w = *p.wind;
    g.wind = dbGain(w.db) / noiseLoudness([&](double f) { return bandpassGain(f, w.bp[0] + w.bp[1] * AT_SPEED, w.q); });
  }
  if (p.skid) {
    const SoundProfile::Skid& k = *p.skid;
    g.skid = dbGain(k.db) / noiseLoudness([&](double f) { return bandpassGain(f, k.f, k.q); });
  }
  if (p.squeal) {
    const SoundProfile::Squeal& q = *p.squeal;
    g.squeal = dbGain(q.db) / noiseLoudness([&](double f) { return bandpassGain(f, q.f, q.q); });
  }
  if (p.horn) {
    const SoundProfile::Horn& h = *p.horn;
    auto one = [&h](double f0) { return oscLoudness(h.wave, f0, [&h](double f) { return lowpassGain(f, h.lp, 0); }); };
    g.horn = dbGain(h.db) / std::hypot(one(h.f[0]), one(h.f[1]));
  }
  if (p.starter) g.starter = dbGain(p.starter->db) / oscLoudness("sawtooth", p.starter->f);
  if (p.stallHorn) g.stall = dbGain(p.stallHorn->db) / oscLoudness("triangle", p.stallHorn->f);
  return v;
}

}  // namespace fs
