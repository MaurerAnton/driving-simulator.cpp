// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Replays: a drive recorded as it happens and shown again 1:1 – the player's
 * vehicle (or the person on foot, the tram, the aeroplane), the traffic round
 * it, the trams and trains, the traffic lights and the instructor's notes on
 * a timeline – so that one can look at a junction or a stop again from any
 * side, slowed down.
 *
 * What is recorded is what the renderers draw, not what the simulation
 * thinks: positions, turns, lamps, the dashboard.  The player's vehicle ten
 * times a second, the others five times (in between they are interpolated),
 * the lights twice (the controllers within 600 m).  The last half hour is
 * kept: some 10–20 MB.
 *
 * Origin: src/game/replay.ts @ v1.7.7
 *
 * (The parts of a frame are shared, not copied: a part a frame does not have
 * is null – "as in the last frame that has it".  What a replay draws are the
 * renderers' own kinds – TrafficCar, Walker, Cyclist, RailVehicle – with the
 * fields they read filled in, as the original hands its objects to them.)
 */
#pragma once

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "sim/hooks.h"
#include "sim/pedestrians.h"
#include "sim/rail.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"

namespace fs {

struct CarVisualState;
struct PlaneVisualState;

/** How often the parts of a drive are written down (s), and how much of it is kept. */
inline constexpr double REPLAY_STEP = 0.1;
inline constexpr double REPLAY_OTHERS_STEP = 0.2;
inline constexpr double REPLAY_SIGNALS_STEP = 0.5;
inline constexpr double REPLAY_KEEP = 30 * 60;

/** The numbers per traffic car, person on foot, cyclist and rail car in a frame. */
inline constexpr int CAR_FIELDS = 13;
inline constexpr int PED_FIELDS = 8;
inline constexpr int CYCLIST_FIELDS = 9;
inline constexpr int RAIL_HEAD = 6;
inline constexpr int RAIL_CAR_FIELDS = 5;

/** a part of a frame (a Float32Array; null: none) */
using ReplayPart = std::shared_ptr<const std::vector<float>>;

/** One moment of a drive. */
struct ReplayFrame {
  double t = 0;
  /** the player's model: x, y, z and its turn as a quaternion */
  std::array<float, 7> pose{0, 0, 0, 0, 0, 0, 1};
  /** its visual state (lamps, wheels, the dashboard), packed by a VisualPacker */
  ReplayPart look;
  /** a trailer: x, y, z, yaw, pitch, wheel turn, coupled */
  ReplayPart trailer;
  /** somebody on foot: x, y, z, yaw, metres walked, moving, riding (1) */
  ReplayPart walker;
  /** the traffic (CAR_FIELDS a car: id, style, x, y, z, yaw, pitch, wheel turn, indicator, braking, speed, colour, the bus line it runs or −1) – null: as in the last frame that has it */
  ReplayPart cars;
  /** people on foot (id, colour, x, y, z, yaw, crossing, step) */
  ReplayPart peds;
  /** cyclists (id, colour, x, y, z, yaw, pedal turn, lean, fallen) */
  ReplayPart cyclists;
  /** trams and trains: per vehicle id, kind, make-up (0 one unit, 1 two, 2 with trailer), line (−1 none), pantographs, cars – then x, y, z, yaw, pitch a car */
  ReplayPart rail;
  /** the lights around (null: as before) */
  std::shared_ptr<const SignalSnapshot> signals;
};

/** A note of the instructor, where on the timeline it fell. */
struct ReplayNote {
  double t = 0;
  std::string id;
  /** 'hint' | 'minor' | 'major' | 'fail' */
  std::string severity;
  ViolationData data;
};

/**
 * A field of an object a VisualPacker packs: a number, a flag, a short text
 * or an array of numbers – where it lives (TypeScript reads them off the
 * object by name; here the object names them: visualFields, planeFields).
 */
using PackedRef = std::variant<double*, int*, bool*, std::string*, std::vector<double>*>;
using PackedFields = std::vector<std::pair<std::string, PackedRef>>;

/** The fields of the vehicle's visual state a replay keeps (in the order the object has them). */
PackedFields visualFields(CarVisualState& s);
/** The aeroplane's: its numbers and flags (the wheels, the yoke and the lights are objects: not kept, as in the original). */
PackedFields planeFields(PlaneVisualState& s);

/**
 * Packs an object of numbers, flags, short texts and arrays of numbers (a
 * visual state) into a Float32Array and back – the fields in the order the
 * first object had them.  Texts go into a table of their own.
 */
class VisualPacker {
 public:
  std::vector<float> pack(const PackedFields& o);
  /** Writes a packed state back into `o` (an object of the same kind). */
  void unpack(const std::vector<float>& p, const PackedFields& o) const;

 private:
  std::optional<std::vector<std::string>> keys_;
  std::vector<int> sizes_;
  /** 'n' | 'b' | 's' | 'a' */
  std::vector<char> kinds_;
  int size_ = 0;
  std::vector<std::string> texts_;
};

/** Writes a drive down as it goes. */
class Recorder {
 public:
  std::deque<ReplayFrame> frames;
  std::vector<ReplayNote> notes;
  /** what the vehicle was (the game's vehicle kind) – a replay shows it with that model */
  std::string vehicle;
  VisualPacker look;
  VisualPacker planeLook;

  /** A new drive: nothing recorded yet. */
  void clear(const std::string& vehicle);

  /** Which of the parts that are written less often are due with a frame. */
  struct Due {
    bool others = false;
    bool signals = false;
  };
  /** Whether a frame is due at time t (and with it which of the others). */
  std::optional<Due> due(double t);

  void push(ReplayFrame f);
  void note(ReplayNote n);

  double start() const;
  double end() const;

 private:
  double nextAt_ = 0;
  double nextOthers_ = 0;
  double nextSignals_ = 0;
};

/** A car in a replay: what the renderer reads of a car of the traffic – and the bus line it ran (its place in the traffic's bus lines; −1 none). */
struct ReplayCar : TrafficCar {
  int line = -1;
};

/** A tram or train in a replay: what the renderer reads of one – and its make-up. */
struct ReplayRail : RailVehicle {
  /** 'single' | 'double' | 'trailer' */
  std::string set = "single";
};

/**
 * Plays a recording: where the clock of the replay stands, and what is to be
 * drawn there – interpolated between the two frames round it.
 */
class ReplayView {
 public:
  const Recorder& rec;
  double t;
  double speed = 1;
  bool playing = true;
  std::vector<ReplayCar*> carList;
  std::vector<Pedestrian*> pedList;
  std::vector<Cyclist*> cyclistList;
  std::vector<ReplayRail*> railList;
  /** the player's model now: x, y, z, qx, qy, qz, qw */
  std::array<float, 7> pose{0, 0, 0, 0, 0, 0, 1};

  /** (`from`: none – the start) */
  explicit ReplayView(const Recorder& rec, std::optional<double> from = std::nullopt);

  double start() const { return rec.start(); }
  double end() const { return rec.end(); }

  /** On by dt seconds of the drive's time (at the speed chosen); stops at the end. */
  void advance(double dt);
  void seek(double t);

  /** The frame before the clock and the one after it, and how far between them it stands. */
  struct Between {
    const ReplayFrame* a = nullptr;
    const ReplayFrame* b = nullptr;
    double u = 0;
  };
  std::optional<Between> frame();

  /** The lights as they were at the clock. */
  std::shared_ptr<const SignalSnapshot> signals() const;

  /** The traffic at the clock, interpolated: fills carList, pedList, cyclistList. */
  void traffic();
  /** The trams and trains at the clock, interpolated: fills railList. */
  void trains();

 private:
  std::unordered_map<int64_t, std::unique_ptr<ReplayCar>> cars_;
  std::unordered_map<int, std::unique_ptr<Walker>> peds_;
  std::unordered_map<int, std::unique_ptr<Cyclist>> cyclists_;
  std::unordered_map<int, std::unique_ptr<ReplayRail>> rail_;

  /** The index of the last frame at or before t. */
  size_t index(double t) const;
  /** The newest frame at or before the clock that has the part `key` (-1: none). */
  template <class P>
  int latest(P ReplayFrame::* key) const;
  /** The one after frame i that has the part `key` (to move towards; null: none). */
  const ReplayFrame* following(ReplayPart ReplayFrame::* key, int i) const;
};

}  // namespace fs
