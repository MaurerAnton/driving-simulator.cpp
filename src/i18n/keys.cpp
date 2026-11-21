// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The labels of the keys, as the original writes them (src/i18n/keys.ts).
 *
 * Origin: src/i18n/keys.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#include "i18n/keys.h"

#include <algorithm>

namespace fs {

namespace {
/** the letters the browser reports for the codes (navigator.keyboard.getLayoutMap) */
std::map<std::string, std::string> g_layout;

/** Languages whose usual keyboard swaps Y and Z. */
bool qwertz(const std::string& lang) {
  return lang == "de" || lang == "hr" || lang == "hu" || lang == "cs" || lang == "sq";
}

/** The French layout, as far as the keys used here differ from a US keyboard. */
std::string azerty(const std::string& code) {
  if (code == "KeyQ") return "A";
  if (code == "KeyA") return "Q";
  if (code == "KeyW") return "Z";
  if (code == "KeyZ") return "W";
  if (code == "KeyM") return ",";
  return "";
}
}  // namespace

void setKeyLayout(std::map<std::string, std::string> layout) { g_layout = std::move(layout); }

std::string keyLabel(const std::string& code, const std::string& lang,
                     const std::function<std::string(const std::string&)>& name) {
  if (code == "Space") return name("key.Space");
  if (code == "Enter" || code == "NumpadEnter") return name("key.Enter");
  if (code == "ShiftLeft" || code == "ShiftRight") return name("key.Shift");
  if (code == "Escape") return name("key.Escape");
  if (code == "ArrowUp") return "\xe2\x86\x91";    // ↑
  if (code == "ArrowDown") return "\xe2\x86\x93";  // ↓
  if (code == "ArrowLeft") return "\xe2\x86\x90";  // ←
  if (code == "ArrowRight") return "\xe2\x86\x92"; // →
  if (code.rfind("Digit", 0) == 0 && code.size() == 6) return code.substr(5);
  if (code.rfind("Key", 0) == 0 && code.size() == 4) {
    auto it = g_layout.find(code);
    if (it != g_layout.end() && it->second.size() == 1) {
      std::string real = it->second;
      if (real[0] >= 'a' && real[0] <= 'z') real[0] = static_cast<char>(real[0] - 'a' + 'A');
      return real;
    }
    std::string letter = code.substr(3);
    if (qwertz(lang)) {
      if (letter == "Z") return "Y";
      if (letter == "Y") return "Z";
      return letter;
    }
    if (lang == "fr") {
      std::string a = azerty(code);
      if (!a.empty()) return a;
    }
    return letter;
  }
  return code;  // F1 …
}

std::string touchLabel(const std::string& code, const std::string& lang,
                       const std::function<std::string(const std::string&)>& name) {
  // values starting with "@" are text keys (labels of the "⋯" sheet)
  static const std::map<std::string, std::string> TOUCH = {
      {"KeyW", "@tbGas"},         {"KeyS", "@tbBrake"},  {"KeyE", "\xe2\x96\xb2"},  // ▲
      {"Digit1", "\xe2\x96\xb2"}, {"KeyQ", "\xe2\x96\xbc"},                        // ▼
      {"KeyR", "\xe2\x96\xbc"},   {"Space", "P"},        {"KeyZ", "\xe2\x97\x80"},  // ◀
      {"KeyC", "\xe2\x96\xb6"},   {"Enter", "\xe2\x8b\x8f @tbStarter"},             // ⋯
      {"KeyB", "\xe2\x8b\x8f @tbBelt"},
      {"KeyI", "\xe2\x8b\x8f @tbIgnition"},
      {"KeyX", "\xe2\x8b\x8f @tbHazard"},
      {"KeyJ", "\xe2\x8b\x8f @tbLookLeft"},
      {"KeyK", "\xe2\x8b\x8f @tbLookRight"},
      {"KeyL", "\xe2\x8b\x8f @tbLights"},
      {"KeyO", "\xe2\x8b\x8f @tbHighBeam"},
      {"KeyH", "\xe2\x8b\x8f @tbHorn"},
      {"KeyV", "\xe2\x8b\x8f @tbCamera"},
      {"KeyT", "\xe2\x8b\x8f @tbReset"},
      {"KeyM", "\xe2\x8b\x8f @tbMenu"},
      {"Escape", "\xe2\x8b\x8f @tbMenu"},
  };
  auto it = TOUCH.find(code);
  if (it == TOUCH.end()) return keyLabel(code, lang, name);
  // the name of the button, without what it says in brackets ("Starter (hold)" → "Starter")
  std::string t = it->second;
  size_t at = t.find('@');
  if (at == std::string::npos) return t;
  std::string head = t.substr(0, at);
  std::string label = name(t.substr(at + 1));
  // strip a trailing "(…)" / "（…）"
  size_t open = label.find_last_of("(\xef\xbc\x88");   // ( and （
  if (open != std::string::npos) {
    size_t close = label.find_first_of(")\xef\xbc\x89", open);  // ) and ）
    if (close != std::string::npos) label.erase(open, close - open + 1);
  }
  // trim the spaces that were around the bracket
  while (!label.empty() && (label.back() == ' ' || label.front() == ' ')) {
    if (label.back() == ' ') label.pop_back();
    if (!label.empty() && label.front() == ' ') label.erase(label.begin());
  }
  return head + label;
}

}  // namespace fs
