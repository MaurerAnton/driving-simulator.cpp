// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/stage.ts @ v1.7.7 (startFromSpot)
#include "ui/stage.h"

#include <cmath>
#include <limits>

namespace fs {

bool startFromSpot(const VehicleKind& kind, Settings::LastStart& last, const SpawnJson& spot, const std::vector<TramStopChoice>* tramStops,
                   const std::vector<RunwayChoice>* runways, const std::string& unnamed) {
  const auto nearest = [&spot](const auto* list) -> const auto* {
    using T = std::remove_reference_t<decltype((*list)[0])>;
    const T* best = nullptr;
    double d = std::numeric_limits<double>::infinity();
    if (list)
      for (const T& p : *list) {
        const double e = std::hypot(p.x - spot.x, p.z - spot.z);
        if (e < d) {
          d = e;
          best = &p;
        }
      }
    return best;
  };
  if (kind == "tram") {
    const TramStopChoice* t = nearest(tramStops);
    if (t) last.tramStop = tramKey(*t);
    return t != nullptr;
  }
  if (kind == "plane") {
    const RunwayChoice* r = nearest(runways);
    if (r) last.runway = runwayKey(*r);
    return r != nullptr;
  }
  StartAddress a;
  a.label = spot.name.empty() ? unnamed : spot.name;
  a.street = spot.name;
  a.x = spot.x;
  a.z = spot.z;
  a.exact = false;
  last.address = a;
  return true;
}

}  // namespace fs
