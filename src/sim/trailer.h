// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The trailer of the truck: a centre-axle trailer with a tandem axle on a
 * rigid drawbar ("Starrdeichselanhänger"), as it is used for training and
 * testing class CE.
 *
 * It has one joint – the coupling – and its wheels roll but do not slip
 * sideways: the axle group follows the coupling the way a real trailer does,
 * cutting corners when pulled and breaking out to one side when pushed.
 * That is the whole difficulty of reversing with it, and it comes out of two
 * lines of geometry (see step()).
 *
 * Its mass acts on the truck through the coupling: it has to be accelerated
 * and braked, it pulls downhill and its own brakes help.  The side force on
 * the coupling is left out (the truck does not feel the trailer in bends).
 *
 * Origin: src/sim/trailer.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <cmath>
#include <string>

#include "sim/vehicle/types.h"
#include "sim/vehicle/spec.h"   // TRUCK_HITCH

namespace fs {

struct Vehicle;   // sim/vehicle/vehicle.h

struct TrailerSpec {
  /** mass as driven (kg) */
  double mass = 0;
  /** from the coupling eye to the middle of the tandem axle (m) */
  double hitchToAxle = 0;
  /** from the coupling eye to the front wall of the body, and to the rear end (m) */
  double bodyFront = 0;
  double length = 0, width = 0, height = 0;
  /** distance between the two axles (m) */
  double axleSpread = 0, track = 0, wheelRadius = 0;
  /** deceleration its brakes give at full pedal (m/s²) */
  double brake = 0;
  double rollingResistance = 0;
};

/** 7.9 m from the coupling to the rear end, 2.5 m wide, 6 t as driven (10 t gross). */
extern const TrailerSpec TANDEM_TRAILER;

class Trailer {
 public:
  Trailer(const TrailerSpec& specIn, Ground& groundIn) : spec(specIn), ground(groundIn) {}

  const TrailerSpec spec;
  /** the middle of the axle group on the ground */
  double x = 0, y = 0, z = 0;
  /** heading (0 = north, positive = to the left) and slope (nose up positive) */
  double yaw = 0, pitch = 0;
  /** the coupling eye at the tip of the drawbar */
  double hitchX = 0, hitchY = 0, hitchZ = 0;
  /** speed along its own axis (m/s, forwards positive) */
  double speed = 0;
  /** angle between truck and trailer (rad; 0 = in line, positive = the truck is turned to the left of the trailer) */
  double articulation = 0;
  bool coupled = false;
  /** its own parking brake (spring brake): holds it when it stands alone */
  bool parked = true;
  /** how far the wheels have turned (rad), for the picture */
  double wheelSpin = 0;

  /** Where the coupling of the truck is right now (world). */
  static void truckHitch(const Vehicle& truck, double out[3]);
  /** Stand the trailer somewhere by itself: axle group at (x, z), facing `yaw`. */
  void place(double x, double z, double yaw);
  /** Hang it on the truck, in line behind it. */
  void coupleBehind(const Vehicle& truck);
  /** Couple where it stands (the truck has backed up to it). */
  void couple() {
    coupled = true;
    accel_ = 0;
  }
  void uncouple();
  /**
   * How far the truck's coupling is from the trailer's eye (m, on the
   * ground) and how far out of line the two stand (rad) – for backing up to
   * a trailer that stands alone.
   */
  void offsetFrom(const Vehicle& truck, double& dist, double& angle) const;
  /**
   * One step, after the truck has moved.  `input` is what the driver does
   * with the truck: the trailer brakes with the truck's brake pedal and
   * parking brake.
   */
  void step(double dt, const Vehicle& truck, const DriverInput& input);

 private:
  Ground& ground;
  double accel_ = 0;
  double hit_[3] = {0, 0, 0};
  GroundSample sample_;

  /** heights and the place of the coupling eye from where the axle group stands */
  void settle(double hitchY = NAN);
};

}  // namespace fs
