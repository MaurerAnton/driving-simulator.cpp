// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/semice.ts @ v1.7.7
/**
 * Class CE with the articulated lorry: the same three tasks as with the
 * drawbar combination (game/ce.ts) – coupling and uncoupling, reversing round
 * a corner, reversing to the ramp – done with a tractor unit and its
 * semi-trailer (sim/semitrailer.ts).
 *
 * The coupling procedure has the same steps in the same order – only some of
 * them are done differently, and are said differently:
 *
 *   uncoupling ("Absatteln"): stand straight, handbrake; set the trailer's
 *   parking brake, chocks; wind the landing gear down; red line, yellow
 *   line, cables off; pull the fifth wheel's release handle; drive slowly
 *   out from under the trailer – it stays on its legs.
 *
 *   coupling ("Aufsatteln"): look at the trailer (secured, kingpin, the fifth
 *   wheel open); reverse in line until the fifth wheel stands just before the
 *   trailer's front, stop; compare the heights (the trailer's plate a little
 *   below the fifth wheel); reverse at a crawl under it until the jaws close
 *   round the kingpin; check the lock; yellow, red, cables on; wind the
 *   landing gear up; chocks away, the trailer's parking brake off; lights;
 *   brake test.
 *
 * What the steps do to the trailer (its brake, its lines, its legs) is set
 * here from the procedure, and the tractor is stopped where it stands
 * against the trailer.
 */
#pragma once

#include <array>
#include <string>

#include "game/ce.h"

namespace fs {

class Trailer;
class Vehicle;

/** What the judge of the reversing tasks needs of the articulated lorry. */
extern const RigShape SEMI_RIG;

/**
 * Where to stop before going under the trailer: the fifth wheel this far
 * from the kingpin – just before its ramps touch the trailer's front edge
 * (1.6 m ahead of the kingpin, the fifth wheel's half-width more).
 */
inline constexpr std::array<double, 2> SEMI_APPROACH = {2.3, 4.0};

/** The i18n key of what to do now ('do') or of a thing to do on foot ('step'): "semi.…" where the articulated lorry has its own, else "ce.…". */
std::string taskTextKey(bool semi, const std::string& what, const std::string& id);

/**
 * The tractor backing up to a semi-trailer that stands alone: the kingpin
 * goes into the fifth wheel's jaws and no further; a tractor that comes in
 * beside the kingpin goes on under the trailer until its cab stands against
 * the trailer's front.  Pushes the tractor back out to where it stops and
 * returns whether it ran in faster than a crawl.  `mpos`: scratch for the
 * tractor's model origin.
 */
bool semiStop(Vehicle& truck, const Trailer& trailer, double* mpos);

/** What the steps done so far have done to the semi-trailer: its brake, its lines, its landing gear. */
void syncSemi(Trailer& trailer, const CouplingProcedure& proc);

}  // namespace fs
