// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/speech.ts @ v1.7.7
#include "audio/speech.h"

// The texts are made speakable by src/i18n (speakable, splitForSpeech) and read in the locale
// it names for a language (localeOf): the speaker is there once those headers are.
#if __has_include("i18n/index.h") && __has_include("i18n/speechtext.h")

#include <algorithm>
#include <memory>

#include "audio/timers.h"
#include "core/js.h"
#include "i18n/index.h"
#include "i18n/speechtext.h"
#include "platform/plat.h"

namespace fs {

namespace {

/** how long to wait for the voices before speaking with the language alone (ms) */
const double VOICE_WAIT = 2500;

/** `text.length`: how long a text is as JavaScript counts (UTF-16 units) */
double lengthOf(const std::string& text) {
  double n = 0;
  for (size_t i = 0; i < text.size(); i++) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    // (every character counts one – a continuation byte none, a character beyond the BMP two)
    if ((c & 0xc0) != 0x80) n += c >= 0xf0 ? 2 : 1;
  }
  return n;
}

}  // namespace

void Speech::init() {
  if (started_ != 0 || !hasSpeechSynthesis()) return;
  synth_ = speechSynthesis();
  started_ = plat::nowMs();
  auto load = [this] {
    std::vector<SpeechSynthesisVoice> list = synth_ ? synth_->getVoices() : std::vector<SpeechSynthesisVoice>();
    if (list == voices_) return;
    voices_ = std::move(list);
    // (a copy: one who is told may stop listening)
    const std::map<int, std::function<void()>> listeners = listeners_;
    for (const auto& l : listeners) l.second();
  };
  load();
  synth_->addEventListener("voiceschanged", load);
  // (Safari and some Androids fill the list without the event)
  auto tries = std::make_shared<int>(0);
  auto poll = std::make_shared<int>(0);
  *poll = timers::setInterval(
      [this, load, tries, poll] {
        load();
        if (++*tries >= 12 || !voices_.empty()) timers::clearInterval(*poll);
      },
      250);
}

bool Speech::supported() {
  init();
  return synth_.has_value();
}

std::function<void()> Speech::onVoices(std::function<void()> f) {
  init();
  const int id = nextListener_++;
  listeners_[id] = std::move(f);
  return [this, id] { listeners_.erase(id); };
}

std::vector<SpeechSynthesisVoice> Speech::voicesFor(const std::string& lang) {
  init();
  return rankVoices(voices_, localeOf(lang));
}

VoiceStatus Speech::status(const std::string& lang) {
  init();
  return voiceStatus(voices_, localeOf(lang));
}

std::optional<SpeechSynthesisVoice> Speech::voiceFor(const std::string& lang, const SpeechPref& pref) {
  init();
  return chooseVoice(voices_, localeOf(lang), pref.voice, failed_);
}

bool Speech::say(const std::string& text, const std::string& lang, const SpeechSettings& s, bool queue) {
  if (!s.voice) return false;
  return speak(text, lang, s, queue);
}

bool Speech::test(const std::string& text, const std::string& lang, const SpeechSettings& s) { return speak(text, lang, s, false); }

void Speech::stop() {
  queue_.clear();
  reset();
}

bool Speech::speak(const std::string& text, const std::string& lang, const SpeechSettings& s, bool queue) {
  if (!supported() || trim(text).empty()) return false;
  if (!voices_.empty() && status(lang) == VoiceStatus::None) return false;
  const SpeechPref pref = prefOf(s.speech, lang);
  std::vector<Piece> pieces;
  for (const std::string& t : splitForSpeech(speakable(text, lang))) pieces.push_back(Piece{t, lang, pref, s.volume});
  if (pieces.empty()) return false;
  if (!queue) stop();
  for (Piece& p : pieces) queue_.push_back(std::move(p));
  if (!current_ && pending_ == 0) next(queue ? 0 : 60);
  return true;
}

void Speech::reset() {
  timers::clearTimeout(watchdog_);
  timers::clearTimeout(pending_);
  pending_ = 0;
  const bool was = static_cast<bool>(current_);
  current_ = SpeechSynthesisUtterance();
  if (synth_ && (was || synth_->speaking() || synth_->pending())) synth_->cancel();
}

void Speech::next(double delay) {
  timers::clearTimeout(pending_);
  pending_ = timers::setTimeout(
      [this] {
        pending_ = 0;
        play();
      },
      delay);
}

void Speech::play() {
  if (!synth_ || queue_.empty() || current_) return;
  const SpeechSynthesis synth = *synth_;
  const Piece p = queue_.front();
  // (the voices not there yet: wait a little for them)
  if (voices_.empty() && plat::nowMs() - started_ < VOICE_WAIT) {
    next(200);
    return;
  }
  const std::optional<SpeechSynthesisVoice> voice = voiceFor(p.lang, p.pref);
  if (!voices_.empty() && !voice) {
    queue_.clear();
    return;
  }
  queue_.pop_front();
  const SpeechSynthesisUtterance u(p.text);
  u.lang(voice && !voice->lang.empty() ? voice->lang : localeOf(p.lang));
  if (voice) u.voice(*voice);
  u.rate(p.pref.rate);
  u.pitch(p.pref.pitch);
  u.volume(p.volume);
  auto done = [this, u] {
    if (!(current_ == u)) return;
    timers::clearTimeout(watchdog_);
    current_ = SpeechSynthesisUtterance();
    if (!queue_.empty()) next(0);
  };
  u.onend(done);
  u.onerror([this, u, voice, p, done](const SpeechSynthesisErrorEvent& e) {
    if (!(current_ == u)) return;
    // (a voice over the network without a network: the next best voice reads the piece again)
    if (voice && !voice->localService && e.error != "interrupted" && e.error != "canceled" && failed_.count(voice->voiceURI) == 0) {
      failed_.insert(voice->voiceURI);
      queue_.push_front(p);
    }
    done();
  });
  current_ = u;
  // (if no end ever comes: on after the longest the piece can take)
  timers::clearTimeout(watchdog_);
  watchdog_ = timers::setTimeout(
      [this, u, synth] {
        if (!(current_ == u)) return;
        current_ = SpeechSynthesisUtterance();
        synth.cancel();
        if (!queue_.empty()) next(60);
      },
      4000 + (lengthOf(p.text) * 250) / std::max(0.5, p.pref.rate));
  // (Safari and Chrome can be left paused – after a call, a locked screen)
  if (synth.paused()) synth.resume();
  synth.speak(u);
}

/** The one speaker of the game. */
Speech speech;

}  // namespace fs

#endif
