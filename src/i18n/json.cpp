// SPDX-License-Identifier: AGPL-3.0-only
/** Reading the language files (json.h). */
#include "i18n/json.h"

#include "i18n/unicode.h"

namespace fs::i18n {

void JsonCursor::space() {
  while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) i_++;
}

char JsonCursor::peek() {
  space();
  return i_ < s_.size() ? s_[i_] : '\0';
}

bool JsonCursor::take(char c) {
  space();
  if (i_ >= s_.size() || s_[i_] != c) return false;
  i_++;
  return true;
}

bool JsonCursor::atEnd() {
  space();
  return i_ == s_.size();
}

bool JsonCursor::string(std::string& out) {
  space();
  if (i_ >= s_.size() || s_[i_] != '"') return false;
  i_++;
  out.clear();
  auto hex4 = [&](size_t at, unsigned& code) {
    if (at + 4 > s_.size()) return false;
    code = 0;
    for (size_t k = 0; k < 4; k++) {
      const char h = s_[at + k];
      unsigned d;
      if (h >= '0' && h <= '9') d = static_cast<unsigned>(h - '0');
      else if (h >= 'a' && h <= 'f') d = static_cast<unsigned>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') d = static_cast<unsigned>(h - 'A' + 10);
      else return false;
      code = code * 16 + d;
    }
    return true;
  };
  for (;;) {
    // (the text between two escapes in one piece: nearly all of a language file)
    const size_t from = i_;
    while (i_ < s_.size() && s_[i_] != '"' && s_[i_] != '\\' && static_cast<unsigned char>(s_[i_]) >= 0x20) i_++;
    out.append(s_.substr(from, i_ - from));
    if (i_ >= s_.size()) return false;
    const char c = s_[i_++];
    if (c == '"') return true;
    // (a line break or another control character inside a string is not JSON)
    if (c != '\\' || i_ >= s_.size()) return false;
    const char e = s_[i_++];
    switch (e) {
      case '"': out += '"'; break;
      case '\\': out += '\\'; break;
      case '/': out += '/'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'u': {
        unsigned code = 0;
        if (!hex4(i_, code)) return false;
        i_ += 4;
        char32_t cp = code;
        // (a character beyond U+FFFF is written as two halves)
        unsigned low = 0;
        if (code >= 0xD800 && code <= 0xDBFF && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u' && hex4(i_ + 2, low) && low >= 0xDC00 && low <= 0xDFFF) {
          cp = 0x10000 + ((static_cast<char32_t>(code) - 0xD800) << 10) + (low - 0xDC00);
          i_ += 6;
        }
        appendUtf8(out, cp);
        break;
      }
      default: return false;
    }
  }
}

bool JsonCursor::skip() { return skip(0); }

bool JsonCursor::skip(int depth) {
  if (depth > 64) return false;
  const char c = peek();
  if (c == '"') {
    std::string unused;
    return string(unused);
  }
  if (c == '{') return object([&](const std::string&) { return skip(depth + 1); });
  if (c == '[') return array([&] { return skip(depth + 1); });
  // true, false, null, a number: up to the next sign of the structure
  const size_t from = i_;
  while (i_ < s_.size() && s_[i_] != ',' && s_[i_] != '}' && s_[i_] != ']' && s_[i_] != ' ' && s_[i_] != '\n' && s_[i_] != '\r' && s_[i_] != '\t') i_++;
  const std::string_view word = s_.substr(from, i_ - from);
  if (word == "true" || word == "false" || word == "null") return true;
  // -?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?
  size_t k = 0;
  auto digits = [&] {
    const size_t start = k;
    while (k < word.size() && word[k] >= '0' && word[k] <= '9') k++;
    return k - start;
  };
  if (k < word.size() && word[k] == '-') k++;
  const size_t whole = k;
  if (digits() == 0 || (word[whole] == '0' && k - whole > 1)) return false;
  if (k < word.size() && word[k] == '.') {
    k++;
    if (digits() == 0) return false;
  }
  if (k < word.size() && (word[k] == 'e' || word[k] == 'E')) {
    k++;
    if (k < word.size() && (word[k] == '+' || word[k] == '-')) k++;
    if (digits() == 0) return false;
  }
  return k == word.size();
}

}  // namespace fs::i18n
