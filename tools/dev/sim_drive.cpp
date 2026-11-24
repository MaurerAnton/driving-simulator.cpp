// SPDX-License-Identifier: AGPL-3.0-only
// LINK: sim world core platform geo
/**
 * The driver of the autopilot at the wheel of the real vehicle model, among the traffic and
 * under the eyes of the rule monitor, on a test town – for comparing with the original
 * (tools/diff/sim_drive.ts; the loop is the one of the original's tests/drive.sim.ts):
 *
 *   node tools/diff/sim_drive.ts /home/user/driving-simulator /tmp/town.json lights 300 12 > a.txt
 *   build/sim_drive /tmp/town.json lights 300 12 > b.txt        (or: --make in place of the file)
 *
 * Two more arguments: the variant (tour | deadend | leftlane | reckless) and the vehicle (car | truck), as there.
 *   tools/diff/compare.py a.txt b.txt
 */
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/js.h"
#include "sim/autodriver.h"
#include "sim/ground.h"
#include "sim/jsmath.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "world/lanes.h"
#include "world/world.h"
#include "world/worldjson.h"
#include "tools/dev/sim_towns.h"

using namespace fs;

namespace {
/** flat land at the height of the town, no cover of its own */
class Flat : public TerrainSource {
 public:
  double heightAt(double, double) override { return 100; }
  int coverAt(double, double) override { return 0; }
};
}  // namespace

int main(int argc, char** argv) {
  const std::string file = argc > 1 ? argv[1] : "--make";
  const std::string which = argc > 2 ? argv[2] : "lights";
  const double SECONDS = argc > 3 ? jsNumber(argv[3]) : 300;
  const int CARS = argc > 4 ? static_cast<int>(jsNumber(argv[4])) : 12;
  const std::string VARIANT = argc > 5 ? argv[5] : "tour";
  const std::string VEHICLE = argc > 6 ? argv[6] : "car";
  // the vehicle: the compact car with the automatic gearbox, or the truck (measured from the middle of its wheelbase: its front 3.75 m ahead)
  const VehicleSpec& SPEC = VEHICLE == "truck" ? TRUCK : COMPACT_CAR_AUTOMATIC;
  const double FRONT = VEHICLE == "truck" ? 3.75 : 2.14;
  const double LENGTH = SPEC.length.value_or(4.28);
  const double WIDTH = SPEC.width.value_or(1.79);
  // (the town as tests/town.h makes it – for the numbers of its streets – and its roads as given)
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
  traffic.targetCount = CARS;
  RuleMonitor rules(world, lanes, signals, traffic);
  rules.strict = true;
  if (VEHICLE == "truck") rules.truck(true);
  Flat terrain;
  WorldGround ground(world, terrain);
  Vehicle v(SPEC, ground);
  PolySample smp = newPolySample();
  SurfaceHit hit = newSurfaceHit();

  // from the street between the junctions (0, 1) and (1, 1), eastwards – or (deadend) northwards up the last column, towards the cul-de-sac
  const std::vector<Lane*>& first = VARIANT == "deadend" ? lanes.edgeLanes[town.street(opt.cols - 1, 0, opt.cols - 1, 1)]->back : lanes.edgeLanes[town.street(0, 1, 1, 1)]->fwd;
  Lane* start = first[0];
  // (leftlane: the car stands in the lane beside the one its way begins on, where there is one – it has to get in lane)
  smp = lanePoint(VARIANT == "leftlane" && first.size() > 1 ? *first[1] : *start, 15, smp);
  v.teleport(smp.x, 100 + 0.02, smp.z, jsAtan2(-smp.tx, -smp.tz));
  v.resetDrivetrain(true);

  // the way: turns in a fixed order, no lane twice
  const int PATTERN[] = {TURN_STRAIGHT, TURN_RIGHT, TURN_LEFT, TURN_STRAIGHT, TURN_LEFT, TURN_RIGHT, TURN_STRAIGHT, TURN_STRAIGHT, TURN_LEFT, TURN_RIGHT};
  std::vector<Connector*> route;
  std::unordered_set<const Lane*> taken{start};
  {
    Lane* l = start;
    for (int k = 0; k < 80; k++) {
      const int want = PATTERN[k % 10];
      std::vector<Connector*> free;
      for (Connector* q : l->out)
        if (!taken.count(q->to)) free.push_back(q);
      Connector* c = nullptr;
      for (Connector* q : free) {
        if (q->turn == want) {
          c = q;
          break;
        }
      }
      if (!c) {
        for (Connector* q : free) {
          if (q->turn != TURN_UTURN) {
            c = q;
            break;
          }
        }
      }
      if (!c && !free.empty()) c = free[0];
      if (!c) break;
      route.push_back(c);
      taken.insert(c->to);
      l = c->to;
    }
  }
  // (it ends on a street it has not been on before: the place to halt at is looked for by the street)
  const auto beenOn = [&](size_t upTo, const Edge* e) {
    if (start->edge == e) return true;
    for (size_t k = 0; k < upTo; k++)
      if (route[k]->to->edge == e) return true;
    return false;
  };
  while (route.size() > 1 && beenOn(route.size() - 1, route.back()->to->edge)) route.pop_back();
  AutoDriverOptions opts;
  opts.spec = SPEC;
  opts.front = FRONT;
  opts.truck = VEHICLE == "truck";
  opts.obey = VARIANT != "reckless";
  AutoDriver driver(world, signals, rules, &traffic, opts);
  const Lane* last = route.back()->to;
  smp = lanePoint(*last, last->len * 0.6, smp);
  driver.halt = AutoDriver::Halt{smp.x, smp.z};

  const double DT = 1.0 / 60;
  double mpos[3] = {0, 0, 0};
  std::string out = "route";
  for (const Connector* c : route) out += ' ' + jsStr(c->from->id) + '>' + jsStr(c->to->id);
  out += '\n';
  const int steps = static_cast<int>(jsRound(SECONDS * 60));
  int crashes = 0;
  int turned = 0;
  for (int i = 0; i < steps; i++) {
    v.getModelPosition(mpos);
    Lane* lane = rules.laneNow();
    const double laneS = rules.laneFront();
    const AutoCommand& cmd = driver.step(DT, v, mpos, route);
    bool standing = false;
    if (cmd.turnRound) {
      // the end of a street the way leads back out of: as in the game, the car is turned round on the spot
      const Lane* to = cmd.turnRound->to;
      smp = lanePoint(*to, jsMin(3, to->len / 2), smp);
      const double ty2 = world.surfaceAt(smp.x, smp.z, smp.y + 1.5, hit) ? hit.y : smp.y;
      v.teleport(smp.x, ty2 + 0.05, smp.z, jsAtan2(-smp.tx, -smp.tz));
      driver.reset();
      turned++;
      rules.placed();
      standing = true;
    }
    const DriverInput input = standing ? DriverInput{.steer = 0, .throttle = 0, .brake = 1, .clutch = 0, .handbrake = 0, .gear = 1, .ignition = true, .starter = false}
                                       : DriverInput{.steer = cmd.steer, .throttle = cmd.throttle, .brake = cmd.brake, .clutch = 0, .handbrake = 0, .gear = 1, .ignition = true, .starter = false};
    v.step(DT, input);
    signals.update(DT);
    v.getModelPosition(mpos);
    // (the traffic wants the middle of the vehicle's length)
    const double mid = LENGTH / 2 - FRONT;
    PlayerState me;
    me.x = mpos[0] + jsSin(v.yaw) * mid;
    me.y = mpos[1];
    me.z = mpos[2] + jsCos(v.yaw) * mid;
    me.yaw = v.yaw;
    me.speed = v.speed;
    me.length = LENGTH;
    me.width = WIDTH;
    me.lane = lane;
    me.s = laneS - LENGTH / 2;
    traffic.update(DT, me);
    for (const TrafficEvent& e : traffic.events)
      if (e.type == "collision") crashes++;
    PlayerSnapshot snap;
    snap.x = mpos[0];
    snap.y = mpos[1];
    snap.z = mpos[2];
    snap.yaw = v.yaw;
    snap.speed = v.speed;
    snap.indicator = cmd.indicator;
    snap.hazard = false;
    snap.lookYaw = cmd.look;
    snap.seatbelt = true;
    snap.handbrake = 0;
    snap.stalled = false;
    snap.hitScenery = 0;
    snap.length = LENGTH;
    snap.front = FRONT;
    rules.update(DT, snap, traffic.events);
    if ((i + 1) % 6 != 0) continue;
    const std::optional<RuleContext::Junction>& j = rules.context.junction;
    const AutoDriver::Seen& s = driver.seen;
    const auto num = [&](double x) { out += jsStr(x) + ' '; };
    out += "step ";
    for (const double x : {static_cast<double>(i + 1), mpos[0], mpos[1], mpos[2], v.yaw, v.speed, v.rpm, static_cast<double>(v.gear), cmd.steer, cmd.throttle, cmd.brake, static_cast<double>(cmd.indicator), cmd.look}) num(x);
    out += (cmd.remark.empty() ? std::string("-") : cmd.remark) + ' ';
    for (const double x : {cmd.waiting ? 1.0 : 0.0, cmd.arrived ? 1.0 : 0.0, rules.context.limit, j ? j->dist : -1.0, static_cast<double>(j ? j->signal : -9), static_cast<double>(j ? j->role : -9),
                           rules.context.onRoad ? 1.0 : 0.0, static_cast<double>(rules.laneNow() ? rules.laneNow()->id : -1), rules.laneFront(), s.limit, s.distToLine, s.gap, s.mustStop ? 1.0 : 0.0,
                           s.crossStop ? 1.0 : 0.0, static_cast<double>(s.turn), static_cast<double>(traffic.cars.size()), static_cast<double>(rules.log.size()), static_cast<double>(crashes)})
      num(x);
    out += jsStr(turned) + '\n';
  }
  for (const Violation& f : rules.log) out += "fault " + f.id + ' ' + f.severity + ' ' + jsStr(f.time) + ' ' + jsStr(f.x) + ' ' + jsStr(f.z) + '\n';
  out += "end faults " + jsStr(static_cast<double>(rules.log.size())) + " points " + jsStr(rules.points()) + " crashes " + jsStr(crashes) + " turned " + jsStr(turned) + " arrived " + (driver.cmd.arrived ? "1" : "0") + '\n';
  std::fputs(out.c_str(), stdout);
  return 0;
}
