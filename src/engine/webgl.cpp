// SPDX-License-Identifier: AGPL-3.0-only
/**
 * WebGL2 backend: scene traversal and (when a context exists) draw calls.
 *
 * Origin: three.js (renderers/WebGLRenderer.js) as used by the TypeScript
 * game, reduced to what the game actually calls.
 */
#include "engine/webgl.h"

#include <cmath>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#endif

namespace fs {

WebGLRenderer::WebGLRenderer() = default;
WebGLRenderer::~WebGLRenderer() = default;

bool WebGLRenderer::init(int canvasWidth, int canvasHeight) {
  width_ = canvasWidth;
  height_ = canvasHeight;
#ifdef __EMSCRIPTEN__
  EmscriptenWebGLContextAttributes attr;
  emscripten_webgl_init_context_attributes(&attr);
  attr.alpha = EM_FALSE;
  attr.depth = EM_TRUE;
  attr.stencil = EM_FALSE;
  attr.antialias = EM_TRUE;
  attr.majorVersion = 2;
  attr.minorVersion = 0;
  attr.enableExtensionsByDefault = EM_TRUE;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context("#view", &attr);
  if (ctx <= 0) {
    available_ = false;
    return false;
  }
  emscripten_webgl_make_context_current(ctx);
  available_ = true;
  glViewport(0, 0, width_, height_);
  return true;
#else
  // Native build: no WebGL.  The renderer still collects the draw list, which
  // is what the unit tests inspect.
  available_ = false;
  return false;
#endif
}

void WebGLRenderer::setSize(int width, int height, bool updateStyle) {
  (void)updateStyle;
  width_ = width;
  height_ = height;
#ifdef __EMSCRIPTEN__
  if (available_) glViewport(0, 0, width_, height_);
#endif
}

void WebGLRenderer::setPixelRatio(double ratio) { pixelRatio_ = ratio; }

void WebGLRenderer::setRenderTarget(const std::shared_ptr<RenderTarget>& target) { target_ = target; }

// ---------------------------------------------------------------------------
// Scene traversal
// ---------------------------------------------------------------------------

void WebGLRenderer::collect(Object3D& root, const Matrix4& parentWorld, std::vector<DrawItem>& out) {
  if (!root.visible) return;
  Matrix4 world;
  world.multiplyMatrices(parentWorld, root.matrix);

  // Mesh: has a geometry and a material kind (the port stores the kind on the
  // object itself; three.js keeps it in mesh.material)
  auto* mesh = dynamic_cast<Mesh*>(&root);
  if (mesh && mesh->geometry) {
    DrawItem item;
    item.geometry = mesh->geometry;
    item.kind = mesh->kind;
    item.world = world;
    item.renderOrder = root.renderOrder;
    item.frustumCulled = root.frustumCulled;
    item.instances = mesh->instances;
    item.instanceColors = mesh->instanceColors;
    out.push_back(std::move(item));
  }

  for (auto& c : root.children) collect(*c, world, out);
  for (Object3D* b : root.borrowed_)
    if (b) collect(*b, world, out);
}

void WebGLRenderer::gatherLights(Object3D& root, FrameLights& out) {
  if (auto* sun = dynamic_cast<DirectionalLight*>(&root)) {
    // three.js: the light's direction is (target.position - light.position)
    Vector3 dir;
    dir.subVectors(sun->target.position, sun->position).normalize();
    out.sunDir = dir;
    out.sunColor = sun->color;
    out.sunIntensity = sun->intensity;
  } else if (auto* hemi = dynamic_cast<HemisphereLight*>(&root)) {
    out.skyColor = hemi->skyColor;
    out.groundColor = hemi->groundColor;
    out.hemiIntensity = hemi->intensity;
  } else if (auto* amb = dynamic_cast<AmbientLight*>(&root)) {
    // ambient light adds to the hemisphere term
    out.skyColor.r += amb->color.r * amb->intensity;
    out.skyColor.g += amb->color.g * amb->intensity;
    out.skyColor.b += amb->color.b * amb->intensity;
    out.groundColor.r += amb->color.r * amb->intensity;
    out.groundColor.g += amb->color.g * amb->intensity;
    out.groundColor.b += amb->color.b * amb->intensity;
    out.hemiIntensity += amb->intensity;
  }
  if (const Fog* fog = findFogOf(root)) {
    out.fogColor = fog->color;
    out.fogNear = fog->near;
    out.fogFar = fog->far;
  }
  for (auto& c : root.children) gatherLights(*c, out);
  for (Object3D* b : root.borrowed_)
    if (b) gatherLights(*b, out);
}

void WebGLRenderer::render(Scene& scene, Camera& camera) {
  lastItems_.clear();
  lastLights_ = FrameLights();
  gatherLights(scene, lastLights_);
  Matrix4 identity;
  collect(scene, identity, lastItems_);
  renderItems(lastItems_, camera, lastLights_);
}

void WebGLRenderer::render(Scene& scene, Camera* camera) {
  if (camera) render(scene, *camera);
  else {
    lastItems_.clear();
    lastLights_ = FrameLights();
    gatherLights(scene, lastLights_);
    Matrix4 identity;
    collect(scene, identity, lastItems_);
  }
}

void WebGLRenderer::renderItems(const std::vector<DrawItem>& items, Camera& camera,
                                const FrameLights& lights) {
  if (!available_) return;
#ifdef __EMSCRIPTEN__
  (void)camera;
  (void)lights;
  (void)items;
  // The program/buffer plumbing is added with the first visual increment that
  // needs it; the traversal above is what the game logic depends on today.
#else
  (void)camera;
  (void)lights;
  (void)items;
#endif
}

}  // namespace fs
