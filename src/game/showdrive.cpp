// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/showdrive.ts @ v1.7.7
#include "game/showdrive.h"

#include <cmath>
#include <limits>
#include <utility>

namespace fs {

void ShowSpots::add(ShowSpot s) {
  list.push_back(std::move(s));
  boxList_.clear();
}

const ShowSpot* ShowSpots::choose(int i) {
  if (i < 0 || static_cast<size_t>(i) >= list.size()) return nullptr;
  next_ = i;
  return &list[static_cast<size_t>(i)];
}

const ShowSpot* ShowSpots::start() {
  const int s = next_;
  next_ = -1;
  taken = s;
  for (size_t i = 0; i < list.size(); i++)
    for (Object3D* r : list[i].roots) r->visible = static_cast<int>(i) != taken;
  return s >= 0 ? &list[static_cast<size_t>(s)] : nullptr;
}

int ShowSpots::near(double x, double z, double reach) const {
  int best = -1;
  double bd = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < list.size(); i++) {
    if (static_cast<int>(i) == taken) continue;
    const ShowSpot& p = list[i];
    const double dx = x - p.x;
    const double dz = z - p.z;
    const double along = -dx * std::sin(p.yaw) - dz * std::cos(p.yaw);
    const double side = dx * std::cos(p.yaw) - dz * std::sin(p.yaw);
    if (along <= p.outline.front + reach && along >= -p.outline.rear - reach && std::abs(side) <= p.outline.hw + reach) {
      const double d = std::hypot(dx, dz);
      if (d < bd) {
        bd = d;
        best = static_cast<int>(i);
      }
    }
  }
  return best;
}

std::vector<PickBox> ShowSpots::boxes() {
  if (boxList_.size() != list.size()) {
    boxList_.clear();
    for (size_t i = 0; i < list.size(); i++) {
      const ShowSpot& s = list[i];
      PickBox b;
      b.front = s.outline.front;
      b.rear = s.outline.rear;
      b.hw = s.outline.hw;
      b.x = s.x;
      b.y = s.y;
      b.z = s.z;
      b.yaw = s.yaw;
      b.height = s.height;
      b.what = PickTarget{.kind = "show", .id = -1, .i = static_cast<int>(i)};
      boxList_.push_back(b);
    }
  }
  if (taken < 0) return boxList_;
  std::vector<PickBox> out;
  for (const PickBox& b : boxList_)
    if (b.what.kind != "show" || b.what.i != taken) out.push_back(b);
  return out;
}

}  // namespace fs
