// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/render/instruments.ts @ v1.7.7
#include "render/instruments.h"

#include <cmath>
#include <functional>
#include <string>

#include "core/js.h"

namespace fs {

namespace {

const int W = 768;
const int H = 256;
const std::string FONT = "\"DIN Alternate\", \"Bahnschrift\", \"Roboto Condensed\", \"Arial Narrow\", \"Helvetica Neue\", Arial, sans-serif";

}  // namespace

InstrumentPanel::InstrumentPanel() : canvas(W, H), face_(W, H) {
  ctx_ = canvas.getContext2D();
  drawFace();
  // never a blank cluster: until the first update it shows the dials, unlit
  ctx_.drawImage(face_, 0, 0);
  texture = canvasTexture(canvas);
  texture->srgb = true;
  texture->anisotropy = 4;
}

void InstrumentPanel::drawFace() {
  const Ctx2D c = face_.getContext2D();
  c.fillStyle("#07090b");
  c.fillRect(0, 0, W, H);
  const auto dial = [&c](double cx, double cy, double r, double max, double major, double minor, const std::function<std::string(double)>& label, const std::string& unit,
                         double redFrom = JS_INF) {
    c.beginPath();
    c.arc(cx, cy, r, 0, PI * 2);
    c.fillStyle("#101418");
    c.fill();
    c.lineWidth(3);
    c.strokeStyle("#3a4047");
    c.stroke();
    const double a0 = PI * 0.75;
    const double span = PI * 1.5;
    for (double v = 0; v <= max + 1e-6; v += minor) {
      const double a = a0 + (v / max) * span;
      const bool isMajor = std::fabs(v / major - jsRound(v / major)) < 1e-6;
      const double r0 = r * (isMajor ? 0.8 : 0.87);
      c.beginPath();
      c.moveTo(cx + std::cos(a) * r0, cy + std::sin(a) * r0);
      c.lineTo(cx + std::cos(a) * r * 0.94, cy + std::sin(a) * r * 0.94);
      c.lineWidth(isMajor ? 3.2 : 1.6);
      c.strokeStyle(v >= redFrom ? "#e03a2a" : "#e8ecef");
      c.stroke();
      if (isMajor) {
        c.fillStyle(v >= redFrom ? "#e03a2a" : "#e8ecef");
        c.font("600 " + jsStr(jsRound(r * 0.17)) + "px " + FONT);
        c.textAlign("center");
        c.textBaseline("middle");
        c.fillText(label(v), cx + std::cos(a) * r * 0.65, cy + std::sin(a) * r * 0.65);
      }
    }
    c.fillStyle("#9aa3ab");
    c.font("500 " + jsStr(jsRound(r * 0.13)) + "px " + FONT);
    c.textAlign("center");
    c.fillText(unit, cx, cy + r * 0.38);
  };
  dial(150, 128, 116, 7000, 1000, 250, [](double v) { return jsStr(v / 1000); }, "×1000/min", 6500);
  dial(W - 150, 128, 116, 240, 20, 10, [](double v) { return jsStr(v); }, "km/h");
}

void InstrumentPanel::update(const DashState& s, double now) {
  // (a clock that starts below zero or jumps back must not stop the instruments:
  // only a short step forward counts as "drawn a moment ago")
  const double since = now - last_;
  if (since >= 0 && since < 0.045) return;
  last_ = now;
  const Ctx2D& c = ctx_;
  c.drawImage(face_, 0, 0);
  const bool lit = s.ignition;
  const auto needle = [&c](double cx, double cy, double r, double frac) {
    const double a = PI * 0.75 + jsMax(0, jsMin(1.02, frac)) * PI * 1.5;
    c.save();
    c.translate(cx, cy);
    c.rotate(a);
    c.beginPath();
    c.moveTo(-r * 0.16, -3.5);
    c.lineTo(r * 0.9, -1.2);
    c.lineTo(r * 0.9, 1.2);
    c.lineTo(-r * 0.16, 3.5);
    c.closePath();
    c.fillStyle("#ff5a2a");
    c.fill();
    c.restore();
    c.beginPath();
    c.arc(cx, cy, r * 0.1, 0, PI * 2);
    c.fillStyle("#22272c");
    c.fill();
  };
  needle(150, 128, 116, s.rpm / 7000);
  needle(W - 150, 128, 116, s.speedKmh / 240);

  // centre display
  const double cx = W / 2;
  c.fillStyle("#0b1014");
  c.fillRect(cx - 100, 28, 200, 200);
  c.textAlign("center");
  c.textBaseline("middle");
  if (lit) {
    c.fillStyle("#e8ecef");
    c.font("600 62px " + FONT);
    c.fillText(jsStr(jsRound(jsMax(0, s.speedKmh))), cx, 92);
    c.font("500 18px " + FONT);
    c.fillStyle("#9aa3ab");
    c.fillText("km/h", cx, 132);
    if (s.showGear || s.automatic) {
      const std::string g = s.automatic ? (s.gear == -2 ? "P" : s.gear == -1 ? "R" : s.gear == 0 ? "N" : "D" + std::to_string(s.gear))
                                        : s.gear == -1 ? "R" : s.gear == 0 ? "N" : std::to_string(s.gear);
      c.font("600 30px " + FONT);
      c.fillStyle("#ffd24a");
      c.fillText(g, cx, 166);
    }
    c.font("500 15px " + FONT);
    c.fillStyle("#7d868e");
    c.fillText(toFixed(s.odometerKm, 1) + " km", cx, 196);
    // fuel bar
    c.strokeStyle("#5b646c");
    c.lineWidth(1.5);
    c.strokeRect(cx - 60, 210, 120, 9);
    c.fillStyle(s.fuel < 0.15 ? "#ffb020" : "#cfd6dc");
    c.fillRect(cx - 59, 211, 118 * jsMax(0, jsMin(1, s.fuel)), 7);
  }

  // telltales
  const auto lamp = [&c](double x, double y, bool on, const std::string& color, const std::function<void()>& draw) {
    c.save();
    c.translate(x, y);
    c.globalAlpha(on ? 1 : 0.07);
    c.fillStyle(color);
    c.strokeStyle(color);
    c.lineWidth(3);
    draw();
    c.restore();
  };
  const auto arrow = [&c](double dir) {
    return [&c, dir]() {
      c.beginPath();
      c.moveTo(dir * 16, 0);
      c.lineTo(0, -13);
      c.lineTo(0, -6);
      c.lineTo(-dir * 14, -6);
      c.lineTo(-dir * 14, 6);
      c.lineTo(0, 6);
      c.lineTo(0, 13);
      c.closePath();
      c.fill();
    };
  };
  const bool blink = s.blinkOn && lit;
  lamp(cx - 78, 18, blink && (s.hazard || s.indicator < 0), "#35d04a", arrow(-1));
  lamp(cx + 78, 18, blink && (s.hazard || s.indicator > 0), "#35d04a", arrow(1));
  const auto text = [&c](const std::string& t, int size = 15) {
    return [&c, t, size]() {
      c.font("700 " + std::to_string(size) + "px " + FONT);
      c.textAlign("center");
      c.textBaseline("middle");
      c.fillText(t, 0, 0);
    };
  };
  lamp(cx - 30, 14, lit && s.lowBeam, "#35d04a", text("◐", 20));
  lamp(cx + 30, 14, lit && s.highBeam, "#3d8bff", text("◑", 20));
  lamp(300, 232, lit && s.handbrake, "#ff3b30", text("(P)", 17));
  lamp(W - 300, 232, lit && s.abs, "#ffb020", text("ABS", 14));
  lamp(262, 208, lit && !s.engineRunning, "#ff3b30", text("⚠", 20));
  lamp(W - 262, 208, lit && !s.seatbelt, "#ff3b30", text("🔔", 16));
  // the fuel lamp, right of the fuel bar: the reserve is reached
  lamp(cx + 78, 214, lit && s.fuel < 0.12, "#ffb020", [&c]() {
    c.fillRect(-8, -8, 9, 16);
    c.fillRect(-10, 8, 13, 2.5);
    c.beginPath();
    c.moveTo(1, -5);
    c.lineTo(6, -1);
    c.lineTo(6, 5);
    c.lineWidth(2);
    c.stroke();
  });
  texture->needsUpdate();
}

}  // namespace fs
