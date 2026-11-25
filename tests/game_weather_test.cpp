// LINK: game render engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The weather: Open-Meteo's answer read as the game's weather (every WMO code
 * 0–99), the weather the player picks, what is left of the tyres' grip, how
 * often the real weather is asked for (with a fake fetch and a fake clock),
 * the forecast at another moment of the game's clock and the climate beyond
 * it, the weather "now" for the rest of the game, and what the renderer makes
 * of it (fog, light, particles – without a graphics chip).
 *
 * Origin: tests/game/weather.test.ts @ v1.7.7
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/date.h"
#include "core/js.h"
#include "game/outdoors.h"
#include "game/weather.h"
#include "geo/utm.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "render/materials.h"
#include "render/season.h"
#include "render/weather.h"

using namespace fs;

static constexpr double HOUR = 3600000;

/** an answer's current block: overcast, 8 °C, 20 km, a light wind – and what is given */
static WeatherState cur(int code, const std::function<void(OpenMeteoCurrent&)>& extra = nullptr) {
  OpenMeteoCurrent c;
  c.interval = 900;
  c.weather_code = code;
  c.temperature_2m = 8;
  c.cloud_cover = 95;
  c.visibility = 20000;
  c.wind_speed_10m = 3;
  c.wind_direction_10m = 240;
  c.is_day = 1;
  c.precipitation = 0;
  c.snowfall = 0;
  if (extra) extra(c);
  return weatherFromOpenMeteo(c);
}

/** the query of an address, as URL.searchParams has it */
static std::map<std::string, std::string> query(const std::string& url) {
  std::map<std::string, std::string> out;
  const size_t q = url.find('?');
  if (q == std::string::npos) return out;
  for (const std::string& part : split(url.substr(q + 1), "&")) {
    const size_t eq = part.find('=');
    out[decodeURIComponent(part.substr(0, eq))] = eq == std::string::npos ? "" : decodeURIComponent(part.substr(eq + 1));
  }
  return out;
}
static std::optional<std::string> param(const std::string& url, const std::string& key) {
  const auto m = query(url);
  auto it = m.find(key);
  if (it == m.end()) return std::nullopt;
  return it->second;
}

/** "2026-11-20T14:00" → ms (UTC) */
static double isoMs(const std::string& s) {
  return utcMs(std::stoi(s.substr(0, 4)), std::stoi(s.substr(5, 2)) - 1, std::stoi(s.substr(8, 2)), std::stoi(s.substr(11, 2)), std::stoi(s.substr(14, 2)));
}

/** an answer that comes later, as in a browser */
static Task<plat::Response> later(plat::Response r) {
  Resolver<plat::Response> res;
  plat::soon([res, r] { res.resolve(r); });
  return res.task();
}
static plat::Response answerOf(int status, const std::string& body) {
  plat::Response r;
  r.status = status;
  r.body.assign(body.begin(), body.end());
  return r;
}

/** a fake service: the current weather, or hours from start_hour on */
static WeatherFetch service(std::function<std::string()> answer, std::vector<std::string>* urls, std::vector<plat::Request>* inits) {
  return [answer, urls, inits](plat::Request init) -> Task<plat::Response> {
    const std::string url = init.url;
    if (urls) urls->push_back(url);
    if (inits) inits->push_back(init);
    const std::string a = answer();
    if (a == "offline") return later(answerOf(0, ""));
    if (a == "error") return later(answerOf(503, "{}"));
    if (a == "refuse") return later(answerOf(400, "{\"error\":true}"));
    if (a == "garbage") return later(answerOf(200, "{\"hello\":\"world\"}"));
    const std::optional<std::string> start = param(url, "start_hour");
    if (!start)
      return later(answerOf(200, "{\"current\":{\"interval\":900,\"weather_code\":63,\"precipitation\":1,\"temperature_2m\":9,\"cloud_cover\":100,\"snow_depth\":0}}"));
    const double from = isoMs(*start) / 1000;
    const double to = isoMs(*param(url, "end_hour")) / 1000;
    std::vector<double> time;
    for (double t = from; t <= to; t += 3600) time.push_back(t);
    // overcast in the first hour, snow from the second on (empty: the service kept nothing of those hours)
    const auto val = [&](const std::function<double(size_t)>& f) {
      std::string s = "[";
      for (size_t i = 0; i < time.size(); i++) s += (i ? "," : "") + (a == "empty" ? std::string("null") : jsStr(f(i)));
      return s + "]";
    };
    std::string ts = "[";
    for (size_t i = 0; i < time.size(); i++) ts += (i ? "," : "") + jsStr(time[i]);
    ts += "]";
    const std::string body = "{\"hourly\":{\"time\":" + ts + ",\"weather_code\":" + val([](size_t i) { return i < 1 ? 3.0 : 73.0; }) +
                             ",\"temperature_2m\":" + val([](size_t) { return -1.0; }) + ",\"cloud_cover\":" + val([](size_t) { return 100.0; }) +
                             ",\"precipitation\":" + val([](size_t i) { return i < 1 ? 0.0 : 1.0; }) + ",\"snowfall\":" + val([](size_t i) { return i < 1 ? 0.0 : 1.2; }) +
                             ",\"snow_depth\":" + val([](size_t i) { return static_cast<double>(i) * 0.01; }) + ",\"is_day\":" + val([](size_t) { return 1.0; }) + "}}";
    return later(answerOf(200, body));
  };
}

static WeatherPtr await(std::optional<Task<WeatherPtr>> t) {
  assert(t);
  return plat::native::wait(std::move(*t));
}

/** the parts of render/sky.h's Sky that the weather touches */
static SkyParts fakeSky() {
  return {.fog = std::make_shared<Fog>(Color(0xc2d3e4), 375, 1312.5),
          .sun = directionalLight(Color(0xfff4e0), 3.2),
          .hemi = hemisphereLight(Color(0xbfd6ff), Color(0x6b6a55), 1.2),
          .uZenith = uniform(Color(0x3a74c6)),
          .uHorizon = uniform(Color(0xc2d3e4)),
          .uSunColor = uniform(Color(0xfff2d0))};
}
static const Color& colorOf(const UniformPtr& u) { return std::get<Color>(u->value); }

int main() {
  // every WMO code 0–99 at any temperature gives a sound weather
  for (int code = 0; code <= 99; code++) {
    for (const double temperature : {-6.0, 0.5, 9.0}) {
      const WeatherState s = cur(code, [&](OpenMeteoCurrent& c) { c.temperature_2m = temperature; });
      assert(std::find(WEATHER_KINDS.begin(), WEATHER_KINDS.end(), s.kind) != WEATHER_KINDS.end());
      assert(s.code == code);
      for (const double v : {s.intensity, s.cloud, s.wet, s.ice}) assert(v >= 0 && v <= 1);
      assert(s.visibility >= 30 && s.visibility <= 50000);
      assert(s.wind >= 0 && s.gust >= s.wind && s.windFrom >= 0 && s.windFrom < 360);
      assert(s.source == "real");
      const bool falls = s.kind == "drizzle" || s.kind == "rain" || s.kind == "snow";
      assert(falls == (s.intensity > 0 && s.kind != "fog" && s.kind != "storm"));
      const double g = gripFactor(s);
      assert(g >= 0.25 && g <= 1);
    }
  }

  // the codes read as their words say: clear, cloudy, fog, drizzle, rain, snow, showers, thunderstorms
  {
    assert(cur(0, [](OpenMeteoCurrent& c) { c.cloud_cover = 5; }).kind == "clear");
    assert(cur(2, [](OpenMeteoCurrent& c) { c.cloud_cover = 50; }).kind == "clear");
    assert(cur(3, [](OpenMeteoCurrent& c) { c.cloud_cover = 100; }).kind == "cloudy");
    const WeatherState fog = cur(45);
    assert(fog.kind == "fog");
    assert(fog.visibility <= 400);
    assert(cur(48, [](OpenMeteoCurrent& c) { c.temperature_2m = -3; }).ice > 0);   // rime fog leaves ice
    const auto steps = [](std::vector<int> codes, const std::string& kind) {
      std::vector<WeatherState> s;
      for (const int c : codes) s.push_back(cur(c));
      for (const WeatherState& x : s) assert(x.kind == kind);
      for (size_t i = 1; i < s.size(); i++) assert(s[i].intensity > s[i - 1].intensity);
    };
    steps({51, 53, 55}, "drizzle");
    steps({61, 63, 65}, "rain");
    steps({71, 73, 75}, "snow");
    steps({80, 81, 82}, "rain");
    steps({85, 86}, "snow");
    assert(cur(77).kind == "snow");
    for (const int c : {56, 57, 66, 67}) {
      assert(cur(c).ice == 1);
      assert(gripFactor(cur(c)) <= 0.3 + 1e-9);   // glaze ice
    }
    for (const int c : {95, 96, 99}) assert(cur(c).kind == "storm");
    assert(cur(99).intensity == 1);
    // rain and snow mixed: whichever it looks more like
    for (const int c : {68, 69, 83, 84}) {
      assert(cur(c, [](OpenMeteoCurrent& x) { x.temperature_2m = -2; }).kind == "snow");
      assert(cur(c, [](OpenMeteoCurrent& x) { x.temperature_2m = 6; }).kind == "rain");
    }
    // it rained in the last hour, it does not now: the road is still wet
    const WeatherState after = cur(21);
    assert(after.kind == "cloudy");
    assert(after.intensity == 0);
    assert(after.wet >= 0.5);
    // thunder without rain
    const WeatherState dry = cur(17);
    assert(dry.kind == "storm");
    assert(dry.intensity == 0);
  }

  // the amounts say how hard: the sum of a quarter hour, of an hour, and no code at all
  {
    assert(cur(61).intensity == 0.3);   // nothing measured: as the code words it
    const WeatherState pour = cur(61, [](OpenMeteoCurrent& c) { c.precipitation = 2.4; });   // 9.6 mm/h
    assert(pour.intensity >= 0.75);
    const WeatherState hourly = cur(61, [](OpenMeteoCurrent& c) {
      c.precipitation = 2.4;
      c.interval = 3600;
    });   // 2.4 mm/h
    assert(hourly.intensity < pour.intensity && hourly.intensity > 0.3);
    // no code: by the amounts
    {
      OpenMeteoCurrent c;
      c.precipitation = 1;
      c.temperature_2m = 6;
      assert(weatherFromOpenMeteo(c).kind == "rain");
      OpenMeteoCurrent d;
      d.precipitation = 0.5;
      d.snowfall = 0.7;
      d.temperature_2m = -1;
      assert(weatherFromOpenMeteo(d).kind == "snow");
    }
    // nothing at all: plausible defaults
    const WeatherState none = weatherFromOpenMeteo({});
    assert(none.kind == "clear");
    assert(none.code == -1);
    assert(none.temperature == 10);
    assert(none.isDay == true);
    assert(none.snowDepth == 0);
    Json j;
    assert(Json::parse("{\"nothing\":1}", j) && parseOpenMeteo(j) == nullptr);
    assert(parseOpenMeteo(Json()) == nullptr);
    assert(Json::parse("{\"current\":{\"weather_code\":63}}", j) && parseOpenMeteo(j)->kind == "rain");
  }

  // how far one sees: the model's number within what fog, rain and snow allow
  {
    const WeatherState misty = cur(3, [](OpenMeteoCurrent& c) { c.visibility = 400; });
    assert(misty.kind == "fog");   // a dry sky with 400 m is fog
    assert(misty.visibility == 400);
    assert(cur(75, [](OpenMeteoCurrent& c) { c.visibility = 30000; }).visibility <= 600);    // heavy snow
    assert(cur(65, [](OpenMeteoCurrent& c) { c.visibility = 30000; }).visibility <= 3000);   // heavy rain
    OpenMeteoCurrent c;
    c.weather_code = 0;
    assert(weatherFromOpenMeteo(c).visibility == 25000);
    assert(cur(0, [](OpenMeteoCurrent& x) { x.visibility = 5; }).visibility == 30);   // never below 30 m
  }

  // the request: the place rounded to 0.1°, the ground height to 10 m – nothing else; hour by hour for another moment
  {
    const std::string url = weatherUrl(51.2834, 9.4612, 172.4);
    assert(url.substr(0, url.find('?')) == "https://api.open-meteo.com/v1/forecast");
    assert(param(url, "latitude") == "51.3");
    assert(param(url, "longitude") == "9.5");
    assert(param(url, "elevation") == "170");
    assert(param(url, "wind_speed_unit") == "ms");
    assert(param(url, "current")->find("weather_code") != std::string::npos);
    std::vector<std::string> keys;
    for (const auto& [k, v] : query(url)) keys.push_back(k);
    assert((keys == std::vector<std::string>{"current", "elevation", "latitude", "longitude", "timezone", "wind_speed_unit"}));
    assert(!param(weatherUrl(51.3, 9.4), "elevation"));
    const std::string hours = weatherUrl(51.3, 9.4, std::nullopt, WeatherSpan{utcMs(2026, 10, 25, 14, 20), utcMs(2026, 10, 26, 15)});
    assert(param(hours, "start_hour") == "2026-11-25T14:00");
    assert(param(hours, "end_hour") == "2026-11-26T15:00");
    assert(param(hours, "timeformat") == "unixtime");
    assert(!param(hours, "current"));
    assert(param(hours, "hourly")->find("snow_depth") != std::string::npos);
    assert(std::string(WEATHER_ATTRIBUTION.text) == "Weather data by Open-Meteo.com");
    assert(std::string(WEATHER_ATTRIBUTION.url) == "https://open-meteo.com/");
  }

  // the weather the player picks needs no network and never changes
  {
    int calls = 0;
    RealWeather real({.fetch = [&calls](plat::Request) {
      calls++;
      return later(answerOf(200, "{}"));
    }});
    for (const WeatherSetting& setting : WEATHER_SETTINGS) {
      assert(isWeatherSetting(setting));
      if (setting == "real") continue;
      const WeatherPtr s = weatherFor(setting, &real);
      assert(s == (setting == "off" ? CLEAR_WEATHER() : FIXED_WEATHER(setting)));
      assert(s->source == (setting == "off" ? "none" : "fixed"));
    }
    assert(calls == 0);
    assert(!isWeatherSetting("hail"));
    assert(FIXED_WEATHER("fog")->visibility == 120);
    assert(FIXED_WEATHER("snow")->kind == "snow");
    assert(FIXED_WEATHER("rain")->wet > 0.8);
  }

  // grip: a wet road about 0.8, snow about 0.5, glaze ice 0.3, snow on the road down to 0.5
  {
    assert(gripFactor(*FIXED_WEATHER("clear")) == 1);
    assert(gripFactor(*FIXED_WEATHER("cloudy")) == 1);
    const double rain = gripFactor(*FIXED_WEATHER("rain"));
    assert(rain > 0.7 && rain < 0.85);
    const double snow = gripFactor(*FIXED_WEATHER("snow"));
    assert(snow >= 0.4 && snow <= 0.55);
    assert(gripFactor(*FIXED_WEATHER("storm")) < rain);
    assert(gripFactor(*FIXED_WEATHER("drizzle")) > rain);
    assert(gripFactor(*FIXED_WEATHER("fog")) > 0.9 && gripFactor(*FIXED_WEATHER("fog")) < 1);
    assert(gripFactor(*FIXED_WEATHER("clear"), 1) == 0.5);
    assert(gripFactor(*FIXED_WEATHER("clear"), 0.5) > gripFactor(*FIXED_WEATHER("clear"), 1));
    assert(gripFactor(*FIXED_WEATHER("clear"), JS_NAN) == 1);
    // harder rain, less grip
    double last = 1;
    for (const double precipitation : {0.0, 0.5, 1.0, 2.0, 4.0}) {
      const double g = gripFactor(cur(61, [&](OpenMeteoCurrent& c) { c.precipitation = precipitation; }));
      assert(g <= last);
      last = g;
    }
  }

  // the wind blows from where the report says to the other side
  {
    struct {
      double x = 0, z = 0;
    } v;
    WeatherState w = *FIXED_WEATHER("clear");
    w.wind = 4;
    w.windFrom = 0;
    windVector(w, v);
    assert(std::abs(v.x) < 1e-9 && std::abs(v.z - 4) < 1e-9);   // from the north: to the south (+z)
    w.windFrom = 270;
    windVector(w, v);
    assert(std::abs(v.x - 4) < 1e-9 && std::abs(v.z) < 1e-9);   // from the west: to the east (+x)
  }

  // the real weather: asked for only when the game asks, at most every 15 minutes, one at a time, failures quiet
  {
    double t = utcMs(2026, 10, 16, 12);
    std::string answer = "ok";
    std::vector<std::string> urls;
    std::vector<plat::Request> inits;
    RealWeather real({.fetch = service([&answer] { return answer; }, &urls, &inits), .now = [&t] { return t; }});
    assert(real.current() == nullptr);
    assert(weatherFor("real", &real) == CLEAR_WEATHER());   // nothing yet: clear
    assert(real.due());
    std::optional<Task<WeatherPtr>> p = real.request(51.2834, 9.4612, 172);
    assert(p);
    assert(real.pending());
    assert(!real.request(51.3, 9.4));   // one at a time
    const WeatherPtr s = await(std::move(p));
    assert(s && s->kind == "rain");
    assert(weatherFor("real", &real)->kind == "rain");
    assert(urls.size() == 1);
    // nothing about the player goes with it
    assert(urls[0].find("latitude=51.3&longitude=9.5&") != std::string::npos);
    assert(inits[0].cache == "no-store");
    assert((inits[0].headers == std::vector<std::pair<std::string, std::string>>{{"Accept-Language", "en"}}));
    // within 15 minutes nothing more, however often the game asks
    for (int i = 0; i < 22; i++) {
      t += 40000;
      assert(!real.request(51.3, 9.4));
    }
    assert(urls.size() == 1);
    t += 80000;   // 16 min
    for (const char* failure : {"offline", "error", "garbage"}) {
      answer = failure;
      std::optional<Task<WeatherPtr>> q = real.request(51.3, 9.4);
      assert(q);
      assert(await(std::move(q)) == nullptr);
      assert(real.current()->kind == "rain");   // the last answer stays
      t += 60000;
      assert(!real.request(51.3, 9.4));   // not again sooner
      t += 15 * 60000;
    }
    assert(urls.size() == 4);
    // two hours without an answer: too old – clear
    t += 2 * HOUR;
    assert(real.current() == nullptr);
    assert(weatherFor("real", &real) == CLEAR_WEATHER());
    assert(real.requests == 4);
  }

  // no answer in time: the request ends quietly after the timeout
  {
    // (a fetch that never answers; the original also checks that it was called off – the host's fetch here has no AbortController)
    RealWeather deaf({.fetch = [](plat::Request) { return Resolver<plat::Response>().task(); }, .timeoutMs = 30});
    const double t0 = plat::nowMs();
    assert(await(deaf.request(51.3, 9.4)) == nullptr);
    assert(plat::nowMs() - t0 < 2000);
    assert(!deaf.pending());
  }

  // at another moment of the game clock: the forecast for that hour; beyond its reach the climate
  {
    double t = utcMs(2026, 10, 16, 12);
    std::string answer = "ok";
    std::vector<std::string> urls;
    RealWeather real({.fetch = service([&answer] { return answer; }, &urls, nullptr), .now = [&t] { return t; }});
    // in four days, at ten past three
    const double at = utcMs(2026, 10, 20, 15, 10);
    assert(real.covers(at));
    assert(real.due(at));
    const WeatherPtr s = await(real.request(51.3, 9.4, 170, at));
    assert(param(urls[0], "start_hour") == "2026-11-20T14:00");   // from the hour before
    assert(param(urls[0], "end_hour") == "2026-11-21T15:00");     // a day on
    // the nearest hour, 15:00, is the answer's second: snow
    assert(s && s->kind == "snow");
    assert(weatherFor("real", &real, at) == s);
    assert(weatherFor("real", &real, utcMs(2026, 10, 20, 14, 5))->kind == "cloudy");   // the hour before: still dry
    assert(weatherFor("real", &real)->kind == "clear");   // the real now is the current weather – not asked yet
    // the clock runs on within the hours fetched: nothing new for 15 minutes
    t += 2 * 60000;
    assert(!real.request(51.3, 9.4, 170, at + 3 * HOUR));
    assert(weatherFor("real", &real, at + 3 * HOUR)->kind == "snow");
    // another moment: soon (a minute after the last request), not at once
    const double later_ = utcMs(2026, 10, 25, 9);
    t += 30000;
    assert(real.due(later_));   // more than a minute since the last
    answer = "empty";
    // the past beyond what the service kept: answered without data – the climate
    const double past = utcMs(2026, 9, 1, 12);
    assert(await(real.request(51.3, 9.4, 170, past)) == nullptr);
    assert(!real.covers(past));
    assert(weatherFor("real", &real, past) == climateWeather(past));
    // beyond the forecast: nothing is asked, the climate is shown
    const double far = utcMs(2027, 0, 20, 14);
    t += 5 * 60000;
    assert(!real.covers(far));
    assert(!real.due(far));
    assert(!real.request(51.3, 9.4, 170, far));
    const WeatherPtr c = weatherFor("real", &real, far);
    assert(c->source == "climate");
    assert(c == climateWeather(far));
    // a refusal (a moment the service will not answer) counts as no data too
    answer = "refuse";
    t += 5 * 60000;
    const double soon = utcMs(2026, 10, 28, 8);
    assert(await(real.request(51.3, 9.4, 170, soon)) == nullptr);
    assert(!real.covers(soon));
    assert(weatherFor("real", &real, soon)->source == "climate");
    // back at the real time: the current weather
    answer = "ok";
    assert(await(real.request(51.3, 9.4, 170))->kind == "rain");
    assert(weatherFor("real", &real)->kind == "rain");
    assert(weatherFor("real", &real, t + 5 * 60000)->kind == "rain");   // a game clock a few minutes off is still now
  }

  // an hourly answer: hours without data are left out
  {
    Json j;
    assert(Json::parse(R"({"hourly":{"time":[1000,4600,8200],"weather_code":[null,61,3],"temperature_2m":[null,5,4],"precipitation":[null,1,0]}})", j));
    const std::optional<WeatherHours> h = parseOpenMeteoHourly(j);
    assert(h && (h->t == std::vector<double>{4600000, 8200000}));
    assert(h->s[0]->kind == "rain");
    assert(h->s[1]->kind == "cloudy");
    assert(Json::parse(R"({"hourly":{"time":[1000],"weather_code":[null],"temperature_2m":[null]}})", j) && !parseOpenMeteoHourly(j));
    assert(Json::parse(R"({"current":{}})", j) && !parseOpenMeteoHourly(j));
    assert(!parseOpenMeteoHourly(Json("nonsense")));
  }

  // the climate: a typical hour of the season round Kassel, the same object for the same hour
  {
    const WeatherPtr jan = climateWeather(utcMs(2027, 0, 15, 14));
    const WeatherPtr jul = climateWeather(utcMs(2027, 6, 15, 14));
    assert(jan->source == "climate");
    assert(jan->intensity == 0);
    assert(jan->temperature < 5 && jul->temperature > 15);
    assert(jan->kind == "cloudy");
    assert(jul->kind == "clear");
    const double a = utcMs(2027, 3, 10, 13, 10);
    assert(climateWeather(a) == climateWeather(a + 20 * 60000));   // the same hour, the same object
    assert(climateWeather(utcMs(2027, 3, 10, 13))->temperature > climateWeather(utcMs(2027, 3, 10, 3))->temperature);   // warmer in the afternoon
    assert(climateWeather(utcMs(2027, 0, 15, 2))->isDay == false);
    assert(climateWeather(utcMs(2027, 0, 15, 11))->isDay == true);
    assert(weatherFor("real", nullptr, utcMs(2027, 0, 15, 2))->source == "climate");   // no service: the climate at another moment
    assert(weatherFor("real", nullptr) == CLEAR_WEATHER());   // no service now: clear
  }

  // the weather now, for the rest of the game: rain, wind, snow on the ground
  {
    assert(weatherNowPtr() == CLEAR_WEATHER());
    assert(!isRaining());
    setWeatherNow(FIXED_WEATHER("rain"));
    assert(weatherNowPtr() == FIXED_WEATHER("rain"));
    assert(isRaining());
    assert(rainIntensity() == 0.6);
    assert(windSpeed() == 5);
    assert(snowIntensity() == 0);
    assert(snowOnGround() == 0);
    setWeatherNow(FIXED_WEATHER("drizzle"));
    assert(isRaining());
    setWeatherNow(FIXED_WEATHER("snow"), 0.8);
    assert(!isRaining());
    assert(snowIntensity() == 0.55);
    assert(snowOnGround() == 0.8);   // as the player sees it, with the season
    assert(snowOnGround(*FIXED_WEATHER("snow")) == 0.75);   // of a state: 3 cm of 4
    WeatherState deep = *FIXED_WEATHER("snow");
    deep.snowDepth = 0.1;
    assert(snowOnGround(deep) == 1);
    WeatherState fresh = *FIXED_WEATHER("snow");
    fresh.snowDepth = 0;
    assert(snowOnGround(fresh) > 0.5);   // falling snow settles
    fresh.temperature = 4;
    assert(snowOnGround(fresh) == 0);   // not when it is warm
    assert(rainIntensity(*FIXED_WEATHER("storm")) == 0.9);
    setWeatherNow(CLEAR_WEATHER());
    assert(snowOnGround() == 0);
  }

  // ------------------------------------------------------------ the picture

  // fog: as the game sets it in clear weather, down to the visibility in fog – and starting at the camera
  {
    FogRange out;
    weatherFog(30000, 375, 1312.5, out);
    assert(out.near == 375 && out.far == 1312.5);
    weatherFog(120, 375, 1312.5, out);
    assert(std::abs(out.far - 126) < 1e-9);
    assert(out.near < 0);
    double lastFar = JS_INF;
    for (const double v : {20000.0, 5000.0, 2000.0, 800.0, 200.0, 50.0}) {
      weatherFog(v, 750, 7600, out);
      assert(out.far <= lastFar && out.near < out.far);
      lastFar = out.far;
    }
  }

  // the renderer: rain and snow by intensity, darker light in the rain, the sky given back as it was
  {
    WeatherRenderer r(1000);
    assert(r.quality() == 1000);
    Mesh& rain = *r.rainMesh();
    Mesh& snow = *r.snowMesh();
    assert(r.group->children.size() == 2 && r.group->children[0].get() == &rain);
    const Vector3 cam(100, 180, -50);
    r.jump();
    r.update(cam, 1.0 / 60, *FIXED_WEATHER("rain"));
    assert(rain.visible && !snow.visible);
    assert(rain.geometry->instanceCount == jsRound(1000 * (0.12 + 0.88 * 0.6)));
    assert(r.look.sunFactor < 0.3 && r.look.ambientFactor > 0.9);
    assert(std::abs(r.look.visibility - 4000) < 1);
    assert(r.look.wet > 0.8);
    r.jump();
    r.update(cam, 1.0 / 60, *FIXED_WEATHER("snow"));
    assert(!rain.visible && snow.visible);
    assert(snow.geometry->instanceCount == jsRound(600 * (0.15 + 0.85 * 0.55)));
    // fading over: the rain comes within seconds, not at once
    r.jump();
    r.update(cam, 1.0 / 60, *FIXED_WEATHER("clear"));
    assert(!rain.visible && !snow.visible);
    r.update(cam, 0.1, *FIXED_WEATHER("rain"));
    assert(r.look.rain > 0 && r.look.rain < 0.1);
    for (int i = 0; i < 300; i++) r.update(cam, 0.1, *FIXED_WEATHER("rain"));
    assert(std::abs(r.look.rain - 0.648) < 0.01);
    // more particles at a higher quality (a new geometry, once)
    r.quality(3000);
    r.update(cam, 1.0 / 60, *FIXED_WEATHER("rain"));
    assert(rain.geometry->instanceCount > 1900);

    // the sky
    const SkyParts sky = fakeSky();
    const Color beforeFog = sky.fog->color;
    const Color beforeZenith = colorOf(sky.uZenith);
    const Color beforeHemi = sky.hemi->color;
    r.attachSky(&sky);
    r.jump();
    r.update(cam, 1.0 / 60, *FIXED_WEATHER("fog"));
    assert(std::abs(sky.fog->far - 126) < 0.5);
    assert(sky.fog->near < 0);
    assert(sky.sun->intensity < 3.2 * 0.1);
    assert(!colorOf(sky.uZenith).equals(beforeZenith));   // a grey sky
    // the game sets its own fog (applySettings): taken as the clear weather's from then on
    sky.fog->near = 600;
    sky.fog->far = 2000;
    r.update(cam, 1.0 / 60, *FIXED_WEATHER("fog"));
    assert(sky.fog->far < 200);
    r.jump();
    r.update(cam, 1.0 / 60, *FIXED_WEATHER("clear"));
    assert(sky.fog->near == 600);
    assert(sky.fog->far == 2000);
    // given back exactly when detached
    r.jump();
    r.update(cam, 1.0 / 60, *FIXED_WEATHER("storm"));
    r.attachSky(nullptr);
    assert(sky.fog->near == 600);
    assert(sky.fog->far == 2000);
    assert(sky.sun->intensity == 3.2);
    assert(sky.hemi->intensity == 1.2);
    assert(sky.fog->color.equals(beforeFog));
    assert(colorOf(sky.uZenith).equals(beforeZenith));
    assert(sky.hemi->color.equals(beforeHemi));
    r.dispose();
    assert(r.group->parent == nullptr);
  }

  // a thunderstorm: lightning now and then
  {
    WeatherRenderer r(200);
    const Vector3 cam;
    r.jump();
    int flashes = 0;
    for (int i = 0; i < 60 * 120; i++) {
      r.update(cam, 1.0 / 60, *FIXED_WEATHER("storm"));
      if (r.look.flash > 0.5) flashes++;
    }
    assert(r.strikes >= 2 && r.strikes <= 40);
    assert(flashes > 0);
    std::printf("%d strikes in two minutes\n", r.strikes);
    WeatherRenderer calm(200);
    calm.jump();
    for (int i = 0; i < 60 * 60; i++) calm.update(cam, 1.0 / 60, *FIXED_WEATHER("rain"));
    assert(calm.strikes == 0);
  }

  // wet roads: a material is taught once, with a program of its own
  {
    WeatherRenderer r(100);
    SeasonLook look;
    MaterialPtr m = makeGroundMaterial(false);
    look.patch(*m, "ground");
    const uint32_t v = m->version;
    r.addWetness(*m);
    r.addWetness(*m);
    assert(m->patch->key == "fs-ground|fs-season-ground|fs-wet");
    assert(m->version > v);   // compiled anew
    assert(m->patch->fragmentSource.find("uFsWet") != std::string::npos && m->patch->vertexSource.find("vFsWetXZ") != std::string::npos);
    assert(m->patch->uniforms.count("uFsWet") && m->patch->uniforms.count("uFsSnow"));
    r.jump();
    r.update(Vector3(0, 0, 0), 1.0 / 60, *FIXED_WEATHER("rain"));
    assert(std::get<double>(m->patch->uniforms.at("uFsWet")->value) > 0.8);
    // (the paint too – the two materials of the roads the game makes wet)
    MaterialPtr paint = makeGroundMaterial(true);
    look.patch(*paint, "paint");
    r.addWetness(*paint);
    assert(paint->patch->key == "fs-paint|fs-season-paint|fs-wet");
  }

  // outdoors: the weather and the season together – the picture, the grip, the weather now, the requests
  {
    auto scene = make<Scene>();
    const SkyParts sky = fakeSky();
    std::vector<std::string> urls;
    double t = utcMs(2027, 0, 15, 12);
    auto realIn = std::make_unique<RealWeather>(RealWeatherOptions{.fetch = service([] { return std::string("ok"); }, &urls, nullptr), .now = [&t] { return t; }});
    Outdoors o(*scene, &sky, 500, std::move(realIn));
    const auto has = [&](const ObjectPtr& c) { return std::find(scene->children.begin(), scene->children.end(), c) != scene->children.end(); };
    assert(has(o.view.group) && has(o.tracks.mesh));
    const OutdoorsClock clock = {.real = true, .ms = [&t] { return t; }};
    const Vector3 cam(1000, 200, -2000);
    const Vector3 place(1000, 183, -2000);
    o.update(1.0 / 60, cam, place, {.weather = "rain", .season = "winter"}, nullptr, &clock);
    assert(o.weather == FIXED_WEATHER("rain"));
    assert(o.season.snow == 1);
    assert(std::abs(o.grip - 0.65) < 1e-9);   // rain on a snowy road
    assert(o.tracks.amount == 0.7);
    assert(weatherNowPtr() == FIXED_WEATHER("rain"));
    assert(snowOnGround() == 1);
    assert(sky.fog->far < 1312.5 || sky.sun->intensity < 3.2);   // the sky follows
    assert(urls.empty());   // a chosen weather asks nothing
    // the real weather, where the vehicle is
    o.update(1.0 / 60, cam, place, {.weather = "real", .season = "real"}, nullptr, &clock);
    assert(urls.size() == 1);
    const auto [lat, lon] = worldToLatLon(place.x, place.z);
    assert(param(urls[0], "latitude") == toFixed(jsRound(lat * 10) / 10, 1));
    assert(param(urls[0], "longitude") == toFixed(jsRound(lon * 10) / 10, 1));
    assert(param(urls[0], "elevation") == "180");
    plat::native::drain();
    o.update(1.0 / 60, cam, place, {.weather = "real", .season = "real"}, nullptr, &clock);
    assert(o.weather->kind == "rain");
    assert(o.season.snow == 0);   // January – but the real weather reports no snow lying
    for (int i = 0; i < 100; i++) o.update(1.0 / 60, cam, place, {.weather = "real", .season = "real"}, nullptr, &clock);
    assert(urls.size() == 1);   // not again for a quarter of an hour
    // the game clock at another moment: the forecast for that hour
    t += 2 * 60000;
    const OutdoorsClock other = {.real = false, .ms = [] { return utcMs(2027, 0, 17, 15, 10); }};
    o.update(1.0 / 60, cam, place, {.weather = "real", .season = "real"}, nullptr, &other);
    assert(urls.size() == 2);
    assert(urls[1].find("start_hour=2027-01-17T14:00") != std::string::npos);
    o.dispose();
    assert(weatherNowPtr() == CLEAR_WEATHER());
    assert(sky.fog->far == 1312.5);
    assert(sky.sun->intensity == 3.2);
  }

  std::printf("game_weather_test: ok\n");
  return 0;
}
