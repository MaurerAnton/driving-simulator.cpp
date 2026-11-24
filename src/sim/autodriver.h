// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/autodriver.ts @ v1.7.7
/**
 * A driver who keeps to the rules: follows a route along the lanes with the
 * real vehicle model, at the speed the signs allow and the bends permit,
 * stops at red lights and stop lines, indicates, looks over the shoulder,
 * keeps a distance, gives way and lets pedestrians cross.
 *
 * It drives the car of the demonstration drives in the game, and it is the
 * "virtual learner driver" of tests/drive.sim.ts – what is shown to a
 * learner is what is checked against the rule monitor there.
 *
 * The driver only decides; whoever uses it steps the vehicle and the rule
 * monitor (whose idea of the lane the car is on the driver shares).
 */
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/pastblock.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

struct AutoDriverOptions {
  VehicleSpec spec;
  /** from the point the vehicle is measured at (Vehicle.getModelPosition) to its front bumper (m) */
  double front = 0;
  /** a truck with a trailer: slower in turns, earlier on the brakes, 60 km/h outside town and 80 on the motorway */
  bool truck = false;
  /** false: lights and stop signs are ignored (to check that the rule monitor notices) */
  bool obey = true;
  /**
   * at most this outside built-up areas and on the motorway (km/h) – by default a truck's 60 and 80.
   * (Not given: by `truck`; given without a value: none – the original's undefined and null.)
   */
  std::optional<std::optional<RuleMonitor::Caps>> caps = std::nullopt;
};

/** Why the driver does what it is doing – for telling a learner who watches: '' | 'red' | 'stop' | 'yield' | 'zebra' | 'follow' | 'turn' | 'lane' | 'rbl' | 'roundabout' | 'ringExit' | 'halt' | 'deadEnd' | 'cyclist' | 'arrow' | 'lightsOut' | 'closed' */
using AutoRemark = std::string;

struct AutoCommand {
  /** -1 … 1, positive = to the right */
  double steer = 0;
  double throttle = 0;
  double brake = 0;
  /** -1 left, 0 off, 1 right */
  int indicator = 0;
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
  Connector* turnRound = nullptr;
};

class AutoDriver {
 public:
  struct Halt {
    double x = 0;
    double z = 0;
  };
  struct Seen {
    Connector* next = nullptr;
    double tx = 0;
    double tz = 0;
    double limit = 0;
    double distToLine = JS_INF;
    bool mustStop = false;
    bool crossStop = false;
    int turn = 0;
    double gap = JS_INF;
  };

  World& world;
  SignalSystem& signals;
  RuleMonitor& rules;
  /** the other road users, if the driver is to mind them */
  Traffic* traffic;
  /** (with `caps` worked out: always given) */
  const AutoDriverOptions opts;
  /** where to come to a halt (a place on a road of the route's last piece), or none: drive on */
  std::optional<Halt> halt = std::nullopt;
  AutoCommand cmd;
  /** what the last step saw – for tracing */
  Seen seen;

  AutoDriver(World& world, SignalSystem& signals, RuleMonitor& rules, Traffic* traffic, const AutoDriverOptions& opts);

  /** Forget what was under way (after the car was put somewhere else). */
  void reset();

  /**
   * One step.  `mpos`: where the vehicle is (Vehicle.getModelPosition),
   * `route`: the connectors of the way to go (from the navigation), in order.
   */
  AutoCommand& step(double dt, const Vehicle& v, const double* mpos, const std::vector<Connector*>& route);

 private:
  const double turnSpeed;
  const double decel;
  double stoppedFor = 0;
  double yieldWait = 0;
  double changeT = 0;
  double netWait = 0;
  int lastTurn = 0;
  Connector* lastNext = nullptr;
  Lane* haltLane = nullptr;
  /** going round something that stands in the way (src/sim/pastblock) */
  std::shared_ptr<PastState> past = nullptr;
  PolyProjection proj;
  /** the way ahead as bendCap samples it: which way it runs, every few metres */
  std::array<float, 2 * 48> bendDir{};
  PolySample bendSmp = newPolySample();
  int bendTick = 0;
  double bendLimit = JS_INF;
  SurfaceHit hit = newSurfaceHit();
  SelfDriving me;
  PolySample smp = newPolySample();
  double haltS = 0;

  /**
   * The speed the bends ahead allow right now: each is to be reached at the
   * speed it can be driven at (with the sideways pull the steering keeps to),
   * by braking gently from here.  The steering alone sees a bend a second
   * ahead – enough in town, far too late for a hairpin behind a straight on a
   * country road.  `from`: how far along the lane (or, in a junction, along
   * the path through it) the car is.
   */
  double bendCap(Lane* lane, double from, Connector* next, const std::vector<Connector*>& route, double speed);
};

}  // namespace fs
