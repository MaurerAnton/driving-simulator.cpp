// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/navscreen.ts @ v1.7.7
#include "render/navscreen.h"

#include <cmath>
#include <string>
#include <vector>

#include "core/js.h"
#include "engine/geometries.h"

namespace fs {

namespace {

/** the picture's pixels: the size of the car's centre display (the HUD draws its map to fit any) */
const int W = 512;
const int H = 288;

/** (made when the first screen is: it lives as long as the program) */
const MaterialPtr& BEZEL() {
  static const MaterialPtr* m = new MaterialPtr(lambert({.color = Color(0x16181b)}));
  return *m;
}

/** The still picture: streets, a route and the arrow of the vehicle, and a column of tiles – drawn once for all screens. */
const TexturePtr& still() {
  static const TexturePtr* stillPicture = nullptr;
  if (stillPicture) return *stillPicture;
  const Canvas c(W, H);
  const Ctx2D x = c.getContext2D();
  x.fillStyle("#121d29");
  x.fillRect(0, 0, W, H);
  x.lineCap("round");
  x.lineJoin("round");
  using Pts = std::vector<std::array<double, 2>>;
  const auto line = [&x](const Pts& pts, const std::string& colour, double width) {
    x.strokeStyle(colour);
    x.lineWidth(width);
    x.beginPath();
    for (size_t i = 0; i < pts.size(); i++) {
      if (i == 0) x.moveTo(pts[i][0], pts[i][1]);
      else x.lineTo(pts[i][0], pts[i][1]);
    }
    x.stroke();
  };
  for (const Pts& s : {
           Pts{{-10, 70}, {180, 58}, {380, 34}},
           Pts{{-10, 200}, {200, 180}, {380, 160}},
           Pts{{70, -10}, {92, 130}, {104, 300}},
           Pts{{262, -10}, {270, 150}, {282, 300}},
       })
    line(s, "#2c3d50", 16);
  line({{100, 300}, {96, 190}, {200, 180}, {268, 172}, {266, 40}, {262, -10}}, "#3d8bff", 8);
  x.fillStyle("#eef2f5");
  x.beginPath();
  x.moveTo(98, 210);
  x.lineTo(113, 246);
  x.lineTo(98, 238);
  x.lineTo(83, 246);
  x.closePath();
  x.fill();
  x.fillStyle("#0e1822");
  x.fillRect(380, 0, W - 380, H);
  int i = 0;
  for (const char* colour : {"#3d8bff", "#35c06a", "#ffb020"}) {
    x.fillStyle("#1a2836");
    x.fillRect(404, 26 + i * 80, 84, 62);
    x.fillStyle(colour);
    x.beginPath();
    x.arc(446, 57 + i * 80, 14, 0, PI * 2);
    x.fill();
    i++;
  }
  TexturePtr t = canvasTexture(c);
  t->srgb = true;
  t->anisotropy = 4;
  stillPicture = new TexturePtr(t);
  return *stillPicture;
}

}  // namespace

NavScreen::NavScreen(double width) : VehicleScreen(Canvas(W, H)) {
  texture_->srgb = true;
  texture_->anisotropy = 4;
  const double h = (width * H) / W;
  const std::shared_ptr<Mesh> frame = make<Mesh>(boxGeometry(width + 0.024, h + 0.024, 0.022), BEZEL());
  frame->position.z = -0.012;
  // a self-lit picture, as bright by night as by day
  face_ = make<Mesh>(planeGeometry(width, h), basic({.map = still(), .toneMapped = false}));
  face_->name = "screen";
  face_->position.z = 0.0012;
  group->add(frame, face_);
  group->name = "nav screen";
}

NavScreen& NavScreen::place(Object3D& parent, const Vector3& at, const Vector3& eye, const Vector3* mount) {
  Vector3 d = eye.clone().sub(at);
  group->position.copy(at);
  // (a plane looks along +z: turned about y towards the eye, then tilted up or down to it)
  group->rotation.set(-std::asin(jsMax(-1, jsMin(1, d.y / d.length()))), std::atan2(d.x, d.z), 0, Euler::YXZ);
  parent.add(group);
  if (mount) {
    // the arm: from behind the middle of the screen to the mount
    const Vector3 from = at.clone().addScaledVector(d.normalize(), -0.03);
    Vector3 along = mount->clone().sub(from);
    const double len = along.length();
    if (len > 0.01) {
      const std::shared_ptr<Mesh> arm = make<Mesh>(boxGeometry(0.03, 0.03, len), BEZEL());
      arm->position.copy(from).add(*mount).multiplyScalar(0.5);
      arm->quaternion.setFromUnitVectors(Vector3(0, 0, 1), along.divideScalar(len));
      parent.add(arm);
    }
  }
  return *this;
}

Object3D* NavScreen::hitMesh() { return face_.get(); }

void NavScreen::setLive(bool on) {
  Material& m = *face_->material;
  const TexturePtr& want = on ? texture_ : still();
  if (m.map != want) {
    m.map = want;
    m.needsUpdate();
  }
}

void NavScreen::drawn() { texture_->needsUpdate(); }

}  // namespace fs
