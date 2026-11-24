// SPDX-License-Identifier: AGPL-3.0-only
// Origin: three.js r187 src/core/Raycaster.js, and the raycast() of src/objects/{Mesh,InstancedMesh,LOD}.js
#include "engine/engine.h"
#include "engine/internal.h"

namespace fs {

namespace {

/** checkIntersection + checkGeometryIntersection of Mesh.js: the triangle a, b, c of a mesh against the ray */
bool checkGeometryIntersection(Object3D& object, const Matrix4& matrixWorld, const Material& material, const Raycaster& raycaster, const Ray& ray,
                               const BufferAttribute& position, size_t a, size_t b, size_t c, Intersection& intersection) {
  Vector3 vA, vB, vC, point;
  vA.fromBufferAttribute(position, a);
  vB.fromBufferAttribute(position, b);
  vC.fromBufferAttribute(position, c);
  const Vector3* intersect;
  if (material.side == Side::Back) {
    intersect = ray.intersectTriangle(vC, vB, vA, true, point);
  } else {
    intersect = ray.intersectTriangle(vA, vB, vC, (material.side == Side::Front), point);
  }
  if (intersect == nullptr) return false;
  Vector3 intersectionPointWorld;
  intersectionPointWorld.copy(point);
  intersectionPointWorld.applyMatrix4(matrixWorld);
  const double distance = raycaster.ray.origin.distanceTo(intersectionPointWorld);
  if (distance < raycaster.near || distance > raycaster.far) return false;
  intersection.distance = distance;
  intersection.point = intersectionPointWorld;
  intersection.object = &object;
  intersection.a = static_cast<int>(a);
  intersection.b = static_cast<int>(b);
  intersection.c = static_cast<int>(c);
  Triangle::getNormal(vA, vB, vC, intersection.normal);
  intersection.instanceId = -1;
  return true;
}

/**
 * Mesh.raycast: `object` is what a hit names; the geometry and the material(s) are the mesh's, the world
 * matrix the mesh's own – or, of an InstancedMesh, the one of an instance.
 */
void raycastMesh(Object3D& object, Mesh& mesh, const Matrix4& matrixWorld, const Raycaster& raycaster, std::vector<Intersection>& intersects) {
  if (!mesh.geometry) return;
  BufferGeometry& geometry = *mesh.geometry;
  const bool materialArray = !mesh.materials.empty();
  if (!materialArray && !mesh.material) return;

  // test with bounding sphere in world space
  if (!geometry.boundingSphere) geometry.computeBoundingSphere();
  Sphere sphere;
  sphere.copy(*geometry.boundingSphere);
  sphere.applyMatrix4(matrixWorld);

  // check distance from ray origin to bounding sphere
  Ray ray;
  ray.copy(raycaster.ray).recast(raycaster.near);
  if (sphere.containsPoint(ray.origin) == false) {
    Vector3 sphereHitAt;
    if (ray.intersectSphere(sphere, sphereHitAt) == nullptr) return;
    const double range = raycaster.far - raycaster.near;
    if (ray.origin.distanceToSquared(sphereHitAt) > range * range) return;
  }

  // convert ray to local space of mesh
  Matrix4 inverseMatrix;
  inverseMatrix.copy(matrixWorld).invert();
  ray.copy(raycaster.ray).applyMatrix4(inverseMatrix);

  // test with bounding box in local space
  if (geometry.boundingBox) {
    if (ray.intersectsBox(*geometry.boundingBox) == false) return;
  }

  // test for intersections with geometry (_computeIntersections)
  const AttrPtr index = geometry.index;
  const AttrPtr position = geometry.getAttribute("position");
  if (!position) return;
  const std::vector<BufferGeometry::Group>& groups = geometry.groups;
  const double drawStart = geometry.drawRange.start;
  // (drawRange.count is "to the end" when it is INT_MAX: three.js's Infinity)
  const double drawEnd = geometry.drawRange.count == INT_MAX ? JS_INF : drawStart + geometry.drawRange.count;
  Intersection intersection;
  const double vertexCount = static_cast<double>(index ? index->count() : position->count());
  const auto corner = [&](size_t i) { return index ? enginedetail::vertexAt(*index, i) : i; };
  if (materialArray) {
    for (const BufferGeometry::Group& group : groups) {
      if (group.materialIndex < 0 || static_cast<size_t>(group.materialIndex) >= mesh.materials.size()) continue;
      const MaterialPtr& groupMaterial = mesh.materials[static_cast<size_t>(group.materialIndex)];
      if (!groupMaterial) continue;
      const double start = jsMax(0, jsMax(group.start, drawStart));
      const double end = jsMin(vertexCount, jsMin(static_cast<double>(group.start) + group.count, drawEnd));
      for (double j = start; j < end; j += 3) {
        const size_t i = static_cast<size_t>(j);
        if (checkGeometryIntersection(object, matrixWorld, *groupMaterial, raycaster, ray, *position, corner(i), corner(i + 1), corner(i + 2), intersection)) {
          intersects.push_back(intersection);
        }
      }
    }
  } else {
    const double start = jsMax(0, drawStart);
    const double end = jsMin(vertexCount, drawEnd);
    for (double j = start; j < end; j += 3) {
      const size_t i = static_cast<size_t>(j);
      if (checkGeometryIntersection(object, matrixWorld, *mesh.material, raycaster, ray, *position, corner(i), corner(i + 1), corner(i + 2), intersection)) {
        intersects.push_back(intersection);
      }
    }
  }
}

/** InstancedMesh.raycast */
void raycastInstancedMesh(InstancedMesh& mesh, const Raycaster& raycaster, std::vector<Intersection>& intersects) {
  const Matrix4& matrixWorld = mesh.matrixWorld;
  const int raycastTimes = mesh.count;
  if (!mesh.material && mesh.materials.empty()) return;
  if (!mesh.geometry) return;

  // test with bounding sphere first
  if (!mesh.boundingSphere) mesh.computeBoundingSphere();
  Sphere sphere;
  sphere.copy(*mesh.boundingSphere);
  sphere.applyMatrix4(matrixWorld);
  if (raycaster.ray.intersectsSphere(sphere) == false) return;

  // now test each instance
  Matrix4 instanceLocalMatrix, instanceWorldMatrix;
  std::vector<Intersection> instanceIntersects;
  for (int instanceId = 0; instanceId < raycastTimes; instanceId++) {
    // calculate the world matrix for each instance
    mesh.getMatrixAt(instanceId, instanceLocalMatrix);
    instanceWorldMatrix.multiplyMatrices(matrixWorld, instanceLocalMatrix);
    // the mesh represents this single instance
    raycastMesh(mesh, mesh, instanceWorldMatrix, raycaster, instanceIntersects);
    // process the result of raycast
    for (Intersection& intersect : instanceIntersects) {
      intersect.instanceId = instanceId;
      intersect.object = &mesh;
      intersects.push_back(intersect);
    }
    instanceIntersects.clear();
  }
}

/** object.raycast(raycaster, intersects), by what the object is (a sprite, points and lines are not asked) */
void raycastObject(Object3D& object, const Raycaster& raycaster, std::vector<Intersection>& intersects);

/** LOD.getObjectForDistance */
Object3D* objectForDistance(const LOD& lod, double distance) {
  const std::vector<LOD::Level>& levels = lod.levels;
  if (levels.size() > 0) {
    size_t i, l;
    for (i = 1, l = levels.size(); i < l; i++) {
      double levelDistance = levels[i].distance;
      if (levels[i].object->visible) levelDistance -= levelDistance * levels[i].hysteresis;
      if (distance < levelDistance) break;
    }
    return levels[i - 1].object.get();
  }
  return nullptr;
}

void raycastObject(Object3D& object, const Raycaster& raycaster, std::vector<Intersection>& intersects) {
  switch (object.type) {
    case ObjectType::Mesh: {
      Mesh& mesh = static_cast<Mesh&>(object);
      raycastMesh(mesh, mesh, mesh.matrixWorld, raycaster, intersects);
      break;
    }
    case ObjectType::InstancedMesh:
      raycastInstancedMesh(static_cast<InstancedMesh&>(object), raycaster, intersects);
      break;
    case ObjectType::LOD: {
      LOD& lod = static_cast<LOD&>(object);
      if (lod.levels.size() > 0) {
        Vector3 v1;
        v1.setFromMatrixPosition(lod.matrixWorld);
        const double distance = raycaster.ray.origin.distanceTo(v1);
        Object3D* const level = objectForDistance(lod, distance);
        if (level) raycastObject(*level, raycaster, intersects);
      }
      break;
    }
    default:
      break;
  }
}

/** intersect() of Raycaster.js (the raycaster's layers are three.js's default: layer 0) */
void intersect(Object3D& object, const Raycaster& raycaster, std::vector<Intersection>& intersects, bool recursive) {
  if ((object.layers & 1u) != 0) raycastObject(object, raycaster, intersects);
  if (recursive == true) {
    for (size_t i = 0, l = object.children.size(); i < l; i++) intersect(*object.children[i], raycaster, intersects, true);
  }
}

void ascSort(std::vector<Intersection>& intersects) {
  std::stable_sort(intersects.begin(), intersects.end(), [](const Intersection& a, const Intersection& b) { return a.distance < b.distance; });
}

}  // namespace

void Raycaster::setFromCamera(const Vector2& coords, const Camera& camera) {
  if (camera.isPerspective) {
    ray.origin.setFromMatrixPosition(camera.matrixWorld);
    ray.direction.set(coords.x, coords.y, 0.5).unproject(camera).sub(ray.origin).normalize();
  } else {
    ray.origin.set(coords.x, coords.y, camera.projectionMatrix.elements[14]).unproject(camera);   // set origin in plane of camera
    ray.direction.set(0, 0, -1).transformDirection(camera.matrixWorld);
  }
}

std::vector<Intersection> Raycaster::intersectObject(Object3D& object, bool recursive) const {
  std::vector<Intersection> intersects;
  intersect(object, *this, intersects, recursive);
  ascSort(intersects);
  return intersects;
}

std::vector<Intersection> Raycaster::intersectObjects(const std::vector<ObjectPtr>& objects, bool recursive) const {
  std::vector<Intersection> intersects;
  for (const ObjectPtr& object : objects)
    if (object) intersect(*object, *this, intersects, recursive);
  ascSort(intersects);
  return intersects;
}

}  // namespace fs
