// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (the aeroplane: apPhaseKey, airfields read, flyFrom, startPlaneAir, groundHeight,
//         firstRunway, planeAdapter, startPlane, placePlaneProxy, stepFlight, toggleFlightAuto, afterFlightStep, planeVisual)
/**
 * The light aeroplane in the game: chosen in the menu it stands at the end
 * of a runway (sim/airfield.ts), is flown by hand – with the help of
 * sim/autopilot.ts' FlightAssist – or by its autopilot, and drawn with its
 * instruments (render/plane.ts).  The game's vehicle stands below it as a
 * proxy: the map, the tiles and the minimap keep to it.
 */
#include <algorithm>
#include <cmath>

#include "audio/audio.h"
#include "core/js.h"
#include "game/game.h"
#include "i18n/index.h"
#include "input/input.h"
#include "render/airfield.h"
#include "render/far.h"
#include "render/plane.h"
#include "sim/airfield.h"
#include "sim/autopilot.h"
#include "sim/flight.h"
#include "sim/ground.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "ui/bigmap.h"
#include "ui/hud.h"
#include "world/far.h"

namespace fs {

/** What the aeroplane's autopilot is doing, in a few words a pilot would use (a text key). */
std::string apPhaseKey(const AutopilotState& st) {
  const std::string& p = st.phase;
  if (p == "lineup" || p == "takeoffRoll" || p == "rotate" || p == "initialClimb") return "ap.takeoff";
  if (p == "rejected") return "ap.rejected";
  if (p == "climb") return "ap.climb";
  if (p == "upwind" || p == "crosswind" || p == "downwind" || p == "base") return "ap.circuit";
  if (p == "final" || p == "flare") return "ap.final";
  if (p == "rollout" || p == "stopped") return "ap.landed";
  if (p == "goAround") return "ap.goAround";
  if (p == "orbit") return "ap.orbit";
  if (p == "enroute" || p == "route") return "ap.enroute";
  return "ap.hold";
}

/** The airfields: drawn on the ground, firm under an aeroplane's wheels, and the ends of their runways to take off from (game.ts:719). */
void Game::airRead(std::shared_ptr<const AirJson> json) {
  if (!json) return;
  air = json;
  airGround = std::make_unique<AirfieldGround>(*ground, json->airfields);
  airfieldView->set(json->airfields);
  std::vector<RunwayChoice> ends;
  for (const AirfieldJson& f : json->airfields) {
    const std::string field = !f.icao.empty() ? (!f.name.empty() ? f.name : f.icao) + " (" + f.icao + ")" : f.name;
    for (const RunwayJson& r : f.runways) {
      if (std::hypot(r.x2 - r.x1, r.z2 - r.z1) < 300) continue;
      for (const bool second : {false, true}) {
        const RunwayStart s = runwayStart(r, second);
        ends.push_back(RunwayChoice{.field = field, .end = runwayEnd(r, second), .x = s.x, .z = s.z, .yaw = s.yaw});
      }
    }
  }
  hud->setRunways(std::move(ends));
}

/**
 * Free flight: the aeroplane put over a place chosen on the map, `agl`
 * metres above the ground there (150 … 3000), flying level the way the road
 * there runs – once the ground there is loaded (tryPlace).
 */
void Game::flyFrom(const SpawnJson& spot_, double agl) {
  if (!aircraft || lesson || (state != "map" && state != "pause")) return;
  // (the spot may be the map's own, which closing the map clears: held here)
  SpawnJson spot = spot_;
  // (later: endWatch(false) – game.ts:3435)
  bigMap->close();
  spot.rough = false;
  spawn = spot;
  airAgl = std::max(150.0, std::min(3000.0, agl));
  planePending = true;
  traffic->clear();
  rail->clear();
  keepClock = true;
  state = "placing";
  showModel(false);
  hud->setHudVisible(true);
  hud->showLoading(tr("loadingTiles", settings.lang));
}

/** In the air over a place, `agl` metres above the ground, flying level at cruising speed with the power and trim for it. */
void Game::startPlaneAir(const SpawnJson& sp, double agl) {
  airAgl = 0;
  if (!aircraft) aircraft = std::make_unique<Aircraft>(TRAINER, powerOf(*def, settings.powers));
  Aircraft& a = *aircraft;
  const double speed = TRAINER.vcruise;
  a.placeInAir(sp.x, groundHeight(sp.x, sp.z) + agl, sp.z, sp.yaw, speed, &flightGround());
  const LevelFlight lf = a.levelFlight(speed);
  flight.throttle = lf.throttle;
  flight.flaps = 0;
  flight.trim = lf.trim;
  flight.crashT = -1;
  // (placed in the air: the autopilot holds this height and heading until the stick is touched)
  flightAuto->off();
  flightAuto->hold(a.heading(), a.altitude());
  flightAssist->reset(lf.trim);
  flightTake = 0;
  input->c.gear = 0;
  ui.addClass("flying");
  placePlaneProxy();
  lastGood = GoodSpot{{sp.x, a.pos.y, sp.z, sp.yaw}, 0};
  camSnap = true;
  showModel(true);
  keepClock = false;
  resume();
}

/** The ground's height: from the tiles, else the coarse ground of the region, else 0. */
double Game::groundHeight(double x, double z) {
  const double y = app.tiles->heightAt(x, z);
  if (!std::isnan(y)) return y;
  const double f = far->data ? farHeight(*far->data, x, z) : NAN;
  return std::isnan(f) ? 0 : f;
}

/** The first end of a runway of the map (a flight asked for without a choice, by a link). */
std::optional<RunwayChoice> Game::firstRunway() {
  if (!air) return std::nullopt;
  const auto longEnough = [](const RunwayJson& r) { return std::hypot(r.x2 - r.x1, r.z2 - r.z1) >= 300; };
  for (const AirfieldJson& f : air->airfields) {
    const auto r = std::find_if(f.runways.begin(), f.runways.end(), longEnough);
    if (r == f.runways.end()) continue;
    const RunwayStart s = runwayStart(*r, false);
    return RunwayChoice{.field = f.name, .end = runwayEnd(*r, false), .x = s.x, .z = s.z, .yaw = s.yaw};
  }
  return std::nullopt;
}

/**
 * The aeroplane's model, as the game holds every vehicle's – its parts move with the flight (planeVisual).
 * (2.0: kept beside the road vehicles' models – `models` holds those – made the first time it is needed.)
 */
PlaneModel& Game::planeAdapter() {
  if (planeModel) return *planeModel;
  planeModel = std::make_unique<PlaneModel>();
  planeModel->root->traverse([](Object3D& o) {
    if (o.type == ObjectType::Mesh) o.castShadow = true;
  });
  app.scene->add(planeModel->root);
  planeModel->setCabinOpacity(settings.cabinOpacity);
  planeModel->root->visible = false;
  return *planeModel;
}

Ground& Game::flightGround() { return airGround ? static_cast<Ground&>(*airGround) : static_cast<Ground&>(*ground); }

/** On its wheels at the end of the runway, engine running, flaps at 10°: ready to take off. */
void Game::startPlane(const SpawnJson& sp) {
  aircraft = std::make_unique<Aircraft>(TRAINER, powerOf(*def, settings.powers));
  Aircraft& a = *aircraft;
  a.placeOnGround(sp.x, sp.z, sp.yaw, flightGround(), true);
  flight.throttle = 0;
  flight.flaps = 1;
  flight.trim = 0.15;
  flight.crashT = -1;
  flightAuto->off();
  flightAssist->reset(flight.trim);
  flightTake = 0;
  // the demonstration flight chosen in the menu: from this runway round the field (or the sights) and down again
  if (planeDemoPending) {
    planeDemoPending = false;
    const std::optional<RunwayAt> r = air ? runwayAt(air->airfields, sp.x, sp.z, sp.yaw) : std::nullopt;
    if (r) {
      flightAuto->demo(*r->runway, r->fromSecond);
      hud->message(tr("apOn", settings.lang));
    }
  }
  // (the gear buttons are the flap lever now: from where they stand nothing is to be moved)
  input->c.gear = 0;
  ui.addClass("flying");
  placePlaneProxy();
  lastGood = GoodSpot{{sp.x, a.pos.y, sp.z, sp.yaw}, 0};
  camSnap = true;
  showModel(true);
  if (!keepClock) {
    startOdo = vehicle->odometer;
    startTime = time;
  }
  keepClock = false;
  resume();
}

/** The game's vehicle stands below the aeroplane: the map, the tiles and the minimap keep to it. */
void Game::placePlaneProxy() {
  const Aircraft& a = *aircraft;
  vehicle->teleport(a.pos.x, a.pos.y, a.pos.z, a.yaw());
  mpos[0] = a.pos.x;
  mpos[1] = a.pos.y;
  mpos[2] = a.pos.z;
}

/**
 * A step of the flight: the stick, the rudder, the throttle lever (it
 * stays where it is put), the flaps, the trim – which follows a stick held
 * off the middle for a while, so that the aeroplane keeps the attitude it
 * is put in –, the brakes on the ground.  The traffic goes on below.
 */
void Game::stepFlight(double dt) {
  Aircraft& a = *aircraft;
  FlightLevers& f = flight;
  const Input::Flight fi = input->flightInput();
  // the flap lever also on the gear buttons (▲ ▼ on the screen, E Q on the keys): down for more flap, up for less
  Controls& ci = input->c;
  if (ci.gear != 0) {
    f.flaps = std::max(0, std::min(3, f.flaps + (ci.gear < 0 ? 1 : -1)));
    ci.gear = 0;
    audio->click("tick");
  }
  Ground& ground = flightGround();
  FlightAutopilot& ap = *flightAuto;
  if (ap.engaged()) {
    // whoever moves the stick, the rudder or the throttle for a moment flies by hand from there (the levers stay as they were)
    const bool hands = std::abs(fi.pitch) > 0.25 || std::abs(fi.roll) > 0.25 || std::abs(fi.yaw) > 0.3 || std::abs(fi.throttle) > 0.3 || fi.brake > 0.3;
    flightTake = hands ? flightTake + dt : 0;
    if (flightTake > 0.18) {
      ap.off();
      flightAssist->reset(f.trim);
      hud->message(tr("apOff", settings.lang));
    }
  }
  if (ap.engaged()) {
    const FlightControls& ac = ap.update(a, dt, ground);
    if (flightGoto && ap.state.mode == "hold") {
      const FlightGoto g = *flightGoto;
      flightGoto.reset();
      ap.goto_(g.x, g.z, std::max(g.alt, a.altitude()));
    }
    // (the levers follow what the autopilot does: taking over is smooth)
    f.throttle = ac.throttle;
    f.flaps = ap.flapNotch();
    f.trim = ac.trim;
    *flightControls = ac;
    if (f.crashT < 0) a.step(dt, ac, ground);
    afterFlightStep(a, dt);
    return;
  }
  f.throttle = std::max(0.0, std::min(1.0, f.throttle + fi.throttle * dt * 0.5));
  if (!a.onGround()) f.trim = std::max(-1.0, std::min(1.0, f.trim + fi.pitch * dt * 0.06));
  FlightControls& c = *flightControls;
  c.throttle = f.throttle;
  c.elevator = fi.pitch;
  c.aileron = fi.roll;
  // the rudder: from its keys, else a little with the ailerons (turns stay balanced) – and on the ground the nose wheel follows the stick
  c.rudder = std::max(-1.0, std::min(1.0, fi.yaw + fi.roll * (a.onGround() ? 0.9 : 0.25)));
  c.flaps = f.flaps / 3.0;
  c.trim = f.trim;
  c.brake = fi.brake;
  c.parkingBrake = false;
  c.engineOn = true;
  // the help for flying by hand (sim/autopilot.ts FlightAssist): softer stick, balanced turns, wings level when let go, no stall
  const std::string& help = settings.flightAssist;
  if (help != "off") {
    flightAssist->options = help == "light" ? ASSIST_LIGHT : ASSIST_BEGINNER;
    flightAssist->apply(a, StickInput{fi.pitch, fi.roll, fi.yaw}, dt, c);
    f.trim = c.trim;
  }
  if (f.crashT < 0) a.step(dt, c, ground);
  afterFlightStep(a, dt);
}

/** The aeroplane on or off its autopilot (the demonstration button, or its key): from the runway a demonstration flight, in the air holding height and heading. */
void Game::toggleFlightAuto() {
  if (!aircraft) return;
  Aircraft& a = *aircraft;
  FlightAutopilot& ap = *flightAuto;
  const std::string lang = settings.lang;
  if (ap.engaged()) {
    ap.off();
    flightAssist->reset(flight.trim);
    hud->message(tr("apOff", lang));
    return;
  }
  const std::optional<RunwayAt> r = a.onGround() && air ? runwayAt(air->airfields, a.pos.x, a.pos.z, a.yaw()) : std::nullopt;
  if (r) ap.demo(*r->runway, r->fromSecond);
  else if (!a.onGround()) {
    ap.hold(a.heading(), a.altitude());
    ap.takeOver({flight.throttle, flight.trim});
  } else {
    hud->message(tr("apNoRunway", lang));
    return;
  }
  flightTake = 0;
  hud->message(tr("apOn", lang));
}

/** What follows each step of the flight, by hand or by the autopilot: the world below, a crash. */
void Game::afterFlightStep(Aircraft& a, double dt) {
  FlightLevers& f = flight;
  placePlaneProxy();
  rules->time += dt;
  signals->update(dt);
  // below: the trains and trams, the traffic (high up nobody comes near enough to be in the way)
  PlayerState player;
  player.x = a.pos.x;
  player.y = a.pos.y;
  player.z = a.pos.z;
  player.yaw = a.yaw();
  player.speed = 0;
  player.length = 0.1;
  player.width = 0.1;
  stepRail(dt, player, false);
  traffic->obstacles.clear();
  traffic->update(dt, player);
  airfieldView->update(a.pos.x, a.pos.z);
  const std::string lang = settings.lang;
  if (a.crashed && f.crashT < 0) {
    f.crashT = time;
    hud->message(tr("planeCrashed", lang), tr(std::string("planeCrash.") + crashKindName(*a.crashed), lang));
  }
  if (f.crashT > 0 && time - f.crashT > 4) finish();
}

/** How the aeroplane looks: its control surfaces, wheels, propeller, lights – and its instruments. */
PlaneVisualState Game::planeVisual(Aircraft& a) {
  const FlightSurfaces s = a.surfaces();
  const FlightControls& c = *flightControls;
  const double deg = 180 / M_PI;
  PlaneVisualState v;
  v.aileron = s.aileron;
  v.elevator = s.elevator;
  v.rudder = s.rudder;
  v.flapsDeg = a.flapsDeg();
  v.throttle = flight.throttle;
  v.rpm = a.rpm();
  v.propAngle = a.propAngle();
  for (const WheelView& w : a.wheels) v.wheels.push_back({w.compression, w.spin, w.steer});
  v.yoke = {c.elevator, c.aileron};
  v.pedals = c.rudder;
  v.lights = {.nav = true, .beacon = a.engineRunning(), .strobe = !a.onGround(), .landing = a.agl() < 300, .taxi = a.onGround()};
  v.iasKt = a.ias() * 1.94384;
  v.altitudeFt = a.altitude() * 3.28084;
  v.vsFpm = a.verticalSpeed() * 196.85;
  v.headingDeg = a.heading();
  v.pitchDeg = a.pitch() * deg;
  v.bankDeg = a.bank() * deg;
  v.slip = a.slip();
  v.turnRateDegS = a.turnRate() * deg;
  v.rpmGauge = a.rpm();
  v.fuel = a.fuel / TRAINER.fuel;
  v.flapsGauge = a.flapsDeg();
  v.stallWarning = a.stallWarning();
  v.parkingBrake = false;
  v.engineOn = a.engineRunning();
  return v;
}

}  // namespace fs
