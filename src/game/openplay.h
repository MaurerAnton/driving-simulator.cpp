// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The parts that open and the seats of the car, in the game: what the game
 * (game/game.ts) asks of them through a few short hooks.
 *
 * On foot beside the vehicle one opens and shuts what is where one stands
 * – a door of the car, its bonnet or tailgate, the rear doors of a truck's
 * box, the back of a trailer – with the key . (full stop) or a button, and at a door
 * of the car one gets in with G or "Get in": into the seat of that door,
 * the driver's or a passenger's.  Getting in or out of the car is no fade
 * to black any more: the door opens, one moves between the street and the
 * seat, the door is pulled shut (the other vehicles keep their fade).
 *
 * In a passenger's seat the car is not driven: what the player's keys,
 * pedals and buttons would do to it is held off, the instructor's rules do
 * not judge, the HUD says where one sits.  The demonstration drive drives
 * on with the player beside the empty driver's seat or in the back –
 * watching it is what the passenger's seat is for.
 *
 * Origin: src/game/openplay.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/cardoors.h"
#include "game/onfoot.h"
#include "platform/dom.h"
#include "ui/openbuttons.h"

namespace fs {

class PlayerCar;
struct Controls;
struct DriverInput;

/** What can be done where the person stands: the tip line, the key it names, and the buttons. */
struct WalkHud {
  std::string tip;
  std::string action;
  std::vector<OpenButton> buttons;
};

/** the key that opens and shuts (game: input action 'open') */
inline const std::string OPEN_KEY = "Period";

class OpenPlay {
 public:
  Openings openings;
  /** getting into a seat of the car or out of it, step by step (null: none) */
  std::unique_ptr<Boarding> board;
  SeatId seat = "driver";

  bool passenger() const { return seat != "driver"; }
  bool busy() const { return board != nullptr; }

  /** The camera on its way between the street and the seat, and how far it has come (0 … 1). */
  bool blending() const { return board && board->phase == "move"; }
  double blend() const { return board ? board->blend() : 1; }

  /** A new drive (or another vehicle): everything shut, in the driver's seat. */
  void reset(PlayerCar* car);

  /** Sit in this seat: the car's eye point moves there. */
  void setSeat(PlayerCar* car, const SeatId& seat);

  /**
   * Somebody is on foot beside the vehicle (its parts as `wv` has them):
   * where its parts are opened – and, getting out of the car, the person
   * stands beside the door that was used.
   */
  void walk(OnFoot& w, const WalkVehicle& wv, PlayerCar* car, bool truck);

  /** What is where the person stands (null: nothing to open here). */
  std::optional<OpeningSpot> at(const OnFoot* w) const;

  /** Open or shut what is there; returns what it was (null: nothing). */
  std::optional<OpeningId> toggleAt(const OnFoot* w);

  /** Into the seat of this door of the car. */
  void beginIn(const DoorId& door);

  /** Out of the car, through the door of the seat one sits in. */
  void beginOut();

  /**
   * One step: the parts move on, getting in or out goes on.  `click` gets
   * the sounds (a door opened, a door shut: 'door' | 'doorShut').
   */
  BoardStep step(double dt, const std::function<void(const std::string& kind)>& click);

  /**
   * Show the parts as far open as they are: the car's.
   * (later: a truck's box and a trailer's back – render/rearopen.ts, render/truck.ts)
   */
  void pose(PlayerCar* car);

  /**
   * In a passenger's seat: the car does nothing the player's controls say –
   * it stands as it was left (or the demonstration drive drives it, then
   * only the hands on the controls are held off, so as not to take it over).
   */
  void idle(Controls& c, DriverInput& d, bool demo, double parkedSteer);

  /** The buttons near the person on foot (or none), and what a tap on them does. */
  void buttons(const dom::El& parent, const std::vector<OpenButton>& list, const std::function<void(const std::string& id)>& onTap);

  /** What can be done where the person stands: the tip line, and the buttons. */
  std::optional<WalkHud> walkHud(const OnFoot* w, bool touch, const std::string& lang, bool canEnter) const;

  /** In a passenger's seat: where one sits, and how to get out (when the car stands). */
  std::string seatTip(bool touch, const std::string& lang, bool canGetOut) const;

 private:
  std::string label(const OpeningId& id, bool open, const std::string& lang) const;

  std::vector<OpeningSpot> spots_;
  /** the person on foot the spots were found for (another – after a crash, out of the aeroplane – has none) */
  const OnFoot* spotsFor_ = nullptr;
  std::vector<double> carValues_ = std::vector<double>(CAR_OPENINGS.size(), 0.0);
  std::vector<OpeningEvent> events_;
  struct Held {
    int gear;
    bool ignition;
  };
  std::optional<Held> held_;
  std::unique_ptr<OpenButtons> ui_;
};

}  // namespace fs
