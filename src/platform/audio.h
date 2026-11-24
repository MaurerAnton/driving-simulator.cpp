// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Sound: the part of the browser's Web Audio API that the game uses, by the same
 * names, so that the original's audio code translates line by line:
 *
 *     const ctx = new AudioContext();                 AudioContext ctx = AudioContext::create();
 *     const g = ctx.createGain();                     GainNode g = ctx.createGain();
 *     g.gain.value = 0.5;                             g.gain.value(0.5);
 *     g.gain.setTargetAtTime(0, t, 0.1);              g.gain.setTargetAtTime(0, t, 0.1);
 *     const o = ctx.createOscillator();               OscillatorNode o = ctx.createOscillator();
 *     o.type = 'sawtooth';                            o.type("sawtooth");
 *     o.connect(g).connect(ctx.destination);          o.connect(g).connect(ctx.destination());
 *     lfo.connect(o.frequency);                       lfo.connect(o.frequency);
 *     o.start(t); o.stop(t + 1);                      o.start(t); o.stop(t + 1);
 *     o.onended = () => g.disconnect();               o.onended([g] { g.disconnect(); });
 *
 * Properties are set with a call of their name (C++ has no setters).  A node, a
 * setting of a node, a buffer, a wave, a context: each is a reference to the
 * browser's object (copying it copies the reference, as in JavaScript; the empty
 * one is `null`).  When the last reference to a node is gone the host lets go of
 * it – what still sounds goes on sounding, as in a browser: a source that was
 * started plays to its end, and a node lives for as long as something feeds it.
 * Engine sounds make and drop nodes all the time; nothing has to be freed by hand.
 *
 * A call the browser refuses (a ramp to zero, a source started twice, a
 * connection that is not there to be undone) is ignored here: where the original
 * catches such an error, the translation has nothing to catch.
 *
 * A context only sounds after a tap or a key (the browsers' autoplay rule): make
 * it – or resume() it – from the handler of such an event, as the original does.
 *
 * Also here: the browser's speech synthesis (speechSynthesis and
 * SpeechSynthesisUtterance), as far as the spoken instructions use it.
 *
 * In the browser these are the real things (platform/web/library_audio.js);
 * natively nothing sounds – the host keeps what was built and asked for, and the
 * tests read it (platform/native/audio_native.h).
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fs {

class AudioNode;
class AudioContext;
struct SpeechSynthesisVoice;

/** What the two hosts implement.  Every object is named by a small number; 0 is "none". */
namespace audioBackend {

/** the kinds of node (the same numbers in platform/web/library_audio.js – keep the two lists alike) */
enum NodeKind : int { KindGain = 0, KindBiquad, KindOscillator, KindBufferSource, KindCompressor, KindDelay, KindPanner };
/** the settings of nodes, by number (the same list in library_audio.js: FSA.PARAMS) */
enum Param : int { PGain = 0, PFrequency, PDetune, PQ, PPan, PDelayTime, PThreshold, PKnee, PRatio, PAttack, PRelease, PPlaybackRate, PCount };
/** what is asked of a setting */
enum ParamOp : int { OpValue = 0, OpSetValueAtTime, OpLinearRamp, OpExponentialRamp, OpSetTargetAtTime, OpCancel };

void retain(int h);
void release(int h);

/** window.AudioContext ?? window.webkitAudioContext */
bool supported();
/** new AudioContext() (0: the browser would not) */
int createContext();
/** what: 0 currentTime, 1 sampleRate, 2 state (0 suspended, 1 running, 2 closed) */
double contextNumber(int ctx, int what);
/** what: 0 resume(), 1 suspend() */
void contextCall(int ctx, int what);
int destination(int ctx);
/** `a`: createDelay's longest delay (s) */
int createNode(int ctx, int kind, double a);
/** typeof ctx.createStereoPanner === 'function' */
bool hasStereoPanner(int ctx);
int createBuffer(int ctx, int channels, int length, double sampleRate);
void bufferWrite(int buffer, int channel, const float* data, int n, int start);
void bufferRead(int buffer, int channel, float* out, int n);
int createWave(int ctx, const float* real, const float* imag, int n, bool disableNormalization);
/** `param` −1: into the node `destination`; else into that setting of it */
void connect(int node, int destination, int param);
/** `destination` 0: from everything it feeds */
void disconnect(int node, int destination, int param);
void param(int node, int which, int op, double value, double time, double timeConstant);
double paramValue(int node, int which);
void setType(int node, const char* type);
void setWave(int node, int wave);
void setBuffer(int node, int buffer);
void setLoop(int node, bool on);
/** `duration` below 0: none given */
void start(int node, double when, double offset, double duration);
void stop(int node, double when);
/** node.onended = listener (0: null); returns the listener that was there (0: none) */
int onEnded(int node, int listener);

/** 'speechSynthesis' in window */
bool speechSupported();
/** from now on audioDetail::voicesChanged() is called when the browser's list of voices changes */
void speechInit();
std::vector<SpeechSynthesisVoice> voices();
/** speak a new utterance; its end or its failure comes back through audioDetail::speechEvent(id, …) */
void speak(int id, const std::string& text, const std::string& lang, const SpeechSynthesisVoice* voice, double rate, double pitch, double volume);
void speechCancel();
void speechResume();
/** 1 paused, 2 speaking, 4 pending */
int speechFlags();

}  // namespace audioBackend

namespace audioDetail {

/** A reference to an object of the host: the host lets go of the object when the last one is gone. */
class Ref {
 public:
  Ref() = default;
  explicit Ref(int h) : h_(h) {
    if (h_) audioBackend::retain(h_);
  }
  Ref(const Ref& o) : h_(o.h_) {
    if (h_) audioBackend::retain(h_);
  }
  Ref(Ref&& o) noexcept : h_(o.h_) { o.h_ = 0; }
  Ref& operator=(const Ref& o) {
    if (o.h_ != h_) {
      if (o.h_) audioBackend::retain(o.h_);
      if (h_) audioBackend::release(h_);
      h_ = o.h_;
    }
    return *this;
  }
  Ref& operator=(Ref&& o) noexcept {
    if (this != &o) {
      if (h_) audioBackend::release(h_);
      h_ = o.h_;
      o.h_ = 0;
    }
    return *this;
  }
  ~Ref() {
    if (h_) audioBackend::release(h_);
  }
  int h() const { return h_; }

 private:
  int h_ = 0;
};

/** keep a handler for a source's 'ended' event; the number the host is given for it */
int addEnded(std::function<void()> fn);
void dropEnded(int listener);
/** how many 'ended' handlers wait (tests, and the leak check) */
int endedCount();

// --- what a host calls when something happens ---
/** a source has ended: run its handler (once – it is forgotten then) */
void ended(int listener);
/** an utterance has been read to its end (kind 0) or has failed (kind 1, `error`: the event's word for why) */
void speechEvent(int id, int kind, const std::string& error);
/** the browser's list of voices has changed */
void voicesChanged();

}  // namespace audioDetail

// ---------------------------------------------------------------------------
// Settings, nodes
// ---------------------------------------------------------------------------

/** One setting of a node that can be moved in time: node.gain, node.frequency … */
class AudioParam {
 public:
  /** no setting (null) */
  AudioParam() = default;
  /** setting number `which` (audioBackend::Param) of the node the host named `node` */
  AudioParam(int node, int which) : node_(node), which_(which) {}
  explicit operator bool() const { return node_.h() != 0; }
  bool operator==(const AudioParam& o) const { return node_.h() == o.node_.h() && which_ == o.which_; }

  /** `param.value = v` */
  void value(double v) const { audioBackend::param(node_.h(), which_, audioBackend::OpValue, v, 0, 0); }
  /** `param.value` */
  double value() const { return audioBackend::paramValue(node_.h(), which_); }
  const AudioParam& setValueAtTime(double value, double startTime) const {
    audioBackend::param(node_.h(), which_, audioBackend::OpSetValueAtTime, value, startTime, 0);
    return *this;
  }
  const AudioParam& linearRampToValueAtTime(double value, double endTime) const {
    audioBackend::param(node_.h(), which_, audioBackend::OpLinearRamp, value, endTime, 0);
    return *this;
  }
  const AudioParam& exponentialRampToValueAtTime(double value, double endTime) const {
    audioBackend::param(node_.h(), which_, audioBackend::OpExponentialRamp, value, endTime, 0);
    return *this;
  }
  const AudioParam& setTargetAtTime(double target, double startTime, double timeConstant) const {
    audioBackend::param(node_.h(), which_, audioBackend::OpSetTargetAtTime, target, startTime, timeConstant);
    return *this;
  }
  const AudioParam& cancelScheduledValues(double cancelTime) const {
    audioBackend::param(node_.h(), which_, audioBackend::OpCancel, 0, cancelTime, 0);
    return *this;
  }

  /** the node it is a setting of, and which one (for the hosts) */
  int node() const { return node_.h(); }
  int which() const { return which_; }

 private:
  audioDetail::Ref node_;
  int which_ = 0;
};

class AudioNode {
 public:
  /** no node (null) */
  AudioNode() = default;
  /** the node the host named `handle` */
  explicit AudioNode(int handle) : ref_(handle) {}
  int handle() const { return ref_.h(); }
  /** is there a node (`if (node)`, `node !== null`)? */
  explicit operator bool() const { return ref_.h() != 0; }
  /** the same node? */
  bool operator==(const AudioNode& o) const { return ref_.h() == o.ref_.h(); }

  /** a.connect(b) – it answers with b, so that a.connect(b).connect(c) reads as in the original */
  template <class Node>
    requires std::is_base_of_v<AudioNode, Node>
  const Node& connect(const Node& destination) const {
    audioBackend::connect(ref_.h(), destination.handle(), -1);
    return destination;
  }
  /** a.connect(b.frequency): a moves a setting of b */
  void connect(const AudioParam& destination) const { audioBackend::connect(ref_.h(), destination.node(), destination.which()); }
  /** from everything it feeds; from one node; from one setting */
  void disconnect() const { audioBackend::disconnect(ref_.h(), 0, -1); }
  void disconnect(const AudioNode& destination) const { audioBackend::disconnect(ref_.h(), destination.handle(), -1); }
  void disconnect(const AudioParam& destination) const { audioBackend::disconnect(ref_.h(), destination.node(), destination.which()); }

 private:
  audioDetail::Ref ref_;
};

class GainNode : public AudioNode {
 public:
  GainNode() = default;
  explicit GainNode(int handle) : AudioNode(handle), gain(handle, audioBackend::PGain) {}
  AudioParam gain;
};

class BiquadFilterNode : public AudioNode {
 public:
  BiquadFilterNode() = default;
  explicit BiquadFilterNode(int handle)
      : AudioNode(handle), frequency(handle, audioBackend::PFrequency), detune(handle, audioBackend::PDetune), Q(handle, audioBackend::PQ), gain(handle, audioBackend::PGain) {}
  AudioParam frequency, detune, Q, gain;
  /** `filter.type = …`: 'lowpass' | 'highpass' | 'bandpass' | 'lowshelf' | 'highshelf' | 'peaking' | 'notch' | 'allpass' */
  void type(const std::string& v) const { audioBackend::setType(handle(), v.c_str()); }
};

class DynamicsCompressorNode : public AudioNode {
 public:
  DynamicsCompressorNode() = default;
  explicit DynamicsCompressorNode(int handle)
      : AudioNode(handle),
        threshold(handle, audioBackend::PThreshold),
        knee(handle, audioBackend::PKnee),
        ratio(handle, audioBackend::PRatio),
        attack(handle, audioBackend::PAttack),
        release(handle, audioBackend::PRelease) {}
  AudioParam threshold, knee, ratio, attack, release;
};

class DelayNode : public AudioNode {
 public:
  DelayNode() = default;
  explicit DelayNode(int handle) : AudioNode(handle), delayTime(handle, audioBackend::PDelayTime) {}
  AudioParam delayTime;
};

class StereoPannerNode : public AudioNode {
 public:
  StereoPannerNode() = default;
  explicit StereoPannerNode(int handle) : AudioNode(handle), pan(handle, audioBackend::PPan) {}
  AudioParam pan;
};

/** `ctx.createBuffer(…)`: samples to be played by an AudioBufferSourceNode */
class AudioBuffer {
 public:
  AudioBuffer() = default;
  AudioBuffer(int handle, int numberOfChannels, int length, double sampleRate) : ref_(handle), channels_(numberOfChannels), length_(length), sampleRate_(sampleRate) {}
  int handle() const { return ref_.h(); }
  explicit operator bool() const { return ref_.h() != 0; }
  bool operator==(const AudioBuffer& o) const { return ref_.h() == o.ref_.h(); }
  /** samples per channel */
  int length() const { return length_; }
  int numberOfChannels() const { return channels_; }
  double sampleRate() const { return sampleRate_; }
  /** seconds */
  double duration() const { return sampleRate_ > 0 ? length_ / sampleRate_ : 0; }
  /**
   * The samples of a channel – a copy (in JavaScript `getChannelData` hands out the buffer's own
   * memory, which is then written into; here the samples are made in a vector and handed
   * over with copyToChannel).
   */
  std::vector<float> getChannelData(int channel) const;
  /** buffer.copyToChannel(source, channel, startInChannel) */
  void copyToChannel(const std::vector<float>& source, int channel, int startInChannel = 0) const;

 private:
  audioDetail::Ref ref_;
  int channels_ = 0, length_ = 0;
  double sampleRate_ = 0;
};

/** `ctx.createPeriodicWave(real, imag)`: one period of a wave, as its partials */
class PeriodicWave {
 public:
  PeriodicWave() = default;
  explicit PeriodicWave(int handle) : ref_(handle) {}
  int handle() const { return ref_.h(); }
  explicit operator bool() const { return ref_.h() != 0; }

 private:
  audioDetail::Ref ref_;
};

/** createPeriodicWave's third argument */
struct PeriodicWaveConstraints {
  bool disableNormalization = false;
};

/** What an oscillator and a buffer source have in common: they are started and stopped, and tell when they have ended. */
class AudioScheduledSourceNode : public AudioNode {
 public:
  AudioScheduledSourceNode() = default;
  explicit AudioScheduledSourceNode(int handle) : AudioNode(handle) {}
  /** stop(when): at that time of the context's clock (0: now) */
  void stop(double when = 0) const { audioBackend::stop(handle(), when); }
  /**
   * `node.onended = fn` (an empty fn is `= null`): called once, when the source has stopped.
   * Capture what it needs BY VALUE (nodes, numbers) – it runs long after the caller has returned.
   */
  void onended(std::function<void()> fn) const;
};

class OscillatorNode : public AudioScheduledSourceNode {
 public:
  OscillatorNode() = default;
  explicit OscillatorNode(int handle) : AudioScheduledSourceNode(handle), frequency(handle, audioBackend::PFrequency), detune(handle, audioBackend::PDetune) {}
  AudioParam frequency, detune;
  /** `osc.type = …`: 'sine' | 'square' | 'sawtooth' | 'triangle' */
  void type(const std::string& v) const { audioBackend::setType(handle(), v.c_str()); }
  void setPeriodicWave(const PeriodicWave& wave) const { audioBackend::setWave(handle(), wave.handle()); }
  void start(double when = 0) const { audioBackend::start(handle(), when, 0, -1); }
};

class AudioBufferSourceNode : public AudioScheduledSourceNode {
 public:
  AudioBufferSourceNode() = default;
  explicit AudioBufferSourceNode(int handle)
      : AudioScheduledSourceNode(handle), playbackRate(handle, audioBackend::PPlaybackRate), detune(handle, audioBackend::PDetune) {}
  AudioParam playbackRate, detune;
  /** `source.buffer = b` */
  void buffer(const AudioBuffer& b) const { audioBackend::setBuffer(handle(), b.handle()); }
  /** `source.loop = on` */
  void loop(bool on) const { audioBackend::setLoop(handle(), on); }
  /** start(when, offset): from `offset` seconds into the buffer, to its end (or round and round) */
  void start(double when = 0, double offset = 0) const { audioBackend::start(handle(), when, offset, -1); }
  /** start(when, offset, duration): for `duration` seconds at most */
  void start(double when, double offset, double duration) const { audioBackend::start(handle(), when, offset, duration < 0 ? 0 : duration); }
};

// ---------------------------------------------------------------------------
// The context
// ---------------------------------------------------------------------------

class AudioContext {
 public:
  /** no context (null) */
  AudioContext() = default;
  explicit AudioContext(int handle) : ref_(handle) {}
  /** is there a Web Audio API here at all (`window.AudioContext ?? window.webkitAudioContext`)? */
  static bool supported();
  /** `new AudioContext()` – the empty one where there is none.  From a tap or a key, or it stays 'suspended'. */
  static AudioContext create();

  int handle() const { return ref_.h(); }
  explicit operator bool() const { return ref_.h() != 0; }
  bool operator==(const AudioContext& o) const { return ref_.h() == o.ref_.h(); }

  /** seconds on the context's own clock, which everything is scheduled on */
  double currentTime() const { return audioBackend::contextNumber(ref_.h(), 0); }
  double sampleRate() const { return audioBackend::contextNumber(ref_.h(), 1); }
  /** 'suspended' | 'running' | 'closed' */
  std::string state() const;
  /** (the original does not wait for the promise these answer with: neither can it be waited for here) */
  void resume() const { audioBackend::contextCall(ref_.h(), 0); }
  void suspend() const { audioBackend::contextCall(ref_.h(), 1); }
  /** `ctx.destination`: the loudspeakers */
  AudioNode destination() const { return AudioNode(audioBackend::destination(ref_.h())); }

  GainNode createGain() const { return GainNode(audioBackend::createNode(ref_.h(), audioBackend::KindGain, 0)); }
  BiquadFilterNode createBiquadFilter() const { return BiquadFilterNode(audioBackend::createNode(ref_.h(), audioBackend::KindBiquad, 0)); }
  OscillatorNode createOscillator() const { return OscillatorNode(audioBackend::createNode(ref_.h(), audioBackend::KindOscillator, 0)); }
  AudioBufferSourceNode createBufferSource() const { return AudioBufferSourceNode(audioBackend::createNode(ref_.h(), audioBackend::KindBufferSource, 0)); }
  DynamicsCompressorNode createDynamicsCompressor() const { return DynamicsCompressorNode(audioBackend::createNode(ref_.h(), audioBackend::KindCompressor, 0)); }
  /** createDelay(maxDelayTime): the longest delay it can be set to (s) */
  DelayNode createDelay(double maxDelayTime = 1) const { return DelayNode(audioBackend::createNode(ref_.h(), audioBackend::KindDelay, maxDelayTime)); }
  /** `typeof ctx.createStereoPanner === 'function'` (old Safari has none) */
  bool hasStereoPanner() const { return audioBackend::hasStereoPanner(ref_.h()); }
  StereoPannerNode createStereoPanner() const { return StereoPannerNode(audioBackend::createNode(ref_.h(), audioBackend::KindPanner, 0)); }
  /** createBuffer(numberOfChannels, length, sampleRate): silent until samples are copied into it */
  AudioBuffer createBuffer(int numberOfChannels, int length, double sampleRate) const;
  /** createPeriodicWave(real, imag, { disableNormalization }) – `real` and `imag` equally long, index 0 the mean */
  PeriodicWave createPeriodicWave(const std::vector<float>& real, const std::vector<float>& imag, PeriodicWaveConstraints constraints = PeriodicWaveConstraints()) const;

 private:
  audioDetail::Ref ref_;
};

/** (the original's music takes a BaseAudioContext: here there is only the one kind) */
using BaseAudioContext = AudioContext;

// ---------------------------------------------------------------------------
// Speech
// ---------------------------------------------------------------------------

/** One of the browser's voices (speechSynthesis.getVoices()). */
struct SpeechSynthesisVoice {
  std::string name;
  /** the language it reads, as the browser writes it ("de-DE", "pt_PT") */
  std::string lang;
  std::string voiceURI;
  /** on the device (false: over the network) */
  bool localService = false;
  /** `voice.default` (a word C++ keeps for itself) */
  bool default_ = false;

  bool operator==(const SpeechSynthesisVoice& o) const = default;
};

/** What an utterance's 'error' event says. */
struct SpeechSynthesisErrorEvent {
  /** 'interrupted', 'canceled', 'network', 'synthesis-failed', 'not-allowed' … */
  std::string error;
};

/**
 * `new SpeechSynthesisUtterance(text)`: a text to be read out, once.  A reference, as a
 * node is: two are equal when they are the same utterance.
 */
class SpeechSynthesisUtterance {
 public:
  /** no utterance (null) */
  SpeechSynthesisUtterance() = default;
  explicit SpeechSynthesisUtterance(const std::string& text);
  explicit operator bool() const { return static_cast<bool>(s_); }
  bool operator==(const SpeechSynthesisUtterance& o) const { return s_ == o.s_; }

  const std::string& text() const;
  /** `u.lang = …` and the others */
  void lang(const std::string& v) const;
  void voice(const SpeechSynthesisVoice& v) const;
  void rate(double v) const;
  void pitch(double v) const;
  void volume(double v) const;
  /** `u.onend = fn`, `u.onerror = fn` – one of the two is called, once, and both are let go then */
  void onend(std::function<void()> fn) const;
  void onerror(std::function<void(const SpeechSynthesisErrorEvent&)> fn) const;

  /** (what is kept of it: audio_common.cpp) */
  struct State;

 private:
  friend class SpeechSynthesis;
  std::shared_ptr<State> s_;
};

/** `window.speechSynthesis` */
class SpeechSynthesis {
 public:
  std::vector<SpeechSynthesisVoice> getVoices() const { return audioBackend::voices(); }
  /** (an utterance that is being read is held here until it has ended: a browser may forget what nothing holds) */
  void speak(const SpeechSynthesisUtterance& u) const;
  void cancel() const { audioBackend::speechCancel(); }
  void resume() const { audioBackend::speechResume(); }
  bool paused() const { return (audioBackend::speechFlags() & 1) != 0; }
  bool speaking() const { return (audioBackend::speechFlags() & 2) != 0; }
  bool pending() const { return (audioBackend::speechFlags() & 4) != 0; }
  /** addEventListener('voiceschanged', fn) – the only event there is; returns what removeEventListener takes */
  int addEventListener(const std::string& type, std::function<void()> fn) const;
  void removeEventListener(const std::string& type, int listener) const;
};

/** `'speechSynthesis' in window` */
bool hasSpeechSynthesis();
/** `window.speechSynthesis` */
SpeechSynthesis speechSynthesis();

}  // namespace fs
