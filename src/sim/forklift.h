// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/forklift.ts @ v1.7.7
/**
 * A counterbalance forklift (Gabelstapler) of 2.5 t – what it does, without
 * the drawing (render/forklift.ts) or the game round it (game/forkliftplay.ts):
 *
 *  - it is driven on its front wheels and steered with its rear wheels: it
 *    turns about a point on the line of its front axle, very tightly (78° of
 *    lock), and its rear swings out.  About 20 km/h at the most, a little
 *    less with a load; in tight turns the speed is held down, as modern
 *    forklifts do (curve speed control).  The model is kinematic – no tyre
 *    slip –, stopped dead by whatever it runs into;
 *  - the mast: the forks lifted (0 … 3.3 m), the mast tilted forwards (6°)
 *    or back (10°), the forks shifted sideways (± 10 cm);
 *  - the pallets: the forks slide into a pallet's openings when they are
 *    square to it, central and at the height of its openings; lifted, it
 *    goes up on them; lowered, it is set down on what is under it – the
 *    ground, the beams of a rack, the floor of a truck;
 *  - the load moment: the forklift stands on its front axle like a see-saw,
 *    the counterweight behind against the load ahead.  Too heavy, too far
 *    out, or braked hard with the load high – the rear wheels lift and it
 *    tips forwards.  Sideways it stands on three points (the front wheels
 *    and the pivot of the rear axle): turned hard, with the load high above,
 *    it tips onto its side.  Both are worked out as rotations about the
 *    tipping line, from the moments of the masses about it, so a short
 *    excess lifts a wheel and lets it fall back, a long one throws it over.
 *
 * World frame as everywhere: x east, y up, z south; yaw 0 faces north,
 * positive turns left; forward = (−sin yaw, −cos yaw).  The forklift's point
 * of reference is the ground below the middle of its wheelbase (as for every
 * vehicle of the game).  A pallet's long side runs along its own z.
 */
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "sim/vehicle/types.h"

namespace fs {

inline constexpr double FORK_G = 9.81;

/** The forklift's measures and masses (a generic diesel or electric counterbalance forklift of 2.5 t). */
struct ForkliftSpec {
  /** its own mass with the operator (kg) */
  double mass = 3900;
  double wheelbase = 1.65;
  /** half the front track (to the middle of the tyres) */
  double halfTrack = 0.49;
  /** its centre of mass: behind the front axle, above the ground */
  double cgBehind = 0.95;
  double cgHeight = 0.8;
  /** what it may lift (kg) at the rated load centre (m from the forks' heel) */
  double capacity = 2500;
  double ratedCentre = 0.5;
  /** the forks' heel (the face of the carriage) ahead of the front axle, the mast upright */
  double heelAhead = 0.47;
  double forkLength = 1.15;
  /** the tines: half the distance between their middles, their width and thickness */
  double forkHalfGap = 0.3;
  double forkWidth = 0.12;
  double forkThick = 0.045;
  /** the forks' top above the ground: lowered onto it, and at the most */
  double liftMin = 0.045;
  double liftMax = 3.3;
  /** lifting and lowering (m/s), tilting (rad/s), the side-shift (m/s, and its stroke) */
  double liftSpeed = 0.5;
  double lowerSpeed = 0.55;
  double tiltForward = 6 * (3.141592653589793 / 180);
  double tiltBack = 10 * (3.141592653589793 / 180);
  double tiltRate = 4 * (3.141592653589793 / 180);
  double sideMax = 0.1;
  double sideRate = 0.12;
  /** top speed empty and with a load (m/s), backwards */
  double vmax = 5.6;
  double vmaxLaden = 5.0;
  double vReverse = 5.0;
  /** drive, service brake, rolling off with the pedal up (hydrostatic braking), m/s² */
  double accel = 1.6;
  double brake = 2.0;
  double coast = 0.9;
  /** the rear wheels' lock (rad) and how fast it is wound on (rad/s) */
  double maxSteer = 78 * (3.141592653589793 / 180);
  double steerRate = 1.4;
  /** the curve speed control: the lateral acceleration it holds the speed to in turns (m/s²) */
  double curveLateral = 3.0;
  /** the relief valve: the hydraulics lift no more than this many times the capacity */
  double relief = 1.6;
  /** the rear axle swings on its pivot until its stops: how much wider that makes the base sideways (m) */
  double axleStops = 0.1;
  /** the body against walls and posts: circles along its axis (z, + = back) and their radius; its height */
  std::array<double, 3> circles = {-0.7, 0.02, 0.7};
  double radius = 0.58;
  double height = 2.2;
  /** pitch and roll at which it is past saving (rad) */
  double tipForward = 0.6;
  double tipSide = 0.5;
};
inline const ForkliftSpec FORKLIFT{};

/** Its vehicle spec for the rest of the game (the sound, the steering keys' lock, its measures) – it is not driven by sim/vehicle. */
extern const VehicleSpec FORKLIFT_SPEC;

// ---------------------------------------------------------------------------- the world

/** A box standing upright, turned about y: its middle, half its sides along its own x and z, its yaw, from y0 to y1. */
struct OBox {
  double x = 0;
  double z = 0;
  double hx = 0;
  double hz = 0;
  double yaw = 0;
  double y0 = 0;
  double y1 = 0;
};

/** Something a pallet can be set down on: its top (an OBox's y1 is ignored), e.g. a rack's beams, a truck's floor. */
struct Shelf : OBox {
  std::string id;
  /** the top a pallet stands on */
  double y = 0;
};

/** What a pallet is: its sides (len along its z, wid along its x), the pallet's height, the goods on it. */
struct PalletDef {
  double len = 0;
  double wid = 0;
  double base = 0;
  double goods = 0;
  /** pallet and goods (kg) */
  double mass = 0;
  /** how the goods look (0 cartons, 1 sacks, 2 drums, 3 a crate) */
  int kind = 0;
};

/** A Euro pallet with goods of this height and mass. */
PalletDef euroPallet(double goods, double mass, int kind = 0);

/** On the forks: its middle this far ahead of the heel and to the right of the forks' middle, turned so much against the forklift. */
struct PalletFork {
  double d = 0;
  double s = 0;
  double yaw = 0;
};

/** A pallet in the world: where its bottom's middle is, its yaw; on the forks: where on them. */
struct Pallet {
  std::string id;
  PalletDef def;
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  std::optional<PalletFork> fork;
  /** what it stands on (a shelf's id, 'ground') */
  std::string on = "ground";
};

/** The mast leans about its foot this high above the ground (just ahead of the front axle). */
inline constexpr double MAST_PIVOT = 0.35;

/** The deck's underside above the pallet's bottom: where the forks lift it. */
inline constexpr double DECK = 0.1;

/** (the pallets are owned by whoever made the world – the place, a test –, pointed to here) */
struct ForkWorld {
  /** the ground's height */
  std::function<double(double, double)> ground;
  std::vector<OBox> obstacles;
  std::vector<Shelf> shelves;
  std::vector<Pallet*> pallets;
};

/** What the operator does: the pedals, the direction (1 forwards, −1 backwards, 0 none), the parking brake, the wheel, the mast's levers (−1 … 1). */
struct ForkCommand {
  double throttle = 0;
  double brake = 0;
  double dir = 0;
  bool hold = false;
  double steer = 0;
  double lift = 0;
  double tilt = 0;
  double shift = 0;
};

/** What happened in a step. */
struct ForkEvents {
  /** ran into something at this speed (m/s; 0: nothing) */
  double hit = 0;
  /** a pallet taken onto the forks (and how far the forks were in, 0 … 1), one set down */
  Pallet* picked = nullptr;
  double pickedDepth = 0;
  Pallet* placed = nullptr;
  /** the relief valve opened: too heavy to lift */
  bool relief = false;
  /** the rear wheels came off the ground / a front wheel lifted sideways */
  bool rearLift = false;
  bool sideLift = false;
  /** it went over now */
  bool tipped = false;
};

// ---------------------------------------------------------------------------- geometry

/** Is the point inside the box (in the plan)? */
bool inBox(const OBox& b, double x, double z, double margin = 0);
/** Does a circle touch the box (in the plan)? */
bool circleBox(const OBox& b, double x, double z, double r);
/** Do two boxes overlap in the plan (separating axes)? */
bool boxBox(const OBox& a, const OBox& b);
/** A pallet as a box (with its goods). */
OBox palletBox(const Pallet& p);

/** The forks' frame: the heel's middle (on the forks' top), forwards and to the right. */
struct ForkFrame {
  double hx = 0;
  double hz = 0;
  double top = 0;
  double fx = 0;
  double fz = 0;
  double rx = 0;
  double rz = 0;
  double tilt = 0;
};

/** How a pallet lies on the forks: square to them, central, at the height of its openings – and how far they are in. */
struct Engagement {
  /** the forks fit into its openings (they may be slid in or out, it may be lifted) */
  bool fits = false;
  /** how far the forks are in, of the pallet's depth along them (0 … 1) */
  double depth = 0;
  /** its middle ahead of the heel, and to the right */
  double along = 0;
  double lateral = 0;
  /** how far it is turned against the forks (rad, the nearer of its two axes) */
  double misalign = 0;
};

/** The forks against a pallet (standing on something, not on the forks). */
Engagement engagement(const ForkFrame& f, const Pallet& p, const ForkliftSpec& spec = FORKLIFT);

/** The load chart: what may be lifted with its middle this far from the heel (kg). */
double capacityAt(double centre, const ForkliftSpec& spec = FORKLIFT);

/** What tips the forklift: the load's mass, its middle ahead of the front axle and its height (m), the deceleration (m/s², + = slowing down when going forwards) and the lateral acceleration (+ = outwards to the right). */
struct Loading {
  double mass = 0;
  double ahead = 0;
  double height = 0;
  double decel = 0;
  double lateral = 0;
};

/** (`{ forward, side }` in the original – fs::Stability is another's) */
struct ForkStability {
  double forward = 0;
  double side = 0;
};

/**
 * How near it is to tipping, standing level: forwards (the moment about the
 * front axle that lifts the rear against the one that holds it down) and
 * sideways (over the line from a front wheel to the rear axle's pivot).
 * 1: the wheels lift.
 */
ForkStability stability(const Loading& l, const ForkliftSpec& spec = FORKLIFT);

// ---------------------------------------------------------------------------- the forklift

class Forklift {
 public:
  const ForkliftSpec spec;
  /** the ground below the middle of the wheelbase, the heading */
  double x = 0;
  double y = 0;
  double z = 0;
  double yaw = 0;
  /** speed (m/s, + forwards), the rear wheels' angle (rad, + = turning right) */
  double v = 0;
  double steer = 0;
  /** the forks' top above the ground, the mast's tilt (+ back), the side-shift (+ right) */
  double lift;
  double tilt = 0;
  double side = 0;
  /** tipping: forwards (rad, the rear up) and sideways (rad, to the side `rollSide`: 1 right, −1 left) */
  double pitch = 0;
  double pitchV = 0;
  double roll = 0;
  double rollV = 0;
  double rollSide = 1;
  /** "forward", "side" or "" (not tipped) */
  std::string tipped;
  /** the accelerations of the last step: along (m/s²) and outwards (+ right) */
  double aLong = 0;
  double aLat = 0;
  /** the yaw rate (rad/s) */
  double omega = 0;
  /** the pallet on the forks */
  Pallet* load = nullptr;
  /** how far the wheels have rolled (m: front, rear) */
  std::array<double, 2> rolled = {0, 0};
  ForkEvents ev;

  explicit Forklift(const ForkliftSpec& spec = FORKLIFT);

  void place(double x, double y, double z, double yaw);

  /** The forks' frame now. */
  ForkFrame forks() const { return forksAt(x, z, yaw, lift, tilt, side); }
  /** The forks' frame for another pose. */
  ForkFrame forksAt(double x, double z, double yaw, double lift, double tilt, double side) const;

  /** The load: its mass, its middle ahead of the front axle and above the ground. */
  Loading loading() const;
  /** The load's middle ahead of the heel (the load centre of the load chart); 0 without a load. */
  double loadCentre() const { return load && load->fork ? load->fork->d : 0; }
  /** How near to tipping it is now (1: the wheels lift). */
  ForkStability stability() const { return fs::stability(loading(), spec); }

  /**
   * Everything the forklift touches now, by name: its body (circles), its
   * forks (points along the tines, not in the openings of a pallet they fit)
   * and the load on them – against the world's solids and the pallets.
   */
  std::set<std::string> touching(const ForkWorld& w) const;

  struct Support {
    double y;
    std::string on;
  };
  /** What a pallet would be set down on where it is now: the highest top under its middle not above its bottom. */
  Support supportOf(const Pallet& p, const ForkWorld& w) const;

  const ForkEvents& step(double dt, const ForkCommand& cmd, const ForkWorld& w);

 private:
  /** Where the pallet on the forks is with the forks so. */
  void carry(Pallet& p, const ForkFrame& f) const;
  /** Something touched now that was not before. */
  bool newContact(const ForkWorld& w, const std::set<std::string>& before) const;
  /** The rotations about the tipping lines: forwards about the front axle, sideways about the stability triangle's side. */
  void tip(double dt, ForkEvents& ev);
};

}  // namespace fs
