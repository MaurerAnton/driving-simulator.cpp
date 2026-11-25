// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/pointwatch.ts @ v1.7.7
#include "game/pointwatch.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "engine/engine.h"
#include "input/input.h"

namespace fs {

using dom::El;
using dom::el;

PointWatcher::PointWatcher(WatchTarget target, PointWatchHost host) : target_(std::move(target)), h_(std::move(host)) {
  update();
  showBar();
}

PointWatcher::~PointWatcher() { dispose(); }

void PointWatcher::update() {
  const std::optional<WatchAt> p = target_.get();
  gone_ = !p;
  if (!p) return;
  at.x = p->x;
  at.y = std::isfinite(p->y) ? p->y : h_.ground(p->x, p->z);
  at.z = p->z;
  at.yaw = p->yaw;
  ghost.x = p->x;
  ghost.y = at.y + 60;
  ghost.z = p->z;
  ghost.yaw = p->yaw;
}

void PointWatcher::step(double, const Controls& c) {
  update();
  if (sub_) sub_.textContent(gone_ ? h_.goneText() : subText());
  if (!target_.take || gone_) return;
  // touching a driving control asks whether to take it over (as with a tram or a bus: spectate.ts)
  if (!base_) base_ = Base{.handbrake = c.handbrake, .gear = c.gear};
  const bool touched = c.throttle > 0.25 || c.brake > 0.25 || std::abs(c.steer) > 0.4 || std::abs(c.handbrake - base_->handbrake) > 0.5 || c.gear != base_->gear;
  const bool idle = c.throttle < 0.08 && c.brake < 0.08 && std::abs(c.steer) < 0.15;
  if (!dialog_) {
    if (touched && armed_) ask();
    else if (idle) armed_ = true;
  }
}

std::string PointWatcher::subText() const {
  const std::string& s = target_.sub;
  if (!target_.take) return s;
  const std::string hint = h_.t("watchHint", {});
  return s.empty() ? hint : hint.empty() ? s : s + " · " + hint;
}

void PointWatcher::ask() {
  if (dialog_ || !target_.take) return;
  armed_ = false;
  El o = el("div", "overlay watchask");
  El p = el("div", "panel narrow");
  p.append(el("h1", "", h_.t("watchAskVehicle", {})), el("p", "", h_.t("watchAskVehicleSub", {{"what", target_.title}})));
  El row = el("div", "buttons");
  El yes = el("button", "btn", h_.t("watchYes", {}));
  yes.type("button");
  yes.onclick([this] {
    closeDialog();
    if (!gone_ && target_.take) {
      // (a copy: taking it over ends the watching – this watcher goes meanwhile)
      const std::function<void()> take = target_.take;
      take();
    }
  });
  El no = el("button", "btn ghost", h_.t("watchNo", {}));
  no.type("button");
  no.onclick([this] {
    closeDialog();
    const std::function<void()> back = h_.back;
    back();
  });
  El keep = el("button", "btn ghost", h_.t("watchKeep", {}));
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

void PointWatcher::closeDialog() {
  if (dialog_) dialog_.remove();
  dialog_ = El();
}

void PointWatcher::camera(Camera& cam, const Controls& c, double dt) {
  const WatchAt& a = at;
  const double yaw = a.yaw + c.lookYaw;
  const double lift = std::max(0.05, std::min(1.45, 0.35 - c.lookPitch));
  const double dist = std::max(target_.length / 2 + 8, std::min(220.0, c.lookZoom * 1.7));
  double tx = a.x + std::sin(yaw) * dist * std::cos(lift);
  double ty = a.y + 1.8 + dist * std::sin(lift);
  double tz = a.z + std::cos(yaw) * dist * std::cos(lift);
  const double g = h_.ground(tx, tz);
  if (std::isfinite(g)) ty = std::max(ty, g + 1.5);
  if (!camReady_) {
    cam.position.set(tx, ty, tz);
    camReady_ = true;
  } else {
    const double k = 1 - std::exp(-dt * 5);
    tx = cam.position.x + (tx - cam.position.x) * k;
    ty = cam.position.y + (ty - cam.position.y) * k;
    tz = cam.position.z + (tz - cam.position.z) * k;
    cam.position.set(tx, ty, tz);
  }
  cam.near = 0.3;
  cam.fov = 55;
  cam.lookAt(a.x, a.y + 1.8, a.z);
  cam.updateProjectionMatrix();
}

void PointWatcher::showBar() {
  const El hud = h_.hud();
  if (!hud || bar_) return;
  El b = el("div", "watchbar");
  El text = el("div", "watch-text");
  sub_ = el("small", "", subText());
  text.append(el("b", "", target_.title), sub_);
  if (target_.take) {
    El take = el("button", "btn", h_.t("watchTake", {}));
    take.type("button");
    take.onclick([this](dom::Event& e) {
      e.stopPropagation();
      if (!gone_ && target_.take) {
      // (a copy: taking it over ends the watching – this watcher goes meanwhile)
      const std::function<void()> take = target_.take;
      take();
    }
    });
    b.append(text, take);
  } else b.append(text);
  El back = el("button", "btn ghost", h_.backText());
  back.type("button");
  back.onclick([this] {
    const std::function<void()> fn = h_.back;
    fn();
  });
  b.append(back);
  hud.append(b);
  hud.addClass("watching");
  bar_ = b;
}

void PointWatcher::dispose() {
  closeDialog();
  if (bar_) bar_.remove();
  bar_ = El();
  if (const El hud = h_.hud()) hud.removeClass("watching");
}

}  // namespace fs
