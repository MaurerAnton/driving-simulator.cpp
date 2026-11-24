// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/sky.ts @ v1.7.7
#include "render/sky.h"

#include <cmath>

#include "core/js.h"
#include "engine/geometries.h"

namespace fs {

Sky::Sky() {
  GeometryPtr geo = sphereGeometry(1, 32, 16);
  MaterialParams params;
  params.side = Side::Back;
  params.depthWrite = false;
  params.depthTest = false;
  params.fog = false;
  MaterialPtr mat = shaderMaterial(
      R"GLSL(
        varying vec3 vDir;
        void main() {
          vDir = normalize(position);
          vec4 p = modelViewMatrix * vec4(position, 0.0);
          gl_Position = projectionMatrix * vec4(p.xyz, 1.0);
          gl_Position.z = gl_Position.w; // at the far plane
        })GLSL",
      R"GLSL(
        uniform vec3 uSunDir;
        uniform vec3 uZenith;
        uniform vec3 uHorizon;
        uniform vec3 uSunColor;
        varying vec3 vDir;
        void main() {
          vec3 d = normalize(vDir);
          float h = clamp(d.y, -0.2, 1.0);
          float t = pow(1.0 - max(h, 0.0), 3.2);
          vec3 col = mix(uZenith, uHorizon, t);
          // below the horizon: fade to the fog colour so distant terrain blends in
          col = mix(col, uHorizon, smoothstep(0.0, -0.08, d.y));
          float s = max(dot(d, uSunDir), 0.0);
          col += uSunColor * (pow(s, 900.0) * 6.0 + pow(s, 24.0) * 0.22 + pow(s, 4.0) * 0.06);
          gl_FragColor = vec4(col, 1.0);
          #include <tonemapping_fragment>
          #include <colorspace_fragment>
        })GLSL",
      {{"uSunDir", uSunDir}, {"uZenith", uZenith}, {"uHorizon", uHorizon}, {"uSunColor", uSunColor}}, params);
  mat->name = "sky";
  dome = make<Mesh>(geo, mat);
  dome->frustumCulled = false;
  dome->renderOrder = -1000;
  dome->name = "sky";
  setSun(52, 215);
}

void Sky::setSun(double elevationDeg, double azimuthDeg) {
  const double el = (elevationDeg * PI) / 180;
  const double az = (azimuthDeg * PI) / 180;
  // compass azimuth: 0 = north (-Z), 90 = east (+X)
  const Vector3 dir(std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el));
  std::get<Vector3>(uSunDir->value).copy(dir);
  sun->position.copy(dir).multiplyScalar(400);
  const double day = MathUtils::smoothstep(elevationDeg, -4, 18);
  const double warm = 1 - MathUtils::smoothstep(elevationDeg, 2, 30);
  sun->color.setRGB(1, 0.96 - 0.3 * warm, 0.88 - 0.5 * warm);
  sun->intensity = 3.2 * day;
  hemi->intensity = 0.25 + 0.95 * day;
  Color zenith = Color(0x0a1020).lerp(Color(0x3a74c6), day);
  Color horizon = Color(0x1a2030).lerp(Color(0xc2d3e4), day);
  horizon.lerp(Color(0xf0b890), warm * day * 0.45);
  std::get<Color>(uZenith->value).copy(zenith);
  std::get<Color>(uHorizon->value).copy(horizon);
  fog->color.copy(horizon);
}

void Sky::follow(const Camera& camera, const Vector3& focus) {
  dome->position.copy(camera.position);
  dome->scale.setScalar(1);
  const Vector3& dir = std::get<Vector3>(uSunDir->value);
  double fx = focus.x;
  double fy = focus.y;
  double fz = focus.z;
  if (sun->castShadow) {
    // The shadow box moves with the car.  Moved in whole texels of the shadow map (along the
    // light's own axes) the edges of the shadows stay still instead of crawling.
    const Camera& cam = *sun->shadow.camera;
    const double texel = (cam.right - cam.left) / sun->shadow.mapSize.x;
    const Vector3& r = lightRight_.set(dir.z, 0, -dir.x).normalize();   // up × dir
    const Vector3& u = lightUp_.crossVectors(dir, r);
    const double a = jsRound((fx * r.x + fz * r.z) / texel) * texel;
    const double b = jsRound((fx * u.x + fy * u.y + fz * u.z) / texel) * texel;
    const double c = fx * dir.x + fy * dir.y + fz * dir.z;
    fx = r.x * a + u.x * b + dir.x * c;
    fy = u.y * b + dir.y * c;
    fz = r.z * a + u.z * b + dir.z * c;
  }
  sun->position.set(fx + dir.x * 300, fy + dir.y * 300, fz + dir.z * 300);
  sun->target->position.set(fx, fy, fz);
  sun->target->updateMatrixWorld();
}

}  // namespace fs
