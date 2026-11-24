// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/spawnplace.ts @ v1.7.7
/**
 * Where a vehicle may be put down: the reckoning behind "a car here"
 * (game/spawncar.ts), behind a start at the very place chosen ("place
 * precisely") and behind the vehicle standing behind the main menu.
 *
 * A vehicle can only be put where the world is in memory – the tiles that
 * are loaded round the player (`LoadedArea`, drawn on the large map as a
 * line round them) – and only where it stands free: not in a wall (the
 * collider says that) and not inside a building (the footprints of the
 * buildings say that: the collider only knows their walls).  When the place
 * asked for is not free, the nearest free one round it is taken.
 *
 * And a tap on the 3D picture: where the ray from the eye through the finger
 * meets the ground, and whether it meets a vehicle standing there first.
 * Everything here is plain arithmetic, tested in tests/game_spawnplace_test.cpp.
 *
 * (The original's `exactSpots` – a WeakSet that marks the start places chosen
 * "precisely" by the object they are – has no counterpart here: a start place
 * is a value in C++, not an object with an identity.  Whoever keeps the start
 * place keeps that mark beside it.)
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs {

/** A square tile of the world that is loaded: its centre and side (m). */
struct TileBox {
  double cx = 0;
  double cz = 0;
  double size = 0;
};

/** The outer sides of a set of square tiles of one grid (x1, z1, x2, z2 per side). */
std::vector<float> tileOutline(const std::vector<TileBox>& tiles);

/** The tiles in memory: where a vehicle may be put, and the line round them for the map. */
class LoadedArea {
 public:
  void set(const std::string& key, const TileBox& box, bool loaded);
  void clear();
  size_t size() const { return tiles_.size(); }

  /** Is (x, z) on a loaded tile, at least `margin` metres inside the edge of the loaded area? */
  bool has(double x, double z, double margin = 0) const;

  /** The line round the loaded area: the sides of the tiles that no other loaded tile shares (x1, z1, x2, z2 per side). */
  const std::vector<float>& outline();

 private:
  /** in the order they came (the order the line round them is given in) */
  std::vector<std::pair<std::string, TileBox>> tiles_;
  std::optional<std::vector<float>> outlineCache_;

  bool on(double x, double z) const;
};

/** Is (x, z) inside the ring that starts at `o` (x, z pairs) and has `n` corners? */
template <class Pts>
bool inRing(double x, double z, const Pts& pts, size_t o, size_t n) {
  bool inside = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double xi = pts[o + i * 2];
    const double zi = pts[o + i * 2 + 1];
    const double xj = pts[o + j * 2];
    const double zj = pts[o + j * 2 + 1];
    if ((zi > z) != (zj > z) && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}

/** The footprints of the buildings of the loaded tiles (the rings of their walls): is a point inside one? */
class Footprints {
 public:
  /**
   * The rings of a tile: `pts` their corners (x, z, …), `counts` how many corners each has – any
   * arrays of numbers (a tile's sections); null, or no ring at all: the tile has none.
   */
  template <class Pts, class Counts>
  void set(const std::string& key, const Pts* pts, const Counts* counts) {
    if (!pts || !counts || counts->size() == 0) {
      tiles_.erase(key);
      return;
    }
    std::vector<double> p(pts->size());
    for (size_t i = 0; i < p.size(); i++) p[i] = static_cast<double>((*pts)[i]);
    std::vector<uint32_t> c(counts->size());
    for (size_t i = 0; i < c.size(); i++) c[i] = static_cast<uint32_t>((*counts)[i]);
    setRings(key, std::move(p), std::move(c));
  }

  void remove(const std::string& key) { tiles_.erase(key); }

  /** Is (x, z) inside a building? */
  bool inside(double x, double z) const;

 private:
  struct TileRings {
    std::vector<double> pts;
    std::vector<uint32_t> counts;
    /** per ring: its first number in `pts`, and its box (x0, z0, x1, z1) */
    std::vector<uint32_t> start;
    std::vector<float> box;
    /** of all rings: x0, z0, x1, z1 */
    double all[4] = {0, 0, 0, 0};
  };
  std::unordered_map<std::string, TileRings> tiles_;

  void setRings(const std::string& key, std::vector<double> pts, std::vector<uint32_t> counts);
};

/** The outline of a vehicle in the plan, from its model origin (as VehicleDef.outline). */
struct Outline {
  double front = 0;
  double rear = 0;
  double hw = 0;
};

/**
 * The points of a vehicle's plan that must not lie inside a building: its
 * middle, the middle of its front and of its rear, and its four corners.
 */
std::vector<double> outlinePoints(double x, double z, double yaw, const Outline& o);

/** Does the plan of a vehicle standing at (x, z), facing yaw, reach into a building? */
bool inBuilding(double x, double z, double yaw, const Outline& o, const std::function<bool(double x, double z)>& inside);

/** A place to stand: where, and facing which way. */
struct FreePlace {
  double x = 0;
  double z = 0;
  double yaw = 0;
};

/**
 * The free place nearest to (x, z): (x, z) itself if it is free, else the
 * first free one on rings round it, `step` metres apart, out to `maxR` – the
 * vehicle facing `yaw` (or the other way, if only that fits).  Null: none.
 */
std::optional<FreePlace> freeNear(double x, double z, double yaw, const std::function<bool(double x, double z, double yaw)>& free, double maxR = 40,
                                  double step = 1.5);

/**
 * A place beside somebody standing at (x, z) facing yaw, for a car to be put
 * at: a few steps to the right, else to the left, ahead, behind – the car
 * facing the same way – else the nearest free place round them.
 */
std::optional<FreePlace> besidePlace(double x, double z, double yaw, const std::function<bool(double x, double z, double yaw)>& free, double maxR = 30);

/** A point of the ground. */
struct GroundPoint {
  double x = 0;
  double y = 0;
  double z = 0;
};

/**
 * Where a ray (from the eye o, along d) first meets the ground (heightAt:
 * NaN where the ground is not known) – within `max` metres.  Null: it does not.
 */
std::optional<GroundPoint> rayGround(double ox, double oy, double oz, double dx, double dy, double dz, const std::function<double(double x, double z)>& heightAt,
                                     double max = 800);

/** A vehicle standing: its plan (from its model origin at x, y, z, facing yaw) and its height. */
struct StandingBox : Outline {
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  double height = 0;
};

/** How far along a ray (from o, along d – of any length) it meets a standing vehicle's box; −1: it does not. */
double rayBox(double ox, double oy, double oz, double dx, double dy, double dz, const StandingBox& b);

}  // namespace fs
