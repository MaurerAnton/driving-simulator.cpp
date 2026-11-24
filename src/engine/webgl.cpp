// SPDX-License-Identifier: AGPL-3.0-only
/**
 * WebGL2 backend: scene traversal and (when a context exists) draw calls.
 *
 * Origin: three.js (renderers/WebGLRenderer.js) as used by the TypeScript
 * game, reduced to what the game actually calls.
 */
#include "engine/webgl.h"

#include <cmath>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#endif

namespace fs {

const Vector3 Object3D::DEFAULT_UP(0, 1, 0);

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

#ifndef __EMSCRIPTEN__
void WebGLRenderer::uploadTexture(const std::string&, const uint8_t*, int, int, bool) {
  // no context in the native build: the collector never samples textures
}

void WebGLRenderer::bindTexture(const std::string&) {}
#endif

void WebGLRenderer::setSkyUniforms(const Color& zenith, const Color& horizon, const Color& sunColor) {
  skyZenith_ = zenith;
  skyHorizon_ = horizon;
  skySunColor_ = sunColor;
}

void WebGLRenderer::setRenderTarget(const std::shared_ptr<RenderTarget>& target) { target_ = target; }

// ---------------------------------------------------------------------------
// Scene traversal
// ---------------------------------------------------------------------------

void WebGLRenderer::collect(Object3D& root, const Matrix4& parentWorld, std::vector<DrawItem>& out) {
  if (!root.visible) return;
  Matrix4 world;
  world.multiplyMatrices(parentWorld, root.matrix);

  // LOD: three.js picks the level nearest the camera (the renderer's
  // projectObject calls lod.update(camera) before walking the children)
  if (auto* lod = dynamic_cast<LOD*>(&root)) {
    if (lod->levels.size() > 1 && lastCameraPosValid_) {
      Vector3 d;
      d.subVectors(lastCameraPos_, Vector3(world.e[12], world.e[13], world.e[14]));
      double distance = d.length();
      for (size_t i = 1; i < lod->levels.size(); i++) {
        if (lod->levels[i].object) lod->levels[i].object->visible = distance >= lod->levels[i].distance;
      }
      if (lod->levels[0].object) lod->levels[0].object->visible = true;
    }
  }

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
    item.opacity = mesh->opacity;
    item.depthWrite = mesh->depthWrite;
    item.groupSplit = mesh->groupSplit;
    item.texture = mesh->texture;
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
  lastCameraPos_.setFromMatrixPosition(camera.matrixWorld);
  lastCameraPosValid_ = true;
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
    lastCameraPosValid_ = false;
    Matrix4 identity;
    collect(scene, identity, lastItems_);
  }
}

void WebGLRenderer::renderItems(const std::vector<DrawItem>& items, Camera& camera,
                                const FrameLights& lights) {
  info.calls = 0;
  info.triangles = 0;
  for (const auto& item : items) {
    if (!item.visible || !item.geometry) continue;
    info.calls++;
    size_t verts = item.geometry->position ? item.geometry->position->count() : 0;
    info.triangles += static_cast<int>(verts / 3);
  }
  if (!available_) return;
#ifdef __EMSCRIPTEN__
  // three.js render(): the camera's world matrix (and its inverse, the
  // view matrix) are refreshed here, before anything is drawn
  camera.updateMatrixWorld();
  camera.matrixWorldInverse.copy(camera.matrixWorld);
  camera.matrixWorldInverse.invert();

  if (target_) {
    // an off-screen pass (the mirror glasses): its own viewport
    glBindFramebuffer(GL_FRAMEBUFFER, framebufferFor(*target_));
    glViewport(0, 0, target_->width, target_->height);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, static_cast<GLsizei>(width_ * pixelRatio_),
               static_cast<GLsizei>(height_ * pixelRatio_));
  }

  glClearColor(static_cast<GLfloat>(lights.fogColor.r), static_cast<GLfloat>(lights.fogColor.g),
               static_cast<GLfloat>(lights.fogColor.b), 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  // three.js sorts the draw list: by renderOrder, then front-to-back for the
  // opaque (the sky's -1000 first, the marks' 1, the glass' 5 last)
  std::vector<const DrawItem*> order;
  order.reserve(items.size());
  for (const auto& item : items)
    if (item.visible && item.geometry) order.push_back(&item);
  std::stable_sort(order.begin(), order.end(),
                   [](const DrawItem* a, const DrawItem* b) { return a->renderOrder < b->renderOrder; });

  Matrix4 view = camera.matrixWorldInverse;
  Matrix4 projection = camera.projectionMatrix;
  Vector3 cameraPos;
  cameraPos.setFromMatrixPosition(camera.matrixWorld);

  for (const DrawItem* item : order) {
    const DrawItem& it = *item;
    const GeometryPtr& g = it.geometry;
    if (!g->position) continue;

    // three.js geometry groups: the sign plates draw their front face with
    // the atlas program and their back with the bare-metal one
    const bool useMap = !it.texture.empty();
    const bool useColor = g->color != nullptr;
    // the program needs USE_INSTANCING_COLOR only where there are colours
    // (three.js: the define follows the instanceColor attribute being set)
    const bool instColor = !it.instances.empty() && !it.instanceColors.empty();
    if (it.groupSplit >= 0 && it.groupSplit < static_cast<long>(g->index ? g->index->count() : 0)) {
      Program& front = programFor(it.kind, false, true, useColor, instColor);
      drawItem(it, front, view, projection, cameraPos, lights, it.kind, 0, it.groupSplit);
      Program& back = programFor(MaterialKind::SignBack, true, true, false, false);
      drawItem(it, back, view, projection, cameraPos, lights, MaterialKind::SignBack,
               it.groupSplit, static_cast<int>(g->index->count()) - it.groupSplit);
    } else {
      int count = g->index ? static_cast<int>(g->index->count())
                           : static_cast<int>(g->position->count());
      Program& p = programFor(it.kind, false, useMap, useColor, instColor);
      drawItem(it, p, view, projection, cameraPos, lights, it.kind, 0, count);
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
#else
  (void)camera;
  (void)lights;
  (void)items;
#endif
}

#ifdef __EMSCRIPTEN__
// ---------------------------------------------------------------------------
// Programs, buffers, textures: the GL side (three.js WebGLProgram /
// WebGLAttributes / WebGLTextures, reduced to what the game uploads)
// ---------------------------------------------------------------------------

int WebGLRenderer::Program::loc(const std::string& name) {
  auto it = uniforms.find(name);
  if (it != uniforms.end()) return it->second;
  int l = static_cast<int>(glGetUniformLocation(program, name.c_str()));
  uniforms[name] = l;
  return l;
}

int WebGLRenderer::Program::attr(const std::string& name) {
  auto it = attribs.find(name);
  if (it != attribs.end()) return it->second;
  int l = static_cast<int>(glGetAttribLocation(program, name.c_str()));
  attribs[name] = l;
  return l;
}

namespace {

/** Compile one shader stage; logs the source when it fails. */
GLuint compileShader(GLenum type, const std::string& src) {
  GLuint sh = glCreateShader(type);
  const char* s = src.c_str();
  glShaderSource(sh, 1, &s, nullptr);
  glCompileShader(sh);
  GLint ok = GL_FALSE;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
    std::string log(static_cast<size_t>(len), '\0');
    glGetShaderInfoLog(sh, len, nullptr, log.data());
    std::fprintf(stderr, "fahrsim: shader compile failed (%s):\n%s\n",
                 type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.c_str());
    std::fprintf(stderr, "%s\n", src.c_str());
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

/** Doubles → floats: the GPU keeps what the game computed in double. */
std::vector<float> toFloat(const std::vector<double>& in) {
  std::vector<float> out(in.size());
  for (size_t i = 0; i < in.size(); i++) out[i] = static_cast<float>(in[i]);
  return out;
}

/** One instance matrix as 4 column vectors of vec4 (three.js instanceMatrix). */
std::vector<float> instanceMatrixFloats(const std::vector<Matrix4>& m) {
  std::vector<float> out;
  out.reserve(m.size() * 16);
  for (const Matrix4& x : m)
    for (int i = 0; i < 16; i++) out.push_back(static_cast<float>(x.e[i]));
  return out;
}

/** One instance colour as vec3. */
std::vector<float> instanceColorFloats(const std::vector<Color>& c) {
  std::vector<float> out;
  out.reserve(c.size() * 3);
  for (const Color& x : c) {
    out.push_back(static_cast<float>(x.r));
    out.push_back(static_cast<float>(x.g));
    out.push_back(static_cast<float>(x.b));
  }
  return out;
}

/** The uniform names of the assembled shaders (set when the program links). */
const char* const kUniformNames[] = {
    "modelMatrix",   "viewMatrix",         "projectionMatrix", "modelViewMatrix",
    "normalMatrix",  "cameraPosition",     "diffuse",          "ambient",
    "emissive",      "fogColor",           "fogNear",          "fogFar",
    "uSunDir",       "uSunCol",            "uSunInt",          "uSkyCol",
    "uGroundCol",    "uHemiInt",           "uZenith",          "uHorizon",
    "uSunColor",     "map",                "uTexel",           "instanceColor"};
const char* const kAttribNames[] = {
    "position", "normal", "uv", "color", "amat", "astyle", "auvd", "instanceMatrix",
    "instanceColor"};

}  // namespace

WebGLRenderer::Program& WebGLRenderer::programFor(MaterialKind kind, bool signBack, bool useMap,
                                                   bool useColor, bool instColor) {
  // three.js's program cache key carries every define: the same material kind
  // compiles to different programs with and without a texture, colours or
  // per-instance colours (WebGLPrograms.getProgramCacheKey)
  std::string key = programCacheKey(kind, signBack);
  if (useMap) key += "+map";
  if (useColor) key += "+color";
  if (instColor) key += "+inst";
  auto it = programs_.find(key);
  if (it != programs_.end()) return it->second;

  Program p;
  std::string vs = vertexShaderFor(kind);
  std::string fs = fragmentShaderFor(kind);
  std::string defines;
  if (useMap) defines += "#define USE_MAP\n";
  if (useColor) defines += "#define USE_COLOR\n";
  if (instColor) defines += "#define USE_INSTANCING_COLOR\n";
  vs = defines + vs;
  fs = defines + fs;
  GLuint v = compileShader(GL_VERTEX_SHADER, vs);
  GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
  p.program = glCreateProgram();
  glAttachShader(p.program, v);
  glAttachShader(p.program, f);
  glLinkProgram(p.program);
  GLint linked = GL_FALSE;
  glGetProgramiv(p.program, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint len = 0;
    glGetProgramiv(p.program, GL_INFO_LOG_LENGTH, &len);
    std::string log(static_cast<size_t>(len), '\0');
    glGetProgramInfoLog(p.program, len, nullptr, log.data());
    std::fprintf(stderr, "fahrsim: program link failed (%s):\n%s\n", key.c_str(), log.c_str());
  }
  glDeleteShader(v);
  glDeleteShader(f);
  // resolve the locations once: absent uniforms/attributes stay -1
  for (const char* n : kUniformNames) p.loc(n);
  for (const char* n : kAttribNames) p.attr(n);
  auto res = programs_.emplace(key, std::move(p));
  return res.first->second;
}

unsigned WebGLRenderer::bufferFor(const std::vector<double>& data, GpuBuffer& b) {
  auto it = b.buffers.find(&data);
  if (it != b.buffers.end()) return it->second;
  GLuint buf = 0;
  glGenBuffers(1, &buf);
  glBindBuffer(GL_ARRAY_BUFFER, buf);
  std::vector<float> f = toFloat(data);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(f.size() * sizeof(float)), f.data(),
               GL_STATIC_DRAW);
  b.buffers[&data] = buf;
  return buf;
}

void WebGLRenderer::uploadInstances(const DrawItem& item, GpuBuffer& b) {
  const std::vector<Matrix4>& m = item.instances;
  if (m.empty()) return;
  // three.js InstancedMesh: the matrices change almost every frame (the
  // traffic, the props thinning out), so they are re-uploaded on a count change
  std::vector<float> fm = instanceMatrixFloats(m);
  if (!b.instanceMatrixBuffer) {
    glGenBuffers(1, &b.instanceMatrixBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, b.instanceMatrixBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(fm.size() * sizeof(float)), fm.data(),
                 GL_DYNAMIC_DRAW);
    b.instanceCount = static_cast<long long>(m.size());
  } else if (b.instanceCount != static_cast<long long>(m.size())) {
    glBindBuffer(GL_ARRAY_BUFFER, b.instanceMatrixBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(fm.size() * sizeof(float)), fm.data(),
                 GL_DYNAMIC_DRAW);
    b.instanceCount = static_cast<long long>(m.size());
  } else {
    glBindBuffer(GL_ARRAY_BUFFER, b.instanceMatrixBuffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(fm.size() * sizeof(float)),
                    fm.data());
  }
  if (!item.instanceColors.empty()) {
    // per-instance colours: a second buffer, uploaded with the matrices
    std::vector<float> fc = instanceColorFloats(item.instanceColors);
    if (!b.colorBuffer) {
      GLuint buf = 0;
      glGenBuffers(1, &buf);
      glBindBuffer(GL_ARRAY_BUFFER, buf);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(fc.size() * sizeof(float)), fc.data(),
                   GL_DYNAMIC_DRAW);
      b.colorBuffer = buf;
    } else {
      glBindBuffer(GL_ARRAY_BUFFER, b.colorBuffer);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(fc.size() * sizeof(float)), fc.data(),
                   GL_DYNAMIC_DRAW);
    }
  }
}

void WebGLRenderer::normalMatrix3(const Matrix4& modelView, float out[9]) {
  // three.js Matrix3.getNormalMatrix: setFromMatrix4 (upper 3x3), invert, transpose
  double m[9] = {modelView.e[0], modelView.e[4], modelView.e[8], modelView.e[1], modelView.e[5],
                 modelView.e[9], modelView.e[2], modelView.e[6], modelView.e[10]};
  double n[9];
  double a00 = m[0], a01 = m[1], a02 = m[2], a10 = m[3], a11 = m[4], a12 = m[5], a20 = m[6],
         a21 = m[7], a22 = m[8];
  double b01 = a22 * a11 - a12 * a21, b11 = -a22 * a10 + a12 * a20, b21 = a21 * a10 - a11 * a20;
  double det = a00 * b01 + a01 * b11 + a02 * b21;
  if (det == 0) {
    for (int i = 0; i < 9; i++) out[i] = (i % 4 == 0) ? 1.0f : 0.0f;
    return;
  }
  det = 1.0 / det;
  n[0] = b01 * det;
  n[1] = (-a22 * a01 + a02 * a21) * det;
  n[2] = (a12 * a01 - a02 * a11) * det;
  n[3] = b11 * det;
  n[4] = (a22 * a00 - a02 * a20) * det;
  n[5] = (-a12 * a00 + a02 * a10) * det;
  n[6] = b21 * det;
  n[7] = (-a21 * a00 + a01 * a20) * det;
  n[8] = (a11 * a00 - a01 * a10) * det;
  // transpose (the inverse-transposed normal matrix)
  out[0] = static_cast<float>(n[0]);
  out[1] = static_cast<float>(n[3]);
  out[2] = static_cast<float>(n[6]);
  out[3] = static_cast<float>(n[1]);
  out[4] = static_cast<float>(n[4]);
  out[5] = static_cast<float>(n[7]);
  out[6] = static_cast<float>(n[2]);
  out[7] = static_cast<float>(n[5]);
  out[8] = static_cast<float>(n[8]);
}

void WebGLRenderer::drawItem(const DrawItem& item, Program& p, const Matrix4& view,
                             const Matrix4& projection, const Vector3& cameraPos,
                             const FrameLights& lights, MaterialKind kind, int firstIndex,
                             int indexCount) {
  const GeometryPtr& g = item.geometry;
  if (!g || !g->position || indexCount <= 0) return;

  GpuBuffer& b = gpu_[g->id];
  const bool useMap = !item.texture.empty();
  const bool useColor = g->color != nullptr;

  // the geometry's arrays (three.js WebGLAttributes.update: one VBO per attribute)
  unsigned positionBuf = bufferFor(g->position->array, b);
  unsigned normalBuf = g->normal ? bufferFor(g->normal->array, b) : 0;
  unsigned uvBuf = g->uv ? bufferFor(g->uv->array, b) : 0;
  unsigned colorBuf = g->color ? bufferFor(g->color->array, b) : 0;
  unsigned amatBuf = 0, astyleBuf = 0, auvdBuf = 0;
  for (const auto& kv : g->named()) {
    if (kv.first == "amat") amatBuf = bufferFor(kv.second->array, b);
    else if (kv.first == "astyle") astyleBuf = bufferFor(kv.second->array, b);
    else if (kv.first == "auvd") auvdBuf = bufferFor(kv.second->array, b);
  }

  // the index buffer (GL_UNSIGNED_INT: the game's geometries exceed 65535)
  if (!g->index) return;
  if (b.ibo == 0 || b.indexCount != static_cast<long long>(g->index->array.size())) {
    if (b.ibo == 0) glGenBuffers(1, &b.ibo);
    std::vector<GLuint> idx(g->index->array.size());
    for (size_t i = 0; i < idx.size(); i++)
      idx[i] = static_cast<GLuint>(g->index->array[i]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(GLuint)),
                 idx.data(), GL_STATIC_DRAW);
    b.indexCount = static_cast<long long>(idx.size());
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.ibo);

  const bool instanced = !item.instances.empty();
  if (instanced) uploadInstances(item, b);

  glUseProgram(p.program);
  // three.js material.transparent / depthWrite: the see-through glass blends
  // over what is behind it and does not write depth
  if (item.opacity < 1.0) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(item.depthWrite ? GL_TRUE : GL_FALSE);
    if (int l = p.loc("diffuse"); l >= 0)
      glUniform4f(l, 1, 1, 1, static_cast<GLfloat>(item.opacity));
  } else {
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (int l = p.loc("diffuse"); l >= 0) glUniform4f(l, 1, 1, 1, 1);
  }
  // three.js material.side: BackSide for the sky dome, FrontSide otherwise
  if (kind == MaterialKind::Sky) {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
  } else {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
  }

  // -- the per-frame uniforms (three.js p_uniforms.setValue per render) -----
  Matrix4 modelView;
  modelView.multiplyMatrices(view, item.world);
  float m16[16];
  auto toFloat16 = [&](const Matrix4& m, float* out) {
    for (int i = 0; i < 16; i++) out[i] = static_cast<float>(m.e[i]);
  };
  if (int l = p.loc("modelMatrix"); l >= 0) {
    toFloat16(item.world, m16);
    glUniformMatrix4fv(l, 1, GL_FALSE, m16);
  }
  if (int l = p.loc("viewMatrix"); l >= 0) {
    toFloat16(view, m16);
    glUniformMatrix4fv(l, 1, GL_FALSE, m16);
  }
  if (int l = p.loc("projectionMatrix"); l >= 0) {
    toFloat16(projection, m16);
    glUniformMatrix4fv(l, 1, GL_FALSE, m16);
  }
  if (int l = p.loc("modelViewMatrix"); l >= 0) {
    toFloat16(modelView, m16);
    glUniformMatrix4fv(l, 1, GL_FALSE, m16);
  }
  if (int l = p.loc("normalMatrix"); l >= 0) {
    float nm[9];
    normalMatrix3(modelView, nm);
    glUniformMatrix3fv(l, 1, GL_FALSE, nm);
  }
  if (int l = p.loc("cameraPosition"); l >= 0)
    glUniform3f(l, static_cast<GLfloat>(cameraPos.x), static_cast<GLfloat>(cameraPos.y),
                static_cast<GLfloat>(cameraPos.z));
  if (int l = p.loc("ambient"); l >= 0) glUniform3f(l, 0, 0, 0);
  if (int l = p.loc("emissive"); l >= 0) glUniform3f(l, 0, 0, 0);
  if (int l = p.loc("fogColor"); l >= 0)
    glUniform3f(l, static_cast<GLfloat>(lights.fogColor.r), static_cast<GLfloat>(lights.fogColor.g),
                static_cast<GLfloat>(lights.fogColor.b));
  if (int l = p.loc("fogNear"); l >= 0) glUniform1f(l, static_cast<GLfloat>(lights.fogNear));
  if (int l = p.loc("fogFar"); l >= 0) glUniform1f(l, static_cast<GLfloat>(lights.fogFar));
  if (int l = p.loc("uSunDir"); l >= 0)
    glUniform3f(l, static_cast<GLfloat>(lights.sunDir.x), static_cast<GLfloat>(lights.sunDir.y),
                static_cast<GLfloat>(lights.sunDir.z));
  if (int l = p.loc("uSunCol"); l >= 0)
    glUniform3f(l, static_cast<GLfloat>(lights.sunColor.r), static_cast<GLfloat>(lights.sunColor.g),
                static_cast<GLfloat>(lights.sunColor.b));
  if (int l = p.loc("uSunInt"); l >= 0) glUniform1f(l, static_cast<GLfloat>(lights.sunIntensity));
  if (int l = p.loc("uSkyCol"); l >= 0)
    glUniform3f(l, static_cast<GLfloat>(lights.skyColor.r), static_cast<GLfloat>(lights.skyColor.g),
                static_cast<GLfloat>(lights.skyColor.b));
  if (int l = p.loc("uGroundCol"); l >= 0)
    glUniform3f(l, static_cast<GLfloat>(lights.groundColor.r),
                static_cast<GLfloat>(lights.groundColor.g),
                static_cast<GLfloat>(lights.groundColor.b));
  if (int l = p.loc("uHemiInt"); l >= 0) glUniform1f(l, static_cast<GLfloat>(lights.hemiIntensity));
  // the sky dome's own uniforms (three.js sets them on its ShaderMaterial;
  // only the Sky program declares them)
  if (kind == MaterialKind::Sky) {
    if (int l = p.loc("uZenith"); l >= 0)
      glUniform3f(l, static_cast<GLfloat>(skyZenith_.r), static_cast<GLfloat>(skyZenith_.g),
                  static_cast<GLfloat>(skyZenith_.b));
    if (int l = p.loc("uHorizon"); l >= 0)
      glUniform3f(l, static_cast<GLfloat>(skyHorizon_.r), static_cast<GLfloat>(skyHorizon_.g),
                  static_cast<GLfloat>(skyHorizon_.b));
    if (int l = p.loc("uSunColor"); l >= 0)
      glUniform3f(l, static_cast<GLfloat>(skySunColor_.r), static_cast<GLfloat>(skySunColor_.g),
                  static_cast<GLfloat>(skySunColor_.b));
  }

  // -- the texture (the sign atlas on the face, the cover on the terrain) --
  if (useMap) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureFor(item.texture));
    if (int l = p.loc("map"); l >= 0) glUniform1i(l, 0);
  } else if (int l = p.loc("map"); l >= 0) {
    glUniform1i(l, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  // -- the attributes (three.js WebGLBindingStates.setupVertexAttributes) --
  struct Attr {
    const char* name;
    unsigned buf;
    int size;
    const std::vector<double>* data;
    bool bound;
  };
  Attr attrs[8] = {
      {"position", positionBuf, 3, &g->position->array, false},
      {"normal", normalBuf, 3, g->normal ? &g->normal->array : nullptr, false},
      {"uv", uvBuf, 2, g->uv ? &g->uv->array : nullptr, false},
      {"color", colorBuf, 3, g->color ? &g->color->array : nullptr, false},
      {"amat", amatBuf, 1, nullptr, false},
      {"astyle", astyleBuf, 4, nullptr, false},
      {"auvd", auvdBuf, 4, nullptr, false},
  };
  int enabledCount = 0;
  for (auto& a : attrs) {
    if (!a.buf) continue;
    int loc = p.attr(a.name);
    if (loc < 0) continue;
    glBindBuffer(GL_ARRAY_BUFFER, a.buf);
    glEnableVertexAttribArray(static_cast<GLuint>(loc));
    glVertexAttribPointer(static_cast<GLuint>(loc), a.size, GL_FLOAT, GL_FALSE, 0, nullptr);
    a.bound = true;
    enabledCount++;
    (void)enabledCount;
  }
  // the named attributes carry their array for the version check
  for (const auto& kv : g->named()) {
    if (kv.first != "amat" && kv.first != "astyle" && kv.first != "auvd") continue;
    int loc = p.attr(kv.first.c_str());
    if (loc < 0) continue;
    glBindBuffer(GL_ARRAY_BUFFER, b.buffers[&kv.second->array]);
    glEnableVertexAttribArray(static_cast<GLuint>(loc));
    int size = kv.second->itemSize;
    glVertexAttribPointer(static_cast<GLuint>(loc), size, GL_FLOAT, GL_FALSE, 0, nullptr);
  }

  // instancing: the matrices (and colours) per instance
  int instMatLoc = instanced ? p.attr("instanceMatrix") : -1;
  if (instanced && instMatLoc >= 0) {
    glBindBuffer(GL_ARRAY_BUFFER, b.instanceMatrixBuffer);
    for (int c = 0; c < 4; c++) {
      GLuint l = static_cast<GLuint>(instMatLoc + c);
      glEnableVertexAttribArray(l);
      glVertexAttribPointer(l, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float),
                            reinterpret_cast<const void*>(static_cast<uintptr_t>(c * 4 * sizeof(float))));
      glVertexAttribDivisor(l, 1);
    }
  }
  int instColLoc = (instanced && !item.instanceColors.empty()) ? p.attr("instanceColor") : -1;
  if (instColLoc >= 0 && b.colorBuffer) {
    glBindBuffer(GL_ARRAY_BUFFER, b.colorBuffer);
    glEnableVertexAttribArray(static_cast<GLuint>(instColLoc));
    glVertexAttribPointer(static_cast<GLuint>(instColLoc), 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glVertexAttribDivisor(static_cast<GLuint>(instColLoc), 1);
  }

  // -- the draw itself ------------------------------------------------------
  const void* offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(firstIndex) * sizeof(GLuint));
  if (instanced) {
    glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, offset,
                            static_cast<GLsizei>(item.instances.size()));
  } else {
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, offset);
  }

  // leave the attrib divisors behind for the next draw (three.js resets them)
  if (instMatLoc >= 0)
    for (int c = 0; c < 4; c++) glVertexAttribDivisor(static_cast<GLuint>(instMatLoc + c), 0);
  if (instColLoc >= 0) glVertexAttribDivisor(static_cast<GLuint>(instColLoc), 0);
}

unsigned WebGLRenderer::textureFor(const std::string& key) {
  auto it = textures_.find(key);
  if (it != textures_.end()) return it->second;
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  textures_[key] = tex;
  return tex;
}

void WebGLRenderer::uploadTexture(const std::string& key, const uint8_t* rgba, int width,
                                  int height, bool mipmaps) {
  if (!available_ || !rgba || width <= 0 || height <= 0) return;
  GLuint tex = textureFor(key);
  glBindTexture(GL_TEXTURE_2D, tex);
  // the sign atlas is drawn top-down like a canvas (three.js CanvasTexture
  // flips Y on upload); the cover rasters are DataTextures (flipY = false).
  // GLES has no UNPACK_FLIP_Y, so the rows are turned here.
  const uint8_t* pixels = rgba;
  std::vector<uint8_t> flipped;
  if (key == "signs") {
    size_t row = static_cast<size_t>(width) * 4;
    flipped.resize(static_cast<size_t>(row) * static_cast<size_t>(height));
    for (int y = 0; y < height; y++)
      std::memcpy(flipped.data() + static_cast<size_t>(y) * row,
                  rgba + static_cast<size_t>(height - 1 - y) * row, row);
    pixels = flipped.data();
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  if (mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  }
}

void WebGLRenderer::bindTexture(const std::string& key) {
  if (!available_) return;
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textureFor(key));
}

unsigned WebGLRenderer::framebufferFor(RenderTarget& target) {
  if (target.fbo) return target.fbo;
  glGenTextures(1, &target.colorTex);
  glBindTexture(GL_TEXTURE_2D, target.colorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, target.width, target.height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &target.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.colorTex, 0);
  if (target.depthBuffer) {
    glGenRenderbuffers(1, &target.depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, target.width, target.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target.depthRbo);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return target.fbo;
}

#endif  // __EMSCRIPTEN__

}  // namespace fs
