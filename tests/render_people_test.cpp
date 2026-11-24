// SPDX-License-Identifier: AGPL-3.0-only
// LINK: render engine sim world game core platform geo
// Origin: tests/sim/pedestrians.test.ts @ v1.7.7 (the case of the looks)
/**
 * Who they are follows from how they look: render/people decides the look, sim/pedestrians reads the
 * age from the look number – the two are held together here.  And the renderer shows a crowd.
 */
#include <cstdio>
#include <string>
#include <vector>

#include "render/people.h"
#include "sim/pedestrians.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

int main() {
  std::printf("render_people_test (tests/sim/pedestrians.test.ts: the looks)\n");
  test("who they are follows from how they look: the old walk slower, children quicker (render/people decides the look)");
  for (int id = 0; id < 3000; id += 7) {
    const Look L = lookOf(id);
    const int age = ageOfLook(id);
    ok((age == AGE::CHILD) == (L.scale < 0.89), "child or not: look " + std::to_string(id));
    ok((age == AGE::OLD) == (L.stoop >= 0.1), "old or not: look " + std::to_string(id));
  }
  test("the renderer poses a crowd: six parts, as many instances as people (and what they carry)");
  PeopleRenderer r(8);
  std::vector<PersonPose> people;
  for (int i = 0; i < 5; i++) {
    PersonPose p;
    p.x = i * 2.0;
    p.z = -i;
    p.walk = i % 2 == 0 ? i * 3.1 : -1;
    p.look = i * 101;
    p.umbrella = i == 3;
    people.push_back(p);
  }
  r.update(people, 1.5);
  ok(r.group->children.size() == 6, "six parts");
  int shown = 0;
  for (const auto& c : r.group->children) shown += c->visible ? 1 : 0;
  ok(shown >= 5, "the body parts are shown");
  r.update({}, 2);
  for (const auto& c : r.group->children) ok(!c->visible, "nobody: nothing shown");
  std::printf("ok\n");
  return 0;
}
