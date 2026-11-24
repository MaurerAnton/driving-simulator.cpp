// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/openplay.ts @ v1.7.7
#include "game/openplay.h"

#include "i18n/index.h"
#include "input/input.h"
#include "render/car.h"
#include "sim/vehicle/types.h"

namespace fs {

namespace {

std::string seatText(const SeatId& seat) {
  return seat == "driver" ? "seatDriver" : seat == "front" ? "seatFront" : seat == "rearLeft" ? "seatRearLeft" : "seatRearRight";
}

/** (a Float32Array keeps them: as 32-bit numbers) */
double f32(double v) { return static_cast<float>(v); }

}  // namespace

void OpenPlay::reset(PlayerCar* car) {
  openings.reset();
  board = nullptr;
  spots_.clear();
  setSeat(car, "driver");
}

void OpenPlay::setSeat(PlayerCar* car, const SeatId& s) {
  seat = s;
  held_.reset();
  const std::array<double, 3> e = seatEye(s);
  if (car) car->eye.set(e[0], e[1], e[2]);
}

void OpenPlay::walk(OnFoot& w, const WalkVehicle& wv, PlayerCar* car, bool truck) {
  const SpotUnit* u = !wv.units.empty() ? &wv.units[0] : nullptr;
  std::vector<OpeningSpot> spots;
  if (u && car) {
    const CarShell& sh = car->exterior.shell;
    for (const OpeningSpot& s : carSpots(*u, {.frontAhead = wv.door, .rearDoorRear = sh.zAxleR - 0.255, .nose = sh.zFront, .tail = sh.zRear})) spots.push_back(s);
  } else if (u && truck) spots.push_back(rearSpot("body", *u));
  if (wv.units.size() > 1) spots.push_back(rearSpot("trailer", wv.units[1]));
  spots_ = spots;
  spotsFor_ = &w;
  const Boarding* b = board.get();
  if (b && b->way == "out") {
    for (const OpeningSpot& s : spots)
      if (s.id == b->door) {
        w.x = s.x;
        w.z = s.z;
        break;
      }
  }
}

std::optional<OpeningSpot> OpenPlay::at(const OnFoot* w) const {
  return w && w == spotsFor_ ? nearestSpot(spots_, w->x, w->z) : std::nullopt;
}

std::optional<OpeningId> OpenPlay::toggleAt(const OnFoot* w) {
  const std::optional<OpeningSpot> s = at(w);
  if (!s || board) return std::nullopt;
  openings.toggle(s->id);
  return s->id;
}

void OpenPlay::beginIn(const DoorId& door) { board = std::make_unique<Boarding>("in", door); }

void OpenPlay::beginOut() { board = std::make_unique<Boarding>("out", doorOfSeat(seat)); }

BoardStep OpenPlay::step(double dt, const std::function<void(const std::string& kind)>& click) {
  std::vector<OpeningEvent>& ev = events_;
  ev.clear();
  openings.step(dt, ev);
  for (const OpeningEvent& e : ev) click(e.what == "opening" ? "door" : "doorShut");
  Boarding* b = board.get();
  if (!b) return {};
  const BoardStep r = b->step(dt, openings);
  if (r.done) board = nullptr;
  return r;
}

void OpenPlay::pose(PlayerCar* car) {
  const Openings& o = openings;
  if (car) {
    for (size_t i = 0; i < CAR_OPENINGS.size(); i++) carValues_[i] = f32(o.get(CAR_OPENINGS[i]));
    car->setOpenings(carValues_);
  }
  // (later: truck.body.setDoors, setTrailerOpen – render/truck.ts, render/rearopen.ts)
}

void OpenPlay::idle(Controls& c, DriverInput& d, bool demo, double parkedSteer) {
  if (!passenger()) return;
  c.steer = d.steer = parkedSteer;
  c.throttle = d.throttle = 0;
  c.brake = d.brake = 0;
  c.clutch = d.clutch = 0;
  d.starter = false;
  if (demo) {
    c.handbrake = d.handbrake = 0;
    held_.reset();
    return;
  }
  if (!held_) held_ = Held{c.gear, c.ignition};
  c.handbrake = d.handbrake = 1;
  c.gear = d.gear = held_->gear;
  c.ignition = d.ignition = held_->ignition;
}

void OpenPlay::buttons(const dom::El& parent, const std::vector<OpenButton>& list, const std::function<void(const std::string& id)>& onTap) {
  if (!ui_) {
    if (list.empty()) return;
    ui_ = std::make_unique<OpenButtons>(parent, onTap);
  }
  ui_->show(list);
}

std::optional<WalkHud> OpenPlay::walkHud(const OnFoot* w, bool touch, const std::string& lang, bool canEnter) const {
  const std::optional<OpeningSpot> s = at(w);
  if (!s || board) return std::nullopt;
  const Openings& o = openings;
  const std::vector<FootAct> acts = footActs(s->id, o, canEnter && isDoor(s->id));
  const std::string what = label(s->id, o.wanted(s->id), lang);
  std::vector<OpenButton> buttons;
  for (const FootAct& a : acts) buttons.push_back({a, a == "enter" ? tr("getInSeat", lang) : what});
  if (isDoor(s->id) && canEnter) {
    const std::string seatName = tr(seatText(s->id == "fl" ? "driver" : s->id == "fr" ? "front" : s->id == "rl" ? "rearLeft" : "rearRight"), lang);
    return WalkHud{tr(touch ? "doorTipTouch" : o.wanted(s->id) ? "doorTipOpen" : "doorTip", lang, {{"seat", seatName}}), "KeyG", buttons};
  }
  return WalkHud{tr(touch ? "openTipTouch" : "openTip", lang, {{"what", what}}), OPEN_KEY, buttons};
}

std::string OpenPlay::seatTip(bool touch, const std::string& lang, bool canGetOut) const {
  const std::string seatName = tr(seatText(seat), lang);
  return canGetOut ? tr(touch ? "seatTipTouch" : "seatTip", lang, {{"seat", seatName}}) : tr("seatRiding", lang, {{"seat", seatName}});
}

std::string OpenPlay::label(const OpeningId& id, bool open, const std::string& lang) const {
  const std::string k = isDoor(id) ? "Door" : id == "bonnet" ? "Bonnet" : id == "boot" ? "Boot" : id == "body" ? "Rear" : "Trailer";
  return tr((open ? "close" : "open") + k, lang);
}

}  // namespace fs
