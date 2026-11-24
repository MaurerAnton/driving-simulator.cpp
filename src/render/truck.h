// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/truck.ts @ v1.7.7
/**
 * The truck of the class-CE lessons and its centre-axle trailer: a two-axle
 * distribution truck with a box body, 8.2 m long, and a tandem trailer of
 * 7.9 m.  Simple shapes, generic livery – no real vehicle is reproduced.
 *
 * Both are built around the points the physics use: the truck around the
 * ground point below the middle of its wheelbase, the trailer around the
 * middle of its axle group (x right, y up, z back, the front towards -z).
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/engine.h"
#include "render/boxbody.h"
#include "render/fleet.h"
#include "render/fleetdetail.h"
#include "render/instruments.h"
#include "render/mirrors.h"
#include "render/navscreen.h"
#include "sim/vehicle/index.h"

namespace fs {

struct CarVisualState;

// (the original's module constants: made when first asked for, and kept for the life of the program)
const MaterialPtr& WHITE();
const MaterialPtr& CAB();
const MaterialPtr& DARK();
const MaterialPtr& FRAME();
const MaterialPtr& TYRE();
const MaterialPtr& RIM();
/** window glass: tinted, seen through from both sides */
const MaterialPtr& GLASS();
const MaterialPtr& LAMP();
/** the headlights switched off (glass over a dark reflector), and on full beam */
const MaterialPtr& LAMP_OFF();
const MaterialPtr& LAMP_HIGH();
/** the tail lamps with the lights off */
const MaterialPtr& TAIL_OFF();
const MaterialPtr& TAIL();
const MaterialPtr& AMBER();
const MaterialPtr& LAMP_STOP();

/** what of the light switch the lamps show (`{ lowBeam?, highBeam?, braking? }`) */
struct LampSwitch {
  bool lowBeam = false;
  bool highBeam = false;
  bool braking = false;
};
LampSwitch lampSwitch(const CarVisualState& s);

/**
 * The lamps of a model as the light switch has them: every mesh in `root`
 * made of the headlight material (LAMP / LAMP_OFF / LAMP_HIGH) shows the
 * beams, every given tail lamp glows with the lights on (and brightly on the brake).
 * `heads`: the headlights found the last time (null: look for them) – the list is returned.
 */
std::vector<Mesh*> lightLamps(Object3D& root, const LampSwitch& s, const std::vector<std::shared_ptr<Mesh>>& tails, const std::optional<std::vector<Mesh*>>& heads);

/** 'x+' | 'x-' | 'y+' | 'y-' | 'z+' | 'z-' */
using Facing = std::string_view;

/**
 * A flat rectangle looking along an axis.  `a` runs across it (x, or z for a
 * panel facing x), `b` up it (y, or z for a panel facing up or down), `at`
 * is where it stands on the axis it faces.
 */
std::shared_ptr<Mesh> panel(Facing facing, double a0, double a1, double b0, double b1, double at, const MaterialPtr& material);

/** A box from x0…x1, y0…y1, z0…z1. */
std::shared_ptr<Mesh> box(double x0, double x1, double y0, double y1, double z0, double z1, const MaterialPtr& material, bool shadow = true);

/** A wheel (or a twin wheel) lying on the x axis, its middle at the origin. */
std::shared_ptr<Group> wheel(double radius, double width);

/** Large lettering on a plate: dark on white, readable from the chase camera. */
std::shared_ptr<Mesh> lettering(const std::string& text, double width, double height);

inline const std::string BRAND = "maureranton.com";

class PlayerTruck : public VehicleModel {
 public:
  /** the trailer: a group of its own, placed in the world by the game */
  const std::shared_ptr<Group> trailer = make<Group>();
  /** the box with its rear doors (render/boxbody.ts: opened while goods are loaded) */
  std::unique_ptr<BoxBody> body;

  /** `spec`: the truck it is the picture of (the 12-tonner of class C/CE, or the 7.5-tonner of C1) */
  explicit PlayerTruck(const VehicleSpec& spec = TRUCK);

  /**
   * How solid the inside of the cab looks from the driver's seat: 1 = solid,
   * less = the road shows through lining, dashboard and steering wheel.
   * (From outside the cab is always solid: its paint is another face.)
   */
  void setCabinOpacity(double opacity) override;

  /** The truck's moving parts and lamps. */
  void update(const CarVisualState& s, double dt, double now) override;

  /** The finer model (true) or the plain one. */
  void setDetail(bool on) override;

  /** Where the trailer is (the middle of its axle group on the ground), and whether it stands on its leg. */
  void setTrailer(double x, double y, double z, double yaw, double pitch, double wheelSpin, bool coupled);

  void dispose();

 private:
  const VehicleSpec spec_;
  /** the screen with the map on the dashboard (render/navscreen.ts) */
  std::unique_ptr<NavScreen> screen_;
  std::vector<std::shared_ptr<Group>> wheels_;
  std::vector<std::shared_ptr<Group>> trailerWheels_;
  const std::shared_ptr<Group> steering_ = make<Group>();
  std::vector<std::shared_ptr<Mesh>> brakeLamps_;
  std::optional<std::vector<Mesh*>> heads_;
  struct Indicator {
    std::shared_ptr<Mesh> mesh;
    int side;
  };
  std::vector<Indicator> indicators_;
  std::shared_ptr<Mesh> leg_;
  const std::shared_ptr<Group> cabin_ = make<Group>();
  InstrumentPanel panel_;
  /** the inside of the cab's walls (seen only from within) and what is fitted in the cab */
  const MaterialPtr lining_;
  const MaterialPtr fittings_;
  const MaterialPtr off_;
  /** the finer model of the "very high" graphics (render/fleetdetail.ts): made the first time it is asked for */
  std::unique_ptr<FleetDetail> detail_;
};

}  // namespace fs
