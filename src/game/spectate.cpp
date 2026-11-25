// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/spectate.ts @ v1.7.7 (lineWay, Watcher – transitSource and lineTakeOver: game/transitsource.cpp)
#include "game/spectate.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/geom.h"
#include "core/js.h"
#include "engine/engine.h"
#include "input/input.h"
#include "sim/rail.h"
#include "sim/traffic.h"
#include "world/buslines.h"
#include "world/lanes.h"

namespace fs {

using dom::El;
using dom::el;

std::optional<TourPreview> lineWay(TransitSource& src, const TakeOver& at) {
  std::vector<float> line;
  double metres = 0;
  for (int w : at.ways) {
    const TransitCourse c = src.wayCourse(w);
    if (!c) continue;
    for (size_t i = 0; i + 1 < c->size(); i += 2) {
      line.push_back((*c)[i]);
      line.push_back((*c)[i + 1]);
    }
    metres += courseLength(*c);
  }
  const size_t L = line.size();
  if (at.ways.empty() || L < 4) return std::nullopt;
  const Spot end = at.end ? *at.end
                          : Spot{line[L - 2], line[L - 1], std::atan2(-(static_cast<double>(line[L - 2]) - line[L - 4]), -(static_cast<double>(line[L - 1]) - line[L - 3])), std::nullopt};
  TourPreview out;
  out.plan.ways.assign(at.ways.begin() + 1, at.ways.end());
  out.plan.metres = metres;
  out.plan.fresh = metres;
  out.plan.again = 0;
  out.plan.valued = metres;
  out.line = std::move(line);
  out.from = at.ways[0];
  out.end = SpawnJson{.name = "", .x = end.x, .z = end.z, .yaw = end.yaw, .kind = "street", .rough = false, .g = ""};
  return out;
}

// ---------------------------------------------------------------- Watcher

Watcher::Watcher(const WatchRequest& r, WatchHost host) : req(r), h_(std::move(host)) {
  update(0);
  showBar();
}

Watcher::~Watcher() { dispose(); }

std::string Watcher::t(const std::string& key, const TrParams& p) const { return tr(key, h_.lang(), p); }

std::optional<TripState> Watcher::timetableTrip() {
  const Timetable* tt = h_.source->timetable();
  if (!tt) return std::nullopt;
  const ServiceTime now = h_.source->now();
  const std::vector<TripState> list = req.kind == "tram" ? tripsAt(*tt, req.line, now) : busTripsAt(*tt, req.line, now);
  for (const TripState& q : list)
    if (q.pattern == req.pattern && std::abs(q.t0 - req.t0) < 1) return q;
  return std::nullopt;
}

std::optional<Spot> Watcher::spotOfTrip(const TripState& trip) {
  const TimetablePattern& p = *trip.pattern;
  const auto at = [](const std::vector<int>& v, int i) { return i >= 0 && static_cast<size_t>(i) < v.size() ? v[static_cast<size_t>(i)] : -1; };
  return spotOf(*h_.source, TripSpot{.kind = req.kind,
                                     .line = req.line,
                                     .fromStop = at(p.lineStops, trip.from),
                                     .toStop = at(p.lineStops, trip.to),
                                     .fromPlace = at(p.places, trip.from),
                                     .toPlace = at(p.places, trip.to),
                                     .standing = trip.standing,
                                     .frac = trip.frac});
}

void Watcher::update(double dt) {
  clock_ += dt;
  if (clock_ - tripAt_ > 0.5 || tripAt_ < 0) {
    tripAt_ = clock_;
    trip_ = timetableTrip();
    const std::optional<Spot> s = trip_ ? spotOfTrip(*trip_) : std::nullopt;
    if (s) spot_ = s;
  }
  RailTraffic& rail = h_.rail();
  Traffic& traffic = h_.traffic();
  if (req.kind == "tram") {
    if (tram_ && std::none_of(rail.vehicles.begin(), rail.vehicles.end(), [this](const auto& v) { return v.get() == tram_; })) tram_ = nullptr;
    if (!tram_)
      for (const auto& v : rail.vehicles)
        if (v->trip == req.trip) {
          tram_ = v.get();
          break;
        }
    // (the traffic of the trams did not bring it – it has enough about: it is put on its tracks here)
    if (!tram_ && spot_ && trip_ && h_.ready(spot_->x, spot_->z)) {
      waited_ += dt;
      if (waited_ > 3) {
        waited_ = 0;
        tram_ = placeTram(rail, *trip_);
      }
    }
  } else {
    if (bus_ && std::find(traffic.cars.begin(), traffic.cars.end(), bus_) == traffic.cars.end()) bus_ = nullptr;
    // (the buses of the timetables are put on their roads by the traffic itself: src/game/timedtransit.ts)
    if (!bus_)
      for (TrafficCar* c : traffic.cars)
        if (c->bus && c->bus->trip == req.trip) {
          bus_ = c;
          break;
        }
    if (!bus_ && spot_ && trip_ && h_.ready(spot_->x, spot_->z)) bus_ = placeBus(traffic);
  }
  // where it is now
  if (tram_) {
    length_ = consistLength(*tram_->consist);
    // (the middle of the tram – its front leads the way)
    const RailCar& c = tram_->cars[(tram_->cars.size() - 1) / 2];
    const RailCar& f = tram_->cars[0];
    at.x = c.x;
    at.y = c.y;
    at.z = c.z;
    at.yaw = f.yaw;
    // the traffic comes and goes round its last car (behind it, a "player" is in nobody's way)
    const RailCar& r = tram_->cars.back();
    setGhost(r.x, r.y, r.z, r.yaw);
  } else if (bus_) {
    // (as it is drawn: eased through the corners of its way – sim/smoothpose.ts)
    at.x = bus_->drawn ? bus_->drawn->x : bus_->x;
    at.y = bus_->y;
    at.z = bus_->drawn ? bus_->drawn->z : bus_->z;
    at.yaw = bus_->drawn ? bus_->drawn->yaw : bus_->yaw;
    setGhost(bus_->x, bus_->y, bus_->z, bus_->yaw);
  } else if (spot_) {
    at.x = spot_->x;
    at.z = spot_->z;
    at.yaw = spot_->yaw;
    const double gy = h_.ground(spot_->x, spot_->z);
    at.y = std::isfinite(gy) ? gy : at.y;
    setGhost(spot_->x, at.y, spot_->z, spot_->yaw);
  }
}

void Watcher::setGhost(double x, double y, double z, double yaw) {
  PlayerState& g = ghost;
  g.x = x;
  // (high above: the cars of the traffic neither give way to it nor run into it)
  g.y = y + 30;
  g.z = z;
  g.yaw = yaw;
}

RailVehicle* Watcher::placeTram(RailTraffic& rail, const TripState& trip) {
  const LineJson* line = h_.source->tramLine(req.line);
  const TimetablePattern& p = *trip.pattern;
  const auto stopAt = [&](int k) -> const std::pair<int, double>* {
    if (!line || k < 0 || static_cast<size_t>(k) >= p.lineStops.size()) return nullptr;
    const int i = p.lineStops[static_cast<size_t>(k)];
    return i >= 0 && static_cast<size_t>(i) < line->stops.size() ? &line->stops[static_cast<size_t>(i)] : nullptr;
  };
  const std::pair<int, double>* a = stopAt(trip.from);
  const std::pair<int, double>* b = stopAt(trip.to);
  if (!b) b = a;
  if (!a || !b) return nullptr;
  const std::optional<LineSpot> sp = rail.lineSpot(req.line, PathMark{a->first, a->second}, PathMark{b->first, b->second}, trip.standing ? 0 : trip.frac);
  if (!sp) return nullptr;
  RailVehicle* v = rail.spawnAt(sp->path, sp->s, sp->dir, trip.standing ? 0 : 6);
  if (!v) return nullptr;
  v->trip = req.trip;
  v->line = RailLineAt{.index = req.line, .k = sp->k};
  return v;
}

TrafficCar* Watcher::placeBus(Traffic& traffic) {
  const std::optional<Spot>& s = spot_;
  BusLines* lines = h_.source->busLines();
  const BusLine* line = lines && req.line >= 0 && static_cast<size_t>(req.line) < lines->lines.size() ? lines->lines[static_cast<size_t>(req.line)] : nullptr;
  if (!s || !line || !s->k) return nullptr;
  if (*s->k < 0 || static_cast<size_t>(*s->k) >= line->ways.size()) return nullptr;
  const int w = line->ways[static_cast<size_t>(*s->k)];
  if (w < 0) return nullptr;
  const LaneGraph::EdgeLanes* el = h_.lanes->edgeLanes[w >> 1];
  static const std::vector<Lane*> none;
  const std::vector<Lane*>& lanes = el ? ((w & 1) == 0 ? el->fwd : el->back) : none;
  Lane* lane = nullptr;
  for (Lane* l : lanes)
    if (l->idx == 0) {
      lane = l;
      break;
    }
  if (!lane && !lanes.empty()) lane = lanes[0];
  if (!lane) return nullptr;
  PolyProjection proj;
  projectOnPolyline(lane->edge->pl, s->x, s->z, proj);
  const double along = lane->dir > 0 ? proj.s - lane->s0 : lane->s0 - proj.s;
  TrafficCar* car = traffic.addCar(*lane, std::max(1.0, std::min(lane->len - 1, along)), true, BusOnLine{.ref = req.ref, .to = req.dest, .index = req.line, .k = *s->k, .off = 0});
  if (!car) return nullptr;
  // (it drives off at once if it is under way, not at a crawl – and keeps to its trip's times)
  car->v = trip_ && trip_->standing ? 0 : car->v;
  if (car->bus) car->bus->trip = req.trip;
  return car;
}

void Watcher::step(double dt, const Controls& c) {
  update(dt);
  if (!base_) base_ = Base{.handbrake = c.handbrake, .gear = c.gear};
  const bool touched = c.throttle > 0.25 || c.brake > 0.25 || std::abs(c.steer) > 0.4 || std::abs(c.handbrake - base_->handbrake) > 0.5 || c.gear != base_->gear;
  const bool idle = c.throttle < 0.08 && c.brake < 0.08 && std::abs(c.steer) < 0.15;
  if (!dialog_) {
    if (touched && armed_) ask();
    else if (idle) armed_ = true;
  }
  showBar();
}

void Watcher::camera(Camera& cam, const Controls& c, double dt) {
  const WatchAt& a = at;
  const bool isLong = req.kind == "tram";
  const double yaw = a.yaw + c.lookYaw;
  const double lift = std::max(0.05, std::min(1.45, 0.3 - c.lookPitch));
  // (behind its end, further back as the player zooms out)
  const double dist = std::max(length_ / 2 + (isLong ? 14 : 8), std::min(160.0, c.lookZoom * (isLong ? 2.4 : 1.7)));
  const double up = isLong ? 2.4 : 1.8;
  double tx = a.x + std::sin(yaw) * dist * std::cos(lift);
  double ty = a.y + up + dist * std::sin(lift);
  double tz = a.z + std::cos(yaw) * dist * std::cos(lift);
  const double g = h_.ground(tx, tz);
  if (std::isfinite(g)) ty = std::max(ty, g + 1.5);
  if (!camReady_) {
    cam.position.set(tx, ty, tz);
    camReady_ = true;
  } else {
    const double k = std::min(1.0, dt * 5);
    tx = cam.position.x + (tx - cam.position.x) * k;
    ty = cam.position.y + (ty - cam.position.y) * k;
    tz = cam.position.z + (tz - cam.position.z) * k;
    cam.position.set(tx, ty, tz);
  }
  cam.near = 0.3;
  cam.fov = 55;
  cam.lookAt(a.x, a.y + up, a.z);
  cam.updateProjectionMatrix();
}

void Watcher::showBar() {
  const std::optional<TripState>& trip = trip_;
  const TimetablePattern* p = trip ? trip->pattern : nullptr;
  const auto stopName = [&](int i) {
    const std::string s = p && i >= 0 && static_cast<size_t>(i) < p->stops.size() ? p->stops[static_cast<size_t>(i)] : "";
    return s.empty() ? std::string("?") : s;
  };
  const std::string next = !trip || !p          ? t("watchGone")
                           : trip->standing     ? t("transitAt", {{"stop", stopName(trip->from)}})
                                                : t("transitNext", {{"stop", stopName(trip->to)}, {"min", std::max(0.0, jsRound((trip->until - h_.source->now().s) / 60))}});
  const std::string waiting = !tram_ && !bus_ ? t("watchWaiting") : "";
  const std::string text = req.ref + "|" + req.dest + "|" + next + "|" + waiting + "|" + h_.lang();
  if (bar_ && text == barText_) return;
  barText_ = text;
  const El hud = h_.hud();
  if (!hud) return;
  if (!bar_) {
    bar_ = el("div", "watchbar");
    hud.append(bar_);
    // (what belongs to the player's own vehicle steps back meanwhile: maps.css)
    hud.addClass("watching");
  }
  const El b = bar_;
  b.replaceChildren();
  const std::string colour = lineColour(req.kind, req.ref, {req.colour});
  El badge = el("span", "bm-line " + req.kind, req.ref.empty() ? "?" : req.ref);
  badge.style("background", colour);
  badge.style("color", textOn(colour));
  El what = el("div", "wb-text");
  El title = el("b", "", t(req.kind == "tram" ? "transitTram" : "transitBus") + " " + req.ref + " → " + req.dest);
  const std::string second = waiting.empty() ? t("watchHint") : waiting;
  what.append(title, el("small", "", next.empty() ? second : second.empty() ? next : next + " · " + second));
  El take = el("button", "btn", t("watchTake"));
  take.type("button");
  take.onclick([this](dom::Event& e) {
    e.stopPropagation();
    ask();
  });
  El back = el("button", "btn ghost", t("watchBack"));
  back.type("button");
  back.onclick([this](dom::Event& e) {
    e.stopPropagation();
    // (a copy: going back ends the watching – this watcher goes meanwhile)
    const std::function<void()> fn = h_.back;
    fn();
  });
  b.append(badge, what, take, back);
}

void Watcher::ask() {
  if (dialog_) return;
  armed_ = false;
  El o = el("div", "overlay watchask");
  El p = el("div", "panel narrow");
  p.append(el("h1", "", t(req.kind == "tram" ? "watchAskTram" : "watchAskBus")), el("p", "", t("watchAskSub", {{"ref", req.ref}, {"dest", req.dest}})));
  El row = el("div", "buttons");
  El yes = el("button", "btn", t("watchYes"));
  yes.type("button");
  yes.onclick([this] {
    closeDialog();
    take();
  });
  El no = el("button", "btn ghost", t("watchNo"));
  no.type("button");
  no.onclick([this] {
    closeDialog();
    // (a copy: going back ends the watching – this watcher goes meanwhile)
    const std::function<void()> fn = h_.back;
    fn();
  });
  El keep = el("button", "btn ghost", t("watchKeep"));
  keep.type("button");
  keep.onclick([this] {
    closeDialog();
    // (what the controls are now counts as untouched)
    base_.reset();
  });
  row.append(yes, no, keep);
  p.append(row);
  o.append(p);
  h_.ui.append(o);
  dialog_ = o;
  yes.focus();
}

void Watcher::closeDialog() {
  if (dialog_) dialog_.remove();
  dialog_ = El();
}

void Watcher::take() {
  const std::optional<TripState>& trip = trip_;
  const TimetablePattern* p = trip ? trip->pattern : nullptr;
  const auto stopName = [&](int i) { return p && i >= 0 && static_cast<size_t>(i) < p->stops.size() ? p->stops[static_cast<size_t>(i)] : std::string(); };
  const std::string stop = !trip || !p ? "" : trip->standing ? stopName(trip->from) : stopName(trip->to);
  TakeOver to;
  if (req.kind == "bus") {
    const int k = bus_ && bus_->bus && bus_->bus->line ? bus_->bus->line->k : spot_ && spot_->k ? *spot_->k : -1;
    to = lineTakeOver(*h_.source, req.line, k);
  }
  // (a bus: a little to the right of the middle of its road, in its lane – so that it is put facing its way)
  const double right = bus_ ? 0 : 2;
  to.x = at.x + std::cos(at.yaw) * right;
  to.z = at.z - std::sin(at.yaw) * right;
  to.yaw = at.yaw;
  to.stop = stop;
  // (copies: taking it over ends the watching – this watcher goes meanwhile)
  const WatchRequest r = req;
  const auto fn = h_.takeOver;
  fn(r, to);
}

void Watcher::dispose() {
  closeDialog();
  if (bar_) {
    if (El parent = bar_.parentElement()) parent.removeClass("watching");
    bar_.remove();
  }
  bar_ = El();
}

}  // namespace fs
