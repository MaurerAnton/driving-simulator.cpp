// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/weather.ts @ v1.7.7
#include "game/weather.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "core/date.h"
#include "core/js.h"
#include "game/clock.h"

namespace fs {

const std::vector<WeatherKind> WEATHER_KINDS = {"clear", "cloudy", "fog", "drizzle", "rain", "snow", "storm"};
const std::vector<WeatherSetting> WEATHER_SETTINGS = {"off", "real", "cloudy", "fog", "drizzle", "rain", "snow", "storm"};

bool isWeatherSetting(const std::string& v) { return std::find(WEATHER_SETTINGS.begin(), WEATHER_SETTINGS.end(), v) != WEATHER_SETTINGS.end(); }

// ------------------------------------------------------------ fixed weather

namespace {

double clamp01(double v) { return v <= 0 ? 0 : v >= 1 ? 1 : v; }

WeatherPtr fixed(WeatherState s, const std::string& source = "fixed") {
  s.source = source;
  return std::make_shared<const WeatherState>(std::move(s));
}

WeatherState state(const char* kind, int code, double intensity, double cloud, double visibility, double wind, double gust, double windFrom, double temperature,
                   bool isDay, double wet, double ice, double snowDepth) {
  WeatherState s;
  s.kind = kind;
  s.code = code;
  s.intensity = intensity;
  s.cloud = cloud;
  s.visibility = visibility;
  s.wind = wind;
  s.gust = gust;
  s.windFrom = windFrom;
  s.temperature = temperature;
  s.isDay = isDay;
  s.wet = wet;
  s.ice = ice;
  s.snowDepth = snowDepth;
  return s;
}

/** (static tables that outlive everything: docs/STATUS.md) */
const std::map<std::string, WeatherPtr>& fixedTable() {
  static const auto* t = new std::map<std::string, WeatherPtr>{
      {"clear", fixed(state("clear", 0, 0, 0.1, 30000, 2.5, 5, 250, 16, true, 0, 0, 0))},
      {"cloudy", fixed(state("cloudy", 3, 0, 0.9, 20000, 4, 8, 240, 11, true, 0, 0, 0))},
      // thick enough to need the fog lamps, thin enough to drive by
      {"fog", fixed(state("fog", 45, 0.8, 1, 120, 1, 2, 200, 5, true, 0.3, 0, 0))},
      {"drizzle", fixed(state("drizzle", 53, 0.55, 1, 5000, 3, 6, 240, 9, true, 0.72, 0, 0))},
      // a moderate rain from the south-west, the usual one in Hesse
      {"rain", fixed(state("rain", 63, 0.6, 1, 4000, 5, 10, 240, 11, true, 0.9, 0, 0))},
      {"snow", fixed(state("snow", 73, 0.55, 1, 1500, 3, 6, 60, -2, true, 0.25, 0, 0.03))},
      {"storm", fixed(state("storm", 95, 0.9, 1, 2000, 13, 24, 250, 19, true, 1, 0, 0))},
  };
  return *t;
}

}  // namespace

const WeatherPtr& FIXED_WEATHER(const std::string& kind) {
  static const auto* none = new WeatherPtr();
  const auto& t = fixedTable();
  auto it = t.find(kind);
  return it != t.end() ? it->second : *none;
}

const WeatherPtr& CLEAR_WEATHER() {
  static const auto* c = new WeatherPtr(fixed(*FIXED_WEATHER("clear"), "none"));
  return *c;
}

WeatherPtr weatherFor(const WeatherSetting& setting, const RealWeather* real, std::optional<double> at) {
  if (setting == "off") return CLEAR_WEATHER();
  if (setting != "real") {
    const WeatherPtr& f = FIXED_WEATHER(setting);
    return f ? f : CLEAR_WEATHER();
  }
  WeatherPtr s = real ? real->current(at) : nullptr;
  if (s) return s;
  if (at && std::isfinite(*at) && !(real ? real->covers(at) : false)) return climateWeather(*at);
  return CLEAR_WEATHER();
}

// ------------------------------------------------------------ grip and wind

double gripFactor(const WeatherState& s, double roadSnow) {
  // the film of water on a wet road
  double g = 1 - 0.2 * clamp01(s.wet);
  // heavy rain: water stands in the ruts, the tyres begin to float
  if (s.kind == "rain" || s.kind == "storm") g -= 0.1 * clamp01(s.intensity);
  // fresh snow on the road
  if (s.kind == "snow") g = std::min(g, 0.55 - 0.15 * clamp01(s.intensity));
  // snow lying on the road
  g = std::min(g, 1 - 0.5 * clamp01(std::isfinite(roadSnow) ? roadSnow : 0));
  // glaze from freezing rain, rime, a wet road frozen
  g = std::min(g, 1 - 0.7 * clamp01(s.ice));
  return std::min(1.0, std::max(0.25, g));
}

// ------------------------------------------ the weather now, for anyone

namespace {

WeatherPtr& shownPtr() {
  static auto* s = new WeatherPtr(CLEAR_WEATHER());
  return *s;
}
double shownGroundSnow = -1;

}  // namespace

const WeatherState& weatherNow() { return *shownPtr(); }
const WeatherPtr& weatherNowPtr() { return shownPtr(); }

void setWeatherNow(WeatherPtr s, std::optional<double> groundSnow) {
  shownPtr() = s ? std::move(s) : CLEAR_WEATHER();
  shownGroundSnow = groundSnow && std::isfinite(*groundSnow) ? clamp01(*groundSnow) : -1;
}

double rainIntensity(const WeatherState& s) { return s.kind == "drizzle" || s.kind == "rain" || s.kind == "storm" ? clamp01(s.intensity) : 0; }
double rainIntensity() { return rainIntensity(weatherNow()); }

bool isRaining(const WeatherState& s) { return rainIntensity(s) > 0; }
bool isRaining() { return isRaining(weatherNow()); }

double snowIntensity(const WeatherState& s) { return s.kind == "snow" ? clamp01(s.intensity) : 0; }
double snowIntensity() { return snowIntensity(weatherNow()); }

double windSpeed(const WeatherState& s) { return std::max(0.0, s.wind); }
double windSpeed() { return windSpeed(weatherNow()); }

double snowOnGround(const WeatherState& w) {
  double c = clamp01(w.snowDepth / FULL_SNOW_DEPTH);
  if (w.kind == "snow" && w.intensity > 0 && w.temperature <= 1.5) c = std::max(c, 0.3 + 0.5 * clamp01(w.intensity));
  return c;
}

double snowOnGround() {
  if (shownGroundSnow >= 0) return shownGroundSnow;
  return snowOnGround(weatherNow());
}

// ---------------------------------------------------- the WMO weather codes

namespace {

/** What a WMO present-weather code (table 4677, 0 … 99) says before the measured amounts refine it. */
struct CodeInfo {
  /** 'sky': clear or cloudy as the cloud cover says; 'mixed': rain and snow – one or the other by the temperature */
  std::string kind;
  /** how hard it falls as the code words it (slight, moderate, heavy) – the least the intensity will be */
  double level;
  /** what the code says of the visibility (m): fog is fog below 1 km whatever the model's number */
  double vis;
  /** the road still wet from what fell in the last hour */
  double wet;
  /** ice on the road */
  double ice;
};

CodeInfo info(const char* kind, double level = 0, double vis = JS_INF, double wet = 0, double ice = 0) { return {kind, level, vis, wet, ice}; }

/** slight … heavy, intermittent or continuous (the steps of 50–55, 60–65, 70–75) */
constexpr double STEPS[] = {0.2, 0.3, 0.45, 0.55, 0.75, 0.85};

/**
 * The full table 4677: Open-Meteo gives only some of its codes (0–3, 45, 48,
 * 51–57, 61–67, 71–77, 80–86, 95–99), but any of them may come.
 */
CodeInfo codeInfo(int code) {
  // 0–3: clear, mainly clear, partly cloudy, overcast (Open-Meteo's reading of "cloud development")
  if (code <= 3) return info("sky");
  // smoke, haze, dust in the air
  if (code <= 6) return info("sky", 0, 5000);
  // dust or sand raised by the wind, whirls, a duststorm in sight
  if (code <= 9) return info("sky", 0, 3000);
  if (code == 10) return info("fog", 0.25, 2500);   // mist
  if (code <= 12) return info("fog", 0.45, 800);    // shallow fog
  // lightning without thunder, precipitation in sight but not here
  if (code <= 16) return info("sky");
  if (code == 17) return info("storm");   // thunder, nothing falling
  if (code == 18) return info("sky");     // squalls (the wind says how strong)
  if (code == 19) return info("storm");   // funnel cloud
  // 20–29: something fell in the last hour, nothing now – the road is still wet
  if (code <= 29) {
    switch (code) {
      case 20: return info("sky", 0, JS_INF, 0.5);   // drizzle or snow grains
      case 22: return info("sky", 0, JS_INF, 0.2);   // snow
      case 24: return info("sky", 0, JS_INF, 0.6, 0.8);   // freezing drizzle or rain
      case 26: return info("sky", 0, JS_INF, 0.3);   // snow showers
      case 28: return info("sky", 0, 5000, 0.2);     // fog
      default: return info("sky", 0, JS_INF, code == 23 || code == 25 || code == 27 ? 0.6 : 0.7);   // rain, showers, thunderstorm
    }
  }
  // dust- and sandstorms
  if (code <= 35) return info("sky", 0, code >= 33 ? 500 : 1500);
  // drifting snow below eye level, blowing snow above it
  if (code <= 39) {
    static constexpr double level[] = {0.2, 0.35, 0.5, 0.85};
    static constexpr double vis[] = {4000, 2000, 1000, 300};
    return info("snow", level[code - 36], vis[code - 36]);
  }
  if (code == 40) return info("sky", 0, 5000);   // fog at a distance
  if (code == 41) return info("fog", 0.4, 800);  // fog in patches
  // 42–47: fog, the sky seen through it (even codes) or not
  if (code <= 47) return code % 2 == 0 ? info("fog", 0.55, 600) : info("fog", 0.8, 300);
  // depositing rime: ice on the road
  if (code <= 49) return info("fog", 0.75, 350, 0.3, 0.5);
  // drizzle
  if (code <= 55) return info("drizzle", STEPS[code - 50]);
  if (code == 56) return info("drizzle", 0.3, JS_INF, 0, 1);   // freezing
  if (code == 57) return info("drizzle", 0.7, JS_INF, 0, 1);
  if (code == 58) return info("rain", 0.3);   // drizzle and rain
  if (code == 59) return info("rain", 0.6);
  // rain
  if (code <= 65) return info("rain", STEPS[code - 60]);
  if (code == 66) return info("rain", 0.3, JS_INF, 0, 1);   // freezing
  if (code == 67) return info("rain", 0.7, JS_INF, 0, 1);
  if (code == 68) return info("mixed", 0.3);   // rain (or drizzle) and snow
  if (code == 69) return info("mixed", 0.65);
  // snow
  if (code <= 75) return info("snow", STEPS[code - 70]);
  if (code == 76) return info("snow", 0.1);    // diamond dust
  if (code == 77) return info("snow", 0.25);   // snow grains
  if (code == 78) return info("snow", 0.1);    // single star-like crystals
  if (code == 79) return info("snow", 0.4);    // ice pellets
  // showers
  if (code <= 82) {
    static constexpr double level[] = {0.35, 0.65, 1};
    return info("rain", level[code - 80]);   // of rain: slight, moderate or heavy, violent
  }
  if (code <= 84) return info("mixed", code == 83 ? 0.35 : 0.65);   // of rain and snow
  if (code <= 86) return info("snow", code == 85 ? 0.35 : 0.7);     // of snow
  if (code <= 88) return info("snow", code == 87 ? 0.35 : 0.6);     // of snow pellets, small hail
  if (code <= 90) return info("rain", code == 89 ? 0.5 : 0.8);      // of hail, with rain, no thunder
  // rain or snow now, a thunderstorm in the last hour
  if (code <= 92) return info("rain", code == 91 ? 0.3 : 0.65);
  if (code <= 94) return info("mixed", code == 93 ? 0.3 : 0.65);
  // thunderstorms: slight or moderate, with hail, heavy, with a duststorm, heavy with hail
  if (code == 98) return info("storm", 0.5, 1500);
  static constexpr double storm[] = {0.6, 0.75, 0.9, 0, 1};
  return info("storm", storm[code - 95]);
}

// --------------------------------------------------------- the API's answer

double num(const std::optional<double>& v, double fallback) { return v && std::isfinite(*v) ? *v : fallback; }

/** a member of an answer as a number (none: not there, or not a number) */
std::optional<double> numberOf(const Json& o, std::string_view key) {
  const Json& v = o.at(key);
  if (v.isNumber()) return v.number;
  return std::nullopt;
}

OpenMeteoCurrent currentOf(const Json& c) {
  OpenMeteoCurrent o;
  if (c.at("time").isString()) o.time = c.at("time").str;
  o.interval = numberOf(c, "interval");
  o.temperature_2m = numberOf(c, "temperature_2m");
  o.weather_code = numberOf(c, "weather_code");
  o.precipitation = numberOf(c, "precipitation");
  o.rain = numberOf(c, "rain");
  o.showers = numberOf(c, "showers");
  o.snowfall = numberOf(c, "snowfall");
  o.cloud_cover = numberOf(c, "cloud_cover");
  o.visibility = numberOf(c, "visibility");
  o.wind_speed_10m = numberOf(c, "wind_speed_10m");
  o.wind_direction_10m = numberOf(c, "wind_direction_10m");
  o.wind_gusts_10m = numberOf(c, "wind_gusts_10m");
  o.is_day = numberOf(c, "is_day");
  o.snow_depth = numberOf(c, "snow_depth");
  return o;
}

}  // namespace

WeatherState weatherFromOpenMeteo(const OpenMeteoCurrent& c) {
  const double temperature = num(c.temperature_2m, 10);
  const double interval = num(c.interval, 900) > 0 ? num(c.interval, 900) : 900;
  const double perHour = 3600 / interval;
  // all that fell, as water: precipitation, or else its parts (snow: 7 cm make 10 mm)
  const double snowWater = (std::max(0.0, num(c.snowfall, 0)) * 10) / 7;
  const double total = std::max(0.0, num(c.precipitation, std::max(0.0, num(c.rain, 0)) + std::max(0.0, num(c.showers, 0)) + snowWater));
  const double rate = total * perHour;   // mm/h

  const std::optional<double>& rawCode = c.weather_code;
  const int code = rawCode && std::isfinite(*rawCode) && std::floor(*rawCode) == *rawCode && *rawCode >= 0 && *rawCode <= 99 ? static_cast<int>(*rawCode) : -1;
  CodeInfo ci;
  if (code >= 0) ci = codeInfo(code);
  else if (rate > 0.05) {
    // no code: what falls, by the amounts
    ci = snowWater * perHour > rate * 0.5 ? info("snow", 0.2) : info(rate < 0.5 ? "drizzle" : "rain", 0.2);
  } else ci = info("sky");

  const std::optional<double>& cloudApi = c.cloud_cover;
  static constexpr double SKY_CLOUD[] = {0, 0.25, 0.55, 1};
  double cloud = cloudApi && std::isfinite(*cloudApi) ? clamp01(*cloudApi / 100) : code >= 0 && code <= 3 ? SKY_CLOUD[code] : 0.6;

  WeatherKind kind;
  if (ci.kind == "sky") kind = cloud >= 0.65 ? "cloudy" : "clear";
  else if (ci.kind == "mixed") kind = temperature <= 1 ? "snow" : "rain";   // sleet: whichever it looks more like
  else kind = ci.kind;

  // how hard: as the code words it, or as much as the amounts say (light rain 1 mm/h, heavy 8 and more)
  double intensity = ci.level;
  if (kind == "rain" || kind == "storm") intensity = std::max(intensity, std::sqrt(rate / 16));
  else if (kind == "drizzle") intensity = std::max(intensity, std::sqrt(rate / 1.6));
  else if (kind == "snow") intensity = std::max(intensity, std::sqrt(rate / 3.5));
  intensity = clamp01(intensity);
  const bool falls = (kind == "rain" || kind == "drizzle" || kind == "snow" || kind == "storm") && intensity > 0;
  if (falls) cloud = std::max(cloud, kind == "storm" ? 0.9 : 0.85);

  // how far one sees: the model's number, no farther than the code and what falls allow
  double visibility = std::min(num(c.visibility, 25000) > 0 ? num(c.visibility, 25000) : 25000, ci.vis);
  if (falls) {
    const double dry = 1 - intensity;
    const double limit = kind == "snow" ? 250 + 8000 * dry * dry : kind == "drizzle" ? 1500 + 8000 * dry * dry : kind == "storm" ? 1000 + 12000 * dry * dry : 1500 + 20000 * dry * dry;
    visibility = std::min(visibility, limit);
  }
  // a dry sky with the view below a kilometre is fog, whatever the code
  if ((kind == "clear" || kind == "cloudy") && visibility < 1000) {
    kind = "fog";
    intensity = std::max(0.3, 1 - visibility / 1000);
  } else if (kind == "fog") intensity = std::max(intensity, clamp01(1 - visibility / 1000));
  visibility = std::min(50000.0, std::max(30.0, visibility));

  // the road: wet in rain, damp in fog, slush when snow falls above freezing
  double wet = ci.wet;
  if ((kind == "rain" || kind == "storm") && intensity > 0) wet = 0.75 + 0.25 * intensity;
  else if (kind == "drizzle") wet = 0.55 + 0.3 * intensity;
  else if (kind == "snow") wet = temperature > 1.5 ? 0.7 : 0.25;
  else if (kind == "fog") wet = std::max(wet, 0.3);
  // it fell in the last quarter of an hour, even if the code says nothing falls now
  if (kind != "snow" && rate > 0) wet = std::max(wet, 0.6);
  double ice = ci.ice;
  if ((kind == "rain" || kind == "drizzle" || kind == "storm") && intensity > 0 && temperature <= -0.5) ice = std::max(ice, 0.8);   // rain on frozen ground
  else if (kind == "fog" && temperature <= -0.5) ice = std::max(ice, 0.4);   // freezing fog leaves rime
  else if (kind != "snow" && wet >= 0.5 && temperature <= -1) ice = std::max(ice, 0.6);   // the wet road freezes

  const double dir = num(c.wind_direction_10m, 240);
  const double wind = std::max(0.0, num(c.wind_speed_10m, 2));
  WeatherState s;
  s.kind = kind;
  s.code = code;
  s.intensity = intensity;
  s.cloud = cloud;
  s.visibility = visibility;
  s.wind = wind;
  s.gust = std::max(wind, num(c.wind_gusts_10m, wind * 1.5));
  s.windFrom = std::fmod(std::fmod(dir, 360) + 360, 360);
  s.temperature = temperature;
  s.isDay = num(c.is_day, 1) != 0;
  s.wet = clamp01(wet);
  s.ice = clamp01(ice);
  s.snowDepth = std::min(5.0, std::max(0.0, num(c.snow_depth, 0)));
  s.source = "real";
  return s;
}

WeatherPtr parseOpenMeteo(const Json& json) {
  if (!json.isObject()) return nullptr;
  const Json& current = json.at("current");
  if (!current.isObject()) return nullptr;
  return std::make_shared<const WeatherState>(weatherFromOpenMeteo(currentOf(current)));
}

namespace {

/** what is asked for, now or hour by hour */
const std::vector<std::string>& fieldList() {
  static const auto* f = new std::vector<std::string>{"temperature_2m", "weather_code", "precipitation", "rain", "showers", "snowfall", "snow_depth",
                                                      "cloud_cover", "visibility", "wind_speed_10m", "wind_direction_10m", "wind_gusts_10m", "is_day"};
  return *f;
}
const std::string& fields() {
  static const auto* f = new std::string(join(fieldList(), ","));
  return *f;
}

}  // namespace

std::optional<WeatherHours> parseOpenMeteoHourly(const Json& json) {
  if (!json.isObject()) return std::nullopt;
  const Json& h = json.at("hourly");
  if (!h.isObject()) return std::nullopt;
  const Json& time = h.at("time");
  if (!time.isArray()) return std::nullopt;
  const std::vector<std::string>& list = fieldList();
  std::vector<const Json*> cols;
  for (const std::string& f : list) cols.push_back(h.at(f).isArray() ? &h.at(f) : nullptr);
  WeatherHours out;
  for (size_t i = 0; i < time.array.size(); i++) {
    const Json& t = time.array[i];
    if (!t.isNumber() || !std::isfinite(t.number)) continue;
    // (the sums – precipitation, snowfall – are of the hour before)
    Json c = Json::makeObject();
    c.set("interval", 3600);
    bool any = false;
    for (size_t k = 0; k < list.size(); k++) {
      if (!cols[k] || i >= cols[k]->array.size()) continue;
      const Json& v = cols[k]->array[i];
      if (!v.isNumber()) continue;
      c.set(list[k], v.number);
      if (list[k] == "weather_code" || list[k] == "temperature_2m") any = true;
    }
    if (!any) continue;
    out.t.push_back(t.number * 1000);
    out.s.push_back(std::make_shared<const WeatherState>(weatherFromOpenMeteo(currentOf(c))));
  }
  if (out.t.empty()) return std::nullopt;
  return out;
}

// ---------------------------------------------- the weather of the climate

namespace {

constexpr double HOUR = WEATHER_HOUR;

/** Kassel's climate by month (DWD normals 1991–2020, rounded): mean temperature (°C), half the day's range (°C), cloud cover */
constexpr std::array<std::array<double, 3>, 12> CLIMATE = {{
    {1.0, 2.0, 0.8},
    {1.6, 2.5, 0.75},
    {4.8, 3.5, 0.7},
    {9.2, 4.5, 0.62},
    {13.3, 5.0, 0.6},
    {16.4, 5.0, 0.6},
    {18.4, 5.0, 0.58},
    {17.9, 5.0, 0.55},
    {14.0, 4.5, 0.6},
    {9.5, 3.5, 0.7},
    {5.0, 2.2, 0.82},
    {1.8, 1.8, 0.83},
}};
double climateHour = JS_NAN;

WeatherPtr& climateState() {
  static auto* s = new WeatherPtr(CLEAR_WEATHER());
  return *s;
}

}  // namespace

WeatherPtr climateWeather(double ms) {
  const double hour = std::floor(ms / HOUR);
  if (hour == climateHour) return climateState();
  const int month = utcFields(hour * HOUR).month;
  const std::array<double, 3>& c = month >= 0 && month < 12 ? CLIMATE[static_cast<size_t>(month)] : CLIMATE[0];
  const double mean = c[0], half = c[1], cloud = c[2];
  // the sun's time here (9.45° E); the warmest is at about half past two
  const double solar = std::fmod(std::fmod(ms / HOUR + 9.45 / 15, 24) + 24, 24);
  const double temperature = jsRound((mean + half * std::cos(((solar - 14.5) / 24) * 2 * 3.14159265358979323846)) * 10) / 10;
  const double grey = clamp01((cloud - 0.55) / 0.3);
  const double wind = 3 + 1.2 * grey;
  const WeatherKind kind = cloud >= 0.65 ? "cloudy" : "clear";
  WeatherState s;
  s.kind = kind;
  s.code = kind == "cloudy" ? 3 : 2;
  s.intensity = 0;
  s.cloud = cloud;
  s.visibility = 24000 - 10000 * grey;
  s.wind = wind;
  s.gust = wind * 2;
  s.windFrom = 240;
  s.temperature = temperature;
  s.isDay = sunAt(ms).elevation > -0.833;
  s.wet = 0;
  s.ice = 0;
  s.snowDepth = 0;
  s.source = "climate";
  climateState() = std::make_shared<const WeatherState>(std::move(s));
  climateHour = hour;
  return climateState();
}

// ------------------------------------------------------------ the request

namespace {

/** a forecast for another moment stays good this long (ms) */
constexpr double FORECAST_MAX_AGE_MS = 12 * HOUR;
/** a game moment this close to the real one is "now": the current weather */
constexpr double NOW_SLACK = 10 * 60000;
/** hours asked for at once for another moment: from the hour before it, a day on (at the fastest clock three real hours) */
constexpr double BLOCK_HOURS = 25;

/** "2026-11-25T14:00" (UTC) */
std::string isoHour(double ms) {
  const DateFields f = utcFields(std::floor(ms / HOUR) * HOUR);
  const auto two = [](int v) { return padStart(std::to_string(v), 2, '0'); };
  return padStart(std::to_string(f.year), 4, '0') + "-" + two(f.month + 1) + "-" + two(f.date) + "T" + two(f.hours) + ":00";
}

}  // namespace

std::string weatherUrl(double lat, double lon, std::optional<double> elevation, std::optional<WeatherSpan> hours) {
  const double la = std::max(-90.0, std::min(90.0, jsRound(lat * 10) / 10));
  const double lo = std::max(-180.0, std::min(180.0, jsRound(lon * 10) / 10));
  std::string url = OPEN_METEO_URL + "?latitude=" + toFixed(la, 1) + "&longitude=" + toFixed(lo, 1);
  url += hours ? "&hourly=" + fields() + "&timeformat=unixtime&start_hour=" + isoHour(hours->from) + "&end_hour=" + isoHour(hours->to) : "&current=" + fields();
  url += "&wind_speed_unit=ms&timezone=GMT";
  if (elevation && std::isfinite(*elevation) && *elevation > -450 && *elevation < 9000) url += "&elevation=" + jsStr(jsRound(*elevation / 10) * 10);
  return url;
}

RealWeather::RealWeather(RealWeatherOptions opts)
    : refreshMs(opts.refreshMs.value_or(WEATHER_REFRESH_MS)),
      minGapMs(opts.minGapMs.value_or(WEATHER_MIN_GAP_MS)),
      timeoutMs(opts.timeoutMs.value_or(WEATHER_TIMEOUT_MS)),
      maxAgeMs(opts.maxAgeMs.value_or(WEATHER_MAX_AGE_MS)),
      fetchFn_(opts.fetch ? std::move(opts.fetch) : WeatherFetch([](plat::Request r) { return plat::fetch(std::move(r)); })),
      now_(opts.now ? std::move(opts.now) : std::function<double()>([] { return plat::dateMs(); })) {}

bool RealWeather::isNow(std::optional<double> at, double now) const { return !at || !std::isfinite(*at) || std::abs(*at - now) < NOW_SLACK; }

bool RealWeather::covers(std::optional<double> at) const {
  const double now = now_();
  if (isNow(at, now)) return true;
  const double t = *at;
  if (!(t >= now - FORECAST_BACK_MS && t <= now + FORECAST_AHEAD_MS)) return false;
  return !(blockEmpty_ && t >= blockFrom_ && t <= blockTo_);
}

WeatherPtr RealWeather::current(std::optional<double> at) const {
  const double now = now_();
  if (isNow(at, now)) return last_ && now - lastAt_ <= maxAgeMs ? last_ : nullptr;
  if (!hours_ || now - hoursAt_ > FORECAST_MAX_AGE_MS) return nullptr;
  const WeatherHours& h = *hours_;
  const double t = *at;
  // the nearest hour (no more than three quarters of an hour away)
  int best = -1;
  double bestD = 0.75 * HOUR;
  for (size_t i = 0; i < h.t.size(); i++) {
    const double d = std::abs(h.t[i] - t);
    if (d <= bestD) {
      best = static_cast<int>(i);
      bestD = d;
    }
  }
  return best >= 0 ? h.s[static_cast<size_t>(best)] : nullptr;
}

bool RealWeather::due(std::optional<double> at) const {
  if (inflight_ || !fetchFn_) return false;
  const double now = now_();
  if (isNow(at, now)) return now - askedNow_ >= refreshMs;
  if (!covers(at)) return false;
  const double t = *at;
  // within the hours asked last (an hour to spare for a clock that runs fast): refreshed as seldom as the current weather
  if (t >= blockFrom_ && t <= blockTo_ - HOUR) return now - askedHours_ >= refreshMs;
  // another moment: soon – but not more often than minGapMs
  return now - askedAny_ >= minGapMs;
}

std::optional<Task<WeatherPtr>> RealWeather::request(double lat, double lon, std::optional<double> elevation, std::optional<double> at) {
  if (!std::isfinite(lat) || !std::isfinite(lon) || !due(at)) return std::nullopt;
  const double now = now_();
  askedAny_ = now;
  requests++;
  inflight_ = true;
  if (isNow(at, now)) {
    askedNow_ = now;
    return askNow(weatherUrl(lat, lon, elevation));
  }
  const double t = *at;
  // from the hour before the moment a day on, within what the service tells
  const double from = std::max(std::floor(t / HOUR) * HOUR - HOUR, std::ceil((now - FORECAST_BACK_MS) / HOUR) * HOUR);
  const double to = std::max(from, std::min(from + BLOCK_HOURS * HOUR, std::floor((now + FORECAST_AHEAD_MS) / HOUR) * HOUR));
  askedHours_ = now;
  blockFrom_ = from;
  blockTo_ = to;
  blockEmpty_ = false;
  return askHours(weatherUrl(lat, lon, elevation, WeatherSpan{from, to}), from, t);
}

Task<WeatherPtr> RealWeather::askNow(std::string url) {
  std::weak_ptr<bool> life = life_;
  std::optional<Loaded> answer = co_await load(std::move(url));
  if (life.expired()) co_return nullptr;
  WeatherPtr s = answer && answer->json ? parseOpenMeteo(*answer->json) : nullptr;
  if (s) {
    last_ = s;
    lastAt_ = now_();
  }
  inflight_ = false;
  co_return s;
}

Task<WeatherPtr> RealWeather::askHours(std::string url, double from, double t) {
  std::weak_ptr<bool> life = life_;
  std::optional<Loaded> answer = co_await load(std::move(url));
  if (life.expired()) co_return nullptr;
  std::optional<WeatherHours> hours = answer && answer->json ? parseOpenMeteoHourly(*answer->json) : std::nullopt;
  if (hours) {
    hours_ = std::move(hours);
    hoursAt_ = now_();
  } else if (answer && blockFrom_ == from) blockEmpty_ = true;   // answered – but nothing for those hours
  WeatherPtr s = current(t);
  inflight_ = false;
  co_return s;
}

namespace {

/** the fetch itself: its answer handed to `done` (null for none at all), unless the time ran out first */
Task<void> fetchInto(WeatherFetch fetchFn, plat::Request req, Resolver<std::optional<std::optional<Json>>> done) {
  plat::Response res = co_await fetchFn(std::move(req));
  if (!res.ok()) {
    if (res.status >= 400 && res.status < 500) done.resolve(std::optional<std::optional<Json>>(std::optional<Json>()));
    else done.resolve(std::optional<std::optional<Json>>());
    co_return;
  }
  Json json;
  // (an answer that is no JSON: as the original's res.json() throwing – no answer)
  if (!Json::parse(res.text(), json)) done.resolve(std::optional<std::optional<Json>>());
  else done.resolve(std::optional<std::optional<Json>>(std::optional<Json>(std::move(json))));
}

}  // namespace

Task<std::optional<RealWeather::Loaded>> RealWeather::load(std::string url) {
  if (!fetchFn_) co_return std::nullopt;
  // (raced rather than trusted to the answer: an answer that hangs still ends in time)
  Resolver<std::optional<std::optional<Json>>> done;
  plat::setTimeout([done] { done.resolve(std::optional<std::optional<Json>>()); }, timeoutMs);
  plat::Request req;
  req.url = std::move(url);
  req.method = "GET";
  // nothing of the player: no stored copy (no cookies and no referrer: the host's fetch)
  req.cache = "no-store";
  // (a fixed language instead of the browser's; a "simple" header: no CORS preflight)
  req.headers = {{"Accept-Language", "en"}};
  fetchInto(fetchFn_, std::move(req), done).detach();
  std::optional<std::optional<Json>> got = co_await done.task();
  if (!got) co_return std::nullopt;
  co_return Loaded{std::move(*got)};
}

}  // namespace fs
