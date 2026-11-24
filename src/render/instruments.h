// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Instrument cluster drawn into a canvas texture: rev counter, speedometer, telltales.
 *
 * Origin: src/render/instruments.ts @ v1.7.7
 */
#pragma once

#include "engine/engine.h"
#include "platform/canvas.h"

namespace fs {

struct DashState {
  double speedKmh = 0;
  double rpm = 0;
  int gear = 0;
  /** -1 left, 0 off, 1 right (current lamp phase is in `blinkOn`) */
  int indicator = 0;
  bool hazard = false;
  bool blinkOn = false;
  bool lowBeam = false;
  bool highBeam = false;
  bool handbrake = false;
  bool ignition = false;
  bool engineRunning = false;
  bool abs = false;
  bool seatbelt = false;
  double fuel = 0;
  bool automatic = false;
  /** show the engaged gear (driver aid; a real manual car has no such display) */
  bool showGear = false;
  double odometerKm = 0;
};

class InstrumentPanel {
 public:
  const Canvas canvas;
  TexturePtr texture;

  InstrumentPanel();
  InstrumentPanel(const InstrumentPanel&) = delete;
  InstrumentPanel& operator=(const InstrumentPanel&) = delete;

  /** Redraw at most ~20 times a second. */
  void update(const DashState& s, double now);

 private:
  Ctx2D ctx_;
  const Canvas face_;
  double last_ = 0;

  /** Static dial faces, drawn once. */
  void drawFace();
};

}  // namespace fs
