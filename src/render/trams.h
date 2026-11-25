// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Trams and trains as seen from the road: each car a simple lofted body
 * (skirt, window band, roof), bogies, bellows between the cars, a
 * pantograph on trams.  Generic shapes and liveries – no real vehicle is
 * reproduced.  Where they are and how they move is decided in sim/rail.ts.
 *
 * With the detail on (`RailActors.detail`: medium graphics and above) every
 * body has double plug doors on both sides.  Shut they are drawn as part of
 * the body; while they move and stand open the body with its doorways is
 * drawn instead, and the leaves – meshes of their own – come out of the wall
 * and slide apart.  Without the detail the bodies are the plain ones.
 *
 * Origin: src/render/trams.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/engine.h"
#include "sim/rail.h"

namespace fs {

/** A passenger door of a body: the middle of its opening (metres from the middle of the body, negative towards its front), the side (-1 left, 1 right), the width of the opening. */
struct DoorPlace {
  double z = 0;
  int side = 1;
  double width = 0;
};

/**
 * The passenger doors of a body (sim/rail.ts carLayout): two double doors
 * on each side, a quarter of the body's length before and behind its middle
 * – just behind the cab on an end section, evenly spread on a middle
 * section and on a trailer car.  (render/transit.ts sends the people at the
 * stops to the same places.)  Only doors that fit beside the slanted cab
 * ends and beside each other are there.
 */
std::vector<DoorPlace> carDoors(const CarRole& role);

class RailActors {
 public:
  RailActors();
  RailActors(const RailActors&) = delete;
  RailActors& operator=(const RailActors&) = delete;

  const std::shared_ptr<Group> group;

  /**
   * Doors on the bodies, opening and shutting (true, the default) – or the
   * plain bodies (false: the low graphics setting).  Changing it builds
   * what is shown anew (at the next update – the original at once, with what it showed last).
   */
  bool detail() const { return detailOn_; }
  void detail(bool on);

  /** the vehicle the player rides in (its outside is not drawn: they sit inside it) */
  const RailVehicle* hidden = nullptr;

  /** Show the vehicles where the simulation has them (`lines`: the lines, for the displays at their fronts). */
  void update(const std::vector<std::unique_ptr<RailVehicle>>& vehicles, const std::vector<RailLine>& lines = {});
  /** … the same of vehicles held elsewhere (a replay's: game/replay.h) */
  void update(const std::vector<const RailVehicle*>& vehicles, const std::vector<RailLine>& lines = {});

  void dispose();

 private:
  /** The doors of a car body as drawn: the body with them shut and with its doorways, and the four groups of leaves (LEAF_GROUPS). */
  struct CarDoors {
    GeometryPtr shut;
    GeometryPtr open;
    std::array<std::shared_ptr<Mesh>, 4> leaves;
  };
  struct CarParts {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Mesh> arm;
    std::shared_ptr<Mesh> head;
    std::optional<CarDoors> doors;
    std::shared_ptr<Mesh> display;
  };
  struct Shown {
    std::shared_ptr<Group> g;
    std::vector<CarParts> cars;
    double doorsAt = -1;
  };

  MaterialPtr material_;
  std::unordered_map<std::string, GeometryPtr> geometries_;
  /** the bodies with doors and the leaves (let go when the detail is switched off) */
  std::unordered_map<std::string, GeometryPtr> doorGeometries_;
  std::map<int, Shown> shown_;
  bool detailOn_ = true;
  GeometryPtr pantoArm_;
  GeometryPtr pantoHead_;
  /** the destination displays: one texture per text, and what each vehicle shows */
  std::unordered_map<std::string, MaterialPtr> displays_;
  std::unordered_map<int, std::string> showing_;
  GeometryPtr displayGeo_;

  GeometryPtr carGeometry(const Consist& c, int kind, const CarRole& role, bool coupler, const std::string& mode = "none");
  GeometryPtr leafGeometry(const Consist& c, int kind, const CarRole& role, int side, int way);
  MaterialPtr displayMaterial(const std::string& text);
  static void placeLeaves(Shown& s, double open);
};

}  // namespace fs
