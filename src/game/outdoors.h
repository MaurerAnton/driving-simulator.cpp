// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/outdoors.ts @ v1.7.7
/**
 * The weather and the season, put together for the game: one object it
 * makes once and updates once a frame.
 *
 * It asks for the real weather where the vehicle is (when the setting wants
 * it, as often as RealWeather allows, at the game clock's moment), picks the
 * weather to show (weatherFor), works out the season (seasonFor – once an hour
 * of game time, or when the weather or the settings change), and from both:
 * the rain and snow in the picture and the sky's fog and light
 * (WeatherRenderer), the season's colours and snow in the materials
 * (SeasonLook), the wheel tracks on a snowy road (SnowTracks), the grip of
 * the tyres (`grip`, for WorldGround.weatherGrip) and the weather "now" that
 * any other part of the game may ask (setWeatherNow).
 *
 * (Real water in the terrain and the far view – render/water.ts WaterLook, the original's `water` here – is not
 * ported yet: the terrain and the far view are taught the season only.)
 */
#pragma once

#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "engine/engine.h"
#include "game/weather.h"
#include "render/season.h"
#include "render/snowtracks.h"
#include "render/weather.h"

namespace fs {

class LaneGraph;

/** what of the game's clock is needed (game/clock.h's gameClock fits: outdoorsClock()) */
struct OutdoorsClock {
  /** at the real time (the current weather), or at a moment of its own (the forecast for that hour) */
  bool real = true;
  std::function<double()> ms{};
};

/** the game's clock as Outdoors reads it */
OutdoorsClock outdoorsClock();

/** what of the settings is needed (game/settings.h's Settings has them) */
struct OutdoorsSettings {
  WeatherSetting weather = "real";
  SeasonSetting season = "real";
  /** the graphics' weatherFx (none: no graphics given) */
  std::optional<bool> weatherFx{};
};

/**
 * The season to show (1.7.1): a season chosen is shown; "by the date" only with the weather on the vehicle and the
 * roads (the graphics' weatherFx – "No mercy") – otherwise it is always summer.
 */
SeasonSetting seasonShown(const OutdoorsSettings& settings);

class Outdoors {
 public:
  const std::unique_ptr<RealWeather> real;
  WeatherRenderer view;
  SeasonLook look;
  SnowTracks tracks;
  /** the weather and the season shown now */
  WeatherPtr weather = CLEAR_WEATHER();
  SeasonState season;
  /** what the weather and the snow on the road leave of the tyres' grip (for WorldGround.weatherGrip) */
  double grip = 1;

  /**
   * `scene`: where the rain, the snow and the tracks are added; `sky`: the
   * game's Sky (its fog, sun, sky light and dome follow the weather);
   * `quality`: the most raindrops at once (WEATHER_PARTICLES).
   */
  Outdoors(Object3D& scene, const SkyParts* sky, double quality = WEATHER_PARTICLES("medium"), std::unique_ptr<RealWeather> real = nullptr);
  Outdoors(const Outdoors&) = delete;
  Outdoors& operator=(const Outdoors&) = delete;

  /**
   * Teach a material the season – and, the roads and their markings, the
   * wet: for the tiles' materials (terrain, ground, paint, struct, building),
   * the far view's, the trees' of each kind.  Once per material.
   */
  void patch(Material& material, const SeasonTarget& target);

  /**
   * Once a frame, before the picture is drawn (and after the sky's sun was
   * set for the frame): `dt` seconds since the last, the camera's position,
   * the place of the vehicle or walker (x, z; y its ground height, NaN when
   * not known), the settings, the lanes for the wheel tracks (null: none)
   * and the game's clock (null: gameClock).
   */
  void update(double dt, const Vector3& camera, const Vector3& place, const OutdoorsSettings& settings, const LaneGraph* lanes = nullptr,
              const OutdoorsClock* clock = nullptr);

  void dispose();

 private:
  WeatherSetting weatherSetting_;
  SeasonSetting seasonSetting_;
  double hour_ = std::numeric_limits<double>::quiet_NaN();
};

}  // namespace fs
