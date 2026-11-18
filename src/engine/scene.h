// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Scene graph: Object3D with position/quaternion/scale, children, matrix
 * updates and frustum culling helpers — the part of three.js the game uses
 * (Object3D, Group, Scene, Camera, lights).
 *
 * Origin: three.js (core/Object3D.js, objects/Group.js, objects/Mesh.js,
 * cameras/Camera.js, lights/*, materials/*, geometries/*) as used by the
 * TypeScript game; behaviour mirrors the JavaScript originals.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/math.h"

namespace fs {

// ---------------------------------------------------------------------------
// Object3D
// ---------------------------------------------------------------------------

class Object3D {
 public:
  Vector3 position;
  Quaternion quaternion;
  Vector3 scale{1, 1, 1};
  Matrix4 matrix;            // local matrix
  Matrix4 matrixWorld;       // world matrix after updateMatrixWorld
  int renderOrder = 0;
  bool visible = true;
  bool frustumCulled = true;
  std::string name;
  Object3D* parent = nullptr;
  std::vector<std::unique_ptr<Object3D>> children;

  Object3D() = default;
  virtual ~Object3D() = default;

  Object3D* add(std::unique_ptr<Object3D> child) {
    if (child->parent) child->parent->remove(child.get());
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
  }

  void remove(Object3D* child) {
    for (size_t i = 0; i < children.size(); i++) {
      if (children[i].get() == child) {
        children[i]->parent = nullptr;
        children.erase(children.begin() + static_cast<long>(i));
        return;
      }
    }
  }

  /** Rebuild the local matrix from position/quaternion/scale (three.js updateMatrix). */
  void updateMatrix() { matrix.compose(position, quaternion, scale); }

  /** Walk the tree, refreshing local and world matrices (three.js updateMatrixWorld). */
  void updateMatrixWorld(bool force = false) {
    if (matrixAutoUpdate) updateMatrix();
    if (matrixWorldNeedsUpdate || force) {
      if (parent) matrixWorld.multiplyMatrices(parent->matrixWorld, matrix);
      else matrixWorld.copy(matrix);
      matrixWorldNeedsUpdate = false;
      force = true;
    }
    for (auto& c : children) c->updateMatrixWorld(force);
  }

  void traverse(const std::function<void(Object3D&)>& fn) {
    fn(*this);
    for (auto& c : children) c->traverse(fn);
  }

  bool matrixAutoUpdate = true;
  bool matrixWorldNeedsUpdate = false;

  // three.js helpers used by the game
  Vector3& getWorldPosition(Vector3& out) const { return out.setFromMatrixPosition(matrixWorld); }
  Vector3 getWorldDirection() const {
    Vector3 e(0, 0, 1);
    e.applyQuaternion(quaternion);
    return e;
  }
};

// ---------------------------------------------------------------------------
// Group / Scene / Camera
// ---------------------------------------------------------------------------

class Group : public Object3D {};

class Scene : public Object3D {};

class Camera : public Object3D {
 public:
  double aspect = 1;
  double near = 0.1;
  double far = 2000;
  Matrix4 projectionMatrix;
  Matrix4 projectionMatrixInverse;

  void updateProjectionMatrix() {
    // perspective projection from fov/aspect/near/far (three.js PerspectiveCamera)
    double fov = fovDeg * DEG;
    double top = near * std::tan(0.5 * fov) / zoom;
    double height = 2.0 * top;
    double width = aspect * height;
    double left = -0.5 * width;
    projectionMatrix.makePerspective(left, left + width, top, top - height, near, far);
    projectionMatrixInverse.copy(projectionMatrix).invert();
  }

  double fovDeg = 50;
  double zoom = 1;
};

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

class Light : public Object3D {
 public:
  double intensity = 1;
  Color color;
};

class DirectionalLight : public Light {
 public:
  DirectionalLight() = default;
  DirectionalLight(uint32_t hex, double i) { color.setHex(hex); intensity = i; }
  Object3D target;  // where the light points at
};

class HemisphereLight : public Light {
 public:
  HemisphereLight() = default;
  HemisphereLight(uint32_t sky, uint32_t ground, double i) {
    skyColor.setHex(sky);
    groundColor.setHex(ground);
    intensity = i;
  }
  Color skyColor;
  Color groundColor;
};

class AmbientLight : public Light {
 public:
  AmbientLight() = default;
  AmbientLight(uint32_t hex, double i) { color.setHex(hex); intensity = i; }
};

// ---------------------------------------------------------------------------
// Fog (three.js Fog)
// ---------------------------------------------------------------------------

struct Fog {
  Color color;
  double near = 1;
  double far = 1000;
  Fog() = default;
  Fog(uint32_t hex, double n, double f) : near(n), far(f) { color.setHex(hex); }
};

}  // namespace fs
