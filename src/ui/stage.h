// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The main menu as a stage.  The 3D picture is the menu: the vehicle chosen
 * stands large in the middle of it, where the drive will begin, and the
 * camera slowly circles it (game.ts places the vehicle, `stageFrame` below
 * says from where it is seen).  A swipe – or the arrows at the edges, the
 * arrow keys, a gamepad – brings the next vehicle; a strip of round icons
 * jumps to a kind of vehicle (cars, motorcycles, trucks …); small badges
 * name its licence class; one big round button starts the drive.
 *
 * Everything else waits behind small round buttons and opens in a sheet over
 * the lower part of the screen: where to start (a pin), what to do (free
 * drive, lessons, the test, a demonstration drive …), the vehicle's options
 * (gearbox, engine, trailer), and at the top the language, the map, what is
 * new, the settings and the rest.  Text only where a picture cannot say it:
 * names – and labels on hover, on a long press and for screen readers.
 *
 * The two menus of before stay (carousel.h, and the simple list in
 * hud_menu.cpp); the settings choose, and each menu can switch to another.
 *
 * What the menu decides – the order of the vehicles, what can be done with
 * one, where it starts, how the camera frames it – are pure functions at the
 * top of this file (tests/ui_stage_test.cpp); the page is built below them.
 *
 * Origin: src/ui/stage.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/task.h"
#include "game/address.h"
#include "game/nav.h"
#include "game/settings.h"
#include "game/vehicles.h"
#include "i18n/index.h"
#include "platform/dom.h"
#include "ui/carousel.h"
#include "ui/hud.h"
#include "world/types.h"

namespace fs {

// ===================================================================== what the menu decides

/** What the menu needs to know of a vehicle (a VehicleDef has all of it). */
struct KindInfo {
  VehicleKind kind;
  std::string model;
  std::string licence;
  /** it pulls a trailer */
  bool trailer = false;
};
/** vehicles by their kind, in the order of their table (`Object.entries`) */
using KindTable = std::vector<std::pair<VehicleKind, KindInfo>>;
/** what the menu needs of VEHICLES */
const KindTable& vehicleInfo();

/**
 * The kind of vehicle a vehicle belongs to in the strip of round icons: the
 * cars, the motorcycles, the trucks, the buses …  A model the menu does not
 * know is a kind of its own (a crane is recognised by its name).
 */
std::string groupOf(const VehicleKind& kind, const std::string& model);

/** A kind of vehicle in the strip: its vehicles, in the order of the licence classes. */
struct StageGroup {
  std::string id;
  std::vector<VehicleKind> kinds;
};

/** The kinds of vehicle, in the order their first vehicle comes in `kinds` (the order of the licence classes). */
std::vector<StageGroup> stageGroups(const std::vector<VehicleKind>& kinds = VEHICLE_KINDS, const KindTable& defs = vehicleInfo());

/** The next (step 1) or the previous (−1) vehicle: kind after kind, and round again after the last. */
VehicleKind cycleVehicle(const std::vector<StageGroup>& groups, const VehicleKind& current, int step);

/** The vehicle of the next (or the previous) kind: the one last chosen of that kind, else its first. */
VehicleKind cycleGroup(const std::vector<StageGroup>& groups, const VehicleKind& current, int step, const std::map<std::string, VehicleKind>& last = {});

/**
 * The same vehicle with a trailer, or without its trailer: its licence class
 * with an E (B → BE, C1 → C1E …) or without it.  Null: there is none.
 */
std::optional<VehicleKind> trailerPartner(const VehicleKind& kind, const KindTable& defs = vehicleInfo());

/**
 * A vehicle's name without the licence class in brackets at its end – the
 * badge beside it says that ("Car (class B)" → "Car", "小汽车（B 类）" → "小汽车").
 * A name whose brackets say something else stays as it is.
 */
std::string shortName(const std::string& name, const std::string& licence);

/** Something the player can do with a vehicle. */
struct StageMode {
  /** what the drive is started with: 'free', 'demo', 'tour', 'show' or the id of a lesson */
  std::string id;
  /** a drive of one's own, a lesson, a test: 'drive' | 'lesson' | 'exam' */
  std::string group;
  const LessonDef* lesson = nullptr;
};

/**
 * What can be done with a kind of vehicle – what the other menus offer for
 * it, in their order: the drives of one's own, then its lessons (those on the
 * practice yard first, the test last).
 */
std::vector<StageMode> stageModes(const VehicleKind& kind, bool hasYard, const std::vector<LessonDef>& lessons = LESSONS);

/** The way to play that was chosen, if this vehicle has it – else the first (the free drive). */
std::string pickMode(const std::vector<std::string>& modes, const std::string& wanted);

/** Where the drive would begin, as the menu shows it and starts it. */
struct StagePlace {
  /** what the drive is started with (but for its mode) */
  StartRequest request;
  /** the vehicle standing there, for the picture behind the menu (null: not known yet) */
  std::optional<MenuPreview> preview;
  /** the town (for the aeroplane: the airfield; '' at an address) and the place in it */
  std::string town;
  std::string name;
  /** the aeroplane: the end of the runway (its number) */
  std::optional<std::string> runway;
  /** 'ok' | 'loading' (the places of this vehicle are not read yet) | 'none' (the map has none) */
  std::string state;
};

inline std::string tramKey(const TramStopChoice& t) { return t.name + "|" + t.town; }
inline std::string runwayKey(const RunwayChoice& r) { return r.field + "|" + r.end; }

/**
 * Where the drive of this vehicle would begin: the start point or address of
 * the last drive – the tram at a stop (one in the middle of Kassel at
 * first), the aeroplane at the end of a runway.  As the other menus do it.
 * (`tramStops`, `runways`: null – not read yet.)
 */
StagePlace stagePlace(const VehicleKind& kind, const Settings::LastStart& last, const std::vector<SpawnJson>& spawns, const std::vector<TramStopChoice>* tramStops,
                      const std::vector<RunwayChoice>* runways);

/**
 * A place tapped on the map opened from the menu becomes the start of the next drive: the nearest tram stop for
 * the tram, the nearest end of a runway for the aeroplane, else the place itself as an address (on the road
 * nearest to it).  Changes `last`; false: nothing fits.
 */
bool startFromSpot(const VehicleKind& kind, Settings::LastStart& last, const SpawnJson& spot, const std::vector<TramStopChoice>* tramStops,
                   const std::vector<RunwayChoice>* runways, const std::string& unnamed = "");

/** Where the camera of the stage stands: at `angle` round the vehicle, `dist` from its middle and `rise` above it, aimed `aim` above (− below) its middle. */
// (stage.ts `StageFrame` – named apart: render/theoryprops.h has a StageFrame of its own, and the browser build links both)
struct StageCam {
  double angle = 0;
  double dist = 0;
  double rise = 0;
  double aim = 0;
  /** the vertical field of view (degrees) */
  double fov = 0;
};

/**
 * The camera of the stage at `time` (s): the vehicle large in the middle of
 * the picture – its length across about 60 % of a wide screen (all of an
 * upright one: a wider lens then), its height across 40 % – seen from a
 * little above, a little higher in the picture than its middle (the buttons
 * are below).  It circles slowly, breathes a little in and out, and comes
 * in from further away for a moment after the vehicle changed (`since`).
 */
StageCam frameOf(double time, const std::string& kind, double radius, double aspect, double since = -1e9);

/** The camera of the stage for the vehicle behind the menu now (game.ts menuShow) – it comes in anew whenever that vehicle changes. */
StageCam stageFrame(double time, const std::string& kind, double radius, double aspect);

// ===================================================================== the page

/**
 * Another way to play that a part of the game adds to the menu (the theory
 * lessons …): a tile among the others, started with the big button – like
 * the others (the drive is started with `id` as its mode), or by `run`.
 */
struct StageExtraMode {
  std::string id;
  /** its name */
  std::string label;
  /** a line drawing on a 24 × 24 grid (the inside of the <svg>) – an open book if none */
  std::string icon;
  /** for these vehicles only (all if not given) */
  std::optional<std::vector<VehicleKind>> kinds;
  /** only where the map has a practice yard */
  bool yard = false;
  std::function<void()> run = nullptr;
  /** in the "Learn" group (a sheet of its own; a tap runs it at once) */
  bool learn = false;
};

struct StageContext {
  Lang lang;
  std::function<std::string(const std::string& key, const TrParams& vars)> t;
  Settings* settings = nullptr;
  std::vector<SpawnJson> spawns;
  /** (null: not read yet) */
  const std::vector<TramStopChoice>* tramStops = nullptr;
  const std::vector<RunwayChoice>* runways = nullptr;
  /** whether the map has a practice yard (the lessons on it, the test place) */
  bool hasYard = false;
  /** the list of languages (a <select>) */
  dom::El language;
  /** a field for an address (calls `pick` with the one chosen) */
  std::function<dom::El(std::function<void(const AddressHit& hit)> pick)> address;
  /** the other pages – each comes back to this menu – and the full-screen map */
  struct Pages {
    std::function<void()> settings, news, map, controls, log, about;
  } pages;
  /** where the player is now (the browser asks first): a start place there – or why not (a text); null: not offered */
  struct Here {
    std::optional<AddressHit> hit;
    std::string error;
  };
  std::function<Task<Here>()> here = nullptr;
  /** more ways to play, from other parts of the game */
  std::vector<StageExtraMode> extraModes;
  /** "Version 1.4.0" */
  std::string version;
  std::function<void(const StartRequest& req)> start;
  /** the vehicle behind the menu (null: none – the view from above while its place is not known) */
  std::function<void(const MenuPreview* p)> preview;
  /** draw the menu anew (after a choice) */
  std::function<void()> redraw;
};

/** The main menu as a stage. */
dom::El stageMenu(const StageContext& c);

}  // namespace fs
