// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 – the trams and trains, their lines and timetables, the stops, the lines of the
// city buses (game.ts:709–771, 3361, 4293–4328, 6093–6107, 6180–6268, 6686–6696)
#include <algorithm>
#include <cmath>

#include "core/geom.h"
#include "game/clock.h"
#include "game/nav.h"
#include "sim/vehicle/index.h"
#include "game/game.h"
#include "game/timedtransit.h"
#include "game/transitsource.h"
#include "i18n/index.h"
#include "input/input.h"
#include "sim/rules.h"
#include "render/trams.h"
#include "sim/timetable.h"
#include "sim/traffic.h"
#include "ui/hud.h"
#include "core/js.h"
#include "render/fleet.h"
#include "world/buslines.h"
#include "world/loader.h"
#include "world/netindex.h"
#include "world/stops.h"

namespace fs {

void Game::railsRead(std::shared_ptr<const std::vector<RailJson>> json) {
  if (!json || json->empty()) return;
  rail = std::make_unique<RailTraffic>(*json);
  // (later: roadEvents.setTracks, levelCrossings.setRail – game.ts:715)
  applySettings();
}

void Game::linesRead(std::shared_ptr<const LinesJson> json) {
  lines = json;
  lineStopsDirty = true;
  // … and their timetables (they refer to the lines by their place in that file)
  if (!json) return;
  [](Game* self, std::shared_ptr<const LinesJson> linesJson) -> Task<void> {
    const std::shared_ptr<const TimetableJson> tj = co_await self->app.map->timetable();
    if (!tj) co_return;
    try {
      self->timetable = parseTimetable(*tj, linesJson.get());
      self->lineStopsDirty = true;
      // (later: transit.setTimetable – the departure displays at the stops, render/transit.ts – game.ts:747)
    } catch (const std::exception& e) {
      plat::warn(std::string("the timetables could not be read: ") + e.what());
    }
  }(this, json).detach();
}

void Game::stopsRead(std::shared_ptr<const StopsJson> json) {
  if (!json) return;
  stops = std::make_unique<Stops>(*json);
  // (later: transit.setStops – what is drawn at a stop, render/transit.ts – game.ts:763)
  traffic->crowd->stops = stops.get();
  stopSources.clear();
  for (const StopPlace& p : stops->places) stopSources.push_back(StopSource{p.name, p.town, p.x, p.z});
  // the lines of the city buses (they name the stops by their place in that file): the buses of the traffic run them
  const int places = static_cast<int>(stops->places.size());
  [](Game* self, int n) -> Task<void> {
    std::shared_ptr<BusLines> got = co_await loadBusLines(self->app.map.get(), n);
    self->busLines = got;
    self->traffic->setBusLines(got.get());
  }(this, places).detach();
  std::vector<TramStopChoice> choices;
  for (const StopPlace* p : stops->tramPlaces()) choices.push_back(TramStopChoice{p->name, p->town, p->x, p->z});
  hud->setTramStops(std::move(choices));
  // with the stops, city buses come with the traffic: they call at the stops on their way
  laneStops.clear();
  traffic->busStops = [this](const Lane& lane) { return busStopsOn(lane); };
}

std::string Game::stopName(int path, double mark) {
  const RailPath* p = path >= 0 && path < static_cast<int>(rail->net.paths.size()) ? &rail->net.paths[static_cast<size_t>(path)] : nullptr;
  int i = -1;
  if (p)
    for (size_t k = 0; k < p->stops.size(); k++)
      if (std::abs(p->stops[k] - mark) < 0.5) {
        i = static_cast<int>(k);
        break;
      }
  if (i >= 0 && !p->stopNames[static_cast<size_t>(i)].empty()) return p->stopNames[static_cast<size_t>(i)];
  const std::string key = std::to_string(path) + ":" + jsStr(jsRound(mark));
  auto it = stopNames.find(key);
  std::string name;
  if (it == stopNames.end()) {
    RailSample smp;
    rail->net.sample(path, mark, smp);
    name = app.world->nearestRoad(smp.x, smp.z, 60, 0, hit) && hit.edge ? hit.edge->name : "";
    stopNames[key] = name;
  } else name = it->second;
  return !name.empty() ? name : tr("tramStop", settings.lang);
}

const std::vector<BusStopOnLane>& Game::busStopsOn(const Lane& lane) {
  auto known = laneStops.find(lane.id);
  if (known != laneStops.end()) return known->second;
  std::vector<BusStopOnLane> list;
  const Edge& e = *lane.edge;
  if (stops && e.len > 10) {
    PolySample smp;
    samplePolyline(e.pl, e.len / 2, smp);
    PolyProjection proj;
    std::vector<StopPlace*> near;
    for (const StopPlace* place : stops->near(smp.x, smp.z, e.len / 2 + 40, near)) {
      if (!(place->kind & STOP_FLAG_BUS)) continue;
      for (size_t pi = 0; pi < place->poles.size(); pi++) {
        const StopPole& pole = place->poles[pi];
        if ((pole.flags & STOP_FLAG_TRAM) && !(pole.flags & STOP_FLAG_BUS)) continue;
        projectOnPolyline(e.pl, pole.x, pole.z, proj);
        if (proj.s < 1 || proj.s > e.len - 1 || std::abs(proj.off) > e.width / 2 + 7) continue;
        // on the right of the lane's direction: left of the edge (off > 0) when it runs against it
        if ((lane.dir > 0 ? -proj.off : proj.off) < 0.5) continue;
        const double s = (lane.dir > 0 ? proj.s - lane.s0 : lane.s0 - proj.s) + 1.5;
        if (s > 12 && s < lane.len) list.push_back(BusStopOnLane{s, place->id * 64 + static_cast<int>(pi)});
      }
    }
    std::stable_sort(list.begin(), list.end(), [](const BusStopOnLane& a, const BusStopOnLane& b) { return a.s < b.s; });
  }
  return laneStops.emplace(lane.id, std::move(list)).first->second;
}

const BigMapHost::Tramways* Game::tramways() {
  if (!rail || rail->net.paths.empty()) return nullptr;
  if (tramMap && tramMapOf == rail.get() && tramMapStops == stops.get()) return &*tramMap;
  BigMapHost::Tramways m;
  for (const RailPath& p : rail->net.paths) {
    if (p.kind != RAIL_TRAM && p.kind != RAIL_LIGHT) continue;
    const size_t n = p.pts.size() / 3;
    std::vector<float> t(n * 2);
    for (size_t i = 0; i < n; i++) {
      t[i * 2] = static_cast<float>(p.pts[i * 3]);
      t[i * 2 + 1] = static_cast<float>(p.pts[i * 3 + 2]);
    }
    m.tracks.push_back(std::move(t));
  }
  if (stops)
    for (const StopPlace* p : stops->tramPlaces()) m.stops.push_back(BigMapHost::Tramways::Stop{p->x, p->z, p->name});
  m.version = (tramMap ? tramMap->version : 0) + 1;
  tramMap = std::move(m);
  tramMapOf = rail.get();
  tramMapStops = stops.get();
  return &*tramMap;
}

TransitSource* Game::transitData() {
  if (!transitSrc) {
    TransitGetters get;
    get.timetable = [this] { return timetable.get(); };
    get.lines = [this] { return lines.get(); };
    get.rail = [this] { return rail.get(); };
    get.busLines = [this] { return traffic ? traffic->busLines() : nullptr; };
    get.net = [this]() -> const NetIndex* { return netIndex.get(); };
    get.world = app.world;
    get.stops = [this] { return stops.get(); };
    transitSrc = transitSource(std::move(get));
  }
  return transitSrc.get();
}

void Game::stepRail(double dt, const PlayerState& player, bool withCars) {
  if (!rail) return;
  // trams and trains: they keep clear of the cars, the cars of them
  std::vector<RailObstacle>& others = railOthers;
  others.clear();
  if (withCars)
    for (const TrafficCar* c : traffic->cars) others.push_back(RailObstacle{c->x, c->z, 1.3});
  // (later: roadEvents.railBlocks – the crowd of a march on its street, game.ts:4298)
  rail->update(dt, RailPlayer{player.x, player.y, player.z, player.yaw, player.length, player.width}, others);
}

void Game::updateLines() {
  if (!rail || !lines || rail->net.paths.empty() || !(lineStopsDirty || linesFor != rail.get())) return;
  // the lines: the trams run them, the departure boards list them
  lineStopsDirty = false;
  linesFor = rail.get();
  const std::vector<RailPath>& paths = rail->net.paths;
  // (later: transit.lines – the departure boards, render/transit.ts – game.ts:6692)
  // (every line in its place – the timetables count them so –, one that does not fit the tracks without a way)
  std::vector<RailLine> list;
  for (const LineJson& l : lines->lines) {
    RailLine r{l.ref, l.from, l.to, l.colour, l.kind, {}};
    const bool fits = std::all_of(l.paths.begin(), l.paths.end(), [&](const std::pair<int, int>& p) { return p.first < static_cast<int>(paths.size()); });
    if (fits)
      for (const auto& [p, d] : l.paths) r.seq.push_back(PathDir{p, d > 0 ? 1 : -1});
    list.push_back(std::move(r));
  }
  rail->setLines(std::move(list));
  if (timetable) {
    TramScheduleHost h;
    h.timetable = [this] { return timetable.get(); };
    h.lines = [this] { return lines.get(); };
    h.rail = [this]() -> RailTraffic& { return *rail; };
    h.nowMs = [] { return gameClock.ms(); };
    railSchedule = tramSchedule(std::move(h));
  } else railSchedule.reset();
  rail->schedule = railSchedule.get();
}

/** stops in the bus lesson (game.ts BUS_STOPS) */
constexpr int BUS_STOPS = 5;

Game::BusStopsHere Game::busStopsHere() {
  Lane* lane = currentLane();
  if (!lane) return {};
  BusStopsHere out;
  out.front = laneFront();
  for (const BusStopOnLane& s : busStopsOn(*lane)) {
    const int place = static_cast<int>(std::floor(s.id / 64.0));
    out.stops.push_back(BusStopAhead{s.id, s.s, stops && place >= 0 && place < static_cast<int>(stops->places.size()) ? stops->places[static_cast<size_t>(place)].name : ""});
  }
  return out;
}

void Game::updateBusLine(double dt) {
  BusService* bs = busService.get();
  if (!bs) return;
  const Vehicle& v = *vehicle;
  const std::string& lang = settings.lang;
  const BusStopsHere here = busStopsHere();
  std::vector<BusEvent> ev;
  bs->step(dt, here.front, here.stops, v.speed, static_cast<int>(input->c.indicator), ev);
  for (const BusEvent& e : ev) rules->noteAt(e.fault, e.severity, mpos[0], mpos[2], {}, 8);
  const BusStopAhead* next = bs->next(here.front, here.stops);
  busNav = next ? tr("tramNextStop", lang, {{"name", !next->name.empty() ? next->name : tr("tramStop", lang)}, {"d", std::max(0.0, jsRound(next->s - here.front))}}) : tr("tramNoStop", lang);
  lessonLine = tr("tramProgress", lang, {{"n", std::min(bs->served, BUS_STOPS)}, {"max", BUS_STOPS}});
  if (bs->served >= BUS_STOPS && !bs->doorsOpen && std::abs(v.speed) < 0.3) finish();
}

/** stops in the tram lesson (game.ts TRAM_STOPS) */
constexpr int TRAM_STOPS = 5;

void Game::placeTramProxy() {
  const RailCar& c = tram->v.cars[0];
  vehicle->teleport(c.x, c.y, c.z, c.yaw);
  vehicle->getModelPosition(mpos.data());
}

void Game::noteTram(const std::vector<TramEvent>& events) {
  for (const TramEvent& e : events) {
    ViolationData data;
    for (const auto& [k, v] : e.data) data.emplace_back(k, v);
    rules->noteAt(e.fault, e.severity, mpos[0], mpos[2], data, 8);
  }
}

void Game::stepTram(double dt) {
  TramDriver& t = *tram;
  Controls& c = input->c;
  std::vector<TramEvent>& ev = tramEvents;
  ev.clear();
  if (tramAuto) {
    // driving by itself – until the controller is touched for a moment: then the player drives
    // (with the assisted autopilot only for as long as the controller is held: it drives on after)
    tramTake = c.throttle > 0.2 || c.brake > 0.2 || c.handbrake > 0.5 ? tramTake + dt : 0;
    if (tramHeld) {
      tramFree = c.throttle < 0.05 && c.brake < 0.05 && c.handbrake < 0.5 ? tramFree + dt : 0;
      if (tramFree > 2.5) {
        tramHeld = false;
        tramTake = 0;
        hud->message(tr("demoResumed", settings.lang));
      }
    } else if (tramTake > 0.18 && settings.autopilotAssist) {
      tramHeld = true;
      tramFree = 0;
      hud->message(tr("demoHeld", settings.lang));
    } else if (tramTake > 0.18) {
      tramAuto = false;
      hud->message(tr("demoTaken", settings.lang), tr("tramAutoAgain", settings.lang));
    }
    if (tramAuto && !tramHeld) t.drive(ev);
  }
  // (later: the walker, the cab's door – nobody in the cab: the tram stands braked, and the keys walk – game.ts:6135)
  if (!tramAuto || tramHeld) {
    t.lever = std::max(-1.0, std::min(1.0, c.throttle - c.brake));
    t.emergency = c.handbrake > 0.5;
  }
  // the indicator asks for the way at the next switch
  t.wish = c.indicator < 0 ? 1 : c.indicator > 0 ? -1 : 0;
  t.step(dt, ev);
  placeTramProxy();
  rules->time += dt;
  signals->update(dt);
  // the other trams and trains (the player's is among them and is left to its driver)
  std::vector<RailObstacle>& others = railOthers;
  others.clear();
  for (const TrafficCar* car : traffic->cars) others.push_back(RailObstacle{car->x, car->z, 1.3});
  // (later: roadEvents.railBlocks – the crowd of a march on its street, game.ts:6155)
  // (the others appear around the player's tram and keep clear of it like of any other)
  const RailCar front = t.v.cars[0];
  rail->update(dt, RailPlayer{front.x, front.y, front.z, front.yaw, t.v.consist->carLen, t.v.consist->width}, others);
  // the road traffic keeps clear of every car of the tram
  std::vector<PlayerState>& boxes = railBoxes;
  boxes.clear();
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
      b.own = rv.get() == &t.v;
      boxes.push_back(b);
    }
  }
  traffic->obstacles.clear();
  for (PlayerState& b : boxes) traffic->obstacles.push_back(&b);
  PlayerState p;
  p.x = front.x;
  p.y = front.y;
  p.z = front.z;
  p.yaw = front.yaw;
  p.speed = t.speed();
  p.length = t.v.consist->carLen;
  p.width = t.v.consist->width;
  p.horn = c.horn;
  traffic->update(dt, p);
  for (const TrafficEvent& e : traffic->events) {
    if (e.type == "collision") rules->noteAt("crash", "fail", front.x, front.z, {{"speed", jsRound(t.speed() * 3.6)}}, 6);
    else if (e.type == "cyclistEndangered") rules->noteAt(e.hit ? "cyclistHit" : "cyclistCutOff", e.hit ? "fail" : "major", front.x, front.z, {}, 8);
  }
  noteTram(ev);
  // the lesson: a line with its stops
  if (lesson && lesson->kind == "tram") {
    const int n = std::min(t.served, TRAM_STOPS);
    lessonLine = tr("tramProgress", settings.lang, {{"n", n}, {"max", TRAM_STOPS}});
    if (t.served >= TRAM_STOPS && t.speed() < 0.1 && !t.doorsOpen) finish();
  }
  if (failT > 0 && time > failT) finish();
}

void Game::tramHud(HudState& hs, bool touch) {
  const std::string& lang = settings.lang;
  TramDriver& t = *tram;
  // the tram: the next stop, the controller, the switch asked for, the doors
  const TramDriver::Ahead& A = t.ahead;
  hs.limit = t.limit;
  hs.zone = 0;
  if (A.signalIn < 120 && A.signal >= 0) hs.junction = RuleContext::Junction{A.signalIn, 0, CTRL_SIGNALS, A.signal, -2, false};
  else hs.junction = std::nullopt;
  hs.gear = t.emergency ? "!" : t.lever > 0.02 ? "+" + jsStr(jsRound(t.lever * 100)) : t.lever < -0.02 ? "−" + jsStr(jsRound(-t.lever * 100)) : "0";
  hs.navText = A.stop < 800 ? tr("tramNextStop", lang, {{"name", stopName(A.stopPath, A.stopMark)}, {"d", std::max(0.0, jsRound(A.stop))}}) : tr("tramNoStop", lang);
  const std::string control = t.emergency ? tr("tramEmergency", lang) : t.lever > 0.02 ? tr("tramPower", lang, {{"p", jsRound(t.lever * 100)}}) : t.lever < -0.02 ? tr("tramBrake", lang, {{"p", jsRound(-t.lever * 100)}}) : tr("tramCoast", lang);
  const std::string way = tr(t.wish > 0 ? "tramWayLeft" : t.wish < 0 ? "tramWayRight" : "tramWayStraight", lang);
  // where the power comes from: the wire (pantograph up, down, on its way) – or the regional tram's engine
  const std::string power = !t.wired() ? tr(t.hybrid() ? "tramEngine" : "tramNoWire", lang) : t.panto >= 0.999 ? tr("tramPantoUp", lang) : t.pantoUp ? tr("tramPantoRising", lang) : tr("tramPantoDown", lang);
  std::vector<std::string> parts = {control, way, tr(t.doorsOpen ? "tramDoorsOpen" : "tramDoorsClosed", lang), power};
  const RailLine* line = t.v.line && t.v.line->index >= 0 && t.v.line->index < static_cast<int>(rail->lines.size()) ? &rail->lines[static_cast<size_t>(t.v.line->index)] : nullptr;
  if (line) parts.insert(parts.begin(), tr("tramLineTo", lang, {{"line", line->ref}, {"to", line->to}}));
  // standing at a stop of its line: on time by the timetable, or how late (or early)
  if (timetable && t.v.line && A.stop < 8 && A.stopPath >= 0 && t.speed() < 0.5) {
    const std::optional<ScheduledStop> sch = scheduledAt(*timetable, t.v.line->index, stopName(A.stopPath, A.stopMark), serviceTime(gameNow()));
    if (sch) {
      const double m = jsRound(sch->late / 60);
      parts.push_back(m == 0 ? tr("tramOnTime", lang) : m > 0 ? tr("tramLate", lang, {{"m", m}}) : tr("tramEarly", lang, {{"m", -m}}));
    }
  }
  if (lesson && lesson->kind == "tram") parts.insert(parts.begin(), lessonLine);
  if (tramAuto) parts.insert(parts.begin(), tr("tramAutoRunning", lang));
  hs.navSub = join(parts, " · ");
  hs.navArrow = 0;
  hs.navActive = false;
  hs.tip = "";
  hs.tipAction = "";
  if (t.doorsOpen && t.passengers() > 0) hs.tip = tr("tramPassengers", lang, {{"n", t.passengers()}});
  else if (!tramAuto && t.wired() && !t.pantoUp && !t.doorsOpen && t.speed() < 3) {
    // no power: the pantograph is down (as at the start of a drive)
    hs.tip = tr(touch ? "tramPantoRaiseTouch" : "tramPantoRaise", lang);
    hs.tipAction = "KeyP";
  } else if (!tramAuto && t.pantoUp && t.ahead.wireEndIn < 160) {
    // the wire ends ahead: the pantograph must be down before it does
    hs.tip = tr(touch ? "tramWireEndsTouch" : "tramWireEnds", lang, {{"d", jsRound(t.ahead.wireEndIn)}});
    hs.tipAction = "KeyP";
  } else if (t.doorsOpen) {
    hs.tip = tr(touch ? "tramCloseDoorsTouch" : "tramCloseDoors", lang);
    hs.tipAction = "KeyG";
  } else if (t.atUnservedStop()) {
    hs.tip = tr(touch ? "tramOpenDoorsTouch" : "tramOpenDoors", lang);
    hs.tipAction = "KeyG";
  } else if (A.switchIn < 60 && t.speed() > 0.5) hs.tip = tr("tramSwitchAhead", lang, {{"d", jsRound(A.switchIn)}});
}

}  // namespace fs
