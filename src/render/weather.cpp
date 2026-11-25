// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/weather.ts @ v1.7.7
#include "render/weather.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

#include "core/js.h"
#include "engine/renderer.h"
#include "platform/plat.h"
#include "render/glsl_gen.h"
#include "render/sky.h"

namespace fs {

int WEATHER_PARTICLES(const WeatherQuality& quality) {
  if (quality == "low") return 600;
  if (quality == "high") return 6000;
  if (quality == "ultra" || quality == "nomercy") return 10000;
  return 3000;
}

namespace {

constexpr double SNOW_SHARE = 0.6;

// ------------------------------------------------------------- the colours

/** the weather's sky (sRGB): at the horizon, where the fog takes its colour, and overhead */
const std::pair<Color, Color>& skyLin(const WeatherKind& kind) {
  static const auto* t = new std::unordered_map<std::string, std::pair<Color, Color>>{
      {"clear", {Color(0xb7bfc8), Color(0x8f99a5)}},  {"cloudy", {Color(0xb7bfc8), Color(0x8f99a5)}}, {"fog", {Color(0xc4c8cc), Color(0xbec2c6)}},
      {"drizzle", {Color(0xa4abb3), Color(0x87909a)}}, {"rain", {Color(0x949ba3), Color(0x737b85)}},   {"snow", {Color(0xc9ced4), Color(0xaab1ba)}},
      {"storm", {Color(0x656c74), Color(0x42484f)}},
  };
  auto it = t->find(kind);
  return it != t->end() ? it->second : t->at("cloudy");
}

double luminance(const Color& c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

/** the sky light of an overcast day: neutral, without the blue of a clear sky */
const Color GREY_SKYLIGHT(0xd2d6da);
/** what a lightning flash lights the clouds with */
const Color FLASH_COLOR(0xe8e8ff);
/** the horizon of the game's clear noon (sky.ts), for how bright the sky is now */
double dayHorizonLum() {
  static const double l = luminance(Color(0xc2d3e4));
  return l;
}

double smooth(double e0, double e1, double x) {
  const double t = std::min(1.0, std::max(0.0, (x - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
}

/** a in [0, m) */
double wrap(double a, double m) { return a - m * std::floor(a / m); }

/** small, quick, repeatable random numbers (the same rain every time) */
std::function<double()> rng(uint32_t seed) {
  auto s = std::make_shared<uint32_t>(seed);
  return [s]() {
    *s = *s + 0x6d2b79f5u;
    uint32_t t = *s;
    t = static_cast<uint32_t>(jsImul(static_cast<int32_t>(t ^ (t >> 15)), static_cast<int32_t>(t | 1)));
    t ^= t + static_cast<uint32_t>(jsImul(static_cast<int32_t>(t ^ (t >> 7)), static_cast<int32_t>(t | 61)));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  };
}

// -------------------------------------------------------- the particles

// (the original's ShaderMaterial, as it is written there)
const char* const PARTICLE_VERT = R"GLSL(
#include <fog_pars_vertex>
attribute vec4 aSeed;        // xyz: the particle's place in the box (0 … 1); w: a random number of its own
uniform vec3 uBox;           // size of the box round the camera (m)
uniform vec3 uShift[3];      // how far each speed class has fallen and drifted, within the box (m)
uniform vec3 uCamLocal;      // the camera in the box (m)
uniform vec3 uVel;           // the particles' velocity (m/s): the wind, and the fall of the middle class
uniform vec3 uCamVel;        // the camera's velocity (m/s)
uniform float uExposure;     // how many seconds of the motion a streak shows
uniform float uRadius;       // half the width of a drop or a flake (m)
uniform float uPixel;        // the size of a pixel 1 m from the camera (m)
uniform float uTime;         // s
uniform float uFlutter;      // how far snowflakes sway (m)
uniform mat4 uShelter;       // box → the shelter's unit cube
uniform float uShelterOn;
varying vec3 vShape;         // across (−1 … 1), along (in half-widths from the head), the streak's length (in half-widths)
varying float vAlpha;

void main() {
  int cls = int(min(aSeed.w * 3.0, 2.0));
  float speed = 0.85 + 0.15 * float(cls);
  vec3 p = mod(aSeed.xyz * uBox + uShift[cls], uBox);
#ifdef SNOW
  float ph = aSeed.w * 61.0;
  p.x += uFlutter * sin(uTime * (0.6 + aSeed.y) + ph);
  p.z += uFlutter * sin(uTime * (0.5 + aSeed.z) + ph * 1.7);
#endif
  // faded out towards the walls of the box, so that none is seen to wrap round
  vec3 d = p - uCamLocal;
  float fade = 1.0 - smoothstep(0.6, 1.0, length(d.xz) / (0.5 * uBox.x));
  float h = p.y / uBox.y;
  fade *= smoothstep(0.0, 0.08, h) * (1.0 - smoothstep(0.82, 1.0, h));
  // nothing falls inside the vehicle
  vec3 q = (uShelter * vec4(p, 1.0)).xyz;
  if (uShelterOn > 0.5 && max(max(abs(q.x), abs(q.y)), abs(q.z)) < 1.0) fade = 0.0;
  // the streak: from the particle back along its motion relative to the camera
  vec3 rel = vec3(uVel.x, uVel.y * speed, uVel.z) - uCamVel;
  vec4 head = modelViewMatrix * vec4(p, 1.0);
  vec4 tail = modelViewMatrix * vec4(p - rel * (uExposure * (0.7 + 0.6 * fract(aSeed.w * 13.7))), 1.0);
  float dist = max(length(head.xyz), 1e-3);
  // at least about a pixel wide: thinner than that it is drawn fainter instead
  float hw = max(uRadius, uPixel * dist * 0.7);
  vec3 axis = tail.xyz - head.xyz;
  float len = length(axis);
  vec3 dir = len > 1e-5 ? axis / len : vec3(0.0, 1.0, 0.0);
  vec3 side = cross(dir, head.xyz / dist);
  float sl = length(side);
  side = sl > 1e-3 ? side / sl : vec3(1.0, 0.0, 0.0);
  float along = mix(-hw, len + hw, position.y);
  vec4 mvPosition = vec4(head.xyz + dir * along + side * (position.x * hw), 1.0);
  gl_Position = projectionMatrix * mvPosition;
  // one that is not to be seen goes beyond the far plane: nothing of it is drawn
  if (fade < 0.001) gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
  vShape = vec3(position.x, along / hw, len / hw);
  // (and right at the eye it would fill the view: faded)
  vAlpha = fade * (uRadius / hw) * smoothstep(0.25, 0.9, dist);
  #include <fog_vertex>
}
)GLSL";
const char* const PARTICLE_FRAG = R"GLSL(
#include <fog_pars_fragment>
uniform vec3 uColor;
uniform float uOpacity;
varying vec3 vShape;
varying float vAlpha;

void main() {
  // the distance from the line from head to tail, in half-widths: a capsule with soft edges
  float beyond = max(0.0, max(-vShape.y, vShape.y - vShape.z));
  float r = length(vec2(vShape.x, beyond));
  float a = vAlpha * uOpacity * (1.0 - smoothstep(0.35, 1.0, r));
#ifndef SNOW
  // a streak is brightest at the drop and fades along its tail
  a *= 1.0 - 0.65 * clamp(vShape.y / max(vShape.z, 0.001), 0.0, 1.0);
#endif
  gl_FragColor = vec4(uColor, a);
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
  #include <fog_fragment>
}
)GLSL";

}  // namespace

SkyParts skyParts(Sky& sky) {
  return {.fog = sky.fog, .sun = sky.sun, .hemi = sky.hemi, .uZenith = sky.uZenith, .uHorizon = sky.uHorizon, .uSunColor = sky.uSunColor, .dome = sky.dome};
}

FogRange& weatherFog(double visibility, double near, double far, FogRange& out) {
  const double f = far > 0 ? std::min(1.0, (1.05 * std::max(1.0, visibility)) / far) : 1;
  out.far = far * f;
  const double g = 1 - f;
  out.near = near * f * f - 0.25 * out.far * g * g;
  return out;
}

// -------------------------------------------------------- the particles

WeatherField::WeatherField(bool snow, double capacityIn, const Vector3& boxIn, double camShare, const WeatherShelter* shelter, uint32_t seed)
    : box(boxIn), camShare_(camShare), seed_(seed) {
  std::get<Vector3>(u.uBox->value).copy(box);
  std::get<Vector3>(u.uCamLocal->value).set(box.x * 0.5, box.y * camShare, box.z * 0.5);
  // (fogDensity, fogNear, fogFar, fogColor: the renderer's own, as for every material with fog)
  std::map<std::string, UniformPtr> uniforms = {
      {"uBox", u.uBox},         {"uShift", u.uShift}, {"uCamLocal", u.uCamLocal}, {"uVel", u.uVel},           {"uCamVel", u.uCamVel},
      {"uExposure", u.uExposure}, {"uRadius", u.uRadius}, {"uPixel", u.uPixel},     {"uTime", u.uTime},         {"uFlutter", u.uFlutter},
      {"uShelter", u.uShelter}, {"uShelterOn", u.uShelterOn}, {"uColor", u.uColor}, {"uOpacity", u.uOpacity},
  };
  MaterialPtr material = shaderMaterial(PARTICLE_VERT, PARTICLE_FRAG, uniforms, {.transparent = true, .side = Side::Double, .depthWrite = false, .fog = true});
  if (snow) material->defines = {{"SNOW", ""}};
  mesh = make<Mesh>(geometry(static_cast<int>(std::max(1.0, capacityIn))), material);
  capacity = static_cast<int>(std::max(0.0, jsRound(capacityIn)));
  mesh->name = snow ? "snow" : "rain";
  mesh->frustumCulled = false;
  // after the vehicles' glass (5): the rain is nearly always nearer than anything else see-through
  mesh->renderOrder = 10;
  mesh->visible = false;
  Mesh* self = mesh.get();
  Uniforms* uu = &u;
  // Per picture (the mirrors are pictures of their own): the size of a pixel, and the shelter
  // where it is now – after the scene's matrices were brought up to date.
  mesh->onBeforeRender = [self, uu, shelter](Renderer& renderer, Scene&, Camera& camera) {
    const RenderTargetPtr& target = renderer.getRenderTarget();
    const double height = target ? target->height : renderer.bufferHeight();
    const double fov = camera.isPerspective ? (180 / std::numbers::pi) * 2 * std::atan(std::tan((std::numbers::pi / 180) * 0.5 * camera.fov) / camera.zoom) : 60;
    uu->uPixel->value = (2 * std::tan((fov * std::numbers::pi) / 360)) / std::max(1.0, height);
    Object3D* s = shelter->object;
    if (s && !shelter->box.isEmpty()) {
      Vector3 c, h;
      shelter->box.getCenter(c);
      shelter->box.getSize(h).multiplyScalar(0.5).addScalar(0.05);
      // box → the shelter's unit cube: (the object's world matrix · its box)⁻¹ · this mesh's world matrix
      Matrix4 m;
      m.makeScale(h.x, h.y, h.z).setPosition(c).premultiply(s->matrixWorld).invert().multiply(self->matrixWorld);
      uu->uShelter->value = m;
      uu->uShelterOn->value = 1.0;
    } else uu->uShelterOn->value = 0.0;
  };
}

WeatherField::~WeatherField() {
  if (mesh) mesh->onBeforeRender = nullptr;
}

void WeatherField::setCapacity(double n) {
  capacity = static_cast<int>(std::max(0.0, jsRound(n)));
  if (capacity > allocated_) {
    GeometryPtr old = mesh->geometry;
    mesh->geometry = geometry(capacity);
    old->dispose();
  }
}

GeometryPtr WeatherField::geometry(int n) {
  auto g = make<BufferGeometry>();
  // one quad: x across the streak (−1 … 1), y from its head (0) to its tail (1)
  g->setAttribute("position", attr(std::vector<float>{-1, 0, 0, 1, 0, 0, -1, 1, 0, 1, 1, 0}, 3));
  g->setIndex(std::vector<uint32_t>{0, 1, 2, 2, 1, 3});
  std::vector<float> seeds(static_cast<size_t>(n) * 4);
  const std::function<double()> rand = rng(seed_);
  for (float& s : seeds) s = static_cast<float>(rand());
  AttrPtr a = attr(std::move(seeds), 4);
  a->divisor = 1;
  g->setAttribute("aSeed", a);
  g->instanceCount = 0;
  allocated_ = n;
  return g;
}

void WeatherField::advance(double cx, double cy, double cz, double step, double vx, double vy, double vz, double amount) {
  const Vector3& B = box;
  const double lx = cx - B.x * 0.5;
  const double ly = cy - B.y * camShare_;
  const double lz = cz - B.z * 0.5;
  mesh->position.set(lx, ly, lz);
  std::vector<float>& shift = std::get<std::vector<float>>(u.uShift->value);
  for (int c = 0; c < 3; c++) {
    Vector3& d = drift_[static_cast<size_t>(c)];
    d.x = wrap(d.x + vx * step, B.x);
    d.y = wrap(d.y + vy * (0.85 + 0.15 * c) * step, B.y);
    d.z = wrap(d.z + vz * step, B.z);
    // (the box's corner subtracted here, in double precision: the shader only sees small numbers)
    shift[static_cast<size_t>(c) * 3] = static_cast<float>(wrap(d.x - lx, B.x));
    shift[static_cast<size_t>(c) * 3 + 1] = static_cast<float>(wrap(d.y - ly, B.y));
    shift[static_cast<size_t>(c) * 3 + 2] = static_cast<float>(wrap(d.z - lz, B.z));
  }
  std::get<Vector3>(u.uVel->value).set(vx, vy, vz);
  const int n = std::min(capacity, static_cast<int>(jsRound(capacity * std::min(1.0, std::max(0.0, amount)))));
  mesh->geometry->instanceCount = n;
  mesh->visible = n > 0;
}

void WeatherField::dispose() {
  mesh->removeFromParent();
  mesh->geometry->dispose();
  mesh->material->dispose();
}

// --------------------------------------------- the sky's own values, kept

double Kept::read(double v) {
  if (!known_ || v != last_) {
    base = v;
    known_ = true;
  }
  return base;
}

double Kept::wrote(double v) {
  last_ = v;
  return v;
}

const Color& KeptColor::read(const Color& c) {
  if (!known_ || !c.equals(last_)) {
    base.copy(c);
    known_ = true;
  }
  return base;
}

// ------------------------------------------------------------ the renderer

namespace {

/** how quickly the picture follows the weather (s; a change of the setting shows within a few seconds) */
constexpr double FADE = 2.5;
/** the near-ground wind: two thirds or so of the wind 10 m up */
constexpr double GROUND_WIND = 0.75;

Color& colorOf(const UniformPtr& u) { return std::get<Color>(u->value); }

}  // namespace

WeatherRenderer::WeatherRenderer(double quality) : rand_(rng(0x5eed)) {
  look.fogColor = skyLin("cloudy").first;
  look.zenithColor = skyLin("cloudy").second;
  group->name = "weather";
  const double q = std::max(0.0, jsRound(quality));
  // rain: a box 30 m wide and 18 m high, two fifths of it below the camera; snow: a smaller one (flakes are seen less far)
  rain_ = std::make_unique<WeatherField>(false, q, Vector3(30, 18, 30), 0.4, &shelter_, 1);
  snow_ = std::make_unique<WeatherField>(true, jsRound(q * SNOW_SHARE), Vector3(24, 14, 24), 0.45, &shelter_, 2);
  group->add(rain_->mesh, snow_->mesh);
}

WeatherRenderer::~WeatherRenderer() {
  attachSky(nullptr);
  group->removeFromParent();
}

void WeatherRenderer::quality(double n) {
  const double q = std::isfinite(n) ? std::max(0.0, jsRound(n)) : WEATHER_PARTICLES("medium");
  rain_->setCapacity(q);
  snow_->setCapacity(jsRound(q * SNOW_SHARE));
}

void WeatherRenderer::setShelter(Object3D* object, std::optional<Box3> box) {
  shelter_.object = object;
  if (box) shelter_.box = *box;
  else if (!object) shelter_.box.makeEmpty();
}

void WeatherRenderer::attachSky(const SkyParts* sky) {
  if (sky_) restoreSky();
  sky_.reset();
  dome_ = false;
  if (!sky) return;
  sky_ = *sky;
  const auto isColor = [](const UniformPtr& u) { return u && std::holds_alternative<Color>(u->value); };
  dome_ = isColor(sky->uZenith) && isColor(sky->uHorizon) && isColor(sky->uSunColor);
  applySky();
}

void WeatherRenderer::addWetness(Material& material) {
  // (once – told by the material's program key, not by its address)
  if (material.patch && material.patch->key.find("|fs-wet") != std::string::npos) return;
  // the program's key: the material's own one and ours – the name of the shader the original's patch leaves
  const std::string name = (material.patch ? material.patch->key : "") + "|fs-wet";
  ShaderPatchPtr next = gamePatch(name.c_str());
  if (!next) {
    plat::warn("weather: no shader " + name);
    return;
  }
  if (material.patch) next->uniforms = material.patch->uniforms;
  next->uniforms["uFsWet"] = uFsWet;
  next->uniforms["uFsWetSky"] = uFsWetSky;
  material.patch = next;
  material.needsUpdate();
}

void WeatherRenderer::update(const Vector3& camera, double dt, const WeatherState& state) {
  const double step = std::isfinite(dt) && dt > 0 ? std::min(dt, 0.25) : 0;
  time_ += step;
  const Target& t = target_;
  aim(state);
  const bool snap = snapNext_;
  const double k = snap ? 1 : 1 - std::exp(-step / FADE);
  snapNext_ = false;
  WeatherLook& L = look;
  L.rain += (t.rain - L.rain) * k;
  L.snow += (t.snow - L.snow) * k;
  L.tint += (t.tint - L.tint) * k;
  L.sunFactor += (t.sun - L.sunFactor) * k;
  L.ambientFactor += (t.ambient - L.ambientFactor) * k;
  L.fogColor.lerp(t.fog, k);
  L.zenithColor.lerp(t.zenith, k);
  // (roads dry more slowly than they get wet)
  L.wet += (t.wet - L.wet) * (t.wet > L.wet || snap ? k : 1 - std::exp(-step / (FADE * 4)));
  logVis_ += (t.logVis - logVis_) * k;
  L.visibility = std::exp(logVis_);
  fall_ += (t.fall - fall_) * k;
  exposure_ += (t.exposure - exposure_) * k;
  radius_ += (t.radius - radius_) * k;
  opacity_ += (t.opacity - opacity_) * k;
  windX_ += (t.windX - windX_) * k;
  windZ_ += (t.windZ - windZ_) * k;
  storm_ += (t.storm - storm_) * k;

  // the camera's velocity: the streaks show the motion relative to it
  if (hasCam_ && std::isfinite(dt) && dt > 1e-4) {
    const double dx = camera.x - camLast_.x;
    const double dy = camera.y - camLast_.y;
    const double dz = camera.z - camLast_.z;
    if (dx * dx + dy * dy + dz * dz > 2500) camVel_.set(0, 0, 0);   // a jump (a new drive, a replay)
    else {
      const double a = 1 - std::exp(-dt / 0.12);
      camVel_.x += (dx / dt - camVel_.x) * a;
      camVel_.y += (dy / dt - camVel_.y) * a;
      camVel_.z += (dz / dt - camVel_.z) * a;
      const double v = camVel_.length();
      if (v > 70) camVel_.multiplyScalar(70 / v);
    }
  }
  camLast_.set(camera.x, camera.y, camera.z);
  hasCam_ = true;

  // lightning
  double flash = 0;
  if (storm_ > 0.3) {
    nextStrike_ -= step;
    if (nextStrike_ <= 0) {
      flashT_ = 0;
      strikes++;
      nextStrike_ = (5 + rand_() * 20) / storm_;
    }
  }
  if (flashT_ >= 0) {
    const double f = flashT_;
    flash = f < 0.06 ? 1 : f < 0.12 ? 0.2 : f < 0.2 ? 0.8 : 0.8 * std::exp(-(f - 0.2) / 0.12);
    flashT_ += step;
    if (flashT_ > 1.5) flashT_ = -1;
  }
  L.flash = flash;

  // the particles: wind with gusts, the fall, their colour in this light
  const double gust = 1 + 0.3 * std::sin(time_ * 0.31) * std::sin(time_ * 0.17 + 1.3);
  const double wx = windX_ * gust;
  const double wz = windZ_ * gust;
  const double light = std::min(1.2, std::max(0.25, 0.3 * L.sunFactor + 0.6 * L.ambientFactor) + flash);
  WeatherField::Uniforms& ru = rain_->u;
  std::get<Vector3>(ru.uCamVel->value).copy(camVel_);
  ru.uExposure->value = exposure_;
  ru.uRadius->value = radius_;
  ru.uOpacity->value = opacity_;
  colorOf(ru.uColor).setRGB(0.62 * light, 0.66 * light, 0.72 * light);
  rain_->advance(camera.x, camera.y, camera.z, step, wx, -fall_, wz, L.rain);
  WeatherField::Uniforms& su = snow_->u;
  std::get<Vector3>(su.uCamVel->value).copy(camVel_);
  su.uExposure->value = 0.02;
  su.uRadius->value = 0.009;
  su.uOpacity->value = 0.85;
  su.uFlutter->value = 0.25;
  su.uTime->value = std::fmod(time_, 3600);
  const double sl = 0.55 + 0.45 * light;
  colorOf(su.uColor).setRGB(0.86 * sl, 0.88 * sl, 0.92 * sl);
  snow_->advance(camera.x, camera.y, camera.z, step, wx, -1.1, wz, L.snow);

  applySky();
  uFsWet->value = roadWater ? L.wet : 0.0;
  if (!sky_) colorOf(uFsWetSky).copy(L.fogColor);
}

void WeatherRenderer::aim(const WeatherState& s) {
  Target& t = target_;
  const double I = std::min(1.0, std::max(0.0, s.intensity));
  const double cloud = std::min(1.0, std::max(0.0, s.cloud));
  const double vis = std::min(50000.0, std::max(20.0, s.visibility));
  const WeatherKind& kind = s.kind;
  const bool falls = I > 0 && (kind == "drizzle" || kind == "rain" || kind == "storm" || kind == "snow");
  t.rain = 0;
  t.snow = 0;
  if (kind == "drizzle" && I > 0) {
    t.rain = 0.35 + 0.65 * I;
    t.fall = 2.2;
    t.exposure = 0.07;
    t.radius = 0.0016;
    t.opacity = 0.22;
  } else if ((kind == "rain" || kind == "storm") && I > 0) {
    t.rain = (kind == "storm" ? 0.2 : 0.12) + (kind == "storm" ? 0.8 : 0.88) * I;
    t.fall = kind == "storm" ? 9.5 : 8;
    t.exposure = 0.04;
    t.radius = kind == "storm" ? 0.0032 : 0.0028;
    t.opacity = kind == "storm" ? 0.34 : 0.3;
  } else if (kind == "snow" && I > 0) t.snow = 0.15 + 0.85 * I;
  windVector(s, wind_);
  t.windX = wind_.x * GROUND_WIND;
  t.windZ = wind_.z * GROUND_WIND;
  t.logVis = std::log(vis);
  const std::pair<Color, Color>& sky = skyLin(kind);
  t.fog.copy(sky.first);
  t.zenith.copy(sky.second);
  const double fogDensity = 1 - smooth(300, 5000, vis);
  if (kind == "clear" || kind == "cloudy") t.tint = 0.85 * smooth(0.25, 1, cloud);
  else if (kind == "fog") t.tint = 0.55 + 0.45 * (1 - smooth(150, 2500, vis));
  else t.tint = std::max(0.85 * smooth(0.25, 1, cloud), 0.85 + 0.15 * I);
  const double p = falls ? I : 0;
  const double storm = kind == "storm" ? 1 : 0;
  t.sun = std::min(1.0, std::max(0.03, (1 - 0.85 * std::pow(cloud, 1.5)) * (1 - 0.45 * p) * (1 - 0.7 * fogDensity) * (storm != 0 ? 0.55 : 1)));
  // overcast, the light comes from the whole sky: less sun, more sky light (a snowy sky is bright)
  t.ambient = std::min(1.45, std::max(0.45, 1 + 0.32 * cloud - 0.3 * p - 0.35 * storm + (kind == "snow" ? 0.12 * I : 0) + 0.05 * fogDensity));
  t.wet = std::min(1.0, std::max(0.0, s.wet));
  t.storm = storm;
}

void WeatherRenderer::applySky() {
  if (!sky_) return;
  SkyParts& sky = *sky_;
  const WeatherLook& L = look;
  Fog& fog = *sky.fog;
  const double near = fogNear_.read(fog.near);
  const double far = fogFar_.read(fog.far);
  weatherFog(L.visibility, near, far, fogOut_);
  fog.near = fogNear_.wrote(fogOut_.near);
  fog.far = fogFar_.wrote(fogOut_.far);
  // the weather's greys are those of a clear noon; as bright as the sky is now (a night stays dark)
  const Color base = fogColor_.read(fog.color);
  const double bright = std::min(1.2, std::max(0.03, luminance(base) / dayHorizonLum()));
  const double flash = L.flash * 0.45;
  fog.color.copy(base).lerp(tmp_.copy(L.fogColor).multiplyScalar(bright), L.tint).lerp(FLASH_COLOR, flash);
  fogColor_.wrote(fog.color);
  colorOf(uFsWetSky).copy(fog.color);
  sky.sun->intensity = sunI_.wrote(sunI_.read(sky.sun->intensity) * L.sunFactor);
  sky.hemi->intensity = hemiI_.wrote(hemiI_.read(sky.hemi->intensity) * (L.ambientFactor + 1.6 * L.flash));
  const Color hemiBase = hemiColor_.read(sky.hemi->color);
  sky.hemi->color.copy(hemiBase).lerp(GREY_SKYLIGHT, L.tint * 0.8);
  hemiColor_.wrote(sky.hemi->color);
  if (dome_) {
    Color& horizon = colorOf(sky.uHorizon);
    const Color horizonBase = horizon_.read(horizon);
    horizon.copy(horizonBase).lerp(tmp_.copy(L.fogColor).multiplyScalar(bright), L.tint).lerp(FLASH_COLOR, flash);
    horizon_.wrote(horizon);
    Color& zenith = colorOf(sky.uZenith);
    const Color zenithBase = zenith_.read(zenith);
    zenith.copy(zenithBase).lerp(tmp_.copy(L.zenithColor).multiplyScalar(bright), L.tint).lerp(FLASH_COLOR, flash * 0.7);
    zenith_.wrote(zenith);
    // the sun's disc and glow fade behind the clouds
    Color& sunColor = colorOf(sky.uSunColor);
    const Color sunBase = sunColor_.read(sunColor);
    sunColor.copy(sunBase).multiplyScalar(L.sunFactor * L.sunFactor);
    sunColor_.wrote(sunColor);
  }
}

void WeatherRenderer::restoreSky() {
  if (!sky_) return;
  SkyParts& sky = *sky_;
  sky.fog->near = fogNear_.read(sky.fog->near);
  sky.fog->far = fogFar_.read(sky.fog->far);
  sky.fog->color.copy(fogColor_.read(sky.fog->color));
  sky.sun->intensity = sunI_.read(sky.sun->intensity);
  sky.hemi->intensity = hemiI_.read(sky.hemi->intensity);
  sky.hemi->color.copy(hemiColor_.read(sky.hemi->color));
  if (dome_) {
    colorOf(sky.uHorizon).copy(horizon_.read(colorOf(sky.uHorizon)));
    colorOf(sky.uZenith).copy(zenith_.read(colorOf(sky.uZenith)));
    colorOf(sky.uSunColor).copy(sunColor_.read(colorOf(sky.uSunColor)));
  }
  for (Kept* k : {&fogNear_, &fogFar_, &sunI_, &hemiI_}) k->forget();
  for (KeptColor* k : {&fogColor_, &hemiColor_, &zenith_, &horizon_, &sunColor_}) k->forget();
}

void WeatherRenderer::dispose() {
  attachSky(nullptr);
  uFsWet->value = 0.0;
  rain_->dispose();
  snow_->dispose();
  group->removeFromParent();
}

}  // namespace fs
