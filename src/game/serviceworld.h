// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/serviceworld.ts @ v1.7.7
/**
 * Fuel stations, car washes and the loading of a truck – in the game.
 *
 * This keeps what the game needs of them, and is called from a few places
 * of the hub (game*.cpp):
 *
 *  - load: the stations and washes of the map (services.json.gz; a map
 *    without the file simply has none);
 *  - tile: as a tile of the map comes, the blocks the building models drew
 *    for the canopies and wash halls are taken out of it (its building mesh,
 *    its collision outlines, the trees under them), and the sites whose
 *    middle lies on it are built a frame at a time (render/services.h) –
 *    with their own collision outlines: islands, columns, walls;
 *  - begin: a new drive – a tank of the vehicle's kind with some fuel in it,
 *    some dirt on it, and on a free drive with a truck of class C1 or C the
 *    goods it is to be loaded with (its physics get the load: sim/payload.h;
 *    with a trailer behind the box's doors – C1E, CE – nothing is loaded);
 *  - startScene: the loading of that truck at a depot beside the road (or
 *    at the roadside where there is no room), render/loading.h, before the
 *    drive begins;
 *  - drive (every step of a drive): the engine burns its fuel (sim/fuel.h)
 *    and stops with the tank empty; the dirt grows; what the vehicle stands
 *    at – a pump, the mark in a car wash – and what is going on there
 *    (game/services.h); on a station's ground nothing is judged as on the
 *    road (rules.yard);
 *  - visual and hud (every frame): the fuel gauge, the dirt on the model,
 *    the nozzle and the gantry, and the tip that says what to do – on a
 *    touch screen the tip itself is the button, with keys it is 9 (the
 *    "service" action of input).
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/task.h"
#include "engine/engine.h"
#include "game/services.h"
#include "platform/dom.h"
#include "render/dirt.h"
#include "render/loading.h"
#include "render/services.h"
#include "sim/fuel.h"
#include "world/services.h"

namespace fs {

class SceneryCollider;
class MapLoader;
class Vehicle;
class VehicleModel;
struct DriverInput;
struct VehicleSpec;
struct VehicleDef;
struct CarVisualState;
struct HudState;
struct LoadedTile;

/** What the services need of the game. */
struct ServiceHost {
  Object3D* scene = nullptr;
  SceneryCollider* collider = nullptr;
  /** where the overlay of the loading scene goes */
  dom::El ui;
  std::shared_ptr<MapLoader> map;
  /** the ground at a point (NaN where it is not loaded) */
  std::function<double(double x, double z)> ground;
  /** is a point on a road or a pavement? */
  std::function<bool(double x, double z)> onRoad;
  /** would the vehicle being driven touch anything standing there? */
  std::function<bool(double x, double z, double yaw)> blocked;
  std::function<std::string()> lang;
  /** a notice in the street-name block */
  std::function<void(const std::string& title, const std::string& sub)> message;
  /** 'chime' | 'tick' | 'door' */
  std::function<void(const std::string& sound)> click;
  std::function<void()> hideOverlay;
  /** the rules' `yard` (nothing judged on a station's ground) */
  bool* yard = nullptr;
  std::function<bool()> shadows;
};

class ServiceWorld {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  /** the fuel in the tank of the vehicle being driven (null: one without a tank) */
  std::unique_ptr<FuelTank> tank;
  /** the dirt on it, 0 clean … 1 filthy */
  double dirt = 0;
  /** the dirt drawn on the body (the graphics' weatherFx, 1.7.1: "No mercy"); it gathers all the same, for the car wash */
  bool dirtShown = true;

  explicit ServiceWorld(ServiceHost host);
  ServiceWorld(const ServiceWorld&) = delete;
  ServiceWorld& operator=(const ServiceWorld&) = delete;
  ~ServiceWorld();

  /** The fuel stations and car washes of the map (for the big map; none before the file is read). */
  const std::vector<svc::ServiceSite>& sites() const;

  /** Read the stations and washes of the map (none if it has no services.json.gz). */
  Task<void> load();

  /** A tile came (or went): take the blocks of the canopies and wash halls out of it, and build the sites on it. */
  void tile(LoadedTile& tile, bool loaded);

  /**
   * A new drive with this vehicle: its tank, its dirt – and on a free drive
   * (`free`) the goods of a truck of class C1 or C: returns the spec to build
   * its physics with (with the load in it).
   */
  VehicleSpec begin(const VehicleDef& def, const VehicleSpec& spec, bool free);

  /**
   * The truck stands where the drive begins: if it is to be loaded, the
   * scene of the loading begins (true – `done` is called when it is over);
   * else nothing happens (false).
   */
  bool startScene(Vehicle& v, VehicleModel& model, std::function<void()> done);

  /**
   * A step of a drive (before the physics): fuel, dirt, what the vehicle
   * stands at, what is going on there.  `auto`: the vehicle drives by
   * itself (a demonstration) – it burns nothing then.  `task`: the yard of
   * the truck lessons is in charge of the rules.
   */
  void drive(double dt, Vehicle& v, DriverInput& d, const VehicleDef& def, bool walking, bool task, bool autoDrive);

  /** The service button (9, or the tip tapped): skip the loading, fill up, stop the pump, wash, call for help. */
  void act(const std::string& state);

  /** The frame's pictures: the fuel gauge of the instruments, the dirt, the sites, the nozzle, the gantry, the loading. */
  void visual(CarVisualState& s, VehicleModel& model, double dt);

  /** The HUD: the fuel gauge and its lamp, and the tip of what to do here. */
  void hud(HudState& hs, bool touch);

  /** The camera of the loading scene (nothing otherwise). */
  void camera(Camera& cam);

  /** Is the loading scene running? */
  bool loading() const { return scene_ != nullptr; }

  /** The drive is over (to the menu, or a new one begins): whatever was going on stops. */
  void end();

 private:
  struct Activity {
    /** 'fill' | 'wash' | 'help' */
    std::string kind;
    std::unique_ptr<Refuelling> r;
    std::unique_ptr<Washing> w;
    const svc::ServiceSite* site = nullptr;
    int canopy = 0;
    int pump = 0;
    int side = 0;
    bool noted = false;
    int dir = 1;
    double t = 0;
  };
  /** What the vehicle stands at, as worked out each step. */
  struct Here {
    /** on the ground of a station, a wash or the depot (nothing is judged there as on the road) */
    bool onSite = false;
    struct Pump {
      const svc::ServiceSite* site;
      int canopy;
      int pump;
      int side;
      std::string fuel;
      std::string why;
    };
    std::optional<Pump> pump;
    struct Bay {
      const svc::ServiceSite* site;
      int dir;
      double along;
      std::string why;
    };
    std::optional<Bay> bay;
  };
  struct Scene {
    std::shared_ptr<LoadingScene> s;
    std::function<void()> done;
    dom::El box;
    dom::El caption;
    std::string shown;
  };
  struct Depot {
    std::shared_ptr<LoadingScene> s;
    double x;
    double z;
  };

  ServiceHost host_;
  std::unique_ptr<svc::ServiceIndex> index_;
  std::map<int, std::unique_ptr<SiteView>> views_;
  /** sites whose tile has come, to be built (one a frame) */
  std::vector<const svc::ServiceSite*> queue_;
  FillView fillView_;
  SprayView spray_;
  DirtLook dirtLook_;
  const VehicleDef* def_ = nullptr;
  double height_ = 1.45;
  std::unique_ptr<Activity> doing_;
  Here here_;
  bool standing_ = true;
  bool engineOn_ = false;
  bool yardSet_ = false;
  bool lowWarned_ = false;
  bool emptyWarned_ = false;
  /** the goods of the truck, and the scene of its loading (and the depot it leaves standing) */
  struct Goods {
    std::vector<PalletPlan> pallets;
    double mass;
  };
  std::optional<Goods> goods_;
  std::unique_ptr<Scene> scene_;
  std::optional<Depot> depot_;
  std::array<double, 3> p_{};
  /** the nozzle stands in the filler (it is not moved again while the fuel runs) */
  bool fillShown_ = false;
  std::shared_ptr<bool> life_ = std::make_shared<bool>(true);

  void drop(int i);
  void caption();
  void finishScene();
  Here whereIs(double x, double z, Vehicle& v, const VehicleDef& def, bool walking);
  std::string nearestText(double x, double z, double yaw) const;
  int pumpIndex(const svc::ServiceSite& site, int canopy, int pump) const;
  void buildNext(double px, double pz);
  void stopAll();
  void dropDepot();
  void queueAdd(const svc::ServiceSite* s);
};

}  // namespace fs
