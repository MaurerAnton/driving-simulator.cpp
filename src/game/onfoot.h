// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/onfoot.ts @ v1.7.7
/**
 * On foot, beside the vehicle: before the drive (with the start "beside the
 * vehicle") or after getting out – or without any vehicle, as a pedestrian
 * in the town (then nothing holds one near anything).
 *
 * Whoever drives off has looked at the vehicle first – the check before the
 * drive (Abfahrtkontrolle: part of the test for the classes C and CE, and
 * what every driver is responsible for, § 23 Abs. 1 StVO): the tyres, the
 * lights at the front and at the back, with a trailer the coupling.  Here
 * that is done by walking round the vehicle and looking at each of them for
 * a moment; rings on the ground show where.  Then to the driver's door: open
 * it, sit in, close it – and drive.
 *
 * Walking: forwards and back, turning, looking about – with the keys, the
 * pedals and the steering bar on the screen, a gamepad, the mouse or a
 * finger.  Never through the vehicle, a wall or a post, and not further
 * than a few dozen metres from the vehicle.
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"

namespace fs {

/** 'tyres' | 'front' | 'rear' | 'coupling' */
using CheckId = std::string;
extern const std::vector<CheckId> CHECK_ORDER;

/** One thing to look at: a point on the vehicle, and from how near it is seen. */
struct CheckSpot {
  CheckId id;
  double x = 0;
  double y = 0;
  double z = 0;
  /** where its ring lies on the ground: beside the vehicle, where one stands to look */
  double rx = 0;
  double rz = 0;
  double reach = 0;
  /** seconds it has been looked at; done once that was long enough */
  double seen = 0;
  bool done = false;
};

/** A part of the vehicle in the plan: its model origin, heading and outline. */
struct WalkUnit {
  double x = 0;
  double z = 0;
  double yaw = 0;
  /** metres ahead of the origin to the front, behind it to the rear, half the width */
  double front = 0;
  double rear = 0;
  double hw = 0;
  /** the axles: metres ahead (+) of or behind (−) the origin, and half the track over the tyres */
  std::vector<double> axles;
  double track = 0;
};

/** The vehicle as somebody on foot meets it. */
struct WalkVehicle {
  struct Point {
    double x = 0;
    double y = 0;
    double z = 0;
  };
  /** the vehicle itself and, if there is one, the trailer behind it */
  std::vector<WalkUnit> units;
  double y = 0;
  /** how far ahead of the origin of the first unit the driver's door is, and on which side (−1 left, the default; 1 right: a bus) */
  double door = 0;
  std::optional<double> doorSide;
  /** the coupling (world), with a trailer */
  std::optional<Point> hitch;
};

/** eye height (m) */
inline constexpr double EYE = 1.62;
/** walking pace (m/s) */
inline constexpr double WALK = 1.45;
/** running, unless the settings say otherwise (m/s: 12 km/h) */
inline constexpr double RUN = 3.3;
/** from this far away the vehicle is behind one (m): the world loads round the person, the hint says how far it is */
inline constexpr double TETHER = 60;

/** What there is to check on a vehicle: every tyre, the lights at the front and at the very back, the coupling. */
std::vector<CheckSpot> checkSpots(const WalkVehicle& v);

struct WalkMove {
  double fwd = 0;
  double turn = 0;
  bool run = false;
  /** a step to the right (+) or left, −1 … 1 (game/walkmodes: the classic way of walking) */
  std::optional<double> side;
};

struct WalkLook {
  double yaw = 0;
  double pitch = 0;
  /** moving sideways and forwards by dragging with two fingers (in "views": about four metres each) */
  double right = 0;
  double fwd = 0;
};

class OnFoot {
 public:
  struct Start {
    double x = 0;
    double y = 0;
    double z = 0;
    double yaw = 0;
  };
  struct Spot {
    double x = 0;
    double z = 0;
  };
  struct Progress {
    CheckId id;
    int done = 0;
    int of = 0;
  };
  struct Result {
    bool step = false;
    /** the spot checked in this step (null: none) */
    const CheckSpot* checked = nullptr;
  };

  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  double pitch = -0.12;
  std::vector<CheckSpot> spots;
  /** too far from the vehicle a moment ago */
  bool tooFar = false;
  /** how fast running is (m/s) – the settings say */
  double runSpeed = RUN;
  /** the speed of the last step (m/s), and whether it was run */
  double speed = 0;
  bool running = false;
  /** metres walked in all (for the gait of the figure) */
  double distance = 0;

  /**
   * Beside the driver's door of `vehicle`, looking along it – or, without a
   * vehicle, at `start`.
   */
  explicit OnFoot(std::optional<WalkVehicle> vehicle, std::optional<Start> start = std::nullopt);

  /** Where one stands to get in: beside the driver's door (where one stands, without a vehicle). */
  Spot doorSpot() const;
  /** Close enough to the door to open it. */
  bool atDoor() const;
  /** How many of each kind of thing there is to check, and how many are done. */
  std::vector<Progress> progress() const;
  bool allChecked() const;
  /** The kinds of things not checked yet. */
  std::vector<CheckId> missing() const;

  /**
   * One step.  `ground(x, z, yRef)`: the height to stand on there,
   * `blocked(x, y, z, r)`: whether something stands there (a wall, a post).
   * Returns what changed: a step was taken (for its sound), something was
   * checked.
   */
  Result update(double dt, const WalkMove& move, const WalkLook& look, const std::function<double(double x, double z, double yRef)>& ground,
                const std::function<bool(double x, double y, double z, double r)>& blocked);

 private:
  std::optional<WalkVehicle> vehicle;
  double walked = 0;
};

/** The rings on the ground that show what there is to look at: amber until it is checked, then green. */
class CheckMarkers {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  CheckMarkers();
  /** Lay the rings for these spots; `ground(x, z)` is the height there. */
  void show(const std::vector<CheckSpot>& spots, const std::function<double(double x, double z)>& ground);
  void update(const std::vector<CheckSpot>& spots);
  void hide();

 private:
  MaterialPtr open;
  MaterialPtr done;
  GeometryPtr ring;
  std::vector<std::shared_ptr<Mesh>> meshes;
};

}  // namespace fs
