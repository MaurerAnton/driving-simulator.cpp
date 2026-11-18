// SPDX-License-Identifier: AGPL-3.0-only
/**
 * 3D vector, quaternion, Euler, matrix and colour maths — the part of
 * three.js the game uses (Vector3, Quaternion, Euler, Matrix4, Color).
 *
 * Origin: three.js (math/Vector3.js, math/Quaternion.js, math/Euler.js,
 * math/Matrix4.js, math/Color.js) as used by the TypeScript game; the
 * operations mirror the JavaScript ones so numbers come out the same.
 *
 * Conventions follow three.js exactly: column-major matrices, right-handed
 * vectors (the game's own ground plane is y-up, x-east, z-south as the
 * original code documents in src/core/geom.ts).
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace fs {

constexpr double PI = 3.14159265358979323846;
constexpr double TAU = PI * 2.0;
constexpr double DEG = PI / 180.0;
constexpr double RAD = 180.0 / PI;

inline double clampD(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
inline double lerpD(double a, double b, double t) { return a + (b - a) * t; }
inline double smoothstepD(double e0, double e1, double x) {
  if (e1 == e0) return x < e0 ? 0.0 : 1.0;
  double t = clampD((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

struct Vector3;

// ---------------------------------------------------------------------------
// Vector3
// ---------------------------------------------------------------------------

struct Vector3 {
  double x = 0, y = 0, z = 0;

  Vector3() = default;
  Vector3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

  Vector3& set(double x_, double y_, double z_) { x = x_; y = y_; z = z_; return *this; }
  Vector3& setScalar(double s) { x = s; y = s; z = s; return *this; }
  Vector3& copy(const Vector3& v) { x = v.x; y = v.y; z = v.z; return *this; }

  Vector3& add(const Vector3& v) { x += v.x; y += v.y; z += v.z; return *this; }
  Vector3& addScalar(double s) { x += s; y += s; z += s; return *this; }
  Vector3& addVectors(const Vector3& a, const Vector3& b) {
    x = a.x + b.x; y = a.y + b.y; z = a.z + b.z; return *this;
  }
  Vector3& sub(const Vector3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
  Vector3& subVectors(const Vector3& a, const Vector3& b) {
    x = a.x - b.x; y = a.y - b.y; z = a.z - b.z; return *this;
  }
  Vector3& multiply(const Vector3& v) { x *= v.x; y *= v.y; z *= v.z; return *this; }
  Vector3& multiplyScalar(double s) { x *= s; y *= s; z *= s; return *this; }
  Vector3& divideScalar(double s) { return multiplyScalar(1.0 / s); }
  Vector3& min(const Vector3& v) {
    x = v.x < x ? v.x : x; y = v.y < y ? v.y : y; z = v.z < z ? v.z : z; return *this;
  }
  Vector3& max(const Vector3& v) {
    x = v.x > x ? v.x : x; y = v.y > y ? v.y : y; z = v.z > z ? v.z : z; return *this;
  }
  Vector3& clamp(const Vector3& lo, const Vector3& hi) {
    x = clampD(x, lo.x, hi.x); y = clampD(y, lo.y, hi.y); z = clampD(z, lo.z, hi.z); return *this;
  }
  Vector3& floor() { x = std::floor(x); y = std::floor(y); z = std::floor(z); return *this; }
  Vector3& ceil() { x = std::ceil(x); y = std::ceil(y); z = std::ceil(z); return *this; }
  Vector3& round() { x = std::round(x); y = std::round(y); z = std::round(z); return *this; }
  Vector3& negate() { x = -x; y = -y; z = -z; return *this; }

  double dot(const Vector3& v) const { return x * v.x + y * v.y + z * v.z; }
  Vector3& cross(const Vector3& v) {
    // this = this × v
    double ax = x, ay = y, az = z;
    x = ay * v.z - az * v.y;
    y = az * v.x - ax * v.z;
    z = ax * v.y - ay * v.x;
    return *this;
  }
  Vector3& crossVectors(const Vector3& a, const Vector3& b) {
    x = a.y * b.z - a.z * b.y;
    y = a.z * b.x - a.x * b.z;
    z = a.x * b.y - a.y * b.x;
    return *this;
  }

  double lengthSq() const { return x * x + y * y + z * z; }
  double length() const { return std::sqrt(lengthSq()); }
  Vector3& normalize() {
    double l = length();
    if (l > 0) return multiplyScalar(1.0 / l);
    return *this;
  }
  double distanceTo(const Vector3& v) const { return std::sqrt(distanceToSquared(v)); }
  double distanceToSquared(const Vector3& v) const {
    double dx = x - v.x, dy = y - v.y, dz = z - v.z;
    return dx * dx + dy * dy + dz * dz;
  }
  double manhattanDistanceTo(const Vector3& v) const {
    return std::fabs(x - v.x) + std::fabs(y - v.y) + std::fabs(z - v.z);
  }

  Vector3& lerp(const Vector3& v, double t) {
    x += (v.x - x) * t; y += (v.y - y) * t; z += (v.z - z) * t; return *this;
  }
  Vector3& lerpVectors(const Vector3& a, const Vector3& b, double t) {
    x = a.x + (b.x - a.x) * t; y = a.y + (b.y - a.y) * t; z = a.z + (b.z - a.z) * t; return *this;
  }
  Vector3& reflect(const Vector3& normal) {
    // this -= 2 * (this · normal) * normal
    return addScaledVector(normal, -2.0 * dot(normal));
  }
  Vector3& addScaledVector(const Vector3& v, double s) {
    x += v.x * s; y += v.y * s; z += v.z * s; return *this;
  }
  Vector3& projectOnVector(const Vector3& v) {
    double denom = v.lengthSq();
    if (denom == 0) return set(0, 0, 0);
    double scalar = dot(v) / denom;
    return copy(v).multiplyScalar(scalar);
  }
  Vector3& projectOnPlane(const Vector3& planeNormal) {
    Vector3 v = planeNormal * dot(planeNormal);
    return sub(v);
  }
  Vector3& setFromMatrixPosition(const struct Matrix4& m);
  Vector3& setFromMatrixColumn(const struct Matrix4& m, int index);
  Vector3& applyMatrix4(const struct Matrix4& m);
  Vector3& applyQuaternion(const struct Quaternion& q);
  Vector3& applyEuler(const struct Euler& e);
  Vector3& applyAxisAngle(const Vector3& axis, double angle);

  bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator!=(const Vector3& o) const { return !(*this == o); }

  // sugar used by the game code: v * s
  Vector3 operator*(double s) const { Vector3 r(*this); r.multiplyScalar(s); return r; }
  Vector3 operator+(const Vector3& o) const { Vector3 r(*this); r.add(o); return r; }
  Vector3 operator-(const Vector3& o) const { Vector3 r(*this); r.sub(o); return r; }
};

inline Vector3 operator*(double s, const Vector3& v) { return v * s; }

// ---------------------------------------------------------------------------
// Euler (three.js default order XYZ; the game mostly uses 'YXZ' for cameras)
// ---------------------------------------------------------------------------

struct Euler {
  double x = 0, y = 0, z = 0;
  // three.js Euler order as a small enum; the game uses XYZ and YXZ
  enum Order { XYZ, YXZ } order = XYZ;

  Euler() = default;
  Euler(double x_, double y_, double z_, Order o = XYZ) : x(x_), y(y_), z(z_), order(o) {}
  Euler& set(double x_, double y_, double z_, Order o = XYZ) {
    x = x_; y = y_; z = z_; order = o; return *this;
  }
  Euler& copy(const Euler& e) { x = e.x; y = e.y; z = e.z; order = e.order; return *this; }
};

// ---------------------------------------------------------------------------
// Quaternion
// ---------------------------------------------------------------------------

struct Quaternion {
  double x = 0, y = 0, z = 0, w = 1;

  Quaternion() = default;
  Quaternion(double x_, double y_, double z_, double w_) : x(x_), y(y_), z(z_), w(w_) {}

  Quaternion& set(double x_, double y_, double z_, double w_) {
    x = x_; y = y_; z = z_; w = w_; return *this;
  }
  Quaternion& copy(const Quaternion& q) { x = q.x; y = q.y; z = q.z; w = q.w; return *this; }
  Quaternion& setFromAxisAngle(const Vector3& axis, double angle) {
    double half = angle / 2.0;
    double s = std::sin(half);
    x = axis.x * s; y = axis.y * s; z = axis.z * s; w = std::cos(half);
    return *this;
  }
  Quaternion& setFromRotationMatrix(const struct Matrix4& m);
  Quaternion& setFromEuler(const Euler& e);
  /** Shortest rotation taking `from` to `to` (three.js setFromUnitVectors). */
  Quaternion& setFromUnitVectors(const Vector3& from, const Vector3& to);
  Quaternion& multiply(const Quaternion& q) { return multiplyQuaternions(*this, q); }
  Quaternion& multiplyQuaternions(const Quaternion& a, const Quaternion& b) {
    double ax = a.x, ay = a.y, az = a.z, aw = a.w;
    double bx = b.x, by = b.y, bz = b.z, bw = b.w;
    x = ax * bw + aw * bx + ay * bz - az * by;
    y = ay * bw + aw * by + az * bx - ax * bz;
    z = az * bw + aw * bz + ax * by - ay * bx;
    w = aw * bw - ax * bx - ay * by - az * bz;
    return *this;
  }
  Quaternion& premultiply(const Quaternion& q) { return multiplyQuaternions(q, *this); }
  Quaternion& conjugate() { x = -x; y = -y; z = -z; return *this; }
  double lengthSq() const { return x * x + y * y + z * z + w * w; }
  double length() const { return std::sqrt(lengthSq()); }
  Quaternion& normalize() {
    double l = length();
    if (l == 0) return set(0, 0, 0, 1);
    double il = 1.0 / l;
    x *= il; y *= il; z *= il; w *= il;
    return *this;
  }
  double dot(const Quaternion& q) const { return x * q.x + y * q.y + z * q.z + w * q.w; }
  Quaternion& slerp(const Quaternion& qb, double t);
  Quaternion& identity() { return set(0, 0, 0, 1); }
  Quaternion& invert() { conjugate(); return *this; }
  bool operator==(const Quaternion& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
};

// ---------------------------------------------------------------------------
// Matrix4 (column-major, three.js layout)
// ---------------------------------------------------------------------------

struct Matrix4 {
  // elements in three.js order: e[0..3] = column 0, etc.
  double e[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  Matrix4() = default;

  Matrix4& identity() {
    for (int i = 0; i < 16; i++) e[i] = (i % 5 == 0) ? 1.0 : 0.0;
    return *this;
  }
  Matrix4& copy(const Matrix4& m) {
    for (int i = 0; i < 16; i++) e[i] = m.e[i];
    return *this;
  }
  Matrix4& set(double n11, double n12, double n13, double n14,
               double n21, double n22, double n23, double n24,
               double n31, double n32, double n33, double n34,
               double n41, double n42, double n43, double n44) {
    e[0] = n11; e[4] = n12; e[8] = n13; e[12] = n14;
    e[1] = n21; e[5] = n22; e[9] = n23; e[13] = n24;
    e[2] = n31; e[6] = n32; e[10] = n33; e[14] = n34;
    e[3] = n41; e[7] = n42; e[11] = n43; e[15] = n44;
    return *this;
  }
  Matrix4& makeTranslation(double x, double y, double z) {
    return set(1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1);
  }
  Matrix4& makeRotationFromEuler(const Euler& euler);
  Matrix4& makeRotationFromQuaternion(const Quaternion& q);
  Matrix4& multiply(const Matrix4& m) { return multiplyMatrices(*this, m); }
  Matrix4& premultiply(const Matrix4& m) { return multiplyMatrices(m, *this); }
  Matrix4& multiplyMatrices(const Matrix4& a, const Matrix4& b);
  Matrix4& multiplyScalar(double s) {
    for (int i = 0; i < 16; i++) e[i] *= s;
    return *this;
  }
  double determinant() const { return determinantImpl(); }
  Matrix4& invert() { return invertImpl(); }
  Matrix4& transpose() { return transposeImpl(); }
  Matrix4& setPosition(const Vector3& v) { e[12] = v.x; e[13] = v.y; e[14] = v.z; return *this; }
  // compose (three.js Matrix4.compose): rotation from quaternion scaled by `scale`, then translated
  Matrix4& compose(const Vector3& position, const Quaternion& quaternion, const Vector3& scale) {
    double x = quaternion.x, y = quaternion.y, z = quaternion.z, w = quaternion.w;
    double x2 = x + x, y2 = y + y, z2 = z + z;
    double xx = x * x2, xy = x * y2, xz = x * z2;
    double yy = y * y2, yz = y * z2, zz = z * z2;
    double wx = w * x2, wy = w * y2, wz = w * z2;
    double sx = scale.x, sy = scale.y, sz = scale.z;
    // three.js: R = rotation(q), then column 0 *= sx, column 1 *= sy, column 2 *= sz.
    // Column-major storage: R[0][c] = e[c*4+0], so the scale factors land on e[c*4+r].
    double r00 = 1.0 - (yy + zz), r01 = xy - wz,  r02 = xz + wy;
    double r10 = xy + wz,         r11 = 1.0 - (xx + zz), r12 = yz - wx;
    double r20 = xz - wy,         r21 = yz + wx,  r22 = 1.0 - (xx + yy);
    e[0]  = r00 * sx;  e[4]  = r01 * sy;  e[8]  = r02 * sz;
    e[1]  = r10 * sx;  e[5]  = r11 * sy;  e[9]  = r12 * sz;
    e[2]  = r20 * sx;  e[6]  = r21 * sy;  e[10] = r22 * sz;
    e[12] = position.x;
    e[13] = position.y;
    e[14] = position.z;
    e[3] = 0; e[7] = 0; e[11] = 0; e[15] = 1;
    return *this;
  }
  Matrix4 decompose(Vector3& position, Quaternion& quaternion, Vector3& scale) const;
  Vector3& setFromMatrixPosition(Vector3& v) const { return v.set(e[12], e[13], e[14]); }
  Matrix4& makePerspective(double left, double right, double top, double bottom, double near, double far);
  Matrix4& makeOrthographic(double left, double right, double top, double bottom, double near, double far);
  Matrix4& lookAt(const Vector3& eye, const Vector3& target, const Vector3& up);
  double at(int row, int col) const { return e[col * 4 + row]; }
  bool operator==(const Matrix4& o) const {
    for (int i = 0; i < 16; i++) if (e[i] != o.e[i]) return false;
    return true;
  }

  // transpose (three.js Matrix4.transpose)
  Matrix4& transposeImpl() {
    for (int i = 0; i < 4; i++) {
      int j = i * 4;
      int k = i + 1;
      for (int l = 4; l < 16; l += 4) {
        double t = e[j + l - 4];
        e[j + l - 4] = e[k];
        e[k] = t;
        k++;
      }
    }
    return *this;
  }

  // determinant (three.js Matrix4.determinant) — cofactor expansion on the
  // four 4x4 minors, written exactly like the JavaScript original.
  double determinantImpl() const {
    const double n11 = e[0], n12 = e[4], n13 = e[8], n14 = e[12];
    const double n21 = e[1], n22 = e[5], n23 = e[9], n24 = e[13];
    const double n31 = e[2], n32 = e[6], n33 = e[10], n34 = e[14];
    const double n41 = e[3], n42 = e[7], n43 = e[11], n44 = e[15];
    return (
      n41 * (+ n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34) +
      n42 * (+ n11 * n24 * n33 - n11 * n23 * n34 + n13 * n21 * n34 - n14 * n21 * n33 + n14 * n22 * n31 - n13 * n22 * n31) +
      n43 * (+ n11 * n23 * n34 - n11 * n24 * n33 + n14 * n21 * n33 - n12 * n21 * n34 - n14 * n22 * n31 + n12 * n24 * n31) +
      n44 * (- n13 * n22 * n31 - n11 * n24 * n32 + n11 * n23 * n34 + n14 * n22 * n31 - n13 * n24 * n32 + n12 * n23 * n34)
    );
  }

  // inverse (three.js Matrix4.invert)
  Matrix4& invertImpl() {
    const double a11 = e[0], a12 = e[4], a13 = e[8],  a14 = e[12];
    const double a21 = e[1], a22 = e[5], a23 = e[9],  a24 = e[13];
    const double a31 = e[2], a32 = e[6], a33 = e[10], a34 = e[14];
    const double a41 = e[3], a42 = e[7], a43 = e[11], a44 = e[15];
    const double b00 = a11 * a22 - a12 * a21;
    const double b01 = a11 * a23 - a13 * a21;
    const double b02 = a11 * a24 - a14 * a21;
    const double b03 = a12 * a23 - a13 * a22;
    const double b04 = a12 * a24 - a14 * a22;
    const double b05 = a13 * a24 - a14 * a23;
    const double b06 = a31 * a42 - a32 * a41;
    const double b07 = a31 * a43 - a33 * a41;
    const double b08 = a31 * a44 - a34 * a41;
    const double b09 = a32 * a43 - a33 * a42;
    const double b10 = a32 * a44 - a34 * a42;
    const double b11 = a33 * a44 - a34 * a43;
    double det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (det == 0) return identity();
    det = 1.0 / det;
    e[0]  = (a22 * b11 - a23 * b10 + a24 * b09) * det;
    e[4]  = (a13 * b10 - a12 * b11 - a14 * b09) * det;
    e[8]  = (a42 * b05 - a43 * b04 + a44 * b03) * det;
    e[12] = (a33 * b04 - a32 * b05 - a34 * b03) * det;
    e[1]  = (a23 * b08 - a21 * b11 - a24 * b07) * det;
    e[5]  = (a11 * b11 - a13 * b08 + a14 * b07) * det;
    e[9]  = (a43 * b02 - a41 * b05 - a44 * b01) * det;
    e[13] = (a31 * b05 - a33 * b02 + a34 * b01) * det;
    e[2]  = (a21 * b10 - a22 * b08 + a24 * b06) * det;
    e[6]  = (a12 * b08 - a11 * b10 - a14 * b06) * det;
    e[10] = (a41 * b04 - a42 * b02 + a44 * b00) * det;
    e[14] = (a32 * b02 - a31 * b04 - a34 * b00) * det;
    e[3]  = (a22 * b07 - a21 * b09 - a23 * b06) * det;
    e[7]  = (a11 * b09 - a12 * b07 + a13 * b06) * det;
    e[11] = (a42 * b01 - a41 * b03 - a43 * b00) * det;
    e[15] = (a31 * b03 - a32 * b01 + a33 * b00) * det;
    return *this;
  }
};

// ---------------------------------------------------------------------------
// Color (sRGB floats 0..1, like three.js Color)
// ---------------------------------------------------------------------------

struct Color {
  double r = 1, g = 1, b = 1;

  Color() = default;
  Color(double r_, double g_, double b_) : r(r_), g(g_), b(b_) {}

  Color& setHex(uint32_t hex) {
    r = ((hex >> 16) & 0xff) / 255.0;
    g = ((hex >> 8) & 0xff) / 255.0;
    b = (hex & 0xff) / 255.0;
    return *this;
  }
  Color& set(double r_, double g_, double b_) { r = r_; g = g_; b = b_; return *this; }
  Color& copy(const Color& c) { r = c.r; g = c.g; b = c.b; return *this; }
  Color& multiplyScalar(double s) { r *= s; g *= s; b *= s; return *this; }
  Color& lerp(const Color& c, double t) {
    r += (c.r - r) * t; g += (c.g - g) * t; b += (c.b - b) * t; return *this;
  }
  double getHex() const {
    uint32_t R = (uint32_t)std::lround(clampD(r, 0, 1) * 255.0);
    uint32_t G = (uint32_t)std::lround(clampD(g, 0, 1) * 255.0);
    uint32_t B = (uint32_t)std::lround(clampD(b, 0, 1) * 255.0);
    return (double)((R << 16) | (G << 8) | B);
  }
};

// ---------------------------------------------------------------------------
// MathUtils (the handful the game uses)
// ---------------------------------------------------------------------------

namespace MathUtils {
inline double degToRad(double d) { return d * DEG; }
inline double radToDeg(double r) { return r * RAD; }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double clamp(double v, double lo, double hi) { return clampD(v, lo, hi); }
/** three.js MathUtils.smoothstep */
inline double smoothstep(double x, double min, double max) {
  if (min >= max) return x < min ? 0 : 1;
  double t = clampD((x - min) / (max - min), 0, 1);
  return t * t * (3 - 2 * t);
}
}  // namespace MathUtils

// ---------------------------------------------------------------------------
// inline definitions that need the complete types
// ---------------------------------------------------------------------------

inline Vector3& Vector3::setFromMatrixPosition(const Matrix4& m) {
  x = m.e[12]; y = m.e[13]; z = m.e[14]; return *this;
}
inline Vector3& Vector3::setFromMatrixColumn(const Matrix4& m, int index) {
  x = m.e[index * 4]; y = m.e[index * 4 + 1]; z = m.e[index * 4 + 2]; return *this;
}
inline Vector3& Vector3::applyMatrix4(const Matrix4& m) {
  double xe = x, ye = y, ze = z;
  const double* e = m.e;
  double w = e[3] * xe + e[7] * ye + e[11] * ze + e[15];
  if (w == 0) w = 1.0;
  x = (e[0] * xe + e[4] * ye + e[8] * ze + e[12]) / w;
  y = (e[1] * xe + e[5] * ye + e[9] * ze + e[13]) / w;
  z = (e[2] * xe + e[6] * ye + e[10] * ze + e[14]) / w;
  return *this;
}
inline Vector3& Vector3::applyQuaternion(const Quaternion& q) {
  // three.js: v' = q v q*
  double ix = q.w * x + q.y * z - q.z * y;
  double iy = q.w * y + q.z * x - q.x * z;
  double iz = q.w * z + q.x * y - q.y * x;
  double iw = -q.x * x - q.y * y - q.z * z;
  x = ix * q.w + iw * -q.x + iy * -q.z - iz * -q.y;
  y = iy * q.w + iw * -q.y + iz * -q.x - ix * -q.z;
  z = iz * q.w + iw * -q.z + ix * -q.y - iy * -q.x;
  return *this;
}
inline Vector3& Vector3::applyEuler(const Euler& e) {
  Quaternion q;
  q.setFromEuler(e);
  return applyQuaternion(q);
}
inline Vector3& Vector3::applyAxisAngle(const Vector3& axis, double angle) {
  Quaternion q;
  q.setFromAxisAngle(axis, angle);
  return applyQuaternion(q);
}

inline Quaternion& Quaternion::setFromEuler(const Euler& e) {
  double c1 = std::cos(e.x / 2.0), s1 = std::sin(e.x / 2.0);
  double c2 = std::cos(e.y / 2.0), s2 = std::sin(e.y / 2.0);
  double c3 = std::cos(e.z / 2.0), s3 = std::sin(e.z / 2.0);
  if (e.order == Euler::XYZ) {
    x = s1 * c2 * c3 + c1 * s2 * s3;
    y = c1 * s2 * c3 - s1 * c2 * s3;
    z = c1 * c2 * s3 + s1 * s2 * c3;
    w = c1 * c2 * c3 - s1 * s2 * s3;
  } else {  // YXZ
    x = s1 * c2 * c3 + c1 * s2 * s3;
    y = c1 * s2 * c3 - s1 * c2 * s3;
    z = c1 * c2 * s3 - s1 * s2 * c3;
    w = c1 * c2 * c3 + s1 * s2 * s3;
  }
  return *this;
}
inline Quaternion& Quaternion::setFromUnitVectors(const Vector3& fromV, const Vector3& toV) {
  double r = fromV.dot(toV) + 1;
  if (r < 1e-8) {
    // opposite vectors: pick any perpendicular axis
    r = 0;
    if (std::fabs(fromV.x) > std::fabs(fromV.z)) {
      x = -fromV.y;
      y = fromV.x;
      z = 0;
    } else {
      x = 0;
      y = -fromV.z;
      z = fromV.y;
    }
  } else {
    x = fromV.y * toV.z - fromV.z * toV.y;
    y = fromV.z * toV.x - fromV.x * toV.z;
    z = fromV.x * toV.y - fromV.y * toV.x;
  }
  w = r;
  return normalize();
}

inline Quaternion& Quaternion::setFromRotationMatrix(const Matrix4& m) {
  // three.js Matrix4.extractRotation path: quaternion from a pure rotation matrix
  double te = m.e[0] + m.e[5] + m.e[10];
  if (te > 0) {
    double s = 0.5 / std::sqrt(te + 1.0);
    w = 0.25 / s;
    x = (m.e[6] - m.e[9]) * s;
    y = (m.e[8] - m.e[2]) * s;
    z = (m.e[1] - m.e[4]) * s;
  } else if (m.e[0] > m.e[5] && m.e[0] > m.e[10]) {
    double s = 2.0 * std::sqrt(1.0 + m.e[0] - m.e[5] - m.e[10]);
    w = (m.e[6] - m.e[9]) / (4.0 * s);
    x = 0.25 * s;
    y = (m.e[4] + m.e[1]) / s;
    z = (m.e[8] + m.e[2]) / s;
  } else if (m.e[5] > m.e[10]) {
    double s = 2.0 * std::sqrt(1.0 + m.e[5] - m.e[0] - m.e[10]);
    w = (m.e[8] - m.e[2]) / (4.0 * s);
    x = (m.e[4] + m.e[1]) / s;
    y = 0.25 * s;
    z = (m.e[9] + m.e[6]) / s;
  } else {
    double s = 2.0 * std::sqrt(1.0 + m.e[10] - m.e[0] - m.e[5]);
    w = (m.e[1] - m.e[4]) / (4.0 * s);
    x = (m.e[8] + m.e[2]) / s;
    y = (m.e[9] + m.e[6]) / s;
    z = 0.25 * s;
  }
  return *this;
}
inline Quaternion& Quaternion::slerp(const Quaternion& qb, double t) {
  if (t == 0) return *this;
  if (t == 1) return copy(qb);
  double x1 = x, y1 = y, z1 = z, w1 = w;
  double cosHalfTheta = w1 * qb.w + x1 * qb.x + y1 * qb.y + z1 * qb.z;
  if (cosHalfTheta < 0) {
    w1 = -w1; x1 = -x1; y1 = -y1; z1 = -z1;
    cosHalfTheta = -cosHalfTheta;
  }
  if (cosHalfTheta >= 1.0) return *this;
  double sqrSinHalfTheta = 1.0 - cosHalfTheta * cosHalfTheta;
  if (sqrSinHalfTheta <= std::numeric_limits<double>::epsilon()) {
    double s = 1.0 - t;
    x = s * x1 + t * qb.x;
    y = s * y1 + t * qb.y;
    z = s * z1 + t * qb.z;
    w = s * w1 + t * qb.w;
    return normalize();
  }
  double sinHalfTheta = std::sqrt(sqrSinHalfTheta);
  double halfTheta = std::atan2(sinHalfTheta, cosHalfTheta);
  double ratioA = std::sin((1.0 - t) * halfTheta) / sinHalfTheta;
  double ratioB = std::sin(t * halfTheta) / sinHalfTheta;
  x = x1 * ratioA + qb.x * ratioB;
  y = y1 * ratioA + qb.y * ratioB;
  z = z1 * ratioA + qb.z * ratioB;
  w = w1 * ratioA + qb.w * ratioB;
  return *this;
}

inline Matrix4& Matrix4::makeRotationFromEuler(const Euler& euler) {
  double a = std::cos(euler.x), b = std::sin(euler.x);
  double c = std::cos(euler.y), d = std::sin(euler.y);
  double ez = std::cos(euler.z), fz = std::sin(euler.z);
  if (euler.order == Euler::XYZ) {
    double ae = a * ez, af = a * fz, be = b * ez, bf = b * fz;
    e[0] = c * ez;
    e[4] = -c * fz;
    e[8] = d;
    e[1] = af + be * d;
    e[5] = ae - bf * d;
    e[9] = -b * c;
    e[2] = bf - ae * d;
    e[6] = be + af * d;
    e[10] = a * c;
  } else if (euler.order == Euler::YXZ) {
    double ce = c * ez, cf = c * fz, de = d * ez, df = d * fz;
    e[0] = ce + df * b;
    e[4] = cf * b - de;
    e[8] = a * d;
    e[1] = a * fz;
    e[5] = a * ez;
    e[9] = -b;
    e[2] = de * b - cf;
    e[6] = df + ce * b;
    e[10] = a * c;
  }
  e[3] = 0; e[7] = 0; e[11] = 0;
  e[12] = 0; e[13] = 0; e[14] = 0; e[15] = 1;
  return *this;
}

inline Matrix4& Matrix4::makeRotationFromQuaternion(const Quaternion& q) {
  double te0 = 2.0 * q.x * q.x, te1 = 2.0 * q.y * q.y, te2 = 2.0 * q.z * q.z;
  double te3 = 2.0 * q.x * q.y, te4 = 2.0 * q.y * q.z, te5 = 2.0 * q.x * q.z;
  double te6 = 2.0 * q.w * q.x, te7 = 2.0 * q.w * q.y, te8 = 2.0 * q.w * q.z;
  e[0] = 1.0 - te1 - te2;
  e[4] = te3 - te8;
  e[8] = te5 + te7;
  e[1] = te3 + te8;
  e[5] = 1.0 - te0 - te2;
  e[9] = te4 - te6;
  e[2] = te5 - te7;
  e[6] = te4 + te6;
  e[10] = 1.0 - te0 - te1;
  e[3] = 0; e[7] = 0; e[11] = 0;
  e[12] = 0; e[13] = 0; e[14] = 0; e[15] = 1;
  return *this;
}

inline Matrix4& Matrix4::multiplyMatrices(const Matrix4& a, const Matrix4& b) {
  const double* ae = a.e;
  const double* be = b.e;
  double* te = e;
  double a11 = ae[0], a12 = ae[4], a13 = ae[8], a14 = ae[12];
  double a21 = ae[1], a22 = ae[5], a23 = ae[9], a24 = ae[13];
  double a31 = ae[2], a32 = ae[6], a33 = ae[10], a34 = ae[14];
  double a41 = ae[3], a42 = ae[7], a43 = ae[11], a44 = ae[15];
  double b11 = be[0], b12 = be[4], b13 = be[8], b14 = be[12];
  double b21 = be[1], b22 = be[5], b23 = be[9], b24 = be[13];
  double b31 = be[2], b32 = be[6], b33 = be[10], b34 = be[14];
  double b41 = be[3], b42 = be[7], b43 = be[11], b44 = be[15];
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


inline Matrix4 Matrix4::decompose(Vector3& position, Quaternion& quaternion, Vector3& scale) const {
  Matrix4 out;
  position.set(e[12], e[13], e[14]);
  double sx = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
  double sy = std::sqrt(e[4] * e[4] + e[5] * e[5] + e[6] * e[6]);
  double sz = std::sqrt(e[8] * e[8] + e[9] * e[9] + e[10] * e[10]);
  if (e[0] < 0) sx = -sx;
  if (e[5] < 0) sy = -sy;
  if (e[10] < 0) sz = -sz;
  scale.set(sx, sy, sz);
  for (int i = 0; i < 16; i++) out.e[i] = e[i];
  double invSX = sx != 0 ? 1.0 / sx : 0;
  double invSY = sy != 0 ? 1.0 / sy : 0;
  double invSZ = sz != 0 ? 1.0 / sz : 0;
  out.e[0] *= invSX; out.e[1] *= invSX; out.e[2] *= invSX;
  out.e[4] *= invSY; out.e[5] *= invSY; out.e[6] *= invSY;
  out.e[8] *= invSZ; out.e[9] *= invSZ; out.e[10] *= invSZ;
  quaternion.setFromRotationMatrix(out);
  return out;
}

inline Matrix4& Matrix4::makePerspective(double left, double right, double top, double bottom, double near, double far) {
  double x = 2.0 * near / (right - left);
  double y = 2.0 * near / (top - bottom);
  double a = (right + left) / (right - left);
  double b = (top + bottom) / (top - bottom);
  double c = -(far + near) / (far - near);
  double d = -2.0 * far * near / (far - near);
  e[0] = x; e[4] = 0; e[8] = a; e[12] = 0;
  e[1] = 0; e[5] = y; e[9] = b; e[13] = 0;
  e[2] = 0; e[6] = 0; e[10] = c; e[14] = d;
  e[3] = 0; e[7] = 0; e[11] = -1; e[15] = 0;
  return *this;
}

inline Matrix4& Matrix4::makeOrthographic(double left, double right, double top, double bottom, double near, double far) {
  double w = 1.0 / (right - left);
  double h = 1.0 / (top - bottom);
  double p = 1.0 / (far - near);
  e[0] = 2.0 * w; e[4] = 0; e[8] = 0; e[12] = -(right + left) * w;
  e[1] = 0; e[5] = 2.0 * h; e[9] = 0; e[13] = -(top + bottom) * h;
  e[2] = 0; e[6] = 0; e[10] = -2.0 * p; e[14] = -(far + near) * p;
  e[3] = 0; e[7] = 0; e[11] = 0; e[15] = 1;
  return *this;
}

inline Matrix4& Matrix4::lookAt(const Vector3& eye, const Vector3& target, const Vector3& up) {
  Vector3 z;
  z.subVectors(eye, target);
  if (z.lengthSq() == 0) z.z = 1;
  z.normalize();
  Vector3 x;
  x.crossVectors(up, z);
  if (x.lengthSq() == 0) {
    // degenerate: nudge one axis
    if (std::fabs(z.z) == 1.0) x.x += 1e-4;
    else x.z += 1e-4;
    x.normalize();
  }
  x.normalize();
  Vector3 y;
  y.crossVectors(z, x);
  e[0] = x.x; e[4] = y.x; e[8] = z.x;
  e[1] = x.y; e[5] = y.y; e[9] = z.y;
  e[2] = x.z; e[6] = y.z; e[10] = z.z;
  return *this;
}

}  // namespace fs
