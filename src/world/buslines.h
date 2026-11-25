// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The lines of the city buses (buslines.json.gz, made by
 * tools/build-buslines.ts from the route relations of OpenStreetMap): for
 * each direction of a line the roads it drives along and the stops it calls
 * at – what the buses of the traffic follow (src/sim/traffic.ts), with the
 * line and its destination on their fronts.
 *
 * The roads are named as the route planner names them (routegraph.ts): a
 * "way" is a road of the map driven in one direction, road * 2 + (0: from
 * its junction a to b, 1: from b to a).  A road keeps its number in every
 * piece of the network it comes with, so a lane knows at once whether a line
 * runs along it (`wayOf` in routegraph.ts) – wherever the network is loaded.
 *
 * Origin: src/world/buslines.ts @ v1.7.7
 */
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/task.h"
#include "world/types.h"

namespace fs {

class MapLoader;

/** A stop of a line: the stop place (its number in stops.json.gz), the line's way it lies on (index into `ways`) and how far along that way (m, in the direction of travel). */
struct BusLineStop {
  int place = 0;
  int k = 0;
  double m = 0;
};

/** One direction of a bus line. */
struct BusLine {
  /** its number in BusLines.lines */
  int index = 0;
  /** "52", "N3" – '' where OpenStreetMap has none */
  std::string ref;
  std::string name;
  std::string from;
  std::string to;
  std::string operator_;
  /** "#e3000f" or '' */
  std::string colour;
  /** 0 bus, 1 trolleybus */
  int kind = 0;
  /** the route relation in OpenStreetMap */
  int64_t osm = 0;
  /** the ways it drives, in order: its route – and from `end` on the way round to where `back` begins */
  std::vector<int32_t> ways;
  int end = 0;
  /** its stops, in order */
  std::vector<BusLineStop> stops;
  /** the line it goes on as at the end of its route (the line back, or itself round again), or null */
  BusLine* back = nullptr;
};

/** The ways of a line as the file keeps them: each as the difference to the one before (mostly ±2, which packs small). */
std::vector<int> encodeWays(const std::vector<int32_t>& ways);

class BusLines {
 public:
  explicit BusLines(const BusLinesJson& json);
  BusLines(const BusLines&) = delete;
  BusLines& operator=(const BusLines&) = delete;

  struct At {
    BusLine* line = nullptr;
    int k = 0;
  };
  /** (pointers into `own_`, which keeps them in place) */
  std::vector<BusLine*> lines;
  /** the build of the road network the ways belong to (meta.generated of the map), and how many stop places the stops count on */
  std::string map;
  int places = 0;

  /** Do these lines belong to this build of the map – its road network (meta.generated) and, if given, the number of its stop places? */
  bool fits(const std::string& generated, std::optional<int> places = std::nullopt) const;
  /** Visit the lines that drive along way `w`, each with where on its ways that is (a line that drives it twice comes twice). */
  void forEachAt(int w, const std::function<void(BusLine& line, int k)>& visit) const;
  /** The lines along way `w`, with where on their ways it is. */
  std::vector<At> at(int w) const;
  /** Does any line drive along way `w`? */
  bool has(int w) const;
  /** The stops of a line on its ways k0 … k1, in order. */
  std::vector<BusLineStop>& stopsOn(const BusLine& line, int k0, int k1, std::vector<BusLineStop>& out) const;

 private:
  std::deque<BusLine> own_;
  /** for each way, which lines drive along it: refLine/refK[start[w] … start[w + 1] - 1] (ways beyond `start` have none) */
  std::vector<uint32_t> start_;
  std::vector<uint32_t> refLine_;
  std::vector<uint32_t> refK_;
};

/**
 * The bus lines of a map – null where the map has none, or where they
 * were made for another build of its road network (the numbers of the roads
 * would lead astray), or – given the number of its stop places – for other
 * stops.
 */
Task<std::shared_ptr<BusLines>> loadBusLines(MapLoader* map, std::optional<int> places);

}  // namespace fs
