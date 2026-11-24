// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/lessonplaces.ts @ v1.7.7
#include "game/lessonplaces.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "core/geom.h"
#include "core/js.h"
#include "core/random.h"
#include "game/nav.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

std::optional<PlaceKind> placeKindOf(const LessonDef& l) {
  if (l.kind == "basics") return "quiet";
  if (l.kind == "exam") return "area";
  if (l.kind == "route") {
    if (!l.targets.empty() && l.targets[0] != "home") return l.targets[0];
  }
  return std::nullopt;
}

namespace {

/** how far from the first start place (the home town) the districts of the home region lie (m) */
const double HOME_REGION = 12000;
/** … for the motorway lesson: a slip road is further to look for */
const double MOTORWAY_RADIUS = 3000;

/** how far before the place the vehicle is put (m) */
const double LEAD_IN = 110;

/** The name of a start place's district: "Altenbauna – Parkplatz" → "Altenbauna". */
std::string districtOf(const std::string& name) { return trim(split(name, " – ")[0]); }

}  // namespace

double searchRadius(const PlaceKind& kind) { return kind == "motorway" ? MOTORWAY_RADIUS : SEARCH_RADIUS; }

std::vector<LessonArea> areaChoices(const std::vector<SpawnJson>& spawns, size_t max, double within) {
  if (spawns.empty()) return {};
  const SpawnJson& home = spawns[0];
  std::unordered_set<std::string> seen;
  struct Far {
    LessonArea area;
    double d;
  };
  std::vector<Far> out;
  for (const SpawnJson& s : spawns) {
    const std::string name = districtOf(s.name);
    const double d = jsHypot(s.x - home.x, s.z - home.z);
    if (name.empty() || seen.count(name) || d > within) continue;
    seen.insert(name);
    out.push_back({LessonArea{name, s.x, s.z}, d});
  }
  std::stable_sort(out.begin(), out.end(), [](const Far& a, const Far& b) { return a.d < b.d; });
  std::vector<LessonArea> areas;
  for (size_t i = 0; i < out.size() && i < max; i++) areas.push_back(out[i].area);
  return areas;
}

std::optional<LessonStart> lessonStart(const LessonDef& lesson, const LessonPlaceMode& mode, const LessonArea* area, const std::vector<SpawnJson>& spawns,
                                       const SpawnJson& current, const std::function<double()>& rnd) {
  const std::optional<PlaceKind> kind = placeKindOf(lesson);
  if (!kind || mode == "start") return std::nullopt;
  std::optional<LessonArea> centre;
  if (mode == "area") {
    if (area) centre = *area;
    else {
      const std::vector<LessonArea> first = areaChoices(spawns, 1);
      if (!first.empty()) centre = first[0];
    }
  } else {
    const std::vector<LessonArea> region = areaChoices(spawns, 200, HOME_REGION);
    if (!region.empty()) {
      const double r = rnd ? rnd() : fs::rnd();
      centre = region[static_cast<size_t>(std::min(static_cast<double>(region.size() - 1), std::floor(r * static_cast<double>(region.size()))))];
    }
  }
  if (!centre) return std::nullopt;
  // (the district's car park if it has one near its middle: a proper place to begin – else a road near the middle, once loaded)
  const SpawnJson* best = nullptr;
  double bd = 1500;
  for (const SpawnJson& s : spawns) {
    const double d = jsHypot(s.x - centre->x, s.z - centre->z);
    if (d < bd) {
      bd = d;
      best = &s;
    }
  }
  SpawnJson spawn;
  if (best) spawn = *best;
  else {
    spawn.name = centre->name;
    spawn.x = centre->x;
    spawn.z = centre->z;
    spawn.yaw = 0;
    spawn.kind = "street";
    spawn.rough = true;
  }
  return LessonStart{.spawn = spawn, .kind = *kind, .fallback = current};
}

namespace {

/** The rightmost lane arriving at the junction on this arm (null: none, or not usable). */
Lane* arriving(const LaneGraph& graph, const Arm& a, const std::function<bool(const Lane&)>& usable) {
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[a.edge->id];
  if (!el) return nullptr;
  // (the arm leaves the junction: traffic towards it drives from b to a)
  Lane* first = nullptr;
  for (Lane* l : a.out ? el->back : el->fwd) {
    if (!(l->to == a.node && usable(*l))) continue;
    if (l->idx == 0) return l;
    if (!first) first = l;
  }
  return first;
}

struct Spot {
  double x;
  double z;
  double yaw;
};

/** A spot `before` metres before the end of a lane (but not off its start). */
Spot spotOn(const Lane& lane, double before) {
  PolySample smp = newPolySample();
  const double d = std::max(std::min(lane.len - 30, lane.len - before), std::min(20.0, lane.len * 0.4));
  lanePoint(lane, d, smp);
  return {smp.x, smp.z, std::atan2(-smp.tx, -smp.tz)};
}

/** A public street, not a yard or a car park (narrow village streets are fine for the learner, if not for the traffic). */
bool street(const Edge& e) { return LaneGraph::drivable(e, false) && e.access == ACCESS_YES && e.tier >= 2 && !(e.flags & EF_MINOR_SERVICE); }

/** May a lesson begin on this road (a proper public street, not the motorway)? */
bool startRoad(const Edge& e) { return street(e) && e.cls > TRUNK_LINK && e.cls <= RESIDENTIAL; }

/** May the destination be this road (somewhere one can be "on", with a way out)? */
bool goalRoad(const LaneGraph& graph, const Edge& e) { return street(e) && e.len - e.cutA - e.cutB >= 25 && !deadEndSpur(graph, e); }

/** The junctions of a roundabout's ring, from one of them. */
std::vector<Node*> ringOf(Node& n) {
  std::vector<Node*> out = {&n};
  std::unordered_set<Node*> seen = {&n};
  for (size_t i = 0; i < out.size() && out.size() < 24; i++) {
    for (const Arm* a : out[i]->arms) {
      if (!(a->edge->flags & EF_ROUNDABOUT)) continue;
      Node* m = a->edge->a == out[i] ? a->edge->b : a->edge->a;
      if (!seen.count(m) && m->ctrl == CTRL_ROUNDABOUT) {
        seen.insert(m);
        out.push_back(m);
      }
    }
  }
  return out;
}

/** The way from the lane to the road: through the junction only (or round the ring), without turning round. */
bool shortWay(LaneGraph& graph, const Lane& lane, const Edge& goal, size_t most) {
  const std::vector<Connector*> r = graph.route(lane, goal, false);
  return !r.empty() && r.size() <= most && !std::any_of(r.begin(), r.end(), [](const Connector* c) { return c->turn == TURN_UTURN; });
}

struct Candidate {
  Lane* lane;
  Node* node;
  Edge* goal;
  double score;
  double before;
};

/** How the lane's way turns into road `e` at its end (TURN; -1 if it does not lead there directly). */
int turnOf(const LaneGraph& graph, const Lane& lane, const Edge& e) {
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[lane.edge->id];
  const std::vector<Lane*> own = {const_cast<Lane*>(&lane)};
  for (const Lane* l : el ? (lane.dir > 0 ? el->fwd : el->back) : own)
    for (const Connector* c : l->out)
      if (c->to->edge == &e) return c->turn;
  return -1;
}

/** what pickGoal found: a destination, none needed (the lesson finds its own) – or, `leads` false, nothing this approach leads to */
struct Goal {
  bool leads = false;
  Edge* edge = nullptr;
};

/**
 * The destination behind the place: a road leading away from the junction
 * (a turn rather than straight on where there is one), the country road at
 * the edge of town, the motorway behind the slip road.  `leads` false: this
 * approach does not lead to what the lesson is about.
 */
Goal pickGoal(World& world, LaneGraph& graph, const PlaceKind& kind, Node& n, const Arm& from, const Lane& lane, const std::function<double()>& rnd) {
  if (kind == "rural") {
    // a road of the open country leading away from here
    for (const Arm* b : n.arms) {
      Edge* e = b->edge;
      if (b == &from || !b->isExit || (e->flags & EF_URBAN) || e->tier < 3 || e->cls <= TRUNK_LINK || e->len < 180) continue;
      if (goalRoad(graph, *e) && shortWay(graph, lane, *e, 1)) return {true, e};
    }
    return {};
  }
  if (kind == "motorway") {
    // a slip road onto the motorway from here – and the motorway it leads to
    const Arm* link = nullptr;
    for (const Arm* b : n.arms) {
      if (b != &from && b->isExit && b->edge->cls == MOTORWAY_LINK) {
        link = b;
        break;
      }
    }
    if (!link || !shortWay(graph, lane, *link->edge, 1)) return {};
    struct Way {
      Edge* e;
      double d;
    };
    std::vector<Way> ways;
    for (Edge* e : world.allEdges) {
      if (e->cls != MOTORWAY || e->len < 250) continue;
      const double d = jsHypot(e->a->x - n.x, e->a->z - n.z);
      if (d < 2500) ways.push_back({e, d});
    }
    std::stable_sort(ways.begin(), ways.end(), [](const Way& p, const Way& q) { return p.d < q.d; });
    for (size_t i = 0; i < ways.size() && i < 8; i++) {
      Edge* e = ways[i].e;
      const std::vector<Connector*> r = graph.route(lane, *e, false);
      if (!r.empty() && r.size() <= 8 && r[0]->to->edge == link->edge && !std::any_of(r.begin(), r.end(), [](const Connector* c) { return c->turn == TURN_UTURN; })) return {true, e};
    }
    return {};
  }
  // a junction: through it to one of its other roads – round the ring to an exit of the roundabout
  const std::vector<Node*> nodes = kind == "roundabout" ? ringOf(n) : std::vector<Node*>{&n};
  std::vector<Edge*> exits;
  for (const Node* m : nodes) {
    for (const Arm* b : m->arms) {
      Edge* e = b->edge;
      if (b == &from || e == from.edge || !b->isExit || (e->flags & EF_ROUNDABOUT) || e->cls <= TRUNK_LINK) continue;
      if (goalRoad(graph, *e)) exits.push_back(e);
    }
  }
  // (shuffled, turns first: the lesson is about the junction, and turning shows more of it)
  struct Order {
    Edge* e;
    double k;
  };
  std::vector<Order> order;
  for (Edge* e : exits) {
    // (the chance first, as the original draws it: before it looks which way the road turns)
    const double r = rnd();
    order.push_back({e, r + (kind != "roundabout" && turnOf(graph, lane, *e) == TURN_STRAIGHT ? 1 : 0)});
  }
  std::stable_sort(order.begin(), order.end(), [](const Order& p, const Order& q) { return p.k < q.k; });
  for (const Order& o : order)
    if (shortWay(graph, lane, *o.e, kind == "roundabout" ? nodes.size() + 2 : 1)) return {true, o.e};
  return {};
}

}  // namespace

std::optional<LessonSpot> findLessonPlace(World& world, LaneGraph& graph, const PlaceKind& kind, double x, double z, const PlaceOptions& opt) {
  const double radius = opt.radius ? *opt.radius : searchRadius(kind);
  const std::function<double()> rnd = opt.rnd ? opt.rnd : std::function<double()>([] { return fs::rnd(); });
  const std::function<bool(const Lane&)> usable = opt.usable ? opt.usable : std::function<bool(const Lane&)>([](const Lane&) { return true; });
  const auto near = [x, z](double px, double pz) { return jsHypot(px - x, pz - z); };
  std::vector<Candidate> cands;
  // (each candidate a little shuffled: not always the same place nearest the middle)
  const auto score = [&rnd](double d, double room) { return d + rnd() * 500 - std::min(room, LEAD_IN) * 2; };

  if (kind == "quiet") {
    // a quiet, straight, level street of a built-up area, long enough to get to 30 km/h and stop again
    for (Edge* e : world.allEdges) {
      if (e->cls != RESIDENTIAL && e->cls != UNCLASSIFIED) continue;
      if (!(e->flags & EF_URBAN) || (e->flags & (EF_MINOR_SERVICE | EF_BRIDGE | EF_TUNNEL)) || !startRoad(*e)) continue;
      const double len = e->len - e->cutA - e->cutB;
      if (len < 180) continue;
      const std::vector<double>& p = e->pl.pts;
      const size_t k = p.size() - 3;
      const double d = near((p[0] + p[k]) / 2, (p[2] + p[k + 2]) / 2);
      if (d > radius) continue;
      if (jsHypot(p[k] - p[0], p[k + 2] - p[2]) < 0.97 * e->len) continue;
      if (std::fabs(p[k + 1] - p[1]) > 0.04 * e->len) continue;
      const LaneGraph::EdgeLanes* el = graph.edgeLanes[e->id];
      Lane* lane = nullptr;
      if (el) {
        for (const std::vector<Lane*>* side : {&el->fwd, &el->back}) {
          for (Lane* l : *side) {
            if (usable(*l) && l->idx == 0) {
              lane = l;
              break;
            }
          }
          if (lane) break;
        }
      }
      if (!lane) continue;
      cands.push_back({lane, nullptr, nullptr, d + rnd() * 500, lane->len - 20});
    }
  } else if (kind != "area") {
    for (Node* n : world.allNodes) {
      if (n->stub || n->arms.size() < 2) continue;
      const double d = near(n->x, n->z);
      if (d > radius) continue;
      if (kind == "rural" || kind == "motorway") {
        if (n->arms.size() < 2) continue;
      } else if (!junctionOfType(*n, kind)) continue;
      for (const Arm* a : n->arms) {
        if (!a->isApproach || !startRoad(*a->edge)) continue;
        if (kind == "roundabout" && (a->edge->flags & EF_ROUNDABOUT)) continue;
        // the priority lesson is about giving way: from the road with the sign
        if (kind == "priority" && a->role != ROLE_YIELD && a->role != ROLE_STOP) continue;
        if (kind == "signals" && a->sg < 0 && a->lamp < 0) continue;
        Lane* lane = arriving(graph, *a, usable);
        if (!lane || lane->len < 45) continue;
        const Goal goal = pickGoal(world, graph, kind, *n, *a, *lane, rnd);
        if (!goal.leads) continue;
        // (towards the country road from the town, if there is such a place: out of town is the lesson)
        cands.push_back({lane, n, goal.edge, score(d, lane->len) + (kind == "rural" && !(a->edge->flags & EF_URBAN) ? 600 : 0), LEAD_IN});
      }
    }
  }
  if (cands.empty()) return std::nullopt;
  std::stable_sort(cands.begin(), cands.end(), [](const Candidate& p, const Candidate& q) { return p.score < q.score; });
  const Candidate& c = cands[0];
  const Spot at = spotOn(*c.lane, c.before);
  LessonSpot spot;
  spot.x = at.x;
  spot.z = at.z;
  spot.yaw = at.yaw;
  spot.name = !c.lane->edge->name.empty() ? c.lane->edge->name : c.lane->edge->ref;
  spot.goal = c.goal;
  spot.node = c.node;
  return spot;
}

}  // namespace fs
