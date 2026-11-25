// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/roadworks.ts @ v1.7.7
/**
 * The roadworks and the demonstrations of the day as they are seen
 * (sim/roadevents.ts says where they are and what they are made of).
 *
 * A work zone: its signs on mobile feet (drawn with the sign atlas of
 * render/signs.ts), the barrier (Zeichen 600) and the beacons (Zeichen 605)
 * in red and white with their yellow lamps – lit at night, the beacons'
 * flashing –, cones, a fence round the pit, a mini excavator, a heap of
 * gravel, a site trailer, the temporary traffic lights with their three
 * lamps, and the workers in orange.  A march: the police barriers with
 * their lamps and tape, the signs of the diversion, officers, the patrol
 * cars of render/emergency.ts at its ends (blue lights, "STOP POLIZEI"), and the
 * crowd – everybody of render/people.ts, many of them with placards on
 * sticks or a banner between two of them, in bright colours and without
 * any words.
 *
 * Everything that stands still is built once per zone or closure as two
 * merged meshes (one painted per vertex, one with the red-and-white stripes)
 * when it comes within reach of the camera, and thrown away when it is far
 * behind; what moves – the people, the placards, the lamps – is updated each
 * frame and drawn instanced.
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/engine.h"
#include "render/people.h"
#include "sim/roadevents.h"

namespace fs {

class AudioOut;
class CrowdNoise;
class SignRenderer;

// ---- src/render/emergency.ts (not translated yet) -----------------------------------------------
// What the closures of the marches use of it: a patrol car's model.  The hook `buildPoliceCar` is null
// until that file is there – the closures then stand without their patrol cars.  When it is translated
// these declarations go and its own take their place.

/** A model of an emergency vehicle (class EmergencyModel: what is used of it here). */
class EmergencyModel {
 public:
  virtual ~EmergencyModel() = default;
  std::shared_ptr<Group> group = make<Group>();
  /** blue lights on or off, at the time `t` (s) */
  virtual void setLights(bool on, double t) = 0;
  /** the "STOP POLIZEI" sign raised */
  virtual void setSign(bool up) = 0;
  virtual void setMotion(double speed, bool braking, bool left, bool right) = 0;
};

/** a patrol car (render/emergency.ts buildPoliceCar) – null until that is translated */
inline std::function<std::shared_ptr<EmergencyModel>()> buildPoliceCar = nullptr;

// ---------------------------------------------------------------------------------------------------

class RoadworksRenderer {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  /** how many of a crowd are drawn at most (the quality of the graphics) */
  int crowdCap = 220;

  /** `audio`: the game's sound, for the march to be heard (none: silent). */
  explicit RoadworksRenderer(AudioOut* audio = nullptr);
  ~RoadworksRenderer();

  /**
   * Each frame: the sites near the camera built (and those far behind
   * thrown away), the lamps and lights as they are now, the people.
   * `time`: seconds of the game (for the small movements of the people), `dt`: since the last frame.
   */
  void update(RoadEvents* ev, SignRenderer* signs, double camX, double camZ, double time, double dt = 0);

  /** Pieces of a site, collected and then merged into two meshes (and the lamps). */
  class Parts;

 private:
  /** One temporary light: its head and the three lamps in it. */
  struct SiteLight {
    const WorkZone* zone = nullptr;
    int group = 0;
    std::shared_ptr<Mesh> red;
    std::shared_ptr<Mesh> amber;
    std::shared_ptr<Mesh> green;
  };
  /** What is built of a zone or a closure. */
  struct Site {
    /** in the world's frame (the signs); `local` in it at the middle of the site (everything else) */
    std::shared_ptr<Group> group = make<Group>();
    std::shared_ptr<Group> local = make<Group>();
    /** its signs, made by the sign renderer – given back to it when the site goes */
    SignRenderer* signsBy = nullptr;
    std::shared_ptr<Group> signs;
    /** the patrol cars at a closure (taken from the pool, given back when the site goes) */
    std::vector<std::shared_ptr<EmergencyModel>> police;
    std::vector<SiteLight> lights;
    double x = 0;
    double z = 0;
    /** the version of the events it was built for (a closure is rebuilt when its march changes) */
    double version = 0;
  };

  std::unique_ptr<PeopleRenderer> people;
  std::map<std::string, Site> sites;
  /** patrol cars not shown now (models are kept for the next closure) */
  std::vector<std::shared_ptr<EmergencyModel>> policePool;
  TexturePtr stripeTex;
  MaterialPtr plainMat;
  MaterialPtr stripeMat;
  /** the lamps: a barrier's burn steadily at night, a beacon's flash */
  MaterialPtr lampMat;
  MaterialPtr blinkMat;
  /** the lamps of the temporary lights: off, and lit */
  MaterialPtr offRed, offAmber, offGreen, onRed, onAmber, onGreen;
  GeometryPtr lampGeo;
  std::vector<PersonPose> poses;
  std::unordered_map<int, Look> worn;
  PolySample smp;
  // the placards and banners of the marches
  std::shared_ptr<InstancedMesh> sticks, boards, marks, banners, bands;
  Matrix4 m4;
  Quaternion q;
  Vector3 v;
  Vector3 sc;
  Color col;
  Vector3 up{0, 1, 0};
  /** the whistles and the murmur of a march (null: silent) */
  std::unique_ptr<CrowdNoise> noise;

  /** Is the zone a site was built for still there (its road loaded, the same day)? */
  bool zoneStill(RoadEvents& ev, const std::string& key);
  void drop(Site& site);
  void finish(InstancedMesh& m, int n);
  /** A worker: walks to and fro between two points (at a slow pace, pausing at the ends) or stands. */
  void worker(const WorksLayout::Worker& w, double time, int i);
  /** A police officer's look: the dark blue uniform, or the yellow vest over it. */
  int police(int n);
  /** The crowd of a march; returns the counts of sticks, boards and banners used so far. */
  std::array<int, 3> crowd(RoadEvents& ev, const Demonstration& d, double ms, double time, double camX, double camZ, int sticks0, int boards0, int banners0);
  void signsOf(const std::vector<SignPost>& posts, Parts& parts, double ox, double oy, double oz, SignRenderer* signs, Site& site);
  void barrier(const WorksBarrier& b, Parts& parts, double ox, double oy, double oz);
  Site buildWorks(const WorkZone& z, const WorksLayout& L, SignRenderer* signs);
  void finishSite(Site& site, Parts& parts, double ox, double oy, double oz);
  void excavator(Parts& parts, double x, double y, double z, double yaw);
  void trailer(Parts& parts, double x, double y, double z, double yaw);
  Site buildClosure(const ClosureLayout& L, SignRenderer* signs, double version);
};

}  // namespace fs
