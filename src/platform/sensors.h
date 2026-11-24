// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the player's controls need of the browser besides the page (dom.h) and the
 * device (plat.h): the phone's orientation sensor and its permission, the gamepads
 * as the browser lists them (with their mapping and which buttons count as pressed),
 * whether an event is the user's own, the console's quiet line.
 *
 * Declarations only: the browser's side is platform/web, this machine's is
 * platform/native/sensors_native.cpp (settable from tests: sensors_native.h).
 * Used by input/input.cpp and ui/tiltsteer.cpp.
 */
#pragma once

#include <string>
#include <vector>

#include "core/task.h"
#include "platform/dom.h"

namespace fs::plat {

// --- the console -------------------------------------------------------------

/** console.debug(text): a line the console shows only when asked for all of them */
void debug(const std::string& text);

// --- events ------------------------------------------------------------------

/**
 * event.isTrusted, for an event the page handed to a handler: true when the user
 * did it (a real key, a real finger), false when a script dispatched it.
 * (An Event the game makes itself – `new KeyboardEvent(…)` in the original – never
 * comes here: it is untrusted by its making.)
 */
bool isTrusted(const dom::Event& e);

// --- the orientation of the device -------------------------------------------
// (the readings themselves come as the window's 'deviceorientation' events:
//  dom::Event's alpha, beta, gamma, hasOrientation)

/** `'DeviceOrientationEvent' in window`: does this browser know the sensor at all? */
bool hasDeviceOrientation();
/** `typeof DeviceOrientationEvent.requestPermission === 'function'`: must the browser be asked first (iOS)? */
bool deviceOrientationAsks();
/**
 * `await DeviceOrientationEvent.requestPermission()`, to be called from a gesture:
 * 'granted' or 'denied'.  Where the browser's promise is rejected, the task ends
 * with an exception (or answers ''): both are taken as refused.
 */
Task<std::string> requestDeviceOrientationPermission();

// --- gamepads ----------------------------------------------------------------

/** gamepad.buttons[i]: `pressed` as the browser decides it (a trigger counts from a little way in), `value` 0 … 1 */
struct GamepadButton {
  bool pressed = false;
  double value = 0;
};

/** One gamepad as navigator.getGamepads() has it – with what plat::Gamepad leaves out: `mapping` and the buttons' `pressed`. */
struct GamepadState {
  /** gamepad.index: its slot */
  int index = 0;
  /** gamepad.id */
  std::string id;
  /** gamepad.mapping: 'standard', or '' for a device the browser has no layout for (steering wheels) */
  std::string mapping;
  /** gamepad.axes */
  std::vector<double> axes;
  /** gamepad.buttons */
  std::vector<GamepadButton> buttons;
};

/** navigator.getGamepads(): the pads that are there and `connected`, in the order of their slots (empty: none, or the browser has no gamepads) */
std::vector<GamepadState> getGamepads();

}  // namespace fs::plat
