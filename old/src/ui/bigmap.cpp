// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The full-screen map (origin: src/ui/bigmap.ts @ ff37c5d, v0.7.0).
 *
 * The panel is a HudNode tree the platform renders; the picture is drawn by
 * `paint` into the canvas the platform paints (as the mini map does).  Drag,
 * tap, wheel and keys carry what the fingers and the keyboard do.
 */
#include "ui/bigmap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "geo/utm.h"
#include "i18n/index.h"
#include "ui/addresspicker.h"
#include "world/world.h"

namespace fs {

namespace {

/** px per metre: the whole map on a phone … one house filling the screen */
constexpr double MIN_SCALE = 0.012;
constexpr double MAX_SCALE = 4;

/** How roads are drawn, by priority tier (see src/world/types.ts): colour, least width in px, from which zoom on. */
struct RoadStyle {
  double min, max;
  const char* color;
  double px;
  double from;
};
const RoadStyle CLASSES[4] = {
    {0, 1.5, "#55606b", 0.8, 0.11},
    {2, 2.9, "#aab4bd", 1, 0.045},
    {3, 5.5, "#f1f3f5", 1.6, 0},
    {6, 9, "#f5b942", 2.4, 0},
};

/** How prominent a place is on the map. */
int rank(const std::string& kind) {
  if (kind == "city" || kind == "town") return 3;
  if (kind == "suburb" || kind == "village") return 2;
  if (kind == "quarter" || kind == "neighbourhood" || kind == "hamlet") return 1;
  return 0;
}

std::string t(const std::string& key, const std::string& lang) { return fs::tr(key, lang); }

}  // namespace

BigMap::BigMap(BigMapHost host)
    : host_(std::move(host)),
      photos_([this] { /* a picture arrived: the next paint shows it */ },
              host_.http) {}

BigMap::~BigMap() = default;

void BigMap::open(bool closeUp) {
  if (open_) return;
  double x = 0, z = 0, yaw = 0;
  if (host_.car) host_.car(x, z, yaw);
  cx_ = x;
  cz_ = z;
  scale_ = closeUp ? 1.6 : 0.22;
  hasSpot_ = false;
  open_ = true;

  const std::string lang = host_.lang ? host_.lang() : "en";
  auto root = std::make_unique<HudNode>();
  root->tag = "div";
  root->cls = "overlay bigmap";
  root->id = "bigmap";

  // the canvas the platform paints (the picture comes from paint())
  auto canvas = std::make_unique<HudNode>();
  canvas->tag = "canvas";
  canvas->id = "bigmapCanvas";
  root->add(std::move(canvas));

  // the top: title and tools
  auto top = std::make_unique<HudNode>();
  top->tag = "div";
  top->cls = "bm-top";
  top->id = "bmTop";
  auto title = std::make_unique<HudNode>();
  title->tag = "div";
  title->cls = "bm-title";
  auto b = std::make_unique<HudNode>();
  b->tag = "b";
  b->text = t("mapTitle", lang);
  title->add(std::move(b));
  auto small = std::make_unique<HudNode>();
  small->tag = "small";
  bool tele = host_.canTeleport && host_.canTeleport();
  small->text = t(tele ? "mapHint" : "mapHintLesson", lang);
  title->add(std::move(small));
  top->add(std::move(title));

  auto tools = std::make_unique<HudNode>();
  tools->tag = "div";
  tools->cls = "bm-tools";
  auto tool = [&](const std::string& label, const std::string& title2, const std::string& cls,
                 std::function<void()> fn) {
    auto btn = std::make_unique<HudNode>();
    btn->tag = "button";
    btn->cls = ("btn ghost " + cls);
    if (!cls.empty() && btn->cls.back() == ' ') btn->cls.pop_back();
    btn->text = label;
    btn->html = label;
    btn->click = std::move(fn);
    (void)title2;
    tools->add(std::move(btn));
  };
  tool("\xe2\x97\x8e", t("mapCentre", lang), "", [this]() {   // ◎
    double x = 0, z = 0, yaw = 0;
    if (host_.car) host_.car(x, z, yaw);
    cx_ = x;
    cz_ = z;
    if (scale_ < 0.12) scale_ = 0.22;
  });
  tool("+", "+", "", [this]() { zoom(1.6, -1, -1); });
  tool("\xe2\x88\x92", "\xe2\x88\x92", "", [this]() { zoom(1.0 / 1.6, -1, -1); });   // −
  tool("\xe2\x9c\x95", t("realityClose", lang), "bm-close", [this]() { close(); });  // ✕
  top->add(std::move(tools));
  root->add(std::move(top));

  // the bottom: aerial toggle, path toggle, clear path
  auto bottom = std::make_unique<HudNode>();
  bottom->tag = "div";
  bottom->cls = "bm-bottom";
  bottom->id = "bmBottom";
  auto check = [&](const std::string& id, const std::string& label, bool on,
                  std::function<void(bool)> set) {
    auto lab = std::make_unique<HudNode>();
    lab->tag = "label";
    lab->cls = "bm-check";
    auto box = std::make_unique<HudNode>();
    box->tag = "input";
    box->id = id;
    box->text = on ? "checked" : "";
    box->click = [set, on]() { set(!on); };
    lab->add(std::move(box));
    auto span = std::make_unique<HudNode>();
    span->tag = "span";
    span->text = label;
    lab->add(std::move(span));
    return lab;
  };
  bool aerialOn = host_.aerial && host_.aerial();
  bottom->add(check("bmAerial", t("mapAerial", lang), aerialOn, [this](bool on) {
    if (host_.setAerial) host_.setAerial(on);
  }));
  bottom->add(check("bmPath", t("mapPath", lang), showPath_, [this](bool on) {
    showPath_ = on;
  }));
  auto clear = std::make_unique<HudNode>();
  clear->tag = "button";
  clear->cls = "btn ghost";
  clear->text = t("mapClearPath", lang);
  clear->click = [this]() {
    if (host_.path) host_.path->clear();
  };
  bottom->add(std::move(clear));
  // a demonstration drive is planned here (a free drive only)
  if (tele) {
    auto demoBtn = std::make_unique<HudNode>();
    demoBtn->tag = "button";
    demoBtn->cls = "btn bm-demo";
    demoBtn->text = t("demo", lang);
    demoBtn->click = [this]() {
      planOpen_ = !planOpen_;
      fillPlan();
    };
    // (the demo button comes first in the row)
    bottom->children.insert(bottom->children.begin(), std::move(demoBtn));
  }
  root->add(std::move(bottom));

  // the plan panel and the spot bar (hidden until there is something)
  auto plan = std::make_unique<HudNode>();
  plan->tag = "div";
  plan->cls = "bm-plan";
  plan->id = "bmPlan";
  plan->text = "hidden";
  root->add(std::move(plan));
  auto bar = std::make_unique<HudNode>();
  bar->tag = "div";
  bar->cls = "bm-spot";
  bar->id = "bmSpot";
  bar->text = "hidden";
  root->add(std::move(bar));

  // hand the panel to the platform (the Hud restores its own tree on close)
  for (auto& c : root->children)
    if (c->id == "bmPlan") planNode_ = c.get();
  if (stops_.size() > 0) planOpen_ = true;
  if (host_.onShow) host_.onShow(std::move(root));
}

void BigMap::plan() {
  if (!open_ || !host_.canTeleport || !host_.canTeleport()) return;
  planOpen_ = true;
  fillPlan();
}

// ------------------------------------------------------- gestures

void BigMap::zoom(double factor, double px, double py) {
  // the world point under (px, py) keeps its place (default: the middle)
  int w = 0, h = 0;
  if (host_.windowSize) host_.windowSize(w, h);
  double ox = px < 0 ? 0 : px - w / 2.0;
  double oy = py < 0 ? 0 : py - h / 2.0;
  double next = std::min(MAX_SCALE, std::max(MIN_SCALE, scale_ * factor));
  cx_ += ox / scale_ - ox / next;
  cz_ += oy / scale_ - oy / next;
  scale_ = next;
}

void BigMap::choose(double px, double py) {
  int w = 0, h = 0;
  if (host_.windowSize) host_.windowSize(w, h);
  double x = cx_ + (px - w / 2.0) / scale_;
  double z = cz_ + (py - h / 2.0) / scale_;
  hasSpot_ = false;
  if (host_.spotAt) {
    SpawnJson s = host_.spotAt(x, z);
    if (!s.name.empty() || s.kind == "street") {
      spot_ = s;
      hasSpot_ = true;
    }
  }
}

void BigMap::down(double x, double y) {
  dragging_ = true;
  lastX_ = x;
  lastY_ = y;
  moved_ = 0;
}

void BigMap::drag(double x, double y) {
  if (!dragging_) return;
  double dx = x - lastX_, dy = y - lastY_;
  lastX_ = x;
  lastY_ = y;
  moved_ += static_cast<int>(std::fabs(dx) + std::fabs(dy));
  cx_ -= dx / scale_;
  cz_ -= dy / scale_;
}

void BigMap::up(double x, double y) {
  if (!dragging_) return;
  dragging_ = false;
  // a tap (not a drag): choose that place
  if (moved_ < 8) choose(x, y);
}

void BigMap::wheel(double dy, double x, double y) {
  zoom(std::exp(-std::max(-120.0, std::min(120.0, dy)) * 0.004), x, y);
}

void BigMap::key(const std::string& code) {
  int w = 0, h = 0;
  if (host_.windowSize) host_.windowSize(w, h);
  double step = 90 / scale_;
  if (code == "ArrowLeft") cx_ -= step;
  else if (code == "ArrowRight") cx_ += step;
  else if (code == "ArrowUp") cz_ -= step;
  else if (code == "ArrowDown") cz_ += step;
  else if (code == "Equal" || code == "NumpadAdd" || code == "Add") zoom(1.6, -1, -1);
  else if (code == "Minus" || code == "NumpadSubtract" || code == "Subtract") zoom(1.0 / 1.6, -1, -1);
}

void BigMap::fillPlan() {
  const std::string lang = host_.lang ? host_.lang() : "en";
  HudNode* plan = planNode_;
  if (!plan) return;
  plan->children.clear();
  plan->text = planOpen_ ? "" : "hidden";
  if (!planOpen_) return;
  auto b = std::make_unique<HudNode>();
  b->tag = "b";
  b->text = t("demo", lang);
  plan->add(std::move(b));
  auto hint = std::make_unique<HudNode>();
  hint->tag = "small";
  hint->text = t("demoHint", lang);
  plan->add(std::move(hint));

  // the address field: a place by its address
  AddressPickerOptions o;
  o.placeholder = t("addressPlaceholder", lang);
  o.loading = t("realityLoading", lang);
  o.none = t("addressNone", lang);
  o.load = host_.loadAddresses;
  o.pick = [this](const AddressHit& hit) {
    bool ok = false;
    if (host_.spotOf) {
      SpawnJson s = host_.spotOf(hit, ok);
      if (ok) {
        stops_.push_back(s);
        cx_ = s.x;
        cz_ = s.z;
        if (scale_ < 0.5) scale_ = 0.5;
        fillPlan();
      }
    }
  };
  picker_ = std::make_unique<AddressPicker>(addressPicker(std::move(o)));
  plan->children.push_back(std::move(picker_->node));

  if (stops_.empty()) {
    auto none = std::make_unique<HudNode>();
    none->tag = "small";
    none->cls = "bm-none";
    none->text = t("demoNone", lang);
    plan->add(std::move(none));
  } else {
    auto list = std::make_unique<HudNode>();
    list->tag = "ol";
    for (size_t i = 0; i < stops_.size(); i++) {
      auto li = std::make_unique<HudNode>();
      li->tag = "li";
      auto span = std::make_unique<HudNode>();
      span->tag = "span";
      const SpawnJson& st = stops_[i];
      if (!st.name.empty()) {
        span->text = st.name;
      } else if (host_.latLon) {
        auto ll = host_.latLon(st.x, st.z);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.5f, %.5f", ll.first, ll.second);
        span->text = buf;
      }
      li->add(std::move(span));
      auto del = std::make_unique<HudNode>();
      del->tag = "button";
      del->text = "\xe2\x9c\x95";   // ✕
      del->click = [this, i]() {
        stops_.erase(stops_.begin() + static_cast<long>(i));
        fillPlan();
      };
      li->add(std::move(del));
      list->add(std::move(li));
    }
    plan->add(std::move(list));
  }
  // the row: start (only with places), clear
  auto row = std::make_unique<HudNode>();
  row->tag = "div";
  row->cls = "bm-row";
  auto go = std::make_unique<HudNode>();
  go->tag = "button";
  go->cls = "btn";
  go->text = t("demoStart", lang);
  if (stops_.empty()) go->text += " (disabled)";
  go->click = [this]() {
    if (stops_.empty()) return;
    std::vector<SpawnJson> stops;
    stops.swap(stops_);
    if (host_.startDemo) host_.startDemo(stops);
    fillPlan();
  };
  row->add(std::move(go));
  if (!stops_.empty()) {
    auto clear = std::make_unique<HudNode>();
    clear->tag = "button";
    clear->cls = "btn ghost";
    clear->text = t("demoClear", lang);
    clear->click = [this]() {
      stops_.clear();
      fillPlan();
    };
    row->add(std::move(clear));
  }
  plan->add(std::move(row));
}

void BigMap::close() {
  if (!open_) return;
  open_ = false;
  root_.reset();
  planNode_ = nullptr;
  planOpen_ = false;
  picker_.reset();
  if (host_.onHide) host_.onHide();
  if (host_.close) host_.close();
}

void BigMap::paint(Canvas2D& g) {
  if (!open_ || !host_.world) return;
  int W = g.width(), H = g.height();
  if (W <= 0 || H <= 0) return;
  const WorldJson& m = host_.world->json();
  // do not lose the map: its middle stays within reach
  cx_ = std::min(m.meta.maxX, std::max(m.meta.minX, cx_));
  cz_ = std::min(m.meta.maxZ, std::max(m.meta.minZ, cz_));
  const double s = scale_;
  g.setTransform(1, 0, 0, 1, 0, 0);
  g.setAlpha(1);
  g.fillRect(0, 0, W, H, parseColor("#16202a"));
  // world → screen
  g.translate(W / 2.0, H / 2.0);
  g.scale(s, s);
  g.translate(-cx_, -cz_);
  const double minX = cx_ - W / 2.0 / s, maxX = cx_ + W / 2.0 / s;
  const double minZ = cz_ - H / 2.0 / s, maxZ = cz_ + H / 2.0 / s;

  // the area the map covers
  g.fillRect(m.meta.minX, m.meta.minZ, m.meta.maxX - m.meta.minX, m.meta.maxZ - m.meta.minZ,
             parseColor("#1c2a36"));

  // aerial photographs under the roads (only within the map, only what is in view)
  const bool aerial = host_.aerial && host_.aerial();
  if (aerial) {
    photos_.beginFrame();
    double e0 = std::max(minX, m.meta.minX) + m.meta.originE;
    double e1 = std::min(maxX, m.meta.maxX) + m.meta.originE;
    double n0 = m.meta.originN - std::min(maxZ, m.meta.maxZ);
    double n1 = m.meta.originN - std::max(minZ, m.meta.minZ);
    if (e1 > e0 && n1 > n0) {
      int level = aerialLevel(s);
      // never more than a screenful of pictures at once
      while (level < 8 && ((e1 - e0) / aerialTileSize(level) + 1) *
                                  ((n1 - n0) / aerialTileSize(level) + 1) >
                              30)
        level++;
      // a coarse picture first (it is there at once), the fine ones on top as they arrive
      for (int l : (level + 3 <= 8 ? std::vector<int>{level + 3, level} : std::vector<int>{8, level})) {
        for (const AerialTile& tile : aerialTiles(l, e0, n0, e1, n1)) {
          const Canvas2D* img = photos_.get(tile);
          if (img)
            g.drawImage(*img, tile.minE - m.meta.originE,
                        m.meta.originN - (tile.minN + tile.size), img->width(), img->height());
        }
        if (l == level) break;
      }
    }
  }

  // over a photograph the roads are only hinted at
  g.setAlpha(aerial ? 0.38 : 1);
  for (const RoadStyle& cls : CLASSES) {
    if (s < cls.from) continue;
    uint32_t color = parseColor(cls.color);
    // one pass per width: roads of a class are drawn at their real width once
    // that is more than the least
    std::map<int, std::vector<std::vector<std::pair<double, double>>>> byWidth;
    host_.world->edgesInBox(minX, minZ, maxX, maxZ, [&](Edge& e) {
      if (e.tier < cls.min || e.tier > cls.max) return;
      int w = std::max(static_cast<int>(cls.px / s), static_cast<int>(std::lround(e.width)));
      auto& list = byWidth[w];
      list.emplace_back();
      auto& pts = e.pl.pts;
      auto& path = list.back();
      if (pts.size() < 6) return;
      path.push_back({pts[0], pts[2]});
      for (size_t i = 3; i < pts.size(); i += 3) path.push_back({pts[i], pts[i + 2]});
    });
    for (auto& kv : byWidth)
      for (auto& path : kv.second) g.strokePolyline(path, color, kv.first, true);
  }
  g.setAlpha(1);

  // where the car has been
  if (showPath_ && host_.path) {
    const std::vector<double>& pts = host_.path->points;
    uint32_t blue = parseColor("#38b6ff");
    double lw = std::max(3.0 / s, 2.5);
    std::vector<std::pair<double, double>> run;
    for (size_t i = 0; i + 1 < pts.size(); i += 2) {
      if (std::isnan(pts[i])) {
        if (run.size() > 1) g.strokePolyline(run, blue, lw, true);
        run.clear();
        continue;
      }
      run.push_back({pts[i], pts[i + 1]});
    }
    if (run.size() > 1) g.strokePolyline(run, blue, lw, true);
  }

  // from here on in screen pixels
  const std::string lang = host_.lang ? host_.lang() : "en";
  g.setTransform(1, 0, 0, 1, 0, 0);
  auto sx = [&](double x) { return W / 2.0 + (x - cx_) * s; };
  auto sy = [&](double z) { return H / 2.0 + (z - cz_) * s; };

  // names of towns and districts
  struct Taken { double x, y, half; };
  std::vector<Taken> taken;
  double carX = 0, carZ = 0, carYaw = 0;
  if (host_.car) host_.car(carX, carZ, carYaw);
  const double carSX = sx(carX), carSY = sy(carZ);
  // (the names are drawn largest first so that a big town keeps its name)
  std::vector<const PlaceJson*> sorted;
  for (const PlaceJson& pl : m.places) {
    if (pl.x <= minX || pl.x >= maxX || pl.z <= minZ || pl.z >= maxZ) continue;
    if (rank(pl.kind) == 0) continue;
    sorted.push_back(&pl);
  }
  std::stable_sort(sorted.begin(), sorted.end(), [](const PlaceJson* a, const PlaceJson* b) {
    return rank(a->kind) < rank(b->kind);
  });
  for (const PlaceJson* pl : sorted) {
    int r = rank(pl->kind);
    if (r == 0 || (r == 1 && s < 0.05) || (r == 2 && s < 0.03)) continue;
    double size = r == 3 ? 17 : (r == 2 ? 14 : 12);
    double x = sx(pl->x);
    double y = sy(pl->z);
    double half = pl->name.size() * size * 0.3 + 6;
    // a name that would lie under the car's arrow is written below it
    if (std::fabs(x - carSX) < half + 10 && std::fabs(y - carSY) < size * 0.6 + 14)
      y = carSY + 18 + size * 0.6;
    bool clash = false;
    for (const Taken& tk : taken)
      if (std::fabs(tk.x - x) < tk.half + half && std::fabs(tk.y - y) < size + 4) clash = true;
    if (clash) continue;
    taken.push_back({x, y, half});
    uint32_t fill = r == 3 ? 0xffffffff : parseColor("#d5dde4");
    g.strokeText(pl->name, x, y, size, parseColor("#0c1818") | 0xff000000);
    g.fillText(pl->name, x, y, size, fill);
  }

  // the places of a demonstration drive, numbered in their order
  for (size_t i = 0; i < stops_.size(); i++) {
    double x = sx(stops_[i].x), y = sy(stops_[i].z);
    g.fillCircle(x, y, 11, parseColor("#38b6ff"));
    g.strokeCircle(x, y, 11, parseColor("#06131c"), 2);
    g.fillText(std::to_string(i + 1), x, y + 0.5, 13, parseColor("#06131c"));
  }

  // the chosen place: a pin over the spot
  if (hasSpot_) {
    double x = sx(spot_.x), y = sy(spot_.z);
    g.fillCircle(x, y - 14, 8, parseColor("#ff5252"));
    g.line(x, y - 14, x, y, 0xffffffff, 2);
  }

  // the car
  {
    double x = sx(carX), y = sy(carZ);
    // an arrow pointing the way the car faces (0 = north, positive = left)
    double a = -carYaw;
    double ca = std::cos(a), sa = std::sin(a);
    auto rot = [&](double px, double py, double& ox, double& oy) {
      ox = x + px * ca + py * sa;
      oy = y - px * sa + py * ca;
    };
    double p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y;
    rot(0, -13, p0x, p0y);
    rot(8, 9, p1x, p1y);
    rot(0, 5, p2x, p2y);
    rot(-8, 9, p3x, p3y);
    g.fillPoly({{p0x, p0y}, {p1x, p1y}, {p2x, p2y}, {p3x, p3y}}, parseColor("#ffcc00"));
    g.strokePolyline({{p0x, p0y}, {p1x, p1y}, {p2x, p2y}, {p3x, p3y}, {p0x, p0y}},
                     parseColor("#111111"), 2);
  }

  // scale: a round number of metres that is about a fifth of the width
  const double want = W / 5.0 / s;
  double pow10 = std::pow(10.0, std::floor(std::log10(want)));
  double metres = (want / pow10 >= 5 ? 5 : (want / pow10 >= 2 ? 2 : 1)) * pow10;
  double bar = metres * s;
  double bx = 14, by = H - 62;
  g.strokePolyline({{bx, by - 5}, {bx, by}, {bx + bar, by}, {bx + bar, by - 5}}, 0xffffffff, 2);
  std::string label = metres >= 1000
                          ? formatUnit(metres / 1000, "kilometer", lang, 1)
                          : formatUnit(metres, "meter", lang, 1);
  g.fillText(label, bx + 5, by - 4, 12, 0xffffffff);
  if (aerial) {
    // whose pictures these are
    g.strokeText(AERIAL_SOURCE.attribution, bx, by + 5, 11, parseColor("#0c1818") | 0xff000000);
    g.fillText(AERIAL_SOURCE.attribution, bx, by + 5, 11, 0xffffffff);
  }
}

}  // namespace fs
