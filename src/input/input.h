// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/input/input.ts @ v1.7.7
/**
 * Driver input from keyboard, gamepad, steering wheel and touch.
 *
 * Keys are bound by physical position (KeyboardEvent.code), so the layout
 * is the same on QWERTZ and QWERTY keyboards.
 */
#pragma once

#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "input/camzoom.h"
#include "input/steerfeel.h"
#include "platform/dom.h"

namespace fs {

struct Settings;   // game/settings.h

struct Controls {
  double steer = 0;
  double throttle = 0;
  double brake = 0;
  double clutch = 0;
  double handbrake = 0;
  /** manual: -1 R, 0 N, 1…6; automatic: -2 P, -1 R, 0 N, 1 D */
  int gear = 0;
  bool ignition = false;
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
  /** distance of the camera behind the car in metres (mouse wheel) */
  double lookZoom = 0;
  /** which device is steering: decides how much assistance the input layer adds ('keyboard' | 'gamepad' | 'wheel' | 'touch') */
  std::string device;
};

/** door: get out of the vehicle, or in again (on foot beside it) */
/** mirror: look into the next mirror, large over the picture (ui/mirrorzoom.ts) */
/** service: fill up at a pump, wash at a car wash, call for help with an empty tank, skip the loading of a truck (game/serviceworld.ts) */
/** 'view' | 'pause' | 'reset' | 'turn' | 'map' | 'help' | 'reality' | 'door' | 'panto' | 'mirror' | 'service' | 'wiper' | 'open' | 'hud' */
using Action = std::string;

class Input {
 public:
  Controls c{
      .steer = 0,
      .throttle = 0,
      .brake = 0,
      .clutch = 0,
      .handbrake = 1,
      .gear = 0,
      .ignition = true,
      .starter = false,
      .indicator = 0,
      .hazard = false,
      .lowBeam = false,
      .highBeam = false,
      .horn = false,
      .seatbelt = false,
      .lookYaw = 0,
      .lookPitch = 0,
      .lookZoom = 7,
      .device = "keyboard",
  };
  /** touch state, written by the on-screen controls */
  /** the controls on the screen: the steering bar (and, flying, its height: the stick pulled or pushed, + pulled), the pedals */
  struct Touch {
    bool active = false;
    double steer = 0;
    double pitch = 0;
    double throttle = 0;
    double brake = 0;
    double clutch = 0;
  };
  Touch touch;
  /**
   * What the driver is using right now: fingers on the screen or keys ('touch' | 'keys').
   * It follows the last real input, so a tablet with a keyboard works both ways.
   */
  std::string mode;
  /**
   * The camera circles the car (view from behind): looking around has no stops,
   * and the view swings back behind the car by itself once the car is driving.
   * False: the driver's head (stops at the shoulders).
   */
  bool orbit = false;
  /**
   * Where the camera circling the vehicle rests (radians, 0: behind it): it
   * starts there and swings back there – the forklift's view from the side
   * (game/forkliftplay.ts).  Set by `resetLook`.
   */
  double orbitRest = 0;
  /**
   * The free camera: fingers and mouse move the camera itself (see `cam`)
   * instead of turning a head or circling the car, and it stays where it is put.
   */
  bool freeCam = false;
  /**
   * The free camera flies (game/flycam.ts – set by the game while it is the view): the keys W A S D, Q E,
   * the arrows and Page Up / Down fly it without Alt, the wheel and a pinch set its speed, two fingers
   * lift or lower it – and the vehicle is held where it is.
   */
  bool flyCam = false;
  /** the stick on the screen (ui/touchstick.ts): to the right (+) and forwards (+), −1 … 1 each */
  struct Stick {
    double x = 0;
    double y = 0;
  };
  Stick stick;
  /** on foot: running switched on (Caps Lock, the runner's button by the stick) */
  bool runToggle = false;
  /** suspend driving input (menus) */
  bool enabled = true;
  std::string lastGamepadId;
  /**
   * A crane is being worked (game/craneplay.ts): its controls (ui/cranepanel.ts) take the keys and the
   * gamepad – only the camera (V), the map (M) and the door key (G: back to driving) stay the game's.
   */
  bool crane = false;
  /** zooming the view (input/camzoom.ts): pinch, the wheel, + and − – while `viewZoom` (set by the game: on foot, the driver's seat) */
  CamZoom zoom;
  bool viewZoom = false;
  /** how far the outside camera can be moved away (m) – more for a truck with a trailer */
  /** how far the camera behind the vehicle goes back as usual (the vehicle's own measure) – scrolled on, ZOOM_FAR times that */
  double zoomMax = 18;

  Input(dom::El canvas, const Settings& settings);
  // (the page's listeners name this object: it stays where it is)
  Input(const Input&) = delete;
  Input& operator=(const Input&) = delete;

  /**
   * On foot: forwards (-1 … 1), turning (+ = to the left) and running, from
   * whatever is in use – the keys W S A D (Shift to run), the pedals and the
   * steering bar on the screen, a gamepad's stick and triggers.  (Not from
   * the controls of the vehicle: its steering wheel stays where it is when
   * nobody holds it.)
   */
  struct Walk {
    double fwd = 0;
    double turn = 0;
    double side = 0;
    bool run = false;
  };
  /** `mode`: 'classic' | 'tank' | 'tap' */
  Walk walkInput(const std::string& mode = "tank");

  /**
   * A forklift's mast (game/forkliftplay.ts): the forks up (+) or down, the
   * mast tilted back (+) or forwards, the side-shift right (+) or left, each
   * −1 … 1 – from Page Up / Page Down, Home / End and Insert / Delete (the
   * buttons on the screen press the same keys), and a gamepad's right stick
   * (up and down: the forks) and its pad (left and right: the tilt).
   */
  struct Fork {
    double lift = 0;
    double tilt = 0;
    double shift = 0;
  };
  Fork forkInput();

  /**
   * Flying: the stick (pitch + = pulled, roll + = to the right), the rudder
   * pedals (+ = right), which way the throttle is moved (+ open, − close) and
   * the wheel brakes – from the arrow keys (the stick), W S (the throttle),
   * A D (the rudder) and Space (the brakes); the steering bar on the screen as
   * the stick, its pedals as the throttle (the left one brakes on the ground
   * too); a gamepad's left stick and its triggers.
   */
  struct Flight {
    double pitch = 0;
    double roll = 0;
    double yaw = 0;
    double throttle = 0;
    double brake = 0;
  };
  Flight flightInput();

  /**
   * What was asked of the free camera since it was last read (takeCam):
   * turning in radians, moving sideways / forwards / up and towards what it
   * looks at in parts of the distance to it – by dragging, pinching and the
   * wheel – and flying with the keys (Alt held) in seconds of key time.
   */
  struct Cam {
    double yaw = 0;
    double pitch = 0;
    double right = 0;
    double fwd = 0;
    double dolly = 0;
    double keyRight = 0;
    double keyFwd = 0;
    double keyUp = 0;
    bool fast = false;
    bool reset = false;
    double lift = 0;
    double speed = 0;
  };
  /** What was asked of the free camera since the last call (see `cam`); the object is reused. */
  const Cam& takeCam();

  /** Where the glance keys (or their buttons) turn the head now: left, right, back over the shoulder (radians, 0: ahead). */
  double glanceYaw() const;

  /** The camera was changed: look straight ahead again (circling the vehicle: from `rest`, 0 behind it). */
  void resetLook(bool orbit, bool freeCam = false, double rest = 0);

  /** The car that is being steered (for how much lock the keys may ask for at speed). */
  /** `spec`: the whole vehicle – a heavy one is steered slower and more gently (input/steerfeel.ts). */
  void setSteering(double wheelbase, double maxWheelAngle, const SteerSpec* spec = nullptr);
  void setSteering(double wheelbase, double maxWheelAngle, const SteerSpec& spec) { setSteering(wheelbase, maxWheelAngle, &spec); }

  void onAction(std::function<void(const Action& a)> fn);

  /** The on-screen controls are being used. */
  void touched();

  /** Let go of the on-screen controls (the tilt steering was switched off): the keys take over again. */
  void untouched();

  /** A button on the screen stands in for a key (down / up). */
  void press(const std::string& code, bool down);

  void setAutomatic(bool automatic);

  /**
   * The headlight flasher (the stalk pulled towards one): the main beam lit
   * while O is held – a short tap switches the main beam instead – or while
   * the flasher's own button on the screen is held (its code 'Flash').
   */
  bool flasher() const;

  /** The washer: sprays while its key (7) or button is held. */
  bool washer() const;

  /**
   * Sample all devices.  `speed` (m/s) scales the keyboard steering,
   * `selfAlign` (-1…1) is the tyre feedback from the vehicle model.
   */
  Controls& update(double dt, double speed, double selfAlign);

 private:
  /** what a gamepad says now ('standard' | 'wheel') */
  struct Pad {
    std::string kind;
    double steer = 0;
    double pitch = 0;
    double throttle = 0;
    double brake = 0;
    double clutch = 0;
    double lookX = 0;
    std::vector<bool> buttons;
    bool starter = false;
    bool horn = false;
  };
  /** a finger on the view */
  struct Finger {
    double x = 0;
    double y = 0;
  };

  std::unordered_set<std::string> keys;
  const Settings& settings;
  std::vector<std::function<void(const Action& a)>> actions;
  double mouseLookYaw = 0;
  double mouseLookPitch = 0;
  bool dragging = false;
  bool indicatorArmed = false;
  std::vector<bool> prevButtons;
  int gearMin = -1;
  int gearMax = 6;
  /** a finger glance: the view swings back when the finger lifts */
  bool glance = false;
  int lookId = -1;
  double lookX = 0;
  double lookY = 0;
  Cam cam;
  Cam camOut;
  /** fingers on the view (in the order they came down, as the original's Map keeps them) */
  std::vector<std::pair<int, Finger>> fingers;
  double pinch = 0;
  double lookIdle = 0;
  /** how the keys, the bar and a stick move the steering for this vehicle (input/steerfeel.ts) */
  SteerFeel feel = steerFeel();
  /** when O went down (ms, performance.now) */
  double oDownAt = 0;

  Finger* finger(int pointerId);
  double fingerSpread() const;
  /** Looking around with the mouse or a finger; the stops depend on what is turning: a head or a camera. */
  void turnLook(double dYaw, double dPitch);
  void fire(const Action& a);
  /**
   * One step up (+1) or down (−1) of the gear lever.  A manual gearbox counts
   * R N 1 … 6.  The selector of an automatic is P (−2) R (−1) N (0) D (1),
   * but up from P or R goes towards D – what one wants nearly every time –
   * and down from D through N to R, then P: drive comes before reverse.
   */
  int shift(int g, int dir) const;
  /** `trusted`: the event's isTrusted (a real key – the on-screen buttons send untrusted ones) */
  void onKey(dom::Event& e, bool down, bool trusted);
  bool key(std::initializer_list<const char*> codes) const;
  bool anyDrivingKey() const;
  std::optional<Pad> readPad();
  void applyPadButtons(const std::vector<bool>& buttons, bool wheel);
};

}  // namespace fs
