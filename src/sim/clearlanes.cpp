// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/clearlanes.ts @ v1.7.7
#include "sim/clearlanes.h"

#include "sim/jsmath.h"

namespace fs {

std::pair<int, int> clearSigns(LaneGraph& lanes, std::vector<SignPost>& signs) {
  PathFoot foot;
  int moved = 0;
  int dropped = 0;
  for (int i = static_cast<int>(signs.size()) - 1; i >= 0; i--) {
    SignPost& s = signs[static_cast<size_t>(i)];
    const PathFoot* at = lanes.besideLanes(s.x, s.z, s.y, POST_CLEAR, foot);
    if (!at) {
      signs.erase(signs.begin() + i);
      dropped++;
    } else if (at->d > 0) {
      s.x = jsRound(at->x * 100) / 100;
      s.z = jsRound(at->z * 100) / 100;
      moved++;
    }
  }
  return {moved, dropped};
}

int clearObjects(LaneGraph& lanes, std::vector<TileObject>& objects) {
  PathFoot foot;
  int dropped = 0;
  for (int i = static_cast<int>(objects.size()) - 1; i >= 0; i--) {
    const TileObject& o = objects[static_cast<size_t>(i)];
    if (o.k != "lamp") continue;
    if (lanes.nearestPath(o.x, o.z, o.y, POST_CLEAR, foot) < POST_CLEAR) {
      objects.erase(objects.begin() + i);
      dropped++;
    }
  }
  return dropped;
}

bool postInLane(LaneGraph& lanes, double x, double y, double z) {
  PathFoot foot;
  return lanes.nearestPath(x, z, y, POST_CLEAR, foot) < POST_CLEAR;
}

std::vector<float> clearWalls(LaneGraph& lanes, const std::vector<float>* rings_, const std::vector<uint16_t>* ringCounts, const std::vector<float>* segments_) {
  std::vector<float> out;
  if (rings_ && ringCounts) {
    const std::vector<float>& rings = *rings_;
    size_t o = 0;
    for (const uint16_t n : *ringCounts) {
      for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        const double x1 = rings[o + i * 2];
        const double z1 = rings[o + i * 2 + 1];
        const double x2 = rings[o + j * 2];
        const double z2 = rings[o + j * 2 + 1];
        if (!lanes.wallInLane(x1, z1, x2, z2, JS_NAN, WALL_CLEAR)) {
          out.push_back(static_cast<float>(x1));
          out.push_back(static_cast<float>(z1));
          out.push_back(static_cast<float>(x2));
          out.push_back(static_cast<float>(z2));
          out.push_back(static_cast<float>(JS_NAN));
        }
      }
      o += static_cast<size_t>(n) * 2;
    }
  }
  if (segments_) {
    const std::vector<float>& segments = *segments_;
    for (size_t i = 0; i + 4 < segments.size(); i += 5) {
      if (!lanes.wallInLane(segments[i], segments[i + 1], segments[i + 2], segments[i + 3], segments[i + 4], WALL_CLEAR)) {
        out.push_back(segments[i]);
        out.push_back(segments[i + 1]);
        out.push_back(segments[i + 2]);
        out.push_back(segments[i + 3]);
        out.push_back(segments[i + 4]);
      }
    }
  }
  return out;
}

}  // namespace fs
