// SPDX-License-Identifier: AGPL-3.0-only
/** ui/sdlhud.h: the HUD of the SDL window, painted with the native 2D canvas from what the Hud shows. */
#include "ui/sdlhud.h"

#if !defined(__EMSCRIPTEN__)

#include <algorithm>
#include <cmath>

#include "core/js.h"
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

SdlHud::Picture SdlHud::paint(int width, int height) {
  const std::optional<HudState>& st = hud_.lastState();
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
  if (!empty_ && key == shown_ && mapNow == mapVersion_) return Picture{pixels_.data(), width, height, version_};
  shown_ = key;
  mapVersion_ = mapNow;
  empty_ = false;
  if (canvas_.width() != width) canvas_.width(width);
  if (canvas_.height() != height) canvas_.height(height);
  // (the page's sizes are CSS pixels of a window about 1280 × 720: scaled with the window)
  const double scale = std::clamp(std::min(width / 1280.0, height / 720.0), 0.6, 3.0);
  draw(scale, width, height);
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
    const double dw = 360, dx = (W - dw) / 2, dy = H - 14 - dashH;
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
    const double rx = dx + 206, ry = dy + dashH / 2 - 4, rw = 120;
    g.fillStyle("rgba(255, 255, 255, 0.12)");
    g.beginPath();
    g.roundRect(rx, ry, rw, 8, 4);
    g.fill();
    const double share = std::clamp(s.rpm / (s.rpmMax && *s.rpmMax != 0 ? *s.rpmMax : 7000), 0.0, 1.0);
    if (share > 0) {
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
  if (!s.tip.empty()) {
    const bool urgent = s.tipAction == "KeyT";
    g.font(std::string(urgent ? "600 " : "") + "14px " + FONT);
    const double tw = std::min(g.measureText(s.tip).width, W * 0.9 - 24);
    const double bx = (W - tw - 24) / 2, by = H - 82 - 30;
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
  const double ms = W < 720 ? 120 : 190;
  if (map && map.width() > 0 && canvasNative::version(map.getContext2D()) > 0) {
    const double mx = W - 14 - ms, my = H - 14 - ms;
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
  {
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
    const SdlHud::Picture p = current->paint(plat::innerWidth(), plat::innerHeight());
    OverlayImage o;
    o.rgba = p.rgba;
    o.width = p.width;
    o.height = p.height;
    o.version = p.version;
    return o;
  };
}

}  // namespace fs

#endif  // !__EMSCRIPTEN__
