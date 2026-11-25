// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/theoryscenes.ts @ v1.7.7
#include "game/theoryscenes.h"

#include <algorithm>
#include <cmath>

#include "game/theorystage.h"

namespace fs::theory {

namespace {
constexpr double PI_ = 3.14159265358979323846;
}

// ------------------------------------------------------------------ timing

std::vector<Key> plan(double t, double s, double v, const std::vector<Leg>& legs) {
  std::vector<Key> keys = {{t, s, v}};
  for (const Leg& l : legs) {
    if (l.wait) t += *l.wait;
    else if (l.to) {
      t += (2 * (*l.to - s)) / (v + l.v);
      s = *l.to;
      v = l.v;
    } else {
      s += (*l.dt * (v + l.v)) / 2;
      t += *l.dt;
      v = l.v;
    }
    keys.emplace_back(t, s, v);
  }
  return keys;
}

namespace {
/** Standing still at `s` (a parked lorry, a police officer). */
std::vector<Key> stand(double s) { return {{0, s, 0}}; }

// ------------------------------------------------------------- what people do
double smooth(double x) {
  const double c = std::max(0.0, std::min(1.0, x));
  return c * c * (3 - 2 * c);
}

double wrap(double a) {
  double d = a;
  while (d > PI_) d -= 2 * PI_;
  while (d < -PI_) d += 2 * PI_;
  return d;
}

/** how long a police officer takes to move his arms from one sign to the next, and a flagman to turn his sign (s) */
constexpr double MOVE = 0.6;

/** The arms of each sign (the original's ARMS): the right arm swung forward and out to the side, the left arm the same (rad – render/people.ts PersonPose.arms). */
std::array<double, 4> armsOfSign(const Gesture& g) {
  // the basic position: the arms hanging
  if (g == "down") return {0.03, 0.07, 0.03, 0.07};
  // both arms out to the sides, level with the shoulders
  if (g == "arms") return {0, 1.52, 0, 1.52};
  // the right arm straight up
  if (g == "raise") return {3.0, 0.08, 0.03, 0.07};
  // the flagman holds his sign up in front of him with the right hand ('stop', 'go')
  return {1.15, 0.22, 0.03, 0.07};
}

/** hands on the handles of a rollator */
constexpr std::array<double, 4> ROLLATOR_ARMS = {0.62, 0.1, 0.62, 0.1};
}  // namespace

double faceAt(const ActorDef& def, double t, double base) {
  double from = base;
  double yaw = base;
  for (const auto& [t0, to, dur] : def.face) {
    if (t <= t0) break;
    yaw = from + wrap(to - from) * smooth((t - t0) / std::max(1e-3, dur));
    from = to;
  }
  return wrap(yaw);
}

std::optional<GestureNow> gestureAt(const ActorDef& def, double t) {
  std::optional<Gesture> sign;
  std::optional<Gesture> was;
  double since = 0;
  for (const auto& [t0, g] : def.gesture) {
    if (t < t0) break;
    was = sign ? *sign : g;
    sign = g;
    since = t0;
  }
  if (sign && was) return GestureNow{.sign = *sign, .since = since, .was = *was};
  return std::nullopt;
}

std::optional<std::array<double, 4>> armsAt(const ActorDef& def, double t) {
  if (def.role == "senior") return ROLLATOR_ARMS;
  const std::optional<GestureNow> g = gestureAt(def, t);
  if (!g) return std::nullopt;
  const std::array<double, 4> a = armsOfSign(g->was);
  const std::array<double, 4> b = armsOfSign(g->sign);
  const double k = smooth((t - g->since) / MOVE);
  return std::array<double, 4>{a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k, a[3] + (b[3] - a[3]) * k};
}

double paddleAt(const ActorDef& def, double t) {
  const std::optional<GestureNow> g = gestureAt(def, t);
  if (!g) return 0;
  const double a = g->was == "go" ? 1 : 0;
  const double b = g->sign == "go" ? 1 : 0;
  return a + (b - a) * smooth((t - g->since) / MOVE);
}

int beamAt(const ActorDef& def, double t) {
  int b = 0;
  for (const auto& [t0, state] : def.beam)
    if (t >= t0) b = state;
  return b;
}

// ------------------------------------------------------------------ places
namespace {
namespace lessons {
constexpr int BLUE = 0x2f5f8a;
constexpr int RED = 0xa8231b;
constexpr int WHITE = 0xe2e2dc;
constexpr int GREEN = 0x2f7a46;
constexpr int SILVER = 0x9aa1a8;
/** facing south (towards you coming up from the south), east */
constexpr double SOUTH = PI_;
constexpr double EAST = -PI_ / 2;

LessonView viewAt(double x, double z, double yaw, double pitch, double dist) { return LessonView{.at = std::array<double, 2>{x, z}, .yaw = yaw, .pitch = pitch, .dist = dist}; }
LessonView viewOf(const std::string& id, double ahead, double yaw, double pitch, double dist) { return LessonView{.at = id, .ahead = ahead, .yaw = yaw, .pitch = pitch, .dist = dist}; }

/** A street in town north–south: one lane each way (laneW), pavements (walk), the broken middle line. */
Stage townRoad(double z0, double z1, double laneW, double walk) {
  Stage st = straight({.z0 = z0, .z1 = z1, .bounds = {{-laneW - walk}, {-laneW}, {laneW}, {laneW + walk}}, .strips = {"walk", "asphalt", "walk"}, .kerbs = {1, 2}});
  pushAll(st.surfaces, dashes(0, z0, 0, z1, 0.12, 3, 6));
  return st;
}

/**
 * A country road north–south from z = 72 to −72: one lane each way, the
 * broken middle line and the edge lines; the guide posts (Leitpfosten) on
 * both sides every 50 m – as on every country road, a measure of how far
 * one sees.
 */
Stage countryRoad(double laneW = 3.25, double walkway = 0) {
  const std::vector<Bound> bounds = walkway > 0 ? std::vector<Bound>{{-laneW - walkway}, {-laneW}, {laneW}, {laneW + walkway}} : std::vector<Bound>{{-laneW}, {laneW}};
  Stage st = straight({.z0 = 72,
                       .z1 = -72,
                       .bounds = bounds,
                       .strips = walkway > 0 ? std::vector<std::string>{"walk", "asphalt", "walk"} : std::vector<std::string>{"asphalt"},
                       .kerbs = walkway > 0 ? std::vector<int>{1, 2} : std::vector<int>{}});
  pushAll(st.surfaces, along({0}, 72, -72, 0.12, std::array<double, 2>{6, 12}));
  pushAll(st.surfaces, along({laneW - 0.3}, 72, -72, 0.15));
  pushAll(st.surfaces, along({-laneW + 0.3}, 72, -72, 0.15));
  std::vector<PropPlace> props;
  if (walkway == 0) {
    for (const double z : {60.0, 10.0, -40.0}) {
      props.push_back({.kind = "post", .x = laneW + 0.9, .z = z, .yaw = yawOf(0, 1)});
      props.push_back({.kind = "post", .x = -laneW - 0.9, .z = z, .yaw = yawOf(0, -1)});
    }
  }
  st.props = props;
  return st;
}

/** Trees along both sides of a country road (a forest): rows from the road out, a little out of line. */
void forest(Stage& st, double near, double from, double to) {
  std::vector<PropPlace>& props = st.props;
  int k = 0;
  for (double z = from; z > to; z -= 5.5) {
    for (const double side : {1.0, -1.0}) {
      for (int row = 0; row < 3; row++) {
        k++;
        const double jitter = static_cast<double>((k * 7919) % 13) / 13 - 0.5;
        props.push_back({.kind = "tree", .x = side * (near + row * 4.2 + jitter * 1.6), .z = z + jitter * 2.4 + row * 1.7, .size = 0.85 + (static_cast<double>((k * 31) % 7) / 7) * 0.4});
      }
    }
  }
}

// ------------------------------------------------------------- 1 · parking
Stage parkingStage() {
  const Road ns{.lanes = 1, .laneW = 3.5, .walk = 2.5, .centre = "dash"};
  const Road ew{.lanes = 1, .laneW = 3.25, .walk = 2.5, .centre = "dash"};
  Cross c = crossroads({
      .ns = ns,
      .ew = ew,
      .r = 5,
      .arms = {.N = {.len = 36}, .E = {.len = 22}, .S = {.len = 68, .signs = {{56, {"283"}}, {34, {"286"}}}}, .W = {.len = 22}},
  });
  c.stage.paths["me"] = c.move('S', 'N');
  // your passenger: out by the door on the right, onto the pavement and away south
  c.stage.paths["walker"] = polyPath({2.9, 19.7, 4.3, 20.2, 4.75, 21.5, 4.75, 62});
  return c.stage;
}

/** 1 · Stopping and parking: Zeichen 283, Zeichen 286, how far from a junction. */
LessonDef PARKING() {
  const double parkLine = parkingStage().paths.at("me").marks.at("line");
  return {
      .id = "parkingSigns",
      .stage = parkingStage,
      .actors =
          {
              {
                  .id = "me",
                  .kind = "me",
                  .path = "me",
                  // through the zone of 283, a stop at the kerb under 286, on over the junction and parked beyond it
                  .keys = plan(0, parkLine - 60, 10, {legDt(1.5, 10), legTo(parkLine - 13.9, 0), legWait(3.6), legDt(2.5, 8), legTo(parkLine + 27, 0), legWait(1)}),
                  .side = {{5.2, -0.62, 2.2}, {11.4, 0, 1.8}, {17.4, -0.62, 2.4}},
                  .blink = {{4.8, 1}, {7.8, 0}, {11.0, -1}, {13.2, 0}, {16.8, 1}, {21, 0}},
              },
              {.id = "walker", .kind = "person", .path = "walker", .keys = plan(8.3, 0, 0.8, {legDt(1, 1.3), legDt(12, 1.3)}), .look = 13, .name = "theory.who.passenger", .leaves = "me"},
          },
      .steps =
          {
              {.at = 1.5, .answers = {"!no", "!yes"}, .right = "!no", .ref = "Anlage 2 StVO, Zeichen 283"},
              {.at = 7.9, .answers = {"out", "never", "park"}, .right = "out", .ref = "§ 12 Abs. 2 StVO, Anlage 2 StVO, Zeichen 286"},
              {.at = 15.2, .answers = {"m3", "m5", "m10"}, .right = "m5", .ref = "§ 12 Abs. 3 Nr. 1 StVO"},
          },
      .end = 22.5,
      .view = viewOf("me", 6, 0.6, -0.62, 28),
  };
}

// ---------------------------------------------------- 2 · following distance
/** 2 · The distance to the car ahead: half the speedometer, the second you need to react, the stopping distance. */
LessonDef FOLLOW() {
  return {
      .id = "followingDistance",
      .stage =
          [] {
            Stage st = countryRoad();
            st.signs.push_back({4.15, 66, yawOf(0, 1), {"274-60"}});
            st.paths["lane"] = lanePath(1.625, 72, -72);
            st.paths["other"] = lanePath(-1.625, -72, 72);
            return finish(std::move(st));
          },
      .actors =
          {
              // 60 km/h, 30 m behind the red car; it brakes at 3.2 s, you a second later – and stop with room to spare
              {.id = "me", .kind = "me", .path = "lane", .keys = plan(0, 0, 16.67, {legDt(4.2, 16.67), legDt(4.76, 0), legWait(1.2)})},
              {.id = "red", .kind = "car", .path = "lane", .keys = plan(0, 34.3, 16.67, {legDt(3.2, 16.67), legDt(4.76, 0), legWait(2.2)}), .colour = RED, .name = "theory.who.red"},
              {.id = "white", .kind = "car", .path = "other", .keys = plan(0, 18, 15, {legDt(10, 15)}), .colour = WHITE, .name = "theory.who.white"},
          },
      .steps =
          {
              {.at = 1.5, .answers = {"m15", "m30", "m60"}, .right = "m30", .ref = "§ 4 Abs. 1 StVO"},
              {.at = 3.3, .answers = {"m5", "m17", "m30"}, .right = "m17", .ref = "§ 3 Abs. 1, § 4 Abs. 1 StVO"},
              {.at = 5.6, .answers = {"m35", "m54", "m100"}, .right = "m54", .ref = "§ 3 Abs. 1, § 4 Abs. 1 StVO"},
          },
      .end = 10.2,
      .view = viewOf("me", 16, 0.55, -0.42, 30),
  };
}

// ------------------------------------------------- 3 · obstacle and cyclist
/** 3 · A country road: passing a cyclist (2 m), then a broken-down lorry in your lane with a car coming. */
LessonDef NARROW() {
  return {
      .id = "narrowPass",
      .stage =
          [] {
            Stage st = countryRoad();
            st.paths["lane"] = lanePath(1.625, 72, -72);
            st.paths["truck"] = lanePath(1.9, 72, -72);
            st.paths["bike"] = lanePath(2.4, 72, -72);
            st.paths["other"] = lanePath(-1.625, -72, 72);
            // the warning triangle behind the lorry
            st.props.push_back({.kind = "triangle", .x = 2.6, .z = 8, .yaw = yawOf(0, 1)});
            return finish(std::move(st));
          },
      .actors =
          {
              {
                  .id = "me",
                  .kind = "me",
                  .path = "lane",
                  .keys = plan(0, -16, 15, {legDt(4.6, 15), legTo(87, 0), legWait(1.13), legDt(3, 8), legDt(2, 13), legDt(2.5, 13)}),
                  .side = {{1.2, 2.5, 2.0}, {4.4, 0, 2.0}, {10, 2.5, 2.4}, {14.3, 0, 2.0}},
                  .blink = {{0.6, -1}, {3.4, 0}, {3.9, 1}, {5.6, 0}, {9.4, -1}, {12, 0}, {13.9, 1}, {15.6, 0}},
              },
              {.id = "bike", .kind = "bike", .path = "bike", .keys = plan(0, 22, 4.5, {legDt(9.5, 4.5), legTo(77.7, 0), legWait(2)}), .colour = 0x2f8a4a, .name = "theory.who.bike"},
              {.id = "truck", .kind = "truck", .path = "truck", .keys = stand(102.2), .blink = {{0, 2}}, .name = "theory.who.truck"},
              {.id = "white", .kind = "car", .path = "other", .keys = plan(0, -75, 13, {legDt(17.5, 13)}), .colour = WHITE, .name = "theory.who.white"},
          },
      .steps =
          {
              {.at = 1.0, .answers = {"m1", "m15", "m2"}, .right = "m2", .ref = "§ 5 Abs. 4 StVO"},
              {.at = 7.4, .answers = {"me", "white"}, .right = "white", .ref = "§ 6 StVO", .ask = "theory.ask.first"},
          },
      .end = 16.5,
      .view = viewOf("me", 10, 0.6, -0.55, 30),
      .gap = LessonGap{"me", "bike", 3.4, 4.4},
  };
}

// ------------------------------------------------------ 4 · bus at the stop
Stage stopStage() {
  Stage st = townRoad(70, -65, 3.25, 2.5);
  st.signs.push_back({4.1, -12, yawOf(0, 1), {"224"}});
  // the zigzag along the kerb of the stop (Zeichen 299)
  std::vector<double> zig;
  int k = 0;
  for (double z = 4; z >= -12; z -= 1.5, k++) {
    zig.push_back(k % 2 ? 2.35 : 3.05);
    zig.push_back(z);
  }
  for (size_t i = 0; i + 3 < zig.size(); i += 2) st.surfaces.push_back(bar(zig[i], zig[i + 1], zig[i + 2], zig[i + 3], 0.12));
  st.paths["lane"] = lanePath(1.625, 70, -65);
  // two people get off by the front door and walk away along the pavement
  st.paths["p1"] = polyPath({2.95, -8.4, 4.3, -8.0, 4.6, -6, 4.6, 40});
  st.paths["p2"] = polyPath({2.95, -8.4, 4.3, -8.8, 4.9, -11, 4.9, -50});
  return finish(std::move(st));
}

/** 4 · A bus at its stop: hazard lights on, then it indicates to pull out. */
LessonDef BUS_STOP() {
  return {
      .id = "busStopPullOut",
      .stage = stopStage,
      .actors =
          {
              {.id = "bus", .kind = "bus", .path = "lane", .keys = plan(0, 80, 0, {legWait(9.6), legDt(4, 5), legDt(2, 7), legDt(6, 7)}), .blink = {{0, 2}, {7.6, -1}, {14.5, 0}}, .name = "theory.who.lineBus", .display = "52 Baunatal"},
              {.id = "me", .kind = "me", .path = "lane", .keys = plan(0, 0, 9, {legTo(46, 2), legTo(52.5, 0), legWait(1.2), legDt(3, 6), legDt(4, 7)})},
              {.id = "p1", .kind = "person", .path = "p1", .keys = plan(0.4, 0, 0.8, {legDt(1, 1.3), legDt(20, 1.3)}), .look = 37, .name = "theory.who.walker", .leaves = "bus"},
              {.id = "p2", .kind = "person", .path = "p2", .keys = plan(2.2, 0, 0.8, {legDt(1, 1.2), legDt(20, 1.2)}), .look = 52, .name = "theory.who.walker", .leaves = "bus"},
          },
      .steps =
          {
              {.at = 2.4, .answers = {"walk", "slow30", "normal"}, .right = "walk", .ref = "§ 20 Abs. 4 StVO"},
              {.at = 8.4, .answers = {"me", "bus"}, .right = "bus", .ref = "§ 20 Abs. 5 StVO", .ask = "theory.ask.first"},
          },
      .end = 21,
      .view = viewOf("me", 10, -0.6, -0.58, 30),
  };
}

// ------------------------------------------------------------- 5 · at night
/** 5 · At night on a country road: dipping for the oncoming car, and only as fast as you can see. */
LessonDef NIGHT() {
  return {
      .id = "nightDrive",
      .stage =
          [] {
            Stage st = countryRoad();
            st.paths["lane"] = lanePath(1.625, 72, -72);
            st.paths["other"] = lanePath(-1.625, -72, 72);
            // somebody in dark clothes walking along the edge of the road (no pavement): on the left side, facing the traffic
            st.paths["walker"] = polyPath({-2.9, -51, -2.9, -72});
            return finish(std::move(st));
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "lane", .keys = plan(0, 0, 15, {legDt(3, 15), legDt(3, 12), legDt(4, 12)}), .beam = {{0, 2}, {2.7, 1}, {5.6, 2}}},
              {.id = "white", .kind = "car", .path = "other", .keys = plan(0, 28, 14, {legDt(10, 14)}), .colour = WHITE, .name = "theory.who.white", .beam = {{0, 1}}},
              {.id = "walker", .kind = "person", .path = "walker", .keys = plan(0, 0, 1.2, {legDt(10, 1.2)}), .look = 64, .name = "theory.who.walker"},
          },
      .steps =
          {
              {.at = 2.6, .answers = {"dip", "keep", "flash"}, .right = "dip", .ref = "§ 17 Abs. 2 StVO"},
              {.at = 5.4, .answers = {"sight", "limit", "high"}, .right = "sight", .ref = "§ 3 Abs. 1 StVO"},
          },
      .end = 10,
      .view = viewOf("me", 14, 0.5, -0.42, 26),
      .sky = LessonSky{.sun = -6},
  };
}

// -------------------------------------------------------------- 6 · in fog
/** 6 · Fog: under 50 m to see – at most 50 km/h, and the rear fog light. */
LessonDef FOG() {
  return {
      .id = "fogWall",
      .stage =
          [] {
            Stage st = countryRoad();
            st.paths["lane"] = lanePath(1.625, 72, -72);
            return finish(std::move(st));
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "lane", .keys = plan(0, 0, 16.5, {legDt(2.4, 13.8), legDt(6.6, 13.2)}), .beam = {{0, 1}}},
              {.id = "red", .kind = "car", .path = "lane", .keys = plan(0, 52, 12.5, {legDt(9, 12.5)}), .colour = RED, .name = "theory.who.red", .beam = {{0, 1}}},
          },
      .steps =
          {
              {.at = 1.4, .answers = {"k50", "k80", "k100"}, .right = "k50", .ref = "§ 3 Abs. 1 StVO"},
              {.at = 4.2, .answers = {"fog", "high", "none"}, .right = "fog", .ref = "§ 17 Abs. 3 StVO"},
          },
      .end = 9,
      .view = viewOf("me", 6, 0.5, -0.36, 16),
      .sky = LessonSky{.fog = 45},
  };
}

// ----------------------------------------------------------- 7 · an accident
/** 7 · An accident ahead: securing the place, the warning triangle, first aid. */
LessonDef ACCIDENT() {
  return {
      .id = "accidentScene",
      .stage =
          [] {
            Stage st = countryRoad();
            st.paths["lane"] = lanePath(1.625, 72, -72);
            st.paths["a"] = lanePath(1.4, 72, -72);
            // the second car, turned across the road by the crash
            st.paths["b"] = polyPath({-1.5, -57.6, 2.5, -61.6});
            // you, out of the car on the side away from the traffic and on to the cars
            st.paths["you"] = polyPath({1.9, -32.3, 2.95, -32.6, 2.95, -43.4});
            st.paths["hurt"] = polyPath({2.85, -53.6, 2.85, -54.1});
            // the warning triangle: about 100 m back from the end of the queue
            st.props.push_back({.kind = "triangle", .x = 2.7, .z = 69.5, .yaw = yawOf(0, 1), .from = 9.9});
            return finish(std::move(st));
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "lane", .keys = plan(0, 0, 16, {legDt(3, 16), legTo(106, 0), legWait(9)}), .blink = {{4.4, 2}}},
              {.id = "a", .kind = "car", .path = "a", .keys = stand(122), .colour = BLUE, .blink = {{0, 2}}, .name = "theory.who.blue"},
              {.id = "b", .kind = "car", .path = "b", .keys = stand(5), .colour = SILVER},
              {.id = "hurt", .kind = "person", .path = "hurt", .keys = stand(0.25), .look = 71, .name = "theory.who.walker"},
              {.id = "you", .kind = "person", .path = "you", .keys = plan(11, 0, 0.8, {legDt(1, 1.5), legDt(5.5, 1.6), legTo(11.6, 0), legWait(1)}), .look = 9, .name = "theory.who.me", .leaves = "me"},
          },
      .steps =
          {
              {.at = 3.8, .answers = {"hazard", "help", "drive"}, .right = "hazard", .ref = "§ 16 Abs. 2 StVO"},
              {.at = 9.8, .answers = {"m10", "m50", "m100"}, .right = "m100", .ref = "§ 15 Satz 2 StVO"},
              {.at = 13.2, .answers = {"all", "involved", "pros"}, .right = "all", .ref = "§ 323c StGB, § 34 Abs. 1 StVO"},
          },
      .end = 20,
      .view = viewAt(1, -38, 0.6, -0.6, 34),
  };
}

// -------------------------------------------------- 8 · the lorry turns right
Stage truckStage() {
  const Road ns{.lanes = 1, .laneW = 3.5, .bike = 1.85, .walk = 2.5, .centre = "dash"};
  const Road ew{.lanes = 1, .laneW = 3.5, .walk = 2.5, .centre = "dash"};
  Cross c = crossroads({
      .ns = ns,
      .ew = ew,
      .r = 9,
      .arms =
          {
              .N = {.len = 32, .line = "stop", .light = ArmLight{"ns"}},
              .E = {.len = 30, .line = "stop", .light = ArmLight{"ew"}},
              .S = {.len = 66, .line = "stop", .light = ArmLight{"ns"}},
              .W = {.len = 28, .line = "stop", .light = ArmLight{"ew"}},
          },
  });
  Stage& st = c.stage;
  st.paths["me"] = c.move('S', 'N');
  st.paths["bike"] = c.move('S', 'N', "bike");
  // the lorry's way round the corner: wider than a car's (radius 7 m)
  const double R = 7;
  st.paths["truck"] = Trace().to(1.75, 66).to(1.75, 1.75 + R).curve(1.75, 1.75 + R * 0.4477, 1.75 + R * 0.4477, 1.75, 1.75 + R, 1.75).to(30, 1.75).path();
  return st;
}

/** 8 · The lorry turns right: the cyclist beside it is in its blind spot. */
LessonDef TRUCK_BLIND() {
  const double TRUCK_LINE = truckStage().paths.at("bike").marks.at("line");
  return {
      .id = "truckBlindSpot",
      .stage = truckStage,
      .actors =
          {
              {.id = "truck", .kind = "truck", .path = "truck", .keys = plan(0, TRUCK_LINE, 0, {legWait(10.8), legDt(2, 1.8), legDt(7, 1.8), legDt(3, 6), legDt(3, 6)}), .blink = {{0, 1}, {21, 0}}, .name = "theory.who.truck"},
              {.id = "bike", .kind = "bike", .path = "bike", .keys = plan(0, TRUCK_LINE - 20, 6, {legTo(TRUCK_LINE, 0), legWait(0.8), legDt(2.5, 5), legDt(16, 5)}), .colour = 0xd35a1f, .name = "theory.who.bike"},
              {.id = "me", .kind = "me", .path = "me", .keys = plan(0, TRUCK_LINE - 36, 7, {legTo(TRUCK_LINE - 10.7, 0), legWait(13.8), legDt(3, 6), legDt(2, 7)})},
          },
      .steps =
          {
              {.at = 5.2, .answers = {"truck", "bike"}, .right = "bike", .ref = "§ 9 Abs. 3 StVO", .ask = "theory.ask.first", .blind = "truck"},
              {.at = 11.2, .answers = {"walk", "k20", "k30"}, .right = "walk", .ref = "§ 9 Abs. 6 StVO"},
          },
      .end = 25,
      .lights = {{"ns", {{0, 'R'}, {6, 'Y'}, {7, 'G'}}}, {"ew", {{0, 'G'}, {3.5, 'A'}, {4.6, 'R'}}}},
      .view = viewAt(3, 6, 0.65, -0.72, 30),
  };
}

// ------------------------------------------------------- 9 · two lanes, one ends
Stage lanesStage() {
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
  st.signs.push_back({7.3, 44, yawOf(0, 1), {"121-10"}});
  st.paths["left"] = lanePath(1.625, 72, -72);
  st.paths["right"] = lanePath(4.875, 72, -72);
  return finish(std::move(st));
}

/** 9 · Two lanes your way: a car wants into yours too early, and where its lane ends you let it in. */
LessonDef MULTI_LANE() {
  return {
      .id = "multiLane",
      .stage = lanesStage,
      .actors =
          {
              {.id = "l1", .kind = "car", .path = "left", .keys = plan(0, 62, 4.2, {legDt(16, 4.2)}), .colour = SILVER},
              {.id = "me", .kind = "me", .path = "left", .keys = plan(0, 37, 4.2, {legDt(9.2, 4.2), legDt(2.4, 3), legDt(2.4, 4.2), legDt(2, 4.2)})},
              {
                  .id = "blue",
                  .kind = "car",
                  .path = "right",
                  .keys = plan(0, 40.5, 4.6, {legDt(9.2, 4.6), legDt(2, 4.2), legDt(4.8, 4.2)}),
                  .colour = BLUE,
                  .side = {{9.6, 3.25, 2.6}},
                  .blink = {{0.2, -1}, {3.4, 0}, {8.4, -1}, {12.4, 0}},
                  .name = "theory.who.blue",
              },
              {.id = "l3", .kind = "car", .path = "left", .keys = plan(0, 22, 4.2, {legDt(9.2, 4.2), legDt(2.4, 3), legDt(2.4, 4.2), legDt(2, 4.2)}), .colour = GREEN},
          },
      .steps =
          {
              {.at = 1.0, .answers = {"keep", "brake"}, .right = "keep", .ref = "§ 7 Abs. 5 StVO"},
              {.at = 9.3, .answers = {"me", "blue"}, .right = "blue", .ref = "§ 7 Abs. 4 StVO", .ask = "theory.ask.first"},
          },
      .end = 16,
      .view = viewAt(3, 2, 0.5, -0.72, 44),
  };
}

// --------------------------------------------------------------- 10 · tunnel
/** 10 · A tunnel: the headlights on, and what to do when the car breaks down in it. */
LessonDef TUNNEL() {
  return {
      .id = "tunnel",
      .stage =
          [] {
            Stage st = countryRoad(3.75, 1.1);
            st.signs.push_back({4.3, 42, yawOf(0, 1), {"327"}});
            st.props.push_back({.kind = "tunnel", .x = -4.85, .z = 22, .x1 = 4.85, .z1 = -72, .h = 5.2});
            st.paths["lane"] = lanePath(1.875, 72, -72);
            st.paths["other"] = lanePath(-1.875, -72, 72);
            // you, out by the door on the right onto the raised walkway, and along it to the emergency exit
            st.paths["you"] = polyPath({2.9, -13.6, 4.4, -14.0, 4.4, -60});
            return finish(std::move(st));
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "lane", .keys = plan(0, 0, 15, {legDt(2, 15), legDt(5, 4), legTo(88, 0), legWait(8)}), .side = {{8.4, -0.8, 2.6}}, .blink = {{6.4, 2}}, .beam = {{0, 0}, {2.0, 1}}},
              {.id = "white", .kind = "car", .path = "other", .keys = plan(0, 30, 13, {legDt(20, 13)}), .colour = WHITE, .name = "theory.who.white", .beam = {{0, 1}}},
              {.id = "you", .kind = "person", .path = "you", .keys = plan(13.4, 0, 0.8, {legDt(1, 1.5), legDt(6, 1.5)}), .look = 9, .name = "theory.who.me", .leaves = "me"},
          },
      .steps =
          {
              {.at = 1.8, .answers = {"low", "none", "fog"}, .right = "low", .ref = "Anlage 3 StVO, Zeichen 327"},
              {.at = 12.6, .answers = {"out", "stay", "turn"}, .right = "out", .ref = "§ 15 StVO, Anlage 3 StVO, Zeichen 327"},
          },
      .end = 20.4,
      .view = viewOf("me", 4, 0.65, -0.62, 24),
  };
}

// --------------------------------------------------------- 11 · the old lady
/** 11 · An old lady with a rollator crosses the road slowly, away from any crossing. */
LessonDef SENIOR() {
  return {
      .id = "seniorCrossing",
      .stage =
          [] {
            Stage st = townRoad(70, -60, 3.25, 2.5);
            st.paths["me"] = lanePath(1.625, 70, -60);
            st.paths["white"] = lanePath(-1.625, -60, 70);
            st.paths["senior"] = polyPath({5.0, 6, 4.6, 2.0, 3.0, 0.5, -3.0, 0.5, -4.6, -1.0, -4.8, -20});
            return finish(std::move(st));
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = plan(0, 0, 11, {legDt(1, 11), legTo(63, 0), legWait(6.6), legDt(3, 6), legDt(2, 8)})},
              {.id = "white", .kind = "car", .path = "white", .keys = plan(0, 0, 9, {legTo(53.6, 0), legWait(3.6), legDt(3, 6), legDt(2, 8)}), .colour = WHITE, .name = "theory.who.white"},
              {.id = "senior", .kind = "person", .path = "senior", .keys = plan(0, 4.8, 0.55, {legDt(26, 0.55)}), .look = 5, .name = "theory.who.senior", .role = "senior"},
          },
      .steps =
          {
              {.at = 2.4, .answers = {"brake", "horn", "around"}, .right = "brake", .ref = "§ 3 Abs. 2a StVO"},
              {.at = 9.4, .answers = {"after", "now", "horn"}, .right = "after", .ref = "§ 3 Abs. 2a StVO"},
          },
      .end = 21.5,
      .view = viewAt(0, 4, 0.55, -0.6, 24),
  };
}

// ------------------------------------------------------------ 12 · wildlife
/** 12 · A deer at dusk where the forest meets the road – and the second one that follows. */
LessonDef WILDLIFE() {
  return {
      .id = "wildlife",
      .stage =
          [] {
            Stage st = countryRoad();
            st.signs.push_back({4.15, 62, yawOf(0, 1), {"142-10"}});
            forest(st, 7.5, 44, -72);
            st.paths["lane"] = lanePath(1.625, 72, -72);
            // out of the forest on the right, a moment at its edge, across the road and into the forest on the left
            st.paths["deer"] = polyPath({11, -12.6, 5.2, -10.6, -5, -9.4, -16, -8});
            st.paths["deer2"] = polyPath({13, -15.4, 5.4, -13.4, -5, -12.4, -16, -11});
            st.paths["boar"] = polyPath({-5.6, -47, -4.8, -47.4});
            return finish(std::move(st));
          },
      .actors =
          {
              {.id = "me", .kind = "me", .path = "lane", .keys = plan(0, 0, 15, {legDt(1.6, 15), legTo(73, 0), legWait(1.4), legDt(3, 6), legDt(2, 8)}), .beam = {{0, 2}, {1.6, 1}}},
              {.id = "deer", .kind = "deer", .path = "deer", .keys = plan(0, 5.6, 0, {legWait(3.3), legDt(1.4, 8), legDt(2.4, 8)}), .name = "theory.who.deer"},
              {.id = "deer2", .kind = "deer", .path = "deer2", .keys = plan(0, 4.6, 0, {legWait(5.4), legDt(1.4, 8), legDt(2.4, 8)}), .name = "theory.who.deer"},
              {.id = "boar", .kind = "boar", .path = "boar", .keys = stand(0.8), .name = "theory.who.boar"},
          },
      .steps =
          {
              {.at = 1.5, .answers = {"brake", "swerve", "flash"}, .right = "brake", .ref = "Anlage 1 StVO, Zeichen 142"},
              {.at = 5.6, .answers = {"more", "go"}, .right = "more", .ref = "§ 3 Abs. 1 StVO"},
          },
      .end = 15,
      .view = viewOf("me", 18, 0.45, -0.45, 30),
      .sky = LessonSky{.sun = -3},
  };
}

// -------------------------------------------------------- 13 · the police
Stage policeStage() {
  const Road road{.lanes = 1, .laneW = 3.5, .walk = 2.5, .centre = "dash"};
  Cross c = crossroads({
      .ns = road,
      .ew = road,
      .r = 6,
      .arms =
          {
              .N = {.len = 32, .line = "stop", .light = ArmLight{"ns"}},
              .E = {.len = 30, .line = "stop", .light = ArmLight{"ew"}},
              .S = {.len = 66, .line = "stop", .light = ArmLight{"ns"}},
              .W = {.len = 30, .line = "stop", .light = ArmLight{"ew"}},
          },
  });
  Stage& st = c.stage;
  st.paths["me"] = c.move('S', 'N');
  st.paths["white"] = c.move('E', 'W');
  st.paths["blue"] = c.move('W', 'E');
  // the officer in the middle of the crossroads (a path of his own to stand on)
  st.paths["police"] = polyPath({0, 0.2, 0, -0.2});
  return st;
}

/** 13 · A police officer directs the traffic: his signs come before the lights. */
LessonDef POLICE_SIGNALS() {
  const double line = policeStage().paths.at("me").marks.at("line");
  return {
      .id = "policeSignals",
      .stage = policeStage,
      .actors =
          {
              {.id = "me", .kind = "me", .path = "me", .keys = plan(0, line - 30, 9, {legTo(line, 0), legWait(6.4), legDt(3, 6), legDt(3, 7)})},
              {.id = "white", .kind = "car", .path = "white", .keys = plan(0, 0, 8, {legDt(9, 8)}), .colour = WHITE, .name = "theory.who.white"},
              {.id = "blue", .kind = "car", .path = "blue", .keys = plan(1.5, 0, 8, {legDt(7, 8)}), .colour = BLUE, .name = "theory.who.blue"},
              {
                  .id = "police",
                  .kind = "person",
                  .path = "police",
                  .keys = stand(0.2),
                  .look = 21,
                  .name = "theory.who.police",
                  .role = "police",
                  // facing you, arms out; one arm up; side-on to you, arms out
                  .face = {{0, SOUTH, 0.01}, {10.4, EAST, 1.0}},
                  .gesture = {{0, "arms"}, {8.2, "raise"}, {10.4, "down"}, {11.2, "arms"}},
              },
          },
      .steps =
          {
              {.at = 3.6, .answers = {"stop", "go"}, .right = "stop", .ref = "§ 36 Abs. 1, Abs. 2 StVO"},
              {.at = 9.0, .answers = {"wait", "go", "turn"}, .right = "wait", .ref = "§ 36 Abs. 2 Nr. 2 StVO"},
              {.at = 12.0, .answers = {"go", "wait"}, .right = "go", .ref = "§ 36 Abs. 2 Nr. 1 StVO"},
          },
      .end = 18.5,
      // (the lights say the opposite of the officer: green for you, red across)
      .lights = {{"ns", {{0, 'G'}}}, {"ew", {{0, 'R'}}}},
      .view = viewAt(0, 6, 0.5, -0.62, 30),
  };
}

// ------------------------------------------------------------- 14 · flagman
/** 14 · Roadworks with one lane: a flagman with his stop/go sign. */
LessonDef FLAGMAN() {
  return {
      .id = "flagman",
      .stage =
          [] {
            Stage st = countryRoad();
            st.signs.push_back({4.15, 58, yawOf(0, 1), {"123"}});
            st.signs.push_back({4.15, 40, yawOf(0, 1), {"274-50"}});
            std::vector<PropPlace>& props = st.props;
            // the barrier across your lane, the cones along the middle, a heap of earth
            props.push_back({.kind = "barrier", .x = 1.65, .z = 1.5, .yaw = yawOf(0, 1)});
            for (double z = 0; z >= -42; z -= 6) props.push_back({.kind = "cone", .x = 0.25, .z = z});
            props.push_back({.kind = "heap", .x = 2.2, .z = -30});
            props.push_back({.kind = "barrier", .x = 1.65, .z = -43, .yaw = yawOf(0, -1)});
            st.paths["lane"] = lanePath(1.625, 72, -72);
            st.paths["other"] = lanePath(-1.625, -72, 72);
            st.paths["flagman"] = polyPath({2.5, 3.4, 2.5, 3.8});
            st.paths["worker"] = polyPath({1.9, -14, 1.9, -20});
            return finish(std::move(st));
          },
      .actors =
          {
              {.id = "me",
               .kind = "me",
               .path = "lane",
               .keys = plan(0, 0, 13, {legDt(1.5, 13), legTo(62, 0), legWait(3.5), legDt(3, 8), legDt(6, 8), legDt(2, 10)}),
               .side = {{11.6, 3.25, 2.3}, {20.4, 0, 2.2}},
               .blink = {{10.8, -1}, {13.8, 0}, {19.9, 1}, {22, 0}}},
              {.id = "white", .kind = "car", .path = "other", .keys = plan(0, 10, 11, {legDt(12, 11)}), .colour = WHITE, .name = "theory.who.white"},
              {.id = "blue", .kind = "car", .path = "other", .keys = plan(0, -14, 11, {legDt(12, 11)}), .colour = BLUE, .name = "theory.who.blue"},
              {.id = "flagman", .kind = "person", .path = "flagman", .keys = stand(0.2), .look = 33, .name = "theory.who.flagman", .role = "flagman", .face = {{0, SOUTH, 0.01}}, .gesture = {{0, "stop"}, {10.4, "go"}}},
              {.id = "worker", .kind = "person", .path = "worker", .keys = plan(0, 0, 0.5, {legDt(4, 0.5), legDt(2, 0), legWait(30)}), .look = 48, .name = "theory.who.worker", .role = "worker"},
          },
      .steps =
          {
              {.at = 3.0, .answers = {"stop", "slow", "horn"}, .right = "stop", .ref = "§ 45 Abs. 6, § 1 Abs. 2 StVO"},
              {.at = 10.8, .answers = {"slow", "fast", "wait"}, .right = "slow", .ref = "§ 1 Abs. 2, § 3 Abs. 1 StVO"},
          },
      .end = 22.5,
      .view = viewOf("me", 10, 0.6, -0.58, 28),
  };
}
}  // namespace lessons
}  // namespace

const std::vector<LessonDef>& MORE_THEORY() {
  static const auto* list = new std::vector<LessonDef>{lessons::PARKING(), lessons::FOLLOW(), lessons::NARROW(), lessons::BUS_STOP(), lessons::NIGHT(), lessons::FOG(), lessons::ACCIDENT(), lessons::TRUCK_BLIND(), lessons::MULTI_LANE(), lessons::TUNNEL(), lessons::SENIOR(), lessons::WILDLIFE(), lessons::POLICE_SIGNALS(), lessons::FLAGMAN()};
  return *list;
}

std::vector<std::string> MORE_THEORY_IDS() {
  std::vector<std::string> ids;
  for (const LessonDef& l : MORE_THEORY()) ids.push_back(l.id);
  return ids;
}

}  // namespace fs::theory
