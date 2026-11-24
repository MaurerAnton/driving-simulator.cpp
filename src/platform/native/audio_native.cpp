// SPDX-License-Identifier: AGPL-3.0-only
/**
 * audio.h on this machine: nothing sounds.  Every context, node, buffer and wave is a
 * record in a table; the calls are written down in them (platform/native/audio_native.h
 * is how a test reads them).  The clock of a context follows plat::nowMs().
 */
#include <deque>
#include <limits>
#include <map>
#include <memory>

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
};

struct BufferRec : Rec {
  BufferRec() : Rec(Buffer) {}
  int id = 0;
  int length = 0;
  double sampleRate = 0;
  std::vector<std::vector<float>> data;
};

struct WaveRec : Rec {
  WaveRec() : Rec(Wave) {}
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
  int dest = makeNode(ctx, "destination");
  // (the context holds its loudspeakers)
  host().slots[static_cast<size_t>(dest)].refs = 1;
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

int createNode(int ctx, int kind, double) {
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
  b->data.assign(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(length), 0.0f));
  c->buffers++;
  return put(std::move(b));
}

void bufferWrite(int buffer, int channel, const float* data, int n, int start) {
  BufferRec* b = bufferOf(buffer);
  if (!b || channel < 0 || channel >= static_cast<int>(b->data.size()) || start < 0) return;
  std::vector<float>& d = b->data[static_cast<size_t>(channel)];
  for (int i = 0; i < n && start + i < b->length; i++) d[static_cast<size_t>(start + i)] = data[i];
}

void bufferRead(int buffer, int channel, float* out, int n) {
  BufferRec* b = bufferOf(buffer);
  if (!b || channel < 0 || channel >= static_cast<int>(b->data.size())) return;
  const std::vector<float>& d = b->data[static_cast<size_t>(channel)];
  for (int i = 0; i < n && i < b->length; i++) out[i] = d[static_cast<size_t>(i)];
}

int createWave(int ctx, const float*, const float*, int n, bool) {
  ContextRec* c = ctxOf(ctx);
  if (!c || n < 2) return 0;
  c->waves++;
  return put(std::make_unique<WaveRec>());
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
  if (!n || !recOf(wave, Rec::Wave)) return;
  n->info.wave = true;
  n->info.type = "custom";
}

void setBuffer(int node, int buffer) {
  NodeRec* n = nodeOf(node);
  if (!n) return;
  BufferRec* b = bufferOf(buffer);
  n->info.buffer = b ? b->id : 0;
  n->bufferSeconds = b && b->sampleRate > 0 ? b->length / b->sampleRate : -1;
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

}  // namespace fs
