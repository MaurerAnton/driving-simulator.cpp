// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The cars you see: body shell, wheels, lights – and for the player's car the
 * complete cockpit (dashboard, instruments, steering wheel, gear lever,
 * handbrake, pedals, seats, door trims) plus the three mirrors.
 *
 * Car frame: origin on the ground below the middle of the wheelbase,
 * +X right, +Y up, -Z forward.  Left-hand drive.
 *
 * Everything is built from code: no model files, no textures other than the
 * ones drawn here on a canvas.
 *
 * Origin: src/render/car.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/engine.h"
#include "platform/canvas.h"
#include "render/carBody.h"
#include "render/instruments.h"
#include "render/mirrors.h"

namespace fs {

struct CarVisualState : DashState {
  /** steering wheel angle in rad, + = clockwise / right */
  double steerWheel = 0;
  /** road wheel steer angles (rad, + = right) and spin angles (rad) FL, FR, RL, RR */
  std::vector<double> wheelSteer;
  std::vector<double> wheelSpin;
  /** wheel centre height above the car origin plane, per wheel */
  std::vector<double> wheelY;
  double clutch = 0;
  double brake = 0;
  double throttle = 0;
  double handbrakeLever = 0;
  bool braking = false;
  bool reversing = false;
};

struct CarExteriorOptions {
  /** null: COMPACT_DIMS.  (The car keeps pointing to them: they must live as long as it does.) */
  const CarDims* dims = nullptr;
  /** '': no number plates */
  std::string plate{};
  bool seeThroughGlass = false;
  bool roofSign = false;
  bool noMirrorHousings = false;
  /** lettering on the front and the rear bumper (the traffic cars carry none) */
  std::string lettering{};
  /** own copies of the materials all cars share, for a car that changes them (see-through cabin) */
  bool ownMaterials = false;
};

/** Body, wheels and lamps – everything a car shows from outside. */
class CarExterior {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  const CarDims& dims;
  const CarShell& shell;
  std::vector<std::shared_ptr<Group>> wheels;
  std::vector<std::shared_ptr<Group>> wheelSpinners;
  MaterialPtr paint;
  MaterialPtr glass;
  /** the opaque body and what is fixed to it (not wheels, glass, plates, lettering) */
  std::vector<std::shared_ptr<Mesh>> solids;
  /** the lamps as plain blocks (the finer model of render/cardetail.ts has its own, lit by the same materials) */
  std::vector<std::shared_ptr<Mesh>> lampMeshes;
  MaterialPtr head;
  MaterialPtr tail;
  MaterialPtr reverse;
  MaterialPtr blinkL;
  MaterialPtr blinkR;

  /** `color`: the paint, 0xRRGGBB */
  explicit CarExterior(int color, const CarExteriorOptions& opts = CarExteriorOptions());
  CarExterior(const CarExterior&) = delete;
  CarExterior& operator=(const CarExterior&) = delete;

  void setWheels(const std::vector<double>& steer, const std::vector<double>& spin, const std::vector<double>& y);

  struct Lamps {
    bool low = false;
    bool high = false;
    bool brake = false;
    bool reverse = false;
    bool left = false;
    bool right = false;
  };
  void setLamps(const Lamps& o);

 private:
  /**
   * Lettering on both bumpers: a strip lying just in front of the flat nose
   * face (above the number plate) and of the tail face (below it), as wide as
   * the face allows.  Each strip is turned to its own side, so the text reads
   * left to right from in front as well as from behind.
   */
  void addLettering(const std::string& text, int paint);
};

// ---------------------------------------------------------------- cockpit ----

/**
 * The cabin gets little direct light and the scene has no bounce light; a
 * self-lit share keeps it readable instead of a black hole (what your eyes do
 * when they adapt).  `hex` is roughly the colour the part shows in daylight.
 */
MaterialPtr cabinMaterial(int hex, double shine = 0);

/** the door panels inside: their material's mesh, the same without them (null: there is nothing else), and each door's own (fl fr rl rr; null: none) */
struct DoorTrim {
  std::shared_ptr<Mesh> mesh;
  GeometryPtr rest;
  std::vector<GeometryPtr> doors;
};

// ------------------------------------------------------------------ hooks ----
//
// render/cardetail.ts (the much finer car of the "very high" graphics) and render/caropen.ts
// (doors, bonnet and boot that open) are not translated yet.  Each stands here as a class of
// the original's name with just the members the car calls, for the missing part to fill in;
// `create` is its constructor (`new CarDetail(car)`, `new CarOpenings(host)`) and null until
// then – the car is the plain one meanwhile, and nothing of it opens.  When a part is
// translated its declaration here goes and the real class takes its place: nothing in car.cpp
// changes but the two `create` calls.

/** What the finer model is fixed to: the parts of the player's car (render/cardetail.ts CarParts). */
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

/** render/cardetail.ts */
class CarDetail {
 public:
  static inline std::function<std::unique_ptr<CarDetail>(const CarParts& car)> create = nullptr;

  virtual ~CarDetail() = default;
  /** the meshes the see-through cabin is to take in, with their layer (1: the controls in front) */
  std::vector<std::pair<std::shared_ptr<Mesh>, int>> ghosts;
  /** What the finer model added (for the parts that open to take theirs: render/caropen.ts) – the original's getter `meshes`. */
  virtual const std::vector<ObjectPtr>& meshes() const = 0;
  virtual void show(bool on) = 0;
  /** The moving parts: the wheels as the plain ones stand, the gear knob or the selector's display. */
  virtual void update(const CarVisualState& s) = 0;
};

/** What of the player's car the parts are taken from (render/caropen.ts OpeningsHost). */
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

/** render/caropen.ts */
class CarOpenings {
 public:
  static inline std::function<std::unique_ptr<CarOpenings>(const OpeningsHost& host)> create = nullptr;

  virtual ~CarOpenings() = default;
  /** The finer model's meshes (render/cardetail.ts): what lies on a part goes with it. */
  virtual void addDetail(const std::vector<ObjectPtr>& objects) = 0;
  /** How far each part is open (fl fr rl rr bonnet boot), 0 … 1. */
  virtual void pose(const std::vector<double>& values) = 0;
};

/** The driving-school car with its interior and mirrors. */
class PlayerCar {
 public:
  const std::shared_ptr<Group> root = make<Group>();
  CarExterior exterior;
  const std::shared_ptr<Group> cockpit = make<Group>();
  /** driver's eye point in car coordinates (seat position) */
  const Vector3 eye = Vector3(-0.36, 1.2, 0.2);
  std::vector<std::unique_ptr<Mirror>> mirrors;
  /** the centre display: its picture of a map, or the live map (setMapOnScreen) drawn into `mapCanvas` */
  const Canvas mapCanvas = Canvas(512, 288);
  const TexturePtr mapTexture = canvasTexture(mapCanvas);
  /** the door panels inside: their material's mesh, the same without them, and each door's own (fl fr rl rr) */
  std::vector<DoorTrim> doorTrim;

  PlayerCar();
  PlayerCar(const PlayerCar&) = delete;
  PlayerCar& operator=(const PlayerCar&) = delete;

  /** The centre display's picture (a tap on it in the driver's seat: ui/screentap.ts). */
  Object3D* screenMesh() const;

  /** The live map on the centre display (true), or its still picture. */
  void setMapOnScreen(bool on);

  /**
   * How solid the cabin looks from the driver's seat: 1 = as built, smaller
   * values let the road show through dashboard, pillars, doors, roof, bonnet
   * and steering wheel (0.2 = strongly see-through).  Instruments, displays
   * and mirrors stay solid.  Seen from outside the car is always solid.
   *
   * The see-through parts are drawn as one layer: all of them first write
   * their depth only, then each is blended where it is the nearest of them.
   * So every pixel gets exactly one veil – no part shows through another, no
   * order to get wrong, nothing to flicker.  The controls in front (steering
   * wheel, stalks, levers) form a second layer of the same kind so that they
   * stand out against the dashboard behind them.
   */
  void setCabinOpacity(double opacity);

  void update(const CarVisualState& s, double dt, double now);

  /** The finer model, outside and in the cockpit (true), or the plain one. */
  void setDetail(bool on);

  /**
   * The doors, the bonnet and the tailgate (render/caropen.ts) – cut out of the body the first time this is asked for.
   * (Null while render/caropen is not translated.)
   */
  CarOpenings* openings();

  /** How far each part is open (fl fr rl rr bonnet boot, 0 … 1) – nothing is made while all of them are shut. */
  void setOpenings(const std::vector<double>& values);

  void dispose();

 private:
  std::shared_ptr<Mesh> screen_;
  TexturePtr screenStill_;
  InstrumentPanel panel_;
  const std::shared_ptr<Group> steering_ = make<Group>();
  const std::shared_ptr<Group> lever_ = make<Group>();
  const std::shared_ptr<Group> handbrake_ = make<Group>();
  std::vector<std::shared_ptr<Group>> pedals_;
  const std::shared_ptr<Group> stalk_ = make<Group>();
  /** the finer model of the "very high" graphics (render/cardetail.ts): made the first time it is asked for */
  std::unique_ptr<CarDetail> detail_;
  /** the doors, the bonnet and the tailgate as parts that open (render/caropen.ts): made the first time one opens */
  std::unique_ptr<CarOpenings> opener_;
  double leverX_ = 0;
  double leverZ_ = 0;
  double gate_ = 0;
  // ---- see-through cabin (setCabinOpacity) ----
  /** parts that turn see-through, each with a twin that only writes depth; layer 1 = the controls in front */
  struct Ghost {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Mesh> twin;
    int layer = 0;
  };
  std::vector<Ghost> ghosts_;
  std::vector<MaterialPtr> ghostMaterials_;
  double cabinOpacity_ = 1;
  MaterialPtr depthOnly_;
  Vector3 tmp_;

  /** Register a part of the see-through cabin. */
  void ghost(const std::shared_ptr<Mesh>& mesh, int layer);
  bool seesFromInside(const Camera& camera);
};

}  // namespace fs
