// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Traffic-light controllers.
 *
 * Origin: src/sim/signals.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#include "sim/signals.h"

#include <algorithm>
#include <cmath>

namespace fs {

namespace {
/** phase letter → state (R red, Y red+amber, G green, A amber, O off) */
int codeOf(char c) {
  switch (c) {
    case 'R': return SIG_RED;
    case 'Y': return SIG_RED_AMBER;
    case 'G': return SIG_GREEN;
    case 'A': return SIG_AMBER;
    default: return SIG_RED;
  }
}
}  // namespace

SignalSystem::SignalSystem(const std::vector<SignalControllerJson>& controllers_)
    : controllers(controllers_) {
  std::vector<char> present(controllers.size(), 1);
  sync(present);
}

void SignalSystem::sync(const std::vector<char>& present) {
  bool fresh = false;
  for (size_t i = 0; i < controllers.size(); i++) {
    if (i < state_.size() && !state_[i].empty()) continue;
    if (i < present.size() && !present[i]) continue;
    ids_.push_back(static_cast<int>(i));
    if (i >= cycle_.size()) {
      cycle_.resize(i + 1, 0);
      state_.resize(i + 1);
      since_.resize(i + 1);
    }
    double total = 0;
    for (const auto& p : controllers[i].phases) total += p.d;
    cycle_[i] = total;
    state_[i].assign(static_cast<size_t>(std::max(0, controllers[i].groups)), SIG_RED);
    since_[i].assign(static_cast<size_t>(std::max(0, controllers[i].groups)), 0);
    fresh = true;
  }
  if (fresh) update(0);
}

void SignalSystem::update(double dt) {
  time += dt;
  for (int idx : ids_) {
    size_t i = static_cast<size_t>(idx);
    const auto& c = controllers[i];
    if (c.phases.empty() || cycle_[i] <= 0) continue;
    double t = std::fmod(time + c.off, cycle_[i]);
    if (t < 0) t += cycle_[i];
    const SignalPhaseJson* phase = &c.phases.back();
    for (const auto& p : c.phases) {
      if (t < p.d) {
        phase = &p;
        break;
      }
      t -= p.d;
    }
    auto& st = state_[i];
    auto& since = since_[i];
    for (int g = 0; g < c.groups && g < static_cast<int>(st.size()); g++) {
      int s = codeOf(phase->s[static_cast<size_t>(g)]);
      if (s != st[static_cast<size_t>(g)]) {
        st[static_cast<size_t>(g)] = s;
        since[static_cast<size_t>(g)] = 0;
      } else {
        since[static_cast<size_t>(g)] += dt;
      }
    }
  }
}

int SignalSystem::get(int controller, int group) const {
  if (controller < 0 || static_cast<size_t>(controller) >= state_.size()) return SIG_GREEN;
  const auto& st = state_[static_cast<size_t>(controller)];
  if (st.empty()) return SIG_GREEN;
  size_t g = static_cast<size_t>(std::max(0, group));
  return st[std::min(g, st.size() - 1)];
}

double SignalSystem::age(int controller, int group) const {
  if (controller < 0 || static_cast<size_t>(controller) >= since_.size()) return 0;
  const auto& s = since_[static_cast<size_t>(controller)];
  if (s.empty()) return 0;
  size_t g = static_cast<size_t>(std::max(0, group));
  return s[std::min(g, s.size() - 1)];
}

}  // namespace fs
