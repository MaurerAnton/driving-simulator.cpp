// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Route guidance ("in 150 m turn left") and the lesson / exam definitions.
 *
 * Origin: src/game/nav.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

/** What the next manoeuvre is, in i18n terms. */
struct NavHint {
  /** i18n key of the manoeuvre: navLeft, navRight, navStraight, navUturn,
   *  navRoundabout, navFollow, navMotorway, routeDone */
  std::string what = "navFollow";
  /** metres to the manoeuvre (-1 = none) */
  double dist = -1;
  /** roundabout exit number */
  int n = 0;
  /** -1 left, 0 straight/none, 1 right – for the arrow */
  int arrow = 0;
  /** get in lane: -1 = move one lane to the left, 1 = to the right, 0 = this lane is fine */
  int lane = 0;
  bool done = false;
};

class Navigator {
 public:
  explicit Navigator(LaneGraph& graph) : graph(graph) {}

  std::vector<Connector*> route;
  Edge* goal = nullptr;
  NavHint hint;
  /** lanes of the route, for drawing it on the map */
  std::vector<Lane*> routeLanes;

  void clear();
  bool setGoal(Edge* goal, Lane* from);

  /** `lane` and `s` = where the player is (distance of the front bumper along
   *  the lane); x,z = position. */
  NavHint& update(double dt, Lane* lane, double s, double x = NAN, double z = NAN);

 private:
  LaneGraph& graph;
  int idx_ = 0;
  double offRoute_ = 0;
  PolyProjection proj_;

  bool plan(Lane& from);
};

/** Which kind of destination a lesson asks for. */
enum class TargetType { Rbl, Signals, Roundabout, Rural, Motorway, Priority, Home };

/** One lesson or the driving test. */
struct LessonDef {
  std::string id;
  std::pair<std::string, std::string> title;   // [German, English]
  std::pair<std::string, std::string> intro;   // [German, English]
  /** 'free' | 'basics' | 'route' | 'exam' */
  std::string kind;
  std::vector<TargetType> targets;
};

/** The lessons and the exam, as the original ships them. */
const std::vector<LessonDef>& lessons();

/** Pick a destination edge of the wanted kind at a sensible distance from (x, z). */
Edge* findTarget(World& world, LaneGraph& graph, TargetType type, double x, double z,
                 Lane& from, Edge* home, const std::set<Edge*>& avoid);

}  // namespace fs
