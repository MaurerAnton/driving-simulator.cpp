// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Reading the language files (the .json files of web/lang): objects of texts, read in the
 * order they are written.  (platform/json.h keeps an object's members sorted by name; the texts' order
 * is the order of the original's files, which the tools and the trainer of the signs go by.)
 *
 * A cursor: the caller says what it expects next, and gets `false` as soon as the text is
 * not that – a file that is cut off or not JSON at all is refused as a whole.
 */
#pragma once

#include <string>
#include <string_view>

namespace fs::i18n {

class JsonCursor {
 public:
  explicit JsonCursor(std::string_view text) : s_(text) {}

  /** the next character that is not white space (0 at the end) */
  char peek();
  /** step over `c` if it is the next thing */
  bool take(char c);
  /** a string: its text, the escapes resolved (UTF-8) */
  bool string(std::string& out);
  /** step over one value, whatever it is */
  bool skip();
  /** nothing but white space is left */
  bool atEnd();

  /** `{ "name": value, … }`: member(name) reads the value (false from it: the file is refused) */
  template <class F>
  bool object(F&& member) {
    if (!take('{')) return false;
    if (take('}')) return true;
    std::string name;
    for (;;) {
      if (!string(name) || !take(':')) return false;
      if (!member(name)) return false;
      if (take(',')) continue;
      return take('}');
    }
  }

  /** `[ value, … ]`: element() reads each value */
  template <class F>
  bool array(F&& element) {
    if (!take('[')) return false;
    if (take(']')) return true;
    for (;;) {
      if (!element()) return false;
      if (take(',')) continue;
      return take(']');
    }
  }

 private:
  void space();
  bool skip(int depth);

  std::string_view s_;
  size_t i_ = 0;
};

}  // namespace fs::i18n
