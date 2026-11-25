// SPDX-License-Identifier: AGPL-3.0-only
/** ui/sdlhud.h: the HUD of the SDL window, painted with the native 2D canvas from what the Hud shows. */
#include "ui/sdlhud.h"

#if !defined(__EMSCRIPTEN__)

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "input/input.h"
#include "i18n/unicode.h"
#include "i18n/index.h"
#include "platform/native/canvas_native.h"
#include "platform/plat.h"
#include "ui/hud.h"
#include "ui/notices.h"

#include "platform/overlay.h"

namespace fs {

namespace {

// the page's colours and fonts (web/css/style.css: --panel, --line, --text, --muted, --accent; .hud …)
const char* const PANEL = "rgba(18, 26, 36, 0.86)";
const char* const LINE = "rgba(255, 255, 255, 0.14)";
const char* const TEXT = "#eef2f6";
const char* const MUTED = "#a9b4c0";
const char* const ACCENT = "#ffcc00";
const std::string FONT = "system-ui, sans-serif";

/** a text broken at its blanks into lines no wider than `maxW` (a word wider than that stands alone) */
std::vector<std::string> wrap(const Ctx2D& g, const std::string& text, double maxW, size_t maxLines) {
  std::vector<std::string> lines;
  std::string line;
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find(' ', i);
    if (j == std::string::npos) j = text.size();
    const std::string word = text.substr(i, j - i);
    i = j + 1;
    if (word.empty()) continue;
    const std::string tryLine = line.empty() ? word : line + " " + word;
    if (!line.empty() && g.measureText(tryLine).width > maxW) {
      lines.push_back(line);
      line = word;
    } else line = tryLine;
  }
  if (!line.empty()) lines.push_back(line);
  if (lines.size() > maxLines) {
    lines.resize(maxLines);
    lines.back() += " …";
  }
  return lines;
}

/** the arrow of the street-name block in a box of 40 × 34 at x, y: -1 turn left, 1 turn right, 0 straight on */
void arrow(const Ctx2D& g, double x, double y, int turn, const char* colour) {
  g.save();
  g.translate(x, y);
  if (turn > 0) {
    // (the left turn, mirrored)
    g.translate(40, 0);
    g.scale(-1, 1);
  }
  g.strokeStyle(colour);
  g.fillStyle(colour);
  g.lineWidth(4);
  g.lineCap("round");
  g.lineJoin("round");
  g.beginPath();
  if (turn == 0) {
    g.moveTo(20, 31);
    g.lineTo(20, 12);
    g.stroke();
    g.beginPath();
    g.moveTo(10, 14);
    g.lineTo(20, 3);
    g.lineTo(30, 14);
    g.closePath();
  } else {
    g.moveTo(27, 31);
    g.lineTo(27, 15);
    g.lineTo(14, 15);
    g.stroke();
    g.beginPath();
    g.moveTo(15, 6);
    g.lineTo(5, 15);
    g.lineTo(15, 24);
    g.closePath();
  }
  g.fill();
  g.restore();
}

}  // namespace

SdlHud::SdlHud(const Hud& hud) : hud_(hud), canvas_(1, 1), g_(canvas_.getContext2D()) {}

bool SdlHud::touchShown() const {
  const std::optional<HudState>& st = hud_.lastState();
  const Input* in = hud_.inputNow();
  return st && hud_.hudShown() && !st->onFoot && in && in->mode == "touch";
}

std::vector<SdlHud::Control> SdlHud::controls(double W, double H) const {
  std::vector<Control> out;
  if (!touchShown() || W <= 0 || H <= 0) return out;
  // (the page's .touch rules – web/css/style.css – with those of a low window, where a phone held sideways is)
  const bool low = H <= 500;
  const double vw = W / 100, vh = H / 100;
  const double steerW = low ? std::min(34 * vw, 250.0) : 34 * vw, steerH = low ? std::min(20 * vh, 72.0) : 20 * vh;
  const double pedal = low ? std::min(9.75 * vw, 63.0) : 9.75 * vw, gap = 12;
  const double tb = low ? 44 : 52, step = tb + (low ? 8 : 10);
  const double bottom = H - 5 * vh;
  const std::string lang = hud_.lang();
  out.push_back(Control{"steer", "steer", "", 3 * vw, bottom - steerH, steerW, steerH, false});
  const double pedalY = bottom - pedal;
  out.push_back(Control{"pedal", "gas", tr("tbGas", lang), W - 3 * vw - pedal, pedalY, pedal, pedal, false});
  out.push_back(Control{"pedal", "brk", tr("tbBrake", lang), W - 3 * vw - 2 * pedal - gap, pedalY, pedal, pedal, false});
  const std::optional<Transmission>& gearbox = hud_.gearbox();
  const bool manual = gearbox && *gearbox == "manual";
  if (manual) out.push_back(Control{"pedal", "clutch", tr("tbClutch", lang), W - 3 * vw - 3 * pedal - 2 * gap, pedalY, pedal, pedal, false});
  // the indicators above the steering bar, the starter beside them
  const double indY = bottom - steerH - 10 - tb;
  out.push_back(Control{"key", "KeyZ", "<", 3 * vw, indY, tb, tb, true});
  out.push_back(Control{"key", "KeyC", ">", 3 * vw + step, indY, tb, tb, true});
  out.push_back(Control{"key", "Enter", "power", 3 * vw + 2 * step, indY, tb, tb, true});
  // the gears and the handbrake above the pedals: ▲ ▼ as the page has them, and D (first gear) and R
  const double gearY = bottom - (low ? std::min(36 * vh, 138.0) : 36 * vh) - 10 - tb;
  out.push_back(Control{"key", "KeyE", "^", W - 3 * vw - tb, gearY, tb, tb, true});
  out.push_back(Control{"key", "KeyQ", "v", W - 3 * vw - tb - step, gearY, tb, tb, true});
  out.push_back(Control{"key", "Space", "P", W - 3 * vw - tb - 2 * step, gearY, tb, tb, true});
  out.push_back(Control{"key", "Digit1", manual ? "1" : "D", W - 3 * vw - tb, gearY - step, tb, tb, true});
  out.push_back(Control{"key", "KeyR", "R", W - 3 * vw - tb - step, gearY - step, tb, tb, true});
  out.push_back(Control{"key", "KeyV", "camera", W - 3 * vw - tb - 2 * step, gearY - step, tb, tb, true});
  return out;
}

bool SdlHud::pointer(const OverlayPointer& p) {
  Input* in = hud_.inputNow();
  if (!in) return false;
  if (p.type == "down") {
    for (const Control& c : controls(cssW_, cssH_)) {
      if (p.x < c.x || p.x > c.x + c.w || p.y < c.y || p.y > c.y + c.h) continue;
      if (c.kind == "key") in->press(c.code, true);
      held_[p.id] = c.code;
      follow(c.code, p.x, p.y);
      return true;
    }
    // (the tip line: "fasten your seat belt – tap here")
    if (!tipCode_.empty() && touchShown() && p.x >= tipX_ && p.x <= tipX_ + tipW_ && p.y >= tipY_ && p.y <= tipY_ + tipH_) {
      in->press(tipCode_, true);
      held_[p.id] = "tip:" + tipCode_;
      return true;
    }
    return false;
  }
  const auto it = held_.find(p.id);
  if (it == held_.end()) return false;
  const std::string code = it->second;
  if (p.type == "move") {
    follow(code, p.x, p.y);
    return true;
  }
  // up, cancel: let go
  held_.erase(it);
  if (code == "steer") {
    in->touch.steer = 0;
    in->touch.pitch = 0;
    knob_ = 0;
  } else if (code == "gas") in->touch.throttle = 0;
  else if (code == "brk") in->touch.brake = 0;
  else if (code == "clutch") in->touch.clutch = 0;
  else if (code.rfind("tip:", 0) == 0) in->press(code.substr(4), false);
  else in->press(code, false);
  return true;
}

void SdlHud::follow(const std::string& code, double x, double y) {
  Input* in = hud_.inputNow();
  if (!in) return;
  for (const Control& c : controls(cssW_, cssH_)) {
    if (c.code != code) continue;
    if (c.kind == "steer") {
      const double v = std::clamp(((x - c.x) / c.w) * 2 - 1, -1.0, 1.0);
      in->touch.steer = v;
      in->touch.pitch = 0;
      in->touched();
      knob_ = v;
    } else if (c.kind == "pedal") {
      const double v = std::max(0.15, std::min(1.0, 1 - (y - c.y) / c.h + 0.15));
      if (code == "gas") in->touch.throttle = v;
      else if (code == "brk") in->touch.brake = v;
      else in->touch.clutch = v;
      in->touched();
    }
    return;
  }
}

void SdlHud::drawControls(double ratio) {
  const Ctx2D& g = g_;
  const std::vector<Control> cs = controls(cssW_, cssH_);
  if (cs.empty()) return;
  const std::optional<HudState>& st = hud_.lastState();
  const Input* in = hud_.inputNow();
  g.setTransform(ratio, 0, 0, ratio, 0, 0);
  g.globalAlpha(std::clamp(hud_.touchOpacity(), 0.15, 1.0));
  g.textAlign("center");
  g.textBaseline("middle");
  for (const Control& c : cs) {
    bool down = false;
    for (const auto& [id, code] : held_) down = down || code == c.code;
    if (c.kind == "steer") {
      g.fillStyle("rgba(20, 28, 38, 0.55)");
      g.strokeStyle("rgba(255, 255, 255, 0.4)");
      g.lineWidth(1);
      g.beginPath();
      g.roundRect(c.x, c.y, c.w, c.h, 14);
      g.fill();
      g.stroke();
      const double kw = c.w * 0.14;
      g.fillStyle("rgba(255, 204, 0, 0.95)");
      g.beginPath();
      g.roundRect(c.x + (0.43 + knob_ * 0.43) * c.w, c.y + c.h * 0.1, kw, c.h * 0.8, 10);
      g.fill();
    } else if (c.kind == "pedal") {
      const double depth = !in ? 0 : c.code == "gas" ? in->touch.throttle : c.code == "brk" ? in->touch.brake : in->touch.clutch;
      g.fillStyle(c.code == "gas" ? "rgba(40, 170, 90, 0.6)" : c.code == "brk" ? "rgba(210, 60, 60, 0.6)" : "rgba(60, 120, 210, 0.6)");
      g.strokeStyle(down ? "rgba(255, 255, 255, 0.9)" : "rgba(255, 255, 255, 0.4)");
      g.lineWidth(down ? 2 : 1);
      g.beginPath();
      g.roundRect(c.x, c.y, c.w, c.h, 14);
      g.fill();
      g.stroke();
      if (depth > 0) {
        // (how far it is pressed: a lighter band from the bottom)
        g.save();
        g.beginPath();
        g.roundRect(c.x, c.y, c.w, c.h, 14);
        g.clip();
        g.fillStyle("rgba(255, 255, 255, 0.25)");
        g.fillRect(c.x, c.y + c.h * (1 - depth), c.w, c.h * depth);
        g.restore();
      }
      g.fillStyle("#fff");
      g.font("700 " + jsStr(c.w < 60 ? 12 : 13) + "px " + FONT);
      g.textBaseline("alphabetic");
      g.fillText(c.label, c.x + c.w / 2, c.y + c.h - 8, c.w - 6);
      g.textBaseline("middle");
    } else {
      // (lit: held, or what it switches is on – the indicator blinking, the handbrake on)
      bool on = down;
      if (st) {
        if (c.code == "KeyZ") on = on || st->indicator < 0;
        else if (c.code == "KeyC") on = on || st->indicator > 0;
        else if (c.code == "Space") on = on || st->handbrake;
      }
      g.fillStyle(on ? ACCENT : "rgba(20, 28, 38, 0.75)");
      g.strokeStyle(on ? ACCENT : "rgba(255, 255, 255, 0.45)");
      g.lineWidth(1);
      g.beginPath();
      g.arc(c.x + c.w / 2, c.y + c.h / 2, c.w / 2, 0, 2 * M_PI);
      g.fill();
      g.stroke();
      const char* ink = on ? "#111" : "#fff";
      g.fillStyle(ink);
      g.strokeStyle(ink);
      // (the symbols drawn: the fonts that come with the game have no ◀ ▶ ▲ ▼)
      const double cx = c.x + c.w / 2, cy = c.y + c.h / 2, u = c.w / 52;
      if (c.label == "<" || c.label == ">" || c.label == "^" || c.label == "v") {
        const double d = 9 * u;
        g.beginPath();
        if (c.label == "<") g.moveTo(cx - d, cy), g.lineTo(cx + d * 0.7, cy - d), g.lineTo(cx + d * 0.7, cy + d);
        else if (c.label == ">") g.moveTo(cx + d, cy), g.lineTo(cx - d * 0.7, cy - d), g.lineTo(cx - d * 0.7, cy + d);
        else if (c.label == "^") g.moveTo(cx, cy - d), g.lineTo(cx - d, cy + d * 0.7), g.lineTo(cx + d, cy + d * 0.7);
        else g.moveTo(cx, cy + d), g.lineTo(cx - d, cy - d * 0.7), g.lineTo(cx + d, cy - d * 0.7);
        g.closePath();
        g.fill();
      } else if (c.label == "power") {
        g.lineWidth(2.5 * u);
        g.lineCap("round");
        g.beginPath();
        g.arc(cx, cy + 1 * u, 10 * u, -M_PI / 2 + 0.6, 1.5 * M_PI - 0.6);
        g.stroke();
        g.beginPath();
        g.moveTo(cx, cy - 12 * u);
        g.lineTo(cx, cy - 1 * u);
        g.stroke();
      } else if (c.label == "camera") {
        g.lineWidth(2 * u);
        g.beginPath();
        g.roundRect(cx - 12 * u, cy - 7 * u, 24 * u, 16 * u, 3 * u);
        g.stroke();
        g.beginPath();
        g.arc(cx, cy + 1 * u, 5 * u, 0, 2 * M_PI);
        g.stroke();
        g.fillRect(cx - 5 * u, cy - 10 * u, 10 * u, 3 * u);
      } else {
        g.font("700 " + jsStr(c.w < 50 ? 17 : 20) + "px " + FONT);
        g.fillText(c.label, cx, cy + 1);
      }
    }
  }
  g.globalAlpha(1);
  g.lineCap("butt");
  g.textAlign("left");
  g.textBaseline("alphabetic");
  g.setTransform(1, 0, 0, 1, 0, 0);
}

SdlHud::Picture SdlHud::paint(int cssWidth, int cssHeight, double ratio) {
  const std::optional<HudState>& st = hud_.lastState();
  if (!(ratio > 0)) ratio = 1;
  cssW_ = cssWidth;
  cssH_ = cssHeight;
  const int width = static_cast<int>(std::lround(cssWidth * ratio)), height = static_cast<int>(std::lround(cssHeight * ratio));
  if (!st || !hud_.hudShown() || width <= 0 || height <= 0) {
    // (the menus are the page's: over them the window shows nothing of the HUD)
    if (!empty_) {
      empty_ = true;
      version_++;
    }
    return Picture{nullptr, 0, 0, version_};
  }
  const HudState& s = *st;
  const std::shared_ptr<const Notice>& note = hud_.noticeNow();
  const Ctx2D map = hud_.cornerMap().getContext2D();
  const uint64_t mapNow = canvasNative::version(map);
  // what is shown: painted again only when it changes
  std::string key = std::to_string(width) + "x" + std::to_string(height) + "|" + jsStr(jsRound(std::abs(s.speedKmh))) + "|" + s.gear + "|" + jsStr(jsRound(s.rpm / 50)) + "|" +
                    jsStr(s.limit) + "|" + s.limitWhen + "|" + std::to_string(s.zone) + "|" + s.navText + "|" + s.navSub + "|" + std::to_string(s.navArrow) + "|" +
                    (s.navActive ? "1" : "0") + "|" + s.tip + "|" + s.tipAction + "|" + std::to_string(s.faults) + "|" + std::to_string(s.points) + "|" +
                    std::to_string((s.blinkOn && (s.hazard || s.indicator < 0)) ? 1 : 0) + std::to_string((s.blinkOn && (s.hazard || s.indicator > 0)) ? 1 : 0) +
                    (s.onFoot ? "f" : "") + "|" + hud_.lang();
  if (note) key += "|" + note->title + "|" + note->why + "|" + note->meta + "|" + noticeColour(*note);
  touch_ = touchShown();
  if (touch_) {
    // (the touch controls: what is held, how far, what is lit)
    const Input* in = hud_.inputNow();
    key += "|t" + jsStr(jsRound(knob_ * 50)) + "," + jsStr(jsRound(in->touch.throttle * 20)) + "," + jsStr(jsRound(in->touch.brake * 20)) + "," +
           jsStr(jsRound(in->touch.clutch * 20)) + "," + (s.handbrake ? "p" : "") + std::to_string(s.indicator) + "," + jsStr(ratio) + "," +
           (hud_.gearbox() ? *hud_.gearbox() : "") + "," + jsStr(hud_.touchOpacity());
    for (const auto& [id, code] : held_) key += "," + code;
  }
  if (!empty_ && key == shown_ && mapNow == mapVersion_) return Picture{pixels_.data(), width, height, version_};
  shown_ = key;
  mapVersion_ = mapNow;
  empty_ = false;
  if (canvas_.width() != width) canvas_.width(width);
  if (canvas_.height() != height) canvas_.height(height);
  // (the page's sizes are CSS pixels of a window about 1280 × 720: scaled with the window)
  const double scale = ratio * std::clamp(std::min(cssWidth / 1280.0, cssHeight / 720.0), 0.6, 3.0);
  draw(scale, width, height);
  if (tipW_ > 0) {
    // (the tip's box in CSS pixels, for the pointer)
    tipX_ *= scale / ratio;
    tipY_ *= scale / ratio;
    tipW_ *= scale / ratio;
    tipH_ *= scale / ratio;
  }
  if (touch_) drawControls(ratio);
  int w = 0, h = 0;
  canvasNative::pixels(canvas_.el().handle(), pixels_, w, h, true, false);
  version_++;
  return Picture{pixels_.data(), w, h, version_};
}

void SdlHud::draw(double scale, int width, int height) {
  const Ctx2D& g = g_;
  const HudState& s = *hud_.lastState();
  const std::shared_ptr<const Notice>& note = hud_.noticeNow();
  const std::string lang = hud_.lang();
  const double W = width / scale, H = height / scale;
  g.setTransform(1, 0, 0, 1, 0, 0);
  g.clearRect(0, 0, width, height);
  g.setTransform(scale, 0, 0, scale, 0, 0);
  g.textAlign("left");
  g.textBaseline("alphabetic");

  // --- the street-name block, top left (a notice in its place) -------------------------------
  {
    const bool red = note && noticeColour(*note) == "red";
    const char* bg = note ? (red ? "rgba(196, 30, 36, 0.95)" : "rgba(255, 200, 30, 0.96)") : PANEL;
    const char* fg = note ? (red ? "#fff" : "#1c1500") : TEXT;
    const char* sub = note ? fg : MUTED;
    const char* arrowColour = note ? fg : ACCENT;
    const std::string title = note ? note->title : !s.navText.empty() ? s.navText : "Fahrsim Baunatal";
    const std::string under = note ? note->why : s.navSub;
    const std::string meta = note ? note->meta : "";
    const std::string next = note && s.navActive ? s.navText : "";
    const double maxW = std::min(W * 0.46, 520.0);
    const double textW = maxW - 28 - 40 - 12;
    const bool longText = !note && i18n::utf16Length(s.navText) > 64;
    const std::string titleFont = longText ? "600 14px " + FONT : "700 17px " + FONT;
    g.font(titleFont);
    const std::vector<std::string> tl = wrap(g, title, textW, 3);
    g.font("13px " + FONT);
    const std::vector<std::string> ul = under.empty() ? std::vector<std::string>() : wrap(g, under, textW, 3);
    const double tlh = longText ? 18.5 : 22, ulh = 17.5;
    double textH = static_cast<double>(tl.size()) * tlh + static_cast<double>(ul.size()) * ulh + (ul.empty() ? 0 : 2);
    if (!meta.empty()) textH += 18;
    if (!next.empty()) textH += 24;
    double widest = 0;
    g.font(titleFont);
    for (const std::string& l : tl) widest = std::max(widest, g.measureText(l).width);
    g.font("13px " + FONT);
    for (const std::string& l : ul) widest = std::max(widest, g.measureText(l).width);
    const double boxW = std::min(maxW, 28 + 40 + 12 + std::max(widest, 120.0));
    const double boxH = std::max(54.0, textH + 20);
    g.fillStyle(bg);
    g.beginPath();
    g.roundRect(14, 14, boxW, boxH, 12);
    g.fill();
    if (!note) {
      g.strokeStyle(LINE);
      g.lineWidth(1);
      g.stroke();
    }
    // the arrow (a notice shows "!" unless a turn is announced)
    const double ay = 14 + boxH / 2 - 17;
    if (note && !s.navActive) {
      g.fillStyle(arrowColour);
      g.font("800 34px " + FONT);
      g.textAlign("center");
      g.textBaseline("middle");
      g.fillText("!", 14 + 14 + 20, 14 + boxH / 2);
      g.textAlign("left");
      g.textBaseline("alphabetic");
    } else arrow(g, 28, ay, s.navArrow < 0 ? -1 : s.navArrow > 0 ? 1 : 0, arrowColour);
    double y = 14 + 10;
    const double x = 14 + 14 + 40 + 12;
    g.fillStyle(fg);
    g.font(titleFont);
    for (const std::string& l : tl) {
      y += tlh;
      g.fillText(l, x, y - 5);
    }
    g.fillStyle(sub);
    g.font("13px " + FONT);
    if (!ul.empty()) y += 2;
    for (const std::string& l : ul) {
      y += ulh;
      g.fillText(l, x, y - 4);
    }
    if (!meta.empty()) {
      g.font("12px " + FONT);
      g.globalAlpha(0.85);
      y += 18;
      g.fillText(meta, x, y - 4);
      g.globalAlpha(1);
    }
    if (!next.empty()) {
      // (an announced turn stays readable under the notice)
      g.strokeStyle(red ? "rgba(255, 255, 255, 0.35)" : "rgba(0, 0, 0, 0.3)");
      g.lineWidth(1);
      g.beginPath();
      g.moveTo(x, y + 5);
      g.lineTo(14 + boxW - 14, y + 5);
      g.stroke();
      g.font("600 13px " + FONT);
      g.fillStyle(fg);
      y += 24;
      g.fillText((s.navArrow < 0 ? "← " : s.navArrow > 0 ? "→ " : "↑ ") + next, x, y - 4);
    }
  }

  // --- the speed limit, top right ----------------------------------------------------------------
  if (s.limit >= 0 && !s.onFoot) {
    const bool none = s.limit == 0;
    const double r = 39, cx = W - 14 - r, cy = 14 + r;
    if (!none && s.speedKmh > s.limit + 6) {
      g.fillStyle("rgba(255, 60, 60, 0.55)");
      g.beginPath();
      g.arc(cx, cy, r + 6, 0, 2 * M_PI);
      g.fill();
    }
    g.fillStyle(none ? "#555" : "#d0141e");
    g.beginPath();
    g.arc(cx, cy, r, 0, 2 * M_PI);
    g.fill();
    g.fillStyle("#fff");
    g.beginPath();
    g.arc(cx, cy, r - 8, 0, 2 * M_PI);
    g.fill();
    const std::string word = none ? tr("limitNone", lang) : s.zone == 7 ? tr("walk", lang) : "";
    const std::string shown = !word.empty() ? word : jsStr(s.limit);
    g.fillStyle(none ? "#555" : "#111");
    g.textAlign("center");
    g.textBaseline("middle");
    double size = 22;
    g.font("800 22px " + FONT);
    const double tw = g.measureText(shown).width;
    if (tw > 2 * (r - 12)) {
      size = std::max(8.0, 22 * 2 * (r - 12) / tw);
      g.font("800 " + jsStr(size) + "px " + FONT);
    }
    g.fillText(shown, cx, cy);
    if (s.zone == 30 || s.zone == 20 || !s.limitWhen.empty()) {
      const std::string z = !s.limitWhen.empty() ? s.limitWhen : "ZONE";
      g.font("800 12px " + FONT);
      const double zw = g.measureText(z).width + 12;
      g.fillStyle("#fff");
      g.strokeStyle("#111");
      g.lineWidth(3);
      g.beginPath();
      g.roundRect(cx - zw / 2, cy + r + 6, zw, 20, 6);
      g.fill();
      g.stroke();
      g.fillStyle("#111");
      g.fillText(z, cx, cy + r + 16);
    }
    g.textAlign("left");
    g.textBaseline("alphabetic");
  }

  // --- the strip at the bottom: speed, gear, engine speed, indicators ------------------------------
  const double dashH = 52;
  if (!s.onFoot) {
    // (with the touch controls: right of the steering bar, without the engine speed – the page's .touchmode .dash)
    const double dw = touch_ ? 206 : 360, dx = touch_ ? 0.39 * W : (W - dw) / 2, dy = touch_ ? H * 0.95 - dashH : H - 14 - dashH;
    g.fillStyle(PANEL);
    g.beginPath();
    g.roundRect(dx, dy, dw, dashH, 12);
    g.fill();
    g.strokeStyle(LINE);
    g.lineWidth(1);
    g.stroke();
    const double base = dy + 38;
    // indicators
    const bool left = s.blinkOn && (s.hazard || s.indicator < 0), right = s.blinkOn && (s.hazard || s.indicator > 0);
    const auto tri = [&g](double x, double y, int dir, bool on) {
      g.fillStyle(on ? "#35d04a" : "rgba(34, 170, 51, 0.18)");
      g.beginPath();
      g.moveTo(x + dir * 8, y);
      g.lineTo(x - dir * 6, y - 8);
      g.lineTo(x - dir * 6, y + 8);
      g.closePath();
      g.fill();
    };
    tri(dx + 22, dy + dashH / 2, -1, left);
    g.font("800 34px " + FONT);
    g.fillStyle(TEXT);
    g.textAlign("right");
    g.fillText(jsStr(jsRound(std::abs(s.speedKmh))), dx + 112, base);
    g.textAlign("left");
    g.font("12px " + FONT);
    g.fillStyle(MUTED);
    g.fillText("km/h", dx + 118, base);
    g.font("800 24px " + FONT);
    g.fillStyle(ACCENT);
    g.textAlign("center");
    g.fillText(s.gear, dx + 180, base);
    g.textAlign("left");
    // engine speed
    const double rx = dx + 206, ry = dy + dashH / 2 - 4, rw = touch_ ? 0 : 120;
    g.fillStyle("rgba(255, 255, 255, 0.12)");
    g.beginPath();
    g.roundRect(rx, ry, rw, 8, 4);
    g.fill();
    const double share = std::clamp(s.rpm / (s.rpmMax && *s.rpmMax != 0 ? *s.rpmMax : 7000), 0.0, 1.0);
    if (share > 0 && rw > 0) {
      const CanvasGradient grad = g.createLinearGradient(rx, 0, rx + rw, 0);
      grad.addColorStop(0, "#5bd17a");
      grad.addColorStop(0.7, "#ffd24a");
      grad.addColorStop(1, "#ff5a3a");
      g.save();
      g.beginPath();
      g.roundRect(rx, ry, rw, 8, 4);
      g.clip();
      g.fillStyle(grad);
      g.fillRect(rx, ry, rw * share, 8);
      g.restore();
    }
    tri(dx + dw - 22, dy + dashH / 2, 1, right);
  }

  // --- the tip, above the strip -----------------------------------------------------------------------
  tipW_ = 0;
  tipCode_.clear();
  if (!s.tip.empty()) {
    const bool urgent = s.tipAction == "KeyT";
    g.font(std::string(urgent ? "600 " : "") + "14px " + FONT);
    const double tw = std::min(g.measureText(s.tip).width, W * 0.9 - 24);
    const double bx = (W - tw - 24) / 2, by = touch_ ? H * 0.95 - dashH - 8 - 30 : H - 82 - 30;
    if (touch_ && !s.tipAction.empty()) {
      // (a button while the fingers drive: the box in the picture's units, made CSS pixels by paint)
      tipX_ = bx;
      tipY_ = by;
      tipW_ = tw + 24;
      tipH_ = 30;
      tipCode_ = s.tipAction;
    }
    g.fillStyle(urgent ? "rgba(255, 204, 0, 0.92)" : "rgba(0, 0, 0, 0.6)");
    g.beginPath();
    g.roundRect(bx, by, tw + 24, 30, 8);
    g.fill();
    g.fillStyle(urgent ? "#111" : TEXT);
    g.textBaseline("middle");
    g.fillText(s.tip, bx + 12, by + 15, tw);
    g.textBaseline("alphabetic");
  }

  // --- the corner map, bottom right, and the score beside it -------------------------------------
  const Canvas& map = hud_.cornerMap();
  // (with the touch controls: small, at the top, left of the speed limit – the page's .touchmode .map)
  const double ms = touch_ ? 96 : W < 720 ? 120 : 190;
  if (map && map.width() > 0 && canvasNative::version(map.getContext2D()) > 0) {
    const double mx = touch_ ? W - 14 - 78 - 16 - ms : W - 14 - ms, my = touch_ ? 14 : H - 14 - ms;
    g.save();
    g.beginPath();
    g.arc(mx + ms / 2, my + ms / 2, ms / 2, 0, 2 * M_PI);
    g.fillStyle("#1c2630");
    g.fill();
    g.clip();
    g.drawImage(map, mx, my, ms, ms);
    g.restore();
    g.strokeStyle("rgba(255, 255, 255, 0.35)");
    g.lineWidth(2);
    g.beginPath();
    g.arc(mx + ms / 2, my + ms / 2, ms / 2, 0, 2 * M_PI);
    g.stroke();
  }
  if (!touch_) {
    const std::string a = tr("faults", lang) + ": ", b = std::to_string(s.faults), c = " · " + tr("points", lang) + ": ", d = std::to_string(s.points);
    g.font("13px " + FONT);
    const double wa = g.measureText(a).width, wc = g.measureText(c).width;
    g.font("700 13px " + FONT);
    const double wb = g.measureText(b).width, wd = g.measureText(d).width;
    const double bw = wa + wb + wc + wd + 24, bx = W - (W < 720 ? 148 : 218) - bw, by = H - 14 - 30;
    g.fillStyle(PANEL);
    g.beginPath();
    g.roundRect(bx, by, bw, 30, 10);
    g.fill();
    g.strokeStyle(LINE);
    g.lineWidth(1);
    g.stroke();
    double x = bx + 12;
    const double base = by + 20;
    g.font("13px " + FONT);
    g.fillStyle(MUTED);
    g.fillText(a, x, base);
    x += wa;
    g.font("700 13px " + FONT);
    g.fillStyle(TEXT);
    g.fillText(b, x, base);
    x += wb;
    g.font("13px " + FONT);
    g.fillStyle(MUTED);
    g.fillText(c, x, base);
    x += wc;
    g.font("700 13px " + FONT);
    g.fillStyle(TEXT);
    g.fillText(d, x, base);
  }
  g.setTransform(1, 0, 0, 1, 0, 0);
}

void installSdlHud(const Hud& hud) {
  // (one HUD a window; a new Hud – a new game – takes the place of the old one)
  static SdlHud* current = nullptr;
  delete current;
  current = new SdlHud(hud);
  sdlOverlayHook = [] {
    // (painted at up to 1.5 picture pixels a CSS pixel: sharp enough, and cheap enough to paint while driving)
    const SdlHud::Picture p = current->paint(plat::innerWidth(), plat::innerHeight(), std::clamp(plat::devicePixelRatio(), 1.0, 1.5));
    OverlayImage o;
    o.rgba = p.rgba;
    o.width = p.width;
    o.height = p.height;
    o.version = p.version;
    return o;
  };
  sdlOverlayPointerHook = [](const OverlayPointer& p) { return current->pointer(p); };
}

}  // namespace fs

#endif  // !__EMSCRIPTEN__
