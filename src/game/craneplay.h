// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/craneplay.ts @ v1.7.7
/**
 * Working a crane on the construction site – what the game (game.ts) runs
 * for the tower crane, the mobile crane and the loader crane:
 *
 *  - the site is laid out on the practice yard once its ground is loaded
 *    (render/cranesite.ts draws it, game/cranetasks.ts says what is where),
 *    its buildings and loads stand in the vehicles' way, its roads are paved;
 *  - the mobile and the loader crane are driven like any truck from the
 *    depot to their stand; parked there with the parking brake on, the
 *    door key (or the button) changes to working the crane – and back,
 *    once it is folded and its outriggers are in again;
 *  - while the crane is worked its vehicle stands still: the controls move
 *    the crane's drives (sim/crane.ts), the hook swings on its rope, loads
 *    are slung and let go, the limiter watches, the tasks are judged and
 *    what goes wrong is told to the instructor (sim/rules.ts);
 *  - the crane's own cameras: from the operator's seat (looking where the
 *    load is), and round the hook;
 *  - the street-name block says what to do, the crane's display
 *    (ui/cranepanel.ts) what the limiter measures.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "game/ce.h"
#include "game/cranetasks.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "sim/crane.h"

namespace fs {

class GameAudio;
class Input;
struct Controls;
class CraneModel;
class CraneSiteScene;
class CranePanel;
class SceneryCollider;
class WorldGround;
class Vehicle;
struct HudState;
struct Settings;

/** What the crane work needs of the game. */
struct CraneHost {
  std::shared_ptr<Scene> scene;
  dom::El ui;
  const Settings* settings = nullptr;
  /** the practice yard the site is laid on (null: the map has none) */
  const YardFrame* yard = nullptr;
  /** the map's ground (NaN where it is not loaded) */
  std::function<double(double x, double z)> heightAt;
  SceneryCollider* collider = nullptr;
  WorldGround* ground = nullptr;
  Input* input = nullptr;
  GameAudio* audio = nullptr;
  /** tell the instructor of a fault (at a place) */
  std::function<void(const CraneFault& f, double x, double z)> note;
};

/** a place and a heading in the world */
struct CranePlace {
  double x = 0;
  double z = 0;
  double yaw = 0;
};

/**
 * Where a crane's work begins: the mobile and the loader crane at the
 * depot on the site road, the tower crane at its base – in the world, from
 * the practice yard's frame (null: the map has no practice yard).
 */
std::optional<CranePlace> craneStart(const CraneType& type, const YardFrame* yard);

/**
 * The site behind the menu (the crane chosen stands on it): laid out – or
 * taken away again (type null).  `heightAt`: the map's ground.
 */
void craneMenuSite(const CraneHost& host, const std::optional<CraneType>& type, const std::function<double(double, double)>& heightAt);

/** A crane standing as it travels, for the picture behind the menu: folded, its hook hanging (where its model stands). */
void craneMenuPose(CraneModel& model, const CraneType& type);

/**
 * Where somebody who climbed down the tower crane stands: beside its base
 * (axis at bx, bz, turned `yaw`), facing it – on the left of it first, then
 * round it an eighth at a time to the first place `free` says one can stand.
 */
CranePlace towerExit(double bx, double bz, double yaw, const std::function<bool(double x, double z)>& free);

/** Is somebody standing at (x, z) at the foot of the tower crane (axis at bx, bz), to climb up? */
bool atTowerFoot(double bx, double bz, double x, double z);

/** The work with a crane on the site, from the start of the drive to its end. */
class CraneWork {
 public:
  const CraneType type;
  Crane crane;
  CranePanel& panel;
  /** the tasks (null until the site is laid out, or on a map without one) */
  std::unique_ptr<CraneTasks> tasks;
  /** the crane is being worked (its vehicle stands still) – the tower crane always */
  bool operating = false;
  /** all is done, or it tipped: the drive is over (the game shows the result) */
  bool over = false;
  /** asked to change between driving and working the crane (the game decides: see `toggle`) */
  std::function<void()> toggleRequested = nullptr;

  CraneWork(const CraneType& type, CraneHost host);
  CraneWork(const CraneWork&) = delete;
  CraneWork& operator=(const CraneWork&) = delete;
  ~CraneWork();

  /**
   * The tower crane's operator climbs down (`away`) or up again: meanwhile the
   * keys and the on-screen controls are those of somebody on foot (the
   * steering bar and the pedals walk) – the crane stands as it was left.
   */
  void setAway(bool away);

  /** The tower crane: where its operator stands after climbing down (`free`: can one stand there?). */
  CranePlace towerExit(const std::function<bool(double x, double z)>& free) const;

  /** The tower crane: is somebody standing here at its foot (to climb up)? */
  bool atTowerFoot(double x, double z) const;

  /**
   * Change between driving and working the crane: to the crane once the
   * vehicle stands with its parking brake on, back to driving once the
   * crane is folded and its outriggers are in.  The key of a tip when it
   * cannot be done now (null: done).
   */
  std::optional<std::string> toggle(Vehicle& v, bool handbrake);

  /** Each frame while the vehicle is driven: the site, the loads on the flatbed going with it, the guide (drive there, park). */
  void drive(double dt, const Vehicle& v, bool handbrake);

  /** Each frame while the crane is worked: the controls, the crane's steps, the load on the hook, the tasks. */
  void work(double dt, Vehicle& v);

  /** Sling the load the hook is over, or let go of the one on it (once it stands and its slings are slack). */
  void attach();

  /** Pose the crane's model and draw the site's loads as they are; `shown`: the crane's display is shown (while driving or working, not over the menus). */
  void render(CraneModel& model, double now, bool shown);

  /** The panel and the street-name block: what the limiter measures, what to do now.  `walker`: where the player stands on foot (null: not on foot). */
  void hud(HudState& hs, bool touch, const std::optional<std::array<double, 2>>& walker = std::nullopt);

  /**
   * The camera while the crane is worked: from the operator's seat looking
   * where the hook is ('cockpit'), or round the hook ('chase').  False for
   * the free camera (the game's own).
   */
  bool camera(Camera& cam, const std::string& mode, const Controls& c, double dt, CraneModel& model);

  /** The tasks as they went, for the result page (null: none was done). */
  std::optional<dom::El> resultNode() const;

  /** The work ends: the site and the panel go, the keys are the driver's again. */
  void dispose();

 private:
  /** the yard as the site's tasks and drawing take it (cranetasks.ts SitePlacement) */
  struct Placement final : SitePlacement {
    const YardFrame* yard = nullptr;
    std::array<double, 2> toWorld(double u, double w) const override { return yard->toWorld(u, w); }
    std::array<double, 2> toLocal(double x, double z) const override { return yard->toLocal(x, z); }
    double yawToWorld(double yaw) const override { return yard->yawToWorld(yaw); }
    double yawToLocal(double yaw) const override { return yard->yawToLocal(yaw); }
  };

  CraneHost host_;
  CraneSiteScene& site_;
  Placement place_;
  double overAt_ = -1;
  bool laid_ = false;
  double laidTry_ = -10;
  double y0_ = 0;
  std::optional<SiteWorld> geo_;
  std::vector<CraneBox3> boxes_;
  std::vector<CraneBox3> own_;
  double acc_ = 0;
  double time_ = 0;
  /** what happened in the crane's steps since the last frame */
  CraneEvents ev_{.hit = 0, .landed = 0, .liftedOff = false, .cut = false};
  /** a tip for the player, and until when */
  std::string tip_;
  double tipUntil_ = 0;
  TrParams sayData_;
  /** the vehicle as it stands (world), for the tasks and the drawing */
  struct Veh {
    double x = 0, y = 0, z = 0, yaw = 0, speed = 0;
    bool handbrake = true;
  } veh_;
  Quaternion vq_;
  CraneWorld world_;
  CraneWind wind_;
  /** where the camera looks from the operator's seat (smoothed) */
  Vector3 look_;
  bool lookReady_ = false;
  Vector3 tmp_;
  Vector3 tmp2_;
  double beepT_ = 0;
  CraneSnapshot snap_;
  /** the footprints the vehicles run into (the collider keeps pointers to them) */
  std::vector<float> rings_;
  std::vector<uint16_t> ringCounts_;

  void layout();
  void colliders();
  void refreshBoxes();
  double floor(double x, double z, double hx, double hz, double y);
  std::optional<CraneBoxY> bedBox() const;
  void ownBoxes();
  void fromVehicle(const Vehicle& v, bool handbrake);
  void setOperating(bool on);
  void carryLoads();
  WorldLoad* hooked();
  const CraneSnapshot& snapshot();
  void updateTasks();
  void say(const std::string& key, double seconds, const TrParams& data = {});
};

}  // namespace fs
