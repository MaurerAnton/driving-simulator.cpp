// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cars you see: body shell geometry built from cross-sections, plus the
 * four wheels and the Sculpt helper that every hand-built part of the cabin
 * (pillars, dashboard, seats) is made of.
 *
 * Car frame: origin on the ground below the middle of the wheelbase,
 * +X right, +Y up, -Z forward.  Left-hand drive.
 *
 * Origin: src/render/carBody.ts (MaurerAnton/driving-simulator @ ff37c5d,
 * v0.7.0).
 */
#pragma once

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/geometry.h"
#include "engine/scene.h"

namespace fs {

/** The car's proportions. */
struct CarDims {
  double length = 4.28;
  double width = 1.79;
  double height = 1.46;
  double wheelbase = 2.63;
  double track = 1.54;
  double wheelRadius = 0.316;
  double tyreWidth = 0.205;
  double frontOverhang = 0.88;
  /** 0 notchback, 1 hatchback, 2 estate */
  int tail = 0;
};

/** One cross-section of the body loft (a station of the shell). */
struct Section {
  double z = 0;      // station along the car
  double w = 0;      // half width at the sill
  double yb = 0;     // sill (bottom of the body)
  double ym = 0;     // waistline
  double ybelt = 0;  // belt line (bottom of the glass)
  double wr = 0;     // half width at the roof
  double yr = 0;     // roof height
  double yc = 0;     // crown of the roof
};

/** The finished shell: one geometry per material plus the landmarks. */
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
  double zFront = 0, zRear = 0, zAxleF = 0, zAxleR = 0;
  double zCowl = 0, zRoofFront = 0, zRoofRear = 0;
  /** middle of the B pillar and where the rear window meets the boot */
  double zPillarB = 0, zTail = 0;
  double halfWidth = 0, beltY = 0, roofY = 0;
  /** height of the windscreen's lower edge at its corners and in the middle */
  double cowlY = 0, cowlCrownY = 0;
  /** right-hand outline [x, y] of the flat nose and tail faces (the bumpers), bottom → top */
  std::vector<std::vector<double>> noseOutline;
  std::vector<std::vector<double>> tailOutline;
};

/** The standard driving-school compact (4.28 m, 1.79 m wide). */
extern const CarDims COMPACT_DIMS;

/**
 * Collects the triangles of a hand-built part (pillar, dashboard, seat …) and
 * shades it: smooth where neighbouring faces meet at a shallow angle, a sharp
 * edge where they do not.  Rounded shapes can then be drawn with few triangles
 * without looking faceted.
 */
class Sculpt {
 public:
  /** Corners counter-clockwise as seen from the visible side. */
  void tri(const double a[3], const double b[3], const double c[3]);
  void quad(const double a[3], const double b[3], const double c[3], const double d[3]);

  /**
   * Skin over closed rings (each ring the same number of points), visible from
   * outside – or from inside with `inward` (a recess) –, optionally with the
   * two ends closed.
   */
  void tube(const std::vector<std::vector<std::array<double, 3>>>& rings, bool capStart = false,
            bool capEnd = false, bool inward = false);

  /** Skin over open rows of points; `facing` is a direction its visible side looks to. */
  void sheet(const std::vector<std::vector<std::array<double, 3>>>& rows,
             const std::array<double, 3>& facing);

  /** A flat polygon as a fan around its middle, looking towards `facing`. */
  void fan(const std::vector<std::array<double, 3>>& ring, const std::array<double, 3>& facing);

  /** Non-indexed geometry with normals; faces meeting at more than `creaseDeg` keep a sharp edge. */
  GeometryPtr build(double creaseDeg = 40) const;

 private:
  std::vector<double> p_;
};

/** Build the shell of a car with these proportions. */
CarShell buildCarShell(const CarDims& d);

/** One wheel: tyre plus rim, axle along Z (three.js CylinderGeometry rotated). */
GeometryPtr buildWheel(double radius, double width, int rim = 0);

/** The two parts of a wheel, kept separate so the game can tint them. */
std::pair<GeometryPtr, GeometryPtr> buildWheelParts(const CarDims& d);

}  // namespace fs
