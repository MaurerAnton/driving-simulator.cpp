// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/forklift.ts @ v1.7.7
#include "render/forklift.h"

#include <cmath>
#include <utility>

#include "core/js.h"
#include "engine/geometries.h"
#include "sim/forklift.h"

namespace fs {

namespace {

/** (the original's module constants: made when first asked for, held together) */
struct ForkMaterials {
  const MaterialPtr paint = lambert({.color = 0xe8a311});
  const MaterialPtr weight = lambert({.color = 0x3b3f45});
  const MaterialPtr steel = lambert({.color = 0x2c2f33});
  const MaterialPtr mast = lambert({.color = 0x34383d});
  const MaterialPtr fork = lambert({.color = 0x24272a});
  const MaterialPtr chain = lambert({.color = 0x5b5f63});
  const MaterialPtr rubber = lambert({.color = 0x1b1c1e});
  const MaterialPtr rim = lambert({.color = 0xc9c3b5});
  const MaterialPtr seat = lambert({.color = 0x202224});
  const MaterialPtr chrome = phong({.color = 0xc9d0d6, .specular = 0x9aa4ad, .shininess = 90});
  const MaterialPtr lamp = basic({.color = 0xfff4d6});
  const MaterialPtr tail = lambert({.color = 0x7a1712});
  const MaterialPtr beacon = basic({.color = 0xff9a1a});
  const MaterialPtr beaconOff = lambert({.color = 0x8a4a10});
  const MaterialPtr stripe = lambert({.color = 0x161616});
};

const ForkMaterials& M() {
  // (outlives everything: the meshes that hold these are freed at exit in no fixed order)
  static const ForkMaterials* m = new ForkMaterials();
  return *m;
}

/** Parts collected by material and merged into one mesh each. */
class Parts {
 public:
  void add(const MaterialPtr& m, const GeometryPtr& g) {
    GeometryPtr n = g->index ? g->toNonIndexed() : g;
    std::vector<std::string> names;
    for (const auto& [name, a] : n->attributes) names.push_back(name);
    for (const std::string& name : names)
      if (name != "position" && name != "normal") n->deleteAttribute(name);
    for (auto& [mm, l] : lists_) {
      if (mm == m) {
        l.push_back(n);
        return;
      }
    }
    lists_.push_back({m, {n}});
  }

  /** A box from corner to corner. */
  void box(const MaterialPtr& m, double x0, double y0, double z0, double x1, double y1, double z1) {
    GeometryPtr g = boxGeometry(std::abs(x1 - x0), std::abs(y1 - y0), std::abs(z1 - z0));
    g->translate((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    add(m, g);
  }

  /** A round bar from a to b. */
  void rod(const MaterialPtr& m, const Vector3& a, const Vector3& b, double r, int seg = 8) {
    const double len = a.distanceTo(b);
    GeometryPtr g = cylinderGeometry(r, r, len, seg);
    Quaternion q;
    q.setFromUnitVectors(Vector3(0, 1, 0), b.clone().sub(a).normalize());
    g->applyQuaternion(q);
    g->translate((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2);
    add(m, g);
  }

  /** A cylinder lying along x. */
  void drum(const MaterialPtr& m, double x0, double x1, double y, double z, double r, int seg = 16) {
    GeometryPtr g = cylinderGeometry(r, r, std::abs(x1 - x0), seg);
    g->rotateZ(PI / 2);
    g->translate((x0 + x1) / 2, y, z);
    add(m, g);
  }

  void build(Object3D& into, bool shadows = true) {
    for (const auto& [m, list] : lists_) {
      auto mesh = make<Mesh>(mergeGeometries(list, false), m);
      mesh->castShadow = shadows;
      mesh->receiveShadow = shadows;
      into.add(mesh);
    }
    lists_.clear();
  }

 private:
  /** (a Map in the original: by material, in the order they came) */
  std::vector<std::pair<MaterialPtr, std::vector<GeometryPtr>>> lists_;
};

const Vector3 Z_AXIS(0, 0, 1);
Vector3 v3(double x, double y, double z) { return Vector3(x, y, z); }

/** A wheel: tyre and rim, its axis along x, its middle at the origin. */
std::shared_ptr<Group> wheel(double r, double w, bool low) {
  auto g = make<Group>();
  Parts p;
  p.drum(M().rubber, -w / 2, w / 2, 0, 0, r, low ? 12 : 20);
  p.drum(M().rim, -w / 2 - 0.004, w / 2 + 0.004, 0, 0, r * 0.58, low ? 10 : 16);
  if (!low)
    for (int i = 0; i < 6; i++) {
      const double a = (i / 6.0) * PI * 2;
      p.box(M().steel, -w / 2 - 0.01, std::sin(a) * r * 0.42 - 0.02, std::cos(a) * r * 0.42 - 0.02, w / 2 + 0.01, std::sin(a) * r * 0.42 + 0.02, std::cos(a) * r * 0.42 + 0.02);
    }
  p.build(*g);
  return g;
}

}  // namespace

ForkliftModel::ForkliftModel(const std::string& detail) : VehicleModel(Vector3(0, 1.86, 0.2)), low(detail == "low") {
  const ForkMaterials& K = M();
  operator_.x = 0;
  operator_.y = 1.13 - SEAT_HEIGHT;
  operator_.z = 0.36;
  operator_.yaw = 0;
  operator_.walk = -1;
  operator_.look = 9211;
  operator_.sit = true;
  const ForkliftSpec& S = FORKLIFT;
  const double zf = -S.wheelbase / 2;
  const double zr = S.wheelbase / 2;
  root->name = "forklift";
  root->add(tipF);
  tipF->position.set(0, 0, zf);
  tipF->add(tipS);
  tipS->add(body);
  body->position.set(0, 0, -zf);

  // ---- the chassis, the counterweight, the bonnet and the seat, the cowl, the guard
  Parts p;
  p.box(K.paint, -0.55, 0.2, -1.02, 0.55, 0.62, 0.6);
  // (the steps and the mudguards over the front wheels)
  for (double s : {-1.0, 1.0}) {
    p.box(K.paint, s * 0.36, 0.62, -1.02, s * 0.6, 0.98, -0.55);
    p.box(K.steel, s * 0.38, 0.3, -0.5, s * 0.58, 0.36, 0.05);
    p.box(K.paint, s * 0.4, 0.62, -0.05, s * 0.58, 1.0, 0.6);
  }
  // the bonnet (the engine or the battery under it) and the seat on it
  p.box(K.paint, -0.4, 0.62, -0.05, 0.4, 1.02, 0.62);
  p.box(K.seat, -0.25, 1.02, 0.12, 0.25, 1.13, 0.58);
  p.box(K.seat, -0.24, 1.13, 0.52, 0.24, 1.62, 0.62);
  p.box(K.seat, -0.3, 1.05, 0.25, -0.26, 1.28, 0.55);
  p.box(K.seat, 0.26, 1.05, 0.25, 0.3, 1.28, 0.55);
  // the counterweight, rounded at the back, its towing pin
  p.box(K.weight, -0.58, 0.22, 0.6, 0.58, 1.18, 1.05);
  p.drum(K.weight, -0.58, 0.58, 0.7, 1.05, 0.48, low ? 10 : 20);
  p.box(K.stripe, -0.58, 0.22, 1.05, 0.58, 0.36, 1.27);
  if (!low) p.drum(K.chrome, -0.05, 0.05, 0.62, 1.42, 0.035, 8);
  // the cowl ahead of the operator with its dashboard, the steering column
  p.box(K.paint, -0.36, 0.62, -0.98, 0.36, 1.3, -0.6);
  p.box(K.steel, -0.3, 1.3, -0.9, 0.3, 1.36, -0.62);
  p.rod(K.steel, v3(0, 1.25, -0.66), v3(0, 1.5, -0.36), 0.035);
  // the levers of the mast beside the seat
  if (!low)
    for (int i = 0; i < 3; i++) {
      p.rod(K.steel, v3(0.36, 1.0, -0.2 + i * 0.07), v3(0.36, 1.22, -0.24 + i * 0.07), 0.01, 5);
      p.box(K.seat, 0.34, 1.21, -0.26 + i * 0.07, 0.38, 1.26, -0.21 + i * 0.07);
    }
  // the overhead guard: four posts, the frame, the slats
  const double top = 2.18;
  for (double s : {-1.0, 1.0}) {
    p.rod(K.steel, v3(s * 0.5, 0.95, -0.82), v3(s * 0.5, top, -0.7), 0.04);
    p.rod(K.steel, v3(s * 0.48, 1.0, 0.82), v3(s * 0.48, top, 0.66), 0.04);
    p.box(K.steel, s * 0.46, top - 0.04, -0.75, s * 0.54, top + 0.03, 0.72);
  }
  p.box(K.steel, -0.54, top - 0.04, -0.78, 0.54, top + 0.03, -0.68);
  p.box(K.steel, -0.54, top - 0.04, 0.64, 0.54, top + 0.03, 0.72);
  if (!low)
    for (int i = 1; i < 7; i++) p.box(K.steel, -0.03 + (i - 3.5) * 0.14, top - 0.02, -0.68, 0.03 + (i - 3.5) * 0.14, top + 0.01, 0.64);
  // the lamps: work lamps on the front posts, tail lamps in the counterweight
  for (double s : {-1.0, 1.0}) {
    p.box(K.steel, s * 0.5 - 0.06, 1.85, -0.86, s * 0.5 + 0.06, 1.95, -0.76);
    p.box(K.lamp, s * 0.5 - 0.05, 1.86, -0.865, s * 0.5 + 0.05, 1.94, -0.855);
    p.box(K.tail, s * 0.45 - 0.07, 0.95, 1.03, s * 0.45 + 0.07, 1.06, 1.08);
  }
  p.box(K.steel, -0.08, top + 0.03, 0.55, 0.08, top + 0.07, 0.68);
  p.build(*body);
  beacon = make<Mesh>(cylinderGeometry(0.06, 0.07, 0.11, 12), K.beaconOff);
  beacon->position.set(0, top + 0.13, 0.61);
  body->add(beacon);

  // the steering wheel, towards the operator
  Parts sw;
  GeometryPtr ring = torusGeometry(0.17, 0.018, 6, low ? 14 : 24);
  sw.add(K.seat, ring);
  sw.box(K.seat, -0.17, -0.012, -0.012, 0.17, 0.012, 0.012);
  if (!low) {
    GeometryPtr knob = sphereGeometry(0.03, 8, 6);
    knob->translate(0.12, 0.08, 0.03);
    sw.add(K.seat, knob);
  }
  sw.build(*steeringWheel, false);
  steeringWheel->position.set(0, 1.5, -0.36);
  steeringWheel->rotation.x = -0.75;
  auto swHolder = make<Group>();
  swHolder->add(steeringWheel);
  body->add(swHolder);

  // ---- the wheels: the front ones on the drive axle, the rear ones steered on the swinging axle
  for (double s : {-1.0, 1.0}) {
    auto w = wheel(0.33, 0.22, low);
    w->position.set(s * S.halfTrack, 0.33, zf);
    body->add(w);
    wheelsF.push_back(w);
    auto holder = make<Group>();
    holder->position.set(s * 0.42, 0.27, zr);
    auto r = wheel(0.27, 0.18, low);
    holder->add(r);
    body->add(holder);
    steerR.push_back(holder);
    wheelsR.push_back(r);
  }

  // ---- the mast on its foot: the outer channels, the tilt cylinders
  mast->position.set(0, MAST_PIVOT, zf - S.heelAhead + 0.17);
  body->add(mast);
  const double heelZ = -0.17;
  Parts m;
  for (double s : {-1.0, 1.0}) {
    // a channel: web and flanges
    m.box(K.mast, s * 0.4, -0.25, 0.0, s * 0.33, 2.0, 0.03);
    m.box(K.mast, s * 0.4, -0.25, -0.12, s * 0.385, 2.0, 0.03);
    m.box(K.mast, s * 0.345, -0.25, -0.12, s * 0.33, 2.0, 0.03);
    // the chain anchors on the outer mast
    m.box(K.steel, s * 0.15, 0.62, 0.02, s * 0.1, 0.7, 0.08);
  }
  m.box(K.mast, -0.4, 1.9, 0.0, 0.4, 2.0, 0.06);
  m.box(K.mast, -0.4, -0.25, 0.0, 0.4, -0.1, 0.06);
  if (!low) m.box(K.mast, -0.4, 0.95, 0.03, 0.4, 1.03, 0.07);
  m.build(*mast);
  // the inner channels and their crosshead with the chain sheaves
  mast->add(inner);
  Parts n;
  for (double s : {-1.0, 1.0}) {
    n.box(K.mast, s * 0.32, -0.15, -0.1, s * 0.27, 2.05, -0.02);
    if (!low) n.drum(K.steel, s * 0.17, s * 0.09, 2.05, -0.06, 0.07, 12);
  }
  n.box(K.mast, -0.32, 1.97, -0.1, 0.32, 2.07, -0.02);
  n.box(K.mast, -0.32, -0.15, -0.1, 0.32, -0.07, -0.02);
  n.build(*inner);
  // the carriage on the inner channels, the side-shift on it, the backrest, the forks
  mast->add(carriage);
  Parts c;
  c.box(K.steel, -0.5, 0.0, heelZ + 0.05, 0.5, 0.12, heelZ + 0.11);
  c.box(K.steel, -0.5, 0.36, heelZ + 0.05, 0.5, 0.48, heelZ + 0.11);
  c.box(K.mast, -0.22, 0.0, heelZ + 0.11, 0.22, 0.48, heelZ + 0.17);
  c.build(*carriage);
  carriage->add(shifter);
  Parts f;
  // the backrest: a frame with bars
  f.box(K.steel, -0.52, 0.48, heelZ + 0.02, 0.52, 0.53, heelZ + 0.06);
  f.box(K.steel, -0.52, 1.18, heelZ + 0.02, 0.52, 1.23, heelZ + 0.06);
  for (double s : {-1.0, 1.0}) f.box(K.steel, s * 0.52 - 0.025, 0.48, heelZ + 0.02, s * 0.52 + 0.025, 1.23, heelZ + 0.06);
  if (!low)
    for (int i = -3; i <= 3; i++) f.box(K.steel, i * 0.13 - 0.012, 0.53, heelZ + 0.03, i * 0.13 + 0.012, 1.18, heelZ + 0.05);
  for (double s : {-1.0, 1.0}) {
    const double x = s * S.forkHalfGap;
    const double hw = S.forkWidth / 2;
    // the shank up the carriage and the blade, its tip tapered
    f.box(K.fork, x - hw, -S.forkThick, heelZ - S.forkThick, x + hw, 0.55, heelZ);
    f.box(K.fork, x - hw, -S.forkThick, heelZ - S.forkLength + 0.12, x + hw, 0, heelZ);
    f.box(K.fork, x - hw, -S.forkThick * 0.6, heelZ - S.forkLength, x + hw, -0.005, heelZ - S.forkLength + 0.12);
    f.box(K.steel, x - hw - 0.01, 0.4, heelZ - 0.01, x + hw + 0.01, 0.47, heelZ + 0.05);
  }
  f.build(*shifter);
  // the chains (scaled to their length each frame) and the tilt cylinders
  GeometryPtr chainGeo = boxGeometry(0.035, 1, 0.025);
  chainGeo->translate(0, 0.5, 0);
  GeometryPtr cylGeo = cylinderGeometry(0.045, 0.045, 1, 10);
  cylGeo->translate(0, 0.5, 0);
  cylGeo->rotateX(PI / 2);
  for (double s : {-1.0, 1.0}) {
    if (!low) {
      for (int k = 0; k < 2; k++) {
        auto ch = make<Mesh>(chainGeo, K.chain);
        ch->position.x = s * 0.13;
        mast->add(ch);
        chains.push_back(ch);
      }
    }
    auto cyl = make<Mesh>(cylGeo, K.chrome);
    cyl->castShadow = true;
    cyl->position.set(s * 0.42, 1.0, -0.62);
    body->add(cyl);
    cylinders.push_back(cyl);
  }

  // ---- the operator, in work clothes
  people.wear(workerLook(9211, "yellow"));
  body->add(people.group);
  people.update({operator_}, 0);
  poseParts(S.liftMin, 0, 0, 0, 0, {0, 0});
}

void ForkliftModel::setCabinOpacity(double) {
  /* an open cab: nothing to see through */
}

void ForkliftModel::update(const CarVisualState&, double, double) {
  /* posed by `pose` from the forklift's simulation */
}

void ForkliftModel::setOperator(bool on) {
  if (on == operatorShown) return;
  operatorShown = on;
  people.group->visible = on;
}

void ForkliftModel::poseParts(double lift, double tilt, double side, double steer, double wheelTurn, const std::array<double, 2>& rolled) {
  const ForkliftSpec& S = FORKLIFT;
  mast->rotation.x = tilt;
  // the carriage: the forks' top `lift` above the ground; the inner channels rise at half its speed past the free lift
  const double cy = lift - MAST_PIVOT;
  carriage->position.y = cy;
  inner->position.y = (lift - S.liftMin) / 2;
  shifter->position.x = side;
  // the chains: from the carriage up over the sheave, and down to the anchor on the outer mast
  if (!chains.empty()) {
    const double sheave = inner->position.y + 2.05;
    for (size_t i = 0; i < chains.size(); i += 2) {
      Mesh& a = *chains[i];
      a.position.set(a.position.x, cy + 0.45, -0.13);
      a.scale.y = std::max(0.02, sheave - (cy + 0.45));
      Mesh& b = *chains[i + 1];
      b.position.set(b.position.x, 0.7, -0.05);
      b.scale.y = std::max(0.02, sheave - 0.7);
    }
  }
  // the tilt cylinders: from the frame to the mast
  for (size_t i = 0; i < cylinders.size(); i++) {
    Mesh& cyl = *cylinders[i];
    const double s = i == 0 ? -1 : 1;
    tmpA.set(s * 0.42, 0.85, -0.66);
    tmpB.set(s * 0.4, 1.0, 0.03);
    mast->updateMatrix();
    tmpB.applyMatrix4(mast->matrix);
    cyl.position.copy(tmpA);
    const double len = tmpA.distanceTo(tmpB);
    cyl.quaternion.setFromUnitVectors(Z_AXIS, tmpB.sub(tmpA).divideScalar(len));
    cyl.scale.z = len;
  }
  // (the rear wheels point the other way: turning right, the rear swings out to the left)
  for (const auto& r : steerR) r->rotation.y = steer;
  for (const auto& w : wheelsF) w->rotation.x = -rolled[0] / 0.33;
  for (const auto& w : wheelsR) w->rotation.x = -rolled[1] / 0.27;
  steeringWheel->rotation.z = -wheelTurn;
}

void ForkliftModel::pose(const Forklift& f, double now) {
  root->updateMatrixWorld(true);
  poseParts(f.lift, f.tilt, f.side, f.steer, f.steer * 6, {f.rolled[0], f.rolled[1]});
  // tipping: the rear up about the front axle; sideways about the outer front wheel
  tipF->rotation.x = -f.pitch;
  const double side = f.rollSide;
  tipS->position.x = side * FORKLIFT.halfTrack;
  body->position.x = -side * FORKLIFT.halfTrack;
  tipS->rotation.z = -side * f.roll;
  beacon->material = int(std::floor(now * 2.5)) % 2 == 0 ? M().beacon : M().beaconOff;
  if (operatorShown) people.update({operator_}, now);
}

}  // namespace fs
