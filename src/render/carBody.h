// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cars you see: body shell geometry built from cross-sections, plus the
 * four wheels.
 *
 * Car frame: origin on the ground below the middle of the wheelbase,
 * +X right, +Y up, -Z forward.  Left-hand drive.
 *
 * Origin: src/render/carBody.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <memory>
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
  /** inward-facing copy of the opaque shell (cabin lining), without the glazing */
  GeometryPtr lining;
  /** A pillars: solid bars along the windscreen edges, seen from inside and outside */
  GeometryPtr pillars;
  /** z of front and rear axle, bumper tips, cabin landmarks */
  double zFront = 0, zRear = 0, zAxleF = 0, zAxleR = 0;
  double zCowl = 0, zRoofFront = 0, zRoofRear = 0;
  double halfWidth = 0, beltY = 0, roofY = 0;
};

/** The standard driving-school compact (4.28 m, 1.79 m wide). */
extern const CarDims COMPACT_DIMS;

/** Build the shell of a car with these proportions. */
CarShell buildCarShell(const CarDims& d);

/** One wheel: tyre plus rim, axle along Z (three.js CylinderGeometry rotated). */
GeometryPtr buildWheel(double radius, double width, int rim = 0);

/** The two parts of a wheel, kept separate so the game can tint them. */
std::pair<GeometryPtr, GeometryPtr> buildWheelParts(const CarDims& d);

}  // namespace fs
