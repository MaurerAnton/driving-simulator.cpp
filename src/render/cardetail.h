// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The player's car in finer detail – for the "very high" graphics only
 * (Graphics.vehicleDetail): built the first time it is switched on, and
 * hidden again (with the plain parts back) when it is switched off.
 *
 * Outside: panel gaps of the doors, the bonnet and the tailgate, door
 * handles, a black B pillar, rubber seals along the windows and a black band
 * round the windscreen, the scuttle under the windscreen with its two wipers,
 * headlamps and tail lamps that follow the curve of the body (housing,
 * reflectors and projectors, daytime running light, indicator, clear cover –
 * lit by the same materials as the plain lamps), lower air intakes with a
 * honeycomb mesh and fog lamps, number plates with their seals in a holder,
 * badges, an indicator repeater on each door mirror, the exhaust tip, the
 * roof antenna, the fuel flap, and the wheels: tyres with sidewalls and
 * tread, open alloy rims with nuts and a centre cap, brake discs and calipers.
 *
 * Inside: the instrument cluster in chrome-ringed dials with 3D needle hubs,
 * the steering wheel's airbag pad with its badge, buttons on the spokes,
 * grips and stitching, ribbed stalk ends, a shift pattern on the gear knob
 * (or the P R N D of an automatic beside the selector), the defroster grille
 * and tweeters on the dashboard, hazard and start buttons, the light switch,
 * a lock in the glove box, switches, lock pins, pockets and tweeters on the
 * doors, seat-belt buckles, sun-visor clips and mirror lids, grab handles,
 * buttons in the roof console, the sensor housing behind the rear-view mirror,
 * ribbed rubber on the pedals.
 *
 * Parts that share a material are merged into one mesh, the wheels are
 * drawn as instances: the whole of it costs a few dozen draw calls.  The
 * parts the driver can see are handed to the see-through cabin (`ghosts`).
 *
 * Origin: src/render/cardetail.ts @ v1.7.7
 */
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/engine.h"
#include "platform/canvas.h"

namespace fs {

class CarExterior;
class Mirror;
class WheelSet;
struct CarVisualState;

/** What the finer model is fixed to: the parts of the player's car (render/car.ts PlayerCar). */
struct CarParts {
  CarExterior* exterior = nullptr;
  std::shared_ptr<Group> cockpit;
  std::vector<Mirror*> mirrors;
  std::shared_ptr<Group> steering;
  std::shared_ptr<Group> lever;
  std::shared_ptr<Group> handbrake;
  std::vector<std::shared_ptr<Group>> pedals;
  std::shared_ptr<Group> stalk;
  Vector3 eye;
};

class CarDetail {
 public:
  /** the meshes the see-through cabin is to take in, with their layer (1: the controls in front) */
  std::vector<std::pair<std::shared_ptr<Mesh>, int>> ghosts;

  explicit CarDetail(const CarParts& car);
  ~CarDetail();
  CarDetail(const CarDetail&) = delete;
  CarDetail& operator=(const CarDetail&) = delete;

  /** What the finer model added (for the parts that open to take theirs: render/caropen.ts) – the original's getter `meshes`. */
  const std::vector<ObjectPtr>& meshes() const { return added_; }

  void show(bool on);

  /** The moving parts: the wheels as the plain ones stand, the gear knob or the selector's display. */
  void update(const CarVisualState& s);

 private:
  /** Keep the meshes as part of the finer model (and of the see-through cabin, in `layer`; −1: never see-through). */
  void keep(const std::vector<std::shared_ptr<Mesh>>& meshes, int layer);
  /** Parts of the plain model that the finer one takes the place of. */
  void hide(const std::vector<ObjectPtr>& objects);
  /** The detailed wheels where the plain ones stand (their steering and turning). */
  void placeWheels();

  CarParts car_;
  /** what the finer model added (shown with it) and what of the plain model it hides */
  std::vector<ObjectPtr> added_;
  std::vector<ObjectPtr> plain_;
  std::unique_ptr<WheelSet> wheels_;
  Vector3 wheelPos_;
  /** the shift pattern on the knob (a manual gearbox) and the P R N D beside the selector (an automatic) */
  ObjectPtr pattern_;
  std::shared_ptr<Mesh> selector_;
  Canvas selectorCanvas_;
  TexturePtr selectorTexture_;
  std::string selectorShown_;
  bool on_ = false;
  bool automatic_ = false;
};

}  // namespace fs
