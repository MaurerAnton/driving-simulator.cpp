// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Watching a tram or a bus of the timetables (chosen on the big map): the
 * camera goes with it while it runs on along its line, and the world – the
 * ground, the roads, the traffic – comes and goes round it, as it does round
 * the player.  The player's own vehicle waits where it was left.
 *
 * The tram is one of the trams of the traffic (src/sim/rail.ts), which are
 * put at their timetable places when their part of the map is loaded – the
 * trip has the same name there (game.ts railSchedule); if it does not come
 * (the traffic has enough trams about), it is put there here.  A bus is put
 * on its line's road by the traffic (src/sim/traffic.ts addCar) once the
 * roads there are loaded, and follows its line from there.  Until either is
 * there, the camera looks at where the timetable has it.
 *
 * Touching a driving control while watching asks whether the player wants to
 * take the vehicle over: yes – the player drives it from there on its line
 * (game.ts begins a drive of the tram at its stop, of the bus where it is,
 * shown the way of its line); no – back to the player's own vehicle.
 *
 * (transitSource and lineTakeOver, what the big map and the buses of the
 * timetables need of this file, are in game/transitsource.h.)
 *
 * Origin: src/game/spectate.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>

#include "game/transitsource.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "sim/playerstate.h"
#include "sim/timetable.h"
#include "ui/bigmap.h"
#include "ui/transitmap.h"

namespace fs {

class Camera;
struct Controls;
class LaneGraph;
class RailTraffic;
struct RailVehicle;
class Traffic;
struct TrafficCar;

/** What the watching needs of the game. */
struct WatchHost {
  /** the HUD (the bar about what is watched goes into it) and the whole interface (the question) */
  std::function<dom::El()> hud;
  dom::El ui;
  std::function<std::string()> lang;
  TransitSource* source = nullptr;
  std::function<RailTraffic&()> rail;
  std::function<Traffic&()> traffic;
  LaneGraph* lanes = nullptr;
  /** are the roads and the ground round a place loaded? */
  std::function<bool(double x, double z)> ready;
  /** the height of the ground (the far view's where the tiles are not loaded) */
  std::function<double(double x, double z)> ground;
  /** the player takes the vehicle over: a drive of it from here, on its line */
  std::function<void(const WatchRequest& w, const TakeOver& at)> takeOver;
  /** back to the player's own vehicle */
  std::function<void()> back;
};

/**
 * A bus taken over: the ways of its line from where it is to the end of its
 * route, as a way planned for the player to drive and be shown (game.ts
 * startTrip) – ending at its last stop.  Null where the ways are not known.
 */
std::optional<TourPreview> lineWay(TransitSource& src, const TakeOver& at);

/** Where something watched is (and which way it faces). */
struct WatchAt {
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
};

/**
 * What the game holds of something watched – a Watcher or a PointWatcher
 * (game.ts `watcher: Watcher | PointWatcher`): where it is, the "player" the
 * world comes and goes round, a step, the camera, the end.
 */
class WatchView {
 public:
  virtual ~WatchView() = default;
  /** where it is (and its height) */
  WatchAt at;
  /** the player that the traffic comes and goes round: where it is, high above it (nobody gives way to it) */
  PlayerState ghost{.x = 0, .y = 0, .z = 0, .yaw = 0, .speed = 0, .length = 0.2, .width = 0.2};
  /** Where the world is to load round. */
  std::array<double, 2> focus() const { return {at.x, at.z}; }
  virtual void step(double dt, const Controls& c) = 0;
  virtual void camera(Camera& cam, const Controls& c, double dt) = 0;
  virtual void dispose() = 0;
};

/** A tram or bus being watched. */
class Watcher : public WatchView {
 public:
  const WatchRequest req;
  // (at: where it is – the vehicle itself once it is there, else where the timetable has it; its height)

  Watcher(const WatchRequest& req, WatchHost host);
  ~Watcher() override;
  Watcher(const Watcher&) = delete;
  Watcher& operator=(const Watcher&) = delete;

  /**
   * One step of watching: where it is, the controls – touching one asks
   * whether to take it over.  The game steps the world round `ghost`.
   */
  void step(double dt, const Controls& c) override;

  /** The camera: behind and above the vehicle, turned and moved as the player looks round. */
  void camera(Camera& cam, const Controls& c, double dt) override;

  /** Watching ends: the bar and any question go. */
  void dispose() override;

 private:
  WatchHost h_;
  RailVehicle* tram_ = nullptr;
  TrafficCar* bus_ = nullptr;
  /** the trip by the timetable (worked out twice a second) – null: it is not under way (any more) */
  std::optional<TripState> trip_;
  double tripAt_ = -1;
  std::optional<Spot> spot_;
  double clock_ = 0;
  /** how long the area round it has been ready while the tram did not come */
  double waited_ = 0;
  dom::El bar_;
  std::string barText_;
  dom::El dialog_;
  /** the controls as they were when watching began (and after "keep watching"): a change is a touch */
  struct Base {
    double handbrake = 0;
    int gear = 0;
  };
  std::optional<Base> base_;
  /** a control must have been let go before touching one asks again */
  bool armed_ = false;
  bool camReady_ = false;
  /** how long the vehicle is (m): the camera keeps back by it */
  double length_ = 12;

  std::string t(const std::string& key, const TrParams& p = {}) const;
  /** Where the trip is by the timetable now (null: not under way). */
  std::optional<TripState> timetableTrip();
  std::optional<Spot> spotOfTrip(const TripState& trip);
  /** A step: the timetable's place of the trip, the vehicle itself (found among the traffic, or put there), and where it is now. */
  void update(double dt);
  void setGhost(double x, double y, double z, double yaw);
  /** Put the tram on its tracks where the timetable has it, as the traffic of the trams does. */
  RailVehicle* placeTram(RailTraffic& rail, const TripState& trip);
  /** Put the bus on its line's road where the timetable has it: the right-hand lane of that way. */
  TrafficCar* placeBus(Traffic& traffic);
  /** The bar at the top: what is watched, where it goes, and the ways out. */
  void showBar();
  /** "Take over this tram?" */
  void ask();
  void closeDialog();
  /** Take the vehicle over: where it is, the stop it stands at or runs to next – a bus with its line's ways from here on. */
  void take();
};

}  // namespace fs
