// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/forkliftplay.ts @ v1.7.7
/**
 * Driving a forklift on the practice yard – what the game (game.h) runs
 * for it:
 *
 *  - the place is laid out on the practice yard once its ground is loaded
 *    (game/forklifttasks.h says what is where, render/forkliftyard.h
 *    draws it); without a practice yard, round the place the drive starts;
 *  - the forklift is simulated by sim/forklift.h (not by the vehicles'
 *    physics): each frame the controls go in – the pedals, the wheel, the
 *    direction (the gear selector: D forwards, R backwards; P and the
 *    parking brake hold it) and the mast's keys or buttons
 *    (Input.forkInput, ui/forkliftpanel.h) –, and the game's vehicle is
 *    put where the forklift is, so the cameras, the speedometer, the sound
 *    and the replay follow it;
 *  - the guided start and the tasks are judged as they go; what is done
 *    unsafely is told to the instructor: driving with the load raised or
 *    the mast tilted forwards, turning too fast, lifting more than the load
 *    chart allows, the rear wheels lifting, running into things, tipping
 *    over;
 *  - the street-name block says what to do, the forklift's display the
 *    load moment, the load and the mast;
 *  - a free drive (any way to play but "Forklift work" – `FORK_WORK_MODE`
 *    in game/forkliftmodes.h, with the cameras and getting off and on)
 *    lays out no place: the forklift is driven where the menu put it, the
 *    buildings and walls of the map stopping it (`ForkliftHost.blocked`);
 *  - its cameras: from the side (where it starts), the operator's seat,
 *    behind it, the free camera (`ForkView`, V or the camera button);
 *  - the operator gets off (G, the door button, "Get out" in the pause
 *    menu) beside the seat, and on again within a few steps of it.
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "engine/engine.h"
#include "game/ce.h"
#include "game/forkliftmodes.h"
#include "game/forklifttasks.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "sim/forklift.h"
#include "sim/rules.h"

namespace fs {

class GameAudio;
class Input;
struct Controls;
class ForkliftModel;
class ForkYardScene;
class ForkPanel;
class Vehicle;
struct HudState;
struct Settings;

/** What the forklift's work needs of the game. */
struct ForkliftHost {
  std::shared_ptr<Scene> scene;
  dom::El ui;
  const Settings* settings = nullptr;
  /** the practice yard (null: the map has none) */
  const YardFrame* yard = nullptr;
  std::function<double(double x, double z)> heightAt;
  Input* input = nullptr;
  GameAudio* audio = nullptr;
  /** tell the instructor of a fault (at a place; not again within `cooldown` seconds) */
  std::function<void(const std::string& id, const Severity& severity, double x, double z, const ViolationData& data, double cooldown)> note;
  /** does the forklift standing here (its middle, its heading) run into a building or a wall of the map? (a free drive; may be empty) */
  std::function<bool(double x, double y, double z, double yaw)> blocked;
};

struct ForkPlace {
  double x;
  double z;
  double yaw;
};

/** Where the forklift starts on the practice yard (null: the map has none). */
std::optional<ForkPlace> forkliftStart(const YardFrame* yard);

/** The place behind the menu (the forklift chosen stands on it) – or taken away again (`on` false). */
void forkliftMenuSite(const ForkliftHost& host, bool on, const std::function<double(double, double)>& heightAt);

/** The forklift on its place, from the start of the drive to its end. */
class ForkliftWork {
 public:
  Forklift f;
  /** the work on the forklift yard with its tasks (false: a free drive where the menu put it) */
  const bool siteOn;
  /** the camera (V goes through them) */
  ForkView view = "side";
  std::unique_ptr<ForkTasks> tasks;
  /** all is done, or it tipped over: the drive is over (the game shows the result) */
  bool over = false;

  explicit ForkliftWork(ForkliftHost host, bool site = true);
  ForkliftWork(const ForkliftWork&) = delete;
  ForkliftWork& operator=(const ForkliftWork&) = delete;
  ~ForkliftWork();

  /** A frame of driving: the controls, the forklift's steps, the vehicle put where it is, the faults, the tasks. */
  void step(double dt, Vehicle& v, const Controls& c);
  /** Pose the model, the pallets and the marks; `inside`: seen from the operator's seat; `shown`: the display is shown (while driving). */
  void render(ForkliftModel& model, double now, bool inside, bool shown);
  /** A tip of the game's in the tip line (while driving, the forklift's own tips take the line's place). */
  void tell(const std::string& key, double seconds = 3);
  /** Getting off: where the operator stands (null: not while it moves – stop first). */
  std::optional<ForkExit> getOff() const;
  /** Can somebody standing here get on? */
  bool canBoard(double x, double z) const;
  /** The next camera (V): the one now. */
  ForkView nextView();
  /** The display and the street-name block: what to do now, the tips; `walker`: the operator got off and stands there. */
  void hud(HudState& hs, bool touch, const std::optional<std::array<double, 2>>& walker = std::nullopt);
  /** The tasks as they went, for the result page (null: none was finished). */
  std::optional<dom::El> resultNode() const;
  /** The drive ends: the place goes, the display, the buttons. */
  void dispose();

 private:
  ForkliftHost host_;
  /** it has been driven (the tip how to start is said till then) */
  bool moved_ = false;
  ForkYardScene& scene_;
  std::unique_ptr<ForkPanel> panel_;
  std::unique_ptr<ForkSite> site_;
  std::optional<ForkWorld> world_;
  /** (no practice yard: the frame laid round where the drive began) */
  std::unique_ptr<YardFrame> ownFrame_;
  double overAt_ = -1;
  double time_ = 0;
  double laidTry_ = -10;
  double acc_ = 0;
  std::array<double, 3> mpos_ = {0, 0, 0};
  std::string tip_;
  TrParams tipData_;
  double tipUntil_ = 0;
  double beepT_ = 0;
  double warnT_ = 0;
  /** the pallet the overload was told for (once each time it is lifted) */
  std::string overloadTold_;
  ForkEvents ev_;

  /** Lay the place out once its ground is loaded; the forklift where its vehicle stands. */
  bool layout(Vehicle& v);
  void sounds(const ForkEvents& ev, double dt, const ForkCommand& cmd);
  void note(const std::string& id, const Severity& severity, const ViolationData& data = {}, double cooldown = 6);
  void say(const std::string& key, double seconds, const TrParams& data = {});
  /** What the instructor notes. */
  void faults(const ForkEvents& ev, double dt);
  ForkSnapshot snapshot() const;
  void updateTasks();
};

}  // namespace fs
