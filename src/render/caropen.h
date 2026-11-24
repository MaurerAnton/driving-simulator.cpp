// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The parts of the player's car that open: the four side doors, the bonnet
 * and the tailgate (game/cardoors.ts has the logic of them).
 *
 * The body is one shell (render/carBody.ts).  The parts are cut out of it
 * along their panel lines – the ones the finer model draws as gaps
 * (render/cardetail.ts): each door from the sill up to the edge of the
 * roof, with its window, the rear door round the wheel arch; the bonnet
 * between the top of the wings, from the scuttle to the nose; the tailgate
 * with the rear window, from the roof down over the tail.  A triangle that
 * a panel line crosses is split along it (its normals taken across as they
 * are), so the pieces show exactly the surface the whole shell did.  What
 * is fixed to a part goes with it: the door panels inside, the door
 * mirrors, and on "very high" graphics the handles, seals and switches.
 *
 * Each part hangs on its hinge: a door at its front edge (it swings out
 * about 65°), the bonnet at the scuttle, the tailgate at the roof (both
 * lift).  Behind an opened part the opening shows its edges dark, the
 * bonnet an engine bay.
 *
 * Cheap while shut: as long as every part is shut, nothing here is drawn –
 * the car is drawn as it always was, from its whole meshes.  Only once a
 * part opens do those meshes take the shell without the parts, and the
 * parts are drawn on their hinges.
 *
 * Origin: src/render/caropen.ts @ v1.7.7
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "engine/engine.h"
#include "render/carBody.h"

namespace fs {

class Mirror;
struct DoorTrim;
class Soup;

/** What of the player's car the parts are taken from (render/car.ts PlayerCar). */
struct OpeningsHost {
  std::shared_ptr<Group> root;
  struct Exterior {
    std::shared_ptr<Group> group;
    const CarShell* shell = nullptr;
    const CarDims* dims = nullptr;
  } exterior;
  std::shared_ptr<Group> cockpit;
  std::vector<Mirror*> mirrors;
  /** the door panels inside: their material's mesh as a whole, without them, and each door's own (fl fr rl rr) */
  const std::vector<DoorTrim>* doorTrim = nullptr;
  /** take a new mesh into the see-through cabin (the original's `register`, a word C++ keeps to itself) */
  std::function<void(const std::shared_ptr<Mesh>& mesh)> register_;
};

// ------------------------------------------------------------------ panel lines ----

/** [t, value] */
using P2 = std::array<double, 2>;

/** Where the parts end: the lines of the finer model's panel gaps (render/cardetail.ts). */
struct PanelLines {
  /** the front doors from zFD to zB, the rear doors from zB to zRD below the window and to zRW beside it, all above `sill` */
  double zFD = 0;
  double zB = 0;
  double zRD = 0;
  double zRW = 0;
  double sill = 0;
  /** the rear door's edge round the wheel arch: [y, z] from the sill up */
  std::vector<P2> arch;
  /** the bonnet from bz0 to bz1, out to its edge [z, x] */
  double bz0 = 0;
  double bz1 = 0;
  std::vector<P2> bonnetEdge;
  /** the tailgate from tz0 back, out to its edge [z, x] behind the rear window, down to `tailSill` on the tail */
  double tz0 = 0;
  std::vector<P2> tailEdge;
  double tailSill = 0;
  double zTail = 0;
  double zRear = 0;
  double belt = 0;
};

PanelLines panelLines(const CarShell& sh, const CarDims& d);

/**
 * Which part a point of the shell belongs to (−1: the body).  `k`: the
 * segment of the outline it lies on, `cap`: on the flat face of the tail.
 */
int shellPart(const PanelLines& L, double x, double y, double z, int k, bool cap);

class CarOpenings {
 public:
  explicit CarOpenings(const OpeningsHost& host);
  ~CarOpenings();
  CarOpenings(const CarOpenings&) = delete;
  CarOpenings& operator=(const CarOpenings&) = delete;

  /** The finer model's meshes (render/cardetail.ts): what lies on a part goes with it. */
  void addDetail(const std::vector<ObjectPtr>& objects);

  /** How far each part is open (fl fr rl rr bonnet boot), 0 … 1. */
  void pose(const std::vector<double>& values);

 private:
  struct Swap {
    std::shared_ptr<Mesh> mesh;
    GeometryPtr whole;
    GeometryPtr body;
    std::vector<std::shared_ptr<Mesh>> parts;
  };
  struct Show {
    ObjectPtr obj;
    int part;
  };
  struct Skin {
    Soup* soup;
    std::vector<int8_t> region;
  };

  /** Which part each triangle of the cut shell lies on (−1: the body). */
  std::vector<int8_t> classifyShell(const Soup& soup);
  int shellAt(const std::array<double, 3>& a, const std::array<double, 3>& b, const std::array<double, 3>& c);
  /** Which part a point of the shell belongs to. */
  int pointPart(double x, double y, double z, bool cap);
  /** A part's own mesh of a geometry, like `src` (material, shadow, order; in the see-through cabin if it is). */
  std::shared_ptr<Mesh> partMesh(const std::shared_ptr<Mesh>& src, const GeometryPtr& geo, int part);
  void addSwap(const std::shared_ptr<Mesh>& mesh, Soup& soup, const std::vector<int8_t>& region);
  /** A mesh fixed to the body or to a part: each triangle goes where its middle lies (not cut). */
  void addVolume(const std::shared_ptr<Mesh>& mesh);
  /** Which part a point fixed to the car lies on: on its skin, or (a door) on the panel inside it. */
  int volumeAt(double x, double y, double z);
  /** The edges of the cut skin: round each part, and round each opening in the body (for the dark strips). */
  void collectEdges(const std::vector<Skin>& skins, std::vector<std::vector<double>>& partEdges, std::vector<std::vector<double>>& bodyEdges);
  /** Dark strips from edges into the car (along each end's normal, inwards): the cut edge of a sheet of metal or of a frame. */
  GeometryPtr strips(const std::vector<double>& edges);
  /** Under the bonnet: a dark bay, the engine, the battery, a reservoir with a yellow cap, the radiator at the front. */
  std::shared_ptr<Group> engineBay();
  void poseNow();

  OpeningsHost host_;
  PanelLines L_;
  std::function<std::vector<std::array<double, 2>>(double z)> profile_;
  std::vector<std::shared_ptr<Group>> pivots_;
  std::vector<std::shared_ptr<Group>> holders_;
  std::vector<Swap> swaps_;
  /** what is shown only while its part is open: the dark edges of the opening, the engine bay */
  std::vector<Show> shows_;
  std::array<double, 6> values_ = {0, 0, 0, 0, 0, 0};
  bool split_ = false;
  std::vector<double> ds_ = std::vector<double>(6, 0.0);
};

}  // namespace fs
