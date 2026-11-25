// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/transitmap.ts @ v1.7.7
#include "ui/transitmap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/js.h"
#include "platform/plat.h"

namespace fs {

using dom::El;
using dom::el;

// ---------------------------------------------------------------- colours

const std::map<std::string, std::string>& KVG_TRAM_COLOURS() {
  static auto* t = new std::map<std::string, std::string>{
      {"1", "#00954b"}, {"2", "#0069b4"}, {"3", "#95c11f"}, {"4", "#f08100"}, {"5", "#7f5ca3"}, {"6", "#8a4f38"}, {"7", "#fbba00"}, {"8", "#00a5df"},
  };
  return *t;
}

const std::vector<std::string>& LINE_PALETTE() {
  static auto* t = new std::vector<std::string>{
      "#e6194b", "#3cb44b", "#4363d8", "#f58231", "#911eb4", "#42d4f4", "#f032e6", "#bfef45",
      "#469990", "#9a6324", "#800000", "#808000", "#000075", "#e6beff", "#ffd8b1", "#aaffc3",
  };
  return *t;
}

namespace {

/** The colour names OpenStreetMap's route relations use (CSS names), as #rrggbb. */
const std::map<std::string, std::string>& NAMED() {
  static auto* t = new std::map<std::string, std::string>{
      {"black", "#000000"}, {"white", "#ffffff"}, {"red", "#ff0000"}, {"darkred", "#8b0000"}, {"maroon", "#800000"}, {"crimson", "#dc143c"},
      {"green", "#008000"}, {"darkgreen", "#006400"}, {"lightgreen", "#90ee90"}, {"lime", "#00ff00"}, {"limegreen", "#32cd32"}, {"olive", "#808000"},
      {"blue", "#0000ff"}, {"darkblue", "#00008b"}, {"navy", "#000080"}, {"lightblue", "#add8e6"}, {"skyblue", "#87ceeb"}, {"royalblue", "#4169e1"},
      {"steelblue", "#4682b4"}, {"cyan", "#00ffff"}, {"aqua", "#00ffff"}, {"teal", "#008080"}, {"turquoise", "#40e0d0"}, {"yellow", "#ffff00"},
      {"gold", "#ffd700"}, {"orange", "#ffa500"}, {"darkorange", "#ff8c00"}, {"orangered", "#ff4500"}, {"purple", "#800080"}, {"violet", "#ee82ee"},
      {"magenta", "#ff00ff"}, {"fuchsia", "#ff00ff"}, {"pink", "#ffc0cb"}, {"hotpink", "#ff69b4"}, {"brown", "#a52a2a"}, {"chocolate", "#d2691e"},
      {"grey", "#808080"}, {"gray", "#808080"}, {"silver", "#c0c0c0"}, {"darkgrey", "#a9a9a9"}, {"darkgray", "#a9a9a9"}, {"lightgrey", "#d3d3d3"},
      {"lightgray", "#d3d3d3"}, {"beige", "#f5f5dc"}, {"khaki", "#f0e68c"}, {"salmon", "#fa8072"}, {"coral", "#ff7f50"}, {"tomato", "#ff6347"},
      {"indigo", "#4b0082"}, {"darkviolet", "#9400d3"}, {"plum", "#dda0dd"}, {"tan", "#d2b48c"}, {"sienna", "#a0522d"},
  };
  return *t;
}

bool allOf(const std::string& s, size_t n, bool (*ok)(char)) {
  if (s.size() != n) return false;
  for (char c : s)
    if (!ok(c)) return false;
  return true;
}
bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

/** the UTF-16 code units of a UTF-8 string (charCodeAt) */
std::vector<uint16_t> utf16(const std::string& s) {
  std::vector<uint16_t> out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp = c;
    size_t len = 1;
    if (c >= 0xF0) {
      cp = c & 0x07;
      len = 4;
    } else if (c >= 0xE0) {
      cp = c & 0x0F;
      len = 3;
    } else if (c >= 0xC0) {
      cp = c & 0x1F;
      len = 2;
    }
    for (size_t k = 1; k < len && i + k < s.size(); k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += len;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
    } else out.push_back(static_cast<uint16_t>(cp));
  }
  return out;
}

/** Seconds after midnight as the clock shows them ("7:05"; past midnight counted on). */
std::string clock(double t) {
  const int64_t s = ((static_cast<int64_t>(jsRound(t)) % 86400) + 86400) % 86400;
  return std::to_string(s / 3600) + ":" + padStart(std::to_string((s % 3600) / 60), 2, '0');
}

/** Between two stop places (their numbers in stops.json.gz), by the share of the way – a straight line where nothing better is known. */
std::optional<Spot> betweenPlaces(Stops* stops, int a, int b, double frac) {
  const StopPlace* pa = stops && a >= 0 && a < static_cast<int>(stops->places.size()) ? &stops->places[static_cast<size_t>(a)] : nullptr;
  if (!pa) return std::nullopt;
  const StopPlace* pb = stops && b >= 0 && b < static_cast<int>(stops->places.size()) ? &stops->places[static_cast<size_t>(b)] : pa;
  const double f = std::max(0.0, std::min(1.0, frac));
  const double dx = pb->x - pa->x;
  const double dz = pb->z - pa->z;
  return Spot{pa->x + dx * f, pa->z + dz * f, dx != 0 || dz != 0 ? std::atan2(-dx, -dz) : 0, std::nullopt};
}

/** from this zoom on (px per metre) the stops of the buses are drawn – the trams' the map draws itself from 0.05 on */
constexpr double BUS_STOPS_FROM = 0.45;
/** below this zoom the vehicles are dots without their numbers */
constexpr double SMALL_BELOW = 0.035;
/** how far from a vehicle's sign a tap still chooses it (px) – a fingertip's width */
constexpr double TAP_REACH = 28;

}  // namespace

std::optional<std::string> parseColour(const std::string& c) {
  if (c.empty()) return std::nullopt;
  const std::string s = toLowerAscii(trim(c));
  if (s.empty()) return std::nullopt;
  const std::string hex = s[0] == '#' ? s.substr(1) : s;
  if (allOf(hex, 6, isHex)) return "#" + hex;
  if (allOf(hex, 3, isHex)) return std::string("#") + hex[0] + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];
  auto it = NAMED().find(s);
  if (it != NAMED().end()) return it->second;
  return std::nullopt;
}

std::string paletteColour(const TransitKind& kind, const std::string& ref) {
  bool digits = !ref.empty();
  for (char c : ref)
    if (c < '0' || c > '9') digits = false;
  double k = 0;
  if (digits) k = jsNumber(ref);
  else {
    // (a small, stable hash of the name: the same line always gets the same colour)
    uint64_t h = kind == "tram" ? 7 : 3;
    for (uint16_t u : utf16(ref)) h = (h * 31 + u) & 0xFFFFFFFFu;
    k = static_cast<double>(h);
  }
  const std::vector<std::string>& pal = LINE_PALETTE();
  return pal[static_cast<size_t>(std::fmod(k, static_cast<double>(pal.size())))];
}

std::string lineColour(const TransitKind& kind, const std::string& ref, const std::vector<std::string>& given) {
  for (const std::string& g : given) {
    const std::optional<std::string> c = parseColour(g);
    if (c) return *c;
  }
  if (kind == "tram") {
    auto it = KVG_TRAM_COLOURS().find(ref);
    if (it != KVG_TRAM_COLOURS().end()) return it->second;
  }
  return paletteColour(kind, ref);
}

std::string textOn(const std::string& colour) {
  const std::string c = parseColour(colour).value_or("#808080");
  const auto lin = [&](size_t i) {
    const double v = static_cast<double>(std::stoi(c.substr(i, 2), nullptr, 16)) / 255;
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  const double l = 0.2126 * lin(1) + 0.7152 * lin(3) + 0.0722 * lin(5);
  return l > 0.36 ? "#111111" : "#ffffff";
}

// ---------------------------------------------------------------- where the vehicles are

std::optional<Spot> pointAlong(const std::vector<float>& course, double m) {
  const size_t n = course.size() >> 1;
  if (n < 2) return std::nullopt;
  double left = std::max(0.0, m);
  std::optional<Spot> last;
  for (size_t i = 0; i + 1 < n; i++) {
    const double ax = course[i * 2];
    const double az = course[i * 2 + 1];
    const double dx = course[i * 2 + 2] - ax;
    const double dz = course[i * 2 + 3] - az;
    const double l = std::hypot(dx, dz);
    if (l < 1e-6) continue;
    const double yaw = std::atan2(-dx, -dz);
    if (left <= l) return Spot{ax + (dx * left) / l, az + (dz * left) / l, yaw, std::nullopt};
    left -= l;
    last = Spot{ax + dx, az + dz, yaw, std::nullopt};
  }
  return last;
}

double courseLength(const std::vector<float>& course) {
  double len = 0;
  for (size_t i = 2; i + 1 < course.size(); i += 2) len += std::hypot(static_cast<double>(course[i]) - course[i - 2], static_cast<double>(course[i + 1]) - course[i - 1]);
  return len;
}

std::optional<Spot> alongBusLine(const BusLine& line, int a, int b, double frac, const std::function<TransitCourse(int way)>& course) {
  if (a < 0 || a >= static_cast<int>(line.stops.size())) return std::nullopt;
  const BusLineStop& sa = line.stops[static_cast<size_t>(a)];
  const BusLineStop* sb = b >= 0 && b < static_cast<int>(line.stops.size()) ? &line.stops[static_cast<size_t>(b)] : &sa;
  // (a stop that lies behind the first one: stand at the first)
  if (sb->k < sa.k || (sb->k == sa.k && sb->m < sa.m)) sb = &sa;
  std::vector<double> lens;
  for (int k = sa.k; k <= sb->k; k++) {
    const int w = k >= 0 && k < static_cast<int>(line.ways.size()) ? line.ways[static_cast<size_t>(k)] : -1;
    const TransitCourse c = w < 0 ? nullptr : course(w);
    if (!c) return std::nullopt;
    lens.push_back(courseLength(*c));
  }
  double total = 0;
  if (sa.k == sb->k) total = sb->m - sa.m;
  else {
    double mid = 0;
    for (size_t i = 1; i + 1 < lens.size(); i++) mid += lens[i];
    total = lens[0] - sa.m + mid + sb->m;
  }
  double left = std::max(0.0, std::min(1.0, frac)) * std::max(0.0, total);
  int k = sa.k;
  double at = std::min(sa.m, lens[0]);
  while (k < sb->k && at + left > lens[static_cast<size_t>(k - sa.k)]) {
    left -= lens[static_cast<size_t>(k - sa.k)] - at;
    at = 0;
    k++;
  }
  const TransitCourse c = course(line.ways[static_cast<size_t>(k)]);
  std::optional<Spot> p = c ? pointAlong(*c, at + left) : std::nullopt;
  if (!p) return std::nullopt;
  p->k = k;
  return p;
}

std::string tripName(const TransitKind& kind, int line, const TimetablePattern& pattern, double t0) {
  int p = -1;
  const std::vector<TimetablePattern>& list = pattern.line->patterns;
  for (size_t i = 0; i < list.size(); i++)
    if (&list[i] == &pattern) {
      p = static_cast<int>(i);
      break;
    }
  const std::string tail = std::to_string(line) + ":" + std::to_string(p) + ":" + jsStr(t0);
  return kind == "tram" ? tail : "b:" + tail;
}

std::optional<Spot> spotOf(TransitSource& src, const TripSpot& run) {
  const double frac = run.standing ? 0 : run.frac;
  if (run.kind == "tram") {
    const LineJson* line = src.tramLine(run.line);
    const auto stop = [&](int i) -> const std::pair<int, double>* {
      return line && i >= 0 && i < static_cast<int>(line->stops.size()) ? &line->stops[static_cast<size_t>(i)] : nullptr;
    };
    const std::pair<int, double>* a = stop(run.fromStop);
    if (a) {
      const std::pair<int, double>* b = stop(run.toStop);
      if (!b) b = a;
      std::optional<Spot> s = src.tramSpot(run.line, *a, *b, frac);
      if (s) return s;
    }
  } else {
    BusLines* lines = src.busLines();
    const BusLine* line = lines && run.line >= 0 && run.line < static_cast<int>(lines->lines.size()) ? lines->lines[static_cast<size_t>(run.line)] : nullptr;
    if (line && run.fromStop >= 0) {
      std::optional<Spot> s = alongBusLine(*line, run.fromStop, run.toStop >= 0 ? run.toStop : run.fromStop, frac, [&](int w) { return src.wayCourse(w); });
      if (s) return s;
    }
  }
  return betweenPlaces(src.stops(), run.fromPlace, run.toPlace, frac);
}

std::vector<MapVehicle> vehiclesAt(TransitSource& src, const Timetable& tt, const ServiceTime& at, const std::function<std::string(const TransitKind&, int, const std::string&, const std::string&)>& colour) {
  std::vector<MapVehicle> out;
  for (Running& run : runningAt(tt, at)) {
    const std::optional<Spot> s = spotOf(src, TripSpot{run.kind, run.line, run.fromStop, run.toStop, run.fromPlace, run.toPlace, run.standing, run.frac});
    if (!s) continue;
    MapVehicle v;
    v.trip = tripName(run.kind, run.line, *run.pattern, run.t0);
    v.x = s->x;
    v.z = s->z;
    v.yaw = s->yaw;
    v.colour = colour(run.kind, run.line, run.ref, run.colour);
    v.run = std::move(run);
    out.push_back(std::move(v));
  }
  return out;
}

std::vector<Departure> stopDepartures(const Timetable& tt, const StopPlace& place, const ServiceTime& at, int n) {
  std::vector<Departure> list = departures(tt, StopKey(place.id), at, n);
  bool anyTram = false;
  for (const Departure& d : list)
    if (d.kind == "tram") anyTram = true;
  if ((place.kind & STOP_FLAG_TRAM) != 0 && !anyTram) {
    std::vector<Departure> trams;
    for (Departure& d : departures(tt, StopKey(place.name), at, n))
      if (d.kind == "tram") trams.push_back(std::move(d));
    if (!trams.empty()) {
      list.insert(list.end(), trams.begin(), trams.end());
      std::stable_sort(list.begin(), list.end(), [](const Departure& x, const Departure& y) { return x.time < y.time; });
      if (static_cast<int>(list.size()) > n) list.resize(static_cast<size_t>(n));
    }
  }
  return list;
}

// ---------------------------------------------------------------- the layer on the big map

TransitLayer::TransitLayer(TransitMapHost host) : host_(std::move(host)) {}

TransitLayer::~TransitLayer() { timer_.reset(); }

std::string TransitLayer::t(const std::string& key, const TrParams& p) const { return tr(key, host_.lang(), p); }

void TransitLayer::attach(const El& ui, const El& before) {
  pop_ = el("div", "bm-pop");
  pop_.hidden(true);
  ui.insertBefore(pop_, before);
  sel_.reset();
  list_.clear();
  listAt_ = -1e9;
  // once a second: the vehicles a step further (the map drawn again only if one is in view), the minutes of the departures counted down
  timer_ = std::make_shared<int>(0);
  std::weak_ptr<int> alive = timer_;
  auto tick = std::make_shared<std::function<void()>>();
  std::weak_ptr<std::function<void()>> again = tick;
  *tick = [this, alive, again] {
    if (!alive.lock()) return;
    if (shown && anyInView()) host_.redraw();
    if (sel_ && plat::nowMs() - popAt_ > 10000) render();
    if (auto f = again.lock(); f && alive.lock()) plat::setTimeout([f] { (*f)(); }, 1000);
  };
  // (each timeout holds the function while it waits: when the token `timer_` is gone, no next one is set)
  plat::setTimeout([tick] { (*tick)(); }, 1000);
}

void TransitLayer::detach() {
  timer_.reset();
  if (pop_) pop_.remove();
  pop_ = El();
  sel_.reset();
}

El TransitLayer::toggle() {
  El l = el("label", "bm-check");
  El box = el("input");
  box.type("checkbox");
  box.checked(shown);
  box.onchange([this, box] {
    shown = box.checked();
    if (!shown) closePopup();
    host_.redraw();
  });
  l.append(box, el("span", "", t("transitShow")));
  return l;
}

void TransitLayer::closePopup() {
  sel_.reset();
  menuAt_.reset();
  if (pop_) pop_.hidden(true);
}

void TransitLayer::showLine(const TransitKind& kind, int index, const std::string& colour) {
  line_ = LineShown{kind, index, colour};
  render();
  host_.redraw();
}

bool TransitLayer::lineShown(const TransitKind& kind, int index) const { return line_ && line_->kind == kind && line_->index == index; }

void TransitLayer::placeMenu(double x, double y) {
  const El& pop = pop_;
  if (!pop || pop.hidden() || !sel_ || sel_->kind != "vehicle") return;
  if (menuAt_ && std::abs(menuAt_->x - x) < 0.5 && std::abs(menuAt_->y - y) < 0.5) return;
  menuAt_ = MenuAt{x, y};
  const double W = view_.W;
  const double H = view_.H;
  const double w = pop.offsetWidth();
  const double h = pop.offsetHeight();
  const double gap = 26;
  const double left = std::max(8.0, std::min(W - w - 8, x - w / 2));
  const double below = y + gap;
  const double top = below + h <= H - 64 || y - gap - h < 8 ? std::min(below, std::max(8.0, H - h - 8)) : y - gap - h;
  pop.style("left", jsStr(jsRound(left)) + "px");
  pop.style("top", jsStr(jsRound(top)) + "px");
}

std::string TransitLayer::colourOf(const TransitKind& kind, int line, const std::string& ref, const std::string& own) {
  const std::string key = kind + ":" + std::to_string(line) + ":" + ref;
  auto it = colours_.find(key);
  if (it != colours_.end() && !it->second.empty()) return it->second;
  TransitSource* src = host_.source();
  std::optional<std::string> fileColour;
  bool file = false;
  if (src) {
    if (kind == "tram") {
      if (const LineJson* l = src->tramLine(line)) {
        file = true;
        fileColour = l->colour;
      }
    } else if (BusLines* bl = src->busLines(); bl && line >= 0 && line < static_cast<int>(bl->lines.size())) {
      file = true;
      fileColour = bl->lines[static_cast<size_t>(line)]->colour;
    }
  }
  std::vector<std::string> given{own};
  if (fileColour) given.push_back(*fileColour);
  const std::string c = lineColour(kind, ref, given);
  // (kept once the line's file is read: before, its own colour may not be known yet)
  if (file) colours_[key] = c;
  return c;
}

bool TransitLayer::anyInView() {
  TransitSource* src = host_.source();
  const Timetable* tt = src ? src->timetable() : nullptr;
  if (!src || !tt) return false;
  if (line_) return true;
  const double hw = view_.W / 2 / view_.s + 30;
  const double hh = view_.H / 2 / view_.s + 30;
  for (const MapVehicle& v : vehicles(*src, *tt))
    if (std::abs(v.x - view_.cx) < hw && std::abs(v.z - view_.cz) < hh) return true;
  return false;
}

const std::vector<MapVehicle>& TransitLayer::vehicles(TransitSource& src, const Timetable& tt) {
  const double now = plat::nowMs();
  if (now - listAt_ > 900) {
    listAt_ = now;
    list_ = vehiclesAt(src, tt, src.now(), [this](const TransitKind& k, int l, const std::string& r, const std::string& o) { return colourOf(k, l, r, o); });
  }
  return list_;
}

void TransitLayer::draw(const Ctx2D& g, double W, double H, double s, double cx, double cz) {
  view_ = View{W, H, s, cx, cz};
  if (!shown) return;
  TransitSource* src = host_.source();
  if (!src) return;
  const auto sx = [&](double x) { return W / 2 + (x - cx) * s; };
  const auto sy = [&](double z) { return H / 2 + (z - cz) * s; };
  g.save();
  g.lineCap("round");
  g.lineJoin("round");
  // the way of the line asked for
  if (line_) {
    const std::vector<TransitCourse> courses = lineCourses(*src, line_->kind, line_->index);
    g.strokeStyle(line_->colour);
    g.globalAlpha(0.75);
    g.lineWidth(std::max(4.0, std::min(9.0, 6 * s)));
    g.beginPath();
    for (const TransitCourse& cp : courses) {
      const std::vector<float>& c = *cp;
      for (size_t i = 0; i + 1 < c.size(); i += 2) {
        if (i == 0) g.moveTo(sx(c[0]), sy(c[1]));
        else g.lineTo(sx(c[i]), sy(c[i + 1]));
      }
    }
    g.stroke();
    g.globalAlpha(1);
  }
  // the stops of the buses, close up (the trams' are drawn by the map)
  Stops* stops = src->stops();
  if (stops && s >= BUS_STOPS_FROM) {
    const bool big = s >= 1.1;
    std::vector<StopPlace*> near;
    for (const StopPlace* p : stops->near(cx, cz, std::max(W, H) / 2 / s + 30, near)) {
      if ((p->kind & STOP_FLAG_BUS) == 0 || (p->kind & STOP_FLAG_TRAM) != 0) continue;
      const double x = sx(p->x);
      const double y = sy(p->z);
      if (x < -20 || y < -20 || x > W + 20 || y > H + 20) continue;
      const double r = big ? 7 : 4.5;
      g.fillStyle("#f6d31b");
      g.strokeStyle("#0b6b3a");
      g.lineWidth(1.6);
      g.beginPath();
      g.rect(x - r, y - r, r * 2, r * 2);
      g.fill();
      g.stroke();
      if (big) {
        g.fillStyle("#0b6b3a");
        g.font("800 10px system-ui, sans-serif");
        g.textAlign("center");
        g.textBaseline("middle");
        g.fillText("H", x, y + 0.5);
      }
    }
  }
  // the stop chosen
  if (sel_ && sel_->kind == "stop" && sel_->place) {
    const StopPlace& p = *sel_->place;
    g.strokeStyle("#ffffff");
    g.lineWidth(3);
    g.beginPath();
    g.arc(sx(p.x), sy(p.z), 12, 0, M_PI * 2);
    g.stroke();
  }
  // the vehicles
  const Timetable* tt = src->timetable();
  if (tt) {
    const std::vector<MapVehicle>& list = vehicles(*src, *tt);
    const bool small = s < SMALL_BELOW;
    const std::string chosen = sel_ && sel_->kind == "vehicle" ? sel_->trip : "";
    const MapVehicle* top = nullptr;
    for (const MapVehicle& v : list) {
      if (v.trip == chosen) top = &v;
      else drawVehicle(g, v, sx(v.x), sy(v.z), small, false);
    }
    if (top) drawVehicle(g, *top, sx(top->x), sy(top->z), small, true);
    // (the menu of the vehicle chosen goes with it)
    if (top) placeMenu(sx(top->x), sy(top->z));
  }
  g.restore();
}

void TransitLayer::drawVehicle(const Ctx2D& g, const MapVehicle& v, double x, double y, bool small, bool chosen) {
  const double W = view_.W;
  const double H = view_.H;
  if (x < -24 || y < -24 || x > W + 24 || y > H + 24) return;
  const bool dim = line_ && (line_->kind != v.run.kind || line_->index != v.run.line);
  g.globalAlpha(dim ? 0.45 : 1);
  if (small && !chosen) {
    g.fillStyle(v.colour);
    g.strokeStyle("#0c1218");
    g.lineWidth(1);
    g.beginPath();
    g.arc(x, y, 3.5, 0, M_PI * 2);
    g.fill();
    g.stroke();
    g.globalAlpha(1);
    return;
  }
  const std::string ref = !v.run.ref.empty() ? v.run.ref : "?";
  const double r = ref.size() >= 3 ? 12 : 10;
  // a point towards where it goes
  const double fx = -std::sin(v.yaw);
  const double fz = -std::cos(v.yaw);
  g.fillStyle(v.colour);
  g.strokeStyle("#0c1218");
  g.lineWidth(1.5);
  g.beginPath();
  g.moveTo(x + fx * (r + 6), y + fz * (r + 6));
  g.lineTo(x + fx * r * 0.4 - fz * r * 0.75, y + fz * r * 0.4 + fx * r * 0.75);
  g.lineTo(x + fx * r * 0.4 + fz * r * 0.75, y + fz * r * 0.4 - fx * r * 0.75);
  g.closePath();
  g.fill();
  g.stroke();
  // the round sign: a tram's with a white ring, a bus's with a dark one
  g.beginPath();
  g.arc(x, y, r, 0, M_PI * 2);
  g.fill();
  g.lineWidth(chosen ? 3 : 1.6);
  g.strokeStyle(chosen ? "#ffffff" : v.run.kind == "tram" ? "#f4f6f8" : "#0c1218");
  g.stroke();
  g.fillStyle(textOn(v.colour));
  g.font(std::string("800 ") + (ref.size() >= 3 ? "9" : "11") + "px system-ui, sans-serif");
  g.textAlign("center");
  g.textBaseline("middle");
  g.fillText(ref, x, y + 0.5);
  g.globalAlpha(1);
}

std::vector<TransitCourse> TransitLayer::lineCourses(TransitSource& src, const TransitKind& kind, int index) {
  std::vector<TransitCourse> out;
  if (kind == "tram") {
    TransitCourse c = src.tramCourse(index);
    if (c) out.push_back(c);
    return out;
  }
  BusLines* lines = src.busLines();
  const BusLine* line = lines && index >= 0 && index < static_cast<int>(lines->lines.size()) ? lines->lines[static_cast<size_t>(index)] : nullptr;
  if (!line) return out;
  for (int k = 0; k < line->end; k++) {
    const int w = line->ways[static_cast<size_t>(k)];
    TransitCourse c = w >= 0 ? src.wayCourse(w) : nullptr;
    if (c) out.push_back(c);
  }
  return out;
}

bool TransitLayer::tap(double px, double py) {
  if (!shown || !pop_) return false;
  TransitSource* src = host_.source();
  if (!src) return false;
  const double W = view_.W;
  const double H = view_.H;
  const double s = view_.s;
  const double cx = view_.cx;
  const double cz = view_.cz;
  const auto sx = [&](double x) { return W / 2 + (x - cx) * s; };
  const auto sy = [&](double z) { return H / 2 + (z - cz) * s; };
  // a vehicle (the nearest within reach of a finger)
  // (a finger covers more than the sign: within TAP_REACH of its middle counts)
  const MapVehicle* best = nullptr;
  double bd = s < SMALL_BELOW ? TAP_REACH * 0.7 : TAP_REACH;
  for (const MapVehicle& v : list_) {
    const double d = std::hypot(sx(v.x) - px, sy(v.z) - py);
    if (d < bd) {
      bd = d;
      best = &v;
    }
  }
  if (best) {
    sel_ = Sel{"vehicle", best->trip, nullptr};
    const double bx = sx(best->x);
    const double by = sy(best->z);
    render();
    placeMenu(bx, by);
    host_.redraw();
    return true;
  }
  // a stop: the trams' from far off (the map draws them), the buses' close up
  Stops* stops = src->stops();
  if (stops && s >= 0.05) {
    StopPlace* place = nullptr;
    double pd = 15;
    std::vector<StopPlace*> near;
    for (StopPlace* p : stops->near(cx + (px - W / 2) / s, cz + (py - H / 2) / s, 20 / s, near)) {
      const bool tram = (p->kind & STOP_FLAG_TRAM) != 0;
      if (!tram && ((p->kind & STOP_FLAG_BUS) == 0 || s < BUS_STOPS_FROM)) continue;
      const double d = std::hypot(sx(p->x) - px, sy(p->z) - py);
      if (d < pd) {
        pd = d;
        place = p;
      }
    }
    if (place) {
      sel_ = Sel{"stop", "", place};
      render();
      host_.redraw();
      return true;
    }
  }
  return false;
}

void TransitLayer::render() {
  const El pop = pop_;
  if (!pop) return;
  if (!sel_) {
    pop.hidden(true);
    return;
  }
  const Sel sel = *sel_;
  popAt_ = plat::nowMs();
  TransitSource* src = host_.source();
  const Timetable* tt = src ? src->timetable() : nullptr;
  pop.replaceChildren();
  pop.hidden(false);
  const El head = el("div", "bm-head");
  const El close = el("button", "bm-x", "✕");
  close.type("button");
  close.setAttribute("aria-label", t("realityClose"));
  close.onclick([this] {
    closePopup();
    host_.redraw();
  });
  pop.toggleClass("bm-ctx", sel.kind == "vehicle");
  if (sel.kind != "vehicle") {
    pop.style("left", "");
    pop.style("top", "");
  }
  if (sel.kind == "vehicle") {
    const MapVehicle* found = nullptr;
    for (const MapVehicle& q : list_)
      if (q.trip == sel.trip) found = &q;
    if (!found || !src) {
      // (its trip is over)
      closePopup();
      return;
    }
    const MapVehicle v = *found;
    const Running& run = v.run;
    const El title = el("div", "bm-pop-title");
    title.append(badge(run.kind, run.ref, v.colour), el("b", "", !run.dest.empty() ? run.dest : run.toName));
    head.append(title, close);
    pop.append(head);
    const double now = src->now().s;
    const double mins = std::max(0.0, jsRound((run.until - now) / 60));
    const std::string line = run.standing ? t("transitAt", {{"stop", !run.fromName.empty() ? run.fromName : "?"}}) : t("transitNext", {{"stop", !run.toName.empty() ? run.toName : "?"}, {"min", mins}});
    pop.append(el("small", "", t(run.kind == "tram" ? "transitTram" : "transitBus") + " " + run.ref + " · " + line));
    // the two things to do with it, side by side: its line's whole way on the map, the camera on it
    const El row = el("div", "bm-row bm-ctx-row");
    const bool isShown = line_ && line_->kind == run.kind && line_->index == run.line;
    const El lineBtn = el("button", isShown ? "btn" : "btn ghost", t(isShown ? "transitLineHide" : "transitLine"));
    lineBtn.type("button");
    lineBtn.setAttribute("aria-pressed", isShown ? "true" : "false");
    const TransitKind kind = run.kind;
    const int lineIndex = run.line;
    const std::string colour = v.colour;
    lineBtn.onclick([this, isShown, kind, lineIndex, colour] {
      if (isShown) line_.reset();
      else line_ = LineShown{kind, lineIndex, colour};
      render();
      host_.redraw();
    });
    const El watch = el("button", "btn", t("transitAttach"));
    watch.type("button");
    // (not while a lesson or a tour runs: then it says why)
    if (!host_.canWatch()) {
      watch.disabled(true);
      watch.title(t("transitAttachNot"));
    }
    const WatchRequest req{run.kind, run.line, run.ref, v.colour, run.dest, run.pattern, run.t0, v.trip};
    watch.onclick([this, req] {
      if (host_.canWatch()) host_.watch(req);
    });
    row.append(lineBtn, watch);
    pop.append(row);
    if (!host_.canWatch()) pop.append(el("small", "", t("transitAttachNot")));
    menuAt_.reset();
    return;
  }
  // a stop: its next departures
  const StopPlace& p = *sel.place;
  const El title = el("div", "bm-pop-title");
  title.append(el("b", "", !p.name.empty() ? p.name : t("tramStop")));
  if (!p.town.empty()) title.append(el("small", "", p.town));
  head.append(title, close);
  pop.append(head);
  if (!tt || !src) {
    pop.append(el("small", "", t("transitNoTimetable")));
    return;
  }
  const ServiceTime at = src->now();
  const std::vector<Departure> list = stopDepartures(*tt, p, at, 6);
  if (list.empty()) {
    pop.append(el("small", "", t("transitNone")));
    return;
  }
  const El ul = el("ul", "bm-deps");
  for (const Departure& d : list) {
    const El li = el("li");
    const std::string colour = colourOf(d.kind, d.line, d.ref, d.colour);
    const std::string when = d.wait < 60 ? t("transitNow") : d.wait < 3600 ? formatUnit(std::floor(d.wait / 60), Unit::Minute, host_.lang()) : clock(d.time);
    li.append(badge(d.kind, d.ref, colour), el("span", "", d.to), el("i", "", when));
    ul.append(li);
  }
  pop.append(ul);
}

El TransitLayer::badge(const TransitKind& kind, const std::string& ref, const std::string& colour) {
  const El b = el("span", "bm-line " + kind, !ref.empty() ? ref : "?");
  b.style("background", colour);
  b.style("color", textOn(colour));
  return b;
}

}  // namespace fs
