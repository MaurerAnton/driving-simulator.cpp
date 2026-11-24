// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A driver who keeps to the rules: follows a route along the lanes with the
 * real vehicle model, at the speed the signs allow and the bends permit,
 * stops at red lights and stop lines, indicates, looks over the shoulder,
 * keeps a distance, gives way and lets pedestrians cross.
 *
 * It drives the car of the demonstration drives in the game, and it is the
 * "virtual learner driver" of tests/drive.sim.ts – what is shown to a learner
 * is what is checked against the rule monitor there.
 *
 * The driver only decides; whoever uses it steps the vehicle and the rule
 * monitor (whose idea of the lane the car is on the driver shares).
 *
 * Origin: src/sim/autodriver.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/geom.h"
#include "sim/rules.h"
#include "sim/traffic.h"
#include "sim/vehicle/types.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

class Vehicle;
class SignalSystem;
struct VehicleSpec;

/** Options of the driver. */
struct AutoDriverOptions {
  const VehicleSpec* spec = nullptr;
  /** from the point the vehicle is measured at (Vehicle.getModelPosition) to its front bumper (m) */
  double front = 0;
  /** a truck with a trailer: slower in turns, earlier on the brakes, 60 km/h outside town and 80 on the motorway */
  bool truck = false;
  /** false: lights and stop signs are ignored (to check that the rule monitor notices) */
  bool obey = true;
};

/** Why the driver does what it is doing – for telling a learner who watches. */
using AutoRemark = std::string;   // "" | "red" | "stop" | "yield" | "zebra" | "follow" | "turn" | "lane" | "rbl" | "roundabout" | "ringExit" | "halt"

/** What one step decides. */
struct AutoCommand {
  /** -1 … 1, positive = to the right */
  double steer = 0, throttle = 0, brake = 0;
  /** -1 left, 0 off, 1 right */
  double indicator = 0;
  /** the head: rad, positive = looking left */
  double look = 0;
  AutoRemark remark;
  /** standing or stopping because it has to (a light, a line, traffic, the destination) – not stuck */
  bool waiting = false;
  /** standing at the place it was told to halt at */
  bool arrived = false;
  /**
   * Standing at the end of a street the way leads back out of: the path to
   * turn round by.  Turning in the road takes going back and forth, which
   * this driver does not do – whoever runs it puts the car onto `to`.
   */
  struct Connector* turnRound = nullptr;
};

class AutoDriver {
 public:
  World& world;
  SignalSystem& signals;
  RuleMonitor& rules;
  /** the other road users, if the driver is to mind them */
  Traffic* traffic;
  AutoDriverOptions opts;
  /** where to come to a halt (a place on a road of the route's last piece), or null: drive on */
  bool hasHalt = false;
  double haltX = 0, haltZ = 0;
  AutoCommand cmd;
  /** what the last step saw – for tracing */
  struct Seen {
    Connector* next = nullptr;
    double tx = 0, tz = 0, limit = 0, distToLine = INFINITY;
    bool mustStop = false, crossStop = false;
    double turn = 0, gap = INFINITY;
  } seen;

  AutoDriver(World& worldIn, SignalSystem& signalsIn, RuleMonitor& rulesIn, Traffic* trafficIn,
             const AutoDriverOptions& optsIn);

  /** Forget what was under way (after the car was put somewhere else). */
  void reset();

  /**
   * One step.  `mpos`: where the vehicle is (Vehicle.getModelPosition),
   * `route`: the connectors of the way to go (from the navigation), in order.
   */
  AutoCommand step(double dt, const Vehicle& v, const double mpos[3],
                   const std::vector<Connector*>& route);

 private:
  /**
   * How fast the bends further ahead may be come up to: the smallest speed
   * at which every bend on the way (along the lane, through the junction
   * paths, on) can still be driven – looked at a few times a second, since
   * the way ahead changes as the car moves.
   */
  double bendCap(const Lane* lane, double from, const Connector* next,
                 const std::vector<Connector*>& route, double speed);

  double turnSpeed_ = 0, decel_ = 0;
  double stoppedFor_ = 0, yieldWait_ = 0, changeT_ = 0, netWait_ = 0;
  double lastTurn_ = 0;
  Connector* lastNext_ = nullptr;
  Lane* haltLane_ = nullptr;
  double haltS_ = 0;
  PolyProjection proj_;
  SurfaceHit hit_;
  SelfDriving me_;
  PolySample smp_;
  /** how often the bend look has run (it runs every 6th step) */
  int bendTick_ = 0;
  double bendLimit_ = 0;
  double bendDir_[96] = {0};
  PolySample bendSmp_;
};

}  // namespace fs
