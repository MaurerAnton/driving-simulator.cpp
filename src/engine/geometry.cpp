// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Geometry implementation: BufferGeometry/BufferAttribute and the primitive
 * shapes the game builds scenery from.
 *
 * Origin: three.js (core/BufferGeometry.js, geometries/ as used by the
 * TypeScript game; the maths mirrors the JavaScript originals.
 */
#include "engine/geometry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fs {

// ---------------------------------------------------------------------------
// BufferGeometry
// ---------------------------------------------------------------------------

std::shared_ptr<BufferGeometry> BufferGeometry::toNonIndexed() const {
  auto g = std::make_shared<BufferGeometry>();
  if (!index || !position) return g;
  const size_t n = index->count();
  std::vector<double> pos, nor, uvs, cols;
  bool hasNor = normal != nullptr, hasUv = uv != nullptr, hasCol = color != nullptr;
  for (size_t i = 0; i < n; i++) {
    size_t k = static_cast<size_t>(index->array[i]);
    pos.push_back(position->getX(k));
    pos.push_back(position->getY(k));
    pos.push_back(position->getZ(k));
    if (hasNor) {
      nor.push_back(normal->getX(k));
      nor.push_back(normal->getY(k));
      nor.push_back(normal->getZ(k));
    }
    if (hasUv) {
      uvs.push_back(uv->getX(k));
      uvs.push_back(uv->getY(k));
    }
    if (hasCol) {
      cols.push_back(color->getX(k));
      cols.push_back(color->getY(k));
      cols.push_back(color->getZ(k));
    }
  }
  g->position = std::make_shared<BufferAttribute>(std::move(pos), 3);
  if (hasNor) g->normal = std::make_shared<BufferAttribute>(std::move(nor), 3);
  if (hasUv) g->uv = std::make_shared<BufferAttribute>(std::move(uvs), 2);
  if (hasCol) g->color = std::make_shared<BufferAttribute>(std::move(cols), 3);
  return g;
}

void BufferGeometry::computeVertexNormals() {
  if (!position) return;
  const size_t n = position->count();
  if (!normal || normal->count() != n)
    normal = std::make_shared<BufferAttribute>(std::vector<double>(n * 3, 0.0), 3);
  for (size_t i = 0; i < n * 3; i++) normal->array[i] = 0;

  // face normals accumulated per vertex (three.js computeVertexNormals)
  size_t triCount;
  std::vector<size_t> tri;
  if (index && index->count() >= 3) {
    triCount = index->count() / 3;
    for (size_t t = 0; t < triCount; t++) {
      tri.push_back(static_cast<size_t>(index->array[t * 3]));
      tri.push_back(static_cast<size_t>(index->array[t * 3 + 1]));
      tri.push_back(static_cast<size_t>(index->array[t * 3 + 2]));
    }
  } else {
    triCount = n / 3;
    for (size_t i = 0; i < n; i++) tri.push_back(i);
  }

  for (size_t t = 0; t < triCount; t++) {
    size_t ia = tri[t * 3], ib = tri[t * 3 + 1], ic = tri[t * 3 + 2];
    Vector3 pa(position->getX(ia), position->getY(ia), position->getZ(ia));
    Vector3 pb(position->getX(ib), position->getY(ib), position->getZ(ib));
    Vector3 pc(position->getX(ic), position->getY(ic), position->getZ(ic));
    Vector3 cb, ab;
    cb.subVectors(pc, pb);
    ab.subVectors(pa, pb);
    cb.cross(ab);
    for (size_t v : {ia, ib, ic}) {
      if (v >= n) continue;
      normal->array[v * 3] += cb.x;
      normal->array[v * 3 + 1] += cb.y;
      normal->array[v * 3 + 2] += cb.z;
    }
  }
  for (size_t v = 0; v < n; v++) {
    Vector3 nv(normal->array[v * 3], normal->array[v * 3 + 1], normal->array[v * 3 + 2]);
    nv.normalize();
    normal->array[v * 3] = nv.x;
    normal->array[v * 3 + 1] = nv.y;
    normal->array[v * 3 + 2] = nv.z;
  }
}

void BufferGeometry::translate(double x, double y, double z) {
  if (!position) return;
  for (size_t i = 0; i < position->count(); i++) {
    position->array[i * 3] += x;
    position->array[i * 3 + 1] += y;
    position->array[i * 3 + 2] += z;
  }
}

void BufferGeometry::scale(double x, double y, double z) {
  if (!position) return;
  for (size_t i = 0; i < position->count(); i++) {
    position->array[i * 3] *= x;
    position->array[i * 3 + 1] *= y;
    position->array[i * 3 + 2] *= z;
  }
}

void BufferGeometry::rotateX(double a) {
  if (!position) return;
  double c = std::cos(a), s = std::sin(a);
  for (size_t i = 0; i < position->count(); i++) {
    double y = position->array[i * 3 + 1], z = position->array[i * 3 + 2];
    position->array[i * 3 + 1] = y * c - z * s;
    position->array[i * 3 + 2] = y * s + z * c;
  }
}

void BufferGeometry::rotateY(double a) {
  if (!position) return;
  double c = std::cos(a), s = std::sin(a);
  for (size_t i = 0; i < position->count(); i++) {
    double x = position->array[i * 3], z = position->array[i * 3 + 2];
    position->array[i * 3] = x * c + z * s;
    position->array[i * 3 + 2] = -x * s + z * c;
  }
}

void BufferGeometry::rotateZ(double a) {
  if (!position) return;
  double c = std::cos(a), s = std::sin(a);
  for (size_t i = 0; i < position->count(); i++) {
    double x = position->array[i * 3], y = position->array[i * 3 + 1];
    // about the Z axis: x' = x cos − y sin, y' = x sin + y cos
    position->array[i * 3] = x * c - y * s;
    position->array[i * 3 + 1] = x * s + y * c;
  }
}

void BufferGeometry::center() {
  if (!position) return;
  Vector3 mn, mx;
  computeBoundingBox(mn, mx);
  translate(-(mn.x + mx.x) * 0.5, -(mn.y + mx.y) * 0.5, -(mn.z + mx.z) * 0.5);
}

void BufferGeometry::computeBoundingBox(Vector3& outMin, Vector3& outMax) const {
  if (!position || position->count() == 0) {
    outMin.set(0, 0, 0);
    outMax.set(0, 0, 0);
    return;
  }
  double minX = position->array[0], minY = position->array[1], minZ = position->array[2];
  double maxX = minX, maxY = minY, maxZ = minZ;
  for (size_t i = 1; i < position->count(); i++) {
    double x = position->array[i * 3], y = position->array[i * 3 + 1], z = position->array[i * 3 + 2];
    if (x < minX) minX = x;
    if (y < minY) minY = y;
    if (z < minZ) minZ = z;
    if (x > maxX) maxX = x;
    if (y > maxY) maxY = y;
    if (z > maxZ) maxZ = z;
  }
  outMin.set(minX, minY, minZ);
  outMax.set(maxX, maxY, maxZ);
}

// ---------------------------------------------------------------------------
// Primitive geometries
// ---------------------------------------------------------------------------

// Vector2 lives in math.h? No — declare a tiny local 2D point for uvs.
struct Vector2 {
  double x = 0, y = 0;
  Vector2() = default;
  Vector2(double x_, double y_) : x(x_), y(y_) {}
};

static void addTri(std::vector<double>& pos, std::vector<double>& nor, std::vector<double>& uvs,
                   const Vector3& a, const Vector3& b, const Vector3& c,
                   const Vector2* ua = nullptr, const Vector2* ub = nullptr, const Vector2* uc = nullptr) {
  Vector3 cb, ab, n;
  cb.subVectors(c, b);
  ab.subVectors(a, b);
  n.crossVectors(cb, ab);
  n.normalize();
  for (const Vector3* v : {&a, &b, &c}) {
    pos.push_back(v->x);
    pos.push_back(v->y);
    pos.push_back(v->z);
    nor.push_back(n.x);
    nor.push_back(n.y);
    nor.push_back(n.z);
  }
  if (ua && ub && uc) {
    uvs.push_back(ua->x); uvs.push_back(ua->y);
    uvs.push_back(ub->x); uvs.push_back(ub->y);
    uvs.push_back(uc->x); uvs.push_back(uc->y);
  }
}

static GeometryPtr finish(std::vector<double>& pos, std::vector<double>& nor, std::vector<double>& uvs) {
  auto g = std::make_shared<BufferGeometry>();
  g->position = std::make_shared<BufferAttribute>(std::move(pos), 3);
  g->normal = std::make_shared<BufferAttribute>(std::move(nor), 3);
  if (!uvs.empty()) g->uv = std::make_shared<BufferAttribute>(std::move(uvs), 2);
  return g;
}

GeometryPtr makeBoxGeometry(double width, double height, double depth,
                            int widthSegments, int heightSegments, int depthSegments) {
  widthSegments = std::max(1, widthSegments);
  heightSegments = std::max(1, heightSegments);
  depthSegments = std::max(1, depthSegments);
  double w = width / 2, h = height / 2, d = depth / 2;
  std::vector<double> pos, nor, uvs;
  // six faces, built as two triangles each per grid cell
  struct Face { Vector3 u, v, n, o; };
  Face faces[6] = {
      {Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, 0, 1), Vector3(-w, -h, d)},
      {Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, 0, -1), Vector3(w, -h, -d)},
      {Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(1, 0, 0), Vector3(w, -h, d)},
      {Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(-1, 0, 0), Vector3(-w, -h, -d)},
      {Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(-w, h, d)},
      {Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, -1, 0), Vector3(-w, -h, -d)},
  };
  int segs[6] = {widthSegments, widthSegments, depthSegments, depthSegments, widthSegments, heightSegments};
  int segs2[6] = {heightSegments, heightSegments, heightSegments, heightSegments, depthSegments, depthSegments};
  for (int f = 0; f < 6; f++) {
    const Face& fc = faces[f];
    int sx = segs[f], sy = segs2[f];
    Vector3 du(fc.u.x * (2.0 * w / sx), fc.u.y * (2.0 * h / sx), fc.u.z * (2.0 * d / sx));
    Vector3 dv(fc.v.x * (2.0 * w / sy), fc.v.y * (2.0 * h / sy), fc.v.z * (2.0 * d / sy));
    // recompute step from the face extents properly
    du.set(fc.u.x * width / sx, fc.u.y * height / sx, fc.u.z * depth / sx);
    dv.set(fc.v.x * width / sy, fc.v.y * height / sy, fc.v.z * depth / sy);
    for (int iy = 0; iy < sy; iy++) {
      for (int ix = 0; ix < sx; ix++) {
        Vector3 p0, p1, p2, p3;
        p0.copy(fc.o).addScaledVector(du, ix).addScaledVector(dv, iy);
        p1.copy(fc.o).addScaledVector(du, ix + 1).addScaledVector(dv, iy);
        p2.copy(fc.o).addScaledVector(du, ix + 1).addScaledVector(dv, iy + 1);
        p3.copy(fc.o).addScaledVector(du, ix).addScaledVector(dv, iy + 1);
        Vector2 t0(ix, iy), t1(ix + 1, iy), t2(ix + 1, iy + 1), t3(ix, iy + 1);
        addTri(pos, nor, uvs, p0, p1, p2, &t0, &t1, &t2);
        addTri(pos, nor, uvs, p0, p2, p3, &t0, &t2, &t3);
      }
    }
  }
  return finish(pos, nor, uvs);
}

GeometryPtr makePlaneGeometry(double width, double height, int widthSegments, int heightSegments) {
  widthSegments = std::max(1, widthSegments);
  heightSegments = std::max(1, heightSegments);
  double w = width / 2, h = height / 2;
  std::vector<double> pos, nor, uvs;
  for (int iy = 0; iy < heightSegments; iy++) {
    for (int ix = 0; ix < widthSegments; ix++) {
      double x0 = -w + width * ix / widthSegments;
      double x1 = -w + width * (ix + 1) / widthSegments;
      double y0 = -h + height * iy / heightSegments;
      double y1 = -h + height * (iy + 1) / heightSegments;
      Vector3 p0(x0, y0, 0), p1(x1, y0, 0), p2(x1, y1, 0), p3(x0, y1, 0);
      Vector2 t0((x0 + w) / width, (y0 + h) / height), t1((x1 + w) / width, (y0 + h) / height);
      Vector2 t2((x1 + w) / width, (y1 + h) / height), t3((x0 + w) / width, (y1 + h) / height);
      addTri(pos, nor, uvs, p0, p1, p2, &t0, &t1, &t2);
      addTri(pos, nor, uvs, p0, p2, p3, &t0, &t2, &t3);
    }
  }
  return finish(pos, nor, uvs);
}

GeometryPtr makeSphereGeometry(double radius, int widthSegments, int heightSegments,
                               double phiStart, double phiLength, double thetaStart, double thetaLength) {
  widthSegments = std::max(2, widthSegments);
  heightSegments = std::max(1, heightSegments);
  std::vector<double> pos, nor, uvs;
  double ix, iy;
  for (iy = 0; iy <= heightSegments; iy++) {
    double v = iy / heightSegments;
    double theta = thetaStart + v * thetaLength;
    double sinTheta = std::sin(theta), cosTheta = std::cos(theta);
    for (ix = 0; ix <= widthSegments; ix++) {
      double u = ix / widthSegments;
      double phi = phiStart + u * phiLength;
      double sinPhi = std::sin(phi), cosPhi = std::cos(phi);
      double x = -radius * cosPhi * sinTheta;
      double y = radius * cosTheta;
      double z = radius * sinPhi * sinTheta;
      pos.push_back(x); pos.push_back(y); pos.push_back(z);
      nor.push_back(x / radius); nor.push_back(y / radius); nor.push_back(z / radius);
      uvs.push_back(u); uvs.push_back(1 - v);
    }
  }
  auto g = std::make_shared<BufferGeometry>();
  g->position = std::make_shared<BufferAttribute>(std::move(pos), 3);
  g->normal = std::make_shared<BufferAttribute>(std::move(nor), 3);
  g->uv = std::make_shared<BufferAttribute>(std::move(uvs), 2);
  // index grid
  std::vector<double> idx;
  for (iy = 0; iy < heightSegments; iy++) {
    for (ix = 0; ix < widthSegments; ix++) {
      double a = iy * (widthSegments + 1) + ix + 1;
      double b = a + widthSegments + 1;
      idx.push_back(iy * (widthSegments + 1) + ix);
      idx.push_back(b);
      idx.push_back(a);
      idx.push_back(iy * (widthSegments + 1) + ix);
      idx.push_back(b + 1);
      idx.push_back(b);
    }
  }
  g->index = std::make_shared<BufferAttribute>(idx, 1);
  return g;
}

GeometryPtr makeCylinderGeometry(double radiusTop, double radiusBottom, double height,
                                 int radialSegments, int heightSegments, bool openEnded) {
  radialSegments = std::max(3, radialSegments);
  heightSegments = std::max(1, heightSegments);
  std::vector<double> pos, nor, uvs;
  std::vector<std::array<size_t, 3>> caps;
  double halfHeight = height / 2;
  for (int iy = 0; iy < heightSegments; iy++) {
    double v = static_cast<double>(iy) / heightSegments;
    double y = -halfHeight + height * v;
    double r = radiusBottom + (radiusTop - radiusBottom) * v;
    for (int ix = 0; ix <= radialSegments; ix++) {
      double u = static_cast<double>(ix) / radialSegments;
      double theta = u * TAU;
      double sinTheta = std::sin(theta), cosTheta = std::cos(theta);
      double x = r * sinTheta, z = r * cosTheta;
      pos.push_back(x); pos.push_back(y); pos.push_back(z);
      double slope = (radiusBottom - radiusTop) / height;
      double nx = sinTheta, ny = slope, nz = cosTheta;
      double l = std::sqrt(nx * nx + ny * ny + nz * nz);
      nor.push_back(nx / l); nor.push_back(ny / l); nor.push_back(nz / l);
      uvs.push_back(u); uvs.push_back(1 - v);
    }
  }
  if (!openEnded) {
    for (int side = 0; side < 2; side++) {
      // side 0 = top cap (normal +y), side 1 = bottom cap (normal -y)
      double sign = side == 0 ? 1.0 : -1.0;
      double radius = side == 0 ? radiusTop : radiusBottom;
      double y = side == 0 ? halfHeight : -halfHeight;
      size_t centre = pos.size() / 3;
      pos.push_back(0); pos.push_back(y); pos.push_back(0);
      nor.push_back(0); nor.push_back(sign); nor.push_back(0);
      uvs.push_back(0.5); uvs.push_back(0.5);
      size_t ring = pos.size() / 3;
      for (int ix = 0; ix <= radialSegments; ix++) {
        double u = static_cast<double>(ix) / radialSegments;
        double theta = u * TAU;
        double x = radius * std::sin(theta), z = radius * std::cos(theta);
        pos.push_back(x); pos.push_back(y); pos.push_back(z);
        nor.push_back(0); nor.push_back(sign); nor.push_back(0);
        uvs.push_back(std::cos(theta) * 0.5 + 0.5);
        uvs.push_back(std::sin(theta) * 0.5 * sign + 0.5);
      }
      for (int ix = 0; ix < radialSegments; ix++) {
        size_t b = ring + static_cast<size_t>(ix);
        size_t c = b + 1;
        // keep the winding so the face normal points along `sign`
        if (side == 0) caps.push_back({centre, c, b});
        else caps.push_back({centre, b, c});
      }
    }
  }
  // side quads as index triangles
  std::vector<double> idx;
  for (int iy = 0; iy < heightSegments; iy++) {
    for (int ix = 0; ix < radialSegments; ix++) {
      double a = iy * (radialSegments + 1) + ix;
      double b = a + radialSegments + 1;
      idx.push_back(a);
      idx.push_back(b);
      idx.push_back(a + 1);
      idx.push_back(b);
      idx.push_back(b + 1);
      idx.push_back(a + 1);
    }
  }
  for (const auto& t : caps) {
    idx.push_back(static_cast<double>(t[0]));
    idx.push_back(static_cast<double>(t[1]));
    idx.push_back(static_cast<double>(t[2]));
  }
  auto g = std::make_shared<BufferGeometry>();
  g->position = std::make_shared<BufferAttribute>(pos, 3);
  g->normal = std::make_shared<BufferAttribute>(nor, 3);
  g->uv = std::make_shared<BufferAttribute>(uvs, 2);
  g->index = std::make_shared<BufferAttribute>(idx, 1);
  return g;
}

GeometryPtr makeConeGeometry(double radius, double height, int radialSegments, int heightSegments, bool openEnded) {
  return makeCylinderGeometry(0, radius, height, radialSegments, heightSegments, openEnded);
}

GeometryPtr makeCircleGeometry(double radius, int segments, double thetaStart, double thetaLength) {
  segments = std::max(3, segments);
  std::vector<double> pos, nor, uvs;
  pos.push_back(0); pos.push_back(0); pos.push_back(0);
  nor.push_back(0); nor.push_back(0); nor.push_back(1);
  uvs.push_back(0.5); uvs.push_back(0.5);
  for (int ix = 0; ix <= segments; ix++) {
    double u = static_cast<double>(ix) / segments;
    double theta = thetaStart + u * thetaLength;
    double x = radius * std::sin(theta), y = radius * std::cos(theta);
    pos.push_back(x); pos.push_back(y); pos.push_back(0);
    nor.push_back(0); nor.push_back(0); nor.push_back(1);
    uvs.push_back(x / radius * 0.5 + 0.5);
    uvs.push_back(y / radius * 0.5 + 0.5);
  }
  auto g = std::make_shared<BufferGeometry>();
  g->position = std::make_shared<BufferAttribute>(pos, 3);
  g->normal = std::make_shared<BufferAttribute>(nor, 3);
  g->uv = std::make_shared<BufferAttribute>(uvs, 2);
  std::vector<double> idx;
  for (int ix = 1; ix <= segments; ix++) {
    idx.push_back(0);
    idx.push_back(ix + 1);
    idx.push_back(ix);
  }
  g->index = std::make_shared<BufferAttribute>(idx, 1);
  return g;
}

GeometryPtr makeTorusGeometry(double radius, double tube, int radialSegments, int tubularSegments, double arc) {
  radialSegments = std::max(3, radialSegments);
  tubularSegments = std::max(3, tubularSegments);
  std::vector<double> pos, nor, uvs;
  for (int j = 0; j <= radialSegments; j++) {
    for (int i = 0; i <= tubularSegments; i++) {
      double u = static_cast<double>(i) / tubularSegments * arc;
      double v = static_cast<double>(j) / radialSegments * TAU;
      double cx = radius * std::cos(u), cy = radius * std::sin(u);
      double x = (radius + tube * std::cos(v)) * std::cos(u);
      double y = (radius + tube * std::cos(v)) * std::sin(u);
      double z = tube * std::sin(v);
      pos.push_back(x); pos.push_back(y); pos.push_back(z);
      double nx = x - cx, ny = y - cy, nz = z;
      double l = std::sqrt(nx * nx + ny * ny + nz * nz);
      nor.push_back(nx / l); nor.push_back(ny / l); nor.push_back(nz / l);
      uvs.push_back(static_cast<double>(i) / tubularSegments);
      uvs.push_back(static_cast<double>(j) / radialSegments);
    }
  }
  auto g = std::make_shared<BufferGeometry>();
  g->position = std::make_shared<BufferAttribute>(pos, 3);
  g->normal = std::make_shared<BufferAttribute>(nor, 3);
  g->uv = std::make_shared<BufferAttribute>(uvs, 2);
  std::vector<double> idx;
  for (int j = 1; j <= radialSegments; j++) {
    for (int i = 1; i <= tubularSegments; i++) {
      double a = (tubularSegments + 1) * j + i - 1;
      double b = (tubularSegments + 1) * (j - 1) + i - 1;
      double c = (tubularSegments + 1) * (j - 1) + i;
      double d = (tubularSegments + 1) * j + i;
      idx.push_back(a); idx.push_back(b); idx.push_back(d);
      idx.push_back(b); idx.push_back(c); idx.push_back(d);
    }
  }
  g->index = std::make_shared<BufferAttribute>(idx, 1);
  return g;
}

GeometryPtr makeIcosahedronGeometry(double radius, int detail) {
  // golden ratio tetrahedra-based icosahedron (three.js IcosahedronGeometry)
  const double t = (1.0 + std::sqrt(5.0)) / 2.0;
  Vector3 verts[12] = {
      Vector3(-1, t, 0), Vector3(1, t, 0), Vector3(-1, -t, 0), Vector3(1, -t, 0),
      Vector3(0, -1, t), Vector3(0, 1, t), Vector3(0, -1, -t), Vector3(0, 1, -t),
      Vector3(t, 0, -1), Vector3(t, 0, 1), Vector3(-t, 0, -1), Vector3(-t, 0, 1),
  };
  int faces[20][3] = {
      {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
      {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
      {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
      {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
  };
  std::vector<Vector3> v(verts, verts + 12);
  std::vector<std::array<int, 3>> f;
  for (int i = 0; i < 20; i++) f.push_back({faces[i][0], faces[i][1], faces[i][2]});
  for (int d = 0; d < detail; d++) {
    std::vector<std::array<int, 3>> nf;
    std::vector<std::pair<int, int>> edges;
    for (const auto& tri : f) {
      int m[3];
      for (int k = 0; k < 3; k++) {
        int a = tri[k], b = tri[(k + 1) % 3];
        if (a > b) std::swap(a, b);
        int mid = -1;
        for (size_t e = 0; e < edges.size(); e++)
          if (edges[e].first == a && edges[e].second == b) { mid = static_cast<int>(v.size() + e); break; }
        if (mid < 0) {
          edges.push_back({a, b});
          Vector3 p;
          p.addVectors(v[a], v[b]).normalize().multiplyScalar(radius);
          v.push_back(p);
          mid = static_cast<int>(v.size() - 1);
        }
        m[k] = mid;
      }
      nf.push_back({tri[0], m[0], m[2]});
      nf.push_back({tri[1], m[1], m[0]});
      nf.push_back({tri[2], m[2], m[1]});
      nf.push_back({m[0], m[1], m[2]});
    }
    f = nf;
  }
  for (auto& p : v) p.normalize().multiplyScalar(radius);
  std::vector<double> pos, nor, uvs;
  for (const auto& tri : f)
    for (int k = 0; k < 3; k++) {
      const Vector3& p = v[tri[k]];
      pos.push_back(p.x); pos.push_back(p.y); pos.push_back(p.z);
      nor.push_back(p.x / radius); nor.push_back(p.y / radius); nor.push_back(p.z / radius);
      uvs.push_back(std::atan2(p.z, p.x) / TAU + 0.5);
      uvs.push_back(std::asin(std::max(-1.0, std::min(1.0, p.y / radius))) / PI + 0.5);
    }
  auto g = std::make_shared<BufferGeometry>();
  g->position = std::make_shared<BufferAttribute>(pos, 3);
  g->normal = std::make_shared<BufferAttribute>(nor, 3);
  g->uv = std::make_shared<BufferAttribute>(uvs, 2);
  return g;
}

GeometryPtr mergeGeometries(const std::vector<GeometryPtr>& geometries, bool useGroups) {
  (void)useGroups;
  auto out = std::make_shared<BufferGeometry>();
  std::vector<double> pos, nor, uvs, cols;
  bool hasNor = false, hasUv = false, hasCol = false;
  for (const auto& g : geometries) {
    if (!g || !g->position) continue;
    if (g->normal) hasNor = true;
    if (g->uv) hasUv = true;
    if (g->color) hasCol = true;
  }
  for (const auto& g : geometries) {
    if (!g || !g->position) continue;
    size_t n = g->position->count();
    for (size_t i = 0; i < n; i++) {
      pos.push_back(g->position->getX(i));
      pos.push_back(g->position->getY(i));
      pos.push_back(g->position->getZ(i));
      if (hasNor) {
        if (g->normal) {
          nor.push_back(g->normal->getX(i));
          nor.push_back(g->normal->getY(i));
          nor.push_back(g->normal->getZ(i));
        } else {
          nor.push_back(0); nor.push_back(0); nor.push_back(1);
        }
      }
      if (hasUv) {
        if (g->uv) {
          uvs.push_back(g->uv->getX(i));
          uvs.push_back(g->uv->getY(i));
        } else {
          uvs.push_back(0); uvs.push_back(0);
        }
      }
      if (hasCol) {
        if (g->color) {
          cols.push_back(g->color->getX(i));
          cols.push_back(g->color->getY(i));
          cols.push_back(g->color->getZ(i));
        } else {
          cols.push_back(1); cols.push_back(1); cols.push_back(1);
        }
      }
    }
  }
  out->position = std::make_shared<BufferAttribute>(std::move(pos), 3);
  if (hasNor) out->normal = std::make_shared<BufferAttribute>(std::move(nor), 3);
  if (hasUv) out->uv = std::make_shared<BufferAttribute>(std::move(uvs), 2);
  if (hasCol) out->color = std::make_shared<BufferAttribute>(std::move(cols), 3);
  return out;
}

GeometryPtr mergeAndComputeNormals(const std::vector<GeometryPtr>& geometries) {
  auto g = mergeGeometries(geometries, false);
  g->computeVertexNormals();
  return g;
}

}  // namespace fs
