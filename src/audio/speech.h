// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/speech.ts @ v1.7.7
/**
 * The spoken instructions: the browser's speech synthesis, used with care.
 *
 * - The voice: the player's choice for the language, else the best one the
 *   browser has (audio/voicepick.h) – never a voice of another language:
 *   where there is none the text is only shown (Chrome would otherwise read
 *   Albanian or Bengali with an English voice).
 * - The voices arrive late (Chrome: after "voiceschanged", Safari sometimes
 *   without telling): they are looked for again on that event and for a few
 *   seconds after the start; a text that comes before waits for them briefly.
 * - The text is made speakable (i18n/speechtext.h) and cut into sentences:
 *   Chrome stops a voice after about 15 seconds, and a short utterance is
 *   cancelled cleanly when the next instruction comes.
 * - A new instruction replaces the one being read (or queues behind it).
 * - A voice over the network that fails (offline) is left for the session,
 *   and the piece is read again with the next best voice.
 * - Chrome forgets an utterance that nothing holds (and never ends it): the
 *   current one is held, and a watchdog moves on if no end comes.
 */
#pragma once

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "audio/voicepick.h"
#include "platform/audio.h"

namespace fs {

/** What the speaking needs of the settings. */
struct SpeechSettings {
  /** the spoken instructions are on */
  bool voice = false;
  double volume = 1;
  /** per language: voice, rate, pitch */
  SpeechPrefs speech = {};
};

namespace speechDetail {
template <class Prefs>
void copyPrefs(SpeechPrefs& out, const Prefs& prefs) {
  for (const auto& [lang, pref] : prefs) out[lang] = SpeechPref{pref.voice, pref.rate, pref.pitch};
}
/** (`speech?: …`: there may be none) */
template <class Prefs>
void copyPrefs(SpeechPrefs& out, const std::optional<Prefs>& prefs) {
  if (prefs) copyPrefs(out, *prefs);
}
}  // namespace speechDetail

/**
 * The game's settings as they are – any struct with `voice`, `volume` and `speech` (the choices by
 * language, or none) – as what the speaking needs of them.
 */
template <class Settings>
SpeechSettings speechSettingsOf(const Settings& s) {
  SpeechSettings out;
  out.voice = s.voice;
  out.volume = s.volume;
  speechDetail::copyPrefs(out.speech, s.speech);
  return out;
}

class Speech {
 public:
  /** Look for the voices (once; harmless where the browser cannot speak). */
  void init();

  bool supported();

  /** Call when the list of voices changes; gives back a function that stops that. */
  std::function<void()> onVoices(std::function<void()> f);

  /** The voices that read a language, the best first. */
  std::vector<SpeechSynthesisVoice> voicesFor(const std::string& lang);

  VoiceStatus status(const std::string& lang);

  /** The voice that would read a language now (nullopt: none – or the browser lists none). */
  std::optional<SpeechSynthesisVoice> voiceFor(const std::string& lang, const SpeechPref& pref);

  /**
   * Read a text out – when the spoken instructions are on.  `queue`: after
   * what is being read; otherwise instead of it.  false when nothing is read
   * (switched off, no speech, no voice for the language).
   */
  bool say(const std::string& text, const std::string& lang, const SpeechSettings& s, bool queue = false);
  /** (… with the game's settings as they are) */
  template <class Settings>
    requires(!std::is_base_of_v<SpeechSettings, Settings>)
  bool say(const std::string& text, const std::string& lang, const Settings& s, bool queue = false) {
    if (!s.voice) return false;
    return say(text, lang, speechSettingsOf(s), queue);
  }

  /** Read a text whatever the switch says (the test in the settings). */
  bool test(const std::string& text, const std::string& lang, const SpeechSettings& s);
  template <class Settings>
    requires(!std::is_base_of_v<SpeechSettings, Settings>)
  bool test(const std::string& text, const std::string& lang, const Settings& s) {
    return test(text, lang, speechSettingsOf(s));
  }

  /** Stop reading, and forget what was waiting. */
  void stop();

 private:
  struct Piece {
    std::string text;
    std::string lang;
    SpeechPref pref;
    double volume = 1;
  };

  std::optional<SpeechSynthesis> synth_;
  std::vector<SpeechSynthesisVoice> voices_;
  double started_ = 0;
  /** who wants to know when the list of voices changes, in the order they asked */
  std::map<int, std::function<void()>> listeners_;
  int nextListener_ = 1;
  std::deque<Piece> queue_;
  /** the utterance being read (held: Chrome drops what nobody holds) */
  SpeechSynthesisUtterance current_;
  int watchdog_ = 0;
  int pending_ = 0;
  /** voices that failed this session (a network voice while offline) */
  std::unordered_set<std::string> failed_;

  bool speak(const std::string& text, const std::string& lang, const SpeechSettings& s, bool queue);
  void reset();
  /** The next piece – after a short pause (Chrome drops a speak() right after cancel()). */
  void next(double delay);
  void play();
};

/** The one speaker of the game. */
extern Speech speech;

}  // namespace fs
