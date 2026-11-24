// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (begin … stepDrive, the spots – game.ts:2730 … 5155)
#include <algorithm>
#include <cmath>

#include "audio/audio.h"
#include "audio/profiles.h"
#include "core/geom.h"
#include "core/js.h"
#include "game/address.h"
#include "game/arcadedrive.h"
#include "game/ce.h"
#include "game/clock.h"
#include "game/craneplay.h"
#include "game/faultlog.h"
#include "game/flycam.h"
#include "game/game.h"
#include "game/lessonplaces.h"
#include "game/nav.h"
#include "game/routepresets.h"
#include "game/spawnplace.h"
#include "game/track.h"
#include "game/vehicles.h"
#include "i18n/index.h"
#include "input/input.h"
#include "platform/plat.h"
#include "render/actors.h"
#include "render/crane.h"
#include "render/far.h"
#include "render/pedlights.h"
#include "render/props.h"
#include "render/signs.h"
#include "sim/autodriver.h"
#include "sim/collision.h"
#include "sim/follow.h"
#include "sim/ground.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/trailer.h"
#include "sim/vehicle/index.h"
#include "ui/bigmap.h"
#include "ui/geolocate.h"
#include "ui/hud.h"
#include "ui/lessonstart.h"
#include "ui/pathdraw.h"
#include "ui/screentap.h"
#include "ui/tiltsteer.h"
#include "ui/touchstick.h"
#include "ui/worldtap.h"
#include "world/lanes.h"
#include "world/netindex.h"
#include "world/pedlights.h"
#include "world/routegraph.h"
#include "world/types.h"

namespace fs {

void Game::begin(const std::string& modeAsked, int spawnIdx, bool skipIntro, const AddressHit* addressAsked) {
  std::string mode = modeAsked;
  std::optional<AddressHit> address = addressAsked ? std::optional<AddressHit>(*addressAsked) : std::nullopt;
  // (later: the theory lessons, THEORY_MODE – game.ts:2732)
  // a set route from the menu (game/routepresets.ts): a drive of one's own from its first place – the route begins once there
  const RoutePreset* preset = presetOfMode(mode);
  if (preset) {
    mode = "free";
    address = AddressHit{.label = tr("routes." + preset->id + ".name", settings.lang), .street = "", .x = preset->points[0].x, .z = preset->points[0].z, .exact = false};
    wantNet();
  }
  presetPending = preset ? preset->id : "";
  // a lesson on the road: where it takes place, and whether it is driven or watched (ui/lessonstart.ts – game/lessonplaces.ts, lessonwatch.ts)
  const LessonDef* lessonAsked = nullptr;
  for (const LessonDef& l : LESSONS)
    if (l.id == mode) {
      lessonAsked = &l;
      break;
    }
  // (later: canWatch, watching the lesson – game/lessonwatch.ts, game.ts:2746)
  if (lessonAsked && !lessonChosen && placeKindOf(*lessonAsked)) {
    const std::string lang = settings.lang;
    const std::shared_ptr<dom::El> ask = std::make_shared<dom::El>();
    *ask = lessonStartPanel(
        [lang](const std::string& k, const TrParams& v) { return tr(k, lang, v); }, settings, app.world->json.spawns,
        LessonStartOptions{.title = lessonTitle(lessonAsked->id, lang), .place = !address && placeKindOf(*lessonAsked).has_value(), .watch = false},
        [this, ask, mode, spawnIdx, skipIntro, address] {
          ask->remove();
          saveSettings(settings);
          lessonChosen = true;
          begin(mode, spawnIdx, skipIntro, address ? &*address : nullptr);
        },
        [ask] { ask->remove(); });
    ui.append(*ask);
    return;
  }
  // (later: watchLesson = lessonChosen && settings.lessonWatch – game.ts:2757)
  lessonChosen = false;
  // (later: a flight asked for before the airfields were read – game.ts:2759)
  World& world = *app.world;
  audio->start();
  spawnStreet = address ? address->street : "";
  {
    std::optional<SpawnJson> at = address ? startSpot(*address) : std::nullopt;
    const std::vector<SpawnJson>& spawns = world.json.spawns;
    if (at) spawn = std::move(at);
    else if (!spawns.empty()) spawn = spawns[static_cast<size_t>(std::max(0, std::min(static_cast<int>(spawns.size()) - 1, spawnIdx)))];
    else {
      SpawnJson none;
      none.kind = "street";
      spawn = none;
    }
  }
  // (into a vehicle parked on the map, or a car of the traffic taken over: where it stands, as it stands)
  if (parkedAt) {
    SpawnJson here;
    here.name = tr("mapPlace", settings.lang);
    here.x = parkedAt->x;
    here.z = parkedAt->z;
    here.yaw = parkedAt->yaw;
    here.kind = "street";
    spawn = here;
    parkedAt.reset();
  }
  // (later: on foot from the stop Baunatal Stadtmitte; the aeroplane at the end of its runway; the tram at its stop; the test place – game.ts:2775)
  lesson = nullptr;
  for (const LessonDef& l : LESSONS)
    if (l.id == mode) {
      lesson = &l;
      break;
    }
  demo = nullptr;
  routeWatch = nullptr;
  // (later: wrecked, explosion, leaveWalk, footAfterJump, clearPreview, menuPreview, recorder.clear, riding, busRide, busService, busNav,
  //  railActors.hidden, passengerView, doorT, opens, fadeEl, planeDemoPending – game.ts:2818)
  // ("demo" and "tour" are free drives that begin with the map open, for planning – the tram's drives by itself at once)
  planDemo = mode == "demo" || mode == "tour";
  hud->canTeleport = !lesson;
  hud->canFinish = !lesson;
  keepClock = false;
  path->breakLine();
  // the car, or the truck with its trailer (class CE)
  // (a lesson for some vehicles only – the yard tasks of class CE, the bus line – with one of them: the one chosen, if it is one)
  {
    const std::optional<std::vector<VehicleKind>>* allowed = lesson ? &lesson->vehicles : nullptr;
    const bool has = allowed && *allowed;
    const VehicleKind kind = lesson && lesson->vehicle ? *lesson->vehicle
                             : has && std::find((*allowed)->begin(), (*allowed)->end(), settings.vehicle) == (*allowed)->end() ? (**allowed)[0]
                                                                                                                            : settings.vehicle;
    const VehicleDef* d = VEHICLES.find(kind);
    def = d ? d : &VEHICLES.at("car");
  }
  // (a tractor may not go on the motorway: its lesson is driven with the car)
  if (def->noMotorway && lesson && lesson->id == "motorway") def = &VEHICLES.at("car");
  // (later: the other vehicles' models – render/fleet.ts – and the trailers behind them – sim/semitrailer.ts makeTrailer, render/semitruck.ts:
  //  until they are there, a vehicle that is not drawn as the car alone is driven as the car)
  if ((def->model != "car" && !def->crane) || def->trailer) def = &VEHICLES.at("car");
  // (later: the tram: tram, tramAt, tramWait – game.ts:2853)
  // (ways for the truck with its trailer avoid turns that double back within a few metres)
  lanes->longVehicle = def->long_;
  lanes->noMotorway = def->noMotorway;
  routeFor.noMotorway = def->noMotorway;
  // (later: services.begin, the fuel in the tank – game/serviceworld.ts, game.ts:2862)
  const VehicleSpec spec = def->spec(arcadeTransmission(settings.arcadeDrive, settings.transmission), powerOf(*def, settings.powers));
  arcadeDrive->reset();
  vehicle = std::make_unique<Vehicle>(spec, *ground);
  // (later: wading.reset – game.ts:2865)
  // the sound of this kind of vehicle (its engine's revs – the tram and the aeroplane have their own)
  audio->setVehicle(soundKindOf(def->kind), spec);
  ensureModel(*def);
  // (later: trailer = makeTrailer(def.trailer, ground), linkTowed – game.ts:2869)
  trailer = nullptr;
  hud->gearbox(spec.transmission == "automatic" && ((def->model != "car" && def->model != "motorcycle") || settings.arcadeDrive) ? std::optional<Transmission>("automatic") : std::nullopt);
  // (later: endTasks – game.ts:2872)
  // the camera stands further back for 16 m of vehicle
  input->c.lookZoom = def->zoom;
  input->zoomMax = def->zoomMax;
  collider->setShape(def->circles, def->radius);
  rules->setVehicle(def->caps ? std::optional<RuleMonitor::Caps>(RuleMonitor::Caps{def->caps->rural, def->caps->motorway}) : std::nullopt, def->capFault, def->heavy, def->noMotorway);
  // (later: taskFailed, lessonWatch: the car drives the lesson by itself – game.ts:2878)
  // a lesson in a real place (game/lessonplaces.ts): from the district chosen – the place itself is looked for once the roads round it are there
  placeGoal = nullptr;
  placeSearch = lesson && !address && spawn ? lessonStart(*lesson, settings.lessonPlace, settings.lessonArea ? &*settings.lessonArea : nullptr, world.json.spawns, *spawn) : std::nullopt;
  if (placeSearch) {
    spawn = placeSearch->spawn;
    if (placeSearch->kind == "area") placeSearch = std::nullopt;
  }
  roadSpawn = spawn;
  // (later: tasksLeft, taskCount, nextTask, truckSpot; drill, drillBegin – game.ts:2893)
  // a crane: its free drive is the work on the construction site (game/craneplay.ts) – from the depot, the tower crane on its base
  if (crane) crane->dispose();
  crane = nullptr;
  if (def->crane && !lesson && (mode == "free" || mode == "crane" || *def->crane == "tower")) {
    crane = makeCraneWork(*def->crane);
    const std::optional<CranePlace> at = craneStart(*def->crane, yardFrame ? &*yardFrame : nullptr);
    if (at) spawn = SpawnJson{.name = tr("craneSite", settings.lang), .x = at->x, .z = at->z, .yaw = at->yaw, .kind = "street"};
    // (later: showOn – game.ts:2907)
    rules->yard = true;
    hud->canTeleport = false;
  }
  // (later: forklift – game.ts:2912)
  input->setSteering(spec.wheelbase, spec.maxWheelAngle, SteerSpec(spec));
  input->setAutomatic(spec.transmission == "automatic");
  Controls& c = input->c;
  // a free drive starts ready to go: in gear (D1), no handbrake, held still
  // until the driver gives gas or the brake.  Lessons and exams keep the full
  // programme – the handbrake on, released by the driver (the lesson says so).
  const bool freeGo = !lesson && mode == "free";
  spawnHold = freeGo;
  c.handbrake = freeGo ? 0 : 1;
  if (freeGo && spec.transmission == "automatic") c.gear = 1;   // (D1: the selector in drive)
  c.ignition = true;
  c.indicator = 0;
  c.hazard = false;
  // the basics lesson starts with a cold car; everything else is ready to go
  const bool cold = lesson && lesson->kind == "basics";
  // (on a motorcycle there is no belt: the helmet is on)
  c.seatbelt = !cold || def->model == "motorcycle";
  vehicle->resetDrivetrain(!cold);
  rules->reset();
  hud->clearNotices();
  rules->strict = lesson && lesson->kind == "exam";
  traffic->clear();
  // (later: rail.clear – game.ts:2948)
  nav->clear();
  targetIdx = 0;
  basicsStep = 0;
  visited.clear();
  failT = -1;
  homeEdge = -1;
  state = "placing";
  showModel(false);
  hud->showLoading(tr("loadingTiles", settings.lang));
  const std::string lang = settings.lang;
  const std::optional<VehicleDef::Caps>& caps = def->caps;
  const std::string note = lesson && lesson->kind == "route" && (caps || def->noMotorway)
                               ? (caps ? tr("lesson.capNote", lang, {{"vehicle", tr(def->name, lang)}, {"rural", caps->rural}, {"motorway", caps->motorway}})
                                       : tr("lesson.slowNote", lang, {{"vehicle", tr(def->name, lang)}, {"max", def->governed}})) +
                                     " "
                               : "";
  pendingIntro = lesson && !skipIntro ? std::optional<PendingIntro>(PendingIntro{lessonTitle(lesson->id, lang), note + lessonIntro(lesson->id, lang)}) : std::nullopt;
}

/** Called every frame while waiting for the terrain under the start point. */
void Game::tryPlace() {
  // (later: the tram at the stop nearest to the place chosen; the aeroplane at the end of the runway – game.ts:2968)
  SpawnJson sp = *spawn;
  // how far the loading is: the roads around the place, then the tiles (the bar of the loading screen)
  hud->setLoadingProgress(app.map->pieces ? 0.35 * app.map->progress(sp.x, sp.z, 1500) + 0.65 * app.tiles->progress : app.tiles->progress);
  // (the roads around the place: without them there is no lane to stand in, and nothing to drive on)
  if (!app.map->ready(sp.x, sp.z, 1500)) return;
  // a lesson in a real place: now that the roads round the district are there, the place itself (game/lessonplaces.ts)
  if (placeSearch) {
    const LessonStart ps = *placeSearch;
    const double radius = searchRadius(ps.kind);
    if (radius > 1500 && !app.map->ready(sp.x, sp.z, radius + 200)) return;
    placeSearch = std::nullopt;
    const std::optional<LessonSpot> at = findLessonPlace(*app.world, *lanes, ps.kind, sp.x, sp.z, PlaceOptions{.usable = usableLane()});
    if (at) {
      placeGoal = at->goal;
      SpawnJson found;
      found.name = at->name;
      found.x = at->x;
      found.z = at->z;
      found.yaw = at->yaw;
      found.kind = "street";
      sp = found;
      spawn = roadSpawn = sp;
    } else {
      // (nothing fits there: where the player chose to start)
      sp = ps.fallback;
      spawn = roadSpawn = sp;
      hud->message(tr("lessonPlace.none", settings.lang));
    }
    if (!app.map->ready(sp.x, sp.z, 1500)) return;
  }
  if (sp.rough) {
    // chosen on the overview of the map: now that the roads are there, into a lane of the road meant
    // (or at an address before any roads were known: then from the house to its street)
    std::optional<SpawnJson> exact = findRoadSpot(*app.world, *lanes, sp.x, sp.z, spawnStreet, 230, usableLane());
    if (exact) {
      exact->name = sp.name;
      sp = *exact;
    } else sp.rough = false;
    spawn = sp;
  }
  const double ty = app.tiles->heightAt(sp.x, sp.z);
  if (std::isnan(ty) || !app.tiles->settled()) return;
  // "place precisely": at the very place chosen, facing along the road nearest – or the nearest free place round it
  if (isExactSpot(sp)) {
    const Place fit = fitExact(Place{sp.x, sp.z, sp.yaw}, def->outline);
    sp.x = fit.x;
    sp.z = fit.z;
    sp.yaw = fit.yaw;
    sp.rough = false;
    spawn = sp;
  }
  double y = app.world->surfaceAt(sp.x, sp.z, ty + 3, hit) ? hit.y : ty;
  // (later: the practice yard is laid out, task.laid – game.ts:3037)
  Place place{sp.x, sp.z, sp.yaw};
  // (never into a wall or onto a post: then the nearest free place in a lane instead)
  // (later: … or inside a building: inBuilding with spawnCars.footprints – game/spawncar.ts, game.ts:3040)
  if (collider->blocked(sp.x, y, sp.z, sp.yaw, 0.05)) {
    const std::optional<Spot> freeAt = freeSpot(sp.x, sp.z, sp.yaw, 0);
    if (freeAt) {
      place = Place{freeAt->x, freeAt->z, freeAt->yaw};
      y = freeAt->y;
    }
  }
  vehicle->teleport(place.x, y + 0.02, place.z, place.yaw);
  if (trailer) trailer->coupleBehind(*vehicle);
  lastGood = GoodSpot{{place.x, y, place.z, place.yaw}, 0};
  // (later: walkPending, intoParked, footPending: on foot first; the player's tram at the stop's mark – game.ts:3051)
  camSnap = true;
  showModel(true);
  if (!keepClock) {
    startOdo = vehicle->odometer;
    startTime = time;
  }
  keepClock = false;
  if (pendingIntro) {
    state = "intro";
    const PendingIntro intro = *pendingIntro;
    pendingIntro = std::nullopt;
    hud->showIntro(intro.title, intro.text, [this] { resume(); });
  }
  // (later: services.startScene – game.ts:3089)
  else resume();
  if (planDemo && state == "drive") {
    planDemo = false;
    openMap();
    bigMap->plan();
  }
  if (afterPlace && state == "drive") {
    const std::function<void()> f = afterPlace;
    afterPlace = nullptr;
    f();
  }
}

void Game::resume() {
  hud->hideOverlay();
  hud->setHudVisible(true);
  input->enabled = true;
  audio->start();
  state = "drive";
  app.canvas.focus();
  // (later: walkPending, footPending, footAfterJump: the drive begins on foot – game.ts:3110)
}

/** Where the world loads round: the vehicle – or somebody who walked away from it. */
std::array<double, 2> Game::focus() {
  // (later: watcher.focus – game.ts:3133)
  // (the developer's view: the world loads round the camera)
  if (cameraMode == "dev") return {free.pos.x, free.pos.z};
  // (the flying camera: the world loads round it – game/flycam.ts)
  if (flyFree() && fly->ready) return {fly->x, fly->z};
  // (later: the walker who is well away from the vehicle – game.ts:3138)
  return {mpos[0], mpos[2]};
}

void Game::pause() {
  if (state != "drive") return;
  state = "pause";
  input->enabled = false;
  audio->suspend();
  path->save();
  // (later: offerReplay – game.ts:3491)
  hud->showPause();
}

/** The world's moment was set anew (the arcade mode): what keeps to the clock catches up. */
void Game::clockJumped() {
  // (later: rail.resync, resyncBuses(traffic) – game/timedtransit.ts, game.ts:3506)
  sunPlacedAt = -1;
  updateSun();
}

/**
 * The sun: where it stands at the world's moment when the arcade mode set
 * another one (or the settings ask for the real daylight) – otherwise the
 * fair afternoon the game has always had.
 */
void Game::updateSun() {
  const bool own = !gameClock.real() || settings.daylight == "clock";
  const double ms = gameClock.ms();
  if (own) {
    if (sunPlacedAt >= 0 && std::abs(ms - sunPlacedAt) < 20000) return;
    sunPlacedAt = ms;
    const SunPlace sun = sunAt(ms);
    // (below the horizon the night is not black: the sky keeps some light, as on a clear night in town)
    app.sky.setSun(std::max(sun.elevation, -6.0), sun.azimuth);
    // (later: beams.sunElevation – game/beamplay.ts, game.ts:3532)
  } else if (sunPlacedAt != -2) {
    sunPlacedAt = -2;
    app.sky.setSun(52, 215);
    // (later: beams.sunElevation = 52 – game.ts:3536)
  }
}

void Game::finish() {
  path->save();
  if (demo && demo->tour) endDemo("ended");
  demo = nullptr;
  const LessonDef* l = lesson;
  const bool exam = l && l->kind == "exam";
  // (later: taskFailed, taskCount, drill: the tasks on the yard – game.ts:3586)
  const bool failed = rules->failed() || rules->points() >= 10;
  const bool judged = false;
  state = "result";
  input->enabled = false;
  audio->suspend();
  hud->setHudVisible(false);
  lastResult = std::make_unique<ResultSummary>();
  // (later: forkWork as the title; forklift, drill: their resultNode – game.ts:3594)
  lastResult->title = l ? lessonTitle(l->id, settings.lang) : tr(crane ? "craneWork" : "free", settings.lang);
  if (crane)
    if (std::optional<dom::El> extra = crane->resultNode()) lastResult->extra = *extra;
  lastResult->exam = exam || judged;
  lastResult->passed = !failed;
  lastResult->log = rules->log;
  lastResult->points = rules->points();
  lastResult->distanceKm = (vehicle->odometer - startOdo) / 1000;
  lastResult->minutes = (time - startTime) / 60;
  // (later: noteLessonEnd, the lesson log – game/exam/learnstore.ts, game.ts:3604; offerReplay)
  hud->showResult(*lastResult);
}

void Game::onAction(const std::string& a) {
  // F2: the whole display hidden or shown again – in any state
  if (a == "hud") {
    if (hud->view) hud->view->toggleHud();
    return;
  }
  // (later: watching a tram or bus: nothing is done to the player's own vehicle meanwhile – game.ts:3837)
  // (later: 'reality' – game.ts:3838)
  if (a == "pause") {
    if (state == "drive") pause();
    else if (state == "pause") resume();
    else if (state == "map") closeMap();
    // (later: the reality view, the replay, the theory – game.ts:3844)
  }
  // (later: 'open', 'door': on foot, the flaps, the bus's and the tram's doors, the forklift – game.ts:3847;
  //  the tower crane's: its operator climbs down and up again, on foot – towerDoor, game.ts:3867)
  else if (a == "door" && state == "drive" && crane && crane->type != "tower") {
    // the crane: from driving to working it, and back  (later: not while on foot – game.ts:3870)
    craneToggle();
  }
  // (later: 'door': the test place, getting out – game.ts:3873; 'view' in the replay, on foot, on the forklift; 'panto' – game.ts:3893)
  else if (a == "view" && state == "drive") {
    // the driver's seat → behind the car → the free camera → …
    cameraMode = cameraMode == "cockpit" ? "chase" : cameraMode == "chase" ? "free" : "cockpit";
    input->resetLook(cameraMode == "chase", cameraMode == "free");
    if (cameraMode == "free") {
      // it starts where the camera is, and says how it is moved
      fc.ready = false;
      const std::string& lang = settings.lang;
      hud->message(tr("viewFree", lang), tr(inputMode == "touch" ? "freeCamTouch" : "freeCamKeys", lang));
    } else {
      // the next drive begins with the camera the player chose
      settings.view = cameraMode;
      saveSettings(settings);
    }
  } else if (a == "reset" && state == "drive") {
    // (later: in the aeroplane the key is its autopilot – game.ts:3927)
    resetToRoad();
  } else if (a == "turn") {
    // F3, or "Turn around" in the ⋯ sheet: into the lane of the other direction, or on the spot
    turnAround();
  }
  // (later: 'wiper', 'service' – game.ts:3933)
  else if (a == "help") {
    if (state == "drive") {
      state = "pause";
      input->enabled = false;
      hud->showControls([this] { resume(); });
    }
  } else if (a == "map") {
    if (state == "map") closeMap();
    else openMap();
  }
}

/**
 * Back onto the road: into the middle of the lane the car was last driving
 * in, where it touches nothing.  Asked for again from there (something is
 * still in the way): a stretch further on each time.
 */
void Game::resetToRoad() {
  Vehicle& v = *vehicle;
  const GoodSpot g = lastGood;
  v.getModelPosition(mpos.data());
  const bool again = time - resetAt.t < 10 && std::hypot(mpos[0] - resetAt.x, mpos[2] - resetAt.z) < 6;
  resetHop = again ? resetHop + 1 : 0;
  const std::optional<Spot> spot = freeSpot(g.x, g.z, g.yaw, resetHop * 15);
  putOnRoad(spot ? *spot : static_cast<const Spot&>(g));
  // (the parking brake on – unless the car drives by itself: then it simply drives on from there)
  // (later: … nor while the lesson is watched, lessonWatch – game.ts:3972)
  if (!(demo && demo->active)) input->c.handbrake = 1;
}

void Game::putOnRoad(const Spot& spot) {
  Vehicle& v = *vehicle;
  v.teleport(spot.x, spot.y + 0.05, spot.z, spot.yaw);
  if (trailer && trailer->coupled) trailer->coupleBehind(v);
  // (the instructor – and with him the driver of a demonstration drive – forgets where the car was:
  // else the driver would act once more on the lane it has just been taken from)
  rules->placed();
  if (demo && demo->driver) demo->driver->reset();
  // (later: lessonWatch.driver.reset – game.ts:3988)
  lastGood = GoodSpot{{spot.x, spot.y, spot.z, spot.yaw}, time};
  resetAt = ResetAt{spot.x, spot.z, time};
}

/**
 * A place in the middle of a lane where the vehicle touches nothing: the
 * lane nearest to (x, z) that goes the way `yaw` points, at least `ahead`
 * metres on from there – following the route (or the road) over the next
 * junctions if need be.  Null when there is no lane near, or no room.
 */
std::optional<Game::Spot> Game::freeSpot(double x, double z, double yaw, double ahead) {
  World& world = *app.world;
  const double fx = -std::sin(yaw);
  const double fz = -std::cos(yaw);
  Lane* lane = nullptr;
  double at = 0;
  double best = JS_INF;
  world.edgesInBox(x - 15, z - 15, x + 15, z + 15, [&](Edge& e) {
    const LaneGraph::EdgeLanes* el = lanes->edgeLanes[e.id];
    projectOnPolyline(e.pl, x, z, proj);
    for (const std::vector<Lane*>* side : {&el->fwd, &el->back})
      for (Lane* l : *side) {
        if (l->len < 1) continue;
        const double d = std::max(0.0, std::min(l->len, l->dir > 0 ? proj.s - l->s0 : l->s0 - proj.s));
        lanePoint(*l, d, smp);
        // near, going our way, and a road rather than somebody's yard
        const double score = std::hypot(smp.x - x, smp.z - z) + (1 - (smp.tx * fx + smp.tz * fz)) * 6 + (LaneGraph::drivable(e, false) ? 0 : 8);
        if (score < best) {
          best = score;
          lane = l;
          at = d;
        }
      }
  });
  if (!lane || best > 30) return std::nullopt;
  static const std::vector<Connector*> none;
  const std::vector<Connector*>& route = nav->route ? *nav->route : none;
  Lane* cur = lane;
  double d = at + ahead;
  for (double walked = 0, hops = 0; walked < 200 && hops < 14;) {
    if (d > cur->len - 1 && (cur->len > 2.5 || d > cur->len)) {
      // on to the next lane: by the route, else straight on, else by whatever leads on
      Lane* from = cur;
      Connector* c = nullptr;
      for (Connector* r : route)
        if (r->from == from) {
          c = r;
          break;
        }
      if (!c)
        for (Connector* o : from->out)
          if (o->turn == TURN_STRAIGHT) {
            c = o;
            break;
          }
      if (!c)
        for (Connector* o : from->out)
          if (o->turn != TURN_UTURN) {
            c = o;
            break;
          }
      if (!c && !from->out.empty()) c = from->out[0];
      if (!c) return std::nullopt;
      d = std::max(1.5, d - from->len - c->len);
      cur = c->to;
      hops++;
      continue;
    }
    lanePoint(*cur, d, smp);
    const double y = world.surfaceAt(smp.x, smp.z, smp.y + 1.5, hit) ? hit.y : smp.y;
    const double heading = std::atan2(-smp.tx, -smp.tz);
    bool isFree = !collider->blocked(smp.x, y, smp.z, heading, 0.2);
    if (isFree)
      for (const TrafficCar* car : traffic->cars)
        if (std::hypot(car->x - smp.x, car->z - smp.z) < 7) isFree = false;
    if (isFree) return Spot{smp.x, y, smp.z, heading};
    d += 2;
    walked += 2;
  }
  return std::nullopt;
}

// ------------------------------------------------------------- simulation
/** The vehicle driven gets the automatic gearbox, where it has another (for the autopilot, for arcade driving). */
void Game::toAutomatic() {
  if (vehicle->spec.transmission == "automatic") return;
  Vehicle& old = *vehicle;
  old.getModelPosition(mpos.data());
  const VehicleSpec spec = def->spec("automatic", powerOf(*def, settings.powers));
  std::unique_ptr<Vehicle> v = std::make_unique<Vehicle>(spec, *ground);
  v->teleport(mpos[0], mpos[1] + 0.02, mpos[2], old.yaw);
  v->odometer = old.odometer;
  vehicle = std::move(v);
  input->setSteering(spec.wheelbase, spec.maxWheelAngle, SteerSpec(spec));
  input->setAutomatic(true);
  hud->gearbox("automatic");
}

/** Arcade driving for the road vehicle driven now (not the tram, the aeroplane, the forklift, a crane at work, nor on foot or watching). */
bool Game::arcadeNow() {
  // (later: … !watcher, !tram, !aircraft, !forklift, not on foot, !wrecked – game.ts:4068)
  return settings.arcadeDrive && !(crane && crane->operating);
}

void Game::stepDrive(double dt) {
  // (arcade driving switched on in the middle of a drive: the automatic gearbox from here on)
  if (arcadeNow()) toAutomatic();
  Vehicle& v = *vehicle;
  Controls& c = input->update(dt, v.speed, v.steerTorque);
  if (debugLook) {
    c.lookYaw = (*debugLook)[0];
    c.lookPitch = (*debugLook)[1];
  }
  // (later: stepWatch, stepTram, stepFoot, stepFlight – game.ts:4080)
  if (crane && crane->operating) {
    stepCrane(dt);
    return;
  }
  // (later: stepForklift – game.ts:4102)
  DriverInput& d = driver;
  d.steer = c.steer;
  d.throttle = c.throttle;
  d.brake = c.brake;
  d.clutch = c.clutch;
  d.handbrake = c.handbrake;
  d.gear = c.gear;
  d.ignition = c.ignition;
  d.starter = c.starter;
  // the free-drive spawn: in gear, no handbrake, held still until the driver
  // gives gas or the brake – the brake holds it (the converter would creep)
  if (spawnHold) {
    if (c.throttle > 0.05 || c.brake > 0.05) spawnHold = false;
    else {
      d.brake = 1;
      d.throttle = 0;
    }
  }
  // a look back over the left shoulder: that is what is asked for before the door is opened
  if (c.lookYaw > 0.9) lookedBackAt = time;
  // (later: stepDoor, stepOpen, the walker beside the vehicle, opens.idle – game.ts:4126)
  // Speed limiter: a key or a pedal on the screen is all or nothing, which makes it hard to hold
  // 30 or 50 km/h.  With the limiter the throttle eases off just below the current limit – the
  // driver still has to brake when the limit drops.
  const double lim = rules ? rules->context.limit : -1;
  if (settings.limiter && lim > 0 && c.gear != -1 && (c.device == "keyboard" || c.device == "touch")) {
    d.throttle = std::min(d.throttle, std::max(0.0, (lim - 0.5 - v.speed * 3.6) / 4));
  }
  // a truck is governed at 90 km/h, a bus at 100, a tractor runs 40 by construction
  const double gov = def->governed;
  if (gov > 0 && v.speed * 3.6 > gov - 5) d.throttle = std::min(d.throttle, std::max(0.0, (gov - 0.5 - v.speed * 3.6) / 4));
  if (demo && demo->active && demo->driver) {
    // Whoever touches a pedal or the steering takes the car over (held for a moment: a wheel
    // that is not quite centred, or a thumb resting on the screen, is no decision).
    const bool hands = c.throttle > 0.2 || c.brake > 0.2 || std::abs(c.steer) > 0.35 || c.handbrake > 0.5;
    demo->takeT = hands ? demo->takeT + dt : 0;
    if (demo->held) {
      // The assisted autopilot: the player drives for a while – when the hands have been off the
      // controls for a moment, on a road of the network, it drives on to its places from there.
      const bool idle = c.throttle < 0.05 && c.brake < 0.05 && std::abs(c.steer) < 0.1 && c.handbrake < 0.5;
      demo->freeT = idle ? demo->freeT + dt : 0;
      c.gear = 1;
      d.gear = 1;
      if (demo->freeT > 2.5 && currentLane()) {
        demo->held = false;
        demo->takeT = 0;
        demo->stuck = 0;
        // (the way from where the player left it)
        nav->clear();
        hud->message(tr("demoResumed", settings.lang));
      }
    } else if (demo->takeT > 0.18 && settings.autopilotAssist) {
      demo->held = true;
      demo->freeT = 0;
      hud->message(tr("demoHeld", settings.lang));
    } else if (demo->takeT > 0.18) endDemo("taken");
    else {
      v.getModelPosition(mpos.data());
      static const std::vector<Connector*> none;
      const AutoCommand& cmd = demo->driver->step(std::max(dt, 1e-3), v, mpos.data(), nav->route ? *nav->route : none);
      // (until the way is known the car stands: no lane under it yet, or no route found yet)
      const bool ready = nav->busy();
      d.steer = ready ? cmd.steer : 0;
      d.throttle = ready ? cmd.throttle : 0;
      d.brake = ready ? cmd.brake : 0.5;
      d.clutch = 0;
      d.handbrake = 0;
      d.gear = 1;
      d.ignition = true;
      d.starter = !v.engineRunning;
      c.gear = 1;
      c.indicator = cmd.indicator;
      demo->look = cmd.look;
      demo->remark = cmd.remark;
      if (cmd.arrived) {
        d.brake = 1;
        endDemo("arrived");
      }
    }
  }
  // (later: lessonWatch, watching the lesson: the autopilot drives it – game/lessonwatch.ts, game.ts:4199)
  if (autoDrive > 0) {
    // test hook: drive off by itself
    d.ignition = true;
    d.handbrake = 0;
    c.handbrake = 0;
    d.gear = 1;
    c.gear = 1;
    d.throttle = autoDrive;
  }
  // arcade driving: the pedals choose D and R, the vehicle holds at a standstill, the engine runs (game/arcadedrive.ts) –
  // not while the autopilot drives, nor for a passenger
  // (later: … && !lessonWatch && !opens.passenger – game.ts:4224)
  const bool arcade = arcadeNow() && !(demo && demo->active && !demo->held) && autoDrive <= 0;
  if (arcade) {
    arcadeDrive->step(c.throttle, c.brake, v.speed, dt, d, d.throttle, v.engineRunning);
    c.gear = d.gear;
    c.handbrake = d.handbrake;
    c.ignition = true;
  }

  // (later: services.drive; wading.step: through water, the brakes wet (brakeFactor) – game.ts:4232)
  acc = std::min(acc + dt, 0.1);
  bool stalled = false;
  bool grind = false;
  // (later: a vehicle that went up in flames is not there any more, wrecked – game.ts:4246)
  if (flyHold()) acc = 0;
  while (acc >= PHYS_DT) {
    v.step(PHYS_DT, d);
    if (trailer) trailer->step(PHYS_DT, v, d);
    // (later: the semi-trailer swung round against the tractor's cab, semiJackknife – game.ts:4253)
    acc -= PHYS_DT;
    if (v.stalledEvent) stalled = true;
    if (v.gearGrind) grind = true;
    const double hitSpeed = collider->resolve(v);
    if (hitSpeed > impact) impact = hitSpeed;
    // (later: collider.lastBuilding, into the wall of a building at 70 km/h: explode – game.ts:4259)
  }
  path->add(v.pos[0], v.pos[2]);
  // a refused gear change: crunch, then the lever springs back
  if (grind) {
    if (grindT <= 0) audio->click("grind");
    grindT += dt;
    if (grindT > 0.35) {
      c.gear = v.gear;
      grindT = 0;
    }
    tipText = tr("pressClutch", settings.lang);
    tipUntil = time + 2.5;
  } else grindT = 0;

  v.getModelPosition(mpos.data());
  // (the traffic wants the middle of the vehicle's length; a truck is measured from the middle of its wheelbase)
  const double mid = (def->outline.rear - def->outline.front) / 2;
  PlayerState car;
  car.x = mpos[0] + std::sin(v.yaw) * mid;
  car.y = mpos[1];
  car.z = mpos[2] + std::cos(v.yaw) * mid;
  car.yaw = v.yaw;
  car.speed = v.speed;
  car.length = v.spec.length.value_or(4.28);
  car.width = v.spec.width.value_or(1.79);
  // (later: somebody who walked away from the vehicle: the traffic comes and goes round them – game.ts:4286)
  PlayerState& player = car;
  // (the lane the vehicle was on in the last step, and where its middle was along it: the cyclists are sent along by it)
  player.lane = rules->context.onRoad ? currentLane() : nullptr;
  player.s = laneFront() - player.length / 2;
  signals->update(dt);
  // (later: trams and trains: rail.update, roadEvents.railBlocks, their cars among the boxes – game.ts:4293)
  std::vector<PlayerState>& boxes = railBoxes;
  boxes.clear();
  if (trailer) {
    // the trailer is in the way of the traffic like the truck itself
    // (later: … ?? TRAILER_OUTLINE, the trailer of the class CE truck – game/ce.ts, game.ts:4306)
    const Trailer& t = *trailer;
    if (const std::optional<TrailerOutline> o = trailerOf(*def)) {
      const double off = (o->front - o->rear) / 2;
      PlayerState b;
      b.x = t.x - std::sin(t.yaw) * off;
      b.y = t.y;
      b.z = t.z - std::cos(t.yaw) * off;
      b.yaw = t.yaw;
      b.speed = t.coupled ? t.speed : 0;
      b.length = o->front + o->rear;
      b.width = o->hw * 2;
      b.own = t.coupled;
      b.parked = !t.coupled;
      boxes.push_back(b);
    }
  }
  // (later: the walker beside the vehicle, the vehicle left standing, the other players' vehicles – game.ts:4310)
  traffic->obstacles.clear();
  for (PlayerState& b : boxes) traffic->obstacles.push_back(&b);
  // (the horn: the people on foot near it start)
  player.horn = c.horn;
  traffic->update(dt, player);
  // (later: run into a tram (or hit by one): rail.touching – game.ts:4319)
  for (const TrafficEvent& ev : traffic->events) {
    if (ev.type == "collision") audio->click("crash");
    else if (ev.type == "contact") {
      // out of the other vehicle by the shortest way, and no more speed into it (a little bounce): it is solid
      v.translate(ev.nx * (ev.depth + 0.02), 0, ev.nz * (ev.depth + 0.02));
      const double vn = v.vel[0] * ev.nx + v.vel[2] * ev.nz;
      if (vn < 0) v.applyImpulse(v.pos[0], v.pos[1], v.pos[2], -ev.nx * vn * v.spec.mass * 1.2, 0, -ev.nz * vn * v.spec.mass * 1.2);
    } else if (ev.type == "cyclistEndangered" && ev.hit) audio->click("bump");
  }
  // on its side or roof: that is the end of this drive – count it as a crash and start again on the road
  if (v.upright < 0.35) {
    rolledT += dt;
    if (rolledT > 2.2 && std::abs(v.speed) < 5) {
      rolledT = 0;
      impact = std::max(impact, 12.0);
      resetToRoad();
      tipText = tr("rolledOver", settings.lang);
      tipUntil = time + 6;
    }
  } else rolledT = 0;
  const double impactNow = impact;
  if (impactNow > 1.2) audio->click(impactNow > 4 ? "crash" : "bump");
  impact = 0;

  PlayerSnapshot snap;
  snap.x = player.x;
  snap.y = player.y;
  snap.z = player.z;
  snap.yaw = v.yaw;
  snap.speed = v.speed;
  snap.indicator = c.indicator;
  snap.hazard = c.hazard;
  // (on a demonstration drive it is the driver who looks over the shoulder)
  snap.lookYaw = demo && demo->active ? demo->look : c.lookYaw;
  snap.seatbelt = c.seatbelt;
  // (arcade driving: no handbrake to forget, no engine to stall – nothing of it judged)
  snap.handbrake = arcade ? 0 : c.handbrake;
  snap.stalled = stalled && !arcade;
  snap.hitScenery = impactNow;
  snap.length = v.spec.length.value_or(4.28);
  snap.front = def->outline.front;
  // (later: a passenger does not drive: nothing to judge, opens.passenger – game.ts:4371)
  rules->update(dt, snap, traffic->events);
  signalTips(dt);
  // (later: traffic.emergency.judging, its tip; roadEvents.takeTip – game.ts:4374)
  if (!flyParked) rememberGoodSpot(player);
  // (later: updateTask, updateDrill – game.ts:4387)
  if (demo) updateDemo(dt, player);
  else updateLesson(dt, player);
  // a crane driven to its stand: the site, the guide
  if (crane) {
    crane->drive(dt, v, c.handbrake > 0.5);
    if (crane->over) {
      finish();
      return;
    }
  }
  if (failT > 0 && time > failT) finish();
}

void Game::rememberGoodSpot(const PlayerState& p) {
  const RuleContext& ctx = rules->context;
  Lane* lane = currentLane();
  if (ctx.onRoad && lane && std::abs(p.speed) < 40 && !ctx.wrongWay && time - lastGood.t > 1.5) {
    lastGood = GoodSpot{{p.x, p.y, p.z, p.yaw}, time};
  }
  // fell off the world or wandered beyond the map: back to the last good place
  if (std::isnan(app.tiles->heightAt(p.x, p.z)) || p.y < lastGood.y - 60) resetToRoad();
}

Lane* Game::currentLane() { return rules->laneNow(); }

/**
 * Another vehicle, here and now: a free drive with it begins where the
 * player is (the world stays loaded) – the tram at the nearest stop, the
 * aeroplane at the end of the nearest runway.
 */
void Game::changeVehicle(const VehicleKind& kind) {
  if (lesson || (state != "pause" && state != "drive")) return;
  // (later: from where the walker stands – game.ts:4847)
  const double x = mpos[0];
  const double z = mpos[2];
  // (later: endWatch – game.ts:4850)
  if (demo) endDemo("ended");
  settings.vehicle = kind;
  saveSettings(settings);
  // (later: the tram at the nearest stop, the aeroplane at the end of the nearest runway – game.ts:4855)
  const AddressHit here{.label = tr("mapPlace", settings.lang), .street = "", .x = x, .z = z, .exact = false};
  begin("free", 0, true, &here);
}

// ------------------------------------------------- places chosen precisely (game/spawncar.ts)
/** A start place at the very place chosen ("place precisely"): it is remembered as such (the original's WeakSet exactSpots). */
SpawnJson Game::exactSpot(const std::string& name, double x, double z) {
  SpawnJson s;
  s.name = name;
  s.x = x;
  s.z = z;
  s.yaw = 0;
  s.kind = "street";
  exactSpots.push_back({x, z});
  // (only the last few can still be asked about: a place chosen is used at once)
  if (exactSpots.size() > 16) exactSpots.erase(exactSpots.begin());
  return s;
}

bool Game::isExactSpot(const SpawnJson& sp) const {
  for (const std::array<double, 2>& e : exactSpots)
    if (e[0] == sp.x && e[1] == sp.z && sp.yaw == 0 && !sp.rough) return true;
  return false;
}

/** Where a drive at an address or a place chosen begins: in front of it on the road – or, "place precisely", at the very place. */
std::optional<SpawnJson> Game::startSpot(const AddressHit& hit) { return settings.placeExact ? std::optional<SpawnJson>(exactSpot(hit.label, hit.x, hit.z)) : spawnAt(hit); }

/** A place chosen precisely, made fit: facing along the road nearest (if one is near), and if it is not free, the nearest free place round it. */
Game::Place Game::fitExact(const Place& sp, const Outline& o) {
  const std::optional<SpawnJson> road = findRoadSpot(*app.world, *lanes, sp.x, sp.z, "", 30, usableLane());
  const double yaw = road ? road->yaw : sp.yaw;
  // (later: freeNear(…, spawnCars.free …, 40): the nearest free place round it – game/spawncar.ts, game.ts:4971)
  (void)o;
  return Place{sp.x, sp.z, yaw};
}

/**
 * "Start where I am" (a setting): once, when the game starts, the browser
 * is asked where the player is – on the map, the drive is to begin there
 * (the main menu shows the vehicle there).
 */
void Game::startWhereIAm() {
  if (!settings.startHere) return;
  [](Game* self) -> Task<void> {
    const WorldMetaJson& m = self->app.world->json.meta;
    const LocateResult r = co_await locate(MapBounds{m.minX, m.maxX, m.minZ, m.maxZ});
    const std::string lang = self->settings.lang;
    if (!r.ok) {
      self->hud->message(tr(r.why == "outside" ? "myLocationOutside" : r.why == "denied" ? "myLocationDenied" : "myLocationNone", lang));
      co_return;
    }
    if (self->state != "menu") co_return;
    SpawnJson here;
    here.name = tr("myLocation", lang);
    here.x = r.x;
    here.z = r.z;
    here.yaw = 0;
    here.kind = "street";
    self->hud->menuStartAt(here);
    self->hud->redrawMenu();
  }(this).detach();
}

/**
 * Sign posts that stand on a carriageway where no lane runs (between lanes,
 * a road without lanes of its own – faults of the map the lanes do not
 * see): moved beside it, on their side – onto the pavement, or just beyond
 * the edge where there is none.  Posts in a junction stay (its corners).
 */
void Game::signsBesideRoad(std::vector<SignPost>& signs) {
  World& world = *app.world;
  for (SignPost& p : signs) {
    if (!world.surfaceAt(p.x, p.z, p.y + 1.5, hit) || hit.zone != 0 || !hit.edge || hit.node) continue;
    const Edge& e = *hit.edge;
    const int side = hit.off >= 0 ? 1 : -1;
    const double sw = side > 0 ? e.swL : e.swR;
    const double out = e.width / 2 + (sw > 0 ? 0.55 : 0.9);
    samplePolyline(e.pl, hit.s, smp);
    // (left of the direction (tx, tz) is (tz, −tx))
    p.x = smp.x + smp.tz * out * side;
    p.z = smp.z - smp.tx * out * side;
    const double y = footGround(p.x, p.z, p.y + 1.5);
    if (std::isfinite(y)) p.y = y;
  }
}

/** The height to stand on: the road or pavement there, else the ground. */
double Game::footGround(double x, double z, double yRef) { return app.world->surfaceAt(x, z, yRef, hit) ? hit.y : app.tiles->heightAt(x, z); }

}  // namespace fs
