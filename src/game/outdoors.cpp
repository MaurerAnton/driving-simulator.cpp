// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/outdoors.ts @ v1.7.7
#include "game/outdoors.h"

#include "game/clock.h"
#include "geo/utm.h"
#include "world/lanes.h"

namespace fs {

namespace {

constexpr double HOUR = 3600000;

}  // namespace

OutdoorsClock outdoorsClock() { return {.real = gameClock.real(), .ms = [] { return gameClock.ms(); }}; }

SeasonSetting seasonShown(const OutdoorsSettings& settings) {
  return settings.season == "real" && settings.weatherFx && !*settings.weatherFx ? "summer" : settings.season;
}

Outdoors::Outdoors(Object3D& scene, const SkyParts* sky, double quality, std::unique_ptr<RealWeather> realIn)
    : real(realIn ? std::move(realIn) : std::make_unique<RealWeather>()), view(quality), season(look.state) {
  scene.add(view.group, tracks.mesh);
  view.attachSky(sky);
  // (later: water.attachSky(sky?.dome) – render/water.ts)
}

void Outdoors::patch(Material& material, const SeasonTarget& target) {
  look.patch(material, target);
  if (target == "ground" || target == "paint") view.addWetness(material);
  // (later: water.patch for 'terrain' and 'far' – render/water.ts)
}

void Outdoors::update(double dt, const Vector3& camera, const Vector3& place, const OutdoorsSettings& settings, const LaneGraph* lanes, const OutdoorsClock* clockIn) {
  const OutdoorsClock own = clockIn ? OutdoorsClock() : outdoorsClock();
  const OutdoorsClock& clock = clockIn ? *clockIn : own;
  const double ms = clock.ms();
  const std::optional<double> at = clock.real ? std::nullopt : std::optional<double>(ms);
  // the real weather where the vehicle is (asked only when RealWeather allows – cheap to try every frame)
  if (settings.weather == "real" && real->due(at) && std::isfinite(place.x) && std::isfinite(place.z)) {
    const auto [lat, lon] = worldToLatLon(place.x, place.z);
    // (nobody waits for the answer: current() has it once it is there)
    (void)real->request(lat, lon, std::isfinite(place.y) ? std::optional<double>(place.y) : std::nullopt, at);
  }
  WeatherPtr w = weatherFor(settings.weather, real.get(), at);
  const SeasonSetting seasonSet = seasonShown(settings);
  const double hour = std::floor(ms / HOUR);
  if (w != weather || hour != hour_ || settings.weather != weatherSetting_ || seasonSet != seasonSetting_) {
    const bool switched = settings.weather != weatherSetting_ || seasonSet != seasonSetting_;
    weather = w;
    hour_ = hour;
    weatherSetting_ = settings.weather;
    seasonSetting_ = seasonSet;
    season = seasonFor(seasonSet, w.get(), ms);
    look.set(season);
    grip = gripFactor(*w, season.roadSnow);
    tracks.amount = season.roadSnow;
    setWeatherNow(w, season.snow);
    // a setting changed: show it at once rather than fading over
    if (switched) view.jump();
  }
  view.update(camera, dt, *w);
  // (later: water.update(dt) – render/water.ts)
  if (lanes) tracks.update(camera.x, camera.z, lanes->lanes, &lanes->connectors);
}

void Outdoors::dispose() {
  view.dispose();
  tracks.dispose();
  setWeatherNow(CLEAR_WEATHER());
}

}  // namespace fs
