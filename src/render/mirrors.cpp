// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/mirrors.ts @ v1.7.7
#include "render/mirrors.h"

#include "core/js.h"
#include "engine/geometries.h"

namespace fs {

namespace {

Vector3 _n;
Vector3 _up;
Vector3 _right;
Vector3 _c;
Vector3 _eye;
Vector3 _virt;
Vector3 _tmp;
Vector3 _back;
Vector3 _rel;
Matrix4 _m;
Matrix4 _b;
Matrix4 _s;

}  // namespace

Mirror::Mirror(const MirrorSpec& spec_) : spec(spec_) {
  target = make<RenderTarget>(spec.resolution[0], spec.resolution[1], true);
  target->texture->generateMipmaps = false;
  target->texture->minFilter = Filter::Linear;
  target->texture->magFilter = Filter::Linear;
  target->samples = 0;
  // linear, un-tonemapped picture; half floats avoid banding in the shadows
  target->texture->type = PixelType::HalfFloat;
  // glass frame: "up" is world up projected into the mirror plane
  const Vector3 n = spec.normal.clone().normalize();
  localUp_.set(0, 1, 0).addScaledVector(n, -n.y).normalize();
  // right as seen when looking AT the glass from the cabin: up × n
  localRight_.crossVectors(localUp_, n).normalize();
  const double hw = spec.width / 2;
  const double hh = spec.height / 2;
  GeometryPtr g = make<BufferGeometry>();
  std::vector<float> position;
  const auto p = [&](double sx, double sy) {
    position.push_back(static_cast<float>(spec.centre.x + localRight_.x * hw * sx + localUp_.x * hh * sy));
    position.push_back(static_cast<float>(spec.centre.y + localRight_.y * hw * sx + localUp_.y * hh * sy));
    position.push_back(static_cast<float>(spec.centre.z + localRight_.z * hw * sx + localUp_.z * hh * sy));
  };
  p(-1, -1);
  p(1, -1);
  p(1, 1);
  p(-1, 1);
  g->setAttribute("position", attr(std::move(position), 3));
  // The virtual camera looks at the glass from behind, where the viewer's
  // right-hand edge is on ITS left: u runs right → left for the viewer.
  g->setAttribute("uv", attr(std::vector<float>{1, 0, 0, 0, 0, 1, 1, 1}, 2));
  g->setIndex(std::vector<uint32_t>{0, 1, 2, 0, 2, 3});
  g->computeVertexNormals();
  // tone mapping and sRGB encoding happen here, when the glass is drawn to the screen
  MaterialPtr mat = basic({.map = target->texture, .fog = false});
  mesh = make<Mesh>(g, mat);
  mesh->name = "mirror " + spec.name;
  camera->matrixAutoUpdate = false;
}

std::shared_ptr<Mesh> Mirror::makeHousing(double depth, const MaterialPtr& material, double border) {
  const MirrorSpec& s = spec;
  GeometryPtr g = boxGeometry(s.width + border * 2, s.height + border * 2, depth);
  const Vector3 n = s.normal.clone().normalize();
  const Matrix4 basis = Matrix4().makeBasis(localRight_, localUp_, n);
  g->applyMatrix4(basis);
  const Vector3 c = s.centre.clone().addScaledVector(n, -depth / 2 - 0.002);
  g->translate(c.x, c.y, c.z);
  g->deleteAttribute("uv");
  return make<Mesh>(g, material);
}

void Mirror::setEnabled(bool on) {
  enabled = on;
  Material& mat = *mesh->material;
  mat.map = on ? target->texture : nullptr;
  mat.color.set(on ? 0xffffff : 0x1c2228);
  mat.needsUpdate();
}

void Mirror::render(Renderer& renderer, Scene& scene, const Matrix4& carMatrix, const Vector3& eyeWorld, double far) {
  // (a glass not shown – the inside mirror taken off into the HUD – needs no picture)
  if (!enabled || !mesh->visible) return;
  if (!aim(carMatrix, eyeWorld, far)) return;
  const RenderTargetPtr prevTarget = renderer.getRenderTarget();
  const bool prevVisible = mesh->visible;
  mesh->visible = false;
  renderer.setRenderTarget(target);
  renderer.render(scene, *camera);
  renderer.setRenderTarget(prevTarget);
  mesh->visible = prevVisible;
}

bool Mirror::aim(const Matrix4& carMatrix, const Vector3& eyeWorld, double far, double zoom, double aspect, double minTan) {
  const MirrorSpec& s = spec;
  _c.copy(s.centre).applyMatrix4(carMatrix);
  _n.copy(s.normal).transformDirection(carMatrix);
  _up.copy(localUp_).transformDirection(carMatrix);
  _right.copy(localRight_).transformDirection(carMatrix);
  _eye.copy(eyeWorld);
  // distance of the eye in front of the glass
  const double d = _tmp.subVectors(_eye, _c).dot(_n);
  if (d < 0.02) return false;   // eye behind the mirror
  // mirror image of the eye
  _virt.copy(_eye).addScaledVector(_n, -2 * d);
  // camera basis: looks along +n (through the glass into the reflected world);
  // its right-hand side is the viewer's left
  const Vector3& camRight = _tmp.copy(_right).negate();
  _back.copy(_n).negate();
  // write the local matrix: the renderer re-derives matrixWorld from it
  camera->matrix.makeBasis(camRight, _up, _back).setPosition(_virt);
  camera->matrixWorld.copy(camera->matrix);
  camera->matrixWorldInverse.copy(camera->matrixWorld).invert();
  // off-axis frustum: the glass rectangle sits in the near plane
  _rel.subVectors(_c, _virt);
  const double cx = _rel.dot(camRight);
  const double cy = _rel.dot(_up);
  double hw = ((s.width / 2) * s.convex) / zoom;
  double hh = ((s.height / 2) * s.convex) / zoom;
  if (aspect > 0) {
    if (hw / hh < aspect) hw = hh * aspect;
    else hh = hw / aspect;
  }
  if (hh < minTan * d) {
    const double k = (minTan * d) / hh;
    hw *= k;
    hh *= k;
  }
  const double near = d;
  camera->projectionMatrix.makePerspective(cx - hw, cx + hw, cy + hh, cy - hh, near, far);
  camera->projectionMatrixInverse.copy(camera->projectionMatrix).invert();
  return true;
}

void Mirror::place(double shift, double scale, const Vector3* eye) {
  const std::string key = jsStr(shift) + "," + jsStr(scale) + "," + (eye ? jsStr(eye->x) + "," + jsStr(eye->y) + "," + jsStr(eye->z) : std::string());
  if (key == placed_) return;
  placed_ = key;
  MirrorSpec& s = spec;
  if (!base_) base_ = Base{s.centre.clone(), s.normal.clone().normalize(), localRight_.clone(), localUp_.clone(), s.width, s.height};
  const Base& b = *base_;
  s.centre.copy(b.centre);
  s.centre.x += shift;
  s.width = b.width * scale;
  s.height = b.height * scale;
  s.normal.copy(b.normal);
  if (eye) {
    // the direction it showed: the eye's line reflected in the glass
    const Vector3& toEye0 = _tmp.subVectors(*eye, b.centre).normalize();
    const Vector3& look = _rel.copy(b.normal).multiplyScalar(2 * b.normal.dot(toEye0)).sub(toEye0);
    s.normal.copy(mirrorNormal(s.centre, *eye, look));
  }
  const Vector3& n = s.normal.normalize();
  localUp_.set(0, 1, 0).addScaledVector(n, -n.y).normalize();
  localRight_.crossVectors(localUp_, n).normalize();
  // the move: about the old centre, turned from the old frame of the glass to the new one, scaled, to the new centre
  _b.makeBasis(b.right, b.up, b.normal).invert();
  _m.makeBasis(localRight_, localUp_, n).multiply(_b);
  _s.makeScale(scale, scale, scale);
  _m.multiply(_s);
  _m.multiply(_s.makeTranslation(-b.centre.x, -b.centre.y, -b.centre.z));
  _m.premultiply(_s.makeTranslation(s.centre.x, s.centre.y, s.centre.z));
  const auto move = [](Object3D& o) {
    _m.decompose(o.position, o.quaternion, o.scale);
    o.updateMatrix();
  };
  move(*mesh);
  for (const ObjectPtr& o : attached) move(*o);
}

void Mirror::dispose() {
  target->dispose();
  mesh->geometry->dispose();
}

Vector3 mirrorNormal(const Vector3& centre, const Vector3& eye, const Vector3& look) {
  Vector3 toEye = eye.clone().sub(centre).normalize();
  return toEye.add(look.clone().normalize()).normalize();
}

}  // namespace fs
