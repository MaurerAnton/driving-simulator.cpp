// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Vector2, Vector3, Vector4, Quaternion, Euler, Matrix3, Matrix4 – three.js's, method
 * for method and operation for operation, so that the numbers come out the same.
 *
 * Origin: three.js r187 src/math/{Vector2,Vector3,Vector4,Quaternion,Euler,Matrix3,Matrix4}.js
 *
 * Conventions are three.js's: column-major matrices (`elements`, also readable as `e`),
 * right-handed, angles in radians.  Members are plain `double`s; a method that returns
 * `this` in JavaScript returns `*this` by reference.  Where JavaScript answers `null`
 * a pointer is answered here (null: none).
 */
#pragma once

#include "engine/math/mathutils.h"

namespace fs {

struct Vector3;
struct Vector4;
struct Quaternion;
struct Euler;
struct Matrix3;
struct Matrix4;
struct Color;
struct Spherical;

// ---------------------------------------------------------------------------
// Vector2
// ---------------------------------------------------------------------------

struct Vector2 {
  double x = 0, y = 0;

  Vector2() = default;
  Vector2(double x_, double y_) : x(x_), y(y_) {}

  /** `v.width` / `v.height`: other names of x and y (set with a call: v.width(3)) */
  double width() const { return x; }
  double height() const { return y; }
  void width(double value) { x = value; }
  void height(double value) { y = value; }

  Vector2& set(double x_, double y_) { x = x_; y = y_; return *this; }
  Vector2& setScalar(double scalar) { x = scalar; y = scalar; return *this; }
  Vector2& setX(double x_) { x = x_; return *this; }
  Vector2& setY(double y_) { y = y_; return *this; }
  Vector2& setComponent(int index, double value) {
    if (index == 0) x = value; else if (index == 1) y = value;
    return *this;
  }
  double getComponent(int index) const { return index == 0 ? x : index == 1 ? y : JS_NAN; }
  Vector2 clone() const { return *this; }
  Vector2& copy(const Vector2& v) { x = v.x; y = v.y; return *this; }

  Vector2& add(const Vector2& v) { x += v.x; y += v.y; return *this; }
  Vector2& addScalar(double s) { x += s; y += s; return *this; }
  Vector2& addVectors(const Vector2& a, const Vector2& b) { x = a.x + b.x; y = a.y + b.y; return *this; }
  Vector2& addScaledVector(const Vector2& v, double s) { x += v.x * s; y += v.y * s; return *this; }
  Vector2& sub(const Vector2& v) { x -= v.x; y -= v.y; return *this; }
  Vector2& subScalar(double s) { x -= s; y -= s; return *this; }
  Vector2& subVectors(const Vector2& a, const Vector2& b) { x = a.x - b.x; y = a.y - b.y; return *this; }
  Vector2& multiply(const Vector2& v) { x *= v.x; y *= v.y; return *this; }
  Vector2& multiplyScalar(double scalar) { x *= scalar; y *= scalar; return *this; }
  Vector2& divide(const Vector2& v) { x /= v.x; y /= v.y; return *this; }
  Vector2& divideScalar(double scalar) { return multiplyScalar(1 / scalar); }
  inline Vector2& applyMatrix3(const Matrix3& m);

  Vector2& min(const Vector2& v) { x = jsMin(x, v.x); y = jsMin(y, v.y); return *this; }
  Vector2& max(const Vector2& v) { x = jsMax(x, v.x); y = jsMax(y, v.y); return *this; }
  Vector2& clamp(const Vector2& min, const Vector2& max) {
    x = MathUtils::clamp(x, min.x, max.x);
    y = MathUtils::clamp(y, min.y, max.y);
    return *this;
  }
  Vector2& clampScalar(double minVal, double maxVal) {
    x = MathUtils::clamp(x, minVal, maxVal);
    y = MathUtils::clamp(y, minVal, maxVal);
    return *this;
  }
  Vector2& clampLength(double min, double max) {
    const double length = this->length();
    return divideScalar(mathdetail::orOne(length)).multiplyScalar(MathUtils::clamp(length, min, max));
  }
  Vector2& floor() { x = std::floor(x); y = std::floor(y); return *this; }
  Vector2& ceil() { x = std::ceil(x); y = std::ceil(y); return *this; }
  Vector2& round() { x = jsRound(x); y = jsRound(y); return *this; }
  Vector2& roundToZero() { x = std::trunc(x); y = std::trunc(y); return *this; }
  Vector2& negate() { x = -x; y = -y; return *this; }

  double dot(const Vector2& v) const { return x * v.x + y * v.y; }
  /** the z of the 3D cross product */
  double cross(const Vector2& v) const { return x * v.y - y * v.x; }
  double lengthSq() const { return x * x + y * y; }
  double length() const { return std::sqrt(x * x + y * y); }
  double manhattanLength() const { return std::fabs(x) + std::fabs(y); }
  Vector2& normalize() { return divideScalar(mathdetail::orOne(length())); }
  /** the angle to the positive x axis, 0 … 2π */
  double angle() const { return std::atan2(-y, -x) + PI; }
  double angleTo(const Vector2& v) const {
    const double denominator = std::sqrt(lengthSq() * v.lengthSq());
    if (denominator == 0) return PI / 2;
    const double theta = dot(v) / denominator;
    return std::acos(MathUtils::clamp(theta, -1, 1));
  }
  double distanceTo(const Vector2& v) const { return std::sqrt(distanceToSquared(v)); }
  double distanceToSquared(const Vector2& v) const {
    const double dx = x - v.x, dy = y - v.y;
    return dx * dx + dy * dy;
  }
  double manhattanDistanceTo(const Vector2& v) const { return std::fabs(x - v.x) + std::fabs(y - v.y); }
  Vector2& setLength(double length) { return normalize().multiplyScalar(length); }
  Vector2& lerp(const Vector2& v, double alpha) {
    x += (v.x - x) * alpha;
    y += (v.y - y) * alpha;
    return *this;
  }
  Vector2& lerpVectors(const Vector2& v1, const Vector2& v2, double alpha) {
    x = v1.x + (v2.x - v1.x) * alpha;
    y = v1.y + (v2.y - v1.y) * alpha;
    return *this;
  }
  bool equals(const Vector2& v) const { return v.x == x && v.y == y; }
  template <class A>
  Vector2& fromArray(const A& array, size_t offset = 0) {
    x = array[offset];
    y = array[offset + 1];
    return *this;
  }
  std::array<double, 2> toArray() const { return {x, y}; }
  template <class A>
  A& toArray(A& array, size_t offset = 0) const {
    array[offset] = x;
    array[offset + 1] = y;
    return array;
  }
  template <class A>
  Vector2& fromBufferAttribute(const A& attribute, size_t index) {
    x = attribute.getX(index);
    y = attribute.getY(index);
    return *this;
  }
  Vector2& rotateAround(const Vector2& center, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    const double x_ = x - center.x;
    const double y_ = y - center.y;
    x = x_ * c - y_ * s + center.x;
    y = x_ * s + y_ * c + center.y;
    return *this;
  }
  Vector2& random() { x = rnd(); y = rnd(); return *this; }

  bool operator==(const Vector2& o) const { return x == o.x && y == o.y; }
  bool operator!=(const Vector2& o) const { return !(*this == o); }
  // sugar for the C++ side: a + b, a - b, v * s
  Vector2 operator+(const Vector2& o) const { return Vector2(x + o.x, y + o.y); }
  Vector2 operator-(const Vector2& o) const { return Vector2(x - o.x, y - o.y); }
  Vector2 operator*(double s) const { return Vector2(x * s, y * s); }
};

inline Vector2 operator*(double s, const Vector2& v) { return v * s; }

// ---------------------------------------------------------------------------
// Vector3
// ---------------------------------------------------------------------------

struct Vector3 {
  double x = 0, y = 0, z = 0;

  Vector3() = default;
  Vector3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

  Vector3& set(double x_, double y_, double z_) { x = x_; y = y_; z = z_; return *this; }
  /** set(x, y): z stays (three.js: sprite.scale.set(x, y)) */
  Vector3& set(double x_, double y_) { x = x_; y = y_; return *this; }
  Vector3& setScalar(double scalar) { x = scalar; y = scalar; z = scalar; return *this; }
  Vector3& setX(double x_) { x = x_; return *this; }
  Vector3& setY(double y_) { y = y_; return *this; }
  Vector3& setZ(double z_) { z = z_; return *this; }
  Vector3& setComponent(int index, double value) {
    if (index == 0) x = value; else if (index == 1) y = value; else if (index == 2) z = value;
    return *this;
  }
  double getComponent(int index) const { return index == 0 ? x : index == 1 ? y : index == 2 ? z : JS_NAN; }
  Vector3 clone() const { return *this; }
  Vector3& copy(const Vector3& v) { x = v.x; y = v.y; z = v.z; return *this; }

  Vector3& add(const Vector3& v) { x += v.x; y += v.y; z += v.z; return *this; }
  Vector3& addScalar(double s) { x += s; y += s; z += s; return *this; }
  Vector3& addVectors(const Vector3& a, const Vector3& b) { x = a.x + b.x; y = a.y + b.y; z = a.z + b.z; return *this; }
  Vector3& addScaledVector(const Vector3& v, double s) { x += v.x * s; y += v.y * s; z += v.z * s; return *this; }
  Vector3& sub(const Vector3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
  Vector3& subScalar(double s) { x -= s; y -= s; z -= s; return *this; }
  Vector3& subVectors(const Vector3& a, const Vector3& b) { x = a.x - b.x; y = a.y - b.y; z = a.z - b.z; return *this; }
  Vector3& multiply(const Vector3& v) { x *= v.x; y *= v.y; z *= v.z; return *this; }
  Vector3& multiplyScalar(double scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
  Vector3& multiplyVectors(const Vector3& a, const Vector3& b) { x = a.x * b.x; y = a.y * b.y; z = a.z * b.z; return *this; }
  inline Vector3& applyEuler(const Euler& euler);
  inline Vector3& applyAxisAngle(const Vector3& axis, double angle);
  inline Vector3& applyMatrix3(const Matrix3& m);
  inline Vector3& applyNormalMatrix(const Matrix3& m);
  inline Vector3& applyMatrix4(const Matrix4& m);
  inline Vector3& applyQuaternion(const Quaternion& q);
  /** `v.project(camera)` / `v.unproject(camera)`: world ↔ the camera's picture (-1 … 1) */
  template <class C>
  Vector3& project(const C& camera) { return applyMatrix4(camera.matrixWorldInverse).applyMatrix4(camera.projectionMatrix); }
  template <class C>
  Vector3& unproject(const C& camera) { return applyMatrix4(camera.projectionMatrixInverse).applyMatrix4(camera.matrixWorld); }
  /** a direction through the matrix's upper 3×3 (no translation), normalised */
  inline Vector3& transformDirection(const Matrix4& m);
  Vector3& divide(const Vector3& v) { x /= v.x; y /= v.y; z /= v.z; return *this; }
  Vector3& divideScalar(double scalar) { return multiplyScalar(1 / scalar); }

  Vector3& min(const Vector3& v) { x = jsMin(x, v.x); y = jsMin(y, v.y); z = jsMin(z, v.z); return *this; }
  Vector3& max(const Vector3& v) { x = jsMax(x, v.x); y = jsMax(y, v.y); z = jsMax(z, v.z); return *this; }
  Vector3& clamp(const Vector3& min, const Vector3& max) {
    x = MathUtils::clamp(x, min.x, max.x);
    y = MathUtils::clamp(y, min.y, max.y);
    z = MathUtils::clamp(z, min.z, max.z);
    return *this;
  }
  Vector3& clampScalar(double minVal, double maxVal) {
    x = MathUtils::clamp(x, minVal, maxVal);
    y = MathUtils::clamp(y, minVal, maxVal);
    z = MathUtils::clamp(z, minVal, maxVal);
    return *this;
  }
  Vector3& clampLength(double min, double max) {
    const double length = this->length();
    return divideScalar(mathdetail::orOne(length)).multiplyScalar(MathUtils::clamp(length, min, max));
  }
  Vector3& floor() { x = std::floor(x); y = std::floor(y); z = std::floor(z); return *this; }
  Vector3& ceil() { x = std::ceil(x); y = std::ceil(y); z = std::ceil(z); return *this; }
  Vector3& round() { x = jsRound(x); y = jsRound(y); z = jsRound(z); return *this; }
  Vector3& roundToZero() { x = std::trunc(x); y = std::trunc(y); z = std::trunc(z); return *this; }
  Vector3& negate() { x = -x; y = -y; z = -z; return *this; }

  double dot(const Vector3& v) const { return x * v.x + y * v.y + z * v.z; }
  double lengthSq() const { return x * x + y * y + z * z; }
  double length() const { return std::sqrt(x * x + y * y + z * z); }
  double manhattanLength() const { return std::fabs(x) + std::fabs(y) + std::fabs(z); }
  Vector3& normalize() { return divideScalar(mathdetail::orOne(length())); }
  Vector3& setLength(double length) { return normalize().multiplyScalar(length); }
  Vector3& lerp(const Vector3& v, double alpha) {
    x += (v.x - x) * alpha;
    y += (v.y - y) * alpha;
    z += (v.z - z) * alpha;
    return *this;
  }
  Vector3& lerpVectors(const Vector3& v1, const Vector3& v2, double alpha) {
    x = v1.x + (v2.x - v1.x) * alpha;
    y = v1.y + (v2.y - v1.y) * alpha;
    z = v1.z + (v2.z - v1.z) * alpha;
    return *this;
  }
  /** this = this × v */
  Vector3& cross(const Vector3& v) { return crossVectors(*this, v); }
  Vector3& crossVectors(const Vector3& a, const Vector3& b) {
    const double ax = a.x, ay = a.y, az = a.z;
    const double bx = b.x, by = b.y, bz = b.z;
    x = ay * bz - az * by;
    y = az * bx - ax * bz;
    z = ax * by - ay * bx;
    return *this;
  }
  Vector3& projectOnVector(const Vector3& v) {
    const double denominator = v.lengthSq();
    if (denominator == 0) return set(0, 0, 0);
    const double scalar = v.dot(*this) / denominator;
    return copy(v).multiplyScalar(scalar);
  }
  Vector3& projectOnPlane(const Vector3& planeNormal) {
    Vector3 vector = *this;
    vector.projectOnVector(planeNormal);
    return sub(vector);
  }
  /** mirrored at the plane with that (normalised) normal */
  Vector3& reflect(const Vector3& normal) {
    Vector3 vector = normal;
    return sub(vector.multiplyScalar(2 * dot(normal)));
  }
  double angleTo(const Vector3& v) const {
    const double denominator = std::sqrt(lengthSq() * v.lengthSq());
    if (denominator == 0) return PI / 2;
    const double theta = dot(v) / denominator;
    return std::acos(MathUtils::clamp(theta, -1, 1));
  }
  double distanceTo(const Vector3& v) const { return std::sqrt(distanceToSquared(v)); }
  double distanceToSquared(const Vector3& v) const {
    const double dx = x - v.x, dy = y - v.y, dz = z - v.z;
    return dx * dx + dy * dy + dz * dz;
  }
  double manhattanDistanceTo(const Vector3& v) const { return std::fabs(x - v.x) + std::fabs(y - v.y) + std::fabs(z - v.z); }
  inline Vector3& setFromSpherical(const Spherical& s);
  Vector3& setFromSphericalCoords(double radius, double phi, double theta) {
    const double sinPhiRadius = std::sin(phi) * radius;
    x = sinPhiRadius * std::sin(theta);
    y = std::cos(phi) * radius;
    z = sinPhiRadius * std::cos(theta);
    return *this;
  }
  Vector3& setFromCylindricalCoords(double radius, double theta, double y_) {
    x = radius * std::sin(theta);
    y = y_;
    z = radius * std::cos(theta);
    return *this;
  }
  inline Vector3& setFromMatrixPosition(const Matrix4& m);
  inline Vector3& setFromMatrixScale(const Matrix4& m);
  inline Vector3& setFromMatrixColumn(const Matrix4& m, int index);
  inline Vector3& setFromMatrix3Column(const Matrix3& m, int index);
  inline Vector3& setFromEuler(const Euler& e);
  inline Vector3& setFromColor(const Color& c);
  bool equals(const Vector3& v) const { return v.x == x && v.y == y && v.z == z; }
  template <class A>
  Vector3& fromArray(const A& array, size_t offset = 0) {
    x = array[offset];
    y = array[offset + 1];
    z = array[offset + 2];
    return *this;
  }
  std::array<double, 3> toArray() const { return {x, y, z}; }
  template <class A>
  A& toArray(A& array, size_t offset = 0) const {
    array[offset] = x;
    array[offset + 1] = y;
    array[offset + 2] = z;
    return array;
  }
  template <class A>
  Vector3& fromBufferAttribute(const A& attribute, size_t index) {
    x = attribute.getX(index);
    y = attribute.getY(index);
    z = attribute.getZ(index);
    return *this;
  }
  Vector3& random() { x = rnd(); y = rnd(); z = rnd(); return *this; }
  Vector3& randomDirection() {
    const double theta = rnd() * PI * 2;
    const double u = rnd() * 2 - 1;
    const double c = std::sqrt(1 - u * u);
    x = c * std::cos(theta);
    y = u;
    z = c * std::sin(theta);
    return *this;
  }

  bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator!=(const Vector3& o) const { return !(*this == o); }
  // sugar for the C++ side: a + b, a - b, v * s
  Vector3 operator*(double s) const { return Vector3(x * s, y * s, z * s); }
  Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
  Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
};

inline Vector3 operator*(double s, const Vector3& v) { return v * s; }

// ---------------------------------------------------------------------------
// Vector4
// ---------------------------------------------------------------------------

struct Vector4 {
  double x = 0, y = 0, z = 0, w = 1;

  Vector4() = default;
  Vector4(double x_, double y_, double z_, double w_ = 1) : x(x_), y(y_), z(z_), w(w_) {}

  /** `v.width` / `v.height`: other names of z and w (a viewport: x, y, width, height) */
  double width() const { return z; }
  double height() const { return w; }
  void width(double value) { z = value; }
  void height(double value) { w = value; }

  Vector4& set(double x_, double y_, double z_, double w_) { x = x_; y = y_; z = z_; w = w_; return *this; }
  Vector4& setScalar(double scalar) { x = scalar; y = scalar; z = scalar; w = scalar; return *this; }
  Vector4& setX(double x_) { x = x_; return *this; }
  Vector4& setY(double y_) { y = y_; return *this; }
  Vector4& setZ(double z_) { z = z_; return *this; }
  Vector4& setW(double w_) { w = w_; return *this; }
  Vector4& setComponent(int index, double value) {
    if (index == 0) x = value; else if (index == 1) y = value; else if (index == 2) z = value; else if (index == 3) w = value;
    return *this;
  }
  double getComponent(int index) const { return index == 0 ? x : index == 1 ? y : index == 2 ? z : index == 3 ? w : JS_NAN; }
  Vector4 clone() const { return *this; }
  Vector4& copy(const Vector4& v) { x = v.x; y = v.y; z = v.z; w = v.w; return *this; }
  /** copy(a Vector3): w becomes 1, as three.js does for a vector without w */
  inline Vector4& copy(const Vector3& v);

  Vector4& add(const Vector4& v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
  Vector4& addScalar(double s) { x += s; y += s; z += s; w += s; return *this; }
  Vector4& addVectors(const Vector4& a, const Vector4& b) { x = a.x + b.x; y = a.y + b.y; z = a.z + b.z; w = a.w + b.w; return *this; }
  Vector4& addScaledVector(const Vector4& v, double s) { x += v.x * s; y += v.y * s; z += v.z * s; w += v.w * s; return *this; }
  Vector4& sub(const Vector4& v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
  Vector4& subScalar(double s) { x -= s; y -= s; z -= s; w -= s; return *this; }
  Vector4& subVectors(const Vector4& a, const Vector4& b) { x = a.x - b.x; y = a.y - b.y; z = a.z - b.z; w = a.w - b.w; return *this; }
  Vector4& multiply(const Vector4& v) { x *= v.x; y *= v.y; z *= v.z; w *= v.w; return *this; }
  Vector4& multiplyScalar(double scalar) { x *= scalar; y *= scalar; z *= scalar; w *= scalar; return *this; }
  inline Vector4& applyMatrix4(const Matrix4& m);
  Vector4& divide(const Vector4& v) { x /= v.x; y /= v.y; z /= v.z; w /= v.w; return *this; }
  Vector4& divideScalar(double scalar) { return multiplyScalar(1 / scalar); }
  /** x, y, z: the axis; w: the angle */
  inline Vector4& setAxisAngleFromQuaternion(const Quaternion& q);
  inline Vector4& setAxisAngleFromRotationMatrix(const Matrix4& m);
  inline Vector4& setFromMatrixPosition(const Matrix4& m);

  Vector4& min(const Vector4& v) { x = jsMin(x, v.x); y = jsMin(y, v.y); z = jsMin(z, v.z); w = jsMin(w, v.w); return *this; }
  Vector4& max(const Vector4& v) { x = jsMax(x, v.x); y = jsMax(y, v.y); z = jsMax(z, v.z); w = jsMax(w, v.w); return *this; }
  Vector4& clamp(const Vector4& min, const Vector4& max) {
    x = MathUtils::clamp(x, min.x, max.x);
    y = MathUtils::clamp(y, min.y, max.y);
    z = MathUtils::clamp(z, min.z, max.z);
    w = MathUtils::clamp(w, min.w, max.w);
    return *this;
  }
  Vector4& clampScalar(double minVal, double maxVal) {
    x = MathUtils::clamp(x, minVal, maxVal);
    y = MathUtils::clamp(y, minVal, maxVal);
    z = MathUtils::clamp(z, minVal, maxVal);
    w = MathUtils::clamp(w, minVal, maxVal);
    return *this;
  }
  Vector4& clampLength(double min, double max) {
    const double length = this->length();
    return divideScalar(mathdetail::orOne(length)).multiplyScalar(MathUtils::clamp(length, min, max));
  }
  Vector4& floor() { x = std::floor(x); y = std::floor(y); z = std::floor(z); w = std::floor(w); return *this; }
  Vector4& ceil() { x = std::ceil(x); y = std::ceil(y); z = std::ceil(z); w = std::ceil(w); return *this; }
  Vector4& round() { x = jsRound(x); y = jsRound(y); z = jsRound(z); w = jsRound(w); return *this; }
  Vector4& roundToZero() { x = std::trunc(x); y = std::trunc(y); z = std::trunc(z); w = std::trunc(w); return *this; }
  Vector4& negate() { x = -x; y = -y; z = -z; w = -w; return *this; }

  double dot(const Vector4& v) const { return x * v.x + y * v.y + z * v.z + w * v.w; }
  double lengthSq() const { return x * x + y * y + z * z + w * w; }
  double length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
  double manhattanLength() const { return std::fabs(x) + std::fabs(y) + std::fabs(z) + std::fabs(w); }
  Vector4& normalize() { return divideScalar(mathdetail::orOne(length())); }
  Vector4& setLength(double length) { return normalize().multiplyScalar(length); }
  Vector4& lerp(const Vector4& v, double alpha) {
    x += (v.x - x) * alpha;
    y += (v.y - y) * alpha;
    z += (v.z - z) * alpha;
    w += (v.w - w) * alpha;
    return *this;
  }
  Vector4& lerpVectors(const Vector4& v1, const Vector4& v2, double alpha) {
    x = v1.x + (v2.x - v1.x) * alpha;
    y = v1.y + (v2.y - v1.y) * alpha;
    z = v1.z + (v2.z - v1.z) * alpha;
    w = v1.w + (v2.w - v1.w) * alpha;
    return *this;
  }
  bool equals(const Vector4& v) const { return v.x == x && v.y == y && v.z == z && v.w == w; }
  template <class A>
  Vector4& fromArray(const A& array, size_t offset = 0) {
    x = array[offset];
    y = array[offset + 1];
    z = array[offset + 2];
    w = array[offset + 3];
    return *this;
  }
  std::array<double, 4> toArray() const { return {x, y, z, w}; }
  template <class A>
  A& toArray(A& array, size_t offset = 0) const {
    array[offset] = x;
    array[offset + 1] = y;
    array[offset + 2] = z;
    array[offset + 3] = w;
    return array;
  }
  template <class A>
  Vector4& fromBufferAttribute(const A& attribute, size_t index) {
    x = attribute.getX(index);
    y = attribute.getY(index);
    z = attribute.getZ(index);
    w = attribute.getW(index);
    return *this;
  }
  Vector4& random() { x = rnd(); y = rnd(); z = rnd(); w = rnd(); return *this; }

  bool operator==(const Vector4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
  bool operator!=(const Vector4& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Euler: three angles and the order they are applied in (three.js's default: XYZ)
// ---------------------------------------------------------------------------

struct Euler {
  /** `'XYZ'`, `'YXZ'` … (the game: YXZ for cameras and heads, ZXY and ZYX in places) */
  enum Order { XYZ, YXZ, ZXY, ZYX, YZX, XZY };
  static constexpr Order DEFAULT_ORDER = XYZ;

  double x = 0, y = 0, z = 0;
  Order order = XYZ;

  Euler() = default;
  Euler(double x_, double y_, double z_, Order o = XYZ) : x(x_), y(y_), z(z_), order(o) {}

  /** set(x, y, z): the order stays */
  Euler& set(double x_, double y_, double z_) { x = x_; y = y_; z = z_; return *this; }
  Euler& set(double x_, double y_, double z_, Order o) { x = x_; y = y_; z = z_; order = o; return *this; }
  Euler clone() const { return *this; }
  Euler& copy(const Euler& e) { x = e.x; y = e.y; z = e.z; order = e.order; return *this; }
  /** from a pure rotation matrix (its upper 3×3 unscaled), in this order or the one given */
  inline Euler& setFromRotationMatrix(const Matrix4& m);
  inline Euler& setFromRotationMatrix(const Matrix4& m, Order o);
  inline Euler& setFromQuaternion(const Quaternion& q);
  inline Euler& setFromQuaternion(const Quaternion& q, Order o);
  inline Euler& setFromVector3(const Vector3& v);
  inline Euler& setFromVector3(const Vector3& v, Order o);
  /** the same rotation with another order (loses what the angles held beyond one turn) */
  inline Euler& reorder(Order newOrder);
  bool equals(const Euler& e) const { return e.x == x && e.y == y && e.z == z && e.order == order; }
  /** the order's name ('XYZ' …) and back */
  static const char* orderName(Order o) {
    static const char* const names[6] = {"XYZ", "YXZ", "ZXY", "ZYX", "YZX", "XZY"};
    return names[o];
  }

  bool operator==(const Euler& o) const { return equals(o); }
  bool operator!=(const Euler& o) const { return !equals(o); }
};

// ---------------------------------------------------------------------------
// Quaternion
// ---------------------------------------------------------------------------

struct Quaternion {
  double x = 0, y = 0, z = 0, w = 1;

  Quaternion() = default;
  Quaternion(double x_, double y_, double z_, double w_) : x(x_), y(y_), z(z_), w(w_) {}

  /** dst[dstOffset…] = slerp(src0[srcOffset0…], src1[srcOffset1…], t) on plain arrays */
  template <class D, class S0, class S1>
  static void slerpFlat(D& dst, size_t dstOffset, const S0& src0, size_t srcOffset0, const S1& src1, size_t srcOffset1, double t) {
    double x0 = src0[srcOffset0 + 0], y0 = src0[srcOffset0 + 1], z0 = src0[srcOffset0 + 2], w0 = src0[srcOffset0 + 3];
    double x1 = src1[srcOffset1 + 0], y1 = src1[srcOffset1 + 1], z1 = src1[srcOffset1 + 2], w1 = src1[srcOffset1 + 3];
    if (w0 != w1 || x0 != x1 || y0 != y1 || z0 != z1) {
      double dot = x0 * x1 + y0 * y1 + z0 * z1 + w0 * w1;
      if (dot < 0) {
        x1 = -x1;
        y1 = -y1;
        z1 = -z1;
        w1 = -w1;
        dot = -dot;
      }
      double s = 1 - t;
      if (dot < 0.9995) {
        // slerp
        const double theta = std::acos(dot);
        const double sin = std::sin(theta);
        s = std::sin(s * theta) / sin;
        t = std::sin(t * theta) / sin;
        x0 = x0 * s + x1 * t;
        y0 = y0 * s + y1 * t;
        z0 = z0 * s + z1 * t;
        w0 = w0 * s + w1 * t;
      } else {
        // for small angles, lerp then normalize
        x0 = x0 * s + x1 * t;
        y0 = y0 * s + y1 * t;
        z0 = z0 * s + z1 * t;
        w0 = w0 * s + w1 * t;
        const double f = 1 / std::sqrt(x0 * x0 + y0 * y0 + z0 * z0 + w0 * w0);
        x0 *= f;
        y0 *= f;
        z0 *= f;
        w0 *= f;
      }
    }
    dst[dstOffset] = x0;
    dst[dstOffset + 1] = y0;
    dst[dstOffset + 2] = z0;
    dst[dstOffset + 3] = w0;
  }
  template <class D, class S0, class S1>
  static D& multiplyQuaternionsFlat(D& dst, size_t dstOffset, const S0& src0, size_t srcOffset0, const S1& src1, size_t srcOffset1) {
    const double x0 = src0[srcOffset0], y0 = src0[srcOffset0 + 1], z0 = src0[srcOffset0 + 2], w0 = src0[srcOffset0 + 3];
    const double x1 = src1[srcOffset1], y1 = src1[srcOffset1 + 1], z1 = src1[srcOffset1 + 2], w1 = src1[srcOffset1 + 3];
    dst[dstOffset] = x0 * w1 + w0 * x1 + y0 * z1 - z0 * y1;
    dst[dstOffset + 1] = y0 * w1 + w0 * y1 + z0 * x1 - x0 * z1;
    dst[dstOffset + 2] = z0 * w1 + w0 * z1 + x0 * y1 - y0 * x1;
    dst[dstOffset + 3] = w0 * w1 - x0 * x1 - y0 * y1 - z0 * z1;
    return dst;
  }

  Quaternion& set(double x_, double y_, double z_, double w_) { x = x_; y = y_; z = z_; w = w_; return *this; }
  Quaternion clone() const { return *this; }
  Quaternion& copy(const Quaternion& q) { x = q.x; y = q.y; z = q.z; w = q.w; return *this; }
  inline Quaternion& setFromEuler(const Euler& euler);
  /** the axis must be normalised */
  Quaternion& setFromAxisAngle(const Vector3& axis, double angle) {
    const double halfAngle = angle / 2, s = std::sin(halfAngle);
    x = axis.x * s;
    y = axis.y * s;
    z = axis.z * s;
    w = std::cos(halfAngle);
    return *this;
  }
  /** from a pure rotation matrix (its upper 3×3 unscaled) */
  inline Quaternion& setFromRotationMatrix(const Matrix4& m);
  /** the rotation that takes the direction vFrom to vTo (both normalised) */
  Quaternion& setFromUnitVectors(const Vector3& vFrom, const Vector3& vTo) {
    double r = vFrom.dot(vTo) + 1;
    if (r < 1e-8) {   // the epsilon value has been discussed in #31286
      // vFrom and vTo point in opposite directions
      r = 0;
      if (std::fabs(vFrom.x) > std::fabs(vFrom.z)) {
        x = -vFrom.y;
        y = vFrom.x;
        z = 0;
        w = r;
      } else {
        x = 0;
        y = -vFrom.z;
        z = vFrom.y;
        w = r;
      }
    } else {
      // crossVectors( vFrom, vTo ); // inlined to avoid cyclic dependency on Vector3
      x = vFrom.y * vTo.z - vFrom.z * vTo.y;
      y = vFrom.z * vTo.x - vFrom.x * vTo.z;
      z = vFrom.x * vTo.y - vFrom.y * vTo.x;
      w = r;
    }
    return normalize();
  }
  double angleTo(const Quaternion& q) const { return 2 * std::acos(std::fabs(MathUtils::clamp(dot(q), -1, 1))); }
  Quaternion& rotateTowards(const Quaternion& q, double step) {
    const double angle = angleTo(q);
    if (angle == 0) return *this;
    const double t = jsMin(1, step / angle);
    slerp(q, t);
    return *this;
  }
  Quaternion& identity() { return set(0, 0, 0, 1); }
  /** the opposite rotation (the quaternion is taken to have unit length) */
  Quaternion& invert() { return conjugate(); }
  Quaternion& conjugate() {
    x *= -1;
    y *= -1;
    z *= -1;
    return *this;
  }
  double dot(const Quaternion& v) const { return x * v.x + y * v.y + z * v.z + w * v.w; }
  double lengthSq() const { return x * x + y * y + z * z + w * w; }
  double length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
  Quaternion& normalize() {
    double l = length();
    if (l == 0) {
      x = 0;
      y = 0;
      z = 0;
      w = 1;
    } else {
      l = 1 / l;
      x = x * l;
      y = y * l;
      z = z * l;
      w = w * l;
    }
    return *this;
  }
  Quaternion& multiply(const Quaternion& q) { return multiplyQuaternions(*this, q); }
  Quaternion& premultiply(const Quaternion& q) { return multiplyQuaternions(q, *this); }
  Quaternion& multiplyQuaternions(const Quaternion& a, const Quaternion& b) {
    // from http://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/code/index.htm
    const double qax = a.x, qay = a.y, qaz = a.z, qaw = a.w;
    const double qbx = b.x, qby = b.y, qbz = b.z, qbw = b.w;
    x = qax * qbw + qaw * qbx + qay * qbz - qaz * qby;
    y = qay * qbw + qaw * qby + qaz * qbx - qax * qbz;
    z = qaz * qbw + qaw * qbz + qax * qby - qay * qbx;
    w = qaw * qbw - qax * qbx - qay * qby - qaz * qbz;
    return *this;
  }
  Quaternion& slerp(const Quaternion& qb, double t) {
    double x_ = qb.x, y_ = qb.y, z_ = qb.z, w_ = qb.w;
    double dot = this->dot(qb);
    if (dot < 0) {
      x_ = -x_;
      y_ = -y_;
      z_ = -z_;
      w_ = -w_;
      dot = -dot;
    }
    double s = 1 - t;
    if (dot < 0.9995) {
      // slerp
      const double theta = std::acos(dot);
      const double sin = std::sin(theta);
      s = std::sin(s * theta) / sin;
      t = std::sin(t * theta) / sin;
      x = x * s + x_ * t;
      y = y * s + y_ * t;
      z = z * s + z_ * t;
      w = w * s + w_ * t;
    } else {
      // for small angles, lerp then normalize
      x = x * s + x_ * t;
      y = y * s + y_ * t;
      z = z * s + z_ * t;
      w = w * s + w_ * t;
      normalize();
    }
    return *this;
  }
  Quaternion& slerpQuaternions(const Quaternion& qa, const Quaternion& qb, double t) { return copy(qa).slerp(qb, t); }
  /** a uniformly drawn rotation */
  Quaternion& random() {
    // Ken Shoemake: Uniform random rotations. In D. Kirk, editor, Graphics Gems III, pages 124-132. Academic Press, New York, 1992.
    const double theta1 = 2 * PI * rnd();
    const double theta2 = 2 * PI * rnd();
    const double x0 = rnd();
    const double r1 = std::sqrt(1 - x0);
    const double r2 = std::sqrt(x0);
    return set(r1 * std::sin(theta1), r1 * std::cos(theta1), r2 * std::sin(theta2), r2 * std::cos(theta2));
  }
  bool equals(const Quaternion& q) const { return q.x == x && q.y == y && q.z == z && q.w == w; }
  template <class A>
  Quaternion& fromArray(const A& array, size_t offset = 0) {
    x = array[offset];
    y = array[offset + 1];
    z = array[offset + 2];
    w = array[offset + 3];
    return *this;
  }
  std::array<double, 4> toArray() const { return {x, y, z, w}; }
  template <class A>
  A& toArray(A& array, size_t offset = 0) const {
    array[offset] = x;
    array[offset + 1] = y;
    array[offset + 2] = z;
    array[offset + 3] = w;
    return array;
  }
  template <class A>
  Quaternion& fromBufferAttribute(const A& attribute, size_t index) {
    x = attribute.getX(index);
    y = attribute.getY(index);
    z = attribute.getZ(index);
    w = attribute.getW(index);
    return *this;
  }

  bool operator==(const Quaternion& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
  bool operator!=(const Quaternion& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Matrix3 (column-major: elements[0..2] is the first column)
// ---------------------------------------------------------------------------

struct Matrix3 {
  union {
    double elements[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    /** the same numbers by the draft's name */
    double e[9];
  };

  Matrix3() = default;
  /** by rows, as it is written on paper */
  Matrix3(double n11, double n12, double n13, double n21, double n22, double n23, double n31, double n32, double n33) {
    set(n11, n12, n13, n21, n22, n23, n31, n32, n33);
  }

  Matrix3& set(double n11, double n12, double n13, double n21, double n22, double n23, double n31, double n32, double n33) {
    double* te = elements;
    te[0] = n11; te[1] = n21; te[2] = n31;
    te[3] = n12; te[4] = n22; te[5] = n32;
    te[6] = n13; te[7] = n23; te[8] = n33;
    return *this;
  }
  Matrix3& identity() { return set(1, 0, 0, 0, 1, 0, 0, 0, 1); }
  Matrix3& copy(const Matrix3& m) {
    for (int i = 0; i < 9; i++) elements[i] = m.elements[i];
    return *this;
  }
  const Matrix3& extractBasis(Vector3& xAxis, Vector3& yAxis, Vector3& zAxis) const {
    xAxis.setFromMatrix3Column(*this, 0);
    yAxis.setFromMatrix3Column(*this, 1);
    zAxis.setFromMatrix3Column(*this, 2);
    return *this;
  }
  /** the upper left 3×3 of a Matrix4 */
  inline Matrix3& setFromMatrix4(const Matrix4& m);
  Matrix3& multiply(const Matrix3& m) { return multiplyMatrices(*this, m); }
  Matrix3& premultiply(const Matrix3& m) { return multiplyMatrices(m, *this); }
  Matrix3& multiplyMatrices(const Matrix3& a, const Matrix3& b) {
    const double* ae = a.elements;
    const double* be = b.elements;
    double* te = elements;
    const double a11 = ae[0], a12 = ae[3], a13 = ae[6];
    const double a21 = ae[1], a22 = ae[4], a23 = ae[7];
    const double a31 = ae[2], a32 = ae[5], a33 = ae[8];
    const double b11 = be[0], b12 = be[3], b13 = be[6];
    const double b21 = be[1], b22 = be[4], b23 = be[7];
    const double b31 = be[2], b32 = be[5], b33 = be[8];
    te[0] = a11 * b11 + a12 * b21 + a13 * b31;
    te[3] = a11 * b12 + a12 * b22 + a13 * b32;
    te[6] = a11 * b13 + a12 * b23 + a13 * b33;
    te[1] = a21 * b11 + a22 * b21 + a23 * b31;
    te[4] = a21 * b12 + a22 * b22 + a23 * b32;
    te[7] = a21 * b13 + a22 * b23 + a23 * b33;
    te[2] = a31 * b11 + a32 * b21 + a33 * b31;
    te[5] = a31 * b12 + a32 * b22 + a33 * b32;
    te[8] = a31 * b13 + a32 * b23 + a33 * b33;
    return *this;
  }
  Matrix3& multiplyScalar(double s) {
    double* te = elements;
    te[0] *= s; te[3] *= s; te[6] *= s;
    te[1] *= s; te[4] *= s; te[7] *= s;
    te[2] *= s; te[5] *= s; te[8] *= s;
    return *this;
  }
  double determinant() const {
    const double* te = elements;
    const double a = te[0], b = te[1], c = te[2], d = te[3], e_ = te[4], f = te[5], g = te[6], h = te[7], i = te[8];
    return a * e_ * i - a * f * h - b * d * i + b * f * g + c * d * h - c * e_ * g;
  }
  /** the inverse (all zeros if there is none) */
  Matrix3& invert() {
    double* te = elements;
    const double n11 = te[0], n21 = te[1], n31 = te[2], n12 = te[3], n22 = te[4], n32 = te[5], n13 = te[6], n23 = te[7], n33 = te[8],
                 t11 = n33 * n22 - n32 * n23, t12 = n32 * n13 - n33 * n12, t13 = n23 * n12 - n22 * n13,
                 det = n11 * t11 + n21 * t12 + n31 * t13;
    if (det == 0) return set(0, 0, 0, 0, 0, 0, 0, 0, 0);
    const double detInv = 1 / det;
    te[0] = t11 * detInv;
    te[1] = (n31 * n23 - n33 * n21) * detInv;
    te[2] = (n32 * n21 - n31 * n22) * detInv;
    te[3] = t12 * detInv;
    te[4] = (n33 * n11 - n31 * n13) * detInv;
    te[5] = (n31 * n12 - n32 * n11) * detInv;
    te[6] = t13 * detInv;
    te[7] = (n21 * n13 - n23 * n11) * detInv;
    te[8] = (n22 * n11 - n21 * n12) * detInv;
    return *this;
  }
  Matrix3& transpose() {
    double tmp;
    double* m = elements;
    tmp = m[1]; m[1] = m[3]; m[3] = tmp;
    tmp = m[2]; m[2] = m[6]; m[6] = tmp;
    tmp = m[5]; m[5] = m[7]; m[7] = tmp;
    return *this;
  }
  /** the matrix normals are transformed by: the inverse transpose of the upper 3×3 */
  inline Matrix3& getNormalMatrix(const Matrix4& matrix4);
  template <class A>
  const Matrix3& transposeIntoArray(A& r) const {
    const double* m = elements;
    r[0] = m[0];
    r[1] = m[3];
    r[2] = m[6];
    r[3] = m[1];
    r[4] = m[4];
    r[5] = m[7];
    r[6] = m[2];
    r[7] = m[5];
    r[8] = m[8];
    return *this;
  }
  /** a texture's offset, repeat, rotation and its centre in one matrix */
  Matrix3& setUvTransform(double tx, double ty, double sx, double sy, double rotation, double cx, double cy) {
    const double c = std::cos(rotation);
    const double s = std::sin(rotation);
    set(sx * c, sx * s, -sx * (c * cx + s * cy) + cx + tx,
        -sy * s, sy * c, -sy * (-s * cx + c * cy) + cy + ty,
        0, 0, 1);
    return *this;
  }
  Matrix3& scale(double sx, double sy) {
    Matrix3 m3;
    premultiply(m3.makeScale(sx, sy));
    return *this;
  }
  Matrix3& rotate(double theta) {
    Matrix3 m3;
    premultiply(m3.makeRotation(-theta));
    return *this;
  }
  Matrix3& translate(double tx, double ty) {
    Matrix3 m3;
    premultiply(m3.makeTranslation(tx, ty));
    return *this;
  }
  Matrix3& makeTranslation(double x, double y) { return set(1, 0, x, 0, 1, y, 0, 0, 1); }
  Matrix3& makeTranslation(const Vector2& v) { return set(1, 0, v.x, 0, 1, v.y, 0, 0, 1); }
  /** counterclockwise by theta */
  Matrix3& makeRotation(double theta) {
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    return set(c, -s, 0, s, c, 0, 0, 0, 1);
  }
  Matrix3& makeScale(double x, double y) { return set(x, 0, 0, 0, y, 0, 0, 0, 1); }
  bool equals(const Matrix3& matrix) const {
    for (int i = 0; i < 9; i++)
      if (elements[i] != matrix.elements[i]) return false;
    return true;
  }
  template <class A>
  Matrix3& fromArray(const A& array, size_t offset = 0) {
    for (size_t i = 0; i < 9; i++) elements[i] = array[i + offset];
    return *this;
  }
  std::array<double, 9> toArray() const {
    std::array<double, 9> a;
    for (size_t i = 0; i < 9; i++) a[i] = elements[i];
    return a;
  }
  template <class A>
  A& toArray(A& array, size_t offset = 0) const {
    for (size_t i = 0; i < 9; i++) array[offset + i] = elements[i];
    return array;
  }
  Matrix3 clone() const { return *this; }

  bool operator==(const Matrix3& o) const { return equals(o); }
  bool operator!=(const Matrix3& o) const { return !equals(o); }
};

// ---------------------------------------------------------------------------
// Matrix4 (column-major: elements[12..14] is the translation)
// ---------------------------------------------------------------------------

struct Matrix4 {
  union {
    double elements[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    /** the same numbers by the draft's name */
    double e[16];
  };

  Matrix4() = default;
  /** by rows, as it is written on paper */
  Matrix4(double n11, double n12, double n13, double n14, double n21, double n22, double n23, double n24,
          double n31, double n32, double n33, double n34, double n41, double n42, double n43, double n44) {
    set(n11, n12, n13, n14, n21, n22, n23, n24, n31, n32, n33, n34, n41, n42, n43, n44);
  }

  Matrix4& set(double n11, double n12, double n13, double n14, double n21, double n22, double n23, double n24,
               double n31, double n32, double n33, double n34, double n41, double n42, double n43, double n44) {
    double* te = elements;
    te[0] = n11; te[4] = n12; te[8] = n13; te[12] = n14;
    te[1] = n21; te[5] = n22; te[9] = n23; te[13] = n24;
    te[2] = n31; te[6] = n32; te[10] = n33; te[14] = n34;
    te[3] = n41; te[7] = n42; te[11] = n43; te[15] = n44;
    return *this;
  }
  Matrix4& identity() { return set(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1); }
  Matrix4 clone() const { return *this; }
  Matrix4& copy(const Matrix4& m) {
    for (int i = 0; i < 16; i++) elements[i] = m.elements[i];
    return *this;
  }
  Matrix4& copyPosition(const Matrix4& m) {
    double* te = elements;
    const double* me = m.elements;
    te[12] = me[12];
    te[13] = me[13];
    te[14] = me[14];
    return *this;
  }
  Matrix4& setFromMatrix3(const Matrix3& m) {
    const double* me = m.elements;
    set(me[0], me[3], me[6], 0,
        me[1], me[4], me[7], 0,
        me[2], me[5], me[8], 0,
        0, 0, 0, 1);
    return *this;
  }
  const Matrix4& extractBasis(Vector3& xAxis, Vector3& yAxis, Vector3& zAxis) const {
    if (determinantAffine() == 0) {
      xAxis.set(1, 0, 0);
      yAxis.set(0, 1, 0);
      zAxis.set(0, 0, 1);
      return *this;
    }
    xAxis.setFromMatrixColumn(*this, 0);
    yAxis.setFromMatrixColumn(*this, 1);
    zAxis.setFromMatrixColumn(*this, 2);
    return *this;
  }
  /** the three columns are the given axes */
  Matrix4& makeBasis(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis) {
    set(xAxis.x, yAxis.x, zAxis.x, 0,
        xAxis.y, yAxis.y, zAxis.y, 0,
        xAxis.z, yAxis.z, zAxis.z, 0,
        0, 0, 0, 1);
    return *this;
  }
  /** the rotation of m (its upper 3×3 without the scale) */
  Matrix4& extractRotation(const Matrix4& m) {
    if (m.determinantAffine() == 0) return identity();
    double* te = elements;
    const double* me = m.elements;
    Vector3 v1;
    const double scaleX = 1 / v1.setFromMatrixColumn(m, 0).length();
    const double scaleY = 1 / v1.setFromMatrixColumn(m, 1).length();
    const double scaleZ = 1 / v1.setFromMatrixColumn(m, 2).length();
    te[0] = me[0] * scaleX;
    te[1] = me[1] * scaleX;
    te[2] = me[2] * scaleX;
    te[3] = 0;
    te[4] = me[4] * scaleY;
    te[5] = me[5] * scaleY;
    te[6] = me[6] * scaleY;
    te[7] = 0;
    te[8] = me[8] * scaleZ;
    te[9] = me[9] * scaleZ;
    te[10] = me[10] * scaleZ;
    te[11] = 0;
    te[12] = 0;
    te[13] = 0;
    te[14] = 0;
    te[15] = 1;
    return *this;
  }
  Matrix4& makeRotationFromEuler(const Euler& euler) {
    double* te = elements;
    const double x = euler.x, y = euler.y, z = euler.z;
    const double a = std::cos(x), b = std::sin(x);
    const double c = std::cos(y), d = std::sin(y);
    const double e_ = std::cos(z), f = std::sin(z);
    if (euler.order == Euler::XYZ) {
      const double ae = a * e_, af = a * f, be = b * e_, bf = b * f;
      te[0] = c * e_;
      te[4] = -c * f;
      te[8] = d;
      te[1] = af + be * d;
      te[5] = ae - bf * d;
      te[9] = -b * c;
      te[2] = bf - ae * d;
      te[6] = be + af * d;
      te[10] = a * c;
    } else if (euler.order == Euler::YXZ) {
      const double ce = c * e_, cf = c * f, de = d * e_, df = d * f;
      te[0] = ce + df * b;
      te[4] = de * b - cf;
      te[8] = a * d;
      te[1] = a * f;
      te[5] = a * e_;
      te[9] = -b;
      te[2] = cf * b - de;
      te[6] = df + ce * b;
      te[10] = a * c;
    } else if (euler.order == Euler::ZXY) {
      const double ce = c * e_, cf = c * f, de = d * e_, df = d * f;
      te[0] = ce - df * b;
      te[4] = -a * f;
      te[8] = de + cf * b;
      te[1] = cf + de * b;
      te[5] = a * e_;
      te[9] = df - ce * b;
      te[2] = -a * d;
      te[6] = b;
      te[10] = a * c;
    } else if (euler.order == Euler::ZYX) {
      const double ae = a * e_, af = a * f, be = b * e_, bf = b * f;
      te[0] = c * e_;
      te[4] = be * d - af;
      te[8] = ae * d + bf;
      te[1] = c * f;
      te[5] = bf * d + ae;
      te[9] = af * d - be;
      te[2] = -d;
      te[6] = b * c;
      te[10] = a * c;
    } else if (euler.order == Euler::YZX) {
      const double ac = a * c, ad = a * d, bc = b * c, bd = b * d;
      te[0] = c * e_;
      te[4] = bd - ac * f;
      te[8] = bc * f + ad;
      te[1] = f;
      te[5] = a * e_;
      te[9] = -b * e_;
      te[2] = -d * e_;
      te[6] = ad * f + bc;
      te[10] = ac - bd * f;
    } else if (euler.order == Euler::XZY) {
      const double ac = a * c, ad = a * d, bc = b * c, bd = b * d;
      te[0] = c * e_;
      te[4] = -f;
      te[8] = d * e_;
      te[1] = ac * f + bd;
      te[5] = a * e_;
      te[9] = ad * f - bc;
      te[2] = bc * f - ad;
      te[6] = b * e_;
      te[10] = bd * f + ac;
    }
    // bottom row
    te[3] = 0;
    te[7] = 0;
    te[11] = 0;
    // last column
    te[12] = 0;
    te[13] = 0;
    te[14] = 0;
    te[15] = 1;
    return *this;
  }
  Matrix4& makeRotationFromQuaternion(const Quaternion& q) { return compose(Vector3(0, 0, 0), q, Vector3(1, 1, 1)); }
  /** the rotation of something at `eye` that looks at `target` (only the upper 3×3 is written) */
  Matrix4& lookAt(const Vector3& eye, const Vector3& target, const Vector3& up) {
    double* te = elements;
    Vector3 x_, y_, z_;
    z_.subVectors(eye, target);
    if (z_.lengthSq() == 0) {
      // eye and target are in the same position
      z_.z = 1;
    }
    z_.normalize();
    x_.crossVectors(up, z_);
    if (x_.lengthSq() == 0) {
      // up and z are parallel
      if (std::fabs(up.z) == 1) {
        z_.x += 0.0001;
      } else {
        z_.z += 0.0001;
      }
      z_.normalize();
      x_.crossVectors(up, z_);
    }
    x_.normalize();
    y_.crossVectors(z_, x_);
    te[0] = x_.x; te[4] = y_.x; te[8] = z_.x;
    te[1] = x_.y; te[5] = y_.y; te[9] = z_.y;
    te[2] = x_.z; te[6] = y_.z; te[10] = z_.z;
    return *this;
  }
  Matrix4& multiply(const Matrix4& m) { return multiplyMatrices(*this, m); }
  Matrix4& premultiply(const Matrix4& m) { return multiplyMatrices(m, *this); }
  Matrix4& multiplyMatrices(const Matrix4& a, const Matrix4& b) {
    const double* ae = a.elements;
    const double* be = b.elements;
    double* te = elements;
    const double a11 = ae[0], a12 = ae[4], a13 = ae[8], a14 = ae[12];
    const double a21 = ae[1], a22 = ae[5], a23 = ae[9], a24 = ae[13];
    const double a31 = ae[2], a32 = ae[6], a33 = ae[10], a34 = ae[14];
    const double a41 = ae[3], a42 = ae[7], a43 = ae[11], a44 = ae[15];
    const double b11 = be[0], b12 = be[4], b13 = be[8], b14 = be[12];
    const double b21 = be[1], b22 = be[5], b23 = be[9], b24 = be[13];
    const double b31 = be[2], b32 = be[6], b33 = be[10], b34 = be[14];
    const double b41 = be[3], b42 = be[7], b43 = be[11], b44 = be[15];
    te[0] = a11 * b11 + a12 * b21 + a13 * b31 + a14 * b41;
    te[4] = a11 * b12 + a12 * b22 + a13 * b32 + a14 * b42;
    te[8] = a11 * b13 + a12 * b23 + a13 * b33 + a14 * b43;
    te[12] = a11 * b14 + a12 * b24 + a13 * b34 + a14 * b44;
    te[1] = a21 * b11 + a22 * b21 + a23 * b31 + a24 * b41;
    te[5] = a21 * b12 + a22 * b22 + a23 * b32 + a24 * b42;
    te[9] = a21 * b13 + a22 * b23 + a23 * b33 + a24 * b43;
    te[13] = a21 * b14 + a22 * b24 + a23 * b34 + a24 * b44;
    te[2] = a31 * b11 + a32 * b21 + a33 * b31 + a34 * b41;
    te[6] = a31 * b12 + a32 * b22 + a33 * b32 + a34 * b42;
    te[10] = a31 * b13 + a32 * b23 + a33 * b33 + a34 * b43;
    te[14] = a31 * b14 + a32 * b24 + a33 * b34 + a34 * b44;
    te[3] = a41 * b11 + a42 * b21 + a43 * b31 + a44 * b41;
    te[7] = a41 * b12 + a42 * b22 + a43 * b32 + a44 * b42;
    te[11] = a41 * b13 + a42 * b23 + a43 * b33 + a44 * b43;
    te[15] = a41 * b14 + a42 * b24 + a43 * b34 + a44 * b44;
    return *this;
  }
  Matrix4& multiplyScalar(double s) {
    for (int i = 0; i < 16; i++) elements[i] *= s;
    return *this;
  }
  double determinant() const {
    const double* te = elements;
    const double n11 = te[0], n12 = te[4], n13 = te[8], n14 = te[12];
    const double n21 = te[1], n22 = te[5], n23 = te[9], n24 = te[13];
    const double n31 = te[2], n32 = te[6], n33 = te[10], n34 = te[14];
    const double n41 = te[3], n42 = te[7], n43 = te[11], n44 = te[15];
    const double t11 = n23 * n34 - n24 * n33;
    const double t12 = n22 * n34 - n24 * n32;
    const double t13 = n22 * n33 - n23 * n32;
    const double t21 = n21 * n34 - n24 * n31;
    const double t22 = n21 * n33 - n23 * n31;
    const double t23 = n21 * n32 - n22 * n31;
    return n11 * (n42 * t11 - n43 * t12 + n44 * t13) -
           n12 * (n41 * t11 - n43 * t21 + n44 * t22) +
           n13 * (n41 * t12 - n42 * t21 + n44 * t23) -
           n14 * (n41 * t13 - n42 * t22 + n43 * t23);
  }
  /** the determinant of the upper 3×3 */
  double determinantAffine() const {
    const double* te = elements;
    const double n11 = te[0], n12 = te[4], n13 = te[8];
    const double n21 = te[1], n22 = te[5], n23 = te[9];
    const double n31 = te[2], n32 = te[6], n33 = te[10];
    return n11 * (n22 * n33 - n23 * n32) -
           n12 * (n21 * n33 - n23 * n31) +
           n13 * (n21 * n32 - n22 * n31);
  }
  Matrix4& transpose() {
    double* te = elements;
    double tmp;
    tmp = te[1]; te[1] = te[4]; te[4] = tmp;
    tmp = te[2]; te[2] = te[8]; te[8] = tmp;
    tmp = te[6]; te[6] = te[9]; te[9] = tmp;
    tmp = te[3]; te[3] = te[12]; te[12] = tmp;
    tmp = te[7]; te[7] = te[13]; te[13] = tmp;
    tmp = te[11]; te[11] = te[14]; te[14] = tmp;
    return *this;
  }
  Matrix4& setPosition(const Vector3& v) {
    elements[12] = v.x;
    elements[13] = v.y;
    elements[14] = v.z;
    return *this;
  }
  Matrix4& setPosition(double x, double y, double z) {
    elements[12] = x;
    elements[13] = y;
    elements[14] = z;
    return *this;
  }
  /** the inverse (all zeros if there is none) */
  Matrix4& invert() {
    // based on https://github.com/toji/gl-matrix
    double* te = elements;
    const double n11 = te[0], n21 = te[1], n31 = te[2], n41 = te[3],
                 n12 = te[4], n22 = te[5], n32 = te[6], n42 = te[7],
                 n13 = te[8], n23 = te[9], n33 = te[10], n43 = te[11],
                 n14 = te[12], n24 = te[13], n34 = te[14], n44 = te[15],
                 t1 = n11 * n22 - n21 * n12,
                 t2 = n11 * n32 - n31 * n12,
                 t3 = n11 * n42 - n41 * n12,
                 t4 = n21 * n32 - n31 * n22,
                 t5 = n21 * n42 - n41 * n22,
                 t6 = n31 * n42 - n41 * n32,
                 t7 = n13 * n24 - n23 * n14,
                 t8 = n13 * n34 - n33 * n14,
                 t9 = n13 * n44 - n43 * n14,
                 t10 = n23 * n34 - n33 * n24,
                 t11 = n23 * n44 - n43 * n24,
                 t12 = n33 * n44 - n43 * n34;
    const double det = t1 * t12 - t2 * t11 + t3 * t10 + t4 * t9 - t5 * t8 + t6 * t7;
    if (det == 0) return set(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    const double detInv = 1 / det;
    te[0] = (n22 * t12 - n32 * t11 + n42 * t10) * detInv;
    te[1] = (n31 * t11 - n21 * t12 - n41 * t10) * detInv;
    te[2] = (n24 * t6 - n34 * t5 + n44 * t4) * detInv;
    te[3] = (n33 * t5 - n23 * t6 - n43 * t4) * detInv;
    te[4] = (n32 * t9 - n12 * t12 - n42 * t8) * detInv;
    te[5] = (n11 * t12 - n31 * t9 + n41 * t8) * detInv;
    te[6] = (n34 * t3 - n14 * t6 - n44 * t2) * detInv;
    te[7] = (n13 * t6 - n33 * t3 + n43 * t2) * detInv;
    te[8] = (n12 * t11 - n22 * t9 + n42 * t7) * detInv;
    te[9] = (n21 * t9 - n11 * t11 - n41 * t7) * detInv;
    te[10] = (n14 * t5 - n24 * t3 + n44 * t1) * detInv;
    te[11] = (n23 * t3 - n13 * t5 - n43 * t1) * detInv;
    te[12] = (n22 * t8 - n12 * t10 - n32 * t7) * detInv;
    te[13] = (n11 * t10 - n21 * t8 + n31 * t7) * detInv;
    te[14] = (n24 * t2 - n14 * t4 - n34 * t1) * detInv;
    te[15] = (n13 * t4 - n23 * t2 + n33 * t1) * detInv;
    return *this;
  }
  /** the columns multiplied by v's x, y, z */
  Matrix4& scale(const Vector3& v) {
    double* te = elements;
    const double x = v.x, y = v.y, z = v.z;
    te[0] *= x; te[4] *= y; te[8] *= z;
    te[1] *= x; te[5] *= y; te[9] *= z;
    te[2] *= x; te[6] *= y; te[10] *= z;
    te[3] *= x; te[7] *= y; te[11] *= z;
    return *this;
  }
  double getMaxScaleOnAxis() const {
    const double* te = elements;
    const double scaleXSq = te[0] * te[0] + te[1] * te[1] + te[2] * te[2];
    const double scaleYSq = te[4] * te[4] + te[5] * te[5] + te[6] * te[6];
    const double scaleZSq = te[8] * te[8] + te[9] * te[9] + te[10] * te[10];
    return std::sqrt(mathdetail::max3(scaleXSq, scaleYSq, scaleZSq));
  }
  Matrix4& makeTranslation(double x, double y, double z) { return set(1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1); }
  Matrix4& makeTranslation(const Vector3& v) { return set(1, 0, 0, v.x, 0, 1, 0, v.y, 0, 0, 1, v.z, 0, 0, 0, 1); }
  Matrix4& makeRotationX(double theta) {
    const double c = std::cos(theta), s = std::sin(theta);
    return set(1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0, 0, 0, 0, 1);
  }
  Matrix4& makeRotationY(double theta) {
    const double c = std::cos(theta), s = std::sin(theta);
    return set(c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1);
  }
  Matrix4& makeRotationZ(double theta) {
    const double c = std::cos(theta), s = std::sin(theta);
    return set(c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
  }
  /** the axis must be normalised */
  Matrix4& makeRotationAxis(const Vector3& axis, double angle) {
    // Based on http://www.gamedev.net/reference/articles/article1199.asp
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1 - c;
    const double x = axis.x, y = axis.y, z = axis.z;
    const double tx = t * x, ty = t * y;
    return set(tx * x + c, tx * y - s * z, tx * z + s * y, 0,
               tx * y + s * z, ty * y + c, ty * z - s * x, 0,
               tx * z - s * y, ty * z + s * x, t * z * z + c, 0,
               0, 0, 0, 1);
  }
  Matrix4& makeScale(double x, double y, double z) { return set(x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1); }
  Matrix4& makeShear(double xy, double xz, double yx, double yz, double zx, double zy) {
    return set(1, yx, zx, 0, xy, 1, zy, 0, xz, yz, 1, 0, 0, 0, 0, 1);
  }
  /** the matrix of a position, a rotation and a scale */
  Matrix4& compose(const Vector3& position, const Quaternion& quaternion, const Vector3& scale) {
    double* te = elements;
    const double x = quaternion.x, y = quaternion.y, z = quaternion.z, w = quaternion.w;
    const double x2 = x + x, y2 = y + y, z2 = z + z;
    const double xx = x * x2, xy = x * y2, xz = x * z2;
    const double yy = y * y2, yz = y * z2, zz = z * z2;
    const double wx = w * x2, wy = w * y2, wz = w * z2;
    const double sx = scale.x, sy = scale.y, sz = scale.z;
    te[0] = (1 - (yy + zz)) * sx;
    te[1] = (xy + wz) * sx;
    te[2] = (xz - wy) * sx;
    te[3] = 0;
    te[4] = (xy - wz) * sy;
    te[5] = (1 - (xx + zz)) * sy;
    te[6] = (yz + wx) * sy;
    te[7] = 0;
    te[8] = (xz + wy) * sz;
    te[9] = (yz - wx) * sz;
    te[10] = (1 - (xx + yy)) * sz;
    te[11] = 0;
    te[12] = position.x;
    te[13] = position.y;
    te[14] = position.z;
    te[15] = 1;
    return *this;
  }
  /** … and back (a matrix without volume gives scale 1 and no rotation) */
  const Matrix4& decompose(Vector3& position, Quaternion& quaternion, Vector3& scale) const {
    const double* te = elements;
    position.x = te[12];
    position.y = te[13];
    position.z = te[14];
    const double det = determinantAffine();
    if (det == 0) {
      scale.set(1, 1, 1);
      quaternion.identity();
      return *this;
    }
    Vector3 v1;
    double sx = v1.set(te[0], te[1], te[2]).length();
    const double sy = v1.set(te[4], te[5], te[6]).length();
    const double sz = v1.set(te[8], te[9], te[10]).length();
    // if determinant is negative, we need to invert one scale
    if (det < 0) sx = -sx;
    // scale the rotation part
    Matrix4 m1 = *this;
    const double invSX = 1 / sx;
    const double invSY = 1 / sy;
    const double invSZ = 1 / sz;
    m1.elements[0] *= invSX;
    m1.elements[1] *= invSX;
    m1.elements[2] *= invSX;
    m1.elements[4] *= invSY;
    m1.elements[5] *= invSY;
    m1.elements[6] *= invSY;
    m1.elements[8] *= invSZ;
    m1.elements[9] *= invSZ;
    m1.elements[10] *= invSZ;
    quaternion.setFromRotationMatrix(m1);
    scale.x = sx;
    scale.y = sy;
    scale.z = sz;
    return *this;
  }
  /** a perspective projection as WebGL has it (depth -1 … 1) */
  Matrix4& makePerspective(double left, double right, double top, double bottom, double near, double far) {
    double* te = elements;
    const double x = 2 * near / (right - left);
    const double y = 2 * near / (top - bottom);
    const double a = (right + left) / (right - left);
    const double b = (top + bottom) / (top - bottom);
    const double c = -(far + near) / (far - near);
    const double d = (-2 * far * near) / (far - near);
    te[0] = x; te[4] = 0; te[8] = a; te[12] = 0;
    te[1] = 0; te[5] = y; te[9] = b; te[13] = 0;
    te[2] = 0; te[6] = 0; te[10] = c; te[14] = d;
    te[3] = 0; te[7] = 0; te[11] = -1; te[15] = 0;
    return *this;
  }
  /** an orthographic projection as WebGL has it (depth -1 … 1) */
  Matrix4& makeOrthographic(double left, double right, double top, double bottom, double near, double far) {
    double* te = elements;
    const double x = 2 / (right - left);
    const double y = 2 / (top - bottom);
    const double a = -(right + left) / (right - left);
    const double b = -(top + bottom) / (top - bottom);
    const double c = -2 / (far - near);
    const double d = -(far + near) / (far - near);
    te[0] = x; te[4] = 0; te[8] = 0; te[12] = a;
    te[1] = 0; te[5] = y; te[9] = 0; te[13] = b;
    te[2] = 0; te[6] = 0; te[10] = c; te[14] = d;
    te[3] = 0; te[7] = 0; te[11] = 0; te[15] = 1;
    return *this;
  }
  bool equals(const Matrix4& matrix) const {
    for (int i = 0; i < 16; i++)
      if (elements[i] != matrix.elements[i]) return false;
    return true;
  }
  template <class A>
  Matrix4& fromArray(const A& array, size_t offset = 0) {
    for (size_t i = 0; i < 16; i++) elements[i] = array[i + offset];
    return *this;
  }
  std::array<double, 16> toArray() const {
    std::array<double, 16> a;
    for (size_t i = 0; i < 16; i++) a[i] = elements[i];
    return a;
  }
  template <class A>
  A& toArray(A& array, size_t offset = 0) const {
    for (size_t i = 0; i < 16; i++) array[offset + i] = elements[i];
    return array;
  }
  /** the number in a row and a column (0 … 3) */
  double at(int row, int col) const { return elements[col * 4 + row]; }

  bool operator==(const Matrix4& o) const { return equals(o); }
  bool operator!=(const Matrix4& o) const { return !equals(o); }
};

// ---------------------------------------------------------------------------
// what needs the complete types
// ---------------------------------------------------------------------------

inline Vector2& Vector2::applyMatrix3(const Matrix3& m) {
  const double x_ = x, y_ = y;
  const double* e = m.elements;
  x = e[0] * x_ + e[3] * y_ + e[6];
  y = e[1] * x_ + e[4] * y_ + e[7];
  return *this;
}

inline Vector3& Vector3::applyEuler(const Euler& euler) {
  Quaternion quaternion;
  return applyQuaternion(quaternion.setFromEuler(euler));
}
inline Vector3& Vector3::applyAxisAngle(const Vector3& axis, double angle) {
  Quaternion quaternion;
  return applyQuaternion(quaternion.setFromAxisAngle(axis, angle));
}
inline Vector3& Vector3::applyMatrix3(const Matrix3& m) {
  const double x_ = x, y_ = y, z_ = z;
  const double* e = m.elements;
  x = e[0] * x_ + e[3] * y_ + e[6] * z_;
  y = e[1] * x_ + e[4] * y_ + e[7] * z_;
  z = e[2] * x_ + e[5] * y_ + e[8] * z_;
  return *this;
}
inline Vector3& Vector3::applyNormalMatrix(const Matrix3& m) { return applyMatrix3(m).normalize(); }
inline Vector3& Vector3::applyMatrix4(const Matrix4& m) {
  const double x_ = x, y_ = y, z_ = z;
  const double* e = m.elements;
  const double w = 1 / (e[3] * x_ + e[7] * y_ + e[11] * z_ + e[15]);
  x = (e[0] * x_ + e[4] * y_ + e[8] * z_ + e[12]) * w;
  y = (e[1] * x_ + e[5] * y_ + e[9] * z_ + e[13]) * w;
  z = (e[2] * x_ + e[6] * y_ + e[10] * z_ + e[14]) * w;
  return *this;
}
inline Vector3& Vector3::applyQuaternion(const Quaternion& q) {
  // quaternion q is assumed to have unit length
  const double vx = x, vy = y, vz = z;
  const double qx = q.x, qy = q.y, qz = q.z, qw = q.w;
  // t = 2 * cross( q.xyz, v );
  const double tx = 2 * (qy * vz - qz * vy);
  const double ty = 2 * (qz * vx - qx * vz);
  const double tz = 2 * (qx * vy - qy * vx);
  // v + q.w * t + cross( q.xyz, t );
  x = vx + qw * tx + qy * tz - qz * ty;
  y = vy + qw * ty + qz * tx - qx * tz;
  z = vz + qw * tz + qx * ty - qy * tx;
  return *this;
}
inline Vector3& Vector3::transformDirection(const Matrix4& m) {
  // input: THREE.Matrix4 affine matrix
  // vector interpreted as a direction
  const double x_ = x, y_ = y, z_ = z;
  const double* e = m.elements;
  x = e[0] * x_ + e[4] * y_ + e[8] * z_;
  y = e[1] * x_ + e[5] * y_ + e[9] * z_;
  z = e[2] * x_ + e[6] * y_ + e[10] * z_;
  return normalize();
}
inline Vector3& Vector3::setFromMatrixPosition(const Matrix4& m) {
  const double* e = m.elements;
  x = e[12];
  y = e[13];
  z = e[14];
  return *this;
}
inline Vector3& Vector3::setFromMatrixScale(const Matrix4& m) {
  const double sx = setFromMatrixColumn(m, 0).length();
  const double sy = setFromMatrixColumn(m, 1).length();
  const double sz = setFromMatrixColumn(m, 2).length();
  x = sx;
  y = sy;
  z = sz;
  return *this;
}
inline Vector3& Vector3::setFromMatrixColumn(const Matrix4& m, int index) { return fromArray(m.elements, static_cast<size_t>(index) * 4); }
inline Vector3& Vector3::setFromMatrix3Column(const Matrix3& m, int index) { return fromArray(m.elements, static_cast<size_t>(index) * 3); }
inline Vector3& Vector3::setFromEuler(const Euler& e) {
  x = e.x;
  y = e.y;
  z = e.z;
  return *this;
}

inline Vector4& Vector4::copy(const Vector3& v) {
  x = v.x;
  y = v.y;
  z = v.z;
  w = 1;
  return *this;
}
inline Vector4& Vector4::applyMatrix4(const Matrix4& m) {
  const double x_ = x, y_ = y, z_ = z, w_ = w;
  const double* e = m.elements;
  x = e[0] * x_ + e[4] * y_ + e[8] * z_ + e[12] * w_;
  y = e[1] * x_ + e[5] * y_ + e[9] * z_ + e[13] * w_;
  z = e[2] * x_ + e[6] * y_ + e[10] * z_ + e[14] * w_;
  w = e[3] * x_ + e[7] * y_ + e[11] * z_ + e[15] * w_;
  return *this;
}
inline Vector4& Vector4::setAxisAngleFromQuaternion(const Quaternion& q) {
  // http://www.euclideanspace.com/maths/geometry/rotations/conversions/quaternionToAngle/index.htm
  // q is assumed to be normalized
  w = 2 * std::acos(q.w);
  const double s = std::sqrt(1 - q.w * q.w);
  if (s < 0.0001) {
    x = 1;
    y = 0;
    z = 0;
  } else {
    x = q.x / s;
    y = q.y / s;
    z = q.z / s;
  }
  return *this;
}
inline Vector4& Vector4::setAxisAngleFromRotationMatrix(const Matrix4& m) {
  // http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToAngle/index.htm
  // assumes the upper 3x3 of m is a pure rotation matrix (i.e, unscaled)
  double angle, x_, y_, z_;   // variables for result
  const double epsilon = 0.01,   // margin to allow for rounding errors
               epsilon2 = 0.1;   // margin to distinguish between 0 and 180 degrees
  const double* te = m.elements;
  const double m11 = te[0], m12 = te[4], m13 = te[8],
               m21 = te[1], m22 = te[5], m23 = te[9],
               m31 = te[2], m32 = te[6], m33 = te[10];
  if ((std::fabs(m12 - m21) < epsilon) && (std::fabs(m13 - m31) < epsilon) && (std::fabs(m23 - m32) < epsilon)) {
    // singularity found
    // first check for identity matrix which must have +1 for all terms
    // in leading diagonal and zero in other terms
    if ((std::fabs(m12 + m21) < epsilon2) && (std::fabs(m13 + m31) < epsilon2) && (std::fabs(m23 + m32) < epsilon2) &&
        (std::fabs(m11 + m22 + m33 - 3) < epsilon2)) {
      // this singularity is identity matrix so angle = 0
      set(1, 0, 0, 0);
      return *this;   // zero angle, arbitrary axis
    }
    // otherwise this singularity is angle = 180
    angle = PI;
    const double xx = (m11 + 1) / 2;
    const double yy = (m22 + 1) / 2;
    const double zz = (m33 + 1) / 2;
    const double xy = (m12 + m21) / 4;
    const double xz = (m13 + m31) / 4;
    const double yz = (m23 + m32) / 4;
    if ((xx > yy) && (xx > zz)) {
      // m11 is the largest diagonal term
      if (xx < epsilon) {
        x_ = 0;
        y_ = 0.707106781;
        z_ = 0.707106781;
      } else {
        x_ = std::sqrt(xx);
        y_ = xy / x_;
        z_ = xz / x_;
      }
    } else if (yy > zz) {
      // m22 is the largest diagonal term
      if (yy < epsilon) {
        x_ = 0.707106781;
        y_ = 0;
        z_ = 0.707106781;
      } else {
        y_ = std::sqrt(yy);
        x_ = xy / y_;
        z_ = yz / y_;
      }
    } else {
      // m33 is the largest diagonal term so base result on this
      if (zz < epsilon) {
        x_ = 0.707106781;
        y_ = 0.707106781;
        z_ = 0;
      } else {
        z_ = std::sqrt(zz);
        x_ = xz / z_;
        y_ = yz / z_;
      }
    }
    set(x_, y_, z_, angle);
    return *this;   // return 180 deg rotation
  }
  // as we have reached here there are no singularities so we can handle normally
  double s = std::sqrt((m32 - m23) * (m32 - m23) + (m13 - m31) * (m13 - m31) + (m21 - m12) * (m21 - m12));   // used to normalize
  if (std::fabs(s) < 0.001) s = 1;
  // prevent divide by zero, should not happen if matrix is orthogonal and should be
  // caught by singularity test above, but I've left it in just in case
  x = (m32 - m23) / s;
  y = (m13 - m31) / s;
  z = (m21 - m12) / s;
  w = std::acos((m11 + m22 + m33 - 1) / 2);
  return *this;
}
inline Vector4& Vector4::setFromMatrixPosition(const Matrix4& m) {
  const double* e = m.elements;
  x = e[12];
  y = e[13];
  z = e[14];
  w = e[15];
  return *this;
}

inline Euler& Euler::setFromRotationMatrix(const Matrix4& m) { return setFromRotationMatrix(m, order); }
inline Euler& Euler::setFromRotationMatrix(const Matrix4& m, Order o) {
  using MathUtils::clamp;
  const double* te = m.elements;
  const double m11 = te[0], m12 = te[4], m13 = te[8];
  const double m21 = te[1], m22 = te[5], m23 = te[9];
  const double m31 = te[2], m32 = te[6], m33 = te[10];
  switch (o) {
    case XYZ:
      y = std::asin(clamp(m13, -1, 1));
      if (std::fabs(m13) < 0.9999999) {
        x = std::atan2(-m23, m33);
        z = std::atan2(-m12, m11);
      } else {
        x = std::atan2(m32, m22);
        z = 0;
      }
      break;
    case YXZ:
      x = std::asin(-clamp(m23, -1, 1));
      if (std::fabs(m23) < 0.9999999) {
        y = std::atan2(m13, m33);
        z = std::atan2(m21, m22);
      } else {
        y = std::atan2(-m31, m11);
        z = 0;
      }
      break;
    case ZXY:
      x = std::asin(clamp(m32, -1, 1));
      if (std::fabs(m32) < 0.9999999) {
        y = std::atan2(-m31, m33);
        z = std::atan2(-m12, m22);
      } else {
        y = 0;
        z = std::atan2(m21, m11);
      }
      break;
    case ZYX:
      y = std::asin(-clamp(m31, -1, 1));
      if (std::fabs(m31) < 0.9999999) {
        x = std::atan2(m32, m33);
        z = std::atan2(m21, m11);
      } else {
        x = 0;
        z = std::atan2(-m12, m22);
      }
      break;
    case YZX:
      z = std::asin(clamp(m21, -1, 1));
      if (std::fabs(m21) < 0.9999999) {
        x = std::atan2(-m23, m22);
        y = std::atan2(-m31, m11);
      } else {
        x = 0;
        y = std::atan2(m13, m33);
      }
      break;
    case XZY:
      z = std::asin(-clamp(m12, -1, 1));
      if (std::fabs(m12) < 0.9999999) {
        x = std::atan2(m32, m22);
        y = std::atan2(m13, m11);
      } else {
        x = std::atan2(-m23, m33);
        y = 0;
      }
      break;
  }
  order = o;
  return *this;
}
inline Euler& Euler::setFromQuaternion(const Quaternion& q) { return setFromQuaternion(q, order); }
inline Euler& Euler::setFromQuaternion(const Quaternion& q, Order o) {
  Matrix4 matrix;
  matrix.makeRotationFromQuaternion(q);
  return setFromRotationMatrix(matrix, o);
}
inline Euler& Euler::setFromVector3(const Vector3& v) { return set(v.x, v.y, v.z, order); }
inline Euler& Euler::setFromVector3(const Vector3& v, Order o) { return set(v.x, v.y, v.z, o); }
inline Euler& Euler::reorder(Order newOrder) {
  Quaternion quaternion;
  quaternion.setFromEuler(*this);
  return setFromQuaternion(quaternion, newOrder);
}

inline Quaternion& Quaternion::setFromEuler(const Euler& euler) {
  const double x_ = euler.x, y_ = euler.y, z_ = euler.z;
  // http://www.mathworks.com/matlabcentral/fileexchange/
  // 	20696-function-to-convert-between-dcm-euler-angles-quaternions-and-euler-vectors/
  //	content/SpinCalc.m
  const double c1 = std::cos(x_ / 2);
  const double c2 = std::cos(y_ / 2);
  const double c3 = std::cos(z_ / 2);
  const double s1 = std::sin(x_ / 2);
  const double s2 = std::sin(y_ / 2);
  const double s3 = std::sin(z_ / 2);
  switch (euler.order) {
    case Euler::XYZ:
      x = s1 * c2 * c3 + c1 * s2 * s3;
      y = c1 * s2 * c3 - s1 * c2 * s3;
      z = c1 * c2 * s3 + s1 * s2 * c3;
      w = c1 * c2 * c3 - s1 * s2 * s3;
      break;
    case Euler::YXZ:
      x = s1 * c2 * c3 + c1 * s2 * s3;
      y = c1 * s2 * c3 - s1 * c2 * s3;
      z = c1 * c2 * s3 - s1 * s2 * c3;
      w = c1 * c2 * c3 + s1 * s2 * s3;
      break;
    case Euler::ZXY:
      x = s1 * c2 * c3 - c1 * s2 * s3;
      y = c1 * s2 * c3 + s1 * c2 * s3;
      z = c1 * c2 * s3 + s1 * s2 * c3;
      w = c1 * c2 * c3 - s1 * s2 * s3;
      break;
    case Euler::ZYX:
      x = s1 * c2 * c3 - c1 * s2 * s3;
      y = c1 * s2 * c3 + s1 * c2 * s3;
      z = c1 * c2 * s3 - s1 * s2 * c3;
      w = c1 * c2 * c3 + s1 * s2 * s3;
      break;
    case Euler::YZX:
      x = s1 * c2 * c3 + c1 * s2 * s3;
      y = c1 * s2 * c3 + s1 * c2 * s3;
      z = c1 * c2 * s3 - s1 * s2 * c3;
      w = c1 * c2 * c3 - s1 * s2 * s3;
      break;
    case Euler::XZY:
      x = s1 * c2 * c3 - c1 * s2 * s3;
      y = c1 * s2 * c3 - s1 * c2 * s3;
      z = c1 * c2 * s3 + s1 * s2 * c3;
      w = c1 * c2 * c3 + s1 * s2 * s3;
      break;
  }
  return *this;
}
inline Quaternion& Quaternion::setFromRotationMatrix(const Matrix4& m) {
  // http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/index.htm
  // assumes the upper 3x3 of m is a pure rotation matrix (i.e, unscaled)
  const double* te = m.elements;
  const double m11 = te[0], m12 = te[4], m13 = te[8],
               m21 = te[1], m22 = te[5], m23 = te[9],
               m31 = te[2], m32 = te[6], m33 = te[10],
               trace = m11 + m22 + m33;
  if (trace > 0) {
    const double s = 0.5 / std::sqrt(trace + 1.0);
    w = 0.25 / s;
    x = (m32 - m23) * s;
    y = (m13 - m31) * s;
    z = (m21 - m12) * s;
  } else if (m11 > m22 && m11 > m33) {
    const double s = 2.0 * std::sqrt(1.0 + m11 - m22 - m33);
    w = (m32 - m23) / s;
    x = 0.25 * s;
    y = (m12 + m21) / s;
    z = (m13 + m31) / s;
  } else if (m22 > m33) {
    const double s = 2.0 * std::sqrt(1.0 + m22 - m11 - m33);
    w = (m13 - m31) / s;
    x = (m12 + m21) / s;
    y = 0.25 * s;
    z = (m23 + m32) / s;
  } else {
    const double s = 2.0 * std::sqrt(1.0 + m33 - m11 - m22);
    w = (m21 - m12) / s;
    x = (m13 + m31) / s;
    y = (m23 + m32) / s;
    z = 0.25 * s;
  }
  return *this;
}

inline Matrix3& Matrix3::setFromMatrix4(const Matrix4& m) {
  const double* me = m.elements;
  set(me[0], me[4], me[8],
      me[1], me[5], me[9],
      me[2], me[6], me[10]);
  return *this;
}
inline Matrix3& Matrix3::getNormalMatrix(const Matrix4& matrix4) { return setFromMatrix4(matrix4).invert().transpose(); }

namespace MathUtils {
/**
 * A quaternion from "proper" Euler angles (the first and the last axis the same):
 * order is one of 'XYX', 'YZY', 'ZXZ', 'XZX', 'YXY', 'ZYZ' (anything else leaves q as it is).
 */
inline void setQuaternionFromProperEuler(Quaternion& q, double a, double b, double c, const char* order) {
  // Intrinsic Proper Euler Angles - see https://en.wikipedia.org/wiki/Euler_angles
  // rotations are applied to the axes in the order specified by 'order'
  // rotation by angle 'a' is applied first, then by angle 'b', then by angle 'c'
  // angles are in radians
  const double c2 = std::cos(b / 2);
  const double s2 = std::sin(b / 2);
  const double c13 = std::cos((a + c) / 2);
  const double s13 = std::sin((a + c) / 2);
  const double c1_3 = std::cos((a - c) / 2);
  const double s1_3 = std::sin((a - c) / 2);
  const double c3_1 = std::cos((c - a) / 2);
  const double s3_1 = std::sin((c - a) / 2);
  const auto is = [order](const char* name) {
    return order && order[0] == name[0] && order[1] == name[1] && order[2] == name[2] && order[3] == 0;
  };
  if (is("XYX")) q.set(c2 * s13, s2 * c1_3, s2 * s1_3, c2 * c13);
  else if (is("YZY")) q.set(s2 * s1_3, c2 * s13, s2 * c1_3, c2 * c13);
  else if (is("ZXZ")) q.set(s2 * c1_3, s2 * s1_3, c2 * s13, c2 * c13);
  else if (is("XZX")) q.set(c2 * s13, s2 * s3_1, s2 * c3_1, c2 * c13);
  else if (is("YXY")) q.set(s2 * c3_1, c2 * s13, s2 * s3_1, c2 * c13);
  else if (is("ZYZ")) q.set(s2 * s3_1, s2 * c3_1, c2 * s13, c2 * c13);
}
}  // namespace MathUtils

}  // namespace fs
