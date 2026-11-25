// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/weather.ts @ v1.7.7
/**
 * The weather in the picture: rain and snow falling round the camera, and
 * what clouds, rain and fog do to the sky, the light and the view.
 *
 * Rain and snow are one mesh each: a small quad per drop or flake, drawn
 * instanced.  The vertex shader does all the work – nothing per particle on
 * the processor, nothing allocated per frame:
 *  - every particle has a fixed place in a box round the camera; the box
 *    moves with the camera and the places wrap round it, so a particle stays
 *    where it is in the world while the camera drives through the rain;
 *  - all fall and drift with the wind (in three speed classes, so that the
 *    rain is not one rigid curtain);
 *  - a drop is drawn as a streak along its motion relative to the camera,
 *    the way a camera or an eye sees it – at speed the rain comes at you;
 *  - inside the vehicle (the "shelter", setShelter) nothing falls.
 *
 * Sky, fog and light: `look` says what the weather does to them, smoothed
 * over a few seconds.  attachSky() applies it to the game's Sky every frame
 * (and gives the sky its own values back on detach); without it the game may
 * apply the values itself.  addWetness() darkens the roads in the rain and
 * lets them mirror the sky.
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "engine/engine.h"
#include "game/weather.h"

namespace fs {

class Sky;

/** 'low' | 'medium' | 'high' | 'ultra' | 'nomercy' */
using WeatherQuality = std::string;

/** the most raindrops in the air at once, by graphics quality (snow: 60 % of it – the flakes are bigger); medium's for a quality that is none */
int WEATHER_PARTICLES(const WeatherQuality& quality);

/** What the weather does to the picture now (smoothed). */
struct WeatherLook {
  /** how far one sees (m) – weatherFog() makes fog distances of it */
  double visibility = 30000;
  /** the weather's colour of the fog and the horizon, and of the sky overhead (linear, for a clear noon) */
  Color fogColor;
  Color zenithColor;
  /** how much of those colours replaces the clear sky's (0 … 1) */
  double tint = 0;
  /** factors on the sun's and on the sky light's (hemisphere) intensity */
  double sunFactor = 1;
  double ambientFactor = 1;
  /** how wet the roads look (0 … 1) */
  double wet = 0;
  /** a lightning flash, now (0 … 1) */
  double flash = 0;
  /** how much of the rain and of the snow is drawn (0 … 1) */
  double rain = 0;
  double snow = 0;
};

/** The parts of the game's Sky that the weather changes (render/sky.h's Sky has them: skyParts()). */
struct SkyParts {
  std::shared_ptr<Fog> fog;
  LightPtr sun;
  LightPtr hemi;
  /** the dome's uZenith, uHorizon and uSunColor (colours), which are tinted – null: no dome */
  UniformPtr uZenith, uHorizon, uSunColor;
  /** the dome itself (render/water.h mirrors its colours and its sun) – null: none */
  std::shared_ptr<Mesh> dome = nullptr;
};

/** The parts of the game's Sky the weather changes. */
SkyParts skyParts(Sky& sky);

/** fog distances */
struct FogRange {
  double near = 0;
  double far = 0;
};

/**
 * Fog distances for a visibility, from the game's own (`near`, `far`: the haze
 * of a clear day, which stay as they are where one sees farther than they
 * reach).  Below that `far` shrinks to the visibility – there the fog is
 * all but complete – and `near` goes towards the camera and below zero, so
 * that, like real fog, it begins at once and thickens steadily.  Writes `out`.
 */
FogRange& weatherFog(double visibility, double near, double far, FogRange& out);

/** the vehicle the camera sits in: no rain inside its box */
struct WeatherShelter {
  Object3D* object = nullptr;
  /** the box in the object's own frame */
  Box3 box;
};

/** One kind of particle (rain or snow) in its box round the camera. */
class WeatherField {
 public:
  struct Uniforms {
    UniformPtr uBox = uniform(Vector3());
    UniformPtr uShift = uniform(std::vector<float>(9, 0.0f));
    UniformPtr uCamLocal = uniform(Vector3());
    UniformPtr uVel = uniform(Vector3());
    UniformPtr uCamVel = uniform(Vector3());
    UniformPtr uExposure = uniform(0.04);
    UniformPtr uRadius = uniform(0.003);
    UniformPtr uPixel = uniform(0.001);
    UniformPtr uTime = uniform(0.0);
    UniformPtr uFlutter = uniform(0.0);
    UniformPtr uShelter = uniform(Matrix4());
    UniformPtr uShelterOn = uniform(0.0);
    UniformPtr uColor = uniform(Color(0xffffff));
    UniformPtr uOpacity = uniform(0.3);
  } u;
  std::shared_ptr<Mesh> mesh;
  const Vector3 box;
  /** how many there may be (and are in the geometry) */
  int capacity = 0;

  WeatherField(bool snow, double capacity, const Vector3& box, double camShare, const WeatherShelter* shelter, uint32_t seed);
  WeatherField(const WeatherField&) = delete;
  WeatherField& operator=(const WeatherField&) = delete;
  ~WeatherField();

  /** How many may be drawn at most; a larger number than ever before makes a new geometry (never per frame). */
  void setCapacity(double n);
  /**
   * Move the box with the camera, let the particles fall and drift `step`
   * seconds (velocity of the middle class, m/s), draw `amount` of them.
   */
  void advance(double cx, double cy, double cz, double step, double vx, double vy, double vz, double amount);
  void dispose();

 private:
  GeometryPtr geometry(int n);
  int allocated_ = 0;
  /** the camera's height in the box, as a share of the box's height (more of the box above: it falls from there) */
  const double camShare_;
  /** how far each speed class has fallen and drifted, wrapped to the box */
  std::array<Vector3, 3> drift_;
  const uint32_t seed_;
};

/** A number of the sky that the weather changes: the value the game gave it, told from the one the weather wrote. */
class Kept {
 public:
  double base = 0;
  /** the game's value: what is there now, unless it is what the weather wrote last */
  double read(double v);
  double wrote(double v);
  void forget() { known_ = false; }

 private:
  double last_ = std::numeric_limits<double>::quiet_NaN();
  bool known_ = false;
};

class KeptColor {
 public:
  Color base;
  const Color& read(const Color& c);
  void wrote(const Color& c) { last_.copy(c); }
  void forget() { known_ = false; }

 private:
  Color last_;
  bool known_ = false;
};

class WeatherRenderer {
 public:
  /** water standing on the roads in the wet (the graphics' weatherFx, 1.7.1: "No mercy"): without it they stay dry to the eye */
  bool roadWater = true;
  /** add this to the scene */
  const std::shared_ptr<Group> group = make<Group>();
  WeatherLook look;
  /** lightning strikes so far (the sound may follow each with thunder) */
  int strikes = 0;

  /** `quality`: the most raindrops at once (WEATHER_PARTICLES has them by graphics quality) */
  explicit WeatherRenderer(double quality = WEATHER_PARTICLES("medium"));
  WeatherRenderer(const WeatherRenderer&) = delete;
  WeatherRenderer& operator=(const WeatherRenderer&) = delete;
  ~WeatherRenderer();

  /** the rain's and the snow's mesh (the group's children) */
  const std::shared_ptr<Mesh>& rainMesh() const { return rain_->mesh; }
  const std::shared_ptr<Mesh>& snowMesh() const { return snow_->mesh; }

  /** the most raindrops in the air at once (snowflakes: 60 % of it) – WEATHER_PARTICLES(quality) */
  int quality() const { return rain_->capacity; }
  void quality(double n);

  /**
   * The vehicle the camera is in or behind: no rain inside `box` (in the
   * object's own frame – e.g. its outline and height, a little more round
   * the cabin).  null: none (on foot).  (The object must stay while it is the shelter.)
   */
  void setShelter(Object3D* object, std::optional<Box3> box = std::nullopt);

  /** The next update shows the weather at once, without fading over (at the start of a drive). */
  void jump() { snapNext_ = true; }

  /**
   * Apply the look to the game's sky every update: fog distances and colour,
   * the sun and the sky light, the dome.  What the game sets there itself
   * (applySettings: fog distances; setSun: colours) is taken as the clear
   * weather's value from then on.  null: give the sky its own values back.
   */
  void attachSky(const SkyParts* sky);

  /**
   * Wet roads: darker in the rain, mirroring the sky at a glancing angle,
   * with puddles once it has rained a while.  For a MeshLambertMaterial (or
   * Phong/Standard) of the ground – the tiles' road and paint materials.
   * Recompiles the material once; the wetness itself is a uniform.
   */
  void addWetness(Material& material);

  /**
   * Once a frame: the camera's position (world, m), the time since the last
   * frame (s) and the weather to show (weatherFor()).
   */
  void update(const Vector3& camera, double dt, const WeatherState& state);

  /** the wetness uniforms (shared by every wet material) */
  const UniformPtr uFsWet = uniform(0.0);
  const UniformPtr uFsWetSky = uniform(Color(0xa0a8b0));

  /** Off the scene; the sky gets its own values back; wet materials stay patched but dry. */
  void dispose();

 private:
  /** what the weather is heading for */
  struct Target {
    double rain = 0;
    double fall = 8;
    double exposure = 0.04;
    double radius = 0.0028;
    double opacity = 0.3;
    double snow = 0;
    double windX = 0;
    double windZ = 0;
    double logVis = std::log(30000.0);
    Color fog;
    Color zenith;
    double tint = 0;
    double sun = 1;
    double ambient = 1;
    double wet = 0;
    double storm = 0;
  };
  /** Where the weather is heading: the target of the smoothing. */
  void aim(const WeatherState& s);
  /** Bring the look onto the attached sky. */
  void applySky();
  /** The sky's own values back, as the game last set them. */
  void restoreSky();

  WeatherShelter shelter_;
  std::unique_ptr<WeatherField> rain_;
  std::unique_ptr<WeatherField> snow_;
  Target target_;
  // the smoothed state (what `look` does not show)
  double logVis_ = std::log(30000.0);
  double fall_ = 8;
  double exposure_ = 0.04;
  double radius_ = 0.0028;
  double opacity_ = 0.3;
  double windX_ = 0;
  double windZ_ = 0;
  double storm_ = 0;
  double time_ = 0;
  bool snapNext_ = true;
  struct {
    double x = 0, z = 0;
  } wind_;
  // the camera's velocity
  Vector3 camVel_;
  Vector3 camLast_;
  bool hasCam_ = false;
  // lightning
  std::function<double()> rand_;
  double nextStrike_ = 8;
  double flashT_ = -1;
  // the sky, when attached
  std::optional<SkyParts> sky_;
  bool dome_ = false;
  Kept fogNear_, fogFar_, sunI_, hemiI_;
  KeptColor fogColor_, hemiColor_, zenith_, horizon_, sunColor_;
  FogRange fogOut_;
  Color tmp_;
};

}  // namespace fs
