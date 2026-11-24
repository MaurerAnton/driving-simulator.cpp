// LINK: render engine game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * tests/game/cardoors.test.ts: the parts that open and the seats of the car: how far a part is
 * open as time goes, getting in and out step by step (the door opens, one moves, the door shuts),
 * which seat a door leads to, what one can do where one stands – and the car's body cut into its
 * parts: the pieces cover exactly the surface of the whole, and with every part shut the car is
 * drawn from its whole meshes again.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include "game/cardoors.h"
#include "render/car.h"
#include "render/carBody.h"
#include "render/caropen.h"

using namespace fs;

namespace {

bool sameEvents(const std::vector<OpeningEvent>& a, const std::vector<std::pair<std::string, std::string>>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (a[i].id != b[i].first || a[i].what != b[i].second) return false;
  return true;
}

void aPartOpensAndShuts() {
  Openings o;
  std::vector<OpeningEvent> scratch;
  assert(o.get("fl") == 0);
  assert(!o.anyOpen());
  assert(o.toggle("fl"));
  std::vector<OpeningEvent> ev;
  o.step(0.1, ev);
  assert(sameEvents(ev, {{"fl", "opening"}}));
  assert(o.get("fl") > 0 && o.get("fl") < 1);
  for (int i = 0; i < 20; i++) o.step(0.1, scratch);
  assert(o.get("fl") == 1);
  assert(o.anyOpen());
  // halfway shut, then opened again: it turns round where it is
  o.set("fl", false);
  o.step(0.3, scratch);
  const double mid = o.get("fl");
  assert(mid < 1 && mid > 0);
  assert(o.toggle("fl"));
  o.step(0.1, scratch);
  assert(o.get("fl") > mid);
  o.set("fl", false);
  ev.clear();
  for (int i = 0; i < 20; i++) o.step(0.1, ev);
  assert(o.get("fl") == 0);
  assert(sameEvents(ev, {{"fl", "shut"}}));
  assert(!o.anyOpen());
  // the bonnet is slower than a door
  o.set("bonnet", true);
  o.set("fr", true);
  o.step(0.5, scratch);
  assert(o.get("bonnet") < o.get("fr"));
  o.reset();
  assert(!o.anyOpen());
}

void eachDoorLeadsToItsSeat() {
  assert(seatOfDoor("fl") == "driver");
  assert(seatOfDoor("fr") == "front");
  assert(seatOfDoor("rl") == "rearLeft");
  assert(seatOfDoor("rr") == "rearRight");
  for (const DoorId& d : DOORS) assert(doorOfSeat(seatOfDoor(d)) == d);
  assert(isDoor("rr") && !isDoor("bonnet") && !isDoor("boot") && !isDoor("trailer"));
  // the eyes: the driver on the left, the passenger on the right, the rear seats further back
  assert(seatEye("driver")[0] < 0 && seatEye("front")[0] > 0);
  assert(seatEye("rearLeft")[2] > seatEye("driver")[2] + 0.6 && seatEye("rearRight")[0] > 0);
}

void gettingIn() {
  Openings o;
  std::vector<OpeningEvent> scratch;
  Boarding b("in", "fr");
  assert(b.seat == "front");
  std::vector<std::string> log;
  double seatedAt = -1;
  double t = 0;
  for (int i = 0; i < 200 && b.phase != "done"; i++) {
    o.step(0.05, scratch);
    const BoardStep r = b.step(0.05, o);
    t += 0.05;
    if (r.start) log.push_back(std::string("start ") + (o.get("fr") >= 0.85 ? "true" : "false"));
    if (r.arrive) {
      log.push_back("arrive");
      seatedAt = t;
    }
    if (r.done) log.push_back("done " + std::to_string(static_cast<int>(o.get("fr"))));
    if (b.phase == "move") assert(b.blend() >= 0 && b.blend() <= 1);
  }
  assert((log == std::vector<std::string>{"start true", "arrive", "done 0"}));
  assert(seatedAt > MOVE_TIME);
  assert(b.blend() == 1);
  assert(!o.anyOpen());
}

void gettingOutThroughAnOpenDoor() {
  Openings o;
  std::vector<OpeningEvent> scratch;
  o.set("rl", true);
  for (int i = 0; i < 30; i++) o.step(0.05, scratch);
  Boarding b("out", "rl");
  const BoardStep r = b.step(0.016, o);
  assert(r.start);
  assert(b.phase == "move");
  assert(b.blend() == 0);
}

void whereOneStands() {
  const SpotUnit unit = {.x = 100, .z = 50, .yaw = 0, .front = 2.2, .rear = 2.1, .hw = 0.9};
  const CarSpotEdges edges = {.frontAhead = 0.1, .rearDoorRear = 1.06, .nose = -2.2, .tail = 2.1};
  const std::vector<OpeningSpot> spots = carSpots(unit, edges);
  assert(spots.size() == 6);
  const auto spot = [](const std::vector<OpeningSpot>& l, const std::string& id) {
    for (const OpeningSpot& s : l)
      if (s.id == id) return s;
    assert(false);
    return OpeningSpot{};
  };
  const OpeningSpot fl = spot(spots, "fl");
  const OpeningSpot fr = spot(spots, "fr");
  const OpeningSpot bonnet = spot(spots, "bonnet");
  const OpeningSpot boot = spot(spots, "boot");
  // facing north (−z): left is west (−x), the bonnet to the north, the tailgate to the south
  assert(fl.x < 100 && fr.x > 100);
  assert(bonnet.z < 50 - 2.2 && boot.z > 50 + 2.1);
  assert(nearestSpot(spots, fl.x + 0.3, fl.z)->id == "fl");
  assert(!nearestSpot(spots, 100, 50));
  // turned round (yaw π): the left door is on the east side now
  SpotUnit turnedUnit = unit;
  turnedUnit.yaw = PI;
  assert(spot(carSpots(turnedUnit, edges), "fl").x > 100);
  const OpeningSpot back = rearSpot("trailer", unit);
  assert(back.z > 50 + 2.1 && std::fabs(back.x - 100) < 1e-9);
  Openings o;
  assert((footActs("fl", o, true) == std::vector<FootAct>{"open", "enter"}));
  assert((footActs("fl", o, false) == std::vector<FootAct>{"open"}));
  assert((footActs("bonnet", o, true) == std::vector<FootAct>{"open"}));
  o.set("fl", true);
  assert((footActs("fl", o, true) == std::vector<FootAct>{"close", "enter"}));
}

void theShellIsCut() {
  const CarShell sh = buildCarShell(COMPACT_DIMS);
  const PanelLines L = panelLines(sh, COMPACT_DIMS);
  // a point on the flank of each door, on the bonnet, on the rear window, on the roof, on a wing
  assert(shellPart(L, -0.89, 0.7, 0, 3, false) == 0);
  assert(shellPart(L, 0.89, 0.7, 0, 3, false) == 1);
  assert(shellPart(L, -0.89, 0.7, 0.8, 3, false) == 2);
  assert(shellPart(L, 0.89, 1.1, 0.8, 4, false) == 3);
  assert(shellPart(L, 0, 0.85, -1.6, 5, false) == 4);
  assert(shellPart(L, 0.2, 1.2, 1.6, 5, false) == 5);
  assert(shellPart(L, 0, 1.46, 0, 5, false) == -1);
  assert(shellPart(L, 0.85, 0.75, -1.6, 4, false) == -1);
  // below the sill and over the rear wheel: the body
  assert(shellPart(L, 0.89, 0.3, 0, 2, false) == -1);
  assert(shellPart(L, 0.89, 0.5, 1.05, 3, false) == -1);
}

double area(const BufferGeometry& g, const Matrix4& m = Matrix4()) {
  const AttrPtr p = g.getAttribute("position");
  const AttrPtr& idx = g.index;
  const size_t n = idx ? idx->count() : p->count();
  Vector3 a, b, c;
  double s = 0;
  for (size_t i = 0; i + 2 < n; i += 3) {
    a.fromBufferAttribute(*p, idx ? static_cast<size_t>(idx->getX(i)) : i).applyMatrix4(m);
    b.fromBufferAttribute(*p, idx ? static_cast<size_t>(idx->getX(i + 1)) : i + 1).applyMatrix4(m);
    c.fromBufferAttribute(*p, idx ? static_cast<size_t>(idx->getX(i + 2)) : i + 2).applyMatrix4(m);
    s += b.sub(a).cross(c.sub(a)).length() / 2;
  }
  return s;
}

void cutIntoItsParts() {
  const CarShell sh = buildCarShell(COMPACT_DIMS);
  const auto root = make<Group>();
  const auto ext = make<Group>();
  const auto cockpit = make<Group>();
  root->add(ext, cockpit);
  const MaterialPtr paintMat = basic();
  const MaterialPtr glassMat = basic();
  const auto paint = make<Mesh>(sh.paint, paintMat);
  const auto glass = make<Mesh>(sh.glass, glassMat);
  const auto lining = make<Mesh>(sh.lining, basic());
  ext->add(paint, glass);
  cockpit->add(lining);
  const std::vector<DoorTrim> noTrim;
  CarOpenings o(OpeningsHost{.root = root, .exterior = {.group = ext, .shell = &sh, .dims = &COMPACT_DIMS}, .cockpit = cockpit, .mirrors = {}, .doorTrim = &noTrim, .register_ = [](const std::shared_ptr<Mesh>&) {}});
  // shut: nothing changed
  assert(paint->geometry == sh.paint);
  o.pose({0.001, 0.001, 0.001, 0.001, 0.001, 0.001});
  assert(paint->geometry != sh.paint);
  std::vector<Mesh*> parts;
  root->traverse([&](Object3D& m) {
    const std::string tail = "(opening)";
    if (m.type == ObjectType::Mesh && m.name.size() >= tail.size() && m.name.compare(m.name.size() - tail.size(), tail.size(), tail) == 0) parts.push_back(static_cast<Mesh*>(&m));
  });
  const auto ofMat = [&](const MaterialPtr& mat) {
    std::vector<Mesh*> l;
    for (Mesh* m : parts)
      if (m->material == mat) l.push_back(m);
    return l;
  };
  assert(ofMat(paintMat).size() == 6);   // six parts of paint
  assert(ofMat(glassMat).size() >= 5);   // the windows of the doors and the rear window
  for (const auto& [whole, mesh, mat] : {std::tuple{sh.paint, paint, paintMat}, std::tuple{sh.glass, glass, glassMat}}) {
    double sum = area(*mesh->geometry);
    for (Mesh* m : ofMat(mat)) sum += area(*m->geometry);
    const double w = area(*whole);
    if (!(std::fabs(sum - w) < 1e-4 * w)) std::printf("surface kept: %g vs %g\n", sum, w);
    assert(std::fabs(sum - w) < 1e-4 * w);
  }
  // the normals of the pieces are the shell's (no new shading): every vertex normal of a piece is as long as an interpolated one may be
  for (Mesh* m : ofMat(paintMat)) {
    const AttrPtr n = m->geometry->getAttribute("normal");
    for (size_t i = 0; i < n->count(); i++) assert(std::hypot(n->getX(i), n->getY(i), n->getZ(i)) > 0.3);
  }
  // the parts where they belong: the front left door left of the middle, between the windscreen and the B pillar
  const PanelLines L = panelLines(sh, COMPACT_DIMS);
  Mesh* fl = nullptr;
  for (Mesh* m : ofMat(paintMat))
    if (m->parent && m->parent->parent && m->parent->parent->position.x == -sh.halfWidth && m->parent->parent->position.z == L.zFD) fl = m;
  assert(fl);
  fl->geometry->computeBoundingBox();
  const Box3& bb = *fl->geometry->boundingBox;
  assert(bb.max.x < 0 && bb.min.z >= L.zFD - 1e-4 && bb.max.z <= L.zB + 1e-4 && bb.min.y >= L.sill - 1e-4);
  // opened, the door swings out to the left; the bonnet lifts
  o.pose({1, 0, 0, 0, 1, 0});
  root->updateMatrixWorld(true);
  const Box3 fb = Box3().setFromObject(*fl);
  if (!(fb.min.x < -sh.halfWidth - 0.8)) std::printf("door out: %g\n", fb.min.x);
  assert(fb.min.x < -sh.halfWidth - 0.8);
  // shut again: the whole meshes, the parts hidden
  o.pose({0, 0, 0, 0, 0, 0});
  assert(paint->geometry == sh.paint);
  assert(glass->geometry == sh.glass);
  assert(lining->geometry == sh.lining);
  for (Mesh* m : parts) assert(!m->visible);
}

}  // namespace

int main() {
  aPartOpensAndShuts();
  eachDoorLeadsToItsSeat();
  gettingIn();
  gettingOutThroughAnOpenDoor();
  whereOneStands();
  theShellIsCut();
  cutIntoItsParts();
  std::printf("cardoors_test: ok\n");
  return 0;
}
