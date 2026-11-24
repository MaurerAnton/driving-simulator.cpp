// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/tiltsteer.ts @ v1.7.7
/**
 * Steering by tilting the phone (settings: tiltSteer) on a touch screen
 * (input/input.h).  The phone's left/right tilt – DeviceOrientationEvent's
 * `gamma`, degrees about the front-back axis, negative when tilted left – is
 * turned into the steer −1 … +1 the on-screen bar would give, and written into
 * `input.touch.steer` while it runs, so the driving reads it exactly as it
 * reads the bar.
 *
 * The browser's caveats, as they are:
 *
 *  - iOS (Safari and anything WebKit) asks for permission the first time:
 *    `DeviceOrientationEvent.requestPermission()` from a gesture – the toggle
 *    in the settings is that gesture.  Android and desktop never ask.
 *  - Some devices fire no event without motion (a phone lying still may send
 *    nothing until it moves); the listener simply waits.
 *  - The event's `gamma` is only meaningful with the phone held in the usual
 *    portrait way; a phone flat on its back reports ~0 – the steering then
 *    centres itself, which is what the driver wants anyway.
 *
 * The tilt is calibrated when it is switched on: the angle held then is
 * straight ahead (`neutral`), and the steer is the difference, scaled so a
 * comfortable hand tilt (TILT_DEG) reaches full lock.  Let go of the phone and
 * it steers as it hangs – to be switched off again for a phone in a pocket.
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>

#include "core/task.h"
#include "platform/dom.h"

namespace fs {

/** Degrees of tilt from straight ahead that reach full lock (a comfortable hand angle). */
inline constexpr double TILT_DEG = 30;

/** What the driver needs from the phone: the phone's left/right tilt, and a way to ask for it. */
class TiltSource {
 public:
  virtual ~TiltSource() = default;
  /** the current tilt −1 … +1 (negative: tilted left), or null when nothing has arrived yet */
  virtual std::optional<double> steer() const = 0;
  /** start listening (asks the browser where it asks); false: no sensors / permission refused */
  virtual Task<bool> start() = 0;
  /** stop listening and centre the steer */
  virtual void stop() = 0;
};

/**
 * The steer −1 … +1 from the tilt: the difference to the calibrated angle,
 * scaled so `TILT_DEG` of tilt is full lock.  `gamma` is null on a device that
 * reports nothing (a phone on its back): no steering then (null).
 */
double steerFromTilt(double gammaDeg, double neutralDeg);

/** The listeners and state of the tilt steering, when the setting is on. */
class TiltSteer : public TiltSource {
 public:
  /** `out`: where the steer goes every event (input.touch.steer) */
  explicit TiltSteer(std::function<void(double steer)> out);
  // (the page's listeners name this object: it stays where it is)
  TiltSteer(const TiltSteer&) = delete;
  TiltSteer& operator=(const TiltSteer&) = delete;

  std::optional<double> steer() const override { return value; }

  Task<bool> start() override;

  void stop() override;

 private:
  /** the window's 'deviceorientation' listener (0: none) */
  int listener = 0;
  std::optional<double> value = std::nullopt;
  /** where the steer goes every event (input.touch.steer); null: not wired */
  const std::function<void(double steer)> out;
  double lastLog = 0;
  double neutralG = 0;
  double neutralB = 0;

  void onOrient(dom::Event& e);
  /** the first touch or key after start(): the gesture the permission is asked from (`asking`: its two listeners, pointerdown and keydown) */
  Task<void> ask(std::shared_ptr<std::array<int, 2>> asking);
};

}  // namespace fs
