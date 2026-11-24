// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/tiltsteer.ts @ v1.7.7
#include "ui/tiltsteer.h"

#include <cmath>
#include <string>

#include "core/js.h"
#include "platform/plat.h"
#include "platform/sensors.h"

namespace fs {

namespace {

double clamp01(double v) { return v < -1 ? -1 : v > 1 ? 1 : v; }

}  // namespace

double steerFromTilt(double gammaDeg, double neutralDeg) {
  if (!std::isfinite(gammaDeg)) return 0;
  return clamp01((gammaDeg - neutralDeg) / TILT_DEG);
}

TiltSteer::TiltSteer(std::function<void(double steer)> out_) : out(std::move(out_)) {}

void TiltSteer::onOrient(dom::Event& e) {
  // (e.gamma === null || e.beta === null: an event without readings)
  if (!e.hasOrientation) return;
  // (the first reading calibrates: what the phone is held at then is straight ahead)
  if (!value) {
    neutralG = e.gamma;
    neutralB = e.beta;
  }
  // (a phone turned left/right usually moves gamma; held another way – flat, or in a mount –
  //  it moves beta instead.  Whichever strays further from where it was held is the turn.)
  const double dg = e.gamma - neutralG;
  const double db = e.beta - neutralB;
  const double axis = std::abs(db) > std::abs(dg) + 4 ? db : dg;
  value = steerFromTilt(neutralG + axis, neutralG);
  // (a line in the console now and then, so a phone one cannot see into still says whether
  //  the sensor speaks and what it says – console.debug, once a second at most)
  const double now = plat::dateMs();
  if (now - lastLog > 1000) {
    lastLog = now;
    plat::debug("[tilt] gamma " + toFixed(e.gamma, 1) + "° (Δ" + toFixed(dg, 1) + ") beta " + toFixed(e.beta, 1) + "° (Δ" + toFixed(db, 1) + ") → steer " + toFixed(*value, 2));
  }
  out(*value);
}

Task<bool> TiltSteer::start() {
  if (listener) co_return true;
  if (!plat::hasDeviceOrientation()) co_return false;
  // (the listener is attached at once: on Android and most browsers the events come with no
  //  asking.  iOS asks a permission that only a gesture may be given for, so there it is asked
  //  at the first touch or key – until then the events simply do not arrive, and the setting
  //  stays on rather than being turned off for want of a gesture at start-up)
  value = std::nullopt;   // (the next event calibrates)
  listener = dom::window().on("deviceorientation", [this](dom::Event& e) { onOrient(e); });
  plat::debug("[tilt] listening for deviceorientation");
  if (plat::deviceOrientationAsks()) {
    const std::shared_ptr<std::array<int, 2>> asking = std::make_shared<std::array<int, 2>>();
    dom::ListenOptions capture;
    capture.capture = true;
    (*asking)[0] = dom::window().on("pointerdown", [this, asking](dom::Event&) { ask(asking).detach(); }, capture);
    (*asking)[1] = dom::window().on("keydown", [this, asking](dom::Event&) { ask(asking).detach(); }, capture);
  }
  co_return true;
}

Task<void> TiltSteer::ask(std::shared_ptr<std::array<int, 2>> asking) {
  dom::window().off("pointerdown", (*asking)[0], true);
  dom::window().off("keydown", (*asking)[1], true);
  try {
    if ((co_await plat::requestDeviceOrientationPermission()) != "granted") stop();
  } catch (...) {
    stop();
  }
}

void TiltSteer::stop() {
  if (listener) dom::window().off("deviceorientation", listener);
  listener = 0;
  if (value) out(0);
  value = std::nullopt;
}

}  // namespace fs
