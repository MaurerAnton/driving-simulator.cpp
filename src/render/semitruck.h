// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/semitruck.ts @ v1.7.7
/**
 * The heavy trucks: the tractor unit of the articulated lorry (class CE),
 * its curtain-sider semi-trailer, and the three-axle rigid
 * truck with a box body of class C.  Simple shapes, generic liveries – no real
 * vehicle is reproduced.
 *
 * The tractor and the rigid truck share a cab (HeavyCab): a high cab over the
 * engine, its windows, the driver's place with the instruments, the steering
 * wheel and the screen with the map, and the two big mirrors – the parts of
 * the truck of class C1/CE (render/truck.ts) made bigger.  The tractor's is a
 * long-distance cab with a bunk and a high roof; the rigid's a shorter one
 * with a roof spoiler.  They differ in colour from the blue of the C1.
 *
 * Built around the points the physics use (sim/semitrailer.ts): the trucks
 * around the ground point below the middle of their wheelbase, the
 * semi-trailer around the middle of its tri-axle (x right, y up, z back, the
 * front towards −z).
 */
#pragma once

#include <memory>
#include <vector>

#include "engine/engine.h"
#include "render/boxbody.h"
#include "render/fleet.h"
#include "render/instruments.h"
#include "render/navscreen.h"
#include "sim/semitrailer.h"

namespace fs {

struct CarVisualState;
class HeavyCab;

/** The road wheels of a truck: the wheel groups and which of the physics' four (FL, FR, RL, RR) each follows. */
struct Wheels {
  std::vector<std::shared_ptr<Group>> groups;
  std::vector<int> of;
};

/**
 * The tractor unit: long-distance cab, frame with the fifth wheel over the
 * rear axle, fuel tank, the coiled lines (red, yellow, the cable) behind the
 * cab, mudguards and the rear lamps on a bar at the end of the frame.
 */
class PlayerSemi : public VehicleModel {
 public:
  PlayerSemi();
  ~PlayerSemi() override;
  void setCabinOpacity(double opacity) override;
  void update(const CarVisualState& s, double dt, double now) override;
  void dispose();

 private:
  std::unique_ptr<HeavyCab> cab_;
  Lamps lamps_;
  Wheels wheels_;
  const VehicleSpec& spec_ = TRACTOR_UNIT;
};

/**
 * The three-axle rigid truck of class C: a shorter cab with a roof spoiler,
 * green, on a frame with the twin-tyred drive axle and a single-tyred tag
 * axle behind it, and a box body whose rear doors open for the loading
 * (render/boxbody.ts – where services.boxInside expects it).
 */
class PlayerRigid : public VehicleModel {
 public:
  /** the box with its rear doors (render/loading.ts) */
  std::unique_ptr<BoxBody> body;

  PlayerRigid();
  ~PlayerRigid() override;
  void setCabinOpacity(double opacity) override;
  void update(const CarVisualState& s, double dt, double now) override;
  void dispose();

 private:
  std::unique_ptr<HeavyCab> cab_;
  Lamps lamps_;
  Wheels wheels_;
  const VehicleSpec& spec_ = HEAVY_RIGID;
};

/**
 * The curtain-sider semi-trailer: a box with curtains along its sides, a
 * front wall with the sockets for the lines, the kingpin under its front,
 * the landing gear, side guards, the tri-axle with mudguards, rear doors,
 * lamps and the amber side markers.  Placed by the game like the box trailer
 * behind a car (render/fleet.ts CarTrailerModel), with its legs as the
 * semi-trailer it is the picture of has them (`sim`).
 */
class SemiTrailerModel : public TowedModel {
 public:
  /** the semi-trailer it shows (its landing gear); none: wound up, as when coupled */
  const SemiTrailer* sim = nullptr;

  SemiTrailerModel();

  /** Where it is (the middle of its axles on the ground), its lamps as the tractor's – and its legs as the semi-trailer has them. */
  void place(double x, double y, double z, double yaw, double pitch, double wheelSpin, const CarVisualState* s) override;

 private:
  std::vector<std::shared_ptr<Group>> wheels_;
  const std::shared_ptr<Group> feet_ = make<Group>();
  Lamps lamps_;
};

// ------------------------------------------------------------------------- for the game
// (TowedModel – the model of a trailer pulled behind a vehicle, all but the drawbar trailer of the truck of
// class CE, which is part of its model –: render/fleet.h)

/** The model of the trailer for a vehicle's: `have` if it is of the right kind, else a new one. */
std::shared_ptr<TowedModel> towedModel(const TrailerSpec* trailer, const std::shared_ptr<TowedModel>& have);

/** Show the semi-trailer `t` (its legs) with the model, if both are a semi-trailer's. */
void linkTowed(TowedModel* model, const Trailer* t);

}  // namespace fs
