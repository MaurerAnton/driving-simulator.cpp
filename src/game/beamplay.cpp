// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/beamplay.ts @ v1.7.7
#include "game/beamplay.h"

#include <cmath>

#include "sim/traffic.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

bool BeamPlay::sees(const std::function<double(double, double)>& heightAt, double x0, double z0, double x1, double z1) {
  const double y0 = heightAt(x0, z0) + 1.2;
  const double y1 = heightAt(x1, z1) + 1.2;
  for (int i = 1; i < 6; i++) {
    const double t = i / 6.0;
    if (heightAt(x0 + (x1 - x0) * t, z0 + (z1 - z0) * t) > y0 + (y1 - y0) * t + 0.4) return false;
  }
  return true;
}

BeamFrameResult BeamPlay::frame(const BeamFrame& f) {
  clock_ += f.dt;
  if (!lights_) {
    lights_ = std::make_unique<PlayerLights>(f.scene);
    f.scene->add(glow.points);
  }
  const double dk = dark();
  const bool night = sunElevation < DARK_SUN;
  const std::function<double(double, double)> heightAt = f.heightAt;
  const BeamSees seesFn = [heightAt](double x0, double z0, double x1, double z1) { return sees(heightAt, x0, z0, x1, z1); };
  lights_->update({.dark = dk, .root = f.driving ? f.root : nullptr, .front = f.front, .height = f.height, .low = f.low, .high = f.high, .cheap = f.cheap});
  // the traffic near by: its main beam, flashing back
  std::vector<TrafficBeamCar>& near = near_;
  near.clear();
  static const std::vector<TrafficCar*> none;
  const std::vector<TrafficCar*>& cars = f.cars ? *f.cars : none;
  if (dk > 0) {
    for (const TrafficCar* c : cars) {
      if (std::abs(c->x - f.x) > 900 || std::abs(c->z - f.z) > 900) continue;
      const Edge* e = c->lane ? c->lane->edge : nullptr;
      // (across a junction: dipped)
      TrafficBeamCar t;
      t.id = c->id;
      t.x = c->x;
      t.z = c->z;
      t.yaw = c->yaw;
      t.v = c->v;
      t.rural = e && (e->flags & (EF_URBAN | EF_LIT)) == 0;
      near.push_back(t);
    }
  }
  BeamTip tip;
  std::vector<BeamFault> faults;
  if (f.driving) {
    BeamScene s;
    s.x = f.x;
    s.z = f.z;
    s.yaw = f.yaw;
    s.speed = f.speed;
    s.low = f.low;
    s.high = f.high;
    s.dark = night;
    s.poorSight = f.visibility < 150;
    s.lit = f.lit;
    if (night) {
      s.cars.assign(near.begin(), near.end());
      s.riders = f.riders;
    }
    s.sees = seesFn;
    faults = watch.update(f.dt, s);
    tip = watch.tip;
    // (at dusk, before it counts: the lights on)
    if (tip.empty() && !f.low && sunElevation < DUSK_SUN && std::abs(f.speed) > 2) tip = "beam.tipLightsOn";
    if (!f.judged) faults.clear();
  } else
    watch.reset();
  BeamPlayer& p = player_;
  p.id = -1;
  p.x = f.x;
  p.z = f.z;
  p.yaw = f.yaw;
  p.high = f.high;
  traffic.update(f.dt, near, f.driving ? &p : nullptr, f.driving ? watch.dazzling : -1, night, seesFn);
  if (f.camera) {
    std::vector<GlowCar> glowCars;
    glowCars.reserve(cars.size());
    for (const TrafficCar* c : cars)
      glowCars.push_back(GlowCar{.id = int(c->id), .x = c->x, .y = c->y, .z = c->z, .yaw = c->yaw, .length = c->length, .width = c->width, .braking = c->braking});
    glow.update(glowCars, dk, *f.camera, f.heightPx, [this](int id) { return traffic.high(id); }, traffic.glare);
  }
  if (!tip.empty() && (tip != lastTip_ || clock_ - lastTipAt_ > 30)) {
    lastTip_ = tip;
    lastTipAt_ = clock_;
  } else {
    if (tip.empty() && clock_ - lastTipAt_ > 8) lastTip_ = "";
    tip = "";
  }
  return {tip, faults};
}

}  // namespace fs
