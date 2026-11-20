// SPDX-License-Identifier: AGPL-3.0-only
/** Input checks: key bindings, speed-dependent steering, ramping. */
#include "../src/input/input.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace fs;

namespace {
/** Test platform: a held-key set plus an injectable gamepad. */
class TestPlatform : public Platform {
 public:
  std::vector<std::string> held;
  PadState pad;
  double yawDelta = 0, pitchDelta = 0;
  bool drag = false;
  bool touch = false;
  double tSteer = 0, tThrottle = 0, tBrake = 0;

  bool key(const std::string& code) const override {
    for (const auto& k : held) if (k == code) return true;
    return false;
  }
  PadState readPad() override { return pad; }
  double mouseLookYawDelta() override { double d = yawDelta; yawDelta = 0; return d; }
  double mouseLookPitchDelta() override { double d = pitchDelta; pitchDelta = 0; return d; }
  bool dragging() const override { return drag; }
  bool touchActive() const override { return touch; }
  double touchSteer() const override { return tSteer; }
  double touchThrottle() const override { return tThrottle; }
  double touchBrake() const override { return tBrake; }
};
}  // namespace

int main() {
  Settings st;
  TestPlatform p;
  Input in(p, st);

  // gear keys
  in.onKey("KeyE", true, false);
  assert(in.c.gear == 1);
  in.onKey("KeyE", true, false);
  assert(in.c.gear == 2);
  in.onKey("KeyR", true, false);
  assert(in.c.gear == -1);
  in.onKey("KeyN", true, false);
  assert(in.c.gear == 0);
  in.onKey("Digit3", true, false);
  assert(in.c.gear == 3);

  // toggles
  in.onKey("KeyL", true, false);
  assert(in.c.lowBeam);
  in.onKey("KeyO", true, false);
  assert(in.c.highBeam);
  in.onKey("KeyL", true, false);   // turning low beam off clears high beam too
  assert(!in.c.lowBeam && !in.c.highBeam);
  in.onKey("KeyZ", true, false);
  assert(in.c.indicator == -1);
  in.onKey("KeyZ", true, false);
  assert(in.c.indicator == 0);
  in.onKey("KeyX", true, false);
  assert(in.c.hazard);
  in.onKey("KeyB", true, false);
  assert(in.c.seatbelt);

  // handbrake on space, then update with no keys: throttle/brake fall back to 0
  in.onKey("Space", true, false);
  assert(in.c.handbrake == 0);
  Controls c = in.update(1.0 / 60, 0, 0);
  assert(c.throttle == 0 && c.brake == 0);

  // keyboard steering: at standstill the wheel winds to full lock quickly
  p.held = {"KeyA"};
  for (int i = 0; i < 120; i++) c = in.update(1.0 / 60, 0, 0);
  assert(c.steer < -0.9);
  // hands off, standing still: the wheel stays where it was
  p.held.clear();
  Controls after = in.update(1.0 / 60, 0, 0);
  assert(std::fabs(after.steer - c.steer) < 1e-6);
  // at 30 m/s the same keys give far less lock: the limit falls with speed
  // (1/(1+(v/9)^2) → 0.083 at 30 m/s, and the wheel settles there)
  p.held = {"KeyA"};
  for (int i = 0; i < 240; i++) c = in.update(1.0 / 60, 30, 0);
  assert(c.steer < -0.06 && c.steer > -0.12);

  // actions fire
  int fired = 0;
  in.onAction([&](Action a) { if (a == Action::View) fired++; });
  in.onKey("KeyV", true, false);
  assert(fired == 1);

  std::printf("input: all checks passed\n");
  return 0;
}
