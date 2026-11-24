// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (the lesson, frame, updateVisuals, updateCamera – game.ts:5187, 6272 … 7512)
#include <algorithm>
#include <cmath>

#include "audio/audio.h"
#include "audio/profiles.h"
#include "audio/speech.h"
#include "core/geom.h"
#include "core/js.h"
#include "game/address.h"
#include "game/arcadedrive.h"
#include "game/clock.h"
#include "game/cockpit.h"
#include "game/faultlog.h"
#include "game/flycam.h"
#include "game/game.h"
#include "game/nav.h"
#include "game/routepresets.h"
#include "game/track.h"
#include "i18n/index.h"
#include "input/input.h"
#include "platform/frame.h"
#include "platform/json.h"
#include "platform/plat.h"
#include "render/actors.h"
#include "render/far.h"
#include "render/mirrors.h"
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
#include "sim/vehicle/fleet.h"
#include "sim/vehicle/index.h"
#include "ui/bigmap.h"
#include "ui/hud.h"
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

/** Each picture: the stick on the screen, the button offered after a tap on the picture, the keys of the flying camera. */
void Game::footFlyFrame() {
  App& app = this->app;
  if (!touchStick) touchStick = std::make_unique<TouchStick>(ui, *input, [this](const std::string& k) { return tr(k, settings.lang); });
  if (!worldTap) {
    WorldTapHost host;
    host.canvas = app.canvas;
    host.camera = app.camera;
    host.ui = ui;
    host.heightAt = [this](double x, double z) { return this->app.tiles->heightAt(x, z); };
    host.allowed = [this] { return canTeleport(); };
    host.canDrive = [this] { return canDrive(); };
    // (later: … spawnCars.picking, spawnCars.claims – game/spawncar.ts, game.ts:5197)
    host.claimed = [this](double cx, double cy, bool down) { return down ? false : screenTap && screenTap->covers(cx, cy); };
    // (later: tapToWalk, a tap sends the person on foot there – game.ts:5198)
    host.tapped = [](const GroundPoint&) {};
    host.teleport = [this](const GroundPoint& p, double yaw) { teleportHere(p.x, p.y, p.z, yaw); };
    host.drive = [this](const GroundPoint& p) { driveHere(p.x, p.z); };
    host.t = [this](const std::string& k) { return tr(k, settings.lang); };
    worldTap = std::make_unique<WorldTap>(std::move(host));
  }
  const bool flying = flyFree();
  input->flyCam = flying;
  // (the free camera begins anew – where the camera is – each time it is chosen)
  if (cameraMode != "free") fly->ready = false;
  const bool touch = inputMode == "touch" && state == "drive";
  // (later: walking, walkCtl: the ways of walking – game/walkmodes.ts, game.ts:5206)
  touchStick->frame(touch ? (flying ? StickUse("fly") : std::nullopt) : std::nullopt);
  worldTap->frame();
  // (later: tileVersionsFrame – game.ts:5210)
}

/** The pedestrian lights of the roads that are loaded: their heads, and their poles in the way of whoever moves. */
void Game::buildPedLights() {
  pedPoles = fs::pedPoles(*app.world);
  pedHeads->rebuild(pedPoles);
  std::vector<double> discs;
  for (const PedPole& p : pedPoles) discs.insert(discs.end(), {p.x, p.y, p.z, 0.07});
  collider->remove("pedlights");
  collider->add({.key = "pedlights", .rings = nullptr, .ringCounts = nullptr, .ringSkip = nullptr, .segments = nullptr, .discs = discs});
}

/**
 * What the instructor says about lights that are not the usual three
 * lamps – with the rule help on, and not in the test: the lights of the
 * junction ahead are out of order (the signs count), and the green arrow
 * beside a red light is lit (whoever turns right may go).
 */
void Game::signalTips(double dt) {
  const std::optional<RuleContext::Junction>& j = rules->context.junction;
  if (!j || !settings.hudRules || rules->strict || (demo && demo->active) || time < tipUntil) {
    atArrow = 0;
    return;
  }
  const std::string& lang = settings.lang;
  if (j->dark && j->dist < 70) {
    Lane* lane = currentLane();
    Node* node = lane ? lane->to : nullptr;
    if (node && node != toldDark) {
      toldDark = node;
      tipText = tr(j->ctrl == CTRL_RBL ? "tipLightsOutRbl" : "tipLightsOut", lang);
      tipUntil = time + 6;
    }
  }
  // standing at the line on red while the arrow is lit: say so after a moment (once per stop)
  if (j->arrow == SIG_GREEN && j->signal != SIG_GREEN && j->dist < 8 && std::abs(vehicle->speed) < 0.3) {
    const double before = atArrow;
    atArrow += dt;
    if (before < 1.2 && atArrow >= 1.2) {
      tipText = tr("tipArrow", lang);
      tipUntil = time + 5;
    }
  } else atArrow = 0;
}

double Game::laneFront() { return rules->laneFront(); }

void Game::updateLesson(double dt, PlayerState& p) {
  const LessonDef* l = lesson;
  Lane* lane = currentLane();
  Vehicle& v = *vehicle;
  const std::string& lang = settings.lang;
  if (!l) return;
  // (later: the bus on a line, updateBusLine – game.ts:6351)
  if (l->kind == "basics") {
    Controls& c = input->c;
    const bool automatic = v.spec.transmission == "automatic";
    const std::vector<std::pair<std::string, std::function<bool()>>> steps = {
        {"basics.belt", [&] { return c.seatbelt; }},
        {automatic ? "basics.startAuto" : "basics.startManual", [&] { return v.engineRunning; }},
        {automatic ? "basics.gearAuto" : "basics.gearManual", [&] { return automatic ? c.gear == 1 : v.gear == 1; }},
        {"basics.handbrake", [&] { return c.handbrake < 0.5; }},
        {"basics.pullAway", [&] { return v.speed * 3.6 > 15; }},
        {automatic ? "basics.accelAuto" : "basics.accelManual", [&] { return v.speed * 3.6 > 30 && (automatic || v.gear >= 2); }},
        {"basics.brake", [&] { return std::abs(v.speed) < 0.1; }},
        {"basics.reverse", [&] { return v.speed < -1; }},
        {"basics.stop", [&] { return std::abs(v.speed) < 0.1 && c.handbrake > 0.5; }},
    };
    const int n = static_cast<int>(steps.size());
    if (basicsStep < n && steps[basicsStep].second()) {
      basicsStep++;
      audio->click("chime");
    }
    if (basicsStep >= n) {
      finish();
      return;
    }
    lessonLine = std::to_string(basicsStep + 1) + "/" + std::to_string(n) + " · " + tr(steps[basicsStep].first, lang);
    return;
  }
  // routed lessons and the exam
  if (!lane) return;
  if (homeEdge < 0) homeEdge = lane->edge->id;
  if (!nav->busy()) {
    // (the road network comes in pieces: a destination is looked for once the roads all around are there)
    if (!app.map->ready(p.x, p.z)) return;
    // (`l.targets[this.targetIdx]` of a lesson without destinations is no type at all: nothing is found for it)
    const TargetType type = targetIdx < static_cast<int>(l->targets.size()) ? l->targets[targetIdx] : TargetType();
    Edge* home = app.world->edges[homeEdge];
    // (the first destination of a lesson in a real place: the road behind that place – game/lessonplaces.ts)
    Edge* placed = placeGoal;
    placeGoal = nullptr;
    Edge* goal = placed ? placed : type == "home" && !home ? nullptr : findTarget(*app.world, *lanes, type, p.x, p.z, *lane, home, visited);
    // The way home, or to a motorway, can lead through parts of the map that are not loaded:
    // then it is found in the roads of the whole map and followed like a planned drive.
    if (app.map->pieces && (type == "home" || type == "motorway") && !(goal && nav->setGoal(goal, lane))) {
      nav->clear();
      goal = nullptr;
      RouteGraph* g = routeGraphNow();
      if (!g) {
        // (the roads of the whole map are still being read)
        wantNet();
        return;
      }
      const std::optional<std::vector<int>> ways =
          type == "home" ? (homeEdge == lane->edge->id ? std::optional<std::vector<int>>(std::vector<int>()) : quickestToEdge(*g, wayOf(*lane), homeEdge)) : waysToMotorway(*g, *lane, p.x, p.z);
      if (ways) {
        nav->net = g;
        nav->setWays(*ways, *lane, false);
        hudRouteLanes = nav->routeLanes;
        hud->routeLanes = *nav->routeLanes;
        return;
      }
    }
    if (!goal) {
      // nothing suitable in reach: skip this part
      targetIdx++;
      if (targetIdx >= static_cast<int>(l->targets.size())) {
        if (visited.empty() && time - startTime < 20) {
          // not a single destination from this starting point: drive freely instead of ending at once
          lesson = nullptr;
          hud->canTeleport = true;
          hud->message(tr("lessonNoRoute", lang));
        } else finish();
      }
      return;
    }
    visited.insert(goal);
    nav->setGoal(goal, lane);
    hudRouteLanes = nav->routeLanes;
    hud->routeLanes = *nav->routeLanes;
  }
  const NavHint& h = nav->update(dt, lane, laneFront(), p.x, p.z);
  if (hudRouteLanes != nav->routeLanes) {
    hudRouteLanes = nav->routeLanes;
    hud->routeLanes = *nav->routeLanes;
    allowedEdges.clear();
    for (const Lane* rl : *nav->routeLanes) allowedEdges.insert(rl->edge);
    rules->allowed = &allowedEdges;
  }
  if (h.done) {
    targetIdx++;
    nav->clear();
    audio->click("chime");
    if (targetIdx >= static_cast<int>(l->targets.size())) finish();
  }
  lessonLine = lessonTitle(l->id, lang) + " · " + std::to_string(targetIdx + 1) + "/" + std::to_string(l->targets.size());
}

// ------------------------------------------------------------------ frame
void Game::frame(double now) {
  const double tFrame = plat::nowMs();
  // (never backwards: the time stamp of a frame that was held up by a long task can lie before
  // the moment the clock was last read, and a clock running backwards freezes everything timed)
  const double dt = std::min(0.1, std::max(0.0, (now - last) / 1000));
  last = now;
  time += dt;
  frames++;
  // pictures a second, for whoever asked for the display (twice a second, from the clock of the pictures)
  fpsFrames++;
  if (now - fpsSince >= 500) {
    // (the world's moment, when the arcade mode set another one or runs the time faster – and the sun of it)
    // (later: clockBadge(gameClock.ms(), gameClock.rate, locale) while the clock is not the real one – ui/arcade.ts, game.ts:6456)
    hud->setClock(std::nullopt);
    updateSun();
    hud->setFps(fpsSince > 0 && state == "drive" ? (fpsFrames * 1000) / (now - fpsSince) : -1);
    fpsSince = now;
    fpsFrames = 0;
  }
  App& app = this->app;
  TileManager& tiles = *app.tiles;
  // (later: spawnCars.frame – game.ts:6464)
  footFlyFrame();

  // the road network around where things happen (it may come in pieces), and the signal heads of what is loaded
  if (state == "placing") app.map->update(spawn->x, spawn->z, 40);
  else if (vehicle && state != "menu" && state != "loading") {
    const std::array<double, 2> f = focus();
    app.map->update(f[0], f[1], state == "drive" ? 5 : 25);
  }
  if (signalsDirty && time - signalsBuilt > 0.6) {
    signalsDirty = false;
    signalsBuilt = time;
    signalHeads->rebuild(*app.world, signals.get());
    collider->remove("signals");
    collider->add({.key = "signals", .rings = nullptr, .ringCounts = nullptr, .ringSkip = nullptr, .segments = nullptr, .discs = signalHeads->poles});
    buildPedLights();
    // (later: laneStops.clear – game.ts:6480)
  }
  if (state == "placing") {
    tiles.update(spawn->x, spawn->z);
    tryPlace();
  } else if (state == "drive") {
    // fingers or keys: follows what the driver last used, unless the settings say otherwise
    const std::string& want = settings.touchControls;
    inputMode = want == "on" ? "touch" : want == "off" ? "keys" : input->mode;
    hud->setInputMode(inputMode);
    setKeyStyle(inputMode == "touch" ? KeyStyle::Touch : KeyStyle::Keys);
    // The arcade mode lets the time run faster – while the autopilot drives, and since 1.7.0 with the
    // player at the wheel too: the world is stepped several times a picture, as often as the picture
    // can wait for (the world's clock goes on by what was really stepped); none of the steps is longer
    // than a tenth of a second, and the vehicle's physics divides each into its own small steps.
    const double rate = gameClock.rate() > 1 ? gameClock.rate() : 1;
    if (gameClock.rate() > 1 && rate == 1) {
      gameClock.setRate(1);
      if (gameClock.real()) clockJumped();
    }
    const double tSim = plat::nowMs();
    if (rate > 1) {
      // (in as few steps as the world's parts allow – none of them is stepped by more than a tenth of a second)
      const int n = std::max(1, static_cast<int>(std::ceil((dt * rate) / 0.1)));
      const double sub = (dt * rate) / n;
      const double t0 = plat::nowMs();
      // (the frame's own share of the time was already counted at its start)
      time -= dt;
      double done = 0;
      for (int i = 0; i < n && state == "drive"; i++) {
        time += sub;
        stepDrive(sub);
        done += sub;
        if (plat::nowMs() - t0 > 28) break;
      }
      gameClock.tick(done);
    } else {
      stepDrive(dt);
      gameClock.tick(dt);
      // (later: faceBoard, stepLiveGeo, hud.liveOffer, hud.liveOn – game.ts:6518)
    }
    frameTimes.sim += plat::nowMs() - tSim;
    if (!autoNotice.empty() && time - startTime > 0.3) {
      const std::string kind = autoNotice;
      autoNotice = "";
      // (they stay: the automated check may take minutes for a frame)
      const auto show = [this](const std::string& id, const std::string& severity, const std::vector<std::pair<std::string, double>>& data = {}) {
        Violation v;
        v.id = id;
        v.severity = severity;
        v.time = time;
        v.x = 0;
        v.z = 0;
        FaultEntry e;
        e.t = plat::dateMs();
        e.id = id;
        e.severity = severity;
        for (const auto& [k, n] : data) {
          v.data.emplace_back(k, n);
          e.data.emplace_back(k, n);
        }
        e.street = rules->context.roadName;
        e.mode = lesson ? lesson->id : "free";
        e.x = 0;
        e.z = 0;
        hud->toast(v, 3600);
        faultLog->add(e);
      };
      if (kind != "yellow") show("redLight", "fail");
      if (kind != "red") show("speed", "minor", {{"limit", 30}, {"speed", 42}});
    }
    // (later: ?tram=1 puts a tram on the nearest track – game.ts:6534)
    if (!autoDemo.empty() && time - startTime > 0.3) {
      std::vector<SpawnJson> stops;
      for (const std::vector<double>& at : autoDemo) {
        const std::optional<SpawnJson> spot = roadSpot(at.size() > 0 ? at[0] : JS_NAN, at.size() > 1 ? at[1] : JS_NAN, "", 200);
        if (spot) stops.push_back(*spot);
      }
      autoDemo.clear();
      startTrip(stops, autoSelf, nullptr);
    }
    if (!presetPending.empty() && time - startTime > 0.3 && currentLane() && routeGraphNow()) {
      const std::string id = presetPending;
      presetPending = "";
      startPreset(id);
    }
    if (autoTour > 0 && time - startTime > 0.3 && currentLane() && routeGraphNow()) {
      const double km = autoTour;
      autoTour = 0;
      [](Game* self, double km) -> Task<void> {
        const std::optional<TourPreview> tour = co_await self->planTour(km * 1000, std::nullopt);
        if (tour) self->startTrip({}, self->autoSelf, &*tour);
      }(this, km).detach();
    }
    if (!autoMap.empty() && time - startTime > 0.3) {
      const std::string mode = autoMap;
      autoMap = "";
      if (mode == "aerial") settings.reality.aerial = true;
      openMap(mode == "aerial");
    }
    // (later: ?reality= – game.ts:6580)
  }

  double tPart = plat::nowMs();
  const auto lap = [&tPart](double& into) {
    const double now = plat::nowMs();
    into += now - tPart;
    tPart = now;
  };
  VehicleModel& model = this->model();
  // (in the menu the vehicle shown stands still: it is not driven)
  const bool haveCar = vehicle && model.root->visible && state != "menu" && state != "loading";
  // (later: the replay, placeReplay – game.ts:6597)
  if (haveCar) {
    Vehicle& v = *vehicle;
    v.getModelPosition(mpos.data());
    model.root->position.set(mpos[0], mpos[1], mpos[2]);
    model.root->quaternion.set(v.quat[0], v.quat[1], v.quat[2], v.quat[3]);
    model.root->updateMatrixWorld(true);
    // (later: the truck's trailer and the box trailer placed – game.ts:6605)
    // a motorcycle leans into the bend: as far as it must for the speed and the curve
    if (PlayerBike* bike = dynamic_cast<PlayerBike*>(&model)) bike->setLean(bikeLean(v.speed, v.angVel[1]));
    // (later: the aeroplane – game.ts:6610)
    const std::array<double, 2> f = focus();
    tiles.update(f[0], f[1]);
    props->update(f[0], f[1]);
    // (later: grass.update – game.ts:6621)
    updateVisuals(dt);
    // (later: crane.render, forklift.render; the drive written down for its replay – game.ts:6624)
  }
  // (later: menuShow, the vehicle chosen behind the menu of rows – game.ts:6632)
  else if (state == "menu" || state == "loading") {
    // slow fly-over behind the menu
    const double a = time * 0.05;
    const double cx = menuFocus.x + std::cos(a) * 260;
    const double cz = menuFocus.z + std::sin(a) * 260;
    tiles.update(menuFocus.x, menuFocus.z);
    props->update(cx, cz);
    // (later: grass.update – game.ts:6641)
    const double gy = tiles.heightAt(menuFocus.x, menuFocus.z);
    const double base = std::isnan(gy) ? 230 : gy;
    app.camera->position.set(cx, base + 95, cz);
    app.camera->lookAt(menuFocus.x, base + 10, menuFocus.z);
    app.camera->fov = 55;
    app.camera->near = 0.5;
    app.camera->updateProjectionMatrix();
    signals->update(dt);
  }
  lap(frameTimes.visuals);
  // (later: a theory lesson on the practice yard – game.ts:6650)
  signs->flush();
  // (later: the test place, buildShowroom; the replay's lights, traffic and trains – game.ts:6656)
  signalHeads->update(*signals);
  pedHeads->update(*signals);
  {
    static const std::vector<Cyclist*> noCyclists;
    actors->update(traffic->cars, traffic->peds(), time, traffic->cyclists ? traffic->cyclists->list : noCyclists);
  }
  lap(frameTimes.scene);
  // (later: wildView, railActors, transit, the lines of the trams, roadworks, crossingView, explosion – game.ts:6682)
  // (later: headlights – game/beamplay.ts, game.ts:6714)
  // (later: online.frame – game.ts:6716)

  // (later: the reality view's own picture – game.ts:6718)
  if (state == "map") {
    // the map covers the window: nothing of the world needs drawing
  }
  // (later: the replay's camera – game.ts:6723)
  else if (haveCar) {
    updateCamera(dt);
    // (later: updateOutdoors, the player's own figure on foot – game.ts:6730)
    far->update(app.camera->position.x, app.camera->position.z);
    const std::string& mode = settings.mirrors;
    if (cameraMode == "cockpit" && mode != "off") {
      const std::vector<std::unique_ptr<Mirror>>& mirrors = model.mirrors;
      const double farMirror = std::min(450.0, app.tiles->viewRadius * 0.4);
      // (each mirror in the frame of what it is fixed to: the motorcycle's turn with the handlebar and lean with it)
      const auto render = [&](Mirror& m) {
        Object3D* parent = m.mesh->parent;
        m.render(app.renderer, *app.scene, (parent ? parent : model.root.get())->matrixWorld, eyeWorld, farMirror);
      };
      if (mode == "full" || frames < 4)
        for (const std::unique_ptr<Mirror>& m : mirrors) render(*m);
      else if (!mirrors.empty()) {
        // (one mirror a picture – a cab without mirrors, the tram's, has none to turn)
        render(*mirrors[static_cast<size_t>(mirrorTurn++) % mirrors.size()]);
      }
    }
    lap(frameTimes.mirrors);
    // (later: the watcher's place as the middle of the picture – game.ts:6748)
    app.render(model.root->position);
    lap(frameTimes.render);
    // (later: windscreen.frame, hud.setWipers, the fault noView – game.ts:6750)
  } else {
    app.render(menuFocus);
  }
  // (later: signalTimers, queueLabels, hudMirror, mirrorZoom – game.ts:6759)
  // (later: the reality view counted ready – game.ts:6788)
  if (state == "map") {
    if (++mapFrames > 3) ready = true;
  } else mapFrames = 0;
  if (!ready && tiles.settled() && frames > 6 && (state == "drive" || state == "menu" || state == "intro")) {
    if (!autoMap.empty()) {
      /* the map is about to open */
    } else if (state != "drive" || time - startTime > jsNumber(URLSearchParams(plat::locationSearch()).get("wait").value_or("0"))) ready = true;
  }
  if (state == "drive") {
    frameTimes.total += plat::nowMs() - tFrame;
    frameTimes.frames++;
  }
  if (looping) plat::requestAnimationFrame([this](double t) { frame(t); });
}

void Game::updateVisuals(double dt) {
  Vehicle& v = *vehicle;
  Controls& c = input->c;
  CarVisualState& s = visual;
  const double kmh = v.speed * 3.6;
  // (later: the tram: no steering bar on the screen, its lever – game.ts:6882)
  s.speedKmh = std::abs(kmh);
  s.rpm = v.rpm;
  s.gear = v.spec.transmission == "automatic" ? (c.gear == 1 ? v.gear : c.gear) : v.gear;
  s.indicator = c.indicator;
  s.hazard = c.hazard;
  s.blinkOn = static_cast<long long>(std::floor(time * 3)) % 2 == 0;
  s.lowBeam = c.lowBeam;
  // (later: beams.high(…, settings.beamAssist): the main beam put on by the assist – game/beamplay.ts, game.ts:6891;
  //  until then it is lit when it is switched on with the dipped beam, or while the flasher is held)
  s.highBeam = input->flasher() || (c.lowBeam && c.highBeam);
  s.handbrake = c.handbrake > 0.5;
  s.ignition = c.ignition;
  s.engineRunning = v.engineRunning;
  s.abs = v.absActive;
  s.seatbelt = c.seatbelt;
  s.automatic = v.spec.transmission == "automatic";
  s.showGear = settings.hudGear;
  s.odometerKm = v.odometer / 1000;
  s.steerWheel = v.steerWheelAngle;
  v.getWheelHeights(wheelY.data());
  for (int i = 0; i < 4; i++) {
    s.wheelSteer[i] = v.wheels[i].steer;
    s.wheelSpin[i] = v.wheels[i].spin;
    s.wheelY[i] = wheelY[i];
  }
  s.clutch = v.clutchPedal;
  s.brake = driver.brake;
  s.throttle = driver.throttle;
  s.handbrakeLever = c.handbrake;
  s.braking = driver.brake > 0.03;
  s.reversing = v.gear == -1;
  lastVisual = &s;
  // (later: services.visual: the fuel gauge, the dirt – game.ts:6914)
  VehicleModel& model = this->model();
  model.update(s, dt, time);
  // the map on the vehicle's own screen, in the driver's seat – the corner map is hidden meanwhile (render/navscreen.ts)
  // (later: the other models' own navScreen – game.ts:6917)
  VehicleScreen* screen = &model == &car ? carScreen.get() : nullptr;
  const bool onScreen = screen && settings.mapOnScreen && cameraMode == "cockpit";
  // (a tap on the screen opens the big map – or, as the settings choose, brings the map back into the corner: ui/screentap.ts)
  if (!screenTap) screenTap = std::make_unique<ScreenTap>(app.canvas, app.camera, [this] { return settings.screenTap; }, [this] { openMap(); });
  ScreenTap& tap = *screenTap;
  tap.setScreen(onScreen && state == "drive" ? screen : nullptr);
  const bool peek = tap.peek && settings.screenTap == "minimap";
  if (screen) screen->setLive(onScreen && !peek);
  if (onScreen && !peek) hud->screen = Hud::Screen{screen->canvas, [screen] { screen->drawn(); }};
  else hud->screen = std::nullopt;

  // sound
  double skid = 0;
  double rough = 1;
  for (const WheelState& w : v.wheels) {
    skid = std::max(skid, w.skid);
    rough = std::max(rough, w.grip < 0.75 ? 2.4 : 1.0);
  }
  {
    // (later: the aeroplane's and the tram's sounds – game.ts:6937)
    AudioState a;
    a.rpm = v.rpm;
    a.load = v.engineLoad;
    a.engineRunning = v.engineRunning;
    a.starter = c.starter;
    a.speed = v.speed;
    a.skid = skid;
    a.indicatorOn = (c.indicator != 0 || c.hazard) && state == "drive";
    a.blinkOn = s.blinkOn;
    a.horn = c.horn;
    a.rough = rough;
    a.braking = driver.brake;
    a.parkingBrake = c.handbrake > 0.5;
    a.reversing = v.gear == -1;
    audio->update(a);
  }

  // (later: sirens.update, the horns of the emergency vehicles – audio/siren.ts, game.ts:6978)

  // HUD
  if (state != "drive") return;
  const std::string& lang = settings.lang;
  const RuleContext& ctx = rules->context;
  const NavHint& h = nav->hint;
  std::string navText;
  Demo* demo = this->demo.get();
  if (nav->busy() && ((lesson && lesson->kind != "basics") || demo)) {
    const std::string what = h.what == "navRoundabout" ? tr("navRoundabout", lang, {{"n", h.n}}) : tr(h.what, lang);
    if (h.what == "navFollow" || h.dist < 0) navText = tr("navFollow", lang);
    else if (h.dist < 35) navText = tr("navNow", lang, {{"what", what}});
    else navText = tr("navIn", lang, {{"d", jsRound(h.dist / 10) * 10}, {"what", what}});
    speak(h.what, h.dist, navText);
    // § 9 StVO: get in lane in good time – the route goes on from a particular lane
    if (h.lane != 0) navText += " – " + tr(h.lane < 0 ? "laneLeft" : "laneRight", lang);
  } else if (lesson && lesson->kind == "basics") navText = lessonLine;
  // (later: the tasks on the yard, the bus on a line: their line – game.ts:6995)
  std::string tip = time < tipUntil ? tipText : "";
  // on a touch screen the tip says what to tap – and is itself the button for it
  std::string tipAction;
  const bool touch = inputMode == "touch";
  const auto say = [&](const std::string& key, const std::string& action) {
    tip = tr(touch ? key + "Touch" : key, lang);
    tipAction = action;
  };
  // (later: the bus on a line: the doors at the stop – game.ts:7006)
  if (tip.empty()) {
    if (!v.engineRunning) {
      bool stalledNow = false;
      for (const Violation& x : rules->log)
        if (x.id == "stall" && time - x.time < 6) {
          stalledNow = true;
          break;
        }
      say(stalledNow ? "engineStalled" : "startEngine", "Enter");
    } else if (!c.seatbelt && std::abs(kmh) < 5) say("fastenBelt", "KeyB");
    // (the handbrake on with a gear in: said before the throttle is pressed against it)
    else if (c.handbrake > 0.5 && (c.throttle > 0.1 || (s.automatic ? c.gear == 1 || c.gear == -1 : v.gear != 0))) say("releaseHandbrake", "Space");
    else if (c.throttle > 0.1 && std::abs(kmh) < 2 && (s.automatic ? c.gear == -2 || c.gear == 0 : v.gear == 0)) say(s.automatic ? "selectDrive" : "selectGear", "KeyE");
    else {
      Lane* lane = currentLane();
      if (lane && lane->to->ctrl == CTRL_MAP_END && lane->len - laneFront() < 120) tip = tr("offMap", lang);
      else if (!ctx.onRoad && std::abs(kmh) < 3 && time - lastGood.t > 6) say("resetHint", "KeyT");
    }
  }
  const std::string gearText =
      s.automatic ? (c.gear == -2 ? "P" : c.gear == -1 ? "R" : c.gear == 0 ? "N" : "D" + std::to_string(v.gear)) : v.gear == -1 ? "R" : v.gear == 0 ? "N" : std::to_string(v.gear);
  // driving against the handbrake: the lamp flashes and a chime sounds (once per attempt)
  const bool hbWarn = c.handbrake > 0.5 && v.engineRunning && (c.throttle > 0.1 || std::abs(kmh) > 3);
  if (hbWarn && !hbWarned) audio->click("chime");
  hbWarned = hbWarn;
  HudState hs;
  hs.speedKmh = kmh;
  hs.rpm = v.rpm;
  // (the bar is full a little above the limiter: 7000 for the car as before)
  hs.rpmMax = v.spec.limiterRpm * 1.077;
  hs.gear = gearText;
  hs.limit = ctx.limit;
  hs.limitWhen = ctx.limitWhen;
  hs.zone = ctx.zone;
  hs.junction = ctx.junction;
  hs.navText = navText;
  // (later: the tasks on the yard: their sub line – game.ts:7041)
  hs.navSub = lesson && lesson->kind != "basics" ? lessonLine : ctx.roadName;
  hs.navArrow = h.arrow;
  hs.navActive = (lesson || demo) && h.dist >= 0 && h.what != "navFollow" && h.what != "routeDone";
  hs.tip = tip;
  hs.tipAction = tipAction;
  // (the truck's instruments stand far away at the foot of its windscreen: the strip stays)
  // (on foot there is nothing to read off a dashboard)
  hs.showDash = cameraMode != "cockpit" || def->dash;
  hs.onFoot = false;
  // (the autopilot drives by itself: a stop button is offered to take over)
  hs.autopilotDriving = autopilotDriving();
  // (the reminder of the keys is the car's: not for the tram – its lesson says how it is driven – nor for the
  // aeroplane, whose keys the tip line names)
  hs.noKeyHints = false;
  hs.indicator = c.indicator;
  hs.hazard = c.hazard;
  hs.blinkOn = s.blinkOn;
  hs.handbrake = c.handbrake > 0.5;
  hs.handbrakeWarn = hbWarn;
  hs.seatbelt = c.seatbelt;
  hs.engineOn = v.engineRunning;
  hs.lights = c.lowBeam;
  hs.points = rules->points();
  hs.faults = static_cast<int>(std::count_if(rules->log.begin(), rules->log.end(), [](const Violation& x) { return x.severity != "hint"; }));
  hs.x = mpos[0];
  hs.z = mpos[2];
  hs.yaw = v.yaw;
  if (demo) {
    // a drive to places or a tour: where it goes – and, when the car drives by itself, what the
    // driver is doing just now and why
    if (navText.empty()) hs.navText = !ctx.roadName.empty() ? ctx.roadName : tr(demo->active ? "demo" : "free", lang);
    const auto km = [&](double m) { return formatUnit(std::max(0.0, m) / 1000, Unit::Kilometer, lang, 1); };
    const std::string where = demo->tour ? tr("tourRunning", lang, {{"done", km(v.odometer - demo->tour->odo)}, {"total", km(demo->tour->metres)}})
                                         : tr("guideRunning", lang, {{"n", demo->index + 1}, {"max", demo->stops.size()}});
    hs.navSub = !demo->active            ? where
                : !demo->remark.empty() ? tr("demo." + demo->remark, lang)
                : demo->tour            ? where + " · " + tr("demoTakeOver", lang)
                                        : tr("demoRunning", lang, {{"n", demo->index + 1}, {"max", demo->stops.size()}});
  } else if (navText.empty() && !lesson) {
    hs.navText = !ctx.roadName.empty() ? ctx.roadName : tr("free", lang);
    hs.navSub = tr(touch ? "navSubTouch" : "navSubKeys", lang);
    hs.navArrow = 0;
  }
  // (later: a lesson watched: the instructor says what the car does – game.ts:7082; the tram's line of the display – game.ts:7083;
  //  on the bus, riding along, on foot in the town, on foot round the vehicle – game.ts:7132; getting out is offered – game.ts:7200;
  //  a passenger, the buttons beside the person on foot – game.ts:7205; flying – game.ts:7217; watching – game.ts:7242;
  //  services.hud, crane.hud, forklift.hud – game.ts:7247)
  hud->update(hs, time);
}

void Game::speak(const std::string& what, double dist, const std::string& text) {
  if (!settings.voice || !speech.supported()) return;
  // announce each manoeuvre twice: early and right before it
  const std::string stage = dist < 0 ? "" : dist < 45 ? "now" : dist < 260 ? "soon" : "";
  const std::string key = what + ":" + stage + ":" + std::to_string(targetIdx);
  if (stage.empty() || what == "navFollow" || key == lastNavSpoken) return;
  lastNavSpoken = key;
  // (the voice, the text made speakable, the queue: audio/speech.ts)
  speech.say(text, settings.lang, settings);
}

void Game::updateCamera(double dt) {
  // (later: footGlance, the head turned by the glance keys on foot – game.ts:7271)
  Camera& cam = *app.camera;
  // zooming the view (input/camzoom.ts): on foot and in the driver's seat – pinch, the wheel, + and −
  // (later: watcher, crane, doorT, opens, walker – game.ts:7277)
  const bool zoomable = cameraMode == "cockpit";
  input->viewZoom = zoomable;
  input->zoom.update(dt, settings.zoomRelease, zoomable);
  // (later: watcher.camera – game.ts:7280)
  // the developer's view (?cam=… or devView()): a camera standing anywhere – for comparing the world with photos
  if (cameraMode == "dev") {
    cam.near = 0.3;
    cam.fov = devFov;
    cam.position.copy(free.pos);
    cam.rotation.set(free.pitch, free.yaw, 0, Euler::YXZ);
    cam.updateProjectionMatrix();
    return;
  }
  Group& car = *model().root;
  Vehicle& v = *vehicle;
  const Controls& c = input->c;
  // the eye is always needed for the mirrors
  Vector3& lean = tmpV.set(-v.accLat * 0.004, 0, v.accLong * 0.005);
  lean.clampLength(0, 0.06);
  eyeWorld.copy(model().eye).add(lean).applyMatrix4(car.matrixWorld);
  // (later: working a crane – game/craneplay.ts, game.ts:7300)
  // (later: on foot, on the bus, a passenger, getting in and out – game.ts:7306)
  if (cameraMode == "cockpit") {
    cam.near = 0.06;
    cam.fov = 60;
    cam.position.copy(eyeWorld);
    // looking over the shoulder also moves the head a little towards that side
    // (on a demonstration drive the head turns as the driver looks over the shoulder – not in a jerk)
    // (later: lessonWatch – game.ts:7372)
    const double want = demo && demo->active ? demo->look * 0.8 : 0;
    demoHead += std::max(-dt * 5, std::min(dt * 5, want - demoHead));
    // (a rider looks a little down the road: the instruments, the bar and the mirrors in view)
    // (later: the aeroplane's – game.ts:7375)
    PlayerBike* bike = dynamic_cast<PlayerBike*>(&model());
    const double gaze = bike ? bike->gaze : 0;
    tmpQ.setFromEuler(Euler(c.lookPitch - gaze, c.lookYaw + demoHead, 0, Euler::YXZ));
    cam.quaternion.copy(car.quaternion).multiply(tmpQ);
    if (bike) {
      // the rider leans with the motorcycle: the eye swings round the line where the tyres touch the road
      Quaternion bikeRoll;
      bikeRoll.setFromAxisAngle(Vector3(0, 0, 1), -bike->leaning());
      eyeWorld.copy(bike->eye).applyQuaternion(bikeRoll).applyMatrix4(car.matrixWorld);
      cam.position.copy(eyeWorld);
      cam.quaternion.copy(car.quaternion).multiply(bikeRoll).multiply(tmpQ);
    }
  } else if (cameraMode == "chase") {
    cam.near = 0.3;
    cam.fov = 55;
    // The camera circles a point above the car: the mouse (or a finger) turns it all the way
    // round and from road level up to a bird's view, the wheel changes the distance.
    // (With a trailer it circles the coupling – the middle of the combination – and looks from
    // higher up, so that the trailer does not hide the road and its end can be watched.)
    const bool long_ = def->long_;
    // (later: task.run.judge – reversing on the yard, game.ts:7395)
    const bool reversing = false;
    const double lift = std::min(1.5, std::max(0.03, (reversing ? 1.05 : long_ ? 0.42 : 0.2) - c.lookPitch));
    const double reach = c.lookZoom * std::cos(lift);
    const double up = long_ ? 2.2 : 1.1;
    const double behind = reversing ? 9.5 : long_ ? 4.2 : 0;
    const double fx = car.position.x + std::sin(v.yaw) * behind;
    const double fz = car.position.z + std::cos(v.yaw) * behind;
    Vector3& back = tmpV.set(std::sin(c.lookYaw) * reach, up + c.lookZoom * std::sin(lift), std::cos(c.lookYaw) * reach).applyAxisAngle(Vector3(0, 1, 0), v.yaw);
    // (the camera's place relative to the car is what is smoothed – not its place in the world: following a
    // moving point by a share of the way per picture left it trailing by more or less with every uneven
    // picture, a quick little zoom in and out while driving)
    if (camSnap) chaseOff.copy(back);
    else chaseOff.lerp(back, 1 - std::exp(-dt * 8));
    Vector3& target = camTarget.set(fx, car.position.y, fz).add(chaseOff);
    const double g = app.tiles->heightAt(target.x, target.z);
    if (!std::isnan(g)) target.y = std::max(target.y, g + 0.6);
    cam.position.copy(target);
    camSnap = false;
    cam.lookAt(fx, car.position.y + up, fz);
  } else if (cameraMode == "free" && flyFree()) {
    flyCamera(cam, dt);
  } else if (cameraMode == "free") {
    cam.near = 0.3;
    cam.fov = 55;
    Fc& fc = this->fc;
    const Input::Cam& t = input->takeCam();
    const bool long_ = def->long_;
    if (!fc.ready) {
      // from where the camera is now, looking the way it looks
      fc.ready = true;
      fc.off.copy(cam.position).sub(car.position);
      const Vector3& dir = cam.getWorldDirection(tmpV);
      fc.yaw = std::atan2(-dir.x, -dir.z);
      fc.pitch = std::asin(std::max(-1.0, std::min(1.0, dir.y)));
      // (from the driver's seat, or from nowhere in particular: start behind the car instead)
      if (fc.off.length() < 2.5 || fc.off.length() > 400) fc.ready = false;
    }
    if (!fc.ready || t.reset) {
      // behind the car, a little above it
      fc.ready = true;
      const double back = long_ ? 20 : 8;
      fc.off.set(std::sin(v.yaw) * back, long_ ? 7 : 3.2, std::cos(v.yaw) * back);
      fc.yaw = v.yaw;
      fc.pitch = -0.28;
    }
    fc.yaw += t.yaw;
    fc.pitch = std::max(-1.5, std::min(1.3, fc.pitch + t.pitch));
    const double sy = std::sin(fc.yaw);
    const double cy = std::cos(fc.yaw);
    const double cp = std::cos(fc.pitch);
    const double sp = std::sin(fc.pitch);
    TileManager& tiles = *app.tiles;
    double ground = tiles.heightAt(car.position.x + fc.off.x, car.position.z + fc.off.z);
    if (std::isnan(ground)) ground = car.position.y;
    // what a drag or a key moves it by grows with the height: metres near the ground, a block from high up
    const double reach = std::max(4.0, std::min(220.0, (car.position.y + fc.off.y - ground) * 1.6 + 3));
    const double speed = (t.fast ? 4 : 1) * std::max(5.0, std::min(90.0, reach * 0.9));
    const double right = t.right * reach + t.keyRight * speed;
    const double fwd = t.fwd * reach + t.keyFwd * speed;
    const double dolly = t.dolly * reach;
    fc.off.x += cy * right - sy * fwd - sy * cp * dolly;
    fc.off.z += -sy * right - cy * fwd - cy * cp * dolly;
    fc.off.y += sp * dolly + t.keyUp * speed * 0.7;
    // it stays within the part of the map that is loaded around the car, and above the ground
    const double far = std::min(900.0, tiles.viewRadius * 0.7);
    const double hd = std::hypot(fc.off.x, fc.off.z);
    if (hd > far) {
      fc.off.x *= far / hd;
      fc.off.z *= far / hd;
    }
    ground = tiles.heightAt(car.position.x + fc.off.x, car.position.z + fc.off.z);
    if (std::isnan(ground)) ground = car.position.y;
    fc.off.y = std::max(ground + 0.4, std::min(ground + 600, car.position.y + fc.off.y)) - car.position.y;
    cam.position.copy(car.position).add(fc.off);
    camSnap = false;
    cam.quaternion.setFromEuler(Euler(fc.pitch, fc.yaw, 0, Euler::YXZ));
  } else {
    cam.near = 0.2;
    cam.fov = 60;
    Free& f = free;
    const double speed = (keys.contains("ShiftLeft") ? 80 : 12) * dt;
    if (keys.contains("Numpad8")) f.pos.z -= speed;
    if (keys.contains("Numpad2")) f.pos.z += speed;
    if (keys.contains("Numpad4")) f.pos.x -= speed;
    if (keys.contains("Numpad6")) f.pos.x += speed;
    if (freeRelative) {
      cam.position.copy(f.pos).applyQuaternion(car.quaternion).add(car.position);
      tmpQ.setFromEuler(Euler(f.pitch, f.yaw, 0, Euler::YXZ));
      cam.quaternion.copy(car.quaternion).multiply(tmpQ);
    }
  }
  // (later: services.camera – game.ts:7486)
  // (1 – nothing – unless the view is zoomed: only on foot and in the driver's seat)
  cam.fov = input->zoom.fov(cam.fov);
  cam.updateProjectionMatrix();
}

std::string Game::debugInfo() {
  Vehicle* v = vehicle.get();
  const auto fixed1 = [](double x) { return std::round(x * 10) / 10; };
  JsonWriter w;
  w.beginObject();
  w.key("state");
  w.string(state);
  w.key("tiles");
  w.number(static_cast<double>(app.tiles->tiles.size()));
  w.key("frames");
  w.number(frames);
  w.key("cars");
  w.number(traffic ? static_cast<double>(traffic->cars.size()) : 0);
  w.key("kmh");
  w.number(v ? fixed1(v->speed * 3.6) : 0);
  w.key("rpm");
  w.number(v ? std::round(v->rpm) : 0);
  w.key("pos");
  if (v) {
    w.beginArray();
    for (const double x : mpos) w.number(fixed1(x));
    w.endArray();
  } else {
    w.null();
  }
  w.key("faults");
  w.beginArray();
  if (rules)
    for (const Violation& x : rules->log) w.string(x.id);
  w.endArray();
  w.key("missingSigns");
  w.beginArray();
  if (signs)
    for (const std::string& m : signs->missing) w.string(m);
  w.endArray();
  w.key("calls");
  w.number(app.renderer.info.calls);
  w.key("tris");
  w.number(app.renderer.info.triangles);
  // (later: reality.debugInfo – game.ts:7506)
  w.key("reality");
  w.null();
  // the road network that is loaded (it may come in pieces around the car), and whether the roads of the whole map are known
  w.key("roads");
  if (app.world) {
    w.beginObject();
    w.key("pieces");
    w.boolean(app.map->pieces);
    w.key("edges");
    w.number(static_cast<double>(app.world->allEdges.size()));
    w.key("nodes");
    w.number(static_cast<double>(app.world->allNodes.size()));
    w.key("whole");
    w.boolean(netIndex != nullptr);
    w.endObject();
  } else {
    w.null();
  }
  w.key("trip");
  if (demo) {
    w.beginObject();
    w.key("auto");
    w.boolean(demo->active);
    w.key("tour");
    w.number(demo->tour ? std::round(demo->tour->metres) : 0);
    w.key("route");
    w.number(nav->route ? static_cast<double>(nav->route->size()) : -1);
    w.key("left");
    w.number(nav->left());
    w.endObject();
  } else {
    w.null();
  }
  // 2.0: where a frame's time goes (ms a frame, averaged while driving)
  {
    const FrameTimes& f = frameTimes;
    const double n = f.frames > 0 ? f.frames : 1;
    w.key("ms");
    w.beginObject();
    for (const auto& [k, v] : std::initializer_list<std::pair<const char*, double>>{
             {"sim", f.sim}, {"visuals", f.visuals}, {"scene", f.scene}, {"mirrors", f.mirrors}, {"render", f.render}, {"total", f.total}}) {
      w.key(k);
      w.number(fixed1(v / n));
    }
    w.key("frames");
    w.number(f.frames);
    w.endObject();
  }
  w.endObject();
  return w.take();
}

}  // namespace fs
