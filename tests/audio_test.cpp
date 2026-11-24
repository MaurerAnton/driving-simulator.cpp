// SPDX-License-Identifier: AGPL-3.0-only
// LINK: audio platform core
/**
 * The sound, on the native host.  Mirrors of the original's tests –
 *
 *   tests/game/audio.test.ts       the vehicles' sounds: every vehicle of the game has one; an engine fires
 *                                  as often as its layout says, its sound follows the revs and the load, the
 *                                  vehicles are balanced against each other; the tram whines with its speed
 *                                  and squeals in tight curves, the aeroplane's propeller drones at its
 *                                  blade-passing frequency – and on the device nothing is made anew while driving
 *   tests/audio/voicepick.test.ts  the choice of the voice, on made-up lists of voices
 *   tests/sim/emergency.test.ts    its part on the two-tone horn
 *
 * – and beyond them: the numbers of the original itself (worked out with node from v1.7.7), what the
 * original's GameAudio makes and sends on its stand-in for the Web Audio API in the same drive, the other
 * sounds of the game (horns, bell, crowd, bang), and the host's own part (platform/audio.h).
 */
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "audio/audio.h"
#include "audio/boom.h"
#include "audio/crossingbell.h"
#include "audio/crowd.h"
#include "audio/profiles.h"
#include "audio/siren.h"
#include "audio/timers.h"
#include "audio/voicepick.h"
#include "audio/waves.h"
#include "core/random.h"
#include "platform/audio.h"
#include "platform/native/audio_native.h"
#include "platform/native/native.h"
#include "platform/plat.h"

// (the vehicles of the game, where they are part of the tree already: only what their header itself holds)
#if __has_include("game/vehicles.h")
#include "game/vehicles.h"
#define HAVE_VEHICLES 1
#endif

using namespace fs;

namespace {

/** stop with a word about what went wrong */
void check(bool ok, const std::string& what) {
  if (ok) return;
  std::fprintf(stderr, "audio_test: %s\n", what.c_str());
  std::abort();
}

const double INF = std::numeric_limits<double>::infinity();

double dB(double x) { return 20 * std::log10(x / REF_LOUDNESS); }

/** `state({ rpm: 2000, load: 1 })` of the original's tests: what is said, over an engine that runs and stands */
struct Over {
  std::optional<double> rpm = {}, load = {};
  std::optional<bool> engineRunning = {}, starter = {};
  std::optional<double> speed = {}, skid = {};
  std::optional<bool> indicatorOn = {}, blinkOn = {}, horn = {};
  std::optional<double> rough = {}, braking = {};
  std::optional<bool> parkingBrake = {}, reversing = {};
  std::optional<double> curve = {};
  std::optional<bool> airborne = {}, stallWarning = {};
};
AudioState state(const Over& o = Over()) {
  AudioState s;
  s.rpm = o.rpm.value_or(0);
  s.load = o.load.value_or(0);
  s.engineRunning = o.engineRunning.value_or(true);
  s.starter = o.starter.value_or(false);
  s.speed = o.speed.value_or(0);
  s.skid = o.skid.value_or(0);
  s.indicatorOn = o.indicatorOn.value_or(false);
  s.blinkOn = o.blinkOn.value_or(false);
  s.horn = o.horn.value_or(false);
  s.rough = o.rough.value_or(1);
  s.braking = o.braking;
  s.parkingBrake = o.parkingBrake;
  s.reversing = o.reversing;
  s.curve = o.curve;
  s.airborne = o.airborne;
  s.stallWarning = o.stallWarning;
  return s;
}
SoundTargets targets(const Voicing& v, const Over& o, const RpmScale* scale = nullptr) { return soundTargets(v, state(o), scale, newTargets()); }

/** the targets by their names, as `Object.entries(targets)` goes through them */
struct Field {
  const char* name;
  double SoundTargets::* at;
};
const Field FIELDS[] = {
    {"freq", &SoundTargets::freq},
    {"soft", &SoundTargets::soft},
    {"hard", &SoundTargets::hard},
    {"level", &SoundTargets::level},
    {"cutoff", &SoundTargets::cutoff},
    {"jitCents", &SoundTargets::jitCents},
    {"jitAmp", &SoundTargets::jitAmp},
    {"noiseFreq", &SoundTargets::noiseFreq},
    {"noiseLevel", &SoundTargets::noiseLevel},
    {"turboFreq", &SoundTargets::turboFreq},
    {"turboLevel", &SoundTargets::turboLevel},
    {"toneAFreq", &SoundTargets::toneAFreq},
    {"toneALevel", &SoundTargets::toneALevel},
    {"toneBFreq", &SoundTargets::toneBFreq},
    {"toneBLevel", &SoundTargets::toneBLevel},
    {"roadCutoff", &SoundTargets::roadCutoff},
    {"roadLevel", &SoundTargets::roadLevel},
    {"windFreq", &SoundTargets::windFreq},
    {"windLevel", &SoundTargets::windLevel},
    {"skidFreq", &SoundTargets::skidFreq},
    {"skidLevel", &SoundTargets::skidLevel},
    {"hornLevel", &SoundTargets::hornLevel},
    {"starterLevel", &SoundTargets::starterLevel},
};

std::vector<double> gainsOf(const Voicing& v) {
  const Voicing::Gain& g = v.gain;
  return {g.main, g.noise, g.turbo, g.tones[0], g.tones[1], g.road, g.wind, g.skid, g.squeal, g.horn, g.starter, g.stall};
}

/** the kinds with a piston engine */
std::vector<SoundKind> engines() {
  std::vector<SoundKind> out;
  for (SoundKind k : SOUND_KINDS) {
    const std::optional<MainVoice>& m = PROFILES[k].main;
    if (m && m->kind != VoiceKind::Motor) out.push_back(k);
  }
  return out;
}

double amp(const Partials& w, size_t j) { return std::hypot(static_cast<double>(w.real[j]), static_cast<double>(w.imag[j])); }

// ---------------------------------------------------------------- the vehicles' sounds (tests/game/audio.test.ts)

void everyVehicleHasASound() {
  // every vehicle of the game has a sound – with a trailer the same as without, each motorcycle class its own
#ifdef HAVE_VEHICLES
  static_assert(std::is_convertible_v<const VehicleKind&, std::string_view>, "a vehicle's kind is a text (soundKindOf takes it as one)");
  for (const VehicleKind& k : VEHICLE_KINDS) {
    const SoundKind s = soundKindOf(k);
    check(k == "car" || k == "carTrailer" || s != SoundKind::Car, std::string(k) + " has a sound of its own kind");
    (void)PROFILES[s];
  }
  // (every kind of sound is used by some vehicle)
  for (SoundKind s : SOUND_KINDS) {
    bool used = false;
    for (const VehicleKind& k : VEHICLE_KINDS) used = used || soundKindOf(k) == s;
    check(used, std::string(soundKindName(s)) + " is used");
  }
#endif
  // (the vehicles of v1.7.7, by their names)
  const char* const vehicles[] = {"car",  "carTrailer", "moped",   "motorcycle125",  "motorcycle35", "motorcycle",  "lightTruck",  "lightTruckTrailer",
                                  "truckSolo", "truck", "semi",    "minibus",        "minibusTrailer", "bus",       "busTrailer",  "tractorSlow",
                                  "tractor",   "tram",  "plane",   "mobileCrane",    "loaderCrane",  "towerCrane",  "forklift",    "pedestrian"};
  std::set<SoundKind> usedKinds;
  for (const char* k : vehicles) usedKinds.insert(soundKindOf(k));
  assert(usedKinds.size() == SOUND_KINDS.size());
  assert(soundKindOf("carTrailer") == soundKindOf("car"));
  assert(soundKindOf("busTrailer") == SoundKind::Bus);
  assert(soundKindOf("tram") == SoundKind::Tram);
  assert(soundKindOf("plane") == SoundKind::Plane);
  assert(soundKindOf("pedestrian") == SoundKind::OnFoot);
  std::set<SoundKind> bikes;
  for (const char* k : {"moped", "motorcycle125", "motorcycle35", "motorcycle"}) bikes.insert(soundKindOf(k));
  assert(bikes.size() == 4);   // four different motorcycle sounds
  // (the names of the kinds, there and back)
  for (SoundKind s : SOUND_KINDS) assert(soundKindOfName(soundKindName(s)) == s);
  assert(std::string(soundKindName(SoundKind::Motorcycle125)) == "motorcycle125" && std::string(soundKindName(SoundKind::OnFoot)) == "onFoot");
  assert(!soundKindOfName("hovercraft") && soundKindOf("hovercraft") == SoundKind::Car);
}

void profilesAreSound() {
  // the profiles are sound: engine speeds in order, firings within the cycle, waves finite and peaking at 1
  for (SoundKind k : SOUND_KINDS) {
    const SoundProfile& p = PROFILES[k];
    const Voicing& v = voicing(k);
    if (!p.main) {
      assert(!v.soft);
      continue;
    }
    const MainVoice& m = *p.main;
    if (m.kind != VoiceKind::Motor) {
      check(m.idleRpm > 0 && m.maxRpm > m.idleRpm * 2, std::string(soundKindName(k)) + ": idle and max");
      for (const WaveSpec* w : {&m.soft, &m.hard})
        if (w->kind == WaveKind::Pulses)
          for (double a : w->firing) check(a >= 0 && a < 360 * m.turns, std::string(soundKindName(k)) + ": fires within the cycle");
    }
    for (const std::optional<Partials>* w : {&v.soft, &v.hard}) {
      assert(w->has_value());
      const Partials& x = **w;
      assert(x.real.size() <= 2049);   // within what a browser keeps of a wave
      assert(x.real.size() == x.imag.size());
      for (size_t i = 0; i < x.real.size(); i++) assert(std::isfinite(x.real[i]) && std::isfinite(x.imag[i]));
      check(std::abs(peakOf(x) - 1) < 0.01, std::string(soundKindName(k)) + ": normalised to a peak of 1");
    }
    for (double g : gainsOf(v)) check(std::isfinite(g) && g >= 0, std::string(soundKindName(k)) + ": gains finite");
  }
}

void partialsArePulses() {
  // the partials of a pulse train are the pulses: the wave they make has a pulse at every firing
  // (a four-stroke four, no differences between cylinders: four equal pulses per cycle, a quarter apart)
  const Partials w = wavePartials(WaveSpec{.kind = WaveKind::Pulses, .firing = {0, 180, 360, 540}, .rise = 6, .decay = 30, .spread = 0, .skew = 0}, 720, 512, 1);
  const std::vector<double> x = samples(w, 4096);
  std::vector<int> peaks;
  for (int i = 0; i < 4096; i++)
    if (x[static_cast<size_t>(i)] > 0.9) peaks.push_back(i);
  std::vector<int> groups;
  for (size_t j = 0; j < peaks.size(); j++)
    if (j == 0 || peaks[j] - peaks[j - 1] > 50) groups.push_back(peaks[j]);
  assert(groups.size() == 4);   // four pulses
  for (size_t j = 1; j < 4; j++) assert(std::abs(groups[j] - groups[j - 1] - 1024) < 8);   // a quarter cycle apart
  // an even four has nothing between its firing orders; the RMS is that of its partials
  for (size_t k = 1; k < 40; k++)
    if (k % 4) assert(amp(w, k) < 1e-6);
  double s = 0;
  for (int i = 0; i < 4096; i++) s += x[static_cast<size_t>(i)] * x[static_cast<size_t>(i)];
  assert(std::abs(std::sqrt(s / 4096) - rmsOf(w)) < 1e-6);
}

void firingFrequency() {
  // firing frequency: rpm/60 × cylinders/2 for a four-stroke, rpm/60 for the two-stroke – and the waves are strongest there
  assert(firingHz(PROFILES[SoundKind::Car], 3000) == 100);   // a four at 3000 rpm: 100 Hz
  assert(firingHz(PROFILES[SoundKind::Truck], 1200) == 60);   // a six at 1200 rpm: 60 Hz
  assert(firingHz(PROFILES[SoundKind::Motorcycle125], 6000) == 50);   // a single: once every two turns
  assert(firingHz(PROFILES[SoundKind::Moped], 6000) == 100);   // the two-stroke: every turn
  assert(firingHz(PROFILES[SoundKind::Motorcycle35], 6000) == 100);   // a twin
  for (SoundKind k : {SoundKind::Car, SoundKind::Minibus, SoundKind::LightTruck, SoundKind::Truck, SoundKind::Bus, SoundKind::TractorSlow, SoundKind::Tractor, SoundKind::Motorcycle}) {
    const std::string name = soundKindName(k);
    const std::optional<MainVoice>& m = PROFILES[k].main;
    assert(m && m->kind == VoiceKind::Engine && m->hard.kind == WaveKind::Pulses);
    const size_t cylinders = m->hard.firing.size();
    const Voicing& v = voicing(k);
    const SoundTargets t = targets(v, {.rpm = 2000, .load = 1});
    // the main voice's fundamental is a whole cycle; the firings make the partial `cylinders` of it
    check(std::abs(t.freq * static_cast<double>(cylinders) - firingHz(PROFILES[k], 2000)) < 1e-9, name + ": firing order");
    const Partials& hard = *v.hard;
    size_t best = 1;
    for (size_t j = 1; j <= 2 * cylinders; j++)
      if (amp(hard, j) > amp(hard, best)) best = j;
    check(best == cylinders, name + ": strongest at the firing frequency");
    // the cylinders differ a little: the half orders are there, but well below
    check(amp(hard, 1) > 0 && amp(hard, 1) < 0.25 * amp(hard, cylinders), name + ": half orders present but weak");
  }
}

void engineSpeedsAreMapped() {
  // the game's engine speeds are mapped onto the sound's own engine (and taken as they are without a mapping)
  const RpmScale truckEngine{600, 2400};
  assert(soundRpm(PROFILES[SoundKind::Minibus], 600, &truckEngine) == 750);   // idle onto idle
  assert(soundRpm(PROFILES[SoundKind::Minibus], 2400, &truckEngine) == 4000);   // limiter onto its highest
  assert(soundRpm(PROFILES[SoundKind::Minibus], 300, &truckEngine) == 375);   // cranking in proportion
  assert(soundRpm(PROFILES[SoundKind::Car], 3210, nullptr) == 3210);
  const RpmScale carEngine{800, 6500};
  assert(soundRpm(PROFILES[SoundKind::Car], 3210, &carEngine) == 3210);   // the car's own engine: unchanged
  assert(soundRpm(PROFILES[SoundKind::Plane], 2300, &carEngine) == 2300);   // the aeroplane's rpm are its own
  // (every road vehicle of the game with its own engine – its idle onto the profile's idle, its limiter onto the
  //  highest: the original goes through the game's vehicles here, which the game's tests can do; whatever the
  //  engine, idle and limiter land where they should)
  for (SoundKind k : SOUND_KINDS) {
    const SoundProfile& p = PROFILES[k];
    if (!p.mapRpm || !p.main || p.main->kind == VoiceKind::Motor) continue;
    for (const RpmScale& spec : {RpmScale{600, 2400}, RpmScale{800, 6500}, RpmScale{750, 4400}, RpmScale{1300, 9800}}) {
      assert(std::abs(soundRpm(p, spec.idleRpm, &spec) - p.main->idleRpm) < 1e-9);
      assert(std::abs(soundRpm(p, spec.limiterRpm, &spec) - p.main->maxRpm) < 1e-9);
    }
  }
}

void louderAndBrighter() {
  // an engine sounds louder and brighter with load and with revs, its idle well below full load
  for (SoundKind k : engines()) {
    const std::string name = soundKindName(k);
    const Voicing& v = voicing(k);
    const MainVoice& m = *v.profile.main;
    auto at = [&](double n, double load) { return targets(v, {.rpm = m.idleRpm + n * (m.maxRpm - m.idleRpm), .load = load}); };
    for (double n : {0.0, 0.3, 0.6, 0.9}) {
      double last = -INF;
      double lastCut = 0;
      for (double load : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const SoundTargets t = at(n, load);
        check(t.level >= last - 1e-9, name + ": level grows with load");
        check(t.cutoff >= lastCut, name + ": the low-pass opens with load");
        last = t.level;
        lastCut = t.cutoff;
      }
    }
    for (double load : {0.0, 0.5, 1.0}) check(at(0.8, load).freq > at(0.2, load).freq, name + ": pitch rises with revs");
    const double idle = dB(mainLoudness(v, at(0, 0.08)));
    const double full = dB(mainLoudness(v, at(0.9, 1)));
    check(full - idle > 9, name + ": idle well below full load");
  }
}

void balanced() {
  // the vehicles are balanced: each as loud as its profile says, none far louder than the others
  std::vector<double> fulls;
  for (SoundKind k : SOUND_KINDS) {
    const std::string name = soundKindName(k);
    const Voicing& v = voicing(k);
    if (!v.profile.main) continue;
    const MainVoice& m = *v.profile.main;
    const double ref = dB(mainLoudness(v, soundTargets(v, referenceState(v.profile), nullptr, newTargets())));
    check(std::abs(ref - m.db) < 0.1, name + ": as loud at its reference as asked");
    check(m.db >= -6 && m.db <= 6, name + ": within ±6 dB of the car");
    if (m.kind != VoiceKind::Motor) fulls.push_back(dB(mainLoudness(v, targets(v, {.rpm = m.maxRpm * 0.95, .load = 1}))));
  }
  double lo = INF, hi = -INF;
  for (double f : fulls) {
    lo = std::min(lo, f);
    hi = std::max(hi, f);
  }
  check(hi - lo < 6, "full load within 6 dB of each other");
}

void silentWhenOff() {
  // silent when the engine is off; the starter while cranking; compression while turned without firing
  for (SoundKind k : engines()) {
    const std::string name = soundKindName(k);
    const Voicing& v = voicing(k);
    const SoundTargets off = targets(v, {.rpm = 0, .engineRunning = false});
    check(off.level == 0, name + ": off");
    assert(off.noiseLevel == 0);
    assert(off.turboLevel == 0);
    if (v.profile.starter) {
      const SoundTargets crank = targets(v, {.rpm = 250, .engineRunning = false, .starter = true});
      check(crank.starterLevel > 0, name + ": the starter whirrs");
      if (v.profile.main->kind == VoiceKind::Engine) check(crank.level > 0 && crank.hard == 0, name + ": compression, no combustion");
      check(targets(v, {.rpm = 800, .starter = true}).starterLevel == 0, name + ": not once it runs");
    }
  }
  const SoundTargets foot = targets(voicing(SoundKind::OnFoot), {.rpm = 3000, .load = 1, .speed = 1.4, .skid = 1, .horn = true});
  for (const Field& f : FIELDS) {
    const std::string name = f.name;
    const bool isLevel = name == "level" || (name.size() > 5 && name.compare(name.size() - 5, 5, "Level") == 0);
    if (isLevel) check(foot.*f.at == 0, "on foot: " + name);
  }
}

void neverNaN() {
  // the old fields alone still work, and nothing ever comes out as NaN
  uint32_t seed = 7;
  auto r = [&seed]() -> double {
    seed = seed * 1664525u + 1013904223u;
    return seed / 4294967296.0;
  };
  for (SoundKind k : SOUND_KINDS) {
    const Voicing& v = voicing(k);
    SoundTargets out = newTargets();
    for (int i = 0; i < 200; i++) {
      // (each number drawn in the order the original draws it)
      AudioState s = state();
      s.rpm = r() * 12000 - 500;
      s.load = r() * 1.4 - 0.2;
      s.engineRunning = r() < 0.8;
      s.starter = r() < 0.1;
      s.speed = r() * 100 - 10;
      s.skid = r();
      s.horn = r() < 0.5;
      s.rough = r() * 3;
      if (i % 2) {
        s.braking = r();
        s.curve = r() * 0.05;
        s.airborne = r() < 0.5;
        s.stallWarning = r() < 0.5;
        s.reversing = r() < 0.5;
        s.parkingBrake = r() < 0.5;
      }
      const RpmScale truck{600, 2400};
      soundTargets(v, s, i % 3 ? nullptr : &truck, out);
      for (const Field& f : FIELDS) check(std::isfinite(out.*f.at) && out.*f.at >= 0, std::string(soundKindName(k)) + ": " + f.name + " is a number, and not below 0");
    }
  }
}

void turboAndClatter() {
  // turbo and diesel clatter: the whistle comes with boost, the clatter is a diesel's at idle
  const Voicing& v = voicing(SoundKind::Truck);
  const SoundTargets idle = targets(v, {.rpm = 600, .load = 0.08});
  const SoundTargets boost = targets(v, {.rpm = 1900, .load = 1});
  assert(idle.turboLevel == 0);   // no boost at idle
  assert(boost.turboLevel > 0 && boost.turboFreq > idle.turboFreq);   // whistling, higher, under boost
  assert(idle.noiseLevel > 0);   // clatter at idle
  // (a petrol engine's intake roar comes with the load instead)
  const Voicing& car = voicing(SoundKind::Car);
  const double a = targets(car, {.rpm = 3000, .load = 0.05}).noiseLevel;
  const double b = targets(car, {.rpm = 3000, .load = 1}).noiseLevel;
  assert(b > 2 * a);   // intake roar under load
}

void theTram() {
  // the tram: its motors whine higher the faster it goes, louder as they pull or brake; its wheels squeal in tight curves
  const Voicing& v = voicing(SoundKind::Tram);
  auto at = [&v](const Over& over) { return targets(v, over); };
  const double f5 = at({.speed = 5}).freq;
  const double f10 = at({.speed = 10}).freq;
  assert(std::abs(f10 / f5 - 2) < 1e-9);   // pitch in proportion to the speed
  // the gear mesh order at full speed: about a kilohertz – a whine, not a hum
  const MainVoice& main = *PROFILES[SoundKind::Tram].main;
  const double mesh = main.kind == VoiceKind::Motor ? main.perMps * 17 * (70 / 3.6) : 0;
  assert(mesh > 800 && mesh < 2000);
  assert(at({.load = 1, .speed = 10}).level > at({.speed = 10}).level * 1.5);   // louder pulling than coasting
  assert(at({.speed = 10, .braking = 0.8}).level > at({.speed = 10}).level * 1.5);   // louder braking electrically
  assert(at({.load = 1, .engineRunning = false, .speed = 10}).hard == 0);   // no current, no magnetic whine
  assert(at({.load = 1, .speed = 1}).toneALevel > 0);   // the inverter sings as it pulls away
  assert(at({.load = 1, .speed = 15}).toneALevel == 0);   // … and no longer at speed
  assert(at({.speed = 0}).noiseLevel > 0);   // its fans while it stands with power
  // the squeal: a curve of 25 m at 15 km/h – not on the straight, not in a wide curve, not standing
  const double tight = at({.speed = 4, .curve = 1.0 / 25}).skidLevel;
  assert(tight > 0);   // squeals in a tight curve
  assert(at({.speed = 4, .curve = 0}).skidLevel == 0);   // not on the straight
  assert(at({.speed = 4, .curve = 1.0 / 500}).skidLevel == 0);   // not in a wide curve
  assert(at({.speed = 0, .curve = 1.0 / 25}).skidLevel == 0);   // not standing
  assert(at({.speed = 0.5, .braking = 1}).skidLevel > 0);   // the brakes just before it stands
  assert(at({.speed = 12}).roadLevel > at({.speed = 4}).roadLevel);   // the rails roar with speed
  assert(targets(v, {.speed = 10}).level > 0);   // the gears whine while it coasts
}

void theAeroplane() {
  // the aeroplane: the propeller drones at the blade-passing frequency, which windmilling keeps; the stall horn
  const Voicing& v = voicing(SoundKind::Plane);
  const Partials& soft = *v.soft;
  size_t best = 1;
  for (size_t k = 1; k < soft.real.size(); k++)
    if (amp(soft, k) > amp(soft, best)) best = k;
  const SoundTargets t = targets(v, {.rpm = 2400, .load = 0.8, .speed = 50, .airborne = true});
  // two blades: twice a turn – 2400 rpm, 80 Hz
  assert(std::abs(static_cast<double>(best) * t.freq - (2400.0 / 60) * 2) < 1e-9);
  assert(t.soft > 0 && t.hard > 0);   // propeller and exhaust
  const SoundTargets windmill = targets(v, {.rpm = 1200, .engineRunning = false, .speed = 40, .airborne = true});
  assert(windmill.level > 0 && windmill.soft > 0 && windmill.hard == 0);   // a dead engine: the propeller only
  assert(t.roadLevel == 0);   // no wheels rumbling in the air
  assert(targets(v, {.rpm = 1200, .speed = 20}).roadLevel > 0);   // on the runway they do
  assert(t.windLevel > targets(v, {.rpm = 2400, .speed = 25}).windLevel);   // more wind, faster
  assert(targets(v, {.rpm = 2000, .speed = 25, .stallWarning = true}).toneALevel > 0);   // the stall horn
  assert(t.toneALevel == 0);
  assert(targets(v, {.rpm = 2000, .horn = true}).hornLevel == 0);   // no horn
}

void aWeighting() {
  // A-weighting: 1 at 1 kHz, little of a low hum
  assert(std::abs(aWeight(1000) - 1) < 0.005);
  assert(20 * std::log10(aWeight(100)) < -18 && 20 * std::log10(aWeight(100)) > -20.5);
}

// ---------------------------------------------------------------- the numbers of the original

/** What the original (v1.7.7) works out, by node: waves, filters, the gains and the targets of every vehicle. */
struct Ref {
  const char* name;
  std::vector<double> v;
};
const std::vector<Ref>& refs() {
  static const std::vector<Ref> all = {
      {"seeded", {0.9797282677609, 0.3067522644997, 0.484205421526}},
      {"aWeight", {0.03063671908987, 0.9999961084763, 0.8762888109872}},
      {"filters", {1.325382387177, 0.4404276809188, 0.6750693079468, 1.297559962062}},
      {"noiseLoudness", {0.3120343260742, 0.1230046181462}},
      {"oscLoudness", {0.4412869886994, 0.723160959296, 0.242461719898, 0.6492995580809}},
      {"wave", {513, -0.01594272069633, 0.006818281020969, 0.214728012681, 0.3467394709587, 9.372762178828e-06, 0.7324120805455, 4.300798385816, 0.9999999969033, 0.3725322605964}},
      {"gain.car", {0.2063329232458, 0.1536355508445, 0.00346020953584, 0.001780373366598, 0, 0.1056185585675, 0.0641238928068, 0.2803449982528, 0, 0.08994337508971, 0.07780886698957, 0}},
      {"waves.car", {721, 721, 3.783534398116, 3.787468360139, 0.4858222958362, 0.3979646256334}},
      {"targets.car.0", {7.5, 0.8415106807539, 0.1584893192461, 0.1389459758889, 738.0701754386, 4.524532042262, 0.04603776701885, 522.8070175439, 0.06022783129157, 2846.565492937, 1.913038868772e-06, 1, 0, 440, 0, 300, 0, 380, 0, 1150, 0, 0, 0}},
      {"loud.car.0", {0.004859126936886}},
      {"targets.car.1", {72.63888888889, 0.163488357927, 0.836511642073, 0.3060775233104, 3080, 2.490465073781, 0.02908720894817, 1800, 0.2243079042329, 5964.934415974, 0.002647344031519, 836, 0.001566728562606, 440, 0, 1080.88, 0.1232808061644, 688, 0.0344844045761, 1150, 0.08410349947584, 0.08994337508971, 0}},
      {"loud.car.1", {0.08596968164341}},
      {"targets.car.2", {2.083333333333, 1, 0, 0.01354059808801, 600, 5, 0.05, 500, 0, 2800, 0, 19, 0, 440, 0, 314, 0.0006324863732151, 387, 1.781219244633e-05, 1150, 0, 0, 0.07780886698957}},
      {"loud.car.2", {7.537877499036e-05}},
      {"targets.car.3", {19.16666666667, 0, 1, 0.2312011392089, 2121.052631579, 2, 0.025, 842.1052631579, 0.1928530467179, 4353.560197659, 0.0007816877478629, 1824, 0.001780373366598, 440, 0, 2028, 0, 1052, 0.1641571655854, 1150, 0.2803449982528, 0, 0}},
      {"loud.car.3", {0.02689548932401}},
      {"gain.moped", {0.2614979693777, 0.4659132566308, 0, 0, 0, 0.04820307443858, 0.1024360128899, 0.2230043709522, 0, 0.02768647095214, 0.02989687062425, 0}},
      {"waves.moped", {361, 361, 2.970527200134, 3.351244844298, 0.1928098501771, 0.1892899956343}},
      {"targets.moped.0", {7.5, 0.748811356849, 0.251188643151, 0.1342519402804, 1100, 12.73930221164, 0.1573930221164, 2500, 0.121137446724, 3000, 0, 440, 0, 440, 0, 400, 0, 480, 0, 1350, 0, 0, 0}},
      {"loud.moped.0", {0.002823276614492}},
      {"targets.moped.1", {104.1203703704, 0.1253103408454, 0.8746896591546, 0.3543201880852, 6100, 7.127793067608, 0.1012779306761, 4000, 0.5404593776918, 3000, 0, 440, 0, 440, 0, 1250.8, 0.05626391760115, 876, 0.05508781137635, 1350, 0.06690131128566, 0.02768647095214, 0}},
      {"loud.moped.1", {0.07896336864573}},
      {"targets.moped.2", {2.083333333333, 1, 0, 0.005720268080138, 900, 15, 0.18, 2500, 0, 3000, 0, 440, 0, 440, 0, 415, 0.0002886593809174, 489, 2.845444802498e-05, 1350, 0, 0, 0.02989687062425}},
      {"loud.moped.2", {2.545404502456e-05}},
      {"targets.moped.3", {19.16666666667, 0, 1, 0.2256376451, 3133.766233766, 6, 0.09, 2597.402597403, 0.384832248334, 3000, 0, 440, 0, 440, 0, 2275, 0, 1344, 0.2622361929982, 1350, 0.2230043709522, 0, 0}},
      {"loud.moped.3", {0.02090847737214}},
      {"gain.motorcycle125", {0.4350376839311, 0.5832504433921, 0, 0.001048214242126, 0, 0.0614490553297, 0.1446947141285, 0.2230043709522, 0, 0.03652364478476, 0.04223604137402, 0}},
      {"waves.motorcycle125", {401, 401, 2.912934905515, 2.978580716854, 0.2309459248638, 0.1697695677148}},
      {"targets.motorcycle125.0", {7.5, 0.8004737685031, 0.1995262314969, 0.223346488318, 850, 8.802842611019, 0.1260331637952, 1200, 0.1516451152819, 3000, 0, 1, 0, 440, 0, 380, 0, 480, 0, 1350, 0, 0, 0}},
      {"loud.motorcycle125.0", {0.004658297939248}},
      {"targets.motorcycle125.1", {110.8796296296, 0.144612320007, 0.855387679993, 0.5618904273285, 5000, 4.867673920042, 0.08012286240049, 3000, 0.6765705143348, 3000, 0, 1540, 0.0009853213875989, 440, 0, 1227.2, 0.07172498074047, 876, 0.0778136018202, 1350, 0.06690131128566, 0.03652364478476, 0}},
      {"loud.motorcycle125.1", {0.08161346881414}},
      {"targets.motorcycle125.2", {2.083333333333, 1, 0, 0.01141973920319, 700, 10, 0.14, 1200, 0, 3000, 0, 35, 0, 440, 0, 395, 0.0003679816376035, 489, 4.01929761468e-05, 1350, 0, 0, 0.04223604137402}},
      {"loud.motorcycle125.2", {3.838806178285e-05}},
      {"targets.motorcycle125.3", {19.16666666667, 0, 1, 0.3778424806136, 2491.764705882, 4, 0.07, 1369.411764706, 0.4885580184649, 3000, 0, 3360, 0.001048214242126, 440, 0, 2249, 0, 1344, 0.3704184681689, 1350, 0.2230043709522, 0, 0}},
      {"loud.motorcycle125.3", {0.02375915273402}},
      {"gain.motorcycle35", {0.3655755847213, 0.4974022369829, 0, 0.001109087468925, 0, 0.0614490553297, 0.1446947141285, 0.2230043709522, 0, 0.05795165810756, 0.05086500909377, 0}},
      {"waves.motorcycle35", {481, 481, 3.617891250109, 3.757701306079, 0.2968795360753, 0.2271120570876}},
      {"targets.motorcycle35.0", {7.5, 0.8004737685031, 0.1995262314969, 0.1888437398579, 750, 6.161989827713, 0.09002368842516, 900, 0.1293245816156, 3000, 0, 1, 0, 440, 0, 380, 0, 480, 0, 1350, 0, 0, 0}},
      {"loud.motorcycle35.0", {0.006147664299512}},
      {"targets.motorcycle35.1", {100.1157407407, 0.144612320007, 0.855387679993, 0.4782900224761, 4700, 3.40737174403, 0.05723061600035, 2500, 0.5769865949002, 3000, 0, 1320, 0.00104254222079, 440, 0, 1227.2, 0.07172498074047, 876, 0.0778136018202, 1350, 0.06690131128566, 0.05795165810756, 0}},
      {"loud.motorcycle35.1", {0.09443948654813}},
      {"targets.motorcycle35.2", {2.083333333333, 1, 0, 0.01151563091872, 600, 7, 0.1, 900, 0, 3000, 0, 30, 0, 440, 0, 395, 0.0003679816376035, 489, 4.01929761468e-05, 1350, 0, 0, 0.05086500909377}},
      {"loud.motorcycle35.2", {6.558631436729e-05}},
      {"targets.motorcycle35.3", {19.16666666667, 0, 1, 0.3289273059402, 2492.903225806, 2.8, 0.05, 1116.774193548, 0.4248777817841, 3000, 0, 2880, 0.001109087468925, 440, 0, 2249, 0, 1344, 0.3704184681689, 1350, 0.2230043709522, 0, 0}},
      {"loud.motorcycle35.3", {0.02899129978892}},
      {"gain.motorcycle", {0.2046881816078, 0.3321333102943, 0, 0.001151341567072, 0, 0.0614490553297, 0.1446947141285, 0.2230043709522, 0, 0.0630385955755, 0.05293096518571, 0}},
      {"waves.motorcycle", {501, 501, 3.995188091326, 3.991061277347, 0.4028024083894, 0.3006245925401}},
      {"targets.motorcycle.0", {7.5, 0.8004737685031, 0.1995262314969, 0.09550044667861, 900, 4.401421305509, 0.05401421305509, 700, 0.07472999481621, 3000, 0, 1, 0, 440, 0, 380, 0, 480, 0, 1350, 0, 0, 0}},
      {"loud.motorcycle.0", {0.005294850475186}},
      {"targets.motorcycle.1", {123.4259259259, 0.144612320007, 0.855387679993, 0.2835068191757, 6600, 2.433836960021, 0.03433836960021, 2400, 0.4483799688973, 3000, 0, 1210, 0.001082261073048, 440, 0, 1227.2, 0.07172498074047, 876, 0.0778136018202, 1350, 0.06690131128566, 0.0630385955755, 0}},
      {"loud.motorcycle.1", {0.1022425175865}},
      {"targets.motorcycle.2", {2.083333333333, 1, 0, 0.00597007196356, 700, 5, 0.06, 700, 0, 3000, 0, 27.5, 0, 440, 0, 395, 0.0003679816376035, 489, 4.01929761468e-05, 1350, 0, 0, 0.05293096518571}},
      {"loud.motorcycle.2", {5.785613633465e-05}},
      {"targets.motorcycle.3", {19.16666666667, 0, 1, 0.1738047632459, 3182.653061224, 2, 0.03, 890.8163265306, 0.3175601140263, 3000, 0, 2640, 0.001151341567072, 440, 0, 2249, 0, 1344, 0.3704184681689, 1350, 0.2230043709522, 0, 0}},
      {"loud.motorcycle.3", {0.02600089145931}},
      {"gain.minibus", {0.4084607159856, 0.181720877571, 0.005065667306652, 0.002658808544962, 0, 0.1294295506981, 0.07550869131557, 0.3122097364535, 0, 0.09671238707049, 0.09790489895888, 0}},
      {"waves.minibus", {801, 801, 3.578705595233, 3.725320068701, 0.4738489325105, 0.368730893701}},
      {"targets.minibus.0", {7.5, 0.8004737685031, 0.1995262314969, 0.2123243241442, 535.5384615385, 6.161989827713, 0.07201895074012, 1732.307692308, 0.1843069054441, 2274.309042105, 8.940408514741e-06, 1, 0, 440, 0, 280, 0, 350, 0, 1000, 0, 0, 0}},
      {"loud.minibus.0", {0.008184209324088}},
      {"targets.minibus.1", {43.86574074074, 0.144612320007, 0.855387679993, 0.5016784130038, 2520, 3.40737174403, 0.04578449280028, 2400, 0.1344734494025, 4766.163039979, 0.003875650873457, 660, 0.002392927690465, 440, 0, 979.4, 0.1510736329673, 636, 0.0406068962186, 1000, 0.09366292093604, 0.09671238707049, 0}},
      {"loud.minibus.1", {0.1016938432359}},
      {"targets.minibus.2", {2.083333333333, 1, 0, 0.02144418758924, 380, 7, 0.08, 1700, 0, 2200, 0, 15, 0, 440, 0, 292.5, 0.0007750761629226, 356.5, 2.097463647655e-05, 1000, 0, 0, 0.09790489895888}},
      {"loud.minibus.2", {0.0001373935011799}},
      {"targets.minibus.3", {19.16666666667, 0, 1, 0.4017926815371, 1957.230769231, 2.8, 0.04, 2033.846153846, 0.2063230886883, 4109.897104741, 0.002335855055528, 1440, 0.002658808544962, 440, 0, 1826.5, 0, 974, 0.1933022497679, 1000, 0.3122097364535, 0, 0}},
      {"loud.minibus.3", {0.0454114370617}},
      {"gain.lightTruck", {0.7364417431, 0.2407660033419, 0.006809061468563, 0.00376429657549, 0, 0.1754931639271, 0.07969710103443, 0.3853376767928, 0, 0.1147014891438, 0.1239510374998, 0}},
      {"waves.lightTruck", {1001, 1001, 3.587766994803, 3.72269189497, 0.483821896433, 0.3877041333573}},
      {"targets.lightTruck.0", {7.5, 0.8004737685031, 0.1995262314969, 0.4364192519347, 613.3333333333, 7.042274088815, 0.09002368842516, 1500, 0.2331417465694, 1946.042832014, 5.610182336707e-05, 1, 0, 440, 0, 220, 0, 320, 0, 800, 0, 0, 0}},
      {"loud.lightTruck.0", {0.01599788360077}},
      {"targets.lightTruck.1", {25.83333333333, 0.144612320007, 0.855387679993, 0.8946617191502, 2240, 3.894139136034, 0.05723061600035, 2000, 0.1926128026736, 3852.930431983, 0.005209490365347, 572, 0.003387866917941, 440, 0, 778.8, 0.2048403142282, 584, 0.04285932988963, 800, 0.1156013030378, 0.1147014891438, 0}},
      {"loud.lightTruck.1", {0.1235835006089}},
      {"targets.lightTruck.2", {2.083333333333, 1, 0, 0.05369887710104, 320, 8, 0.1, 1400, 0, 1800, 0, 13, 0, 440, 0, 230, 0.001050923590341, 326, 2.213808362067e-05, 800, 0, 0, 0.1239510374998}},
      {"loud.lightTruck.2", {0.000328901453895}},
      {"targets.lightTruck.3", {19.16666666667, 0, 1, 0.8679085882306, 2328.888888889, 3.2, 0.05, 1966.666666667, 0.2340780588047, 4200, 0.006809061468563, 1248, 0.00376429657549, 440, 0, 1456, 0, 896, 0.2040245786481, 800, 0.3853376767928, 0, 0}},
      {"loud.lightTruck.3", {0.09377356185253}},
      {"gain.truck", {0.5058927485955, 0.2023981975425, 0.009465002974348, 0.004372247826603, 0, 0.1943636639485, 0.09117808718656, 0.4355704010601, 0, 0.1437348652507, 0.1300075446133, 0}},
      {"waves.truck", {1001, 1001, 4.25003270349, 4.30272087227, 0.5556236428166, 0.444989846927}},
      {"targets.truck.0", {7.5, 0.8004737685031, 0.1995262314969, 0.3038036345666, 623.3333333333, 5.281705566611, 0.06301658189761, 1300, 0.1970009122747, 1733.872596013, 7.798489226264e-05, 1, 0, 440, 0, 200, 0, 300, 0, 700, 0, 0, 0}},
      {"loud.truck.0", {0.01785732957498}},
      {"targets.truck.1", {25.83333333333, 0.144612320007, 0.855387679993, 0.6357399507898, 2420, 2.920604352025, 0.04006143120025, 1800, 0.1700144859357, 3481.852895984, 0.00724150340403, 484, 0.003935023043943, 440, 0, 703.28, 0.2268664665154, 564, 0.04903354910922, 700, 0.130671120318, 0.1437348652507, 0}},
      {"loud.truck.1", {0.122181125826}},
      {"targets.truck.2", {2.083333333333, 1, 0, 0.03688801291842, 300, 6, 0.07, 1200, 0, 1600, 0, 11, 0, 440, 0, 209, 0.001163927727882, 306, 2.532724644071e-05, 700, 0, 0, 0.1300075446133}},
      {"loud.truck.2", {0.0003838559015394}},
      {"targets.truck.3", {19.16666666667, 0, 1, 0.6214974808229, 2522.222222222, 2.4, 0.035, 1766.666666667, 0.2068959352656, 3800, 0.009465002974348, 1056, 0.004372247826603, 440, 0, 1313, 0, 876, 0.2334159031976, 700, 0.4355704010601, 0, 0}},
      {"loud.truck.3", {0.09779261765323}},
      {"gain.semi", {0.6706977544168, 0.2517655368672, 0.01175949560166, 0.005655576241108, 0, 0.2325848072737, 0.1077968684971, 0.5248832274437, 0, 0.1882754384083, 0.1520860007924, 0}},
      {"waves.semi", {1001, 1001, 4.276123811448, 4.250419824373, 0.5264111018962, 0.4187810832808}},
      {"targets.semi.0", {7.5, 0.7761278861432, 0.2238721138568, 0.4156338614729, 640, 6.059737121801, 0.07104511544573, 1135.483870968, 0.2403548730188, 1550.5290489, 0.0001394901468928, 1, 0, 440, 0, 180, 0, 280, 0, 650, 0, 0, 0}},
      {"loud.semi.0", {0.02202852643975}},
      {"targets.semi.1", {22.52314814815, 0.1350151687699, 0.8649848312301, 0.8538959824462, 2300, 3.367063708834, 0.0454006067508, 1600, 0.2215536724431, 3110.775359986, 0.008996978411935, 418, 0.005090018616997, 440, 0, 653.72, 0.2714792071697, 522, 0.05797076039178, 650, 0.1574649682331, 0.1882754384083, 0}},
      {"loud.semi.1", {0.1374102158373}},
      {"targets.semi.2", {2.083333333333, 1, 0, 0.0533509577377, 260, 7, 0.08, 1000, 0, 1400, 0, 9.5, 0, 440, 0, 188.5, 0.001392811293893, 285.5, 2.994357458253e-05, 650, 0, 0, 0.1520860007924}},
      {"loud.semi.2", {0.0005116239114969}},
      {"targets.semi.3", {19.16666666667, 0, 1, 0.8905865696446, 2500, 2.8, 0.04, 1600, 0.2513594634206, 3400, 0.01175949560166, 912, 0.005655576241108, 440, 0, 1228.5, 0, 808, 0.2759599833526, 650, 0.5248832274437, 0, 0}},
      {"loud.semi.3", {0.1298870310864}},
      {"gain.bus", {0.4052132897326, 0.1077986969339, 0.006018179316417, 0.00656152677417, 0, 0.1708880193146, 0.06454914245193, 0.4355704010601, 0, 0.1175094924982, 0.1158693461011, 0}},
      {"waves.bus", {1001, 1001, 4.284758219222, 4.344346735568, 0.5580070065581, 0.4430739687077}},
      {"targets.bus.0", {7.5, 0.8004737685031, 0.1995262314969, 0.2230819620627, 476.6666666667, 5.281705566611, 0.06301658189761, 1183.333333333, 0.1049240650157, 1839.957714014, 4.958551697026e-05, 1, 0, 440, 0, 210, 0, 300, 0, 700, 0, 0, 0}},
      {"loud.bus.0", {0.01343827163625}},
      {"targets.bus.1", {25.83333333333, 0.144612320007, 0.855387679993, 0.4889584883658, 1780, 2.920604352025, 0.04006143120025, 1600, 0.0905509054245, 3667.391663984, 0.004604400666752, 748, 0.005511682490302, 440, 0, 715.08, 0.199465066279, 564, 0.03471309438526, 700, 0.130671120318, 0.1175094924982, 0}},
      {"loud.bus.1", {0.09530770485458}},
      {"targets.bus.2", {2.083333333333, 1, 0, 0.0265921221387, 240, 6, 0.07, 1100, 0, 1700, 0, 17, 0, 440, 0, 219, 0.00102334613375, 306, 1.793031734776e-05, 700, 0, 0, 0.1158693461011}},
      {"loud.bus.2", {0.0002950294745036}},
      {"targets.bus.3", {19.16666666667, 0, 1, 0.477550461417, 1841.111111111, 2.4, 0.035, 1572.222222222, 0.1101942235325, 4000, 0.006018179316417, 1632, 0.00656152677417, 440, 0, 1326, 0, 876, 0.165245804677, 700, 0.4355704010601, 0, 0}},
      {"loud.bus.3", {0.07537540705701}},
      {"gain.tractorSlow", {0.8021806488833, 0.4989201980168, 0, 0.004477974004808, 0.04421171487323, 0.1387821538394, 0.05028547123145, 0.2708343590177, 0, 0.05679228892125, 0.1326901174534, 0}},
      {"waves.tractorSlow", {751, 751, 3.607349438959, 3.889945290361, 0.3612043675351, 0.2969021492306}},
      {"targets.tractorSlow.0", {7.5, 0.748811356849, 0.251188643151, 0.5322963777057, 650.7142857143, 10.19144176931, 0.1399049085479, 1035.714285714, 0.5081858588371, 3000, 0, 1, 0, 1, 0, 180, 0, 320, 0, 600, 0, 0, 0}},
      {"loud.tractorSlow.0", {0.02032136550481}},
      {"targets.tractorSlow.1", {22.87037037037, 0.1253103408454, 0.8746896591546, 0.9703538633666, 2720, 5.702234454087, 0.09002482726763, 1500, 0.4789633900961, 3000, 0, 1276, 0.004119736084423, 165, 0.04421171487323, 861.4, 0.1619902414747, 584, 0.02704240897336, 600, 0.08125030770532, 0.05679228892125, 0}},
      {"loud.tractorSlow.1", {0.110101642083}},
      {"targets.tractorSlow.2", {2.083333333333, 1, 0, 0.05264310508297, 450, 12, 0.16, 1000, 0, 3000, 0, 29, 0, 3.75, 0, 192.5, 0.0008310833090265, 326, 1.396818645318e-05, 600, 0, 0, 0.1326901174534}},
      {"loud.tractorSlow.2", {0.0003321813601948}},
      {"targets.tractorSlow.3", {19.16666666667, 0, 1, 0.9814351232626, 2900, 4.8, 0.08, 1500, 0.5381210707181, 3000, 0, 2784, 0.004477974004808, 360, 0.04421171487323, 1696.5, 0, 896, 0.1287308063525, 600, 0.2708343590177, 0, 0}},
      {"loud.tractorSlow.3", {0.104073474702}},
      {"gain.tractor", {0.4871266929528, 0.3329522542365, 0.006151621592128, 0.004469152583683, 0.06062014691478, 0.1527957010011, 0.05028547123145, 0.2708343590177, 0, 0.05510118542234, 0.1326901174534, 0}},
      {"waves.tractor", {751, 751, 4.228950592089, 4.201162204904, 0.550732565224, 0.4137054072732}},
      {"targets.tractor.0", {7.5, 0.748811356849, 0.251188643151, 0.3035307309157, 640, 7.643581326985, 0.1049286814109, 1135.714285714, 0.3391356532437, 2084.066944594, 1.83361094234e-05, 1, 0, 1, 0, 180, 0, 320, 0, 600, 0, 0, 0}},
      {"loud.tractor.0", {0.02030043553764}},
      {"targets.tractor.1", {22.87037037037, 0.1253103408454, 0.8746896591546, 0.6161391995442, 2900, 4.276675840565, 0.06751862045072, 1600, 0.319634164067, 4138.469199982, 0.004706494949915, 1012, 0.004111620376988, 132, 0.06062014691478, 783.52, 0.1783472284924, 584, 0.02704240897336, 600, 0.08125030770532, 0.05510118542234, 0}},
      {"loud.tractor.1", {0.117899284407}},
      {"targets.tractor.2", {2.083333333333, 1, 0, 0.02930371512294, 420, 9, 0.12, 1100, 0, 2000, 0, 23, 0, 3, 0, 191, 0.0009150020609994, 326, 1.396818645318e-05, 600, 0, 0, 0.1326901174534}},
      {"loud.tractor.2", {0.0003407418110935}},
      {"targets.tractor.3", {19.16666666667, 0, 1, 0.6226213641087, 3100, 3.6, 0.06, 1600, 0.3591127884979, 4500, 0.006151621592128, 2208, 0.004469152583683, 288, 0.06062014691478, 1521, 0, 896, 0.1287308063525, 600, 0.2708343590177, 0, 0}},
      {"loud.tractor.3", {0.1006925735593}},
      {"gain.tram", {0.07375150539045, 0.03402626544823, 0, 0.007871136539919, 0, 0.2942703808973, 0.03884922904429, 0, 0.3412272805556, 0, 0, 0}},
      {"waves.tram", {52, 57, 1.978620549664, 2.598844604567, 0.5242197445604, 0.3274131277735}},
      {"targets.tram.0", {0.5, 0.9, 0.1, 0.002323172419799, 1050, 3, 0.04, 650, 0.03402626544823, 3000, 0, 1150, 0.0007871136539919, 440, 0, 180, 0, 350, 0, 2900, 0, 0, 0}},
      {"loud.tram.0", {1.030815768955e-06}},
      {"targets.tram.1", {75.9, 0.2, 0.8, 0.07153896022874, 4300, 3, 0.04, 650, 0.03402626544823, 3000, 0, 1150, 0, 440, 0, 1043.12, 0.3434802583882, 614, 0.02089225206382, 2900, 0.1706136402778, 0, 0}},
      {"loud.tram.1", {0.03299471294334}},
      {"targets.tram.2", {1.725, 1, 0, 6.650630099521e-05, 950, 3, 0.04, 650, 0, 3000, 0, 1150, 0, 440, 0, 196, 0.001762209298089, 356, 1.07914525123e-05, 2900, 0.15355227625, 0, 0}},
      {"loud.tram.2", {6.638221393377e-07}},
      {"targets.tram.3", {165.6, 0, 1, 0.08481423119902, 7200, 3, 0.04, 650, 0.03402626544823, 3000, 0, 1150, 0, 440, 0, 2106, 0, 926, 0.09945402635339, 2900, 0.3412272805556, 0, 0}},
      {"loud.tram.3", {0.03590218905046}},
      {"gain.plane", {0.5754647420104, 0.2487222666014, 0, 0, 0, 0.09196194503316, 0.1181847300733, 0.2302371072362, 0, 0, 0.1057622596902, 0.03670085208769}},
      {"waves.plane", {61, 861, 0.9999999767169, 3.781114144821, 0.3031456443268, 0.28751601187}},
      {"targets.plane.0", {7.5, 0.1333333333333, 0.505, 0.5754647420104, 1229.268292683, 5.64, 0.076, 417.0731707317, 0.1251497648777, 3000, 0, 1650, 0, 440, 0, 250, 0, 350, 0, 1100, 0, 0, 0}},
      {"loud.plane.0", {0.01500406945588}},
      {"targets.plane.1", {25.83333333333, 1.581893004115, 0.89, 0.5754647420104, 4300, 3.12, 0.048, 900, 0.3375343149683, 3000, 0, 1650, 0, 440, 0, 944, 0.1073404416223, 570, 0.06355712150609, 1100, 0.06907113217087, 0, 0}},
      {"loud.plane.1", {0.1085502555922}},
      {"targets.plane.2", {2.083333333333, 0.01028806584362, 0, 0.5754647420104, 800, 6, 0.08, 350, 0.09948890664055, 3000, 0, 1650, 0, 440, 0, 262.5, 0.0005507050832423, 355, 3.282909168703e-05, 1100, 0, 0, 0.1057622596902}},
      {"loud.plane.2", {1.790770952166e-05}},
      {"targets.plane.3", {19.16666666667, 0.8707818930041, 1, 0.5754647420104, 3973.170731707, 2.4, 0.04, 792.6829268293, 0.2942202421992, 3000, 0, 1650, 0.03670085208769, 440, 0, 1787.5, 0, 830, 0.3025529089877, 1100, 0.2302371072362, 0, 0}},
      {"loud.plane.3", {0.06753148109586}},
      {"gain.onFoot", {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
      {"targets.onFoot.0", {10, 0, 0, 0, 1000, 0, 0, 1000, 0, 3000, 0, 440, 0, 440, 0, 500, 0, 500, 0, 1000, 0, 0, 0}},
      {"loud.onFoot.0", {0}},
      {"targets.onFoot.1", {10, 0, 0, 0, 1000, 0, 0, 1000, 0, 3000, 0, 440, 0, 440, 0, 500, 0, 500, 0, 1000, 0, 0, 0}},
      {"loud.onFoot.1", {0}},
      {"targets.onFoot.2", {10, 0, 0, 0, 1000, 0, 0, 1000, 0, 3000, 0, 440, 0, 440, 0, 500, 0, 500, 0, 1000, 0, 0, 0}},
      {"loud.onFoot.2", {0}},
      {"targets.onFoot.3", {10, 0, 0, 0, 1000, 0, 0, 1000, 0, 3000, 0, 440, 0, 440, 0, 500, 0, 500, 0, 1000, 0, 0, 0}},
      {"loud.onFoot.3", {0}},
      {"soundRpm", {1894.722222222, 61.7}},
  };
  return all;
}
const std::vector<double>& ref(const std::string& name) {
  for (const Ref& r : refs())
    if (name == r.name) return r.v;
  check(false, "no numbers of the original for " + name);
  std::abort();
}
/** as the original has it, to so many parts in one */
void same(const std::string& name, const std::vector<double>& got, double tolerance = 1e-8) {
  const std::vector<double>& want = ref(name);
  check(got.size() == want.size(), name + ": as many numbers as the original's");
  for (size_t i = 0; i < got.size(); i++) {
    const double scale = std::max(std::abs(want[i]), 1e-6);
    if (std::abs(got[i] - want[i]) <= tolerance * scale) continue;
    std::fprintf(stderr, "audio_test: %s[%zu]: %.15g here, %.15g in the original\n", name.c_str(), i, got[i], want[i]);
    std::abort();
  }
}

void numbersOfTheOriginal() {
  {
    Seeded r = seeded(12345);
    const double a = r();
    const double b = r();
    const double c = r();
    same("seeded", {a, b, c}, 1e-12);
  }
  same("aWeight", {aWeight(50), aWeight(1000), aWeight(8000)});
  same("filters", {lowpassGain(700, 1000, 2), highpassGain(700, 1000, -3), bandpassGain(700, 1000, 1.5), peakingGain(700, 1000, 1.2, 4)});
  same("noiseLoudness", {noiseLoudness([](double f) { return bandpassGain(f, 3600, 0.6); }), noiseLoudness([](double f) { return lowpassGain(f, 900, 1); }, 44100)});
  same("oscLoudness", {oscLoudness("sine", 440), oscLoudness("square", 440), oscLoudness("sawtooth", 110, [](double f) { return lowpassGain(f, 2400, 0); }), oscLoudness("triangle", 1650)});
  {
    const Partials w = wavePartials(WaveSpec{.kind = WaveKind::Pulses, .firing = {0, 180, 360, 540}, .rise = 6, .decay = 30, .spread = 0.1, .skew = 2}, 720, 512, 7);
    double s = 0, s2 = 0;
    for (size_t k = 0; k < w.real.size(); k++) {
      s += static_cast<double>(w.real[k]) * static_cast<double>(k % 7 + 1);
      s2 += static_cast<double>(w.imag[k]) * static_cast<double>(k % 5 + 1);
    }
    // (single-precision numbers: one of them rounded the other way shows in the seventh place)
    same("wave", {static_cast<double>(w.real.size()), w.real[1], w.imag[1], w.real[4], w.imag[4], w.real[511], s, s2, peakOf(w), rmsOf(w)}, 2e-6);
  }
  const RpmScale truck{600, 2400};
  for (SoundKind k : SOUND_KINDS) {
    const std::string name = soundKindName(k);
    const Voicing& v = voicing(k);
    same("gain." + name, gainsOf(v));
    if (v.soft && v.hard) {
      double s = 0, h = 0;
      for (size_t i = 0; i < v.soft->real.size(); i++) s += std::abs(static_cast<double>(v.soft->real[i])) + 2 * std::abs(static_cast<double>(v.soft->imag[i]));
      for (size_t i = 0; i < v.hard->real.size(); i++) h += std::abs(static_cast<double>(v.hard->real[i])) + 2 * std::abs(static_cast<double>(v.hard->imag[i]));
      same("waves." + name, {static_cast<double>(v.soft->real.size()), static_cast<double>(v.hard->real.size()), s, h, rmsOf(*v.soft), rmsOf(*v.hard)}, 2e-6);
    }
    const Over states[4] = {
        {.rpm = 900, .load = 0.1, .speed = 0},
        {.rpm = 3100, .load = 0.8, .speed = 22, .skid = 0.3, .horn = true, .rough = 2.2, .braking = 0.4, .curve = 0.03},
        {.rpm = 250, .load = 0, .engineRunning = false, .starter = true, .speed = 0.5, .braking = 0.9},
        {.rpm = 2300, .load = 1, .speed = 48, .skid = 1, .rough = 3, .airborne = true, .stallWarning = true},
    };
    for (int i = 0; i < 4; i++) {
      const SoundTargets t = targets(v, states[i], i == 1 ? &truck : nullptr);
      std::vector<double> got;
      for (const Field& f : FIELDS) got.push_back(t.*f.at);
      same("targets." + name + "." + std::to_string(i), got);
      same("loud." + name + "." + std::to_string(i), {mainLoudness(v, t)});
    }
  }
  same("soundRpm", {soundRpm(PROFILES[SoundKind::Minibus], 1234, &truck), firingHz(PROFILES[SoundKind::Truck], 1234)});
}

// ---------------------------------------------------------------- on the device (the native host)

void frames(GameAudio& a, int n, const std::function<AudioState(int)>& s) {
  for (int i = 0; i < n; i++) {
    plat::native::advance(1000.0 / 60);
    a.update(s(i));
  }
}

int oscs(const AudioContext& ctx) { return audioNative::made(ctx, "osc"); }

/** the gain of the vehicle's hiss voice: the one the band-pass set up at 3600 Hz feeds */
const audioNative::NodeInfo* hissLevelOf(const AudioContext& ctx) {
  const std::vector<const audioNative::NodeInfo*> all = audioNative::nodes(ctx);
  for (const audioNative::NodeInfo* n : all) {
    if (n->kind != "biquad") continue;
    const audioNative::ParamInfo& f = n->param("frequency");
    const audioNative::ParamInfo& q = n->param("Q");
    if (f.events.empty() || q.events.empty() || f.events[0].what != "value" || f.events[0].value != 3600 || q.events[0].value != 0.6 || n->outputs.size() != 1) continue;
    for (const audioNative::NodeInfo* g : all)
      if (g->id == n->outputs[0]) return g;
  }
  check(false, "the hiss voice is there");
  return nullptr;
}
/** how loud the hisses so far were (dB against the reference: what hiss() was called with) */
std::vector<double> hissesOf(const AudioContext& ctx) {
  const double hissUnit = REF_LOUDNESS / noiseLoudness([](double f) { return bandpassGain(f, 3600, 0.6); }, ctx.sampleRate());
  std::vector<double> out;
  for (const audioNative::ParamEvent& e : hissLevelOf(ctx)->param("gain").events)
    if (e.what == "linear") out.push_back(20 * std::log10(e.value / hissUnit));
  return out;
}

/**
 * The same drive as the original's GameAudio was given on its stand-in for the Web Audio API (node,
 * v1.7.7, Math.random and the clock as here): the same nodes made, the same number of settings sent.
 */
void asTheOriginalOnItsDevice() {
  plat::native::manualClock(true);
  rndSeed(1);
  GameAudio a;
  a.setVehicle(SoundKind::Car);
  a.start();
  const AudioContext ctx = a.context();
  assert(ctx && ctx.currentTime() == 0 && ctx.sampleRate() == 48000);
  using audioNative::created;
  using audioNative::made;
  using audioNative::paramCalls;
  // start 44 22 10 9 2 1 0 0 29 2
  assert(created(ctx) == 44 && made(ctx, "gain") == 22 && made(ctx, "biquad") == 10 && made(ctx, "osc") == 9 && made(ctx, "source") == 2 && made(ctx, "compressor") == 1);
  assert(made(ctx, "delay") == 0 && made(ctx, "panner") == 0 && paramCalls(ctx) == 29 && audioNative::waves(ctx) == 2);
  // (settings sent as the vehicle is set up, in 240 frames of driving, nodes made in them, settings sent in a second of standing still)
  struct Drive {
    SoundKind kind;
    long apply, drive;
  };
  const Drive drives[] = {
      {SoundKind::Car, 29, 3795},        {SoundKind::Moped, 29, 3080},   {SoundKind::Motorcycle125, 29, 3406}, {SoundKind::Motorcycle35, 29, 3446}, {SoundKind::Motorcycle, 29, 3456},
      {SoundKind::Minibus, 29, 3652},    {SoundKind::LightTruck, 29, 3758}, {SoundKind::Truck, 29, 3814},      {SoundKind::Semi, 29, 3837},         {SoundKind::Bus, 29, 3784},
      {SoundKind::TractorSlow, 29, 3585}, {SoundKind::Tractor, 29, 3991}, {SoundKind::Tram, 29, 2386},         {SoundKind::Plane, 29, 2706},        {SoundKind::OnFoot, 29, 24},
  };
  const RpmScale truck{600, 2400};
  for (const Drive& d : drives) {
    const std::string name = soundKindName(d.kind);
    const long c00 = paramCalls(ctx);
    a.setVehicle(d.kind, d.kind == SoundKind::Minibus ? &truck : nullptr);
    const int made0 = created(ctx);
    const long calls0 = paramCalls(ctx);
    frames(a, 240, [&](int i) { return state({.rpm = 900 + 20 * i, .load = (i % 60) / 60.0, .speed = i / 10.0, .rough = 1 + (i % 2), .braking = 0, .curve = d.kind == SoundKind::Tram ? 0.03 : 0}); });
    const long drive = paramCalls(ctx) - calls0;
    check(created(ctx) == made0, name + ": no nodes made in 240 frames");
    frames(a, 10, [](int) { return state({.rpm = 900, .load = 0.1, .speed = 0}); });
    const long calls1 = paramCalls(ctx);
    frames(a, 60, [](int) { return state({.rpm = 900, .load = 0.1, .speed = 0}); });
    check(calls0 - c00 == d.apply, name + ": as many settings sent to set it up as in the original");
    if (drive != d.drive) std::fprintf(stderr, "audio_test: %s: %ld settings sent in the drive, %ld in the original\n", name.c_str(), drive, d.drive);
    check(drive == d.drive, name + ": as many settings sent in the drive as in the original");
    check(paramCalls(ctx) == calls1, name + ": a steady state sends nothing");
  }
  assert(audioNative::waves(ctx) == 30);
  // every short sound: the nodes it makes, the settings it sends
  struct Click {
    ClickKind kind;
    int nodes;
    long calls;
  };
  const Click clicks[] = {
      {ClickKind::Tick, 3, 2},  {ClickKind::Tock, 3, 2}, {ClickKind::Crash, 3, 2},    {ClickKind::Grind, 3, 2},   {ClickKind::Chime, 2, 5},
      {ClickKind::Bump, 3, 2},  {ClickKind::Door, 3, 2}, {ClickKind::DoorShut, 3, 2}, {ClickKind::Step, 6, 4},    {ClickKind::Air, 0, 7},
      {ClickKind::AirShort, 0, 7}, {ClickKind::AirDoor, 3, 9}, {ClickKind::Bell, 12, 14}, {ClickKind::Clack, 12, 8}, {ClickKind::Beep, 3, 4},
  };
  a.setVehicle(SoundKind::Car);
  for (const Click& c : clicks) {
    const int made0 = created(ctx);
    const long calls0 = paramCalls(ctx);
    a.click(c.kind);
    check(created(ctx) - made0 == c.nodes && paramCalls(ctx) - calls0 == c.calls, std::string(clickKindName(c.kind)) + ": the nodes and the settings of the original");
  }
  // the tram's bell while held: three strikes in a second, none once let go
  a.setVehicle(SoundKind::Tram);
  const int osc0 = oscs(ctx);
  frames(a, 60, [](int) { return state({.speed = 3, .horn = true}); });
  const int osc1 = oscs(ctx);
  frames(a, 60, [](int) { return state({.speed = 3, .horn = false}); });
  assert(osc1 - osc0 == 12 && oscs(ctx) == osc1);
  // air brakes: letting the parking brake go, setting it, a puff after braking – and none of it on a car
  const size_t hisses0 = hissesOf(ctx).size();
  a.setVehicle(SoundKind::Truck, &truck);
  frames(a, 5, [](int) { return state({.rpm = 600, .parkingBrake = true}); });
  frames(a, 5, [](int) { return state({.rpm = 600, .parkingBrake = false}); });
  frames(a, 5, [](int) { return state({.rpm = 600, .parkingBrake = true}); });
  frames(a, 30, [](int) { return state({.rpm = 600, .speed = 5, .braking = 0.6, .parkingBrake = true}); });
  frames(a, 5, [](int) { return state({.rpm = 600, .speed = 5, .braking = 0, .parkingBrake = true}); });
  a.setVehicle(SoundKind::Car);
  frames(a, 5, [](int) { return state({.rpm = 800, .parkingBrake = false}); });
  frames(a, 5, [](int) { return state({.rpm = 800, .parkingBrake = true}); });
  frames(a, 30, [](int) { return state({.rpm = 800, .braking = 0.6}); });
  frames(a, 5, [](int) { return state({.rpm = 800, .braking = 0}); });
  {
    const std::vector<double> h = hissesOf(ctx);
    // hisses 3 -10 3 -8.4
    assert(h.size() == hisses0 + 3);
    assert(std::abs(h[hisses0] - -10) < 1e-9 && std::abs(h[hisses0 + 1] - 3) < 1e-9 && std::abs(h[hisses0 + 2] - -8.4) < 1e-9);
  }
  // the reversing alarm: three beeps in three seconds; a car does not beep
  a.setVehicle(SoundKind::Truck);
  const int osc2 = oscs(ctx);
  frames(a, 180, [](int) { return state({.rpm = 700, .reversing = true}); });
  const int osc3 = oscs(ctx);
  a.setVehicle(SoundKind::Car);
  frames(a, 120, [](int) { return state({.rpm = 900, .reversing = true}); });
  assert(osc3 - osc2 == 3 && oscs(ctx) == osc3);
  // total 148 49844 – and the three hisses, seven settings each, which the original's test keeps from sounding
  if (created(ctx) != 148 || paramCalls(ctx) != 49844 + 21) std::fprintf(stderr, "audio_test: %d nodes, %ld settings in all (148, 49865 in the original)\n", created(ctx), paramCalls(ctx));
  assert(created(ctx) == 148 && paramCalls(ctx) == 49844 + 21);
}

void nothingMadeWhileDriving() {
  // on the device: nothing is made while driving, and only what changes is sent
  GameAudio a;
  a.setVehicle(SoundKind::Car);
  assert(!a.context());   // no context before start()
  assert(!a.masterGain());
  int ready = 0;
  a.onReady([&ready](const AudioContext&, const GainNode&) { ready++; });
  const int contexts0 = audioNative::contextsMade();
  a.start();
  const AudioContext ctx = a.context();
  assert(audioNative::contextsMade() == contexts0 + 1);
  assert(ready == 1);   // onReady once the context is there
  assert(a.context() && a.masterGain());   // context and master gain for other sounds of the game
  a.onReady([&ready](const AudioContext& c, const GainNode& master) {
    assert(c && master);
    ready++;
  });
  assert(ready == 2);   // at once when it already is
  const RpmScale truck{600, 2400};
  for (SoundKind kind : SOUND_KINDS) {
    const std::string name = soundKindName(kind);
    a.setVehicle(kind, kind == SoundKind::Minibus ? &truck : nullptr);
    assert(a.vehicleSound() == kind);
    const int made = audioNative::created(ctx);
    const long calls0 = audioNative::paramCalls(ctx);
    frames(a, 240, [&](int i) { return state({.rpm = 900 + 20 * i, .load = (i % 60) / 60.0, .speed = i / 10.0, .rough = 1 + (i % 2), .braking = 0, .curve = kind == SoundKind::Tram ? 0.03 : 0}); });
    check(audioNative::created(ctx) == made, name + ": no nodes made in 240 frames");
    check((audioNative::paramCalls(ctx) - calls0) / 240.0 <= 26, name + ": few settings a frame");
    // standing still with nothing changing: nothing is sent at all
    frames(a, 10, [](int) { return state({.rpm = 900, .load = 0.1, .speed = 0}); });
    const long calls1 = audioNative::paramCalls(ctx);
    frames(a, 60, [](int) { return state({.rpm = 900, .load = 0.1, .speed = 0}); });
    check(audioNative::paramCalls(ctx) == calls1, name + ": a steady state sends nothing");
  }
  // every vehicle with an engine or motors got its waves
  assert(audioNative::waves(ctx) >= 2 * (static_cast<int>(SOUND_KINDS.size()) - 1));
  // start() again: the same context, woken (the browsers' rule: from a tap or a key)
  a.suspend();
  assert(ctx.state() == "suspended");
  const long calls = audioNative::paramCalls(ctx);
  frames(a, 10, [](int i) { return state({.rpm = 1000 + 100.0 * i, .load = 0.5}); });
  assert(audioNative::paramCalls(ctx) == calls);   // (nothing is sent to a context that stands)
  a.start();
  assert(ctx.state() == "running" && a.context() == ctx && audioNative::contextsMade() == contexts0 + 1);
}

void everyShortSoundPlays() {
  // on the device: every short sound plays
  GameAudio a;
  a.start();
  const AudioContext ctx = a.context();
  for (ClickKind k : CLICK_KINDS) {
    const int made = audioNative::created(ctx);
    const long calls = audioNative::paramCalls(ctx);
    a.click(k);
    check(audioNative::created(ctx) > made || audioNative::paramCalls(ctx) > calls, std::string(clickKindName(k)) + " sounds");
    assert(clickKindOf(clickKindName(k)) == k);
  }
  // (by its name, as the game says it – and a name that is none makes no sound)
  const int made = audioNative::created(ctx);
  a.click("doorShut");
  assert(audioNative::created(ctx) == made + 3);
  a.click("kazoo");
  assert(audioNative::created(ctx) == made + 3);
  // a short sound is let go as soon as it is set going: the host keeps nothing of it by name
  const std::vector<const audioNative::NodeInfo*> all = audioNative::nodes(ctx);
  const audioNative::NodeInfo* last = all.back();
  assert(last->kind == "gain" && last->released && last->outputs.size() == 1);
  const audioNative::NodeInfo* src = all[all.size() - 3];
  assert(src->kind == "source" && src->released && src->started >= 0 && src->buffer != 0 && std::abs(src->duration - 0.25) < 1e-12);
}

void bellHissBeep() {
  // on the device: the tram's bell rings while held; air brakes hiss; a truck beeps in reverse
  GameAudio a;
  a.start();
  const AudioContext ctx = a.context();
  a.setVehicle(SoundKind::Tram);
  const int osc0 = oscs(ctx);
  frames(a, 60, [](int) { return state({.speed = 3, .horn = true}); });
  const double strikes = (oscs(ctx) - osc0) / 4.0;
  assert(strikes >= 3 && strikes <= 4);   // strikes in a second
  const int osc1 = oscs(ctx);
  frames(a, 60, [](int) { return state({.speed = 3, .horn = false}); });
  assert(oscs(ctx) == osc1);   // quiet once let go

  // the air: a hiss when the parking brake is set – not for the one a drive begins with, not on a car
  const RpmScale truck{600, 2400};
  a.setVehicle(SoundKind::Truck, &truck);
  frames(a, 5, [](int) { return state({.rpm = 600, .parkingBrake = true}); });
  assert(hissesOf(ctx).size() == 0);   // the parking brake a drive begins with: no pssht
  frames(a, 5, [](int) { return state({.rpm = 600, .parkingBrake = false}); });
  const size_t released = hissesOf(ctx).size();
  frames(a, 5, [](int) { return state({.rpm = 600, .parkingBrake = true}); });
  assert(hissesOf(ctx).size() == released + 1);   // pssht as it is set
  assert(hissesOf(ctx).back() > hissesOf(ctx)[0]);   // louder than letting it go
  // braking, then letting go: a puff
  frames(a, 30, [](int) { return state({.rpm = 600, .speed = 5, .braking = 0.6, .parkingBrake = true}); });
  const size_t before = hissesOf(ctx).size();
  frames(a, 5, [](int) { return state({.rpm = 600, .speed = 5, .braking = 0, .parkingBrake = true}); });
  assert(hissesOf(ctx).size() == before + 1);   // a puff as the brake is let go
  a.setVehicle(SoundKind::Car);
  frames(a, 5, [](int) { return state({.rpm = 800, .parkingBrake = false}); });
  frames(a, 5, [](int) { return state({.rpm = 800, .parkingBrake = true}); });
  frames(a, 30, [](int) { return state({.rpm = 800, .braking = 0.6}); });
  frames(a, 5, [](int) { return state({.rpm = 800, .braking = 0}); });
  assert(hissesOf(ctx).size() == before + 1);   // a car has no air brakes

  // the reversing alarm: a beep a second
  a.setVehicle(SoundKind::Truck);
  const int osc2 = oscs(ctx);
  frames(a, 180, [](int) { return state({.rpm = 700, .reversing = true}); });
  assert(oscs(ctx) - osc2 == 3);   // three beeps in three seconds
  a.setVehicle(SoundKind::Car);
  const int osc3 = oscs(ctx);
  frames(a, 120, [](int) { return state({.rpm = 900, .reversing = true}); });
  assert(oscs(ctx) == osc3);   // a car does not beep
}

void musicOffUntilAskedFor() {
  // on the device: the music is off until asked for – then it is there, and gone again when switched off
  GameAudio a;
  a.setMusic(MusicMode::Quiet, 0.8);
  a.setMusic(MusicMode::Off);
  a.start();
  const AudioContext ctx = a.context();
  const int made = audioNative::created(ctx);
  a.setMusic(MusicMode::Off);
  assert(audioNative::created(ctx) == made);   // off: nothing made
  a.setMusic("on", 0.5);   // (as the settings keep it)
  assert(audioNative::created(ctx) == made + 29);   // on: its nodes are made
  a.setMusic(MusicMode::Off);
  // (asked for before the sound has started: it is there as the sound starts)
  GameAudio b;
  b.setMusic("quiet");
  b.start();
  assert(audioNative::created(b.context()) == made + 29);
  b.setMusic("off");
}

// ---------------------------------------------------------------- the choice of the voice (tests/audio/voicepick.test.ts)

VoiceLike voice(const std::string& name, const std::string& lang, bool localService = true, bool isDefault = false) {
  return VoiceLike{name, lang, "uri:" + name, localService, isDefault};
}

void theChoiceOfTheVoice() {
  // a mix of what browsers list
  const std::vector<VoiceLike> VOICES = {
      voice("Microsoft David - English (United States)", "en-US", true, true),
      voice("Google US English", "en-US", false),
      voice("Google UK English Female", "en-GB", false),
      voice("Microsoft Hedda - German (Germany)", "de-DE"),
      voice("Microsoft Katja Online (Natural) - German (Germany)", "de-DE", false),
      voice("Microsoft Ingrid Online (Natural) - German (Austria)", "de-AT", false),
      voice("Anna", "de-DE"),
      voice("Bubbles", "de-DE"),
      voice("Google português do Brasil", "pt-BR", false),
      voice("Joana", "pt_PT"),
      voice("Google 粤語（香港）", "zh-HK", false),
      voice("Google 普通话（中国大陆）", "zh-CN", false),
      voice("Google 國語（臺灣）", "zh-TW", false),
      voice("eSpeak Hindi", "hi"),
      voice("Google हिन्दी", "hi-IN", false),
      voice("Microsoft Gabrijela Online (Natural) - Croatian (Croatia)", "sr-Latn-RS", false),
      voice("Damayanti", "in-ID"),
  };
  const std::string katja = "Microsoft Katja Online (Natural) - German (Germany)";

  // language tags of every browser are understood
  assert((parseTag("pt_PT") == Tag{"pt", "pt", ""}));
  assert((parseTag("sr-Latn-RS") == Tag{"sr", "rs", "latn"}));
  assert(parseTag("in-ID").lang == "id");
  assert(parseTag("cmn-Hans-CN").lang == "zh");
  assert(parseTag("zh-Hant").region == "tw");

  // the best voice of the locale wins: natural/online first, the right region before another
  assert(rankVoices(VOICES, "de-DE")[0].name == katja);
  // (Austria after Germany, even as a natural voice; the toy voice last)
  std::vector<std::string> de;
  for (const VoiceLike& x : rankVoices(VOICES, "de-DE")) de.push_back(x.name);
  auto indexOf = [&de](const std::string& name) { return std::find(de.begin(), de.end(), name) - de.begin(); };
  assert(indexOf("Microsoft Hedda - German (Germany)") < indexOf("Microsoft Ingrid Online (Natural) - German (Austria)"));
  assert(de.back() == "Bubbles");
  assert(rankVoices(VOICES, "en-GB")[0].name == "Google UK English Female");
  assert(rankVoices(VOICES, "pt-PT")[0].name == "Joana");
  assert(rankVoices(VOICES, "hi-IN")[0].name == "Google हिन्दी");
  assert(rankVoices(VOICES, "zh-CN")[0].name == "Google 普通话（中国大陆）");
  assert(rankVoices(VOICES, "id-ID")[0].name == "Damayanti");

  // never a voice of another language; Cantonese never reads Mandarin
  assert(!voiceFit(voice("Google US English", "en-US"), "sq-AL"));
  assert(rankVoices(VOICES, "sq-AL").empty());
  assert(rankVoices(VOICES, "bn-BD").empty());
  for (const VoiceLike& x : rankVoices(VOICES, "zh-CN")) assert(x.lang != "zh-HK");
  assert(voiceFit(voice("Ting-Ting", "zh-TW"), "zh-CN") == VoiceFit::Language);
  assert(!voiceFit(voice("x", "yue-HK"), "zh-CN"));

  // what the settings say about a language
  assert(voiceStatus(VOICES, "de-DE") == VoiceStatus::Good);
  assert(voiceStatus(VOICES, "ur-PK") == VoiceStatus::None);
  assert(voiceStatus(VOICES, "hr-HR") == VoiceStatus::Related);
  assert(voiceStatus(std::vector<VoiceLike>(), "de-DE") == VoiceStatus::Unknown);

  // the voice the player chose – if it is there, reads the language and has not failed
  assert(chooseVoice(VOICES, "de-DE", "uri:Anna")->name == "Anna");
  assert(chooseVoice(VOICES, "de-DE", "Anna")->name == "Anna");
  assert(chooseVoice(VOICES, "de-DE", "uri:Google US English")->name == katja);
  assert(chooseVoice(VOICES, "de-DE", "uri:gone")->name == katja);
  // (offline: the network voice failed – the next one)
  const std::unordered_set<std::string> failed = {"uri:" + katja};
  assert(chooseVoice(VOICES, "de-DE", "", failed)->name != katja);
  assert(!chooseVoice(VOICES, "sq-AL"));

  // the stored choices are checked
  Json none;
  assert(cleanSpeechPrefs(none).empty());
  Json stored;
  assert(Json::parse(R"({ "de": { "voice": "uri:Anna", "rate": 1.2, "pitch": 0.9 }, "xx1": {}, "en": { "voice": 3, "rate": 9, "pitch": "a" } })", stored));
  const SpeechPrefs clean = cleanSpeechPrefs(stored);
  assert(clean.size() == 2);
  assert((clean.at("de") == SpeechPref{"uri:Anna", 1.2, 0.9}));
  assert((clean.at("en") == SpeechPref{"", 1, 1}));
  assert((prefOf(nullptr, "de") == SpeechPref{"", 1, 1}));
  const SpeechPrefs mine = {{"de", SpeechPref{"a", 1.4, 1}}};
  assert(prefOf(mine, "de").rate == 1.4);
  assert((prefOf(mine, "fr") == DEFAULT_PREF));

  // (the points themselves, as the original gives them: the locale's natural voice over the network, a toy voice, eSpeak, the default)
  assert(voiceScore(VOICES[4], "de-DE") == 140 && voiceScore(VOICES[7], "de-DE") == 20 && voiceScore(VOICES[13], "hi-IN") == 35 && voiceScore(VOICES[0], "en-US") == 105);
  assert(voiceScore(VOICES[1], "en-US") == 125 && voiceScore(VOICES[5], "de-DE") == 90 && voiceScore(VOICES[15], "hr-HR") == 60 && voiceScore(VOICES[0], "de-DE") == -INF);
  assert(voiceScore(VoiceLike{"Samantha", "en-US", "com.apple.voice.compact.en-US.Samantha-network", true, false}, "en-US") == 110);
  assert(voiceScore(voice("Eddy (German (Germany))", "de-DE"), "de-DE") == 90 && voiceScore(voice("Organ", "en-US"), "en-US") == 20 && voiceScore(voice("Organist", "en-US"), "en-US") == 100);
  assert(voiceScore(voice("default", "en-US"), "en-US") == 85 && voiceScore(voice("Voice Online", "en-US"), "en-US") == 130 && voiceScore(voice("Onliner", "en-US"), "en-US") == 100);
}

// ---------------------------------------------------------------- the horns of the emergency vehicles

struct Emergency {
  bool call = true;
  double pitch = 1, phase = 0;
};
struct Car {
  int id = 0;
  double x = 0, z = 0, yaw = 0, v = 0;
  std::optional<Emergency> em = std::nullopt;
};
struct Ear {
  struct {
    double x = 0, y = 0, z = 0;
  } position;
  struct {
    std::array<double, 16> elements = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  } matrixWorld;
};

void theHorn() {
  // the horn: two tones a fourth apart in turn, higher as it comes nearer, quieter far away (tests/sim/emergency.test.ts)
  assert(sirenTone(0.1) == SIREN_HIGH);
  assert(sirenTone(0.1 + SIREN_TONE) == SIREN_LOW);
  assert(sirenTone(0.1 + 2 * SIREN_TONE) == SIREN_HIGH);   // round and round
  assert(std::abs(SIREN_HIGH / SIREN_LOW - 4.0 / 3) < 0.002);   // a pure fourth
  assert(doppler(20, 0) > 1.05 && doppler(-20, 0) < 0.95);   // higher coming, lower going
  assert(std::abs(doppler(0, 0) - 1) < 1e-9);
  assert(doppler(0, 15) > 1);   // driving towards it, too
  assert(sirenLevel(5) == 1);
  assert(std::abs(sirenLevel(48) - 0.25) < 1e-9);   // a quarter at four times the distance

  // … and heard in the game's sound: the nearest three of those on a call, each from its side
  GameAudio a;
  Sirens sirens;
  Ear ear;
  std::vector<Car> cars = {
      Car{1, 0, -100, 0, 20, Emergency{true, 1, 0}},       // ahead (to the north), coming towards the listener
      Car{2, 60, 0, 0, 0, Emergency{false, 1, 0}},         // a police car on patrol: silent
      Car{3, 300, 0, 0, 0, std::nullopt},                  // no emergency vehicle at all
      Car{4, 40, 0, 0, 0, Emergency{true, 1.02, 0.3}},     // to the right, standing
      Car{5, -200, 0, 0, 0, Emergency{true, 1, 0}},
      Car{6, 0, 900, 0, 0, Emergency{true, 1, 0}},         // the fourth nearest: not heard
      Car{7, 0, 5000, 0, 0, Emergency{true, 1, 0}},        // out of hearing
  };
  sirens.update(a.context(), a.masterGain(), cars, ear, false);   // (before the sound has started: nothing)
  a.start();
  const AudioContext ctx = a.context();
  const int made = audioNative::created(ctx);
  a.suspend();
  sirens.update(ctx, a.masterGain(), cars, ear, false);
  assert(audioNative::created(ctx) == made);   // nothing while the sound stands
  a.start();
  plat::native::advance(100);
  sirens.update(ctx, a.masterGain(), cars, ear, false, 0, 0);
  // three horns, each two oscillators with their gains, a mix, two filters, its level and its side
  assert(audioNative::created(ctx) == made + 3 * 9);
  const std::vector<const audioNative::NodeInfo*> all = audioNative::nodes(ctx);
  std::vector<const audioNative::NodeInfo*> horns, pans, levels;
  for (size_t i = all.size() - 27; i < all.size(); i++) {
    if (all[i]->kind == "osc") horns.push_back(all[i]);
    if (all[i]->kind == "panner") pans.push_back(all[i]);
  }
  assert(horns.size() == 6 && pans.size() == 3);
  for (const audioNative::NodeInfo* h : horns) assert(h->started == 0 && !h->released);
  // the nearest first: the one to the right (40 m), then ahead (100 m), then to the left (200 m)
  const double now = ctx.currentTime();
  {
    // standing, to the right: its own pitch and place in the rhythm, no Doppler effect; the second horn tuned a little apart
    const audioNative::ParamEvent* f = horns[0]->param("frequency").last("target");
    assert(f && std::abs(f->value - sirenTone(now + 0.3, 1.02)) < 1e-9 && f->time == now);
    assert(horns[0]->type == "sawtooth" && horns[1]->type == "square");
    assert(std::abs(horns[1]->param("frequency").last("target")->value - f->value * 1.007) < 1e-9);
    assert(std::abs(pans[0]->param("pan").last("target")->value - 0.8) < 1e-12);   // from the right
  }
  {
    // coming towards the listener at 20 m/s (yaw 0 looks along −z … the car at z = −100 drives to +z? no: −sin, −cos: along −z, away)
    const audioNative::ParamEvent* f = horns[2]->param("frequency").last("target");
    const double source = (-std::sin(0.0) * 0 - std::cos(0.0) * 1) * 20;
    assert(f && std::abs(f->value - sirenTone(now, 1) * doppler(source, 0)) < 1e-9);
    assert(std::abs(pans[1]->param("pan").last("target")->value) < 1e-12);   // straight ahead
  }
  assert(std::abs(pans[2]->param("pan").last("target")->value - -0.8) < 1e-12);   // from the left
  // louder the nearer: the levels of the three
  for (size_t i = all.size() - 27; i < all.size(); i++)
    if (all[i]->kind == "gain" && all[i]->param("gain").count("target") > 0) levels.push_back(all[i]);
  assert(levels.size() == 3);
  assert(std::abs(levels[0]->param("gain").last("target")->value - sirenLevel(40) * 0.22) < 1e-12);
  assert(std::abs(levels[1]->param("gain").last("target")->value - sirenLevel(100) * 0.22) < 1e-12);
  // in a closed cab: quieter and duller
  plat::native::advance(16);
  sirens.update(ctx, a.masterGain(), cars, ear, true, 0, 0);
  assert(audioNative::created(ctx) == made + 27);
  assert(std::abs(levels[0]->param("gain").last("target")->value - sirenLevel(40) * 0.12) < 1e-12);
  // the calls are over: the horns fall silent, and after a while they are taken down
  for (Car& c : cars) c.em.reset();
  for (int i = 0; i < 60; i++) {
    plat::native::advance(50);
    sirens.update(ctx, a.masterGain(), cars, ear, true);
  }
  for (const audioNative::NodeInfo* h : horns) assert(h->stopped >= 0 && h->outputs.empty() && h->released);
  for (const audioNative::NodeInfo* p : pans) assert(p->outputs.empty() && p->released);
  // … or at once, when the drive is over
  cars[0].em = Emergency{true, 1, 0};
  plat::native::advance(16);
  sirens.update(ctx, a.masterGain(), cars, ear, false);
  assert(audioNative::created(ctx) == made + 36);
  sirens.stop();
  const std::vector<const audioNative::NodeInfo*> after = audioNative::nodes(ctx);
  for (size_t i = after.size() - 9; i < after.size(); i++) assert(after[i]->released);
  // (the cars as the traffic may keep them: by pointers)
  std::vector<const Car*> byPointer = {&cars[0]};
  sirens.update(ctx, a.masterGain(), byPointer, ear, false);
  assert(audioNative::created(ctx) == made + 45);
  sirens.stop();
}

// ---------------------------------------------------------------- the bell of a level crossing, a march, a bang

void bellCrowdAndBang() {
  GameAudio a;
  CrossingBell bell(a);
  CrowdNoise crowd(a);
  bell.update(20);   // (before the sound has started: nothing, and no harm)
  crowd.update(1.0 / 60, 20, 100, true);
  a.start();
  const AudioContext ctx = a.context();
  const int ended0 = audioDetail::endedCount();

  // the bell: 3.2 strokes a second while it is within hearing, each two partials
  int made = audioNative::created(ctx);
  for (int i = 0; i < 120; i++) {
    plat::native::advance(1000.0 / 60);
    bell.update(30);
  }
  const int strokes = (audioNative::made(ctx, "osc") - 9) / 2;
  assert(strokes >= 6 && strokes <= 8);   // two seconds (and a quarter ahead)
  assert(audioNative::created(ctx) == made + strokes * 5);
  {
    const std::vector<const audioNative::NodeInfo*> all = audioNative::nodes(ctx);
    const audioNative::NodeInfo* g = all[all.size() - 5];
    const audioNative::ParamInfo& env = g->param("gain");
    assert(g->kind == "gain" && env.count("linear") == 1 && env.count("exp") == 1);
    assert(std::abs(env.last("linear")->value - 0.11 * (14.0 / 30)) < 1e-12);   // quieter at a distance
    assert(all[all.size() - 4]->kind == "osc" && all[all.size() - 4]->param("frequency").value == 1480 && all[all.size() - 2]->param("frequency").value == 3960);
  }
  made = audioNative::created(ctx);
  for (int i = 0; i < 60; i++) {
    plat::native::advance(1000.0 / 60);
    bell.update(500);
    bell.update(INF);
  }
  assert(audioNative::created(ctx) == made);   // out of hearing: none
  // each stroke takes itself apart when it has rung out
  assert(audioDetail::endedCount() == ended0 + strokes);
  plat::native::drain();
  assert(audioDetail::endedCount() == ended0);
  {
    // (the first nodes of the context are the loudspeakers and the 44 of the vehicle's voices)
    const std::vector<const audioNative::NodeInfo*> all = audioNative::nodes(ctx);
    for (size_t i = 45; i < all.size(); i++)
      if (all[i]->kind == "osc") assert(all[i]->outputs.empty() && all[i]->released);
  }

  // the march: a murmur while a crowd is within hearing, whistles now and then; gone a while after the crowd is
  rndSeed(3);
  made = audioNative::created(ctx);
  const int sources0 = audioNative::made(ctx, "source");
  const int buffers0 = audioNative::buffers(ctx);
  crowd.update(1.0 / 60, 5000, 100, true);
  assert(audioNative::created(ctx) == made);   // far away: nothing
  int whistled = 0;
  for (int i = 0; i < 600; i++) {
    plat::native::advance(1000.0 / 60);
    const int osc = audioNative::made(ctx, "osc");
    crowd.update(1.0 / 60, 30, 100, true);
    if (audioNative::made(ctx, "osc") > osc) whistled++;
  }
  assert(audioNative::made(ctx, "source") == sources0 + 1 && audioNative::buffers(ctx) == buffers0 + 1);   // one murmur
  assert(whistled >= 3 && whistled <= 20);   // about one a second from a hundred on the march
  const audioNative::NodeInfo* murmur = nullptr;
  {
    const std::vector<const audioNative::NodeInfo*> all = audioNative::nodes(ctx);
    for (size_t i = 45; i < all.size(); i++)
      if (all[i]->kind == "source" && all[i]->loop) murmur = all[i];
  }
  assert(murmur && murmur->stopped < 0 && !murmur->released);
  for (int i = 0; i < 240; i++) {
    plat::native::advance(1000.0 / 60);
    crowd.update(1.0 / 60, INF, 0, false);
  }
  assert(murmur->stopped >= 0 && murmur->outputs.empty() && murmur->released);   // stopped and taken apart
  plat::native::drain();
  assert(audioDetail::endedCount() == ended0);   // the whistles too

  // the bang: a crack, a thump and a roar, into the game's volume
  made = audioNative::created(ctx);
  const long calls = audioNative::paramCalls(ctx);
  playBoom(ctx, a.masterGain(), 0.5);
  assert(audioNative::created(ctx) == made + 8 && audioNative::paramCalls(ctx) == calls + 12);
  {
    const std::vector<const audioNative::NodeInfo*> all = audioNative::nodes(ctx);
    const audioNative::NodeInfo* master = audioNative::info(a.masterGain());
    int intoMaster = 0;
    for (size_t i = all.size() - 8; i < all.size(); i++) {
      assert(all[i]->released);
      if (all[i]->feeds(master->id)) intoMaster++;
    }
    assert(intoMaster == 3);
    const audioNative::NodeInfo* crack = all[all.size() - 8];
    assert(crack->kind == "source" && crack->buffer != 0 && std::abs(crack->stopped - crack->started - 0.2) < 1e-12);
  }
}

// ---------------------------------------------------------------- the host (platform/audio.h)

void theHost() {
  using namespace audioNative;
  // (what earlier sounds left to be done later – a music's graph to be taken apart – is done first)
  plat::native::advance(5000);
  plat::native::drain();
  const int alive0 = alive();
  const int ended0 = audioDetail::endedCount();
  {
    const AudioContext ctx = AudioContext::create();
    assert(ctx && ctx.state() == "running" && ctx.sampleRate() == 48000);
    assert(alive() == alive0 + 1);
    assert(!AudioContext() && AudioContext().state() == "closed");

    // a node is a reference: copies name the same node; when the last one goes the host lets go of it
    const NodeInfo* gainInfo = nullptr;
    {
      const GainNode g = ctx.createGain();
      const GainNode same = g;
      const AudioNode asNode = g;
      assert(g == same && g == asNode && g.gain == same.gain && !(g.gain == AudioParam()));
      assert(alive() == alive0 + 2);
      gainInfo = info(g);
      assert(gainInfo && gainInfo->kind == "gain" && !gainInfo->released && gainInfo->param("gain").value == 1);
      // a setting keeps its node
      AudioParam p;
      {
        const OscillatorNode o = ctx.createOscillator();
        p = o.frequency;
        assert(info(o)->type == "sine" && p.value() == 440);
      }
      assert(alive() == alive0 + 3 && info(p) && info(p)->value == 440);
      p = AudioParam();
      assert(alive() == alive0 + 2);
    }
    assert(alive() == alive0 + 1 && gainInfo->released);
    assert(created(ctx) == 2 && made(ctx, "gain") == 1 && made(ctx, "osc") == 1 && made(ctx, "biquad") == 0);

    // what is asked of a setting is kept: which value, at which time
    plat::native::advance(500);
    const BiquadFilterNode f = ctx.createBiquadFilter();
    f.type("bandpass");
    f.frequency.value(1200);
    const long calls = paramCalls(ctx);
    f.Q.setValueAtTime(3, 1).linearRampToValueAtTime(4, 2).exponentialRampToValueAtTime(5, 3).setTargetAtTime(6, 4, 0.5).cancelScheduledValues(7);
    assert(paramCalls(ctx) == calls + 5);   // (`value = …` is no scheduling)
    const NodeInfo* fi = info(f);
    assert(fi->type == "bandpass" && fi->param("frequency").value == 1200 && fi->param("frequency").events.size() == 1);
    assert(fi->param("frequency").events[0].what == "value" && std::abs(fi->param("frequency").events[0].time - ctx.currentTime()) < 1e-12);
    const std::vector<ParamEvent>& q = fi->param("Q").events;
    assert(q.size() == 5 && q[0].what == "set" && q[0].value == 3 && q[0].time == 1 && q[1].what == "linear" && q[1].time == 2 && q[2].what == "exp" && q[2].value == 5);
    assert(q[3].what == "target" && q[3].value == 6 && q[3].time == 4 && q[3].timeConstant == 0.5 && q[4].what == "cancel" && q[4].time == 7);
    assert(f.Q.value() == 6 && fi->param("Q").count("set") == 1 && fi->param("Q").last("exp")->time == 3);

    // connections: into nodes (in a row, as the original writes them), into settings, and undone
    const OscillatorNode o = ctx.createOscillator();
    const GainNode g = ctx.createGain();
    const OscillatorNode lfo = ctx.createOscillator();
    o.connect(f).connect(g).connect(ctx.destination());
    o.connect(f);   // (twice is once)
    lfo.connect(o.frequency);
    lfo.connect(g.gain);
    const NodeInfo* oi = info(o);
    const NodeInfo* li = info(lfo);
    assert(oi->outputs.size() == 1 && oi->feeds(fi->id) && fi->feeds(info(g)->id) && info(g)->feeds(info(ctx.destination())->id));
    assert(info(ctx.destination())->kind == "destination" && ctx.destination() == ctx.destination());
    assert(li->paramOutputs.size() == 2 && li->paramOutputs[0].first == oi->id && li->paramOutputs[0].second == "frequency" && li->paramOutputs[1].second == "gain");
    lfo.disconnect(o.frequency);
    assert(li->paramOutputs.size() == 1);
    lfo.disconnect(o.frequency);   // (not connected any more: passed over, where the browser would throw)
    f.disconnect(g);
    assert(fi->outputs.empty());
    o.disconnect();
    lfo.disconnect();
    assert(oi->outputs.empty() && li->paramOutputs.empty());

    // sources: started and stopped once, on the context's clock; 'ended' is told once, when the time has come
    int ended = 0;
    const double t = ctx.currentTime();
    o.start(t + 0.1);
    o.start(t + 5);   // (a source starts once)
    o.stop(t + 1);
    assert(oi->started == t + 0.1 && oi->stopped == t + 1);
    o.onended([&ended] { ended += 100; });
    o.onended([&ended] { ended++; });   // (the handler is replaced)
    assert(audioDetail::endedCount() == ended0 + 1);
    plat::native::advance(900);
    plat::native::drain();
    assert(ended == 0);
    ctx.suspend();   // (while the context stands, its clock stands: no end)
    assert(ctx.state() == "suspended");
    const double stood = ctx.currentTime();
    plat::native::advance(5000);
    plat::native::drain();
    assert(ended == 0 && ctx.currentTime() == stood);
    ctx.resume();
    plat::native::advance(90);
    plat::native::drain();
    assert(ended == 0);
    plat::native::advance(20);
    plat::native::drain();
    assert(ended == 1 && audioDetail::endedCount() == ended0);
    plat::native::advance(2000);
    plat::native::drain();
    assert(ended == 1);
    // (a handler that is taken away again is never called)
    lfo.onended([&ended] { ended += 1000; });
    lfo.onended(nullptr);
    lfo.start();
    lfo.stop(ctx.currentTime() + 0.01);
    plat::native::advance(100);
    plat::native::drain();
    assert(ended == 1 && audioDetail::endedCount() == ended0);

    // buffers: samples copied in are the buffer's; a source plays a part of one, and ends by itself
    const AudioBuffer buf = ctx.createBuffer(2, 4800, 48000);
    assert(buf && buf.length() == 4800 && buf.numberOfChannels() == 2 && buf.sampleRate() == 48000 && std::abs(buf.duration() - 0.1) < 1e-12);
    std::vector<float> d(4800);
    for (size_t i = 0; i < d.size(); i++) d[i] = static_cast<float>(i) / 4800.0f;
    buf.copyToChannel(d, 1);
    buf.copyToChannel(std::vector<float>{9, 8, 7}, 0, 4798);   // (what does not fit is left out)
    assert(buf.getChannelData(1) == d && buf.getChannelData(0)[4797] == 0 && buf.getChannelData(0)[4798] == 9 && buf.getChannelData(0)[4799] == 8);
    assert(buf.getChannelData(2).size() == 4800 && buf.getChannelData(2)[10] == 0);   // (no such channel: silence)
    assert(buffers(ctx) == 1 && !ctx.createBuffer(0, 10, 48000));
    const AudioBufferSourceNode src = ctx.createBufferSource();
    src.buffer(buf);
    const double s0 = ctx.currentTime();
    int over = 0;
    src.onended([&over] { over++; });
    src.start(s0, 0.04, 0.5);   // (from 40 ms in, for half a second at most: the buffer ends after 60 ms)
    assert(info(src)->buffer != 0 && info(src)->offset == 0.04 && info(src)->duration == 0.5 && info(src)->param("playbackRate").value == 1);
    plat::native::advance(50);
    plat::native::drain();
    assert(over == 0);
    plat::native::advance(20);
    plat::native::drain();
    assert(over == 1);
    const AudioBufferSourceNode loop = ctx.createBufferSource();
    loop.buffer(buf);
    loop.loop(true);
    loop.onended([&over] { over += 10; });
    loop.start();
    plat::native::advance(10000);
    plat::native::drain();
    assert(over == 1 && info(loop)->loop);   // (round and round: it never ends by itself)
    loop.stop();
    plat::native::drain();
    assert(over == 11);

    // waves
    const std::vector<float> real = {0, 1, 0.5f}, imag = {0, 0, 0.25f};
    const PeriodicWave w = ctx.createPeriodicWave(real, imag, PeriodicWaveConstraints{.disableNormalization = true});
    assert(w && waves(ctx) == 1 && !ctx.createPeriodicWave(std::vector<float>{0}, std::vector<float>{0}));
    lfo.setPeriodicWave(w);
    assert(li->wave && li->type == "custom");

    // the other kinds of node, each with its settings
    const DynamicsCompressorNode c = ctx.createDynamicsCompressor();
    c.threshold.value(-2);
    assert(info(c)->kind == "compressor" && info(c)->param("threshold").value == -2 && c.ratio.value() == 12 && c.knee.value() == 30 && c.attack.value() == 0.003 && c.release.value() == 0.25);
    const DelayNode dl = ctx.createDelay(2);
    dl.delayTime.value(0.5);
    assert(info(dl)->kind == "delay" && dl.delayTime.value() == 0.5);
    assert(ctx.hasStereoPanner());
    const StereoPannerNode pn = ctx.createStereoPanner();
    pn.pan.value(-0.6);
    assert(info(pn)->kind == "panner" && pn.pan.value() == -0.6);
    setStereoPanner(false);   // (an old Safari)
    assert(!ctx.hasStereoPanner() && !ctx.createStereoPanner());
    ctx.createStereoPanner().pan.value(1);   // (calls on nothing are passed over)
    setStereoPanner(true);
  }
  assert(alive() == alive0);
  assert(audioDetail::endedCount() == ended0);

  // a browser before the first tap: the context waits, and resume() does not help until it may run
  {
    setAutoplayAllowed(false);
    GameAudio a;
    a.start();
    const AudioContext ctx = a.context();
    assert(ctx && ctx.state() == "suspended");
    const long calls = paramCalls(ctx);
    const double t = ctx.currentTime();
    plat::native::advance(1000);
    a.update(state({.rpm = 2000, .load = 0.5}));
    a.start();
    assert(ctx.state() == "suspended" && paramCalls(ctx) == calls && ctx.currentTime() == t);
    setAutoplayAllowed(true);   // the tap
    a.start();
    assert(ctx.state() == "running");
    plat::native::advance(1000);
    a.update(state({.rpm = 2000, .load = 0.5}));
    assert(paramCalls(ctx) > calls && std::abs(ctx.currentTime() - t - 1) < 1e-9);
  }
  // a browser without the Web Audio API: no sound, no harm
  {
    setSupported(false);
    assert(!AudioContext::supported() && !AudioContext::create());
    GameAudio a;
    int ready = 0;
    a.onReady([&ready](const AudioContext&, const GainNode&) { ready++; });
    a.start();
    a.setVehicle(SoundKind::Truck);
    a.setMusic(MusicMode::On, 1);
    a.setVolume(0.3);
    a.click(ClickKind::Chime);
    a.update(state({.rpm = 900}));
    a.suspend();
    assert(!a.context() && !a.masterGain() && ready == 0 && a.volume == 0.3);
    setSupported(true);
  }
  // the volume
  {
    GameAudio a;
    a.setVolume(0.4);
    a.start();
    const NodeInfo* master = info(a.masterGain());
    assert(master->param("gain").value == 0.4 && master->param("gain").count("target") == 0);
    a.setVolume(0.9);
    assert(a.volume == 0.9 && master->param("gain").last("target")->value == 0.9 && master->param("gain").last("target")->timeConstant == 0.05);
    // the white noise the voices are made of: one buffer, read by two sources that go round and round
    const std::vector<const NodeInfo*> all = nodes(a.context());
    int looping = 0;
    for (const NodeInfo* n : all)
      if (n->kind == "source" && n->loop && n->started == 0) looping++;
    assert(looping == 2 && buffers(a.context()) == 1);
  }

  // the timers that can be taken back
  {
    const int waiting = timers::pending();
    std::string log;
    const int once = timers::setTimeout([&log] { log += "a"; }, 100);
    const int never = timers::setTimeout([&log] { log += "x"; }, 100);
    int count = 0;
    int every = 0;
    every = timers::setInterval(
        [&] {
          log += "i";
          if (++count == 3) timers::clearInterval(every);
        },
        40);
    assert(once != 0 && never != 0 && every != 0 && timers::pending() == waiting + 3);
    timers::clearTimeout(never);
    timers::clearTimeout(0);
    for (int i = 0; i < 30; i++) {
      plat::native::advance(10);
      plat::native::drain();
    }
    assert(log == "iiai" && timers::pending() == waiting);
    timers::clearTimeout(once);   // (it has run: nothing happens)
  }

  // the speech synthesis
  {
    setSpeechSupported(false);
    assert(!hasSpeechSynthesis());
    setSpeechSupported(true);
    assert(hasSpeechSynthesis());
    const SpeechSynthesis synth = speechSynthesis();
    assert(synth.getVoices().empty() && !synth.speaking() && !synth.pending() && !synth.paused());
    int changed = 0;
    const int listener = synth.addEventListener("voiceschanged", [&changed] { changed++; });
    assert(listener != 0 && synth.addEventListener("boundary", [] {}) == 0);
    const SpeechSynthesisVoice anna{"Anna", "de-DE", "uri:Anna", true, false};
    const SpeechSynthesisVoice katja{"Katja Online (Natural)", "de-DE", "uri:Katja", false, true};
    setVoices({anna, katja});
    assert(changed == 1 && synth.getVoices().size() == 2 && synth.getVoices()[1] == katja && synth.getVoices()[1].default_);
    // (the voices of a browser are voices for the choice: voicepick takes them as they are)
    assert(rankVoices(synth.getVoices(), "de-DE")[0] == katja && chooseVoice(synth.getVoices(), "de-DE", "uri:Anna")->name == "Anna");
    synth.removeEventListener("voiceschanged", listener);
    setVoices({anna});
    assert(changed == 1);

    forgetSpoken();
    std::string log;
    const SpeechSynthesisUtterance u("In 200 Metern rechts abbiegen.");
    assert(u && !SpeechSynthesisUtterance() && u == u && !(u == SpeechSynthesisUtterance("In 200 Metern rechts abbiegen.")) && u.text() == "In 200 Metern rechts abbiegen.");
    u.lang("de-DE");
    u.voice(anna);
    u.rate(1.2);
    u.pitch(0.9);
    u.volume(0.7);
    u.onend([&log] { log += "end;"; });
    u.onerror([&log](const SpeechSynthesisErrorEvent& e) { log += "error:" + e.error + ";"; });
    const SpeechSynthesisUtterance second("Dann links.");
    second.onend([&log] { log += "end2;"; });
    second.onerror([&log](const SpeechSynthesisErrorEvent& e) { log += "error2:" + e.error + ";"; });
    synth.speak(u);
    synth.speak(second);
    assert(spoken().size() == 2 && synth.speaking() && synth.pending());
    const Spoken& s = spoken()[0];
    assert(s.text == "In 200 Metern rechts abbiegen." && s.lang == "de-DE" && s.voiceName == "Anna" && s.voiceURI == "uri:Anna" && s.rate == 1.2 && s.pitch == 0.9 && s.volume == 0.7);
    assert(spoken()[1].voiceName.empty() && spoken()[1].rate == 1);
    assert(endSpeech() && log == "end;" && synth.speaking() && !synth.pending());
    assert(failSpeech("network") && log == "end;error2:network;" && !synth.speaking());
    assert(!endSpeech() && !failSpeech("x"));
    // cancel(): the one being read is told it was interrupted, those waiting that they were cancelled – a moment later
    log.clear();
    const SpeechSynthesisUtterance a("eins"), b("zwei");
    a.onerror([&log](const SpeechSynthesisErrorEvent& e) { log += "a:" + e.error + ";"; });
    b.onerror([&log](const SpeechSynthesisErrorEvent& e) { log += "b:" + e.error + ";"; });
    synth.speak(a);
    synth.speak(b);
    synth.cancel();
    assert(log.empty() && !synth.speaking());
    plat::native::drain();
    assert(log == "a:interrupted;b:canceled;");
    setSpeechPaused(true);
    assert(synth.paused());
    synth.resume();
    assert(!synth.paused());
    setVoices({}, false);
    forgetSpoken();
  }
}

}  // namespace

int main() {
  // (the device tests first: the drive that is compared with the original's begins as its clock does, at nought)
  asTheOriginalOnItsDevice();
  nothingMadeWhileDriving();
  everyShortSoundPlays();
  bellHissBeep();
  musicOffUntilAskedFor();

  everyVehicleHasASound();
  profilesAreSound();
  partialsArePulses();
  firingFrequency();
  engineSpeedsAreMapped();
  louderAndBrighter();
  balanced();
  silentWhenOff();
  neverNaN();
  turboAndClatter();
  theTram();
  theAeroplane();
  aWeighting();
  numbersOfTheOriginal();

  theChoiceOfTheVoice();
  theHorn();
  bellCrowdAndBang();
  theHost();

  std::printf("audio_test: ok\n");
  return 0;
}
