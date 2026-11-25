// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (on foot: round the vehicle, out and in – game.ts:4594–4740, 4865–4955, 5053–5112;
// the tower crane's door – game.ts:4519–4560; tap to walk, stepWalk – game.ts:5380–5413; on foot as a pedestrian – game.ts:5414–5838)
#include <algorithm>
#include <cmath>

#include "audio/audio.h"
#include "core/geom.h"
#include "core/js.h"
#include "game/address.h"
#include "game/cardoors.h"
#include "game/craneplay.h"
#include "game/game.h"
#include "game/onfoot.h"
#include "game/settings.h"
#include "game/spawncar.h"
#include "game/vehicles.h"
#include "game/walkmodes.h"
#include "i18n/index.h"
#include "input/input.h"
#include "platform/intl.h"
#include "render/bike.h"
#include "render/truck.h"
#include "sim/collision.h"
#include "sim/flight.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/trailer.h"
#include "sim/vehicle/vehicle.h"
#include "ui/hud.h"
#include "world/lanes.h"
#include "world/loader.h"
#include "world/pedlights.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

const std::string& checkTextKey(const CheckId& id) {
  static const auto* text = new std::map<CheckId, std::string>{{"tyres", "checkTyres"}, {"front", "checkFront"}, {"rear", "checkRear"}, {"coupling", "checkCoupling"}};
  return text->at(id);
}

namespace {

/** "Licht, Reifen und Spiegel": what was not checked, as the language writes a list */
std::string missingList(const OnFoot& w, const std::string& lang) {
  std::vector<std::string> items;
  for (const CheckId& id : w.missing()) items.push_back(tr(checkTextKey(id), lang));
  return plat::formatList(items, localeOf(lang), {.style = "long", .type = "conjunction"});
}

}  // namespace

// ------------------------------------------------- on foot: round the vehicle, out and in

WalkVehicle Game::walkVehicle() {
  Vehicle& v = *vehicle;
  v.getModelPosition(mpos.data());
  WalkVehicle out;
  if (tram) {
    // the tram: its cars in the way, the door of the cab at the front on the right
    const std::vector<CarRole>& roles = carLayout(*tram->v.consist);
    const double hw = tram->v.consist->width / 2;
    for (size_t i = 0; i < tram->v.cars.size(); i++) {
      const RailCar& c = tram->v.cars[i];
      WalkUnit wu;
      wu.x = c.x;
      wu.z = c.z;
      wu.yaw = c.yaw;
      wu.front = roles[i].len / 2;
      wu.rear = roles[i].len / 2;
      wu.hw = hw;
      out.units.push_back(wu);
    }
    out.y = tram->v.cars[0].y + 0.3;
    out.door = roles[0].len / 2 - 1.4;
    out.doorSide = 1;
    return out;
  }
  const VehicleDef& d = *def;
  WalkUnit u;
  u.x = mpos[0];
  u.z = mpos[2];
  u.yaw = v.yaw;
  u.front = d.outline.front;
  u.rear = d.outline.rear;
  u.hw = d.outline.hw;
  u.axles = d.axles;
  u.track = d.track;
  out.units.push_back(u);
  const Trailer* t = trailer.get();
  const std::optional<TrailerOutline> o = trailerOf(d);
  if (t && t->coupled && o && d.trailer) {
    WalkUnit tu;
    tu.x = t->x;
    tu.z = t->z;
    tu.yaw = t->yaw;
    tu.front = o->front;
    tu.rear = o->rear;
    tu.hw = o->hw;
    tu.axles = d.trailer->axleSpread > 0 ? std::vector<double>{d.trailer->axleSpread / 2, -d.trailer->axleSpread / 2} : std::vector<double>{0};
    tu.track = d.trailer->track / 2;
    out.units.push_back(tu);
    out.hitch = WalkVehicle::Point{t->hitchX, t->hitchY, t->hitchZ};
  }
  out.y = mpos[1];
  out.door = d.door;
  out.doorSide = d.doorSide != 0 ? d.doorSide : -1;
  return out;
}

void Game::getOut() {
  Vehicle& v = *vehicle;
  Controls& c = input->c;
  const std::string& lang = settings.lang;
  if (walker || doorT >= 0 || opens.busy() || (demo && demo->active) || task) return;
  // (later: out of the aeroplane: leavePlane – game.ts:4649)
  if (tram) {
    // out of the tram's cab: it stands braked meanwhile (the passengers' doors as they are)
    if (tram->speed() > 0.1 || tramAuto) return;
    walkStart = false;
    startDoor("out");
    return;
  }
  if (std::abs(v.speed) > 0.3) {
    tipText = tr("stopFirst", lang);
    tipUntil = time + 3;
    return;
  }
  if (&model() == &car && opens.passenger()) {
    // a passenger: out through the door of that seat (what the driver has to see to is not the passenger's)
    walkStart = false;
    opens.beginOut();
    return;
  }
  v.getModelPosition(mpos.data());
  const double x = mpos[0];
  const double z = mpos[2];
  // (a motorcycle has no parking brake to set: the engine off is enough)
  if (!settings.arcadeDrive && ((c.handbrake < 0.5 && def->doorSide != 0) || v.engineRunning)) rules->noteAt("notSecured", "minor", x, z);
  // what comes from behind on the side where the door opens: within four seconds of it (a motorcycle has no door)
  const double side = def->doorSide;
  const double s = std::sin(v.yaw);
  const double co = std::cos(v.yaw);
  bool coming = false;
  struct Mover {
    double x, z, v, yaw;
  };
  std::vector<Mover> movers;
  for (const TrafficCar* k : traffic->cars) movers.push_back({k->x, k->z, k->v, k->yaw});
  if (traffic->cyclists)
    for (const Cyclist* k : traffic->cyclists->list) movers.push_back({k->x, k->z, k->v, k->yaw});
  for (const Mover& m : movers) {
    const double dx = m.x - x;
    const double dz = m.z - z;
    const double ahead = -dx * s - dz * co;
    const double right = dx * co - dz * s;
    const double along = std::cos(m.yaw - v.yaw);
    if (right * side > 0.4 && right * side < 4.5 && ahead < 3 && ahead > -60 && along > 0.5 && m.v > 1 && (0.5 - ahead) / m.v < 4) coming = true;
  }
  // (only a door on the left opens into the road: the front door of a bus opens towards the kerb,
  // a motorcycle has none)
  const bool looked = time - lookedBackAt < 5;
  if (side < 0 && coming) rules->noteAt("doorTraffic", "major", x, z);
  else if (side < 0 && !looked) rules->noteAt("doorCheck", "hint", x, z);
  // (whatever was done: the vehicle does not roll away)
  c.handbrake = 1;
  c.seatbelt = false;
  parkedSteer = c.steer;
  walkStart = false;
  // (the car: its door opens, no fade – game/openplay.ts)
  if (&model() == &car) opens.beginOut();
  else startDoor("out");
}

void Game::getIn() {
  OnFoot* w = walker.get();
  const std::string& lang = settings.lang;
  if (!w || doorT >= 0 || opens.busy()) return;
  if (&model() == &car) {
    // the car: into the seat of the door one stands at (at the bonnet or the boot: open or shut it)
    const std::optional<OpeningSpot> at = opens.at(w);
    if (at && !isDoor(at->id)) {
      opens.toggleAt(w);
      return;
    }
    if (!at || !isDoor(at->id)) {
      tipText = tr("toDoor", lang);
      tipUntil = time + 3;
      return;
    }
    if (at->id == "fl" && walkStart && !w->allChecked()) rules->noteAt("preCheck", "hint", mpos[0], mpos[2], {{"missing", missingList(*w, lang)}}, 30);
    opens.beginIn(at->id);
    return;
  }
  if (!w->atDoor()) {
    tipText = tr("toDoor", lang);
    tipUntil = time + 3;
    return;
  }
  // a drive that began on foot: was the vehicle looked at?
  if (walkStart && !w->allChecked()) rules->noteAt("preCheck", "hint", mpos[0], mpos[2], {{"missing", missingList(*w, lang)}}, 30);
  startDoor("in");
}

void Game::startOnFoot() {
  Controls& c = input->c;
  c.handbrake = 1;
  c.seatbelt = false;
  vehicle->resetDrivetrain(false);
  parkedSteer = 0;
  walkStart = true;
  enterWalk();
}

void Game::enterWalk() {
  const WalkVehicle wv = walkVehicle();
  auto w = std::make_unique<OnFoot>(wv);
  // (where its parts open; out of the car through another door: beside that one)
  opens.walk(*w, wv, &model() == &car ? &car : nullptr, dynamic_cast<PlayerTruck*>(&model()) != nullptr);
  w->y = footGround(w->x, w->z, mpos[1] + 1);
  walker = std::move(w);
  // (nobody sits on the motorcycle while its rider walks round it)
  if (PlayerBike* bike = dynamic_cast<PlayerBike*>(&model())) bike->setRider(false);
  walkSince = time;
  input->resetLook(false, true);
  markers->show(walker->spots, [this](double x, double z) { return footGround(x, z, mpos[1] + 1); });
}

void Game::leaveWalk() {
  walker = nullptr;
  figure->hide();
  if (PlayerBike* bike = dynamic_cast<PlayerBike*>(&model())) bike->setRider(true);
  if (def->model == "motorcycle") input->c.seatbelt = true;
  markers->hide();
  cameraMode = "cockpit";
  input->resetLook(false, false);
}

void Game::startDoor(const std::string& way) {
  doorWay = way;
  doorT = 0;
  doorFrom.pos.copy(app.camera->position);
  doorFrom.quat.copy(app.camera->quaternion);
  audio->click("door");
  fadeEl.addClass("on");
}

void Game::stepDoor(double dt) {
  if (doorT < 0) return;
  const double before = doorT;
  doorT += dt;
  if (before < 0.35 && doorT >= 0.35) {
    if (doorWay == "out") enterWalk();
    else leaveWalk();
  }
  if (before < 0.6 && doorT >= 0.6) {
    audio->click("doorShut");
    fadeEl.removeClass("on");
  }
  if (doorT >= 1) doorT = -1;
}

bool Game::walkerAway() {
  const OnFoot* w = walker.get();
  // (later: wrecked, riding, busRide – game.ts:3128)
  return w && def->kind != "pedestrian" && std::hypot(w->x - mpos[0], w->z - mpos[2]) > TETHER;
}

// ------------------------------------------------- the tower crane: its operator climbs down, and up again

void Game::towerDoor() {
  CraneWork* cw = crane.get();
  if (!cw || cw->type != "tower" || state != "drive") return;
  if (const OnFoot* w = walker.get()) {
    if (!cw->atTowerFoot(w->x, w->z)) {
      tipText = tr("craneTooFar", settings.lang);
      tipUntil = time + 3;
      return;
    }
    walker = nullptr;
    figure->hide();
    cw->setAway(false);
    cameraMode = "cockpit";
    input->resetLook(false, false);
    input->c.lookZoom = 22;
    camSnap = true;
    audio->click("door");
    return;
  }
  vehicle->getModelPosition(mpos.data());
  const double y0 = mpos[1];
  const CranePlace at = cw->towerExit([this, y0](double x, double z) {
    const double y = footGround(x, z, y0 + 2);
    return std::isfinite(y) && !collider->circleBlocked(x, y, z, 0.4);
  });
  cw->setAway(true);
  leaveOnFoot(at.x, at.z, at.yaw, y0);
}

void Game::leaveOnFoot(double x, double z, double yaw, double yRef) {
  const double y = footGround(x, z, yRef + 2);
  walker = std::make_unique<OnFoot>(std::nullopt, OnFoot::Start{x, std::isfinite(y) ? y : yRef, z, yaw});
  walker->runSpeed = settings.runSpeed / 3.6;
  walkSince = time;
  cameraMode = "cockpit";
  input->resetLook(false, true);
  camSnap = true;
  audio->click("door");
}

// ------------------------------------------------- a car here (game/spawncar.ts)

std::unique_ptr<SpawnCars> Game::makeSpawnCars() {
  SpawnCarHost h;
  h.scene = app.scene;
  h.camera = app.camera;
  h.canvas = app.canvas;
  h.ui = ui;
  h.collider = collider.get();
  h.heightAt = [this](double x, double z) { return app.tiles->heightAt(x, z); };
  h.ground = [this](double x, double z) {
    const double ty = app.tiles->heightAt(x, z);
    return app.world->surfaceAt(x, z, ty + 3, hit) ? hit.y : ty;
  };
  h.canSpawn = [this] { return canSpawnCar(); };
  h.canEnter = [this] { return canEnterParked(); };
  h.player = [this] {
    if (const OnFoot* w = walker.get()) return SpawnCarPlace{w->x, w->z, w->yaw};
    vehicle->getModelPosition(mpos.data());
    return SpawnCarPlace{mpos[0], mpos[2], vehicle->yaw};
  };
  h.others = [this] {
    std::vector<SpawnCarCircle> out;
    if (const OnFoot* w = walker.get()) out.push_back({w->x, w->z, 0.5});
    // (later: !wrecked, !tram – game.ts:4897)
    if (state == "drive") {
      if (aircraft) out.push_back({aircraft->pos.x, aircraft->pos.z, 6});
      else {
        // (the player's vehicle: its circles along its axis, as the collider has them)
        vehicle->getModelPosition(mpos.data());
        const double s = std::sin(vehicle->yaw);
        const double c = std::cos(vehicle->yaw);
        for (const double o : def->circles) out.push_back({mpos[0] + s * o, mpos[2] + c * o, def->radius});
      }
    }
    return out;
  };
  h.roadSpot = [this](double x, double z, double d) -> std::optional<SpawnCarPlace> {
    if (!app.map->ready(x, z, d + 60)) return std::nullopt;
    const std::optional<SpawnJson> sp = findRoadSpot(*app.world, *lanes, x, z, "", d, usableLane());
    if (!sp) return std::nullopt;
    return SpawnCarPlace{sp->x, sp->z, sp->yaw};
  };
  h.exact = [this] { return settings.placeExact; };
  h.paint = [this] { return static_cast<int>(car.exterior.paint->color.getHex()); };
  h.carOutline = VEHICLES.at("car").outline;
  h.enter = [this](Parked& p) { enterParked(p); };
  h.tip = [this](const std::string& text) {
    tipText = text;
    tipUntil = time + 5;
  };
  h.t = [this](const std::string& k) { return tr(k, settings.lang); };
  return std::make_unique<SpawnCars>(std::move(h));
}

bool Game::canSpawnCar(bool fromMap) {
  if (!(fromMap ? state == "map" && mapFrom != "menu" : state == "drive") || lesson || task || (demo && demo->active) || watcher || forklift || showOn) return false;
  // (later: riding, busRide – game.ts:4923)
  return walker != nullptr || (crane && crane->operating);
}

bool Game::canEnterParked() {
  // (later: riding, busRide – game.ts:4928)
  if (state != "drive" || lesson || task || (demo && demo->active) || watcher || doorT >= 0 || opens.busy()) return false;
  if (walker || (crane && crane->operating)) return true;
  return !aircraft && !tram && !forklift && std::abs(vehicle->speed) < 0.5;
}

void Game::enterParked(const Parked& p_) {
  if (!canEnterParked()) return;
  // (the parked vehicle is gone from the module once it is got into: what is needed of it is kept here)
  const Parked p = p_;
  const VehicleDef& d = *def;
  vehicle->getModelPosition(mpos.data());
  // (later: wrecked – game.ts:4943)
  const bool stays = !aircraft && !tram && d.kind != "pedestrian" && d.kind != "forklift" && !d.trailer;
  const double height = d.model == "car" || d.model == "motorcycle" ? 1.6 : 3.6;
  if (stays) {
    LeftVehicle from;
    from.kind = settings.vehicle;
    from.x = mpos[0];
    from.y = mpos[1];
    from.z = mpos[2];
    from.yaw = vehicle->yaw;
    from.outline = d.outline;
    from.height = height;
    from.root = d.model == "car" ? nullptr : ObjectPtr(model().root);
    spawnCars->swap(&p_, &from);
  } else spawnCars->swap(&p_, nullptr);
  endWatch(false);
  if (demo) endDemo("ended");
  settings.vehicle = p.kind;
  saveSettings(settings);
  parkedAt = Place{p.x, p.z, p.yaw};
  intoParked = true;
  const AddressHit at{.label = tr("mapPlace", settings.lang), .street = "", .x = p.x, .z = p.z, .exact = true};
  begin("free", 0, true, &at);
}

// ------------------------------------------------- tap to walk, a step on foot

void Game::tapToWalk(double x, double z) {
  OnFoot* w = walker.get();
  // (later: riding, busRide – game.ts:5382)
  if (!w || state != "drive" || doorT >= 0) return;
  if (walkModeOf(settings.walkControls, inputMode == "touch") != "tap" || std::hypot(x - w->x, z - w->z) > 400) return;
  std::vector<FootRoad> roads;
  app.world->edgesInBox(std::min(x, w->x) - 30, std::min(z, w->z) - 30, std::max(x, w->x) + 30, std::max(z, w->z) + 30, [&roads](Edge& e) {
    if (!LaneGraph::drivable(e, false)) return;
    FootRoad r;
    r.pl = &e.pl;
    r.half = e.width / 2;
    r.swL = e.swL;
    r.swR = e.swR;
    for (const CrossingJson& c : e.xings)
      if (c.k == XING_SIGNAL || c.k == XING_ZEBRA) r.xings.push_back(c.s);
    roads.push_back(std::move(r));
  });
  walkCtl.goTo(planFootPath(w->x, w->z, x, z, roads));
}

void Game::stepWalk(double dt) {
  OnFoot* w = walker.get();
  if (!w) return;
  const Input::Cam& cam = input->takeCam();
  const WalkLook look{cam.yaw, cam.pitch, cam.right, cam.fwd};
  // (the way of walking chosen in the settings, a way being walked after a tap: game/walkmodes.ts)
  const WalkMode mode = walkModeOf(settings.walkControls, inputMode == "touch");
  const Input::Walk want = input->walkInput(mode);
  const WalkStep step = walkCtl.step(dt, WalkIntent{want.fwd, want.side, want.turn, want.run}, WalkControl::At{w->x, w->z, w->yaw}, look.yaw != 0 || look.pitch != 0);
  if (walkCtl.blocked) {
    walkCtl.blocked = false;
    tipText = tr("walk.blocked", settings.lang);
    tipUntil = time + 3;
  }
  const WalkMove move{step.fwd, step.turn, step.run, step.side};
  const OnFoot::Result r = w->update(
      dt, move, look, [this](double x, double z, double yRef) { return footGround(x, z, yRef); },
      [this](double x, double y, double z, double rad) { return collider->circleBlocked(x, y, z, rad); });
  if (r.step) audio->click("step");
  if (r.checked) {
    audio->click("tick");
    markers->update(w->spots);
    if (w->allChecked()) {
      tipText = tr("checkDone", settings.lang);
      tipUntil = time + 5;
    }
  }
}

// ------------------------------------------------- on foot, as a pedestrian

void Game::startPedestrian() {
  Vehicle& v = *vehicle;
  v.getModelPosition(mpos.data());
  const double yaw = v.yaw;
  const double rx = std::cos(yaw);
  const double rz = -std::sin(yaw);
  double x = mpos[0];
  double z = mpos[2];
  for (double d = 1.5; d <= 16; d += 0.5) {
    const double px = mpos[0] + rx * d;
    const double pz = mpos[2] + rz * d;
    const bool road = app.world->surfaceAt(px, pz, mpos[1] + 2, hit) && hit.zone == 0;
    const double y = footGround(px, pz, mpos[1] + 1);
    if (!road && std::isfinite(y) && !collider->circleBlocked(px, y, pz, 0.35)) {
      x = px;
      z = pz;
      break;
    }
  }
  auto w = std::make_unique<OnFoot>(std::nullopt, OnFoot::Start{x, footGround(x, z, mpos[1] + 1), z, yaw});
  w->runSpeed = settings.runSpeed / 3.6;
  walker = std::move(w);
  walkSince = time;
  // (later: riding – game.ts:5563)
  footCrossing = nullptr;
  footOnRoad = false;
  footAlong = 0;
  cameraMode = "cockpit";
  input->resetLook(false, true);
  v.teleport(x, walker->y, z, yaw);
  v.getModelPosition(mpos.data());
}

void Game::stepFoot(double dt) {
  OnFoot* w = walker.get();
  if (!w) return;
  // (later: on the bus, riding a tram as a passenger: taken along, looking round – busRide, riding, alightBus, alight,
  //  passengerView – transit, game.ts:5579)
  stepWalk(dt);
  // the game's vehicle stands where the person is: the camera, the map, the traffic keep to it
  vehicle->teleport(w->x, w->y, w->z, w->yaw);
  vehicle->getModelPosition(mpos.data());
  rules->time += dt;
  signals->update(dt);
  // (later: riding, the passenger is no obstacle to anybody: the vehicle around them is – game.ts:5616)
  const bool riding = false;
  PlayerState player;
  player.x = w->x;
  player.y = w->y;
  player.z = w->z;
  player.yaw = w->yaw;
  player.speed = w->speed;
  player.length = riding ? 0.1 : 0.6;
  player.width = riding ? 0.1 : 0.6;
  player.onFoot = !riding;
  stepRail(dt, player, true);
  railBoxes.clear();
  for (const auto& rv : rail->vehicles) {
    const std::vector<CarRole>& roles = carLayout(*rv->consist);
    for (size_t i = 0; i < rv->cars.size(); i++) {
      const RailCar& rc = rv->cars[i];
      PlayerState b;
      b.x = rc.x;
      b.y = rc.y;
      b.z = rc.z;
      b.yaw = rc.yaw;
      b.speed = rv->speed;
      b.length = roles[i].len;
      b.width = rv->consist->width;
      railBoxes.push_back(b);
    }
  }
  traffic->obstacles.clear();
  for (PlayerState& b : railBoxes) traffic->obstacles.push_back(&b);
  traffic->update(dt, player);
  const std::string& lang = settings.lang;
  for (const TrafficEvent& e : traffic->events) {
    if (e.type == "collision") rules->noteAt("pedHit", "fail", w->x, w->z, {}, 6);
    else if (e.type == "forcedBrake") rules->noteAt("pedForcedBrake", "major", w->x, w->z, {}, 10);
  }
  if (!riding) {
    // a tram does not give way: whoever stands on its track is hit
    if (rail->touching(RailPlayer{player.x, player.y, player.z, player.yaw, player.length, player.width})) rules->noteAt("pedHit", "fail", w->x, w->z, {}, 6);
    footRules(dt, *w);
    // what is within reach: a button  (later: a tram to get in, a bus at its stop – boardable, busAt, game.ts:5638)
    footPole = poleInReach(pedPoles, w->x, w->z);
  } else footPole = nullptr;
  if (failT < 0 && rules->failed()) {
    failT = time + 2.5;
    tipText = tr("pedHitTip", lang);
    tipUntil = time + 4;
  }
  if (failT > 0 && time > failT) finish();
}

void Game::footRules(double dt, OnFoot& w) {
  World& world = *app.world;
  const bool onRoad = world.surfaceAt(w.x, w.z, w.y + 1, hit) && hit.zone == 0 && hit.edge != nullptr;
  if (!onRoad || !hit.edge) {
    footOnRoad = false;
    footCrossing = nullptr;
    footAlong = std::max(0.0, footAlong - dt * 2);
    return;
  }
  const Edge& e = *hit.edge;
  // where along the road, and which crossing (if any) the person is on
  PolyProjection& pr = proj;
  projectOnPolyline(e.pl, w.x, w.z, pr);
  const CrossingJson* at = nullptr;
  const CrossingJson* near = nullptr;
  for (const CrossingJson& c : e.xings) {
    if (c.k != XING_SIGNAL && c.k != XING_ZEBRA && c.k != XING_UNMARKED) continue;
    const double d = std::abs(pr.s - c.s);
    if (d <= c.w / 2 + 1) at = &c;
    else if (d < 30 && (c.k == XING_SIGNAL || c.k == XING_ZEBRA)) near = &c;
  }
  const bool stepping = !footOnRoad;
  footOnRoad = true;
  if (stepping) {
    footCrossing = at;
    // onto the road against the red light (once green turns red on the way, one goes on – that is allowed)
    if (at && at->k == XING_SIGNAL && at->sc && signals->pedestrian(*at->sc, at->g.value_or(0)) == "red") rules->noteAt("pedRed", "major", w.x, w.z, {}, 15);
    // across the road a few steps from a crossing (or a light) that is not used
    else if (!at && near) rules->noteAt("pedCrossing", "minor", w.x, w.z, {}, 20);
  }
  // along the road rather than across it, where there is a pavement
  samplePolyline(e.pl, pr.s, smp);
  const double along = std::abs(std::cos(w.yaw) * -smp.tz + std::sin(w.yaw) * -smp.tx) * w.speed;
  footAlong += along * dt;
  if (footAlong > 25 && (e.swL > 0 || e.swR > 0)) {
    rules->noteAt("pedRoad", "minor", w.x, w.z, {}, 30);
    footAlong = 0;
  }
}

void Game::footAction() {
  const std::string& lang = settings.lang;
  // (later: off the bus, onto a bus at its stop, out of the tram at a stop, into a tram standing at a stop –
  //  busRide, footBus, riding, footBoard: alightBus, board, alight – transit, game.ts:5791)
  if (footPole) {
    const PedPole& p = *footPole;
    const std::string st = signals->pedestrian(p.controller, p.group);
    if (st == "dark") tipText = tr("pedLightDark", lang);
    else if (st == "green") tipText = tr("pedLightGreen", lang);
    else {
      signals->request(p.controller);
      audio->click("tick");
      tipText = tr("pedRequested", lang);
    }
    tipUntil = time + 4;
  }
}

}  // namespace fs
