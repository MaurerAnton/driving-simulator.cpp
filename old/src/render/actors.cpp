// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Moving things in the scene: traffic cars, pedestrians and traffic-light
 * heads.
 *
 * Origin: src/render/actors.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  Cars are pooled per body style, pedestrians are simple figures,
 * and the signal heads are two instanced meshes (housing and lamps).
 */
#include "render/actors.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace fs {

namespace {
/** Vertex colours on a geometry (the pedestrians' shirts). */
void tint(GeometryPtr g, uint32_t color) {
  if (!g || !g->position) return;
  Color c;
  c.setHex(color);
  std::vector<double> cols(g->position->count() * 3);
  for (size_t i = 0; i < g->position->count(); i++) {
    cols[i * 3] = c.r;
    cols[i * 3 + 1] = c.g;
    cols[i * 3 + 2] = c.b;
  }
  g->color = std::make_shared<BufferAttribute>(cols, 3);
}

/** The three body styles the traffic draws (compact, hatchback, estate). */
const CarDims STYLES[3] = {
    COMPACT_DIMS,
    {4.6, 1.79, 1.5, 2.7, 1.54, 0.316, 0.205, 0.88, 1},
    {4.45, 1.79, 1.42, 2.68, 1.54, 0.316, 0.205, 0.88, 2},
};
}  // namespace

// ---------------------------------------------------------------------------
// TrafficRenderer
// ---------------------------------------------------------------------------

TrafficRenderer::TrafficRenderer() {
  group.name = "traffic";
  pool_.resize(3);
  for (auto& p : pool_) p.reserve(8);
  pedBody_ = makeBoxGeometry(0.4, 0.62, 0.24);
  pedBody_->translate(0, 1.13, 0);
  pedLeg_ = makeBoxGeometry(0.14, 0.82, 0.16);
  pedLeg_->translate(0, -0.41, 0);
  pedHead_ = makeSphereGeometry(0.115, 10, 8);
  pedHead_->translate(0, 1.58, 0);
}

void TrafficRenderer::update(const std::vector<TrafficCar>& cars,
                             const std::vector<Pedestrian>& peds, double time) {
  std::set<int> seen;
  bool blinkOn = static_cast<int>(std::floor(time * 3)) % 2 == 0;
  for (const auto& c : cars) {
    seen.insert(c.id);
    auto it = cars_.find(c.id);
    Entry* e = nullptr;
    if (it == cars_.end()) {
      Entry ne;
      int style = c.style;
      if (style < 0 || style > 2) style = 0;
      if (static_cast<size_t>(style) >= pool_.size()) style = 0;
      if (!pool_[static_cast<size_t>(style)].empty()) {
        ne.car = std::move(pool_[static_cast<size_t>(style)].back());
        pool_[static_cast<size_t>(style)].pop_back();
      } else {
        ne.car = std::make_unique<CarExterior>(c.color, STYLES[style]);
      }
      ne.style = style;
      ne.car->group.name = "car " + std::to_string(c.id);
      cars_[c.id] = std::move(ne);
      it = cars_.find(c.id);
      group.addRef(&it->second.car->group);
    }
    e = &it->second;
    CarExterior& m = *e->car;
    m.group.position.set(c.x, c.y, c.z);
    m.group.quaternion.setFromEuler(Euler(c.pitch, c.yaw, 0, Euler::YXZ));
    m.group.matrixAutoUpdate = true;
    double r = m.dims.wheelRadius;
    double steer[4] = {0, 0, 0, 0};
    double spin[4] = {c.wheelSpin, c.wheelSpin, c.wheelSpin, c.wheelSpin};
    double wy[4] = {r, r, r, r};
    m.setWheels(steer, spin, wy);
    m.setLamps(true, false, c.braking, false, blinkOn && c.blinker < 0,
               blinkOn && c.blinker > 0);
  }
  for (auto it = cars_.begin(); it != cars_.end();) {
    if (seen.count(it->first)) {
      ++it;
      continue;
    }
    group.remove(&it->second.car->group);
    int style = it->second.style;
    if (style < 0 || style > 2) style = 0;
    pool_[static_cast<size_t>(style)].push_back(std::move(it->second.car));
    it = cars_.erase(it);
  }

  std::set<int> seenP;
  for (const auto& p : peds) {
    seenP.insert(p.id);
    auto it = peds_.find(p.id);
    if (it == peds_.end()) {
      auto g = std::make_unique<Group>();
      // the body wears the pedestrian's shirt colour
      auto body = makeBoxGeometry(0.4, 0.62, 0.24);
      body->translate(0, 1.13, 0);
      tint(body, p.colour);
      g->add(std::unique_ptr<Object3D>(new Mesh(body, MaterialKind::Struct)));
      g->add(std::unique_ptr<Object3D>(new Mesh(pedHead_, MaterialKind::Struct)));
      for (int side : {-1, 1}) {
        auto leg = std::make_unique<Mesh>(pedLeg_, MaterialKind::Struct);
        leg->position.set(side * 0.1, 0.83, 0);
        leg->updateMatrix();
        g->add(std::unique_ptr<Object3D>(leg.release()));
      }
      // the figure carries the pedestrian's colour in its name so the scene
      // walk can tell cars (named "car N") from pedestrians
      g->name = "ped";
      Group* raw = g.get();
      peds_[p.id] = std::move(g);
      group.addRef(raw);
      it = peds_.find(p.id);
    }
    Group* g = it->second.get();
    g->position.set(p.x, p.y, p.z);
    g->quaternion.setFromAxisAngle(Vector3(0, 1, 0), p.yaw);
    g->matrixAutoUpdate = true;
    double swing = p.state == "cross" ? std::sin(p.phase) * 0.55 : 0;
    if (g->children.size() >= 4) {
      // the two legs swing in opposite directions
      static_cast<Object3D*>(g->children[2].get())->quaternion.setFromAxisAngle(
          Vector3(1, 0, 0), swing);
      static_cast<Object3D*>(g->children[3].get())->quaternion.setFromAxisAngle(
          Vector3(1, 0, 0), -swing);
      g->children[2]->matrixAutoUpdate = true;
      g->children[3]->matrixAutoUpdate = true;
    }
  }
  for (auto it = peds_.begin(); it != peds_.end();) {
    if (seenP.count(it->first)) {
      ++it;
      continue;
    }
    group.remove(it->second.get());
    it = peds_.erase(it);
  }
}

// ---------------------------------------------------------------------------
// SignalRenderer
// ---------------------------------------------------------------------------

SignalRenderer::SignalRenderer(World& world) {
  group.name = "signals";
  PolySample tmp;
  struct Placement {
    double x, y, z, yaw;
    int controller, group;
  };
  std::vector<Placement> placements;

  auto place = [&](const Edge& edge, double s, double dir, int controller, int grp, int side) {
    samplePolyline(edge.pl, std::max(0.2, std::min(edge.len - 0.2, s)), tmp);
    double lat = -dir * side;  // driver's right when side = 1
    double sw = lat > 0 ? edge.swL : edge.swR;
    double out = edge.width / 2 + (sw > 0 ? 0.55 : 0.9);
    double x = tmp.x + tmp.tz * out * lat;
    double z = tmp.z - tmp.tx * out * lat;
    double y = world.edgeY(edge, s, x, z) + (sw > 0 ? KERB_H : -0.1);
    double fx = -tmp.tx * dir;
    double fz = -tmp.tz * dir;
    placements.push_back({x, y, z, std::atan2(-fx, -fz), controller, grp});
  };

  for (const auto& node : world.nodes()) {
    if (node.ctrl != CTRL_SIGNALS || node.sc < 0) continue;
    for (const auto& arm : node.arms) {
      if (arm.sg < 0 || !arm.isApproach) continue;
      const Edge& e = *arm.edge;
      double dir = arm.out ? -1 : 1;
      // the pole stands a little beyond the stop line so the first car can see it
      double d = std::max(arm.cut + 0.3, arm.stopS - 2.6);
      double s = arm.out ? d : e.len - d;
      place(e, s, dir, node.sc, arm.sg, 1);
      int lanesIn = arm.out ? e.lanesB : e.lanesF;
      if (lanesIn >= 2 || e.lanesF == 0 || e.lanesB == 0) place(e, s, dir, node.sc, arm.sg, -1);
    }
  }
  for (const auto& c : world.crossings()) {
    if (c.k != XING_SIGNAL || c.sc < 0) continue;
    const Edge& e = world.edges()[static_cast<size_t>(c.e)];
    // crossings that belong to a junction are served by its heads
    bool nearA = e.a->ctrl == CTRL_SIGNALS && c.s < 40;
    bool nearB = e.b->ctrl == CTRL_SIGNALS && e.len - c.s < 40;
    if (nearA || nearB) continue;
    if (e.lanesF > 0) place(e, c.s + c.w / 2 + 0.8, 1, c.sc, c.g >= 0 ? c.g : 0, 1);
    if (e.lanesB > 0) place(e, c.s - c.w / 2 - 0.8, -1, c.sc, c.g >= 0 ? c.g : 0, 1);
  }

  size_t n = placements.size();
  // the housing: pole + head + visor, merged into one instanced mesh
  GeometryPtr pole = makeCylinderGeometry(0.055, 0.065, 3.5, 8, 1, true);
  pole->translate(0, 1.75, 0);
  GeometryPtr box = makeBoxGeometry(0.3, 0.92, 0.22);
  box->translate(0, 3.0, 0.02);
  GeometryPtr visor = makeBoxGeometry(0.34, 0.03, 0.2);
  visor->translate(0, 3.47, -0.16);
  GeometryPtr housing = mergeGeometries({pole, box, visor}, false);
  housing->computeVertexNormals();
  Color housingCol;
  housingCol.setHex(0x1d2022);
  std::vector<double> hcols(housing->position->count() * 3);
  for (size_t i = 0; i < housing->position->count(); i++) {
    hcols[i * 3] = housingCol.r;
    hcols[i * 3 + 1] = housingCol.g;
    hcols[i * 3 + 2] = housingCol.b;
  }
  housing->color = std::make_shared<BufferAttribute>(hcols, 3);
  auto housingMesh = std::make_unique<Mesh>(housing, MaterialKind::Struct);
  housingMesh->frustumCulled = false;

  GeometryPtr lampGeo = makeCircleGeometry(0.095, 14);
  lamps_ = std::make_unique<Mesh>(lampGeo, MaterialKind::Struct);
  lamps_->frustumCulled = false;

  Matrix4 m4;
  Quaternion q, ql, up;
  up.setFromAxisAngle(Vector3(0, 1, 0), 0);
  Vector3 off;
  Color dark;
  dark.setHex(0x111111);
  for (size_t i = 0; i < n; i++) {
    const Placement& p = placements[i];
    // the head faces along its local -Z (same convention as sign plates)
    q.setFromAxisAngle(Vector3(0, 1, 0), p.yaw);
    m4.compose(Vector3(p.x, p.y - 0.25, p.z), q, Vector3(1, 1, 1));
    housingMesh->instances.push_back(m4);
    for (int k = 0; k < 3; k++) {
      off.set(0, 3.0 + 0.29 - k * 0.29 - 0.25, -0.095);
      off.applyQuaternion(q);
      // CircleGeometry faces +Z: turn it round to face -Z
      ql = q;
      ql.multiply(Quaternion().setFromAxisAngle(Vector3(0, 1, 0), PI));
      m4.compose(Vector3(p.x + off.x, p.y + off.y, p.z + off.z), ql, Vector3(1, 1, 1));
      lamps_->instances.push_back(m4);
      // every lamp starts dark; update() lights the ones the signal shows
      lamps_->instanceColors.push_back(dark);
    }
    heads_.push_back({p.controller, p.group, static_cast<int>(i) * 3, -1});
    poles.push_back(p.x);
    poles.push_back(p.y);
    poles.push_back(p.z);
    poles.push_back(0.08);
  }
  group.add(std::unique_ptr<Object3D>(housingMesh.release()));
  group.add(std::unique_ptr<Object3D>(lamps_.release()));
}

void SignalRenderer::update(const SignalSystem& signals) {
  Color red, amber, green, dark;
  red.setHex(0xff2a1a);
  amber.setHex(0xffb000);
  green.setHex(0x1aff6a);
  dark.setHex(0x141414);
  bool changed = false;
  for (auto& h : heads_) {
    int st = signals.get(h.controller, h.group);
    if (st == h.state) continue;
    h.state = st;
    changed = true;
    if (!lamps_) continue;
    size_t i = static_cast<size_t>(h.lamp);
    if (i + 2 >= lamps_->instanceColors.size()) continue;
    lamps_->instanceColors[i] = (st == SIG_RED || st == SIG_RED_AMBER) ? red : dark;
    lamps_->instanceColors[i + 1] = (st == SIG_AMBER || st == SIG_RED_AMBER) ? amber : dark;
    lamps_->instanceColors[i + 2] = st == SIG_GREEN ? green : dark;
  }
  (void)changed;
}

}  // namespace fs
