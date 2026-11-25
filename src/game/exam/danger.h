// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/exam/danger.ts @ v1.7.7
/**
 * "Spot the danger": a moment of each theory scene (game/theory.ts,
 * theoryscenes.ts, theorycrossing.ts), frozen and seen from above – why must
 * you be ready to brake here?  Two to four answers, one or more of them
 * right.  The moment is the time of one of the scene's questions (`step`)
 * moved by `off` seconds, so that it follows the scene when its script is
 * changed.  The words are in src/i18n/exam/<lang>.ts under `danger`.
 */
#pragma once

#include <string>
#include <vector>

namespace fs {

struct DangerItem {
  /** its id (the scene's id: one item per scene) */
  std::string id;
  /** the theory scene */
  std::string scene;
  /** the moment: the time of this question of the scene (index), moved by `off` s */
  int step = 0;
  double off = 0;
  int answers = 0;
  std::vector<int> correct;
  std::string ref;
};

inline const std::vector<DangerItem> DANGER_ITEMS = {
  {"rbl", "rbl", 0, -1.0, 3, {0, 1}, "§ 8 Abs. 1 StVO"},
  {"bend", "bend", 1, -0.5, 3, {0, 1}, "Anlage 3 StVO, Zeichen 306"},
  {"leftTurn", "leftTurn", 0, -0.5, 3, {0, 1}, "§ 9 Abs. 3 StVO"},
  {"zebra", "zebra", 0, -0.5, 3, {0, 1}, "§ 26 Abs. 1 StVO"},
  {"tram", "tram", 0, -0.8, 3, {0, 1}, "§ 9 Abs. 3 StVO"},
  {"corridor", "corridor", 0, -0.5, 3, {0, 1}, "§ 11 Abs. 2 StVO"},
  {"schoolBus", "schoolBus", 1, -0.5, 3, {0, 1}, "§ 20 Abs. 4 StVO"},
  {"overtake", "overtake", 0, -0.5, 3, {0, 1}, "§ 5 Abs. 2, Abs. 4 StVO"},
  {"blindSpot", "blindSpot", 0, -0.5, 3, {0, 1}, "§ 9 Abs. 3 StVO"},
  {"zip", "zip", 0, -0.5, 3, {0, 1}, "§ 7 Abs. 4 StVO"},
  {"motorway", "motorway", 0, -0.5, 3, {0, 1}, "§ 18 Abs. 3 StVO"},
  {"roundabout", "roundabout", 0, -0.5, 3, {0, 1}, "§ 8 Abs. 1a StVO"},
  {"greenArrow", "greenArrow", 1, -0.5, 3, {0, 1}, "§ 37 Abs. 2 Nr. 1 StVO"},
  {"parkingSigns", "parkingSigns", 0, -0.5, 3, {0, 1}, "Anlage 2 StVO, Zeichen 283"},
  {"followingDistance", "followingDistance", 1, -0.3, 3, {0, 1}, "§ 4 Abs. 1 StVO"},
  {"narrowPass", "narrowPass", 1, -0.5, 3, {0, 1}, "§ 6 StVO"},
  {"busStopPullOut", "busStopPullOut", 0, -0.5, 3, {0, 1}, "§ 20 Abs. 4, Abs. 5 StVO"},
  {"nightDrive", "nightDrive", 1, -0.5, 3, {0, 1}, "§ 3 Abs. 1 StVO"},
  {"fogWall", "fogWall", 0, -0.5, 3, {0, 1}, "§ 3 Abs. 1, § 17 Abs. 3 StVO"},
  {"accidentScene", "accidentScene", 0, -0.5, 3, {0, 1}, "§ 16 Abs. 2 StVO, § 323c StGB"},
  {"truckBlindSpot", "truckBlindSpot", 0, -0.5, 3, {0, 1}, "§ 9 Abs. 3 StVO"},
  {"multiLane", "multiLane", 0, -0.5, 3, {0, 1}, "§ 7 Abs. 5 StVO"},
  {"tunnel", "tunnel", 0, -0.5, 3, {0, 1}, "Anlage 3 StVO, Zeichen 327"},
  {"seniorCrossing", "seniorCrossing", 0, -0.5, 3, {0, 1}, "§ 3 Abs. 2a StVO"},
  {"wildlife", "wildlife", 0, -0.5, 3, {0, 1}, "Anlage 1 StVO, Zeichen 142"},
  {"policeSignals", "policeSignals", 0, -0.5, 3, {0, 1}, "§ 36 Abs. 1 StVO"},
  {"flagman", "flagman", 0, -0.5, 3, {0, 1}, "§ 45 Abs. 6, § 1 Abs. 2 StVO"},
  {"levelCrossing", "levelCrossing", 0, -0.5, 3, {0, 1}, "§ 19 Abs. 1, Abs. 2 StVO"},
  {"levelCrossingQueue", "levelCrossingQueue", 0, -0.5, 3, {0, 1}, "§ 19 Abs. 3 StVO"},
};

}  // namespace fs
