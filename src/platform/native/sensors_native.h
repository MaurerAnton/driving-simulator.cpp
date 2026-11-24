// SPDX-License-Identifier: AGPL-3.0-only
/**
 * platform/sensors.h on the native host: what a test sets – the gamepads that are
 * plugged in, whether the device has an orientation sensor and what its owner
 * answers when asked, whether the events that follow count as the user's own.
 */
#pragma once

#include <string>
#include <vector>

#include "platform/sensors.h"

namespace fs::plat::native {

/** everything below as it is at the start: no gamepad, no sensor, events are trusted, no lines kept */
void resetSensors();
/** what getGamepads() answers */
void setGamepads(std::vector<GamepadState> pads);
/** a pad with `axes` axes at rest and `buttons` buttons let go (to be changed and handed to setGamepads) */
GamepadState gamepad(const std::string& id, const std::string& mapping, int axes, int buttons);
/** what isTrusted() answers from now on (true at the start; false: the events dispatched next are a script's) */
void setTrusted(bool on);
/**
 * The orientation sensor: is there one (hasDeviceOrientation), must it be asked for
 * (deviceOrientationAsks), and what the asking answers a moment later (after
 * plat::native::pump()): 'granted', 'denied' – or 'throw': the promise is rejected.
 */
void setDeviceOrientation(bool has, bool asks = false, const std::string& answer = "granted");
/** how often requestDeviceOrientationPermission() was called */
int permissionAsked();
/** the lines written with plat::debug (they are kept, not printed) */
const std::vector<std::string>& debugLines();

}  // namespace fs::plat::native
