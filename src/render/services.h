// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/services.ts @ v1.7.7
/**
 * The fuel stations and car washes as they are drawn: built from the
 * layout of each site (world/services.h) when the player comes near, and
 * taken away again far behind.
 *
 * A fuel station: the canopy – a slab of the outline of the real one, its
 * edge in a colour of the station's own with its name on the long sides,
 * lights in its underside, carried by columns that stand on the islands –,
 * the pump islands (a raised kerb, a yellow post at each end) with a pump
 * on each: a white cabinet, a display on both faces, two nozzles in their
 * holsters on each face with their hoses hanging from the top, and the
 * price sign at the road.  While the player fills up, one nozzle leaves its
 * holster for the vehicle's filler, its hose following, and the display of
 * that pump counts the litres and the price.
 *
 * A car wash: its hall (walls along both sides, open at the ends) or its
 * roof on four columns, or a concrete bay in the open – and the gantry: a
 * portal on rails that travels over the car with two brushes standing at
 * the sides and one lying across the top, all of them turning, spraying
 * foam and water.
 *
 * The parts that do not move are merged per material: a station is a
 * handful of draw calls.  Everything is made of plain shapes and canvas
 * pictures – no real brand's design is copied: the colours come from the
 * name.
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "platform/canvas.h"
#include "world/services.h"

namespace fs {

/** A pump as drawn: where it stands, which way its faces look, where its nozzles hang. */
struct PumpView {
  /** the foot of the pump (world) and the island's direction (u) and across (v) */
  double x = 0;
  double y = 0;
  double z = 0;
  double ux = 0;
  double uz = 0;
  /** its display on each face (swapped for the counting one) */
  std::vector<std::shared_ptr<Mesh>> displays;
};

/** The animated parts of a car wash. */
struct GantryView {
  std::shared_ptr<Group> group;
  std::shared_ptr<Mesh> brushL, brushR, brushTop, lampRed, lampGreen;
  /** the bay's frame and the ground under its middle */
  svc::Rect rect;
  double y = 0;
  /** how far along the bay the gantry can travel (±), and how far its posts stand from the middle */
  double reach = 0;
  double postV = 0;
};

/** What is drawn of one site. */
class SiteView {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  const svc::ServiceSite& site;
  std::vector<PumpView> pumps;
  /** for the collisions: the outlines of islands and walls (flat x, z rings) and the posts (x, y, z, r) */
  std::vector<std::vector<double>> rings;
  std::vector<double> discs;
  std::optional<GantryView> gantry;
  /** the colour of the site's own */
  const uint32_t colour;

  SiteView(const svc::ServiceSite& site, const std::function<double(double, double)>& ground, bool shadows);
  SiteView(const SiteView&) = delete;
  SiteView& operator=(const SiteView&) = delete;

  /**
   * Where the gantry is (−1 … 1 along the bay), which way its front faces
   * (1: towards −u, where a car comes in that drives along u), how far its
   * side brushes are in towards the car (0 apart … 1 at its sides), the top
   * brush's height over the floor (0: up out of the way), how far they turn
   * this frame (rad) – and its light (−1 red, 1 green, 0 off).
   */
  void placeGantry(double at, double face, double close, double topY, double spin, double light);
  /** The middle of the gantry's beam (world): where the water comes from. */
  Vector3& gantryAt(Vector3& out) const;
  void dispose();

 private:
  class Parts;
  double lx(double x) const { return x - site.x; }
  double lz(double z) const { return z - site.z; }
  void canopy(Parts& parts, const svc::CanopyPlan& c, const std::function<double(double, double)>& ground);
  void pump(Parts& parts, double x, double y, double z, double ux, double uz);
  void pylon(const std::array<double, 3>& sign, const std::function<double(double, double)>& ground);
  void wash(Parts& parts, const svc::WashPlan& w, const std::function<double(double, double)>& ground);
};

/** The nozzle in a vehicle's filler, its hose back to the pump, and the counting display. */
class FillView {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  FillView();

  /**
   * Show the nozzle `t` of the way (0 in its holster … 1 in the filler) from
   * the pump on the given side (−1/1 across the island) to the filler (world
   * point), its grip in the fuel's colour.
   */
  void show(PumpView& pump, double side, const Vector3& filler, double t, const std::string& fuel);
  /** The counter on the pump's display. */
  void count(double litres, double cost);
  void hide();

 private:
  std::shared_ptr<Group> nozzle_;
  std::shared_ptr<Mesh> hose_;
  MaterialPtr display_;
  Canvas canvas_;
  TexturePtr tex_;
  PumpView* pump_ = nullptr;
  std::string last_;
};

/** Foam and water from the gantry: a cloud of dots thrown down and out round the car. */
class SprayView {
 public:
  std::shared_ptr<Points> points;

  SprayView();

  /**
   * Run for dt: `rate` new dots a second from the beam over (x, y, z), across
   * the direction (vx, vz) – foam (white, slow) or water (blue, quick).
   */
  void update(double dt, double x, double y, double z, double vx, double vz, double rate, bool foam);
  void stop();

 private:
  static constexpr int N = 320;
  std::vector<float> vel_;
  std::vector<float> life_;
  AttrPtr pos_;
  AttrPtr col_;
  int next_ = 0;
};

}  // namespace fs
