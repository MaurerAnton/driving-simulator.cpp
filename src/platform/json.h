// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A very small JSON reader: the reality providers only need to pull numbers,
 * strings and nested objects out of a response, so a full DOM is not worth
 * the bytes.  The parser is strict enough to reject malformed documents and
 * never throws past its caller.
 *
 * Origin: the `isObj`/`str`/`num` helpers of src/reality/providers.ts.
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs {

class Json {
 public:
  enum class Kind { Null, Bool, Number, String, Array, Object };

  Kind kind = Kind::Null;
  bool boolean = false;
  double number = 0;
  std::string str;
  std::vector<Json> array;
  std::map<std::string, Json> object;

  bool isObject() const { return kind == Kind::Object; }
  bool isArray() const { return kind == Kind::Array; }
  bool isString() const { return kind == Kind::String; }
  bool isNumber() const { return kind == Kind::Number; }

  /** The value as a string ('' for anything but string/number). */
  std::string asStr() const;
  /** The value as a number (NaN when it is not one). */
  double asNum() const;

  /** Object member, or a null value when absent (never throws). */
  const Json& at(const std::string& key) const;
  bool has(const std::string& key) const { return object.count(key) > 0; }

  /** Parse a document; returns false on malformed input. */
  static bool parse(const std::string& text, Json& out);
};

}  // namespace fs
