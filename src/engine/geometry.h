// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Geometry and attributes: BufferGeometry, BufferAttribute, and the primitive
 * geometries the game builds shapes from (Box, Plane, Sphere, Cylinder, Cone,
 * Circle, Torus, Icosahedron, Extrude/Shape).
 *
 * Origin: three.js (core/BufferGeometry.js, core/BufferAttribute.js,
 * geometries/ as used by the TypeScript game.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/math.h"

namespace fs {

// ---------------------------------------------------------------------------
// BufferAttribute
// ---------------------------------------------------------------------------

class BufferAttribute {
 public:
  std::vector<double> array;  // doubles: the game's numbers are all double-precision
  int itemSize = 1;
  bool normalized = false;
  std::string name;
  int version = 0;

  BufferAttribute() = default;
  BufferAttribute(std::vector<double> a, int item) : array(std::move(a)), itemSize(item) {}

  size_t count() const { return itemSize > 0 ? array.size() / static_cast<size_t>(itemSize) : 0; }

  double getX(size_t i) const { return array[i * static_cast<size_t>(itemSize)]; }
  double getY(size_t i) const { return array[i * static_cast<size_t>(itemSize) + 1]; }
  double getZ(size_t i) const { return array[i * static_cast<size_t>(itemSize) + 2]; }
  double getW(size_t i) const { return array[i * static_cast<size_t>(itemSize) + 3]; }

  void setXYZ(size_t i, double x, double y, double z) {
    size_t o = i * static_cast<size_t>(itemSize);
    array[o] = x; array[o + 1] = y; array[o + 2] = z;
  }
  void setXYZW(size_t i, double x, double y, double z, double w) {
    size_t o = i * static_cast<size_t>(itemSize);
    array[o] = x; array[o + 1] = y; array[o + 2] = z; array[o + 3] = w;
  }
  void setXY(size_t i, double x, double y) {
    size_t o = i * static_cast<size_t>(itemSize);
    array[o] = x; array[o + 1] = y;
  }
  void setScalar(size_t i, double v) { array[i * static_cast<size_t>(itemSize)] = v; }

  /** three.js BufferAttribute.setUsage / needsUpdate are bookkeeping only here. */
  void needsUpdate() { version++; }
};

// ---------------------------------------------------------------------------
// BufferGeometry
// ---------------------------------------------------------------------------

class BufferGeometry {
 public:
  std::shared_ptr<BufferAttribute> position;
  std::shared_ptr<BufferAttribute> normal;
  std::shared_ptr<BufferAttribute> uv;
  std::shared_ptr<BufferAttribute> color;
  std::shared_ptr<BufferAttribute> index;  // optional triangle indices
  std::string name;

  BufferGeometry() = default;

  void setAttribute(const std::string& n, std::shared_ptr<BufferAttribute> a) {
    if (n == "position") position = std::move(a);
    else if (n == "normal") normal = std::move(a);
    else if (n == "uv") uv = std::move(a);
    else if (n == "color") color = std::move(a);
    else if (n == "index") index = std::move(a);
  }

  std::shared_ptr<BufferAttribute> getAttribute(const std::string& n) const {
    if (n == "position") return position;
    if (n == "normal") return normal;
    if (n == "uv") return uv;
    if (n == "color") return color;
    if (n == "index") return index;
    return nullptr;
  }

  void deleteAttribute(const std::string& n) {
    if (n == "position") position.reset();
    else if (n == "normal") normal.reset();
    else if (n == "uv") uv.reset();
    else if (n == "color") color.reset();
    else if (n == "index") index.reset();
  }

  /** Drop the triangle index and expand to per-vertex data (three.js toNonIndexed). */
  std::shared_ptr<BufferGeometry> toNonIndexed() const;

  void computeVertexNormals();
  void translate(double x, double y, double z);
  void scale(double x, double y, double z);
  void rotateX(double a);
  void rotateY(double a);
  void rotateZ(double a);
  void center();

  /** Axis-aligned bounds in local space (three.js computeBoundingBox). */
  void computeBoundingBox(Vector3& outMin, Vector3& outMax) const;
};

using GeometryPtr = std::shared_ptr<BufferGeometry>;

// ---------------------------------------------------------------------------
// Primitive geometries
// ---------------------------------------------------------------------------

/** BoxGeometry(width, height, depth, wSeg, hSeg, dSeg). */
GeometryPtr makeBoxGeometry(double width = 1, double height = 1, double depth = 1,
                            int widthSegments = 1, int heightSegments = 1, int depthSegments = 1);

/** PlaneGeometry(width, height, wSeg, hSeg) — lies in the XY plane. */
GeometryPtr makePlaneGeometry(double width = 1, double height = 1,
                              int widthSegments = 1, int heightSegments = 1);

/** SphereGeometry(radius, widthSegments, heightSegments, phiStart, phiLength, thetaStart, thetaLength). */
GeometryPtr makeSphereGeometry(double radius = 1, int widthSegments = 32, int heightSegments = 16,
                               double phiStart = 0, double phiLength = TAU,
                               double thetaStart = 0, double thetaLength = PI);

/** CylinderGeometry(radiusTop, radiusBottom, height, radialSegments, heightSegments, openEnded). */
GeometryPtr makeCylinderGeometry(double radiusTop = 1, double radiusBottom = 1, double height = 1,
                                 int radialSegments = 32, int heightSegments = 1,
                                 bool openEnded = false);

/** ConeGeometry(radius, height, radialSegments, heightSegments, openEnded). */
GeometryPtr makeConeGeometry(double radius = 1, double height = 1, int radialSegments = 32,
                             int heightSegments = 1, bool openEnded = false);

/** CircleGeometry(radius, segments, thetaStart, thetaLength). */
GeometryPtr makeCircleGeometry(double radius = 1, int segments = 32,
                               double thetaStart = 0, double thetaLength = TAU);

/** TorusGeometry(radius, tube, radialSegments, tubularSegments, arc). */
GeometryPtr makeTorusGeometry(double radius = 1, double tube = 0.4,
                              int radialSegments = 12, int tubularSegments = 48,
                              double arc = TAU);

/** IcosahedronGeometry(radius, detail). */
GeometryPtr makeIcosahedronGeometry(double radius = 1, int detail = 0);

/** Merge geometries into one (three.js mergeGeometries, non-indexed result). */
GeometryPtr mergeGeometries(const std::vector<GeometryPtr>& geometries, bool useGroups = false);

/** Merge geometries and then recompute the vertex normals (the game's helper). */
GeometryPtr mergeAndComputeNormals(const std::vector<GeometryPtr>& geometries);

}  // namespace fs
