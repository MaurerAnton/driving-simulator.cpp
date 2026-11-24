// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/airfield.ts @ v1.7.7
/**
 * The airfields of the map as the aeroplane's wheels meet them: a paved
 * runway, taxiway or apron is firm and rolls like a road, a grass runway
 * like a mown field – wherever else the ground is what the world says.
 * Also where an aeroplane is put to begin: at the end of a runway, lined up
 * along it.
 */
#pragma once

#include <string>
#include <vector>

#include "sim/vehicle/types.h"
#include "world/types.h"

namespace fs {

/** a surface the map calls paved */
/** how far above the ground the surfaces of an airfield are drawn (render/airfield.ts: 0.07 to 0.09 m) */
inline constexpr double SURFACE_LIFT = 0.08;

bool paved(const std::string& surface);

class AirfieldGround : public Ground {
 public:
  AirfieldGround(Ground& base, const std::vector<AirfieldJson>& fields);

  /** On a strip of the airfield: paved (1), grass (0) – or not on one (-1). */
  int surfaceAt(double x, double z) const;

  void sample(double x, double z, double yRef, GroundSample& out) override;

 private:
  struct Strip {
    /** the middle line and its direction, its length and half its width */
    double x1;
    double z1;
    double ux;
    double uz;
    double len;
    double hw;
    bool paved;
  };

  Ground& base;
  std::vector<Strip> strips;
  std::vector<std::vector<double>> aprons;

  void addStrip(double x1, double z1, double x2, double z2, double w, bool isPaved);
};

struct RunwayStart {
  double x;
  double z;
  double yaw;
};

/** Where to begin on a runway: 60 m in from one end, lined up along it (yaw as everywhere in the game: 0 north, + left). */
RunwayStart runwayStart(const RunwayJson& r, bool fromSecond);

/**
 * The name of a runway end as it is painted on it: the magnetic heading in
 * tens of degrees ("09", "27") – from the map's "09/27" where it says, else
 * from its direction (north as the map's grid north).
 */
std::string runwayEnd(const RunwayJson& r, bool fromSecond);

}  // namespace fs
