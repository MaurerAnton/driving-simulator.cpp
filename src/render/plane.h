// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/plane.ts @ v1.7.7
/**
 * The light aeroplane: a generic two-seat high-wing trainer of the kind
 * flying schools use – no real type, maker or registration is reproduced –
 * in white with a blue cheat line and "Fahrsim" on the fin.
 *
 * Built on the measures of sim/flightspec.ts (TRAINER) in the flight model's
 * body frame: x to the right, y up, the nose towards −z, the origin at the
 * centre of mass.  The wheels stand where the physics has them (each strut
 * shortens by its compression), the propeller turns about its hub and every
 * control surface about its own hinge line.
 *
 * Outside: a lofted fuselage whose windows are cut out of its skin, the wing
 * on struts with ailerons and flaps, the tail with elevator, trim tab and
 * rudder, a tricycle gear on telescopic spring struts with wheel fairings,
 * the propeller (a faint disc once it runs fast), the lights.  Inside – left
 * out for an aeroplane parked as scenery – what the pilot sees from the left
 * seat: the panel (render/flightpanel.ts), two yokes, pedals, the throttle,
 * the seats, the frames of windscreen and doors, the wing through the side
 * windows.
 *
 * Everything that does not move is merged per material, and the geometry is
 * built once and shared by every aeroplane.
 */
#pragma once

#include <memory>
#include <vector>

#include "engine/engine.h"
#include "render/flightpanel.h"
#include "render/mirrors.h"
#include "sim/flightspec.h"

namespace fs {

/**
 * What the game hands the model every frame.  (The instruments' part – airspeed,
 * altitude … lights, stall warning, parking brake, engine – is the PanelReadings
 * it is made of: the panel is handed the same state.)
 */
struct PlaneVisualState : PanelReadings {
  /** deflections (rad): + aileron = roll to the right (right aileron up, left down), + elevator = trailing edge up, + rudder = trailing edge to the right */
  double aileron = 0;
  double elevator = 0;
  double rudder = 0;
  double flapsDeg = 0;
  /** 0 … 1 */
  double throttle = 0;
  double rpm = 0;
  /** the propeller's angle (rad), growing as it turns */
  double propAngle = 0;
  /** nose, left main, right main (the order of TRAINER.gear): strut compression (m), wheel angle (rad), steering (rad, + = right) */
  struct Wheel {
    double compression = 0;
    double spin = 0;
    double steer = 0;
  };
  std::vector<Wheel> wheels;
  /** −1 … 1 (pitch + = pulled) */
  struct Yoke {
    double pitch = 0;
    double roll = 0;
  } yoke;
  /** −1 … 1, + = right pedal pushed */
  double pedals = 0;
};

struct PlaneModelOptions {
  /** the cockpit (panel, yokes, seats …); false for an aeroplane parked as scenery */
  bool interior = true;
};

class PlaneModel {
 public:
  const std::shared_ptr<Group> root = make<Group>();
  /** the pilot's eyes (TRAINER.eye) */
  const Vector3 eye = Vector3(TRAINER.eye[0], TRAINER.eye[1], TRAINER.eye[2]);
  /** an aeroplane has no mirrors to draw */
  std::vector<std::unique_ptr<Mirror>> mirrors;

  explicit PlaneModel(const PlaneModelOptions& options = PlaneModelOptions());
  PlaneModel(const PlaneModel&) = delete;
  PlaneModel& operator=(const PlaneModel&) = delete;

  /**
   * How solid the cabin looks from the pilot's seat: 1 = solid, less = the
   * world shows through walls, panel frame, seats and yokes (the
   * instruments stay solid).  From outside the cabin is always solid.
   */
  void setCabinOpacity(double opacity);

  /** Moves the control surfaces, propeller, wheels, cockpit controls, lights and instruments. */
  void update(const PlaneVisualState& s, double dt, double now);

  void dispose();

 private:
  struct LegParts {
    GearLeg spec;
    Vector3 root;
    Vector3 foot;
    ObjectPtr outer;
    ObjectPtr inner;
    /** the wheel with its fairing (and the nose wheel's fork), moving with the strut */
    ObjectPtr carriage;
    ObjectPtr spin;
  };
  struct YokeParts {
    ObjectPtr slide;
    ObjectPtr wheel;
  };
  using MeshFn = std::function<std::shared_ptr<Mesh>(const GeometryPtr&, const MaterialPtr&, const ObjectPtr&, bool)>;

  std::vector<ObjectPtr> ailerons_;
  std::vector<ObjectPtr> flaps_;
  const std::shared_ptr<Group> elevator_ = make<Group>();
  const std::shared_ptr<Group> rudder_ = make<Group>();
  const std::shared_ptr<Group> prop_ = make<Group>();
  std::vector<LegParts> legs_;
  // materials of this aeroplane alone: what fades, what lights up, what turns see-through
  MaterialPtr bladeMat_;
  MaterialPtr tipMat_;
  MaterialPtr discMat_;
  std::shared_ptr<Mesh> disc_;
  MaterialPtr glass_;
  MaterialPtr lining_;
  MaterialPtr frames_;
  MaterialPtr navMat_;
  MaterialPtr strobeMat_;
  MaterialPtr beaconMat_;
  MaterialPtr landingMat_;
  MaterialPtr taxiMat_;
  std::vector<std::shared_ptr<Sprite>> navGlows_;
  std::vector<std::shared_ptr<Sprite>> strobeGlows_;
  std::shared_ptr<Sprite> beaconGlow_;
  std::shared_ptr<Mesh> landingGlow_;
  std::shared_ptr<Mesh> taxiGlow_;
  /** what this aeroplane alone owns (the shared geometry stays) */
  std::vector<GeometryPtr> ownedGeometries_;
  std::vector<MaterialPtr> ownedMaterials_;
  // the cockpit
  std::unique_ptr<FlightPanel> panel_;
  bool panelSeen_ = true;
  std::vector<YokeParts> yokes_;
  std::vector<ObjectPtr> pedals_;
  const std::shared_ptr<Group> throttle_ = make<Group>();
  std::vector<MaterialPtr> seeThrough_;
  double cabinOpacity_ = 1;
  Vector3 tmp_;

  /** The cockpit: panel, glare shield, yokes, pedals, throttle, seats, floor and headliner. */
  void buildCockpit(const MeshFn& mesh);
  /** Whether a camera is inside the cabin. */
  bool inside(const Camera& camera);
};

}  // namespace fs
