// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/crane.ts @ v1.7.7
/**
 * The cranes the player works, drawn with every part that makes them stand
 * and move (sim/crane.ts has their masses and dimensions):
 *
 *  - the tower crane: four foundation blocks, the cross base with its
 *    ballast blocks, the lattice mast section by section (with its ladder
 *    and rest platforms), the slewing ring and platform, the cab, the tower
 *    head with the pendant ties, the lattice jib with its load plates, the
 *    trolley and the hook block on two falls of rope, the counter-jib with
 *    its walkway, the hoist winch and the counter-ballast blocks, the
 *    anemometer and the red obstruction lights;
 *  - the mobile crane: a three-axle carrier with the driver's cab, four
 *    outrigger beams with their jacks, pads and the mats under them, the
 *    turntable, the superstructure with the crane cab, the hoist winch and
 *    the counterweight, the boom of four telescopic sections on its
 *    luffing cylinder, the head sheaves and the hook block;
 *  - the loader crane: a flatbed truck, behind its cab the crane's base
 *    with its outriggers, the column, the inner boom on the lift cylinder,
 *    the outer boom on the folding cylinder with its extensions, the hook.
 *
 * Built like the vehicles (render/truck.ts) around the point the physics use
 * – the ground below the middle of the wheelbase; the tower crane: the
 * middle of its base –, x right, y up, the front (and the boom at slewing
 * angle 0) towards −z.  Parts that do not move against each other are
 * merged into one mesh per material; the low graphics preset leaves out the
 * small ones (ladder, railings, the finer lattice).  Generic cranes, no real
 * model is reproduced.
 */
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "render/fleet.h"
#include "sim/crane.h"

namespace fs {

struct CarVisualState;

/** what the cranes are drawn with: everything, or the plain parts only (the low graphics preset) – 'full' | 'low' */
using CraneDetail = std::string;

namespace crane_render {
/** (the original's file-local `Line`: a rope stretched from one point to another each picture – fs::Line is three.js's) */
class Line;
}  // namespace crane_render

/** What every crane model can do beyond a vehicle's: be posed from its simulation, and say where its operator's eyes are. */
class CraneModel : public VehicleModel {
 public:
  const CraneType kind;

  /** joints, outriggers, lift, levelling, hook and rope – as the crane is now */
  virtual void pose(const Crane& c, double now) = 0;
  /** the operator's eyes (world): in the crane's cab, or beside the truck with the radio control */
  virtual Vector3& operatorEye(Vector3& out) = 0;

 protected:
  CraneModel(CraneType kind_, const Vector3& eye_) : VehicleModel(eye_), kind(std::move(kind_)) {}
};

class TowerCraneModel : public CraneModel {
 public:
  explicit TowerCraneModel(const CraneDetail& detail = "full");
  ~TowerCraneModel() override;

  void setCabinOpacity(double opacity) override {}
  void update(const CarVisualState& s, double dt, double now) override;
  /** The obstruction lights flash. */
  void flash(double now);
  void pose(const Crane& c, double now) override;
  Vector3& operatorEye(Vector3& out) override;

 private:
  std::shared_ptr<Group> tip;
  std::shared_ptr<Group> inner;
  /** what turns with the jib */
  const std::shared_ptr<Group> slewing = make<Group>();
  const std::shared_ptr<Group> trolley = make<Group>();
  std::shared_ptr<Group> hook;
  std::vector<std::unique_ptr<crane_render::Line>> falls;
  std::unique_ptr<crane_render::Line> jibRope;
  /** the hoist rope from the jib's root sheave to the trolley */
  std::unique_ptr<crane_render::Line> hoistRope;
  const std::shared_ptr<Group> rotor = make<Group>();
  std::vector<std::shared_ptr<Mesh>> lights;
  const Vector3 cabEye;
};

class MobileCraneModel : public CraneModel {
 public:
  explicit MobileCraneModel(const CraneDetail& detail = "full");
  ~MobileCraneModel() override;

  void setCabinOpacity(double opacity) override {}
  void update(const CarVisualState& s, double dt, double now) override;
  void pose(const Crane& c, double now) override;
  Vector3& operatorEye(Vector3& out) override;

 private:
  std::shared_ptr<Group> tip;
  std::shared_ptr<Group> inner;
  /** the carrier, lifted and levelled by the jacks */
  const std::shared_ptr<Group> body = make<Group>();
  const std::shared_ptr<Group> upper = make<Group>();
  const std::shared_ptr<Group> boom = make<Group>();
  std::vector<std::shared_ptr<Group>> sections;
  const std::shared_ptr<Group> head = make<Group>();
  std::shared_ptr<Mesh> cylHousing;
  std::shared_ptr<Mesh> cylRod;
  /** the hoist rope from the winch's drum to the boom head */
  std::unique_ptr<crane_render::Line> winchRope;
  std::vector<std::shared_ptr<Group>> wheels;
  std::vector<std::shared_ptr<Group>> beams;
  std::vector<std::shared_ptr<Mesh>> rods;
  std::vector<std::shared_ptr<Group>> pads;
  std::vector<std::shared_ptr<Mesh>> mats;
  std::shared_ptr<Group> hook;
  std::vector<std::unique_ptr<crane_render::Line>> falls;
  Lamps lamps;
  std::vector<std::shared_ptr<Mesh>> beacons;
  /** the wheels' heights as the physics has them (they hang lower once the jacks lift the carrier) */
  std::vector<double> wheelY;
  bool operating = false;
};

class LoaderCraneModel : public CraneModel {
 public:
  explicit LoaderCraneModel(const CraneDetail& detail = "full");
  ~LoaderCraneModel() override;

  void setCabinOpacity(double opacity) override {}
  void update(const CarVisualState& s, double dt, double now) override;
  void pose(const Crane& c, double now) override;
  Vector3& operatorEye(Vector3& out) override;

 private:
  /** Place a cylinder (housing and rod) between two points of its parent's frame. */
  static void ram(const std::array<std::shared_ptr<Mesh>, 2>& parts, const Vector3& a, const Vector3& b, double housing);

  std::shared_ptr<Group> tip;
  std::shared_ptr<Group> inner;
  const std::shared_ptr<Group> body = make<Group>();
  const std::shared_ptr<Group> column = make<Group>();
  const std::shared_ptr<Group> innerBoom = make<Group>();
  const std::shared_ptr<Group> outerBoom = make<Group>();
  std::vector<std::shared_ptr<Group>> ext;
  std::array<std::shared_ptr<Mesh>, 2> liftCyl;
  std::array<std::shared_ptr<Mesh>, 2> foldCyl;
  std::vector<std::shared_ptr<Group>> wheels;
  std::vector<std::shared_ptr<Group>> beams;
  std::vector<std::shared_ptr<Mesh>> rods;
  std::vector<std::shared_ptr<Group>> pads;
  std::shared_ptr<Group> hook;
  std::unique_ptr<crane_render::Line> link;
  Lamps lamps;
};

/** The model of a crane for its kind. */
std::unique_ptr<CraneModel> makeCraneModel(const CraneType& type, const CraneDetail& detail);

}  // namespace fs
