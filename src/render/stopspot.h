// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Where the furniture of a bus stop stands: the sign, the shelter, the bench,
 * the departure board and the people waiting – on the pavement beside the
 * carriageway, never on it.
 *
 * The map puts a stop's pole wherever its mapper did: at the kerb, on the
 * pavement, on the road's centre line (a highway=bus_stop node on the way
 * itself, or a stop position), between the two carriageways of a dual road.
 * From the road the pole belongs to and that road's cross-section
 * (src/world/xsection.ts) this works out the line along which the furniture
 * begins: behind the kerb, behind cars parked on the pavement and behind a
 * cycle track where a pavement lies beyond it – on the side the bus stops at.
 *
 *  - A pole beside the road keeps its side; it is moved out only as far as
 *    it stands in the way.
 *  - A pole on the carriageway goes to its nearer kerb – or, mapped on the
 *    road itself or near its middle, to the right of the way the buses run.
 *  - A bus stop on a carriageway of a dual road, or mapped between the two,
 *    goes to the right-hand kerb of the nearer one (a bus opens its doors on
 *    the right).
 *
 * TransitScene (transit.ts) then still checks every piece against the road
 * surface and steps it back where a junction's corner reaches it.
 *
 * Origin: src/render/stopspot.ts @ v1.7.7
 */
#pragma once

#include "world/world.h"

namespace fs {

struct StopSpot {
  /** the foot of the pole: the front line of the furniture */
  double x = 0;
  double z = 0;
  /** facing the road (yaw: 0 = north, + = turned left) */
  double yaw = 0;
  /** the road it belongs to (null: none near – the spot is the pole's own) */
  const Edge* edge = nullptr;
  /** on which side of the road (+1 left, −1 right of its direction a→b) */
  int side = 0;
  /** the distance of the spot from the road's centre line, and how far from it the way is clear */
  double dist = 0;
  double clear = 0;
  /** whether the spot is not where the pole was mapped */
  bool moved = false;
};

/** how far behind the clear line the pole stands (m) */
inline constexpr double KERB_GAP = 0.35;

/**
 * How far from the centre line of `e` nothing of the stop may stand on `side`: the kerb, cars parked on
 * the pavement, and a cycle track with a pavement beyond it.
 */
double clearOf(const Edge& e, int side);

/**
 * The spot for the furniture of the pole at (x, z) with its STOP_FLAG `flags`; `y` picks the road at the
 * pole's level where roads cross.  (The original's StopRoads: what of the road network it needs – the World.)
 */
StopSpot stopSpot(const World& roads, double x, double z, double y, int flags);

/**
 * Does (x, z) lie where nothing of a stop may stand: on a carriageway (parking lanes and cycle lanes are part
 * of it), or on the pavement among the cars parked on it or on a cycle track (`clearOf`)?
 */
bool inTheWay(World& world, double x, double z, double y);

}  // namespace fs
