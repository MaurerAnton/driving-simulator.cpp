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
       {"1 · Anfahren, Schalten, Anhalten", "1 · Pulling away, changing gear, stopping"},
       {"Sitz und Spiegel passen, also: anschnallen (B), Kupplung treten und Motor starten "
        "(Enter halten), 1. Gang, Handbremse lösen (Leertaste). Kupplung langsam kommen lassen "
        "und etwas Gas geben. Geschaltet wird bei etwa 2000–2500 Umdrehungen.",
        "Seat and mirrors are set: fasten your belt (B), press the clutch and start the engine "
        "(hold Enter), select 1st, release the handbrake (Space). Let the clutch up slowly and "
        "add a little throttle. Change up at about 2000–2500 rpm."},
       "basics",
       {}},
      {"rbl",
       {"2 · Rechts vor links (Tempo-30-Zone)", "2 · Right before left (30 km/h zone)"},
       {"In Wohngebieten ohne Vorfahrtschilder gilt: Wer von rechts kommt, fährt zuerst "
        "(§ 8 StVO). Fahre so an jede Einmündung heran, dass du anhalten kannst, und schau "
        "zuerst nach rechts. In der Zone gilt 30 km/h, bis das Zonen-Ende-Schild kommt.",
        "In residential areas without priority signs, whoever comes from the right goes first "
        "(§ 8 StVO). Approach every junction ready to stop and look right first. The 30 km/h "
        "zone lasts until the end-of-zone sign."},
       "route",
       {TargetType::Rbl, TargetType::Rbl, TargetType::Home}},
      {"priority",
       {"3 · Vorfahrt gewähren und Stopp", "3 · Give way and stop"},
       {"Das auf der Spitze stehende Dreieck (Zeichen 205) heißt: Vorfahrt gewähren. Das "
        "Achteck (Zeichen 206): an der Haltlinie anhalten – immer. Die gelbe Raute (Zeichen 306) "
        "zeigt: du bist auf der Vorfahrtstraße.",
        "The inverted triangle (sign 205) means give way. The octagon (sign 206): stop at the "
        "line – always. The yellow diamond (sign 306) tells you that you are on the priority road."},
       "route",
       {TargetType::Priority, TargetType::Priority, TargetType::Home}},
      {"signals",
       {"4 · Ampelkreuzungen und Abbiegen", "4 · Traffic lights and turning"},
       {"Gelb heißt: anhalten, wenn es gefahrlos geht. Bei Grün links abbiegen: Gegenverkehr "
        "durchlassen. Rechts abbiegen: Schulterblick nach rechts, Radfahrer und Fußgänger haben "
        "Vorrang. Blinken nicht vergessen.",
        "Amber means stop if you safely can. Turning left on green: let oncoming traffic pass. "
        "Turning right: shoulder check to the right, cyclists and pedestrians go first. Do not "
        "forget to indicate."},
       "route",
       {TargetType::Signals, TargetType::Signals, TargetType::Home}},
      {"roundabout",
       {"5 · Kreisverkehr", "5 · Roundabout"},
       {"Am Kreisverkehr (Zeichen 215 mit 205) hat der Verkehr im Kreis Vorfahrt. Beim "
        "Einfahren nicht blinken, beim Ausfahren rechts blinken. Auf Fußgänger an den Überwegen "
        "achten.",
        "At a roundabout (sign 215 with 205) traffic on the roundabout has priority. Do not "
        "indicate when entering; indicate right when leaving. Watch for pedestrians at the "
        "crossings."},
       "route",
       {TargetType::Roundabout, TargetType::Roundabout, TargetType::Home}},
      {"rural",
       {"6 · Überlandfahrt", "6 · Country roads"},
       {"Hinter dem Ortsausgangsschild gilt ohne weitere Schilder 100 km/h, am Ortseingang "
        "(gelbe Ortstafel) wieder 50 km/h. Abstand: halber Tacho. Vor Kurven und Kuppen "
        "rechtzeitig Tempo anpassen.",
        "Beyond the end-of-town sign the limit is 100 km/h unless signed otherwise; at the "
        "yellow town sign it is 50 km/h again. Distance: half your speed in metres. Adjust your "
        "speed early for bends and crests."},
       "route",
       {TargetType::Rural, TargetType::Home}},
      {"motorway",
       {"7 · Autobahn A 49", "7 · Motorway A 49"},
       {"Auf dem Beschleunigungsstreifen zügig beschleunigen, Spiegel, Schulterblick, blinken – "
        "der durchgehende Verkehr hat Vorfahrt. Rechts fahren, links überholen. "
        "Richtgeschwindigkeit 130 km/h. Ausfahrt: rechtzeitig blinken, erst auf dem "
        "Verzögerungsstreifen bremsen.",
        "Accelerate briskly on the slip road, mirrors, shoulder check, indicate – through "
        "traffic has priority. Keep right, overtake on the left. Recommended speed 130 km/h. "
        "Exit: indicate early, brake only on the deceleration lane."},
       "route",
       {TargetType::Motorway, TargetType::Home}},
      {"exam",
       {"Prüfungsfahrt", "Driving test"},
       {"Der Prüfer sagt die Richtung an. Bewertet werden Verkehrsbeobachtung, Geschwindigkeit, "
        "Vorfahrt, Abbiegen und Fahrzeugbedienung. Eine Gefährdung oder mehrere schwere Fehler "
        "führen zum Nichtbestehen.",
        "The examiner gives the directions. Observation, speed, right of way, turning and "
        "vehicle handling are assessed. A dangerous situation or several serious faults mean a "
        "fail."},
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
