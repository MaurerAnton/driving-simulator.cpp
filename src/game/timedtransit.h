// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The trams and buses of the traffic as the timetables run them: so that the
 * tram or bus a departure board announces is at the stop when the board
 * says – it comes in a little before its time, its doors open, and it leaves
 * at the minute the board counted down to.
 *
 * The departure boards read the timetable (src/sim/timetable.ts); the
 * vehicles are simulated – a tram by src/sim/rail.ts, a bus among the cars by
 * src/sim/traffic.ts – and come and go round the player.  What ties the two
 * together is here:
 *
 * - Where a vehicle appears: the trips under way round the player are put
 *   where the clock has them on their line (a trip about to leave its first
 *   stop stands there a while before), the nearest first, each once.
 * - How fast a tram runs: no faster than it needs to arrive at its next stop
 *   of the trip ARRIVE_EARLY seconds before it leaves there – the trams of
 *   the simulation are quicker than the timetables' running times, which
 *   count the minutes at the stops in.  It passes the stops its trip does not
 *   call at.
 * - How long it stands: until its time (not before), and when it is late no
 *   longer than the doors take.
 * - At the end of its trip it goes (out of the player's sight).
 * - One that has fallen far behind its trip (the game stood still while the
 *   clock ran on, the player blocked it) is put where its trip is, when the
 *   player is near neither place.
 *
 * Times: the timetable counts seconds after midnight of a service day in
 * Kassel (Europe/Berlin); a trip remembers the moment (ms) of the midnight
 * its times count from, so that it keeps to them past midnight too.  The
 * world's moment is the game's clock (src/game/clock.ts), not the computer's.
 *
 * Origin: src/game/timedtransit.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <memory>

#include "sim/rail.h"
#include "sim/timetable.h"
#include "sim/traffic.h"
#include "ui/transitmap.h"
#include "world/types.h"

namespace fs {

/** how long before its departure a vehicle of the timetable is to arrive at a stop (s): the doors open, people get off and on */
inline constexpr double ARRIVE_EARLY = 15;
/** how long before it leaves its first stop a trip is put there, standing (s) */
inline constexpr double LEAD = 120;

/**
 * The speed to run at so as to cover `dist` metres in `time` seconds,
 * starting and stopping included (`ramp`: the seconds lost per m/s) –
 * Infinity where that cannot be done (or no time is left).
 */
double paceFor(double dist, double time, double ramp = 0.5 / 1.0 + 0.5 / 1.04);

struct TramScheduleHost {
  std::function<const Timetable*()> timetable;
  /** the lines of the trams (lines.json.gz: their stops as piece of track and mark) */
  std::function<const LinesJson*()> lines;
  std::function<RailTraffic&()> rail;
  /** the world's moment (the game's clock), ms since 1970 */
  std::function<double()> nowMs;
};

/** The timetable as the trams of the traffic keep to it (RailTraffic.schedule). */
std::unique_ptr<RailSchedule> tramSchedule(TramScheduleHost h);

/** The world's clock jumped: the buses of the timetables go, and come again at once where their trips are then. */
void resyncBuses(Traffic& traffic);

struct BusScheduleHost {
  std::function<const Timetable*()> timetable;
  /** where the trips are on their lines' roads (spectate.ts transitSource) */
  std::function<TransitSource*()> source;
  std::function<Traffic&()> traffic;
  std::function<double()> nowMs;
};

/**
 * The timetable as the buses of the traffic keep to it (Traffic.busSchedule):
 * the buses of the lines that have timetables come where and when their
 * trips are, and stand at a stop until their time.
 */
std::unique_ptr<BusSchedule> busSchedule(BusScheduleHost h);

}  // namespace fs
