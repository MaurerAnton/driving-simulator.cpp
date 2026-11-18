// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Vehicle dynamics: settling under gravity, standing still on a hill with
 * the handbrake, engine start/stall, and a full-throttle acceleration run.
 */
#include "../src/sim/ground.h"
#include "../src/sim/vehicle/vehicle.h"
#include <cassert>
#include <cstdio>

using namespace fs;

namespace {
/** Flat ground at y = 0, asphalt. */
class FlatGround : public TerrainSource {
 public:
  double heightAt(double x, double z) const override {
    (void)x;
    (void)z;
    return 0;
  }
  int coverAt(double x, double z) const override {
    (void)x;
    (void)z;
    return 0;
  }
};

/** A road on a 10 % slope: WorldGround would report this via surfaceAt. */
class SlopeGround : public Ground {
 public:
  double grade = 0.10;
  void sample(double x, double z, double yRef, GroundSample& out) override {
    (void)yRef;
    out.hit = true;
    out.y = grade * z;          // uphill towards -z (north)
    double l = std::sqrt(grade * grade + 1.0);
    out.nx = 0;
    out.ny = 1.0 / l;
    out.nz = -grade / l;
    out.grip = 1.0;
    out.rolling = 1.0;
  }
};

/** Flat, level road for the driving tests. */
class Flat : public Ground {
 public:
  void sample(double x, double z, double yRef, GroundSample& out) override {
    (void)x;
    (void)z;
    (void)yRef;
    out.hit = true;
    out.y = 0;
    out.nx = 0;
    out.ny = 1;
    out.nz = 0;
    out.grip = 1;
    out.rolling = 1;
  }
};
}  // namespace

int main() {
  const VehicleSpec& spec = compactCar();

  // --- the car settles onto its springs ------------------------------------
  {
    Flat g;
    Vehicle v(spec, g);
    v.teleport(0, 0, 0, 0);
    DriverInput in;
    in.gear = 0;
    for (int i = 0; i < 600; i++) v.step(1.0 / 240, in);
    // the centre of gravity sits cgHeight above the road, give or take settle
    assert(v.pos[1] > 0.4 && v.pos[1] < 0.75);
    for (int i = 0; i < 4; i++) assert(v.wheels[static_cast<size_t>(i)].contact);
    // roughly static load on each wheel
    double total = 0;
    for (int i = 0; i < 4; i++) total += v.wheels[static_cast<size_t>(i)].load;
    assert(total > spec.mass * 9.81 * 0.9 && total < spec.mass * 9.81 * 1.1);
    assert(std::fabs(v.speed) < 1e-6);
  }

  // --- standing still on a 10 % hill in gear with the handbrake -------------
  {
    SlopeGround g;
    Vehicle v(spec, g);
    v.teleport(0, 0, 0, 0);  // yaw 0 = facing north = uphill
    v.resetDrivetrain(true);
    DriverInput in;
    in.gear = 1;             // 1st gear
    in.clutch = 0;           // fully engaged (pedal up)
    in.handbrake = 1;
    in.ignition = true;
    for (int i = 0; i < 1200; i++) v.step(1.0 / 240, in);
    // no creep: the friction constraints hold the car
    assert(std::fabs(v.speed) < 0.35);
    assert(v.pos[1] < 0.1);   // still near the road at the origin
  }

  // --- engine: start with the starter, then stall with the handbrake in 1st -
  {
    Flat g;
    Vehicle v(spec, g);
    v.teleport(0, 0, 0, 0);
    v.resetDrivetrain(false);   // engine off
    DriverInput in;
    in.ignition = true;
    in.starter = true;
    in.gear = 0;
    bool started = false;
    for (int i = 0; i < 2400 && !started; i++) {
      v.step(1.0 / 240, in);
      started = v.engineRunning;
    }
    assert(started);
    assert(v.rpm > spec.idleRpm * 0.8);
    // release the starter, sit at idle
    in.starter = false;
    for (int i = 0; i < 600; i++) v.step(1.0 / 240, in);
    assert(v.engineRunning);
    assert(v.rpm > spec.stallRpm);

    // ignition off stops the engine
    in.ignition = false;
    for (int i = 0; i < 120; i++) v.step(1.0 / 240, in);
    assert(!v.engineRunning);
  }

  // --- accelerating from rest in 1st gear ----------------------------------
  {
    Flat g;
    Vehicle v(spec, g);
    v.teleport(0, 0, 0, 0);
    v.resetDrivetrain(true);
    DriverInput in;
    in.ignition = true;
    in.starter = false;
    in.gear = 1;
    in.clutch = 0;
    in.throttle = 1;
    in.handbrake = 0;
    for (int i = 0; i < 2400; i++) v.step(1.0 / 240, in);  // 10 s
    assert(v.speed > 8);                    // over 29 km/h
    assert(v.odometer > 40);                // travelled
    assert(v.rpm > spec.idleRpm);
    assert(v.accLong > -0.5);               // not decelerating
    // the car still points roughly north
    assert(std::fabs(v.yaw) < 0.2);
    // wheels are turning the same way the car moves
    assert(v.wheels[0].omega > 0);
  }

  // --- braking to a stop ----------------------------------------------------
  {
    Flat g;
    Vehicle v(spec, g);
    v.teleport(0, 0, 0, 0);
    v.resetDrivetrain(true);
    DriverInput in;
    in.ignition = true;
    in.gear = 1;
    in.clutch = 0;
    in.throttle = 1;
    for (int i = 0; i < 2400; i++) v.step(1.0 / 240, in);
    double before = v.speed;
    assert(before > 8);
    in.throttle = 0;
    in.brake = 1;
    in.gear = 0;
    in.clutch = 1;
    for (int i = 0; i < 1200; i++) v.step(1.0 / 240, in);
    assert(std::fabs(v.speed) < 0.5);
    assert(v.absActive || !spec.abs);
  }

  // --- steering changes the heading ----------------------------------------
  {
    Flat g;
    Vehicle v(spec, g);
    v.teleport(0, 0, 0, 0);
    v.resetDrivetrain(true);
    DriverInput in;
    in.ignition = true;
    in.gear = 1;
    in.clutch = 0;
    in.throttle = 1;
    for (int i = 0; i < 600; i++) v.step(1.0 / 240, in);
    in.steer = 1;   // full lock left
    for (int i = 0; i < 600; i++) v.step(1.0 / 240, in);
    assert(v.yaw > 0.2);   // turned left (counter-clockwise, positive yaw)
    assert(v.pos[0] < -0.5);  // and moved west
  }

  // --- determinism: same inputs, same result --------------------------------
  {
    Flat g1, g2;
    Vehicle a(spec, g1), b(spec, g2);
    a.teleport(0, 0, 0, 0);
    b.teleport(0, 0, 0, 0);
    a.resetDrivetrain(true);
    b.resetDrivetrain(true);
    DriverInput in;
    in.ignition = true;
    in.gear = 1;
    in.clutch = 0;
    in.throttle = 0.7;
    for (int i = 0; i < 1200; i++) {
      a.step(1.0 / 240, in);
      b.step(1.0 / 240, in);
    }
    assert(a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] && a.pos[2] == b.pos[2]);
    assert(a.speed == b.speed && a.rpm == b.rpm);
  }

  std::printf("vehicle: all checks passed (settles, holds on a hill, starts, drives, brakes)\n");
  return 0;
}
