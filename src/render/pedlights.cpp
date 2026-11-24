// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/pedlights.ts @ v1.7.7
#include "render/pedlights.h"

#include <string>

#include "engine/geometries.h"
#include "platform/canvas.h"
#include "sim/signals.h"

namespace fs {

namespace {

constexpr double HEAD_Y = 2.25;
constexpr double BOX_Y = 1.05;

/** The two figures side by side, white on dark (the lamp's colour comes from the instance). */
TexturePtr figures() {
  const Canvas c(128, 64);
  const Ctx2D g = c.getContext2D();
  g.fillStyle("#161616");
  g.fillRect(0, 0, 128, 64);
  g.fillStyle("#ffffff");
  g.strokeStyle("#ffffff");
  g.lineCap("round");
  // standing: head, body, arms down, legs together
  g.beginPath();
  g.arc(32, 13, 6, 0, PI * 2);
  g.fill();
  g.fillRect(25, 21, 14, 20);
  g.lineWidth(5);
  g.beginPath();
  g.moveTo(22, 23);
  g.lineTo(21, 38);
  g.moveTo(42, 23);
  g.lineTo(43, 38);
  g.moveTo(29, 41);
  g.lineTo(29, 58);
  g.moveTo(35, 41);
  g.lineTo(35, 58);
  g.stroke();
  // walking: leaning forwards, legs and arms apart
  g.beginPath();
  g.arc(98, 12, 6, 0, PI * 2);
  g.fill();
  g.lineWidth(9);
  g.beginPath();
  g.moveTo(96, 22);
  g.lineTo(93, 38);
  g.stroke();
  g.lineWidth(5);
  g.beginPath();
  g.moveTo(95, 24);
  g.lineTo(84, 33);
  g.moveTo(96, 24);
  g.lineTo(107, 32);
  g.moveTo(93, 38);
  g.lineTo(84, 57);
  g.moveTo(93, 38);
  g.lineTo(104, 57);
  g.stroke();
  TexturePtr t = canvasTexture(c);
  t->srgb = true;
  t->anisotropy = 4;
  return t;
}

/** "Signal kommt" in white on black. */
TexturePtr requestText() {
  const Canvas c(128, 48);
  const Ctx2D g = c.getContext2D();
  g.fillStyle("#101010");
  g.fillRect(0, 0, 128, 48);
  g.fillStyle("#ffffff");
  g.font("bold 19px Arial, sans-serif");
  g.textAlign("center");
  g.textBaseline("middle");
  g.fillText("Signal", 64, 15);
  g.fillText("kommt", 64, 35);
  TexturePtr t = canvasTexture(c);
  t->srgb = true;
  return t;
}

/** A quad facing −z, showing the part u0…u1 of a texture. */
GeometryPtr quad(double w, double h, double u0, double u1) {
  GeometryPtr g = planeGeometry(w, h);
  const AttrPtr uv = g->getAttribute("uv");
  for (size_t i = 0; i < uv->count(); i++) uv->setX(i, uv->getX(i) < 0.5 ? u0 : u1);
  // (PlaneGeometry faces +z: turned round)
  g->rotateY(PI);
  return g;
}

const Color RED(1, 0.16, 0.1);
const Color GREEN(0.25, 1, 0.5);
const Color OFF(0.13, 0.13, 0.13);
const Color WHITE(1, 0.95, 0.85);

}  // namespace

PedLightRenderer::PedLightRenderer()
    : figureTex_(figures()),
      requestTex_(requestText()),
      figureMat_(basic({.map = figureTex_, .fog = true, .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -4})),
      requestMat_(basic({.map = requestTex_, .fog = true, .polygonOffset = true, .polygonOffsetFactor = -2, .polygonOffsetUnits = -4})),
      housingMat_(lambert({.color = Color(0x1d2022)})),
      boxMat_(lambert({.color = Color(0xe8b800)})) {
  group->name = "pedestrian lights";
}

void PedLightRenderer::rebuild(const std::vector<PedPole>& poles) {
  for (const auto& m : meshes_) {
    group->remove(m);
    m->geometry->dispose();
    m->dispose();
  }
  meshes_.clear();
  poles_ = poles;
  const int n = static_cast<int>(poles.size());
  shown_.assign(poles.size(), -1);
  asked_.assign(poles.size(), -1);
  if (n == 0) {
    red_ = green_ = lamp_ = nullptr;
    return;
  }
  const auto bare = [](const GeometryPtr& g) {
    GeometryPtr k = g->index ? g->toNonIndexed() : g;
    k->deleteAttribute("uv");
    return k;
  };
  // pole and head (the head turned to face across the road is placed by the instance; the pole is round)
  const GeometryPtr housing = mergeGeometries({bare(cylinderGeometry(0.045, 0.05, HEAD_Y + 0.35, 8, 1, true)->translate(0, (HEAD_Y + 0.35) / 2, 0).shared_from_this()),
                                               bare(boxGeometry(0.26, 0.62, 0.17)->translate(0, HEAD_Y, 0.05).shared_from_this())},
                                              false);
  housing->computeVertexNormals();
  // the box with its button, on the pole (turned by its own instance)
  const GeometryPtr box = mergeGeometries({bare(boxGeometry(0.13, 0.26, 0.1)->translate(0, BOX_Y, -0.09).shared_from_this()),
                                           bare(cylinderGeometry(0.03, 0.03, 0.02, 12)->rotateX(PI / 2).translate(0, BOX_Y - 0.06, -0.145).shared_from_this())},
                                          false);
  box->computeVertexNormals();
  const auto mk = [this](const GeometryPtr& geo, const MaterialPtr& mat, int count) {
    auto m = make<InstancedMesh>(geo, mat, count);
    m->frustumCulled = false;
    meshes_.push_back(m);
    group->add(m);
    return m;
  };
  const auto housingMesh = mk(housing, housingMat_, n);
  const auto boxMesh = mk(box, boxMat_, n);
  // (the face of the head is at z −0.035, that of the box at −0.14: the lamps a few millimetres in front)
  const auto red = (red_ = mk(quad(0.2, 0.2, 0, 0.5)->translate(0, HEAD_Y + 0.15, -0.039).shared_from_this(), figureMat_, n));
  const auto green = (green_ = mk(quad(0.2, 0.2, 0.5, 1)->translate(0, HEAD_Y - 0.15, -0.039).shared_from_this(), figureMat_, n));
  const auto lamp = (lamp_ = mk(quad(0.1, 0.0375, 0, 1)->translate(0, BOX_Y + 0.07, -0.144).shared_from_this(), requestMat_, n));
  housingMesh->castShadow = true;
  Matrix4 m4;
  Quaternion q;
  const Vector3 up(0, 1, 0);
  const Vector3 one(1, 1, 1);
  Vector3 at;
  for (int i = 0; i < n; i++) {
    const PedPole& p = poles[static_cast<size_t>(i)];
    at.set(p.x, p.y, p.z);
    q.setFromAxisAngle(up, p.yaw);
    m4.compose(at, q, one);
    housingMesh->setMatrixAt(i, m4);
    red->setMatrixAt(i, m4);
    green->setMatrixAt(i, m4);
    red->setColorAt(i, OFF);
    green->setColorAt(i, OFF);
    q.setFromAxisAngle(up, p.boxYaw);
    m4.compose(at, q, one);
    boxMesh->setMatrixAt(i, m4);
    lamp->setMatrixAt(i, m4);
    lamp->setColorAt(i, OFF);
  }
  for (const auto& m : meshes_) m->instanceMatrix->needsUpdate();
}

void PedLightRenderer::update(SignalSystem& signals) {
  if (!red_ || !green_ || !lamp_) return;
  bool lit = false;
  bool asked = false;
  for (size_t i = 0; i < poles_.size(); i++) {
    const PedPole& p = poles_[i];
    const std::string st = signals.pedestrian(p.controller, p.group);
    const int8_t v = st == "green" ? 1 : st == "red" ? 0 : 2;
    if (v != shown_[i]) {
      shown_[i] = v;
      red_->setColorAt(static_cast<int>(i), v == 0 ? RED : OFF);
      green_->setColorAt(static_cast<int>(i), v == 1 ? GREEN : OFF);
      lit = true;
    }
    const int8_t a = signals.requested(p.controller) && st == "red" ? 1 : 0;
    if (a != asked_[i]) {
      asked_[i] = a;
      lamp_->setColorAt(static_cast<int>(i), a ? WHITE : OFF);
      asked = true;
    }
  }
  if (lit) {
    if (red_->instanceColor) red_->instanceColor->needsUpdate();
    if (green_->instanceColor) green_->instanceColor->needsUpdate();
  }
  if (asked && lamp_->instanceColor) lamp_->instanceColor->needsUpdate();
}

void PedLightRenderer::dispose() {
  rebuild({});
  figureTex_->dispose();
  requestTex_->dispose();
  figureMat_->dispose();
  requestMat_->dispose();
  housingMat_->dispose();
  boxMat_->dispose();
}

}  // namespace fs
