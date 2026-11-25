// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The renderer (see renderer.h).
 *
 * What is drawn and how it looks follows three.js r187 (renderers/WebGLRenderer.js,
 * webgl/WebGLLights.js, WebGLShadowMap.js, WebGLMaterials.js, WebGLState.js – MIT
 * licence): the same sorting, the same light numbers, the same shadow matrices,
 * the same blending.  How it gets there is this file's own.
 */
#include "engine/renderer.h"
#include "platform/plat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "engine/gpu.h"
#include "engine/shaders.h"
#include "platform/glhost.h"

#if defined(__EMSCRIPTEN__) || defined(FS_NATIVE_GL)
#define FS_GL 1
#include <GLES3/gl3.h>
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#endif

namespace fs {

#ifdef FS_GL

namespace {

// ---------------------------------------------------------------------------
// Programs and their uniforms
// ---------------------------------------------------------------------------

/** What a uniform of a program is fed from. */
enum class U : uint8_t {
  Custom,
  // once per object
  ModelMatrix, ModelViewMatrix, NormalMatrix, ReceiveShadow, Center,
  // once per material
  Diffuse, Opacity, Emissive, Map, MapTransform, AlphaMap, AlphaMapTransform, EmissiveMap, EmissiveMapTransform, UvTransform, AlphaTest, Specular, Shininess, Size, Scale, Rotation,
  // once per frame
  ProjectionMatrix, ViewMatrix, CameraPosition, IsOrthographic, FogColor, FogNear, FogFar, ToneMappingExposure, AmbientLightColor, LightProbe,
  DirDirection, DirColor, HemiDirection, HemiSky, HemiGround,
  SpotPosition, SpotDirection, SpotColor, SpotDistance, SpotDecay, SpotConeCos, SpotPenumbraCos,
  PointPosition, PointColor, PointDistance, PointDecay,
  ShadowMap, ShadowMatrix, ShadowIntensity, ShadowBias, ShadowNormalBias, ShadowRadius, ShadowMapSize,
  LightMatrix,
};

struct Slot {
  U kind = U::Custom;
  GLint loc = -1;
  GLenum type = 0;
  int size = 1;
  /** which light of an array (directionalLights[ index ]) */
  int index = 0;
  /** a sampler's texture unit */
  int unit = -1;
  std::string name;
};

struct Program {
  GLuint id = 0;
  bool ok = false;
  /** the game's own attributes of this program and their locations (FIRST_CUSTOM_ATTRIBUTE on), and a key of them (0: none) */
  std::vector<std::pair<std::string, int>> attribs;
  uint64_t attribKey = 0;
  std::vector<Slot> frame, material, object, custom;
  /** the frame (camera, target) its frame uniforms were last set for, and the material after that */
  uint32_t frameStamp = 0;
  uint32_t materialId = 0;
  uint32_t materialStamp = 0;
  int nextUnit = 2;
  /** the unit of the material's alpha map (-1: the program has none) */
  int alphaMapUnit = -1;
  /** the unit of the material's emissive map (-1: the program has none) */
  int emissiveMapUnit = -1;
};

/** The programs of one material: one per combination of what the object and the frame add. */
struct MaterialPrograms {
  struct Entry {
    uint32_t variant;
    Program* program;
  };
  std::vector<Entry> list;
};

struct NameKind {
  const char* name;
  U kind;
  int group;   // 0 object, 1 material, 2 frame
};

const NameKind PLAIN[] = {
    {"modelMatrix", U::ModelMatrix, 0},
    {"modelViewMatrix", U::ModelViewMatrix, 0},
    {"normalMatrix", U::NormalMatrix, 0},
    {"receiveShadow", U::ReceiveShadow, 0},
    {"center", U::Center, 0},
    {"diffuse", U::Diffuse, 1},
    {"opacity", U::Opacity, 1},
    {"emissive", U::Emissive, 1},
    {"map", U::Map, 1},
    {"mapTransform", U::MapTransform, 1},
    {"alphaMap", U::AlphaMap, 1},
    {"alphaMapTransform", U::AlphaMapTransform, 1},
    {"emissiveMap", U::EmissiveMap, 1},
    {"emissiveMapTransform", U::EmissiveMapTransform, 1},
    {"uvTransform", U::UvTransform, 1},
    {"alphaTest", U::AlphaTest, 1},
    {"specular", U::Specular, 1},
    {"shininess", U::Shininess, 1},
    {"size", U::Size, 1},
    {"scale", U::Scale, 1},
    {"rotation", U::Rotation, 1},
    {"projectionMatrix", U::ProjectionMatrix, 2},
    {"viewMatrix", U::ViewMatrix, 2},
    {"cameraPosition", U::CameraPosition, 2},
    {"isOrthographic", U::IsOrthographic, 2},
    {"fogColor", U::FogColor, 2},
    {"fogNear", U::FogNear, 2},
    {"fogFar", U::FogFar, 2},
    {"toneMappingExposure", U::ToneMappingExposure, 2},
    {"ambientLightColor", U::AmbientLightColor, 2},
    {"lightProbe[0]", U::LightProbe, 2},
    {"lightMatrix", U::LightMatrix, 2},
};

struct ArrayKind {
  const char* array;
  const char* field;   // "" for an array of plain values
  U kind;
};

const ArrayKind ARRAYS[] = {
    {"directionalLights", "direction", U::DirDirection},
    {"directionalLights", "color", U::DirColor},
    {"hemisphereLights", "direction", U::HemiDirection},
    {"hemisphereLights", "skyColor", U::HemiSky},
    {"hemisphereLights", "groundColor", U::HemiGround},
    {"spotLights", "position", U::SpotPosition},
    {"spotLights", "direction", U::SpotDirection},
    {"spotLights", "color", U::SpotColor},
    {"spotLights", "distance", U::SpotDistance},
    {"spotLights", "decay", U::SpotDecay},
    {"spotLights", "coneCos", U::SpotConeCos},
    {"spotLights", "penumbraCos", U::SpotPenumbraCos},
    {"pointLights", "position", U::PointPosition},
    {"pointLights", "color", U::PointColor},
    {"pointLights", "distance", U::PointDistance},
    {"pointLights", "decay", U::PointDecay},
    {"directionalShadowMap", "", U::ShadowMap},
    {"directionalShadowMatrix", "", U::ShadowMatrix},
    {"directionalLightShadows", "shadowIntensity", U::ShadowIntensity},
    {"directionalLightShadows", "shadowBias", U::ShadowBias},
    {"directionalLightShadows", "shadowNormalBias", U::ShadowNormalBias},
    {"directionalLightShadows", "shadowRadius", U::ShadowRadius},
    {"directionalLightShadows", "shadowMapSize", U::ShadowMapSize},
};

bool isSampler(GLenum t) { return t == GL_SAMPLER_2D || t == GL_SAMPLER_2D_SHADOW || t == GL_SAMPLER_CUBE || t == GL_SAMPLER_3D || t == GL_SAMPLER_2D_ARRAY; }

/** What a uniform's name says it is: fills kind, index and the group it belongs to. */
int classify(const std::string& name, Slot& s) {
  for (const NameKind& n : PLAIN)
    if (name == n.name) {
      s.kind = n.kind;
      return n.group;
    }
  // lights[ i ].field  /  array[ i ]
  size_t open = name.find('[');
  if (open != std::string::npos) {
    size_t close = name.find(']', open);
    std::string array = name.substr(0, open);
    int index = std::atoi(name.c_str() + open + 1);
    std::string field = close != std::string::npos && close + 1 < name.size() && name[close + 1] == '.' ? name.substr(close + 2) : "";
    for (const ArrayKind& a : ARRAYS)
      if (array == a.array && field == a.field) {
        s.kind = a.kind;
        s.index = index;
        return 2;
      }
  }
  s.kind = U::Custom;
  return 3;
}

GLuint compileStage(GLenum type, const std::string& source, const std::string& what) {
  GLuint sh = glCreateShader(type);
  const char* src = source.c_str();
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = GL_FALSE;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
    std::string log(static_cast<size_t>(len > 0 ? len : 1), '\0');
    glGetShaderInfoLog(sh, len, nullptr, log.data());
    std::fprintf(stderr, "fahrsim: the %s shader of %s does not compile:\n%s\n", type == GL_VERTEX_SHADER ? "vertex" : "fragment", what.c_str(), log.c_str());
    // the lines around the first error, numbered
    int errLine = 0;
    size_t e = log.find("0:");
    if (e != std::string::npos) errLine = std::atoi(log.c_str() + e + 2);
    int ln = 1;
    size_t at = 0;
    while (at < source.size()) {
      size_t end = source.find('\n', at);
      if (end == std::string::npos) end = source.size();
      if (errLine > 0 && ln >= errLine - 4 && ln <= errLine + 4) std::fprintf(stderr, "%5d: %.*s\n", ln, static_cast<int>(end - at), source.c_str() + at);
      at = end + 1;
      ln++;
    }
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

Program* linkProgram(const std::string& vs, const std::string& fs, const std::string& what) {
  auto* p = new Program();
  GLuint v = compileStage(GL_VERTEX_SHADER, vs, what);
  GLuint f = compileStage(GL_FRAGMENT_SHADER, fs, what);
  if (!v || !f) {
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
    return p;
  }
  p->id = glCreateProgram();
  glAttachShader(p->id, v);
  glAttachShader(p->id, f);
  // (the game's own attributes: those this vertex shader declares, from FIRST_CUSTOM_ATTRIBUTE on, in their order)
  {
    int next = FIRST_CUSTOM_ATTRIBUTE;
    uint64_t key = 1469598103934665603ull;
    for (const std::string& name : customAttributes(vs)) {
      if (next >= END_CUSTOM_ATTRIBUTES) {
        plat::warn("[renderer] a vertex shader declares more attributes of its own than there are locations: " + name + " is not fed");
        break;
      }
      p->attribs.emplace_back(name, next);
      key = (key ^ std::hash<std::string>()(name) ^ static_cast<uint64_t>(next)) * 1099511628211ull;
      glBindAttribLocation(p->id, static_cast<GLuint>(next), name.c_str());
      next++;
    }
    p->attribKey = p->attribs.empty() ? 0 : (key ? key : 1);
  }
  for (const auto& a : attributeLocations()) glBindAttribLocation(p->id, static_cast<GLuint>(a.second), a.first.c_str());
  glLinkProgram(p->id);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint linked = GL_FALSE;
  glGetProgramiv(p->id, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint len = 0;
    glGetProgramiv(p->id, GL_INFO_LOG_LENGTH, &len);
    std::string log(static_cast<size_t>(len > 0 ? len : 1), '\0');
    glGetProgramInfoLog(p->id, len, nullptr, log.data());
    std::fprintf(stderr, "fahrsim: the program of %s does not link:\n%s\n", what.c_str(), log.c_str());
    return p;
  }
  p->ok = true;
  // what the program asks for, sorted by how often it is set
  GLint count = 0;
  glGetProgramiv(p->id, GL_ACTIVE_UNIFORMS, &count);
  glUseProgram(p->id);
  for (GLint i = 0; i < count; i++) {
    char name[256];
    GLsizei len = 0;
    GLint size = 0;
    GLenum type = 0;
    glGetActiveUniform(p->id, static_cast<GLuint>(i), sizeof name, &len, &size, &type, name);
    Slot s;
    s.name.assign(name, static_cast<size_t>(len));
    s.type = type;
    s.size = size;
    s.loc = glGetUniformLocation(p->id, name);
    if (s.loc < 0) continue;
    int group = classify(s.name, s);
    if (isSampler(type)) {
      // (the diffuse map is unit 0, the shadow map unit 1, the game's own from 2 on)
      s.unit = s.kind == U::Map ? 0 : s.kind == U::ShadowMap ? 1 : p->nextUnit++;
      if (s.kind == U::AlphaMap) p->alphaMapUnit = s.unit;
      if (s.kind == U::EmissiveMap) p->emissiveMapUnit = s.unit;
      glUniform1i(s.loc, s.unit);
    }
    if (s.kind == U::Custom && s.name.size() > 3 && s.name.compare(s.name.size() - 3, 3, "[0]") == 0) s.name.resize(s.name.size() - 3);
    (group == 0 ? p->object : group == 1 ? p->material : group == 2 ? p->frame : p->custom).push_back(std::move(s));
  }
  return p;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

inline void toFloat(const Matrix4& m, float* out) {
  for (int i = 0; i < 16; i++) out[i] = static_cast<float>(m.e[i]);
}

inline double linearToSrgb(double c) { return c < 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 0.41666) - 0.055; }

GLenum glType(AttrType t) {
  switch (t) {
    case AttrType::F32: return GL_FLOAT;
    case AttrType::U8: return GL_UNSIGNED_BYTE;
    case AttrType::I8: return GL_BYTE;
    case AttrType::U16: return GL_UNSIGNED_SHORT;
    case AttrType::I16: return GL_SHORT;
    case AttrType::U32: return GL_UNSIGNED_INT;
    case AttrType::I32: return GL_INT;
  }
  return GL_FLOAT;
}

GLenum glWrap(Wrapping w) { return w == Wrapping::Repeat ? GL_REPEAT : w == Wrapping::MirroredRepeat ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE; }
GLenum glFilter(Filter f) {
  switch (f) {
    case Filter::Nearest: return GL_NEAREST;
    case Filter::Linear: return GL_LINEAR;
    case Filter::NearestMipmapNearest: return GL_NEAREST_MIPMAP_NEAREST;
    case Filter::LinearMipmapNearest: return GL_LINEAR_MIPMAP_NEAREST;
    case Filter::NearestMipmapLinear: return GL_NEAREST_MIPMAP_LINEAR;
    case Filter::LinearMipmapLinear: return GL_LINEAR_MIPMAP_LINEAR;
  }
  return GL_LINEAR;
}
bool usesMipmaps(Filter f) { return f != Filter::Nearest && f != Filter::Linear; }

/** One thing to draw: an object, the part of its geometry, the material for it. */
struct Item {
  Object3D* object;
  BufferGeometry* geometry;
  Material* material;
  int start, count;
  int renderOrder;
  double z;
  Program* program;
};

struct LightState {
  Color ambient{0, 0, 0};
  std::vector<Light*> dir, hemi, spot, point;
  /** the directional lights that cast a shadow come first in `dir`: how many */
  int dirShadows = 0;
  void clear() {
    ambient = Color(0, 0, 0);
    dir.clear();
    hemi.clear();
    spot.clear();
    point.clear();
    dirShadows = 0;
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// The renderer's state
// ---------------------------------------------------------------------------

struct Renderer::Impl {
  Renderer& r;
  explicit Impl(Renderer& owner) : r(owner) {}

  std::unordered_map<std::string, Program*> programs;
  Program* depthPrograms[4] = {nullptr, nullptr, nullptr, nullptr};
  uint32_t frameStamp = 0;
  std::vector<Item> opaque, transparent, casters;
  LightState lights;
  Frustum frustum;
  Matrix4 projScreen, viewMatrix, projectionMatrix;
  Vector3 cameraPosition;
  bool cameraOrtho = false;
  Scene* scene = nullptr;
  Color clearColor{0, 0, 0};
  double clearAlpha = 1;
  bool viewportSet = false;
  int vp[4] = {0, 0, 1, 1};
  bool scissorTest = false;
  int scissor[4] = {0, 0, 1, 1};
  int maxAnisotropy = -1;
  GeometryPtr spriteQuad;

  // what OpenGL is set to (so that nothing is set twice)
  GLuint curProgram = 0, curVao = 0;
  int curCull = -1;        // 0 off, 1 back, 2 front
  int curFrontCw = -1;
  int curBlend = -1;
  int curDepthTest = -1, curDepthWrite = -1, curColorWrite = -1;
  int curPolyOffset = -1;
  double curPolyFactor = 0, curPolyUnits = 0;
  GLuint curTex[16] = {0};
  int curUnit = -1;

  void resetState() {
    curProgram = curVao = 0;
    curCull = curFrontCw = curBlend = curDepthTest = curDepthWrite = curColorWrite = curPolyOffset = -1;
    for (GLuint& t : curTex) t = 0;
    curUnit = -1;
  }

  // --- handles handed back ---------------------------------------------------
  void freeReleased() {
    auto& list = gpu::freed();
    if (list.empty()) return;
    for (const gpu::Freed& f : list) {
      GLuint h = f.handle;
      switch (f.kind) {
        case gpu::Kind::Buffer: glDeleteBuffers(1, &h); break;
        case gpu::Kind::VertexArray:
          if (curVao == h) curVao = 0;
          glDeleteVertexArrays(1, &h);
          break;
        case gpu::Kind::Texture:
          for (GLuint& t : curTex)
            if (t == h) t = 0;
          glDeleteTextures(1, &h);
          break;
        case gpu::Kind::Framebuffer: glDeleteFramebuffers(1, &h); break;
        case gpu::Kind::Renderbuffer: glDeleteRenderbuffers(1, &h); break;
        case gpu::Kind::Programs: delete static_cast<MaterialPrograms*>(f.pointer); break;
      }
    }
    list.clear();
  }

  // --- textures ----------------------------------------------------------------
  void bindTexture(int unit, GLuint tex, GLenum target = GL_TEXTURE_2D) {
    // (the unit is made the active one even when the texture is bound there already: the caller may upload
    // pixels or set parameters next, and those go to the active unit's texture)
    if (curUnit != unit) {
      glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
      curUnit = unit;
    }
    if (curTex[unit] == tex) return;
    glBindTexture(target, tex);
    curTex[unit] = tex;
  }

  /** the texture's handle, its pixels uploaded if they changed; bound to `unit` */
  void useTexture(int unit, Texture& t) {
    if (!t.gpu) {
      GLuint id = 0;
      glGenTextures(1, &id);
      t.gpu = id;
      t.gpuVersion = 0xffffffffu;
      r.info.textures++;
    }
    bindTexture(unit, t.gpu);
    if (t.isRenderTarget || t.gpuVersion == t.version) return;
    t.gpuVersion = t.version;
    bool uploaded = false;
    if (t.data && t.width > 0 && t.height > 0) {
      GLenum internal = GL_RGBA8, format = GL_RGBA;
      int bpp = 4;
      if (t.format == PixelFormat::Red) {
        internal = GL_R8;
        format = GL_RED;
        bpp = 1;
      } else if (t.format == PixelFormat::RGB) {
        internal = t.srgb ? GL_SRGB8 : GL_RGB8;
        format = GL_RGB;
        bpp = 3;
      } else if (t.srgb) internal = GL_SRGB8_ALPHA8;
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      const std::vector<uint8_t>& px = *t.data;
      if (px.size() >= static_cast<size_t>(t.width) * static_cast<size_t>(t.height) * static_cast<size_t>(bpp)) {
        if (t.flipY) {
          size_t row = static_cast<size_t>(t.width) * static_cast<size_t>(bpp);
          std::vector<uint8_t> flipped(row * static_cast<size_t>(t.height));
          for (int y = 0; y < t.height; y++) std::memcpy(flipped.data() + static_cast<size_t>(y) * row, px.data() + static_cast<size_t>(t.height - 1 - y) * row, row);
          glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), t.width, t.height, 0, format, GL_UNSIGNED_BYTE, flipped.data());
        } else glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), t.width, t.height, 0, format, GL_UNSIGNED_BYTE, px.data());
        uploaded = true;
      }
    } else if (t.canvas) {
      uploaded = plat::gl::texImageCanvas(t.canvas.el().handle(), t.flipY, t.srgb, t.width, t.height);
    } else if (t.image) {
      uploaded = plat::gl::texImageImage(t.image, t.flipY, t.srgb, t.width, t.height);
    }
    if (!uploaded) {
      // nothing to show (yet): one white pixel, so that the material still draws
      const uint8_t white[4] = {255, 255, 255, 255};
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    }
    bool mips = uploaded && t.generateMipmaps && usesMipmaps(t.minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(glWrap(t.wrapS)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(glWrap(t.wrapT)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(t.magFilter == Filter::Nearest ? GL_NEAREST : GL_LINEAR));
    Filter mn = t.minFilter;
    if (!mips && usesMipmaps(mn)) mn = (mn == Filter::NearestMipmapNearest || mn == Filter::NearestMipmapLinear) ? Filter::Nearest : Filter::Linear;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(glFilter(mn)));
    if (mips) glGenerateMipmap(GL_TEXTURE_2D);
    if (t.anisotropy > 1 && r.getMaxAnisotropy() > 1)
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, static_cast<GLfloat>(std::min(t.anisotropy, r.getMaxAnisotropy())));
  }

  // --- render targets -----------------------------------------------------------
  void setupTarget(RenderTarget& t, bool depthTexture) {
    if (t.gpuFbo && t.gpuW == t.width && t.gpuH == t.height) return;
    if (!t.gpuFbo) {
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      t.gpuFbo = fbo;
    }
    Texture& tex = *t.texture;
    if (!tex.gpu) {
      GLuint id = 0;
      glGenTextures(1, &id);
      tex.gpu = id;
      r.info.textures++;
    }
    tex.isRenderTarget = true;
    tex.width = t.width;
    tex.height = t.height;
    bindTexture(0, tex.gpu);
    glBindFramebuffer(GL_FRAMEBUFFER, t.gpuFbo);
    if (depthTexture) {
      // the shadow map: only depth, read with a comparison (sampler2DShadow), four taps blended
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, t.width, t.height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex.gpu, 0);
      const GLenum none = GL_NONE;
      glDrawBuffers(1, &none);
      glReadBuffer(GL_NONE);
    } else {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      bool mips = tex.generateMipmaps && usesMipmaps(tex.minFilter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(mips ? glFilter(tex.minFilter) : (tex.minFilter == Filter::Nearest ? GL_NEAREST : GL_LINEAR)));
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(tex.magFilter == Filter::Nearest ? GL_NEAREST : GL_LINEAR));
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(glWrap(tex.wrapS)));
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(glWrap(tex.wrapT)));
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.gpu, 0);
      if (t.depthBuffer) {
        if (!t.gpuDepth) {
          GLuint rb = 0;
          glGenRenderbuffers(1, &rb);
          t.gpuDepth = rb;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, t.gpuDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, t.width, t.height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.gpuDepth);
      }
    }
    t.gpuW = t.width;
    t.gpuH = t.height;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      std::fprintf(stderr, "fahrsim: a render target of %d×%d is not complete\n", t.width, t.height);
  }

  void bindTarget(int& w, int& h) {
    if (r.target_) {
      setupTarget(*r.target_, false);
      glBindFramebuffer(GL_FRAMEBUFFER, r.target_->gpuFbo);
      w = r.target_->width;
      h = r.target_->height;
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, plat::gl::screenFramebuffer());
      w = r.bufferWidth();
      h = r.bufferHeight();
    }
  }

  // --- geometry -------------------------------------------------------------------
  /**
   * The instance buffers each vertex array is set up with (matrices, colours): a vertex array keeps
   * them, so they are pointed to only when they change – not for every draw (WebGL checks each call).
   */
  std::unordered_map<GLuint, std::array<GLuint, 2>> vaoInstances;

  void uploadAttribute(const BufferAttribute& a, GLenum target) {
    if (a.gpu && a.gpuVersion == a.version) return;
    bool fresh = !a.gpu;
    if (fresh) {
      GLuint id = 0;
      glGenBuffers(1, &id);
      a.gpu = id;
      // (an id given out again: a vertex array that pointed to the buffer of that id before must be set up anew)
      for (auto it = vaoInstances.begin(); it != vaoInstances.end();)
        it = it->second[0] == id || it->second[1] == id ? vaoInstances.erase(it) : std::next(it);
    }
    glBindBuffer(target, a.gpu);
    glBufferData(target, static_cast<GLsizeiptr>(a.byteLength()), a.data(), a.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    a.gpuVersion = a.version;
  }

  /** a number that changes when the geometry's set of arrays does (which arrays, in which buffers) */
  static uint64_t layoutOf(const BufferGeometry& g) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) {
      h ^= v;
      h *= 1099511628211ull;
    };
    for (const auto& kv : g.attributes) {
      mix(std::hash<std::string>()(kv.first));
      mix(reinterpret_cast<uintptr_t>(kv.second.get()));
      mix(static_cast<uint64_t>(kv.second->itemSize) * 8 + static_cast<uint64_t>(kv.second->type));
    }
    mix(reinterpret_cast<uintptr_t>(g.index.get()));
    return h ? h : 1;
  }

  void bindVao(GLuint vao) {
    if (curVao == vao) return;
    glBindVertexArray(vao);
    curVao = vao;
  }

  /**
   * The geometry's vertex array, its arrays uploaded; bound.  An InstancedMesh has a vertex array of its
   * own (its geometry's arrays and its instance arrays): several of them often share one geometry – the
   * cars of the traffic – and one array for all would be pointed anew at every switch.
   */
  /** the game's own attributes each vertex array is set up for (Program::attribKey; 0: none) */
  std::unordered_map<GLuint, uint64_t> vaoAttribs;

  void useGeometry(BufferGeometry& g, Object3D* object, const Program& prog) {
    InstancedMesh* inst = object && object->type == ObjectType::InstancedMesh ? static_cast<InstancedMesh*>(object) : nullptr;
    uint32_t& vaoOf = inst ? inst->gpuVao : g.gpuVao;
    uint64_t& layoutStored = inst ? inst->gpuLayout : g.gpuLayout;
    uint64_t layout = layoutOf(g);
    bool rebuild = !vaoOf || layoutStored != layout;
    // (a program with attributes of its own needs them where it has them; one without – the shadow map's – takes the array as it is)
    if (!rebuild && prog.attribKey != 0) {
      auto known = vaoAttribs.find(vaoOf);
      rebuild = known == vaoAttribs.end() || known->second != prog.attribKey;
    }
    if (!vaoOf) {
      GLuint vao = 0;
      glGenVertexArrays(1, &vao);
      vaoOf = vao;
      r.info.geometries++;
    }
    // (a vertex array made anew, or with other arrays: the instance arrays it held are gone)
    if (rebuild) vaoInstances.erase(vaoOf);
    bindVao(vaoOf);
    if (rebuild) {
      // (what another program's own attributes left switched on would be read past its buffer's end)
      for (int loc = FIRST_CUSTOM_ATTRIBUTE; loc < END_CUSTOM_ATTRIBUTES; loc++) glDisableVertexAttribArray(static_cast<GLuint>(loc));
      vaoAttribs[vaoOf] = prog.attribKey;
    }
    for (const auto& kv : g.attributes) {
      const BufferAttribute& a = *kv.second;
      bool changed = !a.gpu || a.gpuVersion != a.version;
      if (!changed && !rebuild) continue;
      uploadAttribute(a, GL_ARRAY_BUFFER);
      if (!rebuild) continue;
      int loc = attributeLocation(kv.first);
      if (loc < 0)
        for (const auto& [name, at] : prog.attribs)
          if (name == kv.first) loc = at;
      if (loc < 0) continue;
      glBindBuffer(GL_ARRAY_BUFFER, a.gpu);
      glEnableVertexAttribArray(static_cast<GLuint>(loc));
      // (whole numbers that are not normalised are still read as floats by the shaders: as three.js does)
      glVertexAttribPointer(static_cast<GLuint>(loc), a.itemSize, glType(a.type), a.normalized ? GL_TRUE : GL_FALSE, 0, nullptr);
      glVertexAttribDivisor(static_cast<GLuint>(loc), static_cast<GLuint>(a.divisor));
    }
    if (g.index) {
      const BufferAttribute& ix = *g.index;
      bool changed = !ix.gpu || ix.gpuVersion != ix.version;
      if (changed || rebuild) {
        uploadAttribute(ix, GL_ELEMENT_ARRAY_BUFFER);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ix.gpu);
      }
    }
    layoutStored = layout;
  }

  /** the arrays of an InstancedMesh, on the bound vertex array */
  void useInstances(InstancedMesh& m) {
    if (m.instanceMatrix) uploadAttribute(*m.instanceMatrix, GL_ARRAY_BUFFER);
    if (m.instanceColor) uploadAttribute(*m.instanceColor, GL_ARRAY_BUFFER);
    const std::array<GLuint, 2> want{m.instanceMatrix ? m.instanceMatrix->gpu : 0u, m.instanceColor ? m.instanceColor->gpu : 0u};
    auto known = vaoInstances.find(curVao);
    if (known != vaoInstances.end() && known->second == want) return;
    vaoInstances[curVao] = want;
    if (m.instanceMatrix) {
      glBindBuffer(GL_ARRAY_BUFFER, m.instanceMatrix->gpu);
      for (int c = 0; c < 4; c++) {
        GLuint loc = static_cast<GLuint>(4 + c);
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float), reinterpret_cast<const void*>(static_cast<uintptr_t>(c) * 4 * sizeof(float)));
        glVertexAttribDivisor(loc, 1);
      }
    }
    if (m.instanceColor) {
      glBindBuffer(GL_ARRAY_BUFFER, m.instanceColor->gpu);
      glEnableVertexAttribArray(8);
      glVertexAttribPointer(8, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
      glVertexAttribDivisor(8, 1);
    }
  }

  // --- OpenGL state -----------------------------------------------------------------
  void useProgram(Program& p) {
    if (curProgram == p.id) return;
    glUseProgram(p.id);
    curProgram = p.id;
  }

  void setSide(Side side, bool mirrored) {
    // three.js: a mirrored object (negative determinant) is wound the other way round
    int cull = side == Side::Double ? 0 : 1;
    bool flip = side == Side::Back;
    int cw = (flip != mirrored) ? 1 : 0;
    if (curCull != cull) {
      if (cull) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
      } else glDisable(GL_CULL_FACE);
      curCull = cull;
    }
    if (curFrontCw != cw) {
      glFrontFace(cw ? GL_CW : GL_CCW);
      curFrontCw = cw;
    }
  }

  void setBlend(Blending b, bool transparent) {
    int mode = (b == Blending::Normal && !transparent) || b == Blending::None ? 0 : static_cast<int>(b);
    if (curBlend == mode) return;
    curBlend = mode;
    if (mode == 0) {
      glDisable(GL_BLEND);
      return;
    }
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    if (b == Blending::Additive) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
    else if (b == Blending::Multiply) glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  }

  void setDepth(bool test, bool write) {
    if (curDepthTest != (test ? 1 : 0)) {
      if (test) glEnable(GL_DEPTH_TEST);
      else glDisable(GL_DEPTH_TEST);
      curDepthTest = test ? 1 : 0;
    }
    if (curDepthWrite != (write ? 1 : 0)) {
      glDepthMask(write ? GL_TRUE : GL_FALSE);
      curDepthWrite = write ? 1 : 0;
    }
  }

  void setColorWrite(bool on) {
    if (curColorWrite == (on ? 1 : 0)) return;
    glColorMask(on, on, on, on);
    curColorWrite = on ? 1 : 0;
  }

  void setPolygonOffset(bool on, double factor, double units) {
    if (!on) {
      if (curPolyOffset != 0) {
        glDisable(GL_POLYGON_OFFSET_FILL);
        curPolyOffset = 0;
      }
      return;
    }
    if (curPolyOffset != 1) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      curPolyOffset = 1;
      curPolyFactor = 1e300;
    }
    if (curPolyFactor != factor || curPolyUnits != units) {
      glPolygonOffset(static_cast<GLfloat>(factor), static_cast<GLfloat>(units));
      curPolyFactor = factor;
      curPolyUnits = units;
    }
  }

  // --- which program -----------------------------------------------------------------
  Program* programFor(Material& m, Object3D& o, BufferGeometry& g) {
    const bool screen = !r.target_;
    const bool instanced = o.type == ObjectType::InstancedMesh;
    const bool instColor = instanced && static_cast<InstancedMesh&>(o).instanceColor != nullptr;
    const bool lit = m.type == MaterialType::Lambert || m.type == MaterialType::Phong || m.type == MaterialType::Shader;
    const bool fog = scene && scene->fog && m.fog;
    const bool shadows = r.shadowMap.enabled && lights.dirShadows > 0 && lit;
    const bool tone = r.toneMapping && screen && m.toneMapped;
    const bool normals = g.normal() != nullptr;
    uint32_t variant = (instanced ? 1u : 0) | (instColor ? 2u : 0) | (fog ? 4u : 0) | (shadows ? 8u : 0) | (tone ? 16u : 0) | (screen ? 32u : 0) | (normals ? 64u : 0) |
                       (static_cast<uint32_t>(lights.dir.size()) << 8) | (static_cast<uint32_t>(lights.hemi.size()) << 12) |
                       (static_cast<uint32_t>(lights.spot.size()) << 16) | (static_cast<uint32_t>(lights.point.size()) << 20) |
                       (static_cast<uint32_t>(lights.dirShadows) << 24);
    if (m.gpuVersion != m.version) {
      // the material changed in a way that needs other programs: forget the ones it had
      if (m.gpuPrograms) static_cast<MaterialPrograms*>(m.gpuPrograms)->list.clear();
      m.gpuVersion = m.version;
    }
    if (!m.gpuPrograms) m.gpuPrograms = new MaterialPrograms();
    auto* mp = static_cast<MaterialPrograms*>(m.gpuPrograms);
    for (const auto& e : mp->list)
      if (e.variant == variant) return e.program;

    ShaderParams p;
    p.type = m.type;
    p.map = m.map != nullptr;
    p.alphaMap = m.alphaMap != nullptr;
    p.emissiveMap = m.emissiveMap != nullptr && (m.type == MaterialType::Lambert || m.type == MaterialType::Phong);
    p.vertexColors = m.vertexColors;
    p.instancing = instanced;
    p.instancingColor = instColor;
    p.fog = fog;
    p.shadowMap = shadows;
    p.doubleSided = m.side == Side::Double;
    p.flipSided = m.side == Side::Back;
    p.flatShading = m.flatShading;
    p.alphaTest = m.alphaTest > 0;
    p.opaque = !m.transparent && m.blending == Blending::Normal;
    p.toneMapping = tone;
    p.srgbOutput = screen;
    p.vertexNormals = normals;
    p.sizeAttenuation = m.sizeAttenuation && (m.type == MaterialType::Points || m.type == MaterialType::Sprite);
    if (lit) {
      p.numDirLights = static_cast<int>(lights.dir.size());
      p.numHemiLights = static_cast<int>(lights.hemi.size());
      p.numSpotLights = static_cast<int>(lights.spot.size());
      p.numPointLights = static_cast<int>(lights.point.size());
      p.numDirLightShadows = shadows ? lights.dirShadows : 0;
    }
    p.patch = m.patch.get();
    p.custom = m.type == MaterialType::Shader ? &m : nullptr;
    std::string key = shaderKey(p);
    Program* prog;
    auto it = programs.find(key);
    if (it != programs.end()) prog = it->second;
    else {
      std::string vs, fsrc;
      buildShader(p, vs, fsrc);
      prog = linkProgram(vs, fsrc, m.name.empty() ? key : m.name + " (" + key + ")");
      programs.emplace(key, prog);
      r.info.programs++;
      curProgram = prog->id;   // (linkProgram used it to set the samplers)
    }
    mp->list.push_back({variant, prog});
    return prog;
  }

  Program* depthProgram(bool instancing, bool map) {
    int i = (instancing ? 1 : 0) | (map ? 2 : 0);
    if (!depthPrograms[i]) {
      std::string vs, fsrc;
      buildDepthShader(instancing, map, vs, fsrc);
      depthPrograms[i] = linkProgram(vs, fsrc, "the shadow map");
      curProgram = depthPrograms[i]->id;
    }
    return depthPrograms[i];
  }

  // --- uniforms -------------------------------------------------------------------------
  void set3(GLint loc, const Vector3& v) { glUniform3f(loc, static_cast<GLfloat>(v.x), static_cast<GLfloat>(v.y), static_cast<GLfloat>(v.z)); }
  void setColor(GLint loc, const Color& c, double k = 1) {
    glUniform3f(loc, static_cast<GLfloat>(c.r * k), static_cast<GLfloat>(c.g * k), static_cast<GLfloat>(c.b * k));
  }
  void setMat4(GLint loc, const Matrix4& m) {
    float f[16];
    toFloat(m, f);
    glUniformMatrix4fv(loc, 1, GL_FALSE, f);
  }
  void setMat3(GLint loc, const Matrix3& m) {
    float f[9];
    for (int i = 0; i < 9; i++) f[i] = static_cast<float>(m.e[i]);
    glUniformMatrix3fv(loc, 1, GL_FALSE, f);
  }

  Vector3 viewDirection(const Vector3& worldDir) {
    Vector3 d = worldDir;
    d.transformDirection(viewMatrix);
    return d;
  }
  Vector3 worldPositionOf(Object3D& o) {
    Vector3 p;
    p.setFromMatrixPosition(o.matrixWorld);
    return p;
  }

  void setFrameUniforms(Program& p) {
    for (const Slot& s : p.frame) {
      switch (s.kind) {
        case U::ProjectionMatrix: setMat4(s.loc, projectionMatrix); break;
        case U::ViewMatrix: setMat4(s.loc, viewMatrix); break;
        case U::CameraPosition: set3(s.loc, cameraPosition); break;
        case U::IsOrthographic: glUniform1i(s.loc, cameraOrtho ? 1 : 0); break;
        case U::FogColor:
          if (scene && scene->fog) {
            // three.js: the fog's colour is mixed in after the output conversion – as an sRGB colour on the screen
            const Color& c = scene->fog->color;
            if (!r.target_) glUniform3f(s.loc, static_cast<GLfloat>(linearToSrgb(c.r)), static_cast<GLfloat>(linearToSrgb(c.g)), static_cast<GLfloat>(linearToSrgb(c.b)));
            else setColor(s.loc, c);
          }
          break;
        case U::FogNear:
          if (scene && scene->fog) glUniform1f(s.loc, static_cast<GLfloat>(scene->fog->near));
          break;
        case U::FogFar:
          if (scene && scene->fog) glUniform1f(s.loc, static_cast<GLfloat>(scene->fog->far));
          break;
        case U::ToneMappingExposure: glUniform1f(s.loc, static_cast<GLfloat>(r.toneMappingExposure)); break;
        case U::AmbientLightColor: setColor(s.loc, lights.ambient); break;
        case U::LightProbe: {
          const float zero[27] = {0};
          glUniform3fv(s.loc, std::min(s.size, 9), zero);
          break;
        }
        case U::DirDirection:
          if (s.index < static_cast<int>(lights.dir.size())) {
            Light& l = *lights.dir[static_cast<size_t>(s.index)];
            Vector3 d = worldPositionOf(l);
            if (l.target) d.sub(worldPositionOf(*l.target));
            set3(s.loc, viewDirection(d));
          }
          break;
        case U::DirColor:
          if (s.index < static_cast<int>(lights.dir.size())) setColor(s.loc, lights.dir[static_cast<size_t>(s.index)]->color, lights.dir[static_cast<size_t>(s.index)]->intensity);
          break;
        case U::HemiDirection:
          if (s.index < static_cast<int>(lights.hemi.size())) set3(s.loc, viewDirection(worldPositionOf(*lights.hemi[static_cast<size_t>(s.index)])));
          break;
        case U::HemiSky:
          if (s.index < static_cast<int>(lights.hemi.size())) setColor(s.loc, lights.hemi[static_cast<size_t>(s.index)]->color, lights.hemi[static_cast<size_t>(s.index)]->intensity);
          break;
        case U::HemiGround:
          if (s.index < static_cast<int>(lights.hemi.size()))
            setColor(s.loc, lights.hemi[static_cast<size_t>(s.index)]->groundColor, lights.hemi[static_cast<size_t>(s.index)]->intensity);
          break;
        case U::SpotPosition:
        case U::SpotDirection:
        case U::SpotColor:
        case U::SpotDistance:
        case U::SpotDecay:
        case U::SpotConeCos:
        case U::SpotPenumbraCos:
          if (s.index < static_cast<int>(lights.spot.size())) {
            Light& l = *lights.spot[static_cast<size_t>(s.index)];
            if (s.kind == U::SpotPosition) {
              Vector3 pos = worldPositionOf(l);
              pos.applyMatrix4(viewMatrix);
              set3(s.loc, pos);
            } else if (s.kind == U::SpotDirection) {
              Vector3 d = worldPositionOf(l);
              if (l.target) d.sub(worldPositionOf(*l.target));
              set3(s.loc, viewDirection(d));
            } else if (s.kind == U::SpotColor) setColor(s.loc, l.color, l.intensity);
            else if (s.kind == U::SpotDistance) glUniform1f(s.loc, static_cast<GLfloat>(l.distance));
            else if (s.kind == U::SpotDecay) glUniform1f(s.loc, static_cast<GLfloat>(l.decay));
            else if (s.kind == U::SpotConeCos) glUniform1f(s.loc, static_cast<GLfloat>(std::cos(l.angle)));
            else glUniform1f(s.loc, static_cast<GLfloat>(std::cos(l.angle * (1 - l.penumbra))));
          }
          break;
        case U::PointPosition:
        case U::PointColor:
        case U::PointDistance:
        case U::PointDecay:
          if (s.index < static_cast<int>(lights.point.size())) {
            Light& l = *lights.point[static_cast<size_t>(s.index)];
            if (s.kind == U::PointPosition) {
              Vector3 pos = worldPositionOf(l);
              pos.applyMatrix4(viewMatrix);
              set3(s.loc, pos);
            } else if (s.kind == U::PointColor) setColor(s.loc, l.color, l.intensity);
            else if (s.kind == U::PointDistance) glUniform1f(s.loc, static_cast<GLfloat>(l.distance));
            else glUniform1f(s.loc, static_cast<GLfloat>(l.decay));
          }
          break;
        case U::ShadowMap:
          if (lights.dirShadows > 0 && lights.dir[0]->shadow.map) bindTexture(1, lights.dir[0]->shadow.map->texture->gpu);
          break;
        case U::ShadowMatrix:
          if (lights.dirShadows > 0) setMat4(s.loc, lights.dir[0]->shadow.matrix);
          break;
        case U::ShadowIntensity:
          if (s.index < lights.dirShadows) glUniform1f(s.loc, static_cast<GLfloat>(lights.dir[static_cast<size_t>(s.index)]->shadow.intensity));
          break;
        case U::ShadowBias:
          if (s.index < lights.dirShadows) glUniform1f(s.loc, static_cast<GLfloat>(lights.dir[static_cast<size_t>(s.index)]->shadow.bias));
          break;
        case U::ShadowNormalBias:
          if (s.index < lights.dirShadows) glUniform1f(s.loc, static_cast<GLfloat>(lights.dir[static_cast<size_t>(s.index)]->shadow.normalBias));
          break;
        case U::ShadowRadius:
          if (s.index < lights.dirShadows) glUniform1f(s.loc, static_cast<GLfloat>(lights.dir[static_cast<size_t>(s.index)]->shadow.radius));
          break;
        case U::ShadowMapSize:
          if (s.index < lights.dirShadows) {
            const Vector2& ms = lights.dir[static_cast<size_t>(s.index)]->shadow.mapSize;
            glUniform2f(s.loc, static_cast<GLfloat>(ms.x), static_cast<GLfloat>(ms.y));
          }
          break;
        default: break;
      }
    }
  }

  void setCustom(Program& p, const Slot& s, const Uniform& u) {
    (void)p;
    const Uniform::Value& v = u.value;
    if (const double* d = std::get_if<double>(&v)) {
      if (s.type == GL_INT || s.type == GL_BOOL) glUniform1i(s.loc, static_cast<GLint>(*d));
      else glUniform1f(s.loc, static_cast<GLfloat>(*d));
    } else if (const int* i = std::get_if<int>(&v)) {
      if (s.type == GL_FLOAT) glUniform1f(s.loc, static_cast<GLfloat>(*i));
      else glUniform1i(s.loc, *i);
    } else if (const bool* b = std::get_if<bool>(&v)) {
      glUniform1i(s.loc, *b ? 1 : 0);
    } else if (const Vector2* v2 = std::get_if<Vector2>(&v)) {
      glUniform2f(s.loc, static_cast<GLfloat>(v2->x), static_cast<GLfloat>(v2->y));
    } else if (const Vector3* v3 = std::get_if<Vector3>(&v)) {
      set3(s.loc, *v3);
    } else if (const Vector4* v4 = std::get_if<Vector4>(&v)) {
      glUniform4f(s.loc, static_cast<GLfloat>(v4->x), static_cast<GLfloat>(v4->y), static_cast<GLfloat>(v4->z), static_cast<GLfloat>(v4->w));
    } else if (const Color* c = std::get_if<Color>(&v)) {
      setColor(s.loc, *c);
    } else if (const Matrix3* m3 = std::get_if<Matrix3>(&v)) {
      setMat3(s.loc, *m3);
    } else if (const Matrix4* m4 = std::get_if<Matrix4>(&v)) {
      setMat4(s.loc, *m4);
    } else if (std::get_if<TexturePtr>(&v)) {
      // (a picture: bound for every draw – bindMaterialTextures)
    } else if (const std::vector<float>* a = std::get_if<std::vector<float>>(&v)) {
      int n = static_cast<int>(a->size());
      if (n == 0) return;
      switch (s.type) {
        case GL_FLOAT: glUniform1fv(s.loc, std::min(n, s.size), a->data()); break;
        case GL_FLOAT_VEC2: glUniform2fv(s.loc, std::min(n / 2, s.size), a->data()); break;
        case GL_FLOAT_VEC3: glUniform3fv(s.loc, std::min(n / 3, s.size), a->data()); break;
        case GL_FLOAT_VEC4: glUniform4fv(s.loc, std::min(n / 4, s.size), a->data()); break;
        case GL_FLOAT_MAT4: glUniformMatrix4fv(s.loc, std::min(n / 16, s.size), GL_FALSE, a->data()); break;
        default: break;
      }
    }
  }

  void setMaterialUniforms(Program& p, Material& m, int targetHeight) {
    for (const Slot& s : p.material) {
      switch (s.kind) {
        case U::Diffuse: setColor(s.loc, m.color); break;
        case U::Opacity: glUniform1f(s.loc, static_cast<GLfloat>(m.opacity)); break;
        case U::Emissive: setColor(s.loc, m.emissive, m.emissiveIntensity); break;
        case U::Map: break;   // (bound for every draw: bindMaterialTextures)
        case U::AlphaMap: break;
        case U::EmissiveMap: break;
        case U::EmissiveMapTransform:
          if (m.emissiveMap) {
            Matrix3 t;
            t.setUvTransform(m.emissiveMap->offset.x, m.emissiveMap->offset.y, m.emissiveMap->repeat.x, m.emissiveMap->repeat.y, 0, 0, 0);
            setMat3(s.loc, t);
          }
          break;
        case U::AlphaMapTransform:
          if (m.alphaMap) {
            Matrix3 t;
            t.setUvTransform(m.alphaMap->offset.x, m.alphaMap->offset.y, m.alphaMap->repeat.x, m.alphaMap->repeat.y, 0, 0, 0);
            setMat3(s.loc, t);
          }
          break;
        case U::MapTransform:
        case U::UvTransform:
          if (m.map) {
            Matrix3 t;
            t.setUvTransform(m.map->offset.x, m.map->offset.y, m.map->repeat.x, m.map->repeat.y, 0, 0, 0);
            setMat3(s.loc, t);
          }
          break;
        case U::AlphaTest: glUniform1f(s.loc, static_cast<GLfloat>(m.alphaTest)); break;
        case U::Specular: setColor(s.loc, m.specular); break;
        case U::Shininess: glUniform1f(s.loc, static_cast<GLfloat>(std::max(m.shininess, 1e-4))); break;
        case U::Size: glUniform1f(s.loc, static_cast<GLfloat>(m.size * r.pixelRatio_)); break;
        case U::Scale: glUniform1f(s.loc, static_cast<GLfloat>(targetHeight * 0.5)); break;
        case U::Rotation: glUniform1f(s.loc, static_cast<GLfloat>(m.rotation)); break;
        default: break;
      }
    }
    for (const Slot& s : p.custom) {
      const Uniform* u = nullptr;
      auto it = m.uniforms.find(s.name);
      if (it != m.uniforms.end()) u = it->second.get();
      else if (m.patch) {
        auto pt = m.patch->uniforms.find(s.name);
        if (pt != m.patch->uniforms.end()) u = pt->second.get();
      }
      if (u) setCustom(p, s, *u);
    }
  }

  /**
   * The material's pictures on their units.  For every draw, not once per material: in the
   * list of see-through things two materials of one program may take turns.  (Nearly always
   * the picture is the one that is bound already, and nothing is sent.)
   */
  void bindMaterialTextures(Program& p, Material& m) {
    if (m.map) useTexture(0, *m.map);
    if (m.alphaMap && p.alphaMapUnit >= 0) useTexture(p.alphaMapUnit, *m.alphaMap);
    if (m.emissiveMap && p.emissiveMapUnit >= 0) useTexture(p.emissiveMapUnit, *m.emissiveMap);
    for (const Slot& s : p.custom) {
      if (s.unit < 0) continue;
      const Uniform* u = nullptr;
      auto it = m.uniforms.find(s.name);
      if (it != m.uniforms.end()) u = it->second.get();
      else if (m.patch) {
        auto pt = m.patch->uniforms.find(s.name);
        if (pt != m.patch->uniforms.end()) u = pt->second.get();
      }
      if (!u) continue;
      if (const TexturePtr* t = std::get_if<TexturePtr>(&u->value))
        if (*t) useTexture(s.unit, **t);
    }
  }

  void setObjectUniforms(Program& p, Object3D& o) {
    Matrix4 mv;
    bool haveMv = false;
    for (const Slot& s : p.object) {
      if ((s.kind == U::ModelViewMatrix || s.kind == U::NormalMatrix) && !haveMv) {
        mv.multiplyMatrices(viewMatrix, o.matrixWorld);
        haveMv = true;
      }
      switch (s.kind) {
        case U::ModelMatrix: setMat4(s.loc, o.matrixWorld); break;
        case U::ModelViewMatrix: setMat4(s.loc, mv); break;
        case U::NormalMatrix: {
          Matrix3 n;
          n.getNormalMatrix(mv);
          setMat3(s.loc, n);
          break;
        }
        case U::ReceiveShadow: glUniform1i(s.loc, o.receiveShadow ? 1 : 0); break;
        case U::Center:
          if (o.type == ObjectType::Sprite) {
            const Vector2& c = static_cast<Sprite&>(o).center;
            glUniform2f(s.loc, static_cast<GLfloat>(c.x), static_cast<GLfloat>(c.y));
          }
          break;
        default: break;
      }
    }
  }

  // --- what is in the picture ---------------------------------------------------------
  bool inFrustum(Object3D& o, BufferGeometry& g, const Frustum& f) {
    if (!o.frustumCulled) return true;
    Sphere s;
    if (o.type == ObjectType::InstancedMesh) {
      auto& im = static_cast<InstancedMesh&>(o);
      if (!im.boundingSphere) im.computeBoundingSphere();
      s = *im.boundingSphere;
    } else if (o.type == ObjectType::Sprite) {
      s.center.set(0, 0, 0);
      s.radius = 0.7071067811865476;
    } else {
      if (!g.boundingSphere) g.computeBoundingSphere();
      s = *g.boundingSphere;
    }
    s.applyMatrix4(o.matrixWorld);
    return f.intersectsSphere(s);
  }

  void project(Object3D& o, const Camera& camera, bool collectCasters) {
    if (!o.visible) return;
    bool seen = (o.layers & camera.layers) != 0;
    switch (o.type) {
      case ObjectType::Light:
        if (seen) {
          Light& l = static_cast<Light&>(o);
          switch (l.lightType) {
            case LightType::Ambient:
              lights.ambient.r += l.color.r * l.intensity;
              lights.ambient.g += l.color.g * l.intensity;
              lights.ambient.b += l.color.b * l.intensity;
              break;
            case LightType::Hemisphere: lights.hemi.push_back(&l); break;
            case LightType::Directional: lights.dir.push_back(&l); break;
            case LightType::Spot: lights.spot.push_back(&l); break;
            case LightType::Point: lights.point.push_back(&l); break;
          }
        }
        break;
      case ObjectType::LOD: {
        LOD& lod = static_cast<LOD&>(o);
        if (lod.autoUpdate) lod.update(camera);
        break;
      }
      case ObjectType::Sprite:
        if (seen) {
          Sprite& s = static_cast<Sprite&>(o);
          if (s.material && s.material->visible && inFrustum(o, *spriteQuad, frustum)) push(o, *spriteQuad, *s.material, 0, 6);
          else if (s.material && s.material->visible) r.info.culled++;
        }
        break;
      case ObjectType::Mesh:
      case ObjectType::InstancedMesh:
      case ObjectType::Points:
      case ObjectType::Line:
      case ObjectType::LineSegments:
        if (seen) {
          Mesh& m = static_cast<Mesh&>(o);
          if (!m.geometry) break;
          BufferGeometry& g = *m.geometry;
          if (collectCasters && o.castShadow && (o.type == ObjectType::Mesh || o.type == ObjectType::InstancedMesh)) {
            Material* cm = m.material ? m.material.get() : (!m.materials.empty() ? m.materials[0].get() : nullptr);
            if (cm && cm->visible) casters.push_back(Item{&o, &g, cm, 0, 0, 0, 0, nullptr});
          }
          if (!inFrustum(o, g, frustum)) {
            r.info.culled++;
            break;
          }
          int total = g.index ? static_cast<int>(g.index->count()) : (g.position() ? static_cast<int>(g.position()->count()) : 0);
          int ds = std::max(0, g.drawRange.start);
          int de = g.drawRange.count == INT_MAX ? total : std::min(total, g.drawRange.start + g.drawRange.count);
          if (!m.materials.empty()) {
            for (const BufferGeometry::Group& grp : g.groups) {
              if (grp.materialIndex < 0 || grp.materialIndex >= static_cast<int>(m.materials.size())) continue;
              Material* gm = m.materials[static_cast<size_t>(grp.materialIndex)].get();
              if (!gm || !gm->visible) continue;
              int s = std::max(ds, grp.start), e = std::min(de, grp.start + grp.count);
              if (e > s) push(o, g, *gm, s, e - s);
            }
          } else if (m.material && m.material->visible && de > ds) push(o, g, *m.material, ds, de - ds);
        }
        break;
      default: break;
    }
    for (const ObjectPtr& c : o.children) project(*c, camera, collectCasters);
  }

  void push(Object3D& o, BufferGeometry& g, Material& m, int start, int count) {
    // three.js sorts by the depth of the object's origin in the picture
    const double* e = o.matrixWorld.e;
    const double* ps = projScreen.e;
    double x = e[12], y = e[13], zz = e[14];
    double w = ps[3] * x + ps[7] * y + ps[11] * zz + ps[15];
    double z = (ps[2] * x + ps[6] * y + ps[10] * zz + ps[14]) / (w != 0 ? w : 1);
    (m.transparent ? transparent : opaque).push_back(Item{&o, &g, &m, start, count, o.renderOrder, z, nullptr});
  }

  // --- drawing ---------------------------------------------------------------------------
  void drawCall(Item& it, GLenum mode) {
    BufferGeometry& g = *it.geometry;
    const bool instanced = it.object->type == ObjectType::InstancedMesh;
    int instances = 1;
    if (instanced) {
      auto& im = static_cast<InstancedMesh&>(*it.object);
      useInstances(im);
      instances = im.count;
      if (instances <= 0) return;
    } else if (g.instanceCount >= 0) instances = g.instanceCount;
    const bool geoInstanced = !instanced && g.instanceCount >= 0;
    if (g.index) {
      GLenum type = g.index->type == AttrType::U32 ? GL_UNSIGNED_INT : g.index->type == AttrType::U8 ? GL_UNSIGNED_BYTE : GL_UNSIGNED_SHORT;
      size_t bytes = g.index->type == AttrType::U32 ? 4 : g.index->type == AttrType::U8 ? 1 : 2;
      const void* offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(it.start) * bytes);
      if (instanced || geoInstanced) glDrawElementsInstanced(mode, it.count, type, offset, instances);
      else glDrawElements(mode, it.count, type, offset);
    } else {
      if (instanced || geoInstanced) glDrawArraysInstanced(mode, it.start, it.count, instances);
      else glDrawArrays(mode, it.start, it.count);
    }
    r.info.calls++;
    if (mode == GL_TRIANGLES) r.info.triangles += it.count / 3 * instances;
    else if (mode == GL_POINTS) r.info.points += it.count * instances;
    else r.info.lines += (mode == GL_LINES ? it.count / 2 : std::max(0, it.count - 1)) * instances;
  }

  void drawItems(std::vector<Item>& items, Camera& camera, int targetHeight) {
    for (Item& it : items) {
      Object3D& o = *it.object;
      Material& m = *it.material;
      Program& p = *it.program;
      if (!p.ok) continue;
      if (o.onBeforeRender) o.onBeforeRender(r, *scene, camera);
      if (m.onBeforeRender) m.onBeforeRender(r, *scene, camera);
      useProgram(p);
      if (p.frameStamp != frameStamp) {
        p.frameStamp = frameStamp;
        p.materialId = 0;
        setFrameUniforms(p);
      }
      if (p.materialId != m.id || p.materialStamp != frameStamp) {
        p.materialId = m.id;
        p.materialStamp = frameStamp;
        setMaterialUniforms(p, m, targetHeight);
      }
      bindMaterialTextures(p, m);
      setObjectUniforms(p, o);
      const bool mirrored = (o.type == ObjectType::Mesh || o.type == ObjectType::InstancedMesh) && o.matrixWorld.determinant() < 0;
      setSide(m.side, mirrored);
      setBlend(m.blending, m.transparent);
      setDepth(m.depthTest, m.depthWrite);
      setColorWrite(m.colorWrite);
      setPolygonOffset(m.polygonOffset, m.polygonOffsetFactor, m.polygonOffsetUnits);
      useGeometry(*it.geometry, it.object, p);
      GLenum mode = o.type == ObjectType::Points ? GL_POINTS : o.type == ObjectType::LineSegments ? GL_LINES : o.type == ObjectType::Line ? GL_LINE_STRIP : GL_TRIANGLES;
      drawCall(it, mode);
      if (o.onAfterRender) o.onAfterRender(r, *scene, camera);
    }
  }

  // --- the shadow map ------------------------------------------------------------------------
  void renderShadow(Light& light, const Camera& viewCamera) {
    (void)viewCamera;
    Light::Shadow& sh = light.shadow;
    if (!sh.camera) return;
    int size = std::max(16, static_cast<int>(sh.mapSize.x));
    GLint maxSize = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    size = std::min(size, static_cast<int>(maxSize));
    if (!sh.map) {
      sh.map = std::make_shared<RenderTarget>(size, size, false);
      sh.needsUpdate = true;
    } else if (sh.map->width != size) {
      sh.map->setSize(size, size);
      sh.needsUpdate = true;
    }
    // three.js LightShadow.updateMatrices: the shadow camera stands at the light and looks at its target
    Camera& cam = *sh.camera;
    Vector3 lp = worldPositionOf(light);
    Vector3 tp = light.target ? worldPositionOf(*light.target) : Vector3(0, 0, 0);
    cam.position.copy(lp);
    cam.lookAt(tp);
    cam.updateMatrixWorld();
    Matrix4 ps;
    ps.multiplyMatrices(cam.projectionMatrix, cam.matrixWorldInverse);
    Matrix4 bias;
    bias.set(0.5, 0, 0, 0.5, 0, 0.5, 0, 0.5, 0, 0, 0.5, 0.5, 0, 0, 0, 1);
    sh.matrix.multiplyMatrices(bias, ps);
    if (!sh.autoUpdate && !sh.needsUpdate) return;
    sh.needsUpdate = false;

    Frustum sf;
    sf.setFromProjectionMatrix(ps);
    setupTarget(*sh.map, true);
    glBindFramebuffer(GL_FRAMEBUFFER, sh.map->gpuFbo);
    glViewport(0, 0, size, size);
    glDisable(GL_SCISSOR_TEST);
    setDepth(true, true);
    setColorWrite(false);
    setBlend(Blending::None, false);
    setPolygonOffset(false, 0, 0);
    glClear(GL_DEPTH_BUFFER_BIT);
    for (Item& it : casters) {
      Object3D& o = *it.object;
      if (!inFrustum(o, *it.geometry, sf)) continue;
      Material& m = *it.material;
      const bool instanced = o.type == ObjectType::InstancedMesh;
      const bool map = m.map != nullptr && m.alphaTest > 0 && it.geometry->uv() != nullptr;
      Program& p = *depthProgram(instanced, map);
      if (!p.ok) continue;
      useProgram(p);
      for (const Slot& s : p.frame)
        if (s.kind == U::LightMatrix) setMat4(s.loc, ps);
      for (const Slot& s : p.object)
        if (s.kind == U::ModelMatrix) setMat4(s.loc, o.matrixWorld);
      if (map) {
        for (const Slot& s : p.material) {
          if (s.kind == U::Map) useTexture(0, *m.map);
          else if (s.kind == U::AlphaTest) glUniform1f(s.loc, static_cast<GLfloat>(m.alphaTest));
        }
      }
      // three.js: the back of a front-sided material is what is drawn into the shadow map
      Side side = m.shadowSide ? *m.shadowSide : (m.side == Side::Front ? Side::Back : m.side == Side::Back ? Side::Front : Side::Double);
      setSide(side, o.matrixWorld.determinant() < 0);
      useGeometry(*it.geometry, it.object, p);
      BufferGeometry& g = *it.geometry;
      int total = g.index ? static_cast<int>(g.index->count()) : (g.position() ? static_cast<int>(g.position()->count()) : 0);
      it.start = std::max(0, g.drawRange.start);
      it.count = (g.drawRange.count == INT_MAX ? total : std::min(total, g.drawRange.start + g.drawRange.count)) - it.start;
      if (it.count > 0) drawCall(it, GL_TRIANGLES);
    }
    setColorWrite(true);
  }
};

// ---------------------------------------------------------------------------
// The renderer
// ---------------------------------------------------------------------------

Renderer::Renderer() : d_(std::make_unique<Impl>(*this)) {}
Renderer::~Renderer() = default;

bool Renderer::init() {
  available_ = plat::gl::hasContext();
  if (!available_) return false;
  d_->resetState();
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  // the quad every sprite is drawn with (three.js Sprite: -0.5 … 0.5, two triangles)
  auto quad = std::make_shared<BufferGeometry>();
  quad->setAttribute("position", attr(std::vector<float>{-0.5f, -0.5f, 0, 0.5f, -0.5f, 0, 0.5f, 0.5f, 0, -0.5f, 0.5f, 0}, 3));
  quad->setAttribute("uv", attr(std::vector<float>{0, 0, 1, 0, 1, 1, 0, 1}, 2));
  quad->setIndex(std::vector<uint32_t>{0, 1, 2, 0, 2, 3});
  d_->spriteQuad = quad;
  return true;
}

void Renderer::setSize(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  if (available_) {
    plat::gl::resizeDrawingBuffer(bufferWidth(), bufferHeight());
    d_->resetState();   // (the host may have bound things of its own meanwhile)
  }
}

void Renderer::setPixelRatio(double ratio) {
  pixelRatio_ = ratio > 0 ? ratio : 1;
  if (available_) {
    plat::gl::resizeDrawingBuffer(bufferWidth(), bufferHeight());
    d_->resetState();
  }
}

int Renderer::bufferWidth() const { return std::max(1, static_cast<int>(std::floor(width_ * pixelRatio_))); }
int Renderer::bufferHeight() const { return std::max(1, static_cast<int>(std::floor(height_ * pixelRatio_))); }

void Renderer::setRenderTarget(const RenderTargetPtr& target) { target_ = target; }

// (in CSS pixels, as three.js: multiplied by the pixel ratio and rounded for the drawing buffer)
void Renderer::setViewport(int x, int y, int width, int height) {
  d_->viewportSet = true;
  d_->vp[0] = static_cast<int>(std::lround(x * pixelRatio_));
  d_->vp[1] = static_cast<int>(std::lround(y * pixelRatio_));
  d_->vp[2] = static_cast<int>(std::lround(width * pixelRatio_));
  d_->vp[3] = static_cast<int>(std::lround(height * pixelRatio_));
}
void Renderer::resetViewport() { d_->viewportSet = false; }
void Renderer::setScissor(int x, int y, int width, int height) {
  d_->scissor[0] = static_cast<int>(std::lround(x * pixelRatio_));
  d_->scissor[1] = static_cast<int>(std::lround(y * pixelRatio_));
  d_->scissor[2] = static_cast<int>(std::lround(width * pixelRatio_));
  d_->scissor[3] = static_cast<int>(std::lround(height * pixelRatio_));
}
void Renderer::setScissorTest(bool on) { d_->scissorTest = on; }

void Renderer::setClearColor(const Color& color, double alpha) {
  d_->clearColor = color;
  d_->clearAlpha = alpha;
}

void Renderer::clear(bool color, bool depth) {
  if (!available_) return;
  int w = 0, h = 0;
  d_->bindTarget(w, h);
  if (d_->scissorTest) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(d_->scissor[0], d_->scissor[1], d_->scissor[2], d_->scissor[3]);
  } else glDisable(GL_SCISSOR_TEST);
  const Color& c = d_->scene && d_->scene->background ? *d_->scene->background : d_->clearColor;
  // (as three.js: the colour is given in the output's colour space)
  if (!target_) glClearColor(static_cast<GLfloat>(linearToSrgb(c.r)), static_cast<GLfloat>(linearToSrgb(c.g)), static_cast<GLfloat>(linearToSrgb(c.b)), static_cast<GLfloat>(d_->clearAlpha));
  else glClearColor(static_cast<GLfloat>(c.r), static_cast<GLfloat>(c.g), static_cast<GLfloat>(c.b), static_cast<GLfloat>(d_->clearAlpha));
  d_->setDepth(true, true);
  d_->setColorWrite(true);
  glClear((color ? GL_COLOR_BUFFER_BIT : 0u) | (depth ? GL_DEPTH_BUFFER_BIT : 0u));
}

void Renderer::render(Scene& scene, Camera& camera) {
  if (!available_) return;
  Impl& d = *d_;
  d.freeReleased();
  if (info.autoReset && !target_) info.reset();
  d.scene = &scene;
  d.frameStamp++;

  if (scene.matrixWorldAutoUpdate) scene.updateMatrixWorld();
  if (!camera.parent && camera.matrixWorldAutoUpdate) camera.updateMatrixWorld();
  d.viewMatrix = camera.matrixWorldInverse;
  d.projectionMatrix = camera.projectionMatrix;
  d.cameraPosition.setFromMatrixPosition(camera.matrixWorld);
  d.cameraOrtho = !camera.isPerspective;
  d.projScreen.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
  d.frustum.setFromProjectionMatrix(d.projScreen);

  // what there is to draw, and by which lights
  d.opaque.clear();
  d.transparent.clear();
  d.casters.clear();
  d.lights.clear();
  d.project(scene, camera, shadowMap.enabled);
  // (the lights that cast a shadow come first, as three.js sorts them)
  std::stable_sort(d.lights.dir.begin(), d.lights.dir.end(), [](const Light* a, const Light* b) { return a->castShadow && !b->castShadow; });
  d.lights.dirShadows = 0;
  if (shadowMap.enabled)
    for (Light* l : d.lights.dir)
      if (l->castShadow && l->shadow.camera) d.lights.dirShadows++;
  // (one shadow map is drawn: the sun's)
  if (d.lights.dirShadows > 1) d.lights.dirShadows = 1;

  // (shadowMap.autoUpdate false: the map of the last picture is used again – a second view of the same frame)
  if (d.lights.dirShadows > 0 && (shadowMap.autoUpdate || !d.lights.dir[0]->shadow.map)) d.renderShadow(*d.lights.dir[0], camera);

  int w = 0, h = 0;
  d.bindTarget(w, h);
  if (d.viewportSet && !target_) glViewport(d.vp[0], d.vp[1], d.vp[2], d.vp[3]);
  else glViewport(0, 0, w, h);
  if (autoClear) clear(true, true);
  else if (d.scissorTest) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(d.scissor[0], d.scissor[1], d.scissor[2], d.scissor[3]);
  } else glDisable(GL_SCISSOR_TEST);

  // the program of each item, then the order: three.js's painter sort, with the
  // program first among equals so that it changes as rarely as possible
  for (Item& it : d.opaque) it.program = d.programFor(*it.material, *it.object, *it.geometry);
  for (Item& it : d.transparent) it.program = d.programFor(*it.material, *it.object, *it.geometry);
  std::stable_sort(d.opaque.begin(), d.opaque.end(), [](const Item& a, const Item& b) {
    if (a.renderOrder != b.renderOrder) return a.renderOrder < b.renderOrder;
    if (a.program != b.program) return a.program < b.program;
    if (a.material->id != b.material->id) return a.material->id < b.material->id;
    if (a.z != b.z) return a.z < b.z;
    return a.object->id < b.object->id;
  });
  std::stable_sort(d.transparent.begin(), d.transparent.end(), [](const Item& a, const Item& b) {
    if (a.renderOrder != b.renderOrder) return a.renderOrder < b.renderOrder;
    if (a.z != b.z) return a.z > b.z;
    return a.object->id < b.object->id;
  });

  // (what a PointsMaterial's size is measured against: the picture's height as three.js takes it)
  int th = target_ ? h : height_;
  d.drawItems(d.opaque, camera, th);
  d.drawItems(d.transparent, camera, th);

  // (leave the state as the next clear expects it)
  d.setDepth(true, true);
  d.setColorWrite(true);
  if (target_ && target_->texture && target_->texture->generateMipmaps && usesMipmaps(target_->texture->minFilter)) {
    d.bindTexture(0, target_->texture->gpu);
    glGenerateMipmap(GL_TEXTURE_2D);
  }
}

void Renderer::compile(Scene& scene, Camera& camera) {
  if (!available_) return;
  Impl& d = *d_;
  d.scene = &scene;
  if (scene.matrixWorldAutoUpdate) scene.updateMatrixWorld();
  if (!camera.parent) camera.updateMatrixWorld();
  d.projScreen.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
  d.frustum.setFromProjectionMatrix(d.projScreen);
  d.opaque.clear();
  d.transparent.clear();
  d.casters.clear();
  d.lights.clear();
  d.project(scene, camera, false);
  d.lights.dirShadows = 0;
  if (shadowMap.enabled)
    for (Light* l : d.lights.dir)
      if (l->castShadow && l->shadow.camera) d.lights.dirShadows = 1;
  for (Item& it : d.opaque) d.programFor(*it.material, *it.object, *it.geometry);
  for (Item& it : d.transparent) d.programFor(*it.material, *it.object, *it.geometry);
}

void Renderer::initTexture(Texture& texture) {
  if (available_) d_->useTexture(0, texture);
}

int Renderer::getMaxAnisotropy() {
  if (!available_) return 1;
  if (d_->maxAnisotropy < 0) {
    d_->maxAnisotropy = 1;
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (ext && std::strstr(ext, "texture_filter_anisotropic")) {
      GLfloat v = 1;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &v);
      d_->maxAnisotropy = static_cast<int>(v);
    }
  }
  return d_->maxAnisotropy;
}

void Renderer::readPixels(int x, int y, int width, int height, uint8_t* out) {
  if (!available_) return;
  int w = 0, h = 0;
  d_->bindTarget(w, h);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

void Renderer::copyFramebufferToTexture(Texture& texture, int x, int y) {
  if (!available_) return;
  int w = 0, h = 0;
  d_->bindTarget(w, h);
  if (!texture.gpu) {
    GLuint id = 0;
    glGenTextures(1, &id);
    texture.gpu = id;
    info.textures++;
  }
  d_->bindTexture(0, texture.gpu);
  int tw = texture.width > 0 ? texture.width : w, th = texture.height > 0 ? texture.height : h;
  glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, x, y, tw, th, 0);
  texture.width = tw;
  texture.height = th;
  texture.isRenderTarget = true;
  bool mips = texture.generateMipmaps && usesMipmaps(texture.minFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(mips ? glFilter(texture.minFilter) : GL_LINEAR));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (mips) glGenerateMipmap(GL_TEXTURE_2D);
}

#else  // no OpenGL in this build: the renderer is there and draws nothing

struct Renderer::Impl {};
Renderer::Renderer() = default;
Renderer::~Renderer() = default;
bool Renderer::init() { return false; }
void Renderer::setSize(int width, int height) {
  width_ = width;
  height_ = height;
}
void Renderer::setPixelRatio(double ratio) { pixelRatio_ = ratio; }
int Renderer::bufferWidth() const { return static_cast<int>(width_ * pixelRatio_); }
int Renderer::bufferHeight() const { return static_cast<int>(height_ * pixelRatio_); }
void Renderer::render(Scene&, Camera&) {}
void Renderer::setRenderTarget(const RenderTargetPtr& target) { target_ = target; }
void Renderer::setViewport(int, int, int, int) {}
void Renderer::resetViewport() {}
void Renderer::setScissor(int, int, int, int) {}
void Renderer::setScissorTest(bool) {}
void Renderer::clear(bool, bool) {}
void Renderer::setClearColor(const Color&, double) {}
int Renderer::getMaxAnisotropy() { return 1; }
void Renderer::compile(Scene&, Camera&) {}
void Renderer::initTexture(Texture&) {}
void Renderer::readPixels(int, int, int, int, uint8_t*) {}
void Renderer::copyFramebufferToTexture(Texture&, int, int) {}

#endif

}  // namespace fs
