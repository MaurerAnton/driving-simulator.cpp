// SPDX-License-Identifier: AGPL-3.0-only
/**
 * audio.h on this machine: every context, node, buffer and wave is a record in a
 * table; the calls are written down in them (platform/native/audio_native.h is how a
 * test reads them).  The clock of a context follows plat::nowMs().  Nothing sounds by
 * itself: a host with loudspeakers (platform/sdl) asks mix() for the samples of the
 * graphs that were built, and plays them.
 */
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>

#include "platform/audio.h"
#include "platform/native/audio_native.h"
#include "platform/plat.h"

namespace fs {

namespace {

using audioNative::NodeInfo;
using audioNative::ParamEvent;
using audioNative::ParamInfo;

const char* const PARAM_NAMES[audioBackend::PCount] = {"gain", "frequency", "detune", "Q", "pan", "delayTime", "threshold", "knee", "ratio", "attack", "release", "playbackRate"};
const char* const KIND_NAMES[] = {"gain", "biquad", "osc", "source", "compressor", "delay", "panner"};
/** how many nodes that nothing names any more are kept for the tests to look at */
const size_t KEEP_RELEASED = 20000;
/** how many events a setting keeps */
const size_t KEEP_EVENTS = 200;

struct Rec {
  enum What { Context, Node, Buffer, Wave };
  What what;
  explicit Rec(What w) : what(w) {}
  virtual ~Rec() = default;
};

struct ContextRec : Rec {
  ContextRec() : Rec(Context) {}
  double sampleRate = 48000;
  bool running = true;
  /** the clock: its seconds when it last started or stopped, and plat::nowMs() of that moment */
  double base = 0, since = 0;
  std::map<std::string, int> made;
  int created = 0;
  long paramCalls = 0;
  int waves = 0, buffers = 0;
  int destination = 0;
  /** its nodes (their handles and ids), in the order they were made */
  std::deque<std::pair<int, int>> nodes;
  /** sources whose end came due while the clock stood (handle, id) */
  std::vector<std::pair<int, int>> waiting;
  /** the time of the next sample mix() makes (s; < 0: none made yet) */
  double mixTime = -1;
};

struct NodeRec : Rec {
  NodeRec() : Rec(Node) {}
  NodeInfo info;
  int ctx = 0;
  /** the handler of its 'ended' event (0: none), and which of the timers set for it counts */
  int endedListener = 0;
  long endSerial = 0;
  /** how long its buffer is (s; −1: it has none) */
  double bufferSeconds = -1;
  // --- what mix() needs ---
  /** the values it began with (a setting nothing was asked of has these) */
  std::array<double, audioBackend::PCount> defaults{};
  /** the samples of its buffer (shared with the buffer: it plays on when the buffer is dropped), their rate */
  std::shared_ptr<const std::vector<std::vector<float>>> samples;
  double samplesRate = 0;
  /** an oscillator's wave of its own: one period, sampled */
  std::shared_ptr<const std::vector<float>> table;
  /** createDelay's longest delay (s) */
  double maxDelay = 1;
  /** where it is: an oscillator's phase (0…1), a source's place in its buffer (samples; < 0: not begun) */
  double phase = 0, playhead = -1;
  /** a filter's memory (per channel: x1 x2 y1 y2), a compressor's gain (dB) */
  double biq[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
  double reduction = 0;
  /** a delay's memory (per channel) and where it writes next */
  std::vector<float> ring[2];
  size_t ringAt = 0;
};

struct BufferRec : Rec {
  BufferRec() : Rec(Buffer) {}
  int id = 0;
  int length = 0;
  double sampleRate = 0;
  /** (shared with the sources that play it) */
  std::shared_ptr<std::vector<std::vector<float>>> data;
};

struct WaveRec : Rec {
  WaveRec() : Rec(Wave) {}
  /** one period of the wave, sampled (mix()) */
  std::shared_ptr<const std::vector<float>> table;
};

struct Slot {
  int refs = 0;
  std::unique_ptr<Rec> rec;
};

struct Host {
  std::vector<Slot> slots = std::vector<Slot>(1);
  std::vector<int> freeSlots;
  /** the nodes nothing names any more (handle, id), the oldest first */
  std::deque<std::pair<int, int>> released;
  int nextNode = 1, nextBuffer = 1;
  int contexts = 0;
  bool supported = true, autoplay = true, panner = true;
  double sampleRate = 48000;
  // speech
  bool speech = true, paused = false, listening = false;
  std::vector<SpeechSynthesisVoice> voices;
  std::vector<audioNative::Spoken> spoken;
  /** the utterances handed over and not yet read: the first is being read */
  std::deque<int> reading;
};

Host& host() {
  static Host* h = new Host();
  return *h;
}

int put(std::unique_ptr<Rec> rec) {
  Host& h = host();
  int at;
  if (!h.freeSlots.empty()) {
    at = h.freeSlots.back();
    h.freeSlots.pop_back();
  } else {
    h.slots.emplace_back();
    at = static_cast<int>(h.slots.size()) - 1;
  }
  h.slots[static_cast<size_t>(at)].refs = 0;
  h.slots[static_cast<size_t>(at)].rec = std::move(rec);
  return at;
}

Rec* recOf(int handle, Rec::What what) {
  Host& h = host();
  if (handle <= 0 || handle >= static_cast<int>(h.slots.size())) return nullptr;
  Rec* r = h.slots[static_cast<size_t>(handle)].rec.get();
  return r && r->what == what ? r : nullptr;
}
ContextRec* ctxOf(int handle) { return static_cast<ContextRec*>(recOf(handle, Rec::Context)); }
NodeRec* nodeOf(int handle) { return static_cast<NodeRec*>(recOf(handle, Rec::Node)); }
BufferRec* bufferOf(int handle) { return static_cast<BufferRec*>(recOf(handle, Rec::Buffer)); }

double timeOf(const ContextRec& c) { return c.base + (c.running ? (plat::nowMs() - c.since) / 1000.0 : 0.0); }

int makeNode(int ctx, const char* kind) {
  auto n = std::make_unique<NodeRec>();
  n->info.id = host().nextNode++;
  n->info.kind = kind;
  n->ctx = ctx;
  int id = n->info.id;
  int h = put(std::move(n));
  if (ContextRec* c = ctxOf(ctx)) c->nodes.emplace_back(h, id);
  return h;
}

/** when a source will have ended (∞: never, as far as is known now) */
double endOf(const NodeRec& n) {
  const double never = std::numeric_limits<double>::infinity();
  const NodeInfo& i = n.info;
  double end = i.stopped >= 0 ? i.stopped : never;
  if (i.kind == "source" && i.started >= 0 && !i.loop && n.bufferSeconds >= 0) {
    double plays = n.bufferSeconds - i.offset;
    if (i.duration >= 0 && i.duration < plays) plays = i.duration;
    if (plays < 0) plays = 0;
    if (i.started + plays < end) end = i.started + plays;
  }
  return end;
}

void fireEnd(int handle, int id, long serial);

/** set a timer for the end of a source that has a handler waiting for it */
void scheduleEnd(int handle) {
  NodeRec* n = nodeOf(handle);
  if (!n || !n->endedListener) return;
  ContextRec* c = ctxOf(n->ctx);
  double end = endOf(*n);
  if (!c || end == std::numeric_limits<double>::infinity()) return;
  long serial = ++n->endSerial;
  int id = n->info.id;
  double ms = (end - timeOf(*c)) * 1000.0;
  plat::setTimeout([handle, id, serial] { fireEnd(handle, id, serial); }, ms > 0 ? ms : 0);
}

void fireEnd(int handle, int id, long serial) {
  NodeRec* n = nodeOf(handle);
  if (!n || n->info.id != id || n->endSerial != serial || !n->endedListener) return;
  ContextRec* c = ctxOf(n->ctx);
  if (!c) return;
  if (endOf(*n) > timeOf(*c) + 1e-9) {
    // (not yet: the clock stood in between, or was set back)
    if (c->running) scheduleEnd(handle);
    else c->waiting.emplace_back(handle, id);
    return;
  }
  int listener = n->endedListener;
  n->endedListener = 0;
  // (last: the handler may make and drop nodes)
  audioDetail::ended(listener);
}

/** forget the oldest of the nodes nothing names, beyond those that are kept */
void purge() {
  Host& h = host();
  while (h.released.size() > KEEP_RELEASED) {
    auto [handle, id] = h.released.front();
    h.released.pop_front();
    NodeRec* n = nodeOf(handle);
    if (!n || n->info.id != id) continue;
    int listener = n->endedListener;
    if (ContextRec* c = ctxOf(n->ctx)) {
      for (auto it = c->nodes.begin(); it != c->nodes.end(); ++it)
        if (it->first == handle && it->second == id) {
          c->nodes.erase(it);
          break;
        }
    }
    h.slots[static_cast<size_t>(handle)].rec.reset();
    h.freeSlots.push_back(handle);
    // (its end never came: the handler goes unheard – and with it what it held)
    if (listener) audioDetail::dropEnded(listener);
  }
}

}  // namespace

namespace audioBackend {

void retain(int h) {
  Host& host_ = host();
  if (h > 0 && h < static_cast<int>(host_.slots.size())) host_.slots[static_cast<size_t>(h)].refs++;
}

void release(int h) {
  Host& host_ = host();
  if (h <= 0 || h >= static_cast<int>(host_.slots.size())) return;
  Slot& s = host_.slots[static_cast<size_t>(h)];
  if (--s.refs > 0) return;
  s.refs = 0;
  if (!s.rec) return;
  switch (s.rec->what) {
    case Rec::Node: {
      NodeRec* n = static_cast<NodeRec*>(s.rec.get());
      // (the loudspeakers stay with their context)
      if (n->info.kind == "destination" || n->info.released) break;
      n->info.released = true;
      host_.released.emplace_back(h, n->info.id);
      purge();
      break;
    }
    case Rec::Buffer:
    case Rec::Wave:
      s.rec.reset();
      host_.freeSlots.push_back(h);
      break;
    case Rec::Context:
      // (kept: its nodes may still be named, and count on it)
      break;
  }
}

bool supported() { return host().supported; }

int createContext() {
  Host& h = host();
  if (!h.supported) return 0;
  auto c = std::make_unique<ContextRec>();
  c->sampleRate = h.sampleRate;
  c->running = h.autoplay;
  c->since = plat::nowMs();
  h.contexts++;
  int ctx = put(std::move(c));
  // (its loudspeakers stay with it, whether a value names them or not: see release())
  int dest = makeNode(ctx, "destination");
  ctxOf(ctx)->destination = dest;
  return ctx;
}

double contextNumber(int ctx, int what) {
  ContextRec* c = ctxOf(ctx);
  if (!c) return what == 2 ? 2 : 0;
  if (what == 0) return timeOf(*c);
  if (what == 1) return c->sampleRate;
  return c->running ? 1 : 0;
}

void contextCall(int ctx, int what) {
  ContextRec* c = ctxOf(ctx);
  if (!c) return;
  if (what == 0) {
    if (c->running || !host().autoplay) return;
    c->since = plat::nowMs();
    c->running = true;
    std::vector<std::pair<int, int>> waiting;
    waiting.swap(c->waiting);
    for (const auto& w : waiting) {
      NodeRec* n = nodeOf(w.first);
      if (n && n->info.id == w.second) scheduleEnd(w.first);
    }
  } else if (what == 1) {
    if (!c->running) return;
    c->base = timeOf(*c);
    c->running = false;
  }
}

int destination(int ctx) {
  ContextRec* c = ctxOf(ctx);
  return c ? c->destination : 0;
}

int createNode(int ctx, int kind, double a) {
  ContextRec* c = ctxOf(ctx);
  if (!c || kind < 0 || kind > KindPanner) return 0;
  if (kind == KindPanner && !host().panner) return 0;
  const char* name = KIND_NAMES[kind];
  c->made[name]++;
  c->created++;
  int h = makeNode(ctx, name);
  NodeInfo& i = nodeOf(h)->info;
  // (the values a browser's nodes begin with)
  switch (kind) {
    case KindGain:
      i.params[PGain].value = 1;
      break;
    case KindBiquad:
      i.type = "lowpass";
      i.params[PFrequency].value = 350;
      i.params[PQ].value = 1;
      break;
    case KindOscillator:
      i.type = "sine";
      i.params[PFrequency].value = 440;
      break;
    case KindBufferSource:
      i.params[PPlaybackRate].value = 1;
      break;
    case KindCompressor:
      i.params[PThreshold].value = -24;
      i.params[PKnee].value = 30;
      i.params[PRatio].value = 12;
      i.params[PAttack].value = 0.003;
      i.params[PRelease].value = 0.25;
      break;
    default:
      break;
  }
  NodeRec* rec = nodeOf(h);
  for (int p = 0; p < PCount; p++) rec->defaults[static_cast<size_t>(p)] = i.params[static_cast<size_t>(p)].value;
  if (kind == KindDelay && a > 0) rec->maxDelay = a;
  return h;
}

bool hasStereoPanner(int ctx) { return ctxOf(ctx) != nullptr && host().panner; }

int createBuffer(int ctx, int channels, int length, double sampleRate) {
  ContextRec* c = ctxOf(ctx);
  if (!c || channels < 1 || length < 1 || !(sampleRate > 0)) return 0;
  auto b = std::make_unique<BufferRec>();
  b->id = host().nextBuffer++;
  b->length = length;
  b->sampleRate = sampleRate;
  b->data = std::make_shared<std::vector<std::vector<float>>>(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(length), 0.0f));
  c->buffers++;
  return put(std::move(b));
}

void bufferWrite(int buffer, int channel, const float* data, int n, int start) {
  BufferRec* b = bufferOf(buffer);
  if (!b || channel < 0 || channel >= static_cast<int>(b->data->size()) || start < 0) return;
  std::vector<float>& d = (*b->data)[static_cast<size_t>(channel)];
  for (int i = 0; i < n && start + i < b->length; i++) d[static_cast<size_t>(start + i)] = data[i];
}

void bufferRead(int buffer, int channel, float* out, int n) {
  BufferRec* b = bufferOf(buffer);
  if (!b || channel < 0 || channel >= static_cast<int>(b->data->size())) return;
  const std::vector<float>& d = (*b->data)[static_cast<size_t>(channel)];
  for (int i = 0; i < n && i < b->length; i++) out[i] = d[static_cast<size_t>(i)];
}

int createWave(int ctx, const float* real, const float* imag, int n, bool disableNormalization) {
  ContextRec* c = ctxOf(ctx);
  if (!c || n < 2) return 0;
  c->waves++;
  // one period: the sum of the harmonics (the browser's PeriodicWave), the loudest sample at 1 unless asked otherwise
  const int N = 2048;
  auto table = std::make_shared<std::vector<float>>(static_cast<size_t>(N), 0.0f);
  double peak = 0;
  for (int k = 0; k < N; k++) {
    double x = 0;
    for (int m = 1; m < n; m++) {
      const double w = 2 * M_PI * m * k / N;
      x += real[m] * std::cos(w) + imag[m] * std::sin(w);
    }
    (*table)[static_cast<size_t>(k)] = static_cast<float>(x);
    peak = std::max(peak, std::abs(x));
  }
  if (!disableNormalization && peak > 0)
    for (float& v : *table) v = static_cast<float>(v / peak);
  auto w = std::make_unique<WaveRec>();
  w->table = std::move(table);
  return put(std::move(w));
}

void connect(int node, int destination, int param) {
  NodeRec* n = nodeOf(node);
  NodeRec* d = nodeOf(destination);
  if (!n || !d) return;
  if (param < 0) {
    if (!n->info.feeds(d->info.id)) n->info.outputs.push_back(d->info.id);
  } else if (param < PCount) {
    std::pair<int, std::string> to(d->info.id, PARAM_NAMES[param]);
    for (const auto& have : n->info.paramOutputs)
      if (have == to) return;
    n->info.paramOutputs.push_back(to);
  }
}

void disconnect(int node, int destination, int param) {
  NodeRec* n = nodeOf(node);
  if (!n) return;
  if (!destination) {
    n->info.outputs.clear();
    n->info.paramOutputs.clear();
    return;
  }
  NodeRec* d = nodeOf(destination);
  if (!d) return;
  int id = d->info.id;
  if (param < 0) {
    std::erase(n->info.outputs, id);
  } else if (param < PCount) {
    std::erase(n->info.paramOutputs, std::pair<int, std::string>(id, PARAM_NAMES[param]));
  }
}

void param(int node, int which, int op, double value, double time, double timeConstant) {
  NodeRec* n = nodeOf(node);
  if (!n || which < 0 || which >= PCount || op < OpValue || op > OpCancel) return;
  static const char* const WHAT[] = {"value", "set", "linear", "exp", "target", "cancel"};
  ContextRec* c = ctxOf(n->ctx);
  ParamInfo& p = n->info.params[static_cast<size_t>(which)];
  if (op != OpCancel) p.value = value;
  if (op == OpValue) time = c ? timeOf(*c) : 0;
  else if (c) c->paramCalls++;
  ParamEvent e;
  e.what = WHAT[op];
  e.value = value;
  e.time = time;
  e.timeConstant = timeConstant;
  p.events.push_back(std::move(e));
  if (p.events.size() > KEEP_EVENTS) p.events.erase(p.events.begin());
}

double paramValue(int node, int which) {
  NodeRec* n = nodeOf(node);
  if (!n || which < 0 || which >= PCount) return 0;
  return n->info.params[static_cast<size_t>(which)].value;
}

void setType(int node, const char* type) {
  if (NodeRec* n = nodeOf(node)) n->info.type = type;
}

void setWave(int node, int wave) {
  NodeRec* n = nodeOf(node);
  WaveRec* w = static_cast<WaveRec*>(recOf(wave, Rec::Wave));
  if (!n || !w) return;
  n->table = w->table;
  n->info.wave = true;
  n->info.type = "custom";
}

void setBuffer(int node, int buffer) {
  NodeRec* n = nodeOf(node);
  if (!n) return;
  BufferRec* b = bufferOf(buffer);
  n->info.buffer = b ? b->id : 0;
  n->bufferSeconds = b && b->sampleRate > 0 ? b->length / b->sampleRate : -1;
  n->samples = b ? b->data : nullptr;
  n->samplesRate = b ? b->sampleRate : 0;
}

void setLoop(int node, bool on) {
  if (NodeRec* n = nodeOf(node)) n->info.loop = on;
}

void start(int node, double when, double offset, double duration) {
  NodeRec* n = nodeOf(node);
  // (a source starts once)
  if (!n || n->info.started >= 0) return;
  n->info.started = when;
  n->info.offset = offset;
  n->info.duration = duration;
  scheduleEnd(node);
}

void stop(int node, double when) {
  NodeRec* n = nodeOf(node);
  if (!n) return;
  n->info.stopped = when;
  scheduleEnd(node);
}

int onEnded(int node, int listener) {
  NodeRec* n = nodeOf(node);
  if (!n) return listener;   // (nothing to hang it on: the caller lets go of it again)
  int old = n->endedListener;
  n->endedListener = listener;
  scheduleEnd(node);
  return old;
}

// --- speech ----------------------------------------------------------------------

bool speechSupported() { return host().speech; }

void speechInit() { host().listening = true; }

std::vector<SpeechSynthesisVoice> voices() { return host().voices; }

void speak(int id, const std::string& text, const std::string& lang, const SpeechSynthesisVoice* voice, double rate, double pitch, double volume) {
  audioNative::Spoken s;
  s.text = text;
  s.lang = lang;
  if (voice) {
    s.voiceName = voice->name;
    s.voiceURI = voice->voiceURI;
  }
  s.rate = rate;
  s.pitch = pitch;
  s.volume = volume;
  host().spoken.push_back(std::move(s));
  host().reading.push_back(id);
}

void speechCancel() {
  std::deque<int> ids;
  ids.swap(host().reading);
  // (as browsers tell it, a moment later: the one being read was interrupted, those waiting cancelled)
  bool first = true;
  for (int id : ids) {
    const char* why = first ? "interrupted" : "canceled";
    first = false;
    plat::soon([id, why] { audioDetail::speechEvent(id, 1, why); });
  }
}

void speechResume() { host().paused = false; }

int speechFlags() {
  const Host& h = host();
  return (h.paused ? 1 : 0) | (!h.reading.empty() ? 2 : 0) | (h.reading.size() > 1 ? 4 : 0);
}

}  // namespace audioBackend

namespace audioNative {

int ParamInfo::count(const std::string& what) const {
  int n = 0;
  for (const ParamEvent& e : events)
    if (e.what == what) n++;
  return n;
}

const ParamEvent* ParamInfo::last(const std::string& what) const {
  for (size_t i = events.size(); i-- > 0;)
    if (events[i].what == what) return &events[i];
  return nullptr;
}

const ParamInfo& NodeInfo::param(const std::string& name) const {
  static const ParamInfo none;
  for (int i = 0; i < audioBackend::PCount; i++)
    if (name == PARAM_NAMES[i]) return params[static_cast<size_t>(i)];
  return none;
}

bool NodeInfo::feeds(int nodeId) const {
  for (int o : outputs)
    if (o == nodeId) return true;
  return false;
}

void setSupported(bool on) { host().supported = on; }
void setAutoplayAllowed(bool on) { host().autoplay = on; }
void setStereoPanner(bool on) { host().panner = on; }
void setSampleRate(double hz) { host().sampleRate = hz; }

int contextsMade() { return host().contexts; }

int created(const AudioContext& ctx) {
  ContextRec* c = ctxOf(ctx.handle());
  return c ? c->created : 0;
}

int made(const AudioContext& ctx, const std::string& kind) {
  ContextRec* c = ctxOf(ctx.handle());
  if (!c) return 0;
  auto it = c->made.find(kind);
  return it == c->made.end() ? 0 : it->second;
}

long paramCalls(const AudioContext& ctx) {
  ContextRec* c = ctxOf(ctx.handle());
  return c ? c->paramCalls : 0;
}

int waves(const AudioContext& ctx) {
  ContextRec* c = ctxOf(ctx.handle());
  return c ? c->waves : 0;
}

int buffers(const AudioContext& ctx) {
  ContextRec* c = ctxOf(ctx.handle());
  return c ? c->buffers : 0;
}

std::vector<const NodeInfo*> nodes(const AudioContext& ctx) {
  std::vector<const NodeInfo*> out;
  ContextRec* c = ctxOf(ctx.handle());
  if (!c) return out;
  for (const auto& [handle, id] : c->nodes) {
    NodeRec* n = nodeOf(handle);
    if (n && n->info.id == id) out.push_back(&n->info);
  }
  return out;
}

const NodeInfo* info(const AudioNode& node) {
  NodeRec* n = nodeOf(node.handle());
  return n ? &n->info : nullptr;
}

const ParamInfo* info(const AudioParam& param) {
  NodeRec* n = nodeOf(param.node());
  if (!n || param.which() < 0 || param.which() >= audioBackend::PCount) return nullptr;
  return &n->info.params[static_cast<size_t>(param.which())];
}

int alive() {
  int n = 0;
  for (const Slot& s : host().slots)
    if (s.rec && s.refs > 0) n++;
  return n;
}

void setSpeechSupported(bool on) { host().speech = on; }

void setVoices(std::vector<SpeechSynthesisVoice> voices, bool tell) {
  host().voices = std::move(voices);
  if (tell && host().listening) audioDetail::voicesChanged();
}

const std::vector<Spoken>& spoken() { return host().spoken; }

void forgetSpoken() { host().spoken.clear(); }

bool endSpeech() {
  Host& h = host();
  if (h.reading.empty()) return false;
  int id = h.reading.front();
  h.reading.pop_front();
  audioDetail::speechEvent(id, 0, std::string());
  return true;
}

bool failSpeech(const std::string& error) {
  Host& h = host();
  if (h.reading.empty()) return false;
  int id = h.reading.front();
  h.reading.pop_front();
  audioDetail::speechEvent(id, 1, error);
  return true;
}

void setSpeechPaused(bool on) { host().paused = on; }

}  // namespace audioNative

// --- the loudspeakers: what the graphs sound like (for a host that has some) ---------

namespace {

constexpr int BLOCK = 128;

/** what a node gives in one block: two channels (a mono node gives the same in both) */
struct Out {
  float l[BLOCK], r[BLOCK];
  bool silent = true, mono = true;
};

/** One setting's automation, put in order of time once per mix() (the events written down by param()). */
struct Timeline {
  struct Ev {
    int op;
    double value, time, tc;
    /** the value just after the event took effect (a target: where it begins to move from) */
    double v = 0;
  };
  std::vector<Ev> ev;
  double def = 0;

  static bool ramp(int op) { return op == audioBackend::OpLinearRamp || op == audioBackend::OpExponentialRamp; }

  /** the value at t of the events before `end` */
  double at(double t, size_t end) const {
    // the last event at or before t, and the one after it
    size_t lo = 0, hi = end;
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (ev[mid].time <= t) lo = mid + 1;
      else hi = mid;
    }
    if (lo == 0) return def;
    const Ev& a = ev[lo - 1];
    if (lo < end && ramp(ev[lo].op)) {
      const Ev& b = ev[lo];
      const double f = b.time > a.time ? (t - a.time) / (b.time - a.time) : 1;
      if (b.op == audioBackend::OpLinearRamp) return a.v + (b.value - a.v) * f;
      if (a.v != 0 && b.value != 0 && (a.v > 0) == (b.value > 0)) return a.v * std::pow(b.value / a.v, f);
      return a.v;
    }
    if (a.op == audioBackend::OpSetTargetAtTime) return a.tc > 0 ? a.value + (a.v - a.value) * std::exp(-(t - a.time) / a.tc) : a.value;
    return a.v;
  }
  double at(double t) const { return at(t, ev.size()); }

  void build(const ParamInfo& p, double initial) {
    static const std::map<std::string, int> OPS = {{"value", audioBackend::OpSetValueAtTime},   {"set", audioBackend::OpSetValueAtTime},
                                                   {"linear", audioBackend::OpLinearRamp},       {"exp", audioBackend::OpExponentialRamp},
                                                   {"target", audioBackend::OpSetTargetAtTime}, {"cancel", audioBackend::OpCancel}};
    def = initial;
    ev.clear();
    for (const ParamEvent& e : p.events) {
      auto it = OPS.find(e.what);
      if (it == OPS.end()) continue;
      if (it->second == audioBackend::OpCancel) {
        std::erase_if(ev, [&](const Ev& x) { return x.time >= e.time; });
        continue;
      }
      Ev x{it->second, e.value, e.time, e.timeConstant};
      auto at_ = std::upper_bound(ev.begin(), ev.end(), x.time, [](double t, const Ev& y) { return t < y.time; });
      ev.insert(at_, x);
    }
    for (size_t i = 0; i < ev.size(); i++) ev[i].v = ev[i].op == audioBackend::OpSetTargetAtTime ? at(ev[i].time, i) : ev[i].value;
    // (nothing was ever scheduled: what it was set to – the browser's default otherwise)
    if (ev.empty()) def = p.events.empty() ? initial : p.value;
  }
};

struct Mixer {
  ContextRec* c = nullptr;
  double rate = 48000;
  /** the time of the block being made, and its number */
  double t = 0;
  long block = 0;
  /** node id → handle; what feeds a node (handles), what moves a setting of it (setting, handle) */
  std::unordered_map<int, int> handleOf;
  std::unordered_map<int, std::vector<int>> inputs;
  std::unordered_map<int, std::vector<std::pair<int, int>>> paramInputs;
  /** the blocks made (handle → its block, and for which block number) */
  struct Made {
    long block = -1;
    bool busy = false;
    std::unique_ptr<Out> out = std::make_unique<Out>();
  };
  std::unordered_map<int, Made> made;
  std::unordered_map<long, Timeline> timelines;

  const Timeline& timeline(int handle, NodeRec& n, int which) {
    const long key = static_cast<long>(handle) * audioBackend::PCount + which;
    auto it = timelines.find(key);
    if (it != timelines.end()) return it->second;
    Timeline& tl = timelines[key];
    tl.build(n.info.params[static_cast<size_t>(which)], n.defaults[static_cast<size_t>(which)]);
    return tl;
  }

  /** a setting through the block: the automation at its start and end (linear between), and what nodes add to it */
  void param(int handle, NodeRec& n, int which, float* out) {
    const Timeline& tl = timeline(handle, n, which);
    const double a = tl.at(t), b = tl.at(t + BLOCK / rate);
    for (int i = 0; i < BLOCK; i++) out[i] = static_cast<float>(a + (b - a) * i / BLOCK);
    auto it = paramInputs.find(n.info.id);
    if (it == paramInputs.end()) return;
    for (const auto& [w, from] : it->second) {
      if (w != which) continue;
      const Out& o = pull(from);
      if (o.silent) continue;
      for (int i = 0; i < BLOCK; i++) out[i] += 0.5f * (o.l[i] + o.r[i]);
    }
  }
  /** a setting at the start of the block only (what a filter's coefficients are made of) */
  double param0(int handle, NodeRec& n, int which) {
    float v[BLOCK];
    param(handle, n, which, v);
    return v[0];
  }

  /** the sum of what feeds a node */
  void sum(int id, Out& in) {
    in.silent = true;
    in.mono = true;
    auto it = inputs.find(id);
    if (it == inputs.end()) return;
    for (int from : it->second) {
      const Out& o = pull(from);
      if (o.silent) continue;
      if (in.silent) {
        std::memcpy(in.l, o.l, sizeof in.l);
        std::memcpy(in.r, o.r, sizeof in.r);
        in.silent = false;
        in.mono = o.mono;
        continue;
      }
      for (int i = 0; i < BLOCK; i++) in.l[i] += o.l[i], in.r[i] += o.r[i];
      in.mono = in.mono && o.mono;
    }
  }

  /** what a node gives in this block */
  const Out& pull(int handle) {
    static const Out quiet{};
    Made& m = made[handle];
    if (m.block == block) return *m.out;
    // (a cycle without a delay: the browser plays it silent)
    if (m.busy) return quiet;
    NodeRec* n = nodeOf(handle);
    if (!n) return quiet;
    m.busy = true;
    Out* o = m.out.get();
    // (a delay gives what it was fed before: its output first, so that a cycle through it closes)
    if (n->info.kind == "delay") {
      delayOut(handle, *n, *o);
      m.block = block;
      Out in;
      sum(n->info.id, in);
      delayIn(*n, in);
    } else {
      make(handle, *n, *o);
      m.block = block;
    }
    m.busy = false;
    return *o;
  }

  void make(int handle, NodeRec& n, Out& o) {
    const std::string& kind = n.info.kind;
    if (kind == "osc") return oscillator(handle, n, o);
    if (kind == "source") return source(handle, n, o);
    sum(n.info.id, o);
    if (o.silent || kind == "destination") return;
    if (kind == "gain") {
      float g[BLOCK];
      param(handle, n, audioBackend::PGain, g);
      for (int i = 0; i < BLOCK; i++) o.l[i] *= g[i], o.r[i] *= g[i];
    } else if (kind == "biquad") biquad(handle, n, o);
    else if (kind == "panner") panner(handle, n, o);
    else if (kind == "compressor") compressor(handle, n, o);
  }

  /** is the source playing at sample i of this block (started, not stopped)? */
  bool playing(const NodeInfo& i, double at) const { return i.started >= 0 && at >= i.started && (i.stopped < 0 || at < i.stopped || i.stopped < i.started); }

  void oscillator(int handle, NodeRec& n, Out& o) {
    o.mono = true;
    o.silent = true;
    if (n.info.started < 0) return;
    float f[BLOCK], d[BLOCK];
    param(handle, n, audioBackend::PFrequency, f);
    param(handle, n, audioBackend::PDetune, d);
    const std::string& type = n.info.type;
    for (int i = 0; i < BLOCK; i++) {
      float v = 0;
      if (playing(n.info, t + i / rate)) {
        const double p = n.phase;
        if (type == "custom" && n.table) {
          const std::vector<float>& tb = *n.table;
          const double x = p * static_cast<double>(tb.size());
          const size_t k = static_cast<size_t>(x) % tb.size();
          const double fr = x - std::floor(x);
          v = static_cast<float>(tb[k] + (tb[(k + 1) % tb.size()] - tb[k]) * fr);
        } else if (type == "square") v = p < 0.5 ? 1.0f : -1.0f;
        else if (type == "sawtooth") v = static_cast<float>(2 * (p - std::floor(p + 0.5)));
        else if (type == "triangle") v = static_cast<float>(p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4);
        else v = static_cast<float>(std::sin(2 * M_PI * p));
        n.phase += f[i] * std::exp2(d[i] / 1200.0) / rate;
        n.phase -= std::floor(n.phase);
        o.silent = false;
      }
      o.l[i] = o.r[i] = v;
    }
  }

  void source(int handle, NodeRec& n, Out& o) {
    o.silent = true;
    if (n.info.started < 0 || !n.samples || n.samples->empty() || n.samplesRate <= 0) return;
    const std::vector<std::vector<float>>& ch = *n.samples;
    const std::vector<float>& L = ch[0];
    const std::vector<float>& R = ch.size() > 1 ? ch[1] : ch[0];
    o.mono = ch.size() == 1;
    const double len = static_cast<double>(L.size());
    float pr[BLOCK], d[BLOCK];
    param(handle, n, audioBackend::PPlaybackRate, pr);
    param(handle, n, audioBackend::PDetune, d);
    const double endAt = endOf(n);
    for (int i = 0; i < BLOCK; i++) {
      const double at = t + i / rate;
      float l = 0, r = 0;
      if (at >= n.info.started && at < endAt) {
        if (n.playhead < 0) n.playhead = n.info.offset * n.samplesRate + (at - n.info.started) * n.samplesRate;
        double x = n.playhead;
        if (n.info.loop && len > 0) x = std::fmod(x, len);
        if (x >= 0 && x < len) {
          const size_t k = static_cast<size_t>(x);
          const double fr = x - static_cast<double>(k);
          const size_t k1 = k + 1 < L.size() ? k + 1 : (n.info.loop ? 0 : k);
          l = static_cast<float>(L[k] + (L[k1] - L[k]) * fr);
          r = static_cast<float>(R[k] + (R[k1] - R[k]) * fr);
          o.silent = false;
        }
        n.playhead += pr[i] * std::exp2(d[i] / 1200.0) * n.samplesRate / rate;
        if (n.info.loop && n.playhead >= len && len > 0) n.playhead = std::fmod(n.playhead, len);
      }
      o.l[i] = l;
      o.r[i] = r;
    }
  }

  void biquad(int handle, NodeRec& n, Out& o) {
    // (the Audio EQ Cookbook, with the Web Audio API's readings of Q and gain)
    const double f = param0(handle, n, audioBackend::PFrequency) * std::exp2(param0(handle, n, audioBackend::PDetune) / 1200.0);
    const double Q = param0(handle, n, audioBackend::PQ), G = param0(handle, n, audioBackend::PGain);
    const double w0 = 2 * M_PI * std::clamp(f, 1.0, rate / 2 - 1) / rate, cw = std::cos(w0), sw = std::sin(w0);
    const double A = std::pow(10.0, G / 40);
    const std::string& type = n.info.type;
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    if (type == "lowpass" || type == "highpass") {
      const double al = sw / (2 * std::pow(10.0, Q / 20));
      const double s = type == "lowpass" ? 1 - cw : 1 + cw;
      b0 = s / 2, b1 = type == "lowpass" ? s : -s, b2 = s / 2, a0 = 1 + al, a1 = -2 * cw, a2 = 1 - al;
    } else if (type == "lowshelf" || type == "highshelf") {
      const double al = sw / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
      if (type == "lowshelf") {
        b0 = A * ((A + 1) - (A - 1) * cw + sa), b1 = 2 * A * ((A - 1) - (A + 1) * cw), b2 = A * ((A + 1) - (A - 1) * cw - sa);
        a0 = (A + 1) + (A - 1) * cw + sa, a1 = -2 * ((A - 1) + (A + 1) * cw), a2 = (A + 1) + (A - 1) * cw - sa;
      } else {
        b0 = A * ((A + 1) + (A - 1) * cw + sa), b1 = -2 * A * ((A - 1) + (A + 1) * cw), b2 = A * ((A + 1) + (A - 1) * cw - sa);
        a0 = (A + 1) - (A - 1) * cw + sa, a1 = 2 * ((A - 1) - (A + 1) * cw), a2 = (A + 1) - (A - 1) * cw - sa;
      }
    } else {
      const double al = sw / (2 * std::max(Q, 1e-4));
      if (type == "bandpass") b0 = al, b1 = 0, b2 = -al, a0 = 1 + al, a1 = -2 * cw, a2 = 1 - al;
      else if (type == "notch") b0 = 1, b1 = -2 * cw, b2 = 1, a0 = 1 + al, a1 = -2 * cw, a2 = 1 - al;
      else if (type == "allpass") b0 = 1 - al, b1 = -2 * cw, b2 = 1 + al, a0 = 1 + al, a1 = -2 * cw, a2 = 1 - al;
      else if (type == "peaking") b0 = 1 + al * A, b1 = -2 * cw, b2 = 1 - al * A, a0 = 1 + al / A, a1 = -2 * cw, a2 = 1 - al / A;
    }
    b0 /= a0, b1 /= a0, b2 /= a0, a1 /= a0, a2 /= a0;
    float* chans[2] = {o.l, o.r};
    for (int c_ = 0; c_ < 2; c_++) {
      double* m = n.biq[c_];
      float* x = chans[c_];
      for (int i = 0; i < BLOCK; i++) {
        const double y = b0 * x[i] + b1 * m[0] + b2 * m[1] - a1 * m[2] - a2 * m[3];
        m[1] = m[0], m[0] = x[i], m[3] = m[2], m[2] = std::abs(y) < 1e-30 ? 0 : y;
        x[i] = static_cast<float>(y);
      }
    }
  }

  void panner(int handle, NodeRec& n, Out& o) {
    float p[BLOCK];
    param(handle, n, audioBackend::PPan, p);
    for (int i = 0; i < BLOCK; i++) {
      const double pan = std::clamp(static_cast<double>(p[i]), -1.0, 1.0);
      if (o.mono) {
        const double x = (pan + 1) / 2, m = o.l[i];
        o.l[i] = static_cast<float>(m * std::cos(x * M_PI / 2));
        o.r[i] = static_cast<float>(m * std::sin(x * M_PI / 2));
      } else if (pan <= 0) {
        const double x = pan + 1, l = o.l[i], r = o.r[i];
        o.l[i] = static_cast<float>(l + r * std::cos(x * M_PI / 2));
        o.r[i] = static_cast<float>(r * std::sin(x * M_PI / 2));
      } else {
        const double l = o.l[i], r = o.r[i];
        o.l[i] = static_cast<float>(l * std::cos(pan * M_PI / 2));
        o.r[i] = static_cast<float>(r + l * std::sin(pan * M_PI / 2));
      }
    }
    o.mono = false;
  }

  void compressor(int handle, NodeRec& n, Out& o) {
    // (a plain one: the level over the threshold, softened by the knee, divided by the ratio; no make-up gain)
    const double th = param0(handle, n, audioBackend::PThreshold), knee = std::max(0.0, param0(handle, n, audioBackend::PKnee));
    const double ratio = std::max(1.0, param0(handle, n, audioBackend::PRatio));
    const double att = std::exp(-1 / (std::max(1e-4, param0(handle, n, audioBackend::PAttack)) * rate));
    const double rel = std::exp(-1 / (std::max(1e-4, param0(handle, n, audioBackend::PRelease)) * rate));
    for (int i = 0; i < BLOCK; i++) {
      const double level = 20 * std::log10(std::max(1e-6, static_cast<double>(std::max(std::abs(o.l[i]), std::abs(o.r[i])))));
      const double over = level - th;
      double want = 0;
      if (over >= knee / 2) want = over * (1 - 1 / ratio);
      else if (over > -knee / 2 && knee > 0) want = (1 - 1 / ratio) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
      const double k = want > n.reduction ? att : rel;
      n.reduction = want + (n.reduction - want) * k;
      const float g = static_cast<float>(std::pow(10.0, -n.reduction / 20));
      o.l[i] *= g;
      o.r[i] *= g;
    }
  }

  void delayOut(int handle, NodeRec& n, Out& o) {
    const size_t size = static_cast<size_t>(n.maxDelay * rate) + 2 * BLOCK;
    if (n.ring[0].size() != size) n.ring[0].assign(size, 0.0f), n.ring[1].assign(size, 0.0f), n.ringAt = 0;
    float d[BLOCK];
    param(handle, n, audioBackend::PDelayTime, d);
    o.silent = false;
    o.mono = false;
    for (int i = 0; i < BLOCK; i++) {
      // (at least a block: the delay is what closes a cycle)
      const double back = std::clamp(d[i] * rate, static_cast<double>(BLOCK), static_cast<double>(size - BLOCK - 1));
      double x = static_cast<double>(n.ringAt + static_cast<size_t>(i) + size) - back;
      x = std::fmod(x, static_cast<double>(size));
      const size_t k = static_cast<size_t>(x), k1 = (k + 1) % size;
      const float fr = static_cast<float>(x - static_cast<double>(k));
      o.l[i] = n.ring[0][k] + (n.ring[0][k1] - n.ring[0][k]) * fr;
      o.r[i] = n.ring[1][k] + (n.ring[1][k1] - n.ring[1][k]) * fr;
    }
  }
  void delayIn(NodeRec& n, const Out& in) {
    const size_t size = n.ring[0].size();
    for (int i = 0; i < BLOCK; i++) {
      const size_t k = (n.ringAt + static_cast<size_t>(i)) % size;
      n.ring[0][k] = in.silent ? 0.0f : in.l[i];
      n.ring[1][k] = in.silent ? 0.0f : in.r[i];
    }
    n.ringAt = (n.ringAt + BLOCK) % size;
  }

  /** who feeds whom now (sources that have ended and that nothing names any more are left out) */
  void wire(double from) {
    handleOf.clear();
    inputs.clear();
    paramInputs.clear();
    timelines.clear();
    std::vector<int> live;
    for (const auto& [handle, id] : c->nodes) {
      NodeRec* n = nodeOf(handle);
      if (!n || n->info.id != id) continue;
      const bool source = n->info.kind == "osc" || n->info.kind == "source";
      if (source && n->info.released && endOf(*n) < from) continue;
      handleOf[id] = handle;
      live.push_back(handle);
    }
    for (int handle : live) {
      NodeRec* n = nodeOf(handle);
      for (int to : n->info.outputs) inputs[to].push_back(handle);
      for (const auto& [to, name] : n->info.paramOutputs)
        for (int p = 0; p < audioBackend::PCount; p++)
          if (name == PARAM_NAMES[p]) paramInputs[to].emplace_back(p, handle);
    }
    // what is no longer kept by anyone (and has no sound left in it) goes
    for (auto it = made.begin(); it != made.end();) {
      if (!handleOf.count(nodeOf(it->first) ? nodeOf(it->first)->info.id : -1)) it = made.erase(it);
      else ++it;
    }
  }
};

std::unordered_map<int, Mixer>& mixers() {
  static auto* m = new std::unordered_map<int, Mixer>();
  return *m;
}

}  // namespace

namespace audioNative {

void mix(float* out, int frames, double sampleRate) {
  std::fill(out, out + static_cast<size_t>(frames) * 2, 0.0f);
  Host& h = host();
  for (size_t handle = 1; handle < h.slots.size(); handle++) {
    ContextRec* c = ctxOf(static_cast<int>(handle));
    if (!c) continue;
    if (!c->running) continue;
    Mixer& m = mixers()[static_cast<int>(handle)];
    m.c = c;
    m.rate = sampleRate;
    // (the mixer's clock keeps near the context's: what was asked for now is heard now)
    const double now = timeOf(*c);
    if (c->mixTime < 0 || c->mixTime < now - 0.2 || c->mixTime > now + 0.5) c->mixTime = now;
    m.wire(c->mixTime);
    auto dest = m.handleOf.find(nodeOf(c->destination) ? nodeOf(c->destination)->info.id : -1);
    if (dest == m.handleOf.end()) continue;
    for (int done = 0; done < frames; done += BLOCK) {
      m.t = c->mixTime;
      m.block++;
      const Out& o = m.pull(dest->second);
      const int n = std::min(BLOCK, frames - done);
      if (!o.silent)
        for (int i = 0; i < n; i++) {
          out[(done + i) * 2] += o.l[i];
          out[(done + i) * 2 + 1] += o.r[i];
        }
      c->mixTime += BLOCK / sampleRate;
    }
  }
  for (int i = 0; i < frames * 2; i++) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

}  // namespace audioNative

}  // namespace fs
