// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/settings.ts @ v1.7.7
#include "game/settings.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

#include "audio/voicepick.h"
#include "core/js.h"
#include "core/jsonwrite.h"
#include "i18n/index.h"
#include "input/camzoom.h"
#include "platform/json.h"
#include "platform/plat.h"

namespace fs {

bool lowDevice() {
  const int cores = plat::hardwareConcurrency();
  const double mem = plat::deviceMemory();
  return cores <= 4 || mem <= 3;
}

const Settings& DEFAULT_SETTINGS() {
  static const Settings defaults = [] {
    Settings s;
    // the first of the browser's languages that the simulator speaks
    s.lang = pickLang(plat::languages());
    // (a weak device – few cores or little memory – begins on the low preset: lowDevice)
    const bool low = lowDevice();
    s.quality = low ? "low" : "medium";
    s.graphics = GRAPHICS_PRESETS.medium;
    s.pixelCap = low ? 1 : 1.5;
    s.render3d = low ? 0.75 : 1;
    s.mirrors = low ? "off" : "alternate";
    s.traffic = low ? 0.4 : 0.6;
    return s;
  }();
  return defaults;
}

namespace {

const char* const KEY = "fahrsim-baunatal.settings.v2";
/** settings of the versions before the playtest (other defaults) */
const char* const OLD_KEY = "fahrsim-baunatal.settings.v1";

/** (a copy of the defaults: its lists and tables are its own) */
Settings fresh() { return DEFAULT_SETTINGS(); }

// --- what was stored, read as JavaScript reads it ----------------------------------------------
//
// The original lays what was stored over the defaults member by member, whatever it is, and then
// puts right what it checks.  A member of another kind than the setting's (a text where a number
// belongs – nothing any version of the game ever stored) cannot be carried along here: it keeps
// the default, as the members the original checks do.

bool isString(const Json& v) { return v.kind == Json::Kind::String; }
bool isNumber(const Json& v) { return v.kind == Json::Kind::Number; }
bool isBoolean(const Json& v) { return v.kind == Json::Kind::Bool; }

/** `!!v` */
bool truthy(const Json& v) {
  switch (v.kind) {
    case Json::Kind::Null: return false;
    case Json::Kind::Bool: return v.boolean;
    case Json::Kind::Number: return v.number != 0 && v.number == v.number;
    case Json::Kind::String: return !v.str.empty();
    default: return true;
  }
}

/** `Number(v)` of a member (`there`: it is there at all – else it is undefined: NaN) */
double numberOf(const Json& v, bool there) {
  if (!there) return JS_NAN;
  switch (v.kind) {
    case Json::Kind::Null: return 0;
    case Json::Kind::Bool: return v.boolean ? 1 : 0;
    case Json::Kind::Number: return v.number;
    case Json::Kind::String: return jsNumber(v.str);
    case Json::Kind::Array:
      // (an array becomes its text first: [] is "", [3] is "3")
      if (v.array.empty()) return 0;
      if (v.array.size() > 1 || v.array[0].kind == Json::Kind::Bool || v.array[0].kind == Json::Kind::Object) return JS_NAN;
      return numberOf(v.array[0], true);
    default: return JS_NAN;
  }
}

void take(const Json& o, const char* name, bool& out) {
  const Json& v = o.at(name);
  if (isBoolean(v)) out = v.boolean;
}
void take(const Json& o, const char* name, double& out) {
  const Json& v = o.at(name);
  if (isNumber(v)) out = v.number;
}
void take(const Json& o, const char* name, std::string& out) {
  const Json& v = o.at(name);
  if (isString(v)) out = v.str;
}
/** (the number of an axis or a button: a whole number) */
void take(const Json& o, const char* name, int& out) {
  const Json& v = o.at(name);
  if (isNumber(v) && v.number == std::floor(v.number) && std::fabs(v.number) <= std::numeric_limits<int>::max()) out = static_cast<int>(v.number);
}

bool oneOf(const std::string& v, std::initializer_list<const char*> words) {
  for (const char* w : words)
    if (v == w) return true;
  return false;
}
bool oneOf(double v, const std::vector<double>& numbers) { return std::find(numbers.begin(), numbers.end(), v) != numbers.end(); }

/** every setting by its name (and the two that are settings no more, or not always there) */
const char* const KNOWN[] = {"lang",         "vehicle",        "transmission", "powers",       "limiter",      "autopilotAssist", "arcadeDrive",      "evenRoads",
                             "evenTerrain",  "beamAssist",     "daylight",     "fuelKey",      "weather",      "season",          "flightAssist",     "music",
                             "musicVolume",  "runSpeed",       "steerSensitivity", "footView", "walkControls", "teleportTap",     "devMap",           "devMapFetch",
                             "devMapRepo",   "devMapRam",      "devMapProgress", "tramSet",    "menuStyle",    "tourPrefs",       "view",             "cabinOpacity",
                             "lastStart",    "quality",        "graphics",     "uiScale",      "pixelCap",     "render3d",        "mirrors",          "hudRules",
                             "signalTimers", "queueLabels",    "hudGear",      "voice",        "volume",       "traffic",         "drawDistance",     "wheel",
                             "invertLookY",  "reality",        "touchControls", "touchOpacity", "tiltSteer",   "keyHints",        "showFps",          "startOutside",
                             "startHere",    "placeExact",     "mapOnScreen",  "mapPack",      "region",       "screenTap",       "zoomRelease",      "insideMirror",
                             "lessonPlace",  "lessonPlaceAsked", "regionAsked", "lessonArea", "lessonWatch",  "wasm",         "server",          "version",          "speech",
                             "power"};

/** Defaults, overlaid with what was stored – whatever is missing or implausible keeps its default. */
Settings merge(const Json& parsed) {
  const Settings d = fresh();
  Settings s = d;
  // { ...d, ...parsed, graphics: { ...d.graphics, ...parsed.graphics }, wheel: …, reality: …, lastStart: …, insideMirror: … }
  take(parsed, "lang", s.lang);
  take(parsed, "vehicle", s.vehicle);
  take(parsed, "transmission", s.transmission);
  take(parsed, "limiter", s.limiter);
  take(parsed, "autopilotAssist", s.autopilotAssist);
  take(parsed, "arcadeDrive", s.arcadeDrive);
  take(parsed, "evenRoads", s.evenRoads);
  take(parsed, "evenTerrain", s.evenTerrain);
  take(parsed, "beamAssist", s.beamAssist);
  take(parsed, "daylight", s.daylight);
  take(parsed, "fuelKey", s.fuelKey);
  take(parsed, "weather", s.weather);
  take(parsed, "season", s.season);
  take(parsed, "flightAssist", s.flightAssist);
  take(parsed, "music", s.music);
  take(parsed, "musicVolume", s.musicVolume);
  take(parsed, "runSpeed", s.runSpeed);
  take(parsed, "steerSensitivity", s.steerSensitivity);
  take(parsed, "footView", s.footView);
  take(parsed, "walkControls", s.walkControls);
  take(parsed, "teleportTap", s.teleportTap);
  take(parsed, "devMap", s.devMap);
  take(parsed, "devMapFetch", s.devMapFetch);
  take(parsed, "devMapRepo", s.devMapRepo);
  take(parsed, "devMapRam", s.devMapRam);
  take(parsed, "devMapProgress", s.devMapProgress);
  take(parsed, "tramSet", s.tramSet);
  take(parsed, "menuStyle", s.menuStyle);
  take(parsed, "view", s.view);
  take(parsed, "cabinOpacity", s.cabinOpacity);
  take(parsed, "quality", s.quality);
  take(parsed, "uiScale", s.uiScale);
  take(parsed, "pixelCap", s.pixelCap);
  take(parsed, "render3d", s.render3d);
  take(parsed, "mirrors", s.mirrors);
  take(parsed, "hudRules", s.hudRules);
  take(parsed, "signalTimers", s.signalTimers);
  take(parsed, "queueLabels", s.queueLabels);
  take(parsed, "hudGear", s.hudGear);
  take(parsed, "voice", s.voice);
  take(parsed, "volume", s.volume);
  take(parsed, "traffic", s.traffic);
  take(parsed, "drawDistance", s.drawDistance);
  take(parsed, "invertLookY", s.invertLookY);
  take(parsed, "touchControls", s.touchControls);
  take(parsed, "touchOpacity", s.touchOpacity);
  take(parsed, "tiltSteer", s.tiltSteer);
  take(parsed, "keyHints", s.keyHints);
  take(parsed, "showFps", s.showFps);
  take(parsed, "startOutside", s.startOutside);
  take(parsed, "startHere", s.startHere);
  take(parsed, "placeExact", s.placeExact);
  take(parsed, "mapOnScreen", s.mapOnScreen);
  take(parsed, "mapPack", s.mapPack);
  take(parsed, "region", s.region);
  take(parsed, "regionAsked", s.regionAsked);   // 2.0
  take(parsed, "screenTap", s.screenTap);
  take(parsed, "zoomRelease", s.zoomRelease);
  take(parsed, "lessonPlace", s.lessonPlace);
  take(parsed, "lessonPlaceAsked", s.lessonPlaceAsked);
  take(parsed, "lessonWatch", s.lessonWatch);
  take(parsed, "wasm", s.wasm);
  take(parsed, "server", s.server);
  {
    const Json& g = parsed.at("graphics");
    take(g, "shadows", s.graphics.shadows);
    take(g, "viewDistance", s.graphics.viewDistance);
    take(g, "propDensity", s.graphics.propDensity);
    take(g, "facades", s.graphics.facades);
    take(g, "propShadows", s.graphics.propShadows);
    take(g, "farView", s.graphics.farView);
    take(g, "vehicleDetail", s.graphics.vehicleDetail);
    take(g, "water", s.graphics.water);
    take(g, "weatherFx", s.graphics.weatherFx);
  }
  {
    const Json& w = parsed.at("wheel");
    take(w, "steerAxis", s.wheel.steerAxis);
    take(w, "throttleAxis", s.wheel.throttleAxis);
    take(w, "brakeAxis", s.wheel.brakeAxis);
    take(w, "clutchAxis", s.wheel.clutchAxis);
    take(w, "pedalsInverted", s.wheel.pedalsInverted);
    take(w, "wheelRange", s.wheel.wheelRange);
    const Json& sh = w.at("shifter");
    if (sh.isArray()) {
      s.wheel.shifter.clear();
      // (what is no number of a button is a button the wheel does not have: -1)
      for (const Json& b : sh.array) {
        const bool whole = isNumber(b) && b.number == std::floor(b.number) && std::fabs(b.number) <= std::numeric_limits<int>::max();
        s.wheel.shifter.push_back(whole ? static_cast<int>(b.number) : -1);
      }
    }
  }
  {
    const Json& r = parsed.at("reality");
    take(r, "panoramax", s.reality.panoramax);
    take(r, "kartaview", s.reality.kartaview);
    take(r, "mapillary", s.reality.mapillary);
    take(r, "mapillaryToken", s.reality.mapillaryToken);
    take(r, "aerial", s.reality.aerial);
  }
  /** `lastStart.spawn` as it was stored (NaN: nothing that is a number) */
  double spawn = d.lastStart.spawn;
  /** `lastStart.address` as it was stored */
  const Json* a = nullptr;
  {
    const Json& l = parsed.at("lastStart");
    if (l.has("spawn")) spawn = isNumber(l.at("spawn")) ? l.at("spawn").number : JS_NAN;
    if (l.has("address")) a = &l.at("address");
    if (isString(l.at("tramStop"))) s.lastStart.tramStop = l.at("tramStop").str;
    if (isString(l.at("runway"))) s.lastStart.runway = l.at("runway").str;
  }
  {
    const Json& m = parsed.at("insideMirror");
    take(m, "mode", s.insideMirror.mode);
    take(m, "shift", s.insideMirror.shift);
    take(m, "size", s.insideMirror.size);
    take(m, "fx", s.insideMirror.fx);
    take(m, "fy", s.insideMirror.fy);
  }
  // (what this version does not know is kept as it is)
  if (parsed.isObject()) {
    for (const auto& [name, member] : parsed.object) {
      if (std::find(std::begin(KNOWN), std::end(KNOWN), name) != std::end(KNOWN)) continue;
      s.unknown.emplace_back(name, member.stringify());
    }
  }

  if (!std::any_of(std::begin(LANGS), std::end(LANGS), [&s](const auto& l) { return l.code == s.lang; })) s.lang = d.lang;
  // (before 1.2.0 there was one engine power, the car's)
  const Json& old = parsed.at("power");
  const Json& powers = parsed.at("powers");
  s.powers.clear();
  // (kept are the powers of the kinds of vehicle there are, where they are numbers)
  if (powers.isObject()) {
    for (const auto& [k, p] : powers.object)
      if (vehicleKind(k) && isNumber(p)) s.powers[k] = p.number;
  }
  if (isNumber(old) && !powers.has("car")) s.powers["car"] = old.number;
  if (s.view != "chase" && s.view != "cockpit") s.view = d.view;
  if (!vehicleKind(s.vehicle)) s.vehicle = d.vehicle;
  if (!oneOf(s.runSpeed, RUN_SPEEDS)) s.runSpeed = d.runSpeed;
  if (!std::isfinite(s.steerSensitivity)) s.steerSensitivity = d.steerSensitivity;
  else s.steerSensitivity = std::min(2.0, std::max(0.5, s.steerSensitivity));
  if (s.footView != "eyes" && s.footView != "behind") s.footView = d.footView;
  if (!oneOf(s.walkControls, {"auto", "classic", "tank", "tap"})) s.walkControls = d.walkControls;
  if (s.devMapFetch != "site" && s.devMapFetch != "repo" && s.devMapFetch != "pack") s.devMapFetch = d.devMapFetch;
  if (!(s.devMapRam >= 16 && s.devMapRam <= 4096)) s.devMapRam = d.devMapRam;
  if (s.tramSet != "single" && s.tramSet != "double" && s.tramSet != "trailer") s.tramSet = d.tramSet;
  if (s.menuStyle != "stage" && s.menuStyle != "carousel" && s.menuStyle != "classic") s.menuStyle = d.menuStyle;
  s.tourPrefs = DEFAULT_TOUR_PREFS;
  {
    const Json& t = parsed.at("tourPrefs");
    take(t, "noMotorway", s.tourPrefs.noMotorway);
    take(t, "noExpress", s.tourPrefs.noExpress);
    take(t, "noMain", s.tourPrefs.noMain);
    take(t, "mostly", s.tourPrefs.mostly);
  }
  if (!oneOf(s.tourPrefs.mostly, {"any", "small", "town", "country"})) s.tourPrefs.mostly = "any";
  const Json& graphics = parsed.at("graphics");
  // (stored before there was a far view: the one of the preset chosen then)
  if (truthy(graphics) && !isNumber(graphics.at("farView"))) {
    // (a quality stored that is no word at all is no preset either)
    const bool word = !parsed.has("quality") || isString(parsed.at("quality"));
    const Graphics* preset = word ? GRAPHICS_PRESETS.find(s.quality) : nullptr;
    s.graphics.farView = (preset ? preset : &GRAPHICS_PRESETS.medium)->farView;
  }
  if (!oneOf(s.graphics.farView, FAR_VIEWS)) {
    double nearest = FAR_VIEWS[0];
    for (double b : FAR_VIEWS)
      if (std::fabs(b - s.graphics.farView) < std::fabs(nearest - s.graphics.farView)) nearest = b;
    s.graphics.farView = nearest;
  }
  if (!oneOf(s.transmission, {"assist", "manual", "automatic"})) s.transmission = d.transmission;
  if (!oneOf(s.quality, {"low", "medium", "high", "ultra", "nomercy", "custom"})) s.quality = d.quality;
  // (stored before there were advanced graphics settings: what the level gave then)
  if (!truthy(graphics)) {
    s.graphics = *GRAPHICS_PRESETS.find(s.quality == "custom" ? "medium" : s.quality);
    // (a graphics preset does not take the resolutions over: they are settings of their own –
    //  the low preset draws less, but the render and display resolution stay as chosen)
    if (isString(parsed.at("quality")) && parsed.at("quality").str == "high") s.pixelCap = 3;
  }
  Graphics& g = s.graphics;
  if (!oneOf(g.shadows, {"off", "near", "far"})) g.shadows = d.graphics.shadows;
  if (!oneOf(g.viewDistance, VIEW_DISTANCES)) g.viewDistance = d.graphics.viewDistance;
  if (!(g.propDensity >= 0 && g.propDensity <= 1)) g.propDensity = d.graphics.propDensity;
  // (stored before there were finer vehicles: as the preset of then gives them – on "very high" only)
  if (!isBoolean(graphics.at("vehicleDetail"))) g.vehicleDetail = s.quality == "ultra";
  if (!isBoolean(graphics.at("water"))) g.water = s.quality == "ultra";
  // (before 1.7.1 "very high" had the weather on the vehicle and the roads; now only "No mercy" – a custom choice of before keeps it)
  if (!isBoolean(graphics.at("weatherFx"))) g.weatherFx = s.quality == "nomercy" || (s.quality == "custom" && g.vehicleDetail);
  if (!(s.uiScale >= 0.7 && s.uiScale <= 1.6)) s.uiScale = d.uiScale;
  if (!oneOf(s.pixelCap, {1, 1.5, 3})) s.pixelCap = d.pixelCap;
  if (!(s.render3d >= 0.5 && s.render3d <= 1)) s.render3d = d.render3d;
  if (!(s.cabinOpacity >= 0.2 && s.cabinOpacity <= 1)) s.cabinOpacity = d.cabinOpacity;
  if (s.screenTap != "bigmap" && s.screenTap != "minimap") s.screenTap = d.screenTap;
  if (std::find(std::begin(ZOOM_RELEASES), std::end(ZOOM_RELEASES), s.zoomRelease) == std::end(ZOOM_RELEASES)) s.zoomRelease = d.zoomRelease;
  if (s.wasm != "auto" && s.wasm != "off") s.wasm = d.wasm;
  // (the address of the server is kept as it was stored: the original puts it right with net/client.ts's
  //  normaliseServerUrl – the server is no part of the port)
  s.speech.emplace();
  for (const auto& [lang, pref] : cleanSpeechPrefs(parsed.at("speech"))) (*s.speech)[lang] = pref;
  {
    Settings::InsideMirror& m = s.insideMirror;
    if (m.mode != "car" && m.mode != "hud") m.mode = d.insideMirror.mode;
    if (!(m.shift >= -0.3 && m.shift <= 0.3)) m.shift = d.insideMirror.shift;
    if (!(m.size >= 0.6 && m.size <= 2)) m.size = d.insideMirror.size;
    if (!(m.fx >= 0 && m.fx <= 1)) m.fx = d.insideMirror.fx;
    if (!(m.fy >= 0 && m.fy <= 1)) m.fy = d.insideMirror.fy;
  }
  const double version = numberOf(parsed.at("version"), parsed.has("version"));
  // settings written before 1.4.0 stored the map's old default (not on the screen) as if chosen: the new default, once
  if (!(version >= 2)) s.mapOnScreen = true;
  // (the same for the aerial photographs under the maps: before 1.4.0 they had to be asked for –
  // 2.0: they are again, so settings of before 1.4.0 keep them off: reality/aerial.h)
  // (and for the main menu: the rows were 1.3.0's default – the stage is 1.4.0's; a list chosen stays a list)
  if (!(version >= 2) && s.menuStyle == "carousel") s.menuStyle = "stage";
  // (1.7.1: simpler physics by default – the even grip of every road and field; settings of before stored the old default)
  if (!(version >= 3)) s.evenRoads = true;
  s.version = SETTINGS_VERSION;
  if (s.daylight != "day" && s.daylight != "clock") s.daylight = d.daylight;
  if (s.music != "off" && s.music != "quiet" && s.music != "on") s.music = d.music;
  if (s.flightAssist != "off" && s.flightAssist != "light" && s.flightAssist != "beginner") s.flightAssist = d.flightAssist;
  if (!oneOf(s.weather, {"off", "real", "cloudy", "fog", "drizzle", "rain", "snow", "storm"})) s.weather = d.weather;
  if (!oneOf(s.season, {"real", "spring", "summer", "autumn", "winter"})) s.season = d.season;
  if (!(s.musicVolume >= 0 && s.musicVolume <= 1)) s.musicVolume = d.musicVolume;
  // (a start place by its number: a whole number, not below 0)
  s.lastStart.spawn = spawn == std::floor(spawn) && spawn >= 0 && spawn <= std::numeric_limits<int>::max() ? static_cast<int>(spawn) : 0;
  s.lastStart.address.reset();
  if (a && truthy(*a) && isString(a->at("label")) && isNumber(a->at("x")) && std::isfinite(a->at("x").number) && isNumber(a->at("z")) && std::isfinite(a->at("z").number)) {
    StartAddress address;
    address.label = a->at("label").str;
    take(*a, "street", address.street);
    address.x = a->at("x").number;
    address.z = a->at("z").number;
    take(*a, "exact", address.exact);
    s.lastStart.address = address;
  }
  if (s.lessonPlace != "start" && s.lessonPlace != "town" && s.lessonPlace != "area") s.lessonPlace = d.lessonPlace;
  const Json& la = parsed.at("lessonArea");
  s.lessonArea.reset();
  if (isString(la.at("name")) && isNumber(la.at("x")) && std::isfinite(la.at("x").number) && isNumber(la.at("z")) && std::isfinite(la.at("z").number))
    s.lessonArea = LessonArea{.name = la.at("name").str, .x = la.at("x").number, .z = la.at("z").number};
  return s;
}

/** `JSON.stringify(s)` */
std::string textOf(const Settings& s) {
  JsonOut o;
  const auto flag = [&o](const char* name, bool v) {
    o.key(name);
    o.boolean(v);
  };
  const auto num = [&o](const char* name, double v) {
    o.key(name);
    o.number(v);
  };
  const auto str = [&o](const char* name, const std::string& v) {
    o.key(name);
    o.string(v);
  };
  o.beginObject();
  str("lang", s.lang);
  str("vehicle", s.vehicle);
  str("transmission", s.transmission);
  o.key("powers");
  o.beginObject();
  for (const auto& [kind, power] : s.powers) num(kind.c_str(), power);
  o.endObject();
  flag("limiter", s.limiter);
  flag("autopilotAssist", s.autopilotAssist);
  flag("arcadeDrive", s.arcadeDrive);
  flag("evenRoads", s.evenRoads);
  flag("evenTerrain", s.evenTerrain);
  flag("beamAssist", s.beamAssist);
  str("daylight", s.daylight);
  str("fuelKey", s.fuelKey);
  str("weather", s.weather);
  str("season", s.season);
  str("flightAssist", s.flightAssist);
  str("music", s.music);
  num("musicVolume", s.musicVolume);
  num("runSpeed", s.runSpeed);
  num("steerSensitivity", s.steerSensitivity);
  str("footView", s.footView);
  str("walkControls", s.walkControls);
  flag("teleportTap", s.teleportTap);
  flag("devMap", s.devMap);
  str("devMapFetch", s.devMapFetch);
  str("devMapRepo", s.devMapRepo);
  num("devMapRam", s.devMapRam);
  flag("devMapProgress", s.devMapProgress);
  str("tramSet", s.tramSet);
  str("menuStyle", s.menuStyle);
  o.key("tourPrefs");
  o.beginObject();
  flag("noMotorway", s.tourPrefs.noMotorway);
  flag("noExpress", s.tourPrefs.noExpress);
  flag("noMain", s.tourPrefs.noMain);
  str("mostly", s.tourPrefs.mostly);
  o.endObject();
  str("view", s.view);
  num("cabinOpacity", s.cabinOpacity);
  o.key("lastStart");
  o.beginObject();
  num("spawn", s.lastStart.spawn);
  o.key("address");
  if (s.lastStart.address) {
    const StartAddress& a = *s.lastStart.address;
    o.beginObject();
    str("label", a.label);
    str("street", a.street);
    num("x", a.x);
    num("z", a.z);
    flag("exact", a.exact);
    o.endObject();
  } else o.null();
  if (s.lastStart.tramStop) str("tramStop", *s.lastStart.tramStop);
  if (s.lastStart.runway) str("runway", *s.lastStart.runway);
  o.endObject();
  str("quality", s.quality);
  o.key("graphics");
  o.beginObject();
  str("shadows", s.graphics.shadows);
  num("viewDistance", s.graphics.viewDistance);
  num("propDensity", s.graphics.propDensity);
  flag("facades", s.graphics.facades);
  flag("propShadows", s.graphics.propShadows);
  num("farView", s.graphics.farView);
  flag("vehicleDetail", s.graphics.vehicleDetail);
  flag("water", s.graphics.water);
  flag("weatherFx", s.graphics.weatherFx);
  o.endObject();
  num("uiScale", s.uiScale);
  num("pixelCap", s.pixelCap);
  num("render3d", s.render3d);
  str("mirrors", s.mirrors);
  flag("hudRules", s.hudRules);
  flag("signalTimers", s.signalTimers);
  flag("queueLabels", s.queueLabels);
  flag("hudGear", s.hudGear);
  flag("voice", s.voice);
  num("volume", s.volume);
  num("traffic", s.traffic);
  num("drawDistance", s.drawDistance);
  o.key("wheel");
  o.beginObject();
  num("steerAxis", s.wheel.steerAxis);
  num("throttleAxis", s.wheel.throttleAxis);
  num("brakeAxis", s.wheel.brakeAxis);
  num("clutchAxis", s.wheel.clutchAxis);
  flag("pedalsInverted", s.wheel.pedalsInverted);
  num("wheelRange", s.wheel.wheelRange);
  o.key("shifter");
  o.beginArray();
  for (int b : s.wheel.shifter) o.number(b);
  o.endArray();
  o.endObject();
  flag("invertLookY", s.invertLookY);
  o.key("reality");
  o.beginObject();
  flag("panoramax", s.reality.panoramax);
  flag("kartaview", s.reality.kartaview);
  flag("mapillary", s.reality.mapillary);
  str("mapillaryToken", s.reality.mapillaryToken);
  flag("aerial", s.reality.aerial);
  o.endObject();
  str("touchControls", s.touchControls);
  num("touchOpacity", s.touchOpacity);
  flag("tiltSteer", s.tiltSteer);
  flag("keyHints", s.keyHints);
  flag("showFps", s.showFps);
  flag("startOutside", s.startOutside);
  flag("startHere", s.startHere);
  flag("placeExact", s.placeExact);
  flag("mapOnScreen", s.mapOnScreen);
  str("mapPack", s.mapPack);
  str("region", s.region);
  if (s.regionAsked) flag("regionAsked", s.regionAsked);   // 2.0 (only when set: settings stored before stay as they were)
  str("screenTap", s.screenTap);
  str("zoomRelease", s.zoomRelease);
  o.key("insideMirror");
  o.beginObject();
  str("mode", s.insideMirror.mode);
  num("shift", s.insideMirror.shift);
  num("size", s.insideMirror.size);
  num("fx", s.insideMirror.fx);
  num("fy", s.insideMirror.fy);
  o.endObject();
  str("lessonPlace", s.lessonPlace);
  flag("lessonPlaceAsked", s.lessonPlaceAsked);
  o.key("lessonArea");
  if (s.lessonArea) {
    o.beginObject();
    str("name", s.lessonArea->name);
    num("x", s.lessonArea->x);
    num("z", s.lessonArea->z);
    o.endObject();
  } else o.null();
  flag("lessonWatch", s.lessonWatch);
  str("wasm", s.wasm);
  str("server", s.server);
  num("version", s.version);
  if (s.speech) {
    o.key("speech");
    o.beginObject();
    for (const auto& [lang, pref] : *s.speech) {
      o.key(lang);
      o.beginObject();
      str("voice", pref.voice);
      num("rate", pref.rate);
      num("pitch", pref.pitch);
      o.endObject();
    }
    o.endObject();
  }
  for (const auto& [name, json] : s.unknown) {
    o.key(name);
    o.raw(json);
  }
  o.endObject();
  return o.text();
}

}  // namespace

Settings resetSettings(const Settings& s) {
  Settings r = fresh();
  r.lang = s.lang;
  r.lastStart = s.lastStart;
  r.mapPack = s.mapPack;
  r.region = s.region;
  r.fuelKey = s.fuelKey;
  r.server = s.server;
  r.reality.mapillaryToken = s.reality.mapillaryToken;
  r.lessonPlaceAsked = s.lessonPlaceAsked;
  r.regionAsked = s.regionAsked;   // 2.0
  return r;
}

bool playedBefore() { return plat::localStorage().getItem(KEY).has_value() || plat::localStorage().getItem(OLD_KEY).has_value(); }

Settings loadSettings() {
  // (what is stored and is no JSON, or no settings at all: the defaults)
  const std::optional<std::string> raw = plat::localStorage().getItem(KEY);
  if (raw && !raw->empty()) {
    Json parsed;
    if (!Json::parse(*raw, parsed) || parsed.kind == Json::Kind::Null) return fresh();
    return merge(parsed);
  }
  const std::optional<std::string> old = plat::localStorage().getItem(OLD_KEY);
  if (old && !old->empty()) {
    // Older versions stored every setting, also those nobody had touched.  What was a default
    // then and is another one now (gearbox with clutch help) follows the new default.
    Json parsed;
    if (!Json::parse(*old, parsed) || parsed.kind == Json::Kind::Null) return fresh();
    if (isString(parsed.at("transmission")) && parsed.at("transmission").str == "assist") parsed.object.erase("transmission");
    const Settings s = merge(parsed);
    saveSettings(s);
    return s;
  }
  return fresh();
}

bool saveSettings(const Settings& s) { return plat::localStorage().setItem(KEY, textOf(s)); }

}  // namespace fs
