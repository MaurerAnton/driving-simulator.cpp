// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The sound of the native host (platform/audio.h on this machine): nothing is
 * heard.  The host keeps what was built – the nodes of every context, how they
 * hang together, what was asked of each of their settings and when – and what was
 * handed to the speech synthesis.  A test reads it here, and does what a browser
 * would do by itself: let the voices arrive, bring an utterance to its end.
 *
 * A context's clock follows plat::nowMs(): seconds since the context was made,
 * standing still while it is suspended (plat::native::advance() moves it).  The
 * 'ended' handler of a source runs from the host's queue once its time has come
 * (plat::native::pump()).
 */
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "platform/audio.h"

namespace fs::audioNative {

/** One thing asked of a setting. */
struct ParamEvent {
  /** 'value' (`param.value = v`), 'set' (setValueAtTime), 'linear', 'exp', 'target' (setTargetAtTime), 'cancel' */
  std::string what;
  double value = 0;
  /** the time it was scheduled for; for 'value': the context's time when it was set */
  double time = 0;
  /** setTargetAtTime's */
  double timeConstant = 0;
};

struct ParamInfo {
  /** what it was last set or sent to */
  double value = 0;
  /** what was asked of it, the newest last (the last 200) */
  std::vector<ParamEvent> events;
  /** how many of these were of a kind */
  int count(const std::string& what) const;
  /** the newest of a kind (nullptr: none) */
  const ParamEvent* last(const std::string& what) const;
};

/** One node as the host knows it. */
struct NodeInfo {
  /** nodes are numbered as they are made (1, 2 …, over all contexts) */
  int id = 0;
  /** 'gain' | 'biquad' | 'osc' | 'source' | 'compressor' | 'delay' | 'panner' | 'destination' */
  std::string kind;
  /** an oscillator's or a filter's type ('custom': an oscillator with a wave of its own) */
  std::string type;
  /** the settings, by audioBackend::Param (see param()) */
  std::array<ParamInfo, audioBackend::PCount> params;
  /** the nodes it feeds (their ids), and the settings it moves (node id, name of the setting) */
  std::vector<int> outputs;
  std::vector<std::pair<int, std::string>> paramOutputs;
  /** start(when, offset, duration) and stop(when) as they were called (−1: not called; duration −1: none given) */
  double started = -1, stopped = -1, offset = 0, duration = -1;
  bool loop = false;
  /** the buffer it plays (buffers are numbered as they are made; 0: none), whether it has a wave of its own */
  int buffer = 0;
  bool wave = false;
  /** no value names it any more (in a browser it lives on while it sounds) */
  bool released = false;

  /** a setting by its name: 'gain', 'frequency', 'detune', 'Q', 'pan', 'delayTime', 'threshold', 'knee', 'ratio', 'attack', 'release', 'playbackRate' */
  const ParamInfo& param(const std::string& name) const;
  bool feeds(int nodeId) const;
};

// --- what kind of browser this is ------------------------------------------------

/** is there a Web Audio API at all (true) */
void setSupported(bool on);
/**
 * May a context run (true)?  false is a browser before the first tap: a new context
 * stays 'suspended', and resume() does not help.
 */
void setAutoplayAllowed(bool on);
/** has it createStereoPanner (true; old Safari: false) */
void setStereoPanner(bool on);
/** the sample rate of the contexts made from now on (48000) */
void setSampleRate(double hz);

// --- what was built ----------------------------------------------------------------

/** how many contexts were made so far */
int contextsMade();
/** nodes made in a context: all of them, and those of a kind */
int created(const AudioContext& ctx);
int made(const AudioContext& ctx, const std::string& kind);
/** scheduling calls on the settings of its nodes (setValueAtTime & co. – not `value = v`) */
long paramCalls(const AudioContext& ctx);
/** periodic waves and buffers made in it */
int waves(const AudioContext& ctx);
int buffers(const AudioContext& ctx);
/**
 * The nodes of a context in the order they were made (its loudspeakers first) – also those
 * nothing names any more: the last twenty thousand of them are kept, and a pointer holds for as
 * long as its node is.
 */
std::vector<const NodeInfo*> nodes(const AudioContext& ctx);
const NodeInfo* info(const AudioNode& node);
const ParamInfo* info(const AudioParam& param);
/** how many objects of the host (contexts, nodes, buffers, waves) some value still names */
int alive();

// --- the loudspeakers ------------------------------------------------------------------

/**
 * The next `frames` samples (a multiple of 128) of everything the running contexts' graphs
 * send to their loudspeakers, mixed: stereo, interleaved (L R L R …), at `sampleRate`, each
 * within −1…1.  For a host that plays them (platform/sdl), called again and again with what
 * its device needs next; a context's sound is made from its own clock on, which keeps near
 * plat::nowMs().  What it plays: oscillators (also with a wave of their own), buffers,
 * gains, filters, delays, stereo panners and a plain compressor, with every setting's
 * automation and what nodes send into settings.  Not: speech.
 */
void mix(float* out, int frames, double sampleRate);

// --- speech ---------------------------------------------------------------------------

/** One utterance as it was handed to speechSynthesis.speak(). */
struct Spoken {
  std::string text;
  std::string lang;
  /** the voice it was given (both empty: none) */
  std::string voiceName, voiceURI;
  double rate = 1, pitch = 1, volume = 1;
};

/** is there speech synthesis at all (true) */
void setSpeechSupported(bool on);
/** what getVoices() answers from now on; `tell`: with the 'voiceschanged' event */
void setVoices(std::vector<SpeechSynthesisVoice> voices, bool tell = true);
/** everything that was spoken so far, in order */
const std::vector<Spoken>& spoken();
void forgetSpoken();
/** the utterance being read comes to its end ('end'); false: nothing was being read */
bool endSpeech();
/** … or fails ('error' with that word, e.g. 'network') */
bool failSpeech(const std::string& error);
/** speechSynthesis.paused (a call came in, the screen was locked) */
void setSpeechPaused(bool on);

}  // namespace fs::audioNative
