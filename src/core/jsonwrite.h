// SPDX-License-Identifier: AGPL-3.0-only
/**
 * JSON written the way JavaScript's JSON.stringify writes it (without indentation):
 * strings in quotes with its escapes, numbers as String(x) (fs::jsStr) – and `null` for
 * what JSON has no word for (NaN, ±Infinity).  What the game keeps in the browser's
 * storage (settings, logs) is written with this, so that the original reads it as its own.
 * Reading is platform/json.h's.
 *
 *     JsonOut o;                       // JSON.stringify({ t: 12, id: 'speed', line: [1, 2] })
 *     o.beginObject();
 *     o.key("t");    o.number(12);
 *     o.key("id");   o.string("speed");
 *     o.key("line"); o.beginArray(); o.number(1); o.number(2); o.endArray();
 *     o.endObject();
 *     store.setItem(KEY, o.text());
 */
#pragma once

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "core/js.h"

namespace fs {

/** `JSON.stringify(s)` of a string, put at the end of `out`: in quotes, with the escapes JavaScript writes. */
inline void jsonQuote(std::string& out, std::string_view s) {
  static const char* const HEX = "0123456789abcdef";
  out += '"';
  for (char ch : s) {
    const unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out += HEX[c >> 4];
          out += HEX[c & 15];
        } else out += ch;
    }
  }
  out += '"';
}

/** `JSON.stringify(s)` of a string */
inline std::string jsonString(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  jsonQuote(out, s);
  return out;
}

/** `JSON.stringify(x)` of a number */
inline std::string jsonNumber(double x) { return std::isfinite(x) ? jsStr(x) : "null"; }

/** A JSON text written piece by piece; the commas come by themselves. */
class JsonOut {
 public:
  void beginObject() { open('{'); }
  void endObject() { shut('}'); }
  void beginArray() { open('['); }
  void endArray() { shut(']'); }
  /** the name of the next member of the object being written */
  void key(std::string_view k) {
    value();
    jsonQuote(out_, k);
    out_ += ':';
    afterKey_ = true;
  }
  void number(double v) {
    value();
    out_ += jsonNumber(v);
  }
  void string(std::string_view s) {
    value();
    jsonQuote(out_, s);
  }
  void boolean(bool b) {
    value();
    out_ += b ? "true" : "false";
  }
  void null() {
    value();
    out_ += "null";
  }
  /** a piece of JSON as it is (a value written elsewhere) */
  void raw(std::string_view json) {
    value();
    out_ += json;
  }

  const std::string& text() const { return out_; }

 private:
  std::string out_;
  /** per array or object that is open: has it a value already (the next one needs a comma)? */
  std::vector<char> filled_;
  bool afterKey_ = false;

  void value() {
    if (afterKey_) {
      afterKey_ = false;
      return;
    }
    if (filled_.empty()) return;
    if (filled_.back()) out_ += ',';
    filled_.back() = 1;
  }
  void open(char c) {
    value();
    out_ += c;
    filled_.push_back(0);
  }
  void shut(char c) {
    filled_.pop_back();
    out_ += c;
  }
};

}  // namespace fs
