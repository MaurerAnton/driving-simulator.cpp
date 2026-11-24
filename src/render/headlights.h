// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Headlights that light the road at dusk and at night.
 *
 * `PlayerLights`: the player's vehicle throws its dipped beam – short,
 * aimed down and a little to the right, so that it reaches further along the
 * right-hand verge than into the oncoming lane (the German pattern) – and,
 * switched on, its main beam: long and wide.  With the "medium" graphics and
 * up these are two real spotlights (no shadows); they are added to the scene
 * only once it first gets dark, so a game played by day pays nothing for
 * them.  On the "low" graphics a pool of light lies on the road instead: a
 * flat shape drawn over the ground in front of the vehicle, glowing.
 *
 * `TrafficGlow`: the lamps of the traffic's cars at night – white in front,
 * red behind, brighter when braking – as glowing points that light nothing
 * (one draw for all of them).  A car with its main beam on, or flashing it,
 * glows large: that is what dazzles.
 *
 * Origin: src/render/headlights.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "engine/engine.h"

namespace fs {

/** what the player's lights need each frame */
struct PlayerLightState {
  /** how dark it is, 0 day … 1 night (sim/beams darkness) */
  double dark = 0;
  /** the vehicle's model (its frame: x right, y up, forward −z, the origin on the ground) – null: no lights (on foot, flying) */
  Object3D* root = nullptr;
  /** from the origin to the front bumper (m) */
  double front = 0;
  /** height of the lamps (m) */
  double height = 0;
  bool low = false;
  bool high = false;
  /** the pool of light on the road instead of the spotlights */
  bool cheap = false;
};

class PlayerLights {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  explicit PlayerLights(const ObjectPtr& scene);

  void update(const PlayerLightState& s);

 private:
  const ObjectPtr scene_;
  bool added_ = false;
  const LightPtr dipped_;
  const LightPtr main_;
  Vector3 lamp_;
  Vector3 aim_;
  /** the pool of light of the low graphics: dipped and main beam */
  std::shared_ptr<Mesh> poolLow_;
  std::shared_ptr<Mesh> poolHigh_;
  const MaterialPtr poolMat_;
  MaterialPtr poolHighMat_;
  const std::shared_ptr<Group> poolFrame_ = make<Group>();
  Matrix4 tmpM_;
};

/** one car as its lamps need it */
struct GlowCar {
  int id = 0;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  double length = 0;
  double width = 0;
  bool braking = false;
};

/**
 * The lamps of the traffic at night: four glowing points a car (two white in
 * front, two red behind), each bright only from its own side.
 */
class TrafficGlow {
 public:
  std::shared_ptr<Points> points;

  TrafficGlow();

  /**
   * Places the lamps of `cars` for the camera: `dark` 0 … 1, `heightPx` the
   * picture's height in pixels, `high(id)` the car's main beam lit, `glare`
   * how much the player is dazzled (it swells the beams coming towards him).
   */
  void update(const std::vector<GlowCar>& cars, double dark, const Camera& camera, double heightPx, const std::function<bool(int id)>& high, double glare);

 private:
  // (the arrays of the original are those of the attributes)
  const AttrPtr pos_;
  const AttrPtr col_;
  const AttrPtr size_;
  const GeometryPtr geo_ = make<BufferGeometry>();
  const UniformPtr uScale_ = uniform(500.0);
};

}  // namespace fs
