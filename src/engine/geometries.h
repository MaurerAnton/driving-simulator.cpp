// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The geometries of three.js as functions: the same vertices, normals, uvs, indices
 * and groups in the same order, with three.js's parameters in their order and with
 * their defaults –
 *
 *     new THREE.BoxGeometry(w, h, d)                    boxGeometry(w, h, d)
 *     new THREE.CylinderGeometry(r, r, h, 12, 1, true)  cylinderGeometry(r, r, h, 12, 1, true)
 *     new THREE.ExtrudeGeometry(shape, { depth: d, bevelEnabled: false })
 *                                                       extrudeGeometry(shape, {.depth = d, .bevelEnabled = false})
 *     mergeGeometries(parts, false)                     mergeGeometries(parts, false)
 *
 * – each a plain BufferGeometry (three.js's `geometry.parameters` are not kept).
 *
 * Origin: three.js r187 src/geometries/{Box,Plane,Cylinder,Cone,Sphere,Torus,Circle,Ring,Lathe,
 * Polyhedron,Icosahedron,Tube,Shape,Extrude}Geometry.js, examples/jsm/utils/BufferGeometryUtils.js
 * (mergeGeometries, mergeAttributes, mergeVertices)
 */
#pragma once

#include <optional>
#include <vector>

#include "engine/curves.h"
#include "engine/engine.h"

namespace fs {

/** a box about the origin: six sides, a group each (+x, -x, +y, -y, +z, -z) */
GeometryPtr boxGeometry(double width = 1, double height = 1, double depth = 1, int widthSegments = 1, int heightSegments = 1, int depthSegments = 1);
/** a sheet in the xy plane, facing +z */
GeometryPtr planeGeometry(double width = 1, double height = 1, int widthSegments = 1, int heightSegments = 1);
/** a cylinder along y: the groups are its side, its top and its bottom */
GeometryPtr cylinderGeometry(double radiusTop = 1, double radiusBottom = 1, double height = 1, int radialSegments = 32, int heightSegments = 1, bool openEnded = false,
                             double thetaStart = 0, double thetaLength = PI * 2);
GeometryPtr coneGeometry(double radius = 1, double height = 1, int radialSegments = 32, int heightSegments = 1, bool openEnded = false, double thetaStart = 0,
                         double thetaLength = PI * 2);
GeometryPtr sphereGeometry(double radius = 1, int widthSegments = 32, int heightSegments = 16, double phiStart = 0, double phiLength = PI * 2, double thetaStart = 0,
                           double thetaLength = PI);
/** a ring of a tube, in the xy plane */
GeometryPtr torusGeometry(double radius = 1, double tube = 0.4, int radialSegments = 12, int tubularSegments = 48, double arc = PI * 2, double thetaStart = 0,
                          double thetaLength = PI * 2);
/** a disc in the xy plane, facing +z */
GeometryPtr circleGeometry(double radius = 1, int segments = 32, double thetaStart = 0, double thetaLength = PI * 2);
GeometryPtr ringGeometry(double innerRadius = 0.5, double outerRadius = 1, int thetaSegments = 32, int phiSegments = 1, double thetaStart = 0, double thetaLength = PI * 2);
/** a profile (x: the distance from the y axis, y: the height) turned about the y axis */
GeometryPtr latheGeometry(const std::vector<Vector2>& points, int segments = 12, double phiStart = 0, double phiLength = PI * 2);
/** the faces of a solid thrown onto a sphere, each divided `detail` times (no index) */
GeometryPtr polyhedronGeometry(const std::vector<double>& vertices, const std::vector<int>& indices, double radius = 1, int detail = 0);
GeometryPtr icosahedronGeometry(double radius = 1, int detail = 0);
/** a tube along a curve in space */
GeometryPtr tubeGeometry(const Curve<Vector3>& path, int tubularSegments = 64, double radius = 1, int radialSegments = 8, bool closed = false);
/** a shape (or several: a group each) filled, in the xy plane */
GeometryPtr shapeGeometry(const Shape& shape, int curveSegments = 12);
GeometryPtr shapeGeometry(const std::vector<Shape>& shapes, int curveSegments = 12);

/** the options of ExtrudeGeometry, with three.js's defaults */
struct ExtrudeOptions {
  /** points to a curve of the shape */
  int curveSegments = 12;
  /** layers along the depth (or along the path) */
  int steps = 1;
  double depth = 1;
  bool bevelEnabled = true;
  double bevelThickness = 0.2;
  /** not given: bevelThickness - 0.1 */
  std::optional<double> bevelSize{};
  double bevelOffset = 0;
  int bevelSegments = 3;
  /** a curve in space to extrude along instead of straight in z (no bevel then) */
  const Curve<Vector3>* extrudePath = nullptr;
};
/** a shape (or several) pulled into the depth: group 0 its two faces, group 1 its sides (no index) */
GeometryPtr extrudeGeometry(const Shape& shape, const ExtrudeOptions& options = ExtrudeOptions());
GeometryPtr extrudeGeometry(const std::vector<Shape>& shapes, const ExtrudeOptions& options = ExtrudeOptions());

/**
 * BufferGeometryUtils.mergeGeometries: one geometry of several that have the same attributes and all an
 * index or none (null when they do not fit together); `useGroups`: a group for each of them.
 */
GeometryPtr mergeGeometries(const std::vector<GeometryPtr>& geometries, bool useGroups = false);
/** BufferGeometryUtils.mergeAttributes (null when the kinds of array, item sizes or `normalized` differ) */
AttrPtr mergeAttributes(const std::vector<AttrPtr>& attributes);
/** BufferGeometryUtils.mergeVertices: vertices whose every attribute is alike within the tolerance become one; the result has an index */
GeometryPtr mergeVertices(const BufferGeometry& geometry, double tolerance = 1e-4);
inline GeometryPtr mergeVertices(const GeometryPtr& geometry, double tolerance = 1e-4) { return mergeVertices(*geometry, tolerance); }

}  // namespace fs
