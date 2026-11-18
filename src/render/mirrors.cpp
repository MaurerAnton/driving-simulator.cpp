// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Working mirrors: the driver's eye reflected in the glass, seen through an
 * off-axis frustum whose near rectangle is the glass itself.
 *
 * Origin: src/render/mirrors.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The camera basis and the projection are the original's.
 */
#include "render/mirrors.h"

#include <cmath>

namespace fs {

Mirror::Mirror(const MirrorSpec& spec_) : spec(spec_) {
  target = std::make_shared<RenderTarget>(spec.resolution.first, spec.resolution.second);
  target->depthBuffer = true;
  target->generateMipmaps = false;

  // glass frame: "up" is world up projected into the mirror plane
  Vector3 n = spec.normal;
  n.normalize();
  localUp.set(0, 1, 0).addScaledVector(n, -n.y).normalize();
  // right as seen when looking AT the glass from the cabin: up × n
  localRight.crossVectors(localUp, n).normalize();

  double hw = spec.width / 2;
  double hh = spec.height / 2;
  auto p = [&](double sx, double sy) {
    return Vector3(spec.centre.x + localRight.x * hw * sx + localUp.x * hh * sy,
                   spec.centre.y + localRight.y * hw * sx + localUp.y * hh * sy,
                   spec.centre.z + localRight.z * hw * sx + localUp.z * hh * sy);
  };
  auto g = std::make_shared<BufferGeometry>();
  std::vector<double> pos;
  for (auto v : {p(-1, -1), p(1, -1), p(1, 1), p(-1, 1)}) {
    pos.push_back(v.x);
    pos.push_back(v.y);
    pos.push_back(v.z);
  }
  g->position = std::make_shared<BufferAttribute>(pos, 3);
  // The virtual camera looks at the glass from behind, where the viewer's
  // right-hand edge is on ITS left: u runs right → left for the viewer.
  g->uv = std::make_shared<BufferAttribute>(std::vector<double>{1, 0, 0, 0, 0, 1, 1, 1}, 2);
  g->index = std::make_shared<BufferAttribute>(std::vector<double>{0, 1, 2, 0, 2, 3}, 1);
  g->computeVertexNormals();
  mesh = std::make_unique<Mesh>(g, MaterialKind::Struct);
  mesh->name = "mirror " + spec.name;
  camera.matrixAutoUpdate = false;
}

std::unique_ptr<Mesh> Mirror::makeHousing(double depth, uint32_t color, double border) {
  const MirrorSpec& s = spec;
  GeometryPtr g = makeBoxGeometry(s.width + border * 2, s.height + border * 2, depth);
  Vector3 n = spec.normal;
  n.normalize();
  // orient the box so its local axes line up with the glass frame
  Matrix4 basis;
  // three.js Matrix4.makeBasis(xAxis, yAxis, zAxis)
  basis.set(localRight.x, localUp.x, n.x, 0, localRight.y, localUp.y, n.y, 0,
            localRight.z, localUp.z, n.z, 0, 0, 0, 0, 1);
  for (size_t i = 0; i < g->position->count(); i++) {
    Vector3 v(g->position->getX(i), g->position->getY(i), g->position->getZ(i));
    v.applyMatrix4(basis);
    g->position->setXYZ(i, v.x, v.y, v.z);
  }
  Vector3 c = spec.centre;
  c.addScaledVector(n, -depth / 2 - 0.002);
  g->translate(c.x, c.y, c.z);
  g->deleteAttribute("uv");
  // tint the housing
  Color col;
  col.setHex(color);
  std::vector<double> cols(g->position->count() * 3);
  for (size_t i = 0; i < g->position->count(); i++) {
    cols[i * 3] = col.r;
    cols[i * 3 + 1] = col.g;
    cols[i * 3 + 2] = col.b;
  }
  g->color = std::make_shared<BufferAttribute>(cols, 3);
  return std::make_unique<Mesh>(g, MaterialKind::Struct);
}

void Mirror::setEnabled(bool on) {
  enabled = on;
  // the glass shows the render target when on, and a neutral grey otherwise
  if (mesh) {
    Color c;
    c.setHex(on ? 0xffffff : 0x1c2228);
    size_t n = mesh->geometry && mesh->geometry->position ? mesh->geometry->position->count() : 0;
    std::vector<double> cols(n * 3);
    for (size_t i = 0; i < n; i++) {
      cols[i * 3] = c.r;
      cols[i * 3 + 1] = c.g;
      cols[i * 3 + 2] = c.b;
    }
    mesh->geometry->color = std::make_shared<BufferAttribute>(cols, 3);
  }
}

void Mirror::render(WebGLRenderer& renderer, Scene& scene, const Matrix4& carMatrix,
                    const Vector3& eyeWorld, double far) {
  if (!enabled) return;
  const MirrorSpec& s = spec;
  Vector3 c = spec.centre;
  c.applyMatrix4(carMatrix);
  Vector3 n = spec.normal;
  n.transformDirection(carMatrix);
  Vector3 up = localUp;
  up.transformDirection(carMatrix);
  Vector3 right = localRight;
  right.transformDirection(carMatrix);
  Vector3 eye = eyeWorld;
  // distance of the eye in front of the glass
  Vector3 tmp;
  tmp.subVectors(eye, c);
  double d = tmp.dot(n);
  if (d < 0.02) return;  // eye behind the mirror
  // mirror image of the eye
  Vector3 virt = eye;
  virt.addScaledVector(n, -2 * d);
  // camera basis: looks along +n (through the glass into the reflected world);
  // its right-hand side is the viewer's left
  Vector3 camRight = right;
  camRight.negate();
  Vector3 back = n;
  back.negate();
  // write the local matrix: the renderer re-derives matrixWorld from it
  camera.matrix.makeBasis(camRight, up, back);
  camera.matrix.setPosition(virt);
  camera.matrixWorld.copy(camera.matrix);
  camera.matrixWorldInverse.copy(camera.matrixWorld);
  camera.matrixWorldInverse.invert();
  // off-axis frustum: the glass rectangle sits in the near plane
  Vector3 rel;
  rel.subVectors(c, virt);
  double cx = rel.dot(camRight);
  double cy = rel.dot(up);
  double hw = (s.width / 2) * s.convex;
  double hh = (s.height / 2) * s.convex;
  double near = d;
  camera.projectionMatrix.makePerspective(cx - hw, cx + hw, cy + hh, cy - hh, near, far);
  camera.projectionMatrixInverse.copy(camera.projectionMatrix);
  camera.projectionMatrixInverse.invert();

  auto prevTarget = renderer.getRenderTarget();
  bool prevVisible = mesh ? mesh->visible : true;
  if (mesh) mesh->visible = false;
  renderer.setRenderTarget(target);
  renderer.render(scene, camera);
  renderer.setRenderTarget(prevTarget);
  if (mesh) mesh->visible = prevVisible;
}

Vector3 mirrorNormal(const Vector3& centre, const Vector3& eye, const Vector3& look) {
  Vector3 toEye;
  toEye.subVectors(eye, centre);
  toEye.normalize();
  Vector3 l = look;
  l.normalize();
  toEye.add(l);
  toEye.normalize();
  return toEye;
}

}  // namespace fs
