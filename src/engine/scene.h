// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Scene graph: Object3D with position/quaternion/scale, children, matrix
 * updates and frustum culling helpers — the part of three.js the game uses
 * (Object3D, Group, Scene, Camera, lights).
 *
 * Origin: three.js (core/Object3D.js, objects/Group.js, objects/Mesh.js,
 * cameras/Camera.js, lights, materials, geometries) as used by the
 * TypeScript game; behaviour mirrors the JavaScript originals.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/geometry.h"
#include "engine/materials.h"
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
  /** children added with addRef(): owned elsewhere, walked like the rest */
  std::vector<Object3D*> borrowed_;

  Object3D() = default;
  virtual ~Object3D() = default;

  Object3D* add(std::unique_ptr<Object3D> child) {
    if (child->parent) child->parent->remove(child.get());
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
  }

  /**
   * Add a node the caller owns (this node must not delete it).  Used for the
   * sky dome and the lights, which the game keeps alive itself.
   */
  Object3D* addRef(Object3D* child) {
    if (!child) return nullptr;
    if (child->parent) child->parent->remove(child);
    child->parent = this;
    borrowed_.push_back(child);
    return child;
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
// Fog (three.js Fog)
// ---------------------------------------------------------------------------

struct Fog {
  Color color;
  double near = 1;
  double far = 1000;
  Fog() = default;
  Fog(uint32_t hex, double n, double f) : near(n), far(f) { color.setHex(hex); }
};

// ---------------------------------------------------------------------------
// Mesh
// ---------------------------------------------------------------------------

/** A drawable node: geometry plus the material kind the backend compiles. */
class Mesh : public Object3D {
 public:
  Mesh() = default;
  Mesh(GeometryPtr g, MaterialKind k) : geometry(std::move(g)), kind(k) {}
  GeometryPtr geometry;
  MaterialKind kind = MaterialKind::Struct;
  /** instanced draws (three.js InstancedMesh): one matrix per instance */
  std::vector<Matrix4> instances;
  std::vector<Color> instanceColors;
  bool castShadow = false;
  bool receiveShadow = false;
};

// ---------------------------------------------------------------------------
// LOD
// ---------------------------------------------------------------------------

/**
 * Level of detail: one node per distance band, the renderer picks the level
 * nearest the camera (three.js LOD).
 */
class LOD : public Object3D {
 public:
  struct Level {
    Object3D* object = nullptr;
    double distance = 0;
  };
  std::vector<Level> levels;

  void addLevel(Object3D* object, double distance) { levels.push_back({object, distance}); }
  /** The level to draw from `distance` metres away (nullptr when empty). */
  Object3D* pick(double distance) const {
    if (levels.empty()) return nullptr;
    const Level* best = &levels[0];
    for (const auto& l : levels)
      if (l.distance < distance) best = &l;
    return best->object;
  }
};

// ---------------------------------------------------------------------------
// Group / Scene / Camera
// ---------------------------------------------------------------------------

class Group : public Object3D {};

class Scene : public Object3D {
 public:
  /** three.js scene.fog — set by the sky in App's constructor. */
  std::shared_ptr<Fog> fog;
};

/** Nearest fog on this node or an ancestor (three.js resolves fog per object). */
inline const Fog* findFogOf(const Object3D& o) {
  const Object3D* n = &o;
  while (n) {
    if (auto* s = dynamic_cast<const Scene*>(n))
      if (s->fog) return s->fog.get();
    n = n->parent;
  }
  return nullptr;
}

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

}  // namespace fs
