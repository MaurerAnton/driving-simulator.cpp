// SPDX-License-Identifier: AGPL-3.0-only
#include "ui/worldlayer.h"

#include <algorithm>
#include <cmath>

#include "platform/plat.h"

namespace fs {

namespace {

// the map's colours (the big map's own, a shade apart for land and sea)
const char* const SEA = "#131c25";
const char* const LAND = "#1f2b36";
const char* const BORDER = "#3b4d5d";
const char* const COAST = "#5a6f82";
const char* const COUNTRY_NAME = "rgba(170, 180, 189, 0.55)";
const char* const REGION = "#38b6ff";
const char* const REGION_FILL = "rgba(56, 182, 255, 0.16)";
const char* const CURRENT = "#ffcc00";
const char* const CURRENT_FILL = "rgba(255, 204, 0, 0.14)";
const char* const CHOSEN_FILL = "rgba(255, 255, 255, 0.22)";
const char* const TEXT = "#f1f3f5";
const char* const TEXT_EDGE = "rgba(12, 18, 24, 0.85)";

/** a box this wide on the screen (px) is drawn as a box with its name in it; smaller ones are dots */
const double BOX_MIN = 54;
/** a finger reaches a dot from this far (px) */
const double REACH = 22;

bool boxOnScreen(const std::array<double, 4>& b, const MapView& v, double pad = 0) {
  return v.sx(b[2]) >= -pad && v.sx(b[0]) <= v.width + pad && v.sy(b[3]) >= -pad && v.sy(b[1]) <= v.height + pad;
}

/** a ring's or a line's points in the picture (the plain ones near, the coarse ones far out) */
void toScreen(const std::vector<double>& xz, const MapView& v, std::vector<double>& out) {
  out.resize(xz.size());
  for (size_t i = 0; i + 1 < xz.size(); i += 2) {
    out[i] = v.sx(xz[i]);
    out[i + 1] = v.sy(xz[i + 1]);
  }
}

}  // namespace

Task<void> WorldLayer::load(std::string siteBase, std::array<double, 4> siteMap, std::string siteName) {
  if (ready_) co_return;
  Task<plat::Response> outlineAsked = plat::fetch(siteBase + "data/outline.bin");
  std::optional<RegionIndex> index = co_await loadRegionIndex(siteBase);
  plat::Response res = co_await std::move(outlineAsked);
  if (res.ok()) outline_ = parseWorldOutline(res.body);
  regions_.clear();
  Region site;
  site.id = SITE;
  site.name = siteName;
  site.box = siteMap;
  regions_.push_back(site);
  if (index)
    for (Region& r : index->regions) regions_.push_back(std::move(r));
  country_.assign(regions_.size(), -1);
  if (!outline_.empty()) {
    for (size_t i = 0; i < regions_.size(); i++) {
      const OutlineCountry* c = countryOfBox(outline_, regions_[i].box);
      if (c) country_[i] = static_cast<int>(c - outline_.countries.data());
    }
  }
  ready_ = true;
}

const Region* WorldLayer::byId(const std::string& id) const {
  for (const Region& r : regions_)
    if (r.id == id) return &r;
  return nullptr;
}

std::string WorldLayer::countryOf(const Region& r, const std::string& lang) const {
  size_t i = static_cast<size_t>(&r - regions_.data());
  if (i >= country_.size() || country_[i] < 0) return "";
  const OutlineCountry& c = outline_.countries[static_cast<size_t>(country_[i])];
  return lang == "de" ? c.nameDe : c.nameEn;
}

bool WorldLayer::overlaps(const std::vector<Placed>& placed, const Placed& p) {
  for (const Placed& q : placed)
    if (p.x0 < q.x1 && p.x1 > q.x0 && p.y0 < q.y1 && p.y1 > q.y0) return true;
  return false;
}

void WorldLayer::draw(const Ctx2D& g, const MapView& v, const std::string& current, const std::string& chosen, const std::string& lang) const {
  g.fillStyle(SEA);
  g.fillRect(0, 0, v.width, v.height);
  const double mpp = 1 / v.scale;   // metres a pixel
  const bool far = !outline_.empty() && mpp >= outline_.farTolerance * 0.6;
  std::vector<double> pts;
  if (!outline_.empty()) {
    // the land: every country's rings as one path (holes run the other way round and are left out)
    g.fillStyle(LAND);
    for (const OutlineCountry& c : outline_.countries) {
      if (!boxOnScreen(c.box, v)) continue;
      g.beginPath();
      for (const OutlineRing& r : c.rings) {
        if (!boxOnScreen(r.box, v)) continue;
        const std::vector<double>& xz = far && !r.far.empty() ? r.far : (far ? r.far : r.xz);
        if (xz.size() < 6) continue;
        toScreen(xz, v, pts);
        g.polyline(pts, true);
      }
      g.fill();
    }
    g.fillStyle(SEA);
    for (const OutlineLake& l : outline_.lakes) {
      if (!boxOnScreen(l.box, v)) continue;
      g.beginPath();
      for (const OutlineRing& r : l.rings) {
        const std::vector<double>& xz = far ? r.far : r.xz;
        if (xz.size() < 6) continue;
        toScreen(xz, v, pts);
        g.polyline(pts, true);
      }
      g.fill();
    }
    g.lineJoin("round");
    for (int pass = 0; pass < 2; pass++) {
      // borders under the coasts
      g.strokeStyle(pass == 0 ? BORDER : COAST);
      g.lineWidth(pass == 0 ? 1 : 1.2);
      g.beginPath();
      for (const OutlineLine& l : outline_.lines) {
        if ((l.kind == OutlineLine::Border) != (pass == 0)) continue;
        if (!boxOnScreen(l.box, v)) continue;
        const std::vector<double>& xz = far ? l.far : l.xz;
        if (xz.size() < 4) continue;
        toScreen(xz, v, pts);
        g.polyline(pts, false);
      }
      g.stroke();
    }
    // the countries' names, while a country is the size of the picture or smaller
    if (v.scale < WORLD_SCALE * 0.5) {
      g.textAlign("center");
      g.textBaseline("middle");
      g.fillStyle(COUNTRY_NAME);
      for (const OutlineCountry& c : outline_.countries) {
        const double w = (c.box[2] - c.box[0]) * v.scale;
        if (w < 90 || w > v.width * 3) continue;
        const double x = v.sx(c.labelX), y = v.sy(c.labelZ);
        if (x < -50 || x > v.width + 50 || y < -20 || y > v.height + 20) continue;
        const int size = static_cast<int>(std::max(12.0, std::min(26.0, w / 9)));
        g.font("600 " + std::to_string(size) + "px system-ui, sans-serif");
        std::string name = lang == "de" ? c.nameDe : c.nameEn;
        // (spaced capitals, as maps write countries)
        g.letterSpacing("0.12em");
        g.fillText(name, x, y);
        g.letterSpacing("0px");
      }
    }
  }
  if (regions_.empty()) return;

  // the regions: large ones first, so that a small one inside a large one lies on top
  std::vector<size_t> order(regions_.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  auto area = [&](size_t i) {
    const auto& b = regions_[i].box;
    return (b[2] - b[0]) * (b[3] - b[1]);
  };
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return area(a) > area(b); });
  std::vector<Placed> placed;
  struct Label {
    size_t i;
    double x, y;
    bool inBox;
  };
  std::vector<Label> labels;
  for (size_t i : order) {
    const Region& r = regions_[i];
    if (!boxOnScreen(r.box, v, 30)) continue;
    const bool isCurrent = r.id == current, isChosen = r.id == chosen && !isCurrent;
    const double x0 = v.sx(r.box[0]), y0 = v.sy(r.box[1]), w = (r.box[2] - r.box[0]) * v.scale, h = (r.box[3] - r.box[1]) * v.scale;
    if (w >= BOX_MIN) {
      g.fillStyle(isChosen ? CHOSEN_FILL : isCurrent ? CURRENT_FILL : REGION_FILL);
      // (where the loaded region's roads are drawn over it, its box is only an outline)
      if (!(isCurrent && v.scale >= WORLD_SCALE)) g.fillRect(x0, y0, w, h);
      g.strokeStyle(isCurrent ? CURRENT : REGION);
      g.lineWidth(isCurrent || isChosen ? 2.5 : 1.5);
      g.strokeRect(x0, y0, w, h);
      labels.push_back({i, x0 + w / 2, y0 + std::min(h / 2, 16.0), true});
    } else {
      const double cx = x0 + w / 2, cy = y0 + h / 2;
      const double rad = isCurrent || isChosen ? 6 : 4.2;
      g.beginPath();
      g.arc(cx, cy, rad, 0, 6.283185307179586);
      g.fillStyle(isCurrent ? CURRENT : isChosen ? "#ffffff" : REGION);
      g.fill();
      g.lineWidth(1.5);
      g.strokeStyle(TEXT_EDGE);
      g.stroke();
      labels.push_back({i, cx, cy - rad - 9, false});
    }
  }
  // their names: the loaded and the chosen one first, then the large ones; a name that would lie on another is left out
  std::stable_sort(labels.begin(), labels.end(), [&](const Label& a, const Label& b) {
    const int pa = regions_[a.i].id == current ? 0 : regions_[a.i].id == chosen ? 1 : 2;
    const int pb = regions_[b.i].id == current ? 0 : regions_[b.i].id == chosen ? 1 : 2;
    if (pa != pb) return pa < pb;
    return area(a.i) > area(b.i);
  });
  g.textAlign("center");
  g.textBaseline("middle");
  g.lineJoin("round");
  for (const Label& l : labels) {
    const Region& r = regions_[l.i];
    const bool strong = r.id == current || r.id == chosen;
    const int size = strong ? 14 : 12;
    g.font(std::string(strong ? "700 " : "600 ") + std::to_string(size) + "px system-ui, sans-serif");
    const double tw = g.measureText(r.name).width;
    Placed p{l.x - tw / 2 - 3, l.y - size / 2.0 - 2, l.x + tw / 2 + 3, l.y + size / 2.0 + 2};
    if (!strong && overlaps(placed, p)) continue;
    placed.push_back(p);
    g.lineWidth(3.5);
    g.strokeStyle(TEXT_EDGE);
    g.strokeText(r.name, l.x, l.y);
    g.fillStyle(r.id == current ? CURRENT : TEXT);
    g.fillText(r.name, l.x, l.y);
  }
}

const Region* WorldLayer::regionAt(const MapView& v, double px, double py) const {
  const double x = v.wx(px), z = v.wz(py);
  const Region* best = nullptr;
  double bestArea = 0;
  // the smallest box that holds the point
  for (const Region& r : regions_) {
    if (!boxContains(r.box, x, z)) continue;
    const double a = (r.box[2] - r.box[0]) * (r.box[3] - r.box[1]);
    if (!best || a < bestArea) {
      best = &r;
      bestArea = a;
    }
  }
  // … unless a dot is nearer to the finger than that box is big: small regions are dots
  const Region* dot = nullptr;
  double dotD = REACH;
  for (const Region& r : regions_) {
    const double w = (r.box[2] - r.box[0]) * v.scale;
    if (w >= BOX_MIN) continue;
    const double d = std::hypot(v.sx((r.box[0] + r.box[2]) / 2) - px, v.sy((r.box[1] + r.box[3]) / 2) - py);
    if (d < dotD) {
      dot = &r;
      dotD = d;
    }
  }
  if (dot && (!best || (best->box[2] - best->box[0]) * v.scale >= BOX_MIN)) return dot;
  return best ? best : dot;
}

MapView WorldLayer::fitAll(double width, double height) const {
  MapView v;
  v.width = width;
  v.height = height;
  if (regions_.empty()) return v;
  double x0 = regions_[0].box[0], z0 = regions_[0].box[1], x1 = regions_[0].box[2], z1 = regions_[0].box[3];
  for (const Region& r : regions_) {
    x0 = std::min(x0, r.box[0]);
    z0 = std::min(z0, r.box[1]);
    x1 = std::max(x1, r.box[2]);
    z1 = std::max(z1, r.box[3]);
  }
  v.cx = (x0 + x1) / 2;
  v.cz = (z0 + z1) / 2;
  v.scale = std::max(MIN_SCALE, std::min(width / (x1 - x0), height / (z1 - z0)) * 0.9);
  return v;
}

MapView WorldLayer::fit(const Region& r, double width, double height) const {
  MapView v;
  v.width = width;
  v.height = height;
  v.cx = (r.box[0] + r.box[2]) / 2;
  v.cz = (r.box[1] + r.box[3]) / 2;
  v.scale = std::min(width / (r.box[2] - r.box[0] + 400), height / (r.box[3] - r.box[1] + 400)) * 0.8;
  return v;
}

}  // namespace fs
