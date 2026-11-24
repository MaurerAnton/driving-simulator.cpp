// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/netfile.ts @ v1.7.7
/**
 * The road network of the whole map as one small file (map/net.bin.gz): what
 * a route planner needs of every road – where it is, how long, how fast,
 * what may be turned into at its ends – and its course in a few points, for
 * drawing the map.  The roads themselves, with their lanes and junctions,
 * come in chunks around the car (world.h); this file is what lets a route
 * or a tour be planned through parts of the map that are not loaded.
 *
 * Written by the map build (tools/build-graph.ts), read by the game.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "world/routegraph.h"

namespace fs {

struct NetData {
  /** junctions: where they are */
  std::vector<float> nodeX;
  std::vector<float> nodeZ;
  /** roads: the junctions at their ends, their length (m) and class */
  std::vector<uint32_t> edgeA;
  std::vector<uint32_t> edgeB;
  std::vector<float> edgeLen;
  std::vector<uint8_t> edgeCls;
  /** ways (road * 2 + direction, see routegraph.h): RG bits, and the speed allowed in km/h (0: not to be driven this way) */
  std::vector<uint8_t> wayKind;
  std::vector<uint8_t> waySpeed;
  /** the turns at the end of each way: first[w] … first[w + 1] - 1 index `to`, `turn` (TURN) and `across` (dm through the junction) */
  std::vector<uint32_t> first;
  std::vector<uint32_t> to;
  std::vector<uint8_t> turn;
  std::vector<uint16_t> across;
  /**
   * the course of each road between its ends: polyFirst[e] … polyFirst[e + 1] - 1
   * index pairs in `poly` – steps (east, south) in quarter metres from the
   * point before, starting at junction a
   */
  std::vector<uint32_t> polyFirst;
  std::vector<int16_t> poly;
};

/** quarter metres: the unit of the steps in NetData.poly */
constexpr double POLY_UNIT = 0.25;

std::vector<uint8_t> packNet(const NetData& d);

/** (throws std::runtime_error if the bytes are not a road-network file) */
NetData unpackNet(std::vector<uint8_t> buf);

/** The network as the route planner searches it. */
RouteGraph routeGraphOfNet(const NetData& d);

/** The course of road `e`: x, z, x, z, … from junction a to junction b. */
std::vector<double> netPolyline(const NetData& d, int e);

}  // namespace fs
