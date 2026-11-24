// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/actors.ts @ v1.7.7
#include "render/actors.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "core/geom.h"
#include "core/js.h"
#include "engine/geometries.h"
#include "render/carBody.h"
#include "sim/hooks.h"
#include "world/types.h"

namespace fs {

namespace {

CarDims dimsWith(double length, double height, double wheelbase, int tail) {
  CarDims d = COMPACT_DIMS;
  d.length = length;
  d.height = height;
  d.wheelbase = wheelbase;
  d.tail = tail;
  return d;
}
const CarDims ESTATE_DIMS = dimsWith(4.6, 1.5, 2.7, 1);
const CarDims SALOON_DIMS = dimsWith(4.45, 1.42, 2.68, 2);

const CarDims* const STYLES[] = {
    &COMPACT_DIMS,
    &ESTATE_DIMS,
    &SALOON_DIMS,
};

/** `c.drawn ?? c`: where a car is drawn – eased through the corners of its way (sim/smoothpose.ts), or where its way puts it */
struct DrawnAt {
  double x;
  double z;
  double yaw;
};
DrawnAt drawnOf(const TrafficCar& c) {
  if (c.drawn) return {c.drawn->x, c.drawn->z, c.drawn->yaw};
  return {c.x, c.z, c.yaw};
}

/** `c.drawn?.steer ?? 0` */
double steerOf(const TrafficCar& c) { return c.drawn && c.drawn->steer ? *c.drawn->steer : 0; }

CarVisualState busStateAtRest() {
  CarVisualState s;
  s.speedKmh = 0;
  s.rpm = 800;
  s.gear = 1;
  s.indicator = 0;
  s.hazard = false;
  s.blinkOn = false;
  s.lowBeam = true;
  s.highBeam = false;
  s.handbrake = false;
  s.ignition = true;
  s.engineRunning = true;
  s.abs = false;
  s.seatbelt = true;
  s.fuel = 0.6;
  s.automatic = true;
  s.showGear = false;
  s.odometerKm = 0;
  s.steerWheel = 0;
  s.wheelSteer = {0, 0, 0, 0};
  s.wheelSpin = {0, 0, 0, 0};
  s.wheelY = {0.48, 0.48, 0.48, 0.48};
  s.clutch = 0;
  s.brake = 0;
  s.throttle = 0;
  s.handbrakeLever = 0;
  s.braking = false;
  s.reversing = false;
  return s;
}

}  // namespace

TrafficRenderer::TrafficRenderer()
    : people(PeopleRenderer::create ? PeopleRenderer::create(CROWD_MAX) : nullptr),
      emergency_(EmergencyFleet::create ? EmergencyFleet::create() : nullptr),
      busState_(busStateAtRest()) {
  group->name = "traffic";
  if (emergency_) group->add(emergency_->group);
  if (people) group->add(people->group);
}

void TrafficRenderer::update(const std::vector<TrafficCar*>& cars, const std::vector<Pedestrian*>& peds, double time, const std::vector<Cyclist*>& cyclists) {
  std::unordered_set<int64_t> seen;
  const bool blinkOn = std::fmod(std::floor(time * 3), 2) == 0;
  std::unordered_set<int64_t> seenB;
  // (the emergency vehicles have their own models: render/emergency.ts)
  if (emergency_) emergency_->update(cars, time);
  for (const TrafficCar* c : cars) {
    if (emKind(c->style) >= 0) continue;
    if (c->style == BUS_STYLE) {
      // a city bus: the lamps say where it is going (to the stop, out of it)
      seenB.insert(c->id);
      PlayerBus* b = buses_.get(c->id);
      if (!b) {
        std::unique_ptr<PlayerBus> made;
        if (!busPool_.empty()) {
          made = std::move(busPool_.back());
          busPool_.pop_back();
        } else made = std::make_unique<PlayerBus>();
        made->root->traverse([](Object3D& o) {
          if (o.type == ObjectType::Mesh || o.type == ObjectType::InstancedMesh) o.castShadow = true;
        });
        b = buses_.set(c->id, std::move(made));
        group->add(b->root);
      }
      // (where it is drawn: eased through the corners of its way – sim/smoothpose.ts)
      const DrawnAt d = drawnOf(*c);
      b->root->position.set(d.x, c->y, d.z);
      b->root->rotation.set(c->pitch, d.yaw, 0, Euler::YXZ);
      // its line and where it goes, on the displays (drawn anew only when they change)
      const BusOnLine* line = c->bus && c->bus->line ? &*c->bus->line : nullptr;
      b->setLine(line ? line->ref : std::string(), line ? line->to : std::string());
      CarVisualState& st = busState_;
      std::fill(st.wheelSpin.begin(), st.wheelSpin.end(), c->wheelSpin);
      // (its front wheels steered as it turns: sim/smoothpose.ts)
      st.wheelSteer[0] = st.wheelSteer[1] = steerOf(*c);
      st.indicator = c->blinker;
      st.blinkOn = blinkOn;
      st.braking = c->braking;
      st.speedKmh = c->v * 3.6;
      b->update(st, 0, time);
      continue;
    }
    seen.insert(c->id);
    CarExterior* m = cars_.get(c->id);
    if (!m) {
      std::vector<std::unique_ptr<CarExterior>>& pool = pool_[c->style];
      std::unique_ptr<CarExterior> made;
      if (!pool.empty()) {
        made = std::move(pool.back());
        pool.pop_back();
      } else made = std::make_unique<CarExterior>(c->color, CarExteriorOptions{.dims = STYLES[c->style]});
      made->paint->color.set(c->color);
      m = cars_.set(c->id, std::move(made));
      group->add(m->group);
    }
    const DrawnAt d = drawnOf(*c);
    m->group->position.set(d.x, c->y, d.z);
    m->group->rotation.set(c->pitch, d.yaw, 0, Euler::YXZ);
    const double r = m->dims.wheelRadius;
    const double steer = steerOf(*c);
    m->setWheels({steer, steer, 0, 0}, {c->wheelSpin, c->wheelSpin, c->wheelSpin, c->wheelSpin}, {r, r, r, r});
    m->setLamps({.low = true, .high = highOf ? highOf(c->id) : false, .brake = c->braking, .reverse = false, .left = blinkOn && c->blinker < 0, .right = blinkOn && c->blinker > 0});
  }
  for (auto it = cars_.list.begin(); it != cars_.list.end();) {
    if (seen.count(it->first)) {
      ++it;
      continue;
    }
    CarExterior& m = *it->second;
    group->remove(m.group);
    const auto found = std::find(std::begin(STYLES), std::end(STYLES), &m.dims);
    const size_t style = found == std::end(STYLES) ? 0 : static_cast<size_t>(found - std::begin(STYLES));
    pool_[style].push_back(std::move(it->second));
    cars_.at.erase(it->first);
    it = cars_.list.erase(it);
  }
  for (auto it = buses_.list.begin(); it != buses_.list.end();) {
    if (seenB.count(it->first)) {
      ++it;
      continue;
    }
    group->remove(it->second->root);
    busPool_.push_back(std::move(it->second));
    buses_.at.erase(it->first);
    it = buses_.list.erase(it);
  }
  drawPeople(peds, time);
  std::unordered_set<int64_t> seenC;
  for (const Cyclist* c : cyclists) {
    seenC.insert(c->id);
    CyclistModel* m = cyclists_.get(c->id);
    if (!m) {
      std::unique_ptr<CyclistModel> made;
      if (!cyclistPool_.empty()) {
        made = std::move(cyclistPool_.back());
        cyclistPool_.pop_back();
      } else if (CyclistModel::create) made = CyclistModel::create(c->colour);
      // (render/cyclist is not translated yet: no cyclist is drawn)
      if (!made) continue;
      made->setColour(c->colour);
      m = cyclists_.set(c->id, std::move(made));
      group->add(m->group);
    }
    m->group->position.set(c->x, c->y, c->z);
    m->group->rotation.y = c->yaw;
    m->pose(c->phase, c->roll, c->down > 0);
  }
  for (auto it = cyclists_.list.begin(); it != cyclists_.list.end();) {
    if (seenC.count(it->first)) {
      ++it;
      continue;
    }
    group->remove(it->second->group);
    cyclistPool_.push_back(std::move(it->second));
    cyclists_.at.erase(it->first);
    it = cyclists_.list.erase(it);
  }
}

void TrafficRenderer::drawPeople(const std::vector<Pedestrian*>& peds, double time) {
  Camera* cam = view;
  double cx = 0;
  double cz = 0;
  double fx = 0;
  double fz = 0;
  if (cam) {
    cx = cam->position.x;
    cz = cam->position.z;
    cam->getWorldDirection(look_);
    const double h = std::hypot(look_.x, look_.z);
    const double l = h != 0 && h == h ? h : 1;
    fx = look_.x / l;
    fz = look_.z / l;
  }
  std::vector<PersonPose>& shown = shown_;
  shown.clear();
  for (const Pedestrian* p : peds) {
    if (cam) {
      const double dx = p->x - cx;
      const double dz = p->z - cz;
      const double d2 = dx * dx + dz * dz;
      if (d2 > 220 * 220 || (d2 > 45 * 45 && dx * fx + dz * fz < -0.2 * std::sqrt(d2))) continue;
    }
    PersonPose q;
    q.x = p->x;
    q.y = p->y;
    q.z = p->z;
    q.yaw = p->yaw;
    q.walk = p->phase >= 0 ? p->phase : -1;
    q.look = p->colour;
    q.head = p->head;
    q.startle = p->startle;
    q.lean = p->lean;
    q.umbrella = p->umbrella;
    shown.push_back(q);
  }
  if (people) people->update(shown, time);
}

namespace {

/** An arrow pointing along +X, 17 cm long (a lamp of an arrow light). */
GeometryPtr arrowShape() {
  Shape s;
  s.moveTo(-0.085, -0.022);
  s.lineTo(0.01, -0.022);
  s.lineTo(0.01, -0.062);
  s.lineTo(0.085, 0);
  s.lineTo(0.01, 0.062);
  s.lineTo(0.01, 0.022);
  s.lineTo(-0.085, 0.022);
  s.closePath();
  return shapeGeometry(s);
}

}  // namespace

SignalRenderer::SignalRenderer(const World& world, const SignalSystem* signals) {
  group->name = "signals";
  rebuild(world, signals);
}

void SignalRenderer::rebuild(const World& world, const SignalSystem* signals) {
  for (const std::shared_ptr<InstancedMesh>& m : {housing_, housing_ ? lamps_ : nullptr, arrowHousing_, arrowLamps_}) {
    if (!m) continue;
    group->remove(m);
    m->geometry->dispose();
    m->material->dispose();
    m->dispose();
  }
  arrowHousing_ = nullptr;
  arrowLamps_ = nullptr;
  heads_.clear();
  arrowHeads_.clear();
  poles.clear();
  PolySample tmp = newPolySample();
  std::vector<HeadSpot> placements;
  const auto place = [&](const Edge& edge, double s, double dir, int controller, int grp, double side, const Arm* arrow = nullptr) {
    samplePolyline(edge.pl, jsMax(0.2, jsMin(edge.len - 0.2, s)), tmp);
    const double lat = -dir * side;   // driver's right when side = 1
    const double sw = lat > 0 ? edge.swL : edge.swR;
    const double out = edge.width / 2 + (sw > 0 ? 0.55 : 0.9);
    const double x = tmp.x + tmp.tz * out * lat;
    const double z = tmp.z - tmp.tx * out * lat;
    const double y = world.edgeY(edge, s, x, z) + (sw > 0 ? KERB_H : -0.1);
    const double fx = -tmp.tx * dir;
    const double fz = -tmp.tz * dir;
    placements.push_back({x, y, z, std::atan2(-fx, -fz), controller, grp, arrow});
  };
  for (const Node* node : world.allNodes) {
    // (lights that are out of order stand there all the same – and flash)
    if (node->stub || (node->ctrl != CTRL_SIGNALS && !node->dark) || node->sc < 0) continue;
    for (const Arm* arm : node->arms) {
      if (arm->lamp < 0 || !arm->isApproach) continue;
      const Edge& e = *arm->edge;
      const double dir = arm->out ? -1 : 1;
      // the pole stands a little beyond the stop line so the first car can still see it
      const double d = jsMax(arm->cut + 0.3, arm->stopS - 2.6);
      const double s = arm->out ? d : e.len - d;
      // (the arrow for turning right hangs beside the head at the right-hand kerb)
      place(e, s, dir, node->sc, arm->lamp, 1, signals && signals->arrows.has(arm) ? arm : nullptr);
      const int lanesIn = arm->out ? e.lanesB : e.lanesF;
      if (lanesIn >= 2 || e.lanesF == 0 || e.lanesB == 0) place(e, s, dir, node->sc, arm->lamp, -1);
    }
  }
  for (const CrossingJson* c : world.crossings) {
    if (c->k != XING_SIGNAL || !c->sc) continue;
    const Edge& e = *world.edges[c->e];
    // crossings that belong to a junction are served by its heads
    const bool nearA = (e.a->ctrl == CTRL_SIGNALS || e.a->dark) && c->s < 40;
    const bool nearB = (e.b->ctrl == CTRL_SIGNALS || e.b->dark) && e.len - c->s < 40;
    if (nearA || nearB) continue;
    if (e.lanesF > 0) place(e, c->s + c->w / 2 + 0.8, 1, *c->sc, c->g.value_or(0), 1);
    if (e.lanesB > 0) place(e, c->s - c->w / 2 - 0.8, -1, *c->sc, c->g.value_or(0), 1);
  }

  spots = placements;
  const int n = static_cast<int>(placements.size());
  const GeometryPtr pole = cylinderGeometry(0.055, 0.065, 3.5, 8, 1, true);
  pole->translate(0, 1.75, 0);
  const GeometryPtr box = boxGeometry(0.3, 0.92, 0.22);
  box->translate(0, 3.0, 0.02);
  const GeometryPtr visor = boxGeometry(0.34, 0.03, 0.2);
  visor->translate(0, 3.47, -0.16);
  std::vector<GeometryPtr> parts;
  for (const GeometryPtr& indexed : {pole, box, visor}) {
    GeometryPtr g = indexed->toNonIndexed();
    g->deleteAttribute("uv");
    parts.push_back(g);
  }
  const GeometryPtr merged = mergeGeometries(parts, false);
  merged->computeVertexNormals();
  const std::shared_ptr<InstancedMesh> housing = make<InstancedMesh>(merged, lambert({.color = Color(0x1d2022)}), std::max(1, n));
  const GeometryPtr lampGeo = circleGeometry(0.095, 14);
  lamps_ = make<InstancedMesh>(lampGeo, basic({.color = Color(0xffffff), .fog = true}), std::max(1, n * 3));
  Matrix4 m4;
  Quaternion q;
  const Vector3 up(0, 1, 0);
  Vector3 off;
  const Color dark(0x111111);
  for (int i = 0; i < n; i++) {
    const HeadSpot& p = placements[static_cast<size_t>(i)];
    // the head faces along its local -Z (same convention as sign plates)
    q.setFromAxisAngle(up, p.yaw);
    m4.compose(Vector3(p.x, p.y - 0.25, p.z), q, Vector3(1, 1, 1));
    housing->setMatrixAt(i, m4);
    for (int k = 0; k < 3; k++) {
      off.set(0, 3.0 + 0.29 - k * 0.29 - 0.25, -0.095).applyQuaternion(q);
      // CircleGeometry faces +Z: turn it round to face -Z
      const Quaternion ql = q.clone().multiply(Quaternion().setFromAxisAngle(up, PI));
      m4.compose(Vector3(p.x + off.x, p.y + off.y, p.z + off.z), ql, Vector3(1, 1, 1));
      lamps_->setMatrixAt(i * 3 + k, m4);
      lamps_->setColorAt(i * 3 + k, dark);
    }
    heads_.push_back({p.controller, p.group, i * 3, -1});
    poles.insert(poles.end(), {p.x, p.y, p.z, 0.08});
  }
  housing->count = n;
  lamps_->count = n * 3;
  housing->instanceMatrix->needsUpdate();
  lamps_->instanceMatrix->needsUpdate();
  housing->frustumCulled = false;
  lamps_->frustumCulled = false;
  housing_ = housing;
  group->add(housing, lamps_);

  // ---- the green arrows: a smaller head with two arrow lamps (amber, green) on the kerb side of the main one ----
  std::vector<HeadSpot> withArrow;
  for (const HeadSpot& p : placements)
    if (p.arrow) withArrow.push_back(p);
  if (withArrow.empty()) return;
  // (seen from the approaching car, to the right of the head is its local -X)
  const double SIDE = -0.34;
  const GeometryPtr smallBox = boxGeometry(0.3, 0.63, 0.22);
  smallBox->translate(SIDE, 2.855, 0.02);
  const GeometryPtr small = smallBox->toNonIndexed();
  small->deleteAttribute("uv");
  small->computeVertexNormals();
  const int arrows = static_cast<int>(withArrow.size());
  const std::shared_ptr<InstancedMesh> arrowHousing = make<InstancedMesh>(small, lambert({.color = Color(0x1d2022)}), arrows);
  const std::shared_ptr<InstancedMesh> arrowLamps = make<InstancedMesh>(arrowShape(), basic({.color = Color(0xffffff), .fog = true}), arrows * 2);
  const Quaternion turned = Quaternion().setFromAxisAngle(up, PI);
  for (int i = 0; i < arrows; i++) {
    const HeadSpot& p = withArrow[static_cast<size_t>(i)];
    q.setFromAxisAngle(up, p.yaw);
    m4.compose(Vector3(p.x, p.y - 0.25, p.z), q, Vector3(1, 1, 1));
    arrowHousing->setMatrixAt(i, m4);
    // (the shape faces +Z and points along +X: turned round it faces the car and points to its right)
    const Quaternion ql = q.clone().multiply(turned);
    for (int k = 0; k < 2; k++) {
      off.set(SIDE, 3.0 - k * 0.29 - 0.25, -0.095).applyQuaternion(q);
      m4.compose(Vector3(p.x + off.x, p.y + off.y, p.z + off.z), ql, Vector3(1, 1, 1));
      arrowLamps->setMatrixAt(i * 2 + k, m4);
      arrowLamps->setColorAt(i * 2 + k, dark);
    }
    arrowHeads_.push_back({p.arrow, i * 2, -9});
  }
  arrowHousing->instanceMatrix->needsUpdate();
  arrowLamps->instanceMatrix->needsUpdate();
  arrowHousing->frustumCulled = false;
  arrowLamps->frustumCulled = false;
  arrowHousing_ = arrowHousing;
  arrowLamps_ = arrowLamps;
  group->add(arrowHousing, arrowLamps);
}

void SignalRenderer::update(const SignalSystem& signals) {
  const Color red(0xff2a1a);
  const Color amber(0xffb000);
  const Color green(0x1aff6a);
  const Color dark(0x141414);
  bool changed = false;
  // (out of order: the amber lamp flashes, once a second)
  const bool flash = std::fmod(signals.time, 1) < 0.5;
  for (Head& h : heads_) {
    int st = signals.get(h.controller, h.group);
    if (st == SIG_FLASH && !flash) st = -1;
    if (st == h.state) continue;
    h.state = st;
    changed = true;
    lamps_->setColorAt(h.lamp, st == SIG_RED || st == SIG_RED_AMBER ? red : dark);
    lamps_->setColorAt(h.lamp + 1, st == SIG_AMBER || st == SIG_RED_AMBER || st == SIG_FLASH ? amber : dark);
    lamps_->setColorAt(h.lamp + 2, st == SIG_GREEN ? green : dark);
  }
  if (changed && lamps_->instanceColor) lamps_->instanceColor->needsUpdate();
  if (!arrowLamps_) return;
  changed = false;
  for (ArrowHead& h : arrowHeads_) {
    const int st = signals.arrow(h.arm);
    if (st == h.state) continue;
    h.state = st;
    changed = true;
    arrowLamps_->setColorAt(h.lamp, st == SIG_AMBER ? amber : dark);
    arrowLamps_->setColorAt(h.lamp + 1, st == SIG_GREEN ? green : dark);
  }
  if (changed && arrowLamps_->instanceColor) arrowLamps_->instanceColor->needsUpdate();
}

}  // namespace fs
