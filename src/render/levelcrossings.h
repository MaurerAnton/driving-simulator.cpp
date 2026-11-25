// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/levelcrossings.ts @ v1.7.7
/**
 * The level crossings near the camera (src/sim/levelcrossing.ts): on each
 * side the mast with the St Andrew's cross (Zeichen 201) and the lights
 * beneath it – yellow above red, or the single red flashing light of an
 * older crossing –, the barrier with its drive (half barriers over the
 * lanes that come to the tracks, full ones over the whole road), lowered
 * and raised as the installation says, and outside built-up areas the
 * beacons before it (Zeichen 156, 159, 162).  The bell rings while the
 * lights come on and the barriers go down.
 *
 * The signs are drawn with the atlas of the map's signs (render/signs.ts),
 * which leaves out those of the map's own that these replace
 * (LevelCrossings.editPost).  Everything else is a handful of boxes and
 * discs per installation, built when it comes within reach.
 */
#pragma once

#include <map>
#include <memory>
#include <vector>

#include "engine/engine.h"

namespace fs {

class AudioOut;
class CrossingBell;
class LevelCrossings;
struct LevelCrossing;
class SignRenderer;

class LevelCrossingRenderer {
 public:
  const std::shared_ptr<Group> group = make<Group>();

  /** `audio`: the game's sound, for the bell (none: silent). */
  explicit LevelCrossingRenderer(AudioOut* audio = nullptr);
  ~LevelCrossingRenderer();

  /** Each frame: build what came within reach, drop what went, set the lamps and barriers. */
  void update(LevelCrossings& lx, SignRenderer* signs, double camX, double camZ, double time);

 private:
  /** One side of an installation: its lamps and the arm of its barrier. */
  struct Side {
    std::shared_ptr<Mesh> yellow;
    std::vector<std::shared_ptr<Mesh>> red;
    /** turns the boom up (about the road's direction) */
    ObjectPtr arm;
    std::vector<std::shared_ptr<Mesh>> boomLamps;
  };
  struct Site {
    const LevelCrossing* c = nullptr;
    std::shared_ptr<Group> group;
    std::vector<Side> sides;
    /** the signs, made by the sign renderer – given back to it when the site goes */
    SignRenderer* signsBy = nullptr;
    std::shared_ptr<Group> signs;
  };
  std::map<int, Site> sites;
  std::unique_ptr<CrossingBell> bell;
  MaterialPtr mastMat, housingMat, plateMat, driveMat, boomMat;
  MaterialPtr offRed, offYellow, onRed, onYellow;
  GeometryPtr lampGeo, smallLampGeo, mastGeo;

  void drop(Site& site);
  /** The lamps (steady, or flashing about once a second) and the barriers of a site as its installation says. */
  void pose(Site& site, double time);
  Site build(LevelCrossings& lx, const LevelCrossing& c, SignRenderer* signs);
  /** The mast with its lights, and the barrier, on the right of traffic going `dir`. */
  Side side(LevelCrossings& lx, const LevelCrossing& c, int dir, Group& group, double y0);
};

}  // namespace fs
