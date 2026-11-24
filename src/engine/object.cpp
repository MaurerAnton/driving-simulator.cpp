// SPDX-License-Identifier: AGPL-3.0-only
// Origin: three.js r187 src/core/Object3D.js, src/objects/InstancedMesh.js, src/objects/LOD.js,
//         and what of src/math/Box3.js and src/math/Frustum.js asks an object
#include "engine/engine.h"
#include "engine/gpu.h"

namespace fs {

// ---------------------------------------------------------------------------
// Rotation: the Euler angles of an object, one thing with its quaternion
// ---------------------------------------------------------------------------
//
// three.js keeps the two alike with callbacks: a change of the angles sets the quaternion
// (`quaternion.setFromEuler(rotation)`), a change of the quaternion sets the angles
// (`rotation.setFromQuaternion(quaternion)`, in the order the angles have).  Here the
// quaternion is a plain struct, so the angles follow it when they are next asked for:
// `seen_` is the quaternion the angles stand for.  (One difference to three.js remains:
// a quaternion that is written with the very value it has leaves the angles as they are,
// where three.js works them out anew – the same rotation, perhaps by other angles.)

void Rotation::pull() const {
  if (*q_ != seen_) {
    e_.setFromQuaternion(*q_, e_.order);
    seen_ = *q_;
  }
}

void Rotation::push() {
  q_->setFromEuler(e_);
  seen_ = *q_;
}

Rotation::Component::operator double() const {
  r_->pull();
  return i_ == 0 ? r_->e_.x : i_ == 1 ? r_->e_.y : r_->e_.z;
}

Rotation::Component& Rotation::Component::operator=(double v) {
  r_->pull();
  if (i_ == 0) r_->e_.x = v;
  else if (i_ == 1) r_->e_.y = v;
  else r_->e_.z = v;
  r_->push();
  return *this;
}

Rotation& Rotation::set(double x_, double y_, double z_) {
  // (the order stays; the other angles need not be worked out first: all three are given)
  e_.x = x_;
  e_.y = y_;
  e_.z = z_;
  push();
  return *this;
}

Rotation& Rotation::set(double x_, double y_, double z_, Euler::Order order) {
  e_.set(x_, y_, z_, order);
  push();
  return *this;
}

Rotation& Rotation::copy(const Rotation& o) { return copy(o.euler()); }

Rotation& Rotation::copy(const Euler& e) {
  e_.copy(e);
  push();
  return *this;
}

void Rotation::order(Euler::Order o) {
  // `rotation.order = o`: the angles stay, read in another order – the quaternion follows
  pull();
  e_.order = o;
  push();
}

Rotation& Rotation::setFromQuaternion(const Quaternion& q) {
  e_.setFromQuaternion(q, e_.order);
  push();
  return *this;
}

Rotation& Rotation::setFromRotationMatrix(const Matrix4& m) {
  e_.setFromRotationMatrix(m, e_.order);
  push();
  return *this;
}

Euler Rotation::euler() const {
  pull();
  return e_;
}

// ---------------------------------------------------------------------------
// Object3D
// ---------------------------------------------------------------------------

Object3D::Object3D() : Object3D(ObjectType::Object3D) {}

Object3D::Object3D(ObjectType t) : id(gpu::nextId()), type(t) {}

Object3D::~Object3D() {
  // (children that are held elsewhere live on, without a parent)
  for (const ObjectPtr& child : children)
    if (child) child->parent = nullptr;
}

Object3D& Object3D::add(const ObjectPtr& child) {
  // (a copy: `child` may be the old parent's own reference, which removeFromParent() takes away)
  const ObjectPtr object = child;
  if (!object) return *this;
  if (object.get() == this) return *this;   // an object can't be added as a child of itself
  object->removeFromParent();
  object->parent = this;
  children.push_back(object);
  return *this;
}

Object3D& Object3D::remove(const ObjectPtr& child) {
  const ObjectPtr object = child;
  return remove(object.get());
}

Object3D& Object3D::remove(Object3D* child) {
  for (size_t index = 0; index < children.size(); index++) {
    if (children[index].get() == child) {
      const ObjectPtr object = children[index];   // (kept until it is out of the list)
      object->parent = nullptr;
      children.erase(children.begin() + static_cast<std::ptrdiff_t>(index));
      break;
    }
  }
  return *this;
}

Object3D& Object3D::removeFromParent() {
  Object3D* const p = parent;
  if (p != nullptr) {
    const ObjectPtr self = weak_from_this().lock();   // (the parent's may be the last reference)
    p->remove(this);
  }
  return *this;
}

Object3D& Object3D::clear() {
  std::vector<ObjectPtr> old;
  old.swap(children);
  for (const ObjectPtr& child : old)
    if (child) child->parent = nullptr;
  return *this;
}

Object3D& Object3D::attach(const ObjectPtr& child) {
  // adds object as a child of this, while maintaining the object's world transform
  // Note: This method does not support scene graphs having non-uniformly-scaled nodes(s)
  const ObjectPtr object = child;
  if (!object) return *this;
  updateWorldMatrix(true, false);
  Matrix4 m1;
  m1.copy(matrixWorld).invert();
  if (object->parent != nullptr) {
    object->parent->updateWorldMatrix(true, false);
    m1.multiply(object->parent->matrixWorld);
  }
  object->applyMatrix4(m1);
  object->removeFromParent();
  object->parent = this;
  children.push_back(object);
  object->updateWorldMatrix(false, true);
  return *this;
}

void Object3D::updateMatrix() {
  matrix.compose(position, quaternion, scale);
  matrixWorldNeedsUpdate = true;
}

void Object3D::updateMatrixWorld(bool force) {
  if (matrixAutoUpdate) updateMatrix();
  if (matrixWorldNeedsUpdate || force) {
    if (matrixWorldAutoUpdate == true) {
      if (parent == nullptr) {
        matrixWorld.copy(matrix);
      } else {
        matrixWorld.multiplyMatrices(parent->matrixWorld, matrix);
      }
    }
    matrixWorldNeedsUpdate = false;
    force = true;
  }
  // make sure descendants are updated if required
  for (size_t i = 0, l = children.size(); i < l && i < children.size(); i++) children[i]->updateMatrixWorld(force);
}

void Object3D::updateWorldMatrix(bool updateParents, bool updateChildren, bool force) {
  if (updateParents == true && parent != nullptr) parent->updateWorldMatrix(true, false);
  if (matrixAutoUpdate) updateMatrix();
  if (matrixWorldNeedsUpdate || force) {
    if (matrixWorldAutoUpdate == true) {
      if (parent == nullptr) {
        matrixWorld.copy(matrix);
      } else {
        matrixWorld.multiplyMatrices(parent->matrixWorld, matrix);
      }
    }
    matrixWorldNeedsUpdate = false;
    force = true;
  }
  // make sure descendants are updated
  if (updateChildren == true) {
    for (size_t i = 0, l = children.size(); i < l && i < children.size(); i++) children[i]->updateWorldMatrix(false, true, force);
  }
}

void Object3D::lookAt(double x, double y, double z) {
  // This method does not support objects having non-uniformly-scaled parent(s)
  const Vector3 target(x, y, z);
  updateWorldMatrix(true, false);
  Vector3 position_;
  position_.setFromMatrixPosition(matrixWorld);
  Matrix4 m1;
  if (type == ObjectType::Camera || type == ObjectType::Light) {
    m1.lookAt(position_, target, up);
  } else {
    m1.lookAt(target, position_, up);
  }
  quaternion.setFromRotationMatrix(m1);
  if (parent) {
    m1.extractRotation(parent->matrixWorld);
    Quaternion q1;
    q1.setFromRotationMatrix(m1);
    quaternion.premultiply(q1.invert());
  }
}

Object3D& Object3D::applyMatrix4(const Matrix4& m) {
  if (matrixAutoUpdate) updateMatrix();
  matrix.premultiply(m);
  matrix.decompose(position, quaternion, scale);
  return *this;
}

Object3D& Object3D::applyQuaternion(const Quaternion& q) {
  quaternion.premultiply(q);
  return *this;
}

Object3D& Object3D::rotateOnAxis(const Vector3& axis, double angle) {
  // rotate object on axis in object space
  // axis is assumed to be normalized
  Quaternion q1;
  q1.setFromAxisAngle(axis, angle);
  quaternion.multiply(q1);
  return *this;
}

Object3D& Object3D::rotateOnWorldAxis(const Vector3& axis, double angle) {
  // rotate object on axis in world space
  // axis is assumed to be normalized
  // method assumes no rotated parent
  Quaternion q1;
  q1.setFromAxisAngle(axis, angle);
  quaternion.premultiply(q1);
  return *this;
}

Object3D& Object3D::rotateX(double angle) { return rotateOnAxis(Vector3(1, 0, 0), angle); }
Object3D& Object3D::rotateY(double angle) { return rotateOnAxis(Vector3(0, 1, 0), angle); }
Object3D& Object3D::rotateZ(double angle) { return rotateOnAxis(Vector3(0, 0, 1), angle); }

Object3D& Object3D::translateOnAxis(const Vector3& axis, double distance) {
  // translate object by distance along axis in object space
  // axis is assumed to be normalized
  Vector3 v1;
  v1.copy(axis).applyQuaternion(quaternion);
  position.add(v1.multiplyScalar(distance));
  return *this;
}

Object3D& Object3D::translateX(double d) { return translateOnAxis(Vector3(1, 0, 0), d); }
Object3D& Object3D::translateY(double d) { return translateOnAxis(Vector3(0, 1, 0), d); }
Object3D& Object3D::translateZ(double d) { return translateOnAxis(Vector3(0, 0, 1), d); }

// (three.js brings the world matrix up to date before it is used: the object is not changed in any
//  other way, so these stay callable on a const object as engine.h declares them)
Vector3& Object3D::localToWorld(Vector3& v) const {
  const_cast<Object3D*>(this)->updateWorldMatrix(true, false);
  return v.applyMatrix4(matrixWorld);
}

Vector3& Object3D::worldToLocal(Vector3& v) const {
  const_cast<Object3D*>(this)->updateWorldMatrix(true, false);
  Matrix4 m1;
  return v.applyMatrix4(m1.copy(matrixWorld).invert());
}

Vector3& Object3D::getWorldPosition(Vector3& target) {
  updateWorldMatrix(true, false);
  return target.setFromMatrixPosition(matrixWorld);
}

Quaternion& Object3D::getWorldQuaternion(Quaternion& target) {
  updateWorldMatrix(true, false);
  Vector3 position_, scale_;
  matrixWorld.decompose(position_, target, scale_);
  return target;
}

Vector3& Object3D::getWorldScale(Vector3& target) {
  updateWorldMatrix(true, false);
  Vector3 position_;
  Quaternion quaternion_;
  matrixWorld.decompose(position_, quaternion_, target);
  return target;
}

Vector3& Object3D::getWorldDirection(Vector3& target) {
  updateWorldMatrix(true, false);
  const double* e = matrixWorld.elements;
  return target.set(e[8], e[9], e[10]).normalize();
}

void Object3D::traverse(const std::function<void(Object3D&)>& fn) {
  fn(*this);
  for (size_t i = 0, l = children.size(); i < l && i < children.size(); i++) {
    const ObjectPtr child = children[i];   // (the callback may take it out of the list)
    child->traverse(fn);
  }
}

void Object3D::traverseVisible(const std::function<void(Object3D&)>& fn) {
  if (visible == false) return;
  fn(*this);
  for (size_t i = 0, l = children.size(); i < l && i < children.size(); i++) {
    const ObjectPtr child = children[i];
    child->traverseVisible(fn);
  }
}

Object3D* Object3D::getObjectByName(const std::string& n) {
  if (name == n) return this;
  for (const ObjectPtr& child : children) {
    Object3D* const object = child->getObjectByName(n);
    if (object != nullptr) return object;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// InstancedMesh
// ---------------------------------------------------------------------------

InstancedMesh::InstancedMesh(GeometryPtr g, MaterialPtr m, int capacity)
    : Mesh(ObjectType::InstancedMesh, std::move(g), std::move(m)), count(capacity), capacity_(capacity) {
  instanceMatrix = attr(std::vector<float>(static_cast<size_t>(capacity) * 16), 16);
  instanceMatrix->divisor = 1;
  const Matrix4 identity;
  for (int i = 0; i < capacity; i++) setMatrixAt(i, identity);
}

void InstancedMesh::setMatrixAt(int i, const Matrix4& m) {
  const size_t offset = static_cast<size_t>(i) * 16;
  for (size_t k = 0; k < 16; k++) instanceMatrix->put(offset + k, m.elements[k]);
}

void InstancedMesh::getMatrixAt(int i, Matrix4& m) const {
  const size_t offset = static_cast<size_t>(i) * 16;
  for (size_t k = 0; k < 16; k++) m.elements[k] = instanceMatrix->at(offset + k);
}

void InstancedMesh::setColorAt(int i, const Color& c) {
  if (!instanceColor) {
    instanceColor = attr(std::vector<float>(instanceMatrix->count() * 3, 1.0f), 3);
    instanceColor->divisor = 1;
  }
  const size_t offset = static_cast<size_t>(i) * 3;
  instanceColor->put(offset, c.r);
  instanceColor->put(offset + 1, c.g);
  instanceColor->put(offset + 2, c.b);
}

void InstancedMesh::getColorAt(int i, Color& c) const {
  if (!instanceColor) {
    c.setRGB(1, 1, 1);
  } else {
    const size_t offset = static_cast<size_t>(i) * 3;
    c.r = instanceColor->at(offset);
    c.g = instanceColor->at(offset + 1);
    c.b = instanceColor->at(offset + 2);
  }
}

void InstancedMesh::computeBoundingSphere() {
  if (!boundingSphere) boundingSphere = Sphere();
  if (!geometry) return;
  if (!geometry->boundingSphere) geometry->computeBoundingSphere();
  boundingSphere->makeEmpty();
  Matrix4 instanceLocalMatrix;
  Sphere sphere;
  for (int i = 0; i < count; i++) {
    getMatrixAt(i, instanceLocalMatrix);
    sphere.copy(*geometry->boundingSphere).applyMatrix4(instanceLocalMatrix);
    boundingSphere->union_(sphere);
  }
}

// ---------------------------------------------------------------------------
// LOD
// ---------------------------------------------------------------------------

LOD& LOD::addLevel(const ObjectPtr& object, double distance, double hysteresis) {
  distance = std::fabs(distance);
  size_t l;
  for (l = 0; l < levels.size(); l++) {
    if (distance < levels[l].distance) break;
  }
  levels.insert(levels.begin() + static_cast<std::ptrdiff_t>(l), Level{object, distance, hysteresis});
  add(object);
  return *this;
}

void LOD::update(const Camera& camera) {
  if (levels.size() > 1) {
    Vector3 v1, v2;
    v1.setFromMatrixPosition(camera.matrixWorld);
    v2.setFromMatrixPosition(matrixWorld);
    const double distance = v1.distanceTo(v2) / camera.zoom;
    levels[0].object->visible = true;
    size_t i, l;
    for (i = 1, l = levels.size(); i < l; i++) {
      double levelDistance = levels[i].distance;
      if (levels[i].object->visible) levelDistance -= levelDistance * levels[i].hysteresis;
      if (distance >= levelDistance) {
        levels[i - 1].object->visible = false;
        levels[i].object->visible = true;
      } else {
        break;
      }
    }
    current_ = static_cast<int>(i - 1);
    for (; i < l; i++) levels[i].object->visible = false;
  }
}

// ---------------------------------------------------------------------------
// Box3 and Frustum, asked about an object
// ---------------------------------------------------------------------------

Box3& Box3::setFromObject(Object3D& object, bool precise) {
  makeEmpty();
  return expandByObject(object, precise);
}

Box3& Box3::expandByObject(Object3D& object, bool precise) {
  // Computes the world-axis-aligned bounding box of an object (including its children),
  // accounting for both the object's, and children's, world transforms
  object.updateWorldMatrix(false, false);
  // (what has a geometry: a Mesh and what is made of it – InstancedMesh, Points, Line, LineSegments)
  Mesh* const mesh = dynamic_cast<Mesh*>(&object);
  const GeometryPtr geometry = mesh ? mesh->geometry : nullptr;
  if (geometry) {
    const AttrPtr positionAttribute = geometry->getAttribute("position");
    // precise AABB computation based on vertex data requires at least a position attribute.
    // instancing isn't supported so far and uses the normal (conservative) code path.
    if (precise == true && positionAttribute && object.type != ObjectType::InstancedMesh) {
      Vector3 vector;
      for (size_t i = 0, l = positionAttribute->count(); i < l; i++) {
        vector.fromBufferAttribute(*positionAttribute, i);
        vector.applyMatrix4(object.matrixWorld);
        expandByPoint(vector);
      }
    } else {
      Box3 box;
      if (object.type == ObjectType::InstancedMesh) {
        // object-level bounding box: the geometry's box through every instance's matrix
        InstancedMesh& instanced = static_cast<InstancedMesh&>(object);
        if (!geometry->boundingBox) geometry->computeBoundingBox();
        Matrix4 instanceLocalMatrix;
        Box3 box3;
        for (int i = 0; i < instanced.count; i++) {
          instanced.getMatrixAt(i, instanceLocalMatrix);
          box3.copy(*geometry->boundingBox).applyMatrix4(instanceLocalMatrix);
          box.union_(box3);
        }
      } else {
        // geometry-level bounding box
        if (!geometry->boundingBox) geometry->computeBoundingBox();
        box.copy(*geometry->boundingBox);
      }
      box.applyMatrix4(object.matrixWorld);
      union_(box);
    }
  }
  if (object.type == ObjectType::Sprite) {
    // (three.js's sprites share one geometry: a square of side 1 about the origin)
    if (precise == true) {
      const double corners[4][2] = {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}};
      Vector3 vector;
      for (const auto& corner : corners) expandByPoint(vector.set(corner[0], corner[1], 0).applyMatrix4(object.matrixWorld));
    } else {
      Box3 box(Vector3(-0.5, -0.5, 0), Vector3(0.5, 0.5, 0));
      box.applyMatrix4(object.matrixWorld);
      union_(box);
    }
  }
  for (size_t i = 0, l = object.children.size(); i < l; i++) expandByObject(*object.children[i], precise);
  return *this;
}

bool Frustum::intersectsObject(Object3D& object) const {
  Sphere sphere;
  if (object.type == ObjectType::InstancedMesh) {
    InstancedMesh& instanced = static_cast<InstancedMesh&>(object);
    if (!instanced.boundingSphere) instanced.computeBoundingSphere();
    sphere.copy(*instanced.boundingSphere).applyMatrix4(object.matrixWorld);
  } else {
    Mesh* const mesh = dynamic_cast<Mesh*>(&object);
    if (!mesh || !mesh->geometry) return true;   // (nothing to cull it by)
    BufferGeometry& geometry = *mesh->geometry;
    if (!geometry.boundingSphere) geometry.computeBoundingSphere();
    sphere.copy(*geometry.boundingSphere).applyMatrix4(object.matrixWorld);
  }
  return intersectsSphere(sphere);
}

bool Frustum::intersectsSprite(const Sprite& sprite) const {
  Sphere sphere;
  sphere.center.set(0, 0, 0);
  const double offset = Vector2(0.5, 0.5).distanceTo(sprite.center);
  sphere.radius = 0.7071067811865476 + offset;
  sphere.applyMatrix4(sprite.matrixWorld);
  return intersectsSphere(sphere);
}

}  // namespace fs
