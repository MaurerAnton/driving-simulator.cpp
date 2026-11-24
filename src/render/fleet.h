// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/fleet.ts @ v1.7.7
/**
 * The vehicles of the other licence classes, as seen from outside and from
 * the driver's place: a city bus (D), a tractor (T), a motorcycle (A) and
 * the box trailer behind a car (BE).  Simple shapes in a generic livery – no
 * real vehicle is reproduced.
 *
 * Built like the truck (render/truck.ts) around the point the physics use –
 * the ground below the middle of the wheelbase, the trailer around the
 * middle of its axles –, x to the right, y up, the front towards −z.
 *
 * (The motorcycle, render/bike.ts, is not part of this file's translation.)
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "platform/canvas.h"
#include "render/fleetdetail.h"
#include "render/instruments.h"
#include "render/mirrors.h"
#include "render/navscreen.h"
#include "sim/vehicle/types.h"

namespace fs {

struct CarVisualState;

/** What the game asks of the model of the vehicle the player drives. */
class VehicleModel {
 public:
  const std::shared_ptr<Group> root = make<Group>();
  /** the driver's eyes (model coordinates) */
  Vector3 eye;
  std::vector<std::unique_ptr<Mirror>> mirrors;
  /** the screen in the cockpit the live map can be shown on (render/navscreen.ts) – null: the map stays in the corner */
  VehicleScreen* navScreen = nullptr;

  VehicleModel() = default;
  explicit VehicleModel(const Vector3& eye_) : eye(eye_) {}
  VehicleModel(const VehicleModel&) = delete;
  VehicleModel& operator=(const VehicleModel&) = delete;
  virtual ~VehicleModel() = default;

  virtual void setCabinOpacity(double opacity) = 0;
  /** the finer model of the "very high" graphics (true) or the plain one – for the models that have one */
  virtual void setDetail(bool on) {}
  virtual void update(const CarVisualState& s, double dt, double now) = 0;
};

/** The lamps every vehicle has: brake lamps, indicators (by side), and their switching. */
class Lamps {
 public:
  std::vector<std::shared_ptr<Mesh>> brake;
  struct Indicator {
    std::shared_ptr<Mesh> mesh;
    int side;
  };
  std::vector<Indicator> indicators;

  /** The brake and tail lamps, the indicators – and, given the model's root, its headlights as the light switch has them. */
  void update(const CarVisualState& s, Object3D* root = nullptr);

 private:
  std::optional<std::vector<Mesh*>> heads_;
};

/** A cab of panels: paint outside, lining inside (seen only from within, and made see-through on request). */
class Shell {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  const MaterialPtr lining;
  const MaterialPtr fittings;

  explicit Shell(MaterialPtr paint);

  void wall(std::string_view facing, double a0, double a1, double b0, double b1, double at, const MaterialPtr& paint = nullptr);
  void glass(std::string_view facing, double a0, double a1, double b0, double b1, double at);
  void setOpacity(double opacity);

 private:
  const MaterialPtr paint_;
};

class Destination;

/** A low-floor city bus, 12 m, three doors on the right, the engine at the back. */
class PlayerBus : public VehicleModel {
 public:
  /** `spec`: the bus (12 m, class D) or the minibus (class D1) – its length, height and doors follow from it. */
  explicit PlayerBus(const VehicleSpec& spec);
  PlayerBus();
  ~PlayerBus() override;

  void setCabinOpacity(double opacity) override;

  /**
   * Show a line on the destination displays: its number and where it goes
   * over the windscreen and beside the front door, the number at the back
   * ('' for both: no line – the maker's name as before).  Drawn anew only
   * when they change.
   */
  void setLine(const std::string& ref, const std::string& to);

  void update(const CarVisualState& s, double dt, double now) override;

  /** The finer model (true) or the plain one. */
  void setDetail(bool on) override;

 private:
  std::unique_ptr<NavScreen> screen_;
  std::vector<std::shared_ptr<Group>> wheels_;
  std::shared_ptr<Group> steering_;
  Lamps lamps_;
  Shell shell_;
  InstrumentPanel panelTex_;
  const VehicleSpec spec_;
  /** the maker's name over the windscreen – where the destination display goes when the bus runs a line */
  std::shared_ptr<Mesh> brand_;
  /** the measures the displays are placed by: half width, the top of the windows, the roof, front and back */
  struct Body {
    double hw, top, roof, zf, zr;
    bool long_;
  } body_{};
  /** the destination display at the front, on the right side and at the back (made when a line is first shown) */
  std::unique_ptr<Destination> dest_;
  std::vector<std::shared_ptr<Mesh>> plates_;
  /** the finer model of the "very high" graphics (render/fleetdetail.ts): made the first time it is asked for */
  std::unique_ptr<FleetDetail> detail_;
};

/** A tractor: big wheels at the back, small ones at the front, a glass cab with the seat in the middle. */
class PlayerTractor : public VehicleModel {
 public:
  PlayerTractor();
  void setCabinOpacity(double opacity) override;
  void update(const CarVisualState& s, double dt, double now) override;
  /** The finer model (true) or the plain one. */
  void setDetail(bool on) override;

 private:
  std::unique_ptr<NavScreen> screen_;
  std::vector<std::shared_ptr<Group>> wheels_;
  /** the visible wheels are of two sizes (the physics use one): their centres stand at their own radius */
  const std::vector<double> radii_ = {0.56, 0.56, 0.8, 0.8};
  std::shared_ptr<Group> steering_;
  Lamps lamps_;
  Shell shell_;
  InstrumentPanel panelTex_;
  std::unique_ptr<FleetDetail> detail_;
};

/**
 * The model of a trailer pulled behind a vehicle – render/semitruck.ts's `TowedModel` (CarTrailerModel |
 * SemiTrailerModel): what both have, as the base they share.
 */
class TowedModel {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  TowedModel() = default;
  TowedModel(const TowedModel&) = delete;
  TowedModel& operator=(const TowedModel&) = delete;
  virtual ~TowedModel() = default;
  /** Where it is (the middle of its axles on the ground), and its lamps as the towing vehicle's (`s` null: as they were). */
  virtual void place(double x, double y, double z, double yaw, double pitch, double wheelSpin, const CarVisualState* s) = 0;
};

/** A tandem box trailer with a tarpaulin behind a car (class BE), around the middle of its axles. */
class CarTrailerModel : public TowedModel {
 public:
  CarTrailerModel();

  /** Where it is (the middle of its axles on the ground), and its lamps as the car's. */
  void place(double x, double y, double z, double yaw, double pitch, double wheelSpin, const CarVisualState* s) override;

 private:
  std::vector<std::shared_ptr<Group>> wheels_;
  Lamps lamps_;
};

/**
 * The driver's cab at the front of the player's tram – only its inside: the
 * tram itself is drawn with the others (render/trams.ts), and from within
 * its walls are not seen.  Around the middle of the first car (on the rails),
 * the front towards −z: the desk with the controller and the instruments, the
 * seat, the walls of the cab below and above the windows.
 */
class TramCab : public VehicleModel {
 public:
  TramCab();
  void setCabinOpacity(double opacity) override;
  /** The controller's position: −1 full brake … 1 full power. */
  void setLever(double lever);
  void update(const CarVisualState& s, double dt, double now) override;

 private:
  std::unique_ptr<NavScreen> screen_;
  Shell shell_;
  InstrumentPanel panelTex_;
  /** the controller's handle: forwards for power, back for the brake */
  const std::shared_ptr<Group> handle_ = make<Group>();
};

}  // namespace fs
