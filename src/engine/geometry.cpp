// SPDX-License-Identifier: AGPL-3.0-only
// Origin: three.js r187 src/core/BufferGeometry.js (InstancedBufferGeometry.js: `instanceCount`)
#include "engine/engine.h"
#include "engine/gpu.h"

namespace fs {

BufferGeometry::BufferGeometry() : id(gpu::nextId()) {}

BufferGeometry::~BufferGeometry() { gpu::release(gpu::Kind::VertexArray, gpuVao); }

// attributes[name] = attribute: a name that is there keeps its place, a new one goes to the end
BufferGeometry& BufferGeometry::setAttribute(const std::string& name, AttrPtr a) {
  for (auto& entry : attributes) {
    if (entry.first == name) {
      entry.second = std::move(a);
      return *this;
    }
  }
  attributes.emplace_back(name, std::move(a));
  return *this;
}

AttrPtr BufferGeometry::getAttribute(const std::string& name) const {
  for (const auto& entry : attributes)
    if (entry.first == name) return entry.second;
  return nullptr;
}

BufferGeometry& BufferGeometry::deleteAttribute(const std::string& name) {
  for (auto it = attributes.begin(); it != attributes.end(); ++it) {
    if (it->first == name) {
      attributes.erase(it);
      break;
    }
  }
  return *this;
}

BufferGeometry& BufferGeometry::setIndex(AttrPtr a) {
  index = std::move(a);
  return *this;
}

BufferGeometry& BufferGeometry::setIndex(const std::vector<uint32_t>& indices) {
  // arrayNeedsUint32: 65535 itself is kept free (the index that restarts a strip)
  bool needsUint32 = false;
  for (size_t i = indices.size(); i-- > 0;) {
    if (indices[i] >= 65535) {
      needsUint32 = true;
      break;
    }
  }
  if (needsUint32) {
    index = attr(std::vector<uint32_t>(indices), 1);
  } else {
    index = attr(std::vector<uint16_t>(indices.begin(), indices.end()), 1);
  }
  return *this;
}

BufferGeometry& BufferGeometry::clearIndex() {
  index = nullptr;
  return *this;
}

BufferGeometry& BufferGeometry::addGroup(int start, int count, int materialIndex) {
  groups.push_back(Group{start, count, materialIndex});
  return *this;
}

BufferGeometry& BufferGeometry::clearGroups() {
  groups.clear();
  return *this;
}

BufferGeometry& BufferGeometry::setDrawRange(int start, int count) {
  drawRange.start = start;
  drawRange.count = count;
  return *this;
}

BufferGeometry& BufferGeometry::applyMatrix4(const Matrix4& matrix) {
  const AttrPtr position = getAttribute("position");
  if (position) {
    position->applyMatrix4(matrix);
    position->needsUpdate();
  }
  const AttrPtr normal = getAttribute("normal");
  if (normal) {
    Matrix3 normalMatrix;
    normalMatrix.getNormalMatrix(matrix);
    normal->applyNormalMatrix(normalMatrix);
    normal->needsUpdate();
  }
  const AttrPtr tangent = getAttribute("tangent");
  if (tangent) {
    tangent->transformDirection(matrix);
    tangent->needsUpdate();
  }
  if (boundingBox) computeBoundingBox();
  if (boundingSphere) computeBoundingSphere();
  return *this;
}

BufferGeometry& BufferGeometry::applyQuaternion(const Quaternion& q) {
  Matrix4 m1;
  m1.makeRotationFromQuaternion(q);
  applyMatrix4(m1);
  return *this;
}

BufferGeometry& BufferGeometry::rotateX(double angle) {
  // rotate geometry around world x-axis
  Matrix4 m1;
  m1.makeRotationX(angle);
  applyMatrix4(m1);
  return *this;
}

BufferGeometry& BufferGeometry::rotateY(double angle) {
  // rotate geometry around world y-axis
  Matrix4 m1;
  m1.makeRotationY(angle);
  applyMatrix4(m1);
  return *this;
}

BufferGeometry& BufferGeometry::rotateZ(double angle) {
  // rotate geometry around world z-axis
  Matrix4 m1;
  m1.makeRotationZ(angle);
  applyMatrix4(m1);
  return *this;
}

BufferGeometry& BufferGeometry::translate(double x, double y, double z) {
  // translate geometry
  Matrix4 m1;
  m1.makeTranslation(x, y, z);
  applyMatrix4(m1);
  return *this;
}

BufferGeometry& BufferGeometry::scale(double x, double y, double z) {
  // scale geometry
  Matrix4 m1;
  m1.makeScale(x, y, z);
  applyMatrix4(m1);
  return *this;
}

BufferGeometry& BufferGeometry::center() {
  computeBoundingBox();
  Vector3 offset;
  boundingBox->getCenter(offset).negate();
  translate(offset.x, offset.y, offset.z);
  return *this;
}

BufferGeometry& BufferGeometry::setFromPoints(const std::vector<Vector3>& points) {
  const AttrPtr positionAttribute = getAttribute("position");
  if (!positionAttribute) {
    std::vector<float> position;
    position.reserve(points.size() * 3);
    for (const Vector3& point : points) {
      position.push_back(static_cast<float>(point.x));
      position.push_back(static_cast<float>(point.y));
      position.push_back(static_cast<float>(point.z));
    }
    setAttribute("position", attr(std::move(position), 3));
  } else {
    const size_t l = std::min(points.size(), positionAttribute->count());   // make sure data do not exceed buffer size
    for (size_t i = 0; i < l; i++) {
      const Vector3& point = points[i];
      positionAttribute->setXYZ(i, point.x, point.y, point.z);
    }
    // (more points than the buffer has room for are left out, as in three.js – which warns)
    positionAttribute->needsUpdate();
  }
  return *this;
}

BufferGeometry& BufferGeometry::computeBoundingBox() {
  if (!boundingBox) boundingBox = Box3();
  const AttrPtr position = getAttribute("position");
  if (position) {
    boundingBox->setFromBufferAttribute(*position);
  } else {
    boundingBox->makeEmpty();
  }
  return *this;
}

BufferGeometry& BufferGeometry::computeBoundingSphere() {
  if (!boundingSphere) boundingSphere = Sphere();
  const AttrPtr position = getAttribute("position");
  if (position) {
    // first, find the center of the bounding sphere
    Vector3& center = boundingSphere->center;
    Box3 box;
    box.setFromBufferAttribute(*position);
    box.getCenter(center);
    // second, try to find a boundingSphere with a radius smaller than the
    // boundingSphere of the boundingBox: sqrt(3) smaller in the best case
    double maxRadiusSq = 0;
    Vector3 vector;
    for (size_t i = 0, il = position->count(); i < il; i++) {
      vector.fromBufferAttribute(*position, i);
      maxRadiusSq = jsMax(maxRadiusSq, center.distanceToSquared(vector));
    }
    boundingSphere->radius = std::sqrt(maxRadiusSq);
  }
  return *this;
}

BufferGeometry& BufferGeometry::computeVertexNormals() {
  const AttrPtr positionAttribute = getAttribute("position");
  if (positionAttribute) {
    AttrPtr normalAttribute = getAttribute("normal");
    if (!normalAttribute || normalAttribute->count() != positionAttribute->count()) {
      normalAttribute = attr(std::vector<float>(positionAttribute->count() * 3), 3);
      setAttribute("normal", normalAttribute);
    } else {
      // reset existing normals to zero
      for (size_t i = 0, il = normalAttribute->count(); i < il; i++) normalAttribute->setXYZ(i, 0, 0, 0);
    }
    Vector3 pA, pB, pC;
    Vector3 nA, nB, nC;
    Vector3 cb, ab;
    // indexed elements
    if (index) {
      for (size_t i = 0, il = index->count(); i < il; i += 3) {
        const size_t vA = static_cast<size_t>(index->getX(i + 0));
        const size_t vB = static_cast<size_t>(index->getX(i + 1));
        const size_t vC = static_cast<size_t>(index->getX(i + 2));
        pA.fromBufferAttribute(*positionAttribute, vA);
        pB.fromBufferAttribute(*positionAttribute, vB);
        pC.fromBufferAttribute(*positionAttribute, vC);
        cb.subVectors(pC, pB);
        ab.subVectors(pA, pB);
        cb.cross(ab);
        nA.fromBufferAttribute(*normalAttribute, vA);
        nB.fromBufferAttribute(*normalAttribute, vB);
        nC.fromBufferAttribute(*normalAttribute, vC);
        nA.add(cb);
        nB.add(cb);
        nC.add(cb);
        normalAttribute->setXYZ(vA, nA.x, nA.y, nA.z);
        normalAttribute->setXYZ(vB, nB.x, nB.y, nB.z);
        normalAttribute->setXYZ(vC, nC.x, nC.y, nC.z);
      }
    } else {
      // non-indexed elements (unconnected triangle soup)
      for (size_t i = 0, il = positionAttribute->count(); i < il; i += 3) {
        pA.fromBufferAttribute(*positionAttribute, i + 0);
        pB.fromBufferAttribute(*positionAttribute, i + 1);
        pC.fromBufferAttribute(*positionAttribute, i + 2);
        cb.subVectors(pC, pB);
        ab.subVectors(pA, pB);
        cb.cross(ab);
        normalAttribute->setXYZ(i + 0, cb.x, cb.y, cb.z);
        normalAttribute->setXYZ(i + 1, cb.x, cb.y, cb.z);
        normalAttribute->setXYZ(i + 2, cb.x, cb.y, cb.z);
      }
    }
    normalizeNormals();
    normalAttribute->needsUpdate();
  }
  return *this;
}

BufferGeometry& BufferGeometry::normalizeNormals() {
  const AttrPtr normals = getAttribute("normal");
  if (!normals) return *this;
  Vector3 vector;
  for (size_t i = 0, il = normals->count(); i < il; i++) {
    vector.fromBufferAttribute(*normals, i);
    vector.normalize();
    normals->setXYZ(i, vector.x, vector.y, vector.z);
  }
  return *this;
}

namespace {

/** the attribute's items in the order the indices name them: a plain attribute of the same kind of array */
template <class T>
std::vector<T> picked(const std::vector<T>& array, int itemSize, const BufferAttribute& indices) {
  const size_t n = indices.length();
  std::vector<T> array2(n * static_cast<size_t>(itemSize));
  size_t index2 = 0;
  for (size_t i = 0; i < n; i++) {
    size_t index = static_cast<size_t>(indices.at(i)) * static_cast<size_t>(itemSize);
    for (int j = 0; j < itemSize; j++, index++) array2[index2++] = index < array.size() ? array[index] : T();
  }
  return array2;
}

AttrPtr convertBufferAttribute(const BufferAttribute& attribute, const BufferAttribute& indices) {
  const int itemSize = attribute.itemSize;
  const bool normalized = attribute.normalized;
  switch (attribute.type) {
    case AttrType::F32: return attr(picked(attribute.f32, itemSize, indices), itemSize, normalized);
    case AttrType::U8: return attr(picked(attribute.u8, itemSize, indices), itemSize, normalized);
    case AttrType::I8: return attr(picked(attribute.i8, itemSize, indices), itemSize, normalized);
    case AttrType::U16: return attr(picked(attribute.u16, itemSize, indices), itemSize, normalized);
    case AttrType::I16: return attr(picked(attribute.i16, itemSize, indices), itemSize, normalized);
    case AttrType::U32: return attr(picked(attribute.u32, itemSize, indices), itemSize, normalized);
    case AttrType::I32: return attr(picked(attribute.i32, itemSize, indices), itemSize, normalized);
  }
  return nullptr;
}

}  // namespace

std::shared_ptr<BufferGeometry> BufferGeometry::toNonIndexed() const {
  if (!index) {
    // (three.js warns: the geometry is already non-indexed – and answers the geometry itself)
    if (auto self = std::const_pointer_cast<BufferGeometry>(weak_from_this().lock())) return self;
    return clone();
  }
  auto geometry2 = std::make_shared<BufferGeometry>();
  for (const auto& entry : attributes) {
    geometry2->setAttribute(entry.first, convertBufferAttribute(*entry.second, *index));
  }
  for (const Group& group : groups) geometry2->addGroup(group.start, group.count, group.materialIndex);
  return geometry2;
}

std::shared_ptr<BufferGeometry> BufferGeometry::clone() const {
  auto g = std::make_shared<BufferGeometry>();
  g->copy(*this);
  return g;
}

BufferGeometry& BufferGeometry::copy(const BufferGeometry& source) {
  // reset
  index = nullptr;
  attributes.clear();
  groups.clear();
  boundingBox.reset();
  boundingSphere.reset();
  // name
  name = source.name;
  // index
  if (source.index) setIndex(source.index->clone());
  // attributes
  for (const auto& entry : source.attributes) setAttribute(entry.first, entry.second->clone());
  // groups
  for (const Group& group : source.groups) addGroup(group.start, group.count, group.materialIndex);
  // bounding box
  if (source.boundingBox) boundingBox = source.boundingBox->clone();
  // bounding sphere
  if (source.boundingSphere) boundingSphere = source.boundingSphere->clone();
  // draw range
  drawRange.start = source.drawRange.start;
  drawRange.count = source.drawRange.count;
  // InstancedBufferGeometry
  instanceCount = source.instanceCount;
  return *this;
}

// dispose(): what the geometry holds on the graphics card is handed back now – its vertex array and the
// buffers of its index and attributes (an attribute another geometry shares is uploaded again when drawn)
void BufferGeometry::dispose() {
  gpu::release(gpu::Kind::VertexArray, gpuVao);
  gpuVao = 0;
  gpuLayout = 0;
  const auto release = [](const AttrPtr& a) {
    if (!a) return;
    gpu::release(gpu::Kind::Buffer, a->gpu);
    a->gpu = 0;
    a->gpuVersion = 0;
  };
  release(index);
  for (const auto& entry : attributes) release(entry.second);
}

}  // namespace fs
