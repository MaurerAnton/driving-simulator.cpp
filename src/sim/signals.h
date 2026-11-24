// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/signals.ts @ v1.7.7
/**
 * Traffic-light controllers: fixed-time programmes with the German sequence
 * green → amber → red → red+amber → green.
 *
 * Three things beyond that:
 *
 *  - The light of a pedestrian crossing (one that belongs to no junction)
 *    waits on green for the vehicles until somebody presses the button at
 *    its pole ("Signal kommt"): then, after a least green for the vehicles,
 *    the programme runs once through the pedestrians' time and comes back to
 *    rest.  The people of the traffic press it as well.
 *
 *  - Lights that are out of order.  A few controllers – other ones every
 *    day – show nothing but a flashing amber.  Their junctions are ruled by
 *    the signs that stand there, or by right before left (the world is told
 *    which ones when it is put together, see `World.lightsOut`).
 *
 *  - The green arrow: beside the light of some approaches hangs a second
 *    one with an arrow to the right.  It is lit while everybody whose way
 *    crosses that of the vehicles turning right has red: then turning right
 *    is allowed although the main light is red (§ 37 Abs. 2 Nr. 1 StVO).
 *    Which approaches have one is worked out from the paths through the
 *    junction (sim/arrows).
 *
 *  - How long a light still shows what it shows (`change`, for the small
 *    timers over the heads: ui/signaltimers) – and, on a tap on such a
 *    timer, the programme put forward to the end of the phase that runs
 *    (`skip`).  Only a main phase is cut short, never an amber, a red-amber
 *    or the moment of red for all between them, and none to less than a few
 *    seconds: the light goes on through its sequence as always, and whoever
 *    has just got green still gets going.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "sim/insertion_map.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

/** FLASH: out of order, flashing amber – no light at all as far as the rules go */
enum Sig : int { SIG_RED = 0, SIG_RED_AMBER = 1, SIG_GREEN = 2, SIG_AMBER = 3, SIG_FLASH = 4 };

/** the vehicles have had red this long when the pedestrians get green (s) */
inline constexpr double PED_CLEAR = 1.5;

/** a phase cut short by a tap on its timer still runs this long from its start (s): the first car gets going, people on foot get across */
inline constexpr double SKIP_LEAST = 6;

/** What a light is going to do: in how many seconds, and what it shows then (SIG; −1: dark, for an arrow). */
struct SignalChange {
  double left = 0;
  int next = 0;
};

/**
 * Which controllers are out of order: a `share` of them, picked by their
 * number and `seed` (the game gives the day, so that other lights are out
 * tomorrow).
 */
std::function<bool(int controller)> lightsOutBy(double seed, double share);

/** The green arrow of an approach. */
struct Arrow {
  /** the controller, and the group of the main light it hangs beside */
  int controller = 0;
  int own = 0;
  /** the groups that must have red while it is lit (those whose vehicles cross the way of the ones turning right) */
  std::vector<int> foes;
  /** the groups that must have green while it is lit (the people at a crossing the turn leads over have red then) */
  std::vector<int> friends;
  /** dark (-1), SIG.GREEN or SIG.AMBER */
  int state = -1;
  /** seconds in that state */
  double since = 0;
};

/** What the controllers around a place showed at a moment (SignalSystem.snapshot) – for a replay to show it again. */
struct SignalSnapshot {
  double time = 0;
  std::vector<int32_t> ids;
  /** per controller: how many groups (the states and ages follow in that order), its own clock, its request */
  std::vector<uint8_t> groups;
  std::vector<float> local;
  std::vector<uint8_t> asked;
  std::vector<uint8_t> states;
  std::vector<float> ages;
  std::vector<std::tuple<const Arm*, int, double>> arrows;
};

class SignalSystem {
 public:
  /** by their number in the map – with gaps where the map is not loaded */
  const std::vector<SignalControllerJson>& controllers;
  /** the green arrows, by the approach they belong to */
  InsertionMap<const Arm*, Arrow> arrows;
  double time = 0;

  /**
   * `present` says which numbers of the list really are there (World::signalsPresent – the
   * original's list simply has gaps); without it every one is.
   */
  explicit SignalSystem(const std::vector<SignalControllerJson>& controllers, std::function<bool(int controller)> lightsOut = nullptr,
                        const std::vector<char>* present = nullptr);

  /**
   * Take up the controllers that were added to the list since (the road
   * network comes in pieces).  Each starts where its programme stands by the
   * clock, so that a junction shows the same lights whenever it is loaded.
   */
  void sync();

  void update(double dt);

  /** What a group shows; SIG.FLASH if its controller is out of order. */
  int get(int controller, int group) const;

  /** Seconds the group has been showing its current state. */
  double age(int controller, int group) const;

  /** Is this controller out of order? */
  bool isDark(int controller) const;

  /**
   * The button at a pedestrian light is pressed.  A pedestrian crossing's
   * light then soon gives the pedestrians green; at a junction the light runs
   * by its programme all the same – the lamp on the box says the request was
   * taken either way.  Returns whether it was new.
   */
  bool request(int controller);

  /**
   * The state of the controllers within `radius` of (x, z): what each group
   * shows and for how long, the crossings' own clocks and requests, the
   * arrows.  `restore` puts it back (a replay; the live state around it).
   */
  SignalSnapshot snapshot(double x, double z, double radius) const;

  /** Put back what `snapshot` took (controllers no longer loaded are left out). */
  void restore(const SignalSnapshot& s);

  /** Is a request waiting at this controller ("Signal kommt")? */
  bool requested(int controller) const;

  /**
   * What the pedestrians see at a crossing whose vehicles have group `g`:
   * green while those vehicles have had red for a moment, red otherwise –
   * dark when the light is out of order.  (A request is taken back once the
   * pedestrians have green: the lamp "Signal kommt" goes out.)
   * 'green' | 'red' | 'dark'
   */
  std::string pedestrian(int controller, int group);

  /**
   * What the green arrow of an approach shows: SIG.GREEN (those who turn
   * right may go although the main light is red), SIG.AMBER (its time is
   * over) or -1 (dark – or there is none).
   */
  int arrow(const Arm* arm) const;

  /**
   * How many seconds of a cycle an arrow with these groups would be lit
   * (to see whether it is worth putting one up).
   */
  double arrowTime(int controller, int own, const std::vector<int>& foes, const std::vector<int>& friends) const;

  /**
   * When group `group` of a controller changes next, and to what.  Infinity
   * while a pedestrian crossing's light rests on the vehicles' green with
   * nobody asking (it waits for the button).  Null where there is nothing to
   * count: no such controller running, or it is out of order.
   */
  std::optional<SignalChange> change(int controller, int group) const;

  /**
   * When the pedestrians' light at a crossing whose vehicles have group
   * `group` changes next: `next` is SIG.GREEN or SIG.RED – of the people on
   * foot.  Null where the light is out of order.
   */
  std::optional<SignalChange> pedestrianChange(int controller, int group) const;

  /**
   * When the green arrow of an approach changes next: `next` is SIG.GREEN,
   * SIG.AMBER or −1 (dark).  Null where there is none, or its light is out of
   * order.  (Looked for in the programme a quarter of a second at a time –
   * once, and again after the arrow changed or a programme was put forward.)
   */
  std::optional<SignalChange> arrowChange(const Arm* arm);

  /**
   * A tap on a timer: the controller goes on to the end of the main phase
   * that runs – at once, or once that phase has run SKIP_LEAST seconds – and
   * from there through amber, the moment of red for all and red-amber as its
   * programme has them.  A pedestrian crossing's light at rest is asked as by
   * its button (served after the least green for the vehicles).  An amber, a
   * red-amber or a clearance phase runs as it is.  Returns whether anything
   * was put forward.
   */
  bool skip(int controller);

 private:
  struct ArrowNext {
    int state = 0;
    double at = 0;
    int next = 0;
    int epoch = 0;
  };
  struct PhaseAt {
    int k = 0;
    double into = 0;
    double left = 0;
  };

  const std::vector<char>* present;
  /** the numbers of the controllers that are running */
  std::vector<int> ids;
  std::vector<double> cycle;
  /** which numbers have a state (the original's lists have gaps) */
  std::vector<char> running;
  /** current state per controller and group */
  std::vector<std::vector<uint8_t>> state;
  /** seconds since the current state of a group began */
  std::vector<std::vector<float>> since;
  /** out of order */
  std::vector<char> dark;
  /**
   * A pedestrian crossing's own clock in its programme (it stands still at
   * rest) – for the other controllers, how far a tap on a timer has put their
   * programme forward (`skip`) –, and whether the button was pressed.
   */
  std::vector<double> local;
  std::vector<char> asked;
  std::function<bool(int controller)> lightsOut;
  /** when each green arrow is going to change next (worked out once, again after it changed or a programme was put forward) */
  std::unordered_map<const Arm*, ArrowNext> arrowNext;
  /** counts every skip and restore: what was worked out ahead before is out of date */
  int epoch = 0;

  /** is there a controller of this number in the list, and is it running (has it a state)? */
  bool listed(int i) const;
  bool runs(int i) const;
  /** What group `g` of a controller shows `ahead` seconds from now (by its programme). */
  int programmed(int i, int g, double ahead) const;
  /** Should the arrow be lit now? */
  bool arrowLit(const Arrow& a) const;
  /** Where a controller's programme stands: the phase (its index), how long it has run, how long it has left. */
  PhaseAt phaseAt(int i) const;
  /** What group `g` shows in phase `k` of controller `i`'s programme. */
  int codeOf(int i, int k, int g) const;
  /** In how many seconds group `group` begins to show `want` (0: it shows it now) – Infinity where it does not before a crossing's light rests. */
  double until(int i, int group, int want) const;
};

}  // namespace fs
