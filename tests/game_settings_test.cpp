// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The settings (src/game/settings.h): kept in the browser as the JSON the original writes,
 * under its keys – what 1.7.7 stored is read here in full and stored again as it was.
 *
 * With the settings cases of the original's tests/ui/stage.test.ts and
 * tests/game/lessonwatch.test.ts (those of cockpit.test.ts, nomercy.test.ts and
 * arcadedrive.test.ts are in the tests of those names).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "game/settings.h"
#include "platform/json.h"
#include "platform/native/native.h"
#include "platform/plat.h"

using namespace fs;

static const char* const KEY = "fahrsim-baunatal.settings.v2";
static const char* const OLD_KEY = "fahrsim-baunatal.settings.v1";

/** a browser's storage with nothing in it – or with one entry */
static void storage() {
  plat::localStorage().removeItem(KEY);
  plat::localStorage().removeItem(OLD_KEY);
}
static void storage(const char* key, const std::string& text) {
  storage();
  plat::localStorage().setItem(key, text);
}

/** assert.deepEqual of two documents (the order of an object's members is no matter) */
static bool same(const Json& a, const Json& b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case Json::Kind::Null: return true;
    case Json::Kind::Bool: return a.boolean == b.boolean;
    case Json::Kind::Number: return a.number == b.number;
    case Json::Kind::String: return a.str == b.str;
    case Json::Kind::Array:
      if (a.array.size() != b.array.size()) return false;
      for (size_t i = 0; i < a.array.size(); i++)
        if (!same(a.array[i], b.array[i])) return false;
      return true;
    case Json::Kind::Object: {
      size_t n = 0;
      for (const auto& [name, member] : a.object) {
        if (!b.has(name) || !same(member, b.at(name))) return false;
        n++;
      }
      size_t m = 0;
      for (const auto& [name, member] : b.object) {
        (void)name;
        (void)member;
        m++;
      }
      return n == m;
    }
  }
  return false;
}

/** what is stored under the key, as a document */
static Json stored() {
  Json j;
  const auto text = plat::localStorage().getItem(KEY);
  assert(text && Json::parse(*text, j));
  return j;
}

/** every setting chosen otherwise than it comes, as 1.7.7 writes it (JSON.stringify of its settings) */
static const char* const EVERY_177 =
    R"({"lang":"fr","vehicle":"bus","transmission":"manual","powers":{"bus":354,"car":115},"limiter":true,"autopilotAssist":false,"arcadeDrive":true,"evenRoads":false,"evenTerrain":true,"beamAssist":true,"daylight":"clock","fuelKey":"k-1","weather":"snow","season":"winter","flightAssist":"light","music":"on","musicVolume":0.25,"runSpeed":18,"steerSensitivity":1.4,"footView":"behind","walkControls":"tap","teleportTap":false,"devMap":true,"devMapFetch":"pack","devMapRepo":"x/y","devMapRam":512,"devMapProgress":false,"tramSet":"double","menuStyle":"classic","tourPrefs":{"noMotorway":true,"noExpress":true,"noMain":true,"mostly":"country"},"view":"cockpit","cabinOpacity":0.6,"lastStart":{"spawn":7,"address":{"label":"Marktplatz 5, Baunatal","street":"Marktplatz","x":12.5,"z":-40,"exact":true},"tramStop":"Rathaus|Kassel","runway":"Calden|22"},"quality":"custom","graphics":{"shadows":"off","viewDistance":1600,"propDensity":0.35,"facades":false,"propShadows":true,"farView":30000,"vehicleDetail":true,"water":false,"weatherFx":true},"uiScale":1.25,"pixelCap":3,"render3d":0.6,"mirrors":"full","hudRules":false,"signalTimers":false,"queueLabels":false,"hudGear":false,"voice":true,"volume":0.15,"traffic":0.85,"drawDistance":900,"wheel":{"steerAxis":1,"throttleAxis":3,"brakeAxis":4,"clutchAxis":6,"pedalsInverted":false,"wheelRange":540,"shifter":[20,21,22,23,24,25,26]},"invertLookY":true,"reality":{"panoramax":true,"kartaview":true,"mapillary":true,"mapillaryToken":"tok","aerial":false},"touchControls":"on","touchOpacity":0.9,"tiltSteer":true,"keyHints":false,"showFps":true,"startOutside":true,"startHere":true,"placeExact":true,"mapOnScreen":false,"mapPack":"mine","region":"nordhessen","screenTap":"minimap","zoomRelease":"12","insideMirror":{"mode":"hud","shift":-0.2,"size":1.7,"fx":0.3,"fy":0.6},"lessonPlace":"area","lessonPlaceAsked":true,"lessonArea":{"name":"Wehlheiden","x":4265,"z":-5927},"lessonWatch":true,"wasm":"off","server":"https://fahrsim.example","version":3,"speech":{"fr":{"voice":"urn:moz-tts:fr","rate":1.3,"pitch":0.8}}})";

/** … and the settings of somebody who has just begun, as 1.7.7 writes them on a device that is not weak, in German */
static const char* const DEFAULT_177 =
    R"({"lang":"de","vehicle":"car","transmission":"automatic","powers":{},"limiter":false,"autopilotAssist":true,"arcadeDrive":false,"evenRoads":true,"evenTerrain":false,"beamAssist":false,"daylight":"day","fuelKey":"","weather":"real","season":"real","flightAssist":"beginner","music":"off","musicVolume":1,"runSpeed":12,"steerSensitivity":1,"footView":"eyes","walkControls":"auto","teleportTap":true,"devMap":false,"devMapFetch":"repo","devMapRepo":"MaurerAnton/driving-simulator","devMapRam":256,"devMapProgress":true,"tramSet":"single","menuStyle":"stage","tourPrefs":{"noMotorway":false,"noExpress":false,"noMain":false,"mostly":"any"},"view":"chase","cabinOpacity":1,"lastStart":{"spawn":0,"address":null},"quality":"medium","graphics":{"shadows":"near","viewDistance":1250,"propDensity":0.8,"facades":true,"propShadows":false,"farView":8000,"vehicleDetail":false,"water":false,"weatherFx":false},"uiScale":1,"pixelCap":1.5,"render3d":1,"mirrors":"alternate","hudRules":true,"signalTimers":true,"queueLabels":true,"hudGear":true,"voice":false,"volume":0.7,"traffic":0.6,"drawDistance":1300,"wheel":{"steerAxis":0,"throttleAxis":2,"brakeAxis":5,"clutchAxis":1,"pedalsInverted":true,"wheelRange":900,"shifter":[12,13,14,15,16,17,18]},"invertLookY":false,"reality":{"panoramax":false,"kartaview":false,"mapillary":false,"mapillaryToken":"","aerial":true},"touchControls":"auto","touchOpacity":0.55,"tiltSteer":false,"keyHints":true,"showFps":false,"startOutside":false,"startHere":false,"placeExact":false,"mapOnScreen":true,"mapPack":"","region":"","screenTap":"bigmap","zoomRelease":"auto","insideMirror":{"mode":"car","shift":0,"size":1,"fx":0.5,"fy":0.09},"lessonPlace":"start","lessonPlaceAsked":false,"lessonArea":null,"lessonWatch":false,"wasm":"auto","server":"","version":3})";

int main() {
  // (the first of the browser's languages that the simulator speaks – asked once, when the defaults are first wanted)
  plat::native::setLanguages({"de-DE", "de", "en"});

  // --- the defaults: as the original's, text for text ---
  {
    const Settings& d = DEFAULT_SETTINGS();
    assert(d.lang == "de");
    assert(!lowDevice());
    assert(d.quality == "medium" && d.pixelCap == 1.5 && d.render3d == 1 && d.mirrors == "alternate" && d.traffic == 0.6);
    assert(d.graphics == GRAPHICS_PRESETS.medium);
    assert(d.version == SETTINGS_VERSION && SETTINGS_VERSION == 3);
    assert(!d.speech && d.unknown.empty());
    storage();
    assert(!playedBefore());
    // nothing stored: the defaults, and nothing is stored by reading them
    const Settings s = loadSettings();
    assert(!playedBefore());
    assert(saveSettings(s));
    assert(playedBefore());
    Json want;
    assert(Json::parse(DEFAULT_177, want));
    assert(same(stored(), want));
    // (tests/game/lessonwatch.test.ts)
    assert(d.lessonWatch == false);   // driven unless chosen
  }

  // --- what 1.7.7 stored is read in full … ---
  {
    storage(KEY, EVERY_177);
    assert(playedBefore());
    const Settings s = loadSettings();
    assert(s.lang == "fr" && s.vehicle == "bus" && s.transmission == "manual");
    assert(s.powers.size() == 2 && s.powers.at("bus") == 354 && s.powers.at("car") == 115);
    assert(s.limiter && !s.autopilotAssist && s.arcadeDrive && !s.evenRoads && s.evenTerrain && s.beamAssist);
    assert(s.daylight == "clock" && s.fuelKey == "k-1" && s.weather == "snow" && s.season == "winter" && s.flightAssist == "light");
    assert(s.music == "on" && s.musicVolume == 0.25 && s.runSpeed == 18 && s.steerSensitivity == 1.4);
    assert(s.footView == "behind" && s.walkControls == "tap" && !s.teleportTap);
    assert(s.devMap && s.devMapFetch == "pack" && s.devMapRepo == "x/y" && s.devMapRam == 512 && !s.devMapProgress);
    assert(s.tramSet == "double" && s.menuStyle == "classic");
    assert(s.tourPrefs.noMotorway && s.tourPrefs.noExpress && s.tourPrefs.noMain && s.tourPrefs.mostly == "country");
    assert(s.view == "cockpit" && s.cabinOpacity == 0.6);
    assert(s.lastStart.spawn == 7 && s.lastStart.address && s.lastStart.address->label == "Marktplatz 5, Baunatal" && s.lastStart.address->street == "Marktplatz");
    assert(s.lastStart.address->x == 12.5 && s.lastStart.address->z == -40 && s.lastStart.address->exact);
    assert(s.lastStart.tramStop == "Rathaus|Kassel" && s.lastStart.runway == "Calden|22");
    assert(s.quality == "custom");
    const Graphics g = {.shadows = "off", .viewDistance = 1600, .propDensity = 0.35, .facades = false, .propShadows = true, .farView = 30000, .vehicleDetail = true, .water = false, .weatherFx = true};
    assert(s.graphics == g);
    assert(s.uiScale == 1.25 && s.pixelCap == 3 && s.render3d == 0.6 && s.mirrors == "full");
    assert(!s.hudRules && !s.signalTimers && !s.queueLabels && !s.hudGear && s.voice && s.volume == 0.15);
    assert(s.speech && s.speech->size() == 1 && s.speech->at("fr").voice == "urn:moz-tts:fr" && s.speech->at("fr").rate == 1.3 && s.speech->at("fr").pitch == 0.8);
    assert(s.traffic == 0.85 && s.drawDistance == 900);
    assert(s.wheel.steerAxis == 1 && s.wheel.throttleAxis == 3 && s.wheel.brakeAxis == 4 && s.wheel.clutchAxis == 6 && !s.wheel.pedalsInverted && s.wheel.wheelRange == 540);
    assert((s.wheel.shifter == std::vector<int>{20, 21, 22, 23, 24, 25, 26}));
    assert(s.invertLookY);
    assert(s.reality.panoramax && s.reality.kartaview && s.reality.mapillary && s.reality.mapillaryToken == "tok" && !s.reality.aerial);
    assert(s.touchControls == "on" && s.touchOpacity == 0.9 && s.tiltSteer && !s.keyHints && s.showFps);
    assert(s.startOutside && s.startHere && s.placeExact && !s.mapOnScreen && s.mapPack == "mine" && s.region == "nordhessen");
    assert(s.screenTap == "minimap" && s.zoomRelease == "12");
    assert(s.insideMirror.mode == "hud" && s.insideMirror.shift == -0.2 && s.insideMirror.size == 1.7 && s.insideMirror.fx == 0.3 && s.insideMirror.fy == 0.6);
    assert(s.lessonPlace == "area" && s.lessonPlaceAsked && s.lessonArea && s.lessonArea->name == "Wehlheiden" && s.lessonArea->x == 4265 && s.lessonArea->z == -5927);
    assert(s.lessonWatch && s.wasm == "off" && s.server == "https://fahrsim.example" && s.version == 3);
    assert(s.unknown.empty());
    // … and stored again as it was: the original reads back what it wrote
    storage();
    assert(saveSettings(s));
    Json want;
    assert(Json::parse(EVERY_177, want));
    assert(same(stored(), want));
  }

  // --- what a later version stores beside the settings known here is kept, and stored again ---
  {
    std::string text = EVERY_177;
    text.insert(text.size() - 1, R"(,"later":{"a":[1,"two",null,true,2.5],"b":"x\ny\"z"},"another":5)");
    storage(KEY, text);
    const Settings s = loadSettings();
    assert(s.unknown.size() == 2);
    storage();
    saveSettings(s);
    Json want;
    assert(Json::parse(text, want));
    assert(same(stored(), want));
    // ("Reset settings" is this version's settings alone)
    assert(resetSettings(s).unknown.empty());
  }

  // --- tests/ui/stage.test.ts: the main menu – the stage for new players, a menu chosen stays;
  //     the rows, the default of 1.3.0, become the stage once ---
  {
    storage();
    assert(DEFAULT_SETTINGS().menuStyle == "stage");
    assert(loadSettings().menuStyle == "stage");   // nothing stored: a new player
    for (const char* style : {"carousel", "classic", "stage"}) {
      // (written by 1.4.0 or later: what was chosen stays)
      storage(KEY, std::string(R"({"vehicle":"bus","menuStyle":")") + style + R"(","version":2})");
      const Settings s = loadSettings();
      assert(s.menuStyle == style);
      assert(s.vehicle == "bus");
    }
    // written by 1.3.0, which stored its default: the rows become the stage once, a list chosen stays a list
    storage(KEY, R"({"vehicle":"bus","menuStyle":"carousel"})");
    assert(loadSettings().menuStyle == "stage");
    storage(KEY, R"({"vehicle":"bus","menuStyle":"classic"})");
    assert(loadSettings().menuStyle == "classic");
    // stored before there was a choice of menus (before 1.3.0), or something unknown: the stage
    storage(KEY, R"({"vehicle":"truck"})");
    assert(loadSettings().menuStyle == "stage");
    storage(KEY, R"({"menuStyle":"cards"})");
    assert(loadSettings().menuStyle == "stage");
    // the settings of the versions before the playtest
    storage(OLD_KEY, R"({"menuStyle":"classic"})");
    assert(loadSettings().menuStyle == "classic");
    storage();
  }

  // --- the settings of the versions before the playtest move to the new key; their gearbox with
  //     clutch help was only the default of then ---
  {
    storage(OLD_KEY, R"({"transmission":"assist","volume":0.2})");
    assert(playedBefore());
    Settings s = loadSettings();
    assert(s.transmission == "automatic" && s.volume == 0.2);
    assert(stored().at("transmission").str == "automatic" && stored().at("volume").number == 0.2);
    storage(OLD_KEY, R"({"transmission":"manual"})");
    assert(loadSettings().transmission == "manual");
    // (the new key counts where both are there)
    plat::localStorage().setItem(OLD_KEY, R"({"transmission":"manual","volume":0.9})");
    plat::localStorage().setItem(KEY, R"({"volume":0.3,"version":3})");
    assert(loadSettings().volume == 0.3);
  }

  // --- before 1.2.0 there was one engine power, the car's ---
  {
    storage(KEY, R"({"power":115})");
    Settings s = loadSettings();
    assert(s.powers.size() == 1 && s.powers.at("car") == 115);
    storage(KEY, R"({"power":60,"powers":{"car":95,"truck":300,"ufo":5,"bus":"x"}})");
    s = loadSettings();
    assert(s.powers.size() == 2 && s.powers.at("car") == 95 && s.powers.at("truck") == 300);
    storage();
    saveSettings(s);
    assert(!stored().has("power"));
  }

  // --- whatever is implausible keeps its default ---
  {
    storage(KEY, R"({"version":3,"lang":"xx","vehicle":"ufo","runSpeed":11,"steerSensitivity":5,"devMapRam":8,"uiScale":3,"pixelCap":2,"render3d":0.1,"cabinOpacity":0,)"
                 R"("graphics":{"shadows":"x","viewDistance":800,"propDensity":2,"farView":6400},"insideMirror":{"mode":"x","shift":1,"size":9,"fx":-1,"fy":2},)"
                 R"("lastStart":{"spawn":-2,"address":{"label":5,"x":1,"z":2}},"lessonArea":{"name":"A","x":"1","z":2},"zoomRelease":"9","tourPrefs":{"mostly":"sea"}})");
    const Settings s = loadSettings();
    const Settings& d = DEFAULT_SETTINGS();
    assert(s.lang == d.lang && s.vehicle == "car" && s.runSpeed == 12 && s.devMapRam == 256 && s.uiScale == 1 && s.pixelCap == d.pixelCap && s.render3d == d.render3d && s.cabinOpacity == 1);
    // (how sharply the keys steer is kept within its range; a far view that is none becomes the nearest there is)
    assert(s.steerSensitivity == 2);
    assert(s.graphics.shadows == "near" && s.graphics.viewDistance == 1250 && s.graphics.propDensity == 0.8 && s.graphics.farView == 5000);
    assert(s.insideMirror.mode == "car" && s.insideMirror.shift == 0 && s.insideMirror.size == 1 && s.insideMirror.fx == 0.5 && s.insideMirror.fy == 0.09);
    assert(s.lastStart.spawn == 0 && !s.lastStart.address && !s.lessonArea && s.zoomRelease == "auto" && s.tourPrefs.mostly == "any");
    // what is no JSON, or no settings at all: the defaults
    for (const char* text : {"{", "not json", "null", "[1,2"}) {
      storage(KEY, text);
      assert(loadSettings().volume == d.volume && loadSettings().version == SETTINGS_VERSION);
    }
  }

  // --- "Reset settings" keeps what is the player's own rather than a choice ---
  {
    storage(KEY, EVERY_177);
    const Settings s = loadSettings();
    const Settings r = resetSettings(s);
    const Settings& d = DEFAULT_SETTINGS();
    assert(r.lang == "fr" && r.lastStart.spawn == 7 && r.lastStart.address && r.lastStart.address->street == "Marktplatz");
    assert(r.mapPack == "mine" && r.region == "nordhessen" && r.fuelKey == "k-1" && r.server == "https://fahrsim.example");
    assert(r.reality.mapillaryToken == "tok" && r.lessonPlaceAsked);
    assert(r.vehicle == d.vehicle && r.volume == d.volume && r.graphics == d.graphics && r.quality == d.quality && !r.reality.panoramax && r.reality.aerial && !r.speech);
  }

  storage();
  std::printf("game_settings_test: ok\n");
  return 0;
}
