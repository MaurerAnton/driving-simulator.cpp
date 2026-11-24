// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/music.ts @ v1.7.7
// (the original's `unref` – a timer that does not keep Node alive, for its tests – has nothing to be here)
#include "audio/music.h"

#include <algorithm>
#include <cmath>

#include "audio/profiles.h"
#include "audio/timers.h"
#include "audio/waves.h"
#include "core/js.h"
#include "core/random.h"
#include "platform/plat.h"

namespace fs {

namespace {

struct Voice {
  /** 'sine' | 'triangle' */
  const char* wave;
  double peak;
  double decay;
  bool held;
};

/** how each instrument sounds: its wave (FM: a sine modulated by another), peak (per unit of strength), how it dies away (s), held while the note is */
const Voice& VOICES(Instrument i) {
  static const Voice all[INSTRUMENT_COUNT] = {
      // a soft electric piano: a sine whose brightness (the modulation) fades fast
      /* bell */ {"sine", 0.11, 0.9, false},
      /* pluck */ {"triangle", 0.09, 0.45, false},
      // wood: a sine struck with a short, bright knock four times its pitch
      /* marimba */ {"sine", 0.12, 0.3, false},
      // a flute-like lead with a slow vibrato
      /* flute */ {"triangle", 0.07, 0.09, true},
      /* pad */ {"triangle", 0.045, 0.5, true},
      /* bass */ {"triangle", 0.14, 0.12, true},
  };
  return all[static_cast<size_t>(i)];
}
/** (the order the original's table of voices is gone through in: the buses are made in it) */
const Instrument VOICE_ORDER[INSTRUMENT_COUNT] = {Instrument::Bell, Instrument::Marimba, Instrument::Pluck, Instrument::Flute, Instrument::Pad, Instrument::Bass};

/** where each instrument stands, left (−1) to right (1) */
double PAN(Instrument i) {
  static const double all[INSTRUMENT_COUNT] = {/* bell */ 0.15, /* pluck */ -0.3, /* marimba */ 0.2, /* flute */ -0.1, /* pad */ 0, /* bass */ 0};
  return all[static_cast<size_t>(i)];
}
/** what goes into the echo */
double SEND(Instrument i) {
  static const double all[INSTRUMENT_COUNT] = {/* bell */ 0.3, /* pluck */ 0.3, /* marimba */ 0.25, /* flute */ 0.25, /* pad */ 0.1, /* bass */ 0};
  return all[static_cast<size_t>(i)];
}

/** how far ahead notes are scheduled (s), and how often (ms) */
const double LOOKAHEAD = 1.2;
const double TICK_MS = 250;

double midiHz(double p) { return 440 * std::pow(2, (p - 69) / 12); }

std::string keyOf(const Piece& piece) { return std::to_string(piece.key) + "/" + modeName(piece.mode); }

}  // namespace

const char* musicModeName(MusicMode mode) { return mode == MusicMode::Quiet ? "quiet" : mode == MusicMode::On ? "on" : "off"; }

std::optional<MusicMode> musicModeOf(std::string_view name) {
  if (name == "off") return MusicMode::Off;
  if (name == "quiet") return MusicMode::Quiet;
  if (name == "on") return MusicMode::On;
  return std::nullopt;
}

double pauseAfter(MusicMode mode, double r) { return mode == MusicMode::Quiet ? 150 + 180 * r : 25 + 45 * r; }

double pieceGain(const Piece& piece, MusicMode mode, double volume) {
  if (mode == MusicMode::Off) return 0;
  const double v = jsMax(0, jsMin(1, volume));
  return (v * REF_LOUDNESS * std::pow(10, MUSIC_DB[mode] / 20)) / std::max(1e-6, pieceLoudness(piece, 1));
}

double pieceLoudness(const Piece& piece, double level) {
  double energy = 0;
  for (const MusicNote& n : piece.notes) {
    const Voice& vc = VOICES(n.i);
    const double a = vc.peak * n.v;
    // (held: the note's length at about its peak; struck: e^(−t/τ)² integrates to τ/2)
    const double time = vc.held ? n.d + vc.decay / 2 : vc.decay / 2;
    const double shape = std::string_view(vc.wave) == "sine" ? 0.5 : 1.0 / 3;
    const double w = aWeight(midiHz(n.p));
    energy += a * a * shape * time * w * w;
  }
  return level * std::sqrt(energy / piece.length);
}

MusicPlayer::MusicPlayer(const BaseAudioContext& ctx, const AudioNode& dest, MusicOptions opts)
    : ctx_(ctx),
      dest_(dest),
      useTimer_(opts.timer.value_or(true)),
      seed_(opts.seed ? *opts.seed : static_cast<uint32_t>(jsInt32(plat::dateMs()) ^ jsInt32(std::floor(rnd() * 0x7fffffff)))) {}

MusicPlayer::~MusicPlayer() {
  // (the timer calls tick() on this player: it must not outlive it)
  if (timer_ != 0) timers::clearInterval(timer_);
}

void MusicPlayer::set(MusicMode mode, double volume) {
  const MusicMode was = mode_;
  mode_ = mode;
  volume_ = jsMax(0, jsMin(1, volume));
  if (mode == MusicMode::Off) {
    stop();
    return;
  }
  const double now = ctx_.currentTime();
  if (!g_) g_ = build();
  if (piece_) g_->level.gain.setTargetAtTime(pieceGain(*piece_, mode, volume_), now, 0.3);
  // switched on: the first piece soon, so that one hears it is on; 'on' after 'quiet': no long wait
  if (was == MusicMode::Off) nextPieceAt_ = now + 3 + 4 * rnd();
  else if (mode == MusicMode::On) nextPieceAt_ = std::min(nextPieceAt_, now + pauseAfter(MusicMode::On, rnd()));
  if (useTimer_ && timer_ == 0) timer_ = timers::setInterval([this] { tick(); }, TICK_MS);
}

void MusicPlayer::tick() {
  // (held for the length of the tick: the player may be switched off by what a note sets going)
  const std::shared_ptr<MusicGraph> g = g_;
  if (mode_ == MusicMode::Off || !g) return;
  const double now = ctx_.currentTime();
  if (!piece_) {
    if (now < nextPieceAt_) return;
    begin(now + 0.1);
  }
  const Piece& p = *piece_;
  const double until = now + LOOKAHEAD;
  while (next_ < p.notes.size()) {
    const MusicNote& n = p.notes[next_];
    const double t = begun_ + n.t;
    if (t >= until) break;
    next_++;
    // (missed while the page was busy: left out rather than played in a heap)
    if (t < now - 0.05) continue;
    play(*g, n, std::max(t, now));
  }
  if (next_ >= p.notes.size() && now > begun_ + p.length) {
    piece_.reset();
    nextPieceAt_ = now + pauseAfter(mode_, rnd());
  }
}

void MusicPlayer::dispose() {
  mode_ = MusicMode::Off;
  stop();
}

void MusicPlayer::begin(double at) {
  // a new piece – not in the key and mode of the last one
  Piece piece = composePiece(seed_++);
  for (int i = 0; i < 4 && keyOf(piece) == lastKey_; i++) piece = composePiece(seed_++);
  lastKey_ = keyOf(piece);
  piece_ = std::move(piece);
  begun_ = at;
  next_ = 0;
  played++;
  // the echo in time with it: a dotted eighth
  const MusicGraph& g = *g_;
  const double dt = (0.75 * 60) / piece_->bpm;
  g.echoA.delayTime.setValueAtTime(dt, at);
  g.echoB.delayTime.setValueAtTime(dt, at);
  g.level.gain.setValueAtTime(pieceGain(*piece_, mode_, volume_), at);
}

std::shared_ptr<MusicPlayer::MusicGraph> MusicPlayer::build() {
  const BaseAudioContext& ctx = ctx_;
  std::vector<AudioNode> nodes;
  auto keep = [&nodes](const auto& n) {
    nodes.push_back(n);
    return n;
  };
  const GainNode level = keep(ctx.createGain());
  level.gain.value(0);
  level.connect(dest_);
  // (a gentle low-pass takes the edge off every instrument at once)
  const BiquadFilterNode soft = keep(ctx.createBiquadFilter());
  soft.type("lowpass");
  soft.frequency.value(5200);
  soft.Q.value(-3);
  soft.connect(level);
  auto pan = [&](double x) -> AudioNode {
    if (!ctx.hasStereoPanner()) return soft;
    const StereoPannerNode p = keep(ctx.createStereoPanner());
    p.pan.value(x);
    p.connect(soft);
    return p;
  };
  // the echo: two delays bouncing left and right, each pass darker and weaker
  const GainNode send = keep(ctx.createGain());
  const DelayNode echoA = keep(ctx.createDelay(2));
  const DelayNode echoB = keep(ctx.createDelay(2));
  const BiquadFilterNode dark = keep(ctx.createBiquadFilter());
  dark.type("lowpass");
  dark.frequency.value(2200);
  const GainNode backA = keep(ctx.createGain());
  const GainNode backB = keep(ctx.createGain());
  backA.gain.value(0.38);
  backB.gain.value(0.38);
  echoA.delayTime.value(0.5);
  echoB.delayTime.value(0.5);
  send.connect(echoA);
  echoA.connect(dark);
  dark.connect(backA);
  backA.connect(echoB);
  echoB.connect(backB);
  backB.connect(echoA);
  dark.connect(pan(-0.6));
  echoB.connect(pan(0.6));
  std::array<GainNode, INSTRUMENT_COUNT> inst;
  for (Instrument i : VOICE_ORDER) {
    const GainNode bus = keep(ctx.createGain());
    bus.connect(pan(PAN(i)));
    if (SEND(i) > 0) {
      const GainNode s = keep(ctx.createGain());
      s.gain.value(SEND(i));
      bus.connect(s);
      s.connect(send);
    }
    inst[static_cast<size_t>(i)] = bus;
  }
  // one slow vibrato shared by every flute note
  const OscillatorNode vibrato = ctx.createOscillator();
  vibrato.frequency.value(5.1);
  const GainNode vibratoDepth = keep(ctx.createGain());
  vibratoDepth.gain.value(9);
  vibrato.connect(vibratoDepth);
  vibrato.start();
  nodes.push_back(vibrato);
  return std::make_shared<MusicGraph>(MusicGraph{level, inst, echoA, echoB, vibrato, vibratoDepth, std::move(nodes)});
}

void MusicPlayer::stop() {
  if (timer_ != 0) {
    timers::clearInterval(timer_);
    timer_ = 0;
  }
  piece_.reset();
  const std::shared_ptr<MusicGraph> g = std::move(g_);
  g_ = nullptr;
  if (!g) return;
  const double now = ctx_.currentTime();
  g->level.gain.cancelScheduledValues(now);
  g->level.gain.setTargetAtTime(0, now, 0.25);
  // (faded out, then taken apart: notes already scheduled die into nothing)
  timers::setTimeout(
      [g] {
        // (already stopped: the host passes over it)
        g->vibrato.stop();
        for (const AudioNode& n : g->nodes) n.disconnect();
      },
      1500);
}

void MusicPlayer::play(const MusicGraph& g, const MusicNote& n, double t) {
  const BaseAudioContext& ctx = ctx_;
  const Voice& vc = VOICES(n.i);
  const double f = midiHz(n.p);
  const double peak = vc.peak * n.v;
  const GainNode& out = g.inst[static_cast<size_t>(n.i)];
  const OscillatorNode o = ctx.createOscillator();
  o.type(vc.wave);
  o.frequency.value(f);
  const GainNode a = ctx.createGain();
  const AudioParam env = a.gain;
  env.setValueAtTime(0, t);
  double end;
  if (n.i == Instrument::Bell || n.i == Instrument::Marimba) {
    // FM: a modulator at 2× (the piano) or 4× (the wooden bar), its depth fading fast – a bright strike that mellows
    const double ratio = n.i == Instrument::Bell ? 2 : 4;
    const OscillatorNode m = ctx.createOscillator();
    m.frequency.value(f * ratio);
    const GainNode depth = ctx.createGain();
    const double dev = f * ratio * (n.i == Instrument::Bell ? 1.6 : 1.2);
    depth.gain.setValueAtTime(dev, t);
    depth.gain.setTargetAtTime(dev * 0.12, t, n.i == Instrument::Bell ? 0.25 : 0.03);
    m.connect(depth);
    depth.connect(o.frequency);
    env.linearRampToValueAtTime(peak, t + 0.004);
    env.setTargetAtTime(0, t + 0.004, vc.decay);
    end = t + vc.decay * 6;
    m.start(t);
    m.stop(end);
  } else if (n.i == Instrument::Pluck) {
    env.linearRampToValueAtTime(peak, t + 0.004);
    env.setTargetAtTime(0, t + 0.004, vc.decay);
    end = t + vc.decay * 6;
  } else if (n.i == Instrument::Flute) {
    env.linearRampToValueAtTime(peak, t + 0.07);
    env.setTargetAtTime(peak * 0.8, t + 0.07, 0.3);
    env.setTargetAtTime(0, t + n.d, vc.decay);
    end = t + n.d + vc.decay * 7;
    g.vibratoDepth.connect(o.detune);
    // (if the graph was taken apart already, the host passes over it)
    o.onended([vibratoDepth = g.vibratoDepth, detune = o.detune] { vibratoDepth.disconnect(detune); });
  } else if (n.i == Instrument::Pad) {
    // (a slow swell, a little detuned against its neighbours: a soft chorus)
    o.detune.value((rnd() - 0.5) * 14);
    env.linearRampToValueAtTime(peak, t + std::min(0.9, n.d / 2));
    env.setTargetAtTime(0, t + n.d, vc.decay);
    end = t + n.d + vc.decay * 7;
  } else {
    env.linearRampToValueAtTime(peak, t + 0.01);
    env.setTargetAtTime(peak * 0.55, t + 0.01, 0.5);
    env.setTargetAtTime(0, t + n.d, vc.decay);
    end = t + n.d + vc.decay * 7;
  }
  o.connect(a);
  a.connect(out);
  o.start(t);
  o.stop(end);
}

}  // namespace fs
