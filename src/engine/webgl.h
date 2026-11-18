// SPDX-License-Identifier: AGPL-3.0-only
/**
 * WebGL2 backend: turns the scene graph into draw calls.
 *
 * The game's materials are the procedural GLSL of engine/materials.h, so the
 * renderer compiles those programs itself instead of relying on three.js'
 * built-in shaders.  Geometry is uploaded as buffers with the same attribute
 * layout the JavaScript game uses (position, normal, uv, colour, amat,
 * astyle, auvd).
 *
 * Origin: three.js (renderers/WebGLRenderer.js) as used by the TypeScript
 * game, reduced to what the game actually calls.
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/geometry.h"
#include "engine/materials.h"
#include "engine/scene.h"

namespace fs {

// ---------------------------------------------------------------------------
// What the backend draws
// ---------------------------------------------------------------------------

/** A drawable: geometry + material kind + the mesh's world matrix. */
struct DrawItem {
  GeometryPtr geometry;
  MaterialKind kind = MaterialKind::Struct;
  Matrix4 world;
  int renderOrder = 0;
  bool visible = true;
  bool frustumCulled = true;
  /** instanced draws: one matrix per instance (three.js InstancedMesh) */
  std::vector<Matrix4> instances;
  /** per-instance colours (three.js InstancedMesh.setColorAt) */
  std::vector<Color> instanceColors;
};

/** The lights a frame needs. */
struct FrameLights {
  Vector3 sunDir;      // world-space, normalised
  Color sunColor;
  double sunIntensity = 1;
  Color skyColor;
  Color groundColor;
  double hemiIntensity = 1;
  Color fogColor;
  double fogNear = 1;
  double fogFar = 1000;
};

/**
 * A minimal WebGL2 renderer: owns the GL context, the program cache and the
 * per-frame draw list.  Under Emscripten the context comes from the canvas;
 * in the native build `init()` reports that no context is available and the
 * renderer becomes a no-op collector (useful for tests).
 */
class WebGLRenderer {
 public:
  WebGLRenderer();
  ~WebGLRenderer();

  /** Bind the canvas and create the context. Returns false without WebGL2. */
  bool init(int canvasWidth, int canvasHeight);
  bool available() const { return available_; }

  void setSize(int width, int height, bool updateStyle);
  void setPixelRatio(double ratio);
  int width() const { return width_; }
  int height() const { return height_; }

  void setRenderTarget(const std::shared_ptr<struct RenderTarget>& target);
  std::shared_ptr<struct RenderTarget> getRenderTarget() const { return target_; }

  /** Walk the scene, collect draw items and (if a context exists) draw them. */
  void render(Scene& scene, Camera& camera);

  /** Draw an explicit list (used by the mirror pass). */
  void renderItems(const std::vector<DrawItem>& items, Camera& camera, const FrameLights& lights);

  /** Collect the current draw list without issuing GL calls (tests). */
  const std::vector<DrawItem>& lastItems() const { return lastItems_; }
  /** Convenience: render with an optional camera (null = lights/geometry only). */
  void render(Scene& scene, Camera* camera);
  const FrameLights& lastLights() const { return lastLights_; }

 private:
  bool available_ = false;
  int width_ = 1, height_ = 1;
  double pixelRatio_ = 1;
  std::shared_ptr<struct RenderTarget> target_;
  std::vector<DrawItem> lastItems_;
  FrameLights lastLights_;

  void collect(Object3D& root, const Matrix4& parentWorld, std::vector<DrawItem>& out);
  void gatherLights(Object3D& root, FrameLights& out);
};

/** An off-screen colour+depth target (three.js WebGLRenderTarget). */
struct RenderTarget {
  int width = 0, height = 0;
  bool depthBuffer = true;
  bool generateMipmaps = false;
  int minFilter = 0;  // 0 = nearest, 1 = linear, 2 = linear mip
  int magFilter = 0;
  RenderTarget() = default;
  RenderTarget(int w, int h) : width(w), height(h) {}
};

}  // namespace fs
