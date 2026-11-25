// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/theory.ts @ v1.7.7
#include "game/theory.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <stdexcept>

#include "core/js.h"
#include "game/theorycrossing.h"
#include "game/theoryscenes.h"
#include "sim/rail.h"
#include "world/types.h"

namespace fs::theory {

namespace {
constexpr double PI_ = 3.14159265358979323846;
}

const Footprint& FOOT(const ActorKind& kind) {
  static const auto* table = [] {
    const Consist& TRAM = *consistOf(RAIL_TRAM, "single");
    return new std::map<std::string, Footprint>{
        {"me", {4.28, 1.79, 2.195, 2.63}},
        {"car", {4.28, 1.79, 2.195, 2.63}},
        {"van", {5.9, 2.02, 2.83, 3.66}},
        {"bus", {12, 2.55, 5.65, 5.9}},
        {"truck", {8.2, 2.5, 3.75, 4.7}},
        // (a tram is placed car by car from its nose: render/theory.ts)
        {"tram", {consistLength(TRAM), TRAM.width, 0, 0}},
        {"bike", {1.75, 0.62, 0.88, 1.06}},
        {"person", {0.45, 0.5, 0.2, 0}},
        {"child", {0.35, 0.4, 0.15, 0}},
        // (animals: placed like people, by the middle of the body – game/theoryscenes.ts)
        {"deer", {1.5, 0.45, 0.75, 0}},
        {"boar", {1.3, 0.55, 0.65, 0}},
    };
  }();
  return table->at(kind);
}

// ------------------------------------------------------------- the lessons
namespace {
namespace lessons {
constexpr int BLUE = 0x2f5f8a;
constexpr int RED = 0xa8231b;
constexpr int WHITE = 0xe2e2dc;
constexpr int GREEN = 0x2f7a46;
constexpr int SILVER = 0x9aa1a8;
constexpr int YELLOW = 0xd8a417;
constexpr int DARK = 0x2c3036;

LessonView viewAt(double x, double z, double yaw, double pitch, double dist) { return LessonView{.at = std::array<double, 2>{x, z}, .yaw = yaw, .pitch = pitch, .dist = dist}; }
LessonView viewOf(const std::string& id, double ahead, double yaw, double pitch, double dist) { return LessonView{.at = id, .ahead = ahead, .yaw = yaw, .pitch = pitch, .dist = dist}; }

/** A street in town north–south: one lane each way, pavements – the lines at x of the kerbs and the pavements' outer edges. */
Stage townStreet(double z0, double z1, double laneW, double walk) {
  Stage st = straight({.z0 = z0, .z1 = z1, .bounds = {{-laneW - walk}, {-laneW}, {laneW}, {laneW + walk}}, .strips = {"walk", "asphalt", "walk"}, .kerbs = {1, 2}});
  return st;
}

/** 1 · Right before left: three cars at a crossroads without signs. */
LessonDef RBL() {
  return {
      .id = "rbl",
      .stage =
          [] {
            const Road road{.lanes = 1, .laneW = 2.75, .walk = 2.2, .centre = "none"};
            Cross c = crossroads({
                .ns = road,
                .ew = road,
                .r = 5,
                .arms = {.N = {.len = 32, .signs = {{24, {"274.1"}}}}, .E = {.len = 30, .signs = {{24, {"274.1"}}}}, .S = {.len = 64, .signs = {{50, {"274.1"}}}}, .W = {.len = 30, .signs = {{24, {"274.1"}}}}},
            });
            c.stage.paths["me"] = c.move('S', 'N');
            c.stage.paths["blue"] = c.move('E', 'W');
            c.stage.paths["white"] = c.move('W', 'E');
            return c.stage;
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = {{0, "line-24", 7}, {1.5, "line-13.5", 7}, {5, "line", 0}, {7.7, "line", 0}, {10.9, "line+12.8", 8}, {12.6, "line+26.4", 8}}},
              {.id = "blue", .kind = "car", .path = "blue", .keys = {{0, "line-30", 7}, {5, "line-3.75", 3.5}, {7.3, "line+7.5", 5.5}, {10.5, "line+29", 8}}, .colour = BLUE, .name = "theory.who.blue"},
              {.id = "white", .kind = "car", .path = "white", .keys = {{0, "line-17", 6}, {1, "line-11", 6}, {4.6, "line", 0}, {9.4, "line", 0}, {12.6, "line+12.8", 8}, {14.5, "line+28", 8}}, .colour = WHITE, .name = "theory.who.white"},
          },
      .steps =
          {
              {.at = 5, .answers = {"blue", "me", "white"}, .right = "blue", .ref = "§ 8 Abs. 1 StVO", .ask = "theory.ask.first"},
              {.at = 7.3, .answers = {"me", "white"}, .right = "me", .ref = "§ 8 Abs. 1 StVO"},
          },
      .end = 14.5,
      .view = viewAt(0, 4, 0.45, -0.7, 34),
  };
}

/** 2 · The priority road bends: you follow it to the left. */
LessonDef BEND() {
  return {
      .id = "bend",
      .stage =
          [] {
            const Road road{.lanes = 1, .laneW = 3, .walk = 2, .centre = "dash"};
            Cross c = crossroads({
                .ns = road,
                .ew = road,
                .r = 6,
                .arms =
                    {
                        .N = {.len = 30, .line = "give", .signs = {{14, {"205", "1002-21"}}}},
                        .E = {.len = 30, .line = "give", .signs = {{14, {"205", "1002-11"}}}},
                        .S = {.len = 62, .signs = {{30, {"306", "1002-10"}}}},
                        .W = {.len = 30, .signs = {{22, {"306", "1002-20"}}}},
                    },
            });
            c.stage.paths["me"] = c.move('S', 'W');
            c.stage.paths["red"] = c.move('N', 'S');
            return c.stage;
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = {{0, "in-34", 9}, {3, "in-12", 6.5}, {5.5, "in+1", 4}, {7.2, "in+8", 4.6}, {10, "in+24", 7.5}}, .blink = {{3.05, -1}, {9.2, 0}}},
              {.id = "red", .kind = "car", .path = "red", .keys = {{0, "line-12", 6}, {4, "line", 0}, {8.6, "line", 0}, {11.4, "line+9.8", 7}, {13.5, "line+24.5", 7}}, .colour = RED, .name = "theory.who.red"},
          },
      .steps =
          {
              {.at = 3, .answers = {"left", "none"}, .right = "left", .ref = "Anlage 3 StVO, Zeichen 306"},
              {.at = 5.5, .answers = {"me", "red"}, .right = "me", .ref = "§ 8 Abs. 1 StVO, Zeichen 205", .ask = "theory.ask.first"},
          },
      .end = 13.5,
      .view = viewAt(0, 3, 0.55, -0.72, 34),
  };
}

/** 3 · Turning left on green: the oncoming car, then the people crossing. */
LessonDef LEFT_TURN() {
  return {
      .id = "leftTurn",
      .stage =
          [] {
            const Road road{.lanes = 1, .laneW = 3.25, .walk = 2.5, .centre = "dash"};
            Cross c = crossroads({
                .ns = road,
                .ew = road,
                .r = 6,
                .arms =
                    {
                        .N = {.len = 32, .line = "stop", .light = ArmLight{"ns"}},
                        .E = {.len = 30, .line = "stop", .light = ArmLight{"ew"}},
                        .S = {.len = 60, .line = "stop", .light = ArmLight{"ns"}},
                        .W = {.len = 30, .line = "stop", .xing = ArmXing{2.6, 4, "furt"}, .light = ArmLight{"ew"}, .pedLight = "pw"},
                    },
            });
            Stage& st = c.stage;
            st.paths["me"] = c.move('S', 'W');
            st.paths["red"] = c.move('N', 'S');
            // somebody on the pavement of the west arm, across it with the lights, and on
            const double x = -c.xingAt('W');
            st.paths["walker"] = polyPath({-14, -4.55, x - 0.8, -4.55, x, -3.9, x, 3.9, x - 0.8, 4.55, -14, 4.55});
            return st;
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = {{0, "in-24", 8}, {1, "in-16", 8}, {5.5, "in+2", 0}, {9.4, "in+2", 0}, {12.6, "in+11.6", 6}, {15, "in+27.2", 7}}, .blink = {{0, -1}, {13, 0}}},
              {.id = "red", .kind = "car", .path = "red", .keys = {{0, "in-48", 9}, {9, "in+33", 9}}, .colour = RED, .name = "theory.who.red"},
              {.id = "walker", .kind = "person", .path = "walker", .keys = {{0, 1.08, 1.3}, {18, 24.48, 1.3}}, .look = 7, .name = "theory.who.walker"},
          },
      .steps =
          {
              {.at = 4.4, .answers = {"me", "red"}, .right = "red", .ref = "§ 9 Abs. 3 StVO", .ask = "theory.ask.first"},
              {.at = 7.4, .answers = {"me", "walker"}, .right = "walker", .ref = "§ 9 Abs. 3 StVO", .ask = "theory.ask.first"},
          },
      .end = 15,
      .lights = {{"ns", {{0, 'G'}}}, {"ew", {{0, 'R'}}}, {"pw", {{0, 'G'}}}},
      .view = viewAt(0, 2, 0.6, -0.72, 34),
  };
}

/** 4 · A zebra crossing: somebody wants to cross, then somebody else. */
LessonDef ZEBRA() {
  return {
      .id = "zebra",
      .stage =
          [] {
            Stage st = townStreet(70, -60, 3.25, 2.5);
            // the middle line, broken off at the crossing
            pushAll(st.surfaces, dashes(0, 70, 0, 6, 0.12, 3, 6));
            pushAll(st.surfaces, dashes(0, -6, 0, -60, 0.12, 3, 6));
            st.surfaces.push_back(bar(0, 6, 0, 2.6, 0.12));
            st.surfaces.push_back(bar(0, -2.6, 0, -6, 0.12));
            // the zebra (Zeichen 293): stripes along the road, half a metre wide
            for (double x = -2.5; x < 2.6; x += 1) st.surfaces.push_back(bar(x, 2, x, -2, 0.5));
            // Zeichen 350 on both sides, for both directions
            st.signs.push_back({4.1, 2.6, yawOf(0, 1), {"350"}});
            st.signs.push_back({-4.1, -2.6, yawOf(0, -1), {"350"}});
            st.paths["me"] = lanePath(1.625, 70, -60, {{"zebra", 70 - 3.0}});
            st.paths["white"] = lanePath(-1.625, -60, 70, {{"zebra", 60 - 3.0}});
            st.paths["walker"] = polyPath({4.5, 22, 4.5, 2.4, 3.6, 0.9, -3.6, 0.9, -4.5, -1.2, -4.5, -24});
            st.paths["walker2"] = polyPath({-4.6, -26, -4.6, -2.4, -3.6, -0.9, 3.6, -0.9, 4.6, 1.2, 4.6, 24});
            return finish(std::move(st));
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = {{0, "zebra-34", 8.3}, {3, "zebra-12", 7}, {6, "zebra", 0}, {14.2, "zebra", 0}, {17.4, "zebra+12.8", 8}, {18.5, "zebra+21.6", 8}}},
              {.id = "white", .kind = "car", .path = "white", .keys = {{0, "zebra-30", 8.3}, {1, "zebra-21.7", 8.3}, {6.2, "zebra", 0}, {12.4, "zebra", 0}, {15.6, "zebra+12.8", 8}, {18.5, "zebra+36", 8}}, .colour = WHITE, .name = "theory.who.white"},
              {.id = "walker", .kind = "person", .path = "walker", .keys = {{0, 13.2, 1.4}, {26, 49.6, 1.4}}, .look = 11, .name = "theory.who.walker"},
              {.id = "walker2", .kind = "person", .path = "walker2", .keys = {{0, 14.4, 1.35}, {26, 49.5, 1.35}}, .look = 23, .name = "theory.who.walker"},
          },
      .steps =
          {
              {.at = 3, .answers = {"brake", "go", "horn"}, .right = "brake", .ref = "§ 26 Abs. 1 StVO"},
              {.at = 8.4, .answers = {"wait", "go"}, .right = "wait", .ref = "§ 26 Abs. 1 StVO"},
          },
      .end = 18.5,
      .view = viewAt(0, 6, 0.5, -0.62, 30),
  };
}

/** 5 · Turning left over the tracks: the tram from behind. */
LessonDef TRAM_LESSON() {
  return {
      .id = "tram",
      .stage =
          [] {
            const Road ns{.lanes = 1, .laneW = 3.25, .median = 6, .walk = 2.5, .centre = "none", .tracks = true};
            const Road ew{.lanes = 1, .laneW = 3.25, .walk = 2.5, .centre = "dash"};
            Cross c = crossroads({
                .ns = ns,
                .ew = ew,
                .r = 6,
                .arms =
                    {
                        .N = {.len = 32, .signs = {{24, {"306"}}}},
                        .E = {.len = 28, .line = "give", .signs = {{14, {"205"}}}},
                        .S = {.len = 70, .signs = {{40, {"306"}}}},
                        .W = {.len = 28, .line = "give", .signs = {{14, {"205"}}}},
                    },
            });
            c.stage.paths["me"] = c.move('S', 'W');
            c.stage.paths["tram"] = c.move('S', 'N', "tram");
            return c.stage;
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = {{0, "in-24.5", 8}, {0.5, "in-20.5", 8}, {5.5, "in-0.5", 0}, {11.5, "in-0.5", 0}, {14.7, "in+9.1", 6}, {16.5, "in+20.8", 6.5}}, .blink = {{0, -1}, {15.5, 0}}},
              {.id = "tram", .kind = "tram", .path = "tram", .keys = {{0, 30, 5}, {5.5, 56, 5}, {8.5, 77, 9}, {16.5, 149, 9}}, .name = "theory.who.tram"},
          },
      .steps = {{.at = 5.5, .answers = {"me", "tram"}, .right = "tram", .ref = "§ 9 Abs. 1, Abs. 3 StVO", .ask = "theory.ask.first"}},
      .end = 16.5,
      .view = viewAt(1, 8, 0.5, -0.62, 40),
  };
}

/** The dual carriageway of the corridor: two lanes each way, a hard shoulder, a crash barrier in the middle. */
Stage dualStage() {
  Stage st = straight({
      .z0 = 72,
      .z1 = -72,
      .bounds = {{-11.75}, {-1.5}, {1.5}, {11.75}},
      .strips = {"asphalt", "grass", "asphalt"},
      .guards = {0},
  });
  for (const double s : {1.0, -1.0}) {
    pushAll(st.surfaces, along({s * 2.25}, 72, -72, 0.15));
    pushAll(st.surfaces, along({s * 5.75}, 72, -72, 0.15, std::array<double, 2>{6, 12}));
    pushAll(st.surfaces, along({s * 9.25}, 72, -72, 0.25));
  }
  st.paths["left"] = lanePath(4, 72, -72);
  st.paths["right"] = lanePath(7.5, 72, -72);
  st.paths["gap"] = lanePath(5.9, 72, -72);
  return finish(std::move(st));
}

/**
 * A car in a jam: rolling at 5 m/s until it slows to walking pace (at 3 s),
 * then – from `shift` on – creeping on while it moves `off` to the side,
 * and standing.
 */
ActorDef jam(const std::string& id, const ActorKind& kind, int colour, const std::string& path, double s0, double shift, double off) {
  const double s1 = s0 + 9.75;
  ActorDef a{.id = id, .kind = kind, .path = path, .keys = {{0, s0, 5}, {3, s1, 1.5}, {6.4, s1 + 5.1, 1.5}, {8.4, s1 + 6.6, 0}}, .side = {{shift, off, 2.6}}};
  if (kind != "me") a.colour = colour;
  return a;
}

/** 6 · An ambulance in the jam: the emergency corridor. */
LessonDef CORRIDOR() {
  return {
      .id = "corridor",
      .stage = dualStage,
      .actors =
          {
              // the left lane moves to the left, everybody else to the right (noses: z = 72 − s), creeping on while they do
              jam("me", "me", 0, "left", 64, 3.3, 1.15),
              jam("a1", "car", BLUE, "left", 77, 3.2, 1.15),
              jam("a2", "car", GREEN, "left", 51, 3.5, 1.15),
              jam("a3", "car", SILVER, "left", 38.5, 3.7, 1.15),
              jam("b1", "car", YELLOW, "right", 80, 3.3, -1.45),
              jam("b2", "car", RED, "right", 64.5, 3.4, -1.45),
              jam("b3", "car", WHITE, "right", 52, 3.6, -1.45),
              jam("b4", "car", DARK, "right", 39, 3.8, -1.45),
              // through the corridor: the ambulance, then the police
              {.id = "amb", .kind = "van", .path = "gap", .keys = {{4.4, 0, 11}, {10.4, 76, 13}}, .colour = 0xf2f2ee, .blue = true, .name = "theory.who.van"},
              {.id = "police", .kind = "van", .path = "gap", .keys = {{12, 0, 11}, {17.4, 74, 14}}, .colour = 0x2d5d9a, .blue = true},
          },
      .steps =
          {
              {.at = 3, .answers = {"left", "right", "stay"}, .right = "left", .ref = "§ 11 Abs. 2 StVO"},
              {.at = 11.6, .answers = {"follow", "keep"}, .right = "keep", .ref = "§ 11 Abs. 2 StVO"},
          },
      .end = 18.6,
      .view = viewAt(6, 12, 0.5, -0.95, 60),
  };
}

/** The street of the school bus: a stop on the right, with its sign and the zigzag line (Zeichen 299). */
Stage busStage() {
  Stage st = townStreet(70, -65, 3.25, 2.5);
  pushAll(st.surfaces, dashes(0, 70, 0, -65, 0.12, 3, 6));
  st.signs.push_back({4.1, -9.5, yawOf(0, 1), {"224"}});
  // the zigzag along the kerb of the stop
  std::vector<double> zig;
  int k = 0;
  for (double z = 6; z >= -9; z -= 1.5, k++) {
    zig.push_back(k % 2 ? 2.35 : 3.05);
    zig.push_back(z);
  }
  for (size_t i = 0; i + 3 < zig.size(); i += 2) st.surfaces.push_back(bar(zig[i], zig[i + 1], zig[i + 2], zig[i + 3], 0.12));
  st.paths["lane"] = lanePath(1.625, 70, -65);
  // children getting off by the front door (the pavement on the right), and one round the front of the bus
  st.paths["kid1"] = polyPath({3.0, -7.0, 4.3, -7.5, 4.4, -12, 4.6, -40});
  st.paths["kid2"] = polyPath({3.0, -7.0, 4.4, -6.4, 4.5, 6, 4.6, 30});
  st.paths["kid3"] = polyPath({3.0, -7.0, 3.9, -8.4, 2.4, -9.6, -4.5, -9.6, -4.7, -16, -4.8, -40});
  return finish(std::move(st));
}

/** 7 · A school bus with its hazard lights on at the stop. */
LessonDef SCHOOL_BUS() {
  return {
      .id = "schoolBus",
      .stage = busStage,
      .actors =
          {
              {.id = "bus", .kind = "bus", .path = "lane", .keys = {{0, 52, 7}, {6.5, 78, 0}, {30, 78, 0}}, .side = {{3, -0.15, 2.4}}, .blink = {{0, 2}}, .name = "theory.who.bus", .display = "Schulbus"},
              {.id = "me",
               .kind = "me",
               .path = "lane",
               .keys = {{0, 28, 7}, {3, 45, 4.5}, {7.4, 60, 0}, {9.6, 60, 0}, {12.6, 62.85, 1.9}, {15.4, 68.17, 1.9}, {16.4, 69.12, 0}, {18.4, 69.12, 0}, {19.4, 70.07, 1.9}, {26, 82.61, 1.9}, {28.4, 94.37, 7.9}, {29.5, 103.06, 7.9}},
               .side = {{9.6, 3.0, 3.6}, {26.1, 0, 2.6}}},
              {.id = "kid1", .kind = "child", .path = "kid1", .keys = {{7.6, 0, 1.1}, {30, 33, 1.3}}, .look = 3, .name = "theory.who.child", .leaves = "bus"},
              {.id = "kid2", .kind = "child", .path = "kid2", .keys = {{9, 0, 1.1}, {30, 33, 1.3}}, .look = 18, .name = "theory.who.child", .leaves = "bus"},
              {.id = "kid3", .kind = "child", .path = "kid3", .keys = {{11, 0, 1.0}, {13, 2.4, 1.2}, {19.2, 9.84, 1.2}, {26, 18, 1.2}}, .look = 29, .name = "theory.who.child", .leaves = "bus"},
          },
      .steps =
          {
              {.at = 2.8, .answers = {"!no", "!yes"}, .right = "!no", .ref = "§ 20 Abs. 3 StVO"},
              {.at = 9.2, .answers = {"walk", "normal", "never"}, .right = "walk", .ref = "§ 20 Abs. 4 StVO"},
          },
      .end = 29.5,
      .view = viewOf("me", 8, -0.55, -0.6, 28),
  };
}

/** 8 · Overtaking a cyclist in town. */
LessonDef OVERTAKE() {
  return {
      .id = "overtake",
      .stage =
          [] {
            Stage st = townStreet(72, -72, 3.25, 2.5);
            pushAll(st.surfaces, dashes(0, 72, 0, -72, 0.12, 3, 6));
            st.signs.push_back({4.1, 66, yawOf(0, 1), {"274-50"}});
            st.paths["lane"] = lanePath(1.625, 72, -72);
            st.paths["bike"] = lanePath(2.4, 72, -72);
            st.paths["white"] = lanePath(-1.625, -72, 72);
            return finish(std::move(st));
          },
      .actors =
          {
              {.id = "bike", .kind = "bike", .path = "bike", .keys = {{0, 16, 4.5}, {20, 106, 4.5}}, .colour = 0x2f8a4a, .name = "theory.who.bike"},
              {.id = "me", .kind = "me", .path = "lane", .keys = {{0, 0, 9}, {3, 19.5, 4.6}, {9.4, 48.3, 4.5}, {12.4, 71.6, 11}, {17, 122, 11}}, .side = {{9.6, 2.05, 2.2}, {14.3, 0, 2.2}}},
              {.id = "white", .kind = "car", .path = "white", .keys = {{0, 20, 9}, {16, 164, 9}}, .colour = WHITE, .name = "theory.who.white"},
          },
      .steps =
          {
              {.at = 3, .answers = {"wait", "go"}, .right = "wait", .ref = "§ 5 Abs. 2, Abs. 4 StVO"},
              {.at = 9.4, .answers = {"m1", "m15", "m2"}, .right = "m15", .ref = "§ 5 Abs. 4 StVO"},
          },
      .end = 16.5,
      .view = viewOf("me", 10, 0.6, -0.55, 26),
      .gap = LessonGap{"me", "bike", 11.9, 13.1},
  };
}

/** 9 · Turning right: the cyclist in the blind spot. */
LessonDef BLIND_SPOT() {
  return {
      .id = "blindSpot",
      .stage =
          [] {
            const Road ns{.lanes = 1, .laneW = 3.25, .bike = 1.85, .walk = 2.5, .centre = "dash"};
            const Road ew{.lanes = 1, .laneW = 3.25, .walk = 2.5, .centre = "dash"};
            Cross c = crossroads({
                .ns = ns,
                .ew = ew,
                .r = 6,
                .arms =
                    {
                        .N = {.len = 32, .line = "stop", .light = ArmLight{"ns"}},
                        .E = {.len = 28, .line = "stop", .light = ArmLight{"ew"}},
                        .S = {.len = 66, .line = "stop", .light = ArmLight{"ns"}},
                        .W = {.len = 28, .line = "stop", .light = ArmLight{"ew"}},
                    },
            });
            Stage& st = c.stage;
            st.paths["me"] = c.move('S', 'E');
            st.paths["bike"] = c.move('S', 'N', "bike");
            // the arrow for turning right in the lane, and a bicycle on the cycle lane (drawn simply: an arrow straight on)
            st.surfaces.push_back(laneArrow(1.625, 16, 0, "right"));
            st.surfaces.push_back(laneArrow(4.175, 18, 0, "straight"));
            return st;
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = {{0, "line-21", 8}, {3.6, "line-2.6", 2.2}, {5.6, "line", 0}, {7.2, "line", 0}, {10.4, "line+10.4", 6.5}, {12, "line+21", 6.8}}, .blink = {{0, 1}, {10.5, 0}}},
              {.id = "bike", .kind = "bike", .path = "bike", .keys = {{0, 34, 5.2}, {12, 96.4, 5.2}}, .colour = 0xd35a1f, .name = "theory.who.bike"},
          },
      .steps = {{.at = 3.6, .answers = {"me", "bike"}, .right = "bike", .ref = "§ 9 Abs. 1, Abs. 3 StVO", .ask = "theory.ask.first", .blind = "me"}},
      .end = 12,
      .lights = {{"ns", {{0, 'G'}}}, {"ew", {{0, 'R'}}}},
      .view = viewAt(2, 8, 0.55, -0.7, 26),
  };
}

/** The street where the right lane ends: two lanes north, one south. */
Stage zipStage() {
  const Taper taper{.z0 = -20, .z1 = -34, .to = 3.25};
  Stage st = straight({
      .z0 = 72,
      .z1 = -72,
      .bounds = {{-5.75}, {-3.25}, {6.5, taper}, {9}},
      .strips = {"walk", "asphalt", "walk"},
      .kerbs = {1, 2},
  });
  pushAll(st.surfaces, dashes(0, 72, 0, -72, 0.12, 3, 6));
  pushAll(st.surfaces, along({3.25}, 72, -20, 0.12, std::array<double, 2>{3, 6}));
  st.signs.push_back({7.3, 40, yawOf(0, 1), {"121-10"}});
  st.surfaces.push_back(laneArrow(4.6, 6, 0, "left"));
  st.surfaces.push_back(laneArrow(4.6, -12, 0, "left"));
  st.paths["left"] = lanePath(1.625, 72, -72);
  st.paths["right"] = lanePath(4.875, 72, -72);
  return finish(std::move(st));
}

/** 10 · Where a lane ends: taking turns (the zip). */
LessonDef ZIP() {
  return {
      .id = "zip",
      .stage = zipStage,
      .actors =
          {
              {.id = "l1", .kind = "car", .path = "left", .keys = {{0, 84, 4}, {16, 148, 4}}, .colour = SILVER},
              {.id = "l2", .kind = "car", .path = "left", .keys = {{0, 68, 4}, {16, 132, 4}}, .colour = BLUE, .name = "theory.who.blue"},
              {.id = "l3", .kind = "car", .path = "left", .keys = {{0, 52, 4}, {16, 116, 4}}, .colour = GREEN},
              {.id = "l4", .kind = "car", .path = "left", .keys = {{0, 36, 4}, {16, 100, 4}}, .colour = DARK},
              {.id = "r1", .kind = "car", .path = "right", .keys = {{0, 72, 6}, {3.8, 92, 4}, {16, 140.8, 4}}, .colour = RED, .side = {{3.1, 3.25, 2.4}}, .blink = {{1.2, -1}, {5.6, 0}}},
              {.id = "me", .kind = "me", .path = "right", .keys = {{0, 32, 9}, {7.8, 92, 4}, {16, 124.8, 4}}, .side = {{7.1, 3.25, 2.4}}, .blink = {{5.2, -1}, {9.6, 0}}},
          },
      .steps = {{.at = 2, .answers = {"end", "early", "stop"}, .right = "end", .ref = "§ 7 Abs. 4 StVO"}},
      .end = 12.5,
      .view = viewAt(3, -4, 0.5, -0.75, 46),
  };
}

/** The motorway with the lane to join it: two lanes each way, the acceleration lane on the right. */
Stage motorwayStage() {
  const Taper t1{.z0 = -30, .z1 = -55, .to = 9.25};
  const Taper t2{.z0 = -30, .z1 = -55, .to = 11.75};
  Stage st = straight({
      .z0 = 72,
      .z1 = -72,
      .bounds = {{-12}, {-1}, {1}, {13, t1}, {15.5, t2}},
      .strips = {"asphalt", "grass", "asphalt", "asphalt"},
      .guards = {0},
  });
  pushAll(st.surfaces, along({-1.75}, 72, -72, 0.15));
  pushAll(st.surfaces, along({-5.5}, 72, -72, 0.15, std::array<double, 2>{6, 12}));
  pushAll(st.surfaces, along({-9.25}, 72, -72, 0.25));
  pushAll(st.surfaces, along({1.75}, 72, -72, 0.15));
  pushAll(st.surfaces, along({5.5}, 72, -72, 0.15, std::array<double, 2>{6, 12}));
  // the broad broken line between the motorway and the lane joining it, then the edge line again
  pushAll(st.surfaces, along({9.25}, 72, -30, 0.3, std::array<double, 2>{6, 6}));
  pushAll(st.surfaces, along({9.25}, -55, -72, 0.25));
  pushAll(st.surfaces, along({13, t1}, 72, -55, 0.25));
  st.paths["left"] = lanePath(3.625, 72, -72);
  st.paths["right"] = lanePath(7.375, 72, -72);
  st.paths["ramp"] = lanePath(11.125, 72, -72);
  return finish(std::move(st));
}

/** 11 · Joining the motorway: who goes first? */
LessonDef MOTORWAY() {
  return {
      .id = "motorway",
      .stage = motorwayStage,
      .actors =
          {
              {.id = "me", .kind = "me", .path = "ramp", .keys = {{0, 4, 12}, {2, 30, 13.5}, {4.9, 69, 13.5}, {7.5, 107, 16.5}, {10, 148, 16.5}}, .side = {{4.8, 3.75, 2.2}}, .blink = {{0.4, -1}, {7.2, 0}}},
              {.id = "truck", .kind = "truck", .path = "right", .keys = {{0, 4, 16.5}, {10, 169, 16.5}}, .name = "theory.who.truck"},
              {.id = "fast", .kind = "car", .path = "left", .keys = {{0, -22, 21}, {10, 188, 21}}, .colour = DARK},
          },
      .steps = {{.at = 2, .answers = {"me", "truck"}, .right = "truck", .ref = "§ 18 Abs. 3 StVO", .ask = "theory.ask.first"}},
      .end = 9.6,
      .view = viewOf("me", 6, 0.55, -0.55, 34),
  };
}

/** 12 · A roundabout: who goes first, and when to indicate. */
LessonDef ROUNDABOUT() {
  return {
      .id = "roundabout",
      .stage =
          [] {
            Round r = roundabout({.island = 8, .apron = 9.5, .outer = 15.5, .laneW = 3.25, .len = {.N = 30, .E = 30, .S = 66, .W = 30}, .r = 8});
            r.stage.paths["me"] = r.move('S', 'N');
            r.stage.paths["red"] = r.move('W', 'E');
            return r.stage;
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = {{0, "line-26", 8.3}, {1, "line-17.7", 8.3}, {5.3, "line", 0}, {6.8, "line", 0}, {10.4, "ring+2", 5.5}, {12.5, "pass:E+2", 6}, {14.55, "exit+2", 6}, {17, "out+6", 8}}, .blink = {{12.55, 1}, {16.2, 0}}},
              {.id = "red", .kind = "car", .path = "red", .keys = {{0, "pass:S-33", 7}, {12, "pass:S+51", 7}}, .colour = RED, .blink = {{5, 1}, {8.2, 0}}, .name = "theory.who.red"},
          },
      .steps =
          {
              {.at = 3.4, .answers = {"me", "red"}, .right = "red", .ref = "§ 8 Abs. 1a StVO", .ask = "theory.ask.first"},
              {.at = 6.6, .answers = {"none", "right", "left"}, .right = "none", .ref = "§ 8 Abs. 1a StVO"},
              {.at = 12.5, .answers = {"right", "none"}, .right = "right", .ref = "§ 8 Abs. 1a, § 9 Abs. 1 StVO"},
          },
      .end = 17.4,
      .view = viewAt(0, 6, 0.4, -0.85, 46),
  };
}

/** 13 · The green arrow beside a red light. */
LessonDef GREEN_ARROW() {
  return {
      .id = "greenArrow",
      .stage =
          [] {
            const Road road{.lanes = 1, .laneW = 3.25, .walk = 2.5, .centre = "dash"};
            const ArmXing furt{.at = 2, .w = 4, .kind = "furt"};
            Cross c = crossroads({
                .ns = road,
                .ew = road,
                .r = 6,
                .arms =
                    {
                        .N = {.len = 30, .line = "stop", .xing = furt, .light = ArmLight{"ns"}, .pedLight = "pns"},
                        .E = {.len = 30, .line = "stop", .xing = furt, .light = ArmLight{"ew"}, .pedLight = "pew"},
                        .S = {.len = 66, .line = "stop", .xing = furt, .light = ArmLight{"ns", true}, .pedLight = "pns"},
                        .W = {.len = 30, .line = "stop", .xing = furt, .light = ArmLight{"ew"}, .pedLight = "pew"},
                    },
            });
            Stage& st = c.stage;
            st.paths["me"] = c.move('S', 'E');
            st.paths["white"] = c.move('W', 'E');
            // somebody crossing the south arm from east to west, on their green
            const double z = c.xingAt('S');
            st.paths["walker"] = polyPath({4.6, 16, 4.6, z + 4.15, 5.4, z, -5.4, z, -4.6, z + 4.15, -4.6, z + 14.75});
            st.surfaces.push_back(laneArrow(1.625, 20, 0, "right"));
            return st;
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = {{0, "line-30", 8.3}, {3, "line-11", 6}, {6.7, "line", 0}, {9, "line", 0}, {12.4, "line+11", 6.5}, {14, "line+21.6", 6.8}}, .blink = {{0, 1}, {12.6, 0}}},
              {.id = "white", .kind = "car", .path = "white", .keys = {{0, 0, 9}, {10, 90, 9}}, .colour = WHITE, .name = "theory.who.white"},
              {.id = "walker", .kind = "person", .path = "walker", .keys = {{0, 5.45, 1.25}, {24, 35.45, 1.25}}, .look = 41, .name = "theory.who.walker"},
          },
      .steps =
          {
              {.at = 3, .answers = {"stop", "roll", "nothing"}, .right = "stop", .ref = "§ 37 Abs. 2 StVO, Zeichen 720"},
              {.at = 7, .answers = {"me", "walker"}, .right = "walker", .ref = "§ 37 Abs. 2 StVO", .ask = "theory.ask.first"},
          },
      .end = 14,
      // the lights of the junction: red for you (with the arrow), green across; the people crossing your road walk with the green across
      .lights = {{"ns", {{0, 'R'}}}, {"ew", {{0, 'G'}}}, {"pns", {{0, 'G'}}}, {"pew", {{0, 'R'}}}},
      .view = viewAt(1, 8, 0.5, -0.68, 30),
  };
}
}  // namespace lessons
}  // namespace

const std::vector<LessonDef>& THEORY() {
  static const auto* list = [] {
    auto* l = new std::vector<LessonDef>{lessons::RBL(), lessons::BEND(), lessons::LEFT_TURN(), lessons::ZEBRA(), lessons::TRAM_LESSON(), lessons::CORRIDOR(), lessons::SCHOOL_BUS(), lessons::OVERTAKE(), lessons::BLIND_SPOT(), lessons::ZIP(), lessons::MOTORWAY(), lessons::ROUNDABOUT(), lessons::GREEN_ARROW()};
    for (const LessonDef& m : MORE_THEORY()) l->push_back(m);
    l->push_back(LEVEL_CROSSING());
    l->push_back(LEVEL_CROSSING_QUEUE());
    return l;
  }();
  return *list;
}

const LessonDef* theoryLesson(const std::string& id) {
  for (const LessonDef& l : THEORY())
    if (l.id == id) return &l;
  return nullptr;
}

// ------------------------------------------------------------------- texts
namespace theoryKeys {
std::string title(const std::string& id) { return "theory." + id + ".title"; }
std::string intro(const std::string& id) { return "theory." + id + ".intro"; }
std::string text(const std::string& id, int n) { return "theory." + id + "." + std::to_string(n) + ".text"; }
std::string ask(const std::string& id, int n, const StepDef& step) { return !step.ask.empty() ? step.ask : "theory." + id + "." + std::to_string(n) + ".ask"; }
std::string why(const std::string& id, int n) { return "theory." + id + "." + std::to_string(n) + ".why"; }
}  // namespace theoryKeys

std::string answerKey(const LessonDef& lesson, int n, const std::string& answer) {
  if (answer.rfind('!', 0) == 0) return "theory.a." + answer.substr(1);
  for (const ActorDef& a : lesson.actors)
    if (a.id == answer) return actorName(a);
  return "theory." + lesson.id + "." + std::to_string(n) + "." + answer;
}

std::string actorName(const ActorDef& a) {
  if (!a.name.empty()) return a.name;
  return a.kind == "me" ? "theory.who.me" : "theory.who." + a.kind;
}

std::vector<std::string> allTheoryKeys() {
  std::vector<std::string> keys;
  std::set<std::string> seen;
  const auto add = [&](const std::string& k) {
    if (seen.insert(k).second) keys.push_back(k);
  };
  for (const LessonDef& l : THEORY()) {
    add(theoryKeys::title(l.id));
    add(theoryKeys::intro(l.id));
    for (size_t i = 0; i < l.steps.size(); i++) {
      const int n = static_cast<int>(i) + 1;
      const StepDef& s = l.steps[i];
      add(theoryKeys::text(l.id, n));
      add(theoryKeys::ask(l.id, n, s));
      add(theoryKeys::why(l.id, n));
      for (const std::string& a : s.answers) add(answerKey(l, n, a));
    }
    for (const ActorDef& a : l.actors) add(actorName(a));
  }
  return keys;
}

// ------------------------------------------------------------------ motion

double markAt(const Path& path, const std::variant<double, std::string>& at) {
  if (const double* n = std::get_if<double>(&at)) return *n;
  const std::string s = trim(std::get<std::string>(at));
  static const std::regex RE(R"(^(.+?)([+-]\d+(?:\.\d+)?)?$)");
  std::smatch m;
  const bool ok = std::regex_match(s, m, RE);
  const std::string name = ok ? m[1].str() : std::get<std::string>(at);
  const auto it = path.marks.find(name);
  if (it == path.marks.end()) throw std::runtime_error("no mark \"" + name + "\" on the path");
  return it->second + (ok && m[2].matched ? std::stod(m[2].str()) : 0);
}

std::shared_ptr<Scene> prepare(const LessonDef& lesson) {
  auto scene = std::make_shared<Scene>();
  scene->lesson = &lesson;
  scene->stage = lesson.stage();
  for (const ActorDef& def : lesson.actors) {
    const auto it = scene->stage.paths.find(def.path);
    if (it == scene->stage.paths.end()) throw std::runtime_error(lesson.id + ": no path \"" + def.path + "\" for " + def.id);
    const Path& path = it->second;
    ActorRun r{.def = &def, .path = &path, .foot = FOOT(def.kind), .keys = {}};
    for (const Key& k : def.keys) r.keys.push_back({k.t, markAt(path, k.at), k.v});
    scene->actors.push_back(std::move(r));
  }
  return scene;
}

Progress& progressAt(const std::vector<ResolvedKey>& keys, double t, Progress& out) {
  const size_t n = keys.size();
  const ResolvedKey& first = keys[0];
  out.started = t >= first.t;
  if (t <= first.t) {
    out.s = first.s;
    out.v = first.v;
    out.a = 0;
    return out;
  }
  const ResolvedKey& last = keys[n - 1];
  if (t >= last.t) {
    out.s = last.s + last.v * (t - last.t);
    out.v = last.v;
    out.a = 0;
    return out;
  }
  size_t i = 0;
  while (i + 2 < n && keys[i + 1].t <= t) i++;
  const ResolvedKey& k0 = keys[i];
  const ResolvedKey& k1 = keys[i + 1];
  const double h = k1.t - k0.t;
  const double u = (t - k0.t) / h;
  const double u2 = u * u;
  const double u3 = u2 * u;
  const double m0 = k0.v * h;
  const double m1 = k1.v * h;
  out.s = (2 * u3 - 3 * u2 + 1) * k0.s + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * k1.s + (u3 - u2) * m1;
  out.v = ((6 * u2 - 6 * u) * k0.s + (3 * u2 - 4 * u + 1) * m0 + (-6 * u2 + 6 * u) * k1.s + (3 * u2 - 2 * u) * m1) / h;
  out.a = ((12 * u - 6) * k0.s + (6 * u - 4) * m0 + (-12 * u + 6) * k1.s + (6 * u - 2) * m1) / (h * h);
  return out;
}

namespace {
double smooth(double x) {
  const double c = std::max(0.0, std::min(1.0, x));
  return c * c * (3 - 2 * c);
}
}  // namespace

SideAt sideAt(const ActorDef& def, double t) {
  double from = 0;
  double off = 0;
  double rate = 0;
  for (const auto& [t0, to, dur] : def.side) {
    if (t <= t0) break;
    const double x = (t - t0) / dur;
    const double c = std::max(0.0, std::min(1.0, x));
    off = from + (to - from) * smooth(x);
    rate = x < 1 ? ((to - from) * 6 * c * (1 - c)) / dur : 0;
    from = to;
  }
  return {off, rate};
}

int blinkAt(const ActorDef& def, double t) {
  int b = 0;
  for (const auto& [t0, state] : def.blink)
    if (t >= t0) b = state;
  return b;
}

namespace {
/** A point `s` along the path, moved `off` to the left of it. */
std::array<double, 2> offPoint(const Path& path, double s, double off) {
  PathPoint pp;
  pathPoint(path, s, pp);
  return {pp.x + pp.dz * off, pp.z - pp.dx * off};
}

double wrapPi(double a) {
  double d = a;
  while (d > PI_) d -= 2 * PI_;
  while (d < -PI_) d += 2 * PI_;
  return d;
}
}  // namespace

double timeAt(const std::vector<ResolvedKey>& keys, double s, double t) {
  Progress back;
  double lo = keys[0].t;
  if (progressAt(keys, lo, back).s >= s) return lo;
  double hi = t;
  for (int i = 0; i < 22; i++) {
    const double mid = (lo + hi) / 2;
    if (progressAt(keys, mid, back).s < s) lo = mid;
    else hi = mid;
  }
  return (lo + hi) / 2;
}

Pose& poseAt(const ActorRun& r, double t, Pose& out) {
  Progress p;
  progressAt(r.keys, t, p);
  const Footprint& f = r.foot;
  const SideAt side = sideAt(*r.def, t);
  const double off = side.off;
  const double rate = side.rate;
  out.s = p.s;
  out.v = p.v;
  out.a = p.a;
  out.blink = blinkAt(*r.def, t);
  // there from its first key until all of it has gone past the end of its path
  out.visible = p.started && p.s - f.length <= r.path->len + 0.5;
  const double mid = p.s - f.front;
  // (the way it is heading drawn sideways as much as it moves sideways at that speed)
  const double lean = std::atan2(rate, std::max(std::abs(p.v), 1.0));
  if (f.wheelbase > 0) {
    // the front axle where the script has it; the rear axle on the track the front one took – sideways
    // where the front was when it passed the place the rear is at now
    const double sf = mid + f.wheelbase / 2;
    const double sr = mid - f.wheelbase / 2;
    const double offR = sideAt(*r.def, timeAt(r.keys, p.s - f.wheelbase, t)).off;
    const auto [fx, fz] = offPoint(*r.path, sf, off);
    const auto [bx, bz] = offPoint(*r.path, sr, offR);
    out.x = (fx + bx) / 2;
    out.z = (fz + bz) / 2;
    const double body = yawOf(fx - bx, fz - bz);
    out.yaw = body;
    // the front wheels point where the front axle goes: along the path there, drawn sideways with it
    PathPoint pq;
    pathPoint(*r.path, sf + 0.3, pq);
    const double ahead = yawOf(pq.dx, pq.dz) + lean;
    out.steer = -std::max(-0.6, std::min(0.6, wrapPi(ahead - body) * 1.6));
  } else {
    // a person (its feet), or a tram (its nose – the drawing places it car by car from there)
    const auto [x, z] = offPoint(*r.path, mid, off);
    out.x = x;
    out.z = z;
    const auto [ax, az] = offPoint(*r.path, mid + 0.35, off);
    const auto [bx, bz] = offPoint(*r.path, mid - 0.35, off);
    out.yaw = yawOf(ax - bx, az - bz) + lean;
    // (somebody who turns on the spot: a police officer, a flagman)
    if (!r.def->face.empty()) out.yaw = faceAt(*r.def, t, out.yaw);
    out.steer = 0;
  }
  return out;
}

std::vector<double> corners(const ActorRun& r, const Pose& p, double grow) {
  const Footprint& f = r.foot;
  const double fx = -std::sin(p.yaw);
  const double fz = -std::cos(p.yaw);
  // (to the left of the heading)
  const double lx = fz;
  const double lz = -fx;
  const double hw = f.width / 2 + grow;
  const bool tram = r.def->kind == "tram";
  const double ahead = (tram ? 0 : f.wheelbase > 0 ? f.front : f.length / 2) + grow;
  const double behind = (tram ? f.length : f.wheelbase > 0 ? f.length - f.front : f.length / 2) + grow;
  return {
      p.x + fx * ahead + lx * hw, p.z + fz * ahead + lz * hw,
      p.x + fx * ahead - lx * hw, p.z + fz * ahead - lz * hw,
      p.x - fx * behind - lx * hw, p.z - fz * behind - lz * hw,
      p.x - fx * behind + lx * hw, p.z - fz * behind + lz * hw,
  };
}

bool outlinesTouch(const std::vector<double>& a, const std::vector<double>& b) {
  for (const std::vector<double>* poly : {&a, &b}) {
    const size_t n = poly->size();
    for (size_t i = 0; i < n; i += 2) {
      const size_t j = (i + 2) % n;
      const double nx = (*poly)[i + 1] - (*poly)[j + 1];
      const double nz = (*poly)[j] - (*poly)[i];
      double amin = JS_INF;
      double amax = -JS_INF;
      double bmin = JS_INF;
      double bmax = -JS_INF;
      for (size_t k = 0; k < a.size(); k += 2) {
        const double d = a[k] * nx + a[k + 1] * nz;
        amin = std::min(amin, d);
        amax = std::max(amax, d);
      }
      for (size_t k = 0; k < b.size(); k += 2) {
        const double d = b[k] * nx + b[k + 1] * nz;
        bmin = std::min(bmin, d);
        bmax = std::max(bmax, d);
      }
      if (amax < bmin || bmax < amin) return false;
    }
  }
  return true;
}

LightState lightAt(const Scene& scene, const std::string& id, double t) {
  LightState s = 'O';
  const auto it = scene.lesson->lights.find(id);
  if (it == scene.lesson->lights.end()) return s;
  for (const auto& [t0, state] : it->second)
    if (t >= t0) s = state;
  return s;
}

namespace {
bool inSurface(const std::vector<double>& ring, const std::vector<std::vector<double>>& holes, double x, double z) {
  // (as theorystage.covers – kept here so the hot path needs no allocation)
  bool inside = false;
  const size_t n = ring.size();
  for (size_t i = 0, j = n - 2; i < n; j = i, i += 2) {
    const double zi = ring[i + 1];
    const double zj = ring[j + 1];
    if ((zi > z) != (zj > z) && x < ((ring[j] - ring[i]) * (z - zi)) / (zj - zi) + ring[i]) inside = !inside;
  }
  if (!inside) return false;
  static const std::vector<std::vector<double>> none;
  for (const std::vector<double>& h : holes)
    if (inSurface(h, none, x, z)) return false;
  return true;
}
}  // namespace

double actorLift(const Scene& scene, const ActorRun& r, double x, double z) {
  if (r.def->kind == "person" || r.def->kind == "child") {
    double best = LIFT("asphalt");
    for (const Surface& s : scene.stage.surfaces) {
      if (s.kind != "walk" && s.kind != "island") continue;
      if (LIFT(s.kind) > best && inSurface(s.ring, s.holes, x, z)) best = LIFT(s.kind);
    }
    return best;
  }
  return LIFT("asphalt");
}

// -------------------------------------------------------------------- a run

const StepDef* TheoryRun::current() const {
  const std::vector<StepDef>& steps = scene->lesson->steps;
  return step >= 0 && step < static_cast<int>(steps.size()) ? &steps[static_cast<size_t>(step)] : nullptr;
}

bool TheoryRun::advance(double dt) {
  if (phase != "play") return false;
  const StepDef* next = current();
  const double tn = t + dt;
  if (next && tn >= next->at) {
    t = next->at;
    phase = "ask";
    picked = std::nullopt;
    return true;
  }
  const double end = scene->lesson->end;
  if (!next && tn >= end) {
    t = end;
    phase = "end";
    return true;
  }
  t = tn;
  return false;
}

std::optional<bool> TheoryRun::answer(const std::string& id) {
  const StepDef* s = current();
  if (phase != "ask" || !s || std::find(s->answers.begin(), s->answers.end(), id) == s->answers.end()) return std::nullopt;
  picked = id;
  phase = "told";
  const bool ok = id == s->right;
  if (ok) right++;
  return ok;
}

void TheoryRun::goOn() {
  if (phase != "told") return;
  step++;
  phase = "play";
  picked = std::nullopt;
}

void TheoryRun::restart() {
  t = 0;
  step = 0;
  phase = "play";
  picked = std::nullopt;
  right = 0;
}

void TheoryRun::skipTo(double to) {
  restart();
  while (current() && current()->at < to) {
    right++;
    step++;
  }
  t = std::max(0.0, std::min(to, scene->lesson->end));
  const StepDef* next = current();
  if (next && next->at <= t + 1e-9) {
    t = next->at;
    phase = "ask";
  } else if (!next && t >= scene->lesson->end) phase = "end";
}

}  // namespace fs::theory
