// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/beams.ts @ v1.7.7
/**
 * Headlights and the main beam as the rules see them (§ 17 StVO).
 *
 * In the dark (and in thick fog) a vehicle drives with its lights on
 * (§ 17 Abs. 1, Abs. 3).  The main beam lights the road far ahead, and it
 * dazzles: it must be dipped in time when somebody comes towards one – a
 * car about 300 m off on the open road, earlier where the road runs
 * straight –, when one follows another vehicle closely (its driver sees the
 * beam in his mirrors), for cyclists and people on foot coming towards one,
 * and on streets that are lit throughout, as in a town, it is not used at
 * all (§ 17 Abs. 2).
 *
 * `BeamWatch` watches the player: what to tell him (a tip, before it is a
 * fault), the faults once he dazzles somebody, and – for the optional
 * main-beam assist – whether the main beam could be on now.
 * `TrafficBeams` gives the cars of the traffic their own main beam: on on
 * the empty country road at night, dipped when anybody comes into its reach
 * (a few drivers dip late), and flashed back twice at a player who dazzles
 * them.  It also says how much the player is dazzled by such a beam (`glare`).
 *
 * Nothing here draws: render/headlights.h does, game/beamplay.h feeds it.
 * Directions as everywhere in the game: yaw 0 faces north (−z), a positive
 * yaw turns left, forward is (−sin yaw, −cos yaw).
 */
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace fs {

/** Somebody on the road: a car, a cyclist, a person on foot. */
struct BeamUser {
  int64_t id = 0;
  double x = 0;
  double z = 0;
  double yaw = 0;
};

/** can one see from (x0, z0) to (x1, z1) – no hill crest between?  (empty: yes) */
using BeamSees = std::function<bool(double x0, double z0, double x1, double z1)>;

/** What the watch needs to know about the player and around him. */
struct BeamScene {
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** signed forward speed (m/s) */
  double speed = 0;
  /** the lights (dipped beam) are on */
  bool low = false;
  /** the main beam is lit (switched on, or the flasher held) */
  bool high = false;
  /** night: the main beam matters, and lights are required */
  bool dark = false;
  /** lights required although it is not night (thick fog, heavy snow) */
  bool poorSight = false;
  /** the street is lit throughout (in a town, or marked as lit): no main beam here */
  bool lit = false;
  /** the cars of the traffic */
  std::vector<BeamUser> cars;
  /** cyclists and people on foot */
  std::vector<BeamUser> riders;
  BeamSees sees;
};

/** 'beam.tipOncoming' | 'beam.tipAhead' | 'beam.tipRider' | 'beam.tipLit' | 'beam.tipLightsOn' ('': none) */
using BeamTip = std::string;
/** 'dazzleOncoming' | 'dazzleAhead' | 'dazzleRider' | 'beamLit' | 'noLights' */
using BeamFaultId = std::string;

struct BeamFault {
  BeamFaultId id;
  /** 'hint' | 'minor' | 'major' */
  std::string severity;
  /** seconds before the same fault is counted again */
  double cooldown = 0;
};

/** Distances (m) and how long (s) a state may last before it is a fault. */
struct BeamConstants {
  /** an oncoming vehicle this near (in the beam, in sight): dip now (the tip) */
  double ONCOMING_TIP = 400;
  /** … this near with the main beam still on: it is dazzled */
  double ONCOMING_FAULT = 200;
  /** … and flashes back */
  double ONCOMING_FLASH = 250;
  /** following another vehicle this closely: dip (the tip) … */
  double AHEAD_TIP = 150;
  /** … this closely for a while: its driver is dazzled by his mirrors */
  double AHEAD_FAULT = 80;
  /** a cyclist or somebody on foot coming towards one */
  double RIDER_TIP = 200;
  double RIDER_FAULT = 100;
  /** reaction time allowed once dazzling begins */
  double GRACE_ONCOMING = 0.8;
  double GRACE_AHEAD = 3;
  double GRACE_RIDER = 1;
  double GRACE_LIT = 5;
  /** driving with the lights off in the dark this long */
  double GRACE_DARK = 4;
  /** the assist puts the main beam on only above this speed (m/s): about 40 km/h */
  double ASSIST_SPEED = 11;
};
inline constexpr BeamConstants BEAM{};

struct Relation {
  double along = 0;
  double side = 0;
  double facing = 0;
};

/** Where `o` is seen from (x, z, yaw): metres ahead, metres to the left, and how its way lies to ours (1 the same, −1 towards us). */
Relation& relate(double x, double z, double yaw, const BeamUser& o, Relation& out);
inline Relation relate(double x, double z, double yaw, const BeamUser& o) {
  Relation r;
  return relate(x, z, yaw, o, r);
}

/** Is `r` (relate's answer) a vehicle coming towards one, inside the reach of a main beam `range` m long? */
bool inOncomingBeam(const Relation& r, double range);
/** … a vehicle going the same way in front, `range` m at most? */
bool inBeamAhead(const Relation& r, double range);

/** How dark it is for the lamps, 0 day … 1 night, by the sun's elevation (degrees). */
double darkness(double sunElevation);

/** Below this elevation of the sun (degrees) lights are required and the main beam counts. */
inline constexpr double DARK_SUN = -2;
/** Below this the tip to switch the lights on is given (dusk). */
inline constexpr double DUSK_SUN = 1;

/** The player's lights, watched by the rules of § 17 StVO. */
class BeamWatch {
 public:
  /** what the driver should do now (a text key) – '': nothing */
  BeamTip tip;
  /** the traffic car the main beam dazzles now (−1: none): it flashes back */
  int64_t dazzling = -1;
  /** the main-beam assist would have the main beam on now */
  bool assistHigh = false;
  /** the nearest vehicle coming towards one in the beam's reach (m; Infinity: none) */
  double oncoming = std::numeric_limits<double>::infinity();

  void reset();
  /** One step of `dt` seconds: the faults found in it (none most of the time). */
  std::vector<BeamFault> update(double dt, const BeamScene& s);

 private:
  double tOncoming_ = 0;
  double tAhead_ = 0;
  double tRider_ = 0;
  double tLit_ = 0;
  double tDark_ = 0;
  Relation r_;
};

/** A car of the traffic, as its own lights see it. */
struct TrafficBeamCar : BeamUser {
  /** speed (m/s) */
  double v = 0;
  /** on a road outside towns, not lit: the main beam may be used */
  bool rural = false;
};

/** The player as the traffic's lights see him, with his main beam. */
struct BeamPlayer : BeamUser {
  bool high = false;
};

/** The main beam of the cars of the traffic (see the top of the file). */
class TrafficBeams {
 public:
  /** how much the player is dazzled by a main beam coming towards him, 0 … 1 */
  double glare = 0;

  void reset();
  /** Is the main beam of car `id` lit now (on, or flashing)? */
  bool high(int64_t id) const;
  /** Is car `id` flashing its main beam now? */
  bool flashing(int64_t id) const;
  /** How far a car dips its beam for somebody coming towards it (m): most in good time, every eighth late. */
  static double dipDistance(int64_t id);
  /**
   * One step: the cars near the player (`cars`), the player (null: none on the
   * road), the car the player dazzles (BeamWatch.dazzling), is it night.
   */
  void update(double dt, const std::vector<TrafficBeamCar>& cars, const BeamPlayer* player, int64_t dazzling, bool dark, const BeamSees& sees = nullptr);

 private:
  struct CarBeam {
    bool high = false;
    /** since when nothing has been in its reach (s of the clock) */
    double clearSince = 0;
    /** when its last flash began (−∞: never) */
    double flash = -std::numeric_limits<double>::infinity();
    /** how long the player has been dazzling it */
    double dazzled = 0;
    int seen = 0;
  };
  /** (a Map in the original: its order of insertion is not looked at) */
  std::map<int64_t, CarBeam> m_;
  double time_ = 0;
  double nextTick_ = 0;
  int stamp_ = 0;
  Relation r_;
};

}  // namespace fs
