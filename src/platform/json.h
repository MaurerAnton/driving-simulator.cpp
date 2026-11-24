// SPDX-License-Identifier: AGPL-3.0-only
/**
 * JSON, read and written the way JavaScript does it (JSON.parse / JSON.stringify).
 *
 * Three tools:
 *
 *  - `JsonReader` walks a text value by value and builds nothing.  It is what the files of
 *    the map are read with while the game runs – a chunk of the road network is megabytes of
 *    numbers, and every tile begins with a header: their numbers go straight into the structs
 *    that are made of them (`fromJson`, FS_JSON_STRUCT).
 *  - `JsonWriter` writes a text piece by piece (`toJson`).
 *  - `Json` is a whole document in memory, for small ones whose shape is looked at piece by
 *    piece (the answers of the reality providers, a stored list).  Its objects keep their
 *    members in JavaScript's order.
 *
 * Numbers are read to the nearest double, as JSON.parse reads them, and written as
 * JavaScript writes them (the shortest text that reads back the same).
 *
 * Origin: JSON.parse / JSON.stringify of the original, and the `isObj`/`str`/`num` helpers
 * of src/reality/providers.ts.
 */
#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fs {

// ---------------------------------------------------------------------------
// Reading without building anything
// ---------------------------------------------------------------------------

class JsonReader {
 public:
  enum class Type { End, Null, Bool, Number, String, Array, Object, Invalid };

  /** the text must outlive the reader */
  explicit JsonReader(std::string_view text) : p_(text.data()), end_(text.data() + text.size()) {}

  /** false once the text has turned out not to be JSON (nothing more is read then) */
  bool ok() const { return ok_; }
  /** has everything been read – is nothing but white space left?  (JSON.parse wants exactly one value) */
  bool atEnd();
  /** what comes next */
  Type peek();

  /**
   * The next value, if it is of the kind asked for: read into `out` (true).  Anything else is
   * passed over and `out` stays as it is (false) – a `null` where a number was expected
   * leaves the default.
   */
  bool number(double& out);
  bool boolean(bool& out);
  bool string(std::string& out);
  /** Is the next value `null`?  Then it is read. */
  bool null();
  /** the next value as a number (NaN if it is not one) / as a text ('' if it is not one) */
  double number();
  std::string string();
  /** pass over the next value, whatever it is */
  void skip();

  /**
   * The next value as an object: `member(key)` is called for each member and reads its value
   * (what it does not read is passed over).  `key` is valid until the reader is used again.
   * False (and the value passed over) if it is not an object.
   */
  template <class F>
  bool object(F&& member) {
    if (!open('{')) return false;
    if (close('}')) return true;
    for (;;) {
      std::string_view key;
      if (!readKey(key)) return false;
      const char* before = p_;
      member(key);
      if (!ok_) return false;
      if (p_ == before) skip();
      if (!next('}')) break;
    }
    depth_--;
    return ok_;
  }

  /** The next value as an array: `element()` is called once for each element and reads it. */
  template <class F>
  bool array(F&& element) {
    if (!open('[')) return false;
    if (close(']')) return true;
    for (;;) {
      ws();
      const char* before = p_;
      element();
      if (!ok_) return false;
      if (p_ == before) skip();
      if (!next(']')) break;
    }
    depth_--;
    return ok_;
  }

 private:
  const char* p_;
  const char* end_;
  bool ok_ = true;
  int depth_ = 0;
  /** a key that had escapes in it */
  std::string keyBuf_;

  void ws() {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\r' || *p_ == '\t')) p_++;
  }
  void fail() {
    ok_ = false;
    p_ = end_;
  }
  /** begin an array or object; false: it is not one (then it was passed over) */
  bool open(char bracket);
  /** is the array or object empty?  (then it is closed) */
  bool close(char bracket);
  /** after a member or element: true – another follows; false – it is closed, or the text is broken */
  bool next(char bracket);
  /** the key of a member and the colon after it; afterwards the reader stands at the value */
  bool readKey(std::string_view& key);
  bool parseNumber(double& out);
  /** a text in quotes; `out` null: only pass over it */
  bool parseString(std::string* out);
  bool literal(const char* word, size_t n);
};

// --- values into variables ---------------------------------------------------
// fromJson(reader, variable) reads the next value into the variable.  What is not of the
// variable's kind (or is null) leaves it as it is.

inline void fromJson(JsonReader& r, double& v) { r.number(v); }
inline void fromJson(JsonReader& r, float& v) {
  double d;
  if (r.number(d)) v = static_cast<float>(d);
}
inline void fromJson(JsonReader& r, bool& v) { r.boolean(v); }
inline void fromJson(JsonReader& r, std::string& v) { r.string(v); }
template <std::integral T>
  requires(!std::same_as<T, bool>)
void fromJson(JsonReader& r, T& v) {
  double d;
  // (whole numbers in the files; what is none stays as it is rather than becoming anything)
  if (r.number(d) && d == d && d >= -9.2e18 && d <= 9.2e18) v = static_cast<T>(static_cast<int64_t>(d));
}
template <class T>
void fromJson(JsonReader& r, std::vector<T>& v) {
  if (r.peek() != JsonReader::Type::Array) {
    r.skip();
    return;
  }
  v.clear();
  r.array([&] {
    v.emplace_back();
    fromJson(r, v.back());
  });
}
template <class T, size_t N>
void fromJson(JsonReader& r, std::array<T, N>& v) {
  size_t i = 0;
  r.array([&] {
    if (i < N) fromJson(r, v[i]);
    i++;
  });
}
template <class A, class B>
void fromJson(JsonReader& r, std::pair<A, B>& v) {
  int i = 0;
  r.array([&] {
    if (i == 0) fromJson(r, v.first);
    else if (i == 1) fromJson(r, v.second);
    i++;
  });
}
template <class... T>
void fromJson(JsonReader& r, std::tuple<T...>& v) {
  size_t i = 0;
  r.array([&] {
    size_t k = 0;
    std::apply([&](auto&... part) { ((k++ == i ? fromJson(r, part) : void()), ...); }, v);
    i++;
  });
}
template <class T>
void fromJson(JsonReader& r, std::optional<T>& v) {
  if (r.null()) return;
  T t{};
  fromJson(r, t);
  v = std::move(t);
}
template <class T>
void fromJson(JsonReader& r, std::shared_ptr<const T>& v) {
  if (r.null()) return;
  auto t = std::make_shared<T>();
  fromJson(r, *t);
  v = std::move(t);
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

class JsonWriter {
 public:
  void beginObject() { open('{'); }
  void endObject() { shut('}'); }
  void beginArray() { open('['); }
  void endArray() { shut(']'); }
  /** the name of the next member of the object being written */
  void key(std::string_view k);
  /** a number as JavaScript writes it; what JSON has no word for (NaN, ±Infinity) becomes null */
  void number(double v);
  void string(std::string_view s);
  void boolean(bool b);
  void null();
  /** a piece of JSON as it is */
  void raw(std::string_view json);

  const std::string& text() const { return out_; }
  std::string take() { return std::move(out_); }

 private:
  std::string out_;
  /** does the next value need a comma before it? (one flag per open array or object) */
  std::vector<char> comma_;
  bool afterKey_ = false;

  void value();
  void open(char c);
  void shut(char c);
};

inline void toJson(JsonWriter& w, double v) { w.number(v); }
inline void toJson(JsonWriter& w, float v) { w.number(static_cast<double>(v)); }
inline void toJson(JsonWriter& w, bool v) { w.boolean(v); }
inline void toJson(JsonWriter& w, const std::string& v) { w.string(v); }
template <std::integral T>
  requires(!std::same_as<T, bool>)
void toJson(JsonWriter& w, T v) {
  w.number(static_cast<double>(v));
}
template <class T>
void toJson(JsonWriter& w, const std::vector<T>& v) {
  w.beginArray();
  for (const T& e : v) toJson(w, e);
  w.endArray();
}
template <class T, size_t N>
void toJson(JsonWriter& w, const std::array<T, N>& v) {
  w.beginArray();
  for (const T& e : v) toJson(w, e);
  w.endArray();
}
template <class A, class B>
void toJson(JsonWriter& w, const std::pair<A, B>& v) {
  w.beginArray();
  toJson(w, v.first);
  toJson(w, v.second);
  w.endArray();
}
template <class... T>
void toJson(JsonWriter& w, const std::tuple<T...>& v) {
  w.beginArray();
  std::apply([&](const auto&... part) { (toJson(w, part), ...); }, v);
  w.endArray();
}
template <class T>
void toJson(JsonWriter& w, const std::optional<T>& v) {
  if (v) toJson(w, *v);
  else w.null();
}
template <class T>
void toJson(JsonWriter& w, const std::shared_ptr<const T>& v) {
  if (v) toJson(w, *v);
  else w.null();
}

/** A member of an object: what is not there (an empty optional) is left out, as JSON.stringify leaves out `undefined`. */
template <class T>
void jsonMember(JsonWriter& w, std::string_view name, const T& v) {
  w.key(name);
  toJson(w, v);
}
template <class T>
void jsonMember(JsonWriter& w, std::string_view name, const std::optional<T>& v) {
  if (!v) return;
  w.key(name);
  toJson(w, *v);
}
template <class T>
void jsonMember(JsonWriter& w, std::string_view name, const std::shared_ptr<const T>& v) {
  if (!v) return;
  w.key(name);
  toJson(w, *v);
}

/** The text of anything toJson can write (JSON.stringify). */
template <class T>
std::string jsonText(const T& v) {
  JsonWriter w;
  toJson(w, v);
  return w.take();
}

/**
 * A text read into anything fromJson can read (JSON.parse).  False when the text is not
 * JSON, or goes on after its value; `out` then holds what was read up to there.
 */
template <class T>
bool jsonParse(std::string_view text, T& out) {
  JsonReader r(text);
  fromJson(r, out);
  return r.ok() && r.atEnd();
}

// --- structs whose fields are the members of a JSON object ---------------------
//
//   struct PlaceJson { std::string name, kind; double x = 0, z = 0; };
//   FS_JSON_STRUCT(PlaceJson, name, kind, x, z)
//
// makes fromJson and toJson for it: each field is the member of the same name.  Members the
// text does not have leave the field's default; members the struct does not have are passed over.

#define FS_JSON_PARENS_ ()
#define FS_JSON_EXPAND_(...) FS_JSON_EXPAND4_(FS_JSON_EXPAND4_(FS_JSON_EXPAND4_(FS_JSON_EXPAND4_(__VA_ARGS__))))
#define FS_JSON_EXPAND4_(...) FS_JSON_EXPAND3_(FS_JSON_EXPAND3_(FS_JSON_EXPAND3_(FS_JSON_EXPAND3_(__VA_ARGS__))))
#define FS_JSON_EXPAND3_(...) FS_JSON_EXPAND2_(FS_JSON_EXPAND2_(FS_JSON_EXPAND2_(FS_JSON_EXPAND2_(__VA_ARGS__))))
#define FS_JSON_EXPAND2_(...) FS_JSON_EXPAND1_(FS_JSON_EXPAND1_(FS_JSON_EXPAND1_(FS_JSON_EXPAND1_(__VA_ARGS__))))
#define FS_JSON_EXPAND1_(...) __VA_ARGS__
#define FS_JSON_EACH_(macro, ...) __VA_OPT__(FS_JSON_EXPAND_(FS_JSON_EACH_STEP_(macro, __VA_ARGS__)))
#define FS_JSON_EACH_STEP_(macro, first, ...) macro(first) __VA_OPT__(FS_JSON_EACH_AGAIN_ FS_JSON_PARENS_(macro, __VA_ARGS__))
#define FS_JSON_EACH_AGAIN_() FS_JSON_EACH_STEP_
#define FS_JSON_READ_(field) \
  if (k_ == #field) {        \
    fromJson(r_, v_.field);  \
    return;                  \
  }
#define FS_JSON_WRITE_(field) ::fs::jsonMember(w_, #field, v_.field);
#define FS_JSON_BODIES_(how, Type, ...)                                                \
  how void fromJson(::fs::JsonReader& r_, Type& v_) {                                   \
    r_.object([&](std::string_view k_) { FS_JSON_EACH_(FS_JSON_READ_, __VA_ARGS__) });  \
  }                                                                                    \
  how void toJson(::fs::JsonWriter& w_, const Type& v_) {                               \
    w_.beginObject();                                                                  \
    FS_JSON_EACH_(FS_JSON_WRITE_, __VA_ARGS__)                                          \
    w_.endObject();                                                                    \
  }
/** in a header: fromJson and toJson of a struct, written out where it is used */
#define FS_JSON_STRUCT(Type, ...) FS_JSON_BODIES_(inline, Type, __VA_ARGS__)
/** … or declared in the header (FS_JSON_DECLARE) and written out once in a .cpp (FS_JSON_DEFINE) */
#define FS_JSON_DECLARE(Type)                    \
  void fromJson(::fs::JsonReader& r_, Type& v_); \
  void toJson(::fs::JsonWriter& w_, const Type& v_);
#define FS_JSON_DEFINE(Type, ...) FS_JSON_BODIES_(, Type, __VA_ARGS__)

// ---------------------------------------------------------------------------
// A whole document in memory
// ---------------------------------------------------------------------------

class Json;

/**
 * The members of an object, in the order JavaScript goes through them: names that are whole
 * numbers first (in rising order), then the others in the order they were put in.
 */
class JsonObject {
 public:
  using value_type = std::pair<std::string, Json>;

  JsonObject();
  ~JsonObject();
  JsonObject(const JsonObject& o);
  JsonObject(JsonObject&& o) noexcept;
  JsonObject& operator=(const JsonObject& o);
  JsonObject& operator=(JsonObject&& o) noexcept;

  size_t size() const;
  bool empty() const { return size() == 0; }
  const value_type* begin() const;
  const value_type* end() const;
  value_type* begin();
  value_type* end();

  /** the member of that name – made (null) if it is not there: `object[key] = value` */
  Json& operator[](std::string_view key);
  /** the member of that name, or null if it is not there */
  const Json* find(std::string_view key) const;
  Json* find(std::string_view key);
  size_t count(std::string_view key) const { return find(key) ? 1 : 0; }
  /** `delete object[key]` */
  bool erase(std::string_view key);
  void clear();

 private:
  struct Impl;
  /** (nothing until the first member is put in) */
  std::unique_ptr<Impl> impl_;
};

class Json {
 public:
  enum class Kind { Null, Bool, Number, String, Array, Object };

  Kind kind = Kind::Null;
  bool boolean = false;
  double number = 0;
  std::string str;
  std::vector<Json> array;
  JsonObject object;

  Json() = default;
  Json(std::nullptr_t) {}
  template <std::same_as<bool> T>
  Json(T v) : kind(Kind::Bool), boolean(v) {}
  template <class T>
    requires(std::is_arithmetic_v<T> && !std::same_as<T, bool>)
  Json(T v) : kind(Kind::Number), number(static_cast<double>(v)) {}
  Json(std::string v) : kind(Kind::String), str(std::move(v)) {}
  Json(std::string_view v) : kind(Kind::String), str(v) {}
  Json(const char* v) : kind(Kind::String), str(v) {}
  /** `[]` and `{}` */
  static Json makeArray() {
    Json j;
    j.kind = Kind::Array;
    return j;
  }
  static Json makeObject() {
    Json j;
    j.kind = Kind::Object;
    return j;
  }

  bool isNull() const { return kind == Kind::Null; }
  bool isBool() const { return kind == Kind::Bool; }
  bool isObject() const { return kind == Kind::Object; }
  bool isArray() const { return kind == Kind::Array; }
  bool isString() const { return kind == Kind::String; }
  bool isNumber() const { return kind == Kind::Number; }

  /** The value as a string ('' for anything but string/number). */
  std::string asStr() const;
  /** The value as a number (NaN when it is not one). */
  double asNum() const;

  /** Object member, or a null value when absent (never throws). */
  const Json& at(std::string_view key) const;
  bool has(std::string_view key) const { return kind == Kind::Object && object.find(key) != nullptr; }
  /** `value[key] = v` on an object (a value that is none becomes one); returns the member */
  Json& set(std::string_view key, Json v);
  /** `value.push(v)` on an array (a value that is none becomes one) */
  void push(Json v);

  /** Parse a document (JSON.parse); returns false on malformed input. */
  static bool parse(std::string_view text, Json& out);
  /** The document as text (JSON.stringify without indentation). */
  std::string stringify() const;
};

/** The next value of a text as a document in memory. */
void fromJson(JsonReader& r, Json& v);
void toJson(JsonWriter& w, const Json& v);

}  // namespace fs
