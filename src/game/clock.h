// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/clock.ts @ v1.7.7
/**
 * The game's clock: the moment the world is at.
 *
 * Normally that is the real time – the trams and buses run by their
 * timetables as they do now, the sun stands where it stands now, the signs
 * with a time on them count now.  In the arcade mode the player may set the
 * world to another day and hour, to see a place as it will be then (its
 * timetables, its light, its season), and let the time run faster while the
 * autopilot drives.  Back at "now" the clock is the real one again, and what
 * follows real data catches up with it.
 *
 * Everything in the game that asks "what time is it in the world?" asks
 * `gameNow()` (not plat::dateMs()): one clock for all of it.
 *
 * (A moment – the original's Date – is its milliseconds since 1970 here; its
 * calendar fields are plat::localFields(ms) and utcFields(ms), core/date.h.)
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fs {

/** A clock that is the real one, or one of its own that the game moves on. */
class GameClock {
 public:
  /** (`realNow`: the real time in milliseconds since 1970 – plat::dateMs unless given) */
  explicit GameClock(std::function<double()> realNow = nullptr);

  /** The moment of the world, in milliseconds since 1970. */
  double ms() const;

  /** The moment of the world. */
  double now() const { return ms(); }

  /** Is the world at the real time, at the real speed? */
  bool real() const { return !own.has_value(); }

  /** Has the player set another day or hour? */
  bool travelled() const { return chosen; }

  /** How many times faster than real the time runs. */
  double rate() const { return speed; }

  /**
   * Time passes in the world: `dt` seconds of it (what the game simulated –
   * at a higher rate that many more; a paused game stands still, unless it
   * is at the real time).  The real clock needs nothing.
   */
  void tick(double dt);

  /** Set the world to another moment (from where it runs on). */
  void set(double ms);

  /**
   * Let the time run faster (`rate` > 1) or as real again (1).  Run faster,
   * the world has a time of its own from here on; back at 1 after only that,
   * it is at the real time again (the timetables catch up) – after the player
   * had set another moment, it stays at the world's own time.
   */
  void setRate(double rate);

  /** Back to the real time, at the real speed. */
  void reset();

 private:
  /** the game's own time (ms since 1970) – null: the real time */
  std::optional<double> own;
  /** set by the player (another day or hour) – not only run faster */
  bool chosen = false;
  /** how many times faster than real the time runs (1: as real) */
  double speed = 1;
  std::function<double()> realNow;
};

/** the fastest the time may run (and the game still keep up) */
inline constexpr double MAX_RATE = 8;
/** the speeds offered while the autopilot drives */
inline const std::vector<double> RATES = {1, 2, 4, 8};

/** The game's one clock. */
inline GameClock gameClock;

/** The moment of the world (the real time, unless the arcade mode set another one). */
inline double gameNow() { return gameClock.now(); }

/** Where the sun stands: its elevation above the horizon and its compass azimuth (degrees). */
struct SunPlace {
  double elevation = 0;
  double azimuth = 0;
};

/**
 * Where the sun stands over the map (northern Hesse, 51.3° N 9.45° E) at a
 * moment: its elevation above the horizon and its compass azimuth (degrees,
 * 0 = north, 90 = east, 180 = south).  The usual approximation of the
 * astronomical almanac – a fraction of a degree, more than enough for light
 * and shadows.
 */
SunPlace sunAt(double ms, double latDeg = 51.3, double lonDeg = 9.45);

/** The season of a moment in the north (by the months: winter Dec–Feb, spring Mar–May, …): 'winter' | 'spring' | 'summer' | 'autumn' */
using Season = std::string;
/** (`date`: the moment in milliseconds since 1970; its month is the device's local one, as the original's getMonth()) */
Season seasonOf(double date);

}  // namespace fs
