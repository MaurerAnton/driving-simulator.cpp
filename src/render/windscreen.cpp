// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/windscreen.ts @ v1.7.7
#include "render/windscreen.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "engine/renderer.h"
#include "render/car.h"
#include "render/fleet.h"
#include "render/truck.h"

namespace fs {

namespace {

/** how far inside the glass the mesh lies (m) */
constexpr double INSET = 0.014;

/**
 * Rows of points across the glass, foot to top (each row left → right as the
 * driver sees it), as a mesh: u across, v up.  `inward`: the way into the cab.
 */
GeometryPtr sheet(const std::vector<std::vector<Vector3>>& rows, const Vector3& inward) {
  std::vector<float> pos;
  std::vector<float> uv;
  std::vector<uint32_t> idx;
  const size_t n = rows[0].size();
  for (size_t j = 0; j < rows.size(); j++) {
    for (size_t i = 0; i < rows[j].size(); i++) {
      const Vector3& p = rows[j][i];
      pos.insert(pos.end(), {static_cast<float>(p.x + inward.x * INSET), static_cast<float>(p.y + inward.y * INSET), static_cast<float>(p.z + inward.z * INSET)});
      uv.insert(uv.end(), {static_cast<float>(static_cast<double>(i) / static_cast<double>(n - 1)), static_cast<float>(static_cast<double>(j) / static_cast<double>(rows.size() - 1))});
    }
  }
  for (size_t j = 0; j + 1 < rows.size(); j++) {
    for (size_t i = 0; i + 1 < n; i++) {
      const uint32_t a = static_cast<uint32_t>(j * n + i);
      const uint32_t m = static_cast<uint32_t>(n);
      idx.insert(idx.end(), {a, a + 1, a + m + 1, a, a + m + 1, a + m});
    }
  }
  auto g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->setAttribute("uv", attr(std::move(uv), 2));
  g->setIndex(idx);
  return g;
}

/** The car's windscreen: from the cowl up to the roof's front edge, a little narrower than the body. */
GlassShape carGlass(const CarShell& sh) {
  const double hw = sh.halfWidth;
  // (kept clear of the A pillars standing on its edges)
  const double k = 0.93;
  const std::vector<Vector3> foot = {Vector3(-hw * 0.9 * k, sh.cowlY, sh.zCowl), Vector3(0, sh.cowlCrownY, sh.zCowl), Vector3(hw * 0.9 * k, sh.cowlY, sh.zCowl)};
  const std::vector<Vector3> top = {Vector3(-hw * 0.74 * k, sh.roofY - 0.055, sh.zRoofFront), Vector3(0, sh.roofY - 0.004, sh.zRoofFront),
                                    Vector3(hw * 0.74 * k, sh.roofY - 0.055, sh.zRoofFront)};
  const double dy = top[1].y - foot[1].y;
  const double dz = top[1].z - foot[1].z;
  Vector3 inward = Vector3(0, -dz, dy).normalize();
  if (inward.z < 0) inward.negate();
  return {sheet({foot, top}, inward), hw * (0.9 + 0.74) * k, std::hypot(dy, dz)};
}

/** An upright pane across the front of a cab: x0 … x1, y0 … y1, standing at z (the cab behind it, +z). */
GlassShape flatGlass(double x0, double x1, double y0, double y1, double z) {
  const std::vector<Vector3> foot = {Vector3(x0, y0, z), Vector3(x1, y0, z)};
  const std::vector<Vector3> top = {Vector3(x0, y1, z), Vector3(x1, y1, z)};
  return {sheet({foot, top}, Vector3(0, 0, 1)), x1 - x0, y1 - y0};
}

/** The target the scene behind the glass is drawn into for its blur: kept at the drawing size, reused between frames. */
RenderTargetPtr ensureTarget(RenderTargetPtr t, int w, int h) {
  if (t && t->width == w && t->height == h) return t;
  if (t) t->dispose();
  auto next = std::make_shared<RenderTarget>(w, h, true);
  next->texture->minFilter = Filter::Linear;
  next->texture->magFilter = Filter::Linear;
  next->texture->generateMipmaps = false;
  return next;
}

// (the original's ShaderMaterial, as it is written there)
const char* const VERT = R"GLSL(
varying vec2 vUv;
void main() {
  vUv = uv;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
)GLSL";
const char* const FRAG = R"GLSL(
uniform sampler2D uState;
uniform sampler2D uGen;
uniform sampler2D uBg;
uniform vec2 uSize;
uniform vec2 uRes;
uniform float uHaveBg;
uniform float uLight;
uniform vec2 uFlow;
uniform float uRun;
uniform vec4 uArm0;
uniform vec4 uArm1;
uniform vec2 uIn;
varying vec2 vUv;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec2 hash2(vec2 p) { return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453); }

// one layer of drops: cells of 'cell' metres, a drop in a cell when its number is under the density
// returns the slope of the drop's surface (xy) and how much of a drop is there (z)
vec3 drops(vec2 p, float cell, float den, float gen, vec2 stretch, float seed) {
  vec2 id = floor(p / cell);
  vec2 f = fract(p / cell) - 0.5;
  vec2 h = hash2(id + seed + gen * 7.13);
  float there = step(hash(id * 1.37 + seed + gen * 3.71), den);
  float r = mix(0.16, 0.4, h.x * h.x);
  vec2 c = (hash2(id + 11.0 + gen) - 0.5) * (1.0 - 2.0 * r);
  vec2 d = (f - c) / stretch;
  float q = dot(d, d) / (r * r);
  float m = there * (1.0 - smoothstep(0.7, 1.0, q));
  return vec3(d / r * m, m);
}

vec3 behind(vec2 sp, float rad) {
  if (uHaveBg < 0.5) return vec3(0.4, 0.43, 0.47) * uLight;
  vec3 c = texture2D(uBg, sp).rgb;
  if (rad < 0.0006) return c;
  float a0 = hash(gl_FragCoord.xy) * 6.2832;
  float asp = uRes.x / uRes.y;
  for (int i = 0; i < 8; i++) {
    float a = a0 + float(i) * 0.7854;
    float r = rad * (0.45 + 0.55 * fract(float(i) * 0.618 + 0.3));
    c += texture2D(uBg, clamp(sp + vec2(cos(a) / asp, sin(a)) * r, 0.001, 0.999)).rgb;
  }
  return c / 9.0;
}

// a wiper: its arm from the pivot, the rubber of the blade along it
float wiper(vec2 p, vec4 arm, float rIn) {
  vec2 d = vec2(cos(arm.z), sin(arm.z));
  vec2 q = p - arm.xy;
  float along = dot(q, d);
  float off = abs(q.x * d.y - q.y * d.x);
  float aa = fwidth(off) + 0.0005;
  float bar = (1.0 - smoothstep(0.007, 0.007 + aa, off)) * step(0.0, along) * step(along, arm.w);
  float blade = (1.0 - smoothstep(0.011, 0.011 + aa, off)) * step(rIn, along) * step(along, arm.w);
  return max(bar, blade * 0.9);
}

void main() {
  vec2 p = vUv * uSize;
  vec4 st = texture2D(uState, vUv);
  float gen = floor(texture2D(uGen, vUv).r * 255.0 + 0.5);
  float film = st.r;
  float snow = st.g;
  float grime = st.b;
  float den = st.a;
  float still = 1.0 - 0.8 * uRun;
  vec3 a = drops(p, 0.009, den * still, gen, vec2(1.0), 0.0);
  vec3 b = drops(p + vec2(0.0031, 0.0047), 0.0055, den * 0.8 * still, gen, vec2(1.0), 5.0);
  vec3 c = drops(p - uFlow, 0.012, den * uRun, gen, vec2(0.75, 1.0 + 1.6 * uRun), 9.0);
  vec2 n = a.xy + b.xy + c.xy;
  float dm = clamp(a.z + b.z + c.z, 0.0, 1.0);
  vec2 sp = gl_FragCoord.xy / uRes;
  // water blurs, a drop is a little lens that turns the picture behind it over
  float rad = (film * 16.0 + grime * 7.0 + snow * 10.0 + dm * 2.0) / uRes.y;
  vec3 col = behind(sp - n * 0.022, rad);
  // a grey veil of water and dirt; the dirt wiped dry lies in arcs
  float streak = 0.7 + 0.3 * sin(length(p - uArm0.xy) * 1500.0);
  float g = clamp(grime * streak, 0.0, 1.0);
  col = mix(col, col * 0.8 + vec3(0.11) * uLight, film * 0.4);
  col = mix(col, vec3(0.42, 0.39, 0.33) * uLight, g * 0.5);
  // the light in a drop: bright on its lower edge, a glint up top
  col += dm * (smoothstep(0.3, 1.0, -n.y) * 0.18 + smoothstep(0.6, 1.0, n.y) * 0.1) * (0.3 + 0.7 * uLight);
  float alpha = max(max(film * 0.9, dm * 0.95), g * 0.85);
  // snow: single flakes first, then a cover that grows together in patches
  vec2 fid = floor(p / 0.006);
  vec2 fh = hash2(fid + gen * 2.1);
  float fr = mix(0.12, 0.35, fh.y);
  float flake = step(hash(fid + 4.4 + gen), snow * 2.5) * (1.0 - smoothstep(fr * 0.6, fr, length(fract(p / 0.006) - 0.5 - (fh - 0.5) * 0.3)));
  vec2 cid = p / 0.03;
  vec2 ci = floor(cid);
  vec2 cf = smoothstep(0.0, 1.0, fract(cid));
  float nz = mix(mix(hash(ci), hash(ci + vec2(1.0, 0.0)), cf.x), mix(hash(ci + vec2(0.0, 1.0)), hash(ci + 1.0), cf.x), cf.y);
  float cover = smoothstep(0.32, 0.8, snow + (nz - 0.5) * 0.35);
  float s = max(cover, flake * 0.9);
  vec3 snowCol = vec3(0.9, 0.92, 0.95) * (0.12 + 0.88 * uLight) * (0.9 + 0.1 * nz) + col * 0.15;
  col = mix(col, snowCol, s);
  alpha = max(alpha, s);
  float arm = max(wiper(p, uArm0, uIn.x), wiper(p, uArm1, uIn.y));
  col = mix(col, vec3(0.02), arm);
  alpha = max(alpha, arm);
  if (alpha < 0.004) discard;
  gl_FragColor = vec4(col, alpha);
}
)GLSL";

/**
 * The picture behind the glass, as the shader reads it.  (In the original it is a copy of a target drawn in
 * sRGB – its numbers are sRGB, and the shader writes what it mixes of them as they are.  This renderer draws a
 * target in linear numbers: they are brought to sRGB as they are read, and the shader's own arithmetic stays
 * as it is.)
 */
std::string fragment() {
  std::string fs = FRAG;
  fs = replaceAll(fs, "texture2D(uBg, ", "fsBgTexel(");
  const std::string after = "varying vec2 vUv;\n";
  const size_t at = fs.find(after);
  fs.insert(at + after.size(), "vec4 fsBgTexel(vec2 p) { return sRGBTransferOETF(texture2D(uBg, p)); }\n");
  return fs;
}

constexpr int STATE_COLS = 48;
constexpr int STATE_ROWS = 24;

}  // namespace

std::optional<GlassShape> findGlass(VehicleModel& model) {
  if (auto* car = dynamic_cast<PlayerCar*>(&model)) return carGlass(car->exterior.shell);
  Group& root = *model.root;
  // the tram's cab has no glass of its own: the opening between its pillars (render/fleet.h TramCab)
  if (root.name == "tram cab") return flatGlass(-1.05, 1.05, 1.27, 2.78, -4.62);
  root.updateMatrixWorld(true);
  const Matrix4 inv = Matrix4().copy(root.matrixWorld).invert();
  const Vector3& eye = model.eye;
  std::optional<Box3> best;
  double bestArea = 0;
  const Material* glass = GLASS().get();
  root.traverse([&](Object3D& o) {
    if (o.type != ObjectType::Mesh) return;
    Mesh& m = static_cast<Mesh&>(o);
    if (m.material.get() != glass || !m.geometry) return;
    BufferGeometry& g = *m.geometry;
    if (!g.boundingBox) g.computeBoundingBox();
    if (!g.boundingBox) return;
    Box3 b = *g.boundingBox;
    b.applyMatrix4(Matrix4().multiplyMatrices(inv, m.matrixWorld));
    const double w = b.max.x - b.min.x;
    const double h = b.max.y - b.min.y;
    // a pane across the front, ahead of the eyes, at their height
    if (b.max.z - b.min.z > 0.12 || w < 0.6 || b.max.z > eye.z - 0.2 || b.max.y < eye.y || b.min.y > eye.y) return;
    if (w * h > bestArea) {
      bestArea = w * h;
      best = b;
    }
  });
  if (!best) return std::nullopt;
  const Box3& b = *best;
  return flatGlass(b.min.x + 0.02, b.max.x - 0.02, b.min.y + 0.02, b.max.y - 0.02, b.max.z + 0.01);
}

WindscreenView::WindscreenView() {
  stateTex_ = dataTexture(std::vector<uint8_t>(STATE_COLS * STATE_ROWS * 4), STATE_COLS, STATE_ROWS, PixelFormat::RGBA);
  genTex_ = dataTexture(std::vector<uint8_t>(STATE_COLS * STATE_ROWS), STATE_COLS, STATE_ROWS, PixelFormat::Red);
  stateTex_->magFilter = Filter::Linear;
  stateTex_->minFilter = Filter::Linear;
  genTex_->magFilter = Filter::Nearest;
  genTex_->minFilter = Filter::Nearest;
  u_.uState = uniform(stateTex_);
  u_.uGen = uniform(genTex_);
  material_ = shaderMaterial(VERT, fragment(),
                             {{"uState", u_.uState},
                              {"uGen", u_.uGen},
                              {"uBg", u_.uBg},
                              {"uSize", u_.uSize},
                              {"uRes", u_.uRes},
                              {"uHaveBg", u_.uHaveBg},
                              {"uLight", u_.uLight},
                              {"uFlow", u_.uFlow},
                              {"uRun", u_.uRun},
                              {"uArm0", u_.uArm0},
                              {"uArm1", u_.uArm1},
                              {"uIn", u_.uIn}},
                             {.transparent = true, .side = Side::Double, .depthWrite = false, .fog = false, .toneMapped = false});
}

WindscreenView::~WindscreenView() { attach(nullptr); }

WiperMode WindscreenView::cycle() {
  mode = nextWiperMode(mode);
  if (screen) screen->wipers.set(mode);
  return mode;
}

double WindscreenView::obscured() const { return screen ? screen->obscured() : 0; }

double WindscreenView::frame(const WindscreenFrame& f) {
  VehicleModel* model = f.detail ? f.model : nullptr;
  if (model != model_) attach(model);
  Windscreen* s = screen.get();
  Mesh* mesh = mesh_.get();
  if (!s || !mesh || !model) {
    blindFor = 0;
    return 0;
  }
  // how the vehicle moves: from where its model was a frame ago
  const Group& root = *model->root;
  Vector3 pos;
  pos.setFromMatrixPosition(root.matrixWorld);
  const double dt = std::max(1e-3, f.dt);
  double speed = 0;
  double cross = 0;
  if (havePos_) {
    const double vx = (pos.x - lastPos_.x) / dt;
    const double vz = (pos.z - lastPos_.z) / dt;
    Vector3 fwd(0, 0, -1);
    fwd.transformDirection(root.matrixWorld);
    Vector3 right(1, 0, 0);
    right.transformDirection(root.matrixWorld);
    speed = vx * fwd.x + vz * fwd.z;
    // (a jump – a new place, a reset – is no speed)
    if (std::abs(speed) > 120) speed = 0;
    // the air across the glass, as the vehicle meets it
    cross = (f.windX - vx) * right.x + (f.windZ - vz) * right.z;
    if (!std::isfinite(cross) || std::abs(cross) > 60) cross = 0;
  }
  lastPos_.copy(pos);
  havePos_ = true;
  const ScreenWeather w = {.rain = f.rain, .snow = f.snow, .temperature = f.temperature, .roadWet = f.roadWet, .speed = speed, .crossWind = cross, .heater = f.heater};
  s->wipers.set(mode);
  s->step(f.dt, w, f.washer);
  const double ob = s->obscured();
  blindFor = f.cockpit && ob > 0.6 && speed > 2 ? blindFor + f.dt : 0;
  mesh->visible = f.cockpit;
  if (!f.cockpit) return speed;
  upload(*s);
  u_.uLight->value = std::max(0.05, std::min(1.0, f.light));
  std::get<Vector2>(u_.uFlow->value).set(std::fmod(s->flow.x, 50), std::fmod(s->flow.y, 50));
  u_.uRun->value = s->running;
  Vector4* arms[2] = {&std::get<Vector4>(u_.uArm0->value), &std::get<Vector4>(u_.uArm1->value)};
  for (int k = 0; k < 2 && k < static_cast<int>(s->arms.size()); k++) {
    const WiperArm& a = s->arms[static_cast<size_t>(k)];
    // (parked they lie at the foot of the glass, where the model has its own)
    arms[k]->set(a.px, a.py, s->armAngle(k), s->wipers.running() ? a.rOut : 0);
  }
  std::get<Vector2>(u_.uIn->value).set(s->arms.size() > 0 ? s->arms[0].rIn : 0, s->arms.size() > 1 ? s->arms[1].rIn : 0);
  // the picture just drawn: what the glass blurs in the next frame (only while there is something on it)
  if ((ob > 0.002 || s->wipers.running()) && f.renderer && f.scene && f.camera) keepPicture(*f.renderer, *f.scene, *f.camera);
  return speed;
}

void WindscreenView::attach(VehicleModel* model) {
  if (mesh_) {
    mesh_->removeFromParent();
    mesh_->geometry->dispose();
    mesh_.reset();
  }
  model_ = model;
  screen.reset();
  havePos_ = false;
  blindFor = 0;
  if (!model) return;
  std::optional<GlassShape> shape = findGlass(*model);
  if (!shape) return;
  screen = std::make_unique<Windscreen>(shape->width, shape->height, STATE_COLS, STATE_ROWS);
  screen->wipers.set(mode);
  std::get<Vector2>(u_.uSize->value).set(shape->width, shape->height);
  auto mesh = make<Mesh>(shape->geometry, material_);
  mesh->name = "windscreen weather";
  mesh->renderOrder = 20;
  mesh->frustumCulled = false;
  mesh->castShadow = false;
  mesh->receiveShadow = false;
  mesh->visible = false;
  model->root->add(mesh);
  mesh_ = mesh;
}

void WindscreenView::upload(const Windscreen& s) {
  std::vector<uint8_t>& d = *stateTex_->data;
  const int n = STATE_COLS * STATE_ROWS;
  for (int k = 0; k < n; k++) {
    const size_t o = static_cast<size_t>(k) * 4;
    const size_t i = static_cast<size_t>(k);
    d[o] = static_cast<uint8_t>(jsRound(s.film[i] * 255.0));
    d[o + 1] = static_cast<uint8_t>(jsRound(s.snow[i] * 255.0));
    d[o + 2] = static_cast<uint8_t>(jsRound(std::min(1.0, static_cast<double>(s.dirt[i]) + s.smear[i]) * 255));
    d[o + 3] = static_cast<uint8_t>(jsRound(s.drops[i] * 255.0));
  }
  std::copy(s.gen.begin(), s.gen.begin() + std::min<size_t>(s.gen.size(), genTex_->data->size()), genTex_->data->begin());
  stateTex_->needsUpdate();
  genTex_->needsUpdate();
}

void WindscreenView::keepPicture(Renderer& renderer, Scene& scene, Camera& camera) {
  if (renderer.getRenderTarget() != nullptr) return;
  const int w = renderer.bufferWidth();
  const int h = renderer.bufferHeight();
  if (w < 2 || h < 2) return;
  if (!offscreen_ || offscreen_->width != w || offscreen_->height != h) {
    u_.uHaveBg->value = 0.0;
    std::get<Vector2>(u_.uRes->value).set(w, h);
  }
  // (the glass itself is in the picture just drawn: it must not be captured,
  //  or the blur would show last frame's water.  Draw the scene behind the
  //  glass once into a target of its own – the picture the player sees keeps
  //  the glass – and read that.  The original copies the target into a texture
  //  of its own; here the target's own picture is read.)
  const bool wasVisible = mesh_ ? mesh_->visible : false;
  if (mesh_) mesh_->visible = false;
  offscreen_ = ensureTarget(offscreen_, w, h);
  u_.uBg->value = offscreen_->texture;
  renderer.setRenderTarget(offscreen_);
  renderer.render(scene, camera);
  renderer.setRenderTarget(nullptr);
  if (mesh_) mesh_->visible = wasVisible;
  u_.uHaveBg->value = 1.0;
}

void WindscreenView::dispose() {
  attach(nullptr);
  material_->dispose();
  stateTex_->dispose();
  genTex_->dispose();
  if (offscreen_) offscreen_->dispose();
  offscreen_.reset();
}

}  // namespace fs
