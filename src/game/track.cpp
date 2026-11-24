// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/track.ts @ v1.7.7
#include "game/track.h"

#include <cmath>

#include "core/js.h"
#include "core/jsonwrite.h"
#include "platform/json.h"

namespace fs {

namespace {
const char* const KEY = "fahrsim-baunatal.path.v1";
/** a new point when the car has moved this far (m) */
const double STEP = 8;
}  // namespace

DrivenPath::DrivenPath(KeyValueStore* storeIn) : store(storeIn) {
  // (nothing usable stored: an empty path)
  if (!store) return;
  const std::optional<std::string> raw = store->getItem(KEY);
  if (!raw || raw->empty()) return;
  Json list;
  if (!Json::parse(*raw, list)) return;
  // stored as whole metres, null for a break
  if (list.isArray() && list.array.size() % 2 == 0)
    for (const Json& v : list.array) points.push_back(v.isNumber() && std::isfinite(v.number) ? v.number : JS_NAN);
  breakLine();
}

void DrivenPath::add(double x, double z) {
  std::vector<double>& p = points;
  const size_t n = p.size();
  if (n >= 2 && !std::isnan(p[n - 2])) {
    const double d = std::hypot(x - p[n - 2], z - p[n - 1]);
    if (d < STEP) return;
    // too far for one step of driving: the car was put somewhere else
    if (d > 60) breakLine();
  }
  p.push_back(jsRound(x * 10) / 10);
  p.push_back(jsRound(z * 10) / 10);
  if (p.size() > PATH_MAX_POINTS * 2) p.erase(p.begin(), p.begin() + static_cast<std::ptrdiff_t>(p.size() - PATH_MAX_POINTS * 2));
  if (++dirty >= 40) save();
}

void DrivenPath::breakLine() {
  std::vector<double>& p = points;
  if (p.size() >= 2 && !std::isnan(p[p.size() - 2])) {
    p.push_back(JS_NAN);
    p.push_back(JS_NAN);
  }
}

double DrivenPath::length() const {
  const std::vector<double>& p = points;
  double sum = 0;
  for (size_t i = 2; i < p.size(); i += 2) {
    if (std::isnan(p[i]) || std::isnan(p[i - 2])) continue;
    sum += std::hypot(p[i] - p[i - 2], p[i + 1] - p[i - 1]);
  }
  return sum;
}

void DrivenPath::clear() {
  points.clear();
  dirty = 0;
  if (store) store->removeItem(KEY);
}

void DrivenPath::save() {
  dirty = 0;
  if (!store) return;
  JsonOut o;
  o.beginArray();
  for (double v : points) {
    if (std::isnan(v)) o.null();
    else o.number(jsRound(v));
  }
  o.endArray();
  // (private window or storage full: the path lives as long as the page)
  store->setItem(KEY, o.text());
}

}  // namespace fs
