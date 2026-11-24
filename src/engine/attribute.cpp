// SPDX-License-Identifier: AGPL-3.0-only
// Origin: three.js r187 src/core/BufferAttribute.js (InstancedBufferAttribute.js: `divisor`)
#include "engine/engine.h"
#include "engine/gpu.h"

namespace fs {

namespace {

/** MathUtils.getNormalizationScale: what a whole number stands for when the attribute is normalised (0: a float) */
double normalizationScale(AttrType type) {
  switch (type) {
    case AttrType::U32: return 4294967295.0;
    case AttrType::U16: return 65535.0;
    case AttrType::U8: return 255.0;
    case AttrType::I32: return 2147483647.0;
    case AttrType::I16: return 32767.0;
    case AttrType::I8: return 127.0;
    case AttrType::F32: break;
  }
  return 0;
}

}  // namespace

BufferAttribute::~BufferAttribute() { gpu::release(gpu::Kind::Buffer, gpu); }

size_t BufferAttribute::length() const {
  switch (type) {
    case AttrType::F32: return f32.size();
    case AttrType::U8: return u8.size();
    case AttrType::I8: return i8.size();
    case AttrType::U16: return u16.size();
    case AttrType::I16: return i16.size();
    case AttrType::U32: return u32.size();
    case AttrType::I32: return i32.size();
  }
  return 0;
}

const void* BufferAttribute::data() const {
  switch (type) {
    case AttrType::F32: return f32.data();
    case AttrType::U8: return u8.data();
    case AttrType::I8: return i8.data();
    case AttrType::U16: return u16.data();
    case AttrType::I16: return i16.data();
    case AttrType::U32: return u32.data();
    case AttrType::I32: return i32.data();
  }
  return nullptr;
}

size_t BufferAttribute::byteLength() const {
  switch (type) {
    case AttrType::F32: return f32.size() * sizeof(float);
    case AttrType::U8: return u8.size();
    case AttrType::I8: return i8.size();
    case AttrType::U16: return u16.size() * sizeof(uint16_t);
    case AttrType::I16: return i16.size() * sizeof(int16_t);
    case AttrType::U32: return u32.size() * sizeof(uint32_t);
    case AttrType::I32: return i32.size() * sizeof(int32_t);
  }
  return 0;
}

// array[i]: outside the array JavaScript reads `undefined` (NaN in arithmetic) and writes nothing
double BufferAttribute::at(size_t i) const {
  if (i >= length()) return JS_NAN;
  switch (type) {
    case AttrType::F32: return f32[i];
    case AttrType::U8: return u8[i];
    case AttrType::I8: return i8[i];
    case AttrType::U16: return u16[i];
    case AttrType::I16: return i16[i];
    case AttrType::U32: return u32[i];
    case AttrType::I32: return i32[i];
  }
  return JS_NAN;
}

// array[i] = v, as a typed array takes a number: a float rounded to single precision, a whole number wrapped to its bits
void BufferAttribute::put(size_t i, double v) {
  if (i >= length()) return;
  switch (type) {
    case AttrType::F32: f32[i] = static_cast<float>(v); break;
    case AttrType::U8: u8[i] = static_cast<uint8_t>(jsInt32(v)); break;
    case AttrType::I8: i8[i] = static_cast<int8_t>(jsInt32(v)); break;
    case AttrType::U16: u16[i] = static_cast<uint16_t>(jsInt32(v)); break;
    case AttrType::I16: i16[i] = static_cast<int16_t>(jsInt32(v)); break;
    case AttrType::U32: u32[i] = jsUint32(v); break;
    case AttrType::I32: i32[i] = jsInt32(v); break;
  }
}

double BufferAttribute::get(size_t i, int c) const {
  double x = at(i * static_cast<size_t>(itemSize) + static_cast<size_t>(c));
  if (normalized) {
    // _denormalize
    const double scale = normalizationScale(type);
    if (scale != 0) x = jsMax(x / scale, -1.0);
  }
  return x;
}

BufferAttribute& BufferAttribute::set(size_t i, int c, double v) {
  if (normalized) {
    // _normalize
    const double scale = normalizationScale(type);
    if (scale != 0) v = jsRound(v * scale);
  }
  put(i * static_cast<size_t>(itemSize) + static_cast<size_t>(c), v);
  return *this;
}

// clone(): the array copied, with itemSize, normalized, the usage and (InstancedBufferAttribute) meshPerAttribute
std::shared_ptr<BufferAttribute> BufferAttribute::clone() const {
  auto a = std::make_shared<BufferAttribute>();
  a->type = type;
  a->itemSize = itemSize;
  a->normalized = normalized;
  a->dynamic = dynamic;
  a->divisor = divisor;
  a->f32 = f32;
  a->u8 = u8;
  a->i8 = i8;
  a->u16 = u16;
  a->i16 = i16;
  a->u32 = u32;
  a->i32 = i32;
  return a;
}

BufferAttribute& BufferAttribute::applyMatrix3(const Matrix3& m) {
  if (itemSize == 2) {
    Vector2 vector2;
    for (size_t i = 0, l = count(); i < l; i++) {
      vector2.fromBufferAttribute(*this, i);
      vector2.applyMatrix3(m);
      setXY(i, vector2.x, vector2.y);
    }
  } else if (itemSize == 3) {
    Vector3 vector;
    for (size_t i = 0, l = count(); i < l; i++) {
      vector.fromBufferAttribute(*this, i);
      vector.applyMatrix3(m);
      setXYZ(i, vector.x, vector.y, vector.z);
    }
  }
  return *this;
}

BufferAttribute& BufferAttribute::applyMatrix4(const Matrix4& m) {
  Vector3 vector;
  for (size_t i = 0, l = count(); i < l; i++) {
    vector.fromBufferAttribute(*this, i);
    vector.applyMatrix4(m);
    setXYZ(i, vector.x, vector.y, vector.z);
  }
  return *this;
}

BufferAttribute& BufferAttribute::applyNormalMatrix(const Matrix3& m) {
  Vector3 vector;
  for (size_t i = 0, l = count(); i < l; i++) {
    vector.fromBufferAttribute(*this, i);
    vector.applyNormalMatrix(m);
    setXYZ(i, vector.x, vector.y, vector.z);
  }
  return *this;
}

BufferAttribute& BufferAttribute::transformDirection(const Matrix4& m) {
  Vector3 vector;
  for (size_t i = 0, l = count(); i < l; i++) {
    vector.fromBufferAttribute(*this, i);
    vector.transformDirection(m);
    setXYZ(i, vector.x, vector.y, vector.z);
  }
  return *this;
}

BufferAttribute& BufferAttribute::transformNormals(const Matrix4& m) {
  Matrix3 normalMatrix;
  normalMatrix.getNormalMatrix(m);
  return applyNormalMatrix(normalMatrix);
}

}  // namespace fs
