// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Procedural car body: a lofted compact hatchback shell built from a handful
 * of cross-section parameters.  No model files – the same builder makes the
 * player's driving-school car and the traffic cars.
 *
 * Car frame: origin on the ground under the middle of the wheelbase,
 * +X right, +Y up, -Z forward (so the bonnet points to -Z).
 *
 * Origin: src/render/carBody.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <functional>
#include <vector>

#include "engine/engine.h"

namespace fs {

struct CarDims {
  double length = 0;
  double width = 0;
  double height = 0;
  double wheelbase = 0;
  double track = 0;
  double wheelRadius = 0;
  double tyreWidth = 0;
  double frontOverhang = 0;
  /** 0 = hatchback, 1 = estate/van-like long roof, 2 = saloon boot */
  int tail = 0;
};

inline const CarDims COMPACT_DIMS = {
    .length = 4.28,
    .width = 1.79,
    .height = 1.46,
    .wheelbase = 2.63,
    .track = 1.54,
    .wheelRadius = 0.316,
    .tyreWidth = 0.205,
    .frontOverhang = 0.88,
    .tail = 0,
};

struct CarShell {
  GeometryPtr paint;
  GeometryPtr glass;
  GeometryPtr trim;
  /** inward-facing copy of the opaque shell below the windows: floor, flanks, bulkheads */
  GeometryPtr lining;
  /** the same for the roof and the pillars between the windows */
  GeometryPtr headliner;
  /** A pillars along the windscreen edges: the faces seen from outside (body colour) … */
  GeometryPtr pillars;
  /** … and their trim towards the cabin */
  GeometryPtr pillarTrim;
  /** z of front and rear axle, bumper tips, cabin landmarks */
  double zFront = 0;
  double zRear = 0;
  double zAxleF = 0;
  double zAxleR = 0;
  double zCowl = 0;
  double zRoofFront = 0;
  double zRoofRear = 0;
  /** middle of the B pillar and where the rear window meets the boot */
  double zPillarB = 0;
  double zTail = 0;
  double halfWidth = 0;
  double beltY = 0;
  double roofY = 0;
  /** height of the windscreen's lower edge at its corners and in the middle */
  double cowlY = 0;
  double cowlCrownY = 0;
  /** right-hand outline [x, y] of the flat nose and tail faces (the bumpers), bottom → top */
  std::vector<std::array<double, 2>> noseOutline;
  std::vector<std::array<double, 2>> tailOutline;
};

/** `[number, number, number]` */
using Vec3 = std::array<double, 3>;

/**
 * Collects the triangles of a hand-built part (pillar, dashboard, seat …) and
 * shades it: smooth where neighbouring faces meet at a shallow angle, a sharp
 * edge where they do not.  Rounded shapes can then be drawn with few triangles
 * without looking faceted.
 */
class Sculpt {
 public:
  /** Corners counter-clockwise as seen from the visible side. */
  Sculpt& tri(const Vec3& a, const Vec3& b, const Vec3& c);

  Sculpt& quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d);

  /**
   * Skin over closed rings (each ring the same number of points), visible from
   * outside – or from inside with `inward` (a recess) –, optionally with the
   * two ends closed.
   */
  Sculpt& tube(const std::vector<std::vector<Vec3>>& rings, bool capStart = false, bool capEnd = false, bool inward = false);

  /** Skin over open rows of points; `facing` is a direction its visible side looks to. */
  Sculpt& sheet(const std::vector<std::vector<Vec3>>& rows, const Vec3& facing);

  /** A flat polygon as a fan around its middle, looking towards `facing`. */
  Sculpt& fan(const std::vector<Vec3>& ring, const Vec3& facing);

  /** Non-indexed geometry with normals; faces meeting at more than `creaseDeg` keep a sharp edge. */
  GeometryPtr build(double creaseDeg = 40) const;

 private:
  std::vector<double> p_;
};

/**
 * The outline of a body across the car at z, as its shell is built: the
 * right-hand points [x, y] from the middle of the floor out over the sill,
 * the flank, the belt line and the edge of the roof (or of the bonnet and
 * the boot) to the middle of the top – for what is fixed to the surface
 * (render/cardetail.ts).  Straight between the points; the left side is the
 * mirror image.
 */
std::function<std::vector<std::array<double, 2>>(double z)> bodyProfile(const CarDims& d);

CarShell buildCarShell(const CarDims& d);

/** what buildWheel answers */
struct WheelGeometry {
  GeometryPtr tyre;
  GeometryPtr rim;
};

/** A wheel lying along the X axis (rotate about X to spin). */
WheelGeometry buildWheel(const CarDims& d);

}  // namespace fs
