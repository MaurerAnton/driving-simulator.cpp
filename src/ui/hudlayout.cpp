// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/hudlayout.ts @ v1.7.7
#include "ui/hudlayout.h"

#include <algorithm>
#include <cmath>

#include "audio/timers.h"
#include "core/js.h"
#include "platform/plat.h"

namespace fs {

using dom::El;

static const char* const KEY = "fahrsim-baunatal.hudlayout.v1";
/** set once the notice about arranging was shown (or the player found it alone) */
static const char* const HINT_KEY = "fahrsim-baunatal.hudlayout.hinted";
/** how far (px) it may wander meanwhile */
static const double LONG_PRESS_SLOP = 10;
/** seconds into the drive the notice about arranging is shown */
static const double HINT_AFTER = 20;

const std::vector<HudLayout::Item>& HudLayout::ITEMS() {
  static const std::vector<Item> items = {
      {"nav", ".side > .nav", "hudLayout.nav"},
      {"assist", ":scope > .assist", "hudLayout.assist"},
      {"dash", ":scope > .dash", "hudLayout.dash"},
      {"tip", ":scope > .tip", "hudLayout.tip"},
      {"score", ":scope > .score", "hudLayout.score"},
      {"keys", ":scope > .keyhelp", "hudLayout.keys"},
      {"fps", ":scope > .fps", "hudLayout.fps"},
      {"clock", ".clock-badge", "hudLayout.clock"},
      {"newVehicle", ".new-vehicle", "hudLayout.newVehicle"},
      {"mirrorBtn", ".mirrorbtn", "mirrorLook"},
      {"locate", ".mini-back", "hudLayout.locate", true},
      {"mapBtn", ".expand", "mapTitle", true},
      // the on-screen controls (touch layout)
      {"steer", ".touch > .steer", "hudLayout.steer"},
      {"gas", ".touch > .pedal.gas", "tbGas"},
      {"brake", ".touch > .pedal.brk", "tbBrake"},
      {"indL", ".touch > .ind-l", "tbIndLeft"},
      {"indR", ".touch > .ind-r", "tbIndRight"},
      {"gearUp", ".touch > .gear-up", "tbGearUp"},
      {"gearDown", ".touch > .gear-dn", "tbGearDown"},
      {"handbrake", ".touch > .hb", "tbHandbrake"},
      {"autopilot", ".touch > .ap", "tbAutopilot"},
      {"more", ".touch > .more-btn", "tbMore"},
  };
  return items;
}

static bool hintedBefore() { return plat::localStorage().getItem(HINT_KEY).has_value(); }

static Json load() {
  Json v;
  const std::optional<std::string> text = plat::localStorage().getItem(KEY);
  if (!Json::parse(text ? *text : "{}", v)) return Json::makeObject();
  return v.isObject() ? v : Json::makeObject();
}

/** `Math.round(v)` written as a number of pixels */
static std::string px(double v) { return jsStr(jsRound(v)) + "px"; }

HudLayout::HudLayout(const El& hud, std::function<std::string(const std::string& key)> t, std::function<void(const std::string& text)> hint)
    : hud(hud), t(std::move(t)), saved(load()), hint(std::move(hint)), hinted(hintedBefore()) {
  El::Options capture;
  capture.capture = true;
  const El window = dom::window();
  onWindow.push_back(window.on("resize", [this] { applyAll(); }));
  // a long press on a read-out begins arranging (on the window: the read-outs let the fingers through to the picture)
  onWindow.push_back(window.on("pointerdown", [this](dom::Event& e) { pressDown(e); }, capture));
  // while arranging: a finger on a part drags it – its own buttons and handlers get nothing
  onHud.push_back(hud.on("pointerdown", [this](dom::Event& e) { down(e); }, capture));
  onHud.push_back(hud.on("click", [this](dom::Event& e) { swallow(e); }, capture));
  onWindow.push_back(window.on("pointermove", [this](dom::Event& e) { move(e); }));
  onWindow.push_back(window.on("pointerup", [this](dom::Event& e) { up(e); }));
  onWindow.push_back(window.on("pointercancel", [this](dom::Event& e) { up(e); }));
}

HudLayout::~HudLayout() {
  endPress();
  const El window = dom::window();
  window.off("resize", onWindow[0]);
  window.off("pointerdown", onWindow[1], true);
  window.off("pointermove", onWindow[2]);
  window.off("pointerup", onWindow[3]);
  window.off("pointercancel", onWindow[4]);
  hud.off("pointerdown", onHud[0], true);
  hud.off("click", onHud[1], true);
}

bool HudLayout::arranging() const { return hud.hasClass("arranging"); }

std::string HudLayout::layoutNow() const { return hud.hasClass("touchmode") ? "touch" : "keys"; }

HudLayout::Bound* HudLayout::boundOf(const std::string& id) {
  for (auto& b : bound)
    if (b.first == id) return &b.second;
  return nullptr;
}

/** `this.saved[this.layout]?.[id]` */
std::optional<HudLayout::Pos> HudLayout::posOf(const std::string& id) const {
  const Json& p = saved.at(layout).at(id);
  if (!p.isObject()) return std::nullopt;
  const Json& free = p.at("free");
  return Pos{p.at("x").asNum(), p.at("y").asNum(), free.isBool() && free.boolean};
}

void HudLayout::tick(double now) {
  if (!hinted && std::isfinite(now)) tickHint(now);
  const std::string layoutIs = layoutNow();
  if (layoutIs == layout && now - lastCheck < 0.5 && now >= lastCheck) return;
  lastCheck = now;
  const bool changed = layoutIs != layout;
  layout = layoutIs;
  for (const Item& item : ITEMS()) {
    const Bound* b = boundOf(item.id);
    if (b && b->el.isConnected() && !changed) continue;
    bind(item);
  }
}

El HudLayout::find(const Item& item) {
  const Bound* b = boundOf(item.id);
  if (b && b->el.isConnected()) return b->el;
  return hud.querySelector(item.selector);
}

void HudLayout::bind(const Item& item) {
  const El el = find(item);
  if (!el) return;
  Bound* b = boundOf(item.id);
  if (!b || b->el != el) {
    // (the element after it: the parts of the display have no text between them)
    const Bound made{el, el.parentElement(), el.nextElementSibling()};
    if (b) *b = made;
    else {
      bound.emplace_back(item.id, made);
      b = &bound.back().second;
    }
    el.dataset("hudMove", item.id);
    el.dataset("hudName", t(item.name));
  }
  apply(item, *b);
}

void HudLayout::applyAll() {
  for (const Item& item : ITEMS()) {
    Bound* b = boundOf(item.id);
    if (b) apply(item, *b);
  }
}

/** Put a part where this layout keeps it (or where it belongs). */
void HudLayout::apply(const Item& item, Bound& b) {
  const std::optional<Pos> p = posOf(item.id);
  const El& el = b.el;
  const double cw = hud.clientWidth();
  const double ch = hud.clientHeight();
  const double W = cw != 0 ? cw : plat::innerWidth();
  const double H = ch != 0 ? ch : plat::innerHeight();
  if (p && p->free && item.detach) {
    if (el.parentElement() != hud) hud.append(el);
    el.addClass("hud-free");
    el.style("translate", "");
    el.style("left", px(p->x * W));
    el.style("top", px(p->y * H));
    el.style("bottom", "auto");
    el.style("right", "auto");
    return;
  }
  if (el.hasClass("hud-free")) putBack(b);
  el.style("translate", p ? px(p->x * W) + " " + px(p->y * H) : "");
}

/** A part taken out of its box: back into it. */
void HudLayout::putBack(Bound& b) {
  const El& el = b.el;
  el.removeClass("hud-free");
  el.style("bottom", "");
  el.style("right", "");
  el.style("top", "");
  el.style("left", "");
  if (b.parent && b.parent.isConnected() && el.parentElement() != b.parent) b.parent.insertBefore(el, b.next && b.next.parentElement() == b.parent ? b.next : El());
}

/** Once, some seconds into a drive: a notice that the display can be arranged (not to one who has arranged it already). */
void HudLayout::tickHint(double now) {
  if (hintFrom < 0 || now < hintFrom) hintFrom = now;
  if (now - hintFrom < HINT_AFTER || hud.style("display") == "none") return;
  markHinted();
  if (saved.object.empty() && hint) hint(t("hudLayout.firstHint"));
}

void HudLayout::markHinted() {
  hinted = true;
  // (a browser that keeps nothing: kept for this page only)
  plat::localStorage().setItem(HINT_KEY, "1");
}

void HudLayout::arrange() {
  endPress();
  if (arranging()) return;
  if (!hinted) markHinted();
  tick(JS_INF);
  lastCheck = -1;
  hud.addClass("arranging");
  for (const Item& item : ITEMS()) {
    const Bound* b = boundOf(item.id);
    if (b) b->el.dataset("hudName", t(item.name));
  }
  El shieldEl = dom::el("div");
  shieldEl.className("hud-arrange-shield");
  El barEl = dom::el("div");
  barEl.className("hud-arrange");
  El text = dom::el("span");
  text.textContent(t("hudLayout.hint"));
  El resetBtn = dom::el("button");
  resetBtn.type("button");
  resetBtn.className("btn ghost");
  resetBtn.textContent(t("hudLayout.reset"));
  resetBtn.onclick([this] { reset(); });
  El done = dom::el("button");
  done.type("button");
  done.className("btn");
  done.textContent(t("hudLayout.done"));
  done.onclick([this] { stop(); });
  barEl.append(text, resetBtn, done);
  hud.prepend(shieldEl);
  hud.append(barEl);
  shield = shieldEl;
  bar = barEl;
}

void HudLayout::stop() {
  hud.removeClass("arranging");
  bar.remove();
  shield.remove();
  bar = shield = El();
  drag.reset();
}

void HudLayout::reset() {
  saved = Json::makeObject();
  save();
  for (const Item& item : ITEMS()) {
    Bound* b = boundOf(item.id);
    if (!b) continue;
    if (b->el.hasClass("hud-free")) putBack(*b);
    b->el.style("translate", "");
  }
}

void HudLayout::save() {
  // (a browser that keeps nothing: the layout holds until the page is left)
  plat::localStorage().setItem(KEY, saved.stringify());
}

void HudLayout::down(dom::Event& e) {
  const El target = e.target();
  if (!arranging() || !target) return;
  if (bar && bar.contains(target)) return;
  const El el = target.closest("[data-hud-move]");
  const Item* item = nullptr;
  if (el) {
    const std::string id = el.dataset("hudMove");
    for (const Item& i : ITEMS())
      if (i.id == id) {
        item = &i;
        break;
      }
  }
  e.preventDefault();
  e.stopPropagation();
  if (!el || !item || !hud.contains(el)) return;
  // how many of the HUD's own pixels a pixel of the screen is (the interface may be zoomed)
  double k = hud.clientWidth() / std::max(1.0, hud.getBoundingClientRect().width);
  if (k == 0 || k != k) k = 1;
  const std::optional<Pos> p = posOf(item->id);
  double pxAt = 0;
  double pyAt = 0;
  if (p && p->free && item->detach) {
    pxAt = el.num("offsetLeft");
    pyAt = el.num("offsetTop");
  } else if (p) {
    pxAt = p->x * hud.clientWidth();
    pyAt = p->y * hud.clientHeight();
  }
  drag = Drag{e.pointerId, item, el, e.clientX, e.clientY, pxAt, pyAt, k, false};
}

/** The part of the display under a point that a long press begins arranging on, if any. */
const HudLayout::Item* HudLayout::readOutAt(double x, double y) {
  for (const std::string& id : LONG_PRESS_IDS) {
    const Bound* b = boundOf(id);
    if (!b || !b->el.isConnected()) continue;
    const dom::Rect r = b->el.getBoundingClientRect();
    if (r.width > 0 && r.height > 0 && x >= r.left && x <= r.right() && y >= r.top && y <= r.bottom()) {
      for (const Item& i : ITEMS())
        if (i.id == id) return &i;
      return nullptr;
    }
  }
  return nullptr;
}

void HudLayout::pressDown(dom::Event& e) {
  endPress();
  if (arranging() || e.button > 0 || hud.style("display") == "none" || !hud.isConnected()) return;
  const El target = e.target();
  // (the controls on the screen, the corner map, the mirror, the menus: theirs)
  if (target && target.closest(".touch, .map, .mirrorbtn, .mirrorzoom, .overlay, select, input")) return;
  if (!readOutAt(e.clientX, e.clientY)) return;
  const int id = e.pointerId;
  Press p{id, e.clientX, e.clientY, 0};
  p.timer = timers::setTimeout(
      [this, id] {
        if (!press || press->id != id) return;
        press.reset();
        plat::vibrate(25);
        arrange();
      },
      LONG_PRESS_MS);
  press = p;
}

void HudLayout::endPress() {
  if (press) timers::clearTimeout(press->timer);
  press.reset();
}

void HudLayout::move(dom::Event& e) {
  if (press && press->id == e.pointerId && std::hypot(e.clientX - press->x, e.clientY - press->y) > LONG_PRESS_SLOP) endPress();
  if (!drag || drag->id != e.pointerId) return;
  Drag& d = *drag;
  const double dx = e.clientX - d.x;
  const double dy = e.clientY - d.y;
  if (!d.moved && std::hypot(dx, dy) < 4) return;
  if (!d.moved && d.item->detach && !d.el.hasClass("hud-free")) {
    // out of its box: from where it is on the screen now
    const dom::Rect r = d.el.getBoundingClientRect();
    const dom::Rect h = hud.getBoundingClientRect();
    d.px = (r.left - h.left) * d.k;
    d.py = (r.top - h.top) * d.k;
    store(*d.item, d.px, d.py, true);
    apply(*d.item, *boundOf(d.item->id));
  }
  d.moved = true;
  const bool free = d.el.hasClass("hud-free");
  double x = d.px + dx * d.k;
  double y = d.py + dy * d.k;
  // (not off the screen: the part's rectangle stays within the HUD)
  const double W = hud.clientWidth();
  const double H = hud.clientHeight();
  if (free) {
    x = std::max(0.0, std::min(W - d.el.offsetWidth(), x));
    y = std::max(0.0, std::min(H - d.el.offsetHeight(), y));
  } else {
    const dom::Rect r = d.el.getBoundingClientRect();
    const dom::Rect h = hud.getBoundingClientRect();
    const std::optional<Pos> cur = posOf(d.item->id);
    // where it would be without a shift (screen px)
    const double baseL = r.left - h.left - ((cur ? cur->x : 0) * W) / d.k;
    const double baseT = r.top - h.top - ((cur ? cur->y : 0) * H) / d.k;
    x = std::max(-baseL * d.k, std::min((h.width - r.width - baseL) * d.k, x));
    y = std::max(-baseT * d.k, std::min((h.height - r.height - baseT) * d.k, y));
  }
  store(*d.item, x, y, free);
  apply(*d.item, *boundOf(d.item->id));
}

void HudLayout::up(dom::Event& e) {
  if (press && press->id == e.pointerId) endPress();
  if (!drag || drag->id != e.pointerId) return;
  const bool moved = drag->moved;
  drag.reset();
  if (moved) save();
}

void HudLayout::swallow(dom::Event& e) {
  const El target = e.target();
  if (!arranging() || !target) return;
  if (bar && bar.contains(target)) return;
  e.preventDefault();
  e.stopPropagation();
}

void HudLayout::store(const Item& item, double x, double y, bool free) {
  const double W = std::max(1.0, hud.clientWidth());
  const double H = std::max(1.0, hud.clientHeight());
  // `this.saved[this.layout] ??= {}`
  Json* l = saved.object.find(layout);
  if (!l || l->isNull()) l = &saved.set(layout, Json::makeObject());
  Json pos = Json::makeObject();
  pos.set("x", x / W);
  pos.set("y", y / H);
  if (free) pos.set("free", true);
  l->set(item.id, std::move(pos));
}

}  // namespace fs
