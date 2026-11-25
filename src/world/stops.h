// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The stops of the map (stops.json.gz, made by tools/build-stops.ts from
 * OpenStreetMap): every tram and bus stop under its name, in its town, with
 * the poles people wait at and the platforms drawn on the map – what the
 * game draws, offers as start points for the tram, and drives the bus to.
 *
 * Origin: src/world/stops.ts @ v1.7.7
 */
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "world/types.h"

namespace fs {

struct StopPlace;

struct StopPole {
  StopPlace* place = nullptr;
  double x = 0;
  double z = 0;
  /** STOP_FLAG bits: what is there (shelter, bench, …) and who calls (tram, bus) */
  int flags = 0;
};

struct StopPlatform {
  StopPlace* place = nullptr;
  int flags = 0;
  /** metres; 0 where the map does not say */
  double width = 0;
  /** x, z, … along its edge, or round it (`area`) */
  std::vector<float> pts;
  bool area = false;
};

struct StopPlace {
  int id = 0;
  std::string name;
  std::string town;
  /** STOP_FLAG.TRAM | BUS | RAIL */
  int kind = 0;
  double x = 0;
  double z = 0;
  std::vector<StopPole> poles;
  std::vector<StopPlatform> platforms;
};

class Stops {
 public:
  explicit Stops(const StopsJson& json);
  Stops(const Stops&) = delete;
  Stops& operator=(const Stops&) = delete;

  /** (a deque: the poles and platforms point to their place) */
  std::deque<StopPlace> places;

  /** The places with a member within `r` metres of (x, z) – each once. */
  std::vector<StopPlace*>& near(double x, double z, double r, std::vector<StopPlace*>& out) const;
  std::vector<const StopPlace*> near(double x, double z, double r) const;

  /** The stops trams call at, by town and name (the start points of the tram). */
  std::vector<StopPlace*> tramPlaces();

  /** The place of that name nearest to (x, z), or null. */
  StopPlace* named(const std::string& name, double x, double z);

 private:
  std::unordered_map<int64_t, std::vector<StopPlace*>> grid_;
  static int64_t key(double x, double z);
};

}  // namespace fs
