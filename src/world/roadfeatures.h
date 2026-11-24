// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/roadfeatures.ts @ v1.7.7
/**
 * Small things built into a road that the wheels feel or the junction shape
 * follows:
 *
 * - traffic calming across the carriageway – raised tables with their ramps,
 *   speed humps, bumps and cushions (OpenStreetMap traffic_calming=…), stored
 *   with each road as EdgeJson.cm and lifting its carriageway (not its
 *   pavements) where they are: the physics (World::surfaceAt) and the road
 *   mesh of the map build (tools/mapgen/roadmesh.ts) use the same lift;
 * - mouths over a continuous pavement (EdgeJson.mk): a living street or a
 *   driveway that meets a road over a lowered kerb, with the pavement
 *   running on across it and no kerbed corner radii.
 *
 * What the original asks of a road or an arm by its shape (`CalmedRoad`,
 * `MouthArm`), the port asks by a template: anything with those fields will do
 * (an `Edge`, an `Arm`, or a plain `CalmedRoad` in a test).
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "core/js.h"
#include "engine/math.h"

namespace fs {

/** traffic_calming=… across the carriageway (EdgeJson.cm kinds) (CALM) */
inline constexpr int CALM_TABLE = 1, CALM_HUMP = 2, CALM_BUMP = 3, CALM_CUSHION = 4;

/** numbers per entry of EdgeJson.cm: station of the middle, CALM kind, length (a table: its flat top), height */
inline constexpr size_t CALM_STRIDE = 4;

/** the ramp of a raised table (m along the road) */
inline constexpr double TABLE_RAMP = 1.2;

/** Usual length and height of each kind (m), when the map says nothing: CALM_DEFAULTS[kind]. */
struct CalmDefault {
  double len;
  double h;
};
inline constexpr CalmDefault CALM_DEFAULTS[5] = {{0, 0}, {4, 0.08}, {3.7, 0.08}, {0.6, 0.06}, {2.5, 0.06}};

/** The CALM kind of a traffic_calming value (0: not one across the carriageway). */
int calmKind(const std::string& v);

/** How far each entry reaches from its middle along the road (m). */
inline double calmHalfLength(double kind, double len) { return kind == CALM_TABLE ? len / 2 + TABLE_RAMP : len / 2; }

/** What a road needs for the lift: its calming entries and where its junction areas begin. */
struct CalmedRoad {
  std::optional<std::vector<double>> calm;
  double len = 0;
  double cutA = 0;
  double cutB = 0;
};

/**
 * Where the middle of entry `i` lies: as stored, but moved off the junction
 * areas (whose surface is the junction's plane) so that the whole of it lies
 * on the road between them.  NaN: no room.
 */
template <class Road>
double calmCentre(const Road& e, size_t i) {
  const std::vector<double>& c = *e.calm;
  const double half = calmHalfLength(c[i + 1], c[i + 2]);
  const double lo = e.cutA + half + 0.3;
  const double hi = e.len - e.cutB - half - 0.3;
  if (lo > hi) return JS_NAN;
  return jsMin(hi, jsMax(lo, c[i]));
}

/** How much the carriageway is raised at station `s` (m; 0 almost everywhere). */
template <class Road>
double calmLift(const Road& e, double s) {
  if (!e.calm) return 0;
  const std::vector<double>& c = *e.calm;
  double lift = 0;
  for (size_t i = 0; i + CALM_STRIDE <= c.size(); i += CALM_STRIDE) {
    const double kind = c[i + 1];
    const double len = c[i + 2];
    const double h = c[i + 3];
    const double half = calmHalfLength(kind, len);
    const double mid = calmCentre(e, i);
    const double d = std::fabs(s - mid);
    if (!(d < half)) continue;
    double y;
    if (kind == CALM_TABLE) y = d <= len / 2 ? h : h * (1 - (d - len / 2) / TABLE_RAMP);
    else {
      const double k = std::cos((PI * d) / len);
      y = h * k * k;
    }
    if (y > lift) lift = y;
  }
  return lift;
}

/** The stations where the lift bends (for meshes that follow it): ramp feet and tops, and steps over humps. */
template <class Road>
std::vector<double> calmBreaks(const Road& e) {
  std::vector<double> out;
  if (!e.calm) return out;
  const std::vector<double>& c = *e.calm;
  for (size_t i = 0; i + CALM_STRIDE <= c.size(); i += CALM_STRIDE) {
    const double mid = calmCentre(e, i);
    if (mid != mid) continue;
    const double kind = c[i + 1];
    const double len = c[i + 2];
    if (kind == CALM_TABLE) {
      out.push_back(mid - len / 2 - TABLE_RAMP);
      out.push_back(mid - len / 2);
      out.push_back(mid + len / 2);
      out.push_back(mid + len / 2 + TABLE_RAMP);
    } else {
      const double n = jsMax(4, std::ceil(len / 0.3));
      for (double k = 0; k <= n; k++) out.push_back(mid - len / 2 + (len * k) / n);
    }
  }
  std::stable_sort(out.begin(), out.end(), [](double p, double q) { return p < q; });
  return out;
}

/**
 * Does this arm meet its junction over a continuous pavement (a lowered kerb
 * across its mouth, no kerbed corners)?  Where its road says so (EdgeJson.mk)
 * and at least two other roads that do not meet there in the same way go on
 * through the junction.
 */
template <class MouthArm>
bool flushMouth(const MouthArm& arm) {
  if (!arm.edge->mouth) return false;
  int through = 0;
  for (const auto& a : arm.node->arms)
    if (a->edge != arm.edge && !a->edge->mouth) through++;
  return through >= 2;
}

}  // namespace fs
