// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/stage.ts @ v1.7.7
#include "ui/stage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <numbers>

#include "core/js.h"
#include "platform/frame.h"
#include "platform/plat.h"

namespace fs {

using dom::El;
using dom::el;

// ===================================================================== what the menu decides

const KindTable& vehicleInfo() {
  static const KindTable* t = [] {
    auto* out = new KindTable();
    for (const VehicleDef& d : VEHICLES.all()) out->push_back({d.kind, KindInfo{.kind = d.kind, .model = d.model, .licence = d.licence, .trailer = d.trailer != nullptr}});
    return out;
  }();
  return *t;
}

namespace {

const KindInfo* infoOf(const KindTable& defs, const VehicleKind& k) {
  for (const auto& [key, info] : defs)
    if (key == k) return &info;
  return nullptr;
}

/** `/crane/i.test(text)` */
bool hasCrane(const std::string& text) {
  std::string low = text;
  std::transform(low.begin(), low.end(), low.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return low.find("crane") != std::string::npos;
}

int wrap(int i, int n) { return ((i % n) + n) % n; }

bool isAlnum(char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'); }

/** the length in bytes of the white space (`\s`: ASCII, the no-break space, the ideographic space) that ends `s` before `end` */
size_t spaceBefore(const std::string& s, size_t end) {
  size_t n = 0;
  while (end > n) {
    const unsigned char c = static_cast<unsigned char>(s[end - n - 1]);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') n += 1;
    else if (end - n >= 2 && s.compare(end - n - 2, 2, "\xC2\xA0") == 0) n += 2;
    else if (end - n >= 3 && s.compare(end - n - 3, 3, "\xE3\x80\x80") == 0) n += 3;
    else break;
  }
  return n;
}

}  // namespace

std::string groupOf(const VehicleKind& kind, const std::string& model) {
  if (model == "car") return "car";
  if (model == "motorcycle") return "moto";
  if (model == "truck" || model == "truckLight" || model == "truckHeavy" || model == "semi") return "truck";
  if (model == "bus" || model == "minibus") return "bus";
  if (model == "tractor") return "tractor";
  if (model == "tram") return "tram";
  if (model == "plane") return "plane";
  if (model == "walker") return "foot";
  return hasCrane(kind + " " + model) ? "crane" : (!model.empty() ? model : kind);
}

std::vector<StageGroup> stageGroups(const std::vector<VehicleKind>& kinds, const KindTable& defs) {
  std::vector<StageGroup> groups;
  for (const VehicleKind& k : kinds) {
    const KindInfo* d = infoOf(defs, k);
    if (!d) continue;
    const std::string id = groupOf(d->kind, d->model);
    auto g = std::find_if(groups.begin(), groups.end(), [&id](const StageGroup& x) { return x.id == id; });
    if (g != groups.end()) g->kinds.push_back(k);
    else groups.push_back(StageGroup{.id = id, .kinds = {k}});
  }
  return groups;
}

VehicleKind cycleVehicle(const std::vector<StageGroup>& groups, const VehicleKind& current, int step) {
  std::vector<VehicleKind> order;
  for (const StageGroup& g : groups) order.insert(order.end(), g.kinds.begin(), g.kinds.end());
  if (order.empty()) return current;
  const auto at = std::find(order.begin(), order.end(), current);
  const int i = at == order.end() ? 0 : static_cast<int>(at - order.begin());
  return order[static_cast<size_t>(wrap(i + step, static_cast<int>(order.size())))];
}

VehicleKind cycleGroup(const std::vector<StageGroup>& groups, const VehicleKind& current, int step, const std::map<std::string, VehicleKind>& last) {
  if (groups.empty()) return current;
  int at = -1;
  for (size_t i = 0; i < groups.size(); i++)
    if (std::find(groups[i].kinds.begin(), groups[i].kinds.end(), current) != groups[i].kinds.end()) {
      at = static_cast<int>(i);
      break;
    }
  const StageGroup& g = groups[static_cast<size_t>(wrap(std::max(0, at) + step, static_cast<int>(groups.size())))];
  const auto was = last.find(g.id);
  return was != last.end() && std::find(g.kinds.begin(), g.kinds.end(), was->second) != g.kinds.end() ? was->second : g.kinds[0];
}

std::optional<VehicleKind> trailerPartner(const VehicleKind& kind, const KindTable& defs) {
  const KindInfo* d = infoOf(defs, kind);
  if (!d || d->licence.empty()) return std::nullopt;
  const std::string want = d->trailer ? (d->licence.ends_with("E") ? d->licence.substr(0, d->licence.size() - 1) : "") : d->licence + "E";
  if (want.empty()) return std::nullopt;
  for (const auto& [k, o] : defs)
    if (k != kind && o.licence == want && o.trailer != d->trailer && groupOf(o.kind, o.model) == groupOf(d->kind, d->model)) return k;
  return std::nullopt;
}

std::string shortName(const std::string& name, const std::string& licence) {
  if (licence.empty()) return name;
  // /^(.*?)\s*[(（]([^()（）]*)[)）]\s*$/ – the brackets at the end, and what is before them
  const size_t end = name.size() - spaceBefore(name, name.size());
  size_t closeLen = 0;
  if (end >= 1 && name[end - 1] == ')') closeLen = 1;
  else if (end >= 3 && name.compare(end - 3, 3, "）") == 0) closeLen = 3;
  if (!closeLen) return name;
  const size_t innerEnd = end - closeLen;
  // (the opening bracket: the last one before; none of the four between)
  size_t open = std::string::npos;
  size_t openLen = 0;
  for (size_t i = innerEnd; i > 0;) {
    if (name[i - 1] == '(') {
      open = i - 1;
      openLen = 1;
      break;
    }
    if (name[i - 1] == ')') return name;
    if (i >= 3 && name.compare(i - 3, 3, "（") == 0) {
      open = i - 3;
      openLen = 3;
      break;
    }
    if (i >= 3 && name.compare(i - 3, 3, "）") == 0) return name;
    i--;
  }
  if (open == std::string::npos) return name;
  const std::string inner = name.substr(open + openLen, innerEnd - open - openLen);
  std::string head = name.substr(0, open - spaceBefore(name, open));
  if (head.empty()) return name;
  // (the class as a word of its own in the brackets)
  bool found = false;
  for (size_t at = inner.find(licence); at != std::string::npos && !found; at = inner.find(licence, at + 1)) {
    const bool before = at == 0 || !isAlnum(inner[at - 1]);
    const bool after = at + licence.size() >= inner.size() || !isAlnum(inner[at + licence.size()]);
    found = before && after;
  }
  if (!found) return name;
  // (.replace(/[\s,،、]+$/, ''))
  for (bool more = true; more && !head.empty();) {
    more = false;
    const size_t sp = spaceBefore(head, head.size());
    if (sp) {
      head.resize(head.size() - sp);
      more = true;
    } else if (head.back() == ',') {
      head.pop_back();
      more = true;
    } else if (head.size() >= 2 && head.compare(head.size() - 2, 2, "،") == 0) {
      head.resize(head.size() - 2);
      more = true;
    } else if (head.size() >= 3 && head.compare(head.size() - 3, 3, "、") == 0) {
      head.resize(head.size() - 3);
      more = true;
    }
  }
  return head;
}

std::vector<StageMode> stageModes(const VehicleKind& kind, bool hasYard, const std::vector<LessonDef>& lessons) {
  // (a vehicle of the road: not the tram on its tracks, not the aeroplane, not on foot)
  const bool road = std::find(ROAD_VEHICLE_KINDS.begin(), ROAD_VEHICLE_KINDS.end(), kind) != ROAD_VEHICLE_KINDS.end();
  std::vector<StageMode> out{{.id = "free", .group = "drive"}};
  // (the drives the vehicle does by itself and the tours are on the roads – the tram's on its tracks)
  // (the aeroplane's is a demonstration flight: its autopilot takes off, flies round the field and lands)
  if (kind == "tram" || kind == "plane" || road) out.push_back({.id = "demo", .group = "drive"});
  if (road) out.push_back({.id = "tour", .group = "drive"});
  // the test place: every model of the game on the practice yard
  if (kind != "tram" && kind != "plane" && hasYard) out.push_back({.id = "show", .group = "drive"});
  const auto rank = [](const std::string& k) { return k == "yard" ? 0 : k == "exam" ? 2 : 1; };
  std::vector<const LessonDef*> fits;
  for (const LessonDef& l : lessons) {
    const bool forKind = l.vehicles ? std::find(l.vehicles->begin(), l.vehicles->end(), kind) != l.vehicles->end()
                         : l.vehicle ? *l.vehicle == kind
                                     : road && !(l.notFor && std::find(l.notFor->begin(), l.notFor->end(), kind) != l.notFor->end());
    if (forKind && (l.kind != "yard" || hasYard)) fits.push_back(&l);
  }
  std::stable_sort(fits.begin(), fits.end(), [&rank](const LessonDef* a, const LessonDef* b) { return rank(a->kind) < rank(b->kind); });
  for (const LessonDef* l : fits) out.push_back({.id = l->id, .group = l->kind == "exam" ? "exam" : "lesson", .lesson = l});
  return out;
}

std::string pickMode(const std::vector<std::string>& modes, const std::string& wanted) {
  if (std::find(modes.begin(), modes.end(), wanted) != modes.end()) return wanted;
  return modes.empty() ? "free" : modes[0];
}

StagePlace stagePlace(const VehicleKind& kind, const Settings::LastStart& last, const std::vector<SpawnJson>& spawns, const std::vector<TramStopChoice>* tramStops,
                      const std::vector<RunwayChoice>* runways) {
  const int spawn = std::min(last.spawn, std::max(0, static_cast<int>(spawns.size()) - 1));
  const auto missing = [spawn](bool read) {
    StagePlace p;
    p.request.spawn = spawn;
    p.state = read ? "none" : "loading";
    return p;
  };
  if (kind == "plane") {
    if (!runways || runways->empty()) return missing(runways != nullptr);
    size_t at = 0;
    for (size_t i = 0; i < runways->size(); i++)
      if (last.runway && runwayKey((*runways)[i]) == *last.runway) {
        at = i;
        break;
      }
    const RunwayChoice& r = (*runways)[at];
    StagePlace p;
    p.request.spawn = spawn;
    p.request.runway = r;
    p.preview = MenuPreview{.kind = kind, .x = r.x, .z = r.z, .yaw = r.yaw};
    p.town = r.field;
    p.name = r.end;
    p.runway = r.end;
    p.state = "ok";
    return p;
  }
  if (kind == "tram") {
    if (!tramStops || tramStops->empty()) return missing(tramStops != nullptr);
    int at = -1;
    for (size_t i = 0; i < tramStops->size(); i++)
      if (last.tramStop && tramKey((*tramStops)[i]) == *last.tramStop) {
        at = static_cast<int>(i);
        break;
      }
    if (at < 0) {
      int k = -1;
      for (size_t i = 0; i < tramStops->size(); i++)
        if ((*tramStops)[i].name == "Königsplatz") {
          k = static_cast<int>(i);
          break;
        }
      at = std::max(0, k);
    }
    const TramStopChoice& t = (*tramStops)[static_cast<size_t>(at)];
    StagePlace p;
    p.request.spawn = spawn;
    p.request.stop = t;
    p.preview = MenuPreview{.kind = kind, .x = t.x, .z = t.z, .yaw = 0, .stop = t};
    p.town = t.town;
    p.name = t.name;
    p.state = "ok";
    return p;
  }
  if (spawns.empty()) return missing(true);
  const std::optional<AddressHit>& address = last.address;
  const SpawnJson& sp = spawns[static_cast<size_t>(spawn)];
  StagePlace p;
  p.request.spawn = spawn;
  p.request.address = address;
  p.preview = address ? MenuPreview{.kind = kind, .x = address->x, .z = address->z, .yaw = sp.yaw} : MenuPreview{.kind = kind, .x = sp.x, .z = sp.z, .yaw = sp.yaw};
  p.town = address ? "" : sp.g;
  p.name = address ? address->label : sp.name;
  p.state = "ok";
  return p;
}

bool startFromSpot(const VehicleKind& kind, Settings::LastStart& last, const SpawnJson& spot, const std::vector<TramStopChoice>* tramStops,
                   const std::vector<RunwayChoice>* runways, const std::string& unnamed) {
  const auto nearest = [&spot](const auto* list) -> const auto* {
    using T = std::remove_reference_t<decltype((*list)[0])>;
    const T* best = nullptr;
    double d = std::numeric_limits<double>::infinity();
    if (list)
      for (const T& p : *list) {
        const double e = std::hypot(p.x - spot.x, p.z - spot.z);
        if (e < d) {
          d = e;
          best = &p;
        }
      }
    return best;
  };
  if (kind == "tram") {
    const TramStopChoice* t = nearest(tramStops);
    if (t) last.tramStop = tramKey(*t);
    return t != nullptr;
  }
  if (kind == "plane") {
    const RunwayChoice* r = nearest(runways);
    if (r) last.runway = runwayKey(*r);
    return r != nullptr;
  }
  StartAddress a;
  a.label = spot.name.empty() ? unnamed : spot.name;
  a.street = spot.name;
  a.x = spot.x;
  a.z = spot.z;
  a.exact = false;
  last.address = a;
  return true;
}

namespace {

/** How tall the models are (m). */
double heightOf(const std::string& model) {
  static const std::map<std::string, double>* h = new std::map<std::string, double>{{"car", 1.5},     {"motorcycle", 1.4}, {"walker", 1.8},     {"plane", 2.6}, {"tractor", 3},
                                                                                     {"minibus", 2.8}, {"bus", 3.2},        {"truckLight", 3.4}, {"truck", 3.8}, {"tram", 3.6}};
  const auto it = h->find(model);
  return it != h->end() ? it->second : 3;
}

struct Size {
  double reach = 0;
  double height = 0;
};

/**
 * How far a vehicle reaches from the middle the camera circles (m) – its
 * longer end, its half span, the trailer behind it – and how tall it is.
 * `radius`: the circle the menu of rows sees it from (game.ts placePreview).
 */
Size sizeOf(const std::string& kind, double radius) {
  const VehicleDef* d = VEHICLES.find(kind);
  // (the tram's circle is made from the length of the whole set: 0.55 of it and 7 m)
  if (!d || d->model == "tram") return {.reach = d ? std::max(6.0, (radius - 7) / 1.1) : radius * 0.4, .height = heightOf(d ? d->model : "")};
  const Outline& o = d->outline;
  return {.reach = std::max({o.front, o.rear, o.hw, d->trailer ? radius * 0.55 : 0.0}), .height = heightOf(d->model)};
}

/** what the camera framed last, and since when (the time of the game) */
struct Framed {
  std::string key;
  double since = -1e9;
};
Framed framed;

}  // namespace

StageCam frameOf(double time, const std::string& kind, double radius, double aspect, double since) {
  const double a = std::min(3.0, std::max(0.3, aspect != 0 && !std::isnan(aspect) ? aspect : 1));
  const bool upright = a < 1;
  const double fov = upright ? 62 : 46;
  const double tv = std::tan((fov * std::numbers::pi) / 360);
  const Size sz = sizeOf(kind, radius);
  const double fit = std::max({sz.reach + 1.5, sz.reach / ((upright ? 0.95 : 0.62) * tv * a), (sz.height * 0.5) / (0.4 * tv)});
  const double t = std::min(1.0, std::max(0.0, (time - since) / 1.4));
  const double dist = fit * (1 + 0.35 * std::pow(1 - t, 3)) * (1 + 0.04 * std::sin(time * 0.23));
  return {.angle = time * 0.09 + 0.6, .dist = dist, .rise = dist * 0.22 + 0.4, .aim = -dist * tv * 0.16, .fov = fov};
}

StageCam stageFrame(double time, const std::string& kind, double radius, double aspect) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.1f", radius);
  const std::string key = kind + "|" + buf;
  if (key != framed.key) {
    framed.key = key;
    framed.since = time;
  }
  return frameOf(time, kind, radius, aspect, framed.since);
}

// ===================================================================== the pictures

namespace {

/** Line drawings on a 24 × 24 grid (stroked in the colour of the text). */
const std::map<std::string, std::string>& ICONS() {
  static const auto* m = new std::map<std::string, std::string>{
      // the kinds of vehicle
      {"car", R"(<path d="M4.5 16.5H3v-3.4c0-.7.5-1.3 1.2-1.5L7 11l2.3-3.3c.3-.4.8-.7 1.3-.7h4.8c.5 0 .9.2 1.2.6L19.3 11l1.6.5c.7.2 1.1.8 1.1 1.5v3.5h-1.5M8.5 16.5h6.5M7 11h12.3M12.8 7v4"/><circle cx="6.5" cy="16.5" r="2"/><circle cx="17" cy="16.5" r="2"/>)"},
      {"moto", R"(<circle cx="5.5" cy="16" r="3.2"/><circle cx="18.5" cy="16" r="3.2"/><path d="M5.5 16l3.3-5h6.4l-2.7 5H9.2M8.8 11L7.5 9.3H5M15.2 11l1.6-4.5H19M16.8 6.5l1.7 9.5"/>)"},
      {"truck", R"(<path d="M3.5 16.5H2.5v-10h11v10M13.5 9h4.4l3.6 3.9v3.6h-1.5M8 16.5h7.5M15.5 9v4h6"/><circle cx="5.8" cy="16.5" r="2"/><circle cx="17.8" cy="16.5" r="2"/>)"},
      {"bus", R"(<path d="M4.8 17H3.5a1 1 0 0 1-1-1V6.5A1.5 1.5 0 0 1 4 5h16a1.5 1.5 0 0 1 1.5 1.5V16a1 1 0 0 1-1 1h-1.3M9.2 17h5.6M2.5 11.5h19M7.5 5v6.5M12.5 5v6.5M17.5 5v12"/><circle cx="7" cy="17" r="2"/><circle cx="17" cy="17" r="2"/>)"},
      {"tractor", R"(<circle cx="7" cy="15.5" r="4.5"/><circle cx="7" cy="15.5" r="1.3"/><circle cx="18.5" cy="17.5" r="2.5"/><path d="M4 10.5V4.5h6.5l1.2 6M11.7 10.5H19l1.5 2.6V15M11.5 17.5H16M17.5 10.5V7"/>)"},
      {"tram", R"(<path d="M7 2.5h10M12 2.5L9.5 5.5h5z"/><rect x="5.5" y="5.5" width="13" height="13.5" rx="3"/><path d="M5.5 12h13M8.5 16h.01M15.5 16h.01M8 21.5l1.8-2.5M16 21.5l-1.8-2.5"/>)"},
      {"plane", R"(<path d="M12 2.5c.8 0 1.3.8 1.3 1.8V9l7.2 4.2v2.1l-7.2-2.2v4.6l2.2 1.7v1.7L12 20.2l-3.5.9v-1.7l2.2-1.7v-4.6l-7.2 2.2v-2.1L10.7 9V4.3c0-1 .5-1.8 1.3-1.8z"/>)"},
      {"foot", R"(<circle cx="13.5" cy="4.3" r="2"/><path d="M9.5 21l2.6-6-1.6-3.5.9-4.5 3 2.6 3 1.2M11.4 7.5L8.2 9.3l-1 3.4M12.1 15l3 2.2.9 3.8"/>)"},
      {"forklift", R"(<path d="M3 17V12h12v5M6 12V6h8v6M17 3.5V19.5M17 19.5h5M17 15h4"/><circle cx="6" cy="18" r="1.8"/><circle cx="12.5" cy="18" r="1.8"/>)"},
      {"crane", R"(<path d="M2.5 17.5V13h12v4.5M14.5 13.5h3.8l2.7 2.4v1.6H2.5M4.5 13L16 4l1.5 1.3L7 13M17.2 5.2V10M16 10h2.4"/><circle cx="5" cy="18" r="1.6"/><circle cx="10" cy="18" r="1.6"/><circle cx="17.5" cy="18" r="1.6"/>)"},
      // (any other vehicle: a steering wheel)
      {"wheel", R"(<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="2.5"/><path d="M3.3 10.4l6.3 1M20.7 10.4l-6.3 1M12 14.5V21"/>)"},
      // the ways to play
      {"free", R"(<path d="M9 3L4 21M15 3l5 18M12 4v2.5M12 10v3M12 16.5V20"/>)"},
      {"demo", R"(<circle cx="11" cy="13" r="8"/><circle cx="11" cy="13" r="2.2"/><path d="M3.3 11.5l5.5.8M18.7 11.5l-5.5.8M11 15.2V21"/><path d="M19.5 1.8l.8 1.9 1.9.8-1.9.8-.8 1.9-.8-1.9-1.9-.8 1.9-.8z" fill="currentColor"/>)"},
      {"tour", R"(<circle cx="5" cy="18.5" r="2"/><circle cx="19" cy="5.5" r="2"/><path d="M7 18.5h8.5a3.5 3.5 0 0 0 0-7h-7a3.5 3.5 0 0 1 0-7H17"/>)"},
      {"show", R"(<rect x="3" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="3" y="13.5" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="13.5" width="7.5" height="7.5" rx="1.5"/>)"},
      {"lesson", R"(<path d="M2.5 9.5L12 5l9.5 4.5L12 14z"/><path d="M6.5 11.5V16c0 1.3 2.5 2.5 5.5 2.5s5.5-1.2 5.5-2.5v-4.5M21.5 9.5V15"/>)"},
      {"exam", R"(<rect x="5" y="4.5" width="14" height="17" rx="2"/><path d="M9 4.5V2.8h6v1.7M8.5 13.5l2.5 2.5 5-5"/>)"},
      {"theory", R"(<path d="M12 6.5C10 5 7 4.5 3 5v13c4-.5 7 0 9 1.5 2-1.5 5-2 9-1.5V5c-4-.5-7 0-9 1.5zM12 6.5v13"/>)"},
      // (the learning modes: a card with ticks)
      {"learn", R"(<rect x="4" y="3" width="16" height="18" rx="2"/><path d="M7.5 8.5l1.5 1.5 3-3M7.5 15l1.5 1.5 3-3M14.5 9h2.5M14.5 15.5h2.5"/>)"},
      {"takeoff", R"(<path d="M2.5 20.5h19M4 14.5l2.8 1.2 12.4-5.6c1.2-.5 1.8-1.8 1.2-2.6-.5-.8-1.8-.9-3-.4l-4 1.8-6.6-3-1.8.8 4.6 4-3.4 1.6-2.4-1.3-1.4.6 1.6 2.9"/>)"},
      {"basics", R"(<circle cx="7.5" cy="12" r="4"/><circle cx="7.5" cy="12" r="1.3"/><path d="M11.5 12h10M18 12v3.2M21 12v2.2"/>)"},
      {"couple", R"(<path d="M2.5 15.5h6.5V8H2.5M9 12.5h4"/><circle cx="14.5" cy="12.5" r="1.6"/><path d="M16.1 12.5h5.4V7M21.5 16v-3.5"/><circle cx="5.5" cy="17.5" r="1.8"/>)"},
      {"corner", R"(<path d="M18 3.5V12a4.5 4.5 0 0 1-4.5 4.5H5M8.5 13l-3.5 3.5L8.5 20"/>)"},
      {"ramp", R"(<path d="M2.5 20.5h19M15.5 20.5v-7h6M3 12h8M6.5 8.5L3 12l3.5 3.5"/>)"},
      // round the menu
      {"pin", R"(<path d="M12 21.5s-6.8-6.2-6.8-11.3a6.8 6.8 0 0 1 13.6 0c0 5.1-6.8 11.3-6.8 11.3z"/><circle cx="12" cy="10.2" r="2.4"/>)"},
      {"town", R"(<path d="M2.5 21h19M4 21V10.5l5-3V21M9 21V4.5l7 3V21M16 21v-9l4.5 2V21M12 10h1M12 13.5h1M12 17h1M6.5 13h.01M6.5 16.5h.01M18.3 16h.01M18.3 18.8h.01"/>)"},
      {"house", R"(<path d="M3 11.5L12 4l9 7.5M5.5 9.5V20.5h13V9.5M10 20.5v-6h4v6"/>)"},
      {"gear",
       R"(<path d="M18.8 9.4L21.4 10L21.4 14L18.8 14.6L18.7 15L20.1 17.2L17.2 20.1L15 18.7L14.6 18.8L14 21.4L10 21.4L9.4 18.8L9 18.7L6.8 20.1L3.9 17.2L5.3 15L5.2 14.6L2.6 14L2.6 10L5.2 9.4L5.3 9L3.9 6.8L6.8 3.9L9 5.3L9.4 5.2L10 2.6L14 2.6L14.6 5.2L15 5.3L17.2 3.9L20.1 6.8L18.7 9z"/><circle cx="12" cy="12" r="3"/>)"},
      {"map", R"(<path d="M3 6.3l5.5-2.3 7 2.8L21 4.5v13.2L15.5 20l-7-2.8L3 19.5z"/><path d="M8.5 4v13.2M15.5 6.8V20"/>)"},
      {"news", R"(<path d="M11 3l1.9 5.6L18.5 10.5l-5.6 1.9L11 18l-1.9-5.6L3.5 10.5l5.6-1.9z"/><path d="M18.5 14.5l.8 2 2 .8-2 .8-.8 2-.8-2-2-.8 2-.8z"/>)"},
      {"more", R"(<circle cx="5.5" cy="12" r="1.5" fill="currentColor"/><circle cx="12" cy="12" r="1.5" fill="currentColor"/><circle cx="18.5" cy="12" r="1.5" fill="currentColor"/>)"},
      {"globe", R"(<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c2.4 2.6 3.7 5.6 3.7 9s-1.3 6.4-3.7 9c-2.4-2.6-3.7-5.6-3.7-9S9.6 5.6 12 3z"/>)"},
      {"pad", R"(<path d="M7 8h10a4.2 4.2 0 0 1 4.1 3.4l.8 4a2.3 2.3 0 0 1-4 2l-2.4-2.4H8.5L6.1 17.4a2.3 2.3 0 0 1-4-2l.8-4A4.2 4.2 0 0 1 7 8z"/><path d="M8 10.5v3.5M6.25 12.25h3.5M15.5 11h.01M17.5 13h.01"/>)"},
      {"log", R"(<rect x="4.5" y="3" width="15" height="18" rx="2"/><path d="M8.5 8h7M8.5 12h7M8.5 16h4"/>)"},
      {"info", R"(<circle cx="12" cy="12" r="9"/><path d="M12 11v5.5M12 7.5h.01"/>)"},
      {"prev", R"(<path d="M15 4.5L7.5 12l7.5 7.5"/>)"},
      {"next", R"(<path d="M9 4.5l7.5 7.5L9 19.5"/>)"},
      {"play", R"(<path d="M8.5 5.2v13.6c0 .8.9 1.3 1.6.9l10.6-6.8c.6-.4.6-1.3 0-1.7L10.1 4.3c-.7-.4-1.6.1-1.6.9z" fill="currentColor"/>)"},
      {"close", R"(<path d="M6 6l12 12M18 6L6 18"/>)"},
      {"sliders", R"(<path d="M4 7h9.5M18.5 7H20M4 17h1.5M10.5 17H20"/><circle cx="16" cy="7" r="2.5"/><circle cx="8" cy="17" r="2.5"/>)"},
      {"gearbox",
       R"(<path d="M6 5v14M12 5v14M18 5v7H6"/><circle cx="6" cy="5" r="1.4" fill="currentColor"/><circle cx="12" cy="5" r="1.4" fill="currentColor"/><circle cx="18" cy="5" r="1.4" fill="currentColor"/><circle cx="6" cy="19" r="1.4" fill="currentColor"/><circle cx="12" cy="19" r="1.4" fill="currentColor"/>)"},
      {"power", R"(<path d="M3.5 17a8.5 8.5 0 1 1 17 0"/><path d="M12 17l4.5-5.5M6.5 13.5l1 .6M12 7.8v1.2M17.5 13.5l-1 .6"/>)"},
      {"trailer", R"(<path d="M2 15.5h3M5 8h14v7.5H5zM19 15.5h2.5"/><circle cx="12" cy="17.5" r="2"/>)"},
      {"set1", R"(<rect x="5" y="6" width="14" height="10" rx="2"/><path d="M5 11h14"/><circle cx="8.5" cy="18" r="1.3"/><circle cx="15.5" cy="18" r="1.3"/>)"},
      {"set2",
       R"(<rect x="1.5" y="6" width="9.5" height="10" rx="2"/><rect x="13" y="6" width="9.5" height="10" rx="2"/><path d="M1.5 11H11M13 11h9.5M11 13.5h2"/><circle cx="6.2" cy="18" r="1.3"/><circle cx="17.8" cy="18" r="1.3"/>)"},
      {"set3",
       R"(<rect x="1.5" y="6" width="11" height="10" rx="2"/><rect x="15" y="8" width="7.5" height="8" rx="1.5"/><path d="M1.5 11h11M12.5 13.5H15"/><circle cx="7" cy="18" r="1.3"/><circle cx="18.7" cy="18" r="1.3"/>)"},
      {"run", R"(<circle cx="15" cy="4.3" r="2"/><path d="M5 11.5l3.3-3.3 4.2.5 2.5 3 3.5.8M12.5 8.7l-2.5 5 3.5 2.5-1.5 5M10 13.7L7 15H3.5"/>)"},
      {"stage", R"(<path d="M5.5 12.5H5v-2l2-.6 2-2.4h5l2.5 2.4 2.5.6v2h-.5M8 12.5h6"/><circle cx="7" cy="12.5" r="1.4"/><circle cx="16.5" cy="12.5" r="1.4"/><ellipse cx="12" cy="17.5" rx="9.5" ry="3"/>)"},
      {"rows", R"(<rect x="3" y="4" width="18" height="4.5" rx="1.5"/><rect x="3" y="10.5" width="18" height="4.5" rx="1.5"/><rect x="3" y="17" width="18" height="3.5" rx="1.5"/>)"},
      {"list",
       R"(<path d="M9 6h12M9 12h12M9 18h12"/><circle cx="4.5" cy="6" r="1.2" fill="currentColor"/><circle cx="4.5" cy="12" r="1.2" fill="currentColor"/><circle cx="4.5" cy="18" r="1.2" fill="currentColor"/>)"},
  };
  return *m;
}

/** Traffic signs in their own colours, as the lessons are known by them. */
const std::map<std::string, std::string>& SIGNS() {
  static const auto* m = new std::map<std::string, std::string>{
      {"rbl", R"(<path d="M12 2.2l10.3 18.3H1.7z" fill="#d0141e"/><path d="M12 6.6l6.6 11.6H5.4z" fill="#fff"/><path d="M9.5 16.5l5-5.4M14.5 16.5l-5-5.4" stroke="#111" stroke-width="1.7"/>)"},
      {"priority", R"(<path d="M12 1.5L22.5 12 12 22.5 1.5 12z" fill="#fff" stroke="#333" stroke-width=".6"/><path d="M12 5.2L18.8 12 12 18.8 5.2 12z" fill="#f6c817"/>)"},
      {"signals",
       R"(<rect x="7" y="1.5" width="10" height="21" rx="3" fill="#20262d" stroke="#56606b" stroke-width=".8"/><circle cx="12" cy="6.3" r="2.3" fill="#e53935"/><circle cx="12" cy="12" r="2.3" fill="#ffb300"/><circle cx="12" cy="17.7" r="2.3" fill="#43a047"/>)"},
      {"roundabout",
       R"(<circle cx="12" cy="12" r="10.5" fill="#1b5fa8" stroke="#fff" stroke-width="1"/><path d="M12 5.6a6.4 6.4 0 1 1-5.8 3.7" fill="none" stroke="#fff" stroke-width="2"/><path d="M4.2 8.2l3.9-.2-.6 3.7z" fill="#fff"/>)"},
      {"rural",
       R"(<rect x="1.5" y="5" width="21" height="14" rx="1.5" fill="#f6c817" stroke="#111" stroke-width=".8"/><path d="M5 9h8M5 12.5h6M5 16h9" stroke="#111" stroke-width="1.2"/><path d="M3 18L21 6" stroke="#d0141e" stroke-width="2"/>)"},
      {"motorway",
       R"(<rect x="1.5" y="1.5" width="21" height="21" rx="3" fill="#1b5fa8" stroke="#fff" stroke-width="1"/><path d="M4.5 12.5h15" stroke="#fff" stroke-width="1.6"/><path d="M8.3 21l1.5-6.8M15.7 21l-1.5-6.8M8.8 10.7L10 4M15.2 10.7L14 4" stroke="#fff" stroke-width="1.6"/>)"},
      // (the stop of trams and buses: sign 224)
      {"stop", R"(<circle cx="12" cy="12" r="10.5" fill="#f6c817" stroke="#0a7d3b" stroke-width="2"/><path d="M8.3 6.8v10.4M15.7 6.8v10.4M8.3 12h7.4" stroke="#0a7d3b" stroke-width="2.4"/>)"},
  };
  return *m;
}

const char* const LINE = R"(viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" focusable="false")";

/** A picture by its name: a sign in its colours, or a line drawing (an unknown name: the steering wheel). */
std::string icon(const std::string& name) {
  const auto sign = SIGNS().find(name);
  if (sign != SIGNS().end()) return R"(<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">)" + sign->second + "</svg>";
  const auto line = ICONS().find(name);
  return std::string("<svg ") + LINE + ">" + (line != ICONS().end() ? line->second : ICONS().at("wheel")) + "</svg>";
}

/** a value of a table, '' where there is none */
std::string at(const std::map<std::string, std::string>& m, const std::string& k) {
  const auto it = m.find(k);
  return it != m.end() ? it->second : "";
}

/** The picture of a kind of vehicle in the strip. */
const std::map<std::string, std::string>& GROUP_ICON() {
  static const auto* m = new std::map<std::string, std::string>{{"car", "car"},   {"moto", "moto"},   {"truck", "truck"}, {"bus", "bus"},     {"tractor", "tractor"},
                                                                {"tram", "tram"}, {"plane", "plane"}, {"foot", "foot"},   {"crane", "crane"}, {"forklift", "forklift"}};
  return *m;
}
/** The name of a kind with several vehicles (a kind with one is named by its vehicle). */
const std::map<std::string, std::string>& GROUP_NAME() {
  static const auto* m = new std::map<std::string, std::string>{{"car", "stageCars"},   {"moto", "stageMotorcycles"},  {"truck", "stageTrucks"},
                                                                {"bus", "stageBuses"}, {"tractor", "stageTractors"}, {"crane", "stageCranes"}};
  return *m;
}
/** The picture of a lesson: the sign it is about, or what is done in it. */
const std::map<std::string, std::string>& LESSON_ICON() {
  static const auto* m = new std::map<std::string, std::string>{{"basics", "basics"},     {"rbl", "rbl"},       {"priority", "priority"}, {"signals", "signals"},
                                                                {"roundabout", "roundabout"}, {"rural", "rural"}, {"motorway", "motorway"}, {"ceCouple", "couple"},
                                                                {"ceCorner", "corner"},   {"ceRamp", "ramp"},   {"tramLine", "stop"},     {"busLine", "stop"}};
  return *m;
}

// ===================================================================== the page

/** 'place' | 'mode' | 'lessons' | 'learn' | 'options' | 'more' ('': none) */
using Sheet = std::string;

struct Acts {
  /** the next (1) or previous (−1) vehicle, in the reading direction */
  std::function<void(int step)> vehicle;
  std::function<void(int step)> group;
  std::function<void()> go;
  std::function<bool()> close;
};

/**
 * What the menu keeps while it is drawn anew after each choice: the sheet
 * that is open, the way to play chosen, the vehicle last chosen of each
 * kind, the side the last vehicle came from (its name slides in from there),
 * the control that had the focus – and whether keys or a gamepad are used
 * (then the focus is given back, and it moves into a sheet that opens).
 */
struct St {
  Sheet sheet;
  std::string wanted = "free";
  std::map<std::string, VehicleKind> last;
  int came = 0;
  std::string focus;
  bool keys = false;
  El root;
  /** what the keys and the gamepad do on the menu shown now */
  std::shared_ptr<Acts> act;
  bool listening = false;
  /** the gamepad: whether it is looked at (false: not while no menu of this kind is shown), its buttons a picture ago, those held since before the menu came (their letting go does not count), the way the stick or the cross is held and when that acts again */
  bool pad = false;
  bool padFresh = false;
  std::vector<bool> padPrev;
  std::vector<bool> padHeld;
  std::string padDir;
  double padNext = 0;
  /** Enter went down on the menu itself (the drive starts when it comes up) */
  bool enter = false;
};
St& st() {
  static St* s = new St();
  return *s;
}

/** A button with a picture only: its label is for hover, a long press and screen readers. */
El iconButton(const std::string& cls, const std::string& picture, const std::string& label, const std::string& key, std::function<void()> act) {
  std::string c = "st-ico " + cls;
  while (!c.empty() && c.back() == ' ') c.pop_back();
  El b = el("button", c);
  b.type("button");
  b.innerHTML(picture);
  b.title(label);
  b.setAttribute("aria-label", label);
  b.dataset("k", key);
  b.onclick(std::move(act));
  return b;
}

/** Is the page written from right to left (then "next" lies to the left)? */
bool rtl(const El& n) { return n.computedStyle("direction") == "rtl"; }

void listen();
void pollPad(double now);
void moveFocus(const El& box, int step);

/** clearTimeout of the long press: a timeout acts only while its number is the newest */
int pressSeq = 0;

/** What a sheet's tile, chip … is made of. */
struct ModeLook {
  std::string label;
  std::string picture;
  std::string hint;
};

/** A choice among a few, in a row (a picture or a word on each). */
struct Chip {
  std::string text;
  std::string picture;
  bool on = false;
  std::string key;
  std::string label;
  std::function<void()> act;
};

/** A tile of a sheet: a picture in a circle and a name under it. */
El tile(const std::string& picture, const std::string& text, bool on, const std::string& key, std::function<void()> act, const std::string& hint = "") {
  El b = el("button", std::string("st-tile") + (on ? " on" : ""));
  b.type("button");
  El p = el("span", "st-pic");
  p.innerHTML(picture);
  b.append(p, el("span", "st-text", text));
  b.title(!hint.empty() ? hint : text);
  b.setAttribute("aria-pressed", on ? "true" : "false");
  b.dataset("k", key);
  b.onclick(std::move(act));
  return b;
}

void chips(const El& row, const std::vector<Chip>& items) {
  El seg = el("div", "st-chips");
  for (const Chip& it : items) {
    El b = el("button", std::string("st-chip") + (it.on ? " on" : ""));
    b.type("button");
    if (!it.picture.empty()) {
      El p = el("span", "st-chippic");
      p.innerHTML(it.picture);
      b.append(p);
    }
    if (!it.text.empty()) b.append(el("span", "", it.text));
    if (!it.label.empty()) {
      b.title(it.label);
      b.setAttribute("aria-label", it.label);
    }
    b.setAttribute("aria-pressed", it.on ? "true" : "false");
    b.dataset("k", it.key);
    b.onclick(it.act);
    seg.append(b);
  }
  row.append(seg);
}

/** A line of a sheet: a small picture (with its name for hover and screen readers), then what is in it. */
El line(const El& box, const std::string& picture, const std::string& text) {
  El row = el("div", "st-line");
  El p = el("span", "st-linepic");
  p.innerHTML(picture);
  p.title(text);
  p.setAttribute("role", "img");
  p.setAttribute("aria-label", text);
  row.append(p);
  box.append(row);
  return row;
}

/** A list to choose from (a town, a stop): the phone's own picker – compact, and quick with many. */
void select(const El& row, const std::string& lbl, const std::vector<std::pair<std::string, std::string>>& options, const std::string& value, const std::string& key,
            std::function<void(const std::string&)> change) {
  El sel = el("select", "st-select");
  sel.setAttribute("aria-label", lbl);
  sel.title(lbl);
  for (const auto& [v, text] : options) {
    El o = el("option", "", text);
    o.value(v);
    sel.append(o);
  }
  sel.value(value);
  sel.dataset("k", key);
  sel.onchange([sel, change] { change(sel.value()); });
  row.append(sel);
}

/** What the sheets need of the menu drawn now – kept for as long as their buttons are there. */
struct Menu {
  StageContext c;
  VehicleKind kind;
  const VehicleDef* def = nullptr;
  StagePlace place;
  std::vector<StageMode> modes;
  std::vector<StageExtraMode> extras;
  std::string mode;
  std::string groupId;
  El root;
  El sheetHost;

  std::string t(const std::string& k, const TrParams& v = {}) const { return c.t(k, v); }
  Settings& s() const { return *c.settings; }

  void choose(const VehicleKind& k, int came) const {
    if (k == s().vehicle) return;
    st().came = came;
    s().vehicle = k;
    saveSettings(s());
    c.redraw();
  }

  ModeLook modeOf(const std::string& id) const {
    for (const StageExtraMode& m : extras)
      if (m.id == id) return {m.label, !m.icon.empty() ? std::string("<svg ") + LINE + ">" + m.icon + "</svg>" : icon("theory"), m.label};
    if (id == "free") {
      const std::string hint = t(kind == "tram" ? "freeTramDesc" : kind == "pedestrian" ? "freeWalkDesc" : kind == "plane" ? "freeFlyDesc" : "freeDesc");
      return {t(kind == "plane" ? "freeFly" : "free"), icon(kind == "plane" ? "takeoff" : kind == "pedestrian" ? "foot" : "free"), hint};
    }
    if (id == "demo") return {t(kind == "plane" ? "demoFly" : "demo"), icon(kind == "plane" ? "takeoff" : "demo"), t(kind == "tram" ? "demoTramDesc" : kind == "plane" ? "demoFlyDesc" : "demoDesc")};
    if (id == "tour") return {t("tourTitle"), icon("tour"), t("tourDesc")};
    if (id == "show") return {t("showTitle"), icon("show"), t("showDesc")};
    const LessonDef* l = nullptr;
    for (const StageMode& m : modes)
      if (m.id == id) {
        l = m.lesson;
        break;
      }
    const std::string lk = l ? l->kind : "";
    std::string low = lk;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const std::string li = at(LESSON_ICON(), id);
    const std::string picture = icon(lk == "exam" ? "exam" : !li.empty() ? li : low.find("theory") != std::string::npos ? "theory" : "lesson");
    return {lessonTitle(id, c.lang), picture, id == "exam" ? t("examDesc") : lessonIntro(id, c.lang)};
  }

  /** A choice was made in a sheet: keep it, close the sheet (unless `stay`), draw the menu anew. */
  void chosenIn(bool stay = false) const {
    St& S = st();
    if (!stay && !S.sheet.empty()) {
      S.focus = S.sheet == "lessons" || S.sheet == "learn" ? "mode" : S.sheet;
      S.sheet = "";
    }
    saveSettings(s());
    c.redraw();
  }

  /** To another page from a sheet: when the menu comes back, the sheet is closed. */
  static void away(const std::function<void()>& page) {
    st().sheet = "";
    if (page) page();
  }
};

/** Open a sheet ('': close it) – without drawing the menu anew. */
void open(const std::shared_ptr<Menu>& m, const Sheet& k);

/** Where to start: the town (airfield), the place in it – or an address, or a place on the map. */
void fillPlace(const std::shared_ptr<Menu>& m, const El& box) {
  const StageContext& c = m->c;
  const StagePlace& place = m->place;
  const VehicleKind& kind = m->kind;
  Settings* sp = c.settings;
  if (place.state != "ok") box.append(el("p", "st-note", place.state == "loading" ? m->t("realityLoading") : "–"));
  // the places of a town: a few as buttons, many in a list
  const auto places = [m](const El& row, const std::vector<Chip>& items) {
    if (items.size() <= 12) chips(row, items);
    else {
      int at0 = 0;
      for (size_t i = 0; i < items.size(); i++)
        if (items[i].on) {
          at0 = static_cast<int>(i);
          break;
        }
      std::vector<std::pair<std::string, std::string>> opts;
      for (size_t i = 0; i < items.size(); i++) opts.emplace_back(std::to_string(i), items[i].text);
      select(row, m->t("startPoint"), opts, std::to_string(at0), "place-list", [items](const std::string& v) {
        const double n = jsNumber(v);
        if (n >= 0 && static_cast<size_t>(n) < items.size()) items[static_cast<size_t>(n)].act();
      });
    }
  };
  if (kind == "plane" && c.runways && !c.runways->empty() && place.state == "ok") {
    const std::vector<RunwayChoice> list = *c.runways;
    std::vector<std::pair<std::string, std::string>> fields;
    for (const RunwayChoice& r : list)
      if (std::none_of(fields.begin(), fields.end(), [&r](const auto& f) { return f.first == r.field; })) fields.emplace_back(r.field, r.field);
    select(line(box, icon("plane"), m->t("startPoint")), m->t("startPoint"), fields, place.town, "town", [list, sp, m](const std::string& f) {
      for (const RunwayChoice& r : list)
        if (r.field == f) {
          sp->lastStart.runway = runwayKey(r);
          break;
        }
      m->chosenIn(true);
    });
    std::vector<Chip> ends;
    for (const RunwayChoice& r : list) {
      if (r.field != place.town) continue;
      const std::string key = runwayKey(r);
      ends.push_back(Chip{.text = m->t("runwayEnd", {{"end", r.end}}), .on = place.runway && r.end == *place.runway, .key = "end-" + r.end, .act = [sp, key, m] {
                            sp->lastStart.runway = key;
                            m->chosenIn();
                          }});
    }
    places(line(box, icon("takeoff"), place.town), ends);
  } else if (kind == "tram" && c.tramStops && !c.tramStops->empty() && place.state == "ok") {
    const std::vector<TramStopChoice> list = *c.tramStops;
    std::vector<std::pair<std::string, std::string>> towns;
    for (const TramStopChoice& x : list)
      if (std::none_of(towns.begin(), towns.end(), [&x](const auto& f) { return f.first == x.town; })) towns.emplace_back(x.town, x.town);
    select(line(box, icon("town"), m->t("startPoint")), m->t("startPoint"), towns, place.town, "town", [list, sp, m](const std::string& town) {
      for (const TramStopChoice& x : list)
        if (x.town == town) {
          sp->lastStart.tramStop = tramKey(x);
          break;
        }
      m->chosenIn(true);
    });
    std::vector<Chip> stops;
    for (const TramStopChoice& x : list) {
      if (x.town != place.town) continue;
      const std::string key = tramKey(x);
      stops.push_back(Chip{.text = x.name, .on = x.name == place.name, .key = "stop-" + x.name, .act = [sp, key, m] {
                             sp->lastStart.tramStop = key;
                             m->chosenIn();
                           }});
    }
    places(line(box, icon("stop"), place.town), stops);
  } else if (kind != "plane" && kind != "tram" && !c.spawns.empty()) {
    // the start points of the map by town, in the order the map lists them
    const std::vector<SpawnJson>& spawns = c.spawns;
    std::vector<std::string> towns;
    for (const SpawnJson& q : spawns)
      if (std::find(towns.begin(), towns.end(), q.g) == towns.end()) towns.push_back(q.g);
    const int spawn = place.request.spawn;
    const Settings::LastStart& last = sp->lastStart;
    const std::string here = last.address ? "" : spawn >= 0 && static_cast<size_t>(spawn) < spawns.size() ? spawns[static_cast<size_t>(spawn)].g : "";
    std::vector<std::pair<std::string, std::string>> options;
    if (last.address) options.emplace_back("@", last.address->label);
    for (const std::string& g : towns) options.emplace_back(g, g.empty() ? "–" : g);
    select(line(box, icon("town"), m->t("startPoint")), m->t("startPoint"), options, last.address ? "@" : here, "town", [m, sp](const std::string& g) {
      if (g == "@") return;
      int first = -1;
      const std::vector<SpawnJson>& sps = m->c.spawns;
      for (size_t i = 0; i < sps.size(); i++)
        if (sps[i].g == g) {
          first = static_cast<int>(i);
          break;
        }
      sp->lastStart.spawn = std::max(0, first);
      sp->lastStart.address.reset();
      m->chosenIn(true);
    });
    if (!last.address) {
      std::vector<Chip> items;
      for (size_t i = 0; i < spawns.size(); i++) {
        if (spawns[i].g != here) continue;
        const int idx = static_cast<int>(i);
        items.push_back(Chip{.text = spawns[i].name, .on = idx == spawn, .key = "spawn-" + std::to_string(i), .act = [sp, idx, m] {
                               sp->lastStart.spawn = idx;
                               sp->lastStart.address.reset();
                               m->chosenIn();
                             }});
      }
      const std::string gi = at(GROUP_ICON(), m->groupId);
      places(line(box, icon(!gi.empty() ? gi : "pin"), !here.empty() ? here : m->t("startPoint")), items);
    }
    // … or anywhere: street and house number
    El field = c.address([sp, m](const AddressHit& hit) {
      sp->lastStart.address = hit;
      m->chosenIn();
    });
    field.addClass("st-address");
    line(box, icon("house"), m->t("startAddress")).append(field);
  }
  // … or a place on the map
  El map = el("button", "st-wide");
  map.type("button");
  map.innerHTML(icon("map"));
  map.append(el("span", "", m->t("stageOnMap")));
  map.dataset("k", "onmap");
  const std::function<void()> mapPage = c.pages.map;
  map.onclick([mapPage] { Menu::away(mapPage); });
  box.append(map);
  // … or where the player is now
  if (c.here) {
    const std::function<Task<StageContext::Here>()> here = c.here;
    El me = el("button", "st-wide");
    me.type("button");
    me.innerHTML(icon("pin"));
    El lbl = el("span", "", m->t("myLocation"));
    me.append(lbl);
    me.dataset("k", "here");
    me.onclick([me, lbl, here, sp, m] {
      [](El me, El lbl, std::function<Task<StageContext::Here>()> here, Settings* sp, std::shared_ptr<Menu> m) -> Task<void> {
        me.disabled(true);
        const StageContext::Here r = co_await here();
        me.disabled(false);
        if (r.hit) {
          sp->lastStart.address = *r.hit;
          m->chosenIn();
        } else lbl.textContent(r.error);
      }(me, lbl, here, sp, m)
                                                                                         .detach();
    });
    box.append(me);
  }
}

/** What to do: the drives of one's own, the lessons (a sheet of their own when there are several), the test – and what other parts add. */
void fillModes(const std::shared_ptr<Menu>& m, const El& box, bool lessonsOnly) {
  El grid = el("div", "st-tiles");
  const auto add = [&](const std::string& id) {
    const ModeLook look = m->modeOf(id);
    grid.append(tile(
        look.picture, look.label, id == m->mode, "mode-" + id,
        [m, id] {
          st().wanted = id;
          m->chosenIn();
        },
        look.hint));
  };
  std::vector<const StageMode*> lessons;
  for (const StageMode& x : m->modes)
    if (x.group == "lesson") lessons.push_back(&x);
  if (lessonsOnly)
    for (const StageMode* x : lessons) add(x->id);
  else {
    for (const StageMode& x : m->modes)
      if (x.group == "drive") add(x.id);
    if (lessons.size() == 1) add(lessons[0]->id);
    else if (lessons.size() > 1) {
      const bool on = std::any_of(lessons.begin(), lessons.end(), [&m](const StageMode* x) { return x->id == m->mode; });
      El b = tile(icon("lesson"), m->t("stageLessons"), on, "lessons", [m] { open(m, "lessons"); });
      if (El pic = b.querySelector(".st-pic")) pic.append(el("i", "st-count", std::to_string(lessons.size())));
      grid.append(b);
    }
    for (const StageMode& x : m->modes)
      if (x.group == "exam") add(x.id);
    for (const StageExtraMode& x : m->extras)
      if (!x.learn) add(x.id);
    // the learning modes (ui/learn.ts): a sheet of their own
    if (std::any_of(m->extras.begin(), m->extras.end(), [](const StageExtraMode& x) { return x.learn; }))
      grid.append(tile(icon("learn"), m->t("learn.title"), false, "learn", [m] { open(m, "learn"); }, m->t("learn.hint")));
  }
  box.append(grid);
}

/** The learning modes: the theory exam, the sign trainer, spotting dangers, the lesson log – each opens its page at once. */
void fillLearn(const std::shared_ptr<Menu>& m, const El& box) {
  El grid = el("div", "st-tiles");
  for (const StageExtraMode& x : m->extras)
    if (x.learn) {
      const std::function<void()> run = x.run;
      grid.append(tile(!x.icon.empty() ? std::string("<svg ") + LINE + ">" + x.icon + "</svg>" : icon("learn"), x.label, false, "mode-" + x.id, [run] { Menu::away(run); }));
    }
  box.append(grid);
}

/** The vehicle's options: its gearbox, its engine, its trailer – the make-up of the tram, how fast one runs on foot. */
void fillOptions(const std::shared_ptr<Menu>& m, const El& box) {
  const VehicleDef& def = *m->def;
  const VehicleKind& kind = m->kind;
  Settings* sp = m->c.settings;
  if (def.model == "car" || def.model == "motorcycle") {
    std::vector<Chip> items;
    for (const std::string v : {"assist", "manual", "automatic"})
      items.push_back(Chip{.text = m->t(v == "assist" ? "trAssist" : v == "manual" ? "trManual" : "trAutomatic"), .on = sp->transmission == v, .key = "tr-" + v, .act = [sp, v, m] {
                             sp->transmission = v;
                             m->chosenIn(true);
                           }});
    chips(line(box, icon("gearbox"), m->t("transmission")), items);
  }
  if (def.powers.size() > 1) {
    const double now = powerOf(def, sp->powers);
    std::vector<Chip> items;
    for (double ps : def.powers) {
      const VehicleKind dk = def.kind;
      items.push_back(Chip{.text = jsStr(ps) + " PS", .on = ps == now, .key = "ps-" + jsStr(ps), .label = jsStr(ps) + " PS (" + jsStr(jsRound(ps * 0.7355)) + " kW)", .act = [sp, dk, ps, m] {
                             sp->powers[dk] = ps;
                             m->chosenIn(true);
                           }});
    }
    chips(line(box, icon("power"), m->t("enginePower")), items);
  }
  if (const std::optional<VehicleKind> partner = trailerPartner(kind)) {
    const VehicleKind solo = def.trailer ? *partner : kind;
    const VehicleKind pair = def.trailer ? kind : *partner;
    const std::string gi = at(GROUP_ICON(), m->groupId);
    const std::string picture = icon(!gi.empty() ? gi : "wheel");
    chips(line(box, icon("trailer"), m->t("stageTrailer")),
          {Chip{.text = "", .picture = picture, .on = kind == solo, .key = "solo", .label = m->t(VEHICLES.at(solo).name), .act = [m, solo] { m->choose(solo, -1); }},
           Chip{.text = "", .picture = picture + icon("trailer"), .on = kind == pair, .key = "pair", .label = m->t(VEHICLES.at(pair).name), .act = [m, pair] { m->choose(pair, 1); }}});
  }
  if (kind == "tram") {
    std::vector<Chip> items;
    for (const std::string v : {"single", "double", "trailer"})
      items.push_back(Chip{.text = "",
                           .picture = icon(v == "single" ? "set1" : v == "double" ? "set2" : "set3"),
                           .on = sp->tramSet == v,
                           .key = "set-" + v,
                           .label = m->t(v == "single" ? "tramSetSingle" : v == "double" ? "tramSetDouble" : "tramSetTrailer"),
                           .act = [sp, v, m] {
                             sp->tramSet = v;
                             m->chosenIn(true);
                           }});
    chips(line(box, icon("tram"), m->t("tramSet")), items);
  }
  if (kind == "pedestrian") {
    std::vector<Chip> items;
    for (double v : RUN_SPEEDS)
      items.push_back(Chip{.text = jsStr(v), .on = sp->runSpeed == v, .key = "run-" + jsStr(v), .label = jsStr(v) + " km/h", .act = [sp, v, m] {
                             sp->runSpeed = v;
                             m->chosenIn(true);
                           }});
    chips(line(box, icon("run"), m->t("runSpeed")), items);
  }
}

/** The rest: the controls, the log of faults, about the game – and the other two menus. */
void fillMore(const std::shared_ptr<Menu>& m, const El& box) {
  const StageContext::Pages pg = m->c.pages;
  Settings* sp = m->c.settings;
  El grid = el("div", "st-tiles");
  grid.append(tile(icon("pad"), m->t("controls"), false, "controls", [pg] { Menu::away(pg.controls); }), tile(icon("log"), m->t("logTitle"), false, "log", [pg] { Menu::away(pg.log); }),
              tile(icon("info"), m->t("about"), false, "about", [pg] { Menu::away(pg.about); }));
  box.append(grid);
  El styles = el("div", "st-tiles");
  for (const std::array<std::string, 3>& it : std::vector<std::array<std::string, 3>>{{"stage", "stage", "menuStage"}, {"carousel", "rows", "menuCarousel"}, {"classic", "list", "menuClassic"}}) {
    const std::string v = it[0];
    styles.append(tile(icon(it[1]), m->t(it[2]), sp->menuStyle == v, "style-" + v, [sp, v, m] {
      if (sp->menuStyle == v) return;
      sp->menuStyle = v;
      m->chosenIn();
    }));
  }
  line(box, icon("stage"), m->t("menuStyle")).append(el("b", "st-linetext", m->t("menuStyle")));
  box.append(styles);
}

/** A sheet: its title, a close button, what is in it. */
std::vector<El> buildSheet(const std::shared_ptr<Menu>& m, const Sheet& k) {
  El scrim = el("div", "st-scrim");
  scrim.onclick([m] { open(m, ""); });
  El box = el("div", "st-sheet");
  box.setAttribute("role", "dialog");
  El head = el("div", "st-sheethead");
  static const std::map<std::string, std::pair<std::string, std::string>>* titles = new std::map<std::string, std::pair<std::string, std::string>>{
      {"place", {"pin", "startPoint"}},     {"mode", {"free", "stageMode"}},          {"lessons", {"lesson", "stageLessons"}},
      {"learn", {"learn", "learn.title"}}, {"options", {"sliders", "menuOptions"}}, {"more", {"more", "stageMore"}}};
  const auto& [pictureName, headingKey] = titles->at(k);
  const std::string heading = m->t(headingKey);
  box.setAttribute("aria-label", heading);
  if (k == "lessons" || k == "learn") head.append(iconButton("st-small st-back", icon("prev"), m->t("back"), "back", [m] { open(m, "mode"); }));
  else {
    El p = el("span", "st-headpic");
    p.innerHTML(icon(pictureName));
    head.append(p);
  }
  head.append(el("b", "", heading), iconButton("st-small st-close", icon("close"), m->t("realityClose"), "close", [m] { open(m, ""); }));
  box.append(head);
  if (k == "place") fillPlace(m, box);
  else if (k == "mode" || k == "lessons") fillModes(m, box, k == "lessons");
  else if (k == "learn") fillLearn(m, box);
  else if (k == "options") fillOptions(m, box);
  else fillMore(m, box);
  return {scrim, box};
}

void open(const std::shared_ptr<Menu>& m, const Sheet& k) {
  St& S = st();
  const Sheet was = S.sheet;
  S.sheet = k;
  m->sheetHost.replaceChildren();
  if (!k.empty()) m->sheetHost.append(buildSheet(m, k));
  m->root.toggleClass("sheeted", !k.empty());
  if (!S.keys) return;
  // (keys or a gamepad: into the sheet – to what is chosen in it –, and back to the button that opened it)
  if (!k.empty()) {
    El f = m->sheetHost.querySelector(".on[data-k]");
    if (!f) f = m->sheetHost.querySelector("[data-k]");
    if (f) f.focus();
  } else if (!was.empty()) {
    if (El b = m->root.querySelector("[data-k=\"" + (was == "lessons" || was == "learn" ? std::string("mode") : was) + "\"]")) b.focus();
  }
}

}  // namespace

El stageMenu(const StageContext& c) {
  St& S = st();
  Settings& s = *c.settings;
  const auto t = [&c](const std::string& k, const TrParams& v = {}) { return c.t(k, v); };
  const VehicleDef& def = VEHICLES.find(s.vehicle) ? *VEHICLES.find(s.vehicle) : VEHICLES.at("car");
  const VehicleKind kind = def.kind;
  const std::vector<StageGroup> groups = stageGroups();
  int groupIdx = 0;
  for (size_t i = 0; i < groups.size(); i++)
    if (std::find(groups[i].kinds.begin(), groups[i].kinds.end(), kind) != groups[i].kinds.end()) {
      groupIdx = static_cast<int>(i);
      break;
    }
  const StageGroup* group = groups.empty() ? nullptr : &groups[static_cast<size_t>(groupIdx)];
  if (group) S.last[group->id] = kind;
  const auto m = std::make_shared<Menu>();
  m->c = c;
  m->kind = kind;
  m->def = &def;
  m->place = stagePlace(kind, s.lastStart, c.spawns, c.tramStops, c.runways);
  m->modes = stageModes(kind, c.hasYard);
  for (const StageExtraMode& x : c.extraModes)
    if ((!x.kinds || std::find(x.kinds->begin(), x.kinds->end(), kind) != x.kinds->end()) && (!x.yard || c.hasYard)) m->extras.push_back(x);
  std::vector<std::string> ids;
  for (const StageMode& x : m->modes) ids.push_back(x.id);
  for (const StageExtraMode& x : m->extras) ids.push_back(x.id);
  m->mode = pickMode(ids, S.wanted);
  m->groupId = group ? group->id : "";
  const StagePlace& place = m->place;
  const std::string& mode = m->mode;
  const auto name = [&t](const VehicleDef& d) { return shortName(t(d.name), d.licence); };
  const auto groupName = [&](const StageGroup& g) {
    const std::string key = at(GROUP_NAME(), g.id);
    return !key.empty() && g.kinds.size() > 1 ? t(key) : name(VEHICLES.at(g.kinds[0]));
  };

  El root = el("div", std::string("st") + (S.keys ? " keys" : ""));
  S.root = root;
  m->root = root;

  // ---- the picture itself: a swipe brings the next vehicle
  El swipe = el("div", "st-swipe");
  swipe.setAttribute("aria-hidden", "true");

  const auto acts = std::make_shared<Acts>();

  // ---- at the top: the name of the game, how far the place is loaded; the language, the map, what is new, the settings, the rest
  El top = el("div", "st-top");
  El brand = el("div", "st-brand");
  brand.innerHTML("Fahrsim <span>Baunatal</span>");
  brand.append(el("small", "", c.version));
  El tools = el("div", "st-tools");
  El lang = el("label", "st-ico st-lang");
  lang.title(t("language"));
  lang.innerHTML(icon("globe"));
  std::string upper = s.lang;
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  lang.append(el("b", "", upper), c.language);
  c.language.dataset("k", "lang");
  const StageContext::Pages pages = c.pages;
  tools.append(lang, iconButton("", icon("map"), t("mapTitle"), "map", [pages] { pages.map(); }), iconButton("", icon("news"), t("news"), "news", [pages] { pages.news(); }),
               iconButton("", icon("gear"), t("settings"), "settings", [pages] { pages.settings(); }), iconButton("", icon("more"), t("stageMore"), "more", [m] { open(m, "more"); }));
  top.append(brand, tools);
  // (hud.ts setMenuLoading writes into it while the ground and the roads round the place come)
  El loading = el("small", "cm-loading st-loading");

  // ---- at the edges: the previous and the next vehicle
  El prev = iconButton("st-arrow st-prev", icon("prev"), t("stagePrev"), "prev", [acts] { acts->vehicle(-1); });
  El next = iconButton("st-arrow st-next", icon("next"), t("stageNext"), "next", [acts] { acts->vehicle(1); });

  // ---- at the foot: the kinds of vehicle, the one chosen with its classes, where, what, and the big button
  El foot = el("div", "st-foot");
  El kinds = el("div", "st-kinds");
  kinds.setAttribute("role", "group");
  kinds.setAttribute("aria-label", t("vehicle"));
  for (size_t i = 0; i < groups.size(); i++) {
    const StageGroup& g = groups[i];
    const bool on = static_cast<int>(i) == groupIdx;
    const std::string gid = g.id;
    const VehicleKind first = g.kinds[0];
    const int dir = static_cast<int>(i) > groupIdx ? 1 : -1;
    const std::string gi = at(GROUP_ICON(), g.id);
    El b = iconButton(std::string("st-kind") + (on ? " on" : ""), icon(!gi.empty() ? gi : "wheel"), groupName(g), "kind-" + g.id, [on, gid, first, dir, m] {
      if (on) return;
      const auto was = st().last.find(gid);
      m->choose(was != st().last.end() ? was->second : first, dir);
    });
    b.setAttribute("aria-pressed", on ? "true" : "false");
    kinds.append(b);
  }

  El title = el("div", std::string("st-title") + (S.came > 0 ? " in-next" : S.came < 0 ? " in-prev" : ""));
  S.came = 0;
  El label = el("b", "st-name", name(def));
  title.append(label);
  El classes = el("span", "st-classes");
  const std::vector<VehicleKind> groupKinds = group ? group->kinds : std::vector<VehicleKind>{kind};
  const auto indexIn = [&groupKinds](const VehicleKind& k) {
    const auto it = std::find(groupKinds.begin(), groupKinds.end(), k);
    return it == groupKinds.end() ? -1 : static_cast<int>(it - groupKinds.begin());
  };
  for (const VehicleKind& k : groupKinds) {
    const VehicleDef& d = VEHICLES.at(k);
    if (d.licence.empty()) continue;
    if (groupKinds.size() == 1) {
      classes.append(el("span", "st-cls on", d.licence));
      continue;
    }
    El b = el("button", std::string("st-cls") + (k == kind ? " on" : ""), d.licence);
    b.type("button");
    b.title(t(d.name));
    b.setAttribute("aria-label", t(d.name));
    b.setAttribute("aria-pressed", k == kind ? "true" : "false");
    b.dataset("k", "class-" + k);
    const int dir = indexIn(k) > indexIn(kind) ? 1 : -1;
    b.onclick([m, k, dir] { m->choose(k, dir); });
    classes.append(b);
  }
  if (classes.childElementCount() > 0) title.append(classes);
  const bool hasOptions = def.model == "car" || def.model == "motorcycle" || def.powers.size() > 1 || trailerPartner(kind).has_value() || kind == "tram" || kind == "pedestrian";
  if (hasOptions) title.append(iconButton("st-opts", icon("sliders"), t("menuOptions"), "options", [m] { open(m, "options"); }));

  // where: the place of the drive's beginning
  std::string placeText;
  if (place.state == "loading") placeText = t("realityLoading");
  else if (place.state == "none") placeText = "–";
  else {
    // ([town, end or name].filter((x, i, all) => x && (i === 0 || x !== all[0])).join(' · '))
    const std::string first = place.town;
    const std::string second = place.runway ? t("runwayEnd", {{"end", *place.runway}}) : place.name;
    placeText = first;
    if (!second.empty() && second != first) placeText = placeText.empty() ? second : placeText + " · " + second;
  }

  /** A pill: a picture in a circle and a name beside it. */
  const auto pill = [](const std::string& cls, const std::string& picture, const std::string& text, const std::string& aria, const std::string& key, std::function<void()> act) {
    El b = el("button", "st-pill " + cls);
    b.type("button");
    El i = el("span", "st-pic");
    i.innerHTML(picture);
    b.append(i, el("span", "st-text", text));
    b.title(aria + ": " + text);
    b.setAttribute("aria-label", aria + ": " + text);
    b.dataset("k", key);
    b.onclick(std::move(act));
    return b;
  };
  El placeBtn = pill("st-place", icon("pin"), placeText, t("startPoint"), "place", [m] { open(m, "place"); });

  // what: the way to play chosen
  const ModeLook chosen = m->modeOf(mode);
  El modeBtn = pill("st-mode", chosen.picture, chosen.label, t("stageMode"), "mode", [m] { open(m, "mode"); });
  modeBtn.title(t("stageMode") + ": " + chosen.label);

  // … and go
  El go = el("button", "st-go");
  go.type("button");
  go.innerHTML(icon("play"));
  go.title(t("start") + " · " + chosen.label);
  go.setAttribute("aria-label", t("start") + " · " + chosen.label);
  go.dataset("k", "go");
  const std::function<void()> start = [m] {
    for (const StageExtraMode& x : m->extras)
      if (x.id == m->mode && x.run) {
        x.run();
        return;
      }
    StartRequest req = m->place.request;
    req.mode = m->mode;
    m->c.start(req);
  };
  go.onclick(start);
  foot.append(kinds, title, placeBtn, go, modeBtn);

  // ---- the sheet over the lower part of the screen, when one is open
  El sheetHost = el("div", "st-sheethost");
  m->sheetHost = sheetHost;
  El tip = el("div", "st-tip");
  tip.hidden(true);
  root.append(swipe, top, loading, prev, next, foot, sheetHost, tip);

  // ---- what the keys, the gamepad, the swipe and the long press do
  const std::vector<StageGroup> groupsCopy = groups;
  acts->vehicle = [groupsCopy, kind, m](int step) { m->choose(cycleVehicle(groupsCopy, kind, step), step); };
  acts->group = [groupsCopy, kind, m](int step) { m->choose(cycleGroup(groupsCopy, kind, step, st().last), step); };
  acts->go = start;
  acts->close = [m] {
    const Sheet sh = st().sheet;
    if (sh.empty()) return false;
    open(m, sh == "lessons" || sh == "learn" ? "mode" : "");
    return true;
  };
  S.act = acts;
  listen();
  if (!S.pad) {
    S.padFresh = true;
    S.pad = true;
    plat::requestAnimationFrame(pollPad);
  }

  // a swipe over the picture: the next vehicle (the name follows the finger a little)
  struct Drag {
    bool on = false;
    int id = 0;
    double x = 0;
    double y = 0;
  };
  const auto drag = std::make_shared<Drag>();
  const auto settle = [label, drag] {
    label.style("transform", "");
    label.style("opacity", "");
    drag->on = false;
  };
  swipe.on("pointerdown", [swipe, drag](dom::Event& e) {
    if (e.button > 0 || drag->on) return;
    *drag = Drag{.on = true, .id = e.pointerId, .x = e.clientX, .y = e.clientY};
    swipe.setPointerCapture(e.pointerId);
  });
  swipe.on("pointermove", [label, drag](dom::Event& e) {
    if (!drag->on || e.pointerId != drag->id) return;
    const double dx = e.clientX - drag->x;
    char buf[64];
    std::snprintf(buf, sizeof buf, "translateX(%.1fpx)", dx * 0.3);
    label.style("transform", buf);
    label.style("opacity", jsStr(1 - std::min(0.6, std::abs(dx) / 360)));
  });
  swipe.on("pointerup", [drag, settle, acts, root](dom::Event& e) {
    if (!drag->on || e.pointerId != drag->id) return;
    const double dx = e.clientX - drag->x;
    const double dy = e.clientY - drag->y;
    settle();
    if (std::abs(dx) > 40 && std::abs(dx) > 1.3 * std::abs(dy)) acts->vehicle((dx < 0 ? 1 : -1) * (rtl(root) ? -1 : 1));
  });
  swipe.on("pointercancel", [settle](dom::Event&) { settle(); });
  swipe.on("lostpointercapture", [settle](dom::Event&) { settle(); });
  // (a swipe on a touchpad: sideways scrolling)
  struct Wheel {
    double sum = 0;
    double at = 0;
  };
  const auto wheel = std::make_shared<Wheel>();
  swipe.on(
      "wheel",
      [wheel, acts, root](dom::Event& e) {
        if (std::abs(e.deltaX) <= std::abs(e.deltaY)) return;
        e.preventDefault();
        const double now = plat::nowMs();
        if (now - wheel->at > 350) wheel->sum = 0;
        wheel->at = now;
        wheel->sum += e.deltaX;
        if (std::abs(wheel->sum) > 90) {
          const double sign = wheel->sum > 0 ? 1 : -1;
          const int step = static_cast<int>(sign) * (rtl(root) ? -1 : 1);
          wheel->sum = -1e6 * sign;
          acts->vehicle(step);
        }
      },
      dom::ListenOptions{.capture = false, .passive = false, .once = false});

  // a long press on a button with a picture only: its name, for a moment (and the press does not count)
  const auto swallow = std::make_shared<bool>(false);
  root.on("pointerdown", [root, tip, swallow](dom::Event& e) {
    St& S2 = st();
    if (S2.keys) {
      S2.keys = false;
      root.removeClass("keys");
    }
    *swallow = false;
    tip.hidden(true);
    const int mine = ++pressSeq;
    if (e.pointerType == "mouse") return;
    const El target = e.target();
    const El b = target ? target.closest(".st-ico, .st-pill, .st-go, .st-chip, .st-cls") : El();
    if (!b) return;
    std::string text = b.getAttribute("aria-label");
    if (text.empty()) text = b.get("title");
    if (text.empty()) return;
    const double x0 = e.clientX;
    const double y0 = e.clientY;
    const auto moveId = std::make_shared<int>(0);
    *moveId = root.on("pointermove", [x0, y0](dom::Event& mv) {
      if (std::hypot(mv.clientX - x0, mv.clientY - y0) > 12) ++pressSeq;
    });
    root.on("pointerup", [root, moveId](dom::Event&) { root.off("pointermove", *moveId); }, dom::ListenOptions{.capture = false, .passive = false, .once = true});
    plat::setTimeout(
        [mine, swallow, tip, b, text] {
          if (mine != pressSeq) return;
          *swallow = true;
          tip.textContent(text);
          tip.hidden(false);
          const dom::Rect r = b.getBoundingClientRect();
          const double w = plat::innerWidth();
          const double h = plat::innerHeight();
          const bool high = r.top < h / 2;
          tip.className(std::string("st-tip") + (high ? " below" : "") + (r.left < w * 0.25 ? " start" : r.right() > w * 0.75 ? " end" : ""));
          // (placed as a share of the screen: right whatever the zoom of the interface)
          char buf[64];
          std::snprintf(buf, sizeof buf, "%.2f%%", ((r.left + r.right()) / 2 / w) * 100);
          tip.style("--x", buf);
          std::snprintf(buf, sizeof buf, "%.2f%%", (high ? r.bottom() : r.top) / h * 100);
          tip.style("--y", buf);
          plat::setTimeout([tip] { tip.hidden(true); }, 1600);
        },
        480);
  });
  root.on("pointerup", [](dom::Event&) { ++pressSeq; });
  root.on("pointercancel", [](dom::Event&) { ++pressSeq; });
  root.on(
      "click",
      [swallow](dom::Event& e) {
        if (!*swallow) return;
        *swallow = false;
        e.preventDefault();
        e.stopPropagation();
      },
      dom::ListenOptions{.capture = true, .passive = false, .once = false});
  root.on("contextmenu", [](dom::Event& e) { e.preventDefault(); });
  // the control that has the focus: given back to it when the menu is drawn anew
  root.on("focusin", [](dom::Event& e) {
    const El target = e.target();
    const std::string k = target ? target.dataset("k") : "";
    if (!k.empty()) st().focus = k;
  });

  // the sheet that was open stays open; the chosen kind in view in its strip; the focus back where it was
  if (!S.sheet.empty()) open(m, S.sheet);
  plat::requestAnimationFrame([root, kinds, go](double) {
    if (!root.isConnected()) return;
    const El on = kinds.querySelector(".on");
    if (on && kinds.num("scrollWidth") > kinds.clientWidth() + 2) on.scrollIntoView();
    St& S2 = st();
    if (S2.keys && !S2.focus.empty()) {
      const El f = root.querySelector("[data-k=\"" + S2.focus + "\"]");
      (f ? f : go).focus();
    }
  });

  c.preview(place.preview ? &*place.preview : nullptr);
  return root;
}

// ===================================================================== keys and gamepad

namespace {

/** The keys: ← → the vehicle, ↑ ↓ the kind, Enter starts, Esc closes a sheet (one listener for every menu drawn). */
void listen() {
  St& S = st();
  if (S.listening) return;
  S.listening = true;
  dom::window().on("keydown", [](dom::Event& e) {
    St& S2 = st();
    const El root = S2.root;
    const std::shared_ptr<Acts> a = S2.act;
    if (!root || !root.isConnected() || !a || e.ctrlKey || e.metaKey || e.altKey) return;
    const El target = e.target();
    if (target && (target.matches("input") || target.matches("select") || target.matches("textarea"))) return;
    if (!S2.keys) {
      S2.keys = true;
      root.addClass("keys");
    }
    const bool inSheet = !S2.sheet.empty();
    const std::string code = e.code;
    if (code == "Escape") {
      if (a->close()) e.preventDefault();
      return;
    }
    if (code.starts_with("Arrow")) {
      e.preventDefault();
      const bool back = code == "ArrowLeft" || code == "ArrowUp";
      if (inSheet) moveFocus(root.querySelector(".st-sheet"), back ? -1 : 1);
      else if (code == "ArrowLeft" || code == "ArrowRight") a->vehicle((back ? -1 : 1) * (rtl(root) ? -1 : 1));
      else a->group(back ? -1 : 1);
      return;
    }
    // Enter on the menu itself (no button has the focus): the drive starts – when the key comes up again
    if ((code == "Enter" || code == "NumpadEnter") && !e.repeat && !inSheet && !(target && root.contains(target) && target.closest("button, label"))) {
      S2.enter = true;
      e.preventDefault();
    }
  });
  dom::window().on("keyup", [](dom::Event& e) {
    St& S2 = st();
    if ((e.code == "Enter" || e.code == "NumpadEnter") && S2.enter) {
      S2.enter = false;
      if (S2.root && S2.root.isConnected() && S2.sheet.empty() && S2.act) S2.act->go();
    }
  });
}

/** The focus to the next (1) or the previous (−1) control in `box` – round. */
void moveFocus(const El& box, int step) {
  if (!box) return;
  std::vector<El> list;
  // (shown – `offsetParent !== null` – and not disabled)
  for (const El& e : box.querySelectorAll("[data-k]"))
    if (e.offsetWidth() > 0 || e.offsetHeight() > 0)
      if (!e.disabled()) list.push_back(e);
  if (list.empty()) return;
  const El active = dom::activeElement();
  const auto it = std::find(list.begin(), list.end(), active);
  const int n = static_cast<int>(list.size());
  const int i = it == list.end() ? -1 : static_cast<int>(it - list.begin());
  list[static_cast<size_t>(i < 0 ? (step > 0 ? 0 : n - 1) : wrap(i + step, n))].focus();
}

/**
 * The gamepad, looked at once a picture while a menu of this kind is shown:
 * the stick or the cross ← → the vehicle (in a sheet: the next button),
 * ↑ ↓ from button to button, LB / RB the kind, A presses the button that has
 * the focus (or starts), B closes the sheet, Start starts.  (A, B and Start
 * act when let go: the press must not reach the drive that begins.)
 */
void pollPad(double) {
  St& S = st();
  const El root = S.root;
  const std::shared_ptr<Acts> a = S.act;
  if (!root || !root.isConnected() || !a) {
    S.pad = false;
    return;
  }
  plat::requestAnimationFrame(pollPad);
  // (the first pad there is: platform/plat.h lists the standard ones)
  const std::vector<plat::Gamepad> pads = plat::gamepads();
  if (pads.empty()) return;
  const plat::Gamepad& gp = pads[0];
  std::vector<bool> b;
  for (double v : gp.buttons) b.push_back(v > 0.5);
  const auto btn = [&b](size_t i) { return i < b.size() && b[i]; };
  const double ax0 = !gp.axes.empty() ? gp.axes[0] : 0;
  const double ax1 = gp.axes.size() > 1 ? gp.axes[1] : 0;
  const int x = ax0 < -0.6 || btn(14) ? -1 : ax0 > 0.6 || btn(15) ? 1 : 0;
  const int y = ax1 < -0.6 || btn(12) ? -1 : ax1 > 0.6 || btn(13) ? 1 : 0;
  const std::string dir = x != 0 ? "x" + std::to_string(x) : y != 0 ? "y" + std::to_string(y) : "";
  const double now = plat::nowMs();
  if (S.padFresh) {
    // (what is held as the menu comes – the handbrake of the drive before, the stick – does not act)
    S.padFresh = false;
    S.padPrev = b;
    S.padHeld = b;
    S.padDir = dir;
    S.padNext = now + 420;
    return;
  }
  const std::vector<bool> prev = S.padPrev;
  const std::vector<bool> held = S.padHeld;
  S.padPrev = b;
  for (size_t i = 0; i < S.padHeld.size(); i++) S.padHeld[i] = S.padHeld[i] && btn(i);
  const auto was = [](const std::vector<bool>& v, size_t i) { return i < v.size() && v[i]; };
  const auto down = [&](size_t i) { return btn(i) && !was(prev, i); };
  const auto up = [&](size_t i) { return !btn(i) && was(prev, i) && !was(held, i); };
  if (std::any_of(b.begin(), b.end(), [](bool p) { return p; }) || !dir.empty()) {
    if (!S.keys) {
      S.keys = true;
      root.addClass("keys");
    }
  }
  // (held: again after a moment, then quicker.  One thing a picture: most of them draw the menu anew)
  const bool fresh = dir != S.padDir;
  S.padDir = dir;
  if (!dir.empty() && (fresh || now >= S.padNext)) {
    S.padNext = now + (fresh ? 420 : 170);
    const El sheet = root.querySelector(".st-sheet");
    if (sheet) moveFocus(sheet, x != 0 ? x : y);
    else if (x != 0) a->vehicle(x * (rtl(root) ? -1 : 1));
    else moveFocus(root, y);
  } else if (down(4) || down(5)) a->group(down(4) ? -1 : 1);
  else if (up(1)) {
    if (!a->close()) {
      if (const El f = dom::activeElement()) f.blur();
    }
  } else if (up(0)) {
    const El f = dom::activeElement();
    if (f && f.matches("select") && root.contains(f)) f.call("showPicker");
    else if (f && root.contains(f) && !f.dataset("k").empty()) f.click();
    else if (S.sheet.empty()) a->go();
  } else if (up(9) && S.sheet.empty()) a->go();
}

}  // namespace

}  // namespace fs
