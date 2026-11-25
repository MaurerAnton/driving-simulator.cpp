// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Game: watching a tram or bus of the timetables (game/spectate.h), anything
 * else the map shows or a tap chooses (game/pointwatch.h), taking it over,
 * getting into a model of the test place (game/showdrive.h) – game.ts:3142 …
 * 3436: startWatch, startPointWatch, pickBoxes, picked, takeOverTram,
 * showBeside, enterShow, endWatch, clearRail, stepWatch, takeOverWatched.
 * (takeOverCar is in game_trips.cpp, transitData in game_transit.cpp.)
 *
 * Origin: src/game/game.ts @ v1.7.7
 */
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/clock.h"
#include "game/game.h"
#include "game/pointwatch.h"
#include "game/showdrive.h"
#include "game/spawncar.h"
#include "game/spectate.h"
#include "game/takeover.h"
#include "game/vehicles.h"
#include "i18n/index.h"
#include "input/input.h"
#include "render/tiles.h"
#include "sim/hooks.h"
#include "sim/rail.h"
#include "sim/roadevents.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "ui/bigmap.h"
#include "ui/hud.h"
#include "world/loader.h"

namespace fs {

void Game::startWatch(const WatchRequest& v) {
  if (state == "map" && mapFrom == "menu") {
    // from the main menu: a free drive begins (the vehicle and the place chosen there), and the watching with it once placed
    closeMap();
    const Settings::LastStart ls = settings.lastStart;
    begin("free", ls.spawn, true, ls.address ? &*ls.address : nullptr);
    const WatchRequest req = v;
    afterPlace = [this, req] { startWatch(req); };
    return;
  }
  TransitSource* src = transitData();
  if (!src || lesson || replay || (state != "map" && state != "drive" && state != "pause")) return;
  if (watcher) watcher->dispose();
  watcher = nullptr;
  // (the trams and cars round the player go: those round the vehicle watched come – the tram itself with them)
  clearRail();
  traffic->clear();
  WatchHost host;
  host.hud = [this] { return ui.querySelector(".hud"); };
  host.ui = ui;
  host.lang = [this] { return settings.lang; };
  host.source = src;
  host.rail = [this]() -> RailTraffic& { return *rail; };
  host.traffic = [this]() -> Traffic& { return *traffic; };
  host.lanes = lanes.get();
  host.ready = [this](double x, double z) { return app.map->ready(x, z, 400) && !std::isnan(app.tiles->heightAt(x, z)); };
  host.ground = [this](double x, double z) { return groundHeight(x, z); };
  host.takeOver = [this](const WatchRequest& w, const TakeOver& at) { takeOverWatched(w, at); };
  host.back = [this] { endWatch(); };
  watcher = std::make_unique<Watcher>(v, std::move(host));
  if (state == "map") {
    mapFrom = "drive";
    closeMap();
  } else if (state == "pause") resume();
}

void Game::startPointWatch(const WatchWhat& what) {
  if (lesson || replay || (state != "map" && state != "drive" && state != "pause")) return;
  const std::string lang = settings.lang;
  WatchTarget target;
  if (what.kind == "em" || what.kind == "car") {
    // an emergency vehicle (from the map), or any car, bus or emergency vehicle tapped (game/takeover.ts) – each can be taken over
    TrafficCar* car = nullptr;
    for (TrafficCar* c : traffic->cars)
      if (c->id == what.id) {
        car = c;
        break;
      }
    if (!car) return;
    static const std::vector<std::string> names = {"emPolice", "emAmbulance", "emFire", "emDoctor"};
    const std::optional<BusOnLine>& line = car->bus ? car->bus->line : std::nullopt;
    if (car->em) {
      const int k = car->em->kind;
      target.title = tr(k >= 0 && static_cast<size_t>(k) < names.size() ? names[static_cast<size_t>(k)] : "emPolice", lang);
    } else if (car->bus) {
      std::string t = tr("transitBus", lang) + " " + (line ? line->ref + " → " + line->to : "");
      while (!t.empty() && t.back() == ' ') t.pop_back();
      target.title = t;
    } else target.title = tr("watchCar", lang);
    target.sub = car->em ? tr(car->em->call ? "emOnCall" : "emPatrol", lang) : "";
    target.length = car->length;
    const int64_t id = what.id;
    target.get = [this, id]() -> std::optional<WatchAt> {
      for (const TrafficCar* c : traffic->cars)
        if (c->id == id) {
          // (as it is drawn: eased through the corners of its way – sim/smoothpose.ts)
          return c->drawn ? WatchAt{.x = c->drawn->x, .y = c->y, .z = c->drawn->z, .yaw = c->drawn->yaw} : WatchAt{.x = c->x, .y = c->y, .z = c->z, .yaw = c->yaw};
        }
      return std::nullopt;
    };
    target.take = [this, id] { takeOverCar(static_cast<int>(id)); };
  } else if (what.kind == "rail") {
    // a tram or a train tapped: a tram can be taken over (from its next stop)
    const RailVehicle* v = nullptr;
    for (const auto& q : rail->vehicles)
      if (q->id == what.id) {
        v = q.get();
        break;
      }
    if (!v) return;
    const bool isTram = v->kind != RAIL_TRAIN;
    const std::string ref = v->line && static_cast<size_t>(v->line->index) < rail->lines.size() ? rail->lines[static_cast<size_t>(v->line->index)].ref : "";
    std::string t = tr(isTram ? "transitTram" : "watchTrain", lang) + " " + ref;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    target.title = t;
    target.sub = "";
    target.length = consistLength(*v->consist);
    const int id = what.id;
    target.get = [this, id]() -> std::optional<WatchAt> {
      for (const auto& q : rail->vehicles)
        if (q->id == id) {
          const RailCar& c = q->cars[(q->cars.size() - 1) / 2];
          return WatchAt{.x = c.x, .y = c.y, .z = c.z, .yaw = q->cars[0].yaw};
        }
      return std::nullopt;
    };
    if (isTram) target.take = [this, id] { takeOverTram(id); };
  } else {
    const RoadEventInfo ev = *what.ev;
    const bool isDemo = ev.kind == "demo";
    target.title = isDemo ? tr("eventDemoTitle", lang, {{"place", ev.detail}}) : tr("eventWorks", lang);
    target.sub = ev.street;
    target.length = isDemo ? 40 : 24;
    target.get = [this, ev, isDemo]() -> std::optional<WatchAt> {
      if (isDemo && roadEvents)
        if (const auto c = roadEvents->crowdAt(ev.x, ev.z, gameClock.ms())) return WatchAt{.x = c->x, .y = c->y, .z = c->z, .yaw = c->yaw};
      return WatchAt{.x = ev.x, .y = std::nan(""), .z = ev.z, .yaw = 0};
    };
  }
  if (watcher) watcher->dispose();
  watcher = nullptr;
  PointWatchHost host;
  host.hud = [this] { return ui.querySelector(".hud"); };
  host.ui = ui;
  host.t = [this](const std::string& k, const TrParams& p) { return tr(k, settings.lang, p); };
  host.backText = [this] { return tr("watchBack", settings.lang); };
  host.goneText = [this] { return tr("watchGone", settings.lang); };
  host.ground = [this](double x, double z) { return groundHeight(x, z); };
  host.back = [this] { endWatch(); };
  watcher = std::make_unique<PointWatcher>(std::move(target), std::move(host));
  if (state == "map") {
    mapFrom = "drive";
    closeMap();
  } else if (state == "pause") resume();
}

// ------------------------------------------------- any vehicle watched, taken over; the models of the test place got into (game/takeover.ts, showdrive.ts)
std::optional<std::vector<PickBox>> Game::pickBoxes() {
  // (later: riding, busRide – game.ts:3261)
  if (state != "drive" || lesson || task || (demo && demo->active) || (spawnCars && spawnCars->picking) || doorT >= 0 || opens.busy()) return std::nullopt;
  std::vector<PickBox> out;
  // (on foot, or standing in the vehicle seen from outside – in the driver's seat a tap is for its screen and mirrors)
  const bool still = walker != nullptr || (std::abs(vehicle->speed) < 0.5 && cameraMode != "cockpit" && !aircraft && !tram);
  if (showOn && !watcher && still)
    for (const PickBox& b : showSpots.boxes()) out.push_back(b);
  if (watcher || (still && !forklift && !crane && !showOn)) {
    for (const TrafficCar* c : traffic->cars) {
      PickBox b;
      b.x = c->drawn ? c->drawn->x : c->x;
      b.y = c->y;
      b.z = c->drawn ? c->drawn->z : c->z;
      b.yaw = c->drawn ? c->drawn->yaw : c->yaw;
      b.height = c->length > 7 ? 3.1 : c->length > 5 ? 2.7 : 1.5;
      b.front = c->length / 2;
      b.rear = c->length / 2;
      b.hw = c->width / 2;
      b.what = PickTarget{.kind = "car", .id = static_cast<int>(c->id), .i = -1};
      out.push_back(b);
    }
    for (const auto& v : rail->vehicles) {
      if (v->player) continue;
      const std::vector<CarRole>& roles = carLayout(*v->consist);
      for (size_t i = 0; i < v->cars.size() && i < roles.size(); i++) {
        const RailCar& rc = v->cars[i];
        PickBox b;
        b.x = rc.x;
        b.y = rc.y;
        b.z = rc.z;
        b.yaw = rc.yaw;
        b.height = 3.4;
        b.front = roles[i].len / 2;
        b.rear = roles[i].len / 2;
        b.hw = v->consist->width / 2;
        b.what = PickTarget{.kind = "rail", .id = v->id, .i = -1};
        out.push_back(b);
      }
    }
  }
  return out;
}

void Game::picked(const PickTarget& t) {
  if (t.kind == "show") enterShow(t.i);
  else startPointWatch(WatchWhat{.kind = t.kind, .id = t.id, .ev = std::nullopt});
}

/** Take over a tram watched: a drive of the tram from the stop nearest to it, on its line. */
void Game::takeOverTram(int id) {
  const RailVehicle* v = nullptr;
  for (const auto& q : rail->vehicles)
    if (q->id == id) {
      v = q.get();
      break;
    }
  if (!v) return;
  const std::string lang = settings.lang;
  const RailCar f = v->cars[0];
  const std::string ref = v->line && static_cast<size_t>(v->line->index) < rail->lines.size() ? rail->lines[static_cast<size_t>(v->line->index)].ref : "";
  endWatch(false);
  if (demo) endDemo("ended");
  const VehicleKind was = settings.vehicle;
  settings.vehicle = "tram";
  const TramStopChoice stop{.name = "", .town = "", .x = f.x, .z = f.z};
  try {
    tramLineRef = ref;
    begin("free", 0, true, nullptr, &stop);
  } catch (...) {
    settings.vehicle = was;
    throw;
  }
  settings.vehicle = was;
  hud->message(tr("watchTakenVehicle", lang, {{"what", tr(VEHICLES.at("tram").name, lang)}}));
}

int Game::showBeside() {
  const OnFoot* w = walker.get();
  if (!w || !showOn || watcher) return -1;
  const int i = showSpots.near(w->x, w->z);
  if (i < 0) return -1;
  const ShowSpot& s = showSpots.list[static_cast<size_t>(i)];
  vehicle->getModelPosition(mpos.data());
  return std::hypot(s.x - w->x, s.z - w->z) < std::hypot(mpos[0] - w->x, mpos[2] - w->z) ? i : -1;
}

void Game::enterShow(int i) {
  if (!showOn || state != "drive" || watcher || !showSpots.choose(i)) return;
  const ShowSpot& s = showSpots.list[static_cast<size_t>(i)];
  // (the vehicle chosen from now on, as when getting into a parked one)
  settings.vehicle = s.kind;
  saveSettings(settings);
  intoParked = true;
  begin("show", 0, true);
}

void Game::endWatch(bool clear) {
  if (!watcher) return;
  // (it is let go only after this step: endWatch may be called from inside it – its bar's buttons)
  std::unique_ptr<WatchView> w = std::move(watcher);
  w->dispose();
  if (clear) {
    clearRail();
    traffic->clear();
  }
  camSnap = true;
  watchGone = std::move(w);
}

void Game::clearRail() {
  std::vector<std::unique_ptr<RailVehicle>> own;
  for (auto& q : rail->vehicles)
    if (q->player) own.push_back(std::move(q));
  rail->clear();
  for (auto& q : own) rail->vehicles.push_back(std::move(q));
}

void Game::stepWatch(double dt) {
  WatchView& w = *watcher;
  w.step(dt, input->c);
  // (the step may have ended the watching: its question answered)
  if (!watcher) return;
  const PlayerState g = w.ghost;
  signals->update(dt);
  std::vector<RailObstacle>& others = railOthers;
  others.clear();
  for (const TrafficCar* car : traffic->cars) others.push_back(RailObstacle{car->x, car->z, 1.3});
  // (and the crowd of a march on its street)
  if (roadEvents) roadEvents->railBlocks(others);
  rail->update(dt, RailPlayer{g.x, g.y, g.z, g.yaw, g.length, g.width}, others);
  std::vector<PlayerState>& boxes = railBoxes;
  boxes.clear();
  for (const auto& rv : rail->vehicles) {
    const std::vector<CarRole>& roles = carLayout(*rv->consist);
    for (size_t i = 0; i < rv->cars.size() && i < roles.size(); i++) {
      const RailCar& rc = rv->cars[i];
      PlayerState b;
      b.x = rc.x;
      b.y = rc.y;
      b.z = rc.z;
      b.yaw = rc.yaw;
      b.speed = rv->speed;
      b.length = roles[i].len;
      b.width = rv->consist->width;
      boxes.push_back(b);
    }
  }
  traffic->obstacles.clear();
  for (PlayerState& b : boxes) traffic->obstacles.push_back(&b);
  traffic->update(dt, g);
}

void Game::takeOverWatched(const WatchRequest& w, const TakeOver& at) {
  TransitSource* src = transitSrc.get();
  endWatch(false);
  const VehicleKind was = settings.vehicle;
  // (for this drive only: the vehicle chosen in the menu stays what it was)
  settings.vehicle = w.kind == "tram" ? "tram" : "bus";
  try {
    if (w.kind == "tram") {
      tramLineRef = w.ref;
      const TramStopChoice stop{.name = at.stop, .town = "", .x = at.x, .z = at.z};
      begin("free", 0, true, nullptr, &stop);
    } else {
      const AddressHit hit{.label = w.ref + " → " + w.dest, .street = "", .x = at.x, .z = at.z, .exact = true};
      begin("free", 0, true, &hit);
      const std::optional<TourPreview> way = src ? lineWay(*src, at) : std::nullopt;
      if (way) afterPlace = [this, way] { startTrip({}, true, &*way); };
    }
  } catch (...) {
    settings.vehicle = was;
    throw;
  }
  settings.vehicle = was;
  hud->message(tr("watchTaken", settings.lang, {{"ref", w.ref}, {"dest", w.dest}}));
}

}  // namespace fs
