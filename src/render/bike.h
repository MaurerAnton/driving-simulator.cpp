// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The player's motorcycle and its rider, built in four sizes – one for each
 * motorcycle class: a 50 cm³ moped (AM), a slim 125 (A1), a naked twin of
 * 35 kW (A2) and a larger four-cylinder (A).  Generic machines of their kind:
 * no real make or model is reproduced.
 *
 * Built like the other vehicles around the point the physics use – the
 * ground below the middle of the wheelbase –, x to the right, y up, the front
 * towards −z; wheelbase and wheel size are those of the class's physics
 * (sim/vehicle/fleet.ts).  Everything but the root leans into the bend about
 * the line where the tyres touch the road.  The front – fork, wheel,
 * handlebar with instruments, lamp and mirrors – turns about the raked
 * steering axis, the rider's arms follow the grips, and fork and swing arm
 * follow the heights of the wheels in the physics.
 *
 * The rider moves as a rider does (RiderMotion): he looks through the bend,
 * leans into it a little more than the machine, tucks in on the throttle,
 * braces himself on the brakes, looks over his shoulder when an indicator
 * comes on and puts a foot down when he stops.
 *
 * Seen through the rider's own eyes his helmet is not drawn: it leaves itself
 * out of any picture taken from inside it (its shadow stays, and the mirrors,
 * whose cameras stand elsewhere, still show it).
 *
 * Origin: src/render/bike.ts @ v1.7.7
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "render/car.h"
#include "render/fleet.h"
#include "render/instruments.h"
#include "render/mirrors.h"

namespace fs {

/** The four builds: 'moped' (AM), 'light' (A1), 'mid' (A2), 'big' (A). */
using BikeVariant = std::string;

/** What the rider's movements are made from: the visual state and the lean, boiled down. */
struct RiderInput {
  /** speed (km/h, not below 0); whether it goes backwards (a motorcycle is pushed back: he paddles with his feet) */
  double speedKmh = 0;
  bool reversing = false;
  /** the front wheel's angle on the road (rad, + = to the right) and the wheelbase (m): the curve it rides */
  double steer = 0;
  double wheelbase = 1.45;
  /** the handlebar's turn about the steering axis (rad, + = to the right): the grips his hands follow */
  double bar = 0;
  /** how far the motorcycle leans as drawn (rad, + = to the right) */
  double lean = 0;
  /** throttle and brake, 0 … 1 */
  double throttle = 0;
  double brake = 0;
  /** the indicator: −1 left, 0 off, 1 right */
  double indicator = 0;
};

/**
 * The rider's pose.  Each angle (rad) is relative to the part it hangs from: + is to the right for a
 * turn or a tilt, forwards (down) for a bend; shifts in m.
 */
struct RiderPose {
  /** the hips: across the seat (+ = to the right) and forwards along it; how they roll, bend and turn on it */
  double hipShift = 0;
  double hipSlide = 0;
  double hipRoll = 0;
  double hipPitch = 0;
  double hipYaw = 0;
  /** the upper body, relative to the motorcycle (the hips' turns included: the waist bends by the difference) */
  double torsoRoll = 0;
  double torsoPitch = 0;
  double torsoYaw = 0;
  /**
   * … and the same without his looking round (glances, the shoulder check) and without his breathing and
   * shifting while he waits: what the camera of his eyes goes with – it moves as he rides, never as he looks
   */
  double eyeRoll = 0;
  double eyePitch = 0;
  double eyeYaw = 0;
  /** the head relative to the upper body (the neck takes part of the bend and the tilt) */
  double headYaw = 0;
  double headPitch = 0;
  double headRoll = 0;
  /** the elbows: + out (tucked in), − in and down (braced) */
  double elbows = 0;
  /** each foot from its peg (0) to the road (1); each knee from the tank (0) to opened out (1) */
  double footL = 1;
  double footR = 0;
  double kneeL = 0;
  double kneeR = 0;
  /** how far the motorcycle settles on to a foot on the road (rad, + = to the right) */
  double tilt = 0;
};

/**
 * How the rider moves: one step a frame from what the motorcycle does, into `pose` (the same object
 * every time – nothing is made per frame).  It keeps its own clock, which runs only with the steps:
 * no time passing, no movement.
 *
 * - He looks through the bend: at the point of his way some 3 s ahead, on the arc the front wheel's
 *   angle draws – half the turn of that arc lies in front of him.  The angle of the wheel turns his
 *   head before the machine leans: he looks first, as he should.  At a standstill only half of it (the
 *   bar may be left turned), pushed backwards over the shoulder.
 * - His upper body leans into the bend a little more than the machine, and from about 11° his hips
 *   slide across the seat towards the inside; from about 17° the inside knee opens.  Half of what
 *   riding asks of his body – the lean, tucking in, turning with the bar – is in his hips, the rest in
 *   his back.  His head keeps the eyes nearer the horizon than his shoulders are (it takes back half
 *   their lean) – as riders do.
 * - Hard on the throttle (or very fast) he tucks in, elbows out; on the brakes he braces himself on
 *   the bar: upper body back, arms straight, elbows in, hips slid forwards.
 * - When an indicator comes on, a shoulder check to that side (head and shoulders, 1.2 s).
 * - Standing he puts the left foot on the road (the right stays on the brake pedal; both when pushed
 *   back) and the machine settles a little on to it; he breathes, shifts his weight and looks round
 *   now and then.  Riding straight on, now and then a glance into a mirror.
 */
class RiderMotion {
 public:
  RiderPose pose;

  RiderMotion();
  const RiderPose& step(const RiderInput& i, double dt);

 private:
  /** where he looks through the bend, and where he looks with the glances (rad, + = right of straight ahead); how far down; smoothed */
  double through_ = 0;
  double view_ = 0;
  double nod_ = 0;
  double tuck_ = 0;
  double brace_ = 0;
  /** whether it stands: the foot comes down below 3 km/h and goes up again above 6; smoothed (1 standing) */
  bool stopped_ = true;
  double still_ = 1;
  /** the shoulder check: the indicator last seen, the side looked to, the time since it began */
  double lastIndicator_ = 0;
  double checkSide_ = 0;
  double checkT_;
  /** his clock (s): the beat of his breathing, his shifting and his glances */
  double clock_ = 0;
};

/**
 * A motorcycle with its rider.  It leans into the bend (`setLean`) – the
 * camera of the rider's eyes leans with it.
 *
 * (`eye`, the camera of the rider's view, is a little below and behind his eyes – at the chin of the
 * helmet –, from where instruments, bar and hands are in sight when he looks down at them: `gaze`
 * below the horizon.)
 */
class PlayerBike : public VehicleModel {
 public:
  /** how far below the horizon the rider looks when he looks ahead (rad): the road, and below it instruments, bar and gloves */
  const double gaze = 0.28;
  const BikeVariant variant;

  /** `variant`: the class's build – 'moped' (AM), 'light' (A1), 'mid' (A2), 'big' (A). */
  explicit PlayerBike(const BikeVariant& variant = "big");
  ~PlayerBike() override;

  void setCabinOpacity(double opacity) override;

  /** How far it leans into the bend (rad, + = to the right). */
  void setLean(double angle);

  /** Whether the rider sits on it (not while he walks round it). */
  void setRider(bool on);

  void update(const CarVisualState& s, double dt, double now) override;

  /** The lean as drawn right now, with the little settling on to his foot (for the camera of the rider's eyes). */
  double leaning() const;

  /** RiderMotion's pose as the rider has it now (for tests). */
  const RiderPose& pose() const { return motion_.pose; }

 private:
  /** (tests/bike_test.cpp looks inside: the arms, the legs, the front, the head) */
  friend struct BikeInside;
  struct Arm {
    /** −1 left, 1 right; the shoulder in the torso's frame, the wrist in the front's */
    int side;
    Vector3 shoulder;
    Vector3 wrist;
    std::shared_ptr<Mesh> upper;
    std::shared_ptr<Group> fore;
  };
  struct Leg {
    /** −1 left, 1 right; the hip joint in the frame of the hips, the ankle on its peg in the motorcycle's */
    int side;
    Vector3 hip;
    Vector3 peg;
    /** the thigh, the shin with the boot's shaft (both along +y from the joint above), the foot about the ankle */
    std::shared_ptr<Mesh> thigh;
    std::shared_ptr<Group> shin;
    std::shared_ptr<Group> foot;
  };
  struct Shock {
    std::shared_ptr<Group> mesh;
    /** lower eye in the swing arm's frame, upper eye in the body's */
    Vector3 low;
    Vector3 high;
    double length;
  };

  /** everything that leans: the root stays upright, so that the game can place it */
  const std::shared_ptr<Group> body_ = make<Group>();
  /** the front, turning about the steering axis; in it what moves along the fork with the wheel, and the wheel */
  const std::shared_ptr<Group> front_ = make<Group>();
  const std::shared_ptr<Group> slider_ = make<Group>();
  const std::shared_ptr<Group> frontWheel_ = make<Group>();
  /** the swing arm about its pivot, and the rear wheel in it */
  const std::shared_ptr<Group> swing_ = make<Group>();
  const std::shared_ptr<Group> rearWheel_ = make<Group>();
  /** the rider: his hips on the seat, the upper body about the waist, the neck on it, the head on the neck */
  const std::shared_ptr<Group> pelvis_ = make<Group>();
  const std::shared_ptr<Group> torso_ = make<Group>();
  const std::shared_ptr<Group> neck_ = make<Group>();
  const std::shared_ptr<Group> head_ = make<Group>();
  std::vector<ObjectPtr> rider_;
  std::vector<Arm> arms_;
  std::vector<Leg> legs_;
  /** how he moves, and what it is worked out from (the same objects every frame) */
  RiderMotion motion_;
  RiderInput input_;
  /** where his hips sit at rest */
  Vector3 hipRest_;
  /** the wheels' axles along the motorcycle (z, m): between them the road under a foot put down */
  double zFront_ = 0;
  double zRear_ = 0;
  /** whether he sits on it */
  bool riderOn_ = true;
  std::vector<Shock> shocks_;
  Lamps lamps_;
  InstrumentPanel panel_;
  Vector3 axis_;
  double rake_ = 0;
  double radius_ = 0;
  /** the swing arm: length, and its height difference from the pivot to the axle at rest */
  double armLength_ = 0;
  double armDrop_ = 0;
  /**
   * The eye in the frame of the upper body: it moves with the hips and the shoulders, smoothly and a
   * few centimetres (the game reads `eye` every frame) – not with the head: the head's own turns (the
   * look through a bend, the shoulder check, glances) are seen from outside and in the mirrors, while
   * the camera of the eyes looks where the player turns it.
   */
  Vector3 eyeRest_;
  /** the frame of the upper body the eye goes with (RiderPose eye…), made anew every frame in these */
  Euler eyeEuler_;
  Quaternion eyeQ_;
  Matrix4 eyeM_;
  /** the lean the game asks for, and the lean as drawn (following it smoothly) */
  double lean_ = 0;
  double leanDrawn_ = 0;
};

}  // namespace fs
