// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
/**
 * The rule monitor watching a car that does most things wrong – for comparing with the original
 * (tools/diff/sim_rules.ts, which says what the car does):
 *
 *   node tools/diff/sim_rules.ts /home/user/driving-simulator /tmp/town.json lights > a.txt
 *   build/sim_rules /tmp/town.json lights > b.txt        (or: --make in place of the file)
 *   tools/diff/compare.py a.txt b.txt --atol 1e-9
 */
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "core/js.h"
#include "sim/jsmath.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/lanes.h"
#include "world/world.h"
#include "world/worldjson.h"
#include "tools/dev/sim_towns.h"

using namespace fs;

namespace {
/** what is done wrong on the k-th street of the way: speed (m/s), metres left of the middle of the lane, the indicator, the handbrake, the belt */
struct Plan {
  double speed;
  double off;
  int indicator;
  double handbrake;
  bool belt;
};
const Plan PLAN[] = {
    {21, 0, 1, 0, true}, {8, 2.75, 1, 0, true}, {8, -2.4, 1, 0, true}, {8, 0, 0, 0, true}, {8, 0, -1, 0, true}, {8, 0, 1, 1, true}, {12, 0, 1, 0, true}, {8, 0, 1, 0, false},
};
}  // namespace

int main(int argc, char** argv) {
  const std::string file = argc > 1 ? argv[1] : "--make";
  const std::string which = argc > 2 ? argv[2] : "lights";
  const TownOptions opt = simdiff::townOptions(which);
  Town town = makeTown(opt);
  WorldJson json;
  if (file == "--make") {
    json = town.json;
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
  LaneGraph lanes(world);
  SignalSystem signals(world.signals);
  Traffic traffic(world, lanes, signals);
  traffic.targetCount = 0;
  RuleMonitor rules(world, lanes, signals, traffic);
  PolySample smp = newPolySample();

  // the way: turns in a fixed order, no lane twice (as tools/dev/sim_drive.cpp)
  Lane* start = lanes.edgeLanes[town.street(0, 1, 1, 1)]->fwd[0];
  const int PATTERN[] = {TURN_STRAIGHT, TURN_RIGHT, TURN_LEFT, TURN_STRAIGHT, TURN_LEFT, TURN_RIGHT, TURN_STRAIGHT, TURN_STRAIGHT, TURN_LEFT, TURN_RIGHT};
  std::vector<Connector*> route;
  std::unordered_set<const Lane*> taken{start};
  {
    Lane* l = start;
    for (int k = 0; k < 16; k++) {
      const int want = PATTERN[k % 10];
      std::vector<Connector*> free;
      for (Connector* q : l->out)
        if (!taken.count(q->to)) free.push_back(q);
      Connector* c = nullptr;
      for (Connector* q : free)
        if (!c && q->turn == want) c = q;
      for (Connector* q : free)
        if (!c && q->turn != TURN_UTURN) c = q;
      if (!c && !free.empty()) c = free[0];
      if (!c) break;
      route.push_back(c);
      taken.insert(c->to);
      l = c->to;
    }
  }

  const double DT = 1.0 / 30;
  std::string out = "route";
  for (const Connector* c : route) out += ' ' + jsStr(c->from->id) + '>' + jsStr(c->to->id);
  out += '\n';
  int step = 0;
  const auto see = [&](double x, double y, double z, double yaw, double speed, int indicator, double look, double handbrake, bool belt) {
    signals.update(DT);
    const PlayerState high{.x = x, .y = y + 40, .z = z, .yaw = yaw, .speed = 0, .length = 4.3, .width = 1.8};
    traffic.update(DT, high);
    PlayerSnapshot snap;
    snap.x = x;
    snap.y = y;
    snap.z = z;
    snap.yaw = yaw;
    snap.speed = speed;
    snap.indicator = indicator;
    snap.hazard = false;
    snap.lookYaw = look;
    snap.seatbelt = belt;
    snap.handbrake = handbrake;
    snap.stalled = false;
    snap.hitScenery = 0;
    snap.length = 4.3;
    rules.update(DT, snap, traffic.events);
    step++;
    if (step % 10 != 0) return;
    const RuleContext& c = rules.context;
    const std::optional<RuleContext::Junction>& j = c.junction;
    out += "step ";
    for (const double v : {static_cast<double>(step), x, z, speed, c.limit, static_cast<double>(c.zone), c.urban ? 1.0 : 0.0, c.onRoad ? 1.0 : 0.0, c.wrongWay ? 1.0 : 0.0, j ? j->dist : -1.0,
                           static_cast<double>(j ? j->role : -9), static_cast<double>(j ? j->ctrl : -9), static_cast<double>(j ? j->signal : -9), static_cast<double>(j ? j->arrow : -9), j && j->dark ? 1.0 : 0.0,
                           static_cast<double>(rules.laneNow() ? rules.laneNow()->id : -1), rules.laneFront(), static_cast<double>(rules.log.size()), static_cast<double>(rules.points())})
      out += jsStr(v) + ' ';
    std::string name = c.roadName.empty() ? "-" : c.roadName;
    const size_t at = name.find("Street ");
    if (at != std::string::npos) name.replace(at, 7, "Street_");
    out += name + '\n';
  };
  // street by street: its lane, then the path through the junction at its end
  {
    Lane* lane = start;
    for (size_t k = 0; k <= route.size(); k++) {
      const Plan& plan = PLAN[k % 8];
      Connector* conn = k < route.size() ? route[k] : nullptr;
      // the turn at the end of this street: indicated as the plan says (1: as it should be, 0: not, −1: to the wrong side)
      const int turn = conn ? (conn->turn == TURN_LEFT ? -1 : conn->turn == TURN_RIGHT ? 1 : 0) : 0;
      const int shown = turn * plan.indicator;
      for (double d = 2; d < lane->len; d += plan.speed * DT) {
        smp = lanePoint(*lane, d, smp);
        const double off = d > 15 && d < lane->len - 25 ? plan.off : 0;
        const bool near = lane->len - d < 40;
        see(smp.x + smp.tz * off, smp.y, smp.z - smp.tx * off, jsAtan2(-smp.tx, -smp.tz), plan.speed, near ? shown : 0, near && lane->len - d > 8 && lane->len - d < 20 ? (shown < 0 ? 1.3 : shown > 0 ? -1.3 : 0) : 0,
            plan.handbrake, plan.belt);
      }
      if (!conn) break;
      for (double d = 0; d < conn->len; d += plan.speed * DT) {
        smp = samplePolyline(conn->pl, d, smp);
        see(smp.x, smp.y, smp.z, jsAtan2(-smp.tx, -smp.tz), plan.speed, shown, 0, plan.handbrake, plan.belt);
      }
      lane = conn->to;
    }
  }
  // a one-way street, driven up against its direction: westwards along the street from (0, 2) to (1, 2)
  if (!opt.oneWay.empty()) {
    for (double x = opt.block - 10; x > 10; x -= 8 * DT) see(x, 100, 2 * opt.block, PI / 2, 8, 0, 0, 0, true);
  }
  for (const Violation& f : rules.log) {
    out += "fault " + f.id + ' ' + f.severity + ' ' + jsStr(f.time) + ' ' + jsStr(f.x) + ' ' + jsStr(f.z);
    for (const auto& [k, v] : f.data) out += ' ' + k + '=' + (std::holds_alternative<double>(v) ? jsStr(std::get<double>(v)) : std::get<std::string>(v));
    out += '\n';
  }
  out += "end faults " + jsStr(static_cast<double>(rules.log.size())) + " points " + jsStr(rules.points()) + " failed " + (rules.failed() ? "1" : "0") + '\n';
  std::fputs(out.c_str(), stdout);
  return 0;
}
