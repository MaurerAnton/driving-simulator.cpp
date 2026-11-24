// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The parts of a vehicle that open, and the seats of the car – the logic,
 * without anything drawn (render/caropen.ts and render/rearopen.ts move
 * the parts, game/openplay.ts puts it into the game).
 *
 * The car has six: its four side doors, the bonnet and the tailgate; a
 * truck's box body its two rear wing doors; a trailer its tailgate (the
 * car's box trailer) or its rear doors (the truck's).  Each is open by a
 * number from 0 (shut) to 1 (wide open) that moves towards where it is
 * wanted at the pace that part swings.
 *
 * Getting in or out of the car (Boarding) is three steps: the door of that
 * seat opens (unless it stands open already), one moves between the place
 * beside it and the seat, the door is pulled shut.  Each door belongs to
 * a seat: the front left one to the driver's, the front right one to the
 * front passenger's, the rear ones to the rear seats behind them.
 *
 * Origin: src/game/cardoors.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fs {

/** 'fl' | 'fr' | 'rl' | 'rr' */
using DoorId = std::string;
/** DoorId | 'bonnet' | 'boot' | 'body' | 'trailer' */
using OpeningId = std::string;
/** 'driver' | 'front' | 'rearLeft' | 'rearRight' */
using SeatId = std::string;

inline const std::vector<DoorId> DOORS = {"fl", "fr", "rl", "rr"};
/** the car's parts in the order render/caropen.ts builds them */
inline const std::vector<OpeningId> CAR_OPENINGS = {"fl", "fr", "rl", "rr", "bonnet", "boot"};

bool isDoor(const OpeningId& id);

/** The seat a door leads to. */
SeatId seatOfDoor(const DoorId& door);

/** The door beside a seat. */
DoorId doorOfSeat(const SeatId& seat);

/** Where the eyes are in each seat of the car (car frame: x right, y up, −z ahead). */
std::array<double, 3> seatEye(const SeatId& seat);

/** What happened to a part in a step: it began to open, or it came to be shut. */
struct OpeningEvent {
  OpeningId id;
  /** 'opening' | 'shut' */
  std::string what;
};

/** How far each part is open, and where it is going. */
class Openings {
 public:
  /** How far it is open now: 0 shut … 1 wide open. */
  double get(const OpeningId& id) const;

  /** Whether it is being opened (or stands open) – rather than being shut (or shut). */
  bool wanted(const OpeningId& id) const;

  void set(const OpeningId& id, bool open);

  /** Open it if it is shut or being shut, shut it otherwise; returns whether it is now to be open. */
  bool toggle(const OpeningId& id);

  /** Anything not quite shut? */
  bool anyOpen() const;

  /** Move every part on by `dt` seconds; `events` gets what began to open and what came to be shut. */
  std::vector<OpeningEvent>& step(double dt, std::vector<OpeningEvent>& events);

  /** Everything shut at once (a new drive). */
  void reset();

 private:
  // (Maps: in the order the parts were first named)
  std::vector<std::pair<OpeningId, double>> value_;
  std::vector<std::pair<OpeningId, double>> target_;
};

/** A place on the ground where one stands to open something (world x, z), and what. */
struct OpeningSpot {
  OpeningId id;
  double x = 0;
  double z = 0;
};

/** A part of the vehicle in the plan: as game/onfoot.ts WalkUnit has it. */
struct SpotUnit {
  double x = 0;
  double z = 0;
  double yaw = 0;
  double front = 0;
  double rear = 0;
  double hw = 0;
};

struct CarSpotEdges {
  double frontAhead = 0;
  double rearDoorRear = 0;
  double nose = 0;
  double tail = 0;
};

/**
 * Where one stands for each of the car's parts: beside each door (the front
 * ones where the driver stands to get in, `frontAhead` metres ahead of the
 * origin; the rear ones a little ahead of their rear edge, clear of where
 * they swing), in front of the bonnet, behind the tailgate.  `edges`: z
 * (car frame, −z ahead) of the rear doors' rear edge, of the nose and of
 * the tail.
 */
std::vector<OpeningSpot> carSpots(const SpotUnit& u, const CarSpotEdges& edges);

/** The spot behind a unit (its rear doors, its tailgate). */
OpeningSpot rearSpot(const OpeningId& id, const SpotUnit& u);

/** The spot nearest to (x, z) within `reach` metres, if any. */
std::optional<OpeningSpot> nearestSpot(const std::vector<OpeningSpot>& spots, double x, double z, double reach = 1.2);

/** What one can do standing at a part: open or shut it – and at a door of the car, get in there ('open' | 'close' | 'enter'). */
using FootAct = std::string;

std::vector<FootAct> footActs(const OpeningId& id, const Openings& openings, bool canEnter);

/** 'open' | 'move' | 'close' | 'done' */
using BoardPhase = std::string;

/** seconds from beside the door into the seat, or out of it */
inline constexpr double MOVE_TIME = 0.85;

/** What a step of getting in or out brought. */
struct BoardStep {
  /** the move between seat and street began (in: one leaves the street; out: one is put beside the door) */
  bool start = false;
  /** the move ended (in: one sits) */
  bool arrive = false;
  /** the door is shut again: all done */
  bool done = false;
};

/**
 * Getting into a seat of the car or out of it: the door of that seat opens,
 * one moves, the door is shut.  Driven by the parts' numbers in `Openings`
 * (the door's swing sets the pace of the first and the last step).
 */
class Boarding {
 public:
  BoardPhase phase = "open";
  /** 'in' | 'out' */
  const std::string way;
  const DoorId door;
  const SeatId seat;

  Boarding(const std::string& way, const DoorId& door);

  /** How far the camera has come from where it was to where it goes: 0 … 1 (eased). */
  double blend() const;

  BoardStep step(double dt, Openings& o);

 private:
  /** seconds into the move */
  double t_ = 0;
};

}  // namespace fs
