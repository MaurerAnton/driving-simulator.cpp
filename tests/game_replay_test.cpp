// LINK: game sim render world core platform engine i18n audio input geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Replays: what is written down while driving, how much of it is kept, and
 * what a replay shows between two frames – the player's vehicle, the
 * traffic, the trams, the lights.
 *
 * Origin: tests/game/replay.test.ts @ v1.7.7
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "game/replay.h"
#include "sim/signals.h"
#include "town.h"

using namespace fs;

static ReplayFrame frame(double t, double x) {
  ReplayFrame f;
  f.t = t;
  f.pose = {static_cast<float>(x), 0, 0, 0, 0, 0, 1};
  return f;
}

static ReplayPart part(std::vector<float> v) { return std::make_shared<const std::vector<float>>(std::move(v)); }

int main() {
  // a visual state packed and unpacked comes back the same: numbers, flags, texts, arrays
  {
    VisualPacker p;
    double speedKmh = 48.5;
    std::string gear = "D";
    bool braking = true;
    std::vector<double> wheelSpin = {1, 2, 3, 4};
    // (`skip: { nested }` – an object – is no field the packer knows: it is not named)
    const PackedFields s = {{"speedKmh", &speedKmh}, {"gear", &gear}, {"braking", &braking}, {"wheelSpin", &wheelSpin}};
    const std::vector<float> a = p.pack(s);
    speedKmh = 12;
    gear = "R";
    braking = false;
    wheelSpin = {5, 6, 7, 8};
    const std::vector<float> b = p.pack(s);
    double oSpeed = 0;
    std::string oGear;
    bool oBraking = false;
    std::vector<double> oSpin = {0, 0, 0, 0};
    const PackedFields out = {{"speedKmh", &oSpeed}, {"gear", &oGear}, {"braking", &oBraking}, {"wheelSpin", &oSpin}};
    p.unpack(a, out);
    assert(oSpeed == 48.5 && oGear == "D" && oBraking == true && (oSpin == std::vector<double>{1, 2, 3, 4}));
    p.unpack(b, out);
    assert(oGear == "R");
    assert(oBraking == false);
    assert((oSpin == std::vector<double>{5, 6, 7, 8}));
  }

  // written ten times a second, the others five, the lights twice; only the last half hour is kept
  {
    Recorder r;
    r.clear("car");
    int frames = 0;
    int others = 0;
    int signals = 0;
    for (double t = 0; t < 10; t += 1.0 / 60) {
      const auto due = r.due(t);
      if (!due) continue;
      frames++;
      if (due->others) others++;
      if (due->signals) signals++;
      ReplayFrame f = frame(t, t);
      if (due->others) f.cars = part({});
      r.push(std::move(f));
    }
    std::printf("%d frames, %d with the traffic, %d with the lights in 10 s\n", frames, others, signals);
    assert(std::abs(frames - 100) <= 2);
    assert(std::abs(others - 50) <= 2);
    assert(std::abs(signals - 20) <= 2);
    // a long drive: what is older than half an hour goes, and the first frame kept still has the traffic
    for (double t = 10; t < REPLAY_KEEP + 200; t += 0.1) {
      ReplayFrame f = frame(t, t);
      if (static_cast<long>(std::floor(t * 10 + 0.5)) % 50 == 0) f.cars = part({});
      r.push(std::move(f));
    }
    std::printf("%.0f s kept\n", r.end() - r.start());
    assert(r.end() - r.start() <= REPLAY_KEEP + 10);
    assert(r.frames[0].cars);
  }

  // between two frames the replay moves the player, the cars and the trams part of the way; it plays, stops at the end, and jumps
  {
    Recorder r;
    r.clear("car");
    auto car = [](float id, float x) { return std::vector<float>{id, 0, x, 0, 0, static_cast<float>(std::numbers::pi - 0.1), 0, 0, 0, 0, 10, static_cast<float>(0xff0000), -1}; };
    ReplayFrame f0 = frame(0, 0);
    std::vector<float> both = car(7, 10);
    for (float v : car(8, 50)) both.push_back(v);
    f0.cars = part(both);
    f0.rail = part({3, 0, 0, 2, 1, 1, 100, 0, 0, 0.5F, 0});
    r.push(std::move(f0));
    ReplayFrame f1 = frame(0.2, 4);
    f1.cars = part(car(7, 12));
    f1.rail = part({3, 0, 0, 2, 1, 1, 104, 0, 0, 0.7F, 0});
    r.push(std::move(f1));
    ReplayView v(r);
    v.seek(0.1);
    v.frame();
    assert(std::abs(v.pose[0] - 2) < 1e-6);
    v.traffic();
    assert(v.carList.size() == 2);
    const ReplayCar* c7 = nullptr;
    const ReplayCar* c8 = nullptr;
    for (const ReplayCar* c : v.carList) (c->id == 7 ? c7 : c8) = c;
    assert(c7 && c8);
    assert(std::abs(c7->x - 11) < 1e-4);
    assert(c8->x == 50);
    v.trains();
    assert(v.railList.size() == 1);
    assert(std::abs(v.railList[0]->cars[0].x - 102) < 1e-4 && std::abs(v.railList[0]->cars[0].yaw - 0.6) < 1e-4);
    assert(v.railList[0]->line && v.railList[0]->line->index == 2);
    static_assert(RAIL_HEAD + RAIL_CAR_FIELDS == 11);
    static_assert(CAR_FIELDS == 13);
    v.seek(0);
    v.speed = 2;
    v.advance(1);
    assert(std::abs(v.t - 0.2) < 1e-9);
    assert(v.playing == false);
  }

  // the lights: a snapshot of the controllers round a place, put back later, shows what they showed then
  {
    TownOptions o;
    o.cols = 2;
    o.rows = 1;
    o.block = 300;
    o.pavement = 2;
    o.crossings = {TownCrossing{.street = {0, 0, 1, 0}, .at = 0.5, .k = XING_SIGNAL}};
    Town town = makeTown(o);
    SignalSystem signals(town.world->signals);
    const int sc = *town.world->crossings[0]->sc;
    for (double t = 0; t < 30; t += 0.1) signals.update(0.1);
    signals.request(sc);
    while (signals.pedestrian(sc, 0) != "green") signals.update(0.1);
    const SignalControllerJson& c = *town.world->signals[sc];
    const SignalSnapshot snap = signals.snapshot(c.x, c.z, 600);
    bool has = false;
    for (int id : snap.ids) has = has || id == sc;
    assert(has);
    for (double t = 0; t < 40; t += 0.1) signals.update(0.1);
    assert(signals.get(sc, 0) == SIG_GREEN);
    signals.restore(snap);
    assert(signals.pedestrian(sc, 0) == "green");
    assert(signals.get(sc, 0) != SIG_GREEN);
    assert(signals.snapshot(c.x + 5000, c.z, 600).ids.empty());
  }

  std::printf("game_replay_test: ok\n");
  return 0;
}
