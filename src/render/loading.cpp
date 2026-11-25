// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/loading.ts @ v1.7.7
#include "render/loading.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/js.h"
#include "engine/geometries.h"
#include "platform/canvas.h"

namespace fs {

namespace {


constexpr double FORK_REACH = 1.2;
struct Speeds {
  double fork, back, jack, turn;
};
constexpr Speeds SPEED = {1.7, 1.1, 1.3, 1.6};

/** The yaw of a heading along (dx, dz). */
double headingOf(double dx, double dz) { return std::atan2(-dx, -dz); }

/** An actor's way, built move by move. */
class Way {
 public:
  std::vector<Key> keys;
  Way(double t, double x, double z, double yaw) { keys.push_back({t, x, z, yaw, 0.08, -1}); }

  Key& last() { return keys.back(); }

  /** Wait until t (or for a moment). */
  Way& wait(double until) {
    const Key k = last();
    if (until > k.t) {
      Key n = k;
      n.t = until;
      keys.push_back(n);
    }
    return *this;
  }

  /** Go straight to (x, z), facing `yaw` at the end (turning on the way), at `speed`. */
  Way& go(double x, double z, double yaw, double speed) {
    const Key k = last();
    const double d = std::hypot(x - k.x, z - k.z);
    double dy = yaw - k.yaw;
    dy -= jsRound(dy / (2 * PI)) * 2 * PI;
    const double t = std::max({d / speed, std::abs(dy) / SPEED.turn, 0.2});
    Key n = k;
    n.t = k.t + t;
    n.x = x;
    n.z = z;
    n.yaw = k.yaw + dy;
    keys.push_back(n);
    return *this;
  }

  /** Lift or lower the forks to h. */
  Way& lift(double h) { return lift(h, last().carry); }
  Way& lift(double h, int carry) {
    const Key k = last();
    Key n = k;
    n.t = k.t + std::max(0.3, std::abs(h - k.h) / 0.6);
    n.h = h;
    n.carry = carry;
    keys.push_back(n);
    return *this;
  }

  /** Take or leave a pallet (no time). */
  Way& hold(int carry) {
    last().carry = carry;
    return *this;
  }
};

// ------------------------------------------------------------------ the models

/** (the original's module constants: made when first asked for, kept to the end) */
struct LoadingMaterials {
  static MaterialPtr lam(uint32_t c) { return lambert({.color = c}); }
  const MaterialPtr wall = lam(0xa9b7c4);
  const MaterialPtr plinth = lam(0x8e8b84);
  const MaterialPtr floor = lam(0x8f8d88);
  const MaterialPtr roof = lam(0x6f7378);
  const MaterialPtr black = lam(0x1d1f22);
  const MaterialPtr steel = lam(0x8d949b);
  const MaterialPtr yellow = lam(0xf2b705);
  const MaterialPtr orange = lam(0xe0661a);
  const MaterialPtr wood = lam(0xb08a5a);
  const MaterialPtr dark = lam(0x2a2d31);
  const MaterialPtr light = basic({.color = 0xfff6dc});
  const MaterialPtr stripe = lam(0x202020);
  const std::array<MaterialPtr, 4> GOODS = {lam(0xb98c5a), lam(0xe8e4d8), lam(0x2f62c4), lam(0x9a7b4f)};
};

const LoadingMaterials& MAT() {
  static const LoadingMaterials* m = new LoadingMaterials();
  return *m;
}

std::shared_ptr<Mesh> boxMesh(double w, double h, double d, double x, double y, double z, const MaterialPtr& m) {
  auto mesh = make<Mesh>(boxGeometry(w, h, d), m);
  mesh->position.set(x, y + h / 2, z);
  mesh->castShadow = true;
  mesh->receiveShadow = true;
  return mesh;
}

/** A forklift: its forks along −z, the origin on the ground below its front axle; `forks` moves up and down. */
std::pair<std::shared_ptr<Group>, std::shared_ptr<Group>> forkliftModel() {
  const LoadingMaterials& M = MAT();
  auto g = make<Group>();
  g->add(boxMesh(1.05, 0.75, 1.9, 0, 0.25, 0.75, M.yellow));
  g->add(boxMesh(1.05, 0.6, 0.5, 0, 0.6, 1.55, M.dark));  // the counterweight
  for (const auto& [x, z] : {std::pair{-0.45, 0.05}, std::pair{0.45, 0.05}, std::pair{-0.45, 1.45}, std::pair{0.45, 1.45}})
    g->add(boxMesh(0.06, 1.25, 0.06, x, 1.0, z, M.dark));  // the guard's posts
  g->add(boxMesh(1.0, 0.06, 1.5, 0, 2.22, 0.75, M.dark));
  g->add(boxMesh(0.5, 0.12, 0.5, 0, 1.0, 0.95, M.black));  // the seat
  for (const auto& [x, z, r] : {std::array{-0.5, 0.0, 0.32}, std::array{0.5, 0.0, 0.32}, std::array{-0.48, 1.35, 0.26}, std::array{0.48, 1.35, 0.26}}) {
    auto w = make<Mesh>(cylinderGeometry(r, r, 0.22, 14), M.black);
    w->rotation.z = PI / 2;
    w->position.set(x, r, z);
    g->add(w);
  }
  // the mast at the front
  for (double x : {-0.3, 0.3}) g->add(boxMesh(0.08, 2.3, 0.12, x, 0.05, -0.45, M.dark));
  auto carriage = make<Group>();
  carriage->add(boxMesh(0.8, 0.5, 0.06, 0, 0, -0.55, M.dark));
  for (double x : {-0.2, 0.2}) carriage->add(boxMesh(0.1, 0.045, 1.15, x, 0, -0.55 - 0.575, M.steel));
  g->add(carriage);
  return {g, carriage};
}

/** A pallet truck: its forks along −z, its handle at the back (+z), the origin at the middle of the forks. */
std::pair<std::shared_ptr<Group>, std::shared_ptr<Group>> jackModel() {
  const LoadingMaterials& M = MAT();
  auto g = make<Group>();
  auto forks = make<Group>();
  for (double x : {-0.18, 0.18}) forks->add(boxMesh(0.16, 0.07, 1.15, x, 0.0, 0, M.orange));
  forks->add(boxMesh(0.55, 0.32, 0.18, 0, 0.0, 0.66, M.orange));
  g->add(forks);
  auto handle = make<Mesh>(cylinderGeometry(0.02, 0.02, 1.1, 6), M.dark);
  handle->position.set(0, 0.75, 0.95);
  handle->rotation.x = -0.45;
  g->add(handle);
  auto grip = boxMesh(0.32, 0.04, 0.04, 0, 1.24, 1.18, M.black);
  g->add(grip);
  return {g, forks};
}

/** The warehouse with its dock behind the truck: its wall at `wallZ`, 14 m wide, 11 m deep, its floor `floor` above the ground. */
std::shared_ptr<Group> depotModel(double wallZ, double floor) {
  const LoadingMaterials& M = MAT();
  auto g = make<Group>();
  constexpr double W = 14;
  constexpr double D = 11;
  constexpr double H = 7.2;
  constexpr double doorW = 2.9;
  constexpr double doorH = 3.0;
  constexpr double t = 0.25;
  // the front wall round the dock door, its plinth up to the floor
  const double z = wallZ + t / 2;
  g->add(boxMesh(W, floor + 2.2, t, 0, -2.2, z, M.plinth));
  const double side = (W - doorW) / 2;
  for (double s : {-1.0, 1.0}) g->add(boxMesh(side, H - floor, t, s * (doorW / 2 + side / 2), floor, z, M.wall));
  g->add(boxMesh(doorW, H - floor - doorH, t, 0, floor + doorH, z, M.wall));
  // the other walls, the roof and the floor inside
  for (double s : {-1.0, 1.0}) g->add(boxMesh(t, H + 2.2, D, s * (W / 2 - t / 2), -2.2, wallZ + D / 2, M.wall));
  g->add(boxMesh(W, H + 2.2, t, 0, -2.2, wallZ + D - t / 2, M.wall));
  g->add(boxMesh(W + 0.4, 0.35, D + 0.4, 0, H, wallZ + D / 2, M.roof));
  g->add(boxMesh(W - 2 * t, floor + 2.2, D - 2 * t, 0, -2.2, wallZ + D / 2, M.floor));
  // the dock: the leveller bridging the gap to the truck's floor, the shelter round the door, the bumpers
  g->add(boxMesh(2.0, 0.05, DOCK_GAP + 0.1, 0, floor - 0.05, wallZ - DOCK_GAP / 2 + 0.05, M.steel));
  for (double s : {-1.0, 1.0}) {
    g->add(boxMesh(0.25, floor - 0.1, DOCK_GAP - 0.15, s * 1.0, 0, wallZ - DOCK_GAP / 2, M.plinth));
    g->add(boxMesh(0.35, doorH + 0.4, 0.4, s * (doorW / 2 + 0.17), floor - 0.2, wallZ - 0.2, M.black));
    g->add(boxMesh(0.25, 0.3, 0.2, s * 1.35, floor - 0.45, wallZ - 0.1, M.black));
    // yellow and black at the dock's edge
    g->add(boxMesh(0.5, 0.1, 0.05, s * 0.7, floor - 0.12, wallZ - 0.01, M.yellow));
  }
  g->add(boxMesh(doorW + 0.7, 0.35, 0.4, 0, floor + doorH, wallZ - 0.2, M.black));
  // inside: racks along the side walls, lights under the roof
  for (double s : {-1.0, 1.0}) {
    for (int k = 0; k < 3; k++) {
      const double rz = wallZ + 2.5 + k * 2.8;
      for (double dz : {-1.2, 1.2}) g->add(boxMesh(0.08, 4.2, 0.08, s * 5.6, floor, rz + dz, M.orange));
      for (double y : {1.4, 2.8}) g->add(boxMesh(0.06, 0.1, 2.5, s * 5.6, floor + y, rz, M.orange));
      for (double y : {1.5, 2.9}) g->add(boxMesh(0.9, 0.9, 2.2, s * 5.6, floor + y, rz, M.GOODS[static_cast<size_t>((k + (s > 0 ? 1 : 0)) % 4)]));
    }
  }
  for (double lx : {-3.5, 3.5})
    for (double lz : {2.5, 6.5}) g->add(boxMesh(1.2, 0.06, 0.3, lx, H - 0.08, wallZ + lz, M.light));
  // a sign over the door
  const Canvas canvas(256, 96);
  const Ctx2D c = canvas.getContext2D();
  c.fillStyle("#1f5fa8");
  c.fillRect(0, 0, 256, 96);
  c.fillStyle("#ffffff");
  c.font("700 60px \"Roboto Condensed\", Arial, sans-serif");
  c.textAlign("center");
  c.textBaseline("middle");
  c.fillText("3", 128, 50);
  TexturePtr tex = canvasTexture(canvas);
  tex->srgb = true;
  auto sign = make<Mesh>(planeGeometry(0.9, 0.34), basic({.map = tex}));
  sign->position.set(0, floor + doorH + 0.75, wallZ - 0.02);
  sign->rotation.y = PI;
  g->add(sign);
  return g;
}

}  // namespace

std::vector<std::array<double, 2>> palletSlots(const SlotInside& inside, int n) {
  std::vector<std::array<double, 2>> out;
  const double rows = std::floor((inside.rear - inside.front - 0.2) / (PALLET.len + 0.05));
  const double half = std::min(0.45, (inside.x1 - inside.x0) / 4);
  for (int r = 0; r < rows && static_cast<int>(out.size()) < n; r++) {
    const double z = inside.front + 0.1 + PALLET.len / 2 + r * (PALLET.len + 0.05);
    for (double x : {-half, half})
      if (static_cast<int>(out.size()) < n) out.push_back({x, z});
  }
  return out;
}

Key& at(const std::vector<Key>& keys, double t, Key& out) {
  if (t <= keys[0].t) return out = keys[0];
  for (size_t i = 1; i < keys.size(); i++) {
    const Key& b = keys[i];
    if (t <= b.t) {
      const Key& a = keys[i - 1];
      const double u = b.t > a.t ? (t - a.t) / (b.t - a.t) : 1;
      const double e = u * u * (3 - 2 * u);
      out.t = t;
      out.x = a.x + (b.x - a.x) * e;
      out.z = a.z + (b.z - a.z) * e;
      out.yaw = a.yaw + (b.yaw - a.yaw) * e;
      out.h = a.h + (b.h - a.h) * e;
      out.carry = a.carry;
      return out;
    }
  }
  return out = keys.back();
}

Script schedule(int n, const std::vector<std::array<double, 2>>& slots, double rear, double floor, bool dock, double wallZ) {
  std::vector<std::array<double, 3>> stack;
  const std::array<double, 3> staging = dock ? std::array<double, 3>{0, floor, wallZ + 1.0} : std::array<double, 3>{0, floor, rear - 0.62};
  for (int i = 0; i < n; i++) {
    if (dock) stack.push_back({-4.2 + (i % 6) * 1.25 + (i >= 6 ? 0.6 : 0), 0, wallZ + 7.2 + (i >= 6 ? 1.4 : 0)});
    else stack.push_back({4.6, 0, rear + 2.2 + i * 1.0});
  }
  // the height the forklift sets down at, over the ground it drives on (the dock's floor, or the ground)
  const double dropH = dock ? 0 : floor;
  const std::array<double, 3> forkHome = dock ? std::array<double, 3>{3.5, wallZ + 4.5, PI} : std::array<double, 3>{3.2, rear + 7, PI};
  // the pallet truck works from behind the door (on the dock: facing into the box) or from inside the box (facing out)
  const double dir = dock ? 0 : PI;
  const double side = dock ? 1 : -1;
  const std::array<double, 2> jackHome = dock ? std::array<double, 2>{1.5, staging[2] + 1.7} : std::array<double, 2>{0, staging[2] - 1.7};
  Way fork(0, forkHome[0], forkHome[1], forkHome[2]);
  Way jack(0, jackHome[0], jackHome[1], dir);
  std::vector<Script::Pallet> pallets;
  double forkFree = 2.2;
  double clear = 0;
  for (int i = 0; i < n; i++) {
    const double px = stack[static_cast<size_t>(i)][0];
    const double pz = stack[static_cast<size_t>(i)][2];
    // (in the depot the stack is approached from the door's side, facing +z; on the ground from the truck's side, facing +x)
    const double yawPick = dock ? PI : -PI / 2;
    const double ax = dock ? px : px - 3.2;
    const double az = dock ? pz - 3.2 : pz;
    const double nx = dock ? px : px - FORK_REACH;
    const double nz = dock ? pz - FORK_REACH : pz;
    fork.wait(forkFree).go(ax, az, yawPick, SPEED.fork).go(nx, nz, yawPick, SPEED.fork * 0.5).lift(0.08);
    const double pick = fork.last().t;
    fork.hold(i).lift(0.3).go(ax, az, yawPick, SPEED.back);
    // to the door, the forks up on the way, then in and down – once the pallet truck is out of the way
    const double sz = staging[2] + FORK_REACH;
    fork.go(staging[0], sz + 2.4, 0, SPEED.fork).lift(dropH + 0.12).wait(clear).go(staging[0], sz, 0, SPEED.fork * 0.45).lift(dropH + 0.01).hold(-1);
    const double drop = fork.last().t;
    fork.go(staging[0], sz + 2.6, 0, SPEED.back).lift(0.08);
    forkFree = fork.last().t;
    // the pallet truck: under the pallet, up, to its place in the box, down, back out of the way
    const double slx = slots[static_cast<size_t>(i)][0];
    const double slz = slots[static_cast<size_t>(i)][1];
    jack.wait(drop + 0.3).go(staging[0], staging[2] + side * 1.0, dir, SPEED.jack).go(staging[0], staging[2], dir, SPEED.jack * 0.6).lift(0.1);
    const double take = jack.last().t;
    jack.hold(i).go(slx, slz, dir, SPEED.jack).lift(0.08).hold(-1);
    const double placed = jack.last().t;
    jack.go(staging[0] * 0.5, staging[2] + side * 1.0, dir, SPEED.back).go(jackHome[0], jackHome[1], dir, SPEED.jack);
    clear = jack.last().t;
    pallets.push_back({pick, drop, take, placed});
  }
  // the forklift parks
  fork.go(forkHome[0], forkHome[1], forkHome[2], SPEED.fork);
  double closing = std::max(jack.last().t, fork.last().t) + 0.6;
  double end = closing + 2.4;
  // 30 to 60 seconds in all
  const double k = end < 30 ? 30 / end : end > 60 ? 60 / end : 1;
  if (k != 1) {
    for (Key& key : fork.keys) key.t *= k;
    for (Key& key : jack.keys) key.t *= k;
    for (Script::Pallet& p : pallets) {
      p.pick *= k;
      p.drop *= k;
      p.take *= k;
      p.placed *= k;
    }
    closing *= k;
    end *= k;
  }
  Script s;
  s.forklift = std::move(fork.keys);
  s.jack = std::move(jack.keys);
  s.pallets = std::move(pallets);
  s.stack = std::move(stack);
  s.staging = staging;
  s.slots = slots;
  s.closing = closing;
  s.end = end;
  return s;
}

std::shared_ptr<Group> palletModel(const PalletLoad& p) {
  const LoadingMaterials& M = MAT();
  auto g = make<Group>();
  // the pallet: three runners and the deck
  for (double x : {-0.34, 0.0, 0.34}) g->add(boxMesh(0.1, 0.1, PALLET.len, x, 0, 0, M.wood));
  g->add(boxMesh(PALLET.wid, 0.044, PALLET.len, 0, 0.1, 0, M.wood));
  const double h = p.height;
  const MaterialPtr& m = M.GOODS[static_cast<size_t>(p.kind) % M.GOODS.size()];
  if (p.kind == 2) {
    // drums
    for (const auto& [x, z] : {std::pair{-0.2, -0.3}, std::pair{0.2, -0.3}, std::pair{-0.2, 0.3}, std::pair{0.2, 0.3}}) {
      auto d = make<Mesh>(cylinderGeometry(0.19, 0.19, h, 12), m);
      d->position.set(x, PALLET.base + h / 2, z);
      d->castShadow = true;
      g->add(d);
    }
  } else
    g->add(boxMesh(PALLET.wid - 0.04, h, PALLET.len - 0.04, 0, PALLET.base, 0, m));
  return g;
}

// ------------------------------------------------------------------ the scene

LoadingScene::LoadingScene(const ScenePose& poseIn, BoxBody& body, std::vector<PalletLoad> loadsIn) : pose(poseIn), loads(std::move(loadsIn)), body_(body) {
  Group& g = *group;
  g.name = "loading the truck";
  g.position.set(pose.x, pose.y, pose.z);
  g.rotation.y = pose.yaw;
  const BoxBody::Inside& inside = body.inside;
  const double rear = inside.rear + 0.08;
  wallZ = rear + DOCK_GAP;
  const auto slots = palletSlots({inside.x0, inside.x1, inside.front, inside.rear}, static_cast<int>(loads.size()));
  script = schedule(static_cast<int>(slots.size()), slots, rear, inside.floor, pose.dock, wallZ);
  base_ = pose.dock ? inside.floor : 0;
  if (pose.dock) g.add(depotModel(wallZ, inside.floor));
  for (size_t i = 0; i < slots.size(); i++) {
    auto p = palletModel(loads[i]);
    pallets_.push_back(p);
    g.add(p);
  }
  auto [fg, fc] = forkliftModel();
  fork_ = {fg, fc};
  auto [jg, jf] = jackModel();
  jack_ = {jg, jf};
  g.add(fork_.group, jack_.group, people_.group);
  // the forklift's driver, the worker with the pallet truck, the one with the papers
  const std::array<Look, 3> looks = {workerLook(9101, "yellow"), workerLook(9102, "orange"), workerLook(9103, "yellow")};
  for (const Look& L : looks) people_.wear(L);
  for (const Look& L : looks) {
    PersonPose p;
    p.look = L.id;
    poses_.push_back(p);
  }
  update(0);
}

LoadingScene::Progress LoadingScene::progress() const {
  int loaded = 0;
  double mass = 0;
  for (size_t i = 0; i < script.pallets.size(); i++) {
    if (t >= script.pallets[i].placed) {
      loaded++;
      mass += loads[i].mass;
    }
  }
  return {loaded, static_cast<int>(script.pallets.size()), mass};
}

void LoadingScene::skip() { t = std::max(t, script.closing); }

void LoadingScene::update(double dt) {
  t += dt;
  const Script& S = script;
  // the doors: open at the start, shut at the end
  const double open = t < S.closing ? std::min(1.0, t / 2) : std::max(0.0, 1 - (t - S.closing) / 2);
  body_.setDoors(open);
  // the forklift
  const Key& f = at(S.forklift, t, k_);
  fork_.group->position.set(f.x, base_, f.z);
  fork_.group->rotation.y = f.yaw;
  fork_.carriage->position.y = f.h;
  // the pallet truck
  const Key& j = at(S.jack, t, k2_);
  const double jackBase = groundUnder(j.z);
  jack_.group->position.set(j.x, jackBase, j.z);
  jack_.group->rotation.y = j.yaw;
  jack_.forks->position.y = j.carry >= 0 || j.h > 0.09 ? 0.1 : 0.03;
  // the pallets: in the stack, on the forks, at the door, on the pallet truck, in their place
  for (size_t i = 0; i < S.pallets.size(); i++) {
    const Script::Pallet& p = S.pallets[i];
    Group& m = *pallets_[i];
    if (t >= p.placed) {
      m.position.set(S.slots[i][0], S.staging[1], S.slots[i][1]);
      m.rotation.y = 0;
    } else if (t >= p.take) {
      onJack(m);
    } else if (t >= p.drop) {
      m.position.set(S.staging[0], S.staging[1], S.staging[2]);
      m.rotation.y = 0;
    } else if (t >= p.pick) {
      onForks(m, f);
    } else {
      const auto& st = S.stack[i];
      m.position.set(st[0], base_ + st[1], st[2]);
      m.rotation.y = pose.dock ? 0 : PI / 2;
    }
    m.visible = true;
  }
  // the people: the driver on the forklift's seat, the worker behind the pallet truck's handle, the one with the papers by the truck
  PersonPose& driver = poses_[0];
  PersonPose& worker = poses_[1];
  PersonPose& clerk = poses_[2];
  // (the seat lies 0.95 m behind the forklift's front axle: along its +z)
  driver.x = f.x + std::sin(f.yaw) * 0.95;
  driver.z = f.z + std::cos(f.yaw) * 0.95;
  driver.y = base_ + 1.12 - SEAT_HEIGHT;
  driver.yaw = f.yaw;
  driver.sit = true;
  driver.walk = -1;
  const double js = std::sin(j.yaw);
  const double jc = std::cos(j.yaw);
  worker.x = j.x + js * 1.45;
  worker.z = j.z + jc * 1.45;
  worker.y = groundUnder(worker.z);
  worker.yaw = j.yaw;
  worker.walk = walkOf(1, worker.x, worker.z);
  clerk.x = pose.dock ? -2.3 : 2.3;
  clerk.z = pose.dock ? wallZ + 1.3 : wallZ - DOCK_GAP + 1.0;
  clerk.y = pose.dock ? S.staging[1] : 0;
  clerk.yaw = headingOf(j.x - clerk.x, j.z - clerk.z);
  clerk.walk = -1;
  people_.update(poses_, t);
}

double LoadingScene::groundUnder(double) const { return script.staging[1]; }

double LoadingScene::walkOf(int i, double x, double z) {
  const auto [px, pz] = lastPos_[static_cast<size_t>(i)];
  const double d = std::hypot(x - px, z - pz);
  lastPos_[static_cast<size_t>(i)] = {x, z};
  if (d < 1e-3) return -1;
  walked_[static_cast<size_t>(i)] += d;
  return walked_[static_cast<size_t>(i)];
}

void LoadingScene::onForks(Group& m, const Key& f) const {
  const double s = std::sin(f.yaw);
  const double c = std::cos(f.yaw);
  m.position.set(f.x - s * FORK_REACH, base_ + f.h, f.z - c * FORK_REACH);
  m.rotation.y = f.yaw;
}

void LoadingScene::onJack(Group& m) const {
  const Key& j = k2_;
  m.position.set(j.x, groundUnder(j.z) + 0.05, j.z);
  m.rotation.y = j.yaw;
}

void LoadingScene::camera(Camera& cam, const std::function<double(double, double)>& ground) const {
  const ScenePose& p = pose;
  const Script& S = script;
  const double drift = std::sin(t * 0.11) * 0.8;
  const double rear = wallZ - DOCK_GAP;
  std::array<double, 3> eye;
  std::array<double, 3> look;
  if (t < 3.5 || t > S.closing) {
    eye = {-8.5, 3.2, rear - 1.5 + drift};
    look = {0, 1.6, rear + 0.3};
  } else if (p.dock) {
    eye = {-4.2 + drift, S.staging[1] + 2.7, wallZ + 5.8};
    look = {0.3, S.staging[1] + 0.8, wallZ - 1.6};
  } else {
    eye = {-4.8 + drift, 3.6, rear + 8.5};
    look = {0.6, 1.5, rear - 0.8};
  }
  const double s = std::sin(p.yaw);
  const double c = std::cos(p.yaw);
  // the truck's frame → the world
  const double wx = p.x + eye[0] * c + eye[2] * s;
  const double wz = p.z - eye[0] * s + eye[2] * c;
  const double g = ground(wx, wz);
  cam.position.set(wx, std::max(std::isfinite(g) ? g + 1.2 : -JS_INF, p.y + eye[1]), wz);
  cam.lookAt(p.x + look[0] * c + look[2] * s, p.y + look[1], p.z - look[0] * s + look[2] * c);
  cam.fov = 58;
  cam.near = 0.15;
}

void LoadingScene::dispose() {
  people_.dispose();
  // (the geometries and the sign's picture go with their meshes)
  group->removeFromParent();
}

}  // namespace fs
