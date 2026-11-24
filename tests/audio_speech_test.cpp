// SPDX-License-Identifier: AGPL-3.0-only
// LINK: audio i18n platform core
/**
 * The spoken instructions (audio/speech.h) on the native host's speech synthesis: the voice that is
 * taken, the wait for the voices, sentences one after another, a new instruction in place of the one
 * being read or behind it, a voice over the network that fails, the watchdog.  (The original has no
 * test of its speaker; this is the port's own.)
 *
 * The speaker reads what src/i18n makes of a text (speakable, splitForSpeech) in the locale it names
 * (localeOf): until that part is in the tree there is nothing to test here.
 */
#include <cstdio>

#if __has_include("i18n/index.h") && __has_include("i18n/speechtext.h")

#include <cassert>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "audio/speech.h"
#include "audio/timers.h"
#include "i18n/index.h"
#include "i18n/speechtext.h"
#include "platform/audio.h"
#include "platform/native/audio_native.h"
#include "platform/native/native.h"
#include "platform/plat.h"

using namespace fs;
using audioNative::spoken;

namespace {

/** let `ms` milliseconds pass, a few at a time, with everything that comes due in them */
void pass(double ms) {
  for (double t = 0; t < ms; t += 5) {
    plat::native::advance(5);
    plat::native::drain();
  }
}

/** the pieces a text is read in */
std::vector<std::string> piecesOf(const std::string& text, const std::string& lang) { return splitForSpeech(speakable(text, lang)); }

/** the settings as the game keeps them (game/settings.h): the choices by language may be missing */
struct GameSettings {
  std::string lang = "de";
  bool voice = true;
  double volume = 0.7;
  std::optional<std::map<std::string, SpeechPref>> speech = std::nullopt;
};

const SpeechSynthesisVoice ANNA{"Anna", "de-DE", "uri:Anna", true, false};
const SpeechSynthesisVoice KATJA{"Microsoft Katja Online (Natural) - German (Germany)", "de-DE", "uri:Katja", false, false};
const SpeechSynthesisVoice DAVID{"Microsoft David - English (United States)", "en-US", "uri:David", true, true};

}  // namespace

int main() {
  plat::native::manualClock(true);
  plat::native::advance(1);
  const SpeechSynthesis synth = speechSynthesis();
  SpeechSettings s;
  s.voice = true;
  s.volume = 0.8;
  const std::string deDE = localeOf("de");
  assert(deDE == "de-DE");

  // --- switched off, or nothing to say: nothing is read ---
  SpeechSettings off = s;
  off.voice = false;
  assert(!speech.say("Hallo.", "de", off));
  assert(!speech.say("   ", "de", s));
  assert(spoken().empty() && speech.supported());

  // --- the browser lists no voices (yet): the text waits for them a little, then is read with the language alone ---
  assert(speech.status("de") == VoiceStatus::Unknown && speech.voicesFor("de").empty() && !speech.voiceFor("de", SpeechPref()));
  const double asked = plat::nowMs();
  assert(speech.say("Hallo.", "de", s));
  pass(2000);
  assert(spoken().empty());
  pass(1000);
  assert(spoken().size() == 1 && plat::nowMs() - asked >= 2400);
  assert(spoken()[0].text == piecesOf("Hallo.", "de")[0] && spoken()[0].lang == deDE && spoken()[0].voiceName.empty());
  assert(spoken()[0].rate == 1 && spoken()[0].pitch == 1 && spoken()[0].volume == 0.8);
  assert(audioNative::endSpeech());

  // --- the voices arrive: whoever asked is told, the best voice of the language reads ---
  int told = 0;
  const std::function<void()> stopListening = speech.onVoices([&told] { told++; });
  audioNative::setVoices({DAVID, ANNA, KATJA});
  assert(told == 1);
  audioNative::setVoices({DAVID, ANNA, KATJA});   // (the same list again: nothing new)
  assert(told == 1);
  stopListening();
  assert(speech.status("de") == VoiceStatus::Good && speech.status("sq") == VoiceStatus::None);
  assert(speech.voicesFor("de").size() == 2 && speech.voicesFor("de")[0] == KATJA && speech.voiceFor("de", SpeechPref()) == KATJA);
  assert(speech.voiceFor("de", SpeechPref{"uri:Anna", 1, 1}) == ANNA && speech.voiceFor("de", SpeechPref{"Anna", 1, 1}) == ANNA);
  audioNative::forgetSpoken();
  assert(speech.say("Jetzt rechts abbiegen.", "de", s));
  assert(spoken().empty());   // (a short pause first: a browser drops what is spoken right after a cancel)
  pass(60);
  assert(spoken().size() == 1 && spoken()[0].voiceURI == "uri:Katja" && spoken()[0].lang == "de-DE");
  assert(audioNative::endSpeech());
  pass(20);
  assert(spoken().size() == 1 && !synth.speaking());

  // --- never a voice of another language: where there is none the text is only shown ---
  assert(!speech.say("Kthehu djathtas.", "sq", s));
  pass(100);
  assert(spoken().size() == 1);

  // --- a long text is read sentence by sentence ---
  const std::string longText =
      "In zweihundert Metern biegen Sie an der Ampel rechts ab und folgen dann dem Verlauf der Hauptstraße bis zum Ortsausgang. "
      "Achten Sie dabei auf die Radfahrer, die auf dem Schutzstreifen neben Ihnen fahren, und halten Sie ausreichend Abstand. "
      "Danach fahren Sie im Kreisverkehr an der zweiten Ausfahrt hinaus und ordnen sich auf dem linken Fahrstreifen ein.";
  const std::vector<std::string> pieces = piecesOf(longText, "de");
  assert(pieces.size() >= 2);
  audioNative::forgetSpoken();
  assert(speech.say(longText, "de", s));
  pass(60);
  assert(spoken().size() == 1 && spoken()[0].text == pieces[0]);
  pass(500);
  assert(spoken().size() == 1);   // (the next one only when this one has ended)
  assert(audioNative::endSpeech());
  pass(5);
  assert(spoken().size() == 2 && spoken()[1].text == pieces[1]);

  // --- a new instruction takes the place of the one being read; what waited is forgotten ---
  assert(speech.say("Bitte anhalten.", "de", s));
  assert(!synth.speaking());   // (cancelled at once)
  pass(60);
  assert(spoken().size() == 3 && spoken()[2].text == piecesOf("Bitte anhalten.", "de")[0]);
  // --- … or queues behind it ---
  assert(speech.say("Dann links.", "de", s, true));
  pass(200);
  assert(spoken().size() == 3 && synth.speaking());
  assert(audioNative::endSpeech());
  pass(5);
  assert(spoken().size() == 4 && spoken()[3].text == piecesOf("Dann links.", "de")[0]);
  assert(audioNative::endSpeech());
  pass(50);
  assert(spoken().size() == 4 && !synth.speaking());
  for (size_t i = 2; i < pieces.size(); i++)
    for (const audioNative::Spoken& said : spoken()) assert(said.text != pieces[i]);

  // --- a voice over the network that fails is left for the session: the next best voice reads the piece again ---
  audioNative::forgetSpoken();
  assert(speech.say("Die nächste Straße links.", "de", s));
  pass(60);
  assert(spoken().size() == 1 && spoken()[0].voiceURI == "uri:Katja");
  assert(audioNative::failSpeech("network"));
  pass(5);
  assert(spoken().size() == 2 && spoken()[1].voiceURI == "uri:Anna" && spoken()[1].text == spoken()[0].text);
  assert(audioNative::endSpeech());
  assert(speech.voiceFor("de", SpeechPref()) == ANNA && speech.voiceFor("de", SpeechPref{"uri:Katja", 1, 1}) == ANNA);
  // (an utterance that was only interrupted says nothing against its voice – and a voice on the device is never given up)
  assert(speech.say("Geradeaus.", "de", s));
  pass(60);
  assert(spoken().size() == 3 && spoken()[2].voiceURI == "uri:Anna");
  assert(audioNative::failSpeech("synthesis-failed"));
  pass(50);
  assert(spoken().size() == 3 && speech.voiceFor("de", SpeechPref()) == ANNA);

  // --- the player's choice for the language: voice, rate, pitch – and the game's settings as they are ---
  GameSettings game;
  game.speech = std::map<std::string, SpeechPref>{{"en", SpeechPref{"uri:David", 1.3, 0.8}}};
  audioNative::forgetSpoken();
  assert(speech.say("Turn right now.", "en", game));
  pass(60);
  assert(spoken().size() == 1 && spoken()[0].voiceURI == "uri:David" && spoken()[0].rate == 1.3 && spoken()[0].pitch == 0.8 && spoken()[0].volume == 0.7);
  assert(spoken()[0].lang == "en-US");
  assert(audioNative::endSpeech());
  game.voice = false;
  assert(!speech.say("Turn left.", "en", game));
  assert(speech.test("Turn left.", "en", game));   // (the test in the settings reads whatever the switch says)
  pass(60);
  assert(spoken().size() == 2);
  GameSettings plain;   // (no choices stored yet)
  assert(speech.say("Stop.", "en", plain));
  pass(60);
  assert(spoken().size() == 3 && spoken()[2].rate == 1 && spoken()[2].volume == 0.7);

  // --- if no end ever comes, the watchdog moves on ---
  audioNative::forgetSpoken();
  assert(speech.say(longText, "de", s));
  pass(60);
  assert(spoken().size() == 1 && spoken()[0].text == pieces[0]);
  double waited = 0;
  while (spoken().size() < 2 && waited < 200000) {
    pass(100);
    waited += 100;
  }
  assert(spoken().size() == 2 && spoken()[1].text == pieces[1]);
  assert(waited >= 4000 && waited <= 4000 + static_cast<double>(pieces[0].size()) * 250 + 200);

  // --- left paused (a call, a locked screen): woken before it speaks ---
  speech.stop();
  pass(50);
  audioNative::setSpeechPaused(true);
  assert(speech.say("Weiter.", "de", s));
  pass(60);
  assert(!synth.paused());

  // --- stop(): silence, and nothing waits any more ---
  assert(speech.say(longText, "de", s));
  pass(60);
  audioNative::forgetSpoken();
  speech.stop();
  assert(!synth.speaking());
  pass(10000);
  assert(spoken().empty());

  // --- a browser that cannot speak ---
  // (the speaker has found its voice already: only a fresh page would find none)
  std::printf("audio_speech_test: ok\n");
  return 0;
}

#else

int main() {
  std::printf("audio_speech_test: nothing tested – the speaker waits for src/i18n (speakable, splitForSpeech, localeOf)\n");
  return 0;
}

#endif
