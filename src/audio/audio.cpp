// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/audio.ts @ v1.7.7
#include "audio/audio.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "core/random.h"

namespace fs {

namespace {

const char* const CLICK_NAMES[] = {"tick", "tock", "crash", "grind", "chime", "bump", "door", "doorShut", "step", "air", "airShort", "airDoor", "bell", "clack", "beep"};

/** the tram's bell: partials of a dome bell (ratio to the lowest), how strong, how fast they die away (s) */
const double BELL[4][3] = {
    {1, 1, 0.35},
    {2.32, 0.5, 0.18},
    {4.25, 0.25, 0.09},
    {6.63, 0.12, 0.05},
};
const SoundProfile::Relay DEFAULT_RELAY = {.tick = 2600, .tock = 1900, .level = 0.35};
/** the hiss of air: its band-pass (Hz, Q) – loudness of the air sounds are given against this */
const double HISS_F = 3600;
const double HISS_Q = 0.6;

// The settings the main loop moves, numbered for the memory of what was last sent to each.
const int F = 0;
const int SOFT = 1;
const int HARD = 2;
const int LEVEL = 3;
const int CUT = 4;
const int JIT_P = 5;
const int JIT_A = 6;
const int NZ_F = 7;
const int NZ_L = 8;
const int TB_F = 9;
const int TB_L = 10;
const int TA_F = 11;
const int TA_L = 12;
const int TB2_F = 13;
const int TB2_L = 14;
const int RD_C = 15;
const int RD_L = 16;
const int WD_F = 17;
const int WD_L = 18;
const int SK_F = 19;
const int SK_L = 20;
const int HORN = 21;
const int START = 22;

}  // namespace

struct GameAudio::Nodes {
  AudioContext ctx;
  GainNode master;
  GainNode bus;
  AudioBuffer noiseBuf;
  OscillatorNode oscSoft;
  OscillatorNode oscHard;
  GainNode gSoft;
  GainNode gHard;
  BiquadFilterNode mainLp;
  BiquadFilterNode mainBody;
  GainNode jitStage;
  GainNode mainLevel;
  GainNode jitPitch;
  GainNode jitAmp;
  GainNode jitSqueal;
  BiquadFilterNode nzBp;
  GainNode nzAm;
  GainNode nzDepth;
  GainNode nzLevel;
  OscillatorNode turbo;
  GainNode turboLevel;
  OscillatorNode toneA;
  GainNode toneALevel;
  OscillatorNode toneB;
  GainNode toneBLevel;
  BiquadFilterNode roadLp;
  GainNode roadLevel;
  BiquadFilterNode windBp;
  GainNode windLevel;
  BiquadFilterNode skidBp;
  GainNode skidLevel;
  BiquadFilterNode hissBp;
  GainNode hissLevel;
  OscillatorNode horn1;
  OscillatorNode horn2;
  BiquadFilterNode hornLp;
  GainNode hornLevel;
  OscillatorNode starter;
  OscillatorNode starterLfo;
  GainNode starterDepth;
  GainNode starterLevel;
};

const char* clickKindName(ClickKind kind) { return CLICK_NAMES[static_cast<size_t>(kind)]; }

std::optional<ClickKind> clickKindOf(std::string_view name) {
  for (ClickKind k : CLICK_KINDS)
    if (name == clickKindName(k)) return k;
  return std::nullopt;
}

GameAudio::GameAudio() : voice_(&voicing(SoundKind::Car)) { sent_.fill(JS_NAN); }

GameAudio::~GameAudio() = default;

void GameAudio::start() {
  if (n_) {
    n_->ctx.resume();
    return;
  }
  if (!AudioContext::supported()) return;
  const AudioContext ctx = AudioContext::create();
  if (!ctx) return;
  const double t0 = ctx.currentTime();
  auto gain = [&ctx](double v) -> GainNode {
    const GainNode g = ctx.createGain();
    g.gain.value(v);
    return g;
  };
  auto filter = [&ctx](const char* type, double f, double q) -> BiquadFilterNode {
    const BiquadFilterNode b = ctx.createBiquadFilter();
    b.type(type);
    b.frequency.value(f);
    b.Q.value(q);
    return b;
  };
  auto osc = [&ctx](const char* type, double f) -> OscillatorNode {
    const OscillatorNode o = ctx.createOscillator();
    o.type(type);
    o.frequency.value(f);
    return o;
  };

  const GainNode master = gain(volume);
  // a limiter, so that a crash on top of a roaring engine does not clip (set high: below it nothing changes)
  const DynamicsCompressorNode limiter = ctx.createDynamicsCompressor();
  limiter.threshold.value(-2);
  limiter.knee.value(1);
  limiter.ratio.value(20);
  limiter.attack.value(0.003);
  limiter.release.value(0.25);
  master.connect(limiter);
  limiter.connect(ctx.destination());
  const GainNode bus = gain(1);
  const BiquadFilterNode subsonic = filter("highpass", 28, -3);
  bus.connect(subsonic);
  subsonic.connect(master);

  // white noise, read by two sources at different places (so that the voices fed by them do not move together)
  const int len = static_cast<int>(ctx.sampleRate() * 2);
  const AudioBuffer noiseBuf = ctx.createBuffer(1, len, ctx.sampleRate());
  std::vector<float> d(static_cast<size_t>(len));   // (noiseBuf.getChannelData(0): handed to the buffer below)
  uint32_t seed = 12345;
  for (int i = 0; i < len; i++) {
    seed = seed * 1664525u + 1013904223u;
    d[static_cast<size_t>(i)] = static_cast<float>((seed / 4294967296.0) * 2 - 1);
  }
  noiseBuf.copyToChannel(d, 0);
  auto noise = [&](double offset) -> AudioBufferSourceNode {
    const AudioBufferSourceNode s = ctx.createBufferSource();
    s.buffer(noiseBuf);
    s.loop(true);
    s.start(t0, offset);
    return s;
  };
  const AudioBufferSourceNode noiseA = noise(0);
  const AudioBufferSourceNode noiseB = noise(0.77);

  // the main voice: both waves on one clock (started together, they stay in phase as they blend)
  const OscillatorNode oscSoft = osc("sine", 20);
  const OscillatorNode oscHard = osc("sine", 20);
  const GainNode gSoft = gain(0);
  const GainNode gHard = gain(0);
  const BiquadFilterNode mainLp = filter("lowpass", 1000, 1);
  const BiquadFilterNode mainBody = filter("peaking", 150, 1);
  const GainNode jitStage = gain(1);
  const GainNode mainLevel = gain(0);
  oscSoft.connect(gSoft);
  oscHard.connect(gHard);
  gSoft.connect(mainLp);
  gHard.connect(mainLp);
  mainLp.connect(mainBody);
  mainBody.connect(jitStage);
  jitStage.connect(mainLevel);
  mainLevel.connect(bus);
  // unsteadiness: slow random wandering of the engine's speed and strength (and of the tram's squeal)
  const BiquadFilterNode jitLp = filter("lowpass", 20, -3);
  jitRms_ = 0.577 * std::sqrt((1.11 * 20) / (ctx.sampleRate() / 2));
  const GainNode jitPitch = gain(0);
  const GainNode jitAmp = gain(0);
  const GainNode jitSqueal = gain(0);
  noiseA.connect(jitLp);
  jitLp.connect(jitPitch);
  jitLp.connect(jitAmp);
  jitLp.connect(jitSqueal);
  jitPitch.connect(oscSoft.detune);
  jitPitch.connect(oscHard.detune);
  jitAmp.connect(jitStage.gain);

  // the engine's noise, opened and closed by the hard wave: it comes with the firings
  const BiquadFilterNode nzBp = filter("bandpass", 1000, 1);
  const GainNode nzAm = gain(1);
  const GainNode nzDepth = gain(0);
  const GainNode nzLevel = gain(0);
  noiseB.connect(nzBp);
  nzBp.connect(nzAm);
  nzAm.connect(nzLevel);
  nzLevel.connect(bus);
  oscHard.connect(nzDepth);
  nzDepth.connect(nzAm.gain);

  const OscillatorNode turbo = osc("sine", 3000);
  const GainNode turboLevel = gain(0);
  turbo.connect(turboLevel);
  turboLevel.connect(bus);
  const OscillatorNode toneA = osc("sine", 440);
  const GainNode toneALevel = gain(0);
  toneA.connect(toneALevel);
  toneALevel.connect(bus);
  const OscillatorNode toneB = osc("sine", 440);
  const GainNode toneBLevel = gain(0);
  toneB.connect(toneBLevel);
  toneBLevel.connect(bus);

  // rolling, wind, a skidding tyre (the tram's squeal, wandering in pitch), air
  const BiquadFilterNode roadLp = filter("lowpass", 500, 1);
  const GainNode roadLevel = gain(0);
  noiseA.connect(roadLp);
  roadLp.connect(roadLevel);
  roadLevel.connect(bus);
  const BiquadFilterNode windBp = filter("bandpass", 500, 0.6);
  const GainNode windLevel = gain(0);
  noiseB.connect(windBp);
  windBp.connect(windLevel);
  windLevel.connect(bus);
  const BiquadFilterNode skidBp = filter("bandpass", 1150, 6);
  const GainNode skidLevel = gain(0);
  noiseA.connect(skidBp);
  skidBp.connect(skidLevel);
  skidLevel.connect(bus);
  jitSqueal.connect(skidBp.detune);
  const BiquadFilterNode hissBp = filter("bandpass", HISS_F, HISS_Q);
  const GainNode hissLevel = gain(0);
  noiseB.connect(hissBp);
  hissBp.connect(hissLevel);
  hissLevel.connect(bus);
  hissUnit_ = REF_LOUDNESS / noiseLoudness([](double f) { return bandpassGain(f, HISS_F, HISS_Q); }, ctx.sampleRate());

  // horn: two tones
  const OscillatorNode horn1 = osc("sawtooth", 415);
  const OscillatorNode horn2 = osc("sawtooth", 520);
  const BiquadFilterNode hornLp = filter("lowpass", 2400, 1);
  const GainNode hornLevel = gain(0);
  horn1.connect(hornLp);
  horn2.connect(hornLp);
  hornLp.connect(hornLevel);
  hornLevel.connect(bus);

  // starter motor: a whirr that labours through the compression strokes
  const OscillatorNode starter = osc("sawtooth", 88);
  const OscillatorNode starterLfo = osc("sine", 9);
  const GainNode starterDepth = gain(22);
  const GainNode starterLevel = gain(0);
  starterLfo.connect(starterDepth);
  starterDepth.connect(starter.frequency);
  starter.connect(starterLevel);
  starterLevel.connect(bus);

  for (const OscillatorNode* o : {&oscSoft, &oscHard, &turbo, &toneA, &toneB, &horn1, &horn2, &starter, &starterLfo}) o->start(t0);

  n_.reset(new Nodes{
      ctx,
      master,
      bus,
      noiseBuf,
      oscSoft,
      oscHard,
      gSoft,
      gHard,
      mainLp,
      mainBody,
      jitStage,
      mainLevel,
      jitPitch,
      jitAmp,
      jitSqueal,
      nzBp,
      nzAm,
      nzDepth,
      nzLevel,
      turbo,
      turboLevel,
      toneA,
      toneALevel,
      toneB,
      toneBLevel,
      roadLp,
      roadLevel,
      windBp,
      windLevel,
      skidBp,
      skidLevel,
      hissBp,
      hissLevel,
      horn1,
      horn2,
      hornLp,
      hornLevel,
      starter,
      starterLfo,
      starterDepth,
      starterLevel,
  });
  applyVehicle();
  if (musicMode_ != MusicMode::Off) setMusic(musicMode_, musicVolume_);
  std::vector<std::function<void(const AudioContext& ctx, const GainNode& master)>> ready;
  ready.swap(ready_);
  for (const auto& f : ready) f(ctx, master);
}

void GameAudio::suspend() {
  if (n_) n_->ctx.suspend();
}

void GameAudio::setVolume(double v) {
  volume = v;
  if (n_) n_->master.gain.setTargetAtTime(v, n_->ctx.currentTime(), 0.05);
}

AudioContext GameAudio::context() const { return n_ ? n_->ctx : AudioContext(); }

GainNode GameAudio::masterGain() const { return n_ ? n_->master : GainNode(); }

void GameAudio::onReady(std::function<void(const AudioContext& ctx, const GainNode& master)> f) {
  if (n_) f(n_->ctx, n_->master);
  else ready_.push_back(std::move(f));
}

void GameAudio::setVehicle(SoundKind kind, const RpmScale* engine) {
  kind_ = kind;
  voice_ = &voicing(kind);
  if (engine && engine->idleRpm > 0 && engine->limiterRpm > engine->idleRpm) scale_ = RpmScale{engine->idleRpm, engine->limiterRpm};
  else scale_.reset();
  applyVehicle();
}

void GameAudio::setMusic(MusicMode mode, double volume) {
  musicMode_ = mode;
  musicVolume_ = jsMax(0, jsMin(1, volume));
  Nodes* n = n_.get();
  if (!n || (mode == MusicMode::Off && !music_)) return;
  if (!music_) music_ = std::make_unique<MusicPlayer>(n->ctx, n->master);
  music_->set(mode, musicVolume_);
}

void GameAudio::click(ClickKind kind) {
  Nodes* n = n_.get();
  if (!n) return;
  const AudioContext& ctx = n->ctx;
  const double t = ctx.currentTime();
  switch (kind) {
    case ClickKind::Chime: {
      const OscillatorNode o = ctx.createOscillator();
      const GainNode g = ctx.createGain();
      o.frequency.setValueAtTime(880, t);
      o.frequency.setValueAtTime(660, t + 0.12);
      g.gain.setValueAtTime(0.0001, t);
      g.gain.exponentialRampToValueAtTime(0.18, t + 0.01);
      g.gain.exponentialRampToValueAtTime(0.0001, t + 0.4);
      o.connect(g);
      g.connect(n->master);
      o.start(t);
      o.stop(t + 0.45);
      return;
    }
    case ClickKind::Tick:
    case ClickKind::Tock: {
      // (the relay sounds as the vehicle's does: a truck's clacks, a motorcycle's barely ticks)
      const SoundProfile::Relay& r = voice_->profile.relay ? *voice_->profile.relay : DEFAULT_RELAY;
      noiseHit(t, "bandpass", kind == ClickKind::Tick ? r.tick : r.tock, 4, r.level, 0.03);
      return;
    }
    case ClickKind::Crash:
      noiseHit(t, "lowpass", 900, 1, 1.0, 0.5);
      return;
    case ClickKind::Bump:
      noiseHit(t, "lowpass", 220, 1, 0.7, 0.12);
      return;
    case ClickKind::Door:
      // the latch of a door that opens
      noiseHit(t, "bandpass", 1150, 3, 0.5, 0.07);
      return;
    case ClickKind::DoorShut:
      noiseHit(t, "lowpass", 320, 1, 1.0, 0.2);
      return;
    case ClickKind::Grind:
      noiseHit(t, "bandpass", 1700, 1.5, 0.4, 0.28);
      return;
    case ClickKind::Step: {
      // the heel, then the sole – a scuff on a pavement, a crunch on gravel; no two alike
      // (each number is drawn in the order the original draws it)
      const bool gravel = lastRough_ > 1.6;
      const double heelF = 170 + 90 * rnd();
      const double heelLevel = 0.13 * (0.8 + 0.4 * rnd());
      noiseHit(t, "lowpass", heelF, 1, heelLevel, 0.06);
      const double soleT = t + 0.02 + 0.03 * rnd();
      const double soleF = gravel ? 2400 + 600 * rnd() : 900 + 400 * rnd();
      const double soleLevel = (gravel ? 0.09 : 0.05) * (0.8 + 0.4 * rnd());
      noiseHit(soleT, "bandpass", soleF, gravel ? 1.2 : 1.5, soleLevel, gravel ? 0.11 : 0.05);
      return;
    }
    case ClickKind::Air:
      // the spring brakes' air let out: loud at once, the hiss sinking as the pressure goes
      hiss(3, 0.25, 0.28, 4200, 2600);
      return;
    case ClickKind::AirShort:
      hiss(-7, 0.05, 0.1, 4500, 3200);
      return;
    case ClickKind::AirDoor:
      hiss(-2, 0.3, 0.15, 3000, 2200);
      noiseHit(t + 0.75, "lowpass", 260, 1, 0.5, 0.15);
      return;
    case ClickKind::Bell:
      strike(t);
      return;
    case ClickKind::Clack: {
      // (the two axles of a bogie, 1.8 m apart)
      const double gap = std::min(0.5, 1.8 / std::max(1.0, lastSpeed_));
      for (const double dt : {0.0, gap}) {
        noiseHit(t + dt, "lowpass", 1300, 1, 0.45, 0.05);
        noiseHit(t + dt, "bandpass", 2300, 8, 0.25, 0.09);
      }
      return;
    }
    case ClickKind::Beep:
      beep(t);
      return;
  }
}

void GameAudio::update(const AudioState& s) {
  Nodes* n = n_.get();
  if (!n || n->ctx.state() != "running") return;
  const double t = (now_ = n->ctx.currentTime());
  const Voicing& v = *voice_;
  const SoundProfile& p = v.profile;
  const SoundTargets& T = soundTargets(v, s, scale_ ? &*scale_ : nullptr, tg_);

  // the main voice: both oscillators always on the same speed
  if (changed(F, T.freq)) {
    n->oscSoft.frequency.setTargetAtTime(T.freq, t, 0.03);
    n->oscHard.frequency.setTargetAtTime(T.freq, t, 0.03);
  }
  glide(SOFT, n->gSoft.gain, T.soft, 0.06);
  glide(HARD, n->gHard.gain, T.hard, 0.06);
  glide(LEVEL, n->mainLevel.gain, T.level, 0.05);
  glide(CUT, n->mainLp.frequency, T.cutoff, 0.06);
  glide(JIT_P, n->jitPitch.gain, T.jitCents / jitRms_, 0.2);
  glide(JIT_A, n->jitAmp.gain, T.jitAmp / jitRms_, 0.2);
  glide(NZ_F, n->nzBp.frequency, T.noiseFreq, 0.08);
  glide(NZ_L, n->nzLevel.gain, T.noiseLevel, 0.06);
  // (the turbo spools up slowly and runs down faster)
  const double spool = p.turbo ? p.turbo->spool : 0.5;
  const bool rising = !(T.turboLevel < sent_[TB_L]);
  glide(TB_F, n->turbo.frequency, T.turboFreq, rising ? spool : spool * 0.5);
  glide(TB_L, n->turboLevel.gain, T.turboLevel, rising ? spool * 0.7 : spool * 0.35);
  glide(TA_F, n->toneA.frequency, T.toneAFreq, 0.05);
  glide(TA_L, n->toneALevel.gain, T.toneALevel, 0.06);
  glide(TB2_F, n->toneB.frequency, T.toneBFreq, 0.05);
  glide(TB2_L, n->toneBLevel.gain, T.toneBLevel, 0.06);
  glide(RD_C, n->roadLp.frequency, T.roadCutoff, 0.1);
  glide(RD_L, n->roadLevel.gain, T.roadLevel, 0.1);
  glide(WD_F, n->windBp.frequency, T.windFreq, 0.15);
  glide(WD_L, n->windLevel.gain, T.windLevel, 0.15);
  glide(SK_F, n->skidBp.frequency, T.skidFreq, 0.1);
  glide(SK_L, n->skidLevel.gain, T.skidLevel, p.squeal ? 0.08 : 0.04);
  glide(HORN, n->hornLevel.gain, T.hornLevel, 0.015);
  glide(START, n->starterLevel.gain, T.starterLevel, 0.02);

  // the indicator relay
  if (s.indicatorOn && s.blinkOn != lastBlink_) click(s.blinkOn ? ClickKind::Tick : ClickKind::Tock);
  lastBlink_ = s.blinkOn;
  // the tram's bell: struck as it is pressed, and again and again while it is held
  if (p.bell && s.horn && (!lastHorn_ || t >= nextBell_)) {
    strike(t);
    nextBell_ = t + 0.32;
  }
  lastHorn_ = s.horn;
  if (p.air) airBrakes(s, t);
  // the reversing alarm: a beep a second
  if (p.beeper) {
    if (s.reversing.value_or(false) && s.engineRunning) {
      if (t >= nextBeep_) {
        beep(t);
        nextBeep_ = t + 1;
      }
    } else nextBeep_ = 0;
  }
  lastRough_ = s.rough;
  const double speed = std::abs(s.speed);
  lastSpeed_ = speed != 0 && speed == speed ? speed : 0;
}

void GameAudio::applyVehicle() {
  Nodes* n = n_.get();
  if (!n) return;
  const AudioContext& ctx = n->ctx;
  const double t = ctx.currentTime();
  const Voicing& v = *voice_;
  const SoundProfile& p = v.profile;
  n->bus.gain.cancelScheduledValues(t);
  n->bus.gain.setValueAtTime(0, t);
  n->bus.gain.setTargetAtTime(1, t + 0.08, 0.05);
  for (const GainNode* g : {&n->gSoft, &n->gHard, &n->mainLevel, &n->nzLevel, &n->turboLevel, &n->toneALevel, &n->toneBLevel, &n->roadLevel, &n->windLevel, &n->skidLevel, &n->hornLevel,
                            &n->starterLevel, &n->hissLevel}) {
    g->gain.cancelScheduledValues(t);
    g->gain.setValueAtTime(0, t);
  }
  sent_.fill(JS_NAN);
  if (v.soft && v.hard) {
    // (normalised already – peak 1 – so that browsers that normalise again change nothing)
    n->oscSoft.setPeriodicWave(ctx.createPeriodicWave(v.soft->real, v.soft->imag, PeriodicWaveConstraints{.disableNormalization = true}));
    n->oscHard.setPeriodicWave(ctx.createPeriodicWave(v.hard->real, v.hard->imag, PeriodicWaveConstraints{.disableNormalization = true}));
  }
  if (p.main) {
    const MainVoice& m = *p.main;
    n->mainLp.Q.value(m.lpQ);
    n->mainBody.frequency.value(m.body[0]);
    n->mainBody.Q.value(m.body[1]);
    n->mainBody.gain.value(m.body[2]);
  }
  const NoiseVoice* nz = p.noise ? &*p.noise : nullptr;
  n->nzBp.Q.value(nz ? nz->q : 1);
  n->nzAm.gain.value(nz ? 1 - nz->am : 1);
  n->nzDepth.gain.value(nz ? nz->am : 0);
  if (p.road) n->roadLp.Q.value(p.road->q);
  if (p.wind) n->windBp.Q.value(p.wind->q);
  n->skidBp.Q.value(p.squeal ? p.squeal->q : (p.skid ? p.skid->q : 6));
  n->jitSqueal.gain.value(p.squeal ? p.squeal->wobble / jitRms_ : 0);
  n->toneA.type(p.stallHorn ? "triangle" : (p.tones.size() > 0 ? p.tones[0].wave : "sine"));
  n->toneB.type(p.tones.size() > 1 ? p.tones[1].wave : "sine");
  if (p.horn) {
    n->horn1.type(p.horn->wave);
    n->horn2.type(p.horn->wave);
    n->horn1.frequency.value(p.horn->f[0]);
    n->horn2.frequency.value(p.horn->f[1]);
    n->hornLp.frequency.value(p.horn->lp);
  }
  if (p.starter) {
    n->starter.frequency.value(p.starter->f);
    n->starterLfo.frequency.value(p.starter->lfo);
    n->starterDepth.gain.value(p.starter->depth);
  }
  // (a drive begins with the parking brake on: that is no "pssht")
  lastPark_.reset();
  brakeHeld_ = 0;
  purgeAt_ = 0;
  nextBeep_ = 0;
  lastHorn_ = false;
}

bool GameAudio::changed(int i, double v) {
  const double last = sent_[static_cast<size_t>(i)];
  if (std::abs(v - last) <= 1e-4 + std::abs(last) * 0.003) return false;
  sent_[static_cast<size_t>(i)] = v;
  return true;
}

void GameAudio::glide(int i, const AudioParam& param, double v, double tc) {
  if (changed(i, v)) param.setTargetAtTime(v, now_, tc);
}

void GameAudio::airBrakes(const AudioState& s, double t) {
  if (s.parkingBrake.has_value()) {
    if (lastPark_.has_value() && *s.parkingBrake != *lastPark_) {
      if (*s.parkingBrake) hiss(3, 0.25, 0.28, 4200, 2600);
      else hiss(-10, 0.1, 0.12, 3800, 3000);
    }
    lastPark_ = *s.parkingBrake;
  }
  const double b = s.braking.value_or(0);
  if (b > 0.12) brakeHeld_ = std::max(brakeHeld_, b);
  else if (b < 0.04 && brakeHeld_ > 0) {
    if (brakeHeld_ > 0.15 && t >= nextPuff_) {
      hiss(-12 + 6 * std::min(1.0, brakeHeld_), 0.05, 0.12, 4500, 3000);
      nextPuff_ = t + 0.8;
    }
    brakeHeld_ = 0;
  }
  // (the compressor has filled the tanks: the dryer blows out – every minute or two while the engine runs)
  if (s.engineRunning) {
    if (purgeAt_ == 0) purgeAt_ = t + 40 + 60 * rnd();
    else if (t >= purgeAt_) {
      hiss(0, 0.5, 0.35, 3600, 2400);
      purgeAt_ = t + 70 + 90 * rnd();
    }
  } else purgeAt_ = 0;
}

void GameAudio::hiss(double db, double hold, double decay, double from, double to) {
  Nodes* n = n_.get();
  if (!n) return;
  const double t = n->ctx.currentTime();
  const AudioParam& g = n->hissLevel.gain;
  const AudioParam& f = n->hissBp.frequency;
  const double level = std::pow(10, db / 20) * hissUnit_;
  g.cancelScheduledValues(t);
  g.setValueAtTime(0, t);
  g.linearRampToValueAtTime(level, t + 0.015);
  g.setTargetAtTime(0, t + 0.015 + hold, decay);
  f.cancelScheduledValues(t);
  f.setValueAtTime(from, t);
  f.setTargetAtTime(to, t + 0.05, hold + decay);
}

void GameAudio::noiseHit(double t, const char* type, double freq, double q, double level, double dur) {
  Nodes* n = n_.get();
  if (!n) return;
  const AudioContext& ctx = n->ctx;
  const AudioBufferSourceNode src = ctx.createBufferSource();
  src.buffer(n->noiseBuf);
  const BiquadFilterNode f = ctx.createBiquadFilter();
  f.type(type);
  f.frequency.value(freq);
  f.Q.value(q);
  const GainNode g = ctx.createGain();
  g.gain.setValueAtTime(level, t);
  g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
  src.connect(f);
  f.connect(g);
  g.connect(n->master);
  src.start(t, rnd() * 1.5, dur + 0.05);
}

void GameAudio::strike(double t) {
  Nodes* n = n_.get();
  if (!n) return;
  const AudioContext& ctx = n->ctx;
  const GainNode out = ctx.createGain();
  out.gain.value(0.12);
  out.connect(n->bus);
  for (const double* bell : BELL) {
    const double ratio = bell[0], a = bell[1], tc = bell[2];
    const OscillatorNode o = ctx.createOscillator();
    o.frequency.value(1040 * ratio);
    const GainNode g = ctx.createGain();
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(a, t + 0.002);
    g.gain.setTargetAtTime(0, t + 0.002, tc);
    o.connect(g);
    g.connect(out);
    o.start(t);
    o.stop(t + tc * 7);
  }
  noiseHit(t, "bandpass", 3200, 2, 0.12, 0.012);
}

void GameAudio::beep(double t) {
  Nodes* n = n_.get();
  if (!n) return;
  const AudioContext& ctx = n->ctx;
  const OscillatorNode o = ctx.createOscillator();
  o.type("square");
  o.frequency.value(1150);
  const BiquadFilterNode f = ctx.createBiquadFilter();
  f.type("bandpass");
  f.frequency.value(1150);
  f.Q.value(3);
  const GainNode g = ctx.createGain();
  g.gain.setValueAtTime(0, t);
  g.gain.linearRampToValueAtTime(0.035, t + 0.01);
  g.gain.setValueAtTime(0.035, t + 0.42);
  g.gain.linearRampToValueAtTime(0, t + 0.45);
  o.connect(f);
  f.connect(g);
  g.connect(n->bus);
  o.start(t);
  o.stop(t + 0.5);
}

}  // namespace fs
