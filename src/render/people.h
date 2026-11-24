// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/people.ts @ v1.7.7
/**
 * People on foot at the stops of trams and buses: waiting, sitting on a
 * bench, walking to the doors, getting off and walking away – and the
 * people of the town on the pavements (sim/pedestrians): looking out
 * for the traffic, starting at a car, under an umbrella in the rain.
 *
 * There may be a few hundred of them, so each part of the body is one
 * instanced mesh for everybody – thighs, lower legs, torso, arms, the head
 * with its hair, and what people carry –: six draw calls for a crowd.  The
 * pose of every part is worked out here each frame from where a person is
 * and how far they have walked; the gait follows the distance, so the feet
 * keep step at any speed, and the lowest foot always stands on the ground.
 *
 * What a person looks like – clothes and their colours, hair, height
 * (children too), a bag, a backpack, a suitcase on wheels, a walking stick –
 * follows from one number, so the same person looks the same every time.
 * The variants of a part (hair styles, a coat or a skirt, the kinds of bag)
 * share its mesh: the shader folds away those a person does not wear.
 *
 * The frame of a person is that of everything in the game: x to the right,
 * y up, the face towards −z.
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/engine.h"

namespace fs {

/** Where a person is and what they are doing (given every frame). */
struct PersonPose {
  /** the feet on the ground – for somebody sitting, the ground below the hips */
  double x = 0;
  double y = 0;
  double z = 0;
  /** 0 = facing north (−z), positive = turned to the left */
  double yaw = 0;
  /** metres walked so far (drives the gait); −1 = standing still */
  double walk = -1;
  /** a whole number that settles the look: clothes, colours, hair, height, what is carried */
  int look = 0;
  /** sitting on a seat SEAT_HEIGHT above y (a bench, the bench in a shelter) */
  std::optional<bool> sit{};
  /** the head turned (rad, + to the left): looking out for the traffic – standing, instead of looking about now and then */
  std::optional<double> head{};
  /** a start (0 … 1): a horn, a car too close – the arms up, the body back */
  std::optional<double> startle{};
  /** leaning into the walk more (rad): in a hurry, in the rain */
  std::optional<double> lean{};
  /** an umbrella up (not with a walking stick or a suitcase in the hand that would hold it) */
  std::optional<bool> umbrella{};
  /**
   * The arms held as given (rad): the right arm swung forward and out to
   * the side, then the left arm – a police officer's signs, hands on a
   * rollator (the theory lessons: game/theoryscenes armsAt).
   */
  std::optional<std::array<double, 4>> arms{};
};

/** How high the seats are that people sit on (m): the benches of render/stopmodels have this height. */
inline constexpr double SEAT_HEIGHT = 0.45;

/** A person's look, worked out once from the look number. */
struct Look {
  int id = 0;
  /** height / 1.72, and the head a little bigger on children */
  double scale = 1;
  double head = 1;
  double wide = 1;
  double deep = 1;
  int torso = 0;
  int sleeve = 0;
  int hair = 0;
  int shoe = 0;
  Color top;
  Color second;
  Color skin;
  Color hairColour;
  Color legs;
  Color shoes;
  Color bag;
  Color bag2;
  int item = -1;
  /** metres per double step (in the figure's own measure), arm swing and hip swing (rad), forward stoop */
  double stride = 1.42;
  double swing = 0.29;
  double hipSwing = 0;
  double stoop = 0;
  /** 0 … 1: when the idle movements happen */
  double seed = 0;
  /** how far the head is bowed while waiting (somebody reading) */
  double bow = 0;
};

Look lookOf(int id);

/**
 * Somebody at work in a depot or a yard (render/loading): navy work
 * trousers and safety boots, a high-visibility vest over the jacket
 * (yellow or orange), a cap or short hair, nothing carried.  `id` settles
 * the person (height, face, skin) as for anybody else; give it to the
 * renderer with `wear` before showing a person with this id.
 */
Look workerLook(int id, const std::string& vest);

/**
 * A police officer (a dark blue jacket and trousers, a white cap, boots) or
 * somebody old with nothing in the hands (for a rollator) – for the theory
 * lessons.  `id` is the look's own number (give it to the renderer with
 * `wear`); `seed` settles the person (height, face, skin) as for anybody.
 */
Look roleLook(int id, int seed, const std::string& role);

/**
 * Draws the people at the stops: many low-polygon figures, each part of the
 * body one instanced mesh for all of them.  Call `update` every frame with
 * the people to show; add `group` to the scene as it is (the instances are
 * placed in its frame).
 */
class PeopleRenderer {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  explicit PeopleRenderer(int capacity = 256);
  ~PeopleRenderer();
  PeopleRenderer(const PeopleRenderer&) = delete;
  PeopleRenderer& operator=(const PeopleRenderer&) = delete;

  /** Shows exactly these people (those beyond the capacity are left out). `time`: seconds, for the small movements of those waiting. */
  void update(const std::vector<PersonPose>& people, double time);
  void dispose();
  /** People with this look's id wear it (a worker's clothes: workerLook) instead of the look their number gives. */
  void wear(const Look& look);

  /** One part of the body for all the people: an instanced mesh with two colours and a variant per instance. */
  class Part;

 private:
  std::shared_ptr<const Look> lookFor(int id);
  int pose(int i, const PersonPose& p, const std::shared_ptr<const Look>& look, double time, int k);
  void put(Part& part, int index, const Matrix4& local);
  int carry(int k, const std::shared_ptr<const Look>& L, int kind, const Matrix4& local);
  const Matrix4& putDown(double top);

  int capacity;
  std::unique_ptr<Part> thigh;
  std::unique_ptr<Part> shin;
  std::unique_ptr<Part> torso;
  std::unique_ptr<Part> arm;
  std::unique_ptr<Part> head;
  std::unique_ptr<Part> item;
  /** the look of the person in each slot, and of the item in each item slot */
  std::vector<std::shared_ptr<const Look>> slots;
  /** the variant of the torso shown in each slot (it changes when somebody sits down) */
  std::vector<int8_t> torsoShown;
  std::vector<std::shared_ptr<const Look>> itemLooks;
  std::vector<int16_t> itemKinds;
  std::unordered_map<int, std::shared_ptr<const Look>> cache;
  Sphere bounds;
  Vector3 lo;
  Vector3 hi;
  // the frames of one person, in the person's own measure (the instance matrices are world · these)
  Matrix4 world;
  Matrix4 base;
  Matrix4 body;
  Matrix4 armR;
  Matrix4 armL;
  Matrix4 thighR;
  Matrix4 thighL;
  Matrix4 m;
  Matrix4 out;
};

}  // namespace fs
