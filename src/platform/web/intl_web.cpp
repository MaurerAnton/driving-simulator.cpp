// SPDX-License-Identifier: AGPL-3.0-only
/** platform/intl.h in the browser: the real Intl (platform/web/library_intl.js). */
#include <emscripten.h>

#include <cstdlib>
#include <unordered_map>

#include "platform/intl.h"

extern "C" {
char* fsi_error();
char* fsi_number(double value, const char* locale, const char* options);
char* fsi_date(double ms, const char* locale, const char* options);
char* fsi_list(const char* items, int count, const char* locale, const char* options);
void fsi_keyboard(int id);
}

namespace fs::plat {

namespace {

std::string take(char* s) {
  std::string out = s ? s : "";
  std::free(s);
  return out;
}

/** the text Intl gave – or what it threw */
std::string answer(char* s) {
  if (!s) throw IntlError(take(fsi_error()));
  return take(s);
}

std::unordered_map<int, Resolver<std::optional<KeyboardLayout>>>& asked() {
  static std::unordered_map<int, Resolver<std::optional<KeyboardLayout>>> m;
  return m;
}
int nextId = 1;

}  // namespace

std::string formatNumber(double value, const std::string& locale, const NumberFormat& options) {
  return answer(fsi_number(value, locale.c_str(), optionsJson(options).c_str()));
}

std::string formatDate(double ms, const std::string& locale, const DateTimeFormat& options) {
  if (ms != ms) throw IntlError("Invalid time value");
  return answer(fsi_date(ms, locale.c_str(), optionsJson(options).c_str()));
}

std::string formatList(const std::vector<std::string>& items, const std::string& locale, const ListFormat& options) {
  std::string joined;
  for (size_t i = 0; i < items.size(); i++) {
    if (i) joined += '\n';
    joined += items[i];
  }
  return answer(fsi_list(joined.c_str(), static_cast<int>(items.size()), locale.c_str(), optionsJson(options).c_str()));
}

Task<std::optional<KeyboardLayout>> keyboardLayoutMap() {
  int id = nextId++;
  Resolver<std::optional<KeyboardLayout>> r;
  asked().emplace(id, r);
  fsi_keyboard(id);
  return r.task();
}

namespace native {
void setKeyboardLayout(std::optional<KeyboardLayout>) {}
}  // namespace native

}  // namespace fs::plat

/** The keyboard's layout has come: "code\tkey\n…" – or null where the browser has none. */
extern "C" EMSCRIPTEN_KEEPALIVE void fsi_keyboard_done(int id, const char* text) {
  using namespace fs::plat;
  auto it = asked().find(id);
  if (it == asked().end()) return;
  fs::Resolver<std::optional<KeyboardLayout>> r = it->second;
  asked().erase(it);
  if (!text) {
    r.resolve(std::nullopt);
    return;
  }
  KeyboardLayout layout;
  const char* p = text;
  while (*p) {
    const char* tab = p;
    while (*tab && *tab != '\t' && *tab != '\n') tab++;
    if (*tab != '\t') break;
    const char* end = tab + 1;
    while (*end && *end != '\n') end++;
    layout.emplace(std::string(p, tab), std::string(tab + 1, end));
    p = *end ? end + 1 : end;
  }
  r.resolve(std::optional<KeyboardLayout>(std::move(layout)));
}
