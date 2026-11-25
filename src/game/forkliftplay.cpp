// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/forkliftplay.ts @ v1.7.7
#include "game/forkliftplay.h"

#include <cmath>
#include <numeric>

#include "audio/audio.h"
#include "core/js.h"
#include "game/settings.h"
#include "input/input.h"
#include "render/forklift.h"
#include "render/forkliftyard.h"
#include "sim/vehicle/vehicle.h"
#include "ui/forkliftpanel.h"
#include "ui/hud.h"

namespace fs {

namespace {

/** the forklift's simulation step */
constexpr double STEP = 1.0 / 120;
constexpr double DEG_ = PI / 180;

/** the place's drawing: one for the whole game, made the first time (kept to the end: its meshes outlive the statics) */
ForkYardScene* yardScene = nullptr;

ForkYardScene& sceneOf(const ForkliftHost& host) {
  if (!yardScene) {
    yardScene = new ForkYardScene();
    host.scene->add(yardScene->group);
  }
  return *yardScene;
}

/** The middle of the place in the yard's frame (its floor's height is taken there). */
constexpr double MIDDLE_U = -12;
constexpr double MIDDLE_W = -2;

/** '★★☆' */
std::string stars(int n) {
  std::string s;
  for (int i = 0; i < n; i++) s += "★";
  for (int i = 0; i < 3 - n; i++) s += "☆";
  return s;
}

}  // namespace

std::optional<ForkPlace> forkliftStart(const YardFrame* yard) {
  if (!yard) return std::nullopt;
  const auto [x, z] = yard->toWorld(FORK_START.u, FORK_START.w);
  return ForkPlace{x, z, yard->yawToWorld(FORK_START.yaw)};
}

void forkliftMenuSite(const ForkliftHost& host, bool on, const std::function<double(double, double)>& heightAt) {
  if (!on || !host.yard) {
    if (yardScene) yardScene->hide();
    return;
  }
  const auto [mx, mz] = host.yard->toWorld(MIDDLE_U, MIDDLE_W);
  const double y0 = heightAt(mx, mz);
  if (std::isnan(y0)) return;
  ForkYardScene& s = sceneOf(host);
  s.show(*host.yard, y0, host.settings->quality == "low");
  const ForkSite site = buildSite(*host.yard, y0, heightAt);
  s.update(site.pallets, std::nullopt, ForkYardTargets{}, 0);
}

ForkliftWork::ForkliftWork(ForkliftHost host, bool site) : siteOn(site), host_(std::move(host)), scene_(sceneOf(host_)) {
  Input* input = host_.input;
  GameAudio* audio = host_.audio;
  const Settings* settings = host_.settings;
  panel_ = std::make_unique<ForkPanel>(
      host_.ui, [settings] { return settings->lang; }, [input](const std::string& code, bool down) { input->press(code, down); }, [audio] { audio->click("tick"); });
  host_.ui.addClass("forklifting");
}

ForkliftWork::~ForkliftWork() = default;

// ---------------------------------------------------------------------------- the place

bool ForkliftWork::layout(Vehicle& v) {
  if (world_) return true;
  if (time_ - laidTry_ < 0.5) return false;
  laidTry_ = time_;
  v.getModelPosition(mpos_.data());
  if (!siteOn) {
    // a free drive: no place laid out – the ground where it stands, nothing else of its own
    const double y0 = host_.heightAt(mpos_[0], mpos_[2]);
    if (std::isnan(y0)) return false;
    auto heightAt = host_.heightAt;
    const auto ground = [heightAt, y0](double x, double z) {
      const double h = heightAt(x, z);
      return std::isfinite(h) ? h : y0;
    };
    world_ = ForkWorld{.ground = ground, .obstacles = {}, .shelves = {}, .pallets = {}};
    f.place(mpos_[0], ground(mpos_[0], mpos_[2]), mpos_[2], v.yaw);
    return true;
  }
  // (no practice yard: the place round where the drive began, as if the yard were there)
  if (!host_.yard && !ownFrame_) ownFrame_ = std::make_unique<YardFrame>(YardJson{.x = mpos_[0] - FORK_START.u, .z = mpos_[2] - FORK_START.w, .len = 120, .wid = 60, .rot = 0});
  const YardFrame& frame = host_.yard ? *host_.yard : *ownFrame_;
  const auto [mx, mz] = frame.toWorld(MIDDLE_U, MIDDLE_W);
  const double y0 = host_.heightAt(mx, mz);
  if (std::isnan(y0)) return false;
  auto heightAt = host_.heightAt;
  const auto ground = [heightAt, y0](double x, double z) {
    const double h = heightAt(x, z);
    return std::isfinite(h) ? h : y0;
  };
  site_ = std::make_unique<ForkSite>(buildSite(frame, y0, ground));
  world_ = ForkWorld{.ground = ground, .obstacles = site_->obstacles, .shelves = site_->shelves, .pallets = site_->palletPtrs()};
  tasks = std::make_unique<ForkTasks>(*site_);
  scene_.show(frame, y0, host_.settings->quality == "low");
  f.place(mpos_[0], ground(mpos_[0], mpos_[2]), mpos_[2], v.yaw);
  return true;
}

// ---------------------------------------------------------------------------- each frame

void ForkliftWork::step(double dt, Vehicle& v, const Controls& c) {
  time_ += dt;
  v.engineRunning = true;
  if (!layout(v)) {
    v.speed = 0;
    return;
  }
  const ForkWorld& w = *world_;
  const Input::Fork mast = host_.input->forkInput();
  const int autoGear = c.gear;
  ForkCommand cmd;
  cmd.throttle = c.throttle;
  cmd.brake = c.brake;
  cmd.dir = autoGear == 1 ? 1 : autoGear == -1 ? -1 : 0;
  cmd.hold = c.handbrake > 0.5 || autoGear == -2;
  cmd.steer = c.steer;
  cmd.lift = mast.lift;
  cmd.tilt = mast.tilt;
  cmd.shift = mast.shift;
  // the steps, and what happened in them all
  ForkEvents& ev = ev_;
  ev = ForkEvents{};
  acc_ = std::min(acc_ + dt, 0.1);
  const double px = f.x, pz = f.z, pyaw = f.yaw;
  while (acc_ >= STEP) {
    const ForkEvents& e = f.step(STEP, cmd, w);
    ev.hit = std::max(ev.hit, e.hit);
    if (e.picked) {
      ev.picked = e.picked;
      ev.pickedDepth = e.pickedDepth;
    }
    if (e.placed) ev.placed = e.placed;
    ev.relief = ev.relief || e.relief;
    ev.rearLift = ev.rearLift || e.rearLift;
    ev.sideLift = ev.sideLift || e.sideLift;
    ev.tipped = ev.tipped || e.tipped;
    acc_ -= STEP;
  }
  // (a free drive: a building or a wall of the map stops it where it stood – unless it stood in one already)
  const auto& bl = host_.blocked;
  if (!siteOn && bl && (f.x != px || f.z != pz || f.yaw != pyaw) && bl(f.x, f.y, f.z, f.yaw) && !bl(px, f.y, pz, pyaw)) {
    ev.hit = std::max(ev.hit, std::abs(f.v));
    f.x = px;
    f.z = pz;
    f.yaw = pyaw;
    f.v = 0;
  }
  if (std::abs(f.v) > 0.5) moved_ = true;
  // the game's vehicle where the forklift is (the cameras, the speedometer, the sound follow it)
  v.teleport(f.x, f.y, f.z, f.yaw);
  v.speed = f.v;
  v.gear = int(cmd.dir);
  v.odometer += std::abs(f.v) * dt;
  const double work = std::min(1.0, cmd.throttle + std::abs(mast.lift) * 0.8 + std::abs(mast.tilt) * 0.4 + std::abs(mast.shift) * 0.3);
  v.rpm += (v.spec.idleRpm * (1 + work * 1.4) - v.rpm) * std::min(1.0, dt * 4);
  v.engineLoad = work;
  sounds(ev, dt, cmd);
  faults(ev, dt);
  updateTasks();
}

void ForkliftWork::sounds(const ForkEvents& ev, double dt, const ForkCommand& cmd) {
  GameAudio& a = *host_.audio;
  if (ev.hit > 0.3) a.click(ev.hit > 2 ? "crash" : "bump");
  if (ev.picked || ev.placed) a.click("clack");
  // the reversing alarm, and the warning when a wheel lifts
  beepT_ -= dt;
  const bool lifting = f.pitch > 0.01 || f.roll > 0.01;
  if ((lifting || (cmd.dir < 0 && f.v < -0.1)) && beepT_ <= 0 && f.tipped.empty()) {
    a.click("beep");
    beepT_ = lifting ? 0.25 : 0.7;
  }
}

void ForkliftWork::note(const std::string& id, const Severity& severity, const ViolationData& data, double cooldown) {
  host_.note(id, severity, f.x, f.z, data, cooldown);
  if (tasks) tasks->fault(severity);
}

void ForkliftWork::say(const std::string& key, double seconds, const TrParams& data) {
  tip_ = key;
  tipData_ = data;
  tipUntil_ = time_ + seconds;
}

void ForkliftWork::faults(const ForkEvents& ev, double dt) {
  const Pallet* load = f.load;
  const double speed = std::abs(f.v);
  if (ev.tipped) {
    note("forkliftTipped", "fail", {}, 60);
    say(f.tipped == "forward" ? "forkTip.tippedForward" : "forkTip.tippedSide", 6);
    return;
  }
  if (ev.picked) {
    const double c = f.loadCentre();
    const double allowed = capacityAt(c);
    if (ev.picked->def.mass > allowed && overloadTold_ != ev.picked->id) {
      overloadTold_ = ev.picked->id;
      const double kg = jsRound(ev.picked->def.mass);
      const double max = jsRound(allowed / 10) * 10;
      note("forkliftOverload", "major", {{"kg", kg}, {"max", max}}, 3);
      say("forkTip.overload", 5, {{"kg", kg}, {"max", max}});
    }
    if (ev.pickedDepth < 0.8) {
      note("forkliftForksIn", "minor", {{"p", jsRound(ev.pickedDepth * 100)}}, 6);
      say("forkTip.forksIn", 4);
    }
  }
  if (ev.placed && overloadTold_ == ev.placed->id) overloadTold_ = "";
  if (ev.relief) say("forkTip.relief", 3);
  if (ev.rearLift) {
    note("forkliftRearLift", "major", {}, 4);
    say("forkTip.rearLift", 4);
  }
  if (ev.sideLift) {
    note("forkliftTurnFast", "major", {{"kmh", jsRound(speed * 3.6)}}, 4);
    say("forkTip.sideLift", 4);
  }
  if (ev.hit > 0.7) note("forkliftHit", ev.hit > 2 ? "major" : "minor", {{"kmh", jsRound(ev.hit * 3.6)}}, 3);
  // driving: the load low, the mast back, not too fast round the turns
  warnT_ = std::max(0.0, warnT_ - dt);
  const ForkStability st = f.stability();
  if (st.side > 0.8 && !ev.sideLift && f.roll == 0) note("forkliftTurnFast", "minor", {{"kmh", jsRound(speed * 3.6)}}, 6);
  if (load && speed > 0.6) {
    const double above = load->y - f.y;
    if (above > 0.5) {
      note("forkliftLoadHigh", "minor", {{"cm", jsRound(above * 100)}}, 8);
      if (warnT_ <= 0) {
        say("forkTip.loadHigh", 3);
        warnT_ = 4;
      }
    }
    if (f.tilt < -1 * DEG_) note("forkliftTiltForward", "minor", {}, 8);
  } else if (!load && speed > 1.2 && f.lift > 0.6)
    note("forkliftForksHigh", "hint", {}, 10);
}

ForkSnapshot ForkliftWork::snapshot() const {
  ForkSnapshot s;
  s.time = time_;
  s.forks = f.forks();
  s.lift = f.lift;
  s.tilt = f.tilt;
  s.speed = f.v;
  s.ground = f.y;
  s.load = f.load;
  if (site_)
    for (const Pallet& p : site_->pallets) s.pallets.push_back(&p);
  s.moment = f.stability().forward;
  s.tipped = !f.tipped.empty();
  return s;
}

void ForkliftWork::updateTasks() {
  if (ForkTasks* t = tasks.get()) {
    const ForkUpdate ev = t->update(snapshot());
    GameAudio& a = *host_.audio;
    if (ev.stepDone) a.click("chime");
    if (ev.taskDone) {
      const ForkResult& r = t->results.back();
      say("forkTip.taskDone", 4, {{"stars", stars(r.stars)}, {"score", r.score}});
    }
    if (ev.allDone && overAt_ < 0) overAt_ = time_ + 4;
  }
  // (tipped over – on the yard or on a free drive – the drive is over)
  if (!f.tipped.empty() && overAt_ < 0) overAt_ = time_ + 5;
  if (overAt_ > 0 && time_ >= overAt_) over = true;
}

// ---------------------------------------------------------------------------- what is shown

void ForkliftWork::render(ForkliftModel& model, double now, bool inside, bool shown) {
  panel_->root.hidden(!shown);
  model.root->updateMatrixWorld(true);
  model.setOperator(!inside);
  model.pose(f, now);
  ForkTasks* t = tasks.get();
  if (!t || !site_) return;
  const ForkTargets tg = t->targets();
  std::optional<ForkLean> lean;
  if (f.load) lean = ForkLean{f.yaw, f.tilt};
  scene_.update(site_->pallets, lean, ForkYardTargets{.pallets = tg.pallets, .zone = tg.zone, .bed = tg.bed, .slot = tg.zone == &site_->topSlot}, now);
}

void ForkliftWork::tell(const std::string& key, double seconds) { say(key, seconds); }

std::optional<ForkExit> ForkliftWork::getOff() const {
  if (std::abs(f.v) > 0.3 && f.tipped.empty()) return std::nullopt;
  return forkliftExit(f.x, f.z, f.yaw);
}

bool ForkliftWork::canBoard(double x, double z) const { return nearForklift(f.x, f.z, x, z); }

ForkView ForkliftWork::nextView() {
  view = nextForkView(view);
  return view;
}

void ForkliftWork::hud(HudState& hs, bool touch, const std::optional<std::array<double, 2>>& walker) {
  const std::string& lang = host_.settings->lang;
  if (walker) {
    // on foot: the game's tips stay (and, near the seat, how to get on again)
    if (hs.tip.empty() && canBoard((*walker)[0], (*walker)[1])) {
      hs.tip = tr(touch ? "forkTip.getOnTouch" : "forkTip.getOn", lang);
      hs.tipAction = "KeyG";
    }
    return;
  }
  panel_->setMode(touch);
  const ForkStability st = f.stability();
  const Pallet* l = f.load;
  panel_->update({.moment = std::max(st.forward, f.pitch > 0 ? 1.0 : 0.0),
                  .side = std::max(st.side, f.roll > 0 ? 1.0 : 0.0),
                  .load = l ? l->def.mass : 0,
                  .allowed = l ? capacityAt(f.loadCentre()) : FORKLIFT.capacity,
                  .lift = f.lift,
                  .tilt = f.tilt / DEG_});
  ForkTasks* t = tasks.get();
  if (!siteOn) {
    /* (a free drive: the street-name block is the road's) */
  } else if (t) {
    const ForkInstruction ins = t->instruction(snapshot());
    hs.navText = tr(ins.key, lang, ins.data);
    const std::string progress = t->stage == "guide" ? tr("forkProgressGuide", lang) : t->stage == "done" ? "" : tr("forkProgress", lang, {{"n", t->task + 1}, {"max", t->taskCount()}});
    const std::string hint = !ins.hint.empty() ? tr(ins.hint, lang) : "";
    hs.navSub = progress.empty() ? hint : hint.empty() ? progress : progress + " · " + hint;
  } else {
    hs.navText = tr("forkLoading", lang);
    hs.navSub = "";
  }
  hs.navArrow = 0;
  hs.navActive = false;
  if (siteOn) {
    hs.limit = -1;
    hs.junction = std::nullopt;
  }
  // (the road's tips do not fit here: back onto the road, getting out)
  hs.tip = "";
  hs.tipAction = "";
  const Controls& c = host_.input->c;
  if (time_ < tipUntil_) hs.tip = tr(tip_, lang, tipData_);
  else if ((siteOn ? t && t->stage == "guide" && t->step == 0 : !moved_) && std::abs(f.v) < 0.1 && (c.handbrake > 0.5 || c.gear != 1))
    hs.tip = tr(touch ? "forkTip.startTouch" : "forkTip.start", lang);
  else if (moved_ && f.tipped.empty() && std::abs(f.v) < 0.05 && c.handbrake > 0.5) {
    // parked with its brake on: getting off (on a touch screen the tip is the button)
    hs.tip = tr(touch ? "getOutHintTouch" : "getOutHint", lang);
    hs.tipAction = "KeyG";
  }
}

std::optional<dom::El> ForkliftWork::resultNode() const {
  const ForkTasks* t = tasks.get();
  if (!t || t->results.empty()) return std::nullopt;
  const std::string& lang = host_.settings->lang;
  dom::El box = dom::el("div", "crane-results");
  double sum = 0;
  for (const ForkResult& r : t->results) sum += r.score;
  const double total = jsRound(sum / double(t->results.size()));
  dom::El head = dom::el("p", "crane-total");
  head.textContent(tr("forkResult.total", lang, {{"done", int(t->results.size())}, {"max", t->taskCount() + 1}, {"score", total}}));
  box.append(head);
  for (size_t i = 0; i < t->results.size(); i++) {
    const ForkResult& r = t->results[i];
    dom::El row = dom::el("div", std::string("fault ") + (r.stars >= 2 ? "hint" : r.stars == 1 ? "minor" : "major"));
    dom::El b = dom::el("b");
    b.textContent(std::to_string(i + 1) + " · " + tr(r.text, lang) + "  " + stars(r.stars));
    dom::El small = dom::el("small");
    const int min = int(std::floor(r.time / 60));
    const int sec = int(jsRound(std::fmod(r.time, 60)));
    const std::string secs = sec < 10 ? "0" + std::to_string(sec) : std::to_string(sec);
    small.textContent(tr("forkResult.line", lang, {{"score", r.score}, {"time", std::to_string(min) + ":" + secs}, {"minor", r.minor}, {"major", r.major}, {"peak", jsRound(r.peak * 100)}}));
    row.append(b);
    row.append(small);
    box.append(row);
  }
  return box;
}

void ForkliftWork::dispose() {
  scene_.hide();
  panel_->dispose();
  host_.ui.removeClass("forklifting");
}

}  // namespace fs
