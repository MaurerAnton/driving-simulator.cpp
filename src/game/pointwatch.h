// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Watching something on the map that is not a tram or a bus of the
 * timetables: a police car, an ambulance or a fire engine on its call, a
 * demonstration marching through the town, a site of roadworks.  The camera
 * goes with it and circles it as the player looks round; the world loads
 * round it; the player's own vehicle waits.  A bar at the top says what is
 * watched and leads back.  (Trams and buses of the timetables: spectate.h.)
 *
 * A vehicle (any car of the traffic, a bus, an emergency vehicle, a tram –
 * game/takeover.h) can be taken over: "Take over" in the bar, or touching a
 * driving control, which asks first – the player then drives it from where
 * it is (the game does that: `WatchTarget.take`).
 *
 * Origin: src/game/pointwatch.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>

#include "game/spectate.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "sim/playerstate.h"

namespace fs {

class Camera;
struct Controls;

/** What is watched: where it is now (null: gone), what it is called, how long it is. */
struct WatchTarget {
  std::string title;
  std::string sub;
  std::function<std::optional<WatchAt>()> get;
  /** its length (m): the camera keeps back by it – a site or a crowd is looked at from further */
  double length = 0;
  /** a vehicle that can be taken over: the game begins a drive of it from where it is (null: it cannot) */
  std::function<void()> take = nullptr;
};

struct PointWatchHost {
  std::function<dom::El()> hud;
  /** the whole interface (the question whether to take it over goes there) and the texts */
  dom::El ui;
  std::function<std::string(const std::string& key, const TrParams& p)> t;
  /** "Back to my vehicle", and the line said when it is gone */
  std::function<std::string()> backText;
  std::function<std::string()> goneText;
  std::function<double(double x, double z)> ground;
  std::function<void()> back;
};

class PointWatcher : public WatchView {
 public:
  // (at, ghost: where the thing watched is – WatchView)

  PointWatcher(WatchTarget target, PointWatchHost host);
  ~PointWatcher() override;
  PointWatcher(const PointWatcher&) = delete;
  PointWatcher& operator=(const PointWatcher&) = delete;

  void step(double dt, const Controls& c) override;
  /** Behind and above it, turned and moved as the player looks round. */
  void camera(Camera& cam, const Controls& c, double dt) override;
  void dispose() override;

 private:
  WatchTarget target_;
  PointWatchHost h_;
  bool gone_ = false;
  bool camReady_ = false;
  dom::El bar_;
  dom::El sub_;
  dom::El dialog_;
  /** the controls as they were when watching began (and after "keep watching"): a change is a touch */
  struct Base {
    double handbrake = 0;
    int gear = 0;
  };
  std::optional<Base> base_;
  /** a control must have been let go before touching one asks again */
  bool armed_ = false;

  void update();
  std::string subText() const;
  /** "Drive this vehicle yourself?" */
  void ask();
  void closeDialog();
  /** The bar at the top: what is watched, and the way back. */
  void showBar();
};

}  // namespace fs
