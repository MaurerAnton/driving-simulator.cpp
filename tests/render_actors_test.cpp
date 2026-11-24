// LINK: render engine sim world game core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What is drawn of the traffic (src/render/actors): its cars and the traffic lights' heads.
 *
 *  - the models follow the traffic: one for each car, kept for it while it is there, handed
 *    on to another car of the same body when it left; lamps, wheels and place as the car has them;
 *  - the people on foot, the cyclists, the city buses and the emergency vehicles are hooks:
 *    left out while those parts are not there, drawn through them once they are;
 *  - the heads: one housing and three lamps for each place a head stands at, an arrow beside
 *    those that have one; the lamps' colours follow the lights;
 *  - everything, number by number, against the original: tools/diff/render_cars.ts --part actors
 *    prints what src/render/actors.ts @ v1.7.7 makes (tests/fixtures/render-actors.txt), this
 *    prints the same.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "render/actors.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "tests/render_dump.h"
#include "tests/town.h"

using namespace fs;
using fs::dump::out;
using fs::rdump::P;
using fs::rdump::word;

namespace {

/** cars of the traffic, as far as the picture reads them (they keep their addresses) */
struct Cars {
  std::deque<TrafficCar> all;
  TrafficCar* add(int id, int style, int color, double x) {
    TrafficCar c;
    c.id = id;
    c.style = style;
    c.color = color;
    c.x = x;
    c.y = 100 + id * 0.01;
    c.z = -3 * id;
    c.yaw = 0.1 * id;
    c.pitch = 0.01 * id;
    c.wheelSpin = id * 1.5;
    c.blinker = 0;
    c.braking = false;
    c.v = 5;
    all.push_back(c);
    return &all.back();
  }
};

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// ---- the parts that are not translated, standing in ----

struct TestPeople : PeopleRenderer {
  int capacity = 0;
  std::vector<PersonPose> last;
  double at = -1;
  static inline TestPeople* made = nullptr;
  explicit TestPeople(int n) : capacity(n) { made = this; }
  void update(const std::vector<PersonPose>& people, double time) override {
    last = people;
    at = time;
  }
};

struct TestEmergency : EmergencyFleet {
  size_t cars = 0;
  static inline TestEmergency* made = nullptr;
  TestEmergency() { made = this; }
  void update(const std::vector<TrafficCar*>& list, double) override { cars = list.size(); }
};

struct TestBus : PlayerBus {
  std::string ref, to;
  CarVisualState last;
  double at = -1;
  static inline int made = 0;
  static inline TestBus* newest = nullptr;
  TestBus() {
    made++;
    newest = this;
    root->add(make<Mesh>(make<BufferGeometry>(), basic()));
  }
  void setLine(const std::string& r, const std::string& t) override {
    ref = r;
    to = t;
  }
  void update(const CarVisualState& s, double, double now) override {
    last = s;
    at = now;
  }
};

struct TestCyclist : CyclistModel {
  int colour = 0;
  double phase = 0, roll = 0;
  bool down = false;
  static inline int made = 0;
  static inline TestCyclist* newest = nullptr;
  explicit TestCyclist(int c) : colour(c) {
    made++;
    newest = this;
  }
  void setColour(int c) override { colour = c; }
  void pose(double p, double r, bool d) override {
    phase = p;
    roll = r;
    down = d;
  }
};

// ------------------------------------------------------------------------------------------

void theCarsFollowTheTraffic() {
  TrafficRenderer traffic;
  assert(traffic.group->name == "traffic" && traffic.group->children.empty() && !traffic.people);
  Cars cars;
  TrafficCar* a = cars.add(1, 0, 0xd9dadc, 10);
  TrafficCar* b = cars.add(2, 1, 0x1b3fa0, 20);
  TrafficCar* c = cars.add(3, 2, 0x8a1c1c, 30);
  traffic.update({a, b, c}, {}, 0);
  // a model for each car, where the car is
  assert(traffic.group->children.size() == 3);
  Object3D* first = traffic.group->children[0].get();
  assert(near(first->position.x, 10) && near(first->position.y, 100.01) && near(first->position.z, -3));
  Vector3 ahead(0, 0, -1);
  ahead.applyQuaternion(traffic.group->children[1]->quaternion);
  // (yaw 0.2, pitch 0.02: heading a little to the left, nose up)
  assert(ahead.x < -0.19 && ahead.x > -0.2 && ahead.y > 0.019 && ahead.y < 0.021);
  // the same cars again: the same models
  a->x = 11;
  traffic.update({a, b, c}, {}, 0.1);
  assert(traffic.group->children.size() == 3 && traffic.group->children[0].get() == first && near(first->position.x, 11));
  // drawn where its eased pose says, its front wheels steered
  c->drawn = EasedPose{30.5, -9.5, 0.35, std::nullopt, 0.2};
  traffic.update({a, b, c}, {}, 0.2);
  Object3D* third = traffic.group->children[2].get();
  assert(near(third->position.x, 30.5) && near(third->position.z, -9.5) && near(third->position.y, 100.03));
  // one leaves, another of its body comes: it takes the model that became free, in its own paint
  TrafficCar* d = cars.add(9, 0, 0x35c06a, 90);
  traffic.update({b, c}, {}, 0.3);
  assert(traffic.group->children.size() == 2 && first->parent == nullptr);
  traffic.update({b, c, d}, {}, 0.4);
  assert(traffic.group->children.size() == 3 && traffic.group->children[2].get() == first && near(first->position.x, 90));
  Mesh& body = static_cast<Mesh&>(*first->children[0]);
  assert(body.material->color.getHex() == 0x35c06a);
  // … a car of another body gets a model of its own
  TrafficCar* e = cars.add(10, 2, 0x111111, 95);
  traffic.update({b, c, d, e}, {}, 0.5);
  assert(traffic.group->children.size() == 4 && traffic.group->children[3].get() != first);
  // nobody left: no model in the picture
  traffic.update({}, {}, 0.6);
  assert(traffic.group->children.empty());
}

void lampsAndWheels() {
  TrafficRenderer traffic;
  Cars cars;
  TrafficCar* a = cars.add(4, 0, 0xd9dadc, 10);
  a->blinker = -1;
  a->braking = true;
  a->drawn = EasedPose{10, -12, 0.4, std::nullopt, 0.25};
  // the materials of its lamps: the meshes of the body in the order the car is built (paint, trim, pillars, then head, indicator, tail, indicator, reversing lamp of each side)
  const auto lamp = [&traffic](size_t child) { return static_cast<Mesh&>(*traffic.group->children[0]->children[child]).material->color.getHex(); };
  // (before them in the group: the body's paint, its glass, trim and pillars, the four wheels, the two mirror housings)
  const size_t head = 10, blinkL = 11, tail = 12, reversing = 14, blinkR = 16;
  traffic.update({a}, {}, 0);   // (time 0: the indicators' lamps are on)
  assert(lamp(head) == 0xf4f6f0 && lamp(tail) == 0xff2a1e && lamp(reversing) == 0x777777 && lamp(blinkL) == 0xffa010 && lamp(blinkR) == 0x6a3a08);
  traffic.update({a}, {}, 0.4);   // (… and off: three times a second, on and off)
  assert(lamp(blinkL) == 0x6a3a08);
  traffic.update({a}, {}, 0.7);
  assert(lamp(blinkL) == 0xffa010);
  a->blinker = 1;
  a->braking = false;
  traffic.update({a}, {}, 0.7);
  assert(lamp(blinkL) == 0x6a3a08 && lamp(blinkR) == 0xffa010 && lamp(tail) == 0xa01410);
  // the main beam, where the game says so
  traffic.highOf = [](int64_t id) { return id == 4; };
  traffic.update({a}, {}, 0.7);
  assert(lamp(head) == 0xffffff);
  traffic.highOf = nullptr;
  traffic.update({a}, {}, 0.7);
  assert(lamp(head) == 0xf4f6f0);
  // the wheels: the front ones steered, all turned as far as the car has rolled
  Object3D& model = *traffic.group->children[0];
  std::vector<Object3D*> wheels;
  for (const ObjectPtr& o : model.children)
    if (o->type == ObjectType::Group) wheels.push_back(o.get());
  assert(wheels.size() == 4);
  assert(near(wheels[0]->rotation.y, -0.25) && near(wheels[1]->rotation.y, -0.25) && near(wheels[2]->rotation.y, 0) && near(wheels[3]->rotation.y, 0));
  for (Object3D* w : wheels) assert(near(w->children[0]->rotation.x, -6.0) && near(w->position.y, COMPACT_DIMS.wheelRadius));
}

void theHooks() {
  // ---- without the parts: buses, emergency vehicles, cyclists and people are left out ----
  Cars cars;
  TrafficCar* car = cars.add(1, 0, 0xd9dadc, 10);
  TrafficCar* bus = cars.add(2, BUS_STYLE, 0xffffff, 20);
  TrafficCar* police = cars.add(3, 8, 0xffffff, 30);
  TrafficCar* fire = cars.add(4, 13, 0xffffff, 40);
  std::deque<Cyclist> riders(2);
  riders[0].id = 1;
  riders[1].id = 2;
  std::deque<Pedestrian> walkers(3);
  {
    TrafficRenderer traffic;
    traffic.update({car, bus, police, fire}, {&walkers[0], &walkers[1]}, 1, {&riders[0], &riders[1]});
    assert(traffic.group->children.size() == 1);
  }

  // ---- with them ----
  PeopleRenderer::create = [](int capacity) { return std::make_unique<TestPeople>(capacity); };
  EmergencyFleet::create = []() { return std::make_unique<TestEmergency>(); };
  PlayerBus::create = []() { return std::make_unique<TestBus>(); };
  CyclistModel::create = [](int colour) { return std::make_unique<TestCyclist>(colour); };
  {
    TrafficRenderer traffic;
    // the people's and the emergency vehicles' pictures hang in the traffic's group from the start
    assert(TestPeople::made && TestPeople::made->capacity == CROWD_MAX && traffic.people.get() == TestPeople::made);
    assert(traffic.group->children.size() == 2 && traffic.group->children[0] == TestEmergency::made->group && traffic.group->children[1] == TestPeople::made->group);

    // a bus: its own model, its lamps and its line
    bus->blinker = 1;
    bus->braking = true;
    bus->v = 10;
    bus->wheelSpin = 2.5;
    bus->drawn = EasedPose{21, -6.5, 0.3, std::nullopt, 0.15};
    bus->bus = TrafficCar::Bus();
    bus->bus->line = BusOnLine();
    bus->bus->line->ref = "500";
    bus->bus->line->to = "Baunatal";
    traffic.update({car, bus, police, fire}, {}, 0);
    // (every car of the list goes to the emergency vehicles' picture, which takes its own; the car and the bus are drawn here)
    assert(TestEmergency::made->cars == 4);
    assert(traffic.group->children.size() == 4 && TestBus::made == 1);
    TestBus& model = *TestBus::newest;
    assert(traffic.group->children[3] == model.root && model.root->children[0]->castShadow);
    assert(near(model.root->position.x, 21) && near(model.root->position.z, -6.5) && near(model.root->position.y, bus->y));
    assert(model.ref == "500" && model.to == "Baunatal" && near(model.at, 0));
    const CarVisualState& s = model.last;
    assert(s.indicator == 1 && s.blinkOn && s.braking && near(s.speedKmh, 36) && s.lowBeam && s.automatic);
    assert(s.wheelSpin == std::vector<double>({2.5, 2.5, 2.5, 2.5}) && s.wheelSteer == std::vector<double>({0.15, 0.15, 0, 0}) && s.wheelY == std::vector<double>({0.48, 0.48, 0.48, 0.48}));
    // a bus without a line shows none; gone, its model waits for the next bus
    bus->bus->line.reset();
    traffic.update({bus}, {}, 0.4);
    assert(model.ref.empty() && model.to.empty() && !model.last.blinkOn && traffic.group->children.size() == 3);
    traffic.update({}, {}, 1);
    assert(traffic.group->children.size() == 2 && model.root->parent == nullptr);
    traffic.update({bus}, {}, 2);
    assert(TestBus::made == 1 && model.root->parent == traffic.group.get());

    // cyclists: a model each, in their colour, posed as they ride – or lie
    riders[0].x = 5, riders[0].y = 100, riders[0].z = -7, riders[0].yaw = 0.5, riders[0].colour = 0x3d8bff, riders[0].phase = 1.5, riders[0].roll = 9, riders[0].down = 0;
    riders[1].colour = 0xffb020, riders[1].down = 2;
    traffic.update({}, {}, 3, {&riders[0], &riders[1]});
    assert(TestCyclist::made == 2 && traffic.group->children.size() == 4);
    TestCyclist& lying = *TestCyclist::newest;
    assert(lying.colour == 0xffb020 && lying.down);
    Object3D& ridden = *traffic.group->children[2];
    assert(near(ridden.position.x, 5) && near(ridden.position.z, -7) && near(ridden.rotation.y, 0.5));
    // the first left: the next cyclist to come takes its model, in the new colour
    traffic.update({}, {}, 4, {&riders[1]});
    assert(traffic.group->children.size() == 3);
    riders[0].id = 7;
    riders[0].colour = 0x35c06a;
    traffic.update({}, {}, 5, {&riders[1], &riders[0]});
    assert(TestCyclist::made == 2 && traffic.group->children.size() == 4);

    // people: as the traffic has them; without a camera everybody is drawn
    walkers[0].x = 10, walkers[0].z = 0, walkers[0].y = 100, walkers[0].yaw = 1, walkers[0].phase = 12.5, walkers[0].colour = 7, walkers[0].head = 0.3, walkers[0].startle = 0.5, walkers[0].lean = 0.1, walkers[0].umbrella = true;
    walkers[1].x = 0, walkers[1].z = 100, walkers[1].phase = -1, walkers[1].colour = 3;
    walkers[2].x = 0, walkers[2].z = -300;
    traffic.update({}, {&walkers[0], &walkers[1], &walkers[2]}, 6);
    TestPeople& people = *TestPeople::made;
    assert(people.last.size() == 3 && near(people.at, 6));
    const PersonPose& p = people.last[0];
    assert(p.x == 10 && p.y == 100 && p.yaw == 1 && p.walk == 12.5 && p.look == 7 && p.head && *p.head == 0.3 && *p.startle == 0.5 && *p.lean == 0.1 && *p.umbrella && !p.sit && !p.arms);
    assert(people.last[1].walk == -1 && people.last[1].look == 3);
    // with the camera (at the origin, looking north): not those more than 220 m off, nor those further than 45 m behind it
    const std::shared_ptr<Camera> camera = perspectiveCamera();
    camera->updateMatrixWorld();
    traffic.view = camera.get();
    traffic.update({}, {&walkers[0], &walkers[1], &walkers[2]}, 7);
    assert(people.last.size() == 1 && people.last[0].look == 7);
    walkers[1].z = 40;    // behind, but near: a mirror may show them
    walkers[2].z = -200;  // ahead, within reach
    traffic.update({}, {&walkers[0], &walkers[1], &walkers[2]}, 8);
    assert(people.last.size() == 3);
  }
  PeopleRenderer::create = nullptr;
  EmergencyFleet::create = nullptr;
  PlayerBus::create = nullptr;
  CyclistModel::create = nullptr;
}

/** the colour of a lamp instance */
uint32_t colourAt(const InstancedMesh& lamps, int i) {
  Color c;
  lamps.getColorAt(i, c);
  return c.getHex();
}

void theHeads() {
  // one junction with lights in a town with pavements: four approaches, a head at the right-hand kerb of each
  TownOptions opt;
  opt.cols = 3;
  opt.rows = 3;
  opt.pavement = 2;
  opt.lights = {{1, 1}};
  Town town = makeTown(opt);
  SignalSystem signals(town.world->signals);
  SignalRenderer heads(*town.world, &signals);
  assert(heads.group->name == "signals" && heads.group->children.size() == 2);
  assert(heads.spots.size() == 4 && heads.poles.size() == 16);
  InstancedMesh& housing = static_cast<InstancedMesh&>(*heads.group->children[0]);
  InstancedMesh& lamps = static_cast<InstancedMesh&>(*heads.group->children[1]);
  assert(housing.type == ObjectType::InstancedMesh && housing.count == 4 && lamps.count == 12 && !housing.frustumCulled && !lamps.frustumCulled);
  // each stands beside its road a little beyond the stop line, on the kerb, and faces those who come
  const Node& junction = *town.world->nodes[town.node(1, 1)];
  for (size_t i = 0; i < 4; i++) {
    const HeadSpot& s = heads.spots[i];
    const double dx = s.x - junction.x, dz = s.z - junction.z;
    assert(std::hypot(dx, dz) > 3 && std::hypot(dx, dz) < 25);
    assert(near(s.y, 100 + KERB_H, 0.02) && s.controller == 0 && (s.group == 0 || s.group == 1) && !s.arrow);
    // (the head looks along its −z: away from the junction, at the approaching cars)
    const Vector3 facing = Vector3(0, 0, -1).applyAxisAngle(Vector3(0, 1, 0), s.yaw);
    assert(facing.x * dx + facing.z * dz > 0.9 * std::hypot(dx, dz) - 4);
    assert(near(heads.poles[i * 4], s.x) && near(heads.poles[i * 4 + 1], s.y) && near(heads.poles[i * 4 + 2], s.z) && near(heads.poles[i * 4 + 3], 0.08));
    // its three lamps hang one above the other, 29 cm apart, the red one on top
    Matrix4 m;
    double y[3];
    for (int k = 0; k < 3; k++) {
      lamps.getMatrixAt(static_cast<int>(i) * 3 + k, m);
      const Vector3 at = Vector3().setFromMatrixPosition(m);
      y[k] = at.y;
      assert(std::hypot(at.x - s.x, at.z - s.z) < 0.1);
    }
    assert(near(y[0] - y[1], 0.29, 1e-4) && near(y[1] - y[2], 0.29, 1e-4) && near(y[0], s.y + 3.04, 1e-4));
  }
  // the lamps follow the lights: the main direction (group 0) begins with green, the side direction with red
  heads.update(signals);
  for (size_t i = 0; i < 4; i++) {
    const int at = static_cast<int>(i) * 3;
    if (heads.spots[i].group == 0) assert(colourAt(lamps, at) == 0x141414 && colourAt(lamps, at + 1) == 0x141414 && colourAt(lamps, at + 2) == 0x1aff6a);
    else assert(colourAt(lamps, at) == 0xff2a1a && colourAt(lamps, at + 1) == 0x141414 && colourAt(lamps, at + 2) == 0x141414);
  }
  // nothing changed: the colours are not sent to the graphics card again
  const uint32_t version = lamps.instanceColor->version;
  heads.update(signals);
  assert(lamps.instanceColor->version == version);
  // amber after 28 s, then red – and red with amber before the green comes back
  signals.update(29);
  heads.update(signals);
  assert(lamps.instanceColor->version == version + 1);
  for (size_t i = 0; i < 4; i++) {
    const int at = static_cast<int>(i) * 3;
    if (heads.spots[i].group == 0) assert(colourAt(lamps, at) == 0x141414 && colourAt(lamps, at + 1) == 0xffb000 && colourAt(lamps, at + 2) == 0x141414);
  }
  signals.update(3.5);
  heads.update(signals);
  for (size_t i = 0; i < 4; i++) {
    const int at = static_cast<int>(i) * 3;
    if (heads.spots[i].group == 1) assert(colourAt(lamps, at) == 0xff2a1a && colourAt(lamps, at + 1) == 0xffb000 && colourAt(lamps, at + 2) == 0x141414);
    else assert(colourAt(lamps, at) == 0xff2a1a && colourAt(lamps, at + 2) == 0x141414);
  }

  // a green arrow beside one head: a small housing and two arrow lamps more
  const Arm* approach = nullptr;
  for (const Arm* arm : junction.arms)
    if (arm->isApproach && arm->lamp == 1 && !approach) approach = arm;
  assert(approach);
  Arrow arrow;
  arrow.controller = 0;
  arrow.own = 1;
  signals.arrows.set(approach, arrow);
  heads.rebuild(*town.world, &signals);
  assert(heads.group->children.size() == 4 && heads.spots.size() == 4 && heads.poles.size() == 16);
  int withArrow = 0;
  for (const HeadSpot& s : heads.spots) withArrow += s.arrow == approach ? 1 : 0;
  assert(withArrow == 1);
  InstancedMesh& arrowHousing = static_cast<InstancedMesh&>(*heads.group->children[2]);
  InstancedMesh& arrowLamps = static_cast<InstancedMesh&>(*heads.group->children[3]);
  assert(arrowHousing.count == 1 && arrowLamps.count == 2);
  heads.update(signals);
  assert(colourAt(arrowLamps, 0) == 0x141414 && colourAt(arrowLamps, 1) == 0x141414);
  signals.arrows.get(approach)->state = SIG_GREEN;
  heads.update(signals);
  assert(colourAt(arrowLamps, 0) == 0x141414 && colourAt(arrowLamps, 1) == 0x1aff6a);
  signals.arrows.get(approach)->state = SIG_AMBER;
  heads.update(signals);
  assert(colourAt(arrowLamps, 0) == 0xffb000 && colourAt(arrowLamps, 1) == 0x141414);
  // set up anew without the signals: the heads, no arrow
  heads.rebuild(*town.world);
  assert(heads.group->children.size() == 2 && heads.spots.size() == 4);

  // lights that are out of order stand there all the same – and flash amber, once a second
  opt.lightsOut = [](int) { return true; };
  Town dark = makeTown(opt);
  SignalSystem broken(dark.world->signals, opt.lightsOut);
  SignalRenderer flashing(*dark.world, &broken);
  assert(flashing.spots.size() == 4);
  InstancedMesh& lit = static_cast<InstancedMesh&>(*flashing.group->children[1]);
  flashing.update(broken);
  for (int i = 0; i < 4; i++) assert(colourAt(lit, i * 3) == 0x141414 && colourAt(lit, i * 3 + 1) == 0xffb000 && colourAt(lit, i * 3 + 2) == 0x141414);
  broken.update(0.6);
  flashing.update(broken);
  for (int i = 0; i < 4; i++) assert(colourAt(lit, i * 3 + 1) == 0x141414);
  broken.update(0.5);
  flashing.update(broken);
  for (int i = 0; i < 4; i++) assert(colourAt(lit, i * 3 + 1) == 0xffb000);

  // a wide road has a second head on the left; a town without lights has none (and nothing is drawn of them)
  TownOptions wide;
  wide.cols = 3;
  wide.rows = 2;
  wide.wide = {{0, 0, 1, 0}};
  wide.lights = {{1, 0}};
  Town broad = makeTown(wide);
  SignalRenderer more(*broad.world);
  assert(more.spots.size() == 3 + 1);
  Town plain = makeTown({});
  SignalRenderer none(*plain.world);
  assert(none.spots.empty() && none.poles.empty() && static_cast<InstancedMesh&>(*none.group->children[0]).count == 0 && static_cast<InstancedMesh&>(*none.group->children[1]).count == 0);
}

/** everything, number by number: the lines tools/diff/render_cars.ts --part actors prints of the original */
void againstTheOriginal() {
  dump::lines().clear();
  // ---- the cars of the traffic ----
  {
    TrafficRenderer traffic;
    Cars cars;
    const auto shown = [&traffic](const std::string& name) {
      out("traffic", word(name), static_cast<int>(traffic.group->children.size()));
      for (const ObjectPtr& g : traffic.group->children) rdump::pose("car", *g);
    };
    TrafficCar* c1 = cars.add(1, 0, 0xd9dadc, 10);
    TrafficCar* c2 = cars.add(2, 1, 0x1b3fa0, 20);
    c2->blinker = -1;
    c2->braking = true;
    TrafficCar* c3 = cars.add(3, 2, 0x8a1c1c, 30);
    c3->blinker = 1;
    c3->drawn = EasedPose{30.5, -9.5, 0.35, std::nullopt, 0.2};
    TrafficCar* c4 = cars.add(4, 0, 0x202428, 40);
    c4->drawn = EasedPose{40.25, -12.5, 0.45, std::nullopt, std::nullopt};
    const std::vector<TrafficCar*> a = {c1, c2, c3, c4};
    traffic.update(a, {}, 0);
    shown("four cars");
    traffic.highOf = [](int64_t id) { return id == 2; };
    traffic.update(a, {}, 0.4);
    shown("the indicators off, a main beam");
    traffic.highOf = nullptr;
    // two leave, one comes: it takes the model of one that left (the same style), in its own paint
    traffic.update({c2, c3, cars.add(7, 0, 0x35c06a, 70)}, {}, 1);
    shown("two left, one came");
    traffic.update({cars.add(8, 0, 0xffb020, 80), cars.add(9, 0, 0x3d8bff, 90), cars.add(10, 2, 0x111111, 100)}, {}, 2.7);
    shown("all others");
    traffic.update({}, {}, 3);
    shown("none");
    traffic.update(a, {}, 4);
    shown("the first four again");
  }

  // ---- the traffic lights' heads ----
  const auto heads = [](const std::string& name, TownOptions opt, const std::vector<int>& arrowAt, const std::function<bool(int)>& lightsOut) {
    out("town", word(name));
    opt.lightsOut = lightsOut;
    Town town = makeTown(opt);
    World& world = *town.world;
    SignalSystem signals(world.signals, lightsOut);
    // (a green arrow, hung by hand beside the head of an approach: sim/arrows would work out where one belongs)
    const Arm* arrowArm = nullptr;
    if (!arrowAt.empty()) {
      const Node& node = *world.nodes[town.node(arrowAt[0], arrowAt[1])];
      for (const Arm* arm : node.arms)
        if (!arrowArm && arm->isApproach && arm->lamp == arrowAt[2]) arrowArm = arm;
      Arrow arrow;
      arrow.controller = node.sc;
      arrow.own = arrowArm->lamp;
      signals.arrows.set(arrowArm, arrow);
    }
    SignalRenderer r(world, &signals);
    const auto all = [&r]() {
      out("spots", static_cast<int>(r.spots.size()), static_cast<int>(r.poles.size()), static_cast<int>(r.group->children.size()));
      for (const HeadSpot& s : r.spots) out("spot", P(s.x), P(s.y), P(s.z), P(s.yaw), s.controller, s.group, s.arrow ? 1 : 0);
      out("poles", rdump::absSum(r.poles));
      rdump::tree("heads", *r.group);
    };
    all();
    const auto lit = [&r, &signals](const std::string& what) {
      r.update(signals);
      std::vector<double> sums;
      std::vector<int> versions;
      for (const ObjectPtr& o : r.group->children) {
        const InstancedMesh& m = static_cast<InstancedMesh&>(*o);
        sums.push_back(m.instanceColor ? rdump::absSum(*m.instanceColor, static_cast<size_t>(m.count) * 3) : -1.0);
        // (the engine's arrays begin at version 1, three.js's at 0)
        versions.push_back(m.instanceColor ? static_cast<int>(m.instanceColor->version) - 1 : -1);
      }
      out("lit", word(what), sums, versions);
    };
    lit("at the start");
    lit("nothing changed");
    for (const double t : {0.3, 0.3, 27.5, 3.0, 1.0, 1.0, 9.0, 9.25, 3.5}) {
      signals.update(t);
      lit("after " + jsStr(t) + " s");
    }
    if (arrowArm) {
      signals.arrows.get(arrowArm)->state = SIG_GREEN;
      lit("the arrow green");
      signals.arrows.get(arrowArm)->state = SIG_AMBER;
      lit("the arrow amber");
      signals.arrows.get(arrowArm)->state = -1;
      lit("the arrow dark");
    }
    // set up anew (the network comes in pieces): the same heads, no arrow without the signals
    r.rebuild(world);
    all();
    r.rebuild(world, &signals);
    out("rebuilt", static_cast<int>(r.spots.size()), static_cast<int>(r.poles.size()), static_cast<int>(r.group->children.size()));
  };
  TownOptions opt;
  opt.cols = 4;
  opt.rows = 3;
  opt.pavement = 2;
  opt.deadEnd = 60;
  opt.wide = {{1, 1, 2, 1}};
  opt.oneWay = {{0, 0, 1, 0}};
  opt.lights = {{1, 1}, {2, 1}, {1, 0}};
  opt.crossings = {{{2, 0, 3, 0}, 0.5, XING_ZEBRA}, {{1, 2, 2, 2}, 0.4, XING_SIGNAL}, {{2, 1, 3, 1}, 0.2, XING_SIGNAL}, {{0, 0, 1, 0}, 0.3, XING_SIGNAL}};
  heads("with pavements, an arrow", opt, {1, 1, 1}, nullptr);
  heads("one controller out of order", opt, {}, [](int c) { return c == 0; });
  TownOptions small;
  small.cols = 3;
  small.rows = 2;
  small.crossings = {{{0, 0, 1, 0}, 0.5, XING_SIGNAL}};
  heads("no pavements, no lights at junctions", small, {}, nullptr);
  TownOptions tiny;
  tiny.cols = 2;
  tiny.rows = 2;
  heads("no lights at all", tiny, {}, nullptr);

  const int bad = rdump::against("tests/fixtures/render-actors.txt", 1e-6);
  if (bad) {
    std::fprintf(stderr, "render_actors: %d lines differ from the original's\n", bad);
    std::exit(1);
  }
}

}  // namespace

int main() {
  againstTheOriginal();
  theCarsFollowTheTraffic();
  lampsAndWheels();
  theHooks();
  theHeads();
  std::printf("render_actors: all checks passed (%zu lines the same as the original's)\n", dump::lines().size());
  return 0;
}
