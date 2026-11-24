// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/minimap.ts @ v1.7.7
#include "ui/minimap.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "platform/json.h"
#include "platform/observers.h"
#include "platform/plat.h"

namespace fs {

using dom::El;

namespace {
const char* const KEY = "fahrsim-baunatal.minimap.v1";
/** how long after looking round the map comes back to the vehicle (ms) */
const double BACK_AFTER = 6000;
}  // namespace

std::vector<std::pair<std::string, MiniMap::Place>> MiniMap::loadPlaces() {
  std::vector<std::pair<std::string, Place>> out;
  const std::optional<std::string> raw = plat::localStorage().getItem(KEY);
  Json v;
  if (!raw || !Json::parse(*raw, v) || !v.isObject()) return out;
  for (const auto& [name, p] : v.object) {
    // (what is no place gives no numbers: the map then stays where the HUD puts it)
    Place place;
    place.fx = p.isObject() && p.at("fx").isNumber() ? p.at("fx").number : JS_NAN;
    place.fy = p.isObject() && p.at("fy").isNumber() ? p.at("fy").number : JS_NAN;
    out.emplace_back(name, place);
  }
  return out;
}

MiniMap::MiniMap(const dom::El& box_, const Canvas& canvas_, MiniMapHost host_) : host(std::move(host_)), box(box_), canvas(canvas_), places(loadPlaces()) {
  if (AerialCache::create) photosCache = AerialCache::create([] {}, 48);
  box.addClass("mini");
  box.style("touchAction", "none");
  // (what a tap does, and that the rim moves it)
  {
    std::vector<std::string> parts;
    for (const std::string& s : {box.get("title"), host.moveLabel()})
      if (!s.empty()) parts.push_back(s);
    box.title(join(parts, " · "));
  }
  backBtn = dom::el("button");
  backBtn.type("button");
  backBtn.className("mini-back");
  backBtn.hidden(true);
  backBtn.innerHTML(
      "<svg viewBox=\"0 0 24 24\" aria-hidden=\"true\"><circle cx=\"12\" cy=\"12\" r=\"3.2\" fill=\"currentColor\"/><path d=\"M12 2.5v4M12 17.5v4M2.5 12h4M17.5 12h4\" stroke=\"currentColor\" "
      "stroke-width=\"2.4\" stroke-linecap=\"round\"/><circle cx=\"12\" cy=\"12\" r=\"6.6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"/></svg>");
  backBtn.onclick([this](dom::Event& e) {
    e.stopPropagation();
    ox = oz = 0;
    backBtn.hidden(true);
  });
  box.append(backBtn);
  box.on("pointerdown", [this](dom::Event& e) { down(e); });
  box.on("pointermove", [this](dom::Event& e) { move(e); });
  box.on("pointerup", [this](dom::Event& e) { up(e); });
  box.on("pointercancel", [this](dom::Event& e) { up(e); });
  // a drag is no tap: the click after it is swallowed before the map's own handler sees it
  box.on(
      "click",
      [this](dom::Event& e) {
        if (!eatClick) return;
        eatClick = false;
        e.stopPropagation();
        e.preventDefault();
      },
      dom::ListenOptions{.capture = true});
  dom::window().on("resize", [this] { applyPlace(); });
}

MiniMap::Place* MiniMap::placeOf(const std::string& l) {
  for (auto& p : places)
    if (p.first == l) return &p.second;
  return nullptr;
}

std::string MiniMap::layoutNow() const {
  const El hud = box.closest(".hud");
  return hud && hud.hasClass("touchmode") ? "touch" : "keys";
}

void MiniMap::applyPlace() {
  const Place* p = placeOf(layout);
  if (!p) {
    box.style("left", "");
    box.style("top", "");
    box.style("right", "");
    box.style("bottom", "");
    return;
  }
  const auto [W, H] = room();
  putAt(p->fx * W, p->fy * H);
}

std::array<double, 2> MiniMap::room() const {
  const El parent = plat::offsetParent(box);
  return {parent ? parent.clientWidth() : plat::innerWidth(), parent ? parent.clientHeight() : plat::innerHeight()};
}

double MiniMap::ratio() const { return box.offsetWidth() / std::max(1.0, box.getBoundingClientRect().width); }

void MiniMap::putAt(double left, double top) {
  const auto [W, H] = room();
  box.style("left", jsStr(jsRound(jsMax(0, jsMin(W - box.offsetWidth(), left)))) + "px");
  box.style("top", jsStr(jsRound(jsMax(0, jsMin(H - box.offsetHeight(), top)))) + "px");
  box.style("right", "auto");
  box.style("bottom", "auto");
}

bool MiniMap::onRim(const dom::Event& e) const {
  const dom::Rect r = box.getBoundingClientRect();
  const double R = r.width / 2;
  const double d = std::hypot(e.clientX - (r.left + R), e.clientY - (r.top + r.height / 2));
  return d > R - std::max(13.0, R * 0.2);
}

void MiniMap::down(dom::Event& e) {
  eatClick = false;
  // (the buttons on the map keep their own taps)
  if (e.target() && e.target().closest("button")) return;
  const bool mv = onRim(e);
  if (mv) {
    // a double tap on the rim: back to where the map belongs
    const double now = plat::nowMs();
    if (now - lastRimTap < 350) {
      for (size_t i = 0; i < places.size(); i++)
        if (places[i].first == layout) {
          places.erase(places.begin() + static_cast<long>(i));
          break;
        }
      save();
      applyPlace();
      lastRimTap = 0;
      return;
    }
    lastRimTap = now;
  }
  drag = Drag{e.pointerId, mv, e.clientX, e.clientY, 0, box.num("offsetLeft"), box.num("offsetTop")};
  box.setPointerCapture(e.pointerId);
}

void MiniMap::move(dom::Event& e) {
  if (!drag || drag->id != e.pointerId) return;
  Drag& d = *drag;
  const double dx = e.clientX - d.x;
  const double dy = e.clientY - d.y;
  if (d.move) {
    d.moved = std::max(d.moved, std::hypot(dx, dy));
    if (d.moved < 4) return;
    const double k = ratio();
    putAt(d.left + dx * k, d.top + dy * k);
    return;
  }
  d.x = e.clientX;
  d.y = e.clientY;
  d.moved += std::abs(dx) + std::abs(dy);
  if (d.moved < 6) return;
  // the finger moves the map under it: its middle goes the other way, turned back into the directions of the world
  const double k = canvas.width() / std::max(1.0, canvas.el().getBoundingClientRect().width) / scale;
  const double c = std::cos(yaw);
  const double sn = std::sin(yaw);
  ox -= (dx * c + dy * sn) * k;
  oz -= (-dx * sn + dy * c) * k;
  // (not further than the map shows twice over)
  const double far = 1200;
  const double l = std::hypot(ox, oz);
  if (l > far) {
    ox *= far / l;
    oz *= far / l;
  }
  lookedAt = plat::nowMs();
  backBtn.hidden(false);
}

void MiniMap::up(dom::Event& e) {
  if (!drag || drag->id != e.pointerId) return;
  const Drag d = *drag;
  drag.reset();
  if (d.moved >= 6) eatClick = true;
  if (d.move && d.moved >= 4) {
    const auto [W, H] = room();
    const Place place{box.num("offsetLeft") / std::max(1.0, W), box.num("offsetTop") / std::max(1.0, H)};
    if (Place* p = placeOf(layout)) *p = place;
    else places.emplace_back(layout, place);
    save();
  } else if (d.move) {
    // (a tap on the rim is a tap on the map)
    eatClick = false;
  }
  if (!d.move && d.moved >= 6) lookedAt = plat::nowMs();
}

void MiniMap::save() {
  JsonWriter w;
  w.beginObject();
  for (const auto& [name, p] : places) {
    w.key(name);
    w.beginObject();
    w.key("fx");
    w.number(p.fx);
    w.key("fy");
    w.number(p.fy);
    w.endObject();
  }
  w.endObject();
  // (a browser that keeps nothing: the place holds until the page is left)
  plat::localStorage().setItem(KEY, w.take());
}

std::array<double, 2> MiniMap::centre(double x, double z, double yaw_, double scale_, bool own) {
  const double now = plat::nowMs();
  const double dt = std::min(0.5, (now - lastCentre) / 1000);
  lastCentre = now;
  if (own) {
    yaw = yaw_;
    scale = scale_;
    const std::string l = layoutNow();
    if (l != layout) {
      layout = l;
      applyPlace();
    }
  }
  if ((ox != 0 || oz != 0) && !drag && now - lookedAt > BACK_AFTER) {
    const double k = std::exp(-dt * 3);
    ox *= k;
    oz *= k;
    if (std::hypot(ox, oz) < 1) {
      ox = oz = 0;
      backBtn.hidden(true);
    }
  }
  const std::string label = host.backLabel();
  backBtn.title(label);
  backBtn.setAttribute("aria-label", label);
  return {x + ox, z + oz};
}

bool MiniMap::photos(const Ctx2D& g, double cx, double cz, double range, double pxPerMetre) {
  if (!host.photos()) return false;
  // (the aerial photographs are not translated yet: ui/maphooks.h)
  if (!photosCache) return false;
  const MiniMapHost::Meta& m = host.meta;
  const double e0 = std::max(cx - range, m.minX) + m.originE;
  const double e1 = std::min(cx + range, m.maxX) + m.originE;
  const double n0 = m.originN - std::min(cz + range, m.maxZ);
  const double n1 = m.originN - std::max(cz - range, m.minZ);
  if (e1 <= e0 || n1 <= n0) return false;
  photosCache->beginFrame();
  const int level = photosCache->aerialLevel(pxPerMetre);
  bool any = false;
  // a coarse picture first (there at once when the vehicle moves on), the fine ones over it
  const std::vector<int> levels = level + 2 <= 8 ? std::vector<int>{level + 2, level} : std::vector<int>{level};
  for (int l : levels) {
    for (const AerialTile& t : photosCache->aerialTiles(l, e0, n0, e1, n1)) {
      const Image* img = photosCache->get(t);
      if (!img) continue;
      g.drawImage(*img, t.minE - m.originE, m.originN - (t.minN + t.size), t.size, t.size);
      any = true;
    }
  }
  return any;
}

}  // namespace fs
