// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/settings.ts @ v1.7.7
/**
 * User settings, persisted in localStorage – as the JSON the original writes, under its
 * keys: what a player set in 1.7.7 is read here, and what is stored here is read there.
 *
 * The words a setting chooses between ('assist' | 'manual' | 'automatic' …) are kept as the
 * strings they are stored as.
 */
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "game/lessonplaces.h"
#include "game/tourlog.h"
#include "game/vehicles.h"

// What the player set for the voice of one language: audio/voicepick.h (SpeechPref)
#include "audio/voicepick.h"

namespace fs {

/** 'assist' | 'manual' | 'automatic' */
using Transmission = std::string;
/** a preset of the graphics – or 'custom': the advanced settings chosen one by one ('low' | 'medium' | 'high' | 'ultra' | 'nomercy' | 'custom') */
using Quality = std::string;

/** What the graphics presets set – everything but the resolutions, which are settings of their own. */
struct Graphics {
  /** shadows of buildings and vehicles: none, around the vehicle, far around ('off' | 'near' | 'far') */
  std::string shadows = "off";
  /** how far the map is drawn (m) */
  double viewDistance = 0;
  /** how many of the trees and roadside objects are drawn (0 … 1) */
  double propDensity = 0;
  /** buildings with windows, doors and roof tiles (false: plain walls and roofs) */
  bool facades = false;
  /** trees and signs cast shadows */
  bool propShadows = false;
  /** how far the ground beyond the map is drawn – the hills on the horizon (m; 0: not at all) */
  double farView = 0;
  /** the player's vehicles in finer detail, outside and in the cockpit (render/cardetail.ts, render/fleetdetail.ts) */
  bool vehicleDetail = false;
  /** real water: rivers, lakes and ponds with ripples, the sky mirrored and the sun glinting (render/water.ts) */
  bool water = false;
  /**
   * the weather on the vehicle and the roads (1.7.1: only "No mercy" by default) – rain and snow on the windscreen
   * (render/windscreen.ts), water standing on the roads (render/weather.ts), mud on the body (render/dirt.ts) – and the
   * seasons by the date: without it a season "by the date" is summer
   */
  bool weatherFx = false;

  bool operator==(const Graphics&) const = default;
};

/** `GRAPHICS_PRESETS.medium`, `GRAPHICS_PRESETS[quality]`: what each preset of the graphics sets. */
struct GraphicsPresets {
  // (for weak phones: little to draw, few trees, a short view – the resolutions go down with it: lowResolutions)
  Graphics low = {.shadows = "off", .viewDistance = 500, .propDensity = 0.2, .facades = false, .propShadows = false, .farView = 0, .vehicleDetail = false, .water = false, .weatherFx = false};
  Graphics medium = {.shadows = "near", .viewDistance = 1250, .propDensity = 0.8, .facades = true, .propShadows = false, .farView = 8000, .vehicleDetail = false, .water = false, .weatherFx = false};
  Graphics high = {.shadows = "far", .viewDistance = 2000, .propDensity = 1, .facades = true, .propShadows = true, .farView = 12000, .vehicleDetail = false, .water = false, .weatherFx = false};
  Graphics ultra = {.shadows = "far", .viewDistance = 3000, .propDensity = 1, .facades = true, .propShadows = true, .farView = 20000, .vehicleDetail = true, .water = true, .weatherFx = false};
  // (1.7.1: "very high" and the weather on the vehicle and the roads, and the seasons – planned to grow, see README)
  Graphics nomercy = {.shadows = "far", .viewDistance = 3000, .propDensity = 1, .facades = true, .propShadows = true, .farView = 20000, .vehicleDetail = true, .water = true, .weatherFx = true};

  /** `GRAPHICS_PRESETS[quality]` – null: there is no such preset ('custom', or a word that is none) */
  const Graphics* find(const std::string& quality) const {
    return quality == "low" ? &low : quality == "medium" ? &medium : quality == "high" ? &high : quality == "ultra" ? &ultra : quality == "nomercy" ? &nomercy : nullptr;
  }
};
inline const GraphicsPresets GRAPHICS_PRESETS{};
inline const std::vector<double> VIEW_DISTANCES = {500, 700, 1000, 1250, 1600, 2000, 2500, 3000};

/** A weak device by what the browser tells: four cores or fewer, or 3 GB of memory or less (a phone of the lower end). */
bool lowDevice();

/** the ground beyond the map: off, or so far (m) */
inline const std::vector<double> FAR_VIEWS = {0, 5000, 8000, 12000, 20000, 30000};
/** running on foot (km/h): a jog … a sprint */
inline const std::vector<double> RUN_SPEEDS = {8, 10, 12, 15, 18, 22, 26};
/** the camera a drive starts with: behind the car, or at the driver's eyes ('chase' | 'cockpit') */
using View = std::string;

struct WheelMapping {
  int steerAxis = 0;
  int throttleAxis = 2;
  int brakeAxis = 5;
  int clutchAxis = 1;
  /** pedal axes rest at +1 and go to -1 when pressed (typical for wheels) */
  bool pedalsInverted = true;
  /** degrees of wheel rotation lock to lock as configured in the wheel's driver */
  double wheelRange = 900;
  /** button indices of an H-shifter: [1st … 6th, reverse] */
  std::vector<int> shifter = {12, 13, 14, 15, 16, 17, 18};
};

/**
 * An address a drive begins at: "Am Stadtpark 5, Baunatal", its street, where the house (or the street) is in
 * world coordinates, and whether the house number asked for was found.  (What the search of game/address.h
 * finds – its AddressHit – is this very type.)
 */
struct StartAddress {
  std::string label;
  std::string street;
  double x = 0;
  double z = 0;
  bool exact = false;
};

/** 2 (1.4.0): the map on the vehicle's screen became the default – before, a stored `false` was only the old default */
inline constexpr int SETTINGS_VERSION = 3;

struct Settings {
  /** language code, one of LANGS in src/i18n */
  std::string lang = "en";
  /** a car (class B) or the truck with trailer (class CE) */
  VehicleKind vehicle = "car";
  Transmission transmission = "automatic";
  /**
   * the engine power chosen for each kind of vehicle, in PS (metric horsepower, as German vehicle papers print
   * it); one of its VehicleDef.powers – a kind not listed has the power it comes with
   */
  std::map<VehicleKind, double> powers;
  /**
   * with keys or pedals on the screen the throttle eases off at the current speed limit.
   * Off by default since 1.7.5: no speed is held back unless the player asks for it – one
   * who wants 150 km/h in a town can have it; the limit roundel still shows when it is passed.
   */
  bool limiter = false;
  /** the autopilot lets the player take over for a moment and drives on to its places when let go (else taking over ends it) */
  bool autopilotAssist = true;
  /** arcade driving (game/arcadedrive.h): the automatic gearbox, D and R by the pedals, no handbrake, the engine always running – none of it judged */
  bool arcadeDrive = false;
  /** the road physics off (sim/ground.h WorldGround.even): every road and land, in any weather, grips as dry asphalt */
  bool evenRoads = true;
  /**
   * the ground flattened (sim/ground.h WorldGround.evenTerrain): a piece of
   * land lies flat as a road – no bumps, no camber – so it rides and grips the
   * same.  The landscape's hills stay (one may still drive up them).
   */
  bool evenTerrain = false;
  /** the main-beam assist: at night the main beam goes on and off by itself (game/beamplay.ts) */
  bool beamAssist = false;
  /** the light of day: the game's fair afternoon always, or the sun as it stands at the time (night at night) ('day' | 'clock') */
  std::string daylight = "day";
  /** the player's key of Tankerkönig for today's fuel prices (empty: typical prices; world/fuelprices.ts) */
  std::string fuelKey;
  /** the weather: the real one where the vehicle is (Open-Meteo), none, or one chosen – and the season: by the date, or one chosen */
  /** 'off' | 'real' | 'cloudy' | 'fog' | 'drizzle' | 'rain' | 'snow' | 'storm' */
  std::string weather = "real";
  /** 'real' | 'spring' | 'summer' | 'autumn' | 'winter' */
  std::string season = "real";
  /** flying by hand with help (sim/autopilot.ts FlightAssist): none, a little (softer stick, balanced turns, no stall), or the most (wings level when let go, too) ('off' | 'light' | 'beginner') */
  std::string flightAssist = "beginner";
  /** music, made as it plays (audio/music.ts): none, a piece now and then, or more often – and how loud (0…1) ('off' | 'quiet' | 'on') */
  std::string music = "off";
  double musicVolume = 1;
  /** how fast one runs on foot (km/h), one of RUN_SPEEDS */
  double runSpeed = 12;
  /**
   * how sharply A/D turn for a given speed (settings: a slider):
   * 1 the feel as built, above 1 sharper, below 1 gentler (input/steerfeel.ts).
   * A key-steering feel only – a real wheel keeps its own shape.
   */
  double steerSensitivity = 1;
  /** on foot: through one's own eyes, or seen from behind (the camera key switches) ('eyes' | 'behind') */
  std::string footView = "eyes";
  /** how one walks (game/walkmodes.ts): 'auto' – the classic way on a touch screen, as before with keys –, the classic way (stick or W A S D, look by dragging), as before 1.7.0 (pedals and bar, A D turn), or tap to walk ('auto' | 'classic' | 'tank' | 'tap') */
  std::string walkControls = "auto";
  /** a tap (or click) on a place in the 3D picture offers "Teleport here" for a few seconds (ui/worldtap.ts) */
  bool teleportTap = true;
  /** developer mode: show which map tiles have several versions, and let a tile be chosen from a part of map-assets (ui/mapversions.ts) */
  bool devMap = false;
  /** where the parts of map-assets come from: the site, the repository over raw.githubusercontent (CORS ok), or a stored map pack ('site' | 'repo' | 'pack') */
  std::string devMapFetch = "repo";
  /** the repository that holds map-assets (owner/repo), for devMapFetch 'repo' */
  std::string devMapRepo = "MaurerAnton/driving-simulator";
  /** how much of the parts to keep in memory, in megabytes */
  double devMapRam = 256;
  /** show the loading bar while a part of map-assets is downloaded (netstats + hud setLoadingProgress) */
  bool devMapProgress = true;
  /** the tram the player drives: one unit, two coupled units, or one with a trailer car (sim/rail.ts TramSet: 'single' | 'double' | 'trailer') */
  std::string tramSet = "single";
  /** the main menu: the vehicle large in the 3D picture with icons round it (the default since 1.4.0), rows of cards over the 3D scene, or the simple list of before ('stage' | 'carousel' | 'classic') */
  // (new players get the stage; a list chosen before stays – the rows, 1.3.0's default, become the stage once)
  std::string menuStyle = "stage";
  /** what a tour is to lead along (the map's tour planner) */
  TourPrefs tourPrefs;
  View view = "chase";
  /** how solid the interior is drawn in the cockpit view, 0.2 (see-through) … 1 */
  double cabinOpacity = 1;
  /** where the last drive began: a start point of the map, or an address */
  struct LastStart {
    int spawn = 0;
    std::optional<StartAddress> address;
    /** the tram's: "name|town" */
    std::optional<std::string> tramStop;
    /** the aeroplane's: "field|end" */
    std::optional<std::string> runway;
  };
  LastStart lastStart;
  // (a weak device – few cores or little memory – begins on the low preset: lowDevice)
  Quality quality = "medium";
  /** the graphics as the preset (or the advanced settings) set them */
  Graphics graphics = {.shadows = "near", .viewDistance = 1250, .propDensity = 0.8, .facades = true, .propShadows = false, .farView = 8000, .vehicleDetail = false, .water = false, .weatherFx = false};
  /** size of the interface – menus, HUD – (1 = as designed) */
  double uiScale = 1;
  /** most pixels per CSS pixel the picture is drawn with on a sharp screen (1, 1.5 or 3: as sharp as the screen) */
  double pixelCap = 1.5;
  /** share of those pixels the 3D picture is drawn with (0.5 … 1; less: softer and quicker) */
  double render3d = 1;
  /** off | one mirror per frame | all mirrors every frame ('off' | 'alternate' | 'full') */
  std::string mirrors = "alternate";
  /** show the current speed limit and the next priority sign in the HUD */
  bool hudRules = true;
  /** small timers over the traffic lights near by (a tap switches the light on; not in lessons and tests) */
  bool signalTimers = true;
  /** small labels over the queues in the traffic near by: how long, how many cars, the wait (not in lessons and tests) */
  bool queueLabels = true;
  /** show the engaged gear in the instrument cluster */
  bool hudGear = true;
  /** spoken instructor (browser speech synthesis) */
  bool voice = false;
  double volume = 0.7;
  /** per language: the voice chosen ('' = the best there is), rate and pitch (audio/voicepick.h) – none until the settings have been stored once */
  std::optional<std::map<std::string, SpeechPref>> speech;
  /** traffic density 0 … 1 */
  double traffic = 0.6;
  double drawDistance = 1300;
  WheelMapping wheel;
  bool invertLookY = false;
  /**
   * Reality view: which street-level imagery services may be asked for
   * pictures of the place the car is at.  All off until the player says so –
   * a request tells the service the player's IP address and the place.
   */
  struct Reality {
    bool panoramax = false;
    bool kartaview = false;
    bool mapillary = false;
    std::string mapillaryToken;
    /** aerial photographs of the land survey under the maps – the big one, the corner one, the car's screen (on since 1.4.0) */
    bool aerial = true;
  };
  Reality reality;
  /**
   * Driving controls on the screen (steering bar, pedals, buttons):
   * 'auto' shows them while the screen is being touched and hides them as soon
   * as a keyboard is used; 'on' / 'off' force them.
   */
  std::string touchControls = "auto";
  /** how solid the on-screen controls are drawn, 0.15 … 1 */
  double touchOpacity = 0.55;
  /**
   * steer by tilting the phone (DeviceOrientation; ui/tiltsteer.ts): turned left/right
   * turns the vehicle instead of the on-screen steering bar.  Off unless the player asks
   * for it – a phone without the sensors, or in a pocket, must not steer.
   */
  bool tiltSteer = false;
  /** short reminder of the keys while standing or crawling (keyboard only) */
  bool keyHints = true;
  /** show how many pictures a second are drawn (FPS) in a corner of the screen */
  bool showFps = false;
  /** begin each drive on foot beside the vehicle: walk round it, get in */
  bool startOutside = false;
  /** when the game starts, ask the browser where the player is: on the map, the drive begins there */
  bool startHere = false;
  /** "place precisely": a drive at an address or a place chosen begins at that very place, not in the lane of the nearest road */
  bool placeExact = false;
  /** in the driver's seat: the map on the vehicle's own screen (where it has one) instead of the corner */
  bool mapOnScreen = true;
  /** the map: '' the site's, else the id of a map stored in this browser from files (world/mappack.ts) */
  std::string mapPack;
  /** the region: '' the site's own map, else the id of a region in map-assets (world/regions.ts) – loaded instead of the site's map */
  std::string region;
  /** a tap on the vehicle's screen in the driver's seat: the big map, or the map back in the corner of the HUD (another tap: on the screen again) ('bigmap' | 'minimap') */
  std::string screenTap = "bigmap";
  /** when a zoomed view (on foot, in the driver's seat) goes back by itself once let go (input/camzoom.ts ZoomRelease: 'auto' | 'release' | '3' | '6' | '12' | 'never') */
  std::string zoomRelease = "auto";
  /**
   * The inside rear-view mirror (ui/hudmirror.ts): in the car, moved along the
   * windscreen's top as on a rail (`shift`, m to the right) – or taken off into
   * the HUD, its middle at (fx, fy) of the screen; `size`: larger or smaller.
   */
  struct InsideMirror {
    /** 'car' | 'hud' */
    std::string mode = "car";
    double shift = 0;
    double size = 1;
    double fx = 0.5;
    double fy = 0.09;
  };
  InsideMirror insideMirror;
  /** where the driving lessons begin (game/lessonplaces.h): the start place as before, a random fitting place of the town, or one in the district chosen */
  LessonPlaceMode lessonPlace = "start";
  /** the player was asked once where the lessons are to take place */
  bool lessonPlaceAsked = false;
  /** the district chosen for the lessons (lessonPlace 'area'); null: the home town */
  std::optional<LessonArea> lessonArea;
  /** the lessons are watched: the autopilot drives them, the instructor says what it does (game/lessonwatch.ts) – chosen at each lesson's start */
  bool lessonWatch = false;
  /** the computations that have a WebAssembly twin (src/wasm/kernels.ts – tiles, the quickest way): 'auto' in WebAssembly where the browser has it, 'off' in JavaScript only */
  std::string wasm = "auto";
  /** the Fahrsim Baunatal server the player chose (net/client.ts: accounts, progress, photos, playing together) – '' none: the game needs none */
  std::string server;
  /** what the stored settings were written by (SETTINGS_VERSION): older ones are brought up to date once, in merge */
  int version = SETTINGS_VERSION;

  /**
   * What was stored with the settings and is no setting this version knows (a later version's): its name
   * and its value as JSON.  It is kept and stored again, as the original keeps what it does not know.
   */
  std::vector<std::pair<std::string, std::string>> unknown;
};

/**
 * The settings as the game comes: the struct's own values – with the first of the browser's
 * languages that the simulator speaks, and less to draw on a weak device (lowDevice).
 * Worked out when first asked for (the original: when the page loads).
 */
const Settings& DEFAULT_SETTINGS();

/**
 * The settings put back to their defaults ("Reset settings"), except what is
 * the player's own rather than a choice: the language, where the last drive
 * began, the map stored from files, the keys typed in (Tankerkönig,
 * Mapillary) and the answers already given to the game's questions.
 */
Settings resetSettings(const Settings& s);

/** Are there settings of an earlier visit (has this browser played before)? */
bool playedBefore();

Settings loadSettings();

/** false: this browser keeps nothing (private window, storage switched off) */
bool saveSettings(const Settings& s);

}  // namespace fs
