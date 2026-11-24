// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/headlights.ts @ v1.7.7
#include "render/headlights.h"

#include <cmath>

#include "core/js.h"

namespace fs {

namespace {

constexpr uint32_t WARM = 0xfff1dc;

/** A flat fan of light in the vehicle's frame: from `z0` to `z1` ahead (both negative), half-widths w0 … w1, shifted `shift` m to the right at the far end. */
GeometryPtr poolGeometry(double z0, double z1, double w0, double w1, double shift) {
  const int rows = 8;
  const int cols = 6;
  std::vector<float> pos;
  std::vector<float> col;
  std::vector<uint32_t> idx;
  for (int i = 0; i <= rows; i++) {
    const double t = static_cast<double>(i) / rows;
    const double z = z0 + (z1 - z0) * t;
    const double w = w0 + (w1 - w0) * std::sqrt(t);
    // brightest a little way ahead, fading out towards the far end
    const double along = jsMin(1, t * 5) * std::pow(1 - t, 1.4);
    for (int j = 0; j <= cols; j++) {
      const double u = static_cast<double>(j) / cols - 0.5;
      const double across = 1 - (2 * u) * (2 * u);
      const double k = jsMax(0, along * across);
      pos.push_back(static_cast<float>(u * 2 * w + shift * t));
      pos.push_back(0);
      pos.push_back(static_cast<float>(z));
      col.push_back(static_cast<float>(k));
      col.push_back(static_cast<float>(k));
      col.push_back(static_cast<float>(k));
    }
  }
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < cols; j++) {
      const uint32_t a = static_cast<uint32_t>(i * (cols + 1) + j);
      const uint32_t b = a + static_cast<uint32_t>(cols) + 1;
      idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
  auto g = make<BufferGeometry>();
  g->setAttribute("position", attr(std::move(pos), 3));
  g->setAttribute("color", attr(std::move(col), 3));
  g->setIndex(idx);
  return g;
}

constexpr int MAX_POINTS = 4 * 400;

}  // namespace

PlayerLights::PlayerLights(const ObjectPtr& scene)
    : scene_(scene),
      dipped_(spotLight(Color(WARM), 0, 95, 0.42, 0.85, 1.5)),
      main_(spotLight(Color(0xffffff), 0, 240, 0.3, 0.6, 1.5)),
      poolMat_(basic({.vertexColors = true,
                      .transparent = true,
                      .depthWrite = false,
                      .fog = false,
                      .toneMapped = false,
                      .blending = Blending::Additive,
                      .polygonOffset = true,
                      .polygonOffsetFactor = -2,
                      .polygonOffsetUnits = -4})) {
  group->name = "headlights";
  for (const LightPtr& l : {dipped_, main_}) {
    l->castShadow = false;
    group->add(l, l->target);
  }
  // (the shapes start at the bumper: placed by `front` each frame through poolFrame's children)
  poolLow_ = make<Mesh>(poolGeometry(-1, -34, 1.1, 5.5, 2.2), poolMat_);
  poolHighMat_ = poolMat_->clone();
  poolHigh_ = make<Mesh>(poolGeometry(-2, -80, 1.4, 11, 0), poolHighMat_);
  for (const std::shared_ptr<Mesh>& m : {poolLow_, poolHigh_}) {
    m->frustumCulled = false;
    m->renderOrder = 2;
    poolFrame_->add(m);
  }
  poolFrame_->visible = false;
  scene_->add(poolFrame_);
}

void PlayerLights::update(const PlayerLightState& s) {
  const bool on = s.dark > 0.02 && s.root && (s.low || s.high);
  Object3D* const root = s.root;
  // the pool on the road (low graphics)
  const bool pool = on && s.cheap;
  poolFrame_->visible = pool;
  if (pool && root) {
    root->updateWorldMatrix(true, false);
    poolFrame_->matrixAutoUpdate = false;
    poolFrame_->matrix.copy(root->matrixWorld).multiply(tmpM_.makeTranslation(0, 0.06, -s.front));
    poolFrame_->matrixWorldNeedsUpdate = true;
    poolLow_->visible = s.low || s.high;
    poolHigh_->visible = s.high;
    poolMat_->color.setScalar(0.55 * s.dark);
    poolHighMat_->color.setScalar(0.4 * s.dark);
  }
  // the spotlights (medium graphics and up): into the scene the first time it is dark
  const bool spots = on && !s.cheap;
  if (spots && !added_) {
    added_ = true;
    scene_->add(group);
  }
  if (!added_) return;
  if (!spots || !root) {
    dipped_->intensity = 0;
    main_->intensity = 0;
    return;
  }
  root->updateWorldMatrix(true, false);
  const Matrix4& mw = root->matrixWorld;
  lamp_.set(0, s.height, -s.front - 0.1).applyMatrix4(mw);
  // dipped: down onto the road some 35 m ahead, a little to the right (the right verge lit further)
  dipped_->position.copy(lamp_);
  dipped_->target->position.copy(aim_.set(1.6, s.height - 0.95, -s.front - 35).applyMatrix4(mw));
  dipped_->target->updateMatrixWorld();
  dipped_->intensity = 320 * s.dark;
  // main: straight ahead, level, far
  main_->position.copy(lamp_);
  main_->target->position.copy(aim_.set(0, s.height - 0.6, -s.front - 120).applyMatrix4(mw));
  main_->target->updateMatrixWorld();
  main_->intensity = s.high ? 1700 * s.dark : 0;
}

TrafficGlow::TrafficGlow()
    : pos_(attr(std::vector<float>(MAX_POINTS * 3), 3)), col_(attr(std::vector<float>(MAX_POINTS * 3), 3)), size_(attr(std::vector<float>(MAX_POINTS), 1)) {
  pos_->setDynamic();
  col_->setDynamic();
  size_->setDynamic();
  geo_->setAttribute("position", pos_);
  geo_->setAttribute("color", col_);
  geo_->setAttribute("aSize", size_);
  geo_->setDrawRange(0, 0);
  MaterialParams params;
  params.transparent = true;
  params.depthWrite = false;
  params.blending = Blending::Additive;
  const MaterialPtr mat = shaderMaterial(
      R"GLSL(
        attribute float aSize;
        attribute vec3 color;
        uniform float uScale;
        varying vec3 vColor;
        void main() {
          vColor = color;
          vec4 mv = modelViewMatrix * vec4(position, 1.0);
          gl_Position = projectionMatrix * mv;
          // (a lamp far off stays a dot of a pixel or two)
          gl_PointSize = max(2.0, aSize * uScale / max(0.5, -mv.z));
        })GLSL",
      R"GLSL(
        varying vec3 vColor;
        void main() {
          float d = length(gl_PointCoord - 0.5) * 2.0;
          if (d > 1.0) discard;
          float k = exp(-d * d * 7.0) + 0.6 * pow(1.0 - d, 6.0);
          gl_FragColor = vec4(vColor * k, 1.0);
          #include <tonemapping_fragment>
          #include <colorspace_fragment>
        })GLSL",
      {{"uScale", uScale_}}, params);
  points = make<Points>(geo_, mat);
  points->frustumCulled = false;
  points->renderOrder = 5;
  points->name = "traffic lamps";
  points->visible = false;
}

void TrafficGlow::update(const std::vector<GlowCar>& cars, double dark, const Camera& camera, double heightPx, const std::function<bool(int id)>& high, double glare) {
  const bool show = dark > 0.05 && cars.size() > 0;
  points->visible = show;
  if (!show) return;
  uScale_->value = heightPx / (2 * std::tan((camera.fov * PI) / 360));
  const double cx = camera.position.x;
  const double cz = camera.position.z;
  std::vector<float>& pos = pos_->f32;
  std::vector<float>& col = col_->f32;
  std::vector<float>& size = size_->f32;
  int n = 0;
  const auto put = [&](double x, double y, double z, double r, double g, double b, double s) {
    if (n >= MAX_POINTS) return;
    const size_t k = static_cast<size_t>(n);
    pos[k * 3] = static_cast<float>(x);
    pos[k * 3 + 1] = static_cast<float>(y);
    pos[k * 3 + 2] = static_cast<float>(z);
    col[k * 3] = static_cast<float>(r);
    col[k * 3 + 1] = static_cast<float>(g);
    col[k * 3 + 2] = static_cast<float>(b);
    size[k] = static_cast<float>(s);
    n++;
  };
  for (const GlowCar& c : cars) {
    const double dx = cx - c.x;
    const double dz = cz - c.z;
    const double d2 = dx * dx + dz * dz;
    if (d2 > 900 * 900) continue;
    // (`Math.sqrt(d2) || 1`)
    const double dist = std::sqrt(d2);
    const double d = dist == 0 || dist != dist ? 1 : dist;
    const double fx = -std::sin(c.yaw);
    const double fz = -std::cos(c.yaw);
    const double rx = -fz;
    const double rz = fx;
    // (seen from in front, or from behind: how squarely)
    const double face = (fx * dx + fz * dz) / d;
    const bool big = c.length > 7;
    const double half = c.length / 2;
    const double side = c.width / 2 - 0.28;
    if (face > 0.05) {
      const bool lit = high(c.id);
      const double k = dark * face * face * (lit ? 2.2 + 3 * glare : 1.1);
      const double s = lit ? 1.6 + 5 * glare : 0.75;
      const double hy = c.y + (big ? 0.9 : 0.66);
      for (const double sg : {-1.0, 1.0}) put(c.x + fx * (half + 0.12) + rx * side * sg, hy, c.z + fz * (half + 0.12) + rz * side * sg, k, k * 0.96, k * 0.86, s);
    } else if (face < -0.05) {
      const double k = dark * face * face * (c.braking ? 1.6 : 0.7);
      const double hy = c.y + (big ? 1.0 : 0.88);
      for (const double sg : {-1.0, 1.0}) put(c.x - fx * (half + 0.08) + rx * side * sg, hy, c.z - fz * (half + 0.08) + rz * side * sg, k, k * 0.06, k * 0.04, c.braking ? 0.7 : 0.45);
    }
  }
  geo_->setDrawRange(0, n);
  for (const char* name : {"position", "color", "aSize"}) {
    const AttrPtr a = geo_->getAttribute(name);
    a->clearUpdateRanges();
    a->addUpdateRange(0, static_cast<size_t>(n) * static_cast<size_t>(a->itemSize));
    a->needsUpdate();
  }
}

}  // namespace fs
