// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hudmirror.ts @ v1.7.7
#include "ui/hudmirror.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "i18n/index.h"

namespace fs {

using dom::El;

/** the frame's width at size 1 (px of the HUD), and its shape (wider than the glass is not) */
static const double WIDTH = 230;
static const double ASPECT = 3.2;

/** The size setting a frame `w` px of the screen wide stands for. */
static double sizeOf(double w, double zoom) { return w / (WIDTH * zoom); }

HudMirror::HudMirror(const El& ui, MirrorHost host, Settings& settings) : host(host), ui(ui), settings(settings) {
  El hud = ui.querySelector(".hud");
  if (!hud) hud = ui;
  frameEl = dom::el("div");
  frameEl.className("hudmirror");
  frameEl.hidden(true);
  grip = dom::el("span");
  grip.className("hm-grip");
  frameEl.append(grip);
  hud.prepend(frameEl);
  frameEl.on("pointerdown", [this](dom::Event& e) {
    e.preventDefault();
    e.stopPropagation();
    const Settings::InsideMirror& m = this->settings.insideMirror;
    const dom::Rect r = frameEl.getBoundingClientRect();
    drag = Drag{e.pointerId, e.target() == grip, e.clientX, e.clientY, m.fx, m.fy, r.width, false};
    // (the pointer may be gone already: the host's call does not mind)
    frameEl.setPointerCapture(e.pointerId);
  });
  frameEl.on("pointermove", [this](dom::Event& e) {
    if (!drag || drag->id != e.pointerId) return;
    Drag& d = *drag;
    const double dx = e.clientX - d.x;
    const double dy = e.clientY - d.y;
    if (!d.moved && std::hypot(dx, dy) < 4) return;
    d.moved = true;
    Settings::InsideMirror& m = this->settings.insideMirror;
    const dom::Rect c = this->host.canvas.getBoundingClientRect();
    if (d.resize) {
      // (the grip at the right-hand corner: the frame grows to the right as much as it is drawn)
      m.size = std::max(0.6, std::min(2.0, sizeOf(d.w + dx * 2, zoomNow())));
    } else {
      m.fx = std::max(0.0, std::min(1.0, d.fx + dx / std::max(1.0, c.width)));
      m.fy = std::max(0.0, std::min(1.0, d.fy + dy / std::max(1.0, c.height)));
    }
  });
  auto up = [this](dom::Event& e) {
    if (!drag || drag->id != e.pointerId) return;
    const bool moved = drag->moved;
    drag.reset();
    if (moved) saveSettings(this->settings);
  };
  frameEl.on("pointerup", up);
  frameEl.on("pointercancel", up);
}

double HudMirror::zoomNow() const {
  const double z = jsNumber(ui.style("zoom"), false);
  return z != z || z == 0 ? 1 : z;
}

void HudMirror::frame(const HudMirrorView& v) {
  Mirror* inside = nullptr;
  if (v.mirrors)
    for (const auto& mirror : *v.mirrors)
      if (mirror->spec.name == "inside") {
        inside = mirror.get();
        break;
      }
  if (last && last != inside) show(*last, true);
  last = inside;
  const Settings::InsideMirror& m = settings.insideMirror;
  const bool inHud = m.mode == "hud" && settings.mirrors != "off";
  if (inside) {
    if (m.mode == "car") inside->place(m.shift, m.size, v.eyeLocal ? &*v.eyeLocal : nullptr);
    else inside->place(0, 1, nullptr);
    show(*inside, !(inHud && v.cockpit));
  }
  if (v.lang != lang) {
    lang = v.lang;
    frameEl.title(tr("hudMirror.drag", v.lang));
    grip.title(tr("hudMirror.resize", v.lang));
    placed = "";
  }
  if (!inside || !inHud || !v.cockpit) {
    if (!frameEl.hidden()) frameEl.hidden(true);
    return;
  }
  host.renderer.getSize(size);
  const double cw = size.x;
  const double ch = size.y;
  const double z = zoomNow();
  const double w = jsRound(std::min(cw * 0.9, WIDTH * m.size * z));
  const double h = jsRound(w / ASPECT);
  const double x = jsRound(std::max(0.0, std::min(cw - w, m.fx * cw - w / 2)));
  const double y = jsRound(std::max(0.0, std::min(ch - h, m.fy * ch - h / 2)));
  const std::string key = jsStr(x) + "," + jsStr(y) + "," + jsStr(w) + "," + jsStr(h) + "," + jsStr(z);
  if (key != placed) {
    placed = key;
    // (the HUD is drawn at the size of the interface: its pixels are the screen's divided by that)
    frameEl.style("left", jsStr(x / z - 3) + "px");
    frameEl.style("top", jsStr(y / z - 3) + "px");
    frameEl.style("width", jsStr(w / z) + "px");
    frameEl.style("height", jsStr(h / z) + "px");
  }
  const Object3D* parent = inside->mesh->parent;
  const bool drawn =
      parent != nullptr && zoom.draw(host.renderer, host.scene, *inside, parent->matrixWorld, v.eye, v.far, MirrorZoom::Rect{x, y, w, h}, MirrorZoom::Options{.minTan = 0, .refresh = v.refresh});
  if (frameEl.hidden() == drawn) frameEl.hidden(!drawn);
}

void HudMirror::show(Mirror& mirror, bool on) {
  if (mirror.mesh->visible == on) return;
  mirror.mesh->visible = on;
  for (const ObjectPtr& o : mirror.attached) o->visible = on;
}

}  // namespace fs
