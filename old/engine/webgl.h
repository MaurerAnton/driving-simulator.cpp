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

#include <cstdint>
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
  /** three.js material.opacity (1 = opaque; below 1 the mesh blends) */
  double opacity = 1;
  /** three.js material.depthWrite (the glass does not write depth) */
  bool depthWrite = true;
  /**
   * three.js geometry groups: the index range [0, groupSplit) draws with
   * `kind`, the rest with the sign back program (the sign plates' back
   * face).  -1 = the whole index with `kind`.
   */
  int groupSplit = -1;
  /** the texture the program samples as `map` ("" = none) */
  std::string texture;
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

  /** Collect the current draw list without issuing GL calls (tests). */
  const std::vector<DrawItem>& lastItems() const { return lastItems_; }
  /** Convenience: render with an optional camera (null = lights/geometry only). */
  void render(Scene& scene, Camera* camera);
  const FrameLights& lastLights() const { return lastLights_; }

  /** Draw an explicit list: the traversal above plus the GL calls (mirror pass). */
  void renderItems(const std::vector<DrawItem>& items, Camera& camera, const FrameLights& lights);

  /**
   * Upload an RGBA image to a texture the programs can sample as `map`
   * (the sign atlas, the ground-cover rasters).  `key` names the texture;
   * the same key uploads again when the pixels change (three.js
   * texture.needsUpdate).  Under Emscripten this creates the GL texture;
   * in the native build it is a no-op.
   */
  void uploadTexture(const std::string& key, const uint8_t* rgba, int width, int height,
                     bool mipmaps = true);

  /** Bind one uploaded texture to texture unit 0 (the shaders' `map`). */
  void bindTexture(const std::string& key);

  /**
   * The sky dome's own colours (three.js sets them on the dome's
   * ShaderMaterial every time the sun moves): zenith, horizon, sun colour.
   */
  void setSkyUniforms(const Color& zenith, const Color& horizon, const Color& sunColor);

  /** The maximum anisotropy the context offers (three.js capabilities). */
  double maxAnisotropy() const { return 1; }

  /** Draw-call statistics (three.js renderer.info.render). */
  struct Info {
    int calls = 0;
    int triangles = 0;
  };
  Info info;

 private:
  bool available_ = false;
  int width_ = 1, height_ = 1;
  double pixelRatio_ = 1;
  std::shared_ptr<struct RenderTarget> target_;
  std::vector<DrawItem> lastItems_;
  FrameLights lastLights_;
  /** where the camera was for the last collect (the LOD level picks) */
  Vector3 lastCameraPos_;
  bool lastCameraPosValid_ = false;
  /** the sky dome's ShaderMaterial uniforms (Sky::setSun keeps them) */
  Color skyZenith_{0.24, 0.45, 0.78};
  Color skyHorizon_{0.77, 0.83, 0.90};
  Color skySunColor_{1.0, 0.95, 0.82};

  void collect(Object3D& root, const Matrix4& parentWorld, std::vector<DrawItem>& out);
  void gatherLights(Object3D& root, FrameLights& out);

#ifdef __EMSCRIPTEN__
  /** One compiled program (three.js WebGLProgram), keyed by the cache key. */
  struct Program {
    unsigned program = 0;
    std::map<std::string, int> uniforms;    // name → location (-1 = not there)
    std::map<std::string, int> attribs;     // name → location (-1 = not there)
    int loc(const std::string& name);
    int attr(const std::string& name);
  };
  /** One geometry's GPU buffers (three.js WebGLAttributes: one per attribute). */
  struct GpuBuffer {
    std::map<const std::vector<double>*, unsigned> buffers;  // array → vbo
    unsigned ibo = 0;
    unsigned instanceMatrixBuffer = 0;
    unsigned colorBuffer = 0;  // the per-instance colours (InstancedMesh.setColorAt)
    long long indexCount = -1;
    long long vertexCount = -1;
    long long instanceCount = -1;
  };

  std::map<std::string, Program> programs_;
  std::map<unsigned long long, GpuBuffer> gpu_;
  std::map<std::string, unsigned> textures_;

  Program& programFor(MaterialKind kind, bool signBack, bool useMap, bool useColor, bool instColor);
  unsigned bufferFor(const std::vector<double>& data, GpuBuffer& b);
  void uploadInstances(const DrawItem& item, GpuBuffer& b);
  void drawItem(const DrawItem& item, Program& p, const Matrix4& view, const Matrix4& projection,
                const Vector3& cameraPos, const FrameLights& lights, MaterialKind kind,
                int firstIndex, int indexCount);
  unsigned textureFor(const std::string& key);
  /** The off-screen target's framebuffer (colour + depth), created on demand. */
  unsigned framebufferFor(struct RenderTarget& target);
  /** normalMatrix = inverse-transpose of the modelView's upper 3x3 (three.js getNormalMatrix). */
  void normalMatrix3(const Matrix4& modelView, float out[9]);
#endif
};

/** An off-screen colour+depth target (three.js WebGLRenderTarget). */
struct RenderTarget {
  int width = 0, height = 0;
  bool depthBuffer = true;
  bool generateMipmaps = false;
  int minFilter = 0;  // 0 = nearest, 1 = linear, 2 = linear mip
  int magFilter = 0;
  /** GL objects, created on first use (0 = none yet) */
  unsigned fbo = 0, colorTex = 0, depthRbo = 0;
  RenderTarget() = default;
  RenderTarget(int w, int h) : width(w), height(h) {}
};

}  // namespace fs
