// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/water.ts @ v1.7.7
#include "render/water.h"

#include <algorithm>
#include <cmath>

#include "engine/renderer.h"
#include "platform/plat.h"
#include "render/glsl_gen.h"

namespace fs {

// ------------------------------------------------------------------ the water in the materials

void WaterLook::attachSky(const Object3D* dome) {
  if (!dome || dome->type != ObjectType::Mesh) return;
  const MaterialPtr& mat = static_cast<const Mesh*>(dome)->material;
  if (!mat || mat->type != MaterialType::Shader) return;
  const auto& u = mat->uniforms;
  const auto find = [&u](const char* name) -> UniformPtr {
    auto it = u.find(name);
    return it != u.end() ? it->second : nullptr;
  };
  UniformPtr sunDir = find("uSunDir");
  UniformPtr zenith = find("uZenith");
  UniformPtr horizon = find("uHorizon");
  UniformPtr sunColor = find("uSunColor");
  if (!sunDir || !zenith || !horizon || !sunColor) return;
  uniforms.uFsWtrSunDir = sunDir;
  uniforms.uFsWtrZenith = zenith;
  uniforms.uFsWtrHorizon = horizon;
  uniforms.uFsWtrSunColor = sunColor;
}

void WaterLook::apply(const Patched& p, ShaderPatch& out) const {
  out = p.plain;
  if (!on_) return;
  // the program's key: the material's own one and ours – the name of the shader the original's patch leaves
  const std::string name = p.plain.key + "|fs-water-" + p.target;
  ShaderPatchPtr water = gamePatch(name.c_str());
  if (!water) {
    plat::warn("water: no shader " + name);
    return;
  }
  water->uniforms = p.plain.uniforms;
  water->uniforms["uFsWtrTime"] = uniforms.uFsWtrTime;
  water->uniforms["uFsWtrZenith"] = uniforms.uFsWtrZenith;
  water->uniforms["uFsWtrHorizon"] = uniforms.uFsWtrHorizon;
  water->uniforms["uFsWtrSunDir"] = uniforms.uFsWtrSunDir;
  water->uniforms["uFsWtrSunColor"] = uniforms.uFsWtrSunColor;
  out = *water;
}

void WaterLook::enabled(bool on) {
  if (on == on_) return;
  on_ = on;
  size_t k = 0;
  for (size_t i = 0; i < mats_.size(); i++) {
    ShaderPatchPtr mine = mats_[i].mine.lock();
    // (gone with its material – or the material has another patch now)
    if (!mine) continue;
    apply(mats_[i], *mine);
    mats_[i].material->needsUpdate();
    if (k != i) mats_[k] = std::move(mats_[i]);
    k++;
  }
  mats_.resize(k);
}

void WaterLook::update(double dt) {
  if (std::isfinite(dt) && dt > 0) {
    double& t = std::get<double>(uniforms.uFsWtrTime->value);
    t = std::fmod(t + std::min(dt, 0.25), 3600);
  }
}

void WaterLook::patch(Material& material, const WaterTarget& target) {
  // (once: a material whose patch is one of ours keeps it)
  for (const Patched& p : mats_)
    if (!p.mine.expired() && p.mine.lock() == material.patch) return;
  Patched p;
  p.material = &material;
  if (material.patch) p.plain = *material.patch;
  p.target = target;
  auto mine = std::make_shared<ShaderPatch>();
  apply(p, *mine);
  p.mine = mine;
  material.patch = mine;
  material.needsUpdate();
  mats_.push_back(std::move(p));
}

// -------------------------------------------------------------- the spray

namespace {

const char* const SPRAY_VERT = R"GLSL(
attribute float aSize;
attribute float aAlpha;
uniform float uViewH;
varying float vAlpha;
#include <fog_pars_vertex>
void main() {
  vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
  gl_Position = projectionMatrix * mvPosition;
  gl_PointSize = clamp(aSize * projectionMatrix[1][1] * 0.5 * uViewH / max(-mvPosition.z, 0.1), 0.0, 96.0);
  vAlpha = aAlpha;
  #include <fog_vertex>
})GLSL";

const char* const SPRAY_FRAG = R"GLSL(
uniform vec3 uColor;
varying float vAlpha;
#include <fog_pars_fragment>
void main() {
  vec2 d = gl_PointCoord * 2.0 - 1.0;
  float r = dot(d, d);
  if (r > 1.0 || vAlpha <= 0.0) discard;
  gl_FragColor = vec4(uColor, vAlpha * (1.0 - r));
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
  #include <fog_fragment>
})GLSL";

/** gravity on the drops (m/s²) and how quickly the air slows them (1/s) */
constexpr double GRAVITY = 9.81;
constexpr double AIR_DRAG = 1.2;

}  // namespace

WaterSpray::WaterSpray(int capacityIn) : capacity(capacityIn) {
  pool_.resize(static_cast<size_t>(capacity));
  pos_ = attr(std::vector<float>(static_cast<size_t>(capacity) * 3, 0.0f), 3);
  size_ = attr(std::vector<float>(static_cast<size_t>(capacity), 0.0f), 1);
  alpha_ = attr(std::vector<float>(static_cast<size_t>(capacity), 0.0f), 1);
  pos_->setDynamic();
  size_->setDynamic();
  alpha_->setDynamic();
  geo_ = make<BufferGeometry>();
  geo_->setAttribute("position", pos_);
  geo_->setAttribute("aSize", size_);
  geo_->setAttribute("aAlpha", alpha_);
  geo_->setDrawRange(0, 0);
  // (fogDensity, fogNear, fogFar, fogColor: the renderer's own, as for every material with fog)
  mat_ = shaderMaterial(SPRAY_VERT, SPRAY_FRAG, {{"uViewH", uViewH_}, {"uColor", uColor_}}, {.transparent = true, .depthWrite = false, .fog = true});
  points = make<Points>(geo_, mat_);
  points->name = "water spray";
  points->frustumCulled = false;
  points->renderOrder = 2;
  UniformPtr viewH = uViewH_;
  points->onBeforeRender = [viewH](Renderer& renderer, Scene&, Camera&) { viewH->value = static_cast<double>(renderer.bufferHeight()); };
}

void WaterSpray::emit(const SprayParticle& p) {
  SprayParticle* slot;
  if (live_ < capacity) slot = &pool_[static_cast<size_t>(live_++)];
  else {
    slot = &pool_[static_cast<size_t>(next_)];
    next_ = (next_ + 1) % capacity;
  }
  *slot = p;
}

void WaterSpray::update(double dt) {
  const double h = std::isfinite(dt) && dt > 0 ? std::min(dt, 0.1) : 0;
  int n = 0;
  for (int i = 0; i < live_; i++) {
    SprayParticle& p = pool_[static_cast<size_t>(i)];
    p.age += h;
    if (p.age < p.life && !(!p.foam && p.vy < 0 && p.y < p.floor)) {
      if (p.foam) {
        const double k = std::exp(-h * 0.8);
        p.vx *= k;
        p.vz *= k;
      } else {
        const double k = std::exp(-h * AIR_DRAG);
        p.vx *= k;
        p.vz *= k;
        p.vy = p.vy * k - GRAVITY * h;
      }
      p.x += p.vx * h;
      p.y += p.vy * h;
      p.z += p.vz * h;
      // (keep the living ones together at the front of the pool)
      if (n != i) std::swap(pool_[static_cast<size_t>(i)], pool_[static_cast<size_t>(n)]);
      n++;
    }
  }
  live_ = n;
  if (next_ >= n) next_ = 0;
  std::vector<float>& pos = pos_->f32;
  std::vector<float>& size = size_->f32;
  std::vector<float>& alpha = alpha_->f32;
  for (size_t i = 0; i < static_cast<size_t>(n); i++) {
    const SprayParticle& p = pool_[i];
    const double t = p.age / p.life;
    pos[i * 3] = static_cast<float>(p.x);
    pos[i * 3 + 1] = static_cast<float>(p.y);
    pos[i * 3 + 2] = static_cast<float>(p.z);
    size[i] = static_cast<float>(p.size0 + (p.size1 - p.size0) * t);
    alpha[i] = static_cast<float>((p.foam ? 0.35 : 0.55) * std::min(1.0, t * 8) * (1 - t));
  }
  std::get<Color>(uColor_->value).setRGB(0.85, 0.9, 0.93).multiplyScalar(std::max(0.08, std::min(1.2, light)));
  geo_->setDrawRange(0, n);
  for (const AttrPtr& a : {pos_, size_, alpha_}) {
    a->clearUpdateRanges();
    a->addUpdateRange(0, n * a->itemSize);
    a->needsUpdate();
  }
  points->visible = n > 0;
}

void WaterSpray::clear() {
  live_ = 0;
  next_ = 0;
  geo_->setDrawRange(0, 0);
  points->visible = false;
}

void WaterSpray::dispose() {}

}  // namespace fs
