// SPDX-License-Identifier: AGPL-3.0-only
/** platform/sensors.h on the native host: answers that a test sets (sensors_native.h). */
#include "platform/native/sensors_native.h"

#include <stdexcept>

#include "platform/plat.h"

namespace fs::plat {

namespace {

struct Sensors {
  std::vector<GamepadState> pads;
  bool trusted = true;
  bool orientation = false;
  bool asks = false;
  std::string answer = "granted";
  int asked = 0;
  std::vector<std::string> lines;
};

Sensors& sensors() {
  static Sensors s;
  return s;
}

}  // namespace

void debug(const std::string& text) { sensors().lines.push_back(text); }

bool isTrusted(const dom::Event&) { return sensors().trusted; }

bool hasDeviceOrientation() { return sensors().orientation; }

bool deviceOrientationAsks() { return sensors().orientation && sensors().asks; }

Task<std::string> requestDeviceOrientationPermission() {
  Sensors& s = sensors();
  s.asked++;
  // (answered later, as a browser does – when its owner has tapped the question away)
  Resolver<std::string> r;
  const std::string answer = s.answer;
  soon([r, answer] {
    if (answer == "throw") r.reject(std::make_exception_ptr(std::runtime_error("NotAllowedError")));
    else r.resolve(answer);
  });
  return r.task();
}

std::vector<GamepadState> getGamepads() { return sensors().pads; }

namespace native {

void resetSensors() { sensors() = Sensors(); }

void setGamepads(std::vector<GamepadState> pads) { sensors().pads = std::move(pads); }

GamepadState gamepad(const std::string& id, const std::string& mapping, int axes, int buttons) {
  GamepadState g;
  g.id = id;
  g.mapping = mapping;
  g.axes.assign(static_cast<size_t>(axes), 0.0);
  g.buttons.assign(static_cast<size_t>(buttons), GamepadButton());
  return g;
}

void setTrusted(bool on) { sensors().trusted = on; }

void setDeviceOrientation(bool has, bool asks, const std::string& answer) {
  Sensors& s = sensors();
  s.orientation = has;
  s.asks = asks;
  s.answer = answer;
}

int permissionAsked() { return sensors().asked; }

const std::vector<std::string>& debugLines() { return sensors().lines; }

}  // namespace native

}  // namespace fs::plat
