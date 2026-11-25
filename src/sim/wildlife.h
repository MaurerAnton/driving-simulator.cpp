// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/wildlife.ts @ v1.7.7
/**
 * Deer and wild boar on the country roads at dusk and at night.
 *
 * Now and then – rarely – where a country road runs along or through a
 * forest, animals come out of the trees ahead of the player and cross: a
 * roe deer, often a second and a third one after it (where one crossed,
 * more follow – as the theory lesson says), or a wild boar with her young.
 * They step out to the edge of the forest, stand a moment, and run across
 * into the field or the forest on the other side.  Only when it is getting
 * dark or dark (the sun a few degrees above the horizon or lower), only
 * outside towns, never on a motorway, in a tunnel or on a bridge, and only
 * where the ground beside the road is forest (the terrain's cover).
 *
 * While they are on or beside the road they are obstacles for the traffic
 * (sim/traffic.ts `wild`): the cars brake for them as for anything in their
 * lane.  render/wildlife.ts draws them (the models of render/animals.ts).
 *
 * All of this is plain arithmetic over a small host interface – the sun,
 * the cover and height of the ground, the road near a point – so that it
 * runs in the tests without a map; `worldRoads` gives the host its roads
 * from the game's world.
 */
#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "sim/playerstate.h"

namespace fs {

class World;

/** The road near a point: its middle line there, the way it runs (a unit vector), half its width; whether it is a country road animals may cross. */
struct WildRoad {
  double x = 0;
  double z = 0;
  double dx = 0;
  double dz = 0;
  double half = 0;
  bool rural = false;
};

struct WildHost {
  /** the sun's elevation (degrees) */
  std::function<double()> sun;
  /** the ground's cover class (world/cover.ts) at a point */
  std::function<int(double x, double z)> coverAt;
  /** the ground's height at a point (NaN: not known) */
  std::function<double(double x, double z)> heightAt;
  /** the road nearest the point within `reach` metres (false: none) */
  std::function<bool(double x, double z, double reach, WildRoad& out)> road;
};

/** 'deer' | 'boar' | 'piglet' */
using WildKind = std::string;

/** One animal: where it is, which way it faces, how fast it goes, how far it ran (for the legs). */
struct WildAnimal {
  WildKind kind;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  double speed = 0;
  double dist = 0;
  /** out of the forest to the edge; standing there; across the road; away into the far side ('out' | 'wait' | 'cross' | 'away') */
  std::string phase = "out";
  /** seconds until it sets off (the ones following wait longer) */
  double delay = 0;
  /** how long it stands at the edge */
  double pause = 0;
  /** its way: from the forest (a), the edge of the road (e), the far side (b) */
  double ax = 0;
  double az = 0;
  double ex = 0;
  double ez = 0;
  double bx = 0;
  double bz = 0;
  double run = 0;
};

class Wildlife {
 public:
  std::vector<WildAnimal> animals;
  /** the animals as obstacles for the traffic (those near the road) */
  std::vector<PlayerState*> boxes;
  /** off: no animals (and those there go) */
  bool enabled = true;
  /** the chance at each look where a forest meets the road at night (twice that at dusk; 1: every time – to try it out) */
  double chance;

  /** `rnd`: the random numbers (null: Math.random – fs::rnd) */
  explicit Wildlife(WildHost host, std::function<double()> rnd = nullptr);
  Wildlife(const Wildlife&) = delete;
  Wildlife& operator=(const Wildlife&) = delete;

  void clear();

  /** On by dt seconds; `player`: where the player's vehicle is and how fast it goes (x, z, yaw, speed). */
  void update(double dt, const PlayerState& player);

 private:
  WildHost host;
  std::function<double()> rnd;
  double timer;
  double cooldown = 0;
  WildRoad road{0, 0, 0, -1, 3, false};
  std::deque<PlayerState> pool;

  void look(const PlayerState& player);
  void spawn(const WildRoad& r, double side);
  void step(double dt, const PlayerState& player);
  void makeBoxes();
};

/** The host's roads from the game's world: the nearest road to a point, and whether it is a country road animals cross. */
std::function<bool(double x, double z, double reach, WildRoad& out)> worldRoads(World& world);

}  // namespace fs
