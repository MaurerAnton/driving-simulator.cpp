// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7
/**
 * The Game's drives to places, tours and demonstrations (the autopilot), and what they
 * stand on: the roads of the whole map, the road network for planning ways, a place on a
 * road near a point.
 *
 * The methods are the original's, by its names and in its order:
 * wantNet, waysToMotorway, usableLane, routeGraphNow, roadSpot, edgeNear (game.ts:1092),
 * laneAt, planTour, startTrip, startPreset, endDemo, updateDemo (game.ts:2245),
 * takeOverCar (game.ts:3291), autopilotDriving (game.ts:3496), tourWishes (game.ts:6221).
 * (apPhaseKey, what the aeroplane's autopilot is doing – game.ts:216 –, is in game_plane.cpp)
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "audio/audio.h"
#include "core/date.h"
#include "core/geom.h"
#include "core/js.h"
#include "core/task.h"
#include "game/address.h"
#include "game/game.h"
#include "game/nav.h"
#include "game/routepresets.h"
#include "game/takeover.h"
#include "game/tour.h"
#include "game/tourlog.h"
#include "game/vehicles.h"
#include "i18n/index.h"
#include "input/input.h"
#include "platform/plat.h"
#include "render/far.h"
#include "sim/autodriver.h"
#include "sim/autopilot.h"
#include "sim/flight.h"
#include "sim/jsmath.h"
#include "sim/rules.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "ui/bigmap.h"
#include "ui/hud.h"
#include "world/far.h"
#include "world/lanes.h"
#include "world/netfile.h"
#include "world/netindex.h"
#include "world/routegraph.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

namespace {

/** `new Date().toISOString()` */
std::string isoNow() {
  const DateFields f = utcFields(plat::dateMs());
  char buf[40];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", f.year, f.month + 1, f.date, f.hours, f.minutes, f.seconds, f.milliseconds);
  return buf;
}

/** `{ name, x, z, yaw, kind: 'street' }`: a place on a street (`rough`: taken from the overview of the whole map) */
SpawnJson streetSpot(std::string name, double x, double z, double yaw, bool rough = false) {
  SpawnJson s;
  s.name = std::move(name);
  s.x = x;
  s.z = z;
  s.yaw = yaw;
  s.kind = "street";
  s.rough = rough;
  return s;
}

/**
 * `rules.allowed = new Set(nav.routeLanes.map((l) => l.edge))`: the roads the navigation sends the car through.
 * The rule monitor looks at the set through a pointer (sim/rules.h), so the set itself is kept here – one, as
 * the game has one drive at a time; made anew, in place, whenever the route is another one.
 */
std::unordered_set<const Edge*> allowedRoads;

const std::unordered_set<const Edge*>* allowedAlong(const std::vector<Lane*>& routeLanes) {
  allowedRoads.clear();
  for (const Lane* l : routeLanes) allowedRoads.insert(l->edge);
  return &allowedRoads;
}

/** `Math.max(0, Math.min(len, …))` and the like are the original's; `[...el.fwd, ...el.back]` is gone through as the two lists in turn */
template <class Visit>
void eachLane(const LaneGraph::EdgeLanes& el, Visit&& visit) {
  for (Lane* l : el.fwd) visit(*l);
  for (Lane* l : el.back) visit(*l);
}

}  // namespace

/**
 * Read the roads of the whole map (once): for ways to places far away, for
 * tours and for the large map.  Until they are there, only the roads around
 * the car are known.
 */
void Game::wantNet() {
  if (netAsked || netGaveUp || !app.map->pieces || plat::nowMs() < netRetryAt) return;
  netAsked = true;
  [](Game* self) -> Task<void> {
    std::string why;
    try {
      std::shared_ptr<const NetData> net = co_await self->app.map->net();
      std::shared_ptr<NetIndex> index = net ? co_await NetIndex::build(net) : nullptr;
      if (index) {
        // (the index is the game's alone from here on: what `build` made moves into the game's own)
        self->netIndex = std::make_unique<NetIndex>(std::move(*index));
        self->nav->net = &self->netIndex->graph;
        co_return;
      }
      why = "no network in the map";
    } catch (const std::exception& err) {
      why = err.what();
    }
    // 2.0: asked again after a pause (10, 20, 30 s), read anew each time; after four tries the game gives up on it –
    // what waits for it ends then (the lesson's way, the demonstration drive) instead of waiting for ever
    self->app.map->forgetNet();
    self->netAsked = false;
    self->netTries++;
    self->netRetryAt = plat::nowMs() + 10000.0 * self->netTries;
    if (self->netTries >= 4) self->netGaveUp = true;
    plat::warn("the road network of the whole map could not be read (" + why + ")" + (self->netGaveUp ? ": given up" : ": asked again later"));
  }(this).detach();
}

/** The way to the nearest stretch of motorway that can be reached, through the roads of the whole map (for the motorway lesson far from one). */
std::optional<std::vector<int>> Game::waysToMotorway(const RouteGraph& g, Lane& lane, double x, double z) {
  const NetData* net = netIndex ? netIndex->net.get() : nullptr;
  if (!net) return std::nullopt;
  std::vector<std::pair<int, double>> near;
  for (size_t e = 0; e < net->edgeCls.size(); e++) {
    if (net->edgeCls[e] != MOTORWAY || net->edgeLen[e] < 250) continue;
    const double d = jsHypot(static_cast<double>(net->nodeX[net->edgeA[e]]) - x, static_cast<double>(net->nodeZ[net->edgeA[e]]) - z);
    // (a lesson, not a journey)
    if (d < 60000) near.emplace_back(static_cast<int>(e), d);
  }
  std::stable_sort(near.begin(), near.end(), [](const std::pair<int, double>& a, const std::pair<int, double>& b) { return a.second < b.second; });
  for (size_t i = 0; i < near.size() && i < 12; i++) {
    const int e = near[i].first;
    std::optional<std::vector<int>> ways = quickestToEdge(g, wayOf(lane), e);
    if (ways && !ways->empty()) return ways;
  }
  return std::nullopt;
}

/** May the car be put on this lane, or a way be planned from it: is it part of the network one can drive around in? */
std::function<bool(const Lane& l)> Game::usableLane() {
  if (app.map->pieces) {
    const NetIndex* index = netIndex.get();
    if (!index) return nullptr;
    return [index](const Lane& l) { return index->main[static_cast<size_t>(wayOf(l))] == 1; };
  }
  if (!mainSet) mainSet = mainNetwork(*app.world, *lanes);
  const std::unordered_set<int>* set = &*mainSet;
  return [set](const Lane& l) { return set->count(l.id) > 0; };
}

/** The road network for planning ways: that of the whole map if it comes in pieces (null until it is read). */
RouteGraph* Game::routeGraphNow() {
  if (app.map->pieces) return netIndex ? &netIndex->graph : nullptr;
  if (!routeGraph) routeGraph = routeGraphOf(*lanes);
  return &*routeGraph;
}

/**
 * A place on a road near (x, z) – to put the car at, or to drive to.
 * Where the roads are loaded it lies in a lane; elsewhere it is taken from
 * the overview of the whole map (`rough`) and put right when the car gets there.
 */
std::optional<SpawnJson> Game::roadSpot(double x, double z, const std::string& street, double maxDist) {
  World& world = *app.world;
  if (app.map->ready(x, z, maxDist + 60)) return findRoadSpot(world, *lanes, x, z, street, maxDist, usableLane());
  const NetIndex* index = netIndex.get();
  if (!index) return std::nullopt;
  const std::optional<NetSpot> hit = index->nearest(x, z, maxDist, [index](int e) { return index->main[static_cast<size_t>(e) * 2] == 1 || index->main[static_cast<size_t>(e) * 2 + 1] == 1; });
  if (!hit) return std::nullopt;
  // (along the road, the way it may be driven)
  return streetSpot("", hit->x, hit->z, index->main[static_cast<size_t>(hit->edge) * 2] ? hit->yaw : hit->yaw + PI, true);
}

/** The road (its number in the map) nearest to a place, or -1. */
int Game::edgeNear(double x, double z) {
  // (from the roads of the whole map if they are known – and of these a road of the network one can
  // drive around in, if there is one: beside it may lie a yard or a path no way leads to)
  const NetIndex* index = netIndex.get();
  if (index) {
    std::optional<NetSpot> hit = index->nearest(x, z, 150, [index](int e) { return index->main[static_cast<size_t>(e) * 2] == 1 || index->main[static_cast<size_t>(e) * 2 + 1] == 1; });
    if (!hit) hit = index->nearest(x, z, 150);
    if (hit) return hit->edge;
  }
  if (app.map->ready(x, z, 120)) return app.world->nearestRoad(x, z, 60, 0, this->hit) && this->hit.edge ? this->hit.edge->id : -1;
  return -1;
}

// --------------------------------------- drives to places, tours, demonstrations
/** The lane nearest to a place that goes the way `yaw` points. */
Lane* Game::laneAt(double x, double z, double yaw) {
  PolyProjection& proj = this->proj;
  PolySample& smp = this->smp;
  const double fx = -jsSin(yaw);
  const double fz = -jsCos(yaw);
  Lane* lane = nullptr;
  double best = 40;
  app.world->edgesInBox(x - 20, z - 20, x + 20, z + 20, [&](Edge& e) {
    const LaneGraph::EdgeLanes* el = lanes->edgeLanes[e.id];
    projectOnPolyline(e.pl, x, z, proj);
    eachLane(*el, [&](Lane& l) {
      lanePoint(l, jsMax(0, jsMin(l.len, l.dir > 0 ? proj.s - l.s0 : l.s0 - proj.s)), smp);
      const double score = jsHypot(smp.x - x, smp.z - z) + (1 - (smp.tx * fx + smp.tz * fz)) * 6;
      if (score < best) {
        best = score;
        lane = &l;
      }
    });
  });
  return lane;
}

/**
 * A tour of about `metres` through as many different streets as possible:
 * from where the car stands back to there, or on to `end`.
 */
Task<std::optional<TourPreview>> Game::planTour(double metres, std::optional<SpawnJson> end, std::function<void(double share)> progress) {
  wantNet();
  vehicle->getModelPosition(mpos.data());
  Lane* from = currentLane();
  if (!from) from = laneAt(mpos[0], mpos[2], vehicle->yaw);
  if (!from) co_return std::nullopt;
  const RouteGraph* gNow = routeGraphNow();
  if (!gNow) co_return std::nullopt;
  const RouteGraph& g = *gNow;
  const int start = wayOf(*from);
  // It ends on the road it is meant to end on – whichever way that road is then driven: back
  // into a car park with one way in and out, the tour arrives facing the other way.
  const int endEdge = end ? edgeNear(end->x, end->z) : from->edge->id;
  if (endEdge < 0) co_return std::nullopt;
  const int last = end ? endEdge * 2 : start;
  std::vector<int> ends;
  // (a way the network does not have has no length: `g.len[w] > 0` is false for it)
  for (const int w : {last, last ^ 1}) if (static_cast<size_t>(w) < g.len.size() && g.len[static_cast<size_t>(w)] > 0) ends.push_back(w);
  const double seed = tourSeed++;
  std::optional<TourPlan> plan;
  // what the driver wishes: roads to leave out, the kind of street to look for
  const TourWishes wish = tourWishes(g);
  TourSliceOptions opt;
  opt.budgetMs = 500;
  opt.seed = seed;
  opt.progress = progress;
  opt.avoid = wish.avoid;
  opt.like = wish.like;
  const double startLen = static_cast<double>(g.len[static_cast<size_t>(start)]);
  // (a slice at a time: a long tour takes a while, and the map shows how far the planning is)
  for (const int goal : ends) {
    plan = co_await planTourInSlices(g, start, goal, metres, opt);
    if (plan) break;
  }
  bool canTurn = false;
  for (uint32_t k = g.first[static_cast<size_t>(start)]; k < g.first[static_cast<size_t>(start) + 1]; k++) if (g.to[k] == static_cast<uint32_t>(start ^ 1)) canTurn = true;
  if (!plan && canTurn) {
    // from a street with no way on (a cul-de-sac): turn round at its end first
    for (const int goal : ends) {
      std::optional<TourPlan> back = co_await planTourInSlices(g, start ^ 1, goal, jsMax(0, metres - startLen), opt);
      if (!back) continue;
      TourPlan turned;
      turned.ways.reserve(back->ways.size() + 1);
      turned.ways.push_back(start ^ 1);
      turned.ways.insert(turned.ways.end(), back->ways.begin(), back->ways.end());
      turned.metres = back->metres + startLen;
      turned.fresh = back->fresh;
      turned.again = back->again + startLen;
      turned.valued = back->valued;
      plan = std::move(turned);
      break;
    }
  }
  if (!plan || plan->ways.empty()) co_return std::nullopt;
  // its line on the map: from the car along the roads, each in the direction it is driven
  std::vector<double> line{mpos[0], mpos[2]};
  const Sparse<Edge>& edges = app.world->edges;
  const NetData* net = netIndex ? netIndex->net.get() : nullptr;
  const auto add = [&](int w, double fromS) {
    const Edge* e = edges[w >> 1];
    if (!e) {
      // (a road that is not loaded: its course from the overview of the map)
      const std::vector<double> c = net ? netPolyline(*net, w >> 1) : std::vector<double>();
      if ((w & 1) == 0) {
        for (size_t i = 0; i + 1 < c.size(); i += 2) {
          line.push_back(c[i]);
          line.push_back(c[i + 1]);
        }
      } else {
        for (ptrdiff_t i = static_cast<ptrdiff_t>(c.size()) - 2; i >= 0; i -= 2) {
          line.push_back(c[static_cast<size_t>(i)]);
          line.push_back(c[static_cast<size_t>(i) + 1]);
        }
      }
      return;
    }
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
  // (the road the car stands on, looked up anew by its number: the planning took a while, and what the
  // original keeps alive by holding `from` may have been dropped and loaded again here meanwhile)
  if (const Edge* fromEdge = edges[start >> 1]) projectOnPolyline(fromEdge->pl, mpos[0], mpos[2], proj);
  add(start, proj.s);
  for (const int w : plan->ways) add(w, -1);
  TourPreview out;
  out.plan = std::move(*plan);
  // (`Float32Array.from(line)`)
  out.line.assign(line.begin(), line.end());
  out.from = start;
  out.end = end ? *end : streetSpot("", mpos[0], mpos[2], vehicle->yaw);
  co_return out;
}

/**
 * Drive to these places, one after the other, from where the car stands –
 * or along a tour.  `self`: the player drives and is shown the way;
 * otherwise it is a demonstration drive: the car drives by itself.
 */
void Game::startTrip(const std::vector<SpawnJson>& stops_, bool self, const TourPreview* tour_) {
  if (lesson || (stops_.empty() && !tour_) || (state != "map" && state != "drive" && state != "pause")) return;
  // (what was handed in may be the map's own, which closing the map clears: held here for as long as it is read)
  const std::vector<SpawnJson> stops = stops_;
  const std::optional<TourPreview> tour = tour_ ? std::optional<TourPreview>(*tour_) : std::nullopt;
  // (later: endWatch – game.ts:2347)
  routeWatch = nullptr;
  // (on foot beside the vehicle: back into the seat first; a tram is driven by hand only)
  if (walker) leaveWalk();
  if (aircraft) {
    Aircraft& plane = *aircraft;
    // the aeroplane by itself: to the (first) place chosen, at least 300 m above the ground there, circling it –
    // or down onto the runway of an airfield if the place is one
    if (!self && !stops.empty()) {
      const SpawnJson& to = stops[0];
      const std::optional<LandingRunway> field = air ? landingRunway(air->airfields, to.x, to.z) : std::nullopt;
      const double alt = std::max(plane.altitude(), groundHeight(to.x, to.z) + 300);
      const std::optional<RunwayAt> rw = plane.onGround() && air ? runwayAt(air->airfields, plane.pos.x, plane.pos.z, plane.yaw()) : std::nullopt;
      flightGoto.reset();
      if (field && field->distance < 1500 && !plane.onGround()) flightAuto->land(*field->runway, field->fromSecond);
      else if (rw) {
        // on a runway: take off first, then there (stepFlight hands over once it climbs)
        flightAuto->takeoff(*rw->runway, rw->fromSecond);
        flightGoto = FlightGoto{to.x, to.z, alt};
      } else flightAuto->goto_(to.x, to.z, alt);
      flightAuto->takeOver({flight.throttle, flight.trim});
      flightTake = 0;
      hud->message(tr("apOn", settings.lang));
    }
    if (state == "map") closeMap();
    else if (state == "pause") resume();
    return;
  }
  if (tram) {
    // the tram follows its tracks: "by itself" is its demonstration drive (the places chosen do not matter to it)
    if (!self) {
      tramAuto = true;
      tramTake = 0;
      tramHeld = false;
      if (state == "map") closeMap();
      hud->message(tr("tramAutoOn", settings.lang));
    }
    return;
  }
  if (state == "map") {
    mapFrom = "drive";
    closeMap();
  } else if (state == "pause") resume();
  std::unique_ptr<AutoDriver> driver;
  if (!self) {
    // the driver has no hand for a gear lever: the car gets the automatic gearbox for it
    toAutomatic();
    Vehicle& v = *vehicle;
    v.resetDrivetrain(true);
    Controls& c = input->c;
    c.seatbelt = true;
    c.ignition = true;
    c.handbrake = 0;
    c.gear = 1;
    c.indicator = 0;
    c.hazard = false;
    AutoDriverOptions opts;
    opts.spec = v.spec;
    opts.front = def->outline.front;
    opts.truck = def->heavy;
    // (`caps: this.def.caps` – none said: by `truck`)
    if (def->caps) opts.caps = std::optional<RuleMonitor::Caps>(RuleMonitor::Caps{def->caps->rural, def->caps->motorway});
    driver = std::make_unique<AutoDriver>(*app.world, *signals, *rules, traffic.get(), opts);
  }
  nav->clear();
  hud->routeLanes.clear();
  demo = std::make_unique<Demo>();
  demo->stops = tour ? std::vector<SpawnJson>{tour->end} : stops;
  demo->index = 0;
  demo->driver = std::move(driver);
  demo->active = !self;
  demo->look = 0;
  demo->remark = "";
  demo->takeT = 0;
  demo->held = false;
  demo->freeT = 0;
  demo->lost = 0;
  demo->searchAt = 0;
  demo->stuck = 0;
  demo->rescues = 0;
  demo->rescuedAt = -100;
  if (tour) demo->tour = Demo::Tour{.ways = tour->plan.ways, .from = tour->from, .metres = tour->plan.metres, .fresh = tour->plan.fresh, .odo = vehicle->odometer, .set = false, .line = tour->line};
  else demo->tour = std::nullopt;
  hud->clearNotices();
}

/**
 * A set route (game/routepresets.ts): planned from the road the vehicle is on through its places
 * and back, then driven by the player with the way shown, as a tour – leaving it, or driving
 * along it the other way, is a fault (RouteWatch).
 */
void Game::startPreset(const std::string& id) {
  const auto found = std::find_if(ROUTE_PRESETS.begin(), ROUTE_PRESETS.end(), [&](const RoutePreset& r) { return r.id == id; });
  const RoutePreset* preset = found == ROUTE_PRESETS.end() ? nullptr : &*found;
  const std::string lang = settings.lang;
  if (!preset || lesson) return;
  Lane* lane = currentLane();
  const RouteGraph* g = routeGraphNow();
  if (!lane || !g) {
    // (not on a road – or the roads of the whole map are still being read)
    wantNet();
    hud->message(tr(lane ? "realityLoading" : "routes.noRoad", lang));
    return;
  }
  const int from = wayOf(*lane);
  const std::optional<PresetPlan> plan = netIndex ? planPreset(*g, from, *preset, netIndex->main) : planPreset(*g, from, *preset);
  if (!plan) {
    hud->message(tr("routes.none", lang));
    return;
  }
  const NetData* net = netIndex ? netIndex->net.get() : nullptr;
  TourPreview way;
  way.plan = TourPlan{.ways = plan->ways, .metres = plan->metres, .fresh = plan->metres, .again = 0, .valued = plan->metres};
  way.line = routeLine(*g, from, plan->ways, net ? std::function<std::vector<double>(int)>([net](int e) { return netPolyline(*net, e); }) : nullptr);
  way.from = from;
  const RoutePoint& p0 = preset->points[0];
  const std::string name = tr("routes." + preset->id + ".name", lang);
  way.end = streetSpot(name, p0.x, p0.z, 0);
  startTrip({}, true, &way);
  if (!demo) return;
  routeWatch = std::make_unique<RouteWatch>(from, plan->ways);
  hud->message(name, tr("routes.started", lang, {{"km", formatUnit(plan->metres / 1000, Unit::Kilometer, lang, 1)}}));
}

/** The drive is over: the car has arrived, the player takes it over, or there is no way to go on. */
void Game::endDemo(const std::string& why_) {
  if (!this->demo) return;
  // (`why` may be a text of the drive that ends here)
  const std::string why = why_;
  const std::unique_ptr<Demo> demo = std::move(this->demo);
  this->demo = nullptr;
  routeWatch = nullptr;
  // a tour is kept with its date – also one given up half-way (but not one hardly begun)
  if (demo->tour) {
    const double driven = vehicle->odometer - demo->tour->odo;
    if (driven > 200) {
      TourRecord record;
      record.date = isoNow();
      record.planned = jsRound(demo->tour->metres);
      record.driven = jsRound(driven);
      record.fresh = jsRound(demo->tour->fresh);
      record.done = why == "arrived";
      record.line = thinLine(demo->tour->line);
      record.prefs = settings.tourPrefs;
      saveTour(record);
    }
  }
  nav->clear();
  hud->routeLanes.clear();
  rules->allowed = nullptr;
  Controls& c = input->c;
  c.indicator = 0;
  const std::string lang = settings.lang;
  if (why == "arrived") {
    // (the car that drove by itself is left standing; the player's own stays in the player's hands)
    if (demo->active) c.handbrake = 1;
    audio->click("chime");
    const auto km = [&](double m) { return formatUnit(m / 1000, Unit::Kilometer, lang, 1); };
    if (demo->tour) hud->message(tr("tourDone", lang), tr("tourResult", lang, {{"km", km(vehicle->odometer - demo->tour->odo)}, {"fresh", km(demo->tour->fresh)}, {"again", km(jsMax(0, demo->tour->metres - demo->tour->fresh))}}));
    else hud->message(tr("demoArrived", lang));
  } else if (why == "lost") {
    if (demo->active) c.handbrake = 1;
    hud->message(tr("demoNoRoute", lang));
  } else if (why == "ended") {
    if (demo->active) c.handbrake = 1;
    hud->message(tr("tripEnded", lang));
  } else hud->message(tr("demoTaken", lang), tr("demoTakenSub", lang));
}

/** Where the demonstration drive is heading: the route to the next of its places. */
void Game::updateDemo(double dt, PlayerState& p) {
  Demo& demo = *this->demo;
  Lane* lane = currentLane();
  const AutoCommand* cmd = demo.active && demo.driver ? &demo.driver->cmd : nullptr;
  const std::string lang = settings.lang;
  // The end of a street the way leads back out of: the car is turned round on the spot.
  // (Turning in the road means going back and forth, which the driver of this drive does not do.)
  if (cmd && cmd->turnRound) {
    const Lane& to = *cmd->turnRound->to;
    lanePoint(to, jsMin(3, to.len / 2), smp);
    const double y = app.world->surfaceAt(smp.x, smp.z, smp.y + 1.5, hit) ? hit.y : smp.y;
    putOnRoad(Spot{.x = smp.x, .y = y, .z = smp.z, .yaw = jsAtan2(-smp.tx, -smp.tz)});
    camSnap = true;
    hud->message(tr("demoTurned", lang));
    return;
  }
  // Standing, and not for a light, a line or traffic: something is in the way that the driver
  // cannot pass (a fault of the map), or the car has left the road.  It is put back into its
  // lane a little further on.  (Waiting for others who do not move either: after a long while, too.)
  if (!cmd || demo.held || std::fabs(p.speed) > 0.3) demo.stuck = 0;
  else if (!nav->busy()) demo.stuck = 0;
  else if (cmd->waiting && lane) demo.stuck += cmd->remark == "red" ? 0 : dt * 0.45;
  else demo.stuck += dt;
  if (demo.stuck > 7) {
    demo.stuck = 0;
    // (put further on each time it is stuck again soon after: past whatever stands in the way)
    demo.rescues = time - demo.rescuedAt < 60 ? demo.rescues + 1 : 0;
    demo.rescuedAt = time;
    const Place g = lane ? Place{.x = p.x, .z = p.z, .yaw = p.yaw} : Place{.x = lastGood.x, .z = lastGood.z, .yaw = lastGood.yaw};
    const std::optional<Spot> spot = freeSpot(g.x, g.z, g.yaw, 8 + 14 * demo.rescues);
    if (spot) {
      putOnRoad(*spot);
      camSnap = true;
      hud->message(tr("demoRescued", lang));
      return;
    }
    if (!lane) {
      endDemo("lost");
      return;
    }
  }
  if (!lane) {
    // not on a road of the network (a yard, a field): there is no way from here
    demo.lost += dt;
    if (demo.lost > 4 && !nav->busy()) endDemo("lost");
    return;
  }
  const bool last = demo.index >= static_cast<int>(demo.stops.size()) - 1;
  const SpawnJson& stop = demo.stops[static_cast<size_t>(demo.index)];
  if (!nav->busy()) {
    bool found = false;
    std::optional<Demo::Tour>& tour = demo.tour;
    const RouteGraph* gNow = routeGraphNow();
    // (the roads of the whole map are still being read: a moment – 2.0: or never, when they could not be read)
    if (!gNow) {
      if (netGaveUp) endDemo("lost");
      return;
    }
    const RouteGraph& g = *gNow;
    // (a search that finds no way runs through the roads of the whole map: not every picture, then)
    if (time < demo.searchAt) return;
    {
      nav->net = &g;
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
        const int edge = edgeNear(stop.x, stop.z);
        if (edge < 0) ways = std::nullopt;
        else if (edge == lane->edge->id) ways = std::vector<int>();
        else ways = quickestToEdge(g, here, edge);
      }
      if (ways) {
        nav->setWays(*ways, *lane, tour.has_value());
        if (tour) tour->set = true;
        found = true;
      }
    }
    if (!found) {
      // no way there from here (yet): try again shortly – three times –, then give up
      nav->clear();
      demo.searchAt = time + 1;
      demo.lost += 1.2;
      if (demo.lost > 3) endDemo("lost");
      return;
    }
    demo.lost = 0;
    hud->routeLanes = *nav->routeLanes;
    rules->allowed = allowedAlong(*nav->routeLanes);
  }
  // where the car that drives by itself comes to a halt: at the last place – on a tour, which may
  // pass that place before, only once the end is near
  if (demo.driver) demo.driver->halt = last && (!demo.tour || nav->left() <= 1) ? std::optional<AutoDriver::Halt>(AutoDriver::Halt{.x = stop.x, .z = stop.z}) : std::nullopt;
  const NavHint& h = nav->update(dt, lane, laneFront(), p.x, p.z);
  // a set route: kept – or left, or driven the other way (game/routepresets.ts)
  const std::optional<RouteFault> off = routeWatch ? routeWatch->update(wayOf(*lane), vehicle->odometer) : std::nullopt;
  if (off) rules->note(*off, *off == "routeReverse" ? "major" : "minor", {}, 20);
  // (the display keeps the lanes themselves, not the navigation's list: another route is one of other lanes)
  if (hud->routeLanes != *nav->routeLanes) {
    hud->routeLanes = *nav->routeLanes;
    rules->allowed = allowedAlong(*nav->routeLanes);
  }
  if (h.done && !last) {
    demo.index++;
    nav->clear();
    audio->click("chime");
    hud->message(tr("demoStop", lang, {{"n", demo.index}}));
  } else if (h.done && !demo.active) endDemo("arrived");
}

/**
 * Take over a car of the traffic watched – any car, a bus, an emergency
 * vehicle: a free drive from where it is, as the game's vehicle nearest to
 * it (game/takeover.ts takeKindOf), standing at first – a bus on a line
 * shown its line's way.  The vehicle chosen in the menu stays what it was.
 */
void Game::takeOverCar(int id) {
  TrafficCar* car = nullptr;
  for (TrafficCar* c : traffic->cars) {
    if (c->id == id) {
      car = c;
      break;
    }
  }
  if (!car) return;
  const std::string lang = settings.lang;
  // (`car.drawn ?? car`)
  const Place d = car->drawn ? Place{.x = car->drawn->x, .z = car->drawn->z, .yaw = car->drawn->yaw} : Place{.x = car->x, .z = car->z, .yaw = car->yaw};
  const VehicleKind kind = takeKindOf(car->style);
  // (later: a bus on a line is shown its line's way: transitData, lineWay, lineTakeOver – game.ts:3297)
  const int paint = !car->em && !car->bus ? car->color : -1;
  // (later: endWatch – game.ts:3301)
  if (demo) endDemo("ended");
  const VehicleKind was = settings.vehicle;
  settings.vehicle = kind;
  parkedAt = Place{.x = d.x, .z = d.z, .yaw = d.yaw};
  // (it keeps the drive from beginning on foot)
  intoParked = true;
  const AddressHit at{.label = tr("mapPlace", lang), .street = "", .x = d.x, .z = d.z, .exact = true};
  try {
    begin("free", 0, true, &at);
  } catch (...) {
    settings.vehicle = was;
    throw;
  }
  settings.vehicle = was;
  // (a car of the traffic: in its paint)
  if (paint >= 0 && def->model == "car") this->car.exterior.paint->color.setHex(paint);
  // (later: afterPlace – the way of the bus's line, startTrip([], true, way) – game.ts:3314)
  hud->message(tr("watchTakenVehicle", lang, {{"what", tr(VEHICLES.at(kind).name, lang)}}));
}

/** Does the autopilot drive now (the car's, or the tram's), without the player at the controls? */
bool Game::autopilotDriving() {
  // (later: lessonWatch – game.ts:3497)
  return (demo && demo->active && demo->driver && !demo->held) || (tramAuto && !tramHeld);
}

/**
 * The wishes for a tour (settings.tourPrefs) as the planner takes them:
 * motorways and expressways left out altogether if so wished; main roads
 * still used to get somewhere but not looked for; streets of the kind of
 * houses asked for (from the far view's buildings) more welcome than others.
 */
Game::TourWishes Game::tourWishes(const RouteGraph& g_) {
  // (the wishes are kept by the planner while it plans – a slice at a time –: what they look at is held by value,
  // the network and the far view by their address, which is theirs for as long as the map is open)
  const TourPrefs p = settings.tourPrefs;
  const RouteGraph* g = &g_;
  TourWishes out;
  if (p.noMotorway || p.noExpress) {
    out.avoid = [p, g](int w) {
      const int cls = g->cls[static_cast<size_t>(w)];
      return (p.noMotorway && cls <= MOTORWAY_LINK) || (p.noExpress && cls >= TRUNK && cls <= TRUNK_LINK);
    };
  }
  const FarTerrain* far = this->far.get();
  const auto kind = [far, g](int e) -> int {
    const size_t k = static_cast<size_t>(e) * 2;
    return far->data ? farBuilt(*far->data, (static_cast<double>(g->x1[k]) + static_cast<double>(g->x1[k + 1])) / 2, (static_cast<double>(g->z1[k]) + static_cast<double>(g->z1[k + 1])) / 2) : -1;
  };
  // welcome by the buildings along the street: none, small houses, rows and blocks, tall ones
  std::optional<std::array<double, 4>> by;
  if (p.mostly == "small") by = std::array<double, 4>{0.35, 2.5, 0.6, 0.3};
  else if (p.mostly == "town") by = std::array<double, 4>{0.25, 0.7, 1.8, 2.5};
  else if (p.mostly == "country") by = std::array<double, 4>{2.2, 0.7, 0.4, 0.3};
  if (p.noMain || by) {
    out.like = [p, g, by, kind](int e) -> double {
      const int c = g->cls[static_cast<size_t>(e) * 2];
      const bool main = c >= PRIMARY && c <= SECONDARY_LINK;
      const int k = by ? kind(e) : -1;
      return (p.noMain && main ? 0.15 : 1) * (by && k >= 0 ? (*by)[static_cast<size_t>(k)] : 1);
    };
  }
  return out;
}

}  // namespace fs
