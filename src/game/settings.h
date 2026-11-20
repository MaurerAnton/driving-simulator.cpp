// SPDX-License-Identifier: AGPL-3.0-only
/**
 * User settings, persisted in localStorage (in the browser; in the native
 * build they live in memory for the process).
 *
 * Origin: src/game/settings.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 */
#pragma once

#include <string>
#include <vector>

namespace fs {

enum class Transmission { Assist, Manual, Automatic };
enum class Quality { Low, Medium, High };
enum class Mirrors { Off, Alternate, Full };
enum class LangSetting { De, En };

struct WheelMapping {
  int steerAxis = 0;
  int throttleAxis = 2;
  int brakeAxis = 5;
  int clutchAxis = 1;
  /** pedal axes rest at +1 and go to -1 when pressed (typical for wheels) */
  bool pedalsInverted = true;
  /** degrees of wheel rotation lock to lock as configured in the wheel's driver */
  double wheelRange = 900;
  /** button indices of an H-shifter: [1st … 6th, reverse] */
  std::vector<int> shifter{12, 13, 14, 15, 16, 17, 18};
};

struct Settings {
  LangSetting lang = LangSetting::De;
  Transmission transmission = Transmission::Assist;
  Quality quality = Quality::Medium;
  /** off | one mirror per frame | all mirrors every frame */
  Mirrors mirrors = Mirrors::Alternate;
  /** show the current speed limit and the next priority sign in the HUD */
  bool hudRules = true;
  /** show the engaged gear in the instrument cluster */
  bool hudGear = true;
  /** spoken instructor (browser speech synthesis) */
  bool voice = false;
  double volume = 0.7;
  /** traffic density 0 … 1 */
  double traffic = 0.6;
  double drawDistance = 1300;
  WheelMapping wheel;
  bool invertLookY = false;
  /**
   * Reality view: which street-level imagery services may be asked for
   * pictures of the place the car is at.  All off until the player says so –
   * a request tells the service the player's IP address and the place.
   */
  struct Reality {
    bool panoramax = false;
    bool kartaview = false;
    bool mapillary = false;
    std::string mapillaryToken;
  } reality;
  /**
   * Driving controls on the screen (steering bar, pedals, buttons):
   * 'auto' shows them while the screen is being touched and hides them as soon
   * as a keyboard is used; 'on' / 'off' force them.
   */
  /** stored as the original's 'auto' | 'on' | 'off' */
  std::string touchControls = "auto";
  /** how solid the on-screen controls are drawn, 0.15 … 1 */
  double touchOpacity = 0.55;
  /** short reminder of the keys while standing or crawling (keyboard only) */
  bool keyHints = true;
};

/** The defaults the TypeScript DEFAULT_SETTINGS starts from. */
Settings defaultSettings();

/** Read the persisted settings (falling back to defaults, as the JS does). */
Settings loadSettings();

/** Persist the settings (best effort, like the JS try/catch). */
void saveSettings(const Settings& s);

}  // namespace fs
