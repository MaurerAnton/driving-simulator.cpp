// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/voicepick.ts @ v1.7.7
/**
 * Which of the browser's voices reads a language – the choice, without the
 * browser (audio/speech.h does the speaking).
 *
 * Browsers offer very different voices: Chrome on the desktop "Google
 * Deutsch" (over the network, good), Edge "Microsoft Katja Online (Natural)",
 * Safari "Anna" and "Anna (Enhanced)" next to toy voices ("Bubbles",
 * "Zarvox"), Firefox on Linux eSpeak, Android the system's engine.  A voice
 * counts for a language when its language tag says so; the locale of the
 * texts (de-DE, pt-PT, zh-CN) comes first, other regions of the same
 * language after it; Cantonese never reads Mandarin, and no voice of an
 * unrelated language is ever taken – the texts are then shown only.  Among
 * the voices of a language the natural/neural/online/enhanced ones win.
 *
 * Also the player's choices per language (voice, rate, pitch) as the
 * settings keep them, and their check when they are loaded.
 *
 * tests/audio_test.cpp
 */
#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "platform/json.h"

namespace fs {

/** What is used of a SpeechSynthesisVoice (and what the tests make up). */
struct VoiceLike {
  std::string name;
  std::string lang;
  std::string voiceURI;
  bool localService = false;
  /** `default` (a word C++ keeps for itself) */
  bool default_ = false;

  bool operator==(const VoiceLike& o) const = default;
};

/** How well a voice fits a language. */
enum class VoiceFit { Locale, Language, Related };

/** A language as a browser writes it, made comparable: "de_DE" → de-de; Android's old "in" → id. */
struct Tag {
  std::string lang;
  std::string region;
  std::string script;

  bool operator==(const Tag& o) const = default;
};

Tag parseTag(const std::string& tag);

namespace voicepickDetail {
std::optional<VoiceFit> fit(const std::string& name, const std::string& lang, const std::string& locale);
double score(const std::string& name, const std::string& lang, const std::string& voiceURI, bool isDefault, const std::string& locale);
}  // namespace voicepickDetail

/** How a voice fits the locale of the texts (de-DE, pt-PT, ar), or nullopt when it does not read that language. */
template <class V>
std::optional<VoiceFit> voiceFit(const V& v, const std::string& locale) {
  return voicepickDetail::fit(v.name, v.lang, locale);
}

/** Points of a voice for a locale: higher is better; -Infinity when it does not read that language. */
template <class V>
double voiceScore(const V& v, const std::string& locale) {
  return voicepickDetail::score(v.name, v.lang, v.voiceURI, v.default_, locale);
}

/** The voices that read a language, the best first. */
template <class V>
std::vector<V> rankVoices(const std::vector<V>& voices, const std::string& locale) {
  struct Scored {
    const V* v;
    size_t i;
    double s;
  };
  std::vector<Scored> scored;
  for (size_t i = 0; i < voices.size(); i++) {
    const double s = voiceScore(voices[i], locale);
    if (s > -std::numeric_limits<double>::infinity()) scored.push_back(Scored{&voices[i], i, s});
  }
  std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.s != b.s ? a.s > b.s : a.i < b.i; });
  std::vector<V> out;
  out.reserve(scored.size());
  for (const Scored& x : scored) out.push_back(*x.v);
  return out;
}

/**
 * What can be said about the voices of a language:
 * good – there is one of the language; related – only one of a closely related language;
 * none – the browser lists voices, none of them for the language (the texts are shown only);
 * unknown – the browser lists no voices at all (it may still speak: its system decides).
 */
enum class VoiceStatus { Good, Related, None, Unknown };

template <class V>
VoiceStatus voiceStatus(const std::vector<V>& voices, const std::string& locale) {
  if (voices.empty()) return VoiceStatus::Unknown;
  const std::vector<V> ranked = rankVoices(voices, locale);
  if (ranked.empty()) return VoiceStatus::None;
  return voiceFit(ranked[0], locale) == VoiceFit::Related ? VoiceStatus::Related : VoiceStatus::Good;
}

/**
 * The voice to read with: the one the player chose (by its URI, or its name –
 * the URI differs between browsers), if it is there and still reads the
 * language and has not failed; the best one otherwise; nullopt when none fits.
 */
template <class V>
std::optional<V> chooseVoice(const std::vector<V>& voices, const std::string& locale, const std::string& chosen = std::string(),
                             const std::unordered_set<std::string>& failed = std::unordered_set<std::string>()) {
  std::vector<V> ranked;
  for (V& v : rankVoices(voices, locale))
    if (failed.count(v.voiceURI) == 0) ranked.push_back(std::move(v));
  if (!chosen.empty()) {
    for (const V& v : ranked)
      if (v.voiceURI == chosen) return v;
    for (const V& v : ranked)
      if (v.name == chosen) return v;
  }
  if (ranked.empty()) return std::nullopt;
  return ranked[0];
}

// ------------------------------------------------------------------ the player's choices

/** What the player set for the voice of one language. */
struct SpeechPref {
  /** the voice's URI; '' = automatic */
  std::string voice;
  /** 0.6 … 1.6 (1 = the voice's own) */
  double rate = 1;
  /** 0.6 … 1.4 */
  double pitch = 1;

  bool operator==(const SpeechPref& o) const = default;
};

inline constexpr std::array<double, 2> RATE_RANGE = {0.6, 1.6};
inline constexpr std::array<double, 2> PITCH_RANGE = {0.6, 1.4};
/** (voice '', rate 1, pitch 1) */
extern const SpeechPref DEFAULT_PREF;

/** `Record<string, SpeechPref>`: the choices by language ('de', 'en' …) */
using SpeechPrefs = std::map<std::string, SpeechPref>;

/** The choices of a language (the defaults where none; `all` may be null). */
SpeechPref prefOf(const SpeechPrefs* all, const std::string& lang);
inline SpeechPref prefOf(const SpeechPrefs& all, const std::string& lang) { return prefOf(&all, lang); }

/** The choices as they were stored, checked: whatever is broken goes. */
SpeechPrefs cleanSpeechPrefs(const Json& raw);

}  // namespace fs
