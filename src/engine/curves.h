// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Curves, paths and shapes – three.js's, by the same names:
 *
 *     const curve = new THREE.CatmullRomCurve3(points, false, 'centripetal');
 *     const p = curve.getPointAt(t), tan = curve.getTangentAt(t);
 *
 *     CatmullRomCurve3 curve(points, false, CatmullRomCurve3::Centripetal);
 *     Vector3 p = curve.getPointAt(t), tan = curve.getTangentAt(t);
 *
 *     const s = new THREE.Shape(outline);  s.holes.push(new THREE.Path(hole));
 *     Shape s(outline);                     s.holes.push_back(make<Path>(hole));
 *
 * A curve is a `Curve<Vector2>` (in the plane: LineCurve, QuadraticBezierCurve,
 * CubicBezierCurve, EllipseCurve, SplineCurve, and Path and Shape made of them) or a
 * `Curve<Vector3>` (in space: LineCurve3, QuadraticBezierCurve3, CubicBezierCurve3,
 * CatmullRomCurve3).  Points are answered by value (JavaScript's optional target is
 * left out).  ShapeUtils and Earcut are here too.
 *
 * Origin: three.js r187 src/extras/core/{Curve,CurvePath,Path,Shape,Interpolations}.js,
 * the classes of src/extras/curves/, src/extras/ShapeUtils.js, src/extras/Earcut.js, src/extras/lib/earcut.js
 */
#pragma once

#include <array>
#include <concepts>
#include <memory>
#include <string>
#include <vector>

#include "engine/math.h"

namespace fs {

/** what computeFrenetFrames answers: a tangent, a normal and a binormal at each of segments + 1 places */
struct FrenetFrames {
  std::vector<Vector3> tangents, normals, binormals;
};

// ---------------------------------------------------------------------------
// Curve
// ---------------------------------------------------------------------------

template <class V>
class Curve {
 public:
  /** how many pieces the curve's length is measured in */
  int arcLengthDivisions = 200;
  /** set when the curve was changed: the lengths are measured anew */
  bool needsUpdate = false;

  Curve() = default;
  Curve(const Curve&) = default;
  Curve& operator=(const Curve&) = default;
  virtual ~Curve() = default;

  /** the point at t (0 … 1) as the curve's own formula has it */
  virtual V getPoint(double t) const = 0;
  /** the point at the part u (0 … 1) of the curve's length */
  virtual V getPointAt(double u) const;
  /** divisions + 1 points at even steps of t (no number given: 5 – of a path 12) */
  std::vector<V> getPoints() const { return getPoints(defaultDivisions()); }
  virtual std::vector<V> getPoints(int divisions) const;
  /** divisions + 1 points at even steps of length (no number given: 5 – of a path 40) */
  std::vector<V> getSpacedPoints() const { return getSpacedPoints(defaultSpacedDivisions()); }
  virtual std::vector<V> getSpacedPoints(int divisions) const;
  virtual double getLength() const;
  /** the length up to each of `divisions` steps of t (kept until needsUpdate) */
  const std::vector<double>& getLengths() const { return getLengths(arcLengthDivisions); }
  const std::vector<double>& getLengths(int divisions) const;
  virtual void updateArcLengths();
  /** the t of the part u of the length (or of a length `distance` along the curve, when that is not 0) */
  double getUtoTmapping(double u, double distance = 0) const;
  /** the unit tangent at t / at the part u of the length */
  virtual V getTangent(double t) const;
  virtual V getTangentAt(double u) const;
  /** frames along a curve in space, turning as little as the curve allows (TubeGeometry, ExtrudeGeometry) */
  FrenetFrames computeFrenetFrames(int segments, bool closed = false) const
    requires std::same_as<V, Vector3>;
  virtual std::shared_ptr<Curve<V>> clone() const = 0;

  // (what CurvePath.getPoints and ExtrudeGeometry ask of a curve)
  virtual bool isLineCurve() const { return false; }
  virtual bool isEllipseCurve() const { return false; }
  /** of a SplineCurve: how many points it goes through (-1: it is none) */
  virtual int splinePointCount() const { return -1; }
  /** of a CatmullRomCurve3: whether it is closed */
  virtual bool isClosedCatmullRom() const { return false; }

 protected:
  virtual int defaultDivisions() const { return 5; }
  virtual int defaultSpacedDivisions() const { return 5; }
  mutable std::vector<double> cacheArcLengths;
};

extern template class Curve<Vector2>;
extern template class Curve<Vector3>;

// ---------------------------------------------------------------------------
// the curves
// ---------------------------------------------------------------------------

/** a straight line in the plane */
class LineCurve : public Curve<Vector2> {
 public:
  Vector2 v1, v2;
  LineCurve() = default;
  LineCurve(const Vector2& v1_, const Vector2& v2_) : v1(v1_), v2(v2_) {}
  Vector2 getPoint(double t) const override;
  Vector2 getPointAt(double u) const override { return getPoint(u); }
  Vector2 getTangent(double t) const override;
  Vector2 getTangentAt(double u) const override { return getTangent(u); }
  std::shared_ptr<Curve<Vector2>> clone() const override { return std::make_shared<LineCurve>(*this); }
  bool isLineCurve() const override { return true; }
};

/** a straight line in space */
class LineCurve3 : public Curve<Vector3> {
 public:
  Vector3 v1, v2;
  LineCurve3() = default;
  LineCurve3(const Vector3& v1_, const Vector3& v2_) : v1(v1_), v2(v2_) {}
  Vector3 getPoint(double t) const override;
  Vector3 getPointAt(double u) const override { return getPoint(u); }
  Vector3 getTangent(double t) const override;
  Vector3 getTangentAt(double u) const override { return getTangent(u); }
  std::shared_ptr<Curve<Vector3>> clone() const override { return std::make_shared<LineCurve3>(*this); }
  bool isLineCurve() const override { return true; }
};

class QuadraticBezierCurve : public Curve<Vector2> {
 public:
  Vector2 v0, v1, v2;
  QuadraticBezierCurve() = default;
  QuadraticBezierCurve(const Vector2& v0_, const Vector2& v1_, const Vector2& v2_) : v0(v0_), v1(v1_), v2(v2_) {}
  Vector2 getPoint(double t) const override;
  std::shared_ptr<Curve<Vector2>> clone() const override { return std::make_shared<QuadraticBezierCurve>(*this); }
};

class QuadraticBezierCurve3 : public Curve<Vector3> {
 public:
  Vector3 v0, v1, v2;
  QuadraticBezierCurve3() = default;
  QuadraticBezierCurve3(const Vector3& v0_, const Vector3& v1_, const Vector3& v2_) : v0(v0_), v1(v1_), v2(v2_) {}
  Vector3 getPoint(double t) const override;
  std::shared_ptr<Curve<Vector3>> clone() const override { return std::make_shared<QuadraticBezierCurve3>(*this); }
};

class CubicBezierCurve : public Curve<Vector2> {
 public:
  Vector2 v0, v1, v2, v3;
  CubicBezierCurve() = default;
  CubicBezierCurve(const Vector2& v0_, const Vector2& v1_, const Vector2& v2_, const Vector2& v3_) : v0(v0_), v1(v1_), v2(v2_), v3(v3_) {}
  Vector2 getPoint(double t) const override;
  std::shared_ptr<Curve<Vector2>> clone() const override { return std::make_shared<CubicBezierCurve>(*this); }
};

class CubicBezierCurve3 : public Curve<Vector3> {
 public:
  Vector3 v0, v1, v2, v3;
  CubicBezierCurve3() = default;
  CubicBezierCurve3(const Vector3& v0_, const Vector3& v1_, const Vector3& v2_, const Vector3& v3_) : v0(v0_), v1(v1_), v2(v2_), v3(v3_) {}
  Vector3 getPoint(double t) const override;
  std::shared_ptr<Curve<Vector3>> clone() const override { return std::make_shared<CubicBezierCurve3>(*this); }
};

/** an arc of an ellipse (of a circle: xRadius = yRadius), from aStartAngle to aEndAngle */
class EllipseCurve : public Curve<Vector2> {
 public:
  double aX = 0, aY = 0, xRadius = 1, yRadius = 1, aStartAngle = 0, aEndAngle = PI * 2;
  bool aClockwise = false;
  double aRotation = 0;
  EllipseCurve() = default;
  EllipseCurve(double aX_, double aY_, double xRadius_ = 1, double yRadius_ = 1, double aStartAngle_ = 0, double aEndAngle_ = PI * 2, bool aClockwise_ = false, double aRotation_ = 0)
      : aX(aX_), aY(aY_), xRadius(xRadius_), yRadius(yRadius_), aStartAngle(aStartAngle_), aEndAngle(aEndAngle_), aClockwise(aClockwise_), aRotation(aRotation_) {}
  Vector2 getPoint(double t) const override;
  std::shared_ptr<Curve<Vector2>> clone() const override { return std::make_shared<EllipseCurve>(*this); }
  bool isEllipseCurve() const override { return true; }
};

/** a smooth curve through points in the plane */
class SplineCurve : public Curve<Vector2> {
 public:
  std::vector<Vector2> points;
  SplineCurve() = default;
  explicit SplineCurve(std::vector<Vector2> points_) : points(std::move(points_)) {}
  Vector2 getPoint(double t) const override;
  std::shared_ptr<Curve<Vector2>> clone() const override { return std::make_shared<SplineCurve>(*this); }
  int splinePointCount() const override { return static_cast<int>(points.size()); }
};

/** a smooth curve through points in space */
class CatmullRomCurve3 : public Curve<Vector3> {
 public:
  /** `'centripetal'`, `'chordal'`, `'catmullrom'` */
  enum CurveType { Centripetal, Chordal, CatmullRom };
  std::vector<Vector3> points;
  bool closed = false;
  CurveType curveType = Centripetal;
  /** of the 'catmullrom' type */
  double tension = 0.5;
  CatmullRomCurve3() = default;
  explicit CatmullRomCurve3(std::vector<Vector3> points_, bool closed_ = false, CurveType curveType_ = Centripetal, double tension_ = 0.5)
      : points(std::move(points_)), closed(closed_), curveType(curveType_), tension(tension_) {}
  /** with the type by its three.js name */
  CatmullRomCurve3(std::vector<Vector3> points_, bool closed_, const std::string& curveType_, double tension_ = 0.5)
      : points(std::move(points_)), closed(closed_), curveType(curveType_ == "chordal" ? Chordal : curveType_ == "catmullrom" ? CatmullRom : Centripetal), tension(tension_) {}
  CatmullRomCurve3(std::vector<Vector3> points_, bool closed_, const char* curveType_, double tension_ = 0.5)
      : CatmullRomCurve3(std::move(points_), closed_, std::string(curveType_), tension_) {}
  Vector3 getPoint(double t) const override;
  std::shared_ptr<Curve<Vector3>> clone() const override { return std::make_shared<CatmullRomCurve3>(*this); }
  bool isClosedCatmullRom() const override { return closed; }
};

// ---------------------------------------------------------------------------
// CurvePath, Path, Shape
// ---------------------------------------------------------------------------

/** curves one after the other */
template <class V>
class CurvePath : public Curve<V> {
 public:
  std::vector<std::shared_ptr<Curve<V>>> curves;
  /** the points end where they began */
  bool autoClose = false;

  CurvePath() = default;
  /** (a copy has curves of its own) */
  CurvePath(const CurvePath& source);
  CurvePath& operator=(const CurvePath& source);

  void add(std::shared_ptr<Curve<V>> curve) { curves.push_back(std::move(curve)); }
  /** a line back to the start, if the path does not end there */
  CurvePath& closePath();
  V getPoint(double t) const override;
  double getLength() const override;
  void updateArcLengths() override;
  /** the length up to the end of each curve */
  const std::vector<double>& getCurveLengths() const;
  using Curve<V>::getPoints;
  using Curve<V>::getSpacedPoints;
  std::vector<V> getSpacedPoints(int divisions) const override;
  /** the points of every curve: `divisions` to a curve (twice as many to an ellipse, one line to a line), none twice in a row */
  std::vector<V> getPoints(int divisions) const override;
  std::shared_ptr<Curve<V>> clone() const override { return std::make_shared<CurvePath<V>>(*this); }

 protected:
  int defaultDivisions() const override { return 12; }
  int defaultSpacedDivisions() const override { return 40; }
  mutable std::vector<double> cacheLengths;
};

extern template class CurvePath<Vector2>;
extern template class CurvePath<Vector3>;

/** a path in the plane, drawn as on a 2D canvas */
class Path : public CurvePath<Vector2> {
 public:
  Vector2 currentPoint;

  Path() = default;
  explicit Path(const std::vector<Vector2>& points) { setFromPoints(points); }

  Path& setFromPoints(const std::vector<Vector2>& points);
  Path& moveTo(double x, double y);
  Path& lineTo(double x, double y);
  Path& quadraticCurveTo(double aCPx, double aCPy, double aX, double aY);
  Path& bezierCurveTo(double aCP1x, double aCP1y, double aCP2x, double aCP2y, double aX, double aY);
  Path& splineThru(const std::vector<Vector2>& pts);
  /** an arc about a centre given from the current point */
  Path& arc(double aX, double aY, double aRadius, double aStartAngle, double aEndAngle, bool aClockwise = false);
  /** an arc about a centre given as it is */
  Path& absarc(double aX, double aY, double aRadius, double aStartAngle, double aEndAngle, bool aClockwise = false);
  Path& ellipse(double aX, double aY, double xRadius, double yRadius, double aStartAngle, double aEndAngle, bool aClockwise = false, double aRotation = 0);
  Path& absellipse(double aX, double aY, double xRadius, double yRadius, double aStartAngle, double aEndAngle, bool aClockwise = false, double aRotation = 0);
  Path& closePath() {
    CurvePath<Vector2>::closePath();
    return *this;
  }
  std::shared_ptr<Curve<Vector2>> clone() const override { return std::make_shared<Path>(*this); }
};

/** what Shape.extractPoints answers */
struct ShapePoints {
  std::vector<Vector2> shape;
  std::vector<std::vector<Vector2>> holes;
};

/** an outline with holes in it (ShapeGeometry, ExtrudeGeometry) */
class Shape : public Path {
 public:
  std::vector<std::shared_ptr<Path>> holes;

  Shape() = default;
  explicit Shape(const std::vector<Vector2>& points) : Path(points) {}
  /** (a copy has holes of its own) */
  Shape(const Shape& source);
  Shape& operator=(const Shape& source);

  std::vector<std::vector<Vector2>> getPointsHoles(int divisions) const;
  /** the points of the outline and of the holes */
  ShapePoints extractPoints(int divisions) const;
  std::shared_ptr<Curve<Vector2>> clone() const override { return std::make_shared<Shape>(*this); }
};

// ---------------------------------------------------------------------------
// ShapeUtils, Earcut
// ---------------------------------------------------------------------------

namespace ShapeUtils {
/** the signed area of a contour (above 0: counterclockwise) */
double area(const std::vector<Vector2>& contour);
bool isClockWise(const std::vector<Vector2>& pts);
/**
 * Triangles of a contour with holes: each three indices into the points of the contour followed by
 * those of the holes.  As in three.js a last point that repeats the first is taken off the contour
 * and off each hole (the arguments are changed).
 */
std::vector<std::array<int, 3>> triangulateShape(std::vector<Vector2>& contour, std::vector<std::vector<Vector2>>& holes);
std::vector<std::array<int, 3>> triangulateShape(std::vector<Vector2>& contour);
}  // namespace ShapeUtils

namespace Earcut {
/** earcut: the triangles (three indices each) of a polygon given as x0, y0, x1, y1 …, its holes beginning at holeIndices */
std::vector<int> triangulate(const std::vector<double>& data, const std::vector<int>& holeIndices = {}, int dim = 2);
}  // namespace Earcut

}  // namespace fs
