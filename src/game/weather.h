// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/weather.ts @ v1.7.7
/**
 * The weather: the real one over the place the vehicle is at, or one the
 * player picks – rain, snow, fog … – which needs no network at all.
 *
 * The real weather comes from Open-Meteo (https://open-meteo.com/): free for
 * non-commercial use, no key, open to requests from any web page (CORS).
 * Its data are licensed CC BY 4.0, so wherever the game shows them it must
 * say so – WEATHER_ATTRIBUTION holds the text and the links.
 *
 * The real weather follows the game's clock (game/clock.h): at the real time
 * the current weather; at another moment the player set, the forecast for
 * that hour (16 days ahead, about two months back); beyond that a typical
 * hour of that time of year (climateWeather).
 *
 * Only the place of the game is sent – the map's latitude and longitude
 * rounded to 0.1° (some 7 by 11 km round here) and, if given, its ground
 * height – and the hours asked for, never anything about the player: no
 * cookies, no referrer, a fixed language.  (The service does see the IP
 * address, as any server would; Open-Meteo deletes its logs after 90 days.)
 *
 * All of this is data and arithmetic: render/weather.h draws it, the tyres
 * take gripFactor(), and any part of the game may ask weatherNow(),
 * isRaining(), windSpeed(), snowOnGround() ….
 *
 * (A weather state has identity in the original – the same object for the same
 * weather, compared with ===: here a WeatherPtr, shared and never changed.)
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/js.h"
#include "core/task.h"
#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

/** 'clear' | 'cloudy' | 'fog' | 'drizzle' | 'rain' | 'snow' | 'storm' */
using WeatherKind = std::string;
extern const std::vector<WeatherKind> WEATHER_KINDS;

struct WeatherState {
  WeatherKind kind = "clear";
  /** the WMO weather code it stands for (0 … 99; −1: none known) – for a symbol or a word in the HUD */
  int code = 0;
  /** how hard it rains or snows, 0 … 1 (fog: how thick; 0 when nothing falls) */
  double intensity = 0;
  /** share of the sky under cloud, 0 … 1 */
  double cloud = 0;
  /** how far one sees (m) */
  double visibility = 30000;
  /** mean wind 10 m above the ground (m/s) */
  double wind = 0;
  /** the strongest gusts (m/s) */
  double gust = 0;
  /** compass direction the wind comes from (degrees: 0 north, 90 east), as weather reports give it */
  double windFrom = 0;
  /** air temperature 2 m above the ground (°C) */
  double temperature = 0;
  /** the sun is up (the game's own sun does not follow this) */
  bool isDay = true;
  /** how wet the road is, 0 dry … 1 streaming – it stays wet a while after the rain */
  double wet = 0;
  /** ice on the road, 0 … 1: freezing rain (1), rime from a freezing fog, a wet road below zero */
  double ice = 0;
  /** snow lying on the ground (m) – the season (render/season.h) whitens the land by it */
  double snowDepth = 0;
  /** 'real' | 'fixed' | 'climate' | 'none': the real weather (or its forecast), one the player chose, a typical one of the season (beyond the forecast), or none to be had (offline: clear) */
  std::string source = "fixed";
};

/** a weather state as the original hands it round: one object, shared, never changed */
using WeatherPtr = std::shared_ptr<const WeatherState>;

/** The weather setting: none (clear), the real weather, or one of the fixed kinds ('off' | 'real' | 'cloudy' | 'fog' | 'drizzle' | 'rain' | 'snow' | 'storm'). */
using WeatherSetting = std::string;
extern const std::vector<WeatherSetting> WEATHER_SETTINGS;

bool isWeatherSetting(const std::string& v);

// ------------------------------------------------------------ fixed weather

/**
 * The weather of each kind as the player picks it: no network, always the
 * same.  (Shared, never to be changed.)  Null for a kind that is none.
 */
const WeatherPtr& FIXED_WEATHER(const std::string& kind);

/** No weather to be had (switched off, offline, not answered yet): a clear day. */
const WeatherPtr& CLEAR_WEATHER();

class RealWeather;

/**
 * The weather to show for a setting: a fixed one, or the real one at the
 * game's moment `at` (ms since 1970; none: the real time) – clear as long as
 * there is none (offline, not answered yet), the climate's for a moment the
 * forecast does not reach.  Allocates nothing: fine to call every frame.
 */
WeatherPtr weatherFor(const WeatherSetting& setting, const RealWeather* real, std::optional<double> at = std::nullopt);

// ------------------------------------------------------------ grip and wind

/**
 * What the weather (and the season's snow on the road) leaves of the tyres'
 * grip, as a factor on that of a dry road: a wet road about 0.8 (less with
 * water standing in heavy rain), snow about 0.5, ice down to 0.3.  For
 * WorldGround.weatherGrip.  `roadSnow`: SeasonState.roadSnow (0 … 1) – snow
 * lying on the road, compacted by the traffic: down to 0.5.
 */
double gripFactor(const WeatherState& s, double roadSnow = 0);

/**
 * The wind as a velocity in the world frame (m/s, x east, z south) – where it
 * blows to, not where it comes from.
 */
template <typename Out>
Out& windVector(const WeatherState& s, Out& out) {
  const double a = (s.windFrom * 3.14159265358979323846) / 180;
  const double v = std::max(0.0, s.wind);
  // from the north (0°) it blows to the south, +z
  out.x = -std::sin(a) * v;
  out.z = std::cos(a) * v;
  return out;
}

// ------------------------------------------ the weather now, for anyone

/** snow this deep (m) whitens the land completely; less lies in patches */
inline constexpr double FULL_SNOW_DEPTH = 0.04;

/**
 * The weather the game shows now, for any part of the game that wants to
 * know – people under umbrellas, cranes in the wind, dirt on the car.  The
 * game sets it (setWeatherNow) whenever it changes; until then it is clear.
 * The helpers below read it when called without a state.
 */
const WeatherState& weatherNow();
/** the same as the object it is (for comparing with the states it was set to) */
const WeatherPtr& weatherNowPtr();

/**
 * The game: this is the weather shown now; `groundSnow` the snow the player
 * sees lying, with the season's (SeasonState.snow, 0 … 1).
 */
void setWeatherNow(WeatherPtr s, std::optional<double> groundSnow = std::nullopt);

/** How hard it rains – drizzle, rain, a thunderstorm's rain: 0 dry … 1 a downpour. */
double rainIntensity(const WeatherState& s);
double rainIntensity();
/** Does it rain (or drizzle)? */
bool isRaining(const WeatherState& s);
bool isRaining();
/** How hard it snows, 0 … 1. */
double snowIntensity(const WeatherState& s);
double snowIntensity();
/** The mean wind 10 m above the ground (m/s); the gusts are `gust`, the direction windVector(). */
double windSpeed(const WeatherState& s);
double windSpeed();
/**
 * Snow lying on the ground, 0 none … 1 all white.  Without a state: as the
 * player sees it, the season's snow included, when the game said so.  Of a
 * state: by its snow depth, and snow falling below 1.5 °C settles.
 */
double snowOnGround();
double snowOnGround(const WeatherState& s);

// --------------------------------------------------------- the API's answer

/** The `current` block of Open-Meteo's forecast answer (the fields asked for in weatherUrl); a field not there: none. */
struct OpenMeteoCurrent {
  std::optional<std::string> time;
  /** seconds the sums (precipitation …) are taken over: 900 for the 15-minute data */
  std::optional<double> interval;
  std::optional<double> temperature_2m;
  std::optional<double> weather_code;
  /** all that fell in the interval (mm of water) */
  std::optional<double> precipitation;
  std::optional<double> rain;
  std::optional<double> showers;
  /** snow that fell in the interval (cm; 7 cm are 10 mm of water) */
  std::optional<double> snowfall;
  /** % */
  std::optional<double> cloud_cover;
  /** m */
  std::optional<double> visibility;
  /** m/s (asked for with wind_speed_unit=ms) */
  std::optional<double> wind_speed_10m;
  /** degrees, where the wind comes from */
  std::optional<double> wind_direction_10m;
  std::optional<double> wind_gusts_10m;
  /** 1 by day, 0 by night */
  std::optional<double> is_day;
  /** snow lying on the ground (m) */
  std::optional<double> snow_depth;
};

/**
 * Open-Meteo's current weather as the game's weather.  Pure: the WMO code
 * says what falls, the amounts how hard, the cloud cover whether a dry sky
 * is clear or cloudy, the model's visibility (within what the code allows)
 * how far one sees.  A field that is missing takes a plausible default.
 */
WeatherState weatherFromOpenMeteo(const OpenMeteoCurrent& c);

/** The whole answer (parsed JSON) → the weather now; null for anything that is not an answer. */
WeatherPtr parseOpenMeteo(const Json& json);

/** Hours of a forecast: their moments (ms since 1970, ascending) and their weather. */
struct WeatherHours {
  std::vector<double> t;
  std::vector<WeatherPtr> s;
};

/**
 * An hourly answer (asked with timeformat=unixtime) → the weather hour by
 * hour.  Hours without data – the service keeps the past only so long – are
 * left out; null when no hour has any.
 */
std::optional<WeatherHours> parseOpenMeteoHourly(const Json& json);

// ---------------------------------------------- the weather of the climate

/**
 * A typical hour of that time of year round Kassel (pure, from the climate
 * normals): for a moment the forecast does not reach.  Nothing falls; the
 * month's cloud and warmth, warmest in the afternoon; day or night by the
 * sun.  The snow of a typical winter is the season's (render/season.h).
 * The same object for the same hour: fine to ask every frame.
 */
WeatherPtr climateWeather(double ms);

// ------------------------------------------------------------ the request

inline const std::string OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast";

/** What Open-Meteo's licence (CC BY 4.0) asks for wherever its data are shown: this text, linked to the site. */
struct WeatherAttribution {
  const char* text;
  const char* url;
  const char* licence;
  const char* licenceUrl;
};
inline constexpr WeatherAttribution WEATHER_ATTRIBUTION = {"Weather data by Open-Meteo.com", "https://open-meteo.com/", "CC BY 4.0",
                                                           "https://creativecommons.org/licenses/by/4.0/"};

inline constexpr double WEATHER_HOUR = 3600000;
/** how far back and ahead of the real now the service tells the weather hour by hour (ms): it keeps about two months of the past, forecasts 16 days */
inline constexpr double FORECAST_BACK_MS = 60 * 24 * WEATHER_HOUR;
inline constexpr double FORECAST_AHEAD_MS = 15 * 24 * WEATHER_HOUR;

/** the hours asked for (ms) */
struct WeatherSpan {
  double from = 0;
  double to = 0;
};

/**
 * The request for the weather at a place of the map: now, or hour by hour
 * from `hours.from` to `hours.to` (ms; within FORECAST_BACK_MS …
 * FORECAST_AHEAD_MS of now).  Latitude and longitude are rounded to 0.1°.
 * The ground height (m above sea level, rounded to 10 m) is optional; without
 * it the service takes the height of its terrain model at the rounded point –
 * round Kassel that may be the Habichtswald, 300 m higher and 2 °C colder
 * than the town (snow instead of rain).
 */
std::string weatherUrl(double lat, double lon, std::optional<double> elevation = std::nullopt, std::optional<WeatherSpan> hours = std::nullopt);

/**
 * What RealWeather needs of fetch (the host's, or a fake one in the tests): the answer, status 0 when
 * there was none at all (offline – the original's fetch throws then).
 */
using WeatherFetch = std::function<Task<plat::Response>(plat::Request)>;

struct RealWeatherOptions {
  /** default: the host's fetch */
  WeatherFetch fetch{};
  /**
   * the real clock in ms (default plat::dateMs, a wall clock – a night in standby
   * counts): how often to ask and how far the forecast reaches go by it; the
   * game's own moment is what request(), current() and due() are given
   */
  std::function<double()> now{};
  /** the least time between two requests for the same (ms; default 15 min – the service's data change every 15 min) */
  std::optional<double> refreshMs{};
  /** the least time between requests for different moments (ms; default 1 min – the player sets another moment now and then) */
  std::optional<double> minGapMs{};
  /** how long to wait for an answer (ms; default 8 s) */
  std::optional<double> timeoutMs{};
  /** how long the current weather stays good when no newer answer comes (ms; default 2 h – then clear) */
  std::optional<double> maxAgeMs{};
};

inline constexpr double WEATHER_REFRESH_MS = 15 * 60000;
inline constexpr double WEATHER_MIN_GAP_MS = 60000;
inline constexpr double WEATHER_TIMEOUT_MS = 8000;
inline constexpr double WEATHER_MAX_AGE_MS = 2 * WEATHER_HOUR;

/**
 * The real weather – now, or at the game's moment when the player set
 * another one (the arcade mode's clock): the forecast hour by hour up to 16
 * days ahead, the past as far as the service keeps it (about two months).
 *
 * Asked for only when the game asks (request), one request at a time, the
 * same thing at most once per refreshMs whatever the outcome, another moment
 * at most once per minGapMs.  A failure – offline, no answer in time, an
 * error – is silent: the last answer stays while it is fresh enough, and
 * then there is none (weatherFor: clear).  No timer of its own: nothing goes
 * out unless request() is called.
 */
class RealWeather {
 public:
  const double refreshMs;
  const double minGapMs;
  const double timeoutMs;
  const double maxAgeMs;
  /** requests sent so far */
  int requests = 0;

  explicit RealWeather(RealWeatherOptions opts = {});
  RealWeather(const RealWeather&) = delete;
  RealWeather& operator=(const RealWeather&) = delete;

  /**
   * Can the service tell the weather at the game's moment `at`?  Now always;
   * another moment when the forecast reaches it and it was not answered
   * without data.  Where it cannot, weatherFor() shows the climate's.
   */
  bool covers(std::optional<double> at = std::nullopt) const;

  /**
   * The weather at the game's moment `at` (ms since 1970; none: now) as
   * last answered – null when there is none (not asked yet, offline, too old,
   * not in the hours fetched).
   */
  WeatherPtr current(std::optional<double> at = std::nullopt) const;

  /** A request is under way. */
  bool pending() const { return inflight_; }

  /** Would request() send one now for the game's moment `at`?  (Cheap: ask it before working out the place.) */
  bool due(std::optional<double> at = std::nullopt) const;

  /**
   * Ask for the weather at a place (degrees; `elevation`: the ground height
   * there, m) at the game's moment `at` (ms; none: now).  Sent only when
   * due() – otherwise nothing happens and null comes back.  The task never
   * fails: the weather at `at`, or null (current() then keeps what it had).
   * The game need not wait for it.
   */
  std::optional<Task<WeatherPtr>> request(double lat, double lon, std::optional<double> elevation = std::nullopt, std::optional<double> at = std::nullopt);

 private:
  /** the answer: its JSON – none when the service refused the question (4xx) */
  struct Loaded {
    std::optional<Json> json;
  };
  /** `at` (the game's moment, ms; none: the real time) is the real now, near enough. */
  bool isNow(std::optional<double> at, double now) const;
  Task<WeatherPtr> askNow(std::string url);
  Task<WeatherPtr> askHours(std::string url, double from, double t);
  /**
   * The answer's JSON – `json: none` when the service refused the question
   * (4xx: a moment out of its range) – or null for no answer at all (offline,
   * too late, a server error).
   */
  Task<std::optional<Loaded>> load(std::string url);

  WeatherFetch fetchFn_;
  std::function<double()> now_;
  // the game at the real time: the current weather
  WeatherPtr last_;
  double lastAt_ = -JS_INF;
  double askedNow_ = -JS_INF;
  // the game at another moment: the forecast hour by hour
  std::optional<WeatherHours> hours_;
  double hoursAt_ = -JS_INF;
  double askedHours_ = -JS_INF;
  double blockFrom_ = std::numeric_limits<double>::quiet_NaN();
  double blockTo_ = std::numeric_limits<double>::quiet_NaN();
  /** the hours asked last came back without data (the past beyond what the service keeps) */
  bool blockEmpty_ = false;
  double askedAny_ = -JS_INF;
  bool inflight_ = false;
  /** (the tasks under way look at it before they touch this object: it may be gone when the answer comes) */
  std::shared_ptr<bool> life_ = std::make_shared<bool>(true);
};

}  // namespace fs
