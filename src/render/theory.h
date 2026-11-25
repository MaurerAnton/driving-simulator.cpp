// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/theory.ts @ v1.7.7
/**
 * The theory lessons in 3D (game/theory.ts): the place of a lesson – its
 * asphalt, pavements, paint, kerbs, signs and traffic lights – laid over
 * the practice yard, and the vehicles, cyclists and people of its script
 * drawn with the game's own models (the cars of the traffic, the bus, the
 * lorry, the tram, cyclists, people).  Above the vehicles a question is
 * about, their names float; a tap near one picks it.
 *
 * The place is built anew for each lesson and draped over the ground of
 * the map: every outline is cut into triangles, those into smaller ones,
 * and each corner is lifted onto the terrain.  Nothing of the map changes.
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "game/theory.h"
#include "render/car.h"
#include "render/people.h"
#include "render/theoryprops.h"
#include "sim/rail.h"

namespace fs {

class SignRenderer;
class PlayerBus;
class PlayerTruck;
class CyclistModel;
class RailActors;
struct LoadedTile;

class TheoryView {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  explicit TheoryView(SignRenderer& signs);
  ~TheoryView();
  TheoryView(const TheoryView&) = delete;
  TheoryView& operator=(const TheoryView&) = delete;

  /** Lay a lesson's place out and make its actors. False while the ground there is not loaded yet (call again). */
  bool build(const std::shared_ptr<theory::Scene>& scene, const StageFrame& frame, const std::function<double(double x, double z)>& heightAt);

  /** Everything of the last lesson gone. */
  void clear();

  void hide();

  /** The scene at time t of the script (`now`: the clock, for the blinking). */
  void update(double t, double now);

  /**
   * Names over these actors (a question is about them) – or none.  `you`:
   * the learner's own car, marked always (in the accent colour).
   */
  void setLabels(const std::vector<std::pair<std::string, std::string>>& names, const std::string& you);

  /** Where the labels float now (call after `update`). */
  void placeLabels(double now);

  /** The blind spot of an actor shown on the ground ("": none). */
  void showZone(const std::string& id);

  /** The gap beside a vehicle passing another, with its width written over it ("": none); returns the gap (m). */
  double showGap(const std::string& a, const std::string& b, const std::function<std::string(double m)>& text);

  /** Where an actor is drawn (the middle of its body), for the camera that follows it. */
  bool where(const std::string& id, Vector3& out) const;

  /** The heading of an actor on the map (for the camera that follows it). */
  std::optional<double> heading(const std::string& id) const;

  /**
   * Which of these actors a tap at (px, py) – pixels in a view `w` × `h` –
   * means: the one whose body or label is nearest on the screen, within a
   * finger's reach.
   */
  std::optional<std::string> pick(const std::vector<std::string>& ids, const Camera& camera, double px, double py, double w, double h);

 private:
  /** An ambulance or police van: the body, the band round it, its blue lights. */
  struct Van {
    std::unique_ptr<CarExterior> car;
    MaterialPtr band;
    std::vector<MaterialPtr> blue;
    std::vector<std::shared_ptr<Mesh>> lamps;
  };

  struct ActorView {
    /** what is moved: a group (or null: a person, a tram) */
    ObjectPtr root;
    CarExterior* car = nullptr;
    PlayerBus* bus = nullptr;
    PlayerTruck* truck = nullptr;
    CyclistModel* bike = nullptr;
    Van* van = nullptr;
    RailVehicle* tram = nullptr;
    /** a deer or a boar (render/animals.ts) */
    AnimalModel* animal = nullptr;
    std::optional<int> look;
    theory::Pose pose;
    /** where it is drawn (for the labels and for picking): the middle of the body in the map */
    Vector3 at;
    double height = 1.5;
  };

  struct Light {
    std::string id;
    bool ped = false;
    std::vector<MaterialPtr> lamps;
    std::string shown;
  };

  struct Label {
    std::string id;
    std::shared_ptr<Sprite> sprite;
  };

  const std::shared_ptr<Group> place = make<Group>();
  const std::shared_ptr<Group> movers = make<Group>();
  std::unique_ptr<PeopleRenderer> people;
  std::unique_ptr<RailActors> rail;
  /** the props, what people hold, the animals, the headlights in the dark (render/theoryprops.ts) */
  std::unique_ptr<TheoryExtras> extras;
  SignRenderer& signs;
  std::unique_ptr<LoadedTile> signTile;
  std::vector<GeometryPtr> made;
  std::vector<std::shared_ptr<InstancedMesh>> madeInstanced;
  std::vector<MaterialPtr> madeMaterials;
  std::vector<ActorView> actors;
  std::vector<Light> lights;
  std::shared_ptr<theory::Scene> scene;
  std::optional<StageFrame> frame;
  std::function<double(double x, double z)> height = [](double, double) { return 0.0; };
  std::vector<PersonPose> poses;
  /** the labels, in the order they were made (a Map in the original) */
  std::vector<Label> labels;
  std::shared_ptr<Mesh> zone;
  std::shared_ptr<Mesh> gapBar;
  std::shared_ptr<Sprite> gapLabel;
  std::string gapText;
  CarVisualState visual;
  /** the trams of the lesson shown (RailActors draws them from a list of its own kind) */
  std::vector<std::unique_ptr<RailVehicle>> trams;
  /**
   * The models, kept from one lesson to the next (the bus and the lorry have
   * mirrors with pictures of their own: made once): the learner's car, the
   * cars of each body, the vans, the cyclists.
   */
  struct Pool {
    std::unique_ptr<CarExterior> me;
    std::vector<std::unique_ptr<CarExterior>> cars[3];
    std::vector<std::unique_ptr<Van>> vans;
    std::vector<std::unique_ptr<CyclistModel>> bikes;
    std::unique_ptr<PlayerBus> bus;
    std::unique_ptr<PlayerTruck> truck;
  } pool;
  /** what of the pool the lesson shown uses (given back by `clear`) */
  std::vector<std::unique_ptr<CarExterior>> usedCars[3];
  std::vector<std::unique_ptr<Van>> usedVans;
  std::vector<std::unique_ptr<CyclistModel>> usedBikes;

  void buildPlace(const theory::Scene& scene);
  /** An outline (with holes) cut into triangles of at most CELL a side, each corner on the ground plus `lift`: into `out` (x, y, z, …). */
  void drape(const std::vector<double>& ring, const std::vector<std::vector<double>>& holes, double lift, std::vector<float>& out) const;
  void buildActors(const theory::Scene& scene);
  /** The cars of a tram, each where the track has it, from its nose back. */
  void placeTram(const theory::ActorRun& r, const theory::Pose& p, RailVehicle& v);
  int actorIndex(const std::string& id) const;
};

}  // namespace fs
