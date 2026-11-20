// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Viewer for 360° panoramas (equirectangular pictures).
 *
 * Origin: src/render/panorama.ts (MaurerAnton/driving-simulator @ 578e397,
 * v0.4.0).  The sphere is built by the engine's geometry helper, flipped so
 * it is seen from the inside; the camera maths (yaw/pitch/fov and the
 * off-centre look-at) is the original's.
 */
#include "render/panorama.h"

#include <algorithm>
#include <cmath>

namespace fs {

PanoramaView::PanoramaView() {
  GeometryPtr geo = makeSphereGeometry(5, 96, 48);
  // seen from the inside, not mirrored: flip the winding by negating x
  for (size_t i = 0; i < geo->position->count(); i++)
    geo->position->array[i * 3] = -geo->position->array[i * 3];
  geo->computeVertexNormals();
  mesh_ = new Mesh(geo, MaterialKind::Struct);
  scene_.addRef(mesh_);
  camera_.near = 0.1;
  camera_.far = 20;
}

PanoramaView::~PanoramaView() {
  if (mesh_) scene_.remove(mesh_);   // borrow released: the scene must not delete it
  delete mesh_;
  mesh_ = nullptr;
}

void PanoramaView::show(const std::vector<uint8_t>& jpeg, int width, int height, int maxWidth) {
  (void)jpeg;  // the platform hands in decoded pixels; see setPixels()
  (void)width;
  (void)height;
  (void)maxWidth;
}

void PanoramaView::dispose() {
  hasTexture_ = false;
  pixels_.clear();
  width_ = height_ = 0;
}

void PanoramaView::close() { dispose(); }

void PanoramaView::drag(double dx, double dy, double viewHeight) {
  double perPixel = (fov * DEG) / std::max(1.0, viewHeight);
  yaw -= dx * perPixel;
  pitch = std::max(-85 * DEG, std::min(85 * DEG, pitch + dy * perPixel));
}

void PanoramaView::zoom(double delta) {
  fov = std::max(20.0, std::min(100.0, fov * std::exp(delta * 0.0012)));
}

void PanoramaView::render(WebGLRenderer& renderer, double left, double top, double width,
                          double height) {
  if (!hasTexture_ || width < 2 || height < 2) return;
  (void)renderer;
  // the middle of the picture lies on the -X axis, turning right goes towards -Z
  double cp = std::cos(pitch);
  Vector3 dir(-std::cos(yaw) * cp, std::sin(pitch), -std::sin(yaw) * cp);
  camera_.position.set(0, 0, 0);
  camera_.lookAt(dir);
  camera_.aspect = width / height;
  camera_.fov = fov;
  camera_.updateProjectionMatrix();
  (void)left;
  (void)top;
}

}  // namespace fs
