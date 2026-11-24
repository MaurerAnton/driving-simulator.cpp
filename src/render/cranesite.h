// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/cranesite.ts @ v1.7.7
/**
 * The construction site the cranes work on (game/cranetasks.ts lays it out):
 * gravel and asphalt draped over the field of the practice yard, the
 * building going up – slabs, columns, walls, the stair core, the loading
 * platform, the guard rails and the scaffolding –, the tower crane beside
 * it (when it is not the player's), the delivery truck on the site road,
 * the site office and the store at the depot, the fence, the stands marked
 * out for the cranes – and the loads: pallets of bricks, steel beams, a
 * container, formwork, reinforcing mesh, each where the work has put it,
 * with its slings when it hangs on the hook, the load of the task marked
 * and the place it goes lit up.
 *
 * Nothing of the map is changed: like the practice yard of the truck
 * lessons (render/yard.ts) the site is a few meshes following the ground,
 * shown for as long as a crane works there.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/engine.h"
#include "game/cranetasks.h"
#include "render/crane.h"

namespace fs {

namespace cranesite_detail {
/** (the original's file-local `Leg`: a sling's leg) */
class Leg;
}  // namespace cranesite_detail

/** `{ load, zone }`: the load of the task now and the place it goes */
struct CraneSiteTarget {
  std::string load;
  WorldZone zone;
};

/**
 * The construction site.  `show` lays it out once the ground there is
 * loaded (call it again until it says so), `update` puts the loads where
 * the work has them, `hide` takes it all away.
 */
class CraneSiteScene {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  CraneSiteScene();
  ~CraneSiteScene();
  CraneSiteScene(const CraneSiteScene&) = delete;
  CraneSiteScene& operator=(const CraneSiteScene&) = delete;

  /**
   * Lay the site out over the ground: `heightAt` is the map's ground (NaN
   * where it is not loaded yet – then it returns false: call again), `y0`
   * the base height of the building, `towerToo` whether the site's tower
   * crane is to be drawn here (not when it is the player's).
   */
  bool show(const SitePlacement& p, const std::function<double(double, double)>& heightAt, double y0, bool towerToo, const CraneDetail& detail);

  /** The site's tower crane (when it stands there): its obstruction lights flash. */
  void tick(double now);

  /**
   * Put the loads where the work has them; `hook`: the hook's bottom (world)
   * for the slings of the load on it (null: none); `target`: the load of the task now and
   * the place it goes (marked; null: none).
   */
  void update(const std::vector<WorldLoad>& loads, const Vector3* hook, const CraneSiteTarget* target, double now);

  /** Timber bearers lying on the ground (world boxes, from the ground to their top). */
  void addBearers(const std::vector<CraneBoxY>& list);

  void hide();

 private:
  /** One load as it is drawn: its mesh, the bearers under it, the slings when it hangs. */
  struct DrawnLoad {
    std::shared_ptr<Group> group;
    std::shared_ptr<Group> mesh;
    std::shared_ptr<Group> bearers;
    std::vector<std::unique_ptr<cranesite_detail::Leg>> legs;
  };

  /** The place a load goes: a green rectangle the load's size on what it lies on, and a pillar of light above it (seen from afar). */
  void showZone(const WorldZone* zone, const LoadDef* def);
  void clear();

  /** the tower crane standing on the site when the player works another crane (null: the player's own is there) */
  std::unique_ptr<TowerCraneModel> tower;
  std::vector<GeometryPtr> made;
  std::unordered_map<std::string, DrawnLoad> loads;
  std::shared_ptr<Group> zoneMesh;
  std::string zoneFor;
  std::shared_ptr<Mesh> aim;
  Vector3 tmpA;
  Vector3 tmpB;
  CraneDetail detail = "full";
  /** the map's ground (for a place on the ground) */
  std::function<double(double, double)> heightAt = [](double, double) { return 0.0; };
};

}  // namespace fs
