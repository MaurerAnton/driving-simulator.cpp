// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Trams and trains: vehicles that follow the tracks of the map (see
 * tools/mapgen/rails.ts), wherever the player is near tracks.
 *
 * They run in the direction the track was drawn, take the straight way at
 * switches most of the time, slow down for curves, call at the stops, and –
 * being unable to swerve – brake for whatever stands on their track.  At
 * the end of a line they wait and go back.  For the cars of the traffic and
 * for the player they are obstacles like any other vehicle.
 *
 * Origin: src/sim/rail.ts (MaurerAnton/driving-simulator @ ff37c5d, v0.7.0).
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "world/types.h"

namespace fs {

/** A piece of track, ready for running on. */
struct RailPath {
  int kind = 0;
  /** x, y, z of the points along the middle of the track (top of rail) */
  std::vector<double> pts;
  /** metres from the start at each point */
  std::vector<double> cum;
  double len = 0;
  /** pieces joining at the start / at the end: index * 2 + (0: their start is here, 1: their end) */
  std::vector<int> a;
  std::vector<int> b;
  /** stops: metres from the start */
  std::vector<double> stops;
  /** the speed its curves allow at each point (m/s) */
  std::vector<float> cap;
  /**
   * Which way it is run: 1 along the way it was drawn, -1 against it, 0
   * both ways (a single track).  On a double track everybody keeps right,
   * so the other track lies to the left of the direction of travel.
   */
  int way = 0;   // 1 | -1 | 0
};

/** What a rail vehicle is made of. */
struct Consist {
  int cars = 0;
  double carLen = 0;
  /** the joint between two cars */
  double gap = 0;
  double width = 0, height = 0;
  double vmax = 0, accel = 0, brake = 0;
  /** the kinds of track it may run on */
  std::vector<int> tracks;
};

/** The three vehicles of the game (RAIL_TRAM / RAIL_LIGHT / RAIL_TRAIN). */
const Consist* consistOf(int kind);

/** One car body: middle of its floor line at rail level, heading (0 = north, positive = left) and slope. */
struct RailCar {
  double x = 0, y = 0, z = 0;
  double yaw = 0, pitch = 0;
};

struct RailVehicle {
  int id = 0;
  int kind = 0;
  const Consist* consist = nullptr;
  /** where its front end is: piece of track, metres from the piece's start, direction of travel along it */
  int path = 0;
  double s = 0;
  int dir = 1;   // 1 | -1
  double speed = 0;
  /** seconds left standing (at a stop, at the end of the line) */
  double dwell = 0;
  /** metres to run before the next stop counts (so that two marks of one stop are served once) */
  double noStop = 0;
  /** pieces chosen for the way ahead, nearest first */
  std::vector<std::pair<int, int>> plan;   // (path, dir)
  std::vector<RailCar> cars;
  /** where the front end has been, newest first, a point every TRAIL_STEP metres */
  struct Trail {
    std::vector<double> x, y, z;
    std::vector<float> s;
    std::vector<int> path;
    std::vector<int8_t> dir;
    int n = 0, head = 0, count = 0;
    float since = 0;
    void init(int nIn);
    /** k entries back from the newest */
    int at(int k) const { return ((head - k) % n + n * 4) % n; }
    void push(double x, double y, double z, int path, float s, int dir);
  } trail;
  /** something is on the track ahead (for a bell, for tests) */
  bool blocked = false;
};

/** Something a rail vehicle must not run into: a circle on the ground. */
struct RailObstacle {
  double x = 0, z = 0, r = 0;
};

/** What the rail traffic needs to know about the player's car. */
struct RailPlayer {
  double x = 0, y = 0, z = 0;
  double yaw = 0;
  double length = 0, width = 0;
};

class RailNetwork {
 public:
  explicit RailNetwork(const std::vector<RailJson>& rails);

  std::vector<RailPath> paths;

  /** Places on tracks in the 200 m cell around a point: pairs of (piece, metres). */
  std::vector<double> near(double x, double z) const;

  /** Position (and direction of the track) at `s` metres along a piece. */
  void sample(int path, double s, double& x, double& y, double& z, double& tx, double& tz,
              double& cap) const;

  /**
   * The ways on from the end of a piece that is being left in direction
   * `dir`: pieces of a kind the vehicle may use that continue without a
   * sharp bend, the straightest first.
   */
  struct Way {
    int path = 0, dir = 1;
    double straight = 0;
  };
  std::vector<Way> waysOn(int path, int dir, const std::vector<int>& tracks) const;

 private:
  std::map<long long, std::vector<double>> cells_;
  static long long cell(double x, double z);
};

class RailTraffic {
 public:
  explicit RailTraffic(const std::vector<RailJson>& rails,
                       std::function<double()> random = nullptr);

  RailNetwork net;
  std::vector<RailVehicle> vehicles;
  /** how many vehicles may be about at once */
  int target = 4;
  /** new vehicles appear this far from the player (out of the way, but soon in sight), and go when further than `far` */
  double ringMin = 260, ringMax = 620, far = 900;

  void clear() { vehicles.clear(); }

  /**
   * Put a vehicle on the track at a place (its front end), running in
   * direction `dir` of that piece.  Null if its length does not fit behind it.
   */
  RailVehicle* spawnAt(int path, double s, int dir, double speed = 0);

  /** The cars of all vehicles as circles (three along each car), for whoever must keep clear of them. */
  void circles(const RailVehicle* skip, std::vector<RailObstacle>& out) const;

  /**
   * One step.  `others`: what must not be run into besides the player – the
   * cars of the road traffic as circles.
   */
  void update(double dt, const RailPlayer& player, const std::vector<RailObstacle>& others = {});

  /**
   * Does the player's car (a rectangle on the ground) touch a rail vehicle?
   * Returns how to push it clear (unit vector away from the vehicle, depth)
   * and how fast that vehicle moves, or null.
   */
  struct Touch {
    double nx = 0, nz = 0, depth = 0, vx = 0, vz = 0;
  };
  const Touch* touching(const RailPlayer& player);

 private:
  int nextId_ = 1;
  double spawnT_ = 0;
  std::function<double()> random_;
  std::vector<RailObstacle> obstacles_;
  Touch touch_;
  RailVehicle* spawnNear(const RailPlayer& player);
  void step(RailVehicle& v, double dt, const std::vector<RailObstacle>& obstacles);
  void reverse(RailVehicle& v);
  void place(RailVehicle& v);
};

/** Two rectangles on the ground (centre, heading, half length, half width). */
bool boxOverlap(double ax, double az, double ayaw, double ahl, double ahw, double bx, double bz,
                double byaw, double bhl, double bhw, double& nx, double& nz, double& depth);

}  // namespace fs
