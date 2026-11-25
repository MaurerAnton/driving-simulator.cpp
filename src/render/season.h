// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/season.ts @ v1.7.7
/**
 * The seasons: fresh green and blossom in spring, the land as it is drawn
 * now in summer, yellow, red and brown leaves and ploughed fields in autumn,
 * bare trees, dull grass and snow in winter.
 *
 * By default the season follows the game's clock (game/clock.h – the real
 * date, or the one the arcade mode set), by the phenology of northern Hesse:
 * leaves out in late April, turned in October, fallen by late November.  In
 * the "arcade" settings the player picks one.  Snow lies as the real weather
 * reports it (its snow depth) when the real weather is on – otherwise as in
 * a typical winter – and wherever snow is falling.
 *
 * Nothing is rebuilt for a season: the materials of the ground, the roads,
 * the buildings and the trees get a few lines of shader (SeasonLook.patch)
 * that read shared uniforms, so a change of season is a change of uniforms
 * (the bare trees' see-through crowns aside: one recompile of the tree
 * materials when the leaves go or come).  The colour arithmetic exists twice,
 * as GLSL and as the pure functions below – both built from the same
 * constants, so the tests of the one hold for the other.
 *
 * (The shaders are the original's, as its patch leaves them on each material it
 * patches – captured by tools/glsl/weather.mjs: src/render/glsl_gen.cpp.)
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/engine.h"
#include "game/clock.h"
#include "game/weather.h"

namespace fs {

extern const std::vector<Season> SEASONS;
/** the season setting: the real one (by the date), or one chosen ('real' | Season) */
using SeasonSetting = std::string;
extern const std::vector<SeasonSetting> SEASON_SETTINGS;

bool isSeasonSetting(const std::string& v);

struct SeasonState {
  /** the season by name (meteorological: spring is March to May …) */
  Season season = "summer";
  /** the foliage of the broadleaf trees and bushes, 0 bare … 1 full */
  double leaves = 1;
  /** the fresh, light green of young leaves, grass and crops, 0 … 1 */
  double spring = 0;
  /** fruit trees (and a few others) in blossom, 0 … 1 */
  double blossom = 0;
  /** how far the leaves have turned yellow, red and brown; fields ploughed, 0 … 1 */
  double autumn = 0;
  /** the dull grass, bare dark fields and leaf litter of the dormant season, 0 … 1 */
  double dormant = 0;
  /** snow lying on the ground, the roofs and the trees, 0 … 1 (1: all white) */
  double snow = 0;
  /** snow lying on the roads – cleared and driven, less than beside them, 0 … 1 */
  double roadSnow = 0;

  bool operator==(const SeasonState&) const = default;
};

// ------------------------------------------------------------ the dates

/** Day of the year, 1 … 365, by the local calendar of the moment `date` (ms; 29 February counts as 1 March). */
int dayOfYear(double date);

/**
 * The season on a day round Kassel (pure; the local calendar day of `date`, ms).
 * `typicalSnow`: snow as in a typical winter (lying from Christmas to
 * mid-February) – for when the real weather does not say how much there is.
 */
SeasonState seasonAt(double date, bool typicalSnow = true);

/** The seasons as the player picks them: each at its most typical.  (Summer is the land as it was always drawn.)  Summer for a name that is none. */
const SeasonState& SEASON_PRESETS(const std::string& season);

/**
 * The season to show: the one chosen, or the one of the game's day (`date`,
 * ms – by default the game's clock, gameNow()) – with the snow the real weather
 * reports (its snow depth) when it is on, and snow settling wherever it is
 * falling (not when it is warm).  A new object: call it when the setting,
 * the day or the weather changed, not every frame.
 */
SeasonState seasonFor(const SeasonSetting& setting, const WeatherState* weather = nullptr, std::optional<double> date = std::nullopt);

// ------------------------------------------------- the colours, as numbers

using RGB = std::array<double, 3>;

/**
 * the bare twigs' colour (sRGB): a light grey-brown – a bare crown is twigs with the sky between them, so it
 * reads lighter than bark (0x8c7f74, drawn as a solid, shaded mass, read as black blobs against the sky)
 */
inline constexpr uint32_t BARE_CROWN = 0xa39788;
/**
 * how far a bare crown's light ignores the shape of its crown (0 … 1): a mass of twigs has no shaded side –
 * it is lit nearly as the ground is, instead of a ball whose half away from a low sun goes dark
 */
inline constexpr double BARE_FLAT = 0.85;

/**
 * What the season does to a ground colour (linear RGB).  Green plants are
 * told by their colour (green above red and blue), fields by theirs
 * (yellow-tan: red and green alike, little blue), the forest floor by being
 * dark green; grey, brown and blue (streets, yards, water) stay.
 * Mirrors fsSeasonGround in the shaders.
 */
RGB seasonGround(const RGB& c, const SeasonState& s);

/** The colour of a ground-cover class (COVER, world/cover.h) in a season, as sRGB hex – snow aside (that the shader lays). */
uint32_t coverColor(int cls, const SeasonState& s);

/** All the cover classes' colours in a season (sRGB hex, by class) – e.g. for the far view's chunks or a map. */
std::vector<uint32_t> coverPalette(const SeasonState& s);

/** 'deciduous' | 'orchard' | 'evergreen' */
using TreeKind = std::string;

struct CrownLook {
  RGB tint;
  double cover;
};

/**
 * A tree's crown in a season: a factor on its summer colour (linear RGB) and
 * how much of the crown is drawn (1 full; a bare crown 0.45: the twigs, seen
 * through).  `r1`, `r2`: the tree's own random numbers (0 … 1) – when its
 * leaves come and go, which colour they turn; `blossomHere`: this part of the
 * crown would carry blossom.  Mirrors the tree shader.
 */
CrownLook crownLook(const SeasonState& s, const TreeKind& kind, double r1, double r2, bool blossomHere = true);

/**
 * How much of a surface the snow covers (0 … 1): `amount` of snow
 * (SeasonState.snow, or roadSnow on a road), `up` the up-ness of the surface
 * (its normal's y: 1 flat, 0 a wall), `noise` the place's noise (0 … 1:
 * thin snow lies in patches).  Ground and roads take the band 0.55 … 0.8,
 * roofs 0.4 … 0.65 (snow holds on a pitched roof), trees 0.2 … 0.55.
 * Mirrors fsSnowCover in the shaders.
 */
double snowCover(double amount, double up, double lo, double hi, double noise = 0.5);

// ------------------------------------------------------- the season look

/** What can be patched: 'terrain' | 'far' | 'ground' | 'paint' | 'building' | 'struct' | TreeKind. */
using SeasonTarget = std::string;

/**
 * The season in the materials: patch() each material once (terrain, far view,
 * roads, paint, buildings, bridges, the trees of each kind), then set() the
 * season whenever it changes – shared uniforms, nothing rebuilt.
 */
class SeasonLook {
 public:
  struct Uniforms {
    UniformPtr uFsLeaves = uniform(1.0);
    UniformPtr uFsSpring = uniform(0.0);
    UniformPtr uFsBlossom = uniform(0.0);
    UniformPtr uFsAutumn = uniform(0.0);
    UniformPtr uFsDormant = uniform(0.0);
    UniformPtr uFsSnow = uniform(0.0);
    UniformPtr uFsRoadSnow = uniform(0.0);
  } uniforms;
  /** the season shown */
  SeasonState state = SEASON_PRESETS("summer");

  /** Show a season (seasonFor()).  Cheap – uniforms – unless the crowns turn thin or full: then the tree materials compile anew. */
  void set(const SeasonState& s);

  /** The bare crowns are drawn see-through now (their shadows are not: the game may want tree shadows off then). */
  bool thinCrowns() const { return thin_; }

  /**
   * Teach a material the seasons (once; a second call does nothing).  For
   * the MeshLambertMaterials of the game: 'terrain' (the tiles' terrain),
   * 'far' (the far view), 'ground' and 'paint' (the tiles' road and marking
   * materials – 'ground' needs their vFsMat), 'building' (both building
   * materials), 'struct' (bridges, walls), and the trees: 'deciduous'
   * (broadleaf trees, bushes), 'orchard' (fruit and garden trees: blossom),
   * 'evergreen' (conifers) – vertex-coloured, green crowns, brown trunks.
   * (The material must outlive this look: the trees are patched anew when their crowns get thin.)
   */
  void patch(Material& material, const SeasonTarget& target);

 private:
  struct Patched {
    /** the material's own program key before the season (its patch's – '' without one) */
    std::string ownKey;
    SeasonTarget target;
    /** the uniforms of what the material had before */
    std::map<std::string, UniformPtr> ownUniforms;
  };
  /** Put the season's shader on a material (by its own key, the target and whether the crowns are thin). */
  void apply(Material& material, const Patched& p);
  /** the tree materials and what they had before the season */
  std::unordered_map<const Material*, Patched> patched_;
  /** the tree materials: recompiled when the crowns get thin or full again */
  std::vector<Material*> trees_;
  bool thin_ = false;
};

}  // namespace fs
