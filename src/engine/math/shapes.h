// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Box2, Box3, Sphere, Plane, Line3, Frustum, Ray, Triangle, Spherical – three.js's,
 * method for method.
 *
 * Origin: three.js r187 src/math/{Box2,Box3,Sphere,Plane,Line3,Frustum,Ray,Triangle,Spherical}.js
 *
 * Where JavaScript answers `null` (no intersection, a degenerate triangle) a null
 * pointer is answered here – or an empty std::optional where the answer is a number.
 */
#pragma once

#include <optional>
#include <vector>

#include "engine/math/linear.h"

namespace fs {

class Object3D;
class Sprite;
struct Sphere;
struct Plane;
struct Triangle;

// ---------------------------------------------------------------------------
// Spherical: a point by radius, polar angle phi (from +y) and azimuth theta (around y, from +z)
// ---------------------------------------------------------------------------

struct Spherical {
  double radius = 1, phi = 0, theta = 0;

  Spherical() = default;
  Spherical(double radius_, double phi_, double theta_) : radius(radius_), phi(phi_), theta(theta_) {}

  Spherical& set(double radius_, double phi_, double theta_) {
    radius = radius_;
    phi = phi_;
    theta = theta_;
    return *this;
  }
  Spherical& copy(const Spherical& other) {
    radius = other.radius;
    phi = other.phi;
    theta = other.theta;
    return *this;
  }
  /** phi kept off the poles */
  Spherical& makeSafe() {
    const double EPS = 0.000001;
    phi = MathUtils::clamp(phi, EPS, PI - EPS);
    return *this;
  }
  Spherical& setFromVector3(const Vector3& v) { return setFromCartesianCoords(v.x, v.y, v.z); }
  Spherical& setFromCartesianCoords(double x, double y, double z) {
    radius = std::sqrt(x * x + y * y + z * z);
    if (radius == 0) {
      theta = 0;
      phi = 0;
    } else {
      theta = std::atan2(x, z);
      phi = std::acos(MathUtils::clamp(y / radius, -1, 1));
    }
    return *this;
  }
  Spherical clone() const { return *this; }
};

inline Vector3& Vector3::setFromSpherical(const Spherical& s) { return setFromSphericalCoords(s.radius, s.phi, s.theta); }

// ---------------------------------------------------------------------------
// Box2
// ---------------------------------------------------------------------------

struct Box2 {
  Vector2 min{JS_INF, JS_INF};
  Vector2 max{-JS_INF, -JS_INF};

  /** empty */
  Box2() = default;
  Box2(const Vector2& min_, const Vector2& max_) : min(min_), max(max_) {}

  Box2& set(const Vector2& min_, const Vector2& max_) {
    min.copy(min_);
    max.copy(max_);
    return *this;
  }
  Box2& setFromPoints(const std::vector<Vector2>& points) {
    makeEmpty();
    for (const Vector2& p : points) expandByPoint(p);
    return *this;
  }
  Box2& setFromCenterAndSize(const Vector2& center, const Vector2& size) {
    Vector2 halfSize = size;
    halfSize.multiplyScalar(0.5);
    min.copy(center).sub(halfSize);
    max.copy(center).add(halfSize);
    return *this;
  }
  Box2 clone() const { return *this; }
  Box2& copy(const Box2& box) {
    min.copy(box.min);
    max.copy(box.max);
    return *this;
  }
  Box2& makeEmpty() {
    min.x = min.y = JS_INF;
    max.x = max.y = -JS_INF;
    return *this;
  }
  bool isEmpty() const {
    // this is a more robust check for empty than ( volume <= 0 ) because volume can get positive with two negative axes
    return (max.x < min.x) || (max.y < min.y);
  }
  Vector2& getCenter(Vector2& target) const { return isEmpty() ? target.set(0, 0) : target.addVectors(min, max).multiplyScalar(0.5); }
  Vector2& getSize(Vector2& target) const { return isEmpty() ? target.set(0, 0) : target.subVectors(max, min); }
  Vector2 getCenter() const {
    Vector2 target;
    return getCenter(target);
  }
  Vector2 getSize() const {
    Vector2 target;
    return getSize(target);
  }
  Box2& expandByPoint(const Vector2& point) {
    min.min(point);
    max.max(point);
    return *this;
  }
  Box2& expandByVector(const Vector2& vector) {
    min.sub(vector);
    max.add(vector);
    return *this;
  }
  Box2& expandByScalar(double scalar) {
    min.addScalar(-scalar);
    max.addScalar(scalar);
    return *this;
  }
  bool containsPoint(const Vector2& point) const { return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y; }
  bool containsBox(const Box2& box) const { return min.x <= box.min.x && box.max.x <= max.x && min.y <= box.min.y && box.max.y <= max.y; }
  Vector2& getParameter(const Vector2& point, Vector2& target) const {
    // This can potentially have a divide by zero if the box
    // has a size dimension of 0.
    return target.set((point.x - min.x) / (max.x - min.x), (point.y - min.y) / (max.y - min.y));
  }
  bool intersectsBox(const Box2& box) const {
    // using 4 splitting planes to rule out intersections
    return box.max.x >= min.x && box.min.x <= max.x && box.max.y >= min.y && box.min.y <= max.y;
  }
  Vector2& clampPoint(const Vector2& point, Vector2& target) const { return target.copy(point).clamp(min, max); }
  double distanceToPoint(const Vector2& point) const {
    Vector2 vector;
    return clampPoint(point, vector).distanceTo(point);
  }
  Box2& intersect(const Box2& box) {
    min.max(box.min);
    max.min(box.max);
    if (isEmpty()) makeEmpty();
    return *this;
  }
  Box2& union_(const Box2& box) {
    min.min(box.min);
    max.max(box.max);
    return *this;
  }
  Box2& translate(const Vector2& offset) {
    min.add(offset);
    max.add(offset);
    return *this;
  }
  bool equals(const Box2& box) const { return box.min.equals(min) && box.max.equals(max); }
};

// ---------------------------------------------------------------------------
// Box3
// ---------------------------------------------------------------------------

struct Box3 {
  Vector3 min{JS_INF, JS_INF, JS_INF};
  Vector3 max{-JS_INF, -JS_INF, -JS_INF};

  /** empty */
  Box3() = default;
  Box3(const Vector3& min_, const Vector3& max_) : min(min_), max(max_) {}

  Box3& set(const Vector3& min_, const Vector3& max_) {
    min.copy(min_);
    max.copy(max_);
    return *this;
  }
  /** from x, y, z, x, y, z … in a plain array */
  template <class A>
  Box3& setFromArray(const A& array) {
    makeEmpty();
    Vector3 vector;
    for (size_t i = 0, il = array.size(); i < il; i += 3) expandByPoint(vector.fromArray(array, i));
    return *this;
  }
  template <class A>
  Box3& setFromBufferAttribute(const A& attribute) {
    makeEmpty();
    Vector3 vector;
    for (size_t i = 0, il = attribute.count(); i < il; i++) expandByPoint(vector.fromBufferAttribute(attribute, i));
    return *this;
  }
  Box3& setFromPoints(const std::vector<Vector3>& points) {
    makeEmpty();
    for (const Vector3& p : points) expandByPoint(p);
    return *this;
  }
  Box3& setFromPoints(const Vector3* points, size_t count) {
    makeEmpty();
    for (size_t i = 0; i < count; i++) expandByPoint(points[i]);
    return *this;
  }
  Box3& setFromCenterAndSize(const Vector3& center, const Vector3& size) {
    Vector3 halfSize = size;
    halfSize.multiplyScalar(0.5);
    min.copy(center).sub(halfSize);
    max.copy(center).add(halfSize);
    return *this;
  }
  /** the world-space box of an object and what is below it (engine: object.cpp) */
  Box3& setFromObject(Object3D& object, bool precise = false);
  Box3 clone() const { return *this; }
  Box3& copy(const Box3& box) {
    min.copy(box.min);
    max.copy(box.max);
    return *this;
  }
  Box3& makeEmpty() {
    min.x = min.y = min.z = JS_INF;
    max.x = max.y = max.z = -JS_INF;
    return *this;
  }
  bool isEmpty() const {
    // this is a more robust check for empty than ( volume <= 0 ) because volume can get positive with two negative axes
    return (max.x < min.x) || (max.y < min.y) || (max.z < min.z);
  }
  Vector3& getCenter(Vector3& target) const { return isEmpty() ? target.set(0, 0, 0) : target.addVectors(min, max).multiplyScalar(0.5); }
  Vector3& getSize(Vector3& target) const { return isEmpty() ? target.set(0, 0, 0) : target.subVectors(max, min); }
  Vector3 getCenter() const {
    Vector3 target;
    return getCenter(target);
  }
  Vector3 getSize() const {
    Vector3 target;
    return getSize(target);
  }
  Box3& expandByPoint(const Vector3& point) {
    min.min(point);
    max.max(point);
    return *this;
  }
  Box3& expandByVector(const Vector3& vector) {
    min.sub(vector);
    max.add(vector);
    return *this;
  }
  Box3& expandByScalar(double scalar) {
    min.addScalar(-scalar);
    max.addScalar(scalar);
    return *this;
  }
  /** (engine: object.cpp) */
  Box3& expandByObject(Object3D& object, bool precise = false);
  bool containsPoint(const Vector3& point) const {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y && point.z >= min.z && point.z <= max.z;
  }
  bool containsBox(const Box3& box) const {
    return min.x <= box.min.x && box.max.x <= max.x && min.y <= box.min.y && box.max.y <= max.y && min.z <= box.min.z && box.max.z <= max.z;
  }
  Vector3& getParameter(const Vector3& point, Vector3& target) const {
    // This can potentially have a divide by zero if the box
    // has a size dimension of 0.
    return target.set((point.x - min.x) / (max.x - min.x), (point.y - min.y) / (max.y - min.y), (point.z - min.z) / (max.z - min.z));
  }
  bool intersectsBox(const Box3& box) const {
    // using 6 splitting planes to rule out intersections.
    return box.max.x >= min.x && box.min.x <= max.x && box.max.y >= min.y && box.min.y <= max.y && box.max.z >= min.z && box.min.z <= max.z;
  }
  inline bool intersectsSphere(const Sphere& sphere) const;
  inline bool intersectsPlane(const Plane& plane) const;
  inline bool intersectsTriangle(const Triangle& triangle) const;
  Vector3& clampPoint(const Vector3& point, Vector3& target) const { return target.copy(point).clamp(min, max); }
  double distanceToPoint(const Vector3& point) const {
    Vector3 vector;
    return clampPoint(point, vector).distanceTo(point);
  }
  inline Sphere& getBoundingSphere(Sphere& target) const;
  Box3& intersect(const Box3& box) {
    min.max(box.min);
    max.min(box.max);
    // ensure that if there is no overlap, the result is fully empty, not slightly empty with non-inf/+inf values that will cause subsequence intersects to erroneously return valid values.
    if (isEmpty()) makeEmpty();
    return *this;
  }
  Box3& union_(const Box3& box) {
    min.min(box.min);
    max.max(box.max);
    return *this;
  }
  /** the box around the transformed box's eight corners */
  Box3& applyMatrix4(const Matrix4& matrix) {
    // transform of empty box is an empty box.
    if (isEmpty()) return *this;
    // NOTE: I am using a binary pattern to specify all 2^3 combinations below
    Vector3 points[8];
    points[0].set(min.x, min.y, min.z).applyMatrix4(matrix);   // 000
    points[1].set(min.x, min.y, max.z).applyMatrix4(matrix);   // 001
    points[2].set(min.x, max.y, min.z).applyMatrix4(matrix);   // 010
    points[3].set(min.x, max.y, max.z).applyMatrix4(matrix);   // 011
    points[4].set(max.x, min.y, min.z).applyMatrix4(matrix);   // 100
    points[5].set(max.x, min.y, max.z).applyMatrix4(matrix);   // 101
    points[6].set(max.x, max.y, min.z).applyMatrix4(matrix);   // 110
    points[7].set(max.x, max.y, max.z).applyMatrix4(matrix);   // 111
    setFromPoints(points, 8);
    return *this;
  }
  Box3& translate(const Vector3& offset) {
    min.add(offset);
    max.add(offset);
    return *this;
  }
  bool equals(const Box3& box) const { return box.min.equals(min) && box.max.equals(max); }
};

// ---------------------------------------------------------------------------
// Sphere
// ---------------------------------------------------------------------------

struct Sphere {
  Vector3 center;
  /** below 0: empty */
  double radius = -1;

  Sphere() = default;
  Sphere(const Vector3& center_, double radius_) : center(center_), radius(radius_) {}

  Sphere& set(const Vector3& center_, double radius_) {
    center.copy(center_);
    radius = radius_;
    return *this;
  }
  /** around the points: about the middle of their box, or about the centre given */
  Sphere& setFromPoints(const std::vector<Vector3>& points, const Vector3* optionalCenter = nullptr) {
    if (optionalCenter != nullptr) {
      center.copy(*optionalCenter);
    } else {
      Box3 box;
      box.setFromPoints(points).getCenter(center);
    }
    double maxRadiusSq = 0;
    for (const Vector3& p : points) maxRadiusSq = jsMax(maxRadiusSq, center.distanceToSquared(p));
    radius = std::sqrt(maxRadiusSq);
    return *this;
  }
  Sphere& copy(const Sphere& sphere) {
    center.copy(sphere.center);
    radius = sphere.radius;
    return *this;
  }
  bool isEmpty() const { return radius < 0; }
  Sphere& makeEmpty() {
    center.set(0, 0, 0);
    radius = -1;
    return *this;
  }
  bool containsPoint(const Vector3& point) const { return point.distanceToSquared(center) <= (radius * radius); }
  double distanceToPoint(const Vector3& point) const { return point.distanceTo(center) - radius; }
  bool intersectsSphere(const Sphere& sphere) const {
    const double radiusSum = radius + sphere.radius;
    return sphere.center.distanceToSquared(center) <= (radiusSum * radiusSum);
  }
  bool intersectsBox(const Box3& box) const { return box.intersectsSphere(*this); }
  inline bool intersectsPlane(const Plane& plane) const;
  Vector3& clampPoint(const Vector3& point, Vector3& target) const {
    const double deltaLengthSq = center.distanceToSquared(point);
    target.copy(point);
    if (deltaLengthSq > (radius * radius)) {
      target.sub(center).normalize();
      target.multiplyScalar(radius).add(center);
    }
    return target;
  }
  Box3& getBoundingBox(Box3& target) const {
    if (isEmpty()) {
      // Empty sphere produces empty bounding box
      target.makeEmpty();
      return target;
    }
    target.set(center, center);
    target.expandByScalar(radius);
    return target;
  }
  Sphere& applyMatrix4(const Matrix4& matrix) {
    center.applyMatrix4(matrix);
    radius = radius * matrix.getMaxScaleOnAxis();
    return *this;
  }
  Sphere& translate(const Vector3& offset) {
    center.add(offset);
    return *this;
  }
  Sphere& expandByPoint(const Vector3& point) {
    if (isEmpty()) {
      center.copy(point);
      radius = 0;
      return *this;
    }
    Vector3 v1;
    v1.subVectors(point, center);
    const double lengthSq = v1.lengthSq();
    if (lengthSq > (radius * radius)) {
      // calculate the minimal sphere
      const double length = std::sqrt(lengthSq);
      const double delta = (length - radius) * 0.5;
      center.addScaledVector(v1, delta / length);
      radius += delta;
    }
    return *this;
  }
  Sphere& union_(const Sphere& sphere) {
    if (sphere.isEmpty()) return *this;
    if (isEmpty()) {
      copy(sphere);
      return *this;
    }
    if (center.equals(sphere.center) == true) {
      radius = jsMax(radius, sphere.radius);
    } else {
      Vector3 v1, v2;
      v2.subVectors(sphere.center, center).setLength(sphere.radius);
      expandByPoint(v1.copy(sphere.center).add(v2));
      expandByPoint(v1.copy(sphere.center).sub(v2));
    }
    return *this;
  }
  bool equals(const Sphere& sphere) const { return sphere.center.equals(center) && (sphere.radius == radius); }
  Sphere clone() const { return *this; }
};

// ---------------------------------------------------------------------------
// Line3: the segment from start to end
// ---------------------------------------------------------------------------

struct Line3 {
  Vector3 start, end;

  Line3() = default;
  Line3(const Vector3& start_, const Vector3& end_) : start(start_), end(end_) {}

  Line3& set(const Vector3& start_, const Vector3& end_) {
    start.copy(start_);
    end.copy(end_);
    return *this;
  }
  Line3& copy(const Line3& line) {
    start.copy(line.start);
    end.copy(line.end);
    return *this;
  }
  Vector3& getCenter(Vector3& target) const { return target.addVectors(start, end).multiplyScalar(0.5); }
  Vector3& delta(Vector3& target) const { return target.subVectors(end, start); }
  double distanceSq() const { return start.distanceToSquared(end); }
  double distance() const { return start.distanceTo(end); }
  Vector3& at(double t, Vector3& target) const { return delta(target).multiplyScalar(t).add(start); }
  double closestPointToPointParameter(const Vector3& point, bool clampToLine) const {
    Vector3 startP, startEnd;
    startP.subVectors(point, start);
    startEnd.subVectors(end, start);
    const double startEnd2 = startEnd.dot(startEnd);
    if (startEnd2 == 0) return 0;
    const double startEnd_startP = startEnd.dot(startP);
    double t = startEnd_startP / startEnd2;
    if (clampToLine) t = MathUtils::clamp(t, 0, 1);
    return t;
  }
  Vector3& closestPointToPoint(const Vector3& point, bool clampToLine, Vector3& target) const {
    const double t = closestPointToPointParameter(point, clampToLine);
    return delta(target).multiplyScalar(t).add(start);
  }
  /** the squared distance between two segments; c1 and c2 (if given) take the closest points on this and on the other */
  double distanceSqToLine3(const Line3& line, Vector3* c1 = nullptr, Vector3* c2 = nullptr) const {
    // from Real-Time Collision Detection by Christer Ericson, chapter 5.1.9
    using MathUtils::clamp;
    Vector3 c1_, c2_;
    if (c1 == nullptr) c1 = &c1_;
    if (c2 == nullptr) c2 = &c2_;
    const double EPSILON = 1e-8 * 1e-8;   // must be squared since we compare squared length
    double s, t;
    const Vector3& p1 = start;
    const Vector3& p2 = line.start;
    const Vector3& q1 = end;
    const Vector3& q2 = line.end;
    Vector3 d1, d2, r;
    d1.subVectors(q1, p1);   // Direction vector of segment S1
    d2.subVectors(q2, p2);   // Direction vector of segment S2
    r.subVectors(p1, p2);
    const double a = d1.dot(d1);   // Squared length of segment S1, always nonnegative
    const double e = d2.dot(d2);   // Squared length of segment S2, always nonnegative
    const double f = d2.dot(r);
    // Check if either or both segments degenerate into points
    if (a <= EPSILON && e <= EPSILON) {
      // Both segments degenerate into points
      c1->copy(p1);
      c2->copy(p2);
      return c1->distanceToSquared(*c2);
    }
    if (a <= EPSILON) {
      // First segment degenerates into a point
      s = 0;
      t = f / e;   // s = 0 => t = (b*s + f) / e = f / e
      t = clamp(t, 0, 1);
    } else {
      const double c = d1.dot(r);
      if (e <= EPSILON) {
        // Second segment degenerates into a point
        t = 0;
        s = clamp(-c / a, 0, 1);   // t = 0 => s = (b*t - c) / a = -c / a
      } else {
        // The general nondegenerate case starts here
        const double b = d1.dot(d2);
        const double denom = a * e - b * b;   // Always nonnegative
        // If segments not parallel, compute closest point on L1 to L2 and
        // clamp to segment S1. Else pick arbitrary s (here 0)
        if (denom != 0) {
          s = clamp((b * f - c * e) / denom, 0, 1);
        } else {
          s = 0;
        }
        // Compute point on L2 closest to S1(s) using
        // t = Dot((P1 + D1*s) - P2,D2) / Dot(D2,D2) = (b*s + f) / e
        t = (b * s + f) / e;
        // If t in [0,1] done. Else clamp t, recompute s for the new value
        // of t using s = Dot((P2 + D2*t) - P1,D1) / Dot(D1,D1)= (t*b - c) / a
        // and clamp s to [0, 1]
        if (t < 0) {
          t = 0.;
          s = clamp(-c / a, 0, 1);
        } else if (t > 1) {
          t = 1;
          s = clamp((b - c) / a, 0, 1);
        }
      }
    }
    c1->copy(p1).addScaledVector(d1, s);
    c2->copy(p2).addScaledVector(d2, t);
    return c1->distanceToSquared(*c2);
  }
  Line3& applyMatrix4(const Matrix4& matrix) {
    start.applyMatrix4(matrix);
    end.applyMatrix4(matrix);
    return *this;
  }
  bool equals(const Line3& line) const { return line.start.equals(start) && line.end.equals(end); }
  Line3 clone() const { return *this; }
};

// ---------------------------------------------------------------------------
// Plane: the points p with normal · p + constant = 0
// ---------------------------------------------------------------------------

struct Plane {
  Vector3 normal{1, 0, 0};
  double constant = 0;

  Plane() = default;
  Plane(const Vector3& normal_, double constant_) : normal(normal_), constant(constant_) {}

  Plane& set(const Vector3& normal_, double constant_) {
    normal.copy(normal_);
    constant = constant_;
    return *this;
  }
  Plane& setComponents(double x, double y, double z, double w) {
    normal.set(x, y, z);
    constant = w;
    return *this;
  }
  Plane& setFromNormalAndCoplanarPoint(const Vector3& normal_, const Vector3& point) {
    normal.copy(normal_);
    constant = -point.dot(normal);
    return *this;
  }
  /** through three points; counterclockwise seen from where the normal points */
  Plane& setFromCoplanarPoints(const Vector3& a, const Vector3& b, const Vector3& c) {
    Vector3 vector1, vector2;
    const Vector3& normal_ = vector1.subVectors(c, b).cross(vector2.subVectors(a, b)).normalize();
    // Q: should an error be thrown if normal is zero (e.g. degenerate plane)?
    setFromNormalAndCoplanarPoint(normal_, a);
    return *this;
  }
  Plane& copy(const Plane& plane) {
    normal.copy(plane.normal);
    constant = plane.constant;
    return *this;
  }
  Plane& normalize() {
    // Note: will lead to a divide by zero on invalid plane.
    const double inverseNormalLength = 1.0 / normal.length();
    normal.multiplyScalar(inverseNormalLength);
    constant *= inverseNormalLength;
    return *this;
  }
  Plane& negate() {
    constant *= -1;
    normal.negate();
    return *this;
  }
  /** signed: positive on the side the normal points to */
  double distanceToPoint(const Vector3& point) const { return normal.dot(point) + constant; }
  double distanceToSphere(const Sphere& sphere) const { return distanceToPoint(sphere.center) - sphere.radius; }
  Vector3& projectPoint(const Vector3& point, Vector3& target) const { return target.copy(point).addScaledVector(normal, -distanceToPoint(point)); }
  /** where the segment meets the plane (null: nowhere); `clampToLine` false: the whole line through it */
  Vector3* intersectLine(const Line3& line, Vector3& target, bool clampToLine = true) const {
    Vector3 vector1;
    const Vector3& direction = line.delta(vector1);
    const double denominator = normal.dot(direction);
    if (denominator == 0) {
      // line is coplanar, return origin
      if (distanceToPoint(line.start) == 0) return &target.copy(line.start);
      // Unsure if this is the correct method to handle this case.
      return nullptr;
    }
    const double t = -(line.start.dot(normal) + constant) / denominator;
    if ((clampToLine == true) && (t < 0 || t > 1)) return nullptr;
    return &target.copy(line.start).addScaledVector(direction, t);
  }
  bool intersectsLine(const Line3& line) const {
    // Note: this tests if a line intersects the plane, not whether it (or its end-points) are coplanar with it.
    const double startSign = distanceToPoint(line.start);
    const double endSign = distanceToPoint(line.end);
    return (startSign < 0 && endSign > 0) || (endSign < 0 && startSign > 0);
  }
  bool intersectsBox(const Box3& box) const { return box.intersectsPlane(*this); }
  bool intersectsSphere(const Sphere& sphere) const { return sphere.intersectsPlane(*this); }
  Vector3& coplanarPoint(Vector3& target) const { return target.copy(normal).multiplyScalar(-constant); }
  /** through an affine matrix (the normal matrix may be given when it is at hand) */
  Plane& applyMatrix4(const Matrix4& matrix, const Matrix3* optionalNormalMatrix = nullptr) {
    Matrix3 normalMatrix_;
    const Matrix3& normalMatrix = optionalNormalMatrix ? *optionalNormalMatrix : normalMatrix_.getNormalMatrix(matrix);
    Vector3 vector1;
    const Vector3& referencePoint = coplanarPoint(vector1).applyMatrix4(matrix);
    const Vector3& normal_ = normal.applyMatrix3(normalMatrix).normalize();
    constant = -referencePoint.dot(normal_);
    return *this;
  }
  Plane& translate(const Vector3& offset) {
    constant -= offset.dot(normal);
    return *this;
  }
  bool equals(const Plane& plane) const { return plane.normal.equals(normal) && (plane.constant == constant); }
  Plane clone() const { return *this; }
};

// ---------------------------------------------------------------------------
// Triangle
// ---------------------------------------------------------------------------

struct Triangle {
  Vector3 a, b, c;

  Triangle() = default;
  Triangle(const Vector3& a_, const Vector3& b_, const Vector3& c_) : a(a_), b(b_), c(c_) {}

  /** the normal of a, b, c (counterclockwise seen from where it points; the zero vector for a triangle without area) */
  static Vector3& getNormal(const Vector3& a, const Vector3& b, const Vector3& c, Vector3& target) {
    Vector3 v0;
    target.subVectors(c, b);
    v0.subVectors(a, b);
    target.cross(v0);
    const double targetLengthSq = target.lengthSq();
    if (targetLengthSq > 0) return target.multiplyScalar(1 / std::sqrt(targetLengthSq));
    return target.set(0, 0, 0);
  }
  /** the barycentric coordinates of a point (null for a triangle without area – the target is then 0, 0, 0) */
  static Vector3* getBarycoord(const Vector3& point, const Vector3& a, const Vector3& b, const Vector3& c, Vector3& target) {
    // based on: http://www.blackpawn.com/texts/pointinpoly/default.html
    Vector3 v0, v1, v2;
    v0.subVectors(c, a);
    v1.subVectors(b, a);
    v2.subVectors(point, a);
    const double dot00 = v0.dot(v0);
    const double dot01 = v0.dot(v1);
    const double dot02 = v0.dot(v2);
    const double dot11 = v1.dot(v1);
    const double dot12 = v1.dot(v2);
    const double denom = (dot00 * dot11 - dot01 * dot01);
    // collinear or singular triangle
    if (denom == 0) {
      target.set(0, 0, 0);
      return nullptr;
    }
    const double invDenom = 1 / denom;
    const double u = (dot11 * dot02 - dot01 * dot12) * invDenom;
    const double v = (dot00 * dot12 - dot01 * dot02) * invDenom;
    // barycentric coordinates must always sum to 1
    return &target.set(1 - u - v, v, u);
  }
  static bool containsPoint(const Vector3& point, const Vector3& a, const Vector3& b, const Vector3& c) {
    // if the triangle is degenerate then we can't contain a point
    Vector3 v3;
    if (getBarycoord(point, a, b, c, v3) == nullptr) return false;
    return (v3.x >= 0) && (v3.y >= 0) && ((v3.x + v3.y) <= 1);
  }
  /** v1, v2, v3 (vectors of any size, one per corner) weighed by where the point is in p1, p2, p3 (null: a triangle without area) */
  template <class V>
  static V* getInterpolation(const Vector3& point, const Vector3& p1, const Vector3& p2, const Vector3& p3, const V& v1, const V& v2, const V& v3, V& target) {
    Vector3 bary;
    if (getBarycoord(point, p1, p2, p3, bary) == nullptr) {
      target.setScalar(0);
      return nullptr;
    }
    target.setScalar(0);
    target.addScaledVector(v1, bary.x);
    target.addScaledVector(v2, bary.y);
    target.addScaledVector(v3, bary.z);
    return &target;
  }
  /** an attribute's values at three vertices, weighed by barycentric coordinates */
  template <class A, class V>
  static V& getInterpolatedAttribute(const A& attr, size_t i1, size_t i2, size_t i3, const Vector3& barycoord, V& target) {
    V v40, v41, v42;
    v40.setScalar(0);
    v41.setScalar(0);
    v42.setScalar(0);
    v40.fromBufferAttribute(attr, i1);
    v41.fromBufferAttribute(attr, i2);
    v42.fromBufferAttribute(attr, i3);
    target.setScalar(0);
    target.addScaledVector(v40, barycoord.x);
    target.addScaledVector(v41, barycoord.y);
    target.addScaledVector(v42, barycoord.z);
    return target;
  }
  static bool isFrontFacing(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& direction) {
    Vector3 v0, v1;
    v0.subVectors(c, b);
    v1.subVectors(a, b);
    // strictly front facing
    return v0.cross(v1).dot(direction) < 0;
  }

  Triangle& set(const Vector3& a_, const Vector3& b_, const Vector3& c_) {
    a.copy(a_);
    b.copy(b_);
    c.copy(c_);
    return *this;
  }
  Triangle& setFromPointsAndIndices(const std::vector<Vector3>& points, size_t i0, size_t i1, size_t i2) {
    a.copy(points[i0]);
    b.copy(points[i1]);
    c.copy(points[i2]);
    return *this;
  }
  template <class A>
  Triangle& setFromAttributeAndIndices(const A& attribute, size_t i0, size_t i1, size_t i2) {
    a.fromBufferAttribute(attribute, i0);
    b.fromBufferAttribute(attribute, i1);
    c.fromBufferAttribute(attribute, i2);
    return *this;
  }
  Triangle clone() const { return *this; }
  Triangle& copy(const Triangle& triangle) {
    a.copy(triangle.a);
    b.copy(triangle.b);
    c.copy(triangle.c);
    return *this;
  }
  double getArea() const {
    Vector3 v0, v1;
    v0.subVectors(c, b);
    v1.subVectors(a, b);
    return v0.cross(v1).length() * 0.5;
  }
  Vector3& getMidpoint(Vector3& target) const { return target.addVectors(a, b).add(c).multiplyScalar(1.0 / 3); }
  Vector3& getNormal(Vector3& target) const { return Triangle::getNormal(a, b, c, target); }
  Plane& getPlane(Plane& target) const { return target.setFromCoplanarPoints(a, b, c); }
  Vector3* getBarycoord(const Vector3& point, Vector3& target) const { return Triangle::getBarycoord(point, a, b, c, target); }
  template <class V>
  V* getInterpolation(const Vector3& point, const V& v1, const V& v2, const V& v3, V& target) const {
    return Triangle::getInterpolation(point, a, b, c, v1, v2, v3, target);
  }
  bool containsPoint(const Vector3& point) const { return Triangle::containsPoint(point, a, b, c); }
  bool isFrontFacing(const Vector3& direction) const { return Triangle::isFrontFacing(a, b, c, direction); }
  bool intersectsBox(const Box3& box) const { return box.intersectsTriangle(*this); }
  Vector3& closestPointToPoint(const Vector3& p, Vector3& target) const {
    double v, w;
    // algorithm thanks to Real-Time Collision Detection by Christer Ericson,
    // published by Morgan Kaufmann Publishers, (c) 2005 Elsevier Inc.,
    // under the accompanying license; see chapter 5.1.5 for detailed explanation.
    // basically, we're distinguishing which of the voronoi regions of the triangle
    // the point lies in with the minimum amount of redundant computation.
    Vector3 vab, vac, vbc, vap, vbp, vcp;
    vab.subVectors(b, a);
    vac.subVectors(c, a);
    vap.subVectors(p, a);
    const double d1 = vab.dot(vap);
    const double d2 = vac.dot(vap);
    if (d1 <= 0 && d2 <= 0) {
      // vertex region of A; barycentric coords (1, 0, 0)
      return target.copy(a);
    }
    vbp.subVectors(p, b);
    const double d3 = vab.dot(vbp);
    const double d4 = vac.dot(vbp);
    if (d3 >= 0 && d4 <= d3) {
      // vertex region of B; barycentric coords (0, 1, 0)
      return target.copy(b);
    }
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0 && d1 - d3 > 0) {   // modification of the algorithm: d1 - d3 is the squared length of AB, so skip this region if a and b coincide
      v = d1 / (d1 - d3);
      // edge region of AB; barycentric coords (1-v, v, 0)
      return target.copy(a).addScaledVector(vab, v);
    }
    vcp.subVectors(p, c);
    const double d5 = vab.dot(vcp);
    const double d6 = vac.dot(vcp);
    if (d6 >= 0 && d5 <= d6) {
      // vertex region of C; barycentric coords (0, 0, 1)
      return target.copy(c);
    }
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
      w = d2 / (d2 - d6);
      // edge region of AC; barycentric coords (1-w, 0, w)
      return target.copy(a).addScaledVector(vac, w);
    }
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
      vbc.subVectors(c, b);
      w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
      // edge region of BC; barycentric coords (0, 1-w, w)
      return target.copy(b).addScaledVector(vbc, w);   // edge region of BC
    }
    // face region
    const double denom = 1 / (va + vb + vc);
    // u = va * denom
    v = vb * denom;
    w = vc * denom;
    return target.copy(a).addScaledVector(vab, v).addScaledVector(vac, w);
  }
  bool equals(const Triangle& triangle) const { return triangle.a.equals(a) && triangle.b.equals(b) && triangle.c.equals(c); }
};

// ---------------------------------------------------------------------------
// Frustum: six planes, their normals pointing inwards
// ---------------------------------------------------------------------------

struct Frustum {
  Plane planes[6];

  Frustum() = default;
  Frustum(const Plane& p0, const Plane& p1, const Plane& p2, const Plane& p3, const Plane& p4, const Plane& p5) : planes{p0, p1, p2, p3, p4, p5} {}

  Frustum& set(const Plane& p0, const Plane& p1, const Plane& p2, const Plane& p3, const Plane& p4, const Plane& p5) {
    planes[0].copy(p0);
    planes[1].copy(p1);
    planes[2].copy(p2);
    planes[3].copy(p3);
    planes[4].copy(p4);
    planes[5].copy(p5);
    return *this;
  }
  Frustum& copy(const Frustum& frustum) {
    for (int i = 0; i < 6; i++) planes[i].copy(frustum.planes[i]);
    return *this;
  }
  /** from projectionMatrix × matrixWorldInverse (WebGL's depth range -1 … 1) */
  Frustum& setFromProjectionMatrix(const Matrix4& m) {
    const double* me = m.elements;
    const double me0 = me[0], me1 = me[1], me2 = me[2], me3 = me[3];
    const double me4 = me[4], me5 = me[5], me6 = me[6], me7 = me[7];
    const double me8 = me[8], me9 = me[9], me10 = me[10], me11 = me[11];
    const double me12 = me[12], me13 = me[13], me14 = me[14], me15 = me[15];
    planes[0].setComponents(me3 - me0, me7 - me4, me11 - me8, me15 - me12).normalize();
    planes[1].setComponents(me3 + me0, me7 + me4, me11 + me8, me15 + me12).normalize();
    planes[2].setComponents(me3 + me1, me7 + me5, me11 + me9, me15 + me13).normalize();
    planes[3].setComponents(me3 - me1, me7 - me5, me11 - me9, me15 - me13).normalize();
    planes[4].setComponents(me3 - me2, me7 - me6, me11 - me10, me15 - me14).normalize();   // far
    planes[5].setComponents(me3 + me2, me7 + me6, me11 + me10, me15 + me14).normalize();   // near
    return *this;
  }
  /** by the bounding sphere of the object (its own, or its geometry's) in the world (engine: object.cpp) */
  bool intersectsObject(Object3D& object) const;
  bool intersectsSprite(const Sprite& sprite) const;
  bool intersectsSphere(const Sphere& sphere) const {
    const Vector3& center = sphere.center;
    const double negRadius = -sphere.radius;
    for (int i = 0; i < 6; i++) {
      const double distance = planes[i].distanceToPoint(center);
      if (distance < negRadius) return false;
    }
    return true;
  }
  bool intersectsBox(const Box3& box) const {
    Vector3 vector;
    for (int i = 0; i < 6; i++) {
      const Plane& plane = planes[i];
      // corner at max distance
      vector.x = plane.normal.x > 0 ? box.max.x : box.min.x;
      vector.y = plane.normal.y > 0 ? box.max.y : box.min.y;
      vector.z = plane.normal.z > 0 ? box.max.z : box.min.z;
      if (plane.distanceToPoint(vector) < 0) return false;
    }
    return true;
  }
  bool containsPoint(const Vector3& point) const {
    for (int i = 0; i < 6; i++) {
      if (planes[i].distanceToPoint(point) < 0) return false;
    }
    return true;
  }
  Frustum clone() const { return *this; }
};

// ---------------------------------------------------------------------------
// Ray: from an origin along a (normalised) direction
// ---------------------------------------------------------------------------

struct Ray {
  Vector3 origin;
  Vector3 direction{0, 0, -1};

  Ray() = default;
  Ray(const Vector3& origin_, const Vector3& direction_) : origin(origin_), direction(direction_) {}

  Ray& set(const Vector3& origin_, const Vector3& direction_) {
    origin.copy(origin_);
    direction.copy(direction_);
    return *this;
  }
  Ray& copy(const Ray& ray) {
    origin.copy(ray.origin);
    direction.copy(ray.direction);
    return *this;
  }
  Vector3& at(double t, Vector3& target) const { return target.copy(origin).addScaledVector(direction, t); }
  Ray& lookAt(const Vector3& v) {
    direction.copy(v).sub(origin).normalize();
    return *this;
  }
  Ray& recast(double t) {
    Vector3 vector;
    origin.copy(at(t, vector));
    return *this;
  }
  Vector3& closestPointToPoint(const Vector3& point, Vector3& target) const {
    target.subVectors(point, origin);
    const double directionDistance = target.dot(direction);
    if (directionDistance < 0) return target.copy(origin);
    return target.copy(origin).addScaledVector(direction, directionDistance);
  }
  double distanceToPoint(const Vector3& point) const { return std::sqrt(distanceSqToPoint(point)); }
  double distanceSqToPoint(const Vector3& point) const {
    Vector3 vector;
    const double directionDistance = vector.subVectors(point, origin).dot(direction);
    // point behind the ray
    if (directionDistance < 0) return origin.distanceToSquared(point);
    vector.copy(origin).addScaledVector(direction, directionDistance);
    return vector.distanceToSquared(point);
  }
  /** the squared distance to the segment v0–v1; the closest points are written where pointers are given */
  double distanceSqToSegment(const Vector3& v0, const Vector3& v1, Vector3* optionalPointOnRay = nullptr, Vector3* optionalPointOnSegment = nullptr) const {
    // from https://github.com/pmjoniak/GeometricTools/blob/master/GTEngine/Include/Mathematics/GteDistRaySegment.h
    // It returns the min distance between the ray and the segment
    // defined by v0 and v1
    // It can also set two optional targets :
    // - The closest point on the ray
    // - The closest point on the segment
    Vector3 segCenter, segDir, diff;
    segCenter.copy(v0).add(v1).multiplyScalar(0.5);
    segDir.copy(v1).sub(v0).normalize();
    diff.copy(origin).sub(segCenter);
    const double segExtent = v0.distanceTo(v1) * 0.5;
    const double a01 = -direction.dot(segDir);
    const double b0 = diff.dot(direction);
    const double b1 = -diff.dot(segDir);
    const double c = diff.lengthSq();
    const double det = std::fabs(1 - a01 * a01);
    double s0, s1, sqrDist, extDet;
    if (det > 0) {
      // The ray and segment are not parallel.
      s0 = a01 * b1 - b0;
      s1 = a01 * b0 - b1;
      extDet = segExtent * det;
      if (s0 >= 0) {
        if (s1 >= -extDet) {
          if (s1 <= extDet) {
            // region 0
            // Minimum at interior points of ray and segment.
            const double invDet = 1 / det;
            s0 *= invDet;
            s1 *= invDet;
            sqrDist = s0 * (s0 + a01 * s1 + 2 * b0) + s1 * (a01 * s0 + s1 + 2 * b1) + c;
          } else {
            // region 1
            s1 = segExtent;
            s0 = jsMax(0, -(a01 * s1 + b0));
            sqrDist = -s0 * s0 + s1 * (s1 + 2 * b1) + c;
          }
        } else {
          // region 5
          s1 = -segExtent;
          s0 = jsMax(0, -(a01 * s1 + b0));
          sqrDist = -s0 * s0 + s1 * (s1 + 2 * b1) + c;
        }
      } else {
        if (s1 <= -extDet) {
          // region 4
          s0 = jsMax(0, -(-a01 * segExtent + b0));
          s1 = (s0 > 0) ? -segExtent : jsMin(jsMax(-segExtent, -b1), segExtent);
          sqrDist = -s0 * s0 + s1 * (s1 + 2 * b1) + c;
        } else if (s1 <= extDet) {
          // region 3
          s0 = 0;
          s1 = jsMin(jsMax(-segExtent, -b1), segExtent);
          sqrDist = s1 * (s1 + 2 * b1) + c;
        } else {
          // region 2
          s0 = jsMax(0, -(a01 * segExtent + b0));
          s1 = (s0 > 0) ? segExtent : jsMin(jsMax(-segExtent, -b1), segExtent);
          sqrDist = -s0 * s0 + s1 * (s1 + 2 * b1) + c;
        }
      }
    } else {
      // Ray and segment are parallel.
      s1 = (a01 > 0) ? -segExtent : segExtent;
      s0 = jsMax(0, -(a01 * s1 + b0));
      sqrDist = -s0 * s0 + s1 * (s1 + 2 * b1) + c;
    }
    if (optionalPointOnRay) optionalPointOnRay->copy(origin).addScaledVector(direction, s0);
    if (optionalPointOnSegment) optionalPointOnSegment->copy(segCenter).addScaledVector(segDir, s1);
    return sqrDist;
  }
  /** where the ray enters the sphere – or leaves it, from inside (null: it misses) */
  Vector3* intersectSphere(const Sphere& sphere, Vector3& target) const {
    if (sphere.radius < 0) return nullptr;   // handle empty spheres, see #31187
    Vector3 vector;
    vector.subVectors(sphere.center, origin);
    const double tca = vector.dot(direction);
    const double d2 = vector.dot(vector) - tca * tca;
    const double radius2 = sphere.radius * sphere.radius;
    if (d2 > radius2) return nullptr;
    const double thc = std::sqrt(radius2 - d2);
    // t0 = first intersect point - entrance on front of sphere
    const double t0 = tca - thc;
    // t1 = second intersect point - exit point on back of sphere
    const double t1 = tca + thc;
    // test to see if t1 is behind the ray - if so, return null
    if (t1 < 0) return nullptr;
    // test to see if t0 is behind the ray:
    // if it is, the ray is inside the sphere, so return the second exit point scaled by t1,
    // in order to always return an intersect point that is in front of the ray.
    if (t0 < 0) return &at(t1, target);
    // else t0 is in front of the ray, so return the first collision point scaled by t0
    return &at(t0, target);
  }
  bool intersectsSphere(const Sphere& sphere) const {
    if (sphere.radius < 0) return false;   // handle empty spheres, see #31187
    return distanceSqToPoint(sphere.center) <= (sphere.radius * sphere.radius);
  }
  /** how far along the ray the plane is (empty: the ray does not meet it) */
  std::optional<double> distanceToPlane(const Plane& plane) const {
    const double denominator = plane.normal.dot(direction);
    if (denominator == 0) {
      // line is coplanar, return origin
      if (plane.distanceToPoint(origin) == 0) return 0.0;
      // Null is preferable to undefined since undefined means.... it is undefined
      return std::nullopt;
    }
    const double t = -(origin.dot(plane.normal) + plane.constant) / denominator;
    // Return if the ray never intersects the plane
    if (t >= 0) return t;
    return std::nullopt;
  }
  Vector3* intersectPlane(const Plane& plane, Vector3& target) const {
    const std::optional<double> t = distanceToPlane(plane);
    if (!t) return nullptr;
    return &at(*t, target);
  }
  bool intersectsPlane(const Plane& plane) const {
    // check if the ray lies on the plane first
    const double distToPoint = plane.distanceToPoint(origin);
    if (distToPoint == 0) return true;
    const double denominator = plane.normal.dot(direction);
    if (denominator * distToPoint < 0) return true;
    // ray origin is behind the plane (and is pointing behind it)
    return false;
  }
  Vector3* intersectBox(const Box3& box, Vector3& target) const {
    double tmin, tmax, tymin, tymax, tzmin, tzmax;
    const double invdirx = 1 / direction.x, invdiry = 1 / direction.y, invdirz = 1 / direction.z;
    if (invdirx >= 0) {
      tmin = (box.min.x - origin.x) * invdirx;
      tmax = (box.max.x - origin.x) * invdirx;
    } else {
      tmin = (box.max.x - origin.x) * invdirx;
      tmax = (box.min.x - origin.x) * invdirx;
    }
    if (invdiry >= 0) {
      tymin = (box.min.y - origin.y) * invdiry;
      tymax = (box.max.y - origin.y) * invdiry;
    } else {
      tymin = (box.max.y - origin.y) * invdiry;
      tymax = (box.min.y - origin.y) * invdiry;
    }
    if ((tmin > tymax) || (tymin > tmax)) return nullptr;
    if (tymin > tmin || std::isnan(tmin)) tmin = tymin;
    if (tymax < tmax || std::isnan(tmax)) tmax = tymax;
    if (invdirz >= 0) {
      tzmin = (box.min.z - origin.z) * invdirz;
      tzmax = (box.max.z - origin.z) * invdirz;
    } else {
      tzmin = (box.max.z - origin.z) * invdirz;
      tzmax = (box.min.z - origin.z) * invdirz;
    }
    if ((tmin > tzmax) || (tzmin > tmax)) return nullptr;
    if (tzmin > tmin || tmin != tmin) tmin = tzmin;
    if (tzmax < tmax || tmax != tmax) tmax = tzmax;
    // return point closest to the ray (positive side)
    if (tmax < 0) return nullptr;
    return &at(tmin >= 0 ? tmin : tmax, target);
  }
  bool intersectsBox(const Box3& box) const {
    Vector3 vector;
    return intersectBox(box, vector) != nullptr;
  }
  /** where the ray meets the triangle a, b, c (null: it misses; `backfaceCulling`: only from its front) */
  Vector3* intersectTriangle(const Vector3& a, const Vector3& b, const Vector3& c, bool backfaceCulling, Vector3& target) const {
    // Watertight ray/triangle intersection
    // from https://jcgt.org/published/0002/01/05/paper.pdf
    const double dx = direction.x;
    const double dy = direction.y;
    const double dz = direction.z;
    // translate vertices to ray origin
    const double aox = a.x - origin.x, aoy = a.y - origin.y, aoz = a.z - origin.z;
    const double box = b.x - origin.x, boy = b.y - origin.y, boz = b.z - origin.z;
    const double cox = c.x - origin.x, coy = c.y - origin.y, coz = c.z - origin.z;
    // calculate dimension where the ray direction is maximal
    const double adx = std::fabs(dx), ady = std::fabs(dy), adz = std::fabs(dz);
    double dkx, dky, dkz;
    double akx, aky, akz, bkx, bky, bkz, ckx, cky, ckz;
    if (adx >= ady && adx >= adz) {
      dkz = dx; akz = aox; bkz = box; ckz = cox;
      if (dx >= 0) {
        dkx = dy; dky = dz;
        akx = aoy; aky = aoz; bkx = boy; bky = boz; ckx = coy; cky = coz;
      } else {
        dkx = dz; dky = dy;
        akx = aoz; aky = aoy; bkx = boz; bky = boy; ckx = coz; cky = coy;
      }
    } else if (ady >= adz) {
      dkz = dy; akz = aoy; bkz = boy; ckz = coy;
      if (dy >= 0) {
        dkx = dz; dky = dx;
        akx = aoz; aky = aox; bkx = boz; bky = box; ckx = coz; cky = cox;
      } else {
        dkx = dx; dky = dz;
        akx = aox; aky = aoz; bkx = box; bky = boz; ckx = cox; cky = coz;
      }
    } else {
      dkz = dz; akz = aoz; bkz = boz; ckz = coz;
      if (dz >= 0) {
        dkx = dx; dky = dy;
        akx = aox; aky = aoy; bkx = box; bky = boy; ckx = cox; cky = coy;
      } else {
        dkx = dy; dky = dx;
        akx = aoy; aky = aox; bkx = boy; bky = box; ckx = coy; cky = cox;
      }
    }
    if (dkz == 0) return nullptr;
    // calculate shear constants
    const double sx = dkx / dkz, sy = dky / dkz, sz = 1 / dkz;
    // perform shear and scale of vertices
    const double ax = akx - sx * akz, ay = aky - sy * akz;
    const double bx = bkx - sx * bkz, by = bky - sy * bkz;
    const double cx = ckx - sx * ckz, cy = cky - sy * ckz;
    // calculate scaled barycentric coordinates
    const double u = cx * by - cy * bx;
    const double v = ax * cy - ay * cx;
    const double w = bx * ay - by * ax;
    // perform edge tests
    if (backfaceCulling) {
      if (u < 0 || v < 0 || w < 0) return nullptr;
    } else {
      if ((u < 0 || v < 0 || w < 0) && (u > 0 || v > 0 || w > 0)) return nullptr;
    }
    // calculate determinant
    const double det = u + v + w;
    if (det == 0) return nullptr;
    // calculate scaled z-coordinates of vertices and use them to calculate the hit distance
    const double tScaled = sz * (u * akz + v * bkz + w * ckz);
    // reject hits behind the ray origin
    if (det > 0 ? tScaled < 0 : tScaled > 0) return nullptr;
    // ray intersects triangle
    return &at(tScaled / det, target);
  }
  Ray& applyMatrix4(const Matrix4& matrix4) {
    origin.applyMatrix4(matrix4);
    direction.transformDirection(matrix4);
    return *this;
  }
  bool equals(const Ray& ray) const { return ray.origin.equals(origin) && ray.direction.equals(direction); }
  Ray clone() const { return *this; }
};

// ---------------------------------------------------------------------------
// what needs the complete types
// ---------------------------------------------------------------------------

inline bool Box3::intersectsSphere(const Sphere& sphere) const {
  // Find the point on the AABB closest to the sphere center.
  Vector3 vector;
  clampPoint(sphere.center, vector);
  // If that point is inside the sphere, the AABB and sphere intersect.
  return vector.distanceToSquared(sphere.center) <= (sphere.radius * sphere.radius);
}

inline bool Box3::intersectsPlane(const Plane& plane) const {
  // We compute the minimum and maximum dot product values. If those values
  // are on the same side (back or front) of the plane, then there is no intersection.
  double min_, max_;
  if (plane.normal.x > 0) {
    min_ = plane.normal.x * min.x;
    max_ = plane.normal.x * max.x;
  } else {
    min_ = plane.normal.x * max.x;
    max_ = plane.normal.x * min.x;
  }
  if (plane.normal.y > 0) {
    min_ += plane.normal.y * min.y;
    max_ += plane.normal.y * max.y;
  } else {
    min_ += plane.normal.y * max.y;
    max_ += plane.normal.y * min.y;
  }
  if (plane.normal.z > 0) {
    min_ += plane.normal.z * min.z;
    max_ += plane.normal.z * max.z;
  } else {
    min_ += plane.normal.z * max.z;
    max_ += plane.normal.z * min.z;
  }
  return (min_ <= -plane.constant && max_ >= -plane.constant);
}

namespace mathdetail {
inline bool satForAxes(const double* axes, int count, const Vector3& v0, const Vector3& v1, const Vector3& v2, const Vector3& extents) {
  Vector3 testAxis;
  for (int i = 0, j = count - 3; i <= j; i += 3) {
    testAxis.fromArray(axes, static_cast<size_t>(i));
    // project the aabb onto the separating axis
    const double r = extents.x * std::fabs(testAxis.x) + extents.y * std::fabs(testAxis.y) + extents.z * std::fabs(testAxis.z);
    // project all 3 vertices of the triangle onto the separating axis
    const double p0 = v0.dot(testAxis);
    const double p1 = v1.dot(testAxis);
    const double p2 = v2.dot(testAxis);
    // actual test, basically see if either of the most extreme of the triangle points intersects r
    if (jsMax(-max3(p0, p1, p2), min3(p0, p1, p2)) > r) {
      // points of the projected triangle are outside the projected half-length of the aabb
      // the axis is separating and we can exit
      return false;
    }
  }
  return true;
}
}  // namespace mathdetail

inline bool Box3::intersectsTriangle(const Triangle& triangle) const {
  if (isEmpty()) return false;
  // compute box center and extents
  Vector3 center, extents, v0, v1, v2, f0, f1, f2, triangleNormal;
  getCenter(center);
  extents.subVectors(max, center);
  // translate triangle to aabb origin
  v0.subVectors(triangle.a, center);
  v1.subVectors(triangle.b, center);
  v2.subVectors(triangle.c, center);
  // compute edge vectors for triangle
  f0.subVectors(v1, v0);
  f1.subVectors(v2, v1);
  f2.subVectors(v0, v2);
  // test against axes that are given by cross product combinations of the edges of the triangle and the edges of the aabb
  // make an axis testing of each of the 3 sides of the aabb against each of the 3 sides of the triangle = 9 axis of separation
  // axis_ij = u_i x f_j (u0, u1, u2 = face normals of aabb = x,y,z axes vectors since aabb is axis aligned)
  const double axes9[27] = {0, -f0.z, f0.y, 0, -f1.z, f1.y, 0, -f2.z, f2.y,
                            f0.z, 0, -f0.x, f1.z, 0, -f1.x, f2.z, 0, -f2.x,
                            -f0.y, f0.x, 0, -f1.y, f1.x, 0, -f2.y, f2.x, 0};
  if (!mathdetail::satForAxes(axes9, 27, v0, v1, v2, extents)) return false;
  // test 3 face normals from the aabb
  const double axes3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  if (!mathdetail::satForAxes(axes3, 9, v0, v1, v2, extents)) return false;
  // finally testing the face normal of the triangle
  // use already existing triangle edge vectors here
  triangleNormal.crossVectors(f0, f1);
  const double axes1[3] = {triangleNormal.x, triangleNormal.y, triangleNormal.z};
  return mathdetail::satForAxes(axes1, 3, v0, v1, v2, extents);
}

inline Sphere& Box3::getBoundingSphere(Sphere& target) const {
  if (isEmpty()) {
    target.makeEmpty();
  } else {
    getCenter(target.center);
    Vector3 vector;
    target.radius = getSize(vector).length() * 0.5;
  }
  return target;
}

inline bool Sphere::intersectsPlane(const Plane& plane) const { return std::fabs(plane.distanceToPoint(center)) <= radius; }

}  // namespace fs
