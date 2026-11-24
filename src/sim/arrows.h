// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/arrows.ts @ v1.7.7
/**
 * Where a green arrow hangs beside a traffic light.
 *
 * The map says nothing about it, so it is worked out: an approach gets an
 * arrow for turning right where that turn is in nobody's way for a good part
 * of the time its own light is red.  Typically the main road at a T-junction,
 * on the side the side road joins: while the side road has green, the only
 * vehicles that could meet the ones turning right into it – those coming the
 * other way and turning left – have red.
 *
 * Looked at for each approach of a junction with lights:
 *
 *  - the paths through the junction that cross or join the right turn: the
 *    groups of their lights must be red while the arrow is lit (`foes`);
 *  - no pedestrian light across the approach itself: people cross there
 *    exactly while its light is red;
 *  - a pedestrian light across the road the turn leads into: the vehicles
 *    there must have green while the arrow is lit, for the people have red
 *    then (`friends`);
 *  - whether, by the programme of the lights, that leaves the arrow lit long
 *    enough to be worth it.
 *
 * Only junctions that are one junction: where the carriageways of a dual
 * road cross in several, the vehicles between them are let in by the light
 * of another approach, and what is free when cannot be told from one of
 * them.
 */
#pragma once

#include <optional>
#include <vector>

#include "sim/signals.h"
#include "world/lanes.h"
#include "world/world.h"

namespace fs {

/**
 * The arrow an approach could have – or none: there is no right turn, it is
 * never free, or people cross where the vehicles would start.
 */
std::optional<Arrow> findArrow(World& world, LaneGraph& graph, SignalSystem& signals, Node& node, Arm& arm);

/** (one junction of markArrows: the arrows of `node`, at a `share` of the approaches that could have one) */
int markArrowsAt(World& world, LaneGraph& graph, SignalSystem& signals, Node& node, double share);

/**
 * Put up the arrows of the junctions in `nodes` (those that have just been
 * loaded) – at a `share` of the approaches that could have one, always the
 * same ones.  Returns how many.
 */
template <typename Nodes>
int markArrows(World& world, LaneGraph& graph, SignalSystem& signals, const Nodes& nodes, double share = 1) {
  int n = 0;
  for (Node* node : nodes) n += markArrowsAt(world, graph, signals, *node, share);
  return n;
}

/** Take down the arrows of junctions that are no longer loaded. */
template <typename Nodes>
void dropArrows(SignalSystem& signals, const Nodes& nodes) {
  for (Node* node : nodes)
    for (Arm& arm : node->arms) signals.arrows.erase(&arm);
}

}  // namespace fs
