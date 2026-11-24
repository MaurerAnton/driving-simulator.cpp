// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/netdiff.ts @ v1.7.7
/** A change to the road network as a small diff over the published one (see netdiff.h). */
#include "world/netdiff.h"

#include <cmath>
#include <stdexcept>

#include "core/js.h"
#include "platform/json.h"

namespace fs {

namespace {

/** The arrays whose *values* a correction may change (never their length or order). */
const char* const VALUE_SECTIONS[] = {"wayKind", "waySpeed", "turn", "across", "edgeCls"};

bool isValueSection(std::string_view name) {
  for (const char* v : VALUE_SECTIONS)
    if (name == v) return true;
  return false;
}

/** the entries of a value section as numbers */
std::vector<double> valuesOf(const NetData& d, std::string_view name) {
  std::vector<double> out;
  auto copy = [&](const auto& a) {
    out.reserve(a.size());
    for (auto v : a) out.push_back(v);
  };
  if (name == "wayKind") copy(d.wayKind);
  else if (name == "waySpeed") copy(d.waySpeed);
  else if (name == "turn") copy(d.turn);
  else if (name == "across") copy(d.across);
  else if (name == "edgeCls") copy(d.edgeCls);
  return out;
}

/** The typed array a section of the net is stored as (its type comes from the base). */
void fromArray(NetData& d, std::string_view name, const std::vector<double>& values) {
  auto store = [&](auto& a) {
    a.resize(values.size());
    using T = typename std::remove_reference_t<decltype(a)>::value_type;
    for (size_t i = 0; i < values.size(); i++) a[i] = static_cast<T>(jsInt32(values[i]));
  };
  if (name == "wayKind") store(d.wayKind);
  else if (name == "waySpeed") store(d.waySpeed);
  else if (name == "turn") store(d.turn);
  else if (name == "edgeCls") store(d.edgeCls);
  else if (name == "across") store(d.across);
}

}  // namespace

const NetChanges* Changes::find(std::string_view name) const {
  for (const auto& entry : entries)
    if (entry.first == name) return &entry.second;
  return nullptr;
}

void Changes::set(std::string_view name, NetChanges pairs) {
  for (auto& entry : entries) {
    if (entry.first == name) {
      entry.second = std::move(pairs);
      return;
    }
  }
  entries.emplace_back(std::string(name), std::move(pairs));
}

std::string fingerprint(const NetData& d) {
  uint32_t h = 2166136261u;  // FNV-1a
  auto mix = [&](double n) {
    h ^= jsUint32(n);
    h *= 16777619u;
  };
  mix(static_cast<double>(d.edgeA.size()));
  mix(static_cast<double>(d.first.size()));
  mix(static_cast<double>(d.to.size()));
  mix(static_cast<double>(d.polyFirst.size()));
  // The arrays that make up the structure: their lengths and order must match.
  for (const std::vector<uint32_t>* a : {&d.edgeA, &d.edgeB, &d.first, &d.to, &d.polyFirst}) {
    mix(static_cast<double>(a->size()));
    // a stride, so a 1M-entry array is not walked in full but still differs when one entry moves
    const size_t stride = std::max<size_t>(1, a->size() / 4096);
    for (size_t i = 0; i < a->size(); i += stride) mix((*a)[i]);
  }
  return toRadix(static_cast<int64_t>(h), 16);
}

bool matchesFingerprint(const NetData& base, const NetDiff& diff) { return diff.edges == static_cast<double>(base.edgeA.size()) && diff.hash == fingerprint(base); }

NetDiff netDiff(const NetData& base, const NetData& neu) {
  NetDiff diff;
  for (const char* name : VALUE_SECTIONS) {
    const std::vector<double> a = valuesOf(base, name);
    const std::vector<double> b = valuesOf(neu, name);
    if (a.size() != b.size())
      throw std::runtime_error(std::string("netDiff: \"") + name + "\" changed length (" + std::to_string(a.size()) + " → " + std::to_string(b.size()) + ") – not a value correction");
    NetChanges pairs;
    for (size_t i = 0; i < a.size(); i++)
      if (a[i] != b[i]) pairs.emplace_back(static_cast<double>(i), b[i]);
    if (!pairs.empty()) diff.set.set(name, std::move(pairs));
  }
  diff.v = 1;
  diff.kind = "netdiff";
  diff.edges = static_cast<double>(neu.edgeA.size());
  diff.hash = fingerprint(base);
  return diff;
}

NetData applyNetDiff(NetData base, const NetDiff& diff) {
  if (diff.v != 1 || diff.kind != "netdiff") throw std::runtime_error("applyNetDiff: not a network diff");
  if (!matchesFingerprint(base, diff)) throw std::runtime_error("applyNetDiff: the diff was made against another network");
  // (every section the same, the changed ones replaced below; all of them are worked out before one is put in,
  // so that nothing is changed where the diff turns out to be broken)
  std::vector<std::pair<std::string, std::vector<double>>> changed;
  for (const auto& [name, pairs] : diff.set.entries) {
    if (!isValueSection(name)) throw std::runtime_error("applyNetDiff: \"" + name + "\" is not a value section");
    // copy in the section's own type, so a Uint8 stays a Uint8, a Uint16 a Uint16
    std::vector<double> copy = valuesOf(base, name);
    for (const auto& [i, v] : pairs) {
      if (i < 0 || i >= static_cast<double>(copy.size())) throw std::runtime_error("applyNetDiff: \"" + name + "[" + jsStr(i) + "]\" is out of range");
      // (a place between two entries, or one that is no number, is none)
      if (i == std::floor(i)) copy[static_cast<size_t>(i)] = v;
    }
    changed.emplace_back(name, std::move(copy));
  }
  for (const auto& [name, values] : changed) fromArray(base, name, values);
  return base;
}

std::vector<uint8_t> packNetDiff(const NetDiff& diff) {
  JsonWriter w;
  w.beginObject();
  w.key("v");
  w.number(diff.v);
  w.key("kind");
  w.string(diff.kind);
  w.key("edges");
  w.number(diff.edges);
  w.key("hash");
  w.string(diff.hash);
  w.key("set");
  w.beginObject();
  for (const auto& [name, pairs] : diff.set.entries) {
    w.key(name);
    w.beginArray();
    for (const auto& [i, v] : pairs) {
      w.beginArray();
      w.number(i);
      w.number(v);
      w.endArray();
    }
    w.endArray();
  }
  w.endObject();
  w.endObject();
  const std::string& text = w.text();
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::optional<NetDiff> unpackNetDiff(const std::vector<uint8_t>& bytes) {
  std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  // (a byte order mark at the start is not part of the text)
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) text.remove_prefix(3);
  Json o;
  if (!Json::parse(text, o)) return std::nullopt;
  if (!o.isObject()) return std::nullopt;
  const Json& v = o.at("v");
  const Json& kind = o.at("kind");
  const Json& hash = o.at("hash");
  const Json& set = o.at("set");
  if (!(v.isNumber() && v.number == 1 && kind.isString() && kind.str == "netdiff" && hash.isString() && (set.isObject() || set.isArray() || (set.isNull() && o.has("set"))))) return std::nullopt;
  NetDiff diff;
  diff.v = 1;
  diff.kind = "netdiff";
  diff.edges = o.at("edges").isNumber() ? o.at("edges").number : JS_NAN;
  diff.hash = hash.str;
  if (set.isObject()) {
    for (const auto& [name, list] : set.object) {
      NetChanges pairs;
      for (const Json& pair : list.array) {
        const double i = pair.isArray() && pair.array.size() > 0 ? pair.array[0].asNum() : JS_NAN;
        const double value = pair.isArray() && pair.array.size() > 1 ? pair.array[1].asNum() : JS_NAN;
        pairs.emplace_back(i, value);
      }
      diff.set.set(name, std::move(pairs));
    }
  }
  return diff;
}

}  // namespace fs
