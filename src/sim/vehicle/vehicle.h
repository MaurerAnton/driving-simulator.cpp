// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Vehicle dynamics.
 *
 * A 6-DOF rigid body on four independently sprung wheels:
 *
 *  - Suspension: ray-style contact against the ground, spring + bump/rebound
 *    damper + anti-roll bar + bump stop.  The ground height seen by a wheel
 *    is the envelope of several samples along its rolling direction, so a
 *    kerb is climbed over a quarter of a wheel turn instead of in one step.
 *  - Tyres: bristle (relaxation-length) model.  The tyre force follows a
 *    deflection state that integrates slip; at speed this is ordinary slip
 *    ratio / slip angle through a Pacejka-type curve with a friction circle,
 *    at standstill it degenerates into a spring – real static friction, so
 *    the car stands still on a hill on its brakes or in gear.
 *  - Driveline: engine (torque curve, friction, idle control, limiter,
 *    starter, stalling) – dry clutch – gearbox – open differential.  Clutch
 *    and brakes are solved as friction constraints ("how much torque would
 *    lock this, clamp to capacity"), which is exact in both the locked and
 *    the slipping state and never oscillates.
 *
 * Everything is deterministic and allocation-free in step().
 *
 * Origin: src/sim/vehicle/vehicle.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).
 */
#pragma once

#include <array>
#include <cmath>

#include "sim/vehicle/types.h"

namespace fs {

class Vehicle {
 public:
  /** the body shell touching the ground (rolled over / stuck): the tests read it */
  bool inBodyContact() const { return bodyContact; }
  Vehicle(const VehicleSpec& spec, Ground& ground);

  const VehicleSpec spec;
  Ground& ground;

  /** centre of mass (world) */
  double pos[3] = {0, 0, 0};
  double quat[4] = {0, 0, 0, 1};
  double vel[3] = {0, 0, 0};
  double angVel[3] = {0, 0, 0};
  std::array<WheelState, 4> wheels;
  /**
   * Position of the model origin (ground point below the middle of the
   * wheelbase at rest) in body coordinates.  Place the 3D model at
   * pos + R·modelOrigin with the body orientation.
   */
  double modelOrigin[3] = {0, 0, 0};

  // --- derived, refreshed every step ---
  double speed = 0;
  double yaw = 0;
  /** vertical component of the body's up axis: 1 = on its wheels, 0 = on its side, -1 = on the roof */
  double upright = 1;
  double rpm = 0;
  bool engineRunning = false;
  bool stalledEvent = false;
  double engineLoad = 0;
  int gear = 0;
  double clutchLock = 0;
  double accLong = 0;
  double accLat = 0;
  bool absActive = false;
  bool gearGrind = false;
  double steerWheelAngle = 0;
  double steerTorque = 0;
  /** clutch pedal position actually applied (differs from the input with autoClutch) */
  double clutchPedal = 0;
  /** distance driven (m) */
  double odometer = 0;

  /** number of internal integration steps per step() call */
  int substeps = 2;

  /** Place the car at rest with its model origin on (x, y, z), facing `yaw`. */
  void teleport(double x, double y, double z, double yaw);
  void resetDrivetrain(bool engineRunning);
  void applyImpulse(double px, double py, double pz, double jx, double jy, double jz);
  void translate(double dx, double dy, double dz);
  /** Height of each wheel centre above the model's ground plane (for drawing). */
  void getWheelHeights(double out[4]) const;
  /** World position of the model origin. */
  void getModelPosition(double out[3]) const;
  /** Body point (body coordinates) → world. */
  void toWorld(double bx, double by, double bz, double out[3]) const;

  void step(double dt, const DriverInput& input);

 private:
  // --- internal state ---
  double engineOmega = 0;
  double idleI = 0;
  bool limiterCut = false;
  double fireTimer = 0;
  double shiftTimer = 0;
  int autoGear = 1;
  double rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::array<std::array<double, 3>, 4> attach{};
  double comp[4] = {0, 0, 0, 0};
  double compPrev[4] = {0, 0, 0, 0};
  double susLen[4] = {0.36, 0.36, 0.36, 0.36};
  double zx[4] = {0, 0, 0, 0};
  double zy[4] = {0, 0, 0, 0};
  double fxTyre[4] = {0, 0, 0, 0};
  double absMul[4] = {1, 1, 1, 1};
  GroundSample sampleState{};
  std::array<double, 2> staticComp{0, 0};
  double idleOmega = 0;
  double prevVel[3] = {0, 0, 0};
  double frontLat = 0;
  double lastTc = 0;
  double startGrace = 0;
  /** the body shell (not just the tyres) touches the ground: rolled over or stuck nose-down */
  bool bodyContact = false;
  /** 1 + (tyre damping seen by the wheel) · h / inertia: the wheel is integrated implicitly against it */
  double wheelImp[4] = {1, 1, 1, 1};

  void updateRot();
  /** angVel += I⁻¹ · (world torque) · h */
  void addAngular(double tx, double ty, double tz, double h);
  double torqueAt(double rpm) const;
  double ratio(int gear) const;
  void substep(double h, const DriverInput& inp);
  void derive(double dt);
};

/** Torque a longitudinal tyre force exerts against the wheel's rotation. */
inline double rollTorque(double fx, double radius) { return fx * radius; }

/** Pacejka-shaped normalised slip curve (see the header comment). */
inline double pacejka(double rho) {
  double u = 1.9 * rho;
  return std::sin(1.5 * std::atan(u - 0.2 * (u - std::atan(u))));
}

}  // namespace fs
