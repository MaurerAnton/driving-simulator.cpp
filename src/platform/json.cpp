// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A very small JSON reader for the reality providers.
 *
 * Origin: the `isObj`/`str`/`num` helpers of src/reality/providers.ts —
 * those read a parsed object; here the document is parsed too.
 */
#include "platform/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fs {

namespace {
const Json kNull;

class Parser {
 public:
  explicit Parser(const std::string& t) : s_(t) {}

  bool parse(Json& out) {
    if (!value(out)) return false;
    skipWs();
    return i_ == s_.size();
  }

 private:
  const std::string& s_;
  size_t i_ = 0;

  void skipWs() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t'))
      i_++;
  }

  bool lit(const char* w) {
    size_t n = 0;
    while (w[n]) n++;
    if (s_.compare(i_, n, w) != 0) return false;
    i_ += n;
    return true;
  }

  bool value(Json& out) {
    skipWs();
    if (i_ >= s_.size()) return false;
    char c = s_[i_];
    if (c == '{') return object(out);
    if (c == '[') return array(out);
    if (c == '"') {
      out.kind = Json::Kind::String;
      return string(out.str);
    }
    if (lit("true")) {
      out.kind = Json::Kind::Bool;
      out.boolean = true;
      return true;
    }
    if (lit("false")) {
      out.kind = Json::Kind::Bool;
      out.boolean = false;
      return true;
    }
    if (lit("null")) {
      out.kind = Json::Kind::Null;
      return true;
    }
    return number(out);
  }

  bool object(Json& out) {
    out.kind = Json::Kind::Object;
    if (s_[i_] != '{') return false;
    i_++;
    skipWs();
    if (i_ < s_.size() && s_[i_] == '}') {
      i_++;
      return true;
    }
    for (;;) {
      skipWs();
      std::string key;
      if (!string(key)) return false;
      skipWs();
      if (i_ >= s_.size() || s_[i_] != ':') return false;
      i_++;
      Json v;
      if (!value(v)) return false;
      out.object[key] = std::move(v);
      skipWs();
      if (i_ < s_.size() && s_[i_] == ',') {
        i_++;
        continue;
      }
      if (i_ < s_.size() && s_[i_] == '}') {
        i_++;
        return true;
      }
      return false;
    }
  }

  bool array(Json& out) {
    out.kind = Json::Kind::Array;
    if (s_[i_] != '[') return false;
    i_++;
    skipWs();
    if (i_ < s_.size() && s_[i_] == ']') {
      i_++;
      return true;
    }
    for (;;) {
      Json v;
      if (!value(v)) return false;
      out.array.push_back(std::move(v));
      skipWs();
      if (i_ < s_.size() && s_[i_] == ',') {
        i_++;
        continue;
      }
      if (i_ < s_.size() && s_[i_] == ']') {
        i_++;
        return true;
      }
      return false;
    }
  }

  bool string(std::string& out) {
    skipWs();
    if (i_ >= s_.size() || s_[i_] != '"') return false;
    i_++;
    out.clear();
    while (i_ < s_.size() && s_[i_] != '"') {
      if (s_[i_] == '\\' && i_ + 1 < s_.size()) {
        i_++;
        char c = s_[i_];
        switch (c) {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'u': {
            if (i_ + 4 < s_.size()) {
              unsigned code = 0;
              for (int k = 1; k <= 4; k++) {
                char h = s_[i_ + static_cast<size_t>(k)];
                code = code * 16 + static_cast<unsigned>(h <= '9' ? h - '0' : ((h | 32) - 'a' + 10));
              }
              i_ += 4;
              if (code < 0x80) out += static_cast<char>(code);
              else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
              } else {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
              }
            }
            break;
          }
          default: out += c; break;
        }
      } else {
        out += s_[i_];
      }
      i_++;
    }
    if (i_ >= s_.size()) return false;
    i_++;
    return true;
  }

  bool number(Json& out) {
    skipWs();
    size_t start = i_;
    if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) i_++;
    while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.' ||
                              s_[i_] == 'e' || s_[i_] == 'E' || s_[i_] == '-' || s_[i_] == '+'))
      i_++;
    if (i_ == start) return false;
    out.kind = Json::Kind::Number;
    out.number = std::strtod(s_.substr(start, i_ - start).c_str(), nullptr);
    return true;
  }
};
}  // namespace

std::string Json::asStr() const {
  if (kind == Kind::String) return str;
  if (kind == Kind::Number) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10g", number);
    return buf;
  }
  return "";
}

double Json::asNum() const {
  if (kind == Kind::Number) return number;
  if (kind == Kind::String && !str.empty()) return std::strtod(str.c_str(), nullptr);
  return NAN;
}

const Json& Json::at(const std::string& key) const {
  auto it = object.find(key);
  return it == object.end() ? kNull : it->second;
}

bool Json::parse(const std::string& text, Json& out) {
  Parser p(text);
  Json v;
  if (!p.parse(v)) return false;
  out = std::move(v);
  return true;
}

}  // namespace fs
