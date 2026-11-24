// SPDX-License-Identifier: AGPL-3.0-only
/**
 * JavaScript's Map and Set as the simulation uses them: looked up by key, and walked in
 * the order the keys were first put in – the order the original's loops see, and with it
 * the order of everything that follows from them (who is asked first, which random number
 * goes to whom).  The entries keep their addresses while they are in.
 */
#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>
#include <utility>

namespace fs {

template <typename K, typename V>
class InsertionMap {
 public:
  using Item = std::pair<const K, V>;
  using iterator = typename std::list<Item>::iterator;
  using const_iterator = typename std::list<Item>::const_iterator;

  InsertionMap() = default;
  InsertionMap(const InsertionMap& o) { *this = o; }
  InsertionMap& operator=(const InsertionMap& o) {
    if (this == &o) return *this;
    clear();
    for (const Item& it : o.items_) set(it.first, it.second);
    return *this;
  }
  InsertionMap(InsertionMap&&) = default;
  InsertionMap& operator=(InsertionMap&&) = default;

  /** map.get(k): the value, or nullptr */
  V* get(const K& k) {
    auto it = index_.find(k);
    return it == index_.end() ? nullptr : &it->second->second;
  }
  const V* get(const K& k) const {
    auto it = index_.find(k);
    return it == index_.end() ? nullptr : &it->second->second;
  }
  bool has(const K& k) const { return index_.find(k) != index_.end(); }
  /** map.set(k, v): a key that is in keeps its place */
  V& set(const K& k, V v) {
    auto it = index_.find(k);
    if (it != index_.end()) {
      it->second->second = std::move(v);
      return it->second->second;
    }
    items_.emplace_back(k, std::move(v));
    auto last = std::prev(items_.end());
    index_.emplace(k, last);
    return last->second;
  }
  /** map.delete(k) */
  bool erase(const K& k) {
    auto it = index_.find(k);
    if (it == index_.end()) return false;
    items_.erase(it->second);
    index_.erase(it);
    return true;
  }
  /** take the entry at `at` out, and go on behind it (deleting while walking) */
  iterator erase(iterator at) {
    index_.erase(at->first);
    return items_.erase(at);
  }
  void clear() {
    items_.clear();
    index_.clear();
  }
  size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }

  iterator begin() { return items_.begin(); }
  iterator end() { return items_.end(); }
  const_iterator begin() const { return items_.begin(); }
  const_iterator end() const { return items_.end(); }

 private:
  std::list<Item> items_;
  std::unordered_map<K, iterator> index_;
};

template <typename K>
class InsertionSet {
 public:
  using iterator = typename std::list<K>::const_iterator;

  InsertionSet() = default;
  InsertionSet(const InsertionSet& o) { *this = o; }
  InsertionSet& operator=(const InsertionSet& o) {
    if (this == &o) return *this;
    clear();
    for (const K& k : o.items_) add(k);
    return *this;
  }
  InsertionSet(InsertionSet&&) = default;
  InsertionSet& operator=(InsertionSet&&) = default;

  bool has(const K& k) const { return index_.find(k) != index_.end(); }
  /** set.add(k): true when it was new */
  bool add(const K& k) {
    if (index_.find(k) != index_.end()) return false;
    items_.push_back(k);
    index_.emplace(k, std::prev(items_.end()));
    return true;
  }
  /** set.delete(k) */
  bool erase(const K& k) {
    auto it = index_.find(k);
    if (it == index_.end()) return false;
    items_.erase(it->second);
    index_.erase(it);
    return true;
  }
  void clear() {
    items_.clear();
    index_.clear();
  }
  size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }

  iterator begin() const { return items_.begin(); }
  iterator end() const { return items_.end(); }

 private:
  std::list<K> items_;
  std::unordered_map<K, typename std::list<K>::iterator> index_;
};

}  // namespace fs
