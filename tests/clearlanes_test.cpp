// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim game world core platform geo
// Origin: tests/game/clearlanes.test.ts @ v1.7.7
/**
 * Nothing of the scenery may stand in a lane (src/sim/clearlanes) – and a place is known to
 * be taken when the vehicle would touch something there (src/sim/collision).
 */
#include <cmath>
#include <string>
#include <vector>

#include "sim/clearlanes.h"
#include "sim/collision.h"
#include "tests/town.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using fs::harness::ok;
using fs::harness::test;

int main() {
  std::printf("clearlanes_test (tests/game/clearlanes.test.ts)\n");
  Town town = makeTown();
  LaneGraph& lanes = *town.lanes;
  /** a point in the middle of a lane of the street between two junctions, halfway along */
  const Lane* lane = lanes.edgeLanes[town.street(1, 1, 2, 1)]->fwd[0];
  PolySample mid = newPolySample();
  lanePoint(*lane, lane->len / 2, mid);
  PathFoot foot;
  {
    test("the middle of the nearest lane is found, on the level asked for");
    ok(lanes.nearestPath(mid.x, mid.z, mid.y, 2, foot) < 0.01, "in the middle of the lane");
    ok(std::abs(lanes.nearestPath(mid.x, mid.z + 1, mid.y, 2, foot) - 1) < 0.02, "a metre beside it");
    ok(jsHypot(foot.x - mid.x, foot.z - mid.z) < 0.02, "the foot point lies on the lane");
    ok(lanes.nearestPath(mid.x, mid.z + 8, mid.y, 2, foot) == JS_INF, "nothing within reach beside the road");
    ok(lanes.nearestPath(mid.x, mid.z, mid.y + 6, 2, foot) == JS_INF, "a road on another level does not count");
    // in the middle of a junction: the paths through it
    const Node& node = *town.world->nodes[town.node(1, 1)];
    ok(lanes.nearestPath(node.x, node.z, node.y, 3, foot) < 3, "in the middle of a junction: the paths through it");
  }
  {
    test("a sign post in a lane is moved beside the lanes, one beside the road stays");
    std::vector<SignPost> signs(2);
    signs[0].x = mid.x;
    signs[0].y = mid.y;
    signs[0].z = mid.z + 0.3;
    signs[0].s = {"205"};
    signs[0].h = 2;
    signs[1].x = mid.x + 10;
    signs[1].y = mid.y;
    signs[1].z = mid.z + 6;
    signs[1].s = {"306"};
    signs[1].h = 2;
    const std::pair<int, int> r = clearSigns(lanes, signs);
    ok(r.first == 1 && r.second == 0, "one moved, none dropped");
    ok(signs.size() == 2, "both still there");
    ok(lanes.nearestPath(signs[0].x, signs[0].z, signs[0].y, POST_CLEAR, foot) >= POST_CLEAR, "clear of every lane now");
    ok(jsHypot(signs[0].x - mid.x, signs[0].z - mid.z) < 5, "and not far from where it stood");
    ok(signs[1].x == mid.x + 10 && signs[1].z == mid.z + 6, "the one beside the road stays");
    ok(postInLane(lanes, mid.x, mid.y, mid.z) == true, "a post in the lane");
    ok(postInLane(lanes, mid.x, mid.y, mid.z + 6) == false, "a post beside the road");
  }
  {
    test("lamps in a lane are dropped, other things are left alone");
    std::vector<TileObject> objects(3);
    objects[0].k = "lamp";
    objects[0].x = mid.x;
    objects[0].y = mid.y;
    objects[0].z = mid.z;
    objects[1].k = "lamp";
    objects[1].x = mid.x;
    objects[1].y = mid.y;
    objects[1].z = mid.z + 7;
    objects[2].k = "bench";
    objects[2].x = mid.x;
    objects[2].y = mid.y;
    objects[2].z = mid.z;
    ok(clearObjects(lanes, objects) == 1, "one lamp dropped");
    ok(objects.size() == 2 && objects[0].k == "lamp" && objects[1].k == "bench", "the lamp beside the road and the bench stay");
  }
  {
    test("walls lose the pieces that reach into a lane – and only those");
    // across the road, along it at the kerb, along it well beside it, and across it on a bridge high above
    ok(lanes.wallInLane(mid.x, mid.z - 6, mid.x, mid.z + 6, mid.y, WALL_CLEAR) == true, "across the road");
    ok(lanes.wallInLane(mid.x - 10, mid.z + 0.9, mid.x + 10, mid.z + 0.9, mid.y, WALL_CLEAR) == true, "along it at the kerb");
    ok(lanes.wallInLane(mid.x - 10, mid.z + 5, mid.x + 10, mid.z + 5, mid.y, WALL_CLEAR) == false, "along it well beside it");
    ok(lanes.wallInLane(mid.x, mid.z - 6, mid.x, mid.z + 6, mid.y + 7, WALL_CLEAR) == false, "across it on a bridge high above");
    // a house whose corner stands in the lane: the two walls of that corner go, the others stay
    const auto f = [](double x) { return static_cast<float>(x); };
    const std::vector<float> ring = {f(mid.x - 4), f(mid.z + 0.5), f(mid.x + 4), f(mid.z + 0.5), f(mid.x + 4), f(mid.z + 9), f(mid.x - 4), f(mid.z + 9)};
    const std::vector<uint16_t> counts = {4};
    const std::vector<float> segments = {f(mid.x - 3), f(mid.z - 9), f(mid.x + 3), f(mid.z - 9), f(mid.y)};
    const std::vector<float> walls = clearWalls(lanes, &ring, &counts, &segments);
    ok(walls.size() / 5 == 2, "the far wall of the house and the free-standing wall beside the road (" + std::to_string(walls.size() / 5) + ")");
    for (size_t i = 0; i < walls.size(); i += 5) ok(lanes.wallInLane(walls[i], walls[i + 1], walls[i + 2], walls[i + 3], walls[i + 4], WALL_CLEAR) == false, "what is left reaches into no lane");
    const std::optional<std::vector<uint8_t>> bits = ringSidesInLanes(lanes, ring, counts);
    ok(bits.has_value() && bits->size() == 1 && (*bits)[0] == 0b1011, "the sides of the footprint that stand in a lane: all but the far one");
  }
  {
    test("a place is known to be taken when the vehicle would touch something there");
    SceneryCollider c;
    const std::vector<float> wall = {50, -5, 50, 5, 100};
    CollisionSource src;
    src.key = "t";
    src.segments = &wall;
    src.discs = {0, 100, -20, 0.1};
    c.add(src);
    // heading north (yaw 0: forward is -z): a post 20 m ahead, a wall 50 m to the east
    ok(c.blocked(0, 100, 0, 0) == false, "free at the start");
    ok(c.blocked(0, 100, -18.2, 0) == true, "the nose at the post");
    ok(c.blocked(1.2, 100, -20, 0) == false, "past it with a hand of room");
    ok(c.blocked(49.5, 100, 0, 0) == true, "in the wall");
    ok(c.blocked(49.5, 104, 0, 0) == false, "the wall stands on another level");
    c.remove("t");
    ok(c.blocked(49.5, 100, 0, 0) == false, "the tile gone, the place is free");
  }
  std::printf("clearlanes_test: all 5 cases passed\n");
  return 0;
}
