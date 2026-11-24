// SPDX-License-Identifier: AGPL-3.0-only
// Origin: three.js r187 src/geometries/{Box,Plane,Cylinder,Cone,Sphere,Torus,Circle,Ring,Lathe,Polyhedron,Icosahedron,
//         Tube,Shape,Extrude}Geometry.js, examples/jsm/utils/BufferGeometryUtils.js (mergeGeometries, mergeAttributes,
//         mergeVertices)
#include "engine/geometries.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace fs {

namespace {

/** `new Float32BufferAttribute(array, itemSize)`: the numbers rounded to single precision */
AttrPtr floatAttribute(const std::vector<double>& array, int itemSize) {
  std::vector<float> a(array.size());
  for (size_t i = 0; i < array.size(); i++) a[i] = static_cast<float>(array[i]);
  return attr(std::move(a), itemSize);
}

void push(std::vector<double>& a, double x, double y) {
  a.push_back(x);
  a.push_back(y);
}
void push(std::vector<double>& a, double x, double y, double z) {
  a.push_back(x);
  a.push_back(y);
  a.push_back(z);
}
void push(std::vector<uint32_t>& a, long x, long y, long z) {
  a.push_back(static_cast<uint32_t>(x));
  a.push_back(static_cast<uint32_t>(y));
  a.push_back(static_cast<uint32_t>(z));
}
/** i / n as JavaScript divides */
double div(long i, long n) { return static_cast<double>(i) / static_cast<double>(n); }

}  // namespace

// ---------------------------------------------------------------------------
// BoxGeometry
// ---------------------------------------------------------------------------

GeometryPtr boxGeometry(double width, double height, double depth, int widthSegments, int heightSegments, int depthSegments) {
  auto scope = make<BufferGeometry>();
  // buffers
  std::vector<uint32_t> indices;
  std::vector<double> vertices, normals, uvs;
  // helper variables
  long numberOfVertices = 0;
  int groupStart = 0;
  const auto buildPlane = [&](int u, int v, int w, double udir, double vdir, double width_, double height_, double depth_, int gridX, int gridY, int materialIndex) {
    const double segmentWidth = width_ / gridX;
    const double segmentHeight = height_ / gridY;
    const double widthHalf = width_ / 2;
    const double heightHalf = height_ / 2;
    const double depthHalf = depth_ / 2;
    const int gridX1 = gridX + 1;
    const int gridY1 = gridY + 1;
    long vertexCounter = 0;
    int groupCount = 0;
    Vector3 vector;
    // generate vertices, normals and uvs
    for (int iy = 0; iy < gridY1; iy++) {
      const double y = iy * segmentHeight - heightHalf;
      for (int ix = 0; ix < gridX1; ix++) {
        const double x = ix * segmentWidth - widthHalf;
        // set values to correct vector component
        vector.setComponent(u, x * udir);
        vector.setComponent(v, y * vdir);
        vector.setComponent(w, depthHalf);
        // now apply vector to vertex buffer
        push(vertices, vector.x, vector.y, vector.z);
        // set values to correct vector component
        vector.setComponent(u, 0);
        vector.setComponent(v, 0);
        vector.setComponent(w, depth_ > 0 ? 1 : -1);
        // now apply vector to normal buffer
        push(normals, vector.x, vector.y, vector.z);
        // uvs
        uvs.push_back(div(ix, gridX));
        uvs.push_back(1 - div(iy, gridY));
        // counters
        vertexCounter += 1;
      }
    }
    // indices
    // 1. you need three indices to draw a single face
    // 2. a single segment consists of two faces
    // 3. so we need to generate six (2*3) indices per segment
    for (int iy = 0; iy < gridY; iy++) {
      for (int ix = 0; ix < gridX; ix++) {
        const long a = numberOfVertices + ix + gridX1 * iy;
        const long b = numberOfVertices + ix + gridX1 * (iy + 1);
        const long c = numberOfVertices + (ix + 1) + gridX1 * (iy + 1);
        const long d = numberOfVertices + (ix + 1) + gridX1 * iy;
        // faces
        push(indices, a, b, d);
        push(indices, b, c, d);
        // increase counter
        groupCount += 6;
      }
    }
    // add a group to the geometry. this will ensure multi material support
    scope->addGroup(groupStart, groupCount, materialIndex);
    // calculate new start value for groups
    groupStart += groupCount;
    // update total number of vertices
    numberOfVertices += vertexCounter;
  };
  // build each side of the box geometry
  const int X = 0, Y = 1, Z = 2;
  buildPlane(Z, Y, X, -1, -1, depth, height, width, depthSegments, heightSegments, 0);   // px
  buildPlane(Z, Y, X, 1, -1, depth, height, -width, depthSegments, heightSegments, 1);   // nx
  buildPlane(X, Z, Y, 1, 1, width, depth, height, widthSegments, depthSegments, 2);   // py
  buildPlane(X, Z, Y, 1, -1, width, depth, -height, widthSegments, depthSegments, 3);   // ny
  buildPlane(X, Y, Z, 1, -1, width, height, depth, widthSegments, heightSegments, 4);   // pz
  buildPlane(X, Y, Z, -1, -1, width, height, -depth, widthSegments, heightSegments, 5);   // nz
  // build geometry
  scope->setIndex(indices);
  scope->setAttribute("position", floatAttribute(vertices, 3));
  scope->setAttribute("normal", floatAttribute(normals, 3));
  scope->setAttribute("uv", floatAttribute(uvs, 2));
  return scope;
}

// ---------------------------------------------------------------------------
// PlaneGeometry
// ---------------------------------------------------------------------------

GeometryPtr planeGeometry(double width, double height, int widthSegments, int heightSegments) {
  auto geometry = make<BufferGeometry>();
  const double width_half = width / 2;
  const double height_half = height / 2;
  const int gridX = widthSegments;
  const int gridY = heightSegments;
  const int gridX1 = gridX + 1;
  const int gridY1 = gridY + 1;
  const double segment_width = width / gridX;
  const double segment_height = height / gridY;
  std::vector<uint32_t> indices;
  std::vector<double> vertices, normals, uvs;
  for (int iy = 0; iy < gridY1; iy++) {
    const double y = iy * segment_height - height_half;
    for (int ix = 0; ix < gridX1; ix++) {
      const double x = ix * segment_width - width_half;
      push(vertices, x, -y, 0);
      push(normals, 0, 0, 1);
      uvs.push_back(div(ix, gridX));
      uvs.push_back(1 - div(iy, gridY));
    }
  }
  for (int iy = 0; iy < gridY; iy++) {
    for (int ix = 0; ix < gridX; ix++) {
      const long a = ix + gridX1 * iy;
      const long b = ix + gridX1 * (iy + 1);
      const long c = (ix + 1) + gridX1 * (iy + 1);
      const long d = (ix + 1) + gridX1 * iy;
      push(indices, a, b, d);
      push(indices, b, c, d);
    }
  }
  geometry->setIndex(indices);
  geometry->setAttribute("position", floatAttribute(vertices, 3));
  geometry->setAttribute("normal", floatAttribute(normals, 3));
  geometry->setAttribute("uv", floatAttribute(uvs, 2));
  return geometry;
}

// ---------------------------------------------------------------------------
// CylinderGeometry, ConeGeometry
// ---------------------------------------------------------------------------

GeometryPtr cylinderGeometry(double radiusTop, double radiusBottom, double height, int radialSegments, int heightSegments, bool openEnded, double thetaStart,
                             double thetaLength) {
  auto scope = make<BufferGeometry>();
  // buffers
  std::vector<uint32_t> indices;
  std::vector<double> vertices, normals, uvs;
  // helper variables
  long index = 0;
  std::vector<std::vector<long>> indexArray;
  const double halfHeight = height / 2;
  int groupStart = 0;

  const auto generateTorso = [&]() {
    Vector3 normal;
    Vector3 vertex;
    int groupCount = 0;
    // this will be used to calculate the normal
    const double slope = (radiusBottom - radiusTop) / height;
    // generate vertices, normals and uvs
    for (int y = 0; y <= heightSegments; y++) {
      std::vector<long> indexRow;
      const double v = div(y, heightSegments);
      // calculate the radius of the current row
      const double radius = v * (radiusBottom - radiusTop) + radiusTop;
      for (int x = 0; x <= radialSegments; x++) {
        const double u = div(x, radialSegments);
        const double theta = u * thetaLength + thetaStart;
        const double sinTheta = std::sin(theta);
        const double cosTheta = std::cos(theta);
        // vertex
        vertex.x = radius * sinTheta;
        vertex.y = -v * height + halfHeight;
        vertex.z = radius * cosTheta;
        push(vertices, vertex.x, vertex.y, vertex.z);
        // normal
        normal.set(sinTheta, slope, cosTheta).normalize();
        push(normals, normal.x, normal.y, normal.z);
        // uv
        push(uvs, u, 1 - v);
        // save index of vertex in respective row
        indexRow.push_back(index++);
      }
      // now save vertices of the row in our index array
      indexArray.push_back(std::move(indexRow));
    }
    // generate indices
    for (int x = 0; x < radialSegments; x++) {
      for (int y = 0; y < heightSegments; y++) {
        // we use the index array to access the correct indices
        const long a = indexArray[static_cast<size_t>(y)][static_cast<size_t>(x)];
        const long b = indexArray[static_cast<size_t>(y + 1)][static_cast<size_t>(x)];
        const long c = indexArray[static_cast<size_t>(y + 1)][static_cast<size_t>(x + 1)];
        const long d = indexArray[static_cast<size_t>(y)][static_cast<size_t>(x + 1)];
        // faces
        if (radiusTop > 0 || y != 0) {
          push(indices, a, b, d);
          groupCount += 3;
        }
        if (radiusBottom > 0 || y != heightSegments - 1) {
          push(indices, b, c, d);
          groupCount += 3;
        }
      }
    }
    // add a group to the geometry. this will ensure multi material support
    scope->addGroup(groupStart, groupCount, 0);
    // calculate new start value for groups
    groupStart += groupCount;
  };

  const auto generateCap = [&](bool top) {
    // save the index of the first center vertex
    const long centerIndexStart = index;
    Vector2 uv;
    Vector3 vertex;
    int groupCount = 0;
    const double radius = (top == true) ? radiusTop : radiusBottom;
    const double sign = (top == true) ? 1 : -1;
    // first we generate the center vertex data of the cap.
    // because the geometry needs one set of uvs per face,
    // we must generate a center vertex per face/segment
    for (int x = 1; x <= radialSegments; x++) {
      // vertex
      push(vertices, 0, halfHeight * sign, 0);
      // normal
      push(normals, 0, sign, 0);
      // uv
      push(uvs, 0.5, 0.5);
      // increase index
      index++;
    }
    // save the index of the last center vertex
    const long centerIndexEnd = index;
    // now we generate the surrounding vertices, normals and uvs
    for (int x = 0; x <= radialSegments; x++) {
      const double u = div(x, radialSegments);
      const double theta = u * thetaLength + thetaStart;
      const double cosTheta = std::cos(theta);
      const double sinTheta = std::sin(theta);
      // vertex
      vertex.x = radius * sinTheta;
      vertex.y = halfHeight * sign;
      vertex.z = radius * cosTheta;
      push(vertices, vertex.x, vertex.y, vertex.z);
      // normal
      push(normals, 0, sign, 0);
      // uv
      uv.x = (cosTheta * 0.5) + 0.5;
      uv.y = (sinTheta * 0.5 * sign) + 0.5;
      push(uvs, uv.x, uv.y);
      // increase index
      index++;
    }
    // generate indices
    for (int x = 0; x < radialSegments; x++) {
      const long c = centerIndexStart + x;
      const long i = centerIndexEnd + x;
      if (top == true) {
        // face top
        push(indices, i, i + 1, c);
      } else {
        // face bottom
        push(indices, i + 1, i, c);
      }
      groupCount += 3;
    }
    // add a group to the geometry. this will ensure multi material support
    scope->addGroup(groupStart, groupCount, top == true ? 1 : 2);
    // calculate new start value for groups
    groupStart += groupCount;
  };

  // generate geometry
  generateTorso();
  if (openEnded == false) {
    if (radiusTop > 0) generateCap(true);
    if (radiusBottom > 0) generateCap(false);
  }
  // build geometry
  scope->setIndex(indices);
  scope->setAttribute("position", floatAttribute(vertices, 3));
  scope->setAttribute("normal", floatAttribute(normals, 3));
  scope->setAttribute("uv", floatAttribute(uvs, 2));
  return scope;
}

GeometryPtr coneGeometry(double radius, double height, int radialSegments, int heightSegments, bool openEnded, double thetaStart, double thetaLength) {
  return cylinderGeometry(0, radius, height, radialSegments, heightSegments, openEnded, thetaStart, thetaLength);
}

// ---------------------------------------------------------------------------
// SphereGeometry
// ---------------------------------------------------------------------------

GeometryPtr sphereGeometry(double radius, int widthSegments, int heightSegments, double phiStart, double phiLength, double thetaStart, double thetaLength) {
  auto geometry = make<BufferGeometry>();
  widthSegments = std::max(3, widthSegments);
  heightSegments = std::max(2, heightSegments);
  const double thetaEnd = jsMin(thetaStart + thetaLength, PI);
  long index = 0;
  std::vector<std::vector<long>> grid;
  Vector3 vertex;
  Vector3 normal;
  // buffers
  std::vector<uint32_t> indices;
  std::vector<double> vertices, normals, uvs;
  // generate vertices, normals and uvs
  for (int iy = 0; iy <= heightSegments; iy++) {
    std::vector<long> verticesRow;
    const double v = div(iy, heightSegments);
    const double theta = thetaStart + v * thetaLength;
    const double y = radius * std::cos(theta);
    const double ringRadius = std::sqrt(radius * radius - y * y);
    // special case for the poles
    double uOffset = 0;
    if (iy == 0 && thetaStart == 0) {
      uOffset = 0.5 / widthSegments;
    } else if (iy == heightSegments && thetaEnd == PI) {
      uOffset = -0.5 / widthSegments;
    }
    for (int ix = 0; ix <= widthSegments; ix++) {
      const double u = div(ix, widthSegments);
      const double phi = phiStart + u * phiLength;
      // vertex
      vertex.x = -ringRadius * std::cos(phi);
      vertex.y = y;
      vertex.z = ringRadius * std::sin(phi);
      push(vertices, vertex.x, vertex.y, vertex.z);
      // normal
      normal.copy(vertex).normalize();
      push(normals, normal.x, normal.y, normal.z);
      // uv
      push(uvs, u + uOffset, 1 - v);
      verticesRow.push_back(index++);
    }
    grid.push_back(std::move(verticesRow));
  }
  // indices
  for (int iy = 0; iy < heightSegments; iy++) {
    for (int ix = 0; ix < widthSegments; ix++) {
      const long a = grid[static_cast<size_t>(iy)][static_cast<size_t>(ix + 1)];
      const long b = grid[static_cast<size_t>(iy)][static_cast<size_t>(ix)];
      const long c = grid[static_cast<size_t>(iy + 1)][static_cast<size_t>(ix)];
      const long d = grid[static_cast<size_t>(iy + 1)][static_cast<size_t>(ix + 1)];
      if (iy != 0 || thetaStart > 0) push(indices, a, b, d);
      if (iy != heightSegments - 1 || thetaEnd < PI) push(indices, b, c, d);
    }
  }
  // build geometry
  geometry->setIndex(indices);
  geometry->setAttribute("position", floatAttribute(vertices, 3));
  geometry->setAttribute("normal", floatAttribute(normals, 3));
  geometry->setAttribute("uv", floatAttribute(uvs, 2));
  return geometry;
}

// ---------------------------------------------------------------------------
// TorusGeometry
// ---------------------------------------------------------------------------

GeometryPtr torusGeometry(double radius, double tube, int radialSegments, int tubularSegments, double arc, double thetaStart, double thetaLength) {
  auto geometry = make<BufferGeometry>();
  // buffers
  std::vector<uint32_t> indices;
  std::vector<double> vertices, normals, uvs;
  // helper variables
  Vector3 center;
  Vector3 vertex;
  Vector3 normal;
  // generate vertices, normals and uvs
  for (int j = 0; j <= radialSegments; j++) {
    const double v = thetaStart + div(j, radialSegments) * thetaLength;
    for (int i = 0; i <= tubularSegments; i++) {
      const double u = div(i, tubularSegments) * arc;
      // vertex
      vertex.x = (radius + tube * std::cos(v)) * std::cos(u);
      vertex.y = (radius + tube * std::cos(v)) * std::sin(u);
      vertex.z = tube * std::sin(v);
      push(vertices, vertex.x, vertex.y, vertex.z);
      // normal
      center.x = radius * std::cos(u);
      center.y = radius * std::sin(u);
      normal.subVectors(vertex, center).normalize();
      push(normals, normal.x, normal.y, normal.z);
      // uv
      uvs.push_back(div(i, tubularSegments));
      uvs.push_back(div(j, radialSegments));
    }
  }
  // generate indices
  for (int j = 1; j <= radialSegments; j++) {
    for (int i = 1; i <= tubularSegments; i++) {
      // indices
      const long a = static_cast<long>(tubularSegments + 1) * j + i - 1;
      const long b = static_cast<long>(tubularSegments + 1) * (j - 1) + i - 1;
      const long c = static_cast<long>(tubularSegments + 1) * (j - 1) + i;
      const long d = static_cast<long>(tubularSegments + 1) * j + i;
      // faces
      push(indices, a, b, d);
      push(indices, b, c, d);
    }
  }
  // build geometry
  geometry->setIndex(indices);
  geometry->setAttribute("position", floatAttribute(vertices, 3));
  geometry->setAttribute("normal", floatAttribute(normals, 3));
  geometry->setAttribute("uv", floatAttribute(uvs, 2));
  return geometry;
}

// ---------------------------------------------------------------------------
// CircleGeometry
// ---------------------------------------------------------------------------

GeometryPtr circleGeometry(double radius, int segments, double thetaStart, double thetaLength) {
  auto geometry = make<BufferGeometry>();
  segments = std::max(3, segments);
  // buffers
  std::vector<uint32_t> indices;
  std::vector<double> vertices, normals, uvs;
  // helper variables
  Vector3 vertex;
  Vector2 uv;
  // center point
  push(vertices, 0, 0, 0);
  push(normals, 0, 0, 1);
  push(uvs, 0.5, 0.5);
  for (int s = 0, i = 3; s <= segments; s++, i += 3) {
    const double segment = thetaStart + div(s, segments) * thetaLength;
    // vertex
    vertex.x = radius * std::cos(segment);
    vertex.y = radius * std::sin(segment);
    push(vertices, vertex.x, vertex.y, vertex.z);
    // normal
    push(normals, 0, 0, 1);
    // uvs
    uv.x = (vertices[static_cast<size_t>(i)] / radius + 1) / 2;
    uv.y = (vertices[static_cast<size_t>(i + 1)] / radius + 1) / 2;
    push(uvs, uv.x, uv.y);
  }
  // indices
  for (int i = 1; i <= segments; i++) push(indices, i, i + 1, 0);
  // build geometry
  geometry->setIndex(indices);
  geometry->setAttribute("position", floatAttribute(vertices, 3));
  geometry->setAttribute("normal", floatAttribute(normals, 3));
  geometry->setAttribute("uv", floatAttribute(uvs, 2));
  return geometry;
}

// ---------------------------------------------------------------------------
// RingGeometry
// ---------------------------------------------------------------------------

GeometryPtr ringGeometry(double innerRadius, double outerRadius, int thetaSegments, int phiSegments, double thetaStart, double thetaLength) {
  auto geometry = make<BufferGeometry>();
  thetaSegments = std::max(3, thetaSegments);
  phiSegments = std::max(1, phiSegments);
  // buffers
  std::vector<uint32_t> indices;
  std::vector<double> vertices, normals, uvs;
  // some helper variables
  double radius = innerRadius;
  const double radiusStep = ((outerRadius - innerRadius) / phiSegments);
  Vector3 vertex;
  Vector2 uv;
  // generate vertices, normals and uvs
  for (int j = 0; j <= phiSegments; j++) {
    for (int i = 0; i <= thetaSegments; i++) {
      // values are generate from the inside of the ring to the outside
      const double segment = thetaStart + div(i, thetaSegments) * thetaLength;
      // vertex
      vertex.x = radius * std::cos(segment);
      vertex.y = radius * std::sin(segment);
      push(vertices, vertex.x, vertex.y, vertex.z);
      // normal
      push(normals, 0, 0, 1);
      // uv
      uv.x = (vertex.x / outerRadius + 1) / 2;
      uv.y = (vertex.y / outerRadius + 1) / 2;
      push(uvs, uv.x, uv.y);
    }
    // increase the radius for next row of vertices
    radius += radiusStep;
  }
  // indices
  for (int j = 0; j < phiSegments; j++) {
    const long thetaSegmentLevel = static_cast<long>(j) * (thetaSegments + 1);
    for (int i = 0; i < thetaSegments; i++) {
      const long segment = i + thetaSegmentLevel;
      const long a = segment;
      const long b = segment + thetaSegments + 1;
      const long c = segment + thetaSegments + 2;
      const long d = segment + 1;
      // faces
      push(indices, a, b, d);
      push(indices, b, c, d);
    }
  }
  // build geometry
  geometry->setIndex(indices);
  geometry->setAttribute("position", floatAttribute(vertices, 3));
  geometry->setAttribute("normal", floatAttribute(normals, 3));
  geometry->setAttribute("uv", floatAttribute(uvs, 2));
  return geometry;
}

// ---------------------------------------------------------------------------
// LatheGeometry
// ---------------------------------------------------------------------------

GeometryPtr latheGeometry(const std::vector<Vector2>& points, int segments, double phiStart, double phiLength) {
  auto geometry = make<BufferGeometry>();
  // clamp phiLength so it's in range of [ 0, 2PI ]
  phiLength = MathUtils::clamp(phiLength, 0, PI * 2);
  // buffers
  std::vector<uint32_t> indices;
  std::vector<double> vertices, uvs, initNormals, normals;
  // helper variables
  const double inverseSegments = 1.0 / segments;
  Vector3 vertex;
  Vector2 uv;
  Vector3 normal;
  Vector3 curNormal;
  Vector3 prevNormal;
  double dx = 0;
  double dy = 0;
  const long count = static_cast<long>(points.size());
  // pre-compute normals for initial "meridian"
  for (long j = 0; j <= (count - 1); j++) {
    const size_t k = static_cast<size_t>(j);
    if (j == 0) {   // special handling for 1st vertex on path
      // (of a single point three.js reads a point that is not there: its normal is not a number)
      dx = (count > 1 ? points[k + 1].x : JS_NAN) - points[k].x;
      dy = (count > 1 ? points[k + 1].y : JS_NAN) - points[k].y;
      normal.x = dy * 1.0;
      normal.y = -dx;
      normal.z = dy * 0.0;
      prevNormal.copy(normal);
      normal.normalize();
      push(initNormals, normal.x, normal.y, normal.z);
    } else if (j == (count - 1)) {   // special handling for last Vertex on path
      prevNormal.normalize();
      push(initNormals, prevNormal.x, prevNormal.y, prevNormal.z);
    } else {   // default handling for all vertices in between
      dx = points[k + 1].x - points[k].x;
      dy = points[k + 1].y - points[k].y;
      normal.x = dy * 1.0;
      normal.y = -dx;
      normal.z = dy * 0.0;
      curNormal.copy(normal);
      normal.x += prevNormal.x;
      normal.y += prevNormal.y;
      normal.z += prevNormal.z;
      normal.normalize();
      push(initNormals, normal.x, normal.y, normal.z);
      prevNormal.copy(curNormal);
    }
  }
  // generate vertices, uvs and normals
  for (int i = 0; i <= segments; i++) {
    const double phi = phiStart + i * inverseSegments * phiLength;
    const double sin = std::sin(phi);
    const double cos = std::cos(phi);
    for (long j = 0; j <= (count - 1); j++) {
      const size_t k = static_cast<size_t>(j);
      // vertex
      vertex.x = points[k].x * sin;
      vertex.y = points[k].y;
      vertex.z = points[k].x * cos;
      push(vertices, vertex.x, vertex.y, vertex.z);
      // uv
      uv.x = div(i, segments);
      uv.y = div(j, count - 1);
      push(uvs, uv.x, uv.y);
      // normal
      const double x = initNormals[3 * k + 0] * sin;
      const double y = initNormals[3 * k + 1];
      const double z = initNormals[3 * k + 0] * cos;
      push(normals, x, y, z);
    }
  }
  // indices
  for (int i = 0; i < segments; i++) {
    for (long j = 0; j < (count - 1); j++) {
      const long base = j + i * count;
      const long a = base;
      const long b = base + count;
      const long c = base + count + 1;
      const long d = base + 1;
      // faces
      push(indices, a, b, d);
      push(indices, c, d, b);
    }
  }
  // build geometry
  geometry->setIndex(indices);
  geometry->setAttribute("position", floatAttribute(vertices, 3));
  geometry->setAttribute("uv", floatAttribute(uvs, 2));
  geometry->setAttribute("normal", floatAttribute(normals, 3));
  return geometry;
}

// ---------------------------------------------------------------------------
// PolyhedronGeometry, IcosahedronGeometry
// ---------------------------------------------------------------------------

GeometryPtr polyhedronGeometry(const std::vector<double>& vertices, const std::vector<int>& indices, double radius, int detail) {
  auto geometry = make<BufferGeometry>();
  // default buffer data
  std::vector<double> vertexBuffer;
  std::vector<double> uvBuffer;

  // helper functions
  const auto pushVertex = [&](const Vector3& vertex) { push(vertexBuffer, vertex.x, vertex.y, vertex.z); };
  const auto getVertexByIndex = [&](int index, Vector3& vertex) {
    const size_t stride = static_cast<size_t>(index) * 3;
    vertex.x = vertices[stride + 0];
    vertex.y = vertices[stride + 1];
    vertex.z = vertices[stride + 2];
  };
  // Angle around the Y axis, counter-clockwise when looking from above.
  const auto azimuth = [](const Vector3& vector) { return std::atan2(vector.z, -vector.x); };
  // Angle above the XZ plane.
  const auto inclination = [](const Vector3& vector) { return std::atan2(-vector.y, std::sqrt((vector.x * vector.x) + (vector.z * vector.z))); };

  const auto subdivideFace = [&](const Vector3& a, const Vector3& b, const Vector3& c, int detail_) {
    const int cols = detail_ + 1;
    // we use this multidimensional array as a data structure for creating the subdivision
    std::vector<std::vector<Vector3>> v(static_cast<size_t>(cols) + 1);
    // construct all of the vertices for this subdivision
    for (int i = 0; i <= cols; i++) {
      std::vector<Vector3>& row = v[static_cast<size_t>(i)];
      const Vector3 aj = a.clone().lerp(c, div(i, cols));
      const Vector3 bj = b.clone().lerp(c, div(i, cols));
      const int rows = cols - i;
      for (int j = 0; j <= rows; j++) {
        if (j == 0 && i == cols) {
          row.push_back(aj);
        } else {
          row.push_back(aj.clone().lerp(bj, div(j, rows)));
        }
      }
    }
    // construct all of the faces
    for (int i = 0; i < cols; i++) {
      for (int j = 0; j < 2 * (cols - i) - 1; j++) {
        const size_t k = static_cast<size_t>(j / 2);
        const size_t r = static_cast<size_t>(i);
        if (j % 2 == 0) {
          pushVertex(v[r][k + 1]);
          pushVertex(v[r + 1][k]);
          pushVertex(v[r][k]);
        } else {
          pushVertex(v[r][k + 1]);
          pushVertex(v[r + 1][k + 1]);
          pushVertex(v[r + 1][k]);
        }
      }
    }
  };

  const auto subdivide = [&](int detail_) {
    Vector3 a, b, c;
    // iterate over all faces and apply a subdivision with the given detail value
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      // get the vertices of the face
      getVertexByIndex(indices[i + 0], a);
      getVertexByIndex(indices[i + 1], b);
      getVertexByIndex(indices[i + 2], c);
      // perform subdivision
      subdivideFace(a, b, c, detail_);
    }
  };

  const auto applyRadius = [&](double radius_) {
    Vector3 vertex;
    // iterate over the entire buffer and apply the radius to each vertex
    for (size_t i = 0; i < vertexBuffer.size(); i += 3) {
      vertex.x = vertexBuffer[i + 0];
      vertex.y = vertexBuffer[i + 1];
      vertex.z = vertexBuffer[i + 2];
      vertex.normalize().multiplyScalar(radius_);
      vertexBuffer[i + 0] = vertex.x;
      vertexBuffer[i + 1] = vertex.y;
      vertexBuffer[i + 2] = vertex.z;
    }
  };

  const auto correctUV = [&](const Vector2& uv, size_t stride, const Vector3& vector, double azimuth_) {
    if ((azimuth_ < 0) && (uv.x == 1)) uvBuffer[stride] = uv.x - 1;
    if ((vector.x == 0) && (vector.z == 0)) uvBuffer[stride] = azimuth_ / 2 / PI + 0.5;
  };

  const auto correctUVs = [&]() {
    Vector3 a, b, c;
    Vector3 centroid;
    Vector2 uvA, uvB, uvC;
    for (size_t i = 0, j = 0; i < vertexBuffer.size(); i += 9, j += 6) {
      a.set(vertexBuffer[i + 0], vertexBuffer[i + 1], vertexBuffer[i + 2]);
      b.set(vertexBuffer[i + 3], vertexBuffer[i + 4], vertexBuffer[i + 5]);
      c.set(vertexBuffer[i + 6], vertexBuffer[i + 7], vertexBuffer[i + 8]);
      uvA.set(uvBuffer[j + 0], uvBuffer[j + 1]);
      uvB.set(uvBuffer[j + 2], uvBuffer[j + 3]);
      uvC.set(uvBuffer[j + 4], uvBuffer[j + 5]);
      centroid.copy(a).add(b).add(c).divideScalar(3);
      const double azi = azimuth(centroid);
      correctUV(uvA, j + 0, a, azi);
      correctUV(uvB, j + 2, b, azi);
      correctUV(uvC, j + 4, c, azi);
    }
  };

  const auto correctSeam = [&]() {
    // handle case when face straddles the seam, see #3269
    for (size_t i = 0; i < uvBuffer.size(); i += 6) {
      // uv data of a single face
      const double x0 = uvBuffer[i + 0];
      const double x1 = uvBuffer[i + 2];
      const double x2 = uvBuffer[i + 4];
      const double max = mathdetail::max3(x0, x1, x2);
      const double min = mathdetail::min3(x0, x1, x2);
      // 0.9 is somewhat arbitrary
      if (max > 0.9 && min < 0.1) {
        if (x0 < 0.2) uvBuffer[i + 0] += 1;
        if (x1 < 0.2) uvBuffer[i + 2] += 1;
        if (x2 < 0.2) uvBuffer[i + 4] += 1;
      }
    }
  };

  const auto generateUVs = [&]() {
    Vector3 vertex;
    for (size_t i = 0; i < vertexBuffer.size(); i += 3) {
      vertex.x = vertexBuffer[i + 0];
      vertex.y = vertexBuffer[i + 1];
      vertex.z = vertexBuffer[i + 2];
      const double u = azimuth(vertex) / 2 / PI + 0.5;
      const double v = inclination(vertex) / PI + 0.5;
      push(uvBuffer, u, 1 - v);
    }
    correctUVs();
    correctSeam();
  };

  // the subdivision creates the vertex buffer data
  subdivide(detail);
  // all vertices should lie on a conceptual sphere with a given radius
  applyRadius(radius);
  // finally, create the uv data
  generateUVs();
  // build non-indexed geometry
  geometry->setAttribute("position", floatAttribute(vertexBuffer, 3));
  geometry->setAttribute("normal", floatAttribute(vertexBuffer, 3));
  geometry->setAttribute("uv", floatAttribute(uvBuffer, 2));
  if (detail == 0) {
    geometry->computeVertexNormals();   // flat normals
  } else {
    geometry->normalizeNormals();   // smooth normals
  }
  return geometry;
}

GeometryPtr icosahedronGeometry(double radius, int detail) {
  const double t = (1 + std::sqrt(5.0)) / 2;
  const std::vector<double> vertices = {-1, t, 0, 1, t, 0, -1, -t, 0, 1, -t, 0, 0, -1, t, 0, 1, t, 0, -1, -t, 0, 1, -t, t, 0, -1, t, 0, 1, -t, 0, -1, -t, 0, 1};
  const std::vector<int> indices = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                                    3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
  return polyhedronGeometry(vertices, indices, radius, detail);
}

// ---------------------------------------------------------------------------
// TubeGeometry
// ---------------------------------------------------------------------------

GeometryPtr tubeGeometry(const Curve<Vector3>& path, int tubularSegments, double radius, int radialSegments, bool closed) {
  auto geometry = make<BufferGeometry>();
  const FrenetFrames frames = path.computeFrenetFrames(tubularSegments, closed);
  // helper variables
  Vector3 vertex;
  Vector3 normal;
  Vector2 uv;
  Vector3 P;
  // buffer
  std::vector<double> vertices, normals, uvs;
  std::vector<uint32_t> indices;

  const auto generateSegment = [&](int i) {
    // we use getPointAt to sample evenly distributed points from the given path
    P = path.getPointAt(div(i, tubularSegments));
    // retrieve corresponding normal and binormal
    const Vector3& N = frames.normals[static_cast<size_t>(i)];
    const Vector3& B = frames.binormals[static_cast<size_t>(i)];
    // generate normals and vertices for the current segment
    for (int j = 0; j <= radialSegments; j++) {
      const double v = div(j, radialSegments) * PI * 2;
      const double sin = std::sin(v);
      const double cos = -std::cos(v);
      // normal
      normal.x = (cos * N.x + sin * B.x);
      normal.y = (cos * N.y + sin * B.y);
      normal.z = (cos * N.z + sin * B.z);
      normal.normalize();
      push(normals, normal.x, normal.y, normal.z);
      // vertex
      vertex.x = P.x + radius * normal.x;
      vertex.y = P.y + radius * normal.y;
      vertex.z = P.z + radius * normal.z;
      push(vertices, vertex.x, vertex.y, vertex.z);
    }
  };

  // create buffer data (generateBufferData)
  for (int i = 0; i < tubularSegments; i++) generateSegment(i);
  // if the geometry is not closed, generate the last row of vertices and normals
  // at the regular position on the given path
  //
  // if the geometry is closed, duplicate the first row of vertices and normals (uvs will differ)
  generateSegment((closed == false) ? tubularSegments : 0);
  // uvs are generated in a separate function.
  // this makes it easy compute correct values for closed geometries
  for (int i = 0; i <= tubularSegments; i++) {
    for (int j = 0; j <= radialSegments; j++) {
      uv.x = div(i, tubularSegments);
      uv.y = div(j, radialSegments);
      push(uvs, uv.x, uv.y);
    }
  }
  // finally create faces
  for (int j = 1; j <= tubularSegments; j++) {
    for (int i = 1; i <= radialSegments; i++) {
      const long a = static_cast<long>(radialSegments + 1) * (j - 1) + (i - 1);
      const long b = static_cast<long>(radialSegments + 1) * j + (i - 1);
      const long c = static_cast<long>(radialSegments + 1) * j + i;
      const long d = static_cast<long>(radialSegments + 1) * (j - 1) + i;
      // faces
      push(indices, a, b, d);
      push(indices, b, c, d);
    }
  }
  // build geometry
  geometry->setIndex(indices);
  geometry->setAttribute("position", floatAttribute(vertices, 3));
  geometry->setAttribute("normal", floatAttribute(normals, 3));
  geometry->setAttribute("uv", floatAttribute(uvs, 2));
  return geometry;
}

// ---------------------------------------------------------------------------
// ShapeGeometry
// ---------------------------------------------------------------------------

namespace {

/** ShapeGeometry: one shape or several (`groups`: a group for each, as three.js adds them when it is given an array) */
GeometryPtr buildShapeGeometry(const std::vector<const Shape*>& shapes, int curveSegments, bool groups) {
  auto geometry = make<BufferGeometry>();
  // buffers
  std::vector<uint32_t> indices;
  std::vector<double> vertices, normals, uvs;
  // helper variables
  int groupStart = 0;
  int groupCount = 0;

  const auto addShape = [&](const Shape& shape) {
    const long indexOffset = static_cast<long>(vertices.size() / 3);
    ShapePoints points = shape.extractPoints(curveSegments);
    std::vector<Vector2>& shapeVertices = points.shape;
    std::vector<std::vector<Vector2>>& shapeHoles = points.holes;
    // check direction of vertices
    if (ShapeUtils::isClockWise(shapeVertices) == false) std::reverse(shapeVertices.begin(), shapeVertices.end());
    for (size_t i = 0, l = shapeHoles.size(); i < l; i++) {
      std::vector<Vector2>& shapeHole = shapeHoles[i];
      if (ShapeUtils::isClockWise(shapeHole) == true) std::reverse(shapeHole.begin(), shapeHole.end());
    }
    const std::vector<std::array<int, 3>> faces = ShapeUtils::triangulateShape(shapeVertices, shapeHoles);
    // join vertices of inner and outer paths to a single array
    for (size_t i = 0, l = shapeHoles.size(); i < l; i++) {
      const std::vector<Vector2>& shapeHole = shapeHoles[i];
      shapeVertices.insert(shapeVertices.end(), shapeHole.begin(), shapeHole.end());
    }
    // vertices, normals, uvs
    for (size_t i = 0, l = shapeVertices.size(); i < l; i++) {
      const Vector2& vertex = shapeVertices[i];
      push(vertices, vertex.x, vertex.y, 0);
      push(normals, 0, 0, 1);
      push(uvs, vertex.x, vertex.y);   // world uvs
    }
    // indices
    for (size_t i = 0, l = faces.size(); i < l; i++) {
      const std::array<int, 3>& face = faces[i];
      const long a = face[0] + indexOffset;
      const long b = face[1] + indexOffset;
      const long c = face[2] + indexOffset;
      push(indices, a, b, c);
      groupCount += 3;
    }
  };

  // allow single and array values for "shapes" parameter
  if (groups == false) {
    addShape(*shapes[0]);
  } else {
    for (size_t i = 0; i < shapes.size(); i++) {
      addShape(*shapes[i]);
      geometry->addGroup(groupStart, groupCount, static_cast<int>(i));   // enables MultiMaterial support
      groupStart += groupCount;
      groupCount = 0;
    }
  }
  // build geometry
  geometry->setIndex(indices);
  geometry->setAttribute("position", floatAttribute(vertices, 3));
  geometry->setAttribute("normal", floatAttribute(normals, 3));
  geometry->setAttribute("uv", floatAttribute(uvs, 2));
  return geometry;
}

std::vector<const Shape*> pointers(const std::vector<Shape>& shapes) {
  std::vector<const Shape*> p;
  for (const Shape& s : shapes) p.push_back(&s);
  return p;
}

}  // namespace

GeometryPtr shapeGeometry(const Shape& shape, int curveSegments) { return buildShapeGeometry({&shape}, curveSegments, false); }
GeometryPtr shapeGeometry(const std::vector<Shape>& shapes, int curveSegments) { return buildShapeGeometry(pointers(shapes), curveSegments, true); }

// ---------------------------------------------------------------------------
// ExtrudeGeometry
// ---------------------------------------------------------------------------

namespace {

/** Merges index-adjacent points that are within a threshold distance of each other. Array is modified in-place. Threshold distance is empirical, and scaled based on the magnitude of point coordinates. */
void mergeOverlappingPoints(std::vector<Vector2>& points) {
  if (points.empty()) return;
  const double THRESHOLD = 1e-10;
  const double THRESHOLD_SQ = THRESHOLD * THRESHOLD;
  Vector2 prevPos = points[0];
  for (size_t i = 1; i <= points.size(); i++) {
    const size_t currentIndex = i % points.size();
    const Vector2 currentPos = points[currentIndex];
    const double dx = currentPos.x - prevPos.x;
    const double dy = currentPos.y - prevPos.y;
    const double distSq = dx * dx + dy * dy;
    const double scalingFactorSqrt = jsMax(jsMax(std::fabs(currentPos.x), std::fabs(currentPos.y)), jsMax(std::fabs(prevPos.x), std::fabs(prevPos.y)));
    const double thresholdSqScaled = THRESHOLD_SQ * scalingFactorSqrt * scalingFactorSqrt;
    if (distSq <= thresholdSqScaled) {
      points.erase(points.begin() + static_cast<std::ptrdiff_t>(currentIndex));
      i--;
      if (points.empty()) return;
      continue;
    }
    prevPos = currentPos;
  }
}

Vector2 scalePt2(const Vector2& pt, const Vector2& vec, double size) { return pt.clone().addScaledVector(vec, size); }

// Find directions for point movement
Vector2 getBevelVec(const Vector2& inPt, const Vector2& inPrev, const Vector2& inNext) {
  const double EPSILON = std::numeric_limits<double>::epsilon();
  // computes for inPt the corresponding point inPt' on a new contour
  //   shifted by 1 unit (length of normalized vector) to the left
  // if we walk along contour clockwise, this new contour is outside the old one
  //
  // inPt' is the intersection of the two lines parallel to the two
  //  adjacent edges of inPt at a distance of 1 unit on the left side.
  double v_trans_x, v_trans_y, shrink_by;   // resulting translation vector for inPt
  // good reading for geometry algorithms (here: line-line intersection)
  // http://geomalgorithms.com/a05-_intersect-1.html
  const double v_prev_x = inPt.x - inPrev.x, v_prev_y = inPt.y - inPrev.y;
  const double v_next_x = inNext.x - inPt.x, v_next_y = inNext.y - inPt.y;
  const double v_prev_lensq = (v_prev_x * v_prev_x + v_prev_y * v_prev_y);
  // check for collinear edges
  const double collinear0 = (v_prev_x * v_next_y - v_prev_y * v_next_x);
  if (std::fabs(collinear0) > EPSILON) {
    // not collinear
    // length of vectors for normalizing
    const double v_prev_len = std::sqrt(v_prev_lensq);
    const double v_next_len = std::sqrt(v_next_x * v_next_x + v_next_y * v_next_y);
    // shift adjacent points by unit vectors to the left
    const double ptPrevShift_x = (inPrev.x - v_prev_y / v_prev_len);
    const double ptPrevShift_y = (inPrev.y + v_prev_x / v_prev_len);
    const double ptNextShift_x = (inNext.x - v_next_y / v_next_len);
    const double ptNextShift_y = (inNext.y + v_next_x / v_next_len);
    // scaling factor for v_prev to intersection point
    const double sf = ((ptNextShift_x - ptPrevShift_x) * v_next_y - (ptNextShift_y - ptPrevShift_y) * v_next_x) / (v_prev_x * v_next_y - v_prev_y * v_next_x);
    // vector from inPt to intersection point
    v_trans_x = (ptPrevShift_x + v_prev_x * sf - inPt.x);
    v_trans_y = (ptPrevShift_y + v_prev_y * sf - inPt.y);
    // Don't normalize!, otherwise sharp corners become ugly
    //  but prevent crazy spikes
    const double v_trans_lensq = (v_trans_x * v_trans_x + v_trans_y * v_trans_y);
    if (v_trans_lensq <= 2) {
      return Vector2(v_trans_x, v_trans_y);
    } else {
      shrink_by = std::sqrt(v_trans_lensq / 2);
    }
  } else {
    // handle special case of collinear edges
    bool direction_eq = false;   // assumes: opposite
    if (v_prev_x > EPSILON) {
      if (v_next_x > EPSILON) direction_eq = true;
    } else {
      if (v_prev_x < -EPSILON) {
        if (v_next_x < -EPSILON) direction_eq = true;
      } else {
        if (jsSign(v_prev_y) == jsSign(v_next_y)) direction_eq = true;
      }
    }
    if (direction_eq) {
      v_trans_x = -v_prev_y;
      v_trans_y = v_prev_x;
      shrink_by = std::sqrt(v_prev_lensq);
    } else {
      v_trans_x = v_prev_x;
      v_trans_y = v_prev_y;
      shrink_by = std::sqrt(v_prev_lensq / 2);
    }
  }
  return Vector2(v_trans_x / shrink_by, v_trans_y / shrink_by);
}

/** addShape of ExtrudeGeometry: the triangles of one shape, appended to the geometry's positions and uvs */
void extrudeShape(BufferGeometry& scope, std::vector<double>& verticesArray, std::vector<double>& uvArray, const Shape& shape, const ExtrudeOptions& options) {
  std::vector<double> placeholder;
  // options
  const int curveSegments = options.curveSegments;
  const int steps = options.steps;
  const double depth = options.depth;
  bool bevelEnabled = options.bevelEnabled;
  double bevelThickness = options.bevelThickness;
  double bevelSize = options.bevelSize ? *options.bevelSize : bevelThickness - 0.1;
  double bevelOffset = options.bevelOffset;
  int bevelSegments = options.bevelSegments;
  const Curve<Vector3>* const extrudePath = options.extrudePath;
  //
  std::vector<Vector3> extrudePts;
  bool extrudeByPath = false;
  FrenetFrames splineTube;
  Vector3 binormal, normal, position2;
  if (extrudePath) {
    extrudePts = extrudePath->getSpacedPoints(steps);
    extrudeByPath = true;
    bevelEnabled = false;   // bevels not supported for path extrusion
    // SETUP TNB variables
    const bool isClosed = extrudePath->isClosedCatmullRom();
    splineTube = extrudePath->computeFrenetFrames(steps, isClosed);
  }
  // Safeguards if bevels are not enabled
  if (!bevelEnabled) {
    bevelSegments = 0;
    bevelThickness = 0;
    bevelSize = 0;
    bevelOffset = 0;
  }
  // Variables initialization
  ShapePoints shapePoints = shape.extractPoints(curveSegments);
  std::vector<Vector2> vertices = std::move(shapePoints.shape);
  std::vector<std::vector<Vector2>>& holes = shapePoints.holes;
  const bool reverse = !ShapeUtils::isClockWise(vertices);
  if (reverse) {
    std::reverse(vertices.begin(), vertices.end());
    // Maybe we should also check if holes are in the opposite direction, just to be safe ...
    for (size_t h = 0, hl = holes.size(); h < hl; h++) {
      std::vector<Vector2>& ahole = holes[h];
      if (ShapeUtils::isClockWise(ahole)) std::reverse(ahole.begin(), ahole.end());
    }
  }
  mergeOverlappingPoints(vertices);
  for (std::vector<Vector2>& hole : holes) mergeOverlappingPoints(hole);
  const size_t numHoles = holes.size();
  /* Vertices */
  std::vector<Vector2> contour = vertices;   // vertices has all points but contour has only points of circumference
  for (size_t h = 0; h < numHoles; h++) {
    const std::vector<Vector2>& ahole = holes[h];
    vertices.insert(vertices.end(), ahole.begin(), ahole.end());
  }
  const long vlen = static_cast<long>(vertices.size());

  const auto v = [&](double x, double y, double z) {
    placeholder.push_back(x);
    placeholder.push_back(y);
    placeholder.push_back(z);
  };

  std::vector<Vector2> contourMovements(contour.size());
  for (size_t i = 0, il = contour.size(), j = il - 1, k = i + 1; i < il; i++, j++, k++) {
    if (j == il) j = 0;
    if (k == il) k = 0;
    //  (j)---(i)---(k)
    contourMovements[i] = getBevelVec(contour[i], contour[j], contour[k]);
  }
  std::vector<std::vector<Vector2>> holesMovements;
  std::vector<Vector2> verticesMovements = contourMovements;
  for (size_t h = 0, hl = numHoles; h < hl; h++) {
    const std::vector<Vector2>& ahole = holes[h];
    std::vector<Vector2> oneHoleMovements(ahole.size());
    for (size_t i = 0, il = ahole.size(), j = il - 1, k = i + 1; i < il; i++, j++, k++) {
      if (j == il) j = 0;
      if (k == il) k = 0;
      //  (j)---(i)---(k)
      oneHoleMovements[i] = getBevelVec(ahole[i], ahole[j], ahole[k]);
    }
    verticesMovements.insert(verticesMovements.end(), oneHoleMovements.begin(), oneHoleMovements.end());
    holesMovements.push_back(std::move(oneHoleMovements));
  }

  std::vector<std::array<int, 3>> faces;
  if (bevelSegments == 0) {
    faces = ShapeUtils::triangulateShape(contour, holes);
  } else {
    std::vector<Vector2> contractedContourVertices;
    std::vector<std::vector<Vector2>> expandedHoleVertices;
    // Loop bevelSegments, 1 for the front, 1 for the back
    for (int b = 0; b < bevelSegments; b++) {
      //for ( b = bevelSegments; b > 0; b -- ) {
      const double t = div(b, bevelSegments);
      const double z = bevelThickness * std::cos(t * PI / 2);
      const double bs = bevelSize * std::sin(t * PI / 2) + bevelOffset;
      // contract shape
      for (size_t i = 0, il = contour.size(); i < il; i++) {
        const Vector2 vert = scalePt2(contour[i], contourMovements[i], bs);
        v(vert.x, vert.y, -z);
        if (t == 0) contractedContourVertices.push_back(vert);
      }
      // expand holes
      for (size_t h = 0, hl = numHoles; h < hl; h++) {
        const std::vector<Vector2>& ahole = holes[h];
        const std::vector<Vector2>& oneHoleMovements = holesMovements[h];
        std::vector<Vector2> oneHoleVertices;
        for (size_t i = 0, il = ahole.size(); i < il; i++) {
          const Vector2 vert = scalePt2(ahole[i], oneHoleMovements[i], bs);
          v(vert.x, vert.y, -z);
          if (t == 0) oneHoleVertices.push_back(vert);
        }
        if (t == 0) expandedHoleVertices.push_back(std::move(oneHoleVertices));
      }
    }
    faces = ShapeUtils::triangulateShape(contractedContourVertices, expandedHoleVertices);
  }
  const long flen = static_cast<long>(faces.size());
  const double bs = bevelSize + bevelOffset;
  // Back facing vertices
  for (long i = 0; i < vlen; i++) {
    const size_t k = static_cast<size_t>(i);
    const Vector2 vert = bevelEnabled ? scalePt2(vertices[k], verticesMovements[k], bs) : vertices[k];
    if (!extrudeByPath) {
      v(vert.x, vert.y, 0);
    } else {
      // v( vert.x, vert.y + extrudePts[ 0 ].y, extrudePts[ 0 ].x );
      normal.copy(splineTube.normals[0]).multiplyScalar(vert.x);
      binormal.copy(splineTube.binormals[0]).multiplyScalar(vert.y);
      position2.copy(extrudePts[0]).add(normal).add(binormal);
      v(position2.x, position2.y, position2.z);
    }
  }
  // Add stepped vertices...
  // Including front facing vertices
  for (int s = 1; s <= steps; s++) {
    for (long i = 0; i < vlen; i++) {
      const size_t k = static_cast<size_t>(i);
      const Vector2 vert = bevelEnabled ? scalePt2(vertices[k], verticesMovements[k], bs) : vertices[k];
      if (!extrudeByPath) {
        v(vert.x, vert.y, depth / steps * s);
      } else {
        // v( vert.x, vert.y + extrudePts[ s - 1 ].y, extrudePts[ s - 1 ].x );
        normal.copy(splineTube.normals[static_cast<size_t>(s)]).multiplyScalar(vert.x);
        binormal.copy(splineTube.binormals[static_cast<size_t>(s)]).multiplyScalar(vert.y);
        position2.copy(extrudePts[static_cast<size_t>(s)]).add(normal).add(binormal);
        v(position2.x, position2.y, position2.z);
      }
    }
  }
  // Add bevel segments planes
  //for ( b = 1; b <= bevelSegments; b ++ ) {
  for (int b = bevelSegments - 1; b >= 0; b--) {
    const double t = div(b, bevelSegments);
    const double z = bevelThickness * std::cos(t * PI / 2);
    const double bs2 = bevelSize * std::sin(t * PI / 2) + bevelOffset;
    // contract shape
    for (size_t i = 0, il = contour.size(); i < il; i++) {
      const Vector2 vert = scalePt2(contour[i], contourMovements[i], bs2);
      v(vert.x, vert.y, depth + z);
    }
    // expand holes
    for (size_t h = 0, hl = holes.size(); h < hl; h++) {
      const std::vector<Vector2>& ahole = holes[h];
      const std::vector<Vector2>& oneHoleMovements = holesMovements[h];
      for (size_t i = 0, il = ahole.size(); i < il; i++) {
        const Vector2 vert = scalePt2(ahole[i], oneHoleMovements[i], bs2);
        if (!extrudeByPath) {
          v(vert.x, vert.y, depth + z);
        } else {
          v(vert.x, vert.y + extrudePts[static_cast<size_t>(steps - 1)].y, extrudePts[static_cast<size_t>(steps - 1)].x + z);
        }
      }
    }
  }

  const auto addVertex = [&](long index) {
    const size_t k = static_cast<size_t>(index) * 3;
    verticesArray.push_back(placeholder[k + 0]);
    verticesArray.push_back(placeholder[k + 1]);
    verticesArray.push_back(placeholder[k + 2]);
  };
  const auto addUV = [&](const Vector2& vector2) {
    uvArray.push_back(vector2.x);
    uvArray.push_back(vector2.y);
  };
  // WorldUVGenerator.generateTopUV
  const auto f3 = [&](long a, long b, long c) {
    addVertex(a);
    addVertex(b);
    addVertex(c);
    const size_t nextIndex = verticesArray.size() / 3;
    const size_t indexA = nextIndex - 3, indexB = nextIndex - 2, indexC = nextIndex - 1;
    const double a_x = verticesArray[indexA * 3];
    const double a_y = verticesArray[indexA * 3 + 1];
    const double b_x = verticesArray[indexB * 3];
    const double b_y = verticesArray[indexB * 3 + 1];
    const double c_x = verticesArray[indexC * 3];
    const double c_y = verticesArray[indexC * 3 + 1];
    addUV(Vector2(a_x, a_y));
    addUV(Vector2(b_x, b_y));
    addUV(Vector2(c_x, c_y));
  };
  // WorldUVGenerator.generateSideWallUV
  const auto f4 = [&](long a, long b, long c, long d) {
    addVertex(a);
    addVertex(b);
    addVertex(d);
    addVertex(b);
    addVertex(c);
    addVertex(d);
    const size_t nextIndex = verticesArray.size() / 3;
    const size_t indexA = nextIndex - 6, indexB = nextIndex - 3, indexC = nextIndex - 2, indexD = nextIndex - 1;
    const double a_x = verticesArray[indexA * 3];
    const double a_y = verticesArray[indexA * 3 + 1];
    const double a_z = verticesArray[indexA * 3 + 2];
    const double b_x = verticesArray[indexB * 3];
    const double b_y = verticesArray[indexB * 3 + 1];
    const double b_z = verticesArray[indexB * 3 + 2];
    const double c_x = verticesArray[indexC * 3];
    const double c_y = verticesArray[indexC * 3 + 1];
    const double c_z = verticesArray[indexC * 3 + 2];
    const double d_x = verticesArray[indexD * 3];
    const double d_y = verticesArray[indexD * 3 + 1];
    const double d_z = verticesArray[indexD * 3 + 2];
    Vector2 uvs[4];
    if (std::fabs(a_y - b_y) < std::fabs(a_x - b_x)) {
      uvs[0].set(a_x, 1 - a_z);
      uvs[1].set(b_x, 1 - b_z);
      uvs[2].set(c_x, 1 - c_z);
      uvs[3].set(d_x, 1 - d_z);
    } else {
      uvs[0].set(a_y, 1 - a_z);
      uvs[1].set(b_y, 1 - b_z);
      uvs[2].set(c_y, 1 - c_z);
      uvs[3].set(d_y, 1 - d_z);
    }
    addUV(uvs[0]);
    addUV(uvs[1]);
    addUV(uvs[3]);
    addUV(uvs[1]);
    addUV(uvs[2]);
    addUV(uvs[3]);
  };

  /* Faces */
  // Top and bottom faces (buildLidFaces)
  {
    const long start = static_cast<long>(verticesArray.size() / 3);
    if (bevelEnabled) {
      long layer = 0;   // steps + 1
      long offset = vlen * layer;
      // Bottom faces
      for (long i = 0; i < flen; i++) {
        const std::array<int, 3>& face = faces[static_cast<size_t>(i)];
        f3(face[2] + offset, face[1] + offset, face[0] + offset);
      }
      layer = steps + bevelSegments * 2;
      offset = vlen * layer;
      // Top faces
      for (long i = 0; i < flen; i++) {
        const std::array<int, 3>& face = faces[static_cast<size_t>(i)];
        f3(face[0] + offset, face[1] + offset, face[2] + offset);
      }
    } else {
      // Bottom faces
      for (long i = 0; i < flen; i++) {
        const std::array<int, 3>& face = faces[static_cast<size_t>(i)];
        f3(face[2], face[1], face[0]);
      }
      // Top faces
      for (long i = 0; i < flen; i++) {
        const std::array<int, 3>& face = faces[static_cast<size_t>(i)];
        f3(face[0] + vlen * steps, face[1] + vlen * steps, face[2] + vlen * steps);
      }
    }
    scope.addGroup(static_cast<int>(start), static_cast<int>(static_cast<long>(verticesArray.size() / 3) - start), 0);
  }
  // Sides faces (buildSideFaces)
  {
    const auto sidewalls = [&](const std::vector<Vector2>& contour_, long layeroffset) {
      long i = static_cast<long>(contour_.size());
      while (--i >= 0) {
        const long j = i;
        long k = i - 1;
        if (k < 0) k = static_cast<long>(contour_.size()) - 1;
        for (long s = 0, sl = (steps + bevelSegments * 2); s < sl; s++) {
          const long slen1 = vlen * s;
          const long slen2 = vlen * (s + 1);
          const long a = layeroffset + j + slen1, b = layeroffset + k + slen1, c = layeroffset + k + slen2, d = layeroffset + j + slen2;
          f4(a, b, c, d);
        }
      }
    };
    const long start = static_cast<long>(verticesArray.size() / 3);
    long layeroffset = 0;
    sidewalls(contour, layeroffset);
    layeroffset += static_cast<long>(contour.size());
    for (size_t h = 0, hl = holes.size(); h < hl; h++) {
      const std::vector<Vector2>& ahole = holes[h];
      sidewalls(ahole, layeroffset);
      //, true
      layeroffset += static_cast<long>(ahole.size());
    }
    scope.addGroup(static_cast<int>(start), static_cast<int>(static_cast<long>(verticesArray.size() / 3) - start), 1);
  }
}

GeometryPtr buildExtrudeGeometry(const std::vector<const Shape*>& shapes, const ExtrudeOptions& options) {
  auto geometry = make<BufferGeometry>();
  std::vector<double> verticesArray;
  std::vector<double> uvArray;
  for (const Shape* shape : shapes) extrudeShape(*geometry, verticesArray, uvArray, *shape, options);
  // build geometry
  geometry->setAttribute("position", floatAttribute(verticesArray, 3));
  geometry->setAttribute("uv", floatAttribute(uvArray, 2));
  geometry->computeVertexNormals();
  return geometry;
}

}  // namespace

GeometryPtr extrudeGeometry(const Shape& shape, const ExtrudeOptions& options) { return buildExtrudeGeometry({&shape}, options); }
GeometryPtr extrudeGeometry(const std::vector<Shape>& shapes, const ExtrudeOptions& options) { return buildExtrudeGeometry(pointers(shapes), options); }

// ---------------------------------------------------------------------------
// BufferGeometryUtils: mergeGeometries, mergeAttributes, mergeVertices
// ---------------------------------------------------------------------------

AttrPtr mergeAttributes(const std::vector<AttrPtr>& attributes) {
  if (attributes.empty() || !attributes[0]) return nullptr;
  const AttrType type = attributes[0]->type;
  const int itemSize = attributes[0]->itemSize;
  const bool normalized = attributes[0]->normalized;
  for (const AttrPtr& attribute : attributes) {
    // BufferAttribute.array must be of consistent array types across matching attributes,
    // and so must itemSize and normalized
    if (!attribute || attribute->type != type || attribute->itemSize != itemSize || attribute->normalized != normalized) return nullptr;
  }
  auto result = std::make_shared<BufferAttribute>();
  result->type = type;
  result->itemSize = itemSize;
  result->normalized = normalized;
  for (const AttrPtr& attribute : attributes) {
    // (count * itemSize numbers of each: what is beyond a whole item is left out, as `new TypedArray(arrayLength)` leaves it)
    const size_t n = attribute->count() * static_cast<size_t>(itemSize);
    switch (type) {
      case AttrType::F32: result->f32.insert(result->f32.end(), attribute->f32.begin(), attribute->f32.begin() + static_cast<std::ptrdiff_t>(n)); break;
      case AttrType::U8: result->u8.insert(result->u8.end(), attribute->u8.begin(), attribute->u8.begin() + static_cast<std::ptrdiff_t>(n)); break;
      case AttrType::I8: result->i8.insert(result->i8.end(), attribute->i8.begin(), attribute->i8.begin() + static_cast<std::ptrdiff_t>(n)); break;
      case AttrType::U16: result->u16.insert(result->u16.end(), attribute->u16.begin(), attribute->u16.begin() + static_cast<std::ptrdiff_t>(n)); break;
      case AttrType::I16: result->i16.insert(result->i16.end(), attribute->i16.begin(), attribute->i16.begin() + static_cast<std::ptrdiff_t>(n)); break;
      case AttrType::U32: result->u32.insert(result->u32.end(), attribute->u32.begin(), attribute->u32.begin() + static_cast<std::ptrdiff_t>(n)); break;
      case AttrType::I32: result->i32.insert(result->i32.end(), attribute->i32.begin(), attribute->i32.begin() + static_cast<std::ptrdiff_t>(n)); break;
    }
  }
  return result;
}

GeometryPtr mergeGeometries(const std::vector<GeometryPtr>& geometries, bool useGroups) {
  if (geometries.empty() || !geometries[0]) return nullptr;
  const bool isIndexed = geometries[0]->index != nullptr;
  // (the names of the first geometry's attributes, in their order: every geometry must have just these)
  std::vector<std::string> attributesUsed;
  for (const auto& entry : geometries[0]->attributes) attributesUsed.push_back(entry.first);
  std::vector<std::vector<AttrPtr>> attributes(attributesUsed.size());
  auto mergedGeometry = make<BufferGeometry>();
  int offset = 0;
  for (size_t i = 0; i < geometries.size(); ++i) {
    const GeometryPtr& geometry = geometries[i];
    if (!geometry) return nullptr;
    size_t attributesCount = 0;
    // ensure that all geometries are indexed, or none
    if (isIndexed != (geometry->index != nullptr)) return nullptr;
    // gather attributes, exit early if they're different
    for (const auto& entry : geometry->attributes) {
      const auto used = std::find(attributesUsed.begin(), attributesUsed.end(), entry.first);
      if (used == attributesUsed.end()) return nullptr;
      attributes[static_cast<size_t>(used - attributesUsed.begin())].push_back(entry.second);
      attributesCount++;
    }
    // ensure geometries have the same number of attributes
    if (attributesCount != attributesUsed.size()) return nullptr;
    if (useGroups) {
      int count;
      if (isIndexed) {
        count = static_cast<int>(geometry->index->count());
      } else if (geometry->position()) {
        count = static_cast<int>(geometry->position()->count());
      } else {
        return nullptr;   // The geometry must have either an index or a position attribute
      }
      mergedGeometry->addGroup(offset, count, static_cast<int>(i));
      offset += count;
    }
  }
  // merge indices
  if (isIndexed) {
    uint32_t indexOffset = 0;
    std::vector<uint32_t> mergedIndex;
    for (size_t i = 0; i < geometries.size(); ++i) {
      const AttrPtr& index = geometries[i]->index;
      for (size_t j = 0; j < index->count(); ++j) mergedIndex.push_back(static_cast<uint32_t>(index->getX(j)) + indexOffset);
      const AttrPtr position = geometries[i]->position();
      if (!position) return nullptr;   // (three.js fails here: an indexed geometry without positions)
      indexOffset += static_cast<uint32_t>(position->count());
    }
    mergedGeometry->setIndex(mergedIndex);
  }
  // merge attributes
  for (size_t n = 0; n < attributesUsed.size(); n++) {
    const AttrPtr mergedAttribute = mergeAttributes(attributes[n]);
    if (!mergedAttribute) return nullptr;
    mergedGeometry->setAttribute(attributesUsed[n], mergedAttribute);
  }
  return mergedGeometry;
}

namespace {

/** remapAttribute: the items the indices name, in an attribute of the same kind */
AttrPtr remapAttribute(const BufferAttribute& attribute, const std::vector<size_t>& sourceIndices) {
  const size_t stride = static_cast<size_t>(attribute.itemSize);
  auto data = std::make_shared<BufferAttribute>();
  data->type = attribute.type;
  data->itemSize = attribute.itemSize;
  data->normalized = attribute.normalized;
  data->divisor = attribute.divisor;
  data->dynamic = attribute.dynamic;
  const auto pick = [&](const auto& source, auto& array) {
    array.resize(sourceIndices.size() * stride);
    for (size_t i = 0; i < sourceIndices.size(); i++) {
      for (size_t j = 0; j < stride; j++) {
        const size_t k = sourceIndices[i] * stride + j;
        if (k < source.size()) array[i * stride + j] = source[k];
      }
    }
  };
  switch (attribute.type) {
    case AttrType::F32: pick(attribute.f32, data->f32); break;
    case AttrType::U8: pick(attribute.u8, data->u8); break;
    case AttrType::I8: pick(attribute.i8, data->i8); break;
    case AttrType::U16: pick(attribute.u16, data->u16); break;
    case AttrType::I16: pick(attribute.i16, data->i16); break;
    case AttrType::U32: pick(attribute.u32, data->u32); break;
    case AttrType::I32: pick(attribute.i32, data->i32); break;
  }
  return data;
}

}  // namespace

GeometryPtr mergeVertices(const BufferGeometry& geometry, double tolerance) {
  tolerance = jsMax(tolerance, std::numeric_limits<double>::epsilon());
  // Generate an index buffer if the geometry doesn't have one, or optimize it
  // if it's already available.
  std::unordered_map<std::string, uint32_t> hashToIndex;
  const AttrPtr indices = geometry.index;
  const AttrPtr positions = geometry.getAttribute("position");
  const size_t vertexCount = indices ? indices->count() : positions ? positions->count() : 0;
  // next value for triangle indices
  uint32_t nextIndex = 0;
  // attributes and new attribute arrays
  std::vector<size_t> sourceIndices;
  std::vector<uint32_t> newIndices;
  // convert the error tolerance to an amount of decimal places to truncate to
  const double halfTolerance = tolerance * 0.5;
  const double exponent = std::log10(1 / tolerance);
  const double hashMultiplier = std::pow(10, exponent);
  const double hashAdditive = halfTolerance * hashMultiplier;
  std::string hash;
  char number[40];
  for (size_t i = 0; i < vertexCount; i++) {
    const size_t index = indices ? static_cast<size_t>(indices->getX(i)) : i;
    // Generate a hash for the vertex attributes at the current index 'i'
    hash.clear();
    for (const auto& entry : geometry.attributes) {
      const BufferAttribute& attribute = *entry.second;
      const int itemSize = attribute.itemSize;
      for (int k = 0; k < itemSize; k++) {
        // double tilde truncates the decimal value
        const double value = k == 0 ? attribute.getX(index) : k == 1 ? attribute.getY(index) : k == 2 ? attribute.getZ(index) : attribute.getW(index);
        const double truncated = std::trunc(value * hashMultiplier + hashAdditive);
        // (as JavaScript writes the number into the text: -0 is 0, and what is not a number is like itself)
        if (truncated != truncated) hash += "NaN,";
        else if (std::isinf(truncated)) hash += truncated > 0 ? "Infinity," : "-Infinity,";
        else {
          std::snprintf(number, sizeof number, "%.0f,", truncated + 0.0);
          hash += number;
        }
      }
    }
    // Add another reference to the vertex if it's already
    // used by another index
    const auto found = hashToIndex.find(hash);
    if (found != hashToIndex.end()) {
      newIndices.push_back(found->second);
    } else {
      // copy data to the new index in the temporary attributes
      sourceIndices.push_back(index);
      hashToIndex[hash] = nextIndex;
      newIndices.push_back(nextIndex);
      nextIndex++;
    }
  }
  // generate result BufferGeometry
  const GeometryPtr result = geometry.clone();
  for (const auto& entry : geometry.attributes) result->setAttribute(entry.first, remapAttribute(*entry.second, sourceIndices));
  // indices
  result->setIndex(newIndices);
  return result;
}

}  // namespace fs
