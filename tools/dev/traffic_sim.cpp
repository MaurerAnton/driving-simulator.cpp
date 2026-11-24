// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
/**
 * Headless run of the ambient traffic on a small map in one piece – the original's
 * tests/traffic.sim.ts, which soaks the real map, brought to a test town – with every car
 * printed once a second, for comparing with the original (tools/diff/sim_traffic.ts):
 *
 *   node tools/diff/sim_traffic.ts /home/user/driving-simulator /tmp/town.json lights 300 anchor > a.txt
 *   build/traffic_sim /tmp/town.json lights 300 anchor > b.txt        (or: --make in place of the file)
 *   tools/diff/compare.py a.txt b.txt
 *
 * Reports flow, stalemates and car-to-car overlaps in its last line, as the original does.
 */
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "src/core/js.h"
#include "src/sim/jsmath.h"
#include "src/sim/signals.h"
#include "src/sim/traffic.h"
#include "src/world/lanes.h"
#include "src/world/world.h"
#include "tools/dev/sim_towns.h"

using namespace fs;

int main(int argc, char** argv) {
  const std::string file = argc > 1 ? argv[1] : "--make";
  const std::string which = argc > 2 ? argv[2] : "lights";
  const double SECONDS = argc > 3 ? jsNumber(argv[3]) : 300;
  const std::string PLAYER = argc > 4 ? argv[4] : "anchor";
  const town::TownOptions opt = simdiff::townOptions(which);
  WorldJson json;
  if (file == "--make") {
    json = town::makeTown(opt).json;
  } else {
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    if (!parseWorldJson(text.str(), json)) {
      std::fprintf(stderr, "not a world.json: %s\n", file.c_str());
      return 2;
    }
  }
  World world(json);
  LaneGraph graph(world);
  SignalSystem signals(world.signals(), nullptr, &world.signalsPresent());
  Traffic traffic(world, graph, signals);
  traffic.targetCount = 20;

  PlayerState parkedA;
  PlayerState parkedB;
  if (PLAYER == "parked") {
    // two vehicles parked for good in a lane: eastbound on the second row, southbound on the third column
    parkedA = PlayerState{.x = 1.4 * opt.block, .y = 100, .z = opt.block + 1.375, .yaw = -PI / 2, .speed = 0, .length = 4.4, .width = 1.8};
    parkedA.parked = true;
    parkedB = PlayerState{.x = 2 * opt.block - 1.375, .y = 100, .z = 1.5 * opt.block, .yaw = PI, .speed = 0, .length = 4.4, .width = 1.8};
    parkedB.parked = true;
    traffic.obstacles = {&parkedA, &parkedB};
  }

  const double cx = ((opt.cols - 1) * opt.block) / 2;
  const double cz = ((opt.rows - 1) * opt.block) / 2;
  const double dt = 1.0 / 30;
  std::string out;
  int overlaps = 0;
  double maxWait = 0;
  int removedStuck = 0;
  double speedSum = 0;
  int samples = 0;
  int nan = 0;
  int collisions = 0;
  int forced = 0;
  int contacts = 0;
  std::set<std::pair<int64_t, int64_t>> seenOverlap;
  const int steps = static_cast<int>(jsRound(SECONDS * 30));
  for (int step = 0; step < steps; step++) {
    const double t = step * dt;
    signals.update(dt);
    const size_t before = traffic.cars.size();
    // anchor: parked 40 m above the ground so no car ever brakes for it; ghost: eastwards along the second row, again and again
    PlayerState player;
    if (PLAYER == "ghost") {
      player.x = 30 + std::fmod(8 * t, (opt.cols - 1) * opt.block - 60);
      player.y = 100;
      player.z = opt.block + 1.375;
      player.yaw = -PI / 2;
      player.speed = 8;
    } else {
      player.x = cx;
      player.y = 100 + 40;
      player.z = cz;
      player.yaw = 0;
      player.speed = 0;
    }
    player.length = 4.3;
    player.width = 1.8;
    traffic.update(dt, player);
    if (traffic.cars.size() < before)
      for (const TrafficCar* c : traffic.cars)
        if (c->stuck > 44) removedStuck++;
    for (const TrafficEvent& ev : traffic.events) {
      if (ev.type == "collision") collisions++;
      else if (ev.type == "forcedBrake") forced++;
      else if (ev.type == "contact") contacts++;
    }
    for (const TrafficCar* c : traffic.cars) {
      if (!std::isfinite(c->x + c->z + c->v)) nan++;
      speedSum += c->v;
      samples++;
      maxWait = jsMax(maxWait, c->stuck);
    }
    if (step % 6 == 0) {
      const std::vector<TrafficCar*>& cars = traffic.cars;
      for (size_t i = 0; i < cars.size(); i++) {
        for (size_t j = i + 1; j < cars.size(); j++) {
          const TrafficCar& a = *cars[i];
          const TrafficCar& b = *cars[j];
          if (std::abs(a.x - b.x) > 6 || std::abs(a.z - b.z) > 6 || std::abs(a.y - b.y) > 2.5) continue;
          if (boxesOverlap(a.x, a.z, a.yaw, a.length / 2 - 0.25, a.width / 2 - 0.2, b.x, b.z, b.yaw, b.length / 2 - 0.25, b.width / 2 - 0.2)) {
            if (seenOverlap.insert({a.id, b.id}).second) overlaps++;
          }
        }
      }
    }
    if ((step + 1) % 30 != 0) continue;
    out += "t " + jsStr((step + 1) / 30.0) + " cars " + jsStr(static_cast<double>(traffic.cars.size())) + " collisions " + jsStr(collisions) + " forced " + jsStr(forced) + " contacts " + jsStr(contacts) +
           " overlaps " + jsStr(overlaps) + " signal";
    for (size_t i = 0; i < signals.controllers.size(); i++) out += ' ' + jsStr(signals.get(static_cast<int>(i), 0));
    out += '\n';
    for (const TrafficCar* cp : traffic.cars) {
      const TrafficCar& c = *cp;
      const EasedPose d = c.drawn ? *c.drawn : EasedPose{.x = c.x, .z = c.z, .yaw = c.yaw};
      out += "car";
      for (const double x : {static_cast<double>(c.id), static_cast<double>(c.lane ? c.lane->id : -1), static_cast<double>(c.conn ? c.conn->from->id : -1), static_cast<double>(c.conn ? c.conn->to->id : -1), c.s, c.v,
                             c.a, c.x, c.y, c.z, c.yaw, c.pitch, static_cast<double>(c.blinker), c.braking ? 1.0 : 0.0, c.stuck, c.crashed, c.waited, c.yieldWait, d.x, d.z, d.yaw, d.steer.value_or(0)})
        out += ' ' + jsStr(x);
      out += ' ' + (c.past ? c.past->phase : std::string("-")) + ' ' + jsStr(c.past ? c.past->off : 0);
      out += ' ' + jsStr(c.color) + ' ' + jsStr(c.style) + ' ' + jsStr(c.temper) + '\n';
    }
  }
  out += "end cars " + jsStr(static_cast<double>(traffic.cars.size())) + " meanSpeedKmh " + jsStr((speedSum / jsMax(samples, 1)) * 3.6) + " longestStandstill " + jsStr(maxWait) + " removedStuck " +
         jsStr(removedStuck) + " overlaps " + jsStr(overlaps) + " nan " + jsStr(nan) + " collisions " + jsStr(collisions) + " forced " + jsStr(forced) + " contacts " + jsStr(contacts) + '\n';
  std::fputs(out.c_str(), stdout);
  return 0;
}
