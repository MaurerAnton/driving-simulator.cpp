// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The driven path (origin: src/game/track.ts @ ff37c5d, v0.7.0).
 */
#include "game/track.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace fs {

namespace {
const char* kKey = "fahrsim-baunatal.path.v1";

bool broken(double v) { return std::isnan(v); }
}  // namespace

DrivenPath::DrivenPath(KeyValueStore* store) : store_(store) {
  try {
    if (!store_) return;
    const std::string raw = store_->getItem(kKey);
    if (raw.empty()) return;
    // stored as whole metres, null for a break
    std::string nums;
    for (char c : raw)
      nums += (c == '[' || c == ']' || c == ',') ? ' ' : c;
    size_t i = 0;
    while (i < nums.size()) {
      while (i < nums.size() && nums[i] == ' ') i++;
      size_t start = i;
      while (i < nums.size() && nums[i] != ' ') i++;
      if (i == start) continue;
      const std::string tok = nums.substr(start, i - start);
      if (tok == "null")
        points.push_back(std::nan(""));
      else
        points.push_back(std::strtod(tok.c_str(), nullptr));
    }
    if (points.size() % 2 == 1) points.pop_back();
    breakLine();
  } catch (...) {
    /* nothing usable stored */
  }
}

void DrivenPath::add(double x, double z) {
  const size_t n = points.size();
  if (n >= 2 && !broken(points[n - 2])) {
    const double d = std::hypot(x - points[n - 2], z - points[n - 1]);
    if (d < PATH_STEP) return;
    // too far for one step of driving: the car was put somewhere else
    if (d > 60) breakLine();
  }
  points.push_back(std::round(x * 10.0) / 10.0);
  points.push_back(std::round(z * 10.0) / 10.0);
  if (points.size() > PATH_MAX_POINTS * 2)
    points.erase(points.begin(), points.begin() + static_cast<long>(points.size() - PATH_MAX_POINTS * 2));
  if (++dirty_ >= 40) save();
}

void DrivenPath::breakLine() {
  if (points.size() >= 2 && !broken(points[points.size() - 2])) {
    points.push_back(std::nan(""));
    points.push_back(std::nan(""));
  }
}

double DrivenPath::length() const {
  double sum = 0;
  for (size_t i = 2; i < points.size(); i += 2) {
    if (broken(points[i]) || broken(points[i - 2])) continue;
    sum += std::hypot(points[i] - points[i - 2], points[i + 1] - points[i - 1]);
  }
  return sum;
}

void DrivenPath::clear() {
  points.clear();
  dirty_ = 0;
  try {
    if (store_) store_->removeItem(kKey);
  } catch (...) {
    /* ignore */
  }
}

void DrivenPath::save() {
  dirty_ = 0;
  if (!store_) return;
  try {
    // whole metres, null for a break (as the original writes them)
    std::string out = "[";
    for (size_t i = 0; i < points.size(); i++) {
      if (i) out += ",";
      if (broken(points[i]))
        out += "null";
      else {
        char n[32];
        std::snprintf(n, sizeof n, "%.0f", points[i]);
        out += n;
      }
    }
    out += "]";
    store_->setItem(kKey, out);
  } catch (...) {
    /* private window or storage full: the path lives as long as the page */
  }
}

}  // namespace fs
