// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/trailer.ts @ v1.7.7
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
 */
#pragma once

#include <array>
#include <optional>

#include "sim/vehicle/index.h"

namespace fs {

struct TrailerSpec {
  /** mass as driven (kg) */
  double mass = 0;
  /** from the coupling eye to the middle of the tandem axle (m) */
  double hitchToAxle = 0;
  /** from the coupling eye to the front wall of the body, and to the rear end (m) */
  double bodyFront = 0;
  double length = 0;
  double width = 0;
  double height = 0;
  /** distance between the two axles (m) */
  double axleSpread = 0;
  double track = 0;
  double wheelRadius = 0;
  /** deceleration its brakes give at full pedal (m/s²) */
  double brake = 0;
  double rollingResistance = 0;
  /** where the coupling sits on the towing vehicle: metres behind its rear axle, and above the ground (default: the truck's) */
  std::optional<TruckHitch> hitch = std::nullopt;
};

/** 7.9 m from the coupling to the rear end, 2.5 m wide, 6 t as driven (10 t gross). */
inline const TrailerSpec TANDEM_TRAILER = {
    .mass = 6000,
    .hitchToAxle = 5.4,
    .bodyFront = 1.5,
    .length = 7.9,
    .width = 2.5,
    .height = 3.6,
    .axleSpread = 1.3,
    .track = 2.04,
    .wheelRadius = 0.4,
    .brake = 6.2,
    .rollingResistance = 0.008,
};

/**
 * A tandem box trailer behind a car (class BE): 4.85 m from the coupling to
 * the rear end, 2.05 m wide, 1.4 t as driven, with an overrun brake.
 */
inline const TrailerSpec CAR_TRAILER = {
    .mass = 1400,
    .hitchToAxle = 3.1,
    .bodyFront = 1.15,
    .length = 4.85,
    .width = 2.05,
    .height = 2.1,
    .axleSpread = 0.8,
    .track = 1.75,
    .wheelRadius = 0.3,
    .brake = 5.5,
    .rollingResistance = 0.012,
    .hitch = TruckHitch{.behindRearAxle = 0.95, .height = 0.45},
};

/** The same box trailer behind a 7.5 t truck (class C1E): coupled higher, further behind the axle. */
inline const TrailerSpec LIGHT_TRUCK_TRAILER = [] {
  TrailerSpec t = CAR_TRAILER;
  t.mass = 2600;
  t.brake = 5.8;
  t.hitch = TruckHitch{.behindRearAxle = 2.25, .height = 0.75};
  return t;
}();
/** … and behind a bus or a minibus, for the luggage (classes D1E, DE). */
inline const TrailerSpec BUS_TRAILER = [] {
  TrailerSpec t = CAR_TRAILER;
  t.mass = 1800;
  t.hitch = TruckHitch{.behindRearAxle = 3.6, .height = 0.55};
  return t;
}();
inline const TrailerSpec MINIBUS_TRAILER = [] {
  TrailerSpec t = CAR_TRAILER;
  t.mass = 1500;
  t.hitch = TruckHitch{.behindRearAxle = 2.0, .height = 0.5};
  return t;
}();

/** The outline of a trailer in the plan, from the middle of its axle group: ahead to its front, behind to its rear, half its width, ahead to the coupling. */
struct TrailerOutline {
  double front;
  double rear;
  double hw;
  double hitch;
};
TrailerOutline trailerOutline(const TrailerSpec& t);

class Trailer {
 public:
  const TrailerSpec spec;
  /** the middle of the axle group on the ground */
  double x = 0;
  double y = 0;
  double z = 0;
  /** heading (0 = north, positive = to the left) and slope (nose up positive) */
  double yaw = 0;
  double pitch = 0;
  /** the coupling eye at the tip of the drawbar */
  double hitchX = 0;
  double hitchY = 0;
  double hitchZ = 0;
  /** speed along its own axis (m/s, forwards positive) */
  double speed = 0;
  /** angle between truck and trailer (rad; 0 = in line, positive = the truck is turned to the left of the trailer) */
  double articulation = 0;
  bool coupled = false;
  /** its own parking brake (spring brake): holds it when it stands alone */
  bool parked = true;
  /** how far the wheels have turned (rad), for the picture */
  double wheelSpin = 0;

  Trailer(const TrailerSpec& spec, Ground& ground);
  virtual ~Trailer() = default;

  /** how high the coupling is above the ground */
  double hitchHeight() const;

  /** Where the coupling of the towing vehicle is right now (world). */
  void hitchPoint(const Vehicle& v, double* out) const;

  /** Where the coupling of the truck is right now (world). */
  static void truckHitch(const Vehicle& truck, double* out);

  /** Stand the trailer somewhere by itself: axle group at (x, z), facing `yaw`. */
  void place(double x, double z, double yaw);

  /** Hang it on the truck, in line behind it. */
  void coupleBehind(const Vehicle& truck);

  /** Couple where it stands (the truck has backed up to it). */
  void couple();

  void uncouple();

  /**
   * How far the truck's coupling is from the trailer's eye (m, on the
   * ground) and how far out of line the two stand (rad) – for backing up to
   * a trailer that stands alone.
   */
  struct Offset {
    double dist;
    double angle;
  };
  Offset offsetFrom(const Vehicle& truck);

  /**
   * One step, after the truck has moved.  `input` is what the driver does
   * with the truck: the trailer brakes with the truck's brake pedal and
   * parking brake.
   */
  void step(double dt, Vehicle& truck, const DriverInput& input);

 protected:
  std::array<double, 3> hit{0, 0, 0};

  /** heights and the place of the coupling eye from where the axle group stands */
  void settle(std::optional<double> hitchY = std::nullopt);

 private:
  Ground& ground;
  double accel = 0;
  GroundSample sample{};
};

}  // namespace fs
