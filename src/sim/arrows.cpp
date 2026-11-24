// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/arrows.ts @ v1.7.7
#include "sim/arrows.h"

#include <algorithm>
#include <set>

#include "core/geom.h"
#include "core/js.h"
#include "world/types.h"

namespace fs {

namespace {

/** an arrow must be lit for at least this long in every cycle (s) */
const double WORTH = 8;
/** a pedestrian light this near the junction belongs to it (m along the road) */
const double NEAR_ = 60;
/** junctions of one controller this near each other are parts of one big junction (m along the road) */
const double PART = 70;

/** The junctions of the same controller a few metres along the roads from `node` (through mere joints). */
std::vector<Node*> partners(Node& node) {
  std::vector<Node*> out;
  for (Arm* armAt : node.arms) {
    Arm& arm = *armAt;
    Edge* edge = arm.edge;
    Node* at = &node;
    double dist = 0;
    for (int hop = 0; hop < 6; hop++) {
      dist += edge->len;
      if (dist > PART) break;
      Node* far = edge->a == at ? edge->b : edge->a;
      if (far == &node) break;
      if (far->sc == node.sc) {
        out.push_back(far);
        break;
      }
      if (far->stub || far->arms.size() != 2) break;
      Arm* on = nullptr;
      for (Arm* a : far->arms) {
        if (a->edge != edge) {
          on = a;
          break;
        }
      }
      if (!on) break;
      at = far;
      edge = on->edge;
    }
  }
  return out;
}

/** The lanes that arrive at a junction on an approach. */
std::vector<Lane*> lanesOn(LaneGraph& graph, const Arm& arm) {
  std::vector<Lane*> out;
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[arm.edge->id];
  if (!el) return out;
  for (Lane* l : el->fwd)
    if (l->armTo == &arm) out.push_back(l);
  for (Lane* l : el->back)
    if (l->armTo == &arm) out.push_back(l);
  return out;
}

struct Near {
  double dist;
  int sc;
  int g;
};

/** How far from the junction the pedestrian lights across an arm are (those of the junction's own controller or any other). */
std::vector<Near> crossingsNear(const Arm& arm) {
  std::vector<Near> out;
  for (const CrossingJson& x : arm.edge->xings) {
    if (x.k != XING_SIGNAL || !x.sc) continue;
    const double dist = arm.out ? x.s : arm.edge->len - x.s;
    if (dist < NEAR_) out.push_back(Near{dist, *x.sc, x.g.value_or(0)});
  }
  return out;
}

}  // namespace

std::optional<Arrow> findArrow(World& world, LaneGraph& graph, SignalSystem& signals, Node& node, Arm& arm) {
  if (node.stub || node.dark || node.ctrl != CTRL_SIGNALS || node.sc < 0 || arm.sg < 0 || !arm.isApproach) return std::nullopt;
  const SignalControllerJson* controller = world.signals[node.sc];
  if (!controller || controller->ped || controller->groups < 2) return std::nullopt;
  // the turns to the right, from the lane at the kerb
  Lane* lane = nullptr;
  for (Lane* l : lanesOn(graph, arm)) {
    if (l->idx == 0) {
      lane = l;
      break;
    }
  }
  if (!lane) return std::nullopt;
  std::vector<Connector*> turns;
  for (Connector* c : lane->out)
    if (c->turn == TURN_RIGHT && LaneGraph::drivable(*c->to->edge, true)) turns.push_back(c);
  if (turns.empty()) return std::nullopt;
  if (!crossingsNear(arm).empty()) return std::nullopt;
  if (!partners(node).empty()) return std::nullopt;
  graph.resolveNode(node.id);
  std::set<int> foes;
  std::set<int> friends;
  for (Connector* c : turns) {
    for (const Conflict& k : c->conflicts) {
      const Arm* from = k.other->from->armTo;
      if (from == &arm) continue;
      if (from->sg < 0) {
        // an approach without a light: a driveway waits for everybody anyway – anything else, and nobody knows when it is free
        if (from->role != ROLE_MINOR || k.yield) return std::nullopt;
        continue;
      }
      foes.insert(from->sg);
    }
    // the people crossing the road the turn leads into
    const Arm* exit = nullptr;
    for (const Arm* a : node.arms) {
      if (a->edge == c->to->edge && a != &arm) {
        exit = a;
        break;
      }
    }
    if (!exit) continue;
    for (const Near& x : crossingsNear(*exit)) {
      if (x.sc != node.sc) return std::nullopt;
      friends.insert(x.g);
    }
  }
  // (a group cannot be asked to be red and green at once)
  for (const int g : friends)
    if (foes.count(g) || g == arm.sg) return std::nullopt;
  // (the sets are in rising order already: the original sorts them so)
  const std::vector<int> list(foes.begin(), foes.end());
  const std::vector<int> pals(friends.begin(), friends.end());
  if (signals.arrowTime(node.sc, arm.sg, list, pals) < WORTH) return std::nullopt;
  Arrow a;
  a.controller = node.sc;
  a.own = arm.sg;
  a.foes = list;
  a.friends = pals;
  a.state = -1;
  a.since = 0;
  return a;
}

int markArrowsAt(World& world, LaneGraph& graph, SignalSystem& signals, Node& node, double share) {
  int n = 0;
  if (node.ctrl != CTRL_SIGNALS || node.sc < 0) return n;
  // (a part of a bigger junction has come: what was taken for a junction of its own is none)
  for (Node* p : partners(node))
    for (Arm* arm : p->arms) signals.arrows.erase(arm);
  for (Arm* armAt : node.arms) {
    Arm& arm = *armAt;
    if (signals.arrows.has(&arm)) continue;
    if (hash01(node.id * 31.0 + arm.edge->id * 17.0 + 5) >= share) continue;
    std::optional<Arrow> a = findArrow(world, graph, signals, node, arm);
    if (!a) continue;
    signals.arrows.set(&arm, std::move(*a));
    n++;
  }
  return n;
}

}  // namespace fs
