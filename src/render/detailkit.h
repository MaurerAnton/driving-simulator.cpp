// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Building blocks of the finer vehicles of the "very high" graphics
 * (render/cardetail.ts, render/fleetdetail.ts).
 *
 * - Parts: geometry collected per material and merged into one mesh each, so
 *   that a hundred small parts cost a handful of draw calls.
 * - Frame: a little coordinate system (an origin and three axes) to build a
 *   part in where it sits – on a door, a spoke, the face of a lamp.
 * - Surface: rays against the body's shell, quick enough to fix strips and
 *   sheets onto it point by point: panel gaps, window seals, lamp lenses
 *   that follow the curve of the bodywork.
 * - The detailed wheel: a tyre with a rounded sidewall and tread, an open
 *   rim with spokes, nuts and a centre cap, the brake disc behind it and the
 *   caliper on the disc – the four of a vehicle drawn as instances.
 *
 * Origin: src/render/detailkit.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"

namespace fs {

using V3 = std::array<double, 3>;

/** Position and normal only, un-indexed: the form in which parts are merged. */
GeometryPtr bare(const GeometryPtr& g);

/** Geometry collected per material; `build` merges each material's parts into one mesh. */
class Parts {
 public:
  void add(const MaterialPtr& material, const std::vector<GeometryPtr>& geos);
  template <class... G>
  void add(const MaterialPtr& material, const GeometryPtr& g, const G&... more) {
    add(material, std::vector<GeometryPtr>{g, more...});
  }

  /** One mesh per material, added to `parent`. */
  std::vector<std::shared_ptr<Mesh>> build(Object3D& parent, const std::string& name, bool castShadow = false);

 private:
  // (a Map keeps the order the materials came in)
  std::vector<std::pair<MaterialPtr, std::vector<GeometryPtr>>> lists_;
};

/** An origin and three axes (x across, y up, z out of the surface the part sits on): parts are built in it. */
class Frame {
 public:
  Vector3 o;
  Vector3 x;
  Vector3 y;
  Vector3 z;

  Frame(const Vector3& o, const Vector3& x, const Vector3& y, const std::optional<Vector3>& z = std::nullopt);

  /** A frame on a surface point: z its normal, y as near to `up` as the surface lets it be. */
  static Frame on(const Vector3& p, const Vector3& n, const Vector3& up = Vector3(0, 1, 0));

  /** A point given in the frame. */
  Vector3 at(double u, double v, double w = 0) const;

  /** The same frame moved to (u, v, w) of itself (and turned by `roll` about its z). */
  Frame moved(double u, double v, double w = 0, double roll = 0) const;

  /** Geometry built around the origin (x, y, z as the frame's) put into place. */
  GeometryPtr put(const GeometryPtr& g) const;

  /** A box w × h × d (along the frame's x, y, z), its middle at (u, v, w). */
  GeometryPtr box(double w, double h, double d, double u = 0, double v = 0, double wz = 0) const;

  /** A cylinder along the frame's z, its middle at (u, v, w). */
  GeometryPtr cyl(double r, double len, double u = 0, double v = 0, double wz = 0, int seg = 14, std::optional<double> r2 = std::nullopt) const;

  /** A ring (a torus) in the frame's x-y plane. */
  GeometryPtr ring(double r, double tube, double u = 0, double v = 0, double wz = 0, int seg = 24) const;

  /** A flat disc facing the frame's z. */
  GeometryPtr disc(double r, double u = 0, double v = 0, double wz = 0, int seg = 16) const;
};

/** A point where a ray met the surface, and the surface's normal there (towards where the ray came from). */
struct Hit {
  Vector3 p;
  Vector3 n;
};

/**
 * Rays against a few geometries (a body's shell).  The triangles are sorted
 * into slices along z, so that a ray tests only those of the slices it passes.
 */
class Surface {
 public:
  explicit Surface(const std::vector<GeometryPtr>& geos, double step = 0.05);

  /** The first point of the surface on the ray from `o` along the unit vector `d`, within `far`. */
  std::optional<Hit> hit(const Vector3& o, const Vector3& d, double far = 4);

  /** The surface below `p` as seen along `d` (from 1 m before it). */
  std::optional<Hit> onto(const Vector3& p, const Vector3& d);

 private:
  int slice(double z) const;
  std::vector<float> tri_;
  std::vector<std::vector<int>> slices_;
  double z0_ = 0;
  double step_ = 0.05;
  std::vector<uint32_t> seen_;
  uint32_t stamp_ = 0;
};

/**
 * A sheet over the surface: rays from the points of a grid (`origin(u, v)`,
 * u and v from 0 to 1) along `d`, the hits lifted `lift` off the surface.
 * Cells with a missed corner stay open.
 */
GeometryPtr drape(Surface& surf, const std::function<Vector3(double u, double v)>& origin, const Vector3& d, int nu, int nv, double lift);

/** A sheet through rows of surface points, lifted along their normals; open where a point is missing. */
GeometryPtr sheetOf(const std::vector<std::vector<std::optional<Hit>>>& grid, double lift);

/** A strip `width` wide along points of a surface, lifted `lift` off it (a panel gap, a seal, a trim line). */
GeometryPtr ribbon(const std::vector<Hit>& pts, double width, double lift);

/** Points along a path of the surface: `path(t)` for t from 0 to 1 in `n` steps, each brought onto the surface along `d(t)`. */
std::vector<Hit> along(Surface& surf, const std::function<Vector3(double t)>& path, const std::function<Vector3(double t)>& d, int n);

/** A surface of revolution about the x axis: profile points [radius, x], as seen from +x: outward normals when x rises along it. */
GeometryPtr lathe(const std::vector<std::array<double, 2>>& profile, int seg, double phase = 0);

// ------------------------------------------------------------------------------------ the wheel
struct WheelSpec {
  /** outer radius of the tyre, its width (m) */
  double radius = 0;
  double width = 0;
  /** radius of the rim's edge (where the tyre sits on it) */
  double rim = 0;
  /** number of spokes of the rim (0: a steel wheel – a dished disc with holes) */
  int spokes = 0;
  /** wheel nuts round the hub */
  int nuts = 0;
  /** the brake disc and its caliper behind the rim (a car), or a drum (a truck) */
  bool disc = false;
  /** lugs of a tractor tyre instead of a road tread */
  bool lugs = false;
  /** two tyres side by side (the rear axle of a truck or a bus): `width` is that of both */
  bool twin = false;
};

struct WheelPartsOut {
  GeometryPtr tyre;
  GeometryPtr metal;
  /** null: none */
  GeometryPtr caliper;
};

/**
 * The parts of one wheel, its axis along x and its outer face towards +x
 * (side 1) or −x (side −1): what turns with it (tyre, metal: rim and disc)
 * and what only steers with it (the caliper).
 */
WheelPartsOut wheelParts(const WheelSpec& w, int side);

/** The mirror image across x = 0, with the winding and the normals put right. */
GeometryPtr flipX(const GeometryPtr& g);

/** A closed skin through rings of the same number of points (no caps). */
GeometryPtr tubeOf(const std::vector<std::vector<V3>>& rings, bool caps = true);

/**
 * The four detailed wheels of a vehicle as instances: one draw call for the
 * tyres of a side, one for their metal, one for the calipers.  `place`
 * puts wheel i where its plain twin is (its steering and its turning).
 */
class WheelSet {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  struct Materials {
    MaterialPtr tyre;
    MaterialPtr metal;
    MaterialPtr caliper;
  };
  /** `sides`: the side of each wheel (−1 left, 1 right); `materials`: tyre, metal (vertex colours), caliper. */
  WheelSet(const WheelSpec& spec, const std::vector<int>& sides, const Materials& materials);

  /** Wheel i at `pos` (in the vehicle's frame), steered by `steer` (rad, + = right) and turned by `spin` (rad, rolling forward +). */
  void place(int i, const Vector3& pos, double steer, double spin);

  int count() const { return static_cast<int>(sides_.size()); }

 private:
  struct Set {
    std::shared_ptr<InstancedMesh> tyre;
    std::shared_ptr<InstancedMesh> metal;
    std::shared_ptr<InstancedMesh> caliper;
    std::vector<int> index;
  };
  std::vector<Set> meshes_;
  std::vector<int> sides_;
  Matrix4 m_;
  Quaternion q_;
  Euler e_;
  const Vector3 one_ = Vector3(1, 1, 1);
  Vector3 p_;
};

/** A number plate as a German one is: the EU band with the country letter, the characters, the seals (both on the rear plate). */
TexturePtr plateTexture(const std::string& text, bool rear);

}  // namespace fs
