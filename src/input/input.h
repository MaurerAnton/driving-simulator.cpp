// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Driver input from keyboard, gamepad, steering wheel and touch.
 *
 * Keys are bound by physical position (KeyboardEvent.code), so the layout is
 * the same on QWERTZ and QWERTY keyboards.
 *
 * Origin: src/input/input.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The device readers (keyboard, gamepad, wheel, touch) are split into a small
 * `Platform` interface so the same control maths runs in the browser (via
 * Emscripten's HTML5 input) and in the native build (injected events).
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "game/settings.h"

namespace fs {

struct Controls {
  double steer = 0;
  double throttle = 0;
  double brake = 0;
  double clutch = 0;
  double handbrake = 1;
  /** manual: -1 R, 0 N, 1…6; automatic: -2 P, -1 R, 0 N, 1 D */
  int gear = 0;
  bool ignition = true;
  bool starter = false;
  int indicator = 0;
  bool hazard = false;
  bool lowBeam = false;
  bool highBeam = false;
  bool horn = false;
  bool seatbelt = false;
  /** head turn target in radians (+ = left) and pitch */
  double lookYaw = 0;
  double lookPitch = 0;
  /** which device is steering: decides how much assistance the input layer adds */
  std::string device = "keyboard";
};

enum class Action { View, Pause, Reset, Map, Help, Reality };

/** What one gamepad frame reports (the JS `readPad` result). */
struct PadState {
  bool present = false;
  bool wheel = false;      // mapping !== 'standard' and a known wheel id
  double steer = 0, throttle = 0, brake = 0, clutch = 0;
  double lookX = 0;
  std::vector<bool> buttons;
  bool starter = false;
  bool horn = false;
  std::string id;
};

/** The platform hooks the input layer needs from the host. */
class Platform {
 public:
  virtual ~Platform() = default;
  /** Keys currently held, as KeyboardEvent.code strings. */
  virtual bool key(const std::string& code) const = 0;
  /** One gamepad frame, or nothing when no pad is connected. */
  virtual PadState readPad() = 0;
  /** Mouse-look deltas accumulated since the last call (drag to look). */
  virtual double mouseLookYawDelta() = 0;
  virtual double mouseLookPitchDelta() = 0;
  /** Whether the pointer is currently dragging the view. */
  virtual bool dragging() const = 0;
  /** On-screen touch controls, written by the HUD. */
  virtual bool touchActive() const = 0;
  virtual double touchSteer() const = 0;
  virtual double touchThrottle() const = 0;
  virtual double touchBrake() const = 0;
};

class Input {
 public:
  Controls c;
  /** suspend driving input (menus) */
  bool enabled = true;
  std::string lastGamepadId;

  Input(Platform& platform, const Settings& settings);

  void onAction(const std::function<void(Action)>& fn) { actions_.push_back(fn); }
  void setAutomatic(bool automatic) {
    gearMin_ = automatic ? -2 : -1;
    gearMax_ = automatic ? 1 : 6;
    c.gear = automatic ? -2 : 0;
  }

  /** Handle one key event (down = pressed). `repeat` mirrors KeyboardEvent.repeat. */
  void onKey(const std::string& code, bool down, bool repeat);

  /**
   * Sample all devices.  `speed` (m/s) scales the keyboard steering,
   * `selfAlign` (-1…1) is the tyre feedback from the vehicle model.
   */
  Controls update(double dt, double speed, double selfAlign);

 private:
  Platform& platform_;
  Settings settings_;
  std::vector<std::function<void(Action)>> actions_;
  std::vector<std::string> keys_;
  double mouseLookYaw_ = 0;
  double mouseLookPitch_ = 0;
  bool dragging_ = false;
  bool indicatorArmed_ = false;
  std::vector<bool> prevButtons_;
  int gearMin_ = -1;
  int gearMax_ = 6;

  void fire(Action a);
  bool keyDown(const std::string& code) const;
  bool anyDrivingKey() const;
  void applyPadButtons(const std::vector<bool>& buttons, bool wheel);
};

/** JavaScript Math.sign: -1, 0 or 1. */
inline int signOf(double v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

/** approach(cur, target, maxStep): move at most maxStep towards target. */
double approach(double cur, double target, double maxStep);

}  // namespace fs
