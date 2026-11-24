// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/mirrorzoom.ts @ v1.7.7
#include "render/mirrorzoom.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "engine/geometries.h"

namespace fs {

namespace {
/** how far up and down the picture reaches at least (rad, the whole angle) */
const double MIN_ANGLE = (14 * 3.14159265358979323846) / 180;
const double MIN_TAN = std::tan(MIN_ANGLE / 2);
/** the largest picture rendered (px): enough for half a big screen, little enough for a phone's graphics chip */
const double MAX_W = 1600;
const double MAX_H = 900;
}  // namespace

MirrorZoom::MirrorZoom() {
  target_ = make<RenderTarget>(4, 4, true);
  target_->texture->generateMipmaps = false;
  target_->texture->minFilter = Filter::Linear;
  target_->texture->magFilter = Filter::Linear;
  GeometryPtr g = planeGeometry(2, 2);
  // a mirror shows the camera's right on the viewer's left: u runs right → left
  BufferAttribute& uv = *g->getAttribute("uv");
  for (size_t i = 0; i < uv.count(); i++) uv.setX(i, 1 - uv.getX(i));
  MaterialParams p;
  p.map = target_->texture;
  p.depthTest = false;
  p.depthWrite = false;
  p.fog = false;
  quad_ = make<Mesh>(g, basic(p));
  quad_->frustumCulled = false;
  screen_->add(quad_);
}

bool MirrorZoom::draw(Renderer& renderer, Scene& scene, Mirror& mirror, const Matrix4& frame, const Vector3& eye, double far, const Rect& rect,
                      const Options& opts) {
  if (rect.w < 8 || rect.h < 8) return false;
  if (!mirror.aim(frame, eye, far, 1, rect.w / rect.h, opts.minTan.value_or(MIN_TAN))) return false;
  // as sharp as the panel is on the screen, within limits
  const double pr = renderer.getPixelRatio();
  const double k = std::min({1.0, MAX_W / (rect.w * pr), MAX_H / (rect.h * pr)});
  const int w = static_cast<int>(std::max(16.0, jsRound(rect.w * pr * k)));
  const int h = static_cast<int>(std::max(16.0, jsRound(rect.h * pr * k)));
  const bool resized = target_->width != w || target_->height != h;
  if (resized) target_->setSize(w, h);
  if (opts.refresh || resized) picture(renderer, scene, mirror);
  lay(renderer, rect);
  return true;
}

void MirrorZoom::picture(Renderer& renderer, Scene& scene, Mirror& mirror) {
  // the picture: the mirror's own glass is not in it (as when the glass itself is drawn)
  const RenderTargetPtr prevTarget = renderer.getRenderTarget();
  const bool prevVisible = mirror.mesh ? mirror.mesh->visible : false;
  const bool shadows = renderer.shadowMap.autoUpdate;
  if (mirror.mesh) mirror.mesh->visible = false;
  renderer.shadowMap.autoUpdate = false;
  renderer.setRenderTarget(target_);
  renderer.render(scene, *mirror.camera);
  renderer.setRenderTarget(prevTarget);
  renderer.shadowMap.autoUpdate = shadows;
  if (mirror.mesh) mirror.mesh->visible = prevVisible;
}

void MirrorZoom::lay(Renderer& renderer, const Rect& rect) {
  renderer.getSize(size_);
  const bool autoClear = renderer.autoClear;
  renderer.autoClear = false;
  const int x = static_cast<int>(rect.x), y = static_cast<int>(size_.y - rect.y - rect.h), w = static_cast<int>(rect.w), h = static_cast<int>(rect.h);
  renderer.setViewport(x, y, w, h);
  renderer.setScissor(x, y, w, h);
  renderer.setScissorTest(true);
  renderer.render(*screen_, *flat_);
  renderer.setScissorTest(false);
  renderer.setViewport(0, 0, static_cast<int>(size_.x), static_cast<int>(size_.y));
  renderer.setScissor(0, 0, static_cast<int>(size_.x), static_cast<int>(size_.y));
  renderer.autoClear = autoClear;
}

void MirrorZoom::dispose() {
  target_->dispose();
  quad_->geometry->dispose();
}

}  // namespace fs
