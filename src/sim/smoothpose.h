// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/smoothpose.ts @ v1.7.7
/**
 * How the cars of the traffic turn and move smoothly.
 *
 * A car of the traffic (src/sim/traffic) rides on polylines: its lane's
 * road, and through a junction a curve of a handful of straight pieces.  Its
 * heading taken from the piece it is on turns by a whole corner of the line
 * at once – 15° on one picture, 30° on the next – and a lane drawn beside the
 * middle of its road jumps sideways at each bend of that road.
 *
 * Here instead:
 * - the heading is that of the chord between two points of its way, half a
 *   wheelbase behind and ahead of it (as its axles would stand) – it turns
 *   gradually through each corner, with no lag on the curve as a whole;
 * - what turns it besides its way (moving over to another lane, going round
 *   something) eases in rather than jumping;
 * - its place moves on along its heading by the distance it drove, and is
 *   drawn towards the place its way gives – a jump sideways becomes a gentle
 *   drift over a metre or two (a real jump, many metres, is taken at once);
 * - its front wheels are steered by how fast it turns for the way it goes.
 */
#pragma once

#include <optional>

#include "core/geom.h"

namespace fs {

namespace smoothpose {
/**
 * An angle brought to (−π, π].
 * (The original's module has a `wrapAngle` of its own beside core/geom's, with other
 * rounding: here it keeps its name inside this namespace.)
 */
double wrapAngle(double a);
}  // namespace smoothpose

/** The heading (yaw: 0 north, + to the left) from point a to point b on the ground; NaN where they fall together. */
double chordYaw(double ax, double az, double bx, double bz);

/** The heading of the chord of a polyline from s − h to s + h (both kept on it) – NaN where it is too short. */
double chordYawOn(const Polyline3& pl, double s, double h);

/** Half the chord a vehicle of this length turns by (m): about half its wheelbase. */
double chordHalf(double length);

/** The pose that is eased: where it is drawn, which way it faces, the turn besides its way, its front wheels. */
struct EasedPose {
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** the turn besides its way (rad), eased in */
  std::optional<double> yawX = std::nullopt;
  /** the front wheels' angle (rad, + to the right, as the car models take it) */
  std::optional<double> steer = std::nullopt;
};

/**
 * One step of a car's pose: `wayYaw` the heading its way gives (the chord),
 * `extra` the turn besides it (moving over, going round something), (rawX,
 * rawZ) where its way puts it; `ds` how far it drove, `dt` the time,
 * `wheelbase` for the steering.  Updates `p`.
 */
void easePose(EasedPose& p, double rawX, double rawZ, double wayYaw, double extra, double ds, double dt, double wheelbase);

}  // namespace fs
