// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/vehicle/vehicle.ts @ v1.7.7
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
 */
#pragma once

#include <array>

#include "sim/vehicle/types.h"

namespace fs {

class Vehicle {
 public:
  const VehicleSpec spec;
  /** centre of mass (world) */
  std::array<double, 3> pos{0, 0, 0};
  std::array<double, 4> quat{0, 0, 0, 1};
  std::array<double, 3> vel{0, 0, 0};
  std::array<double, 3> angVel{0, 0, 0};
  std::array<WheelState, 4> wheels{};
  /**
   * Position of the model origin (ground point below the middle of the
   * wheelbase at rest) in body coordinates.  Place the 3D model at
   * pos + R·modelOrigin with the body orientation.
   */
  std::array<double, 3> modelOrigin{0, 0, 0};

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

  /** the body shell (not just the tyres) touches the ground: rolled over or stuck nose-down */
  bool bodyContact = false;
  /** number of internal integration steps per step() call */
  int substeps = 2;

  Vehicle(const VehicleSpec& spec, Ground& ground);

  /** Place the car at rest with its model origin on (x, y, z), facing `yaw`. */
  void teleport(double x, double y, double z, double yaw);

  void resetDrivetrain(bool engineRunning);

  void applyImpulse(double px, double py, double pz, double jx, double jy, double jz);

  void translate(double dx, double dy, double dz);

  /** Height of each wheel centre above the model's ground plane, in body coordinates (for drawing the wheels). */
  void getWheelHeights(double* out) const;

  /** World position of the model origin (see `modelOrigin`). */
  void getModelPosition(double* out) const;

  /** Body point (body coordinates) → world. */
  void toWorld(double bx, double by, double bz, double* out) const;

  void step(double dt, const DriverInput& input);

 private:
  Ground& ground;

  // --- internal state ---
  double engineOmega = 0;
  double idleI = 0;
  bool limiterCut = false;
  double fireTimer = 0;
  double shiftTimer = 0;
  int autoGear = 1;
  std::array<double, 9> rot{};
  std::array<std::array<double, 3>, 4> attach{};
  std::array<double, 4> comp{};
  std::array<double, 4> compPrev{};
  std::array<double, 4> susLen;
  std::array<double, 4> zx{};
  std::array<double, 4> zy{};
  std::array<double, 4> fxTyre{};
  /** 1 + (tyre damping seen by the wheel) · h / inertia: the wheel is integrated implicitly against it */
  std::array<double, 4> wheelImp{1, 1, 1, 1};
  std::array<double, 4> absMul{1, 1, 1, 1};
  GroundSample sample{};
  std::array<double, 2> staticComp{0, 0};
  double idleOmega = 0;
  std::array<double, 3> prevVel{0, 0, 0};
  double frontLat = 0;
  double lastTc = 0;
  double startGrace = 0;

  void updateRot();
  /** angVel += I⁻¹ · (world torque) · h */
  void addAngular(double tx, double ty, double tz, double h);
  double torqueAt(double rpm) const;
  double ratio(int gear) const;
  void substep(double h, const DriverInput& inp);
  void derive(double dt);
};

}  // namespace fs
