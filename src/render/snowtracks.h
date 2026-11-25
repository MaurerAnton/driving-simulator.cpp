// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/snowtracks.ts @ v1.7.7
/**
 * Wheel tracks in the snow: where the traffic drives, two strips of dark,
 * wet asphalt along every lane round the camera (and along the paths through
 * the junctions), with soft edges, laid over the snowy road.
 *
 * The roads' own material does not know where the lanes run, so the tracks
 * are a mesh of their own, built from the lane network on the processor –
 * only when snow lies on the road, only round the camera, and anew only when
 * the camera has moved a good way (or the network changed).  Nothing per
 * frame but a distance check; one draw call.
 */
#pragma once

#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include "core/geom.h"
#include "engine/engine.h"
#include "world/lanes.h"

namespace fs {

/** the wheels run this far either side of the middle of a lane (m): half a car's track */
inline constexpr double HALF_TRACK = 0.78;

class SnowTracks {
 public:
  /** add this to the scene */
  std::shared_ptr<Mesh> mesh;
  /** how far round the camera the tracks are laid (m) – less on a slow device */
  double radius = 160;
  /** how much they show: SeasonState.roadSnow (0 … 1; nothing is built while it is 0) */
  double amount = 0;

  SnowTracks();
  SnowTracks(const SnowTracks&) = delete;
  SnowTracks& operator=(const SnowTracks&) = delete;

  /**
   * Once a frame (cheap): the camera's place, the lanes (LaneGraph.lanes – any
   * list of `Lane*`, null at its gaps) and, if given, the paths through the
   * junctions (LaneGraph.connectors).
   */
  template <class Lanes>
  void update(double x, double z, const Lanes& lanes, const std::vector<Connector*>* connectors = nullptr) {
    const double a = std::min(1.0, std::max(0.0, amount));
    mesh->material->opacity = 0.9 * a;
    if (a < 0.02) {
      mesh->visible = false;
      return;
    }
    if (!(std::hypot(x - cx_, z - cz_) < radius * 0.35)) {
      std::vector<const Lane*> list;
      for (const Lane* lane : lanes) list.push_back(lane);
      build(x, z, list, connectors);
    }
    mesh->visible = mesh->geometry->index != nullptr && mesh->geometry->index->count() > 0;
  }

  /** Lay them anew at the next update – a piece of the network came or went. */
  void invalidate() { cx_ = std::numeric_limits<double>::quiet_NaN(); }

  void dispose();

 private:
  void build(double x, double z, const std::vector<const Lane*>& lanes, const std::vector<Connector*>* connectors);
  /** Two tracks along a path of `len` metres (`at`: the point and direction at a distance), where it runs near the camera. */
  void strip(double len, const std::function<PolySample&(double, PolySample&)>& at, double x, double z, std::vector<float>& pos, std::vector<uint32_t>& idx);

  double cx_ = std::numeric_limits<double>::quiet_NaN();
  double cz_ = std::numeric_limits<double>::quiet_NaN();
  PolySample s_ = newPolySample();
};

}  // namespace fs
