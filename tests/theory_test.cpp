// LINK: game sim world i18n core platform
// SPDX-License-Identifier: AGPL-3.0-only
// Origin: tests/game/theory.test.ts @ v1.7.7
/**
 * The theory lessons: every script on its place – the places fit on the
 * practice yard, the vehicles keep to the asphalt, nobody touches anybody,
 * nobody drives backwards or brakes like a wall; at each question the
 * actors it is about are there, and afterwards the scene plays out the
 * answer – whoever goes first really is first through the place both use.
 * Some rules checked on their own (the zebra, the school bus, the gap to
 * the cyclist, the indicator in the roundabout, the green arrow, the zip),
 * the texts, and how a run of a lesson stops, is answered and goes on.
 *
 * (Also the case of tests/sim/levelcrossing.test.ts about the two theory lessons at the
 * level crossing – its other cases are tests/levelcrossing_test.cpp.)
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>

#include "core/js.h"
#include "game/theory.h"
#include "game/theorystage.h"
#include "i18n/index.h"
#include "sim/levelcrossing.h"

// (sceneOf("…") and actor(s, "…") return references into the scenes, not into their arguments)
#pragma GCC diagnostic ignored "-Wdangling-reference"

using namespace fs;
using namespace fs::theory;

static constexpr double DT = 0.05;

static std::map<std::string, std::shared_ptr<Scene>>& scenes() {
  static auto* m = [] {
    auto* out = new std::map<std::string, std::shared_ptr<Scene>>();
    for (const LessonDef& l : THEORY()) (*out)[l.id] = prepare(l);
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
static bool isPerson(const ActorRun& r) { return r.def->kind == "person" || r.def->kind == "child"; }
/** a deer or a wild boar: from the forest over the road and away (game/theoryscenes.ts) */
static bool isAnimal(const ActorRun& r) { return r.def->kind == "deer" || r.def->kind == "boar"; }
static const std::string* en(const std::string& key) { return dictOf("en")->find(key); }

/** Every moment of a lesson, a step of DT apart. */
static std::vector<double> times(const LessonDef& l, double from = 0) {
  std::vector<double> out;
  for (double t = from; t <= l.end + 1e-9; t += DT) out.push_back(t);
  return out;
}

static void lessonsAndTexts() {
  const std::vector<LessonDef>& all = THEORY();
  assert(all.size() >= 10 && all.size() <= 40);
  std::set<std::string> ids;
  for (const LessonDef& l : all) ids.insert(l.id);
  assert(ids.size() == all.size());
  for (const std::string& key : allTheoryKeys()) {
    if (!en(key)) std::printf("missing text %s\n", key.c_str());
    assert(en(key));
  }
  for (const char* key : {"theory.title", "theory.desc", "theory.listText", "theory.loading", "theory.help", "theory.right", "theory.wrong", "theory.goOn", "theory.again", "theory.next", "theory.list", "theory.done", "theory.score", "theory.who.me"}) assert(en(key));
  assert(en("theory.score")->find("{right}") != std::string::npos && en("theory.score")->find("{count}") != std::string::npos);
  const std::regex REF(R"((§ \d+|Anlage \d+) .*StVO|StVO)");
  for (const LessonDef& l : all) {
    assert(l.steps.size() >= 1 && l.steps.size() <= 3);
    double last = 0;
    for (size_t i = 0; i < l.steps.size(); i++) {
      const StepDef& s = l.steps[i];
      assert(s.at > last && s.at < l.end);
      last = s.at;
      assert(std::find(s.answers.begin(), s.answers.end(), s.right) != s.answers.end());
      assert(s.answers.size() >= 2);
      assert(std::regex_search(s.ref, REF));
      for (const std::string& a : s.answers) assert(en(answerKey(l, static_cast<int>(i) + 1, a)));
      if (!s.blind.empty()) assert(std::any_of(l.actors.begin(), l.actors.end(), [&](const ActorDef& x) { return x.id == s.blind; }));
    }
    assert(std::any_of(l.actors.begin(), l.actors.end(), [](const ActorDef& a) { return a.kind == "me"; }));
    std::set<std::string> names;
    for (const ActorDef& a : l.actors) names.insert(a.id);
    assert(names.size() == l.actors.size());
  }
}

static void marks() {
  const Path p = lanePath(0, 50, -50, {{"line", 40}});
  assert(markAt(p, std::string("line")) == 40);
  assert(markAt(p, std::string("line-12")) == 28);
  assert(markAt(p, std::string("line+2.5")) == 42.5);
  assert(markAt(p, std::string("end")) == 100);
  assert(markAt(p, 17.0) == 17);
  bool threw = false;
  try {
    markAt(p, std::string("nowhere+3"));
  } catch (...) {
    threw = true;
  }
  assert(threw);
  const Scene& r = sceneOf("roundabout");
  assert(markAt(*actor(r, "me").path, std::string("pass:E+2")) > markAt(*actor(r, "me").path, std::string("ring")));
}

static void keysPassed() {
  for (const auto& [id, s] : scenes()) {
    for (const ActorRun& r : s->actors) {
      Progress prog;
      for (const ResolvedKey& k : r.keys) {
        progressAt(r.keys, k.t, prog);
        assert(std::abs(prog.s - k.s) < 1e-6 && std::abs(prog.v - k.v) < 1e-6);
      }
      for (size_t i = 1; i < r.keys.size(); i++) assert(r.keys[i].t > r.keys[i - 1].t);
      const ResolvedKey& last = r.keys.back();
      progressAt(r.keys, last.t + 2, prog);
      assert(std::abs(prog.s - (last.s + 2 * last.v)) < 1e-9);
    }
  }
  // sideways: smoothly to the new place, and the indicators by their times
  const Scene& zip = sceneOf("zip");
  const ActorDef& me = *actor(zip, "me").def;
  assert(sideAt(me, 7).off == 0);
  assert(std::abs(sideAt(me, 7.1 + 1.2).off - 3.25 / 2) < 1e-9);
  assert(std::abs(sideAt(me, 20).off - 3.25) < 1e-9);
  assert(sideAt(me, 20).rate == 0);
  assert(blinkAt(me, 5) == 0);
  assert(blinkAt(me, 6) == -1);
  assert(blinkAt(me, 10) == 0);
}

static void fitsTheYard() {
  for (const auto& [id, s] : scenes()) {
    const auto [x0, z0, x1, z1] = s->stage.bounds;
    if (!(x0 >= -33 && x1 <= 33 && z0 >= -75 && z1 <= 75)) std::printf("%s: %g…%g, %g…%g\n", id.c_str(), x0, x1, z0, z1);
    assert(x0 >= -33 && x1 <= 33);
    assert(z0 >= -75 && z1 <= 75);
    assert(std::any_of(s->stage.surfaces.begin(), s->stage.surfaces.end(), [](const Surface& x) { return x.kind == "asphalt"; }));
  }
}

static void simpleOutlines() {
  const auto cross = [](double ax, double az, double bx, double bz, double cx, double cz, double dx, double dz) {
    const double d1 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx);
    const double d2 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);
    const double d3 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
    const double d4 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
    return d1 * d2 < -1e-9 && d3 * d4 < -1e-9;
  };
  for (const auto& [id, s] : scenes()) {
    for (const Surface& surface : s->stage.surfaces) {
      if (surface.kind == "paint" || surface.kind == "steel") continue;
      std::vector<const std::vector<double>*> rings = {&surface.ring};
      for (const auto& h : surface.holes) rings.push_back(&h);
      for (const std::vector<double>* rp : rings) {
        const std::vector<double>& ring = *rp;
        const size_t n = ring.size() / 2;
        assert(n >= 3);
        for (size_t i = 0; i < n; i++) {
          for (size_t j = i + 2; j < n; j++) {
            if (i == 0 && j == n - 1) continue;
            const double ax = ring[i * 2], az = ring[i * 2 + 1], bx = ring[((i + 1) % n) * 2], bz = ring[((i + 1) % n) * 2 + 1];
            const double cx = ring[j * 2], cz = ring[j * 2 + 1], dx = ring[((j + 1) % n) * 2], dz = ring[((j + 1) % n) * 2 + 1];
            if (cross(ax, az, bx, bz, cx, cz, dx, dz)) std::printf("%s: the outline of a %s crosses itself (edges %zu and %zu)\n", id.c_str(), surface.kind.c_str(), i, j);
            assert(!cross(ax, az, bx, bz, cx, cz, dx, dz));
          }
        }
      }
    }
  }
}

static void speeds() {
  const std::map<std::string, double> top = {{"me", 17}, {"car", 21.5}, {"van", 15}, {"bus", 9}, {"truck", 17}, {"tram", 12}, {"bike", 7}, {"person", 1.8}, {"child", 1.9}, {"deer", 12}, {"boar", 9}};
  for (const auto& [id, s] : scenes()) {
    for (const ActorRun& r : s->actors) {
      for (const double t : times(*s->lesson)) {
        const Pose p = pose(r, t);
        if (!p.visible) continue;
        const bool ok = p.v > -0.05 && p.v <= top.at(r.def->kind) + 1e-6 && std::abs(p.a) <= (isPerson(r) ? 1.5 : isAnimal(r) ? 6 : 3.6);
        if (!ok) std::printf("%s/%s at %.2f: %.2f m/s, %.2f m/s²\n", id.c_str(), r.def->id.c_str(), t, p.v, p.a);
        assert(ok);
      }
    }
  }
}

static void nobodyTouches() {
  for (const auto& [id, s] : scenes()) {
    std::vector<Pose> poses(s->actors.size());
    for (const double t : times(*s->lesson)) {
      for (size_t i = 0; i < s->actors.size(); i++) poseAt(s->actors[i], t, poses[i]);
      for (size_t i = 0; i < s->actors.size(); i++) {
        for (size_t j = i + 1; j < s->actors.size(); j++) {
          const ActorRun& a = s->actors[i];
          const ActorRun& b = s->actors[j];
          if (!poses[i].visible || !poses[j].visible || a.def->leaves == b.def->id || b.def->leaves == a.def->id) continue;
          const double grow = isPerson(a) && isPerson(b) ? 0.1 : 0.2;
          const bool touch = outlinesTouch(corners(a, poses[i], grow), corners(b, poses[j], grow));
          if (touch) std::printf("%s: %s and %s touch at %.2f\n", id.c_str(), a.def->id.c_str(), b.def->id.c_str(), t);
          assert(!touch);
        }
      }
    }
  }
}

static void onTheGround() {
  for (const auto& [id, s] : scenes()) {
    std::vector<const Surface*> road;
    std::vector<const Surface*> ground;
    for (const Surface& x : s->stage.surfaces) {
      if (x.kind == "asphalt" || x.kind == "apron") road.push_back(&x);
      if (x.kind == "asphalt" || x.kind == "walk") ground.push_back(&x);
    }
    const auto [x0, z0, x1, z1] = s->stage.bounds;
    for (const ActorRun& r : s->actors) {
      for (const double t : times(*s->lesson)) {
        const Pose p = pose(r, t);
        if (!p.visible) continue;
        // (only while all of it is on the place: it comes from beyond its edge and leaves past it)
        const std::vector<double> c = corners(r, p);
        bool inside = true;
        for (size_t k = 0; k < 8; k += 2)
          if (c[k] < x0 + 0.5 || c[k] > x1 - 0.5 || c[k + 1] < z0 + 0.5 || c[k + 1] > z1 - 0.5) inside = false;
        if (!inside || isAnimal(r)) continue;
        if (isPerson(r)) {
          const bool on = std::any_of(ground.begin(), ground.end(), [&](const Surface* g) { return covers(*g, p.x, p.z); });
          if (!on) std::printf("%s/%s on nothing at %.2f (%.1f, %.1f)\n", id.c_str(), r.def->id.c_str(), t, p.x, p.z);
          assert(on);
        } else {
          for (size_t k = 0; k < 8; k += 2) {
            const bool on = std::any_of(road.begin(), road.end(), [&](const Surface* g) { return covers(*g, c[k], c[k + 1]); });
            if (!on) std::printf("%s/%s off the road at %.2f (%.1f, %.1f)\n", id.c_str(), r.def->id.c_str(), t, c[k], c[k + 1]);
            assert(on);
          }
        }
      }
    }
  }
}

static void thereAtQuestions() {
  for (const auto& [id, s] : scenes()) {
    for (const StepDef& step : s->lesson->steps) {
      assert(pose(actor(*s, "me"), step.at).visible);
      for (const std::string& a : step.answers) {
        for (const ActorRun& r : s->actors)
          if (r.def->id == a) assert(pose(r, step.at).visible);
      }
    }
  }
}

/**
 * "Who goes first?" played out: from the question on, wherever the right one
 * and another of the answers both go, the right one has left before the
 * other comes (a grid of half metres over the place).
 */
static void firstThrough() {
  const double CELL = 0.5;
  for (const auto& [id, s] : scenes()) {
    for (const StepDef& step : s->lesson->steps) {
      std::vector<std::string> movers;
      for (const std::string& a : step.answers)
        if (std::any_of(s->actors.begin(), s->actors.end(), [&](const ActorRun& x) { return x.def->id == a; })) movers.push_back(a);
      if (movers.size() < 2 || std::find(movers.begin(), movers.end(), step.right) == movers.end()) continue;
      const auto cells = [&](const ActorRun& r) {
        std::map<std::pair<long, long>, std::array<double, 2>> out;
        for (const double t : times(*s->lesson, step.at)) {
          const Pose p = pose(r, t);
          if (!p.visible) continue;
          const std::vector<double> c = corners(r, p, isPerson(r) ? 0.25 : 0.15);
          double minX = JS_INF, maxX = -JS_INF, minZ = JS_INF, maxZ = -JS_INF;
          for (size_t k = 0; k < 8; k += 2) {
            minX = std::min(minX, c[k]);
            maxX = std::max(maxX, c[k]);
            minZ = std::min(minZ, c[k + 1]);
            maxZ = std::max(maxZ, c[k + 1]);
          }
          for (long gx = static_cast<long>(std::floor(minX / CELL)); gx <= static_cast<long>(std::floor(maxX / CELL)); gx++) {
            for (long gz = static_cast<long>(std::floor(minZ / CELL)); gz <= static_cast<long>(std::floor(maxZ / CELL)); gz++) {
              const double x = (gx + 0.5) * CELL;
              const double z = (gz + 0.5) * CELL;
              // (inside the outline itself, not only its box)
              if (!outlinesTouch(c, {x - 0.01, z - 0.01, x + 0.01, z - 0.01, x + 0.01, z + 0.01, x - 0.01, z + 0.01})) continue;
              const auto key = std::make_pair(gx, gz);
              auto it = out.find(key);
              if (it != out.end()) it->second[1] = t;
              else out[key] = {t, t};
            }
          }
        }
        return out;
      };
      const auto first = cells(actor(*s, step.right));
      for (const std::string& other : movers) {
        if (other == step.right) continue;
        const auto then = cells(actor(*s, other));
        for (const auto& [key, span] : then) {
          const auto f = first.find(key);
          if (f == first.end()) continue;
          if (!(f->second[1] < span[0]))
            std::printf("%s at %g: %s is at %ld,%ld at %.2f before %s has left it (%.2f)\n", id.c_str(), step.at, other.c_str(), key.first, key.second, span[0], step.right.c_str(), f->second[1]);
          assert(f->second[1] < span[0]);
        }
      }
    }
  }
}

static void zebra() {
  const Scene& s = sceneOf("zebra");
  const ActorRun& me = actor(s, "me");
  const double line = markAt(*me.path, std::string("zebra"));
  for (const double t : times(*s.lesson)) {
    bool onIt = false;
    for (const char* id : {"walker", "walker2"}) {
      const Pose p = pose(actor(s, id), t);
      if (p.visible && std::abs(p.z) < 2.2 && std::abs(p.x) < 3.4) onIt = true;
    }
    if (onIt) assert(pose(me, t).s <= line + 0.05);
  }
}

static void schoolBus() {
  const Scene& s = sceneOf("schoolBus");
  const ActorRun& me = actor(s, "me");
  const ActorRun& bus = actor(s, "bus");
  for (const double t : times(*s.lesson)) {
    const Pose b = pose(bus, t);
    const Pose m = pose(me, t);
    // (both on the same lane path: s is the nose)
    const double busTail = b.s - 12;
    if (b.v > 0.05) assert(m.s < busTail);
    else if (m.s > busTail - 1 && m.s < b.s + 4.5) assert(m.v <= 7 / 3.6 + 0.01);
  }
  assert(blinkAt(*bus.def, 3) == 2);
}

static void overtake() {
  const Scene& s = sceneOf("overtake");
  const ActorRun& me = actor(s, "me");
  const ActorRun& bike = actor(s, "bike");
  const ActorRun& car = actor(s, "white");
  int beside = 0;
  for (const double t : times(*s.lesson)) {
    const Pose m = pose(me, t);
    const Pose b = pose(bike, t);
    const Pose c = pose(car, t);
    // side by side: some of you is level with some of the bicycle
    const bool level = m.s > b.s - 1.75 && m.s - 4.28 < b.s;
    if (level) {
      beside++;
      const double gap = std::abs(m.x - b.x) - 1.79 / 2 - 0.62 / 2;
      assert(gap >= 1.5);
      // (the oncoming car: not between your nose and 40 m ahead of it)
      const double ahead = m.z - c.z;
      assert(!c.visible || ahead < -5 || ahead > 40);
    }
  }
  assert(beside > 0);
}

static void roundabout() {
  const Scene& s = sceneOf("roundabout");
  const ActorRun& me = actor(s, "me");
  const double ring = markAt(*me.path, std::string("ring"));
  const double line = markAt(*me.path, std::string("line"));
  const double exit = markAt(*me.path, std::string("exit"));
  for (const double t : times(*s.lesson)) {
    const Pose p = pose(me, t);
    if (p.s >= line && p.s <= ring + 2) assert(p.blink == 0);
    if (p.s >= exit - 8 && p.s <= exit) assert(p.blink == 1);
  }
}

static void greenArrow() {
  const Scene& s = sceneOf("greenArrow");
  const ActorRun& me = actor(s, "me");
  const double line = markAt(*me.path, std::string("line"));
  double stood = 0;
  for (const double t : times(*s.lesson)) {
    const Pose p = pose(me, t);
    assert(lightAt(s, "ns", t) == 'R');
    if (std::abs(p.s - line) < 0.05 && p.v < 0.05) stood += DT;
    if (p.s > line + 0.05) assert(stood >= 1);
  }
  assert(std::any_of(s.stage.lights.begin(), s.stage.lights.end(), [](const LightPlace& l) { return l.arrow; }));
}

static void indicators() {
  for (const char* id : {"leftTurn", "tram"}) {
    const Scene& s = sceneOf(id);
    const ActorRun& me = actor(s, "me");
    assert(blinkAt(*me.def, s.lesson->steps[0].at) == -1);
  }
  const Scene& bend = sceneOf("bend");
  const ActorRun& me = actor(bend, "me");
  // (not before the question about it – that would give the answer away – but as you go into the bend)
  assert(blinkAt(*me.def, bend.lesson->steps[0].at) == 0);
  double t = 0;
  for (const ResolvedKey& k : me.keys)
    if (k.s >= markAt(*me.path, std::string("in"))) {
      t = k.t;
      break;
    }
  assert(blinkAt(*me.def, t) == -1);
}

static void zip() {
  const Scene& s = sceneOf("zip");
  const ActorRun& me = actor(s, "me");
  // (the lane ends from z = −20 on: both lanes' paths begin at z = 72, so s = 72 − z)
  const double t0 = me.def->side.empty() ? 0 : me.def->side[0][0];
  assert(pose(me, t0).s > 72 + 10);
  std::vector<double> order;
  for (const char* id : {"l1", "r1", "l2", "me", "l3", "l4"}) order.push_back(pose(actor(s, id), s.lesson->end).s);
  for (size_t i = 1; i < order.size(); i++) assert(order[i] < order[i - 1]);
}

static void aRun() {
  const Scene& s = sceneOf("roundabout");
  TheoryRun run(scenes().at("roundabout"));
  assert(run.phase == "play");
  assert(!run.answer("red").has_value());
  int stops = 0;
  for (int i = 0; i < 2000 && run.phase == "play"; i++)
    if (run.advance(0.1)) stops++;
  assert(stops == 1);
  assert(run.phase == "ask");
  assert(run.t == s.lesson->steps[0].at);
  const double t = run.t;
  run.advance(1);
  assert(run.t == t);
  assert(!run.answer("nobody").has_value());
  assert(run.answer("me") == false);
  assert(run.phase == "told");
  assert(run.picked == "me");
  run.advance(1);
  assert(run.t == t);
  run.goOn();
  assert(run.phase == "play");
  while (run.phase == "play") run.advance(0.1);
  assert(run.step == 1);
  assert(run.answer("none") == true);
  run.goOn();
  while (run.phase == "play") run.advance(0.1);
  assert(run.answer("right") == true);
  run.goOn();
  while (run.phase == "play") run.advance(0.1);
  assert(run.phase == "end");
  assert(run.t == s.lesson->end);
  assert(run.right == 2);
  run.restart();
  assert(run.phase == "play");
  assert(run.t == 0);
  assert(run.right == 0);
}

static void skipTo() {
  const Scene& s = sceneOf("roundabout");
  TheoryRun run(scenes().at("roundabout"));
  run.skipTo(s.lesson->steps[1].at);
  assert(run.phase == "ask");
  assert(run.step == 1);
  assert(run.right == 1);
  assert(run.t == s.lesson->steps[1].at);
  run.skipTo(1);
  assert(run.phase == "play");
  assert(run.t == 1);
  assert(run.right == 0);
  run.skipTo(99);
  assert(run.phase == "end");
  assert(run.t == s.lesson->end);
  assert(run.right == static_cast<int>(s.lesson->steps.size()));
}

static void names() {
  const LessonDef& rbl = *theoryLesson("rbl");
  assert(actorName(rbl.actors[0]) == "theory.who.me");
  assert(answerKey(rbl, 1, "blue") == "theory.who.blue");
  assert(answerKey(rbl, 1, "!yes") == "theory.a.yes");
  assert(answerKey(rbl, 2, "fast") == "theory.rbl.2.fast");
}

static void smoothPaths() {
  PathPoint a;
  PathPoint b;
  for (const auto& [id, s] : scenes()) {
    for (const auto& [name, p] : s->stage.paths) {
      // people turn on the spot; the ways of vehicles must not bend more than a car's lock allows (r ≥ 4.5 m)
      if (std::all_of(s->actors.begin(), s->actors.end(), [&](const ActorRun& r) { return r.path != &p || isPerson(r); })) continue;
      for (double d = 1; d < p.len - 1; d += 0.5) {
        pathPoint(p, d - 0.5, a);
        pathPoint(p, d + 0.5, b);
        const double turn = std::abs(std::atan2(a.dx * b.dz - a.dz * b.dx, a.dx * b.dx + a.dz * b.dz));
        if (!(turn < 1 / 4.5 + 0.05)) std::printf("%s/%s bends %.2f rad in a metre at %g\n", id.c_str(), name.c_str(), turn, d);
        assert(turn < 1 / 4.5 + 0.05);
      }
    }
  }
}

// Origin: tests/sim/levelcrossing.test.ts @ v1.7.7 (its case of the theory lessons)
static void levelCrossings() {
  for (const char* id : {"levelCrossing", "levelCrossingQueue"}) {
    const LessonDef* lesson = theoryLesson(id);
    assert(lesson);
    const std::shared_ptr<Scene> scene = prepare(*lesson);
    const ActorRun& me = actor(*scene, "me");
    std::vector<const ActorRun*> others;
    for (const ActorRun& a : scene->actors)
      if (a.def->kind == "car") others.push_back(&a);
    Pose p;
    Pose q;
    for (double t = 0; t <= lesson->end; t += 0.05) {
      poseAt(me, t, p);
      const std::vector<double> c = corners(me, p);
      const double front = std::min({c[1], c[3], c[5], c[7]});
      const double back = std::max({c[1], c[3], c[5], c[7]});
      const bool lit = lightAt(*scene, "lx", t) != 'O';
      // (with the lights on: the nose stays at the stop line, 4.75 m before the track)
      if (lit) assert(front >= 4.7 || back < -LX.ZONE);
      // over the danger area only with room for the whole car beyond it
      if (front < LX.ZONE && back > -LX.ZONE) {
        for (const ActorRun* o : others) {
          poseAt(*o, t, q);
          if (!q.visible) continue;
          const std::vector<double> oc = corners(*o, q);
          const double rear = std::max({oc[1], oc[3], oc[5], oc[7]});
          if (rear < 0) assert(rear < -LX.ZONE - 4.28 - 1);
        }
      }
    }
    assert(std::all_of(lesson->steps.begin(), lesson->steps.end(), [](const StepDef& s) { return s.ref.find("§ 19") != std::string::npos; }));
  }
}

int main() {
  lessonsAndTexts();
  marks();
  keysPassed();
  fitsTheYard();
  simpleOutlines();
  speeds();
  nobodyTouches();
  onTheGround();
  thereAtQuestions();
  firstThrough();
  zebra();
  schoolBus();
  overtake();
  roundabout();
  greenArrow();
  indicators();
  zip();
  aRun();
  skipTo();
  names();
  smoothPaths();
  levelCrossings();
  std::printf("theory_test: all passed\n");
  return 0;
}
