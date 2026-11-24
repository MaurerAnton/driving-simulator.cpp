// SPDX-License-Identifier: AGPL-3.0-only
/**
 * JSON read and written as JavaScript does it: the reader that builds nothing, the writer,
 * and the document in memory (see json.h).
 */
#include "platform/json.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "core/js.h"

namespace fs {

namespace {

/** how deep arrays and objects may be nested (a text that goes deeper is not read: it would only be an attack on the stack) */
const int MAX_DEPTH = 512;

/** objects with more members than this get a table of where each name is */
const size_t INDEXED_FROM = 12;

const double P10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                      1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

bool hex4(const char* s, const char* end, unsigned& out) {
  if (end - s < 4) return false;
  unsigned v = 0;
  for (int i = 0; i < 4; i++) {
    char c = s[i];
    unsigned d;
    if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
    else return false;
    v = v * 16 + d;
  }
  out = v;
  return true;
}

void appendUtf8(std::string& out, unsigned cp) {
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

/** Is the name a whole number as JavaScript counts array places (0 … 2^32 − 2, written plainly)? */
bool arrayIndex(std::string_view key, uint32_t& out) {
  if (key.empty() || key.size() > 10) return false;
  if (key[0] == '0' && key.size() > 1) return false;
  uint64_t v = 0;
  for (char c : key) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<unsigned>(c - '0');
  }
  if (v > 4294967294ull) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

void quote(std::string& out, std::string_view s) {
  static const char* const HEX = "0123456789abcdef";
  out += '"';
  size_t run = 0;
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    const char* esc = nullptr;
    char buf[7];
    if (c == '"') esc = "\\\"";
    else if (c == '\\') esc = "\\\\";
    else if (c >= 0x20) continue;
    else if (c == '\b') esc = "\\b";
    else if (c == '\f') esc = "\\f";
    else if (c == '\n') esc = "\\n";
    else if (c == '\r') esc = "\\r";
    else if (c == '\t') esc = "\\t";
    else {
      buf[0] = '\\';
      buf[1] = 'u';
      buf[2] = '0';
      buf[3] = '0';
      buf[4] = HEX[c >> 4];
      buf[5] = HEX[c & 15];
      buf[6] = 0;
      esc = buf;
    }
    out.append(s.data() + run, i - run);
    out += esc;
    run = i + 1;
  }
  out.append(s.data() + run, s.size() - run);
  out += '"';
}

}  // namespace

// ---------------------------------------------------------------------------
// JsonReader
// ---------------------------------------------------------------------------

bool JsonReader::atEnd() {
  ws();
  return ok_ && p_ >= end_;
}

JsonReader::Type JsonReader::peek() {
  ws();
  if (!ok_ || p_ >= end_) return Type::End;
  switch (*p_) {
    case '{': return Type::Object;
    case '[': return Type::Array;
    case '"': return Type::String;
    case 't':
    case 'f': return Type::Bool;
    case 'n': return Type::Null;
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': return Type::Number;
    default: return Type::Invalid;
  }
}

bool JsonReader::literal(const char* word, size_t n) {
  if (static_cast<size_t>(end_ - p_) < n || std::memcmp(p_, word, n) != 0) {
    fail();
    return false;
  }
  p_ += n;
  return true;
}

bool JsonReader::number(double& out) {
  if (peek() == Type::Number) return parseNumber(out);
  skip();  // (fails at the end of the text and at what is no value at all)
  return false;
}

bool JsonReader::boolean(bool& out) {
  if (peek() != Type::Bool) {
    skip();
    return false;
  }
  if (*p_ == 't') {
    if (!literal("true", 4)) return false;
    out = true;
  } else {
    if (!literal("false", 5)) return false;
    out = false;
  }
  return true;
}

bool JsonReader::string(std::string& out) {
  if (peek() != Type::String) {
    skip();
    return false;
  }
  return parseString(&out);
}

bool JsonReader::null() {
  if (peek() != Type::Null) return false;
  return literal("null", 4);
}

double JsonReader::number() {
  double v = JS_NAN;
  number(v);
  return v;
}

std::string JsonReader::string() {
  std::string s;
  string(s);
  return s;
}

void JsonReader::skip() {
  switch (peek()) {
    case Type::Null: literal("null", 4); break;
    case Type::Bool:
      if (*p_ == 't') literal("true", 4);
      else literal("false", 5);
      break;
    case Type::Number: {
      double d;
      parseNumber(d);
      break;
    }
    case Type::String: parseString(nullptr); break;
    case Type::Array:
      array([this] { skip(); });
      break;
    case Type::Object:
      object([this](std::string_view) { skip(); });
      break;
    default: fail();
  }
}

bool JsonReader::open(char bracket) {
  ws();
  if (!ok_) return false;
  if (p_ >= end_ || *p_ != bracket) {
    skip();
    return false;
  }
  if (depth_ >= MAX_DEPTH) {
    fail();
    return false;
  }
  depth_++;
  p_++;
  return true;
}

bool JsonReader::close(char bracket) {
  ws();
  if (p_ < end_ && *p_ == bracket) {
    p_++;
    depth_--;
    return true;
  }
  return false;
}

bool JsonReader::next(char bracket) {
  ws();
  if (p_ < end_ && *p_ == ',') {
    p_++;
    return true;
  }
  if (p_ < end_ && *p_ == bracket) {
    p_++;
    return false;
  }
  fail();
  return false;
}

bool JsonReader::readKey(std::string_view& key) {
  ws();
  if (p_ >= end_ || *p_ != '"') {
    fail();
    return false;
  }
  // (nearly every key is plain: it is read where it stands)
  const char* s = p_ + 1;
  while (s < end_ && *s != '"' && *s != '\\' && static_cast<unsigned char>(*s) >= 0x20) s++;
  if (s < end_ && *s == '"') {
    key = std::string_view(p_ + 1, static_cast<size_t>(s - p_ - 1));
    p_ = s + 1;
  } else {
    if (!parseString(&keyBuf_)) return false;
    key = keyBuf_;
  }
  ws();
  if (p_ >= end_ || *p_ != ':') {
    fail();
    return false;
  }
  p_++;
  ws();
  return true;
}

bool JsonReader::parseNumber(double& out) {
  const char* s = p_;
  bool neg = false;
  if (s < end_ && *s == '-') {
    neg = true;
    s++;
  }
  const char* const digitsFrom = s;
  if (s >= end_) {
    fail();
    return false;
  }
  // the digits as one whole number (up to 19 of them), and the power of ten that goes with it
  uint64_t m = 0;
  int taken = 0;
  bool more = false;
  int e10 = 0;
  if (*s == '0') s++;
  else if (*s >= '1' && *s <= '9') {
    while (s < end_ && *s >= '0' && *s <= '9') {
      if (taken < 19) {
        m = m * 10 + static_cast<unsigned>(*s - '0');
        taken++;
      } else {
        more = true;
        e10++;
      }
      s++;
    }
  } else {
    fail();
    return false;
  }
  if (s < end_ && *s == '.') {
    s++;
    if (s >= end_ || *s < '0' || *s > '9') {
      fail();
      return false;
    }
    while (s < end_ && *s >= '0' && *s <= '9') {
      if (taken < 19) {
        m = m * 10 + static_cast<unsigned>(*s - '0');
        if (m != 0) taken++;
        e10--;
      } else {
        more = true;
      }
      s++;
    }
  }
  if (s < end_ && (*s == 'e' || *s == 'E')) {
    s++;
    bool eneg = false;
    if (s < end_ && (*s == '+' || *s == '-')) {
      eneg = *s == '-';
      s++;
    }
    if (s >= end_ || *s < '0' || *s > '9') {
      fail();
      return false;
    }
    int ex = 0;
    while (s < end_ && *s >= '0' && *s <= '9') {
      if (ex < 100000) ex = ex * 10 + (*s - '0');
      s++;
    }
    e10 += eneg ? -ex : ex;
  }
  double v;
  if (m == 0 && !more) {
    v = 0;
  } else if (!more && m <= (static_cast<uint64_t>(1) << 53) && e10 >= -22 && e10 <= 22) {
    // both the digits and the power of ten are doubles without rounding: one multiplication
    // or division gives the nearest double
    v = static_cast<double>(m);
    if (e10 < 0) v /= P10[-e10];
    else v *= P10[e10];
  } else {
    // (a long or extreme number, rare: read by the library – to the nearest double as well)
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    v = 0;
    auto res = std::from_chars(digitsFrom, s, v);
    if (res.ec == std::errc::result_out_of_range) {
      // (beyond what a double holds: Infinity, as JavaScript reads it – or 0, for what is too small)
      v = e10 + taken > 0 ? JS_INF : 0;
    }
#else
    std::string tmp(digitsFrom, s);
    v = std::strtod(tmp.c_str(), nullptr);
#endif
  }
  out = neg ? -v : v;
  p_ = s;
  return true;
}

bool JsonReader::parseString(std::string* out) {
  const char* s = p_ + 1;
  const char* run = s;
  if (out) out->clear();
  for (;;) {
    if (s >= end_) {
      fail();
      return false;
    }
    unsigned char c = static_cast<unsigned char>(*s);
    if (c == '"') break;
    if (c < 0x20) {
      fail();
      return false;
    }
    if (c != '\\') {
      s++;
      continue;
    }
    if (out) out->append(run, static_cast<size_t>(s - run));
    s++;
    if (s >= end_) {
      fail();
      return false;
    }
    char e = *s++;
    char plain = 0;
    switch (e) {
      case '"':
      case '\\':
      case '/': plain = e; break;
      case 'b': plain = '\b'; break;
      case 'f': plain = '\f'; break;
      case 'n': plain = '\n'; break;
      case 'r': plain = '\r'; break;
      case 't': plain = '\t'; break;
      case 'u': {
        unsigned cp = 0;
        if (!hex4(s, end_, cp)) {
          fail();
          return false;
        }
        s += 4;
        // the two halves of a character beyond the 16 bits of one \u
        if (cp >= 0xD800 && cp <= 0xDBFF && end_ - s >= 6 && s[0] == '\\' && s[1] == 'u') {
          unsigned lo = 0;
          if (hex4(s + 2, end_, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            s += 6;
          }
        }
        // (a half on its own is no character: U+FFFD, as it comes out of a browser's TextEncoder)
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        if (out) appendUtf8(*out, cp);
        run = s;
        continue;
      }
      default:
        fail();
        return false;
    }
    if (out) out->push_back(plain);
    run = s;
  }
  if (out) out->append(run, static_cast<size_t>(s - run));
  p_ = s + 1;
  return true;
}

// ---------------------------------------------------------------------------
// JsonWriter
// ---------------------------------------------------------------------------

void JsonWriter::value() {
  if (afterKey_) {
    afterKey_ = false;
    return;
  }
  if (!comma_.empty()) {
    if (comma_.back()) out_ += ',';
    comma_.back() = 1;
  }
}

void JsonWriter::open(char c) {
  value();
  out_ += c;
  comma_.push_back(0);
}

void JsonWriter::shut(char c) {
  if (!comma_.empty()) comma_.pop_back();
  out_ += c;
}

void JsonWriter::key(std::string_view k) {
  if (!comma_.empty()) {
    if (comma_.back()) out_ += ',';
    comma_.back() = 1;
  }
  quote(out_, k);
  out_ += ':';
  afterKey_ = true;
}

void JsonWriter::number(double v) {
  value();
  if (std::isfinite(v)) out_ += jsStr(v);
  else out_ += "null";
}

void JsonWriter::string(std::string_view s) {
  value();
  quote(out_, s);
}

void JsonWriter::boolean(bool b) {
  value();
  out_ += b ? "true" : "false";
}

void JsonWriter::null() {
  value();
  out_ += "null";
}

void JsonWriter::raw(std::string_view json) {
  value();
  out_ += json;
}

// ---------------------------------------------------------------------------
// JsonObject
// ---------------------------------------------------------------------------

struct JsonObject::Impl {
  /** in JavaScript's order: the names that are whole numbers first (rising), then the others as they came */
  std::vector<value_type> items;
  size_t numbered = 0;
  /** where each name is, for objects with many members; `stale`: it has to be made anew */
  std::unordered_map<std::string, size_t> where;
  bool stale = true;

  long find(std::string_view key) {
    if (items.size() <= INDEXED_FROM) {
      for (size_t i = 0; i < items.size(); i++)
        if (items[i].first == key) return static_cast<long>(i);
      return -1;
    }
    if (stale) {
      where.clear();
      where.reserve(items.size() * 2);
      for (size_t i = 0; i < items.size(); i++) where[items[i].first] = i;
      stale = false;
    }
    auto it = where.find(std::string(key));
    return it == where.end() ? -1 : static_cast<long>(it->second);
  }

  Json& insert(std::string_view key) {
    uint32_t idx = 0;
    size_t at = items.size();
    if (arrayIndex(key, idx)) {
      // among the numbered ones, in rising order
      size_t lo = 0, hi = numbered;
      while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        uint32_t other = 0;
        arrayIndex(items[mid].first, other);
        if (other < idx) lo = mid + 1;
        else hi = mid;
      }
      at = lo;
      numbered++;
    }
    if (at == items.size()) {
      items.emplace_back(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple());
      if (!stale && items.size() > INDEXED_FROM) where[items.back().first] = at;
      else stale = true;
    } else {
      items.emplace(items.begin() + static_cast<long>(at), std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple());
      stale = true;
    }
    return items[at].second;
  }
};

JsonObject::JsonObject() = default;
JsonObject::~JsonObject() = default;
JsonObject::JsonObject(const JsonObject& o) : impl_(o.impl_ ? std::make_unique<Impl>(*o.impl_) : nullptr) {}
JsonObject::JsonObject(JsonObject&& o) noexcept = default;
JsonObject& JsonObject::operator=(const JsonObject& o) {
  if (this != &o) impl_ = o.impl_ ? std::make_unique<Impl>(*o.impl_) : nullptr;
  return *this;
}
JsonObject& JsonObject::operator=(JsonObject&& o) noexcept = default;

size_t JsonObject::size() const { return impl_ ? impl_->items.size() : 0; }
const JsonObject::value_type* JsonObject::begin() const { return impl_ ? impl_->items.data() : nullptr; }
const JsonObject::value_type* JsonObject::end() const { return impl_ ? impl_->items.data() + impl_->items.size() : nullptr; }
JsonObject::value_type* JsonObject::begin() { return impl_ ? impl_->items.data() : nullptr; }
JsonObject::value_type* JsonObject::end() { return impl_ ? impl_->items.data() + impl_->items.size() : nullptr; }

Json& JsonObject::operator[](std::string_view key) {
  if (!impl_) impl_ = std::make_unique<Impl>();
  long i = impl_->find(key);
  if (i >= 0) return impl_->items[static_cast<size_t>(i)].second;
  return impl_->insert(key);
}

const Json* JsonObject::find(std::string_view key) const {
  if (!impl_) return nullptr;
  long i = impl_->find(key);
  return i < 0 ? nullptr : &impl_->items[static_cast<size_t>(i)].second;
}

Json* JsonObject::find(std::string_view key) {
  if (!impl_) return nullptr;
  long i = impl_->find(key);
  return i < 0 ? nullptr : &impl_->items[static_cast<size_t>(i)].second;
}

bool JsonObject::erase(std::string_view key) {
  if (!impl_) return false;
  long i = impl_->find(key);
  if (i < 0) return false;
  if (static_cast<size_t>(i) < impl_->numbered) impl_->numbered--;
  impl_->items.erase(impl_->items.begin() + i);
  impl_->stale = true;
  return true;
}

void JsonObject::clear() { impl_.reset(); }

// ---------------------------------------------------------------------------
// Json
// ---------------------------------------------------------------------------

namespace {
const Json kNull;
}

std::string Json::asStr() const {
  if (kind == Kind::String) return str;
  if (kind == Kind::Number) return jsStr(number);
  return "";
}

double Json::asNum() const {
  if (kind == Kind::Number) return number;
  if (kind == Kind::String && !str.empty()) return std::strtod(str.c_str(), nullptr);
  return JS_NAN;
}

const Json& Json::at(std::string_view key) const {
  if (kind != Kind::Object) return kNull;
  const Json* v = object.find(key);
  return v ? *v : kNull;
}

Json& Json::set(std::string_view key, Json v) {
  if (kind != Kind::Object) {
    *this = Json();
    kind = Kind::Object;
  }
  Json& slot = object[key];
  slot = std::move(v);
  return slot;
}

void Json::push(Json v) {
  if (kind != Kind::Array) {
    *this = Json();
    kind = Kind::Array;
  }
  array.push_back(std::move(v));
}

void fromJson(JsonReader& r, Json& v) {
  v = Json();
  switch (r.peek()) {
    case JsonReader::Type::Null: r.null(); break;
    case JsonReader::Type::Bool:
      v.kind = Json::Kind::Bool;
      r.boolean(v.boolean);
      break;
    case JsonReader::Type::Number:
      v.kind = Json::Kind::Number;
      r.number(v.number);
      break;
    case JsonReader::Type::String:
      v.kind = Json::Kind::String;
      r.string(v.str);
      break;
    case JsonReader::Type::Array:
      v.kind = Json::Kind::Array;
      r.array([&] {
        v.array.emplace_back();
        fromJson(r, v.array.back());
      });
      break;
    case JsonReader::Type::Object:
      v.kind = Json::Kind::Object;
      r.object([&](std::string_view k) { fromJson(r, v.object[k]); });
      break;
    default: r.skip();  // (no value: the reader notes that the text is broken)
  }
}

void toJson(JsonWriter& w, const Json& v) {
  switch (v.kind) {
    case Json::Kind::Null: w.null(); break;
    case Json::Kind::Bool: w.boolean(v.boolean); break;
    case Json::Kind::Number: w.number(v.number); break;
    case Json::Kind::String: w.string(v.str); break;
    case Json::Kind::Array:
      w.beginArray();
      for (const Json& e : v.array) toJson(w, e);
      w.endArray();
      break;
    case Json::Kind::Object:
      w.beginObject();
      for (const auto& kv : v.object) {
        w.key(kv.first);
        toJson(w, kv.second);
      }
      w.endObject();
      break;
  }
}

bool Json::parse(std::string_view text, Json& out) {
  JsonReader r(text);
  Json v;
  fromJson(r, v);
  if (!r.ok() || !r.atEnd()) return false;
  out = std::move(v);
  return true;
}

std::string Json::stringify() const {
  JsonWriter w;
  toJson(w, *this);
  return w.take();
}

}  // namespace fs
