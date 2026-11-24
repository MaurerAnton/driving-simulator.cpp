// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/keys.ts @ v1.7.7
#include "i18n/keys.h"

#include <optional>
#include <unordered_map>

#include "core/js.h"
#include "i18n/unicode.h"
#include "platform/intl.h"

namespace fs {

namespace {

/** `let layout: { get(code) } | null`: what the browser told (nullopt: nothing, or not asked yet) */
std::optional<plat::KeyboardLayout>& layout() {
  static std::optional<plat::KeyboardLayout> map;
  return map;
}

/** `layout?.get(code)` */
const std::string* layoutGet(std::string_view code) {
  const std::optional<plat::KeyboardLayout>& map = layout();
  if (!map) return nullptr;
  auto it = map->find(std::string(code));
  return it == map->end() ? nullptr : &it->second;
}

/** Languages whose usual keyboard swaps Y and Z. */
bool qwertz(std::string_view lang) { return lang == "de" || lang == "hr" || lang == "hu" || lang == "cs" || lang == "sq"; }

/** The French layout, as far as the keys used here differ from a US keyboard. */
const char* azerty(std::string_view code) {
  if (code == "KeyQ") return "A";
  if (code == "KeyA") return "Q";
  if (code == "KeyW") return "Z";
  if (code == "KeyZ") return "W";
  if (code == "KeyM") return ",";
  return nullptr;
}

const char* arrow(std::string_view code) {
  if (code == "ArrowUp") return "↑";
  if (code == "ArrowDown") return "↓";
  if (code == "ArrowLeft") return "←";
  if (code == "ArrowRight") return "→";
  return nullptr;
}

/**
 * The control on the screen that does what the key does.  Values with an
 * "@" name a text key (labels of the "⋯" sheet).
 */
const char* touch(std::string_view code) {
  static const std::unordered_map<std::string_view, const char*> TOUCH = {
      {"KeyW", "@tbGas"},
      {"KeyS", "@tbBrake"},
      {"KeyE", "▲"},
      {"Digit1", "▲"},
      {"KeyQ", "▼"},
      {"KeyR", "▼"},
      {"Space", "P"},
      {"KeyZ", "◀"},
      {"KeyC", "▶"},
      {"Enter", "⋯ @tbStarter"},
      {"KeyB", "⋯ @tbBelt"},
      {"KeyI", "⋯ @tbIgnition"},
      {"KeyX", "⋯ @tbHazard"},
      {"KeyJ", "⋯ @tbLookLeft"},
      {"KeyK", "⋯ @tbLookRight"},
      {"KeyL", "⋯ @tbLights"},
      {"KeyO", "⋯ @tbHighBeam"},
      {"KeyH", "⋯ @tbHorn"},
      {"KeyV", "⋯ @tbCamera"},
      {"KeyT", "⋯ @tbReset"},
      {"KeyM", "⋯ @tbMenu"},
      {"Escape", "⋯ @tbMenu"},
  };
  auto it = TOUCH.find(code);
  return it == TOUCH.end() ? nullptr : it->second;
}

/** `s.replace(/\s*[(（][^)）]*[)）]\s*$/, '')`: without what stands in brackets at its end */
std::string withoutBrackets(const std::string& s) {
  const std::u32string u = i18n::decodeUtf8(s);
  const size_t n = u.size();
  const char32_t OPEN = 0xFF08, CLOSE = 0xFF09;   // the full-width brackets of Chinese and Japanese
  for (size_t i = 0; i < n; i++) {
    size_t j = i;
    while (j < n && i18n::isSpace(u[j])) j++;
    if (j >= n || (u[j] != U'(' && u[j] != OPEN)) continue;
    j++;
    while (j < n && u[j] != U')' && u[j] != CLOSE) j++;
    if (j >= n) continue;
    j++;
    while (j < n && i18n::isSpace(u[j])) j++;
    if (j == n) return i18n::encodeUtf8(std::u32string_view(u).substr(0, i));
  }
  return s;
}

}  // namespace

Task<void> initKeyLabels() {
  // (not available: the usual layout of the language is assumed)
  std::optional<plat::KeyboardLayout> map = co_await plat::keyboardLayoutMap();
  if (map) layout() = std::move(map);
}

std::string keyLabel(std::string_view code, std::string_view lang, const KeyNameFn& name) {
  if (code == "Space") return name("key.Space");
  if (code == "Enter" || code == "NumpadEnter") return name("key.Enter");
  if (code == "ShiftLeft" || code == "ShiftRight") return name("key.Shift");
  if (code == "Escape") return name("key.Escape");
  if (const char* a = arrow(code)) return a;
  if (code.substr(0, 5) == "Digit") return std::string(code.substr(5));
  if (code.substr(0, 3) == "Key") {
    const std::string* real = layoutGet(code);
    if (real && i18n::utf16Length(*real) == 1) return i18n::toUpperCase(*real);
    const std::string letter(code.substr(3));
    if (qwertz(lang)) return letter == "Z" ? "Y" : letter == "Y" ? "Z" : letter;
    if (lang == "fr") {
      const char* a = azerty(code);
      return a ? a : letter;
    }
    return letter;
  }
  // (the full stop: on foot, opening what one stands at – game/openplay.ts)
  if (code == "Period") {
    const std::string* real = layoutGet(code);
    return real ? *real : ".";
  }
  return std::string(code);   // F1 …
}

std::string touchLabel(std::string_view code, std::string_view lang, const KeyNameFn& name) {
  const char* found = touch(code);
  if (!found) return keyLabel(code, lang, name);
  const std::string_view t = found;
  // the name of the button, without what it says in brackets ("Starter (hold)" → "Starter")
  // t.replace(/@(\w+)/, …): the first "@" with a word behind it
  for (size_t at = t.find('@'); at != std::string_view::npos; at = t.find('@', at + 1)) {
    size_t end = at + 1;
    while (end < t.size() && i18n::isWord(static_cast<unsigned char>(t[end]))) end++;
    if (end == at + 1) continue;
    return std::string(t.substr(0, at)) + withoutBrackets(name(t.substr(at + 1, end - at - 1))) + std::string(t.substr(end));
  }
  return std::string(t);
}

}  // namespace fs
