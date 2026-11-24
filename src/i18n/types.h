// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/i18n/types.ts @ v1.7.7
/**
 * `Record<string, T>` as the texts use it: an object of JavaScript whose keys are read
 * in their order (Object.keys, Object.entries) as well as looked up – the order of the
 * language's file, which the tools and the tests go by.
 */
#pragma once

#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs {

template <class T>
class Record {
 public:
  using Entry = std::pair<std::string, T>;

  Record() = default;
  Record(std::initializer_list<Entry> list) {
    for (const Entry& e : list) set(e.first, e.second);
  }

  /** Object.keys(r).length */
  size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }

  /** `key in r` */
  bool has(std::string_view key) const { return index_.find(key) != index_.end(); }

  /** `r[key]` (nullptr: undefined) */
  const T* find(std::string_view key) const {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &entries_[it->second].second;
  }
  T* find(std::string_view key) {
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : &entries_[it->second].second;
  }

  /** `r[key]` where the key is known to be there (std::out_of_range otherwise) */
  const T& at(std::string_view key) const {
    const T* v = find(key);
    if (!v) throw std::out_of_range("Record: no key \"" + std::string(key) + "\"");
    return *v;
  }

  /**
   * `r[key] = value`.  A key that is there keeps its place.  A new one goes to the end –
   * but, as in JavaScript, a key that is an array index ("205") goes before all others,
   * among its like in ascending order: the catalogue numbers of the signs are read in
   * that order.
   */
  T& set(std::string_view key, T value) {
    auto it = index_.find(key);
    if (it != index_.end()) {
      T& slot = entries_[it->second].second;
      slot = std::move(value);
      return slot;
    }
    uint64_t n = 0;
    if (!arrayIndex(key, n)) {
      index_.emplace(std::string(key), static_cast<uint32_t>(entries_.size()));
      entries_.emplace_back(std::string(key), std::move(value));
      return entries_.back().second;
    }
    // among the indices: before the first greater one
    size_t lo = 0, hi = indices_;
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      uint64_t m = 0;
      arrayIndex(entries_[mid].first, m);
      if (m < n) lo = mid + 1;
      else hi = mid;
    }
    entries_.emplace(entries_.begin() + static_cast<std::ptrdiff_t>(lo), std::string(key), std::move(value));
    indices_++;
    renumber(lo);
    return entries_[lo].second;
  }

  /** `delete r[key]` (false: there was no such key) */
  bool erase(std::string_view key) {
    auto it = index_.find(key);
    if (it == index_.end()) return false;
    size_t at = it->second;
    index_.erase(it);
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(at));
    if (at < indices_) indices_--;
    renumber(at);
    return true;
  }

  /** Object.entries(r): `for (const auto& [key, value] : r)` */
  typename std::vector<Entry>::const_iterator begin() const { return entries_.begin(); }
  typename std::vector<Entry>::const_iterator end() const { return entries_.end(); }
  const std::vector<Entry>& entries() const { return entries_; }

  /** Object.keys(r) */
  std::vector<std::string> keys() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const Entry& e : entries_) out.push_back(e.first);
    return out;
  }

  void reserve(size_t n) {
    entries_.reserve(n);
    index_.reserve(n);
  }

  /** the same keys with the same values, in whatever order (assert.deepEqual) */
  friend bool operator==(const Record& a, const Record& b) {
    if (a.size() != b.size()) return false;
    for (const Entry& e : a.entries_) {
      const T* other = b.find(e.first);
      if (!other || !(*other == e.second)) return false;
    }
    return true;
  }

 private:
  struct Hash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
  };
  struct Equal {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
  };

  /** is the key an array index of JavaScript ("0", "17" … "4294967294": no sign, no leading zero)? */
  static bool arrayIndex(std::string_view key, uint64_t& n) {
    if (key.empty() || key.size() > 10) return false;
    if (key[0] == '0' && key.size() > 1) return false;
    n = 0;
    for (char c : key) {
      if (c < '0' || c > '9') return false;
      n = n * 10 + static_cast<uint64_t>(c - '0');
    }
    return n <= 4294967294ull;
  }

  void renumber(size_t from) {
    for (size_t i = from; i < entries_.size(); i++) index_[entries_[i].first] = static_cast<uint32_t>(i);
  }

  std::vector<Entry> entries_;
  std::unordered_map<std::string, uint32_t, Hash, Equal> index_;
  /** how many of the first entries are array indices */
  size_t indices_ = 0;
};

/** All texts of one language: key → text. */
using Dict = Record<std::string>;

}  // namespace fs
