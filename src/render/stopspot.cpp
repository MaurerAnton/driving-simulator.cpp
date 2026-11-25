// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/stopspot.ts @ v1.7.7
#include "render/stopspot.h"

#include <cmath>
#include <limits>

#include "core/geom.h"
#include "world/xsection.h"

namespace fs {

namespace {

/** how far a stop's road may be from its pole (m) */
constexpr double REACH = 25;

/** Is this a road a bus stops at (not a parking aisle, a field track, a tunnel or bridge below or above)? */
bool busRoad(const Edge& e) { return e.cls != TRACK && (e.flags & (EF_MINOR_SERVICE | EF_TUNNEL)) == 0; }

}  // namespace

double clearOf(const Edge& e, int side) {
  const double half = e.width / 2;
  const SideLayout lay = sideLayout(half, side > 0 ? e.swL : e.swR, side > 0 ? e.xl.get() : e.xr.get());
  double clear = lay.kerb;
  if (lay.park) clear = std::max(clear, (*lay.park)[1]);
  // (on a cycle track only where there is a pavement to stand on beyond it)
  if (lay.track && lay.walk) clear = std::max(clear, (*lay.track)[1]);
  return clear;
}

StopSpot stopSpot(const World& roads, double x, double z, double y, int flags) {
  // the road: the one whose carriageway is nearest, at the pole's level
  const Edge* best = nullptr;
  double bestD = std::numeric_limits<double>::infinity();
  double bestS = 0;
  double bestOff = 0;
  PolyProjection proj;
  PolySample smp;
  roads.segmentsInBox(x - REACH, z - REACH, x + REACH, z + REACH, [&](const Edge& e, int seg) {
    if (!busRoad(e)) return;
    projectOnPolyline(e.pl, x, z, proj, seg, seg);
    const double d = std::max(0.0, std::sqrt(proj.d2) - e.width / 2);
    if (d > REACH) return;
    samplePolyline(e.pl, proj.s, smp, seg);
    if (std::abs(smp.y - y) > 4) return;
    // (at the same distance – the pole on two carriageways at once – the one it is nearer the middle of)
    const double score = d + std::sqrt(proj.d2) * 1e-3;
    if (score < bestD) {
      bestD = score;
      best = &e;
      bestS = proj.s;
      bestOff = proj.off;
    }
  });
  const Edge* e = best;
  if (!e) return StopSpot{x, z, 0, nullptr, 0, 0, 0, false};
  samplePolyline(e->pl, bestS, smp);
  // left of a→b in the world (x east, z south): the tangent turned a quarter to the left
  const double lx = smp.tz;
  const double lz = -smp.tx;
  const double half = e->width / 2;
  const double abs = std::abs(bestOff);
  // the way the buses run: +1 along a→b, −1 against it, 0 both ways
  const int way = e->lanesF > 0 && e->lanesB == 0 ? 1 : e->lanesB > 0 && e->lanesF == 0 ? -1 : 0;
  // the right of the way they run, as a side of a→b (right of a→b is −1)
  const int right = way == 0 ? -1 : -way;
  int side = bestOff >= 0 ? 1 : -1;
  const bool bus = (flags & STOP_FLAG_TRAM) == 0;
  // (mapped on the road itself, or near the middle of the carriageway: to the right of the way the buses run)
  if ((flags & STOP_FLAG_ON_ROAD) || abs < half * 0.35) side = right;
  // on a carriageway of a dual road, or between the two: to its right-hand kerb (a bus opens its doors on the right)
  else if (bus && way != 0 && (e->flags & EF_DUAL) && side != right && abs - half < 15) side = right;
  const double clear = clearOf(*e, side);
  const bool sameSide = (bestOff >= 0 ? 1 : -1) == side;
  const double want = clear + KERB_GAP;
  const double dist = sameSide && abs > want ? abs : want;
  const bool moved = !sameSide || abs < want;
  const double px = moved ? smp.x + lx * side * dist : x;
  const double pz = moved ? smp.z + lz * side * dist : z;
  // facing the road: towards the centre line
  const double fx = -lx * side;
  const double fz = -lz * side;
  return StopSpot{px, pz, std::atan2(-fx, -fz), e, side, dist, clear, moved};
}

bool inTheWay(World& world, double x, double z, double y) {
  static SurfaceHit hit = newSurfaceHit();
  if (!world.surfaceAt(x, z, y, hit)) return false;
  if (hit.zone == 0) return true;
  const Edge* e = hit.edge;
  if (hit.zone != 1 || !e) return false;
  return std::abs(hit.off) < clearOf(*e, hit.off >= 0 ? 1 : -1) - 0.05;
}

}  // namespace fs
