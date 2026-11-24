// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/payload.ts @ v1.7.7
/**
 * A load in the box of a truck, and what it does to the vehicle's physics.
 *
 * The vehicle model (sim/vehicle/vehicle.ts) takes its mass, its moments of
 * inertia and where its centre of mass lies from the spec it is built with
 * – the springs settle under the weight when it is made, the tyres' grip
 * follows the load on each wheel, the brakes have more to stop.  A load is
 * therefore given as a spec of its own: the same truck, heavier by the
 * load, its centre of mass moved towards the load (back and up: the goods
 * stand on the floor of the box, higher than the chassis), and its moments
 * of inertia grown by the load's own and by how far it lies from the new
 * centre (the parallel-axis theorem).  Everything else – engine, brakes,
 * springs – stays as it is: a loaded truck pulls away more slowly, sits
 * lower, takes longer to stop and burns more.
 */
#pragma once

#include "sim/vehicle/types.h"

namespace fs {

struct Payload {
  /** kg */
  double mass = 0;
  /** where its middle lies: metres behind the middle of the wheelbase, and above the ground */
  double z = 0;
  double y = 0;
  /** its length (along the vehicle), width and height (m) */
  double len = 0;
  double wid = 0;
  double hgt = 0;
};

/** The spec of a vehicle with a load in it (the spec itself if there is none). */
VehicleSpec withPayload(const VehicleSpec& spec, const Payload* load);

}  // namespace fs
