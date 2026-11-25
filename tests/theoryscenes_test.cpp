// LINK: game sim world i18n core platform
// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/game/theoryscenes.test.ts @ v1.7.7
/**
 * The later theory lessons (game/theoryscenes.ts): the rule each scene is
 * about, played out as it should be – nobody stops under Zeichen 283, the
 * distance is half the speedometer, 2 m beside the cyclist, the bus is let
 * out, the full beam dipped for the oncoming car, at most 50 km/h in the
 * fog, the warning triangle 100 m back, the lorry turns at walking pace
 * after the cyclist, the zip at the end of the lane, the lights on in the
 * tunnel, the old lady across before anybody moves, the car standing while
 * the deer cross, the police officer's signs and the flagman's obeyed –,
 * the people's arms, signs and headlights, and how a run of these lessons
 * stops at each question.  (The checks every lesson must pass – nobody
 * touches anybody, … – are those of tests/theory_test.cpp.)
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <regex>
#include <string>

#include "game/theory.h"
#include "game/theoryscenes.h"
#include "i18n/index.h"
#include "i18n/violations.h"

// (sceneOf("…") and actor(s, "…") return references into the scenes, not into their arguments)
#pragma GCC diagnostic ignored "-Wdangling-reference"

using namespace fs;
using namespace fs::theory;

static constexpr double DT = 0.05;
static const double PI_ = std::acos(-1.0);

static std::map<std::string, std::shared_ptr<Scene>>& scenes() {
  static auto* m = [] {
    auto* out = new std::map<std::string, std::shared_ptr<Scene>>();
    for (const LessonDef& l : MORE_THEORY()) (*out)[l.id] = prepare(l);
    return out;
  }();
  return *m;
}
static const Scene& sceneOf(const std::string& id) { return *scenes().at(id); }
static const ActorRun& actor(const Scene& s, const std::string& id) {
  for (const ActorRun& a : s.actors)
    if (a.def->id == id) return a;
  assert(false);
  return s.actors[0];
}
static Pose pose(const ActorRun& r, double t) {
  Pose p;
  return poseAt(r, t, p);
}
static std::vector<double> times(const Scene& s) {
  std::vector<double> out;
  for (double t = 0; t <= s.lesson->end + 1e-9; t += DT) out.push_back(t);
  return out;
}

static void fourteenMore() {
  assert(MORE_THEORY().size() == 14);
  for (const std::string& id : MORE_THEORY_IDS()) assert(theoryLesson(id));
  assert(theoryLesson(MORE_THEORY()[0].id) == &THEORY()[13]);
  for (const std::string& key : allTheoryKeys()) assert(dictOf("en")->find(key));
  const std::regex LAW(R"(gesetze-im-internet\.de/(stvo_2013|stgb)/)");
  for (const LessonDef& l : MORE_THEORY()) {
    for (const StepDef& s : l.steps) {
      // every paragraph named links to its law (the StVO – or the criminal code for first aid)
      for (const CitationPart& p : citationParts(s.ref))
        if (p.url) assert(std::regex_search(*p.url, LAW));
    }
  }
  std::vector<CitationPart> first;
  for (const CitationPart& p : citationParts("§ 34 Abs. 1 StVO, § 323c StGB"))
    if (p.url) first.push_back(p);
  assert(*first[1].url == "https://www.gesetze-im-internet.de/stgb/__323c.html");
}

static void aPlan() {
  const std::vector<Key> k = plan(0, 10, 10, {legDt(2, 6), legTo(30, 0), legWait(1.5)});
  assert(k[1].t == 2 && std::get<double>(k[1].at) == 26 && k[1].v == 6);
  assert(std::get<double>(k[2].at) == 30);
  assert(std::abs(k[2].t - (2 + (2 * 4) / 6.0)) < 1e-9);
  assert(k[3].t == k[2].t + 1.5);
  assert(std::get<double>(k[3].at) == 30);
}

static void people() {
  const Scene& s = sceneOf("policeSignals");
  const ActorDef& p = *actor(s, "police").def;
  assert(std::abs(faceAt(p, 1, 0) - PI_) < 1e-6);
  assert(std::abs(faceAt(p, 12, 0) + PI_ / 2) < 1e-6);
  assert(gestureAt(p, 3)->sign == "arms");
  assert(gestureAt(p, 9)->sign == "raise");
  const auto out = *armsAt(p, 3);
  assert(out[1] > 1.4 && out[3] > 1.4);
  const auto up = *armsAt(p, 9.5);
  assert(up[0] > 2.8);
  // (moving from one sign to the next takes a moment)
  const auto mid = *armsAt(p, 8.5);
  assert(mid[0] > 0.2 && mid[0] < 2.8);
  const ActorDef& f = *actor(sceneOf("flagman"), "flagman").def;
  assert(paddleAt(f, 2) == 0);
  assert(paddleAt(f, 12) == 1);
  const ActorDef& senior = *actor(sceneOf("seniorCrossing"), "senior").def;
  assert((*armsAt(senior, 3))[0] > 0.4);
  const ActorDef nobody{.id = "x", .kind = "person", .path = "p", .keys = {{0, 0, 0}}};
  assert(!armsAt(nobody, 1));
  assert(faceAt(nobody, 1, 0.3) == 0.3);
  assert(beamAt(nobody, 1) == 0);
}

static void parking() {
  const Scene& s = sceneOf("parkingSigns");
  const ActorRun& me = actor(s, "me");
  // (the signs stand on the south arm: 283 at z = 56, 286 at z = 34; the cross road's edge at z = −3.25)
  double stood283 = 0;
  for (const double t : times(s)) {
    const Pose p = pose(me, t);
    const double nose = p.z - std::cos(p.yaw) * 2.195;
    if (p.v < 0.05 && nose < 56 && nose > 34) stood283 += DT;
  }
  assert(stood283 == 0);
  const ActorRun& out = actor(s, "walker");
  const double t0 = out.keys[0].t;
  const Pose m = pose(me, t0);
  assert(m.v < 0.05 && m.z < 34 && m.z > 8);
  const Pose end = pose(me, s.lesson->end);
  assert(end.v < 0.05);
  const double tail = end.z + 2.085;
  assert(tail < -3.25 - 5);
}

static void distance() {
  const Scene& s = sceneOf("followingDistance");
  const ActorRun& me = actor(s, "me");
  const ActorRun& red = actor(s, "red");
  const double tRed = red.keys[1].t;
  const double tMe = me.keys[1].t;
  assert(std::abs(tMe - tRed - 1) < 1e-9);
  for (const double t : times(s)) {
    const Pose a = pose(me, t);
    const Pose b = pose(red, t);
    const double gap = b.s - 4.28 - a.s;
    if (t < tRed) assert(gap >= (a.v * 3.6) / 2 - 0.5);
    assert(gap > 5);
  }
  // reaction 1 s at 60 km/h ≈ 17 m, braking to a stop at about 3.5 m/s²
  assert(std::abs(me.keys[2].s - me.keys[1].s - 39.7) < 0.5);
}

static void narrowPass() {
  const Scene& s = sceneOf("narrowPass");
  const ActorRun& me = actor(s, "me");
  const ActorRun& bike = actor(s, "bike");
  const ActorRun& white = actor(s, "white");
  const ActorRun& truck = actor(s, "truck");
  int beside = 0;
  for (const double t : times(s)) {
    const Pose m = pose(me, t);
    const Pose b = pose(bike, t);
    if (m.s > b.s - 1.75 && m.s - 4.28 < b.s) {
      beside++;
      const double gap = std::abs(m.x - b.x) - 1.79 / 2 - 0.62 / 2;
      assert(gap >= 2);
    }
    // in the oncoming lane (your left side over the middle) only once the oncoming car is past you – or far away
    const Pose w = pose(white, t);
    if (m.x - 0.895 < -0.05) assert(w.z - 4.28 > m.z || m.z - w.z > 80);
  }
  assert(beside > 0);
  assert(blinkAt(*truck.def, 1) == 2);
  assert(pose(me, s.lesson->steps[1].at + 2).v < 0.05);
}

static void busStop() {
  const Scene& s = sceneOf("busStopPullOut");
  const ActorRun& me = actor(s, "me");
  const ActorRun& bus = actor(s, "bus");
  for (const double t : times(s)) {
    const Pose m = pose(me, t);
    const Pose b = pose(bus, t);
    assert(m.s < b.s - 12 - 4);
    if (b.v < 0.05 && m.s > b.s - 12 - 15) assert(m.v <= 7 / 3.6 + 0.05);
  }
  assert(blinkAt(*bus.def, 2) == 2);
  const double go = bus.keys[1].t;
  assert(blinkAt(*bus.def, go) == -1);
}

static void nightAndFog() {
  const Scene& night = sceneOf("nightDrive");
  const ActorRun& me = actor(night, "me");
  const ActorRun& white = actor(night, "white");
  assert(night.lesson->sky && night.lesson->sky->sun.value_or(99) < 0);
  for (const double t : times(night)) {
    const Pose m = pose(me, t);
    const Pose w = pose(white, t);
    const double ahead = m.z - w.z;
    if (w.visible && t > night.lesson->steps[0].at + 0.2 && ahead > 0 && ahead < 150) assert(beamAt(*me.def, t) == 1);
  }
  assert(beamAt(*me.def, 0) == 2);
  const Scene& fog = sceneOf("fogWall");
  assert(fog.lesson->sky && fog.lesson->sky->fog.value_or(99) < 50);
  const ActorRun& fm = actor(fog, "me");
  for (const double t : times(fog))
    if (t >= fog.lesson->steps[0].at + 1) assert(pose(fm, t).v <= 50 / 3.6 + 1e-6);
  assert(beamAt(*fm.def, 1) == 1);
}

static void accident() {
  const Scene& s = sceneOf("accidentScene");
  const ActorRun& me = actor(s, "me");
  const double stop = me.keys[2].t;
  assert(blinkAt(*me.def, stop - 1) == 2);
  const double tail = pose(me, stop).z + 2.085;
  const PropPlace* tri = nullptr;
  for (const PropPlace& p : s.stage.props)
    if (p.kind == "triangle") {
      tri = &p;
      break;
    }
  assert(tri && tri->z - tail > 95);
  assert(tri && tri->from.value_or(0) <= s.lesson->steps[1].at + 0.2);
}

static void truckBlind() {
  const Scene& s = sceneOf("truckBlindSpot");
  const ActorRun& truck = actor(s, "truck");
  const ActorRun& bike = actor(s, "bike");
  const double turnIn = 66 - 8.75;
  for (const double t : times(s)) {
    const Pose p = pose(truck, t);
    const Pose b = pose(bike, t);
    // (while its nose goes round the corner: no faster than walking)
    if (p.s > turnIn + 2 && p.s < turnIn + 11) assert(p.v <= 7 / 3.6 + 1e-6);
    if (p.v > 0.05) assert(b.z < -2);
  }
  assert(s.lesson->steps[0].blind == "truck");
  assert(blinkAt(*truck.def, 1) == 1);
}

static void multiLane() {
  const Scene& s = sceneOf("multiLane");
  const ActorRun& me = actor(s, "me");
  const ActorRun& blue = actor(s, "blue");
  const double t0 = blue.def->side.empty() ? 0 : blue.def->side[0][0];
  assert(pose(blue, t0).z < -10);
  for (const double t : times(s)) {
    const Pose b = pose(blue, t);
    const Pose m = pose(me, t);
    if (b.x < 3.25) assert(b.s - 4.28 > m.s + 1);
  }
}

static void tunnel() {
  const Scene& s = sceneOf("tunnel");
  const ActorRun& me = actor(s, "me");
  const PropPlace* tunnel = nullptr;
  for (const PropPlace& p : s.stage.props)
    if (p.kind == "tunnel") tunnel = &p;
  assert(tunnel);
  for (const double t : times(s)) {
    const Pose p = pose(me, t);
    if (p.z - 2.195 < tunnel->z + 5) assert(beamAt(*me.def, t) >= 1);
  }
  const double stop = me.keys[3].t;
  assert(blinkAt(*me.def, stop) == 2);
  assert(pose(actor(s, "you"), stop + 3).x > 3.75);
}

static void senior() {
  const Scene& s = sceneOf("seniorCrossing");
  const ActorRun& senior = actor(s, "senior");
  std::map<std::string, bool> stood = {{"me", false}, {"white", false}};
  for (const double t : times(s)) {
    const Pose p = pose(senior, t);
    const bool onRoad = std::abs(p.x) < 3.3 && p.visible;
    for (const char* id : {"me", "white"}) {
      const Pose c = pose(actor(s, id), t);
      const double nose = std::string(id) == "me" ? c.z - 2.195 : c.z + 2.195;
      if (!onRoad) continue;
      // (a car ahead of her way: its nose 6 m and more from it; once it stands it stays)
      assert(std::abs(nose - 0.5) >= 6);
      if (c.v < 0.05) stood[id] = true;
      else assert(!stood[id]);
    }
  }
  assert(stood["me"] && stood["white"]);
  assert(pose(senior, 5).v < 0.6);
}

static void wildlife() {
  const Scene& s = sceneOf("wildlife");
  const ActorRun& me = actor(s, "me");
  assert(s.lesson->sky && s.lesson->sky->sun.value_or(99) < 0);
  for (const char* id : {"deer", "deer2"}) {
    const ActorRun& d = actor(s, id);
    for (const double t : times(s)) {
      const Pose p = pose(d, t);
      if (!p.visible || std::abs(p.x) > 3.5) continue;
      const Pose m = pose(me, t);
      assert(m.z - 2.195 - p.z > 6);
    }
  }
  assert(std::any_of(s.stage.signs.begin(), s.stage.signs.end(), [](const SignPlace& g) { return std::find(g.codes.begin(), g.codes.end(), "142-10") != g.codes.end(); }));
}

static void policeAndFlagman() {
  const Scene& s = sceneOf("policeSignals");
  const ActorRun& me = actor(s, "me");
  const ActorDef& police = *actor(s, "police").def;
  const double line = markAt(*me.path, std::string("line"));
  for (const double t : times(s)) {
    const Pose p = pose(me, t);
    const auto g = gestureAt(police, t);
    const bool sideOn = std::abs(std::abs(faceAt(police, t, 0)) - PI_ / 2) < 0.05;
    if (!(sideOn && g && g->sign == "arms")) assert(p.s <= line + 0.05);
  }
  const Scene& f = sceneOf("flagman");
  const ActorRun& fm = actor(f, "me");
  const ActorDef& flag = *actor(f, "flagman").def;
  for (const double t : times(f)) {
    const Pose p = pose(fm, t);
    if (paddleAt(flag, t) < 1) assert(p.z - 2.195 > 3.4 + 4 - 0.05 || p.v < 0.05 || p.z > 20);
    if (p.z - 2.195 < 3.4) assert(paddleAt(flag, t) == 1);
  }
}

static void runs() {
  for (const LessonDef& l : MORE_THEORY()) {
    TheoryRun run(scenes().at(l.id));
    size_t asked = 0;
    for (int i = 0; i < 5000 && run.phase != "end"; i++) {
      if (run.advance(0.1) && run.phase == "ask") {
        assert(run.t == l.steps[asked].at);
        assert(run.answer(run.current() ? run.current()->right : "") == true);
        run.goOn();
        asked++;
      }
    }
    assert(asked == l.steps.size());
    assert(run.right == static_cast<int>(l.steps.size()));
    assert(run.t == l.end);
  }
}

int main() {
  fourteenMore();
  aPlan();
  people();
  parking();
  distance();
  narrowPass();
  busStop();
  nightAndFog();
  accident();
  truckBlind();
  multiLane();
  tunnel();
  senior();
  wildlife();
  policeAndFlagman();
  runs();
  std::printf("theoryscenes_test: all passed\n");
  return 0;
}
