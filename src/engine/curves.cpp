// SPDX-License-Identifier: AGPL-3.0-only
// Origin: three.js r187 src/extras/core/{Curve,CurvePath,Path,Shape,Interpolations}.js, src/extras/curves/*.js,
//         src/extras/ShapeUtils.js, src/extras/Earcut.js, src/extras/lib/earcut.js (earcut 3)
#include "engine/curves.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>

namespace fs {

// ---------------------------------------------------------------------------
// Curve
// ---------------------------------------------------------------------------

// Get point at relative position in curve according to arc length
// - u [0 .. 1]
template <class V>
V Curve<V>::getPointAt(double u) const {
  const double t = getUtoTmapping(u);
  return getPoint(t);
}

// Get sequence of points using getPoint( t )
template <class V>
std::vector<V> Curve<V>::getPoints(int divisions) const {
  std::vector<V> points;
  for (int d = 0; d <= divisions; d++) points.push_back(getPoint(static_cast<double>(d) / divisions));
  return points;
}

// Get sequence of points using getPointAt( u )
template <class V>
std::vector<V> Curve<V>::getSpacedPoints(int divisions) const {
  std::vector<V> points;
  for (int d = 0; d <= divisions; d++) points.push_back(getPointAt(static_cast<double>(d) / divisions));
  return points;
}

// Get total curve arc length
template <class V>
double Curve<V>::getLength() const {
  const std::vector<double>& lengths = getLengths();
  return lengths[lengths.size() - 1];
}

// Get list of cumulative segment lengths
template <class V>
const std::vector<double>& Curve<V>::getLengths(int divisions) const {
  if (!cacheArcLengths.empty() && (cacheArcLengths.size() == static_cast<size_t>(divisions) + 1) && !needsUpdate) return cacheArcLengths;
  const_cast<Curve<V>*>(this)->needsUpdate = false;
  std::vector<double> cache;
  V current, last = getPoint(0);
  double sum = 0;
  cache.push_back(0);
  for (int p = 1; p <= divisions; p++) {
    current = getPoint(static_cast<double>(p) / divisions);
    sum += current.distanceTo(last);
    cache.push_back(sum);
    last = current;
  }
  cacheArcLengths = std::move(cache);
  return cacheArcLengths;   // { sums: cache, sum: sum }; Sum is in the last element.
}

template <class V>
void Curve<V>::updateArcLengths() {
  needsUpdate = true;
  getLengths();
}

// Given u ( 0 .. 1 ), get a t to find p. This gives you points which are equidistant
template <class V>
double Curve<V>::getUtoTmapping(double u, double distance) const {
  const std::vector<double>& arcLengths = getLengths();
  long i = 0;
  const long il = static_cast<long>(arcLengths.size());
  double targetArcLength;   // The targeted u distance value to get
  if (distance != 0 && distance == distance) {
    targetArcLength = distance;
  } else {
    targetArcLength = u * arcLengths[static_cast<size_t>(il - 1)];
  }
  // binary search for the index with largest value smaller than target u distance
  long low = 0, high = il - 1;
  double comparison;
  while (low <= high) {
    i = static_cast<long>(std::floor(low + (high - low) / 2.0));   // less likely to overflow, though probably not issue here, JS doesn't really have integers, all numbers are floats
    comparison = arcLengths[static_cast<size_t>(i)] - targetArcLength;
    if (comparison < 0) {
      low = i + 1;
    } else if (comparison > 0) {
      high = i - 1;
    } else {
      high = i;
      break;
      // DONE
    }
  }
  i = high;
  if (i < 0) return JS_NAN;   // (a target before the curve's start: JavaScript reads arcLengths[-1], which is not a number)
  if (arcLengths[static_cast<size_t>(i)] == targetArcLength) return static_cast<double>(i) / (il - 1);
  // we could get finer grain at lengths, or use simple interpolation between two points
  const double lengthBefore = arcLengths[static_cast<size_t>(i)];
  const double lengthAfter = static_cast<size_t>(i + 1) < arcLengths.size() ? arcLengths[static_cast<size_t>(i + 1)] : JS_NAN;
  const double segmentLength = lengthAfter - lengthBefore;
  // determine where we are between the 'before' and 'after' points
  const double segmentFraction = (targetArcLength - lengthBefore) / segmentLength;
  // add that fractional amount to t
  const double t = (i + segmentFraction) / (il - 1);
  return t;
}

// Returns a unit vector tangent at t
// In case any sub curve does not implement its tangent derivation,
// 2 points a small delta apart will be used to find its gradient
// which seems to give a reasonable approximation
template <class V>
V Curve<V>::getTangent(double t) const {
  const double delta = 0.0001;
  double t1 = t - delta;
  double t2 = t + delta;
  // Capping in case of danger
  if (t1 < 0) t1 = 0;
  if (t2 > 1) t2 = 1;
  const V pt1 = getPoint(t1);
  const V pt2 = getPoint(t2);
  V tangent;
  tangent.copy(pt2).sub(pt1).normalize();
  return tangent;
}

template <class V>
V Curve<V>::getTangentAt(double u) const {
  const double t = getUtoTmapping(u);
  return getTangent(t);
}

// see http://www.cs.indiana.edu/pub/techreports/TR425.pdf
template <class V>
FrenetFrames Curve<V>::computeFrenetFrames(int segments, bool closed) const
  requires std::same_as<V, Vector3>
{
  Vector3 normal;
  FrenetFrames frames;
  std::vector<Vector3>& tangents = frames.tangents;
  std::vector<Vector3>& normals = frames.normals;
  std::vector<Vector3>& binormals = frames.binormals;
  const size_t n = static_cast<size_t>(segments);
  tangents.resize(n + 1);
  normals.resize(n + 1);
  binormals.resize(n + 1);
  Vector3 vec;
  Matrix4 mat;
  // compute the tangent vectors for each segment on the curve
  for (size_t i = 0; i <= n; i++) {
    const double u = static_cast<double>(i) / segments;
    tangents[i] = getTangentAt(u);
  }
  // select an initial normal vector perpendicular to the first tangent vector,
  // and in the direction of the minimum tangent xyz component
  double min = std::numeric_limits<double>::max();
  const double tx = std::fabs(tangents[0].x);
  const double ty = std::fabs(tangents[0].y);
  const double tz = std::fabs(tangents[0].z);
  if (tx <= min) {
    min = tx;
    normal.set(1, 0, 0);
  }
  if (ty <= min) {
    min = ty;
    normal.set(0, 1, 0);
  }
  if (tz <= min) {
    normal.set(0, 0, 1);
  }
  vec.crossVectors(tangents[0], normal).normalize();
  normals[0].crossVectors(tangents[0], vec);
  binormals[0].crossVectors(tangents[0], normals[0]);
  // compute the slowly-varying normal and binormal vectors for each segment on the curve
  for (size_t i = 1; i <= n; i++) {
    normals[i] = normals[i - 1].clone();
    binormals[i] = binormals[i - 1].clone();
    vec.crossVectors(tangents[i - 1], tangents[i]);
    if (vec.length() > std::numeric_limits<double>::epsilon()) {
      vec.normalize();
      const double theta = std::acos(MathUtils::clamp(tangents[i - 1].dot(tangents[i]), -1, 1));   // clamp for floating pt errors
      normals[i].applyMatrix4(mat.makeRotationAxis(vec, theta));
    }
    binormals[i].crossVectors(tangents[i], normals[i]);
  }
  // if the curve is closed, postprocess the vectors so the first and last normal vectors are the same
  if (closed == true) {
    double theta = std::acos(MathUtils::clamp(normals[0].dot(normals[n]), -1, 1));
    theta /= segments;
    if (tangents[0].dot(vec.crossVectors(normals[0], normals[n])) > 0) theta = -theta;
    for (size_t i = 1; i <= n; i++) {
      // twist a little...
      normals[i].applyMatrix4(mat.makeRotationAxis(tangents[i], theta * static_cast<double>(i)));
      binormals[i].crossVectors(tangents[i], normals[i]);
    }
  }
  return frames;
}

template class Curve<Vector2>;
template class Curve<Vector3>;

// ---------------------------------------------------------------------------
// Interpolations
// ---------------------------------------------------------------------------

namespace {

double CatmullRom(double t, double p0, double p1, double p2, double p3) {
  const double v0 = (p2 - p0) * 0.5;
  const double v1 = (p3 - p1) * 0.5;
  const double t2 = t * t;
  const double t3 = t * t2;
  return (2 * p1 - 2 * p2 + v0 + v1) * t3 + (-3 * p1 + 3 * p2 - 2 * v0 - v1) * t2 + v0 * t + p1;
}

double QuadraticBezierP0(double t, double p) {
  const double k = 1 - t;
  return k * k * p;
}
double QuadraticBezierP1(double t, double p) { return 2 * (1 - t) * t * p; }
double QuadraticBezierP2(double t, double p) { return t * t * p; }
double QuadraticBezier(double t, double p0, double p1, double p2) { return QuadraticBezierP0(t, p0) + QuadraticBezierP1(t, p1) + QuadraticBezierP2(t, p2); }

double CubicBezierP0(double t, double p) {
  const double k = 1 - t;
  return k * k * k * p;
}
double CubicBezierP1(double t, double p) {
  const double k = 1 - t;
  return 3 * k * k * t * p;
}
double CubicBezierP2(double t, double p) { return 3 * (1 - t) * t * t * p; }
double CubicBezierP3(double t, double p) { return t * t * t * p; }
double CubicBezier(double t, double p0, double p1, double p2, double p3) {
  return CubicBezierP0(t, p0) + CubicBezierP1(t, p1) + CubicBezierP2(t, p2) + CubicBezierP3(t, p3);
}

/**
 * Centripetal CatmullRom Curve - which is useful for avoiding
 * cusps and self-intersections in non-uniform catmull rom curves.
 * http://www.cemyuksel.com/research/catmullrom_param/catmullrom.pdf
 *
 * curve.type accepts centripetal(default), chordal and catmullrom
 * curve.tension is used for catmullrom which defaults to 0.5
 *
 * Based on an optimized c++ solution in
 * - http://stackoverflow.com/questions/9489736/catmull-rom-curve-with-no-cusps-and-no-self-intersections/
 * - http://ideone.com/NoEbVM
 *
 * This CubicPoly class could be used for reusing some variables and calculations,
 * but for three.js curve use, it could be possible inlined and flatten into a single function call
 * which can be placed in CurveUtils.
 */
struct CubicPoly {
  double c0 = 0, c1 = 0, c2 = 0, c3 = 0;
  /*
   * Compute coefficients for a cubic polynomial
   *   p(s) = c0 + c1*s + c2*s^2 + c3*s^3
   * such that
   *   p(0) = x0, p(1) = x1
   *  and
   *   p'(0) = t0, p'(1) = t1.
   */
  void init(double x0, double x1, double t0, double t1) {
    c0 = x0;
    c1 = t0;
    c2 = -3 * x0 + 3 * x1 - 2 * t0 - t1;
    c3 = 2 * x0 - 2 * x1 + t0 + t1;
  }
  void initCatmullRom(double x0, double x1, double x2, double x3, double tension) { init(x1, x2, tension * (x2 - x0), tension * (x3 - x1)); }
  void initNonuniformCatmullRom(double x0, double x1, double x2, double x3, double dt0, double dt1, double dt2) {
    // compute tangents when parameterized in [t1,t2]
    double t1 = (x1 - x0) / dt0 - (x2 - x0) / (dt0 + dt1) + (x2 - x1) / dt1;
    double t2 = (x2 - x1) / dt1 - (x3 - x1) / (dt1 + dt2) + (x3 - x2) / dt2;
    // rescale tangents for parametrization in [0,1]
    t1 *= dt1;
    t2 *= dt1;
    init(x1, x2, t1, t2);
  }
  double calc(double t) const {
    const double t2 = t * t;
    const double t3 = t2 * t;
    return c0 + c1 * t + c2 * t2 + c3 * t3;
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// the curves
// ---------------------------------------------------------------------------

Vector2 LineCurve::getPoint(double t) const {
  Vector2 point;
  if (t == 1) {
    point.copy(v2);
  } else {
    point.copy(v2).sub(v1);
    point.multiplyScalar(t).add(v1);
  }
  return point;
}

Vector2 LineCurve::getTangent(double) const {
  Vector2 tangent;
  return tangent.subVectors(v2, v1).normalize();
}

Vector3 LineCurve3::getPoint(double t) const {
  Vector3 point;
  if (t == 1) {
    point.copy(v2);
  } else {
    point.copy(v2).sub(v1);
    point.multiplyScalar(t).add(v1);
  }
  return point;
}

Vector3 LineCurve3::getTangent(double) const {
  Vector3 tangent;
  return tangent.subVectors(v2, v1).normalize();
}

Vector2 QuadraticBezierCurve::getPoint(double t) const {
  const double x = QuadraticBezier(t, v0.x, v1.x, v2.x);
  const double y = QuadraticBezier(t, v0.y, v1.y, v2.y);
  return Vector2(x, y);
}

Vector3 QuadraticBezierCurve3::getPoint(double t) const {
  const double x = QuadraticBezier(t, v0.x, v1.x, v2.x);
  const double y = QuadraticBezier(t, v0.y, v1.y, v2.y);
  const double z = QuadraticBezier(t, v0.z, v1.z, v2.z);
  return Vector3(x, y, z);
}

Vector2 CubicBezierCurve::getPoint(double t) const {
  const double x = CubicBezier(t, v0.x, v1.x, v2.x, v3.x);
  const double y = CubicBezier(t, v0.y, v1.y, v2.y, v3.y);
  return Vector2(x, y);
}

Vector3 CubicBezierCurve3::getPoint(double t) const {
  const double x = CubicBezier(t, v0.x, v1.x, v2.x, v3.x);
  const double y = CubicBezier(t, v0.y, v1.y, v2.y, v3.y);
  const double z = CubicBezier(t, v0.z, v1.z, v2.z, v3.z);
  return Vector3(x, y, z);
}

Vector2 EllipseCurve::getPoint(double t) const {
  const double EPSILON = std::numeric_limits<double>::epsilon();
  const double twoPi = PI * 2;
  double deltaAngle = aEndAngle - aStartAngle;
  const bool samePoints = std::fabs(deltaAngle) < EPSILON;
  // ensures that deltaAngle is 0 .. 2 PI
  while (deltaAngle < 0) deltaAngle += twoPi;
  while (deltaAngle > twoPi) deltaAngle -= twoPi;
  if (deltaAngle < EPSILON) {
    if (samePoints) {
      deltaAngle = 0;
    } else {
      deltaAngle = twoPi;
    }
  }
  if (aClockwise == true && !samePoints) {
    if (deltaAngle == twoPi) {
      deltaAngle = -twoPi;
    } else {
      deltaAngle = deltaAngle - twoPi;
    }
  }
  const double angle = aStartAngle + t * deltaAngle;
  double x = aX + xRadius * std::cos(angle);
  double y = aY + yRadius * std::sin(angle);
  if (aRotation != 0) {
    const double cos = std::cos(aRotation);
    const double sin = std::sin(aRotation);
    const double tx = x - aX;
    const double ty = y - aY;
    // Rotate the point about the center of the ellipse.
    x = tx * cos - ty * sin + aX;
    y = tx * sin + ty * cos + aY;
  }
  return Vector2(x, y);
}

Vector2 SplineCurve::getPoint(double t) const {
  const long n = static_cast<long>(points.size());
  const double p = (n - 1) * t;
  // (no points, or a t that is no place on the curve: JavaScript fails here – not a point)
  if (n == 0 || !(p >= 0 && p <= static_cast<double>(n - 1))) return Vector2(JS_NAN, JS_NAN);
  const long intPoint = static_cast<long>(std::floor(p));
  const double weight = p - intPoint;
  const Vector2& p0 = points[static_cast<size_t>(intPoint == 0 ? intPoint : intPoint - 1)];
  const Vector2& p1 = points[static_cast<size_t>(intPoint)];
  const Vector2& p2 = points[static_cast<size_t>(intPoint > n - 2 ? n - 1 : intPoint + 1)];
  const Vector2& p3 = points[static_cast<size_t>(intPoint > n - 3 ? n - 1 : intPoint + 2)];
  const double x = CatmullRom(weight, p0.x, p1.x, p2.x, p3.x);
  const double y = CatmullRom(weight, p0.y, p1.y, p2.y, p3.y);
  return Vector2(x, y);
}

Vector3 CatmullRomCurve3::getPoint(double t) const {
  const long l = static_cast<long>(points.size());
  const double p = (l - (closed ? 0 : 1)) * t;
  // (fewer than two points, or a t that is no place on the curve: JavaScript fails here – not a point)
  if (l < 2 || !(std::fabs(p) < 1e15) || (!closed && p < 0)) return Vector3(JS_NAN, JS_NAN, JS_NAN);
  long intPoint = static_cast<long>(std::floor(p));
  double weight = p - intPoint;
  if (closed) {
    intPoint += intPoint > 0 ? 0 : (std::labs(intPoint) / l + 1) * l;
  } else if (weight == 0 && intPoint == l - 1) {
    intPoint = l - 2;
    weight = 1;
  }
  Vector3 p0, p3;   // 4 points (p1 & p2 defined below)
  if (closed || intPoint > 0) {
    p0 = points[static_cast<size_t>((intPoint - 1) % l)];
  } else {
    // extrapolate first point
    p0.subVectors(points[0], points[1]).add(points[0]);
  }
  const Vector3& p1 = points[static_cast<size_t>(intPoint % l)];
  const Vector3& p2 = points[static_cast<size_t>((intPoint + 1) % l)];
  if (closed || intPoint + 2 < l) {
    p3 = points[static_cast<size_t>((intPoint + 2) % l)];
  } else {
    // extrapolate last point
    p3.subVectors(points[static_cast<size_t>(l - 1)], points[static_cast<size_t>(l - 2)]).add(points[static_cast<size_t>(l - 1)]);
  }
  CubicPoly px, py, pz;
  if (curveType == Centripetal || curveType == Chordal) {
    // init Centripetal / Chordal Catmull-Rom
    const double pow = curveType == Chordal ? 0.5 : 0.25;
    double dt0 = std::pow(p0.distanceToSquared(p1), pow);
    double dt1 = std::pow(p1.distanceToSquared(p2), pow);
    double dt2 = std::pow(p2.distanceToSquared(p3), pow);
    // safety check for repeated points
    if (dt1 < 1e-4) dt1 = 1.0;
    if (dt0 < 1e-4) dt0 = dt1;
    if (dt2 < 1e-4) dt2 = dt1;
    px.initNonuniformCatmullRom(p0.x, p1.x, p2.x, p3.x, dt0, dt1, dt2);
    py.initNonuniformCatmullRom(p0.y, p1.y, p2.y, p3.y, dt0, dt1, dt2);
    pz.initNonuniformCatmullRom(p0.z, p1.z, p2.z, p3.z, dt0, dt1, dt2);
  } else if (curveType == CatmullRom) {
    px.initCatmullRom(p0.x, p1.x, p2.x, p3.x, tension);
    py.initCatmullRom(p0.y, p1.y, p2.y, p3.y, tension);
    pz.initCatmullRom(p0.z, p1.z, p2.z, p3.z, tension);
  }
  const double x = px.calc(weight);
  const double y = py.calc(weight);
  const double z = pz.calc(weight);
  return Vector3(x, y, z);
}

// ---------------------------------------------------------------------------
// CurvePath
// ---------------------------------------------------------------------------

template <class V>
CurvePath<V>::CurvePath(const CurvePath& source) : Curve<V>(source), autoClose(source.autoClose) {
  for (const auto& curve : source.curves) curves.push_back(curve->clone());
}

template <class V>
CurvePath<V>& CurvePath<V>::operator=(const CurvePath& source) {
  if (this == &source) return *this;
  Curve<V>::operator=(source);
  std::vector<std::shared_ptr<Curve<V>>> copies;
  for (const auto& curve : source.curves) copies.push_back(curve->clone());
  curves = std::move(copies);
  autoClose = source.autoClose;
  cacheLengths.clear();
  return *this;
}

template <class V>
CurvePath<V>& CurvePath<V>::closePath() {
  if (curves.empty()) return *this;
  // Add a line curve if start and end of lines are not connected
  const V startPoint = curves[0]->getPoint(0);
  const V endPoint = curves[curves.size() - 1]->getPoint(1);
  if (!startPoint.equals(endPoint)) {
    if constexpr (std::same_as<V, Vector2>) {
      curves.push_back(std::make_shared<LineCurve>(endPoint, startPoint));
    } else {
      curves.push_back(std::make_shared<LineCurve3>(endPoint, startPoint));
    }
  }
  return *this;
}

// To get accurate point with reference to
// entire path distance at time t,
// following has to be done:
// 1. Length of each sub path have to be known
// 2. Locate and identify type of curve
// 3. Get t for the curve
// 4. Return curve.getPointAt(t')
template <class V>
V CurvePath<V>::getPoint(double t) const {
  const double d = t * getLength();
  const std::vector<double>& curveLengths = getCurveLengths();
  size_t i = 0;
  // To think about boundaries points.
  while (i < curveLengths.size()) {
    if (curveLengths[i] >= d) {
      const double diff = curveLengths[i] - d;
      const Curve<V>& curve = *curves[i];
      const double segmentLength = curve.getLength();
      const double u = segmentLength == 0 ? 0 : 1 - diff / segmentLength;
      return curve.getPointAt(u);
    }
    i++;
  }
  // (JavaScript answers null: there is no such point – a path without curves, or a t beyond 1)
  return V();
}

// We cannot use the default THREE.Curve getPoint() with getLength() because in
// THREE.Curve, getLength() depends on getPoint() but in THREE.CurvePath
// getPoint() depends on getLength
template <class V>
double CurvePath<V>::getLength() const {
  const std::vector<double>& lens = getCurveLengths();
  return lens.empty() ? JS_NAN : lens[lens.size() - 1];
}

// cacheLengths must be recalculated.
template <class V>
void CurvePath<V>::updateArcLengths() {
  this->needsUpdate = true;
  cacheLengths.clear();
  getCurveLengths();
}

// Compute lengths and cache them
// We cannot overwrite getLengths() because UtoT mapping uses it.
template <class V>
const std::vector<double>& CurvePath<V>::getCurveLengths() const {
  // We use cache values if curves and cache array are same length
  if (!cacheLengths.empty() && cacheLengths.size() == curves.size()) return cacheLengths;
  // Get length of sub-curve
  // Push sums into cached array
  std::vector<double> lengths;
  double sums = 0;
  for (size_t i = 0, l = curves.size(); i < l; i++) {
    sums += curves[i]->getLength();
    lengths.push_back(sums);
  }
  cacheLengths = std::move(lengths);
  return cacheLengths;
}

template <class V>
std::vector<V> CurvePath<V>::getSpacedPoints(int divisions) const {
  std::vector<V> points;
  for (int i = 0; i <= divisions; i++) points.push_back(getPoint(static_cast<double>(i) / divisions));
  if (autoClose) points.push_back(points[0]);
  return points;
}

template <class V>
std::vector<V> CurvePath<V>::getPoints(int divisions) const {
  std::vector<V> points;
  bool hasLast = false;
  V last;
  for (size_t i = 0; i < curves.size(); i++) {
    const Curve<V>& curve = *curves[i];
    const int resolution = curve.isEllipseCurve() ? divisions * 2
        : curve.isLineCurve() ? 1
        : curve.splinePointCount() >= 0 ? divisions * curve.splinePointCount()
        : divisions;
    const std::vector<V> pts = curve.getPoints(resolution);
    for (size_t j = 0; j < pts.size(); j++) {
      const V& point = pts[j];
      if (hasLast && last.equals(point)) continue;   // ensures no consecutive points are duplicates
      points.push_back(point);
      last = point;
      hasLast = true;
    }
  }
  if (autoClose && points.size() > 1 && !points[points.size() - 1].equals(points[0])) points.push_back(points[0]);
  return points;
}

template class CurvePath<Vector2>;
template class CurvePath<Vector3>;

// ---------------------------------------------------------------------------
// Path
// ---------------------------------------------------------------------------

Path& Path::setFromPoints(const std::vector<Vector2>& points) {
  if (points.empty()) return *this;
  moveTo(points[0].x, points[0].y);
  for (size_t i = 1, l = points.size(); i < l; i++) lineTo(points[i].x, points[i].y);
  return *this;
}

Path& Path::moveTo(double x, double y) {
  currentPoint.set(x, y);
  return *this;
}

Path& Path::lineTo(double x, double y) {
  curves.push_back(std::make_shared<LineCurve>(currentPoint.clone(), Vector2(x, y)));
  currentPoint.set(x, y);
  return *this;
}

Path& Path::quadraticCurveTo(double aCPx, double aCPy, double aX, double aY) {
  curves.push_back(std::make_shared<QuadraticBezierCurve>(currentPoint.clone(), Vector2(aCPx, aCPy), Vector2(aX, aY)));
  currentPoint.set(aX, aY);
  return *this;
}

Path& Path::bezierCurveTo(double aCP1x, double aCP1y, double aCP2x, double aCP2y, double aX, double aY) {
  curves.push_back(std::make_shared<CubicBezierCurve>(currentPoint.clone(), Vector2(aCP1x, aCP1y), Vector2(aCP2x, aCP2y), Vector2(aX, aY)));
  currentPoint.set(aX, aY);
  return *this;
}

Path& Path::splineThru(const std::vector<Vector2>& pts) {
  if (pts.empty()) return *this;
  std::vector<Vector2> npts;
  npts.push_back(currentPoint.clone());
  npts.insert(npts.end(), pts.begin(), pts.end());
  curves.push_back(std::make_shared<SplineCurve>(std::move(npts)));
  currentPoint.copy(pts[pts.size() - 1]);
  return *this;
}

Path& Path::arc(double aX, double aY, double aRadius, double aStartAngle, double aEndAngle, bool aClockwise) {
  const double x0 = currentPoint.x;
  const double y0 = currentPoint.y;
  absarc(aX + x0, aY + y0, aRadius, aStartAngle, aEndAngle, aClockwise);
  return *this;
}

Path& Path::absarc(double aX, double aY, double aRadius, double aStartAngle, double aEndAngle, bool aClockwise) {
  absellipse(aX, aY, aRadius, aRadius, aStartAngle, aEndAngle, aClockwise);
  return *this;
}

Path& Path::ellipse(double aX, double aY, double xRadius, double yRadius, double aStartAngle, double aEndAngle, bool aClockwise, double aRotation) {
  const double x0 = currentPoint.x;
  const double y0 = currentPoint.y;
  absellipse(aX + x0, aY + y0, xRadius, yRadius, aStartAngle, aEndAngle, aClockwise, aRotation);
  return *this;
}

Path& Path::absellipse(double aX, double aY, double xRadius, double yRadius, double aStartAngle, double aEndAngle, bool aClockwise, double aRotation) {
  auto curve = std::make_shared<EllipseCurve>(aX, aY, xRadius, yRadius, aStartAngle, aEndAngle, aClockwise, aRotation);
  if (curves.size() > 0) {
    // if a previous curve is present, attempt to join
    const Vector2 firstPoint = curve->getPoint(0);
    if (!firstPoint.equals(currentPoint)) lineTo(firstPoint.x, firstPoint.y);
  }
  curves.push_back(curve);
  const Vector2 lastPoint = curve->getPoint(1);
  currentPoint.copy(lastPoint);
  return *this;
}

// ---------------------------------------------------------------------------
// Shape
// ---------------------------------------------------------------------------

Shape::Shape(const Shape& source) : Path(source) {
  for (const auto& hole : source.holes) holes.push_back(std::make_shared<Path>(*hole));
}

Shape& Shape::operator=(const Shape& source) {
  if (this == &source) return *this;
  Path::operator=(source);
  std::vector<std::shared_ptr<Path>> copies;
  for (const auto& hole : source.holes) copies.push_back(std::make_shared<Path>(*hole));
  holes = std::move(copies);
  return *this;
}

std::vector<std::vector<Vector2>> Shape::getPointsHoles(int divisions) const {
  std::vector<std::vector<Vector2>> holesPts;
  for (size_t i = 0, l = holes.size(); i < l; i++) holesPts.push_back(holes[i]->getPoints(divisions));
  return holesPts;
}

// get points of shape and holes (keypoints based on segments parameter)
ShapePoints Shape::extractPoints(int divisions) const {
  ShapePoints points;
  points.shape = getPoints(divisions);
  points.holes = getPointsHoles(divisions);
  return points;
}

// ---------------------------------------------------------------------------
// earcut
// ---------------------------------------------------------------------------

namespace {

struct Node {
  int i;          // vertex index in coordinates array
  double x, y;    // vertex coordinates
  Node* prev = nullptr;   // previous and next vertex nodes in a polygon ring
  Node* next = nullptr;
  int32_t z = 0;   // z-order curve value
  Node* prevZ = nullptr;   // previous and next nodes in z-order
  Node* nextZ = nullptr;
  bool steiner = false;   // indicates whether this is a steiner point
};

/** one triangulation: the nodes live as long as it does */
struct EarcutRun {
  std::deque<Node> nodes;
  std::vector<int> triangles;

  Node* createNode(int i, double x, double y) {
    nodes.push_back(Node{i, x, y});
    return &nodes.back();
  }

  // create a node and optionally link it with previous one (in a circular doubly linked list)
  Node* insertNode(int i, double x, double y, Node* last) {
    Node* const p = createNode(i, x, y);
    if (!last) {
      p->prev = p;
      p->next = p;
    } else {
      p->next = last->next;
      p->prev = last;
      last->next->prev = p;
      last->next = p;
    }
    return p;
  }

  static void removeNode(Node* p) {
    p->next->prev = p->prev;
    p->prev->next = p->next;
    if (p->prevZ) p->prevZ->nextZ = p->nextZ;
    if (p->nextZ) p->nextZ->prevZ = p->prevZ;
  }

  static double signedArea(const std::vector<double>& data, long start, long end, long dim) {
    double sum = 0;
    for (long i = start, j = end - dim; i < end; i += dim) {
      sum += (data[static_cast<size_t>(j)] - data[static_cast<size_t>(i)]) * (data[static_cast<size_t>(i + 1)] + data[static_cast<size_t>(j + 1)]);
      j = i;
    }
    return sum;
  }

  // signed area of a triangle
  static double area(const Node* p, const Node* q, const Node* r) { return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y); }

  // check if two points are equal
  static bool equals(const Node* p1, const Node* p2) { return p1->x == p2->x && p1->y == p2->y; }

  static int sign(double num) { return num > 0 ? 1 : num < 0 ? -1 : 0; }

  // for collinear points p, q, r, check if point q lies on segment pr
  static bool onSegment(const Node* p, const Node* q, const Node* r) {
    return q->x <= jsMax(p->x, r->x) && q->x >= jsMin(p->x, r->x) && q->y <= jsMax(p->y, r->y) && q->y >= jsMin(p->y, r->y);
  }

  // check if two segments intersect
  static bool intersects(const Node* p1, const Node* q1, const Node* p2, const Node* q2) {
    const int o1 = sign(area(p1, q1, p2));
    const int o2 = sign(area(p1, q1, q2));
    const int o3 = sign(area(p2, q2, p1));
    const int o4 = sign(area(p2, q2, q1));
    if (o1 != o2 && o3 != o4) return true;   // general case
    if (o1 == 0 && onSegment(p1, p2, q1)) return true;   // p1, q1 and p2 are collinear and p2 lies on p1q1
    if (o2 == 0 && onSegment(p1, q2, q1)) return true;   // p1, q1 and q2 are collinear and q2 lies on p1q1
    if (o3 == 0 && onSegment(p2, p1, q2)) return true;   // p2, q2 and p1 are collinear and p1 lies on p2q2
    if (o4 == 0 && onSegment(p2, q1, q2)) return true;   // p2, q2 and q1 are collinear and q1 lies on p2q2
    return false;
  }

  // check if a point lies within a convex triangle
  static bool pointInTriangle(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
    return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py) && (bx - px) * (cy - py) >= (cx - px) * (by - py);
  }

  // check if a point lies within a convex triangle but false if its equal to the first point of the triangle
  static bool pointInTriangleExceptFirst(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
    return !(ax == px && ay == py) && pointInTriangle(ax, ay, bx, by, cx, cy, px, py);
  }

  // check if a polygon diagonal is locally inside the polygon
  static bool locallyInside(const Node* a, const Node* b) {
    return area(a->prev, a, a->next) < 0 ? area(a, b, a->next) >= 0 && area(a, a->prev, b) >= 0 : area(a, b, a->prev) < 0 || area(a, a->next, b) < 0;
  }

  // check if the middle point of a polygon diagonal is inside the polygon
  static bool middleInside(const Node* a, const Node* b) {
    const Node* p = a;
    bool inside = false;
    const double px = (a->x + b->x) / 2;
    const double py = (a->y + b->y) / 2;
    do {
      if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y && (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x)) inside = !inside;
      p = p->next;
    } while (p != a);
    return inside;
  }

  // check if a polygon diagonal intersects any polygon segments
  static bool intersectsPolygon(const Node* a, const Node* b) {
    const Node* p = a;
    do {
      if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i && intersects(p, p->next, a, b)) return true;
      p = p->next;
    } while (p != a);
    return false;
  }

  // check if a diagonal between two polygon nodes is valid (lies in polygon interior)
  static bool isValidDiagonal(const Node* a, const Node* b) {
    return a->next->i != b->i && a->prev->i != b->i && !intersectsPolygon(a, b) &&   // doesn't intersect other edges
           ((locallyInside(a, b) && locallyInside(b, a) && middleInside(a, b) &&   // locally visible
             (area(a->prev, a, b->prev) != 0 || area(a, b->prev, b) != 0)) ||   // does not create opposite-facing sectors
            (equals(a, b) && area(a->prev, a, a->next) > 0 && area(b->prev, b, b->next) > 0));   // special zero-length case
  }

  // z-order of a point given coords and inverse of the longer side of data bbox
  static int32_t zOrder(double x_, double y_, double minX, double minY, double invSize) {
    // coords are transformed into non-negative 15-bit integer range
    int32_t x = jsInt32((x_ - minX) * invSize);
    int32_t y = jsInt32((y_ - minY) * invSize);
    const auto spread = [](int32_t v) {
      uint32_t u = static_cast<uint32_t>(v);
      u = (u | (u << 8)) & 0x00FF00FFu;
      u = (u | (u << 4)) & 0x0F0F0F0Fu;
      u = (u | (u << 2)) & 0x33333333u;
      u = (u | (u << 1)) & 0x55555555u;
      return u;
    };
    return static_cast<int32_t>(spread(x) | (spread(y) << 1));
  }

  // find the leftmost node of a polygon ring
  static Node* getLeftmost(Node* start) {
    Node* p = start;
    Node* leftmost = start;
    do {
      if (p->x < leftmost->x || (p->x == leftmost->x && p->y < leftmost->y)) leftmost = p;
      p = p->next;
    } while (p != start);
    return leftmost;
  }

  // whether sector in vertex m contains sector in vertex p in the same coordinates
  static bool sectorContainsSector(const Node* m, const Node* p) { return area(m->prev, m, p->prev) < 0 && area(p->next, m, m->next) < 0; }

  // create a circular doubly linked list from polygon points in the specified winding order
  Node* linkedList(const std::vector<double>& data, long start, long end, long dim, bool clockwise) {
    Node* last = nullptr;
    if (clockwise == (signedArea(data, start, end, dim) > 0)) {
      for (long i = start; i < end; i += dim) last = insertNode(static_cast<int>(i / dim), data[static_cast<size_t>(i)], data[static_cast<size_t>(i + 1)], last);
    } else {
      for (long i = end - dim; i >= start; i -= dim) last = insertNode(static_cast<int>(i / dim), data[static_cast<size_t>(i)], data[static_cast<size_t>(i + 1)], last);
    }
    if (last && equals(last, last->next)) {
      removeNode(last);
      last = last->next;
    }
    return last;
  }

  // eliminate colinear or duplicate points
  static Node* filterPoints(Node* start, Node* end = nullptr) {
    if (!start) return start;
    if (!end) end = start;
    Node* p = start;
    bool again;
    do {
      again = false;
      if (!p->steiner && (equals(p, p->next) || area(p->prev, p, p->next) == 0)) {
        removeNode(p);
        p = end = p->prev;
        if (p == p->next) break;
        again = true;
      } else {
        p = p->next;
      }
    } while (again || p != end);
    return end;
  }

  // check whether a polygon node forms a valid ear with adjacent nodes
  static bool isEar(const Node* ear) {
    const Node* a = ear->prev;
    const Node* b = ear;
    const Node* c = ear->next;
    if (area(a, b, c) >= 0) return false;   // reflex, can't be an ear
    // now make sure we don't have other points inside the potential ear
    const double ax = a->x, bx = b->x, cx = c->x, ay = a->y, by = b->y, cy = c->y;
    // triangle bbox
    const double x0 = mathdetail::min3(ax, bx, cx), y0 = mathdetail::min3(ay, by, cy), x1 = mathdetail::max3(ax, bx, cx), y1 = mathdetail::max3(ay, by, cy);
    const Node* p = c->next;
    while (p != a) {
      if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 && pointInTriangleExceptFirst(ax, ay, bx, by, cx, cy, p->x, p->y) && area(p->prev, p, p->next) >= 0) return false;
      p = p->next;
    }
    return true;
  }

  static bool isEarHashed(const Node* ear, double minX, double minY, double invSize) {
    const Node* a = ear->prev;
    const Node* b = ear;
    const Node* c = ear->next;
    if (area(a, b, c) >= 0) return false;   // reflex, can't be an ear
    const double ax = a->x, bx = b->x, cx = c->x, ay = a->y, by = b->y, cy = c->y;
    // triangle bbox
    const double x0 = mathdetail::min3(ax, bx, cx), y0 = mathdetail::min3(ay, by, cy), x1 = mathdetail::max3(ax, bx, cx), y1 = mathdetail::max3(ay, by, cy);
    // z-order range for the current triangle bbox;
    const int32_t minZ = zOrder(x0, y0, minX, minY, invSize), maxZ = zOrder(x1, y1, minX, minY, invSize);
    const Node* p = ear->prevZ;
    const Node* n = ear->nextZ;
    // look for points inside the triangle in both directions
    while (p && p->z >= minZ && n && n->z <= maxZ) {
      if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 && p != a && p != c && pointInTriangleExceptFirst(ax, ay, bx, by, cx, cy, p->x, p->y) && area(p->prev, p, p->next) >= 0)
        return false;
      p = p->prevZ;
      if (n->x >= x0 && n->x <= x1 && n->y >= y0 && n->y <= y1 && n != a && n != c && pointInTriangleExceptFirst(ax, ay, bx, by, cx, cy, n->x, n->y) && area(n->prev, n, n->next) >= 0)
        return false;
      n = n->nextZ;
    }
    // look for remaining points in decreasing z-order
    while (p && p->z >= minZ) {
      if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 && p != a && p != c && pointInTriangleExceptFirst(ax, ay, bx, by, cx, cy, p->x, p->y) && area(p->prev, p, p->next) >= 0)
        return false;
      p = p->prevZ;
    }
    // look for remaining points in increasing z-order
    while (n && n->z <= maxZ) {
      if (n->x >= x0 && n->x <= x1 && n->y >= y0 && n->y <= y1 && n != a && n != c && pointInTriangleExceptFirst(ax, ay, bx, by, cx, cy, n->x, n->y) && area(n->prev, n, n->next) >= 0)
        return false;
      n = n->nextZ;
    }
    return true;
  }

  // Simon Tatham's linked list merge sort algorithm
  // http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
  static Node* sortLinked(Node* list) {
    int numMerges;
    int inSize = 1;
    do {
      Node* p = list;
      Node* e;
      list = nullptr;
      Node* tail = nullptr;
      numMerges = 0;
      while (p) {
        numMerges++;
        Node* q = p;
        int pSize = 0;
        for (int i = 0; i < inSize; i++) {
          pSize++;
          q = q->nextZ;
          if (!q) break;
        }
        int qSize = inSize;
        while (pSize > 0 || (qSize > 0 && q)) {
          if (pSize != 0 && (qSize == 0 || !q || p->z <= q->z)) {
            e = p;
            p = p->nextZ;
            pSize--;
          } else {
            e = q;
            q = q->nextZ;
            qSize--;
          }
          if (tail) tail->nextZ = e;
          else list = e;
          e->prevZ = tail;
          tail = e;
        }
        p = q;
      }
      tail->nextZ = nullptr;
      inSize *= 2;
    } while (numMerges > 1);
    return list;
  }

  // interlink polygon nodes in z-order
  static void indexCurve(Node* start, double minX, double minY, double invSize) {
    Node* p = start;
    do {
      if (p->z == 0) p->z = zOrder(p->x, p->y, minX, minY, invSize);
      p->prevZ = p->prev;
      p->nextZ = p->next;
      p = p->next;
    } while (p != start);
    p->prevZ->nextZ = nullptr;
    p->prevZ = nullptr;
    sortLinked(p);
  }

  // link two polygon vertices with a bridge; if the vertices belong to the same ring, it splits polygon into two;
  // if one belongs to the outer ring and another to a hole, it merges it into a single ring
  Node* splitPolygon(Node* a, Node* b) {
    Node* const a2 = createNode(a->i, a->x, a->y);
    Node* const b2 = createNode(b->i, b->x, b->y);
    Node* const an = a->next;
    Node* const bp = b->prev;
    a->next = b;
    b->prev = a;
    a2->next = an;
    an->prev = a2;
    b2->next = a2;
    a2->prev = b2;
    bp->next = b2;
    b2->prev = bp;
    return b2;
  }

  // go through all polygon nodes and cure small local self-intersections
  Node* cureLocalIntersections(Node* start) {
    Node* p = start;
    do {
      Node* const a = p->prev;
      Node* const b = p->next->next;
      if (!equals(a, b) && intersects(a, p, p->next, b) && locallyInside(a, b) && locallyInside(b, a)) {
        triangles.push_back(a->i);
        triangles.push_back(p->i);
        triangles.push_back(b->i);
        // remove two nodes involved
        removeNode(p);
        removeNode(p->next);
        p = start = b;
      }
      p = p->next;
    } while (p != start);
    return filterPoints(p);
  }

  // try splitting polygon into two and triangulate them independently
  void splitEarcut(Node* start, long dim, double minX, double minY, double invSize) {
    // look for a valid diagonal that divides the polygon into two
    Node* a = start;
    do {
      Node* b = a->next->next;
      while (b != a->prev) {
        if (a->i != b->i && isValidDiagonal(a, b)) {
          // split the polygon in two by the diagonal
          Node* c = splitPolygon(a, b);
          // filter colinear points around the cuts
          a = filterPoints(a, a->next);
          c = filterPoints(c, c->next);
          // run earcut on each half
          earcutLinked(a, dim, minX, minY, invSize, 0);
          earcutLinked(c, dim, minX, minY, invSize, 0);
          return;
        }
        b = b->next;
      }
      a = a->next;
    } while (a != start);
  }

  // main ear slicing loop which triangulates a polygon (given as a linked list)
  void earcutLinked(Node* ear, long dim, double minX, double minY, double invSize, int pass) {
    if (!ear) return;
    // interlink polygon nodes in z-order
    if (!pass && invSize != 0) indexCurve(ear, minX, minY, invSize);
    Node* stop = ear;
    // iterate through ears, slicing them one by one
    while (ear->prev != ear->next) {
      Node* const prev = ear->prev;
      Node* const next = ear->next;
      if (invSize != 0 ? isEarHashed(ear, minX, minY, invSize) : isEar(ear)) {
        // cut off the triangle
        triangles.push_back(prev->i);
        triangles.push_back(ear->i);
        triangles.push_back(next->i);
        removeNode(ear);
        // skipping the next vertex leads to less sliver triangles
        ear = next->next;
        stop = next->next;
        continue;
      }
      ear = next;
      // if we looped through the whole remaining polygon and can't find any more ears
      if (ear == stop) {
        // try filtering points and slicing again
        if (!pass) {
          earcutLinked(filterPoints(ear), dim, minX, minY, invSize, 1);
          // if this didn't work, try curing all small self-intersections locally
        } else if (pass == 1) {
          ear = cureLocalIntersections(filterPoints(ear));
          earcutLinked(ear, dim, minX, minY, invSize, 2);
          // as a last resort, try splitting the remaining polygon into two
        } else if (pass == 2) {
          splitEarcut(ear, dim, minX, minY, invSize);
        }
        break;
      }
    }
  }

  // David Eberly's algorithm for finding a bridge between hole and outer polygon
  static Node* findHoleBridge(Node* hole, Node* outerNode) {
    Node* p = outerNode;
    const double hx = hole->x;
    const double hy = hole->y;
    double qx = -JS_INF;
    Node* m = nullptr;
    // find a segment intersected by a ray from the hole's leftmost point to the left;
    // segment's endpoint with lesser x will be potential connection point
    // unless they intersect at a vertex, then choose the vertex
    if (equals(hole, p)) return p;
    do {
      if (equals(hole, p->next)) return p->next;
      else if (hy <= p->y && hy >= p->next->y && p->next->y != p->y) {
        const double x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
        if (x <= hx && x > qx) {
          qx = x;
          m = p->x < p->next->x ? p : p->next;
          if (x == hx) return m;   // hole touches outer segment; pick leftmost endpoint
        }
      }
      p = p->next;
    } while (p != outerNode);
    if (!m) return nullptr;
    // look for points inside the triangle of hole point, segment intersection and endpoint;
    // if there are no points found, we have a valid connection;
    // otherwise choose the point of the minimum angle with the ray as connection point
    Node* const stop = m;
    const double mx = m->x;
    const double my = m->y;
    double tanMin = JS_INF;
    p = m;
    do {
      if (hx >= p->x && p->x >= mx && hx != p->x && pointInTriangle(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, p->x, p->y)) {
        const double tan = std::fabs(hy - p->y) / (hx - p->x);   // tangential
        if (locallyInside(p, hole) && (tan < tanMin || (tan == tanMin && (p->x > m->x || (p->x == m->x && sectorContainsSector(m, p)))))) {
          m = p;
          tanMin = tan;
        }
      }
      p = p->next;
    } while (p != stop);
    return m;
  }

  // find a bridge between vertices that connects hole with an outer ring and link it
  Node* eliminateHole(Node* hole, Node* outerNode) {
    Node* const bridge = findHoleBridge(hole, outerNode);
    if (!bridge) return outerNode;
    Node* const bridgeReverse = splitPolygon(bridge, hole);
    // filter collinear points around the cuts
    filterPoints(bridgeReverse, bridgeReverse->next);
    return filterPoints(bridge, bridge->next);
  }

  static double compareXYSlope(const Node* a, const Node* b) {
    double result = a->x - b->x;
    // when the left-most point of 2 holes meet at a vertex, sort the holes counterclockwise so that when we find
    // the bridge to the outer shell is always the point that they meet at.
    if (result == 0) {
      result = a->y - b->y;
      if (result == 0) {
        const double aSlope = (a->next->y - a->y) / (a->next->x - a->x);
        const double bSlope = (b->next->y - b->y) / (b->next->x - b->x);
        result = aSlope - bSlope;
      }
    }
    return result;
  }

  // link every hole into the outer loop, producing a single-ring polygon without holes
  Node* eliminateHoles(const std::vector<double>& data, const std::vector<int>& holeIndices, Node* outerNode, long dim) {
    std::vector<Node*> queue;
    for (size_t i = 0, len = holeIndices.size(); i < len; i++) {
      const long start = holeIndices[i] * dim;
      const long end = i < len - 1 ? holeIndices[i + 1] * dim : static_cast<long>(data.size());
      Node* const list = linkedList(data, start, end, dim, false);
      if (!list) continue;   // (a hole without points: JavaScript would fail here)
      if (list == list->next) list->steiner = true;
      queue.push_back(getLeftmost(list));
    }
    std::stable_sort(queue.begin(), queue.end(), [](const Node* a, const Node* b) { return compareXYSlope(a, b) < 0; });
    // process holes from left to right
    for (size_t i = 0; i < queue.size(); i++) outerNode = eliminateHole(queue[i], outerNode);
    return outerNode;
  }

  void run(const std::vector<double>& data, const std::vector<int>& holeIndices, long dim) {
    const bool hasHoles = !holeIndices.empty();
    const long outerLen = hasHoles ? holeIndices[0] * dim : static_cast<long>(data.size());
    Node* outerNode = linkedList(data, 0, outerLen, dim, true);
    if (!outerNode || outerNode->next == outerNode->prev) return;
    double minX = 0, minY = 0, invSize = 0;
    if (hasHoles) outerNode = eliminateHoles(data, holeIndices, outerNode, dim);
    // if the shape is not too simple, we'll use z-order curve hash later; calculate polygon bbox
    if (static_cast<long>(data.size()) > 80 * dim) {
      minX = data[0];
      minY = data[1];
      double maxX = minX;
      double maxY = minY;
      for (long i = dim; i < outerLen; i += dim) {
        const double x = data[static_cast<size_t>(i)];
        const double y = data[static_cast<size_t>(i + 1)];
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
      }
      // minX, minY and invSize are later used to transform coords into integers for z-order calculation
      invSize = jsMax(maxX - minX, maxY - minY);
      invSize = invSize != 0 ? 32767 / invSize : 0;
    }
    earcutLinked(outerNode, dim, minX, minY, invSize, 0);
  }
};

}  // namespace

std::vector<int> Earcut::triangulate(const std::vector<double>& data, const std::vector<int>& holeIndices, int dim) {
  // (what is no polygon – a dimension below 2, half a point at the end, holes that do not begin within the points one after the other – has no triangles)
  if (dim < 2 || data.size() % static_cast<size_t>(dim) != 0) return {};
  long last = 0;
  for (int hole : holeIndices) {
    if (hole < last || static_cast<long>(hole) * dim > static_cast<long>(data.size())) return {};
    last = hole;
  }
  EarcutRun run;
  run.run(data, holeIndices, dim);
  return std::move(run.triangles);
}

// ---------------------------------------------------------------------------
// ShapeUtils
// ---------------------------------------------------------------------------

namespace {

void removeDupEndPts(std::vector<Vector2>& points) {
  const size_t l = points.size();
  if (l > 2 && points[l - 1].equals(points[0])) points.pop_back();
}

void addContour(std::vector<double>& vertices, const std::vector<Vector2>& contour) {
  for (size_t i = 0; i < contour.size(); i++) {
    vertices.push_back(contour[i].x);
    vertices.push_back(contour[i].y);
  }
}

}  // namespace

// calculate area of the contour polygon
double ShapeUtils::area(const std::vector<Vector2>& contour) {
  const size_t n = contour.size();
  double a = 0.0;
  for (size_t p = n - 1, q = 0; q < n; p = q++) a += contour[p].x * contour[q].y - contour[q].x * contour[p].y;
  return a * 0.5;
}

bool ShapeUtils::isClockWise(const std::vector<Vector2>& pts) { return ShapeUtils::area(pts) < 0; }

std::vector<std::array<int, 3>> ShapeUtils::triangulateShape(std::vector<Vector2>& contour, std::vector<std::vector<Vector2>>& holes) {
  std::vector<double> vertices;   // flat array of vertices like [ x0,y0, x1,y1, x2,y2, ... ]
  std::vector<int> holeIndices;   // array of hole indices
  std::vector<std::array<int, 3>> faces;   // final array of vertex indices like [ [ a,b,d ], [ b,c,d ] ]
  removeDupEndPts(contour);
  addContour(vertices, contour);
  int holeIndex = static_cast<int>(contour.size());
  for (std::vector<Vector2>& hole : holes) removeDupEndPts(hole);
  for (size_t i = 0; i < holes.size(); i++) {
    holeIndices.push_back(holeIndex);
    holeIndex += static_cast<int>(holes[i].size());
    addContour(vertices, holes[i]);
  }
  const std::vector<int> triangles = Earcut::triangulate(vertices, holeIndices);
  for (size_t i = 0; i + 2 < triangles.size(); i += 3) faces.push_back({triangles[i], triangles[i + 1], triangles[i + 2]});
  return faces;
}

std::vector<std::array<int, 3>> ShapeUtils::triangulateShape(std::vector<Vector2>& contour) {
  std::vector<std::vector<Vector2>> holes;
  return triangulateShape(contour, holes);
}

}  // namespace fs
