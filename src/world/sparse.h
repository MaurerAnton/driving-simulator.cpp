// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Two things the original leans on that C++ does not bring along:
 *
 *  - an array of objects by their number, with gaps – `world.edges[id]` is undefined where
 *    the map is not loaded, and beyond the end of the array: `Sparse<T>`.  It owns its
 *    objects; each keeps its address for as long as it lives.
 *  - a Set that is gone through in the order things were put into it: `OrderedSet<T>`
 *    (the lanes of a network get their numbers in that order).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs {

template <class T>
class Sparse {
 public:
  /** `a[i]`: the object, or null where there is none (also beyond the end) */
  T* operator[](int64_t i) const { return i >= 0 && static_cast<size_t>(i) < v_.size() ? v_[static_cast<size_t>(i)].get() : nullptr; }

  /** `a.length`: one more than the highest number that was ever used */
  size_t size() const { return v_.size(); }
  /** how many objects there are (`a.filter(Boolean).length`) */
  size_t count() const { return count_; }

  /** `a[i] = object`; returns it */
  T* set(int64_t i, std::unique_ptr<T> object) {
    const size_t k = static_cast<size_t>(i);
    if (k >= v_.size()) v_.resize(k + 1);
    if (!v_[k] && object) count_++;
    else if (v_[k] && !object) count_--;
    v_[k] = std::move(object);
    return v_[k].get();
  }

  /** `delete a[i]`: the place is empty again; whoever still looks at the object keeps it alive with what is handed back */
  std::unique_ptr<T> take(int64_t i) {
    if (i < 0 || static_cast<size_t>(i) >= v_.size() || !v_[static_cast<size_t>(i)]) return nullptr;
    count_--;
    return std::move(v_[static_cast<size_t>(i)]);
  }

  /** `for (const x of a)`: every place in turn – null at the gaps */
  class iterator {
   public:
    explicit iterator(const std::unique_ptr<T>* p) : p_(p) {}
    T* operator*() const { return p_->get(); }
    iterator& operator++() {
      ++p_;
      return *this;
    }
    bool operator!=(const iterator& o) const { return p_ != o.p_; }
    bool operator==(const iterator& o) const { return p_ == o.p_; }

   private:
    const std::unique_ptr<T>* p_;
  };
  iterator begin() const { return iterator(v_.data()); }
  iterator end() const { return iterator(v_.data() + v_.size()); }

 private:
  std::vector<std::unique_ptr<T>> v_;
  size_t count_ = 0;
};

/** A `Set` of objects (by their address), gone through in the order they were added. */
template <class T>
class OrderedSet {
 public:
  using const_iterator = typename std::list<T*>::const_iterator;

  /** false: it was in the set already (and keeps its place) */
  bool add(T* item) {
    if (where_.count(item)) return false;
    order_.push_back(item);
    where_[item] = std::prev(order_.end());
    return true;
  }
  /** `set.delete(item)` */
  bool erase(T* item) {
    auto it = where_.find(item);
    if (it == where_.end()) return false;
    order_.erase(it->second);
    where_.erase(it);
    return true;
  }
  bool has(T* item) const { return where_.count(item) > 0; }
  size_t size() const { return order_.size(); }
  bool empty() const { return order_.empty(); }
  const_iterator begin() const { return order_.begin(); }
  const_iterator end() const { return order_.end(); }

 private:
  std::list<T*> order_;
  std::unordered_map<T*, typename std::list<T*>::iterator> where_;
};

}  // namespace fs
