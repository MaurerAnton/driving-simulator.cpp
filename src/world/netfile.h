// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The road network of the whole map as one small file (map/net.bin.gz): what
 * a route planner needs of every road – where it is, how long, how fast,
 * what may be turned into at its ends – and its course in a few points, for
 * drawing the map.  The roads themselves, with their lanes and junctions,
 * come in chunks around the car (world/world.cpp); this file is what lets a
 * route or a tour be planned through parts of the map that are not loaded.
 *
 * Written by the map build (tools/build-graph.ts), read by the game.
 *
 * Origin: src/world/netfile.ts (MaurerAnton/driving-simulator @ 3420ad0,
 * v0.8.0).
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "world/tilefmt.h"

namespace fs {

/** quarter metres: the unit of the steps in NetData::poly */
constexpr double POLY_UNIT = 0.25;

/** The network as the route planner searches it (the sections of net.bin). */
struct NetData {
  /** junctions: where they are */
  std::vector<double> nodeX;
  std::vector<double> nodeZ;
  /** roads: the junctions at their ends, their length (m) and class */
  std::vector<uint32_t> edgeA;
  std::vector<uint32_t> edgeB;
  std::vector<double> edgeLen;
  std::vector<uint8_t> edgeCls;
  /**
   * ways (road * 2 + direction, see routegraph.h): RG bits, and the speed
   * allowed in km/h (0: not to be driven this way)
   */
  std::vector<uint8_t> wayKind;
  std::vector<uint8_t> waySpeed;
  /** the turns at the end of each way: first[w] … first[w + 1] − 1 */
  std::vector<uint32_t> first;
  std::vector<uint32_t> to;
  std::vector<uint8_t> turn;
  /** metres through the junction, in decimetres */
  std::vector<uint16_t> across;
  /** the course of the roads: polyFirst[e] … polyFirst[e + 1] − 1 steps in x, z (POLY_UNIT) */
  std::vector<uint32_t> polyFirst;
  std::vector<int16_t> poly;
};

/** The container as the map build writes it. */
std::vector<uint8_t> packNet(const NetData& d);

/** The network from its container (throws when a section it needs is missing). */
NetData unpackNet(const std::vector<uint8_t>& buf);

/** The course of road `e`: x, z, x, z, … from junction a to junction b. */
std::vector<double> netPolyline(const NetData& d, int e);

}  // namespace fs
