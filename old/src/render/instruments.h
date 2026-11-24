// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Instrument cluster: rev counter, speedometer, telltales — drawn into an
 * RGBA buffer the renderer uploads as a texture.
 *
 * Origin: src/render/instruments.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The original draws with the canvas 2D API; here the same drawing
 * goes into a software rasteriser (src/platform/canvas2d.h) so the port runs
 * without a DOM.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fs {

/** What the cluster shows this frame. */
struct DashState {
  double speedKmh = 0;
  double rpm = 0;
  int gear = 0;
  /** -1 left, 0 off, 1 right (current lamp phase is in blinkOn) */
  int indicator = 0;
  bool hazard = false;
  bool blinkOn = false;
  bool lowBeam = false;
  bool highBeam = false;
  bool handbrake = false;
  bool ignition = true;
  bool engineRunning = false;
  bool abs = false;
  bool seatbelt = false;
  double fuel = 0;
  bool automatic = false;
  /** show the engaged gear (driver aid; a real manual car has no such display) */
  bool showGear = true;
  double odometerKm = 0;
};

/** The finished cluster image (W × H RGBA). */
class InstrumentPanel {
 public:
  static constexpr int W = 768;
  static constexpr int H = 256;

  InstrumentPanel();

  /** Redraw at most ~20 times a second (as the original does). */
  void update(const DashState& s, double now);

  const std::vector<uint8_t>& pixels() const { return pixels_; }
  int width() const { return W; }
  int height() const { return H; }
  /** True when the image changed since the last call. */
  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }

 private:
  std::vector<uint8_t> face_;    // static dial faces, drawn once
  std::vector<uint8_t> pixels_;  // the composed image
  double last_ = -1e9;
  bool dirty_ = false;

  void drawFace();
};

}  // namespace fs
