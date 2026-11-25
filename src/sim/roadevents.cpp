// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/roadevents.ts @ v1.7.7
#include "sim/roadevents.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "game/clock.h"
#include "geo/utm.h"
#include "sim/collision.h"
#include "sim/kasselclock.h"
#include "sim/rail.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/routegraph.h"
#include "world/types.h"

namespace fs {

namespace {

/** A lane's distance of a station. */
double along(const Lane& lane, double s) { return (s - lane.s0) * lane.dir; }

/** The yaw of something facing the traffic that goes `dir` along a road whose direction a → b is (tx, tz). */
double faceYaw(int dir, double tx, double tz) { return std::atan2(dir * tx, dir * tz); }

int32_t imul(double a, int32_t b) { return jsImul(jsInt32(a), b); }

/** The moment of a time of day in Kassel (minutes after midnight) on a date: ms since 1970. */
double berlinMs(int y, int m, int d, double minutes) {
  const double guess = utcMs(y, m - 1, d, 0, 0) + minutes * 60000;
  return guess - kassel::berlinOffset(guess - 3600000) * 1000;
}

/** May a march walk along this road (and its closure make sense)? */
bool marchable(const Edge& e) {
  if (e.cls < PRIMARY || e.cls > LIVING_STREET || e.cls == PRIMARY_LINK || e.cls == SECONDARY_LINK || e.cls == TERTIARY_LINK) return false;
  if ((e.flags & (EF_ROUNDABOUT | EF_BRIDGE | EF_TUNNEL)) != 0 || e.layer != 0) return false;
  return e.lanesF + e.lanesB > 0;
}

/** The road to march on from a junction reached along `from`: on along the same street, or the straightest (no turn round a corner) – null at the end. */
Edge* onward(const Node& node, const Edge& from, const std::unordered_set<int>& used) {
  if (node.stub || node.arms.size() < 2) return nullptr;
  const Arm* inArm = nullptr;
  for (const Arm* a : node.arms)
    if (a->edge == &from) {
      inArm = a;
      break;
    }
  if (!inArm) return nullptr;
  Edge* best = nullptr;
  double bestScore = -JS_INF;
  for (const Arm* a : node.arms) {
    Edge* e = a->edge;
    if (e == &from || used.count(e->id) || !marchable(*e) || (a->out ? e->b : e->a)->stub) continue;
    // (1: straight on – the arm leaves the junction the way the march arrived; a bend of up to some 80° is
    // followed, a turn round a corner is not)
    const double straight = -(inArm->dx * a->dx + inArm->dz * a->dz);
    if (straight < 0.15) continue;
    const double score = straight + (!from.name.empty() && e->name == from.name ? 1 : 0) + (e->tier >= from.tier ? 0.2 : 0);
    if (score > bestScore) {
      bestScore = score;
      best = e;
    }
  }
  return best;
}

PolySample routeSmp;

}  // namespace

// ------------------------------------------------------------------------------------ roadworks

double entryOf(const WorkZone& z, int dir) { return dir > 0 ? z.z0 - z.taper : z.z1 + z.taper; }

double exitOf(const WorkZone& z, int dir) { return dir > 0 ? z.z1 + z.taper : z.z0 - z.taper; }

double waitOf(const WorkZone& z, int dir) { return entryOf(z, dir) - dir * 3; }

bool worksSite(const Edge& e) {
  const int c = e.cls;
  if (c < TRUNK || c > RESIDENTIAL || c == TRUNK_LINK || c == PRIMARY_LINK || c == SECONDARY_LINK || c == TERTIARY_LINK) return false;
  if ((e.flags & (EF_ROUNDABOUT | EF_BRIDGE | EF_TUNNEL | EF_MINOR_SERVICE | EF_LINK)) != 0 || e.layer != 0 || e.zone == 7) return false;
  return LaneGraph::drivable(e, true);
}

int dayKey(int y, int m, int d) { return y * 372 + (m - 1) * 31 + d; }

std::shared_ptr<WorkZone> worksOn(Edge& e, int day, const LaneGraph& lanes) {
  if (!worksSite(e)) return nullptr;
  const LaneGraph::EdgeLanes* el = lanes.edgeLanes[e.id];
  if (!el) return nullptr;
  const int nf = static_cast<int>(el->fwd.size());
  const int nb = static_cast<int>(el->back.size());
  const bool urban = (e.flags & EF_URBAN) != 0;
  const bool main = e.tier >= 3;
  const auto r = [&](int k) { return hash01(imul(e.id + 1, 0x2c1b3c6d) ^ imul(day + 7, 0x297a2d39) ^ imul(k + 1, 0x632be5ab)); };
  const double usable = e.len - e.cutA - e.cutB;
  const double rate = urban ? (main ? WORKS_RATE.urbanMain : WORKS_RATE.urbanMinor) : main ? WORKS_RATE.ruralMain : WORKS_RATE.ruralMinor;
  // (no road has works on more than two days in five)
  if (usable < 60 || r(0) >= std::min(0.4, (rate * usable) / 1000)) return nullptr;
  // which side, and how the traffic passes
  int side;
  std::string control;
  int laneIdx = 0;
  double len;
  if (nf == 1 && nb == 1) {
    side = r(1) < 0.5 ? 1 : -1;
    len = urban ? 16 + r(3) * (main ? 70 : 45) : 60 + r(3) * 190;
    control = urban && len < 36 && (!main || r(4) < 0.35) ? "signs" : "light";
  } else if (nf >= 2 || nb >= 2) {
    side = nf >= 2 && (nb < 2 || r(1) < 0.5) ? 1 : -1;
    control = "lane";
    const int n = side > 0 ? nf : nb;
    // (the left-most lane only where nobody comes the other way beside it: a carriageway of its own)
    laneIdx = (nf == 0 || nb == 0) && r(2) < 0.35 ? n - 1 : 0;
    len = urban ? 40 + r(3) * 100 : 120 + r(3) * 260;
  } else return nullptr;
  const double taper = control == "lane" ? (urban ? 18 : 45) : urban ? 10 : 30;
  // room before it for the signs and to stop: the line to wait at at least this far from the junction
  const double approach = urban ? 30 : 110;
  const double room = usable - 2 * (taper + approach);
  if (room < 14) return nullptr;
  len = std::max(14.0, std::min(len, room));
  if (control == "signs" && len > 40) control = "light";
  const double z0 = e.cutA + approach + taper + r(6) * (room - len);
  const double z1 = z0 + len;
  // not over a crossing for people on foot
  for (const CrossingJson& x : e.xings)
    if (x.s + x.w / 2 > z0 - taper - 10 && x.s - x.w / 2 < z1 + taper + 10) return nullptr;
  // the limit through it (if the road's own is higher)
  const double road = std::max(e.maxF, e.maxB);
  const auto over = [&](double v) { return road <= 0 || road > v; };
  double limit = 0;
  if (urban) limit = over(30) ? 30 : 0;
  else if (control == "lane") limit = over(70) ? 70 : over(50) ? 50 : 0;
  else limit = over(50) ? 50 : over(30) ? 30 : 0;
  const double warn = urban ? 30 + r(5) * 25 : 120 + r(5) * 80;
  // the light: a green each way, and all red long enough for the last one to get through
  const std::array<double, 2> green = urban ? std::array<double, 2>{14 + jsRound(r(7) * 8), 14 + jsRound(r(8) * 8)} : std::array<double, 2>{24 + jsRound(r(7) * 12), 24 + jsRound(r(8) * 12)};
  ZoneSpec spec;
  spec.side = side;
  spec.control = control;
  spec.lane = laneIdx;
  spec.z0 = z0;
  spec.z1 = z1;
  spec.taper = taper;
  spec.limit = limit;
  spec.warn = warn;
  spec.green = green;
  spec.phase = r(9);
  spec.seed = static_cast<int>(std::floor(r(10) * 1e6));
  return makeZone(e, lanes, spec);
}

std::shared_ptr<WorkZone> makeZone(Edge& e, const LaneGraph& lanes, const ZoneSpec& spec) {
  const LaneGraph::EdgeLanes& el = *lanes.edgeLanes[e.id];
  const int side = spec.side;
  const int laneIdx = spec.lane;
  const double len = spec.z1 - spec.z0;
  const bool urban = (e.flags & EF_URBAN) != 0;
  // across: the closed lane, the edge of the road beyond it, the line of the beacons
  const std::vector<Lane*>& lanesOf = side > 0 ? el.fwd : el.back;
  const Lane& L = *lanesOf[laneIdx];
  const double W = e.width;
  double outer;
  double inner;
  if (laneIdx == 0) {
    outer = -side * (W / 2);
    inner = L.off + (side * L.width) / 2;
  } else {
    outer = side * (W / 2);
    inner = L.off - (side * L.width) / 2;
  }
  const double line = inner + jsSign(outer - inner) * 0.3;
  double pass = 0;
  if (spec.control != "lane") {
    const Lane& O = *(side > 0 ? el.back : el.fwd)[0];
    pass = std::abs(L.off - O.off);
  }
  const double clear = std::ceil((len + 2 * spec.taper + 6) / 8) + 2;
  const double cycle = spec.green[0] + spec.green[1] + 2 * (3 + clear + 1);
  PolySample p;
  samplePolyline(e.pl, (spec.z0 + spec.z1) / 2, p);
  auto z = std::make_shared<WorkZone>();
  z->id = e.id;
  z->edge = &e;
  z->z0 = spec.z0;
  z->z1 = spec.z1;
  z->side = side;
  z->control = spec.control;
  z->lane = laneIdx;
  z->limit = spec.limit;
  z->warn = spec.warn;
  z->taper = spec.taper;
  z->urban = urban;
  z->street = !e.name.empty() ? e.name : e.ref;
  z->x = p.x;
  z->z = p.z;
  z->outer = outer;
  z->line = line;
  z->pass = pass;
  z->green = spec.green;
  z->clear = clear;
  z->offset = spec.phase * cycle;
  z->seed = spec.seed;
  return z;
}

WorksLight lightAt(const WorkZone& z, int group, double t) {
  const double g0 = z.green[0];
  const double g1 = z.green[1];
  const double c = z.clear;
  // green for the side of the work, amber, all red, red-amber for the others, their green, amber, all red, red-amber
  static const int states0[] = {SIG_GREEN, SIG_AMBER, SIG_RED, SIG_RED, SIG_RED, SIG_RED, SIG_RED, SIG_RED_AMBER};
  static const int states1[] = {SIG_RED, SIG_RED, SIG_RED, SIG_RED_AMBER, SIG_GREEN, SIG_AMBER, SIG_RED, SIG_RED};
  const double lens[] = {g0, 3, c, 1, g1, 3, c, 1};
  const int n = 8;
  double cycle = 0;
  for (const double l : lens) cycle += l;
  double u = std::fmod(t + z.offset, cycle);
  if (u < 0) u += cycle;
  const int* states = group == 0 ? states0 : states1;
  int k = 0;
  while (u >= lens[k]) {
    u -= lens[k];
    k++;
  }
  // (how long it has shown this: back through the phases before that showed the same)
  double age = u;
  for (int j = 1; j < n; j++) {
    const int i = (k - j + n) % n;
    if (states[i] != states[k]) break;
    age += lens[i];
  }
  return {states[k], age};
}

// ------------------------------------------------------------------------------------ demonstrations

const std::vector<DemoPlace>& DEMO_PLACES() {
  static const auto* places = new std::vector<DemoPlace>{
      {"Königsplatz", "Kassel", 51.3164, 9.4939, 4},
      {"Friedrichsplatz", "Kassel", 51.3128, 9.4975, 4},
      {"Obere Königsstraße", "Kassel", 51.3147, 9.496, 2},
      {"Stadtmitte", "Baunatal", 51.2557, 9.4104, 2},
  };
  return *places;
}

std::vector<DemoPlan> demosOn(int y, int m, int d) {
  const int day = dayKey(y, m, d);
  const int weekday = utcFields(utcMs(y, m - 1, d)).day;
  const bool holiday = kassel::isHoliday(y, m, d);
  const auto r = [&](int k) { return hash01(jsImul(day + 13, 0x7feb352d) ^ jsImul(k + 1, static_cast<int32_t>(0x846ca68bu)) ^ 0x5bd1e995); };
  // [when it begins (minutes after midnight), how likely]
  std::vector<std::pair<double, double>> slots;
  if (m == 5 && d == 1) slots.push_back({10 * 60 + 30, 1});
  else if (weekday == 6) slots.insert(slots.end(), {{11 * 60 + 30, 0.65}, {14 * 60 + 30, 0.25}});
  else if (weekday == 0 || holiday) slots.push_back({13 * 60, 0.15});
  else slots.insert(slots.end(), {{12 * 60, 0.06}, {17 * 60, 0.13}});
  const std::vector<DemoPlace>& places = DEMO_PLACES();
  std::vector<DemoPlan> out;
  double total = 0;
  for (const DemoPlace& p : places) total += p.weight;
  for (int k = 0; k < static_cast<int>(slots.size()); k++) {
    const auto [start, p] = slots[k];
    if (r(k * 8) >= p) continue;
    const double begin = start + (std::floor(r(k * 8 + 1) * 7) - 3) * 15;
    const double minutes = 60 + std::floor(r(k * 8 + 2) * 6) * 15;
    double pick = r(k * 8 + 3) * total;
    const DemoPlace* place = &places[0];
    for (const DemoPlace& pl : places) {
      pick -= pl.weight;
      if (pick < 0) {
        place = &pl;
        break;
      }
    }
    // (not twice at the same place at once)
    for (const DemoPlan& o : out) {
      if (o.place == place && o.until > berlinMs(y, m, d, begin)) {
        place = &places[(static_cast<size_t>(place - places.data()) + 1) % places.size()];
        break;
      }
    }
    const bool big = weekday == 6 || (m == 5 && d == 1);
    const int size = static_cast<int>(jsRound((big ? 60 : 30) + r(k * 8 + 4) * (big ? 160 : 90)));
    out.push_back(DemoPlan{day * 8 + k, place, berlinMs(y, m, d, begin), berlinMs(y, m, d, begin + minutes), size, static_cast<int>(std::floor(r(k * 8 + 5) * 1e6))});
  }
  return out;
}

std::optional<DemoRoute> demoRoute(const World& world, double x, double z, double want) {
  Edge* start = nullptr;
  double bestD = JS_INF;
  PolyProjection proj;
  world.edgesInBox(x - 260, z - 260, x + 260, z + 260, [&](Edge& e) {
    if (!marchable(e) || e.a->stub || e.b->stub) return;
    projectOnPolyline(e.pl, x, z, proj);
    if (proj.d2 > 260 * 260) return;
    // (a bigger street is worth a little more distance; ties go to the lower number)
    const double d = proj.d2 / (e.tier >= 3 ? 2 : 1);
    if (d < bestD - 1e-6 || (std::abs(d - bestD) <= 1e-6 && start != nullptr && e.id < start->id)) {
      bestD = d;
      start = &e;
    }
  });
  if (!start) return std::nullopt;
  Edge* first = start;
  std::vector<int> edges{first->id};
  std::vector<int> dirs{1};
  std::unordered_set<int> used{first->id};
  double total = first->len;
  struct End {
    Node* node;
    Edge* edge;
  };
  End head{first->b, first};
  End tail{first->a, first};
  bool canF = true;
  bool canB = true;
  for (int k = 0; total < want && (canF || canB) && k < 400; k++) {
    const bool forward = canF && (!canB || k % 2 == 0);
    const End& end = forward ? head : tail;
    Edge* next = onward(*end.node, *end.edge, used);
    if (!next) {
      if (forward) canF = false;
      else canB = false;
      continue;
    }
    used.insert(next->id);
    total += next->len;
    Node* other = next->a == end.node ? next->b : next->a;
    if (forward) {
      edges.push_back(next->id);
      dirs.push_back(next->a == end.node ? 1 : -1);
      head = End{other, next};
    } else {
      edges.insert(edges.begin(), next->id);
      dirs.insert(dirs.begin(), next->b == end.node ? 1 : -1);
      tail = End{other, next};
    }
  }
  DemoRoute route;
  // (the names in the order they were first met, as a Map keeps them)
  std::vector<std::pair<std::string, double>> names;
  double length = 0;
  for (size_t i = 0; i < edges.size(); i++) {
    const Edge* e = world.edges[edges[i]];
    route.cum.push_back(length);
    route.lens.push_back(e->len);
    length += e->len;
    if (!e->name.empty()) {
      auto it = std::find_if(names.begin(), names.end(), [&](const auto& n) { return n.first == e->name; });
      if (it == names.end()) names.push_back({e->name, e->len});
      else it->second += e->len;
    }
    route.nodes.push_back((dirs[i] > 0 ? e->a : e->b)->id);
    if (i == edges.size() - 1) route.nodes.push_back((dirs[i] > 0 ? e->b : e->a)->id);
  }
  std::string street;
  double most = 0;
  for (const auto& [n, l] : names) {
    if (l > most) {
      most = l;
      street = n;
    }
  }
  route.edges = edges;
  route.dirs = dirs;
  route.length = length;
  route.street = street;
  route.roads = std::unordered_set<int>(edges.begin(), edges.end());
  for (const int id : edges) {
    route.ways.insert(id * 2);
    route.ways.insert(id * 2 + 1);
  }
  // the side roads of the junctions it passes: closed where they lead into it
  for (size_t i = 1; i + 1 < route.nodes.size(); i++) {
    const Node* node = world.nodes[route.nodes[i]];
    if (!node) continue;
    for (const Arm* a : node->arms) {
      if (route.roads.count(a->edge->id)) continue;
      route.ways.insert(a->edge->id * 2 + (a->out ? 1 : 0));
    }
  }
  return route;
}

CrowdShape crowdShape(const Demonstration& d) {
  const int cols = 4 + (d.seed % 3);
  const int rows = static_cast<int>(std::ceil(static_cast<double>(d.size) / cols));
  const double gap = 1.15;
  return {cols, rows, gap, rows * gap + 1};
}

MarchAt marchAt(const Demonstration& d, double ms) {
  const DemoRoute* route = d.route ? &*d.route : nullptr;
  const CrowdShape shape = crowdShape(d);
  const double L = route ? route->length : 0;
  const double speed = 0.55;
  // (the front, walking on: from just behind the gathering to the end of the way; back: from there to the beginning)
  const double lo = shape.length + 3;
  const double span = std::max(0.0, L - lo - 3);
  const double dur = std::max(1.0, (d.until - d.from) / 1000);
  const double gather = std::min(600.0, dur * 0.12);
  const double t = (ms - d.from) / 1000;
  if (t < gather || span <= 0) return {std::min(lo, L), 1, false, speed};
  const double leg = span / speed;
  const double rest = 240;
  const double u = std::fmod(t - gather, 2 * (leg + rest));
  if (u < leg) return {lo + u * speed, 1, true, speed};
  if (u < leg + rest) return {lo + span, 1, false, speed};
  // (turned round: the last rows lead now)
  if (u < 2 * leg + rest) return {lo + span - shape.length - (u - leg - rest) * speed, -1, true, speed};
  return {3, -1, false, speed};
}

Edge* routeAt(const World& world, const DemoRoute& route, double u, double side, PolySample& out, double* sOut) {
  const int n = static_cast<int>(route.edges.size());
  int i = 0;
  const double v = clamp(u, 0, route.length);
  while (i < n - 1 && route.cum[i + 1] <= v) i++;
  Edge* e = world.edges[route.edges[i]];
  if (!e) return nullptr;
  const double local = clamp(v - route.cum[i], 0, e->len);
  const int dir = route.dirs[i];
  const double s = dir > 0 ? local : e->len - local;
  samplePolyline(e->pl, s, routeSmp);
  const double tx = routeSmp.tx * dir;
  const double tz = routeSmp.tz * dir;
  out.x = routeSmp.x + tz * side;
  out.z = routeSmp.z - tx * side;
  out.y = routeSmp.y;
  out.tx = tx;
  out.tz = tz;
  if (sOut) *sOut = s;
  return e;
}

// ------------------------------------------------------------------------------------ the events

namespace re {
/** what is fenced off and stands about is put into the collider within this distance of the player (m), taken out beyond the other */
constexpr double NEAR = 450;
constexpr double FAR = 650;
}  // namespace re

RoadEvents::RoadEvents(World& world, LaneGraph& lanes, SignalSystem& signals) : world(world), lanes(lanes), signals(signals) {
  now = [] { return gameClock.ms(); };
}

bool RoadEvents::addRoad(Edge& e) {
  std::shared_ptr<WorkZone> z = worksOn(e, day, lanes);
  if (z && !overTrack(*z)) {
    zones.erase(e.id);
    zones.set(e.id, z);
    return true;
  }
  return false;
}

void RoadEvents::setTracks(const RailNetwork* net) {
  tracks = net;
  bool any = false;
  for (auto it = zones.begin(); it != zones.end();) {
    if (!overTrack(*it->second)) {
      ++it;
      continue;
    }
    const int id = it->first;
    it = zones.erase(it);
    uncollide("works:" + std::to_string(id));
    any = true;
  }
  if (any) version++;
}

bool RoadEvents::overTrack(const WorkZone& z) {
  const RailNetwork* net = tracks;
  if (!net || net->paths.empty()) return false;
  const Edge& e = *z.edge;
  const double reach = e.width / 2 + 1;
  PolySample& p = smp;
  // the middle line of the stretch, every 6 m, and the box round it
  std::vector<double> line;
  double minX = JS_INF;
  double maxX = -JS_INF;
  double minZ = JS_INF;
  double maxZ = -JS_INF;
  for (double s = z.z0 - z.taper; s <= z.z1 + z.taper + 1e-6; s += 6) {
    samplePolyline(e.pl, clamp(s, 0, e.len), p);
    line.push_back(p.x);
    line.push_back(p.z);
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minZ = std::min(minZ, p.z);
    maxZ = std::max(maxZ, p.z);
  }
  // the tracks that pass the cells over the box (200 m each)
  std::vector<int> near;
  std::unordered_set<int> seen;
  for (double x = minX - 200; x <= maxX + 200; x += 200) {
    for (double zz = minZ - 200; zz <= maxZ + 200; zz += 200) {
      const std::vector<double> list = net->near(x, zz);
      for (size_t k = 0; k < list.size(); k += 2)
        if (seen.insert(static_cast<int>(list[k])).second) near.push_back(static_cast<int>(list[k]));
    }
  }
  for (const int pi : near) {
    const std::vector<double>& q = net->paths[pi].pts;
    for (size_t j = 3; j + 2 < q.size(); j += 3) {
      const double ax = q[j - 3];
      const double az = q[j - 1];
      const double bx = q[j];
      const double bz = q[j + 2];
      // (most pieces of track are nowhere near)
      if (std::max(ax, bx) < minX - reach || std::min(ax, bx) > maxX + reach || std::max(az, bz) < minZ - reach || std::min(az, bz) > maxZ + reach) continue;
      const double dx = bx - ax;
      const double dz = bz - az;
      const double l2 = dx * dx + dz * dz;
      for (size_t i = 0; i < line.size(); i += 2) {
        const double u = l2 > 1e-9 ? clamp(((line[i] - ax) * dx + (line[i + 1] - az) * dz) / l2, 0, 1) : 0;
        if (std::hypot(ax + dx * u - line[i], az + dz * u - line[i + 1]) < reach) return true;
      }
    }
  }
  return false;
}

void RoadEvents::railBlocks(std::vector<RailObstacle>& out) {
  const double ms = now();
  for (const Demonstration& d : demos) {
    if (!d.route || ms < d.from || ms >= d.until || std::abs(d.x - px) > 2000 || std::abs(d.z - pz) > 2000) continue;
    const MarchAt m = marchAt(d, ms);
    const CrowdShape shape = crowdShape(d);
    for (double k = -2; k < shape.length + 2; k += 5) {
      const Edge* at = routeAt(world, *d.route, m.head - m.dir * k, 0, smp);
      if (at) out.push_back(RailObstacle{smp.x, smp.z, std::max(3.0, at->width / 2 + 0.5)});
    }
  }
}

void RoadEvents::dropRoads(const std::vector<Edge*>& edges) {
  bool any = false;
  for (const Edge* e : edges) {
    std::shared_ptr<WorkZone>* z = zones.get(e->id);
    if (!z || (*z)->edge != e) continue;
    zones.erase(e->id);
    uncollide("works:" + std::to_string(e->id));
    any = true;
  }
  if (any) version++;
}

WorkZone* RoadEvents::zoneOn(const Edge& e) {
  std::shared_ptr<WorkZone>* z = zones.get(e.id);
  return z && (*z)->edge == &e ? z->get() : nullptr;
}

// ---------------------------------------------------------------------------- each step

void RoadEvents::step(double dt, const std::vector<TrafficCar*>& cars, const PlayerState& player) {
  tick -= dt;
  if (tick <= 0 || day < 0) {
    tick = 0.5;
    refresh(player);
  }
  for (auto& [id, z] : zones) {
    z->inside[0] = z->inside[1] = 0;
    z->coming[0] = z->coming[1] = 0;
    z->comingNear[0] = z->comingNear[1] = JS_INF;
    z->mergers.clear();
  }
  if (zones.size() > 0) {
    for (const TrafficCar* car : cars)
      if (car->lane) count(*car->lane, car->s, car->length, car->v);
    if (player.lane && !player.onFoot) count(*player.lane, player.s.value_or(0), player.length, std::abs(player.speed));
  }
  if (!player.onFoot) tips(player);
}

void RoadEvents::count(const Lane& lane, double mid, double length, double v) {
  WorkZone* z = zoneOn(*lane.edge);
  if (!z) return;
  const double front = mid + length / 2;
  if (z->control == "lane") {
    if (lane.dir != z->side || lane.idx != z->lane) return;
    const double close = along(lane, entryOf(*z, z->side));
    if (front > close - 9 && front < close + 0.5 && v < 1) z->mergers.insert(z->mergers.end(), {front, length});
    return;
  }
  const int k = lane.dir > 0 ? 0 : 1;
  const double wait = along(lane, waitOf(*z, lane.dir));
  const double out = along(lane, exitOf(*z, lane.dir));
  if (front > wait + 0.3 && mid - length / 2 < out) z->inside[k]++;
  else if (front <= wait + 0.3 && front > wait - 70 && (v > 0.5 || front > wait - 12)) {
    z->coming[k]++;
    if (v > 1.5) z->comingNear[k] = std::min(z->comingNear[k], wait - front);
  }
}

void RoadEvents::refresh(const PlayerState& player) {
  const double ms = now();
  const kassel::BerlinClock c = kassel::berlinClock(ms);
  const int today = dayKey(c.y, c.m, c.d);
  if (today != day) {
    day = today;
    for (auto& [id, z] : zones) uncollide("works:" + std::to_string(z->id));
    zones.clear();
    addRoads(world.allEdges);
    for (const Demonstration& d : demos) uncollide("demo:" + std::to_string(d.id));
    told.clear();
    demos.clear();
    for (const DemoPlan& p : demosOn(c.y, c.m, c.d)) {
      const auto [x, z] = latLonToWorld(p.place->lat, p.place->lon);
      Demonstration d;
      d.id = p.id;
      d.place = p.place->name;
      d.town = p.place->town;
      d.x = x;
      d.z = z;
      d.from = p.from;
      d.until = p.until;
      d.size = p.size;
      d.seed = p.seed;
      demos.push_back(std::move(d));
    }
    version++;
  }
  // (put somewhere else – a start, a jump on the map – rather than driven there)
  const bool jumped = !(std::hypot(player.x - px, player.z - pz) < 80);
  if (std::isfinite(player.x)) {
    px = player.x;
    pz = player.z;
  }
  if (!told.empty()) forget();
  // where the vehicle is: put down inside a fenced strip, that work is done for today; inside a closed road, its barriers let it out
  const Edge* on = !player.onFoot && std::isfinite(player.x) && world.surfaceAt(player.x, player.z, player.y + 1.5, hit) && hit.edge ? hit.edge : nullptr;
  if (on && jumped) {
    WorkZone* z = zoneOn(*on);
    if (z && hit.s > z->z0 - 1 && hit.s < z->z1 + 1 && (hit.off - z->outer) * (hit.off - z->line) < 0) {
      const int id = z->id;
      zones.erase(id);
      uncollide("works:" + std::to_string(id));
      version++;
    }
  }
  // the marches: their ways once the roads there are loaded; what is closed now, and soon
  std::unordered_set<int> shutNow;
  std::unordered_set<int> soon;
  std::unordered_set<int> roads;
  std::unordered_set<int> nodes;
  for (Demonstration& d : demos) {
    const bool near = std::hypot(d.x - px, d.z - pz) < 3500;
    if (!d.route && near && ms < d.until && ms > d.from - 3 * 3600000.0 && ready(d.x, d.z, 1200)) {
      d.route = demoRoute(world, d.x, d.z, 450 + (d.seed % 7) * 70);
      d.layout.reset();
    }
    if (!d.route) continue;
    const DemoRoute& route = *d.route;
    const bool live = ms >= d.from && ms < d.until;
    if (live || (ms < d.until && d.from - ms < 2 * 3600000.0)) soon.insert(route.ways.begin(), route.ways.end());
    if (live) {
      shutNow.insert(route.ways.begin(), route.ways.end());
      roads.insert(route.roads.begin(), route.roads.end());
      nodes.insert(route.nodes.begin(), route.nodes.end());
    }
    // (barriers stand while it is on, near the player – but let out a vehicle that is inside)
    const std::string key = "demo:" + std::to_string(d.id);
    const double dist = std::hypot(d.x - px, d.z - pz);
    const bool inside = on != nullptr && route.roads.count(on->id) > 0;
    if (live && dist < 1800 && !inside) {
      const ClosureLayout& l = closureLayout(d);
      collide(key, l.discs, l.segments);
    } else if (!live || dist > 2200 || inside) uncollide(key);
  }
  if (shutNow != shut || soon != shutSoon) {
    shut = shutNow;
    shutSoon = soon;
    shutRoads = roads;
    shutNodes = nodes;
    version++;
  }
  // the zones: in the collider near the player
  for (auto& [id, z] : zones) {
    const double d = std::hypot(z->x - px, z->z - pz);
    if (d < re::NEAR) {
      const WorksLayout& l = worksLayout(*z);
      collide("works:" + std::to_string(z->id), l.discs, l.segments);
    } else if (d > re::FAR) uncollide("works:" + std::to_string(z->id));
  }
}

void RoadEvents::collide(const std::string& key, const std::vector<double>& discs, const std::vector<double>& segments) {
  if (inCollider.count(key) || !collider) return;
  std::vector<float> segs(segments.begin(), segments.end());
  CollisionSource src;
  src.key = key;
  src.segments = segs.empty() ? nullptr : &segs;
  src.discs = discs;
  collider->add(src);
  inCollider.insert(key);
}

void RoadEvents::uncollide(const std::string& key) {
  if (!inCollider.erase(key)) return;
  if (collider) collider->remove(key);
}

void RoadEvents::tips(const PlayerState& p) {
  const Lane* lane = p.lane;
  if (lane) {
    const WorkZone* z = zoneOn(*lane->edge);
    if (z) {
      const double front = p.s.value_or(0) + p.length / 2;
      const double d = along(*lane, entryOf(*z, lane->dir)) - front;
      const int key = z->id * 2 + (lane->dir > 0 ? 0 : 1);
      if (d > 0 && d < (z->urban ? 90 : 220) && !told.count(key)) {
        told.insert(key);
        const bool mine = lane->dir == z->side;
        tipKey = z->control == "lane" ? (mine && lane->idx == z->lane ? "tipWorksLane" : "tipWorks") : z->control == "light" ? "tipWorksLight" : mine ? "tipWorks208" : "tipWorks308";
      }
    }
  }
  // a closure ahead
  const double fx = -std::sin(p.yaw);
  const double fz = -std::cos(p.yaw);
  for (const Demonstration& d : demos) {
    if (!d.layout || told.count(-1 - d.id) || now() < d.from || now() >= d.until) continue;
    for (const WorksBarrier& b : d.layout->barriers) {
      const double bx = (b.x1 + b.x2) / 2 - p.x;
      const double bz = (b.z1 + b.z2) / 2 - p.z;
      const double dist = std::hypot(bx, bz);
      if (dist < 90 && (bx * fx + bz * fz) > dist * 0.8) {
        told.insert(-1 - d.id);
        tipKey = "tipDemoClosed";
        break;
      }
    }
  }
}

void RoadEvents::forget() {
  const double ms = now();
  for (auto it = told.begin(); it != told.end();) {
    const int k = *it;
    bool drop;
    if (k >= 0) {
      std::shared_ptr<WorkZone>* z = zones.get(k >> 1);
      drop = !z || std::hypot((*z)->x - px, (*z)->z - pz) > 500;
    } else {
      const Demonstration* d = nullptr;
      for (const Demonstration& o : demos)
        if (o.id == -1 - k) {
          d = &o;
          break;
        }
      drop = !d || ms >= d->until || std::hypot(d->x - px, d->z - pz) > 1500;
    }
    it = drop ? told.erase(it) : std::next(it);
  }
}

std::string RoadEvents::takeTip() {
  std::string k = tipKey;
  tipKey.clear();
  return k;
}

// ---------------------------------------------------------------------------- for the traffic and the driver

Gate RoadEvents::gate(const Lane& lane, double front, double v, const Connector* next) {
  Gate& out = resetGate(g);
  if (zones.size() == 0 && shut.empty()) return out;
  WorkZone* z = zoneOn(*lane.edge);
  if (z) zoneGate(*z, lane, front, v, out);
  // just behind the junction ahead: a zone, a closed road
  if (next && lane.len - front < 75) {
    const Lane& l2 = *next->to;
    const double ahead = lane.len - front + next->len;
    if (shut.count(wayOf(l2)) && !shut.count(wayOf(lane))) {
      out.reroute = true;
      const double d = lane.stopAt - front;
      if (d > -1.5 && std::max(0.0, d) < out.stop) {
        out.stop = std::max(0.0, d);
        out.why = "closed";
      }
    }
    WorkZone* z2 = zoneOn(*l2.edge);
    if (z2) {
      const Gate& gg = zoneGate(*z2, l2, -ahead, v, resetGate(g2));
      if (gg.stop < out.stop) {
        out.stop = gg.stop;
        out.why = gg.why;
      }
      out.cap = std::min(out.cap, gg.cap);
    }
  }
  return out;
}

Gate& RoadEvents::resetGate(Gate& gt) {
  gt.stop = JS_INF;
  gt.cap = JS_INF;
  gt.move = nullptr;
  gt.reroute = false;
  gt.why = "";
  return gt;
}

const Gate* RoadEvents::gateCar(TrafficCar& car) {
  const Lane* lane = car.lane;
  if (!lane) return nullptr;
  g = gate(*lane, car.s + car.length / 2, car.v, car.next);
  return g.stop == JS_INF && g.cap == JS_INF && !g.move && !g.reroute ? nullptr : &g;
}

Gate& RoadEvents::zoneGate(WorkZone& z, const Lane& lane, double front, double v, Gate& out) {
  const int dir = lane.dir;
  // the limit, from its sign on (come up to at an easy deceleration)
  if (z.limit > 0) {
    const double from = along(lane, entryOf(z, dir) - dir * limitLead(z));
    const double to = along(lane, exitOf(z, dir) + dir * 8);
    if (front < to) {
      const double vz = z.limit / 3.6;
      out.cap = std::min(out.cap, std::sqrt(vz * vz + 2 * 1.5 * std::max(0.0, from - front)));
    }
  }
  if (z.control == "lane") {
    if (dir != z.side) return out;
    const double close = along(lane, entryOf(z, dir));
    if (lane.idx == z.lane) {
      // the lane ends: over to the one beside in good time, else wait at its end for a gap
      if (front < close + 0.5 && close - front < 260) {
        out.move = openLane(lane);
        if (close - 2 - front < out.stop) {
          out.stop = std::max(0.0, close - 2 - front);
          out.why = "lane";
        }
      }
    } else if (std::abs(lane.idx - z.lane) == 1 && front < close + 2) {
      // beside it: one of those waiting at its end is let in (one after the other, § 7 Abs. 4 StVO)
      const std::vector<double>& m = z.mergers;
      for (size_t i = 0; i + 1 < m.size(); i += 2) {
        const double ahead = m[i] - front;
        if (ahead > 1.5 && ahead < 26) {
          const double d = ahead - m[i + 1] - 3;
          if (d < out.stop) {
            out.stop = std::max(0.0, d);
            out.why = "lane";
          }
        }
      }
    }
    return out;
  }
  // one side closed: in turns
  const double wait = along(lane, waitOf(z, dir));
  if (front > wait + 0.3) return out;
  const double dist = wait - front;
  if (dist > 130) return out;
  const int mine = dir == z.side ? 0 : 1;
  const int other = dir > 0 ? 1 : 0;
  bool go;
  std::string why = "oncoming";
  if (z.control == "light") {
    const WorksLight st = lightAt(z, mine, signals.time);
    go = st.state == SIG_GREEN || (st.state == SIG_AMBER && (v * v) / (2 * std::max(dist, 0.5)) > 3.2);
    if (!go) why = "light";
    go = go && z.inside[other] == 0;
  } else if (mine == 0) {
    // the side of the work (Zeichen 208): the oncoming traffic goes first
    go = z.inside[other] == 0 && z.coming[other] == 0;
  } else go = z.inside[other] == 0;
  if (!go && dist < out.stop) {
    out.stop = dist;
    out.why = why;
  }
  return out;
}

double RoadEvents::limitLead(const WorkZone& z) const { return z.urban ? z.warn : z.warn * 0.5; }

Lane* RoadEvents::openLane(const Lane& lane) {
  const LaneGraph::EdgeLanes* el = lanes.edgeLanes[lane.edge->id];
  if (!el) return nullptr;
  const std::vector<Lane*>& list = lane.dir > 0 ? el->fwd : el->back;
  const int i = lane.idx == 0 ? 1 : lane.idx - 1;
  return i >= 0 && i < static_cast<int>(list.size()) ? list[i] : nullptr;
}

bool RoadEvents::laneOpen(const Lane& lane, double front) {
  const WorkZone* z = zoneOn(*lane.edge);
  if (!z || z->control != "lane" || lane.dir != z->side || lane.idx != z->lane) return true;
  const double close = along(lane, entryOf(*z, lane.dir));
  const double end = along(lane, lane.dir > 0 ? z->z1 : z->z0);
  return front < close - 260 || front > end + 4;
}

double RoadEvents::shift(const Lane& lane, double mid) {
  if (zones.size() == 0) return 0;
  const WorkZone* z = zoneOn(*lane.edge);
  if (!z || z->control == "lane" || lane.dir != z->side) return 0;
  const double a = along(lane, entryOf(*z, lane.dir));
  const double d = along(lane, exitOf(*z, lane.dir));
  if (mid <= a || mid >= d) return 0;
  const double b = along(lane, lane.dir > 0 ? z->z0 : z->z1);
  const double c = along(lane, lane.dir > 0 ? z->z1 : z->z0);
  if (mid < b) return z->pass * smoothstep(a, b, mid);
  if (mid > c) return z->pass * (1 - smoothstep(c, d, mid));
  return z->pass;
}

void RoadEvents::offset(const Lane& lane, double d, PolySample& p) {
  const double s = shift(lane, d);
  if (s == 0) return;
  p.x += p.tz * s;
  p.z -= p.tx * s;
}

bool RoadEvents::closed(const Lane& lane) { return !shut.empty() && shut.count(wayOf(lane)) > 0; }

bool RoadEvents::blocked(const Lane& lane, double s) {
  if (!shut.empty()) {
    if (shut.count(wayOf(lane))) return true;
    if ((shutNodes.count(lane.from->id) && s < 30) || (shutNodes.count(lane.to->id) && lane.len - s < 30)) return true;
  }
  const WorkZone* z = zoneOn(*lane.edge);
  if (!z) return false;
  const double a = along(lane, entryOf(*z, lane.dir)) - 40;
  const double b = along(lane, exitOf(*z, lane.dir)) + 10;
  return s > a && s < b;
}

double RoadEvents::wayCost(int way) {
  if (!shutSoon.empty() && shutSoon.count(way)) return JS_INF;
  const std::shared_ptr<WorkZone>* z = zones.get(way >> 1);
  if (!z) return 0;
  return (*z)->control == "light" ? 30 : (*z)->control == "signs" ? 12 : 8;
}

const std::vector<PlayerState*>& RoadEvents::withBlocks(const std::vector<PlayerState*>& obstacles, const PlayerState& player) {
  std::vector<PlayerState*>& out = blockList;
  out.clear();
  crowdList.clear();
  for (PlayerState* o : obstacles) out.push_back(o);
  for (auto& [id, z] : zones) {
    if (std::abs(z->x - player.x) > 400 || std::abs(z->z - player.z) > 400) continue;
    for (PlayerState& b : worksLayout(*z).boxes) out.push_back(&b);
  }
  const double ms = now();
  for (Demonstration& d : demos) {
    if (!d.route || ms < d.from || ms >= d.until || std::abs(d.x - player.x) > 1500 || std::abs(d.z - player.z) > 1500) continue;
    for (PlayerState& b : closureLayout(d).boxes) out.push_back(&b);
    crowdBoxes(d, ms);
  }
  return out;
}

std::optional<RoadEvents::CrowdPlace> RoadEvents::crowdAt(double x, double z, double ms) {
  const Demonstration* d = nullptr;
  for (const Demonstration& q : demos)
    if (std::hypot(q.x - x, q.z - z) < 1) {
      d = &q;
      break;
    }
  if (!d || !d->route) return std::nullopt;
  const MarchAt m = marchAt(*d, ms);
  if (!routeAt(world, *d->route, m.head - (m.dir * crowdShape(*d).length) / 2, 0, smp)) return std::nullopt;
  return CrowdPlace{smp.x, smp.y, smp.z, std::atan2(-smp.tx * m.dir, -smp.tz * m.dir)};
}

void RoadEvents::crowdBoxes(const Demonstration& d, double ms) {
  if (!d.route) return;
  const MarchAt m = marchAt(d, ms);
  const CrowdShape shape = crowdShape(d);
  PolySample& p = smp;
  for (double k = 0; k < shape.length; k += 8) {
    const Edge* at = routeAt(world, *d.route, m.head - m.dir * (k + 4), 0, p);
    if (!at) continue;
    PlayerState b;
    b.x = p.x;
    b.y = p.y;
    b.z = p.z;
    b.yaw = std::atan2(-p.tx, -p.tz);
    b.speed = 0;
    b.length = 8.5;
    b.width = at->width;
    crowdList.push_back(b);
    blockList.push_back(&crowdList.back());
  }
}

// ---------------------------------------------------------------------------- for the rule monitor

double RoadEvents::limitAt(const Edge& edge, double s, int dir, double limit) {
  const WorkZone* z = zoneOn(edge);
  if (!z || z->limit <= 0) return limit;
  const double from = entryOf(*z, dir) - dir * limitLead(*z);
  const double to = exitOf(*z, dir) + dir * 8;
  if ((s - from) * dir < 0 || (s - to) * dir > 0) return limit;
  return limit <= 0 ? z->limit : std::min(limit, z->limit);
}

bool RoadEvents::passing(const Edge& edge, double s, int dir) {
  const WorkZone* z = zoneOn(edge);
  if (!z || z->control == "lane" || dir != z->side) return false;
  return (s - entryOf(*z, dir)) * dir > -2 && (s - exitOf(*z, dir)) * dir < 2;
}

void RoadEvents::judge(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, double off, int dir, double dt) {
  const WorkZone* z = zoneOn(edge);
  if (z) {
    // the fenced strip
    const double lo = std::min(z->outer, z->line);
    const double hi = std::max(z->outer, z->line);
    if (s > z->z0 + 0.5 && s < z->z1 - 0.5 && off > lo + 0.25 && off < hi - 0.25 && std::abs(p.speed) > 0.3) {
      inStrip += dt;
      if (inStrip > 0.5) rules.noteAt("worksLane", "major", p.x, p.z, {}, 20);
    } else inStrip = 0;
    // the line where the stretch is waited for
    if (z->control != "lane") {
      const double front = s + dir * (p.front ? *p.front : p.length / 2);
      const double w = waitOf(*z, dir);
      // (from where it was a moment ago: not from where it was before it was put somewhere else)
      if (prevFront && prevFront->zone == z && prevFront->dir == dir && rules.time - prevFront->t < 0.6 && (prevFront->s - w) * dir < 0 && (front - w) * dir >= 0 && p.speed > 0.3) {
        const int group = dir == z->side ? 0 : 1;
        const int other = dir > 0 ? 1 : 0;
        if (z->control == "light") {
          const WorksLight st = lightAt(*z, group, signals.time);
          if (st.state == SIG_RED || st.state == SIG_RED_AMBER) rules.noteAt("redLight", st.state == SIG_RED && st.age < 0.6 ? "major" : "fail", p.x, p.z, {}, 8);
          else if (st.state == SIG_AMBER && st.age > 1 + p.speed / 7) rules.noteAt("amberLight", "minor", p.x, p.z, {}, 8);
        } else if (group == 0) {
          if (z->inside[other] > 0) rules.noteAt("worksYield", "major", p.x, p.z, {}, 10);
          else if (z->comingNear[other] < 35) rules.noteAt("worksYield", "minor", p.x, p.z, {}, 10);
        }
      }
      prevFront = Front{z, dir, front, rules.time};
    }
  } else {
    inStrip = 0;
    prevFront.reset();
  }
  // into a road closed for a march (a few metres in: not merely its mouth) – from an open one: whoever was in it
  // when it was closed, or was put there, drives out of it
  if (!(std::hypot(p.x - jx, p.z - jz) < 25)) wasOpen = false;
  jx = p.x;
  jz = p.z;
  if (!shutRoads.count(edge.id)) wasOpen = true;
  else if (s > edge.cutA + 6 && s < edge.len - edge.cutB - 6) {
    if (wasOpen && std::abs(p.speed) > 0.5) rules.noteAt("closedRoad", "major", p.x, p.z, {}, 30);
    wasOpen = false;
  }
}

// ---------------------------------------------------------------------------- for the map

std::vector<RoadEventInfo> RoadEvents::list() {
  std::vector<RoadEventInfo> out;
  const double ms = now();
  const kassel::BerlinClock c = kassel::berlinClock(ms);
  const double dayFrom = berlinMs(c.y, c.m, c.d, 0);
  const double dayUntil = berlinMs(c.y, c.m, c.d, 24 * 60);
  for (const auto& [id, z] : zones) out.push_back(RoadEventInfo{"works", z->x, z->z, z->street, dayFrom, dayUntil, true, z->control, {}});
  for (const Demonstration& d : demos) {
    std::vector<double> line;
    if (d.route) {
      const DemoRoute& route = *d.route;
      for (double u = 0; u <= route.length; u += std::max(10.0, route.length / 40)) {
        if (routeAt(world, route, u, 0, smp)) {
          line.push_back(jsRound(smp.x));
          line.push_back(jsRound(smp.z));
        }
      }
    }
    out.push_back(RoadEventInfo{"demo", d.x, d.z, d.route ? d.route->street : d.place, d.from, d.until, ms >= d.from && ms < d.until, d.place, line});
  }
  return out;
}

bool RoadEvents::marching(const Demonstration& d) {
  const double ms = now();
  return d.route.has_value() && ms >= d.from && ms < d.until;
}

// ---------------------------------------------------------------------------- what stands there

RoadEvents::Spot& RoadEvents::put(const Edge& e, double s, double o, Spot& out) {
  PolySample& p = smp;
  samplePolyline(e.pl, clamp(s, 0, e.len), p);
  out.x = p.x + p.tz * o;
  out.z = p.z - p.tx * o;
  const double yRef = world.edgeY(e, clamp(s, 0, e.len), out.x, out.z);
  out.y = world.surfaceAt(out.x, out.z, yRef + 1.5, hit) && std::abs(hit.y - yRef) < 1.5 ? hit.y : yRef;
  return out;
}

WorksLayout& RoadEvents::worksLayout(WorkZone& z) {
  if (z.layout) return *z.layout;
  const Edge& e = *z.edge;
  WorksLayout L;
  const int side = z.side;
  const double W = e.width;
  Spot q;
  Spot q2;
  PolySample& p = smp;
  const auto r = [&](int k) { return hash01(z.seed * 31.0 + k * 7919.0); };
  const auto tangent = [&](double s) { samplePolyline(e.pl, clamp(s, 0, e.len), p); };
  const auto yawAt = [&](double s, int dir) {
    tangent(s);
    return faceYaw(dir, p.tx, p.tz);
  };
  const auto roadYaw = [&](double s) {
    tangent(s);
    return std::atan2(-p.tx, -p.tz);
  };
  const auto sign = [](double x, double y, double zz, double yaw, std::vector<std::string> codes, double h) {
    SignPost sp;
    sp.x = x;
    sp.y = y;
    sp.z = zz;
    sp.yaw = yaw;
    sp.s = std::move(codes);
    sp.h = h;
    return sp;
  };
  // --- signs, for each direction that has lanes
  for (const int dir : {1, -1}) {
    if ((dir > 0 ? e.lanesF : e.lanesB) == 0) continue;
    const bool mine = dir == side;
    const double entry = entryOf(z, dir);
    const double exit = exitOf(z, dir);
    // (on this road: not beyond its lanes)
    const double lo = dir > 0 ? e.cutA + 2 : e.len - e.cutB - 2;
    const double hi = dir > 0 ? e.len - e.cutB - 2 : e.cutA + 2;
    const auto within = [&](double s) { return dir > 0 ? clamp(s, lo, hi) : clamp(s, hi, lo); };
    const double sw = dir > 0 ? e.swR : e.swL;
    const double o = -dir * (W / 2 + (sw > 0 ? std::min(0.6, sw * 0.4) : 0.9));
    const auto post = [&](double s, const std::vector<std::string>& codes) {
      if (codes.empty()) return;
      const double at = within(s);
      put(e, at, o, q);
      L.signs.push_back(sign(q.x, q.y, q.z, yawAt(at, dir), codes, 1.4));
      L.discs.insert(L.discs.end(), {q.x, q.y, q.z, 0.09});
    };
    const std::vector<std::string> lim = z.limit > 0 ? std::vector<std::string>{"274-" + jsStr(z.limit)} : std::vector<std::string>{};
    const std::vector<std::string> narrow =
        z.control == "lane" ? (mine ? std::vector<std::string>{z.lane == 0 ? "121-10" : "121-20"} : std::vector<std::string>{}) : std::vector<std::string>{"120"};
    const auto cat = [](std::vector<std::string> a, const std::vector<std::string>& b) {
      a.insert(a.end(), b.begin(), b.end());
      return a;
    };
    if (z.urban) {
      post(entry - dir * z.warn, cat({"123"}, lim));
      if (z.warn > 24) post(entry - dir * z.warn * 0.45, narrow);
    } else {
      post(entry - dir * z.warn, {"123"});
      post(entry - dir * z.warn * 0.5, cat(lim, narrow));
    }
    if (z.control == "signs") post(entry - dir * 4, {mine ? "208" : "308"});
    if (z.limit > 0) {
      const double road = dir > 0 ? e.maxF : e.maxB;
      const double usual = z.urban ? 50 : 100;
      post(exit + dir * 8, {road > 0 && road < 200 && road != usual ? "274-" + jsStr(road) : road == usual ? "278-" + jsStr(z.limit) : "282"});
    }
    // the temporary light at the line, on the right
    if (z.control == "light") {
      const double at = waitOf(z, dir) + dir * 0.8;
      put(e, at, -dir * (W / 2 + (sw > 0 ? 0.45 : 0.75)), q);
      L.lights.push_back(WorksLayout::Light{q.x, q.y, q.z, yawAt(at, dir), mine ? 0 : 1});
      L.discs.insert(L.discs.end(), {q.x, q.y, q.z, 0.3});
    }
  }
  // --- the strip: the barrier where it begins for the side of the work, the beacons along it
  const double entryC = side > 0 ? z.z0 : z.z1;
  const double exitC = side > 0 ? z.z1 : z.z0;
  const double inward = jsSign(z.line - z.outer);
  const double outerIn = z.outer + inward * 0.35;
  const bool passLeft = z.lane == 0;
  put(e, entryC, outerIn - inward * 0.2, q);
  put(e, entryC, z.line, q2);
  const double by = (q.y + q2.y) / 2;
  L.barriers.push_back(WorksBarrier{q.x, q.z, q2.x, q2.z, by, yawAt(entryC, side), static_cast<int>(std::max(2.0, jsRound(std::hypot(q2.x - q.x, q2.z - q.z) / 1.1)))});
  L.segments.insert(L.segments.end(), {q.x, q.z, q2.x, q2.z, by});
  // (Zeichen 222 above it: pass on the open side)
  put(e, entryC - side * 0.5, (z.outer + z.line) / 2, q);
  L.signs.push_back(sign(q.x, q.y, q.z, yawAt(entryC, side), {passLeft ? "222-10" : "222"}, 1.45));
  const auto beacon = [&](double s, double o, bool lamp) {
    put(e, s, o, q);
    L.beacons.push_back(WorksLayout::Beacon{q.x, q.y, q.z, yawAt(s, side), passLeft ? 1 : -1, lamp});
    L.discs.insert(L.discs.end(), {q.x, q.y, q.z, 0.2});
  };
  const int nT = static_cast<int>(std::max(2.0, jsRound(z.taper / (z.urban ? 3 : 7))));
  for (int i = 0; i < nT; i++) {
    const double f = static_cast<double>(i) / nT;
    beacon(entryC - side * z.taper * (1 - f), outerIn + (z.line - outerIn) * f, true);
  }
  const double len = z.z1 - z.z0;
  const int nL = static_cast<int>(std::max(1.0, jsRound(len / (z.urban ? 6 : 12))));
  for (int i = 1; i <= nL; i++) beacon(entryC + side * ((len * i) / nL), z.line, i % 2 == 0 || i == nL);
  // (closing the strip behind: a short slant back towards the edge – out of the way of those who move back into their lane)
  for (int i = 1; i <= 2; i++) beacon(exitC + side * z.taper * 0.3 * i, z.line + (outerIn - z.line) * (i / 3.0), false);
  // --- the work: a pit with a fence round it, a mini excavator, gravel, a site trailer – and the people
  const double mid = (z.z0 + z.z1) / 2;
  const double across = (z.outer + z.line) / 2;
  const double band = std::abs(z.line - z.outer);
  const double pitLen = std::min(9.0, len * 0.38);
  const double pitW = std::max(0.8, band - 1.3);
  put(e, mid, across, q);
  L.pit = WorksLayout::Pit{q.x, q.y, q.z, roadYaw(mid), pitLen, pitW};
  const double fa = pitLen / 2 + 0.6;
  const double fb = std::min(pitW / 2 + 0.3, band / 2 - 0.12);
  const double corners[4][2] = {{-fa, -fb}, {fa, -fb}, {fa, fb}, {-fa, fb}};
  for (const auto& c : corners) {
    put(e, mid + c[0], across + c[1], q);
    L.fence.insert(L.fence.end(), {q.x, q.z});
  }
  L.fenceY = L.pit->y;
  for (int i = 0; i < 4; i++) {
    const int j = (i + 1) % 4;
    L.segments.insert(L.segments.end(), {L.fence[i * 2], L.fence[i * 2 + 1], L.fence[j * 2], L.fence[j * 2 + 1], L.fenceY});
  }
  const int toward = side;
  if (len >= 18) {
    const double s = mid + toward * (fa + 2.6);
    put(e, s, across, q);
    // (facing the pit)
    L.machines.push_back(WorksLayout::Machine{"excavator", q.x, q.y, q.z, yawAt(s, toward)});
    L.discs.insert(L.discs.end(), {q.x, q.y, q.z, 1.2});
  }
  double heapS = JS_NAN;
  if (len >= 26) {
    heapS = mid - toward * (fa + 2.4);
    put(e, heapS, across, q);
    L.machines.push_back(WorksLayout::Machine{"heap", q.x, q.y, q.z, roadYaw(heapS) + r(1) * 3});
    L.discs.insert(L.discs.end(), {q.x, q.y, q.z, 1.1});
  }
  if (len >= 42) {
    const double s = exitC - side * 3.2;
    put(e, s, across, q);
    L.machines.push_back(WorksLayout::Machine{"trailer", q.x, q.y, q.z, roadYaw(s)});
    L.discs.insert(L.discs.end(), {q.x, q.y, q.z, 1.5});
  }
  // cones at the corners of the fence
  for (int i = 0; i < 4; i++) L.cones.push_back(WorksLayout::Cone{L.fence[i * 2], L.fenceY, L.fence[i * 2 + 1]});
  // the workers: one at the pit, one between the heap and the pit, one more on a long site
  const auto look = [&](int k) { return (z.seed + k * 977) % 0xfffff; };
  put(e, mid - toward * (fa + 0.5), across + (r(2) - 0.5) * band * 0.3, q);
  L.workers.push_back(WorksLayout::Worker{q.x, q.z, q.x, q.z, q.y, yawAt(mid, -toward), look(1)});
  if (std::isfinite(heapS)) {
    put(e, heapS + toward * 1.3, across + band * 0.2, q);
    put(e, mid - toward * (fa + 0.2), across + band * 0.25, q2);
    L.workers.push_back(WorksLayout::Worker{q.x, q.z, q2.x, q2.z, q.y, 0, look(2)});
  }
  if (len >= 42 || r(3) < 0.4) {
    const double s = len >= 42 ? exitC - side * 6 : mid + toward * (fa + 0.4);
    put(e, s, across - band * 0.2, q);
    L.workers.push_back(WorksLayout::Worker{q.x, q.z, q.x, q.z, q.y, yawAt(s, toward), look(3)});
  }
  // --- what the traffic stops for: the strip in pieces of 15 m at most
  const int nb = static_cast<int>(std::max(1.0, std::ceil(len / 15)));
  for (int i = 0; i < nb; i++) {
    const double s = z.z0 + (len * (i + 0.5)) / nb;
    put(e, s, across, q);
    PlayerState b;
    b.x = q.x;
    b.y = q.y;
    b.z = q.z;
    b.yaw = roadYaw(s);
    b.speed = 0;
    b.length = len / nb + 0.6;
    b.width = band;
    L.boxes.push_back(b);
  }
  z.layout = std::move(L);
  return *z.layout;
}

ClosureLayout& RoadEvents::closureLayout(Demonstration& d) {
  if (d.layout) return *d.layout;
  ClosureLayout L;
  if (!d.route) {
    noClosure = std::move(L);
    return noClosure;
  }
  const DemoRoute& route = *d.route;
  Spot q;
  Spot q2;
  PolySample& p = smp;
  /** a barrier across road `e` at its end at `node` (`end`: the march's own road at its end – else a side road) */
  const auto barrier = [&](const Edge& e, bool atA, bool end) {
    const double cut = atA ? e.cutA : e.cutB;
    if (e.len < 8) return;
    const double s = atA ? std::min(e.len * 0.5, cut + 3) : std::max(e.len * 0.5, e.len - cut - 3);
    // the traffic comes from outside the closure: into the march's road from the junction, along a side road towards the junction
    const int dir = end == atA ? 1 : -1;
    samplePolyline(e.pl, s, p);
    const double yaw = faceYaw(dir, p.tx, p.tz);
    const double half = e.width / 2 + 0.15;
    put(e, s, -half, q);
    put(e, s, half, q2);
    const double y = (q.y + q2.y) / 2;
    L.barriers.push_back(WorksBarrier{q.x, q.z, q2.x, q2.z, y, yaw, static_cast<int>(std::max(2.0, jsRound(e.width / 1.5)))});
    L.segments.insert(L.segments.end(), {q.x, q.z, q2.x, q2.z, y});
    // on the pavements, short fences that leave room to walk past
    for (const int sgn : {-1, 1}) {
      const double sw = sgn > 0 ? e.swL : e.swR;
      if (sw < 1.6) continue;
      put(e, s, sgn * (half + 0.1), q);
      put(e, s, sgn * (half + sw - 1.3), q2);
      L.segments.insert(L.segments.end(), {q.x, q.z, q2.x, q2.z, q.y});
      L.tape.push_back(ClosureLayout::Tape{q.x, q.z, q2.x, q2.z, q.y});
    }
    // the tape behind it, over the whole street
    const double back = s + dir * 0.6;
    put(e, back, -half - (e.swR > 0 ? e.swR : 0), q);
    put(e, back, half + (e.swL > 0 ? e.swL : 0), q2);
    L.tape.push_back(ClosureLayout::Tape{q.x, q.z, q2.x, q2.z, (q.y + q2.y) / 2});
    // the signs: closed (Zeichen 250), a diversion (457.1) – on the right for those who come
    const double sw = dir > 0 ? e.swR : e.swL;
    put(e, s - dir * 2, -dir * (e.width / 2 + (sw > 0 ? std::min(0.6, sw * 0.4) : 0.8)), q);
    SignPost sp;
    sp.x = q.x;
    sp.y = q.y;
    sp.z = q.z;
    sp.yaw = yaw;
    sp.s = {"250", "457.1"};
    sp.h = 1.3;
    L.signs.push_back(sp);
    L.discs.insert(L.discs.end(), {q.x, q.y, q.z, 0.09});
    // an officer (two where the march itself is closed off), and a police car behind
    for (int k = 0; k < (end ? 2 : 1); k++) {
      put(e, s + dir * (1.2 + k * 0.8), (k == 0 ? -dir : dir) * (e.width / 2 - 0.7), q);
      L.officers.push_back(ClosureLayout::Stand{q.x, q.y, q.z, yaw});
    }
    if (end && e.len > 22) {
      const double cs = s + dir * 7;
      put(e, cs, -dir * std::max(0.0, e.width / 2 - 1.3), q);
      samplePolyline(e.pl, cs, p);
      L.cars.push_back(ClosureLayout::Stand{q.x, q.y, q.z, faceYaw(dir, p.tx, p.tz)});
    }
    // what the traffic stops for
    const WorksBarrier& bar = L.barriers.back();
    PlayerState b;
    b.x = (bar.x1 + bar.x2) / 2;
    b.y = y;
    b.z = (bar.z1 + bar.z2) / 2;
    b.yaw = yaw + M_PI;
    b.speed = 0;
    b.length = 1.2;
    b.width = e.width + 0.3;
    L.boxes.push_back(b);
  };
  const size_t n = route.edges.size();
  // where it begins and where it ends: across its own road
  const Edge* first = world.edges[route.edges[0]];
  const Edge* last = world.edges[route.edges[n - 1]];
  if (first) barrier(*first, route.dirs[0] > 0, true);
  if (last) barrier(*last, route.dirs[n - 1] < 0, true);
  // the side roads of the junctions on its way
  for (size_t i = 1; i + 1 < route.nodes.size(); i++) {
    const Node* node = world.nodes[route.nodes[i]];
    if (!node || node->stub) continue;
    for (const Arm* a : node->arms) {
      if (route.roads.count(a->edge->id) || !a->isApproach) continue;
      barrier(*a->edge, a->out, false);
    }
  }
  d.layout = std::move(L);
  return *d.layout;
}

}  // namespace fs
