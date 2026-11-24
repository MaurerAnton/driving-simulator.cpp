// SPDX-License-Identifier: AGPL-3.0-only
/** The parts of audio.h that are the same on every host: the handlers, the utterances, the small helpers. */
#include <unordered_map>

#include "platform/audio.h"

namespace fs {

namespace {

std::unordered_map<int, std::function<void()>>& endedTable() {
  static std::unordered_map<int, std::function<void()>> t;
  return t;
}
int nextEnded = 1;

/** the listeners of speechSynthesis's 'voiceschanged', in the order they were added */
std::vector<std::pair<int, std::function<void()>>>& voiceListeners() {
  static std::vector<std::pair<int, std::function<void()>>> l;
  return l;
}
int nextVoiceListener = 1;
int nextUtterance = 1;

}  // namespace

struct SpeechSynthesisUtterance::State {
  int id = 0;
  std::string text;
  std::string lang;
  std::optional<SpeechSynthesisVoice> voice;
  double rate = 1, pitch = 1, volume = 1;
  std::function<void()> onend;
  std::function<void(const SpeechSynthesisErrorEvent&)> onerror;
};

namespace {

/** the utterances handed to speak() that have neither ended nor failed yet */
std::unordered_map<int, std::shared_ptr<SpeechSynthesisUtterance::State>>& utterances() {
  static std::unordered_map<int, std::shared_ptr<SpeechSynthesisUtterance::State>> t;
  return t;
}

}  // namespace

namespace audioDetail {

int addEnded(std::function<void()> fn) {
  int id = nextEnded++;
  endedTable().emplace(id, std::move(fn));
  return id;
}

void dropEnded(int listener) { endedTable().erase(listener); }

int endedCount() { return static_cast<int>(endedTable().size()); }

void ended(int listener) {
  auto it = endedTable().find(listener);
  if (it == endedTable().end()) return;
  // (taken out first: a source ends once, and the handler may set up others while it runs)
  std::function<void()> fn = std::move(it->second);
  endedTable().erase(it);
  fn();
}

void speechEvent(int id, int kind, const std::string& error) {
  auto it = utterances().find(id);
  if (it == utterances().end()) return;
  std::shared_ptr<SpeechSynthesisUtterance::State> s = std::move(it->second);
  utterances().erase(it);
  // (both handlers are let go: they often hold the utterance itself, which would then never be freed)
  std::function<void()> onend = std::move(s->onend);
  std::function<void(const SpeechSynthesisErrorEvent&)> onerror = std::move(s->onerror);
  s->onend = nullptr;
  s->onerror = nullptr;
  if (kind == 0) {
    if (onend) onend();
  } else if (onerror) {
    SpeechSynthesisErrorEvent e;
    e.error = error;
    onerror(e);
  }
}

void voicesChanged() {
  // (a copy: a listener may take itself away while it runs)
  std::vector<std::pair<int, std::function<void()>>> list = voiceListeners();
  for (const auto& l : list) l.second();
}

}  // namespace audioDetail

// --- sources -----------------------------------------------------------------

void AudioScheduledSourceNode::onended(std::function<void()> fn) const {
  if (!handle()) return;
  int id = fn ? audioDetail::addEnded(std::move(fn)) : 0;
  int old = audioBackend::onEnded(handle(), id);
  if (old) audioDetail::dropEnded(old);
}

// --- buffers -----------------------------------------------------------------

const std::vector<float> AudioBuffer::getChannelData(int channel) const {
  std::vector<float> out(static_cast<size_t>(length_ > 0 ? length_ : 0), 0.0f);
  if (ref_.h() && channel >= 0 && channel < channels_ && !out.empty()) audioBackend::bufferRead(ref_.h(), channel, out.data(), length_);
  return out;
}

void AudioBuffer::copyToChannel(const std::vector<float>& source, int channel, int startInChannel) const {
  if (!ref_.h() || channel < 0 || channel >= channels_ || startInChannel < 0 || startInChannel >= length_ || source.empty()) return;
  int n = static_cast<int>(source.size());
  if (n > length_ - startInChannel) n = length_ - startInChannel;
  audioBackend::bufferWrite(ref_.h(), channel, source.data(), n, startInChannel);
}

// --- the context ---------------------------------------------------------------

bool AudioContext::supported() { return audioBackend::supported(); }

AudioContext AudioContext::create() { return AudioContext(audioBackend::createContext()); }

std::string AudioContext::state() const {
  if (!ref_.h()) return "closed";
  int s = static_cast<int>(audioBackend::contextNumber(ref_.h(), 2));
  return s == 1 ? "running" : s == 2 ? "closed" : "suspended";
}

AudioBuffer AudioContext::createBuffer(int numberOfChannels, int length, double sampleRate) const {
  int h = audioBackend::createBuffer(ref_.h(), numberOfChannels, length, sampleRate);
  return h ? AudioBuffer(h, numberOfChannels, length, sampleRate) : AudioBuffer();
}

PeriodicWave AudioContext::createPeriodicWave(const std::vector<float>& real, const std::vector<float>& imag, PeriodicWaveConstraints constraints) const {
  size_t n = real.size() < imag.size() ? real.size() : imag.size();
  if (n < 2) return PeriodicWave();
  return PeriodicWave(audioBackend::createWave(ref_.h(), real.data(), imag.data(), static_cast<int>(n), constraints.disableNormalization));
}

// --- speech ----------------------------------------------------------------------

SpeechSynthesisUtterance::SpeechSynthesisUtterance(const std::string& text) : s_(std::make_shared<State>()) {
  s_->id = nextUtterance++;
  s_->text = text;
}

const std::string& SpeechSynthesisUtterance::text() const {
  static const std::string none;
  return s_ ? s_->text : none;
}
void SpeechSynthesisUtterance::lang(const std::string& v) const {
  if (s_) s_->lang = v;
}
void SpeechSynthesisUtterance::voice(const SpeechSynthesisVoice& v) const {
  if (s_) s_->voice = v;
}
void SpeechSynthesisUtterance::rate(double v) const {
  if (s_) s_->rate = v;
}
void SpeechSynthesisUtterance::pitch(double v) const {
  if (s_) s_->pitch = v;
}
void SpeechSynthesisUtterance::volume(double v) const {
  if (s_) s_->volume = v;
}
void SpeechSynthesisUtterance::onend(std::function<void()> fn) const {
  if (s_) s_->onend = std::move(fn);
}
void SpeechSynthesisUtterance::onerror(std::function<void(const SpeechSynthesisErrorEvent&)> fn) const {
  if (s_) s_->onerror = std::move(fn);
}

void SpeechSynthesis::speak(const SpeechSynthesisUtterance& u) const {
  if (!u.s_) return;
  const SpeechSynthesisUtterance::State& s = *u.s_;
  utterances()[s.id] = u.s_;
  audioBackend::speak(s.id, s.text, s.lang, s.voice ? &*s.voice : nullptr, s.rate, s.pitch, s.volume);
}

int SpeechSynthesis::addEventListener(const std::string& type, std::function<void()> fn) const {
  if (type != "voiceschanged" || !fn) return 0;
  audioBackend::speechInit();
  int id = nextVoiceListener++;
  voiceListeners().emplace_back(id, std::move(fn));
  return id;
}

void SpeechSynthesis::removeEventListener(const std::string& type, int listener) const {
  if (type != "voiceschanged") return;
  auto& l = voiceListeners();
  for (size_t i = 0; i < l.size(); i++)
    if (l[i].first == listener) {
      l.erase(l.begin() + static_cast<long>(i));
      return;
    }
}

bool hasSpeechSynthesis() { return audioBackend::speechSupported(); }

SpeechSynthesis speechSynthesis() { return SpeechSynthesis(); }

}  // namespace fs
