// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Route guidance and the lesson / exam definitions.
 *
 * Origin: src/game/nav.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#include "game/nav.h"

#include <algorithm>
#include <map>
#include <cmath>

namespace fs {

void Navigator::clear() {
  route.clear();
  goal = nullptr;
  routeLanes.clear();
  hint.what = "navFollow";
  hint.dist = -1;
  hint.lane = 0;
  hint.done = false;
}

bool Navigator::plan(Lane& from) {
  if (!goal) return false;
  if (from.edge == goal) {
    route.clear();
    routeLanes = {&from};
    idx_ = 0;
    return true;
  }
  std::vector<Connector*> r = graph.route(from, *goal, false);
  if (r.empty() && from.edge != goal) return false;
  route = r;
  idx_ = 0;
  routeLanes.clear();
  routeLanes.push_back(&from);
  for (Connector* c : route) routeLanes.push_back(c->to);
  return true;
}

bool Navigator::setGoal(Edge* goal_, Lane* from) {
  goal = goal_;
  hint.done = false;
  if (!from) {
    route.clear();
    return false;
  }
  return plan(*from);
}

NavHint& Navigator::update(double dt, Lane* lane, double s, double x, double z) {
  NavHint& h = hint;
  if (!goal) return h;
  if (!lane) return h;
  bool arrived = lane->edge == goal;
  if (!arrived && !std::isnan(x)) {
    // Being on the destination road counts as well, whichever lane the monitor
    // has us on – but only beyond its junctions: the road's centre line starts
    // in the middle of the junction, where the turn into it is still under way.
    const Edge& g = *goal;
    if (x > g.minX - 6 && x < g.maxX + 6 && z > g.minZ - 6 && z < g.maxZ + 6) {
      proj_ = projectOnPolyline(g.pl, x, z, proj_);
      double reach = g.width / 2 + 1.5;
      arrived = proj_.d2 < reach * reach && proj_.s > g.cutA + 4 &&
                proj_.s < g.len - g.cutB - 4;
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
  if (route.empty()) {
    plan(*lane);
    return h;
  }
  // where are we on the route?
  int found = -1;
  for (int i = std::max(0, idx_ - 1); i < static_cast<int>(route.size()); i++) {
    Connector* c = route[static_cast<size_t>(i)];
    if (c->from == lane || (c->from->edge == lane->edge && c->from->dir == lane->dir)) {
      found = i;
      break;
    }
  }
  if (found < 0) {
    offRoute_ += dt;
    if (offRoute_ > 1.5) {
      offRoute_ = 0;
      plan(*lane);
    }
    return h;
  }
  offRoute_ = 0;
  idx_ = found;
  // Einordnen: the route continues from a particular lane of this carriageway
  Lane* want = route[static_cast<size_t>(found)]->from;
  h.lane = 0;
  if (want != lane && want->edge == lane->edge)
    h.lane = (want->off - lane->off) * lane->dir > 0 ? -1 : 1;
  // next real manoeuvre
  double dist = std::max(0.0, lane->len - s);
  h.what = "navFollow";
  h.dist = -1;
  h.arrow = 0;
  h.n = 0;
  h.lane = 0;
  for (int i = found; i < static_cast<int>(route.size()); i++) {
    Connector* c = route[static_cast<size_t>(i)];
    Node* node = c->node;
    bool entersRing = node->ctrl == CTRL_ROUNDABOUT && (c->to->edge->flags & EF_ROUNDABOUT) != 0 &&
                      (c->from->edge->flags & EF_ROUNDABOUT) == 0;
    if (entersRing) {
      // count the exits we pass until the route leaves the ring
      int n = 0;
      for (size_t j = static_cast<size_t>(i) + 1; j < route.size(); j++) {
        Connector* cj = route[j];
        if ((cj->from->edge->flags & EF_ROUNDABOUT) == 0) break;
        bool hasExit = false;
        for (const auto& a : cj->node->arms)
          if (a.isExit && (a.edge->flags & EF_ROUNDABOUT) == 0) hasExit = true;
        if (hasExit) n++;
        if ((cj->to->edge->flags & EF_ROUNDABOUT) == 0) break;
      }
      h.what = "navRoundabout";
      h.n = std::max(1, n);
      h.dist = dist;
      h.arrow = 1;
      return h;
    }
    bool real = node->arms.size() >= 3 && (c->from->edge->flags & EF_ROUNDABOUT) == 0;
    if (real && c->turn != TURN_STRAIGHT) {
      h.what = c->turn == TURN_LEFT ? "navLeft" : (c->turn == TURN_RIGHT ? "navRight" : "navUturn");
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

// ---------------------------------------------------------------------------
// Lessons
// ---------------------------------------------------------------------------

const std::vector<LessonDef>& lessons() {
  static const std::vector<LessonDef> list = {
      {"basics",
       "basics",
       {}},
      {"rbl",
       "route",
       {TargetType::Rbl, TargetType::Rbl, TargetType::Home}},
      {"priority",
       "route",
       {TargetType::Priority, TargetType::Priority, TargetType::Home}},
      {"signals",
       "route",
       {TargetType::Signals, TargetType::Signals, TargetType::Home}},
      {"roundabout",
       "route",
       {TargetType::Roundabout, TargetType::Roundabout, TargetType::Home}},
      {"rural",
       "route",
       {TargetType::Rural, TargetType::Home}},
      {"motorway",
       "route",
       {TargetType::Motorway, TargetType::Home}},
      {"exam",
       "exam",
       {TargetType::Rbl, TargetType::Signals, TargetType::Roundabout, TargetType::Priority,
        TargetType::Rural, TargetType::Home}},
  };
  return list;
}

/**
 * Is `e` part of a spur without a way out – a road that only leads to the
 * edge of the map or into a dead end, in either direction of travel?
 * Whoever is sent there has to turn round (or cannot get back at all).
 */
static bool deadEndSpur(LaneGraph& graph, const Edge& e) {
  const LaneGraph::EdgeLanes& el = graph.edgeLanes[static_cast<size_t>(e.id)];
  Lane* firsts[2] = {el.fwd.empty() ? nullptr : el.fwd[0],
                     el.back.empty() ? nullptr : el.back[0]};
  for (Lane* first : firsts) {
    if (!first) continue;
    Lane* lane = first;
    double dist = 0;
    for (int hop = 0; hop < 60 && dist < 5000; hop++) {
      // the roads one can carry on to from this carriageway (not counting turning round)
      const LaneGraph::EdgeLanes& lanes = graph.edgeLanes[static_cast<size_t>(lane->edge->id)];
      const std::vector<Lane*>& sameDir = lane->dir > 0 ? lanes.fwd : lanes.back;
      std::map<Edge*, Lane*> onward;
      for (Lane* l : sameDir) {
        for (Connector* c : l->out) {
          if (c->turn != TURN_UTURN && c->to->edge != lane->edge &&
              LaneGraph::drivable(*c->to->edge, true))
            onward[c->to->edge] = c->to;
        }
      }
      if (onward.empty()) return true;
      if (onward.size() > 1) break;
      lane = onward.begin()->second;
      dist += lane->len;
    }
  }
  return false;
}

Edge* findTarget(World& world, LaneGraph& graph, TargetType type, double x, double z,
                 Lane& from, Edge* home, const std::set<Edge*>& avoid) {
  if (type == TargetType::Home) return home;
  std::vector<std::pair<Edge*, double>> cands;
  auto push = [&](Edge* e, double px, double pz) {
    if (avoid.count(e) || !LaneGraph::drivable(*e, true)) return;
    // the destination must be a stretch of road one can actually be "on"
    if (e->len - e->cutA - e->cutB < 25) return;
    if (deadEndSpur(graph, *e)) return;
    double d = std::hypot(px - x, pz - z);
    if (d > 350 && d < 2600) cands.push_back({e, d});
  };
  if (type == TargetType::Rural) {
    for (auto& e : world.edges())
      if ((e.flags & EF_URBAN) == 0 && e.tier >= 3 && e.cls > TRUNK_LINK && e.len > 180)
        push(&e, e.a->x, e.a->z);
  } else if (type == TargetType::Motorway) {
    for (auto& e : world.edges())
      if (e.cls == MOTORWAY && e.len > 250)
        cands.push_back({&e, std::hypot(e.a->x - x, e.a->z - z)});
  } else {
    for (auto& n : world.nodes()) {
      if (n.arms.size() < 3) continue;
      bool ok = false;
      if (type == TargetType::Rbl) {
        ok = n.ctrl == CTRL_RBL;
        if (ok)
          for (const auto& a : n.arms)
            if (a.edge->tier != 2) {
              ok = false;
              break;
            }
      } else if (type == TargetType::Signals) {
        ok = n.ctrl == CTRL_SIGNALS;
      } else if (type == TargetType::Roundabout) {
        ok = n.ctrl == CTRL_ROUNDABOUT;
      } else if (type == TargetType::Priority) {
        ok = n.ctrl == CTRL_SIGNS;
        if (ok) {
          ok = false;
          for (const auto& a : n.arms)
            if (a.isApproach && (a.role == ROLE_YIELD || a.role == ROLE_STOP) && a.edge->tier >= 2) {
              ok = true;
              break;
            }
        }
      }
      if (!ok) continue;
      // drive through the junction: aim at an arm leading away from it
      for (const auto& a : n.arms) {
        if (!a.isExit) continue;
        if (type == TargetType::Roundabout && (a.edge->flags & EF_ROUNDABOUT) != 0) continue;
        push(a.edge, n.x, n.z);
      }
    }
  }
  std::stable_sort(cands.begin(), cands.end(),
                   [](const auto& p, const auto& q) { return p.second < q.second; });
  // prefer something a few hundred metres away, and make sure it can be reached
  size_t start = std::min(cands.size() - 1, static_cast<size_t>(cands.size() * 0.15));
  std::vector<std::pair<Edge*, double>> order;
  order.insert(order.end(), cands.begin() + static_cast<long>(start), cands.end());
  order.insert(order.end(), cands.begin(), cands.begin() + static_cast<long>(start));
  int tries = 0;
  for (const auto& c : order) {
    if (++tries > 12) break;
    if (!graph.route(from, *c.first, false).empty()) return c.first;
  }
  return nullptr;
}

}  // namespace fs
