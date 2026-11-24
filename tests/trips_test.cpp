// LINK: game sim i18n world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The hub's trips without the hub: what Game::planTour, startTrip, updateDemo and the
 * demonstration drive's part of stepDrive (src/game/game_trips.cpp, game.ts @ v1.7.7) do with
 * the layers below them, done here on the small town of the tests – the same calls in the same
 * order, without the game around them.
 *
 *  1. a round trip as planTour plans it (a slice at a time, both directions of the road it ends
 *     on) with its line for the map – the town, the length and the seed of the original's
 *     "a round trip leads through many streets once and back to where it began";
 *  2. a tour on to another road, and one that begins in a cul-de-sac (turn round at its end
 *     first) – the town of "a tour to another street ends there …";
 *  3. the tour handed to the navigation as ways (updateDemo: nav.setWays), followed to its end
 *     and rejoined after a wrong turn – the town and the tour of "a planned tour becomes lanes
 *     …" and "whoever leaves a tour is led back onto it";
 *  4. demonstration drives with the real vehicle and the driver of the autopilot: to a place,
 *     to two places one after the other, along a tour, and out of a cul-de-sac (turned round on
 *     the spot) – tests/drive.sim.ts with DRIVE_VIA and DRIVE_HALT=1, as the game runs it.
 *
 * Origin: tests/game/tour.test.ts and tests/drive.sim.ts @ v1.7.7 (the cases, the towns and the
 * numbers); the limits of 4. are what the original's code does on these towns, with room:
 * it arrives after 110 s 2.8 m from the place (in the lane of the other direction), 115 s for
 * the two places, 281 s and 1.72 km for the tour (0.3 m from where it began), 115 s out of the
 * cul-de-sac – never more than 0.81 m from the middle of its lane, never standing.
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "game/address.h"
#include "game/nav.h"
#include "game/tour.h"
#include "game/vehicles.h"
#include "platform/native/native.h"
#include "sim/autodriver.h"
#include "sim/ground.h"
#include "sim/jsmath.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "tests/town.h"
#include "world/lanes.h"
#include "world/routegraph.h"
#include "world/world.h"

using namespace fs;

static double len(const RouteGraph& g, int w) { return static_cast<size_t>(w) < g.len.size() ? static_cast<double>(g.len[static_cast<size_t>(w)]) : 0; }

/**
 * Is each way of the list reached from the one before by a turn the junction allows – and none of them by
 * turning round (`turnFirst`: but the first, at the end of a cul-de-sac)?
 */
static bool chained(const RouteGraph& g, int start, const std::vector<int>& ways, bool turnFirst = false) {
  size_t cur = static_cast<size_t>(start);
  size_t i = 0;
  for (int w : ways) {
    bool ok = false;
    for (size_t k = g.first[cur]; k < g.first[cur + 1]; k++)
      if (g.to[k] == static_cast<uint32_t>(w) && (g.turn[k] != TURN_UTURN || (turnFirst && i == 0))) ok = true;
    if (!ok) return false;
    cur = static_cast<size_t>(w);
    i++;
  }
  return true;
}

/** What Game::planTour hands the map: the plan, its line, the way it begins behind. */
struct Planned {
  TourPlan plan;
  std::vector<float> line;
  int from = 0;
  std::vector<int> ends;
  bool canTurn = false;
  bool turned = false;
};

/**
 * Game::planTour without the game (game.ts:2271): from the lane `from`, where the car stands at (x, z), back to
 * it – or on to the road `endEdge` (-1: a round trip).
 */
static std::optional<Planned> hubPlan(const Town& town, const RouteGraph& g, const Lane& from, double x, double z, double metres, int endEdge, double seed, double budgetMs,
                                      std::vector<double>* shares = nullptr) {
  Planned out;
  const int start = wayOf(from);
  // It ends on the road it is meant to end on – whichever way that road is then driven
  const int last = endEdge >= 0 ? endEdge * 2 : start;
  for (const int w : {last, last ^ 1}) if (len(g, w) > 0) out.ends.push_back(w);
  TourSliceOptions opt;
  opt.budgetMs = budgetMs;
  opt.seed = seed;
  if (shares) opt.progress = [shares](double share) { shares->push_back(share); };
  std::optional<TourPlan> plan;
  for (const int goal : out.ends) {
    plan = plat::native::wait(planTourInSlices(g, start, goal, metres, opt));
    if (plan) break;
  }
  for (uint32_t k = g.first[static_cast<size_t>(start)]; k < g.first[static_cast<size_t>(start) + 1]; k++) if (g.to[k] == static_cast<uint32_t>(start ^ 1)) out.canTurn = true;
  if (!plan && out.canTurn) {
    // from a street with no way on (a cul-de-sac): turn round at its end first
    for (const int goal : out.ends) {
      std::optional<TourPlan> back = plat::native::wait(planTourInSlices(g, start ^ 1, goal, jsMax(0, metres - len(g, start)), opt));
      if (!back) continue;
      TourPlan turned;
      turned.ways.push_back(start ^ 1);
      turned.ways.insert(turned.ways.end(), back->ways.begin(), back->ways.end());
      turned.metres = back->metres + len(g, start);
      turned.fresh = back->fresh;
      turned.again = back->again + len(g, start);
      turned.valued = back->valued;
      plan = std::move(turned);
      out.turned = true;
      break;
    }
  }
  if (!plan || plan->ways.empty()) return std::nullopt;
  // its line on the map: from the car along the roads, each in the direction it is driven
  std::vector<double> line{x, z};
  const auto add = [&](int w, double fromS) {
    const Edge* e = town.world->edges[w >> 1];
    assert(e);
    const Polyline3& p = e->pl;
    const int n = static_cast<int>(p.cum.size());
    for (int i = 0; i < n; i++) {
      const size_t k = static_cast<size_t>((w & 1) == 0 ? i : n - 1 - i);
      // (of the road the car stands on, only what lies ahead)
      if (fromS >= 0 && ((w & 1) == 0 ? p.cum[k] < fromS : p.cum[k] > fromS)) continue;
      line.push_back(p.pts[k * 3]);
      line.push_back(p.pts[k * 3 + 2]);
    }
  };
  PolyProjection proj;
  projectOnPolyline(from.edge->pl, x, z, proj);
  add(start, proj.s);
  for (const int w : plan->ways) add(w, -1);
  out.plan = std::move(*plan);
  out.line.assign(line.begin(), line.end());
  out.from = start;
  return out;
}

/** the longest step of a line (x, z, x, z, …) */
static double longestStep(const std::vector<float>& line) {
  double longest = 0;
  for (size_t i = 2; i + 1 < line.size(); i += 2) longest = std::max(longest, jsHypot(static_cast<double>(line[i]) - line[i - 2], static_cast<double>(line[i + 1]) - line[i - 1]));
  return longest;
}

/** flat land at the height of the town's roads */
class Flat : public TerrainSource {
 public:
  double heightAt(double, double) override { return 100; }
  int coverAt(double, double) override { return 0; }
};

struct Place {
  double x = 0;
  double z = 0;
};
struct TourWays {
  std::vector<int> ways;
  int from = 0;
};
struct Drive {
  /** 'arrived' | 'lost' | 'out of time' */
  std::string why;
  double t = 0;
  double km = 0;
  /** how far from the last place the car stands, and how fast it still is */
  double away = 0;
  double speed = 0;
  /** the farthest from the middle of its lane (m), the longest standing still (s) */
  double worstOff = 0;
  double worstStill = 0;
  int turned = 0;
  int stopsPassed = 0;
  std::vector<std::string> findings;
};

/**
 * A demonstration drive as the game runs it: startTrip's driver (game.ts:2390), each step updateDemo's way to the
 * next place (game.ts:2499) and stepDrive's hands on the controls (game.ts:4149) – from `startS` metres along
 * `startLane` to the places, or along a tour.
 */
static Drive drive(const Town& town, Lane& startLane, double startS, const std::vector<Place>& stops, const TourWays* tour, double maxT) {
  const double DT = 1.0 / 120;
  World& world = *town.world;
  LaneGraph& lanes = *town.lanes;
  const RouteGraph g = routeGraphOf(lanes);
  SignalSystem signals(world.signals, world.lightsOut);
  Traffic traffic(world, lanes, signals);
  traffic.targetCount = 0;
  RuleMonitor rules(world, lanes, signals, traffic);
  const VehicleDef& DEF = VEHICLES.at("car");
  const std::optional<RuleMonitor::Caps> caps = DEF.caps ? std::optional<RuleMonitor::Caps>(RuleMonitor::Caps{DEF.caps->rural, DEF.caps->motorway}) : std::nullopt;
  rules.setVehicle(caps, DEF.capFault, DEF.heavy, DEF.noMotorway);
  Navigator nav(lanes);
  Flat terrain;
  WorldGround ground(world, terrain);
  const VehicleSpec SPEC = DEF.spec("automatic", DEF.power);
  Vehicle v(SPEC, ground);
  PolySample smp = newPolySample();
  SurfaceHit hit = newSurfaceHit();
  lanePoint(startLane, startS, smp);
  v.teleport(smp.x, 100 + 0.05, smp.z, jsAtan2(-smp.tx, -smp.tz));
  v.resetDrivetrain(true);
  AutoDriverOptions opts;
  opts.spec = v.spec;
  opts.front = DEF.outline.front;
  opts.truck = DEF.heavy;
  if (DEF.caps) opts.caps = caps;
  AutoDriver driver(world, signals, rules, &traffic, opts);
  std::unordered_set<const Edge*> allowed;
  const auto allow = [&] {
    allowed.clear();
    for (const Lane* l : *nav.routeLanes) allowed.insert(l->edge);
    rules.allowed = &allowed;
  };
  double mpos[3] = {0, 0, 0};
  size_t index = 0;
  Drive out;
  double still = 0;
  PolyProjection proj;
  const std::vector<Connector*> noRoute;
  const std::vector<TrafficEvent> noEvents;
  while (out.t < maxT && out.why.empty()) {
    v.getModelPosition(mpos);
    Lane* lane = rules.laneNow();
    bool standing = false;
    // --- updateDemo
    if (driver.cmd.turnRound) {
      // The end of a street the way leads back out of: the car is turned round on the spot.
      const Lane& to = *driver.cmd.turnRound->to;
      lanePoint(to, jsMin(3, to.len / 2), smp);
      const double y = world.surfaceAt(smp.x, smp.z, smp.y + 1.5, hit) ? hit.y : smp.y;
      // (putOnRoad)
      v.teleport(smp.x, y + 0.05, smp.z, jsAtan2(-smp.tx, -smp.tz));
      rules.placed();
      driver.reset();
      out.turned++;
      standing = true;
    } else if (lane) {
      const bool last = index + 1 >= stops.size();
      const Place& stop = stops[index];
      if (!nav.busy()) {
        nav.net = &g;
        const int here = wayOf(*lane);
        std::optional<std::vector<int>> ways;
        if (tour) {
          // the tour was planned from a road: if the car is on another one by now, to that one first
          std::optional<std::vector<int>> lead = here == tour->from ? std::optional<std::vector<int>>(std::vector<int>()) : quickest(g, here, tour->from);
          if (lead) {
            lead->insert(lead->end(), tour->ways.begin(), tour->ways.end());
            ways = std::move(lead);
          }
        } else {
          // (edgeNear, on a map in one piece)
          const int edge = world.nearestRoad(stop.x, stop.z, 60, 0, hit) && hit.edge ? hit.edge->id : -1;
          if (edge >= 0) ways = edge == lane->edge->id ? std::optional<std::vector<int>>(std::vector<int>()) : quickestToEdge(g, here, edge);
        }
        if (!ways) {
          out.why = "lost";
          break;
        }
        nav.setWays(*ways, *lane, tour != nullptr);
        allow();
      }
      // where the car that drives by itself comes to a halt: at the last place – on a tour, which may
      // pass that place before, only once the end is near
      driver.halt = last && (!tour || nav.left() <= 1) ? std::optional<AutoDriver::Halt>(AutoDriver::Halt{.x = stop.x, .z = stop.z}) : std::nullopt;
      const NavHint& h = nav.update(DT, lane, rules.laneFront(), mpos[0], mpos[2]);
      if (h.done && !last) {
        index++;
        out.stopsPassed++;
        nav.clear();
      }
    }
    // --- stepDrive
    const AutoCommand& cmd = driver.step(jsMax(DT, 1e-3), v, mpos, nav.route ? *nav.route : noRoute);
    // (until the way is known the car stands: no lane under it yet, or no route found yet)
    const bool ready = nav.busy();
    DriverInput d{.steer = ready ? cmd.steer : 0, .throttle = ready ? cmd.throttle : 0, .brake = ready ? cmd.brake : 0.5, .clutch = 0, .handbrake = 0, .gear = 1, .ignition = true, .starter = !v.engineRunning};
    if (standing) d = DriverInput{.steer = 0, .throttle = 0, .brake = 1, .clutch = 0, .handbrake = 0, .gear = 1, .ignition = true, .starter = false};
    if (cmd.arrived) {
      d.brake = 1;
      out.why = "arrived";
    }
    const int indicator = cmd.indicator;
    const double look = cmd.look;
    v.step(DT, d);
    signals.update(DT);
    v.getModelPosition(mpos);
    PlayerSnapshot snap;
    snap.x = mpos[0];
    snap.y = mpos[1];
    snap.z = mpos[2];
    snap.yaw = v.yaw;
    snap.speed = v.speed;
    snap.indicator = indicator;
    snap.hazard = false;
    snap.lookYaw = look;
    snap.seatbelt = true;
    snap.handbrake = 0;
    snap.stalled = false;
    snap.hitScenery = 0;
    snap.length = SPEC.length ? *SPEC.length : 4.28;
    snap.front = DEF.outline.front;
    rules.update(DT, snap, noEvents);
    // how far from the middle of its lane (between the junctions)
    if (const Lane* on = rules.laneNow()) {
      projectOnPolyline(on->edge->pl, mpos[0], mpos[2], proj);
      const double s = on->dir > 0 ? proj.s - on->s0 : on->s0 - proj.s;
      if (s > 0 && s < on->len) out.worstOff = std::max(out.worstOff, std::fabs(proj.off - on->off));
    }
    still = std::fabs(v.speed) < 0.1 && out.t > 3 && out.why.empty() ? still + DT : 0;
    out.worstStill = std::max(out.worstStill, still);
    out.t += DT;
  }
  if (out.why.empty()) out.why = "out of time";
  const Place& end = stops.back();
  out.km = v.odometer / 1000;
  out.away = jsHypot(mpos[0] - end.x, mpos[2] - end.z);
  out.speed = v.speed;
  for (const Violation& f : rules.log) out.findings.push_back(f.severity + ":" + f.id);
  return out;
}

static void say(const char* what, const Drive& r) {
  std::string findings;
  for (const std::string& f : r.findings) findings += " " + f;
  std::printf("  %s: %s after %.1f s and %.3f km, %.2f m from the place at %.2f m/s; %.2f m from the middle of its lane at most, standing %.2f s at most; turned round %d×, %d place(s) passed;%s\n", what,
              r.why.c_str(), r.t, r.km, r.away, r.speed, r.worstOff, r.worstStill, r.turned, r.stopsPassed, findings.empty() ? " no findings" : findings.c_str());
}

int main() {
  // ---- 1. a round trip as the hub plans it, with its line for the map
  {
    TownOptions o;
    o.cols = 6;
    o.rows = 6;
    const Town town = makeTown(o);
    const RouteGraph g = routeGraphOf(*town.lanes);
    const Lane& from = *town.lanes->edgeLanes[town.street(2, 2, 3, 2)]->fwd[0];
    PolySample at = newPolySample();
    lanePoint(from, 20, at);
    std::vector<double> shares;
    const std::optional<Planned> r = hubPlan(town, g, from, at.x, at.z, 3000, -1, 5, 150, &shares);
    assert(r);   // a tour is found
    const TourPlan& plan = r->plan;
    const int start = town.street(2, 2, 3, 2) * 2;
    assert(r->from == start);
    assert((r->ends == std::vector<int>{start, start ^ 1}));   // the road it ends on, either way
    assert(plan.ways.size() > 10);
    assert(plan.ways.back() == start);   // it ends on the street it started from
    assert(chained(g, start, plan.ways));   // no turn that is not allowed, no turning round
    assert(plan.metres <= 3000 + 1);   // not longer than asked for
    assert(plan.metres >= 2400);   // and not much shorter
    assert(std::fabs(plan.fresh + plan.again - plan.metres) < 1);
    assert(plan.fresh / plan.metres > 0.75);   // mostly along new streets
    assert(!r->turned);
    // its line: from the car, along the roads without a gap, back to the street it began on
    assert(r->line.size() >= 4 && r->line.size() % 2 == 0);
    assert(r->line[0] == static_cast<float>(at.x) && r->line[1] == static_cast<float>(at.z));
    assert(longestStep(r->line) <= 20.5);   // (the town's streets have a point every 20 m)
    const Edge& home = *town.world->edges[start >> 1];
    const double ex = r->line[r->line.size() - 2];
    const double ez = r->line[r->line.size() - 1];
    assert(jsHypot(ex - home.b->x, ez - home.b->z) < 0.01);   // (driven from a to b: it ends at b)
    // the map is told how far the planning is
    assert(!shares.empty());
    for (size_t i = 0; i < shares.size(); i++) {
      assert(shares[i] >= 0 && shares[i] <= 1);
      assert(i == 0 || shares[i] >= shares[i - 1]);
    }
    std::printf("1. a round trip: %zu ways, %.0f m (%.0f m new), a line of %zu points, %zu times told how far\n", plan.ways.size(), plan.metres, plan.fresh, r->line.size() / 2, shares.size());
  }

  // ---- 2. a tour on to another road – and one from a cul-de-sac: turn round at its end first
  {
    TownOptions o;
    o.cols = 5;
    o.rows = 4;
    o.deadEnd = 120;
    const Town town = makeTown(o);
    const RouteGraph g = routeGraphOf(*town.lanes);
    const Lane& from = *town.lanes->edgeLanes[town.street(0, 0, 1, 0)]->fwd[0];
    const int goalEdge = town.street(3, 3, 4, 3);
    PolySample at = newPolySample();
    lanePoint(from, 10, at);
    const std::optional<Planned> r = hubPlan(town, g, from, at.x, at.z, 2500, goalEdge, 2, 150);
    assert(r);
    assert((r->ends == std::vector<int>{goalEdge * 2, goalEdge * 2 + 1}));
    assert(r->plan.ways.back() >> 1 == goalEdge);   // it ends on the road it is meant to end on
    assert(chained(g, r->from, r->plan.ways));
    assert(!r->turned);
    const int culDeSac = static_cast<int>(town.world->edges.size()) - 1;
    assert(std::none_of(r->plan.ways.begin(), r->plan.ways.end(), [culDeSac](int w) { return (w >> 1) == culDeSac; }));   // the cul-de-sac is left out
    const Edge& goal = *town.world->edges[goalEdge];
    const double ex = r->line[r->line.size() - 2];
    const double ez = r->line[r->line.size() - 1];
    assert(std::min(jsHypot(ex - goal.a->x, ez - goal.a->z), jsHypot(ex - goal.b->x, ez - goal.b->z)) < 0.01);   // the line ends at an end of that road
    assert(longestStep(r->line) <= 20.5);

    // from the cul-de-sac, facing its end: no way on from there
    const LaneGraph::EdgeLanes& el = *town.lanes->edgeLanes[culDeSac];
    Lane* into = nullptr;
    for (Lane* l : el.fwd) if (l->to->arms.size() == 1) into = l;
    for (Lane* l : el.back) if (l->to->arms.size() == 1) into = l;
    assert(into);
    lanePoint(*into, 30, at);
    const std::optional<Planned> c = hubPlan(town, g, *into, at.x, at.z, 2500, -1, 2, 150);
    assert(c);
    const int start = wayOf(*into);
    assert(c->from == start && c->canTurn && c->turned);
    assert(c->plan.ways.front() == (start ^ 1));   // back out of it first
    assert(c->plan.ways.back() == start || c->plan.ways.back() == (start ^ 1));   // and back into it at the end
    assert(chained(g, start, c->plan.ways, true));
    assert(c->plan.metres <= 2500 + 1 && c->plan.metres > len(g, start));
    assert(std::fabs(c->plan.fresh + c->plan.again - c->plan.metres) < 1);
    std::printf("2. to another road: %zu ways, %.0f m; from the cul-de-sac: %zu ways, %.0f m\n", r->plan.ways.size(), r->plan.metres, c->plan.ways.size(), c->plan.metres);
  }

  // ---- 3. the tour handed to the navigation as ways: followed to its end, rejoined after a wrong turn
  {
    TownOptions o;
    o.cols = 5;
    o.rows = 5;
    const Town town = makeTown(o);
    const RouteGraph g = routeGraphOf(*town.lanes);
    Lane& first = *town.lanes->edgeLanes[town.street(1, 1, 2, 1)]->fwd[0];
    const int start = wayOf(first);
    const std::optional<TourPlan> plan = planTour(g, start, start, 2200, {.budgetMs = 150, .seed = 9});
    assert(plan && plan->ways.size() > 6);
    Navigator nav(*town.lanes);
    nav.net = &g;
    nav.setWays(plan->ways, first, true);
    assert(nav.busy());
    assert(nav.route && nav.route->size() == plan->ways.size());   // the ways are connected lane by lane
    assert(!nav.routeLanes->empty());
    const std::vector<Connector*> path = *nav.route;
    const Lane* lane = &first;
    for (size_t i = 0; i < path.size(); i++) {
      assert(wayOf(*path[i]->from) == wayOf(*lane));   // each path leaves the road the one before led onto
      assert(wayOf(*path[i]->to) == plan->ways[i]);
      lane = path[i]->to;
    }
    // drive it: the tour passes its first street again only at the very end – it is not over before
    assert(!nav.update(0.1, &first, 1).done);   // standing on the street the tour ends on, at its start: not arrived
    for (size_t i = 0; i < path.size(); i++) {
      const bool done = nav.update(0.1, path[i]->to, 1).done;
      assert(done == (i == path.size() - 1));   // arrived exactly at the end
      // (the car that drives a tour by itself is told to halt only once the end is near: nav.left() <= 1)
      assert((nav.left() <= 1) == (i + 2 >= path.size()));
    }

    // a wrong turn at the first junction
    Navigator nav2(*town.lanes);
    nav2.net = &g;
    nav2.setWays(plan->ways, first, true);
    nav2.update(0.1, &first, 1);
    Connector* wrong = nullptr;
    for (Connector* c : first.out) {
      if (c != path[0] && c->turn != TURN_UTURN) {
        wrong = c;
        break;
      }
    }
    assert(wrong);   // there is another way to go at the first junction
    for (double t = 0; t < 2; t += 0.1) nav2.update(0.1, wrong->to, 1);
    assert(nav2.route && !nav2.route->empty() && wayOf(*(*nav2.route)[0]->from) == wayOf(*wrong->to));   // a new route from where the car is now
    assert(nav2.route->size() >= 5 && path.size() >= 5);
    assert(std::equal(nav2.route->end() - 5, nav2.route->end(), path.end() - 5));   // which joins the tour again and follows it to its end
    std::printf("3. the navigation follows the tour of %zu ways and leads back onto it (%zu paths from the wrong turn)\n", plan->ways.size(), nav2.route->size());
  }

  // ---- 4. demonstration drives
  std::printf("4. demonstration drives\n");
  {
    TownOptions o;
    o.cols = 5;
    o.rows = 5;
    const Town town = makeTown(o);
    Lane& first = *town.lanes->edgeLanes[town.street(0, 0, 1, 0)]->fwd[0];
    // a place on the far side of the town, on the street (3,4)–(4,4): "drive there" (roadSpot)
    const std::optional<SpawnJson> spot = findRoadSpot(*town.world, *town.lanes, 350, 395, "", 160);
    assert(spot && std::fabs(spot->x - 350) < 0.01 && std::fabs(spot->z - 398.625) < 0.01);
    const Drive r = drive(town, first, 10, {{spot->x, spot->z}}, nullptr, 600);
    say("to a place", r);
    assert(r.why == "arrived");
    assert(r.t < 220);
    assert(r.away < 4.5 && std::fabs(r.speed) < 0.5);   // on that road beside the place (whichever way it is driven), standing
    assert(r.km > 0.6 && r.km < 0.9);
    assert(r.worstOff < 1.2);   // in its lane all the way
    assert(r.worstStill < 5);
    assert(r.turned == 0 && r.stopsPassed == 0);
    assert(r.findings.empty());   // a law-abiding drive

    // two places, one after the other
    const std::optional<SpawnJson> mid = findRoadSpot(*town.world, *town.lanes, 250, 195, "", 160);
    assert(mid);
    const Drive r2 = drive(town, first, 10, {{mid->x, mid->z}, {spot->x, spot->z}}, nullptr, 900);
    say("to two places", r2);
    assert(r2.why == "arrived");
    assert(r2.stopsPassed == 1);
    assert(r2.t < 230);
    assert(r2.away < 4.5 && std::fabs(r2.speed) < 0.5);
    assert(r2.worstOff < 1.2 && r2.worstStill < 5);
    assert(r2.findings.empty());
  }
  {
    // a tour driven by the car itself: to its end, where it began
    TownOptions o;
    o.cols = 5;
    o.rows = 5;
    const Town town = makeTown(o);
    const RouteGraph g = routeGraphOf(*town.lanes);
    Lane& first = *town.lanes->edgeLanes[town.street(1, 1, 2, 1)]->fwd[0];
    const int start = wayOf(first);
    const std::optional<TourPlan> plan = planTour(g, start, start, 2200, {.budgetMs = 150, .seed = 9});
    assert(plan);
    PolySample at = newPolySample();
    lanePoint(first, 10, at);
    const TourWays tour{plan->ways, start};
    const Drive r = drive(town, first, 10, {{at.x, at.z}}, &tour, 1500);
    say("along a tour", r);
    assert(r.why == "arrived");
    assert(r.t < 560);
    assert(r.away < 3 && std::fabs(r.speed) < 0.5);   // back where it began
    // (what is driven: the tour without the first stretch of its street and the junctions' middles)
    assert(r.km * 1000 > plan->metres * 0.75 && r.km * 1000 < plan->metres + 1);
    assert(r.worstOff < 1.2 && r.worstStill < 5);
    assert(r.turned == 0);
    assert(r.findings.empty());
  }
  {
    // a drive that begins in a cul-de-sac, facing its end: turned round on the spot there
    TownOptions o;
    o.cols = 5;
    o.rows = 4;
    o.deadEnd = 120;
    const Town town = makeTown(o);
    const int culDeSac = static_cast<int>(town.world->edges.size()) - 1;
    const LaneGraph::EdgeLanes& el = *town.lanes->edgeLanes[culDeSac];
    Lane* into = nullptr;
    for (Lane* l : el.fwd) if (l->to->arms.size() == 1) into = l;
    for (Lane* l : el.back) if (l->to->arms.size() == 1) into = l;
    assert(into);
    const std::optional<SpawnJson> spot = findRoadSpot(*town.world, *town.lanes, 150, 295, "", 160);
    assert(spot);
    const Drive r = drive(town, *into, 30, {{spot->x, spot->z}}, nullptr, 600);
    say("out of a cul-de-sac", r);
    assert(r.why == "arrived");
    assert(r.turned == 1);
    assert(r.t < 230);
    assert(r.away < 4.5 && std::fabs(r.speed) < 0.5);
    assert(r.worstOff < 1.4 && r.worstStill < 5);
    assert(r.findings.empty());
  }

  std::printf("trips_test: ok\n");
  return 0;
}
