// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The renderer: draws a Scene as seen by a Camera with WebGL 2 (in the browser) or
 * OpenGL ES 3 (natively) – what THREE.WebGLRenderer does for the game, written for
 * this game:
 *
 *  - every array is uploaded once and again only when its version changed; a
 *    geometry has one vertex array object;
 *  - objects outside the picture are left out by their bounding spheres, before
 *    anything is sent to the graphics card;
 *  - draws are sorted so that the program and the material change as rarely as
 *    possible; what a program needs once a frame (camera, lights, fog) is set once;
 *  - one sun's shadow map, drawn with a small shader of its own;
 *  - the shaders are three.js's (engine/shaders.h), so the picture is the original's.
 *
 * The host makes the context (platform/glhost.h) before init() is called.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"

namespace fs {

class Renderer {
 public:
  Renderer();
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  /** take the host's context into use; false: there is none (nothing is drawn then) */
  bool init();
  bool available() const { return available_; }

  /** the size of the picture in CSS pixels, and how many real pixels one of them is */
  void setSize(int width, int height);
  void setPixelRatio(double ratio);
  double getPixelRatio() const { return pixelRatio_; }
  /** the size of the drawing buffer in real pixels */
  int bufferWidth() const;
  int bufferHeight() const;

  /** draw the scene (to the screen, or to the target set) */
  void render(Scene& scene, Camera& camera);
  /** draw to this target from now on (null: the screen) */
  void setRenderTarget(const RenderTargetPtr& target);
  const RenderTargetPtr& getRenderTarget() const { return target_; }
  /** the part of the target that is drawn to (real pixels; unset: all of it) */
  void setViewport(int x, int y, int width, int height);
  void resetViewport();
  void setScissor(int x, int y, int width, int height);
  void setScissorTest(bool on);
  /** render() clears colour and depth first */
  bool autoClear = true;
  void clear(bool color = true, bool depth = true);
  /** the colour cleared to where the scene has no background */
  void setClearColor(const Color& color, double alpha = 1);

  /** `renderer.shadowMap` */
  struct ShadowMap {
    bool enabled = false;
  } shadowMap;
  /** `renderer.toneMapping = THREE.ACESFilmicToneMapping` (false: none) and its exposure */
  bool toneMapping = false;
  double toneMappingExposure = 1;

  /** `renderer.info.render`: what the last frame took (every render() of it, the shadow map included) */
  struct Info {
    int calls = 0;
    int triangles = 0;
    int points = 0;
    int lines = 0;
    int programs = 0;
    int geometries = 0;
    int textures = 0;
    /** objects that were left out because they are outside the picture */
    int culled = 0;
    bool autoReset = true;
    void reset() { calls = triangles = points = lines = culled = 0; }
  } info;

  /** `renderer.capabilities.getMaxAnisotropy()` */
  int getMaxAnisotropy();
  /** make the programs the scene needs now, so that the first frame does not stall (`renderer.compile`) */
  void compile(Scene& scene, Camera& camera);
  /** upload a texture now (`renderer.initTexture`) */
  void initTexture(Texture& texture);
  /** the pixels of the current target (RGBA, the bottom row first) */
  void readPixels(int x, int y, int width, int height, uint8_t* out);
  /** copy the picture drawn so far into a texture (`copyFramebufferToTexture`) */
  void copyFramebufferToTexture(Texture& texture, int x = 0, int y = 0);

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
  bool available_ = false;
  int width_ = 1, height_ = 1;
  double pixelRatio_ = 1;
  RenderTargetPtr target_;
};

}  // namespace fs
