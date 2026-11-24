// SPDX-License-Identifier: AGPL-3.0-only
// Origin: three.js r187 src/textures/{Texture,CanvasTexture,DataTexture}.js, src/core/RenderTarget.js,
//         src/materials/{Material,MeshBasicMaterial,MeshLambertMaterial,MeshPhongMaterial,LineBasicMaterial,
//         SpriteMaterial,PointsMaterial,ShaderMaterial}.js (the constructors' defaults, copy)
#include "engine/engine.h"
#include "engine/gpu.h"

namespace fs {

// ---------------------------------------------------------------------------
// Texture
// ---------------------------------------------------------------------------

Texture::Texture() : id(gpu::nextId()) {}

Texture::~Texture() { gpu::release(gpu::Kind::Texture, gpu); }

void Texture::dispose() {
  gpu::release(gpu::Kind::Texture, gpu);
  gpu = 0;
  gpuVersion = 0xffffffffu;
}

TexturePtr canvasTexture(const Canvas& canvas) {
  auto texture = std::make_shared<Texture>();
  texture->canvas = canvas;
  if (canvas) {
    texture->width = canvas.width();
    texture->height = canvas.height();
  }
  // (Texture's defaults: mipmaps, linear filters, the first row the top of the picture)
  texture->needsUpdate();
  return texture;
}

TexturePtr dataTexture(std::vector<uint8_t> data, int width, int height, PixelFormat format) {
  auto texture = std::make_shared<Texture>();
  texture->data = std::make_shared<std::vector<uint8_t>>(std::move(data));
  texture->width = width;
  texture->height = height;
  texture->format = format;
  texture->magFilter = Filter::Nearest;
  texture->minFilter = Filter::Nearest;
  texture->generateMipmaps = false;
  texture->flipY = false;
  // (as in three.js the caller says `needsUpdate()` when the pixels are ready)
  return texture;
}

// ---------------------------------------------------------------------------
// RenderTarget
// ---------------------------------------------------------------------------

RenderTarget::RenderTarget(int w, int h, bool depth) : width(w), height(h), depthBuffer(depth) {
  texture = std::make_shared<Texture>();
  texture->width = w;
  texture->height = h;
  texture->isRenderTarget = true;
  // _setTextureOptions
  texture->minFilter = Filter::Linear;
  texture->generateMipmaps = false;
  texture->flipY = false;
}

RenderTarget::~RenderTarget() {
  gpu::release(gpu::Kind::Framebuffer, gpuFbo);
  gpu::release(gpu::Kind::Framebuffer, gpuMsFbo);
  gpu::release(gpu::Kind::Renderbuffer, gpuDepth);
  gpu::release(gpu::Kind::Renderbuffer, gpuMsColor);
}

void RenderTarget::setSize(int w, int h) {
  if (width != w || height != h) {
    width = w;
    height = h;
    if (texture) {
      texture->width = w;
      texture->height = h;
    }
    dispose();
  }
}

// dispose(): the framebuffers, the renderbuffers and the texture they draw into are handed back; the
// renderer makes them anew when the target is next drawn to
void RenderTarget::dispose() {
  gpu::release(gpu::Kind::Framebuffer, gpuFbo);
  gpu::release(gpu::Kind::Framebuffer, gpuMsFbo);
  gpu::release(gpu::Kind::Renderbuffer, gpuDepth);
  gpu::release(gpu::Kind::Renderbuffer, gpuMsColor);
  gpuFbo = gpuMsFbo = gpuDepth = gpuMsColor = 0;
  gpuW = gpuH = 0;
  if (texture) texture->dispose();
}

// ---------------------------------------------------------------------------
// Material
// ---------------------------------------------------------------------------

Material::Material(MaterialType t) : id(gpu::nextId()), type(t) {
  // what the classes of three.js set in their constructors
  specular.setHex(0x111111);
  switch (t) {
    case MaterialType::Sprite:
      transparent = true;
      break;
    case MaterialType::Shader:
    case MaterialType::Depth:
      fog = false;
      break;
    default:
      break;
  }
}

Material::Material(MaterialType t, const MaterialParams& p) : Material(t) {
  // setValues
  if (p.color) color.copy(*p.color);
  map = p.map;
  alphaMap = p.alphaMap;
  emissiveMap = p.emissiveMap;
  vertexColors = p.vertexColors;
  if (p.transparent) transparent = *p.transparent;
  opacity = p.opacity;
  side = p.side;
  depthWrite = p.depthWrite;
  depthTest = p.depthTest;
  if (p.fog) fog = *p.fog;
  toneMapped = p.toneMapped;
  flatShading = p.flatShading;
  alphaTest = p.alphaTest;
  blending = p.blending;
  polygonOffset = p.polygonOffset;
  polygonOffsetFactor = p.polygonOffsetFactor;
  polygonOffsetUnits = p.polygonOffsetUnits;
  if (p.emissive) emissive.copy(*p.emissive);
  emissiveIntensity = p.emissiveIntensity;
  if (p.specular) specular.copy(*p.specular);
  shininess = p.shininess;
  colorWrite = p.colorWrite;
  name = p.name;
}

Material::~Material() { gpu::releasePrograms(gpuPrograms); }

// clone(): `new this.constructor().copy(this)` – every property, the uniforms' values copied (a texture
// stays the same texture); as in three.js not what onBeforeCompile was set to: the copy has no `patch`
std::shared_ptr<Material> Material::clone() const {
  auto m = std::make_shared<Material>(type);
  m->name = name;
  m->color.copy(color);
  m->map = map;
  m->alphaMap = alphaMap;
  m->emissiveMap = emissiveMap;
  m->vertexColors = vertexColors;
  m->transparent = transparent;
  m->opacity = opacity;
  m->side = side;
  m->shadowSide = shadowSide;
  m->depthWrite = depthWrite;
  m->depthTest = depthTest;
  m->fog = fog;
  m->toneMapped = toneMapped;
  m->flatShading = flatShading;
  m->visible = visible;
  m->colorWrite = colorWrite;
  m->alphaTest = alphaTest;
  m->blending = blending;
  m->polygonOffset = polygonOffset;
  m->polygonOffsetFactor = polygonOffsetFactor;
  m->polygonOffsetUnits = polygonOffsetUnits;
  m->emissive.copy(emissive);
  m->emissiveMap = emissiveMap;
  m->emissiveIntensity = emissiveIntensity;
  m->specular.copy(specular);
  m->shininess = shininess;
  m->size = size;
  m->sizeAttenuation = sizeAttenuation;
  m->rotation = rotation;
  m->vertexShader = vertexShader;
  m->fragmentShader = fragmentShader;
  // cloneUniforms
  for (const auto& entry : uniforms) {
    m->uniforms[entry.first] = entry.second ? std::make_shared<Uniform>(*entry.second) : nullptr;
  }
  m->defines = defines;
  return m;
}

MaterialPtr lambert(const MaterialParams& p) { return std::make_shared<Material>(MaterialType::Lambert, p); }
MaterialPtr basic(const MaterialParams& p) { return std::make_shared<Material>(MaterialType::Basic, p); }
MaterialPtr phong(const MaterialParams& p) { return std::make_shared<Material>(MaterialType::Phong, p); }
MaterialPtr lineBasic(const MaterialParams& p) { return std::make_shared<Material>(MaterialType::Line, p); }
MaterialPtr spriteMaterial(const MaterialParams& p) { return std::make_shared<Material>(MaterialType::Sprite, p); }
MaterialPtr pointsMaterial(const MaterialParams& p) { return std::make_shared<Material>(MaterialType::Points, p); }

MaterialPtr shaderMaterial(const std::string& vertexShader, const std::string& fragmentShader, std::map<std::string, UniformPtr> uniforms,
                           const MaterialParams& p) {
  auto m = std::make_shared<Material>(MaterialType::Shader, p);
  m->vertexShader = vertexShader;
  m->fragmentShader = fragmentShader;
  m->uniforms = std::move(uniforms);
  return m;
}

}  // namespace fs
