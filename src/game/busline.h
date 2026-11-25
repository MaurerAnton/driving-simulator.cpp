// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Driving a bus on a line: calling at the stops on the way.  At each one
 * the bus comes to stand with its front door at the pole, the doors are
 * opened, people get off and on, the doors are closed once everybody is
 * through – and the bus pulls out with the indicator on (§ 10 StVO; the
 * others have to let it, § 20 Abs. 5 StVO).
 *
 * What the instructor says: a stop driven past, the doors opened where there
 * is no stop, driving with the doors open, closing them on people still
 * getting on, pulling out without indicating.
 *
 * Origin: src/game/busline.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs {

/** A stop on the lane the bus is in: where its front stands to serve it (metres along the lane), its number and name. */
struct BusStopAhead {
  int id = 0;
  double s = 0;
  std::string name;
};

/** 'busStopMissed' | 'busStopPlace' | 'busDoorsMoving' | 'busDoorsEarly' | 'busPullOut' */
using BusFault = std::string;

struct BusEvent {
  BusFault fault;
  /** 'hint' | 'minor' | 'major' | 'fail' */
  std::string severity;
};

/** how far before or past the place where its front should stand the bus may stop (m) */
inline constexpr double BUS_STOP_BEFORE = 4;
inline constexpr double BUS_STOP_PAST = 3;

class BusService {
 public:
  explicit BusService(std::function<double()> random = nullptr);

  /** stops served (stood there with the doors open until everybody was through) */
  int served = 0;
  bool doorsOpen = false;
  int boarding = 0;
  int alighting = 0;

  int passengers() const { return boarding + alighting; }
  /** The next stop on the lane not done with yet (null: none ahead on it). */
  const BusStopAhead* next(double front, const std::vector<BusStopAhead>& stops) const;
  /** Is the bus standing where it serves a stop that is not done with? */
  const BusStopAhead* atStop(double front, const std::vector<BusStopAhead>& stops, double speed) const;
  /** Open or close the doors (the door key) – only standing. */
  void toggleDoors(double front, const std::vector<BusStopAhead>& stops, double speed, std::vector<BusEvent>& events);
  /**
   * One step: `front` metres along the lane the bus is in (`stops` its stops),
   * its speed, the indicator (−1 left).  What the instructor has to say is
   * added to `events`.
   */
  void step(double dt, double front, const std::vector<BusStopAhead>& stops, double speed, int indicator, std::vector<BusEvent>& events);

 private:
  double nextPassenger_ = 0;
  /** the stop the doors were opened at (−1: none) */
  int at_ = -1;
  /** stops done with: served or driven past */
  std::unordered_set<int> done_;
  /** leaving a stop: how far it has gone since, and whether it indicated to pull out */
  struct Leaving {
    double from = 0;
    bool indicated = false;
  };
  std::optional<Leaving> leaving_;
  bool movingOpen_ = false;
  std::function<double()> random_;
};

}  // namespace fs
