// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/cranepanel.ts @ v1.7.7
/**
 * The crane's display and its controls (plain DOM, like the HUD):
 *
 *  - the load moment limiter's display: the load on the hook against what
 *    the load chart allows here, as a bar (amber from 90 %, red at 100 %,
 *    when the limiter cuts off), the radius, the boom's length and angle,
 *    the hook's height, the swing, the wind at the boom head against the
 *    crane's limit, the carrier's slope as a bubble level, the outrigger
 *    setting, and how far the crane is from tipping;
 *  - the controls, as on a crane's two master switches: the left one slews
 *    (sideways) and hoists (forwards and back) – on the loader crane it
 *    moves the inner boom –, the right one luffs the boom (runs the trolley
 *    of the tower crane, moves the loader crane's outer boom) and
 *    telescopes.  On a keyboard A D / W S and the arrow keys, Shift for
 *    creep speed; a gamepad's two sticks; on a touch screen two joysticks
 *    under the thumbs.  Buttons (and keys) for slinging and letting go of
 *    the load, the creep speed, the outriggers, the jacks, the levelling,
 *    and for changing between driving and working the crane.
 *
 * The game (game/craneplay.ts) reads the controls each step (`read`) and
 * gives the display what the crane measures (`update`).
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "i18n/index.h"
#include "platform/dom.h"
#include "sim/crane.h"

namespace fs {

/** The two master switches (−1 … 1; y + = pushed away), and what is held of the rest. */
struct CraneControls {
  double lx = 0;
  double ly = 0;
  double rx = 0;
  double ry = 0;
  bool fine = false;
  double beams = 0;
  double jacks = 0;
  bool level = false;
};

/** What the display shows besides the limiter's readings. */
struct CraneInfo {
  CraneType type = "tower";
  /** the crane is being worked (else: driven) */
  bool operating = false;
  /** a load hangs on the hook / one is within reach of it */
  bool hooked = false;
  bool canAttach = false;
  /** the swing (m), the hook's height above what is under it (m) */
  double swing = 0;
  /** the outrigger beams out (least, 0 … 1), the pads down, the carrier off its wheels, its slope along its x and z (deg) */
  double beams = 0;
  bool padsDown = false;
  bool onOutriggers = false;
  double slopeX = 0;
  double slopeZ = 0;
  /** the boom is in its rest (folded, for the loader crane) */
  bool stowed = false;
  /** the crane may be driven now (folded, outriggers in), and stands still with its brake on */
  bool canDrive = false;
  bool canOperate = false;
  /** creep speed is on */
  bool fine = false;
};

/** the labels round a joystick ('up' | 'down' | 'left' | 'right') */
struct CraneStickLabels {
  dom::El up, down, left, right;
};

class CranePanel {
 public:
  dom::El root;
  /** the crane is being worked: the keys are the crane's */
  bool active = false;
  /** sling the load or let it go */
  std::function<void()> onAttach = [] {};
  /** from driving to working the crane and back */
  std::function<void()> onToggle = [] {};
  /** something was pressed (for a click) */
  std::function<void()> onClick = [] {};

  CranePanel(const dom::El& parent, std::function<std::string()> lang);
  // (the page's listeners name this object: it stays where it is)
  CranePanel(const CranePanel&) = delete;
  CranePanel& operator=(const CranePanel&) = delete;
  ~CranePanel();

  /** Show it for a crane (or hide it: null). */
  void show(const std::optional<CraneType>& type);

  /** Fingers or keys: the joysticks, or the reminder of the keys ('touch' | 'keys'). */
  void setMode(const std::string& mode);

  /**
   * The controls now: the keys (ramped), a gamepad's sticks, the joysticks
   * on the screen; the set-up held on the buttons, the keys 1 … 5 or the
   * gamepad's cross; the creep speed.
   */
  CraneControls read(double dt);

  /** The creep speed (as the display shows it). */
  bool fine() const;

  /** Show what the limiter measures and what can be done now. */
  void update(const Lmi& l, const CraneInfo& info, double now);

  void dispose();

  /** A joystick on the screen: a ring with a knob that follows the thumb and springs back to the middle. */
  class Stick {
   public:
    dom::El el;
    double x = 0;
    double y = 0;
    Stick(const std::string& cls, const CraneStickLabels& labels);
    Stick(const Stick&) = delete;
    Stick& operator=(const Stick&) = delete;
    void release();

   private:
    dom::El knob;
    int id = -1;
    void move(const dom::Event& e);
  };

 private:
  std::function<std::string()> lang;
  CraneType type = "tower";
  dom::El lmiBox;
  dom::El bar;
  dom::El barText;
  dom::El vals;
  dom::El msg;
  dom::El level;
  dom::El bubble;
  dom::El tipBar;
  dom::El setup;
  dom::El acts;
  dom::El attachBtn;
  dom::El fineBtn;
  dom::El modeBtn;
  dom::El keysBox;
  CraneStickLabels leftLabels;
  CraneStickLabels rightLabels;
  std::unique_ptr<Stick> left;
  std::unique_ptr<Stick> right;
  std::vector<dom::El> setupButtons;
  /** what the set-up buttons on the screen hold */
  struct {
    double beams = 0;
    double jacks = 0;
    bool level = false;
  } touchHold;
  /** the creep speed switched on (button, gamepad); Shift holds it */
  bool fineOn = false;
  /** the keys ramp up rather than jump (a master switch is moved, not flicked) */
  struct {
    double lx = 0, ly = 0, rx = 0, ry = 0;
  } keyAxes;
  std::vector<bool> padPrev;
  std::string mode = "keys";
  std::string shownLang;
  std::string shownType;
  double lastDom = 0;
  /** the page's listeners on window (taken away again with the panel) */
  int keydownId = 0, keyupId = 0, blurId = 0;

  std::string t(const std::string& key, const TrParams& p = {}) const;
  /** A number the way the language writes it (always Western digits). */
  std::string num(double v, int digits = 1) const;
  /** the texts of the buttons and the joysticks' labels, in the language chosen (again when it changes) */
  void label();
};

}  // namespace fs
