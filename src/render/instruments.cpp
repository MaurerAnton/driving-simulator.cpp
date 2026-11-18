// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Instrument cluster: rev counter, speedometer, telltales.
 *
 * Origin: src/render/instruments.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The dial geometry, the needle shape and the lamp positions are
 * carried over; the drawing goes through the software canvas.
 */
#include "render/instruments.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "core/geom.h"
#include "platform/canvas2d.h"

namespace fs {


InstrumentPanel::InstrumentPanel() : face_(static_cast<size_t>(W) * H * 4, 0),
                                      pixels_(static_cast<size_t>(W) * H * 4, 0) {
  drawFace();
}

void InstrumentPanel::drawFace() {
  Canvas2D c(W, H);
  c.clear(parseColor("#07090b"));

  auto dial = [&](double cx, double cy, double r, double max, double major, double minor,
                  const std::function<std::string(double)>& label, const char* unit,
                  double redFrom) {
    c.fillCircle(cx, cy, r, parseColor("#101418"));
    c.strokeCircle(cx, cy, r, parseColor("#3a4047"), 3);
    const double a0 = PI * 0.75;
    const double span = PI * 1.5;
    for (double v = 0; v <= max + 1e-6; v += minor) {
      double a = a0 + (v / max) * span;
      bool isMajor = std::fabs(v / major - std::round(v / major)) < 1e-6;
      double r0 = r * (isMajor ? 0.8 : 0.87);
      uint32_t col = v >= redFrom ? parseColor("#e03a2a") : parseColor("#e8ecef");
      c.line(cx + std::cos(a) * r0, cy + std::sin(a) * r0,
             cx + std::cos(a) * r * 0.94, cy + std::sin(a) * r * 0.94, col,
             isMajor ? 3.2 : 1.6);
      if (isMajor) {
        c.fillText(label(v), cx + std::cos(a) * r * 0.65, cy + std::sin(a) * r * 0.65,
                   std::round(r * 0.17), col);
      }
    }
    c.fillText(unit, cx, cy + r * 0.38, std::round(r * 0.13), parseColor("#9aa3ab"));
  };

  dial(150, 128, 116, 7000, 1000, 250,
       [](double v) { return std::to_string(static_cast<long>(v / 1000)); }, "x1000/min", 6500);
  dial(W - 150, 128, 116, 240, 20, 10,
       [](double v) { return std::to_string(static_cast<long>(v)); }, "km/h", 1e300);

  face_ = c.pixels();
  pixels_ = face_;   // the first frame is the bare dial face
}

void InstrumentPanel::update(const DashState& s, double now) {
  if (now - last_ < 0.045) return;
  last_ = now;
  Canvas2D c(W, H);
  // start from the static face
  std::copy(face_.begin(), face_.end(), pixels_.begin());
  Canvas2D face(W, H);
  face.pixels() = face_;
  c.drawImage(face, 0, 0);

  bool lit = s.ignition;
  auto needle = [&](double cx, double cy, double r, double frac) {
    double a = PI * 0.75 + std::max(0.0, std::min(1.02, frac)) * PI * 1.5;
    double ca = std::cos(a), sa = std::sin(a);
    // the needle is a quad in local coordinates, rotated by `a`
    auto rot = [&](double lx, double ly) {
      return std::make_pair(cx + lx * ca - ly * sa, cy + lx * sa + ly * ca);
    };
    std::vector<std::pair<double, double>> q = {rot(-r * 0.16, -3.5), rot(r * 0.9, -1.2),
                                                rot(r * 0.9, 1.2), rot(-r * 0.16, 3.5)};
    c.fillPoly(q, parseColor("#ff5a2a"));
    c.fillCircle(cx, cy, r * 0.1, parseColor("#22272c"));
  };
  needle(150, 128, 116, s.rpm / 7000);
  needle(W - 150, 128, 116, s.speedKmh / 240);

  // centre display
  double cx = W / 2;
  c.fillRect(cx - 100, 28, 200, 200, parseColor("#0b1014"));
  if (lit) {
    long spd = std::lround(std::max(0.0, s.speedKmh));
    c.fillText(std::to_string(spd), cx, 92, 62, parseColor("#e8ecef"));
    c.fillText("km/h", cx, 132, 18, parseColor("#9aa3ab"));
    if (s.showGear || s.automatic) {
      std::string g;
      if (s.automatic) {
        if (s.gear == -2) g = "P";
        else if (s.gear == -1) g = "R";
        else if (s.gear == 0) g = "N";
        else g = "D" + std::to_string(s.gear);
      } else {
        g = s.gear == -1 ? "R" : (s.gear == 0 ? "N" : std::to_string(s.gear));
      }
      c.fillText(g, cx, 166, 30, parseColor("#ffd24a"));
    }
    char odo[32];
    std::snprintf(odo, sizeof odo, "%.1f km", s.odometerKm);
    c.fillText(odo, cx, 196, 15, parseColor("#7d868e"));
    // fuel bar
    c.strokeRect(cx - 60, 210, 120, 9, parseColor("#5b646c"), 1.5);
    double f = std::max(0.0, std::min(1.0, s.fuel));
    c.fillRect(cx - 59, 211, 118 * f, 7, s.fuel < 0.15 ? parseColor("#ffb020") : parseColor("#cfd6dc"));
  }

  // telltales
  bool blink = s.blinkOn && lit;
  auto arrow = [&](double dir) {
    return [&, dir]() {
      std::vector<std::pair<double, double>> q = {{dir * 16, 0}, {0, -13}, {0, -6},
                                                  {-dir * 14, -6}, {-dir * 14, 6},
                                                  {0, 6}, {0, 13}};
      for (auto& p : q) p.first += cx, p.second += 18;
      c.fillPoly(q, parseColor("#35d04a"));
    };
  };
  if (blink && (s.hazard || s.indicator < 0)) arrow(-1)();
  if (blink && (s.hazard || s.indicator > 0)) arrow(1)();
  auto text = [&](const char* t, double size, double x, double y, uint32_t col) {
    if (lit) c.fillText(t, x, y, size, col);
  };
  if (s.lowBeam) text("L", 20, cx - 30, 14, parseColor("#35d04a"));
  if (s.highBeam) text("H", 20, cx + 30, 14, parseColor("#3d8bff"));
  if (s.handbrake) text("P", 17, 300, 232, parseColor("#ff3b30"));
  if (s.abs) text("ABS", 14, W - 300, 232, parseColor("#ffb020"));
  if (!s.engineRunning) text("!", 20, 262, 208, parseColor("#ff3b30"));
  if (!s.seatbelt) text("B", 16, W - 262, 208, parseColor("#ff3b30"));

  pixels_ = c.pixels();
  dirty_ = true;
}

}  // namespace fs
