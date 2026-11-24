// SPDX-License-Identifier: AGPL-3.0-only
// Origin: three.js r187 src/cameras/{Camera,PerspectiveCamera,OrthographicCamera}.js,
//         src/lights/*.js (the constructors), Vector3.project / unproject
#include "engine/engine.h"

namespace fs {

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

namespace {

/** matrixWorldInverse: the inverse of the world matrix, of a camera that is scaled without its scale */
void updateMatrixWorldInverse(Camera& camera) {
  Vector3 position, scale;
  Quaternion quaternion;
  camera.matrixWorld.decompose(position, quaternion, scale);
  if (scale.x == 1 && scale.y == 1 && scale.z == 1) {
    camera.matrixWorldInverse.copy(camera.matrixWorld).invert();
  } else {
    camera.matrixWorldInverse.compose(position, quaternion, scale.set(1, 1, 1)).invert();
  }
}

}  // namespace

void Camera::updateMatrixWorld(bool force) {
  Object3D::updateMatrixWorld(force);
  updateMatrixWorldInverse(*this);
}

void Camera::updateWorldMatrix(bool updateParents, bool updateChildren, bool force) {
  Object3D::updateWorldMatrix(updateParents, updateChildren, force);
  updateMatrixWorldInverse(*this);
}

Vector3& Camera::getWorldDirection(Vector3& target) { return Object3D::getWorldDirection(target).negate(); }

void Camera::updateProjectionMatrix() {
  if (isPerspective) {
    // PerspectiveCamera
    const double near_ = near;
    double top_ = near_ * std::tan(MathUtils::DEG2RAD * 0.5 * fov) / zoom;
    double height = 2 * top_;
    double width = aspect * height;
    double left_ = -0.5 * width;
    if (view.enabled) {
      const double fullWidth = view.fullWidth, fullHeight = view.fullHeight;
      left_ += view.offsetX * width / fullWidth;
      top_ -= view.offsetY * height / fullHeight;
      width *= view.width / fullWidth;
      height *= view.height / fullHeight;
    }
    projectionMatrix.makePerspective(left_, left_ + width, top_, top_ - height, near_, far);
  } else {
    // OrthographicCamera
    const double dx = (right - left) / (2 * zoom);
    const double dy = (top - bottom) / (2 * zoom);
    const double cx = (right + left) / 2;
    const double cy = (top + bottom) / 2;
    double left_ = cx - dx;
    double right_ = cx + dx;
    double top_ = cy + dy;
    double bottom_ = cy - dy;
    if (view.enabled) {
      const double scaleW = (right - left) / view.fullWidth / zoom;
      const double scaleH = (top - bottom) / view.fullHeight / zoom;
      left_ += scaleW * view.offsetX;
      right_ = left_ + scaleW * view.width;
      top_ -= scaleH * view.offsetY;
      bottom_ = top_ - scaleH * view.height;
    }
    projectionMatrix.makeOrthographic(left_, right_, top_, bottom_, near, far);
  }
  projectionMatrixInverse.copy(projectionMatrix).invert();
}

void Camera::setViewOffset(double fullWidth, double fullHeight, double x, double y, double width, double height) {
  if (isPerspective) aspect = fullWidth / fullHeight;
  view.enabled = true;
  view.fullWidth = fullWidth;
  view.fullHeight = fullHeight;
  view.offsetX = x;
  view.offsetY = y;
  view.width = width;
  view.height = height;
  updateProjectionMatrix();
}

void Camera::clearViewOffset() {
  view.enabled = false;
  updateProjectionMatrix();
}

std::shared_ptr<Camera> perspectiveCamera(double fov, double aspect, double near, double far) {
  auto camera = std::make_shared<Camera>();
  camera->isPerspective = true;
  camera->fov = fov;
  camera->zoom = 1;
  camera->near = near;
  camera->far = far;
  camera->aspect = aspect;
  camera->updateProjectionMatrix();
  return camera;
}

std::shared_ptr<Camera> orthographicCamera(double left, double right, double top, double bottom, double near, double far) {
  auto camera = std::make_shared<Camera>();
  camera->isPerspective = false;
  camera->zoom = 1;
  camera->left = left;
  camera->right = right;
  camera->top = top;
  camera->bottom = bottom;
  camera->near = near;
  camera->far = far;
  camera->updateProjectionMatrix();
  return camera;
}

Vector3& project(Vector3& v, const Camera& camera) { return v.applyMatrix4(camera.matrixWorldInverse).applyMatrix4(camera.projectionMatrix); }

Vector3& unproject(Vector3& v, const Camera& camera) { return v.applyMatrix4(camera.projectionMatrixInverse).applyMatrix4(camera.matrixWorld); }

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

Light::Light(LightType t) : Object3D(ObjectType::Light), lightType(t) {}

LightPtr ambientLight(const Color& color, double intensity) {
  auto light = std::make_shared<Light>(LightType::Ambient);
  light->color = color;
  light->intensity = intensity;
  return light;
}

LightPtr hemisphereLight(const Color& sky, const Color& ground, double intensity) {
  auto light = std::make_shared<Light>(LightType::Hemisphere);
  light->color = sky;
  light->intensity = intensity;
  light->position.copy(Vector3(0, 1, 0));   // Object3D.DEFAULT_UP
  light->updateMatrix();
  light->groundColor = ground;
  return light;
}

LightPtr directionalLight(const Color& color, double intensity) {
  auto light = std::make_shared<Light>(LightType::Directional);
  light->color = color;
  light->intensity = intensity;
  light->position.copy(Vector3(0, 1, 0));   // Object3D.DEFAULT_UP
  light->updateMatrix();
  light->target = std::make_shared<Object3D>();
  light->shadow.camera = orthographicCamera(-5, 5, 5, -5, 0.5, 500);
  return light;
}

LightPtr pointLight(const Color& color, double intensity, double distance, double decay) {
  auto light = std::make_shared<Light>(LightType::Point);
  light->color = color;
  light->intensity = intensity;
  light->distance = distance;
  light->decay = decay;
  light->shadow.camera = perspectiveCamera(90, 1, 0.5, 500);
  return light;
}

LightPtr spotLight(const Color& color, double intensity, double distance, double angle, double penumbra, double decay) {
  auto light = std::make_shared<Light>(LightType::Spot);
  light->color = color;
  light->intensity = intensity;
  light->position.copy(Vector3(0, 1, 0));   // Object3D.DEFAULT_UP
  light->updateMatrix();
  light->target = std::make_shared<Object3D>();
  light->distance = distance;
  light->angle = angle;
  light->penumbra = penumbra;
  light->decay = decay;
  light->shadow.camera = perspectiveCamera(50, 1, 0.5, 500);
  return light;
}

}  // namespace fs
