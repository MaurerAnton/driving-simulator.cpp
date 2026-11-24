// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/addresspicker.ts @ v1.7.7
#include "ui/addresspicker.h"

namespace fs {

namespace detail {
namespace {
int pickers = 0;
}
int nextAddressPicker() { return ++pickers; }
}  // namespace detail

// `/^(?:[a-zäöüß]{1,3}\d{1,3}|\d{1,3})$/i.test(q.trim())`
bool isLineToken(const std::string& q) {
  const std::string s = trim(q);
  size_t i = 0;
  int letters = 0;
  for (;;) {
    if (i >= s.size()) break;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      i++;
      letters++;
      continue;
    }
    // ä ö ü ß and (the pattern ignores case) Ä Ö Ü: two bytes each, C3 xx
    if (c == 0xC3 && i + 1 < s.size()) {
      const unsigned char d = static_cast<unsigned char>(s[i + 1]);
      if (d == 0xA4 || d == 0xB6 || d == 0xBC || d == 0x9F || d == 0x84 || d == 0x96 || d == 0x9C) {
        i += 2;
        letters++;
        continue;
      }
    }
    break;
  }
  int digits = 0;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    i++;
    digits++;
  }
  if (i != s.size()) return false;
  return letters <= 3 && digits >= 1 && digits <= 3;
}

}  // namespace fs
