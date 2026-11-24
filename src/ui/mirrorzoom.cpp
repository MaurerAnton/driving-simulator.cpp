// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/mirrorzoom.ts @ v1.7.7
#include "ui/mirrorzoom.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "core/js.h"
#include "i18n/index.h"
#include "input/input.h"
#include "platform/plat.h"

namespace fs {

using dom::El;

/** TEXT: the name of each mirror */
static std::string textOf(const std::string& name) {
  return name == "left" ? "mirrorLeft" : name == "right" ? "mirrorRight" : name == "inside" ? "mirrorInside" : "mirrorLook";
}
static const char* const ICON =
    "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><rect x=\"2.5\" y=\"6\" width=\"19\" height=\"11\" rx=\"4\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"/><path d=\"M7 14.5 12.5 8.5M11 14.5l4-4.4\" stroke=\"currentColor\" stroke-width=\"1.6\" stroke-linecap=\"round\"/><path d=\"M12 17v3.5\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\"/></svg>";
/** a tap: a pointer let go within this time (ms) and this distance (px) of where it came down */
static const double TAP_MS = 450;
static const double TAP_PX = 12;

MirrorZoomPanel::MirrorZoomPanel(const El& ui, Input& input, MirrorHost host) : host(host), ui(ui) {
  // in the HUD (hidden with it), below its other parts: the street-name block stays readable over the panel
  hud = ui.querySelector(".hud");
  if (!hud) hud = ui;
  frameEl = dom::el("div");
  frameEl.className("mirrorzoom");
  frameEl.hidden(true);
  label = dom::el("span");
  label.className("mz-label");
  closeBtn = dom::el("button");
  closeBtn.type("button");
  closeBtn.className("mz-close");
  closeBtn.textContent("✕");
  frameEl.append(label, closeBtn);
  button = dom::el("button");
  button.type("button");
  button.className("mirrorbtn");
  button.innerHTML(ICON);
  button.hidden(true);
  // (`prepend(frame, button)`: the frame first, the button after it)
  hud.prepend(button);
  hud.prepend(frameEl);
  // (the buttons answer at once, on the finger's touch – and take it, so that it turns no view)
  button.on("pointerdown", [this](dom::Event& e) {
    e.preventDefault();
    e.stopPropagation();
    next();
  });
  closeBtn.on("pointerdown", [this](dom::Event& e) {
    e.preventDefault();
    e.stopPropagation();
    look.close();
  });
  frameEl.on("pointerdown", [this](dom::Event& e) {
    e.preventDefault();
    e.stopPropagation();
    next();
  });
  input.onAction([this](const Action& a) {
    if (a == "mirror") next();
  });
  // a tap on a mirror in the driver's seat
  const El canvas = host.canvas;
  canvas.on("pointerdown", [this](dom::Event& e) {
    if (e.pointerType == "mouse" && e.button != 0) return;
    down = Down{e.pointerId, e.clientX, e.clientY, plat::nowMs()};
  });
  canvas.on("pointerup", [this](dom::Event& e) {
    Down& d = down;
    if (e.pointerId != d.id) return;
    d.id = -1;
    if (plat::nowMs() - d.t > TAP_MS || std::hypot(e.clientX - d.x, e.clientY - d.y) > TAP_PX) return;
    tap(e.clientX, e.clientY);
  });
}

std::vector<std::string> MirrorZoomPanel::names() const {
  std::vector<std::string> out;
  for (const Mirror* m : mirrors) out.push_back(m->spec.name);
  return out;
}

void MirrorZoomPanel::next() { look.next(names()); }

void MirrorZoomPanel::tap(double x, double y) {
  if (!cockpit || mirrors.empty()) return;
  Camera& cam = host.camera;
  const dom::Rect r = host.canvas.getBoundingClientRect();
  std::vector<MirrorOnScreen> boxes;
  for (const Mirror* m : mirrors) {
    const AttrPtr pos = m->mesh->geometry->getAttribute("position");
    double x0 = JS_INF;
    double y0 = JS_INF;
    double x1 = -JS_INF;
    double y1 = -JS_INF;
    bool behind = false;
    for (size_t i = 0; i < pos->count(); i++) {
      const Vector3& p = v.fromBufferAttribute(*pos, i).applyMatrix4(m->mesh->matrixWorld).project(cam);
      if (p.z > 1 || p.z < -1) behind = true;
      const double sx = r.left + ((p.x + 1) / 2) * r.width;
      const double sy = r.top + ((1 - p.y) / 2) * r.height;
      x0 = std::min(x0, sx);
      x1 = std::max(x1, sx);
      y0 = std::min(y0, sy);
      y1 = std::max(y1, sy);
    }
    if (behind) continue;
    Vector3 at;
    const double distance = v.setFromMatrixPosition(m->mesh->matrixWorld).distanceTo(cam.getWorldPosition(at));
    boxes.push_back(MirrorOnScreen{m->spec.name, x0, y0, x1, y1, distance});
  }
  const std::optional<std::string> name = mirrorAt(x, y, boxes, 26);
  if (name) look.pick(*name, names());
}

void MirrorZoomPanel::frame(const MirrorView& view) {
  const MirrorList* list = view.mirrors && !view.mirrors->empty() ? view.mirrors : nullptr;
  mirrors.clear();
  if (list)
    for (const auto& m : *list) mirrors.push_back(m.get());
  cockpit = list && view.cockpit;
  if (view.lang != lang) {
    lang = view.lang;
    const std::string lookTitle = tr("mirrorLook", view.lang);
    button.title(lookTitle);
    button.setAttribute("aria-label", lookTitle);
    const std::string closeTitle = tr("mirrorClose", view.lang);
    closeBtn.title(closeTitle);
    closeBtn.setAttribute("aria-label", closeTitle);
    frameEl.title(tr("mirrorNext", view.lang));
    placed = "";
  }
  if (list) look.keep(names());
  else look.close();
  if (button.hidden() != !list) button.hidden(!list);
  const std::optional<std::string> shown = look.shown;
  button.toggleClass("on", shown.has_value());
  Mirror* mirror = nullptr;
  if (shown)
    for (Mirror* m : mirrors)
      if (m->spec.name == *shown) {
        mirror = m;
        break;
      }
  if (!mirror) {
    if (!frameEl.hidden()) frameEl.hidden(true);
    return;
  }
  // the panel: as large as the upper half of the picture allows, below the "⋯" of the on-screen controls
  // (the canvas's size as the renderer has it: no look at the page's layout each frame)
  host.renderer.getSize(size);
  const double cw = size.x;
  const double ch = size.y;
  double z = jsNumber(ui.style("zoom"), false);
  if (z != z || z == 0) z = 1;
  const bool touch = hud.hasClass("touchmode");
  const PanelRect rect = zoomPanel(cw, ch, zoomAspect(mirror->spec.width, mirror->spec.height), (touch ? 52 : 10) * z);
  const std::string key = jsStr(rect.x) + "," + jsStr(rect.y) + "," + jsStr(rect.w) + "," + jsStr(rect.h) + "," + jsStr(z) + "," + *shown + "," + lang;
  if (key != placed) {
    placed = key;
    // (the HUD is drawn at the size of the interface: its pixels are the screen's divided by that)
    frameEl.style("left", jsStr(rect.x / z - 3) + "px");
    frameEl.style("top", jsStr(rect.y / z - 3) + "px");
    frameEl.style("width", jsStr(rect.w / z) + "px");
    frameEl.style("height", jsStr(rect.h / z) + "px");
    label.textContent(tr(textOf(*shown), lang));
  }
  const Object3D* parent = mirror->mesh->parent;
  const bool drawn = parent != nullptr && zoom.draw(host.renderer, host.scene, *mirror, parent->matrixWorld, view.eye, view.far, MirrorZoom::Rect{rect.x, rect.y, rect.w, rect.h});
  if (frameEl.hidden() == drawn) frameEl.hidden(!drawn);
}

}  // namespace fs
