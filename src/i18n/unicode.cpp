// SPDX-License-Identifier: AGPL-3.0-only
/** Characters as JavaScript's strings and regular expressions see them (unicode.h). */
#include "i18n/unicode.h"

#include <cstddef>
#include <cstdint>

namespace fs::i18n {

namespace {

struct Run {
  char32_t first, last;
};
struct CaseRun {
  char32_t first, last;
  int step;
  int delta;
};
struct CaseSeveral {
  char32_t from;
  char32_t to[3];
};

#include "i18n/unicode.inc"

template <size_t N>
bool inRuns(const Run (&runs)[N], char32_t c) {
  size_t lo = 0, hi = N;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (c > runs[mid].last) lo = mid + 1;
    else hi = mid;
  }
  return lo < N && c >= runs[lo].first;
}

}  // namespace

std::u32string decodeUtf8(std::string_view s) {
  std::u32string out;
  out.reserve(s.size());
  size_t i = 0;
  const size_t n = s.size();
  auto cont = [&](size_t at) { return at < n && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80; };
  while (i < n) {
    const unsigned char b = static_cast<unsigned char>(s[i]);
    if (b < 0x80) {
      out.push_back(b);
      i++;
    } else if (b >= 0xC2 && b <= 0xDF && cont(i + 1)) {
      out.push_back(static_cast<char32_t>(((b & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F)));
      i += 2;
    } else if (b >= 0xE0 && b <= 0xEF && cont(i + 1) && cont(i + 2)) {
      const char32_t c = static_cast<char32_t>(((b & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3F));
      // (too long a form, or half a surrogate pair: no character)
      if (c < 0x800 || (c >= 0xD800 && c <= 0xDFFF)) {
        out.push_back(0xFFFD);
        i++;
      } else {
        out.push_back(c);
        i += 3;
      }
    } else if (b >= 0xF0 && b <= 0xF4 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
      const char32_t c = static_cast<char32_t>(((b & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                                               (static_cast<unsigned char>(s[i + 3]) & 0x3F));
      if (c < 0x10000 || c > 0x10FFFF) {
        out.push_back(0xFFFD);
        i++;
      } else {
        out.push_back(c);
        i += 4;
      }
    } else {
      out.push_back(0xFFFD);
      i++;
    }
  }
  return out;
}

void appendUtf8(std::string& out, char32_t c) {
  if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
  if (c < 0x80) out += static_cast<char>(c);
  else if (c < 0x800) {
    out += static_cast<char>(0xC0 | (c >> 6));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    out += static_cast<char>(0xE0 | (c >> 12));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c <= 0x10FFFF) {
    out += static_cast<char>(0xF0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else appendUtf8(out, 0xFFFD);
}

std::string encodeUtf8(std::u32string_view s) {
  std::string out;
  out.reserve(s.size() * 2);
  for (char32_t c : s) appendUtf8(out, c);
  return out;
}

std::u16string toUtf16(std::string_view s) {
  std::u16string out;
  for (char32_t c : decodeUtf8(s)) {
    if (c < 0x10000) out.push_back(static_cast<char16_t>(c));
    else {
      out.push_back(static_cast<char16_t>(0xD800 + ((c - 0x10000) >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + ((c - 0x10000) & 0x3FF)));
    }
  }
  return out;
}

std::string fromUtf16(std::u16string_view s) {
  std::string out;
  out.reserve(s.size() * 2);
  for (size_t i = 0; i < s.size(); i++) {
    char32_t c = s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
      i++;
    }
    appendUtf8(out, c);
  }
  return out;
}

size_t utf16Length(std::string_view s) {
  size_t n = 0;
  for (char32_t c : decodeUtf8(s)) n += c < 0x10000 ? 1 : 2;
  return n;
}

size_t codePointLength(std::string_view s) { return decodeUtf8(s).size(); }

bool isSpace(char32_t c) {
  // WhiteSpace and LineTerminator of ECMAScript
  return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 ||
         c == 0xFEFF;
}

bool isLetter(char32_t c) { return inRuns(LETTER, c); }
bool isUpperLetter(char32_t c) { return inRuns(UPPER_LETTER, c); }
bool isLowerLetter(char32_t c) { return inRuns(LOWER_LETTER, c); }
bool isNumber(char32_t c) { return inRuns(NUMBER, c); }
bool isLatin(char32_t c) { return inRuns(LATIN, c); }
bool isCyrillic(char32_t c) { return inRuns(CYRILLIC, c); }
bool isArabic(char32_t c) { return inRuns(ARABIC, c); }
bool isPictographic(char32_t c) { return inRuns(PICTOGRAPHIC, c); }

std::string toUpperCase(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char32_t c : decodeUtf8(s)) {
    if (c < 0x80) {
      out += static_cast<char>(c >= U'a' && c <= U'z' ? c - 32 : c);
      continue;
    }
    bool done = false;
    for (const CaseSeveral& m : TO_UPPER_SEVERAL) {
      if (m.from != c) continue;
      for (char32_t u : m.to)
        if (u) appendUtf8(out, u);
      done = true;
      break;
    }
    if (done) continue;
    // (the runs lie in each other where every second letter of a block is a small one: look at all of them)
    for (const CaseRun& r : TO_UPPER) {
      if (c < r.first || c > r.last || (c - r.first) % static_cast<char32_t>(r.step) != 0) continue;
      c = static_cast<char32_t>(static_cast<int64_t>(c) + r.delta);
      break;
    }
    appendUtf8(out, c);
  }
  return out;
}

std::string trimJs(std::string_view s) {
  const std::u32string u = decodeUtf8(s);
  size_t a = 0, b = u.size();
  while (a < b && isSpace(u[a])) a++;
  while (b > a && isSpace(u[b - 1])) b--;
  return encodeUtf8(std::u32string_view(u).substr(a, b - a));
}

}  // namespace fs::i18n
