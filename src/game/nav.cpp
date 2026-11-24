// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/nav.ts @ v1.7.7
#include "game/nav.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "world/types.h"

namespace fs {

namespace {

const double INF = std::numeric_limits<double>::infinity();

/** `list.indexOf(lane)` */
int indexOf(const std::vector<Lane*>& list, const Lane* lane) {
  for (size_t i = 0; i < list.size(); i++)
    if (list[i] == lane) return static_cast<int>(i);
  return -1;
}

const std::vector<Lane*> NO_LANES;

/** the lanes of a way (none where its road is not loaded) */
const std::vector<Lane*>& lanesOf(const LaneGraph& graph, int w) {
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[w >> 1];
  if (!el) return NO_LANES;
  return (w & 1) == 0 ? el->fwd : el->back;
}

struct Through {
  Connector* c = nullptr;
  double cost = 0;
};

}  // namespace

std::optional<std::vector<Connector*>> connectorsAlong(const LaneGraph& graph, const std::vector<int>& ways, const Lane& from) {
  std::vector<const std::vector<Lane*>*> layers;
  layers.push_back(&lanesOf(graph, wayOf(from)));
  for (int w : ways) layers.push_back(&lanesOf(graph, w));
  const size_t n = layers.size();
  for (const auto* l : layers)
    if (l->empty()) return std::nullopt;
  /** what it costs from each lane of a road to the end (changes of lane, mostly) */
  std::vector<double> onward(layers[n - 1]->size(), 0.0);
  std::vector<std::vector<std::optional<Through>>> through(n - 1);
  // (changing lanes on a short piece between two junctions is hard: better be in the right one before)
  const auto change = [&layers](size_t i) { return (*layers[i])[0]->len < 30 ? 3.0 : 1.0; };
  for (size_t k = n - 1; k-- > 0;) {
    const size_t i = k;
    const std::vector<Lane*>& cur = *layers[i];
    const std::vector<Lane*>& tgt = *layers[i + 1];
    const std::vector<double> next = onward;
    bool any = false;
    through[i].resize(cur.size());
    for (size_t a = 0; a < cur.size(); a++) {
      Connector* best = nullptr;
      double bc = INF;
      for (Connector* c : cur[a]->out) {
        const int j = indexOf(tgt, c->to);
        if (j < 0) continue;
        if (next[static_cast<size_t>(j)] < bc) {
          bc = next[static_cast<size_t>(j)];
          best = c;
        }
      }
      if (best) {
        through[i][a] = Through{best, bc};
        any = true;
      }
    }
    if (!any) return std::nullopt;
    onward.assign(cur.size(), 0.0);
    for (size_t a = 0; a < cur.size(); a++) {
      double m = INF;
      for (size_t b = 0; b < through[i].size(); b++) {
        const auto& t = through[i][b];
        if (t) m = std::min(m, t->cost + std::fabs(static_cast<double>(a) - static_cast<double>(b)) * change(i));
      }
      onward[a] = m;
    }
  }
  std::vector<Connector*> out;
  int at = std::max(0, indexOf(*layers[0], &from));
  for (size_t i = 0; i + 1 < n; i++) {
    int best = -1;
    double bc = INF;
    for (size_t b = 0; b < through[i].size(); b++) {
      const auto& t = through[i][b];
      if (!t) continue;
      const double cost = t->cost + std::fabs(static_cast<double>(at) - static_cast<double>(b)) * change(i);
      if (cost < bc) {
        bc = cost;
        best = static_cast<int>(b);
      }
    }
    Connector* c = through[i][static_cast<size_t>(best)]->c;
    out.push_back(c);
    at = indexOf(*layers[i + 1], c->to);
  }
  return out;
}

Navigator::Navigator(LaneGraph& graphIn) : graph(graphIn) {}

void Navigator::setRoute(std::optional<std::vector<Connector*>> r) {
  route = std::move(r);
  routes++;
}

void Navigator::setRouteLanes(std::vector<Lane*> lanes) { routeLanes = std::make_shared<const std::vector<Lane*>>(std::move(lanes)); }

void Navigator::clear() {
  setRoute(std::nullopt);
  goal = nullptr;
  fixed = false;
  ways.reset();
  from = nullptr;
  expanded = 0;
  setRouteLanes({});
  hint.what = "navFollow";
  hint.dist = -1;
  hint.lane = 0;
  hint.done = false;
}

int Navigator::left() const {
  if (!route) return 0;
  // (of a way given as ways, more may follow than is known as lanes yet)
  return static_cast<int>(route->size()) - idx + (ways && expanded < ways->size() ? 1000 : 0);
}

void Navigator::setPath(const std::vector<Connector*>& path, Lane& fromLane) {
  fixed = true;
  setRoute(path);
  goal = !path.empty() ? path.back()->to->edge : fromLane.edge;
  idx = 0;
  offRoute = 0;
  hint.done = false;
  std::vector<Lane*> lanes = {&fromLane};
  for (Connector* c : path) lanes.push_back(c->to);
  setRouteLanes(std::move(lanes));
}

void Navigator::setWays(const std::vector<int>& waysIn, Lane& fromLane, bool tourIn) {
  // (a copy first: the list handed in may be this one's own)
  std::vector<int> copy = waysIn;
  ways = std::move(copy);
  tour = tourIn;
  from = &fromLane;
  fixed = false;
  goal = nullptr;
  setRoute(std::vector<Connector*>{});
  setRouteLanes({&fromLane});
  expanded = 0;
  idx = 0;
  offRoute = 0;
  hint.done = false;
  extend();
}

void Navigator::extend() {
  if (!ways || !route || !from || expanded >= ways->size()) return;
  const std::vector<int>& w = *ways;
  const Lane& last = !route->empty() ? *route->back()->to : *from;
  size_t n = 0;
  while (expanded + n < w.size() && n < 80) {
    if (lanesOf(graph, w[expanded + n]).empty()) break;
    n++;
  }
  // (the junctions at the rim of what is loaded are not whole yet: then a little less far)
  for (size_t step = 1; n > 0; n = n > step ? n - step : 0, step *= 2) {
    const std::optional<std::vector<Connector*>> part =
        connectorsAlong(graph, std::vector<int>(w.begin() + static_cast<std::ptrdiff_t>(expanded), w.begin() + static_cast<std::ptrdiff_t>(expanded + n)), last);
    if (!part) continue;
    // (a new list: whoever draws the route sees that it changed)
    std::vector<Lane*> lanes = *routeLanes;
    for (Connector* c : *part) {
      route->push_back(c);
      lanes.push_back(c->to);
    }
    expanded += n;
    setRouteLanes(std::move(lanes));
    return;
  }
}

void Navigator::rejoinWays(Lane& lane) {
  const std::vector<int>& w = *ways;
  if (!net || w.empty()) return;
  const int here = wayOf(lane);
  std::optional<std::vector<int>> next;
  if (tour) {
    double bestCost = INF;
    for (size_t j = std::min(w.size() - 1, static_cast<size_t>(idx + 1)), n = 0; j < w.size() && n < 10; j++, n++) {
      if (w[j] == here) {
        next = std::vector<int>(w.begin() + static_cast<std::ptrdiff_t>(j + 1), w.end());
        break;
      }
      const std::optional<std::vector<int>> lead = quickest(*net, here, w[j]);
      if (!lead) continue;
      double cost = static_cast<double>(n) * 150;
      for (int v : *lead) cost += static_cast<double>(net->len[static_cast<size_t>(v)]);
      if (cost < bestCost) {
        bestCost = cost;
        next = *lead;
        next->insert(next->end(), w.begin() + static_cast<std::ptrdiff_t>(j + 1), w.end());
      }
    }
  }
  // (no way back onto the tour, or no tour at all: the quickest way to where it ends)
  if (!next) next = quickestToEdge(*net, here, w.back() >> 1);
  if (!next) return;
  setWays(*next, lane, tour);
}

void Navigator::rejoin(Lane& lane) {
  const std::vector<Connector*>& r = *route;
  std::optional<std::vector<Connector*>> best;
  int bestAt = -1;
  double bestCost = INF;
  for (int i = std::min(static_cast<int>(r.size()) - 1, idx + 1), n = 0; i >= 0 && i < static_cast<int>(r.size()) && n < 10; i++, n++) {
    const Lane* target = r[static_cast<size_t>(i)]->from;
    if (target->edge == lane.edge && target->dir == lane.dir) {
      best = std::vector<Connector*>{};
      bestAt = i;
      break;
    }
    std::vector<Connector*> p = graph.route(lane, *target->edge, false, target->dir);
    if (p.empty()) continue;
    double cost = n * 150;
    for (const Connector* c : p) cost += c->len + c->to->len + (c->turn == TURN_UTURN ? 2000 : 0);
    if (cost < bestCost) {
      bestCost = cost;
      best = std::move(p);
      bestAt = i;
    }
  }
  if (!best) {
    // no way back onto it: straight to where it ends
    fixed = false;
    plan(lane);
    return;
  }
  std::vector<Connector*> joined = std::move(*best);
  joined.insert(joined.end(), r.begin() + bestAt, r.end());
  setRoute(std::move(joined));
  idx = 0;
  std::vector<Lane*> lanes = {&lane};
  for (Connector* c : *route) lanes.push_back(c->to);
  setRouteLanes(std::move(lanes));
}

bool Navigator::setGoal(Edge* goalIn, Lane* fromLane) {
  goal = goalIn;
  fixed = false;
  hint.done = false;
  if (!fromLane) {
    setRoute(std::nullopt);
    return false;
  }
  return plan(*fromLane);
}

bool Navigator::plan(Lane& fromLane) {
  if (!goal) return false;
  if (fromLane.edge == goal) {
    setRoute(std::vector<Connector*>{});
    setRouteLanes({&fromLane});
    idx = 0;
    return true;
  }
  std::vector<Connector*> r = graph.route(fromLane, *goal, false);
  if (r.empty()) return false;
  std::vector<Lane*> lanes = {&fromLane};
  for (Connector* c : r) lanes.push_back(c->to);
  setRoute(std::move(r));
  idx = 0;
  setRouteLanes(std::move(lanes));
  return true;
}

const NavHint& Navigator::update(double dt, Lane* lane, double s, double x, double z) {
  NavHint& h = hint;
  if (!busy()) return h;
  if (!lane) return h;
  // more of a way that was given as ways, as the car gets on and the map is loaded ahead
  if (ways && route && expanded < ways->size() && static_cast<int>(route->size()) - idx < 12) extend();
  bool arrived = lane->edge == goal;
  if (ways) {
    static const std::vector<Connector*> none;
    const std::vector<Connector*>& r = route ? *route : none;
    const bool all = expanded >= ways->size();
    const Lane* tail = !r.empty() ? r.back()->to : nullptr;
    arrived = all && (tail == nullptr ? ways->empty() : idx >= static_cast<int>(r.size()) - 1 && lane->edge == tail->edge && lane->dir == tail->dir);
  } else if (fixed) {
    // a tour is over when its last road is reached by its last junction – it may have led along that road before
    static const std::vector<Connector*> none;
    const std::vector<Connector*>& r = route ? *route : none;
    const Lane* tail = !r.empty() ? r.back()->to : nullptr;
    arrived = tail == nullptr || (idx >= static_cast<int>(r.size()) - 1 && lane->edge == tail->edge && lane->dir == tail->dir);
  } else if (!arrived && !std::isnan(x)) {
    // Being on the destination road counts as well, whichever lane the monitor
    // has us on – but only beyond its junctions: the road's centre line starts
    // in the middle of the junction, where the turn into it is still under way.
    const Edge& g = *goal;
    if (x > g.minX - 6 && x < g.maxX + 6 && z > g.minZ - 6 && z < g.maxZ + 6) {
      projectOnPolyline(g.pl, x, z, proj);
      const double reach = g.width / 2 + 1.5;
      arrived = proj.d2 < reach * reach && proj.s > g.cutA + 4 && proj.s < g.len - g.cutB - 4;
    }
  }
  if (arrived) {
    h.done = true;
    h.what = "routeDone";
    h.dist = -1;
    h.arrow = 0;
    h.lane = 0;
    return h;
  }
  if (!route) {
    plan(*lane);
    return h;
  }
  const std::vector<Connector*>& r = *route;
  // where are we on the route?
  int found = -1;
  for (int i = std::max(0, idx - 1); i < static_cast<int>(r.size()); i++) {
    const Lane* f = r[static_cast<size_t>(i)]->from;
    if (f == lane || (f->edge == lane->edge && f->dir == lane->dir)) {
      found = i;
      break;
    }
  }
  if (found < 0) {
    // (on the last road of what is known so far – or nothing is known yet: the map ahead is still being loaded)
    const Lane* end = !r.empty() ? r.back()->to : from;
    const bool waiting = ways.has_value() && expanded < ways->size() && end != nullptr && lane->edge == end->edge && lane->dir == end->dir;
    if (!waiting) offRoute += dt;
    if (offRoute > retry) {
      offRoute = 0;
      const int before = routes;
      if (ways) rejoinWays(*lane);
      else if (fixed) rejoin(*lane);
      else plan(*lane);
      // (No way from here: asked again, but not every second – the search runs through the
      // roads of the whole map, and whoever is off the way stays so for a while.)
      retry = routes == before ? std::min(20.0, retry * 2.5) : 1.5;
    }
    return h;
  }
  offRoute = 0;
  retry = 1.5;
  idx = found;
  // Einordnen: the route continues from a particular lane of this carriageway
  const Lane* want = r[static_cast<size_t>(found)]->from;
  h.lane = want != lane && want->edge == lane->edge ? ((want->off - lane->off) * lane->dir > 0 ? -1 : 1) : 0;
  // next real manoeuvre
  double dist = std::max(0.0, lane->len - s);
  h.what = "navFollow";
  h.dist = -1;
  h.arrow = 0;
  h.n = 0;
  for (size_t i = static_cast<size_t>(found); i < r.size(); i++) {
    const Connector* c = r[i];
    const Node* node = c->node;
    const bool entersRing = node->ctrl == CTRL_ROUNDABOUT && (c->to->edge->flags & EF_ROUNDABOUT) != 0 && !(c->from->edge->flags & EF_ROUNDABOUT);
    if (entersRing) {
      // count the exits we pass until the route leaves the ring
      int n = 0;
      for (size_t j = i + 1; j < r.size(); j++) {
        const Connector* cj = r[j];
        if (!(cj->from->edge->flags & EF_ROUNDABOUT)) break;
        const bool hasExit = std::any_of(cj->node->arms.begin(), cj->node->arms.end(), [](const Arm* a) { return a->isExit && !(a->edge->flags & EF_ROUNDABOUT); });
        if (hasExit) n++;
        if (!(cj->to->edge->flags & EF_ROUNDABOUT)) break;
      }
      h.what = "navRoundabout";
      h.n = std::max(1, n);
      h.dist = dist;
      h.arrow = 1;
      return h;
    }
    const bool real = node->arms.size() >= 3 && !(c->from->edge->flags & EF_ROUNDABOUT);
    if (real && c->turn != TURN_STRAIGHT) {
      h.what = c->turn == TURN_LEFT ? "navLeft" : c->turn == TURN_RIGHT ? "navRight" : "navUturn";
      h.arrow = c->turn == TURN_RIGHT ? 1 : -1;
      h.dist = dist;
      return h;
    }
    if (real && c->to->edge->cls == MOTORWAY_LINK && c->from->edge->cls > TRUNK_LINK) {
      h.what = "navMotorway";
      h.dist = dist;
      h.arrow = c->turn == TURN_LEFT ? -1 : 1;
      return h;
    }
    dist += c->len + c->to->len;
    if (dist > 1500) break;
  }
  return h;
}

bool deadEndSpur(const LaneGraph& graph, const Edge& e) {
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[e.id];
  if (!el) return false;
  for (const Lane* first : {el->fwd.empty() ? nullptr : el->fwd[0], el->back.empty() ? nullptr : el->back[0]}) {
    if (!first) continue;
    const Lane* lane = first;
    double dist = 0;
    for (int hop = 0; hop < 60 && dist < 5000; hop++) {
      // the roads one can carry on to from this carriageway (not counting turning round)
      const LaneGraph::EdgeLanes* lanes = graph.edgeLanes[lane->edge->id];
      if (!lanes) return true;
      /** (the roads in the order they were found; of each the lane found last) */
      std::vector<std::pair<const Edge*, const Lane*>> onward;
      for (const Lane* l : lane->dir > 0 ? lanes->fwd : lanes->back) {
        for (const Connector* c : l->out) {
          if (c->turn != TURN_UTURN && c->to->edge != lane->edge && LaneGraph::drivable(*c->to->edge, true)) {
            auto known = std::find_if(onward.begin(), onward.end(), [c](const auto& o) { return o.first == c->to->edge; });
            if (known != onward.end()) known->second = c->to;
            else onward.emplace_back(c->to->edge, c->to);
          }
        }
      }
      if (onward.empty()) return true;
      if (onward.size() > 1) break;
      lane = onward[0].second;
      dist += lane->len;
    }
  }
  return false;
}

bool junctionOfType(const Node& n, const TargetType& type) {
  if (n.stub || n.arms.size() < 3) return false;
  if (type == "rbl") return n.ctrl == CTRL_RBL && std::all_of(n.arms.begin(), n.arms.end(), [](const Arm* a) { return a->edge->tier == 2; });
  if (type == "signals") return n.ctrl == CTRL_SIGNALS;
  if (type == "roundabout") return n.ctrl == CTRL_ROUNDABOUT;
  if (type == "priority")
    return n.ctrl == CTRL_SIGNS && std::any_of(n.arms.begin(), n.arms.end(), [](const Arm* a) { return a->isApproach && (a->role == ROLE_YIELD || a->role == ROLE_STOP) && a->edge->tier >= 2; });
  return false;
}

Edge* findTarget(World& world, LaneGraph& graph, const TargetType& type, double x, double z, const Lane& from, Edge* home, const std::unordered_set<Edge*>& avoid) {
  if (type == "home") return home;
  struct Cand {
    Edge* e;
    double d;
  };
  std::vector<Cand> cands;
  const auto push = [&](Edge* e, double px, double pz) {
    if (avoid.count(e) || !LaneGraph::drivable(*e, true)) return;
    // the destination must be a stretch of road one can actually be "on"
    if (e->len - e->cutA - e->cutB < 25) return;
    const double d = jsHypot(px - x, pz - z);
    if (d > 350 && d < 2600) cands.push_back({e, d});
  };
  if (type == "rural") {
    for (Edge* e : world.allEdges)
      if (!(e->flags & EF_URBAN) && e->tier >= 3 && e->cls > TRUNK_LINK && e->len > 180) push(e, e->a->x, e->a->z);
  } else if (type == "motorway") {
    for (Edge* e : world.allEdges)
      if (e->cls == MOTORWAY && e->len > 250) cands.push_back({e, jsHypot(e->a->x - x, e->a->z - z)});
  } else {
    for (Node* n : world.allNodes) {
      if (!junctionOfType(*n, type)) continue;
      // drive through the junction: aim at an arm leading away from it
      for (const Arm* a : n->arms) {
        if (!a->isExit) continue;
        if (type == "roundabout" && (a->edge->flags & EF_ROUNDABOUT)) continue;
        push(a->edge, n->x, n->z);
      }
    }
  }
  std::stable_sort(cands.begin(), cands.end(), [](const Cand& p, const Cand& q) { return p.d < q.d; });
  // prefer something a few hundred metres away, and make sure it can be reached
  std::vector<Cand> order;
  if (!cands.empty()) {
    const size_t start = std::min(cands.size() - 1, static_cast<size_t>(std::floor(static_cast<double>(cands.size()) * 0.15)));
    order.insert(order.end(), cands.begin() + static_cast<std::ptrdiff_t>(start), cands.end());
    order.insert(order.end(), cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(start));
  }
  // Destinations that can only be reached or left by turning round are a last resort.
  int tries = 0;
  Edge* fallback = nullptr;
  for (const Cand& c : order) {
    if (deadEndSpur(graph, *c.e)) continue;
    if (++tries > 16) break;
    const std::vector<Connector*> r = graph.route(from, *c.e, false);
    if (r.empty()) continue;
    if (!std::any_of(r.begin(), r.end(), [](const Connector* k) { return k->turn == TURN_UTURN; })) return c.e;
    if (!fallback) fallback = c.e;
  }
  return fallback;
}

}  // namespace fs
