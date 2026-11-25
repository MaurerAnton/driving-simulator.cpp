// SPDX-License-Identifier: AGPL-3.0-only
// LINK: render sim game engine world core platform geo
// Origin: tests/game/airfield.test.ts @ v1.7.7
/**
 * The airfields as an aeroplane meets them: firm ground on the runway, the
 * taxiway and the apron, a mown strip on a grass runway; where a flight
 * begins (lined up at a runway's end) and how its ends are called.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/engine.h"
#include "render/airfield.h"
#include "sim/airfield.h"

using namespace fs;

namespace {

/** flat grass everywhere */
class Grass : public Ground {
 public:
  void sample(double, double, double, GroundSample& out) override {
    out.y = 200;
    out.nx = 0;
    out.ny = 1;
    out.nz = 0;
    out.grip = 0.7;
    out.rolling = 4;
  }
};

AirfieldJson makeField() {
  AirfieldJson f;
  f.name = "Testfeld";
  f.icao = "EDXX";
  f.kind = "airport";
  // a runway east–west (09/27) 1000 m long, a taxiway to an apron, and a grass strip north–south
  f.runways = {{"09/27", -500, 0, 500, 0, 30, "asphalt"}, {"", 0, 300, 0, -300, 30, "grass"}};
  f.taxiways = {{10, "asphalt", {200, 0, 200, 100}}};
  f.aprons = {{"concrete", {180, 100, 260, 100, 260, 160, 180, 160, 180, 100}}};
  return f;
}

}  // namespace

int main() {
  const AirfieldJson field = makeField();

  // the runway, its taxiway and apron are firm; the grass strip rolls like a mown field; elsewhere the ground is as it is
  {
    Grass grass;
    AirfieldGround g(grass, {field});
    GroundSample s{0, 0, 1, 0, 0, 0};
    for (const auto& [x, z] : std::vector<std::pair<double, double>>{{-300, 5}, {200, 60}, {220, 130}}) {
      g.sample(x, z, 201, s);
      assert(s.rolling == 1);
      assert(s.grip >= 1);
    }
    g.sample(5, -200, 201, s);
    assert(s.rolling > 1 && s.rolling < 4);
    g.sample(-300, 80, 201, s);
    assert(s.rolling == 4);
    assert(paved("asphalt") == true);
    assert(paved("grass") == false);
  }

  // a flight begins lined up at the end of a runway – the ends called as they are painted
  {
    const RunwayJson& r = field.runways[0];
    const RunwayStart a = runwayStart(r, false);
    // from the west end towards the east: yaw −90° (0 north, + left)
    assert(std::fabs(a.yaw + M_PI / 2) < 1e-6);
    assert(a.x > -500 && a.x < -400 && std::fabs(a.z) < 1e-6);
    const RunwayStart b = runwayStart(r, true);
    assert(std::fabs(b.yaw - M_PI / 2) < 1e-6 && b.x < 500 && b.x > 400);
    assert(runwayEnd(r, false) == "09");
    assert(runwayEnd(r, true) == "27");
    // without a name in the map: from its direction – north–south from the south end is 36
    assert(runwayEnd(field.runways[1], false) == "36");
    assert(runwayEnd(field.runways[1], true) == "18");
  }

  // what is drawn of an airfield faces up: the runway, its markings, the taxiway with its line, the apron
  {
    AirfieldRenderer view([](double, double) { return 200.0; }, [](double, double) { return true; });
    view.set({field});
    view.update(field.x, field.z);
    int meshes = 0;
    view.group->traverse([&](Object3D& o) {
      // (the figures at the ends are planes of their own, with their figures as a map)
      if (o.type != ObjectType::Mesh) return;
      auto& m = static_cast<Mesh&>(o);
      if (m.material->map) return;
      meshes++;
      const AttrPtr n = m.geometry->getAttribute("normal");
      for (size_t i = 0; i < n->count(); i++) assert(n->getY(i) > 0.9);
    });
    assert(meshes >= 4);
  }

  std::printf("airfield_test: ok\n");
  return 0;
}
