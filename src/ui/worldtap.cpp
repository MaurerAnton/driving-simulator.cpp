// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/worldtap.ts @ v1.7.7
#include "ui/worldtap.h"

#include <cmath>

#include "core/js.h"
#include "platform/plat.h"

namespace fs {

namespace {

/** a tap: let go within this time (ms) and this distance (px) of where it came down */
const double TAP_MS = 450;
const double TAP_PX = 10;
/** how long the button stays (ms) */
const double SHOW_MS = 4500;

}  // namespace

WorldTap::WorldTap(WorldTapHost host_) : host(std::move(host_)) {
  bar = dom::el("div");
  bar.className("world-tap");
  bar.style("position", "absolute");
  bar.style("left", "0");
  bar.style("top", "0");
  bar.style("transform", "translate(-50%, calc(-100% - 14px))");
  bar.style("display", "none");
  bar.style("alignItems", "center");
  bar.style("gap", "6px");
  bar.style("zIndex", "7");
  bar.style("pointerEvents", "auto");
  bar.style("touchAction", "manipulation");
  /** one button of the bar: its icon, the text of the offer and what it does when it is tapped */
  const auto button = [this](const std::string& icon, const std::string& key, std::function<void(const GroundPoint& p, double yaw)> run) {
    const dom::El b = dom::el("button");
    b.type("button");
    b.style("minHeight", "44px");
    b.style("padding", "0 14px");
    b.style("borderRadius", "22px");
    b.style("border", "1px solid rgba(255,255,255,0.75)");
    b.style("background", "rgba(16,24,34,0.86)");
    b.style("color", "#fff");
    b.style("font", "inherit");
    b.style("fontSize", "14px");
    b.style("fontWeight", "600");
    b.style("whiteSpace", "nowrap");
    b.style("display", "flex");
    b.style("alignItems", "center");
    b.style("gap", "6px");
    b.style("boxShadow", "0 2px 10px rgba(0,0,0,0.45)");
    b.onpointerdown([](dom::Event& e) { e.stopPropagation(); });
    b.onclick([this, run = std::move(run)](dom::Event& e) {
      e.stopPropagation();
      const std::optional<At> at_ = at;
      hide();
      if (at_) run(GroundPoint{at_->x, at_->y, at_->z}, at_->yaw);
    });
    // (the words of the language in use now – said again at every offer, after a switch of language)
    b.dataset("key", key);
    b.dataset("icon", icon);
    bar.append(b);
    return b;
  };
  teleportBtn = button("⤓", "teleportHere", [this](const GroundPoint& p, double yaw) {
    if (host.allowed()) host.teleport(p, yaw);
  });
  driveBtn = button("🚗", "tapDrive", [this](const GroundPoint& p, double) {
    if (host.canDrive()) host.drive(p);
  });
  label(teleportBtn);
  label(driveBtn);
  const dom::El r = ring = dom::el("div");
  r.style("position", "absolute");
  r.style("left", "0");
  r.style("top", "0");
  r.style("width", "16px");
  r.style("height", "16px");
  r.style("margin", "-8px 0 0 -8px");
  r.style("borderRadius", "50%");
  r.style("border", "2px solid #fff");
  r.style("boxShadow", "0 0 0 2px rgba(0,0,0,0.35)");
  r.style("display", "none");
  r.style("zIndex", "7");
  r.style("pointerEvents", "none");
  host.ui.append(r, bar);
  const dom::El c = host.canvas;
  c.on("pointerdown", [this](dom::Event& e) {
    if (e.pointerType == "mouse" && e.button != 0) return;
    if (downs.size() > 0) multi = true;
    else multi = false;
    downs[e.pointerId] = Down{e.clientX, e.clientY, plat::nowMs(), host.claimed(e.clientX, e.clientY, true)};
    // (another tap elsewhere: the button offered before goes)
    if (at) hide();
  });
  const auto up = [this](dom::Event& e, bool cancel) {
    const auto it = downs.find(e.pointerId);
    if (it == downs.end()) return;
    const Down d = it->second;
    downs.erase(it);
    if (cancel || multi || d.claimed) return;
    if (plat::nowMs() - d.t > TAP_MS || std::hypot(e.clientX - d.x, e.clientY - d.y) > TAP_PX) return;
    tap(e.clientX, e.clientY);
  };
  dom::window().on("pointerup", [up](dom::Event& e) { up(e, false); });
  dom::window().on("pointercancel", [up](dom::Event& e) { up(e, true); });
}

void WorldTap::tap(double cx, double cy) {
  const WorldTapHost& h = host;
  if (h.claimed(cx, cy, false)) return;
  const dom::Rect r = h.canvas.getBoundingClientRect();
  if (r.width == 0 || r.height == 0) return;
  ndc.set(((cx - r.left) / r.width) * 2 - 1, -((cy - r.top) / r.height) * 2 + 1);
  ray.setFromCamera(ndc, *h.camera);
  const Vector3& o = ray.ray.origin;
  const Vector3& d = ray.ray.direction;
  const std::optional<GroundPoint> g = rayGround(o.x, o.y, o.z, d.x, d.y, d.z, [&h](double x, double z) { return h.heightAt(x, z); }, 2500);
  if (!g) return;
  h.tapped(*g);
  if (!h.allowed() && !h.canDrive()) return;
  at = At{g->x, g->y, g->z, std::atan2(-d.x, -d.z)};
  until = plat::nowMs() + SHOW_MS;
  // (the words of the language in use now)
  label(teleportBtn);
  label(driveBtn);
  frame();
}

void WorldTap::label(const dom::El& b) {
  const std::string text = host.t(b.dataset("key"));
  b.textContent(b.dataset("icon") + " " + text);
  b.title(text);
  b.setAttribute("aria-label", text);
}

void WorldTap::hide() {
  at = std::nullopt;
  bar.style("display", "none");
  ring.style("display", "none");
}

void WorldTap::frame() {
  if (!at) return;
  const At at_ = *at;
  const WorldTapHost& h = host;
  const bool canTeleport = h.allowed();
  const bool canDrive = h.canDrive();
  if (plat::nowMs() > until || (!canTeleport && !canDrive)) {
    hide();
    return;
  }
  // (each button only where it may be used; the bar stays as long as either may)
  teleportBtn.style("display", canTeleport ? "flex" : "none");
  driveBtn.style("display", canDrive ? "flex" : "none");
  const Vector3& p = v.set(at_.x, at_.y, at_.z).project(*h.camera);
  // (behind the camera, or off the picture: not shown until it is back)
  const bool off = p.z > 1 || std::abs(p.x) > 1.05 || std::abs(p.y) > 1.05;
  const dom::Rect r = h.canvas.getBoundingClientRect();
  const dom::Rect u = h.ui.getBoundingClientRect();
  const double sx = r.left - u.left + ((p.x + 1) / 2) * r.width;
  const double sy = r.top - u.top + ((1 - p.y) / 2) * r.height;
  // (kept within the picture, clear of its edges – as wide as the bar is)
  const double half = bar.offsetWidth() / 2 + 8;
  const double bx = jsMax(half, jsMin(u.width - half, sx));
  const double by = jsMax(70, sy);
  bar.style("display", off ? "none" : "flex");
  ring.style("display", off ? "none" : "block");
  bar.style("left", jsStr(bx) + "px");
  bar.style("top", jsStr(by) + "px");
  ring.style("left", jsStr(sx) + "px");
  ring.style("top", jsStr(sy) + "px");
}

}  // namespace fs
