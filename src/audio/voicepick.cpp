// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/audio/voicepick.ts @ v1.7.7
// (the original's regular expressions are written out here as small matchers: each says which expression it is)
#include "audio/voicepick.h"

#include "core/js.h"

namespace fs {

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
/** \w */
bool wordChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

/** does `word` stand in `s` at `at`, whatever the case of its (ASCII) letters? */
bool matchesAt(const std::string& s, size_t at, const char* word) {
  size_t i = 0;
  for (; word[i]; i++)
    if (at + i >= s.size() || lower(s[at + i]) != word[i]) return false;
  return true;
}
size_t lengthOf(const char* word) {
  size_t n = 0;
  while (word[n]) n++;
  return n;
}
/** /word/i */
bool containsI(const std::string& s, const char* word) {
  for (size_t at = 0; at < s.size(); at++)
    if (matchesAt(s, at, word)) return true;
  return false;
}
/** is there a word boundary (\b) in `s` before the character at `at`? */
bool boundary(const std::string& s, size_t at) {
  const bool before = at > 0 && wordChar(s[at - 1]);
  const bool after = at < s.size() && wordChar(s[at]);
  return before != after;
}
/** /\bword\b/i */
bool hasWordI(const std::string& s, const char* word) {
  const size_t n = lengthOf(word);
  for (size_t at = 0; at + n <= s.size(); at++)
    if (matchesAt(s, at, word) && boundary(s, at) && boundary(s, at + n)) return true;
  return false;
}
/** /^word\b/i */
bool startsWithWordI(const std::string& s, const char* word) { return matchesAt(s, 0, word) && boundary(s, lengthOf(word)); }
/** /word$/i */
bool endsWithI(const std::string& s, const char* word) {
  const size_t n = lengthOf(word);
  return s.size() >= n && matchesAt(s, s.size() - n, word);
}
/** /^word$/i */
bool equalsI(const std::string& s, const char* word) { return s.size() == lengthOf(word) && matchesAt(s, 0, word); }

/** /^[a-z]{n}$/ for n from `least` to `most` */
bool lowerLetters(const std::string& s, size_t least, size_t most) {
  if (s.size() < least || s.size() > most) return false;
  for (char c : s)
    if (c < 'a' || c > 'z') return false;
  return true;
}
/** /^\d{3}$/ */
bool threeDigits(const std::string& s) { return s.size() == 3 && s[0] >= '0' && s[0] <= '9' && s[1] >= '0' && s[1] <= '9' && s[2] >= '0' && s[2] <= '9'; }

const char* alias(const std::string& lang) {
  if (lang == "in") return "id";
  if (lang == "iw") return "he";
  if (lang == "cmn") return "zh";
  if (lang == "fil") return "tl";
  if (lang == "nb") return "no";
  return nullptr;
}

/**
 * Languages close enough that a voice of one reads the other's texts well:
 * only where no voice of the language itself is there (status "related").
 */
bool related(const std::string& want, const std::string& has) {
  if (want == "hr") return has == "bs" || has == "sr";
  if (want == "id") return has == "ms";
  return false;
}

/** Is this Chinese voice a Cantonese one (Hong Kong, "yue")? It cannot read Mandarin texts. */
bool cantonese(const std::string& name, const Tag& t) {
  // /cantonese|粤語|粵語|廣東/i
  return t.lang == "yue" || (t.lang == "zh" && (t.region == "hk" || t.region == "mo")) || containsI(name, "cantonese") || name.find("粤語") != std::string::npos ||
         name.find("粵語") != std::string::npos || name.find("廣東") != std::string::npos;
}

/** Apple's toy voices: fun, but not an instructor. */
bool novelty(const std::string& name) {
  // /^(albert|bad news|bahh|bells|boing|bubbles|cellos|deranged|good news|hysterical|jester|organ|pipe organ|superstar|trinoids|whisper|wobble|zarvox)\b/i
  static const char* const words[] = {"albert", "bad news", "bahh",       "bells",     "boing",    "bubbles", "cellos", "deranged", "good news",
                                      "hysterical", "jester", "organ", "pipe organ", "superstar", "trinoids", "whisper", "wobble", "zarvox"};
  for (const char* w : words)
    if (startsWithWordI(name, w)) return true;
  return false;
}
/** Apple's old Eloquence voices (Eddy, Flo, Grandma …): understandable, but robotic. */
bool eloquence(const std::string& name) {
  // /^(eddy|flo|grandma|grandpa|reed|rocko|sandy|shelley)\b/i
  static const char* const words[] = {"eddy", "flo", "grandma", "grandpa", "reed", "rocko", "sandy", "shelley"};
  for (const char* w : words)
    if (startsWithWordI(name, w)) return true;
  return false;
}

/** how long a text is in JavaScript (UTF-16 units), cut to `max` of them: `s.slice(0, max)` */
std::string sliceUtf16(const std::string& s, size_t max) {
  size_t units = 0;
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const size_t bytes = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    const size_t add = bytes == 4 ? 2 : 1;
    if (units + add > max) break;
    units += add;
    i += bytes;
  }
  return s.substr(0, i < s.size() ? i : s.size());
}

}  // namespace

Tag parseTag(const std::string& tag) {
  const std::vector<std::string> parts = split(toLowerAscii(replaceAll(trim(tag), "_", "-")), "-");
  const std::string first = parts.empty() ? std::string() : parts[0];
  const char* known = alias(first);
  Tag out;
  out.lang = known ? known : first;
  for (size_t i = 1; i < parts.size(); i++) {
    const std::string& p = parts[i];
    if (lowerLetters(p, 4, 4)) out.script = p;
    else if ((lowerLetters(p, 2, 2) || threeDigits(p)) && out.region.empty()) out.region = p;
  }
  // (Mandarin written in traditional characters is Taiwan's)
  if (out.lang == "zh" && out.region.empty() && out.script == "hant") out.region = "tw";
  return out;
}

namespace voicepickDetail {

std::optional<VoiceFit> fit(const std::string& name, const std::string& lang, const std::string& locale) {
  const Tag want = parseTag(locale);
  const Tag has = parseTag(lang);
  if (want.lang == "zh" && cantonese(name, has)) return std::nullopt;
  if (has.lang == want.lang) return want.region.empty() || has.region == want.region ? VoiceFit::Locale : VoiceFit::Language;
  if (related(want.lang, has.lang)) return VoiceFit::Related;
  return std::nullopt;
}

double score(const std::string& name, const std::string& lang, const std::string& voiceURI, bool isDefault, const std::string& locale) {
  const std::optional<VoiceFit> f = fit(name, lang, locale);
  if (!f) return -std::numeric_limits<double>::infinity();
  const Tag want = parseTag(locale);
  const Tag has = parseTag(lang);
  // (another region – pt-BR for pt-PT, Mandarin of Taiwan for zh-CN – reads well, with another accent: behind the locale's)
  double s = *f == VoiceFit::Locale ? (!has.region.empty() || want.region.empty() ? 100 : 85) : *f == VoiceFit::Language ? 50 : 20;
  const std::string& n = name;
  // /natural|neural|premium|enhanced|wavenet|studio|journey|polyglot/i
  if (containsI(n, "natural") || containsI(n, "neural") || containsI(n, "premium") || containsI(n, "enhanced") || containsI(n, "wavenet") || containsI(n, "studio") ||
      containsI(n, "journey") || containsI(n, "polyglot"))
    s += 40;
  else if (hasWordI(n, "online")) s += 30;   // /\bonline\b/i
  if (startsWithWordI(n, "google")) s += 25;   // /^google\b/i
  if (containsI(n, "network") || endsWithI(voiceURI, "-network")) s += 10;   // /network/i, /-network$/i
  // /espeak|pico|festival|mbrola|^default$/i
  if (containsI(n, "espeak") || containsI(n, "pico") || containsI(n, "festival") || containsI(n, "mbrola") || equalsI(n, "default")) s -= 15;
  if (eloquence(n)) s -= 10;
  if (novelty(n)) s -= 80;
  if (isDefault) s += 5;
  return s;
}

}  // namespace voicepickDetail

// ------------------------------------------------------------------ the player's choices

const SpeechPref DEFAULT_PREF = {std::string(), 1, 1};

SpeechPref prefOf(const SpeechPrefs* all, const std::string& lang) {
  if (all) {
    auto it = all->find(lang);
    if (it != all->end()) return it->second;
  }
  return SpeechPref();
}

SpeechPrefs cleanSpeechPrefs(const Json& raw) {
  SpeechPrefs out;
  if (!raw.isObject()) return out;
  auto within = [](const Json& x, const std::array<double, 2>& range, double d) -> double { return x.isNumber() && x.number >= range[0] && x.number <= range[1] ? x.number : d; };
  for (const auto& [lang, p] : raw.object) {
    // /^[a-z]{2,3}$/
    if (!lowerLetters(lang, 2, 3) || !(p.isObject() || p.isArray())) continue;
    const Json& voice = p.at("voice");
    SpeechPref pref;
    pref.voice = voice.isString() ? sliceUtf16(voice.str, 300) : std::string();
    pref.rate = within(p.at("rate"), RATE_RANGE, 1);
    pref.pitch = within(p.at("pitch"), PITCH_RANGE, 1);
    out[lang] = pref;
  }
  return out;
}

}  // namespace fs
