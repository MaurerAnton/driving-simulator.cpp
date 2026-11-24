// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/flightpanel.ts @ v1.7.7
/**
 * The instrument panel of the light aeroplane (render/plane.ts), drawn on
 * canvases.  In front of the pilot the classic six – airspeed, attitude and
 * altitude in the upper row, turn coordinator, heading and vertical speed
 * below –, to their right the rev counter and the fuel and flap gauges, to
 * their left the warning lamps and the ignition key, under them the light
 * switches; in the middle of the panel the radios.  Symbols and numbers
 * only: there is nothing to translate.
 *
 * The pilot's half changes all the time and is redrawn at most 15 times a
 * second; the radios never change and are drawn once.
 */
#pragma once

#include "engine/engine.h"
#include "platform/canvas.h"

namespace fs {

/** What the instruments show (a part of the aeroplane's visual state, render/plane.ts). */
struct PanelReadings {
  double iasKt = 0;
  double altitudeFt = 0;
  double vsFpm = 0;
  double headingDeg = 0;
  double pitchDeg = 0;
  double bankDeg = 0;
  /** the ball of the turn coordinator: −1 … 1 (to the end of its tube), + = to the right */
  double slip = 0;
  double turnRateDegS = 0;
  double rpmGauge = 0;
  /** 0 … 1 */
  double fuel = 0;
  /** degrees */
  double flapsGauge = 0;
  bool stallWarning = false;
  bool parkingBrake = false;
  bool engineOn = false;
  /** the light switches below the instruments show these */
  struct Lights {
    bool nav = false;
    bool beacon = false;
    bool strobe = false;
    bool landing = false;
    bool taxi = false;
  } lights;
};

/**
 * Where the pictures lie on the panel (metres): x across the aeroplane from
 * its middle (+ to the right), v up the panel from the lower edge of the
 * pictures.  The pilot's instruments fill `left` … `split`, the radios
 * `split` … `right`.
 */
struct PanelArea {
  double left, split, right, height;
};
inline constexpr PanelArea PANEL_AREA = {-0.52, -0.02, 0.52, 0.3125};

/** the panel's paint – the 3D panel around the pictures has the same colour (render/plane.ts) */
inline constexpr unsigned PANEL_PAINT = 0x3a3f45;

class FlightPanel {
 public:
  /** the pilot's instruments (redrawn) */
  const Canvas canvas;
  TexturePtr texture;
  /** the radios in the middle and the passenger's side (drawn once) */
  TexturePtr radios;

  FlightPanel();
  FlightPanel(const FlightPanel&) = delete;
  FlightPanel& operator=(const FlightPanel&) = delete;

  /**
   * Redraw the instruments – at most 15 times a second.  True if it drew
   * (a caller that knows the panel is out of sight need not call at all).
   */
  bool update(const PanelReadings& r, double now);
  void dispose();

 private:
  Ctx2D ctx_;
  const Canvas face_;
  Canvas card_;
  double last_ = -1;

  void drawFace();
  /** The inclinometer's tube: an arc of a circle whose middle lies above the instrument's middle. */
  struct Tube {
    double cx, cy, R;
  };
  Tube tubeShape() const;
  void drawCard();
  void draw(const PanelReadings& r);
  void attitude(const Ctx2D& c, double pitch, double bank);
  void turn(const Ctx2D& c, double rate, double slip);
};

}  // namespace fs
