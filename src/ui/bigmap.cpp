// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/bigmap.ts @ v1.7.7
#include "ui/bigmap.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/date.h"
#include "core/js.h"
#include "game/history.h"
#include "i18n/unicode.h"
#include "platform/frame.h"
#include "platform/intl.h"
#include "platform/json.h"
#include "platform/plat.h"
#include "geo/utm.h"
#include "ui/geolocate.h"
#include "ui/mapwindow.h"
#include "ui/lessonroutes.h"

namespace fs {

using dom::El;
using dom::el;

namespace {

/** px per metre: the whole map on a phone … one house filling the screen */
const double MIN_SCALE = 0.012;
const double MAX_SCALE = 4;

/** How roads are drawn, by priority tier (see src/world/types.ts): colour, least width in px, from which zoom on. */
struct RoadClass {
  double min;
  double max;
  const char* color;
  double px;
  double from;
};
const RoadClass CLASSES[4] = {
    {0, 1.5, "#55606b", 0.8, 0.11},
    {2, 2.9, "#aab4bd", 1, 0.045},
    {3, 5.5, "#f1f3f5", 1.6, 0},
    {6, 9, "#f5b942", 2.4, 0},
};

// (2.0: how prominent a place is: MapOverview::placeRank – ui/mapoverview.h)

/** `new Date(iso).getTime()` of a time as toISOString writes it ("2026-11-20T16:30:00.000Z") – NaN: it is none */
double dateOfIso(const std::string& iso) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
  const int n = std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d.%d", &y, &mo, &d, &h, &mi, &s, &ms);
  if (n < 3) return JS_NAN;
  return utcMs(y, mo - 1, d, h, mi, s, ms);
}

/** `date.toLocaleDateString(locale)` ("Invalid Date" for a time that is none, as the browser says) */
std::string dateText(double ms, const std::string& locale) {
  try {
    return plat::formatDate(ms, locale);
  } catch (const plat::IntlError&) {
    return "Invalid Date";
  }
}

/** `date.toLocaleTimeString(locale, { hour: '2-digit', minute: '2-digit' })` */
std::string timeText(double ms, const std::string& locale) {
  try {
    return plat::formatDate(ms, locale, {.hour = "2-digit", .minute = "2-digit"});
  } catch (const plat::IntlError&) {
    return "Invalid Date";
  }
}

const std::vector<MapService> NO_SERVICES;
const std::vector<MapEmergency> NO_EMERGENCIES;
const std::vector<StopSource> NO_STOPS;

// 2.0: the globe of the map's bar ("All regions")
const char* const GLOBE_SVG =
    "<svg viewBox=\"0 0 24 24\" width=\"22\" height=\"22\" aria-hidden=\"true\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.8\" stroke-linecap=\"round\">"
    "<circle cx=\"12\" cy=\"12\" r=\"9.2\"/><ellipse cx=\"12\" cy=\"12\" rx=\"4\" ry=\"9.2\"/><path d=\"M2.8 12h18.4M4.6 7h14.8M4.6 17h14.8\"/></svg>";

}  // namespace

void BigMap::RoadPath::stroke(const Ctx2D& g) const {
  g.beginPath();
  for (size_t i = 0; i < starts.size(); i++) {
    const size_t a = starts[i];
    const size_t b = i + 1 < starts.size() ? starts[i + 1] : xy.size();
    g.polyline(xy.data() + a, (b - a) / 2);
  }
  g.stroke();
}

BigMap::BigMap(BigMapHost host_, std::shared_ptr<WorldLayer> world) : host(std::move(host_)), worldLayer(std::move(world)) {
  photos = std::make_unique<AerialCache>([this] { redraw(); });
  if (TransitLayer::create) {
    TransitMapHost th;
    th.lang = [this] { return host.lang(); };
    th.source = [this]() -> TransitSource* { return host.transit ? host.transit() : nullptr; };
    th.canWatch = [this] { return static_cast<bool>(host.watch) && (host.canTeleport() || (host.menu && host.menu())); };
    th.watch = [this](const WatchRequest& v) {
      if (host.watch) host.watch(v);
    };
    th.redraw = [this] { redraw(); };
    transit = TransitLayer::create(std::move(th));
  }
  PathDrawHost dh;
  dh.lang = [this] { return host.lang(); };
  dh.roads = [this]() -> const RoadSource* { return host.roads ? host.roads() : nullptr; };
  dh.toWorld = [this](double px, double py) -> std::array<double, 2> {
    const dom::Rect r = canvas.el().getBoundingClientRect();
    return {cx + (px - r.left - r.width / 2) / scale, cz + (py - r.top - r.height / 2) / scale};
  };
  dh.pan = [this](double dx, double dy) {
    cx -= dx / scale;
    cz -= dy / scale;
    redraw();
  };
  dh.zoom = [this](double f, double px, double py) { zoom(f, px, py); };
  dh.redraw = [this] { redraw(); };
  // (while drawing, the map is free: the panel for planning and the small windows step aside)
  dh.mode = [this](bool on) {
    planBox.hidden(on);
    spot.reset();
    spotBar.hidden(true);
    if (transit) transit->closePopup();
    if (!on) fillPlan();
  };
  dh.drive = [this](const TourPreview& way, bool self) { host.startTrip({}, self, &way); };
  drawer = std::make_unique<PathDrawer>(std::move(dh));
}

BigMap::~BigMap() {
  *alive = false;
  close();
}

std::string BigMap::t(const std::string& key, const TrParams& params) const { return tr(key, host.lang(), params); }

BigMap::Pointer* BigMap::pointer(int id) {
  for (auto& p : pointers)
    if (p.first == id) return &p.second;
  return nullptr;
}

/** (the search field of the map: `load` of its address picker) */
Task<AddressSearchOf<PlaceHit>> BigMap::loadSearch() {
  const std::shared_ptr<bool> guard = alive;
  AddressIndex* idx = co_await host.loadAddresses();
  if (!*guard) co_return nullptr;
  // 2.0: the names of the roads loaded now (those that come later are noted as they come: noteStreets)
  if (streetsSeen != host.world->edges.size()) {
    std::vector<Edge*> all;
    for (size_t i = 0; i < host.world->edges.size(); i++)
      if (Edge* e = host.world->edges[static_cast<int64_t>(i)]) all.push_back(e);
    noteStreets(all);
    streetsSeen = host.world->edges.size();
  }
  co_return [this, guard, idx](const std::string& q, size_t n, const std::optional<AddressIndex::Near>& near) -> std::vector<PlaceHit> {
    if (!*guard) return {};
    // (the favourite places first, among the towns)
    std::vector<TownSource> towns;
    for (const FavouriteRecord& f : loadFavourites()) towns.push_back({f.name, "city", f.x, f.z});
    for (const PlaceJson& p : host.world->json.places) towns.push_back({p.name, p.kind, p.x, p.z});
    const std::vector<StopSource>* stopList = host.stops ? host.stops() : nullptr;
    // (2.0: and the streets of the pieces loaded so far – where the map has no address list, all that is known of its streets)
    const AddressSearch addresses = [this, idx](const std::string& qq, size_t nn, const PlaceNear& nr) {
      std::vector<AddressHit> out = idx ? idx->search(qq, nn, nr) : std::vector<AddressHit>();
      for (AddressHit& h : streets.search(qq, nn, nr)) {
        if (out.size() >= nn) break;
        bool known = false;
        for (const AddressHit& o : out)
          if (normaliseAddress(o.street) == normaliseAddress(h.street)) {
            known = true;
            break;
          }
        if (!known) out.push_back(std::move(h));
      }
      return out;
    };
    std::vector<PlaceHit> hits = searchPlaces(q, towns, stopList ? *stopList : NO_STOPS, addresses, n, near, lineSources());
    // 2.0: the regions of the world, after the places of the loaded map (which make room for them)
    if (worldOn()) {
      const std::string word = t("worldRegion");
      const std::vector<PlaceHit> regions =
          searchRegions(q, regionSources(), 3, [&word](const RegionSource& r) { return (r.country.empty() ? r.name : r.name + ", " + r.country) + " · " + word; });
      if (!regions.empty()) {
        const size_t room = n > regions.size() ? n - regions.size() : 0;
        if (hits.size() > room) hits.resize(room);
        hits.insert(hits.end(), regions.begin(), regions.end());
      }
    }
    return hits;
  };
}

/** (the address field of the panel for planning: `load` of its address picker) */
Task<AddressSearchOf<AddressHit>> BigMap::loadPlanSearch() {
  AddressIndex* idx = co_await host.loadAddresses();
  if (!idx) co_return nullptr;
  co_return [idx](const std::string& q, size_t n, const std::optional<AddressIndex::Near>& near) { return idx->search(q, n, near); };
}

void BigMap::open(const dom::El& parent, bool closeUp) {
  if (node) return;
  const BigMapHost::Car car = host.car();
  cx = car.x;
  cz = car.z;
  scale = closeUp ? 1.6 : 0.22;
  spot.reset();

  El o = el("div", "overlay bigmap");
  o.toggleClass("bm-light", prefs.style == "light");   // 2.0
  canvas = Canvas(el("canvas"));
  canvas.el().tabIndex(0);
  El top = el("div", "bm-top");
  El title = el("div", "bm-title");
  title.append(el("b", "", t("mapTitle")), el("small", "", t(host.canTeleport() || (host.menu && host.menu()) ? "mapHint" : "mapHintLesson")));
  El tools = el("div", "bm-tools");
  const auto button = [](const std::string& label, const std::string& title2, std::function<void()> fn, const std::string& cls = "") {
    El b = el("button", trim("btn ghost " + cls), label);
    b.type("button");
    b.title(title2);
    b.setAttribute("aria-label", title2);
    b.onclick(std::move(fn));
    return b;
  };
  tools.append(
      button("◎", t("mapCentre"),
             [this] {
               const BigMapHost::Car c = host.car();
               cx = c.x;
               cz = c.z;
               if (scale < 0.12) scale = 0.22;
               redraw();
             }),
      button("+", "+", [this] { zoom(1.6); }), button("−", "−", [this] { zoom(1 / 1.6); }), button("✕", t("realityClose"), [this] { host.close(); }, "bm-close"));
  // a place to look for – a town, a stop, a street, an address – and where the player is: the map goes there
  AddressPickerOptions<PlaceHit> so;
  so.placeholder = t("mapSearch");
  so.loading = t("realityLoading");
  so.none = t("addressNone");
  so.load = [this] { return loadSearch(); };
  so.near = [this]() -> std::optional<AddressIndex::Near> { return AddressIndex::Near{cx, cz}; };
  // 2.0: every town of the map before anything is typed; the online place search once typing pauses (unless switched off)
  so.browse = [this] { return browsePlaces(); };
  // (asked a moment later: the map may be gone by then)
  so.online = [this, guard = alive](std::string q) { return *guard ? searchOnline(std::move(q)) : noPlaces(); };
  so.onlineOn = [this] { return prefs.online; };
  so.onlineNote = t("map2.onlineCredit");
  so.searching = t("map2.searching");
  so.dim = [](const PlaceHit& h) { return h.what == "far"; };
  so.pick = [this](const PlaceHit& hit) {
    searchInput.value("");
    searchInput.blur();
    // 2.0: a place the online search found in another region: that region chosen, the map at the place (its schema comes)
    if (hit.what == "regionPlace") {
      showRegion(hit.region);
      cx = hit.x;
      cz = hit.z;
      scale = std::max(scale, 0.02);
      redraw();
      return;
    }
    // 2.0: … or where the game has no map: the world there
    if (hit.what == "far") {
      cx = hit.x;
      cz = hit.z;
      if (worldOn()) scale = std::min(scale, WorldLayer::WORLD_SCALE * 0.5);
      redraw();
      return;
    }
    // 2.0: a region: shown on the world, and selected
    if (hit.what == "region") {
      showRegion(hit.region);
      return;
    }
    // (a line: show its whole way and go to where it runs)
    if (hit.what == "line" && hit.line) {
      const LineSource& l = *hit.line;
      if (transit) transit->showLine(l.kind, l.index, "#e3000f");
      goTo(l.x, l.z, 0.5, hit.label);
      return;
    }
    goTo(hit.x, hit.z, hit.exact || !hit.street.empty() ? 1.2 : 0.3, hit.label);
  };
  const AddressPicker search = addressPicker<PlaceHit>(std::move(so));
  searchInput = search.input;
  search.node.addClass("bm-search");
  El me = button("⌖", t("myLocation"), nullptr);
  me.onclick([this, me] { locateMe(me).detach(); });
  tools.prepend(me);
  // 2.0: every region there is – the globe goes to them all, and back (hidden until the world is there)
  worldBtn = button("", t("worldAll"), [this] { showAllRegions(); }, "bm-world");
  worldBtn.innerHTML(GLOBE_SVG);
  worldBtn.setAttribute("aria-pressed", "false");
  worldBtn.hidden(!worldOn());
  me.after(worldBtn);
  // 2.0: the light map or the dark one
  styleBtn = button("◐", t("map2.style"), [this] { setStyle(prefs.style == "light" ? "dark" : "light"); }, "bm-style");
  styleBtn.setAttribute("aria-pressed", prefs.style == "light" ? "true" : "false");
  worldBtn.after(styleBtn);
  top.append(title, search.node, tools);

  El bottom = el("div", "bm-bottom");
  El pathToggle = el("label", "bm-check");
  El box = el("input");
  box.type("checkbox");
  box.checked(showPath);
  box.onchange([this, box] {
    showPath = box.checked();
    redraw();
  });
  El pathInfo = el("span");
  const std::function<void()> describe = [this, pathInfo] {
    const double km = host.path->length() / 1000;
    pathInfo.textContent(km >= 0.05 ? t("mapPath") + " (" + formatUnit(km, Unit::Kilometer, host.lang(), 1) + ")" : t("mapPath"));
  };
  describe();
  pathToggle.append(box, pathInfo);
  El clear = button(t("mapClearPath"), t("mapClearPath"), [this, describe] {
    host.path->clear();
    describe();
    redraw();
  });
  // the scale, and whose photographs these are
  El scaleBox = el("div", "bm-scale");
  scaleBar = el("i");
  scaleText = el("span");
  credit = el("small");
  scaleBox.append(scaleText, scaleBar, credit);
  // aerial photographs: ticking the box is the player's word that they may be fetched
  // (2.0: the box is in the panel of layers – aerialSwitch(), ui/bigmap_aerial.cpp)
  bottom.append(scaleBox, pathToggle, clear);
  if (host.transit && transit) bottom.append(transit->toggle());
  // which layers are shown: a small panel of switches
  El layersBox = el("div", "bm-layers");
  El layersBtn = button(t("mapLayers"), t("mapLayers"), [layersBox] { layersBox.hidden(!layersBox.hidden()); });
  layersBox.hidden(true);
  for (const LayerId id : LAYERS) {
    El l = el("label", "bm-check");
    El box2 = el("input");
    box2.type("checkbox");
    box2.checked(layers[id]);
    box2.onchange([this, id, box2] {
      layers[id] = box2.checked();
      saveLayers(layers);
      redraw();
    });
    l.append(box2, el("span", "", t(layerKey(id))));
    layersBox.append(l);
  }
  if (photos) layersBox.append(aerialSwitch());
  // 2.0: the online place search may be switched off
  {
    El l = el("label", "bm-check");
    El box2 = el("input");
    box2.type("checkbox");
    box2.checked(prefs.online);
    box2.onchange([this, box2] {
      prefs.online = box2.checked();
      saveMapPrefs(prefs);
    });
    l.append(box2, el("span", "", t("map2.online")));
    layersBox.append(l);
  }
  bottom.append(layersBtn, layersBox);
  // a drive is planned here: places by a tap on the map or by their address, or a tour of the streets
  planBox = el("div", "bm-plan");
  planBox.hidden(true);
  if (host.canTeleport()) {
    scaleBox.after(button(
        t("planTitle"), t("planTitle"),
        [this] {
          planBox.hidden(!planBox.hidden());
          fillPlan();
        },
        "bm-demo"));
  }
  spotBar = el("div", "bm-spot");
  spotBar.hidden(true);
  // 2.0: the card of a region (its name, its country, "Open this map"): in the column, where the place panel is
  regionBar = el("div", "bm-spot bm-region");
  regionBar.hidden(true);
  chosen.reset();
  worldBack.reset();
  worldFar = false;
  // Everything on top of the map stands in one column – the title and the tools, the panel for
  // planning (it takes what room is left, and scrolls), the chosen place, the switches – so that
  // nothing lies over anything else, however small the screen is.
  El ui = el("div", "bm-ui");
  El mid = el("div", "bm-mid");
  mid.append(planBox);
  ui.append(top, mid, spotBar, bottom);
  spotBar.after(regionBar);   // 2.0
  o.append(canvas.el(), ui);
  parent.append(o);
  node = o;
  // the small window of a tram, a bus or a stop (before the switches); the bar for drawing a way (under the title)
  if (transit) transit->attach(ui, bottom);
  drawer->attach(o, canvas.el(), ui, top);
  ungrip = addGrip(planBox, t("winResize"));

  const El c = canvas.el();
  c.on("contextmenu", [this, c](dom::Event& e) {
    e.preventDefault();
    const dom::Rect r = c.getBoundingClientRect();
    if (transit) transit->closePopup();
    showSpot(cx + (e.clientX - r.left - r.width / 2) / scale, cz + (e.clientY - r.top - r.height / 2) / scale);
  });
  c.on("pointerdown", [this, c](dom::Event& e) {
    c.setPointerCapture(e.pointerId);
    if (Pointer* p = pointer(e.pointerId)) *p = {e.clientX, e.clientY};
    else pointers.emplace_back(e.pointerId, Pointer{e.clientX, e.clientY});
    if (pointers.size() == 1) moved = 0;
    pinch = 0;
  });
  c.on("pointermove", [this](dom::Event& e) {
    Pointer* p = pointer(e.pointerId);
    if (!p) return;
    const double dx = e.clientX - p->x;
    const double dy = e.clientY - p->y;
    p->x = e.clientX;
    p->y = e.clientY;
    if (pointers.size() == 1) {
      moved += std::abs(dx) + std::abs(dy);
      cx -= dx / scale;
      cz -= dy / scale;
    } else if (pointers.size() == 2) {
      // two fingers: the distance between them is the zoom, their middle stays where it is
      const Pointer a = pointers[0].second;
      const Pointer b = pointers[1].second;
      const double d = std::hypot(a.x - b.x, a.y - b.y);
      if (pinch > 0 && d > 0) zoom(d / pinch, (a.x + b.x) / 2, (a.y + b.y) / 2);
      pinch = d;
      moved += 100;
    }
    redraw();
  });
  const dom::Handler up = [this](dom::Event& e) {
    bool had = false;
    for (size_t i = 0; i < pointers.size(); i++)
      if (pointers[i].first == e.pointerId) {
        pointers.erase(pointers.begin() + static_cast<long>(i));
        had = true;
        break;
      }
    if (!had) return;
    pinch = 0;
    // a tap (not a drag): choose that place (a fingertip wobbles a little more than a mouse)
    if (pointers.empty() && moved < (e.pointerType == "mouse" ? 8 : 16) && e.type == "pointerup") choose(e.clientX, e.clientY);
  };
  c.on("pointerup", up);
  c.on("pointercancel", up);
  c.on(
      "wheel",
      [this](dom::Event& e) {
        e.preventDefault();
        zoom(std::exp(-std::max(-120.0, std::min(120.0, e.deltaY)) * 0.004), e.clientX, e.clientY);
      },
      dom::ListenOptions{.passive = false});
  onResize = dom::window().on("resize", [this] { redraw(); });
  onKey = dom::window().on("keydown", [this](dom::Event& e) { key(e); }, dom::ListenOptions{.capture = true});
  // places chosen earlier and not driven to yet: go on planning where it was left
  if (!stops.empty()) plan();
  // 2.0: the world may still be on its way: the map draws itself again when it is there
  if (worldLayer && !worldLayer->ready() && !worldAwaited) {
    worldAwaited = true;
    awaitWorld().detach();
  }
  redraw();
  c.focus();
}

Task<void> BigMap::locateMe(dom::El me) {
  const std::shared_ptr<bool> guard = alive;
  me.disabled(true);
  const WorldMetaJson& m = host.world->json.meta;
  const LocateResult r = co_await locate(MapBounds{m.minX, m.maxX, m.minZ, m.maxZ});
  if (!*guard) co_return;
  me.disabled(false);
  if (r.ok) goTo(r.x, r.z, 1.2, t("myLocation"));
  else note(t(r.why == "outside" ? "myLocationOutside" : r.why == "denied" ? "myLocationDenied" : "myLocationNone"));
}

void BigMap::plan() {
  if (!node || !host.canTeleport()) return;
  planBox.hidden(false);
  fillPlan();
}

/** (`changed` of fillPlan) */
void BigMap::planChanged() {
  // (a tour belongs to the places it was planned for)
  tour.reset();
  tourNote = "";
  fillPlan();
  redraw();
}

void BigMap::fillPlan() {
  const El box = planBox;
  box.replaceChildren();
  if (box.hidden()) return;
  const std::string lang = host.lang();
  El closeBtn = el("button", "bm-x", "✕");
  closeBtn.type("button");
  closeBtn.setAttribute("aria-label", t("realityClose"));
  closeBtn.onclick([box] { box.hidden(true); });
  El head = el("div", "bm-head");
  head.append(el("b", "", t("planTitle")), closeBtn);
  foldButton(box, head, [this](bool folded) { return t(folded ? "winUnfold" : "winFold"); });
  box.append(head, el("small", "", t("planHint")));
  if (host.tripActive()) {
    // a drive is under way: it can be ended here (starting another one ends it, too)
    El end = el("button", "btn ghost", t("tripEnd"));
    end.type("button");
    end.onclick([this] {
      host.endTrip();
      fillPlan();
    });
    box.append(end);
  }
  AddressPickerOptions<AddressHit> po;
  po.placeholder = t("addressPlaceholder");
  po.loading = t("realityLoading");
  po.none = t("addressNone");
  po.load = [this] { return loadPlanSearch(); };
  po.near = [this]() -> std::optional<AddressIndex::Near> {
    const BigMapHost::Car c = host.car();
    return AddressIndex::Near{c.x, c.z};
  };
  po.pick = [this](const AddressHit& hit) {
    const std::optional<SpawnJson> s = host.spotOf(hit);
    if (!s) return;
    // (kept: offered again at the next visit)
    savePlace({.name = !s->name.empty() ? s->name : hit.label, .x = s->x, .z = s->z, .yaw = s->yaw});
    stops.push_back(*s);
    cx = s->x;
    cz = s->z;
    if (scale < 0.5) scale = 0.5;
    planInput.value("");
    planChanged();
  };
  const AddressPicker picker = addressPicker<AddressHit>(std::move(po));
  planInput = picker.input;
  box.append(picker.node);
  /** a place kept (a favourite, one looked for before) as a stop of the drive */
  const auto stopOf = [](const std::string& name, double x, double z, double yaw) {
    SpawnJson s;
    s.name = name;
    s.x = x;
    s.z = z;
    s.yaw = yaw;
    s.kind = "street";
    s.rough = true;
    return s;
  };
  if (stops.empty()) {
    box.append(el("small", "bm-none", t("demoNone")));
    // the favourite places: a tap adds one to the drive
    const std::vector<FavouriteRecord> favs = loadFavourites();
    if (!favs.empty()) {
      El list = el("details", "bm-tours");
      list.set("open", true);
      list.append(el("summary", "", "★ " + t("favourites")));
      for (const FavouriteRecord& f : favs) {
        El b = el("button", "bm-tour", f.name);
        b.type("button");
        b.onclick([this, f, stopOf] {
          stops.push_back(stopOf(f.name, f.x, f.z, f.yaw));
          cx = f.x;
          cz = f.z;
          if (scale < 0.5) scale = 0.5;
          planChanged();
        });
        list.append(b);
      }
      box.append(list);
    }
    // what was looked for before: the places picked, the ways driven – a tap sets them again
    const std::vector<PlaceRecord> places = loadPlaces();
    const std::vector<WayRecord> ways = loadWays();
    if (!places.empty()) {
      El recent = el("details", "bm-tours");
      recent.append(el("summary", "", t("recentPlaces")));
      for (const PlaceRecord& p : places) {
        El b = el("button", "bm-tour", p.name);
        b.type("button");
        b.onclick([this, p, stopOf] {
          stops.push_back(stopOf(p.name, p.x, p.z, p.yaw));
          cx = p.x;
          cz = p.z;
          if (scale < 0.5) scale = 0.5;
          planChanged();
        });
        recent.append(b);
      }
      box.append(recent);
    }
    if (!ways.empty()) {
      El recent = el("details", "bm-tours");
      recent.append(el("summary", "", t("recentWays")));
      const std::string loc = localeOf(lang);
      for (const WayRecord& w : ways) {
        std::vector<std::string> names;
        for (const NamedPlace& p : w.stops) names.push_back(p.name);
        El b = el("button", "bm-tour", dateText(dateOfIso(w.date), loc) + " · " + join(names, " → "));
        b.type("button");
        b.onclick([this, w, stopOf] {
          stops.clear();
          for (const NamedPlace& p : w.stops) stops.push_back(stopOf(p.name, p.x, p.z, p.yaw));
          planChanged();
        });
        recent.append(b);
      }
      box.append(recent);
    }
  } else {
    El list = el("ol");
    for (size_t i = 0; i < stops.size(); i++) {
      const SpawnJson& stop = stops[i];
      El li = el("li");
      const auto [lat, lon] = host.latLon(stop.x, stop.z);
      li.append(el("span", "", !stop.name.empty() ? stop.name : toFixed(lat, 5) + ", " + toFixed(lon, 5)));
      El del = el("button", "", "✕");
      del.type("button");
      del.setAttribute("aria-label", "✕ " + std::to_string(i + 1));
      del.onclick([this, i] {
        if (i < stops.size()) stops.erase(stops.begin() + static_cast<long>(i));
        planChanged();
      });
      li.append(del);
      list.append(li);
    }
    box.append(list);
  }

  // --- the set routes (game/routepresets.ts): driven from where the vehicle stands (not from the menu's map: no vehicle yet) ---
  if (host.startPreset && !(host.menu && host.menu()))
    box.append(routePresetSection([this](const std::string& k, const TrParams& v) { return t(k, v); }, [this](const std::string& id) {
      if (host.startPreset) host.startPreset(id);
    }));
  // --- a tour of the streets: as long as asked for, through as many different streets as possible ---
  El tourBox = el("div", "bm-tour");
  tourBox.append(el("b", "", t("tourTitle")), el("small", "", t(!stops.empty() ? "tourHintTo" : "tourHintRound")));
  El row = el("div", "bm-row");
  El km = el("input");
  km.type("number");
  km.set("min", "1");
  km.set("max", "300");
  km.set("step", "1");
  km.set("inputMode", "numeric");
  km.value(jsStr(tourKm));
  km.setAttribute("aria-label", t("tourLength"));
  El kmLabel = el("label", "bm-km");
  kmLabel.append(el("span", "", t("tourLength")), km, el("span", "", "km"));
  El planBtn = el("button", "btn ghost", t("tourPlan"));
  planBtn.type("button");
  planBtn.onclick([this, km, planBtn, tourBox] { planIt(km, planBtn, tourBox).detach(); });
  km.onchange([this, km] {
    const double v = jsNumber(km.value());
    tourKm = std::max(1.0, std::min(300.0, jsRound(v == v && v != 0 ? v : 10)));
    if (tour) planChanged();
  });
  row.append(kmLabel, planBtn);
  // what the tour is to lead along: roads left out, the kind of street looked for
  const std::shared_ptr<TourPrefs> prefs = std::make_shared<TourPrefs>(host.tourPrefs());
  El wishes = el("div", "bm-wishes");
  const auto wish = [this, prefs, wishes](bool TourPrefs::* key, const std::string& label) {
    El l = el("label", "bm-check");
    El wb = el("input");
    wb.type("checkbox");
    wb.checked((*prefs).*key);
    wb.onchange([this, prefs, key, wb] {
      (*prefs).*key = wb.checked();
      host.setTourPrefs(*prefs);
    });
    l.append(wb, el("span", "", label));
    wishes.append(l);
  };
  wish(&TourPrefs::noMotorway, t("tourNoMotorway"));
  wish(&TourPrefs::noExpress, t("tourNoExpress"));
  wish(&TourPrefs::noMain, t("tourNoMain"));
  El mostly = el("select");
  const std::pair<const char*, const char*> kinds[4] = {{"any", "tourAny"}, {"small", "tourSmall"}, {"town", "tourTown"}, {"country", "tourCountry"}};
  for (const auto& [v, k] : kinds) {
    El op = el("option");
    op.value(v);
    op.textContent(t(k));
    mostly.append(op);
  }
  mostly.value(prefs->mostly);
  mostly.onchange([this, prefs, mostly] {
    prefs->mostly = mostly.value();
    host.setTourPrefs(*prefs);
  });
  El ml = el("label", "bm-check");
  ml.append(el("span", "", t("tourMostly")), mostly);
  wishes.append(ml);
  tourBox.append(wishes, row);
  const auto f = [&lang](double m) { return formatUnit(m / 1000, Unit::Kilometer, lang, 1); };
  if (tour) {
    const TourPlan& tp = tour->plan;
    tourBox.append(el("small", "bm-result", t("tourResult", {{"km", f(tp.metres)}, {"fresh", f(tp.fresh)}, {"again", f(tp.again)}})));
  } else if (!tourNote.empty()) tourBox.append(el("small", "bm-result", tourNote));
  // the tours driven before, with their dates: each shown on the map at a tap
  const std::vector<TourRecord> driven = loadTours();
  if (!driven.empty()) {
    El list = el("details", "bm-tours");
    list.append(el("summary", "", t("toursDriven", {{"n", driven.size()}})));
    const std::string loc = localeOf(lang);
    for (size_t i = 0; i < driven.size() && i < 15; i++) {
      const TourRecord& tr_ = driven[i];
      El b = el("button", "bm-tour");
      b.type("button");
      const double d = dateOfIso(tr_.date);
      b.textContent(dateText(d, loc) + " " + timeText(d, loc) + " · " + f(tr_.driven) + " / " + f(tr_.planned) + (tr_.done ? " ✓" : ""));
      const std::vector<double> line = tr_.line;
      b.onclick([this, line] {
        pastTour = std::vector<float>(line.begin(), line.end());
        showAll(*pastTour);
        redraw();
      });
      list.append(b);
    }
    tourBox.append(list);
  }
  box.append(tourBox);
  // --- a way drawn on the map with a finger or the mouse, laid onto the roads ---
  if (host.roads) box.append(drawer->section());

  // --- go: the car drives, or the player ---
  // (what the panel is for stays in view however long the list above it grows)
  El go = el("div", "bm-row bm-go");
  const bool ready = !stops.empty() || tour.has_value();
  const std::function<void(bool)> start = [this](bool self) {
    const std::vector<SpawnJson> list = stops;
    const std::optional<TourPreview> planned = tour;
    if (!list.empty()) {
      std::vector<NamedPlace> named;
      for (const SpawnJson& p : list) named.push_back({p.name, p.x, p.z, p.yaw});
      saveWay(named, self);
    }
    stops.clear();
    tour.reset();
    host.startTrip(list, self, planned ? &*planned : nullptr);
  };
  El autoBtn = el("button", "btn", t("tripAuto"));
  autoBtn.type("button");
  autoBtn.disabled(!ready);
  autoBtn.onclick([start] { start(false); });
  El self = el("button", "btn ghost", t("tripSelf"));
  self.type("button");
  self.disabled(!ready);
  self.onclick([start] { start(true); });
  go.append(autoBtn, self);
  if (ready) {
    // (not among the buttons that start the drive: these stay in view and should not take half the panel)
    El clear = el("button", "btn ghost bm-clear", t("demoClear"));
    clear.type("button");
    clear.onclick([this] {
      stops.clear();
      planChanged();
    });
    box.append(clear);
  }
  box.append(go);
}

/** (the button "plan a tour" of fillPlan) */
Task<void> BigMap::planIt(dom::El km, dom::El planBtn, dom::El tourBox) {
  if (planning) co_return;
  const std::shared_ptr<bool> guard = alive;
  const double v = jsNumber(km.value());
  tourKm = std::max(1.0, std::min(300.0, jsRound(v == v && v != 0 ? v : 10)));
  // a long tour takes a while to work out: a bar shows how far it is, the button waits
  planning = true;
  planBtn.disabled(true);
  El bar = el("div", "bm-progress");
  El fill = el("i");
  El label = el("small", "", t("tourPlanning", {{"p", 0}}));
  bar.append(fill);
  tourBox.append(bar, label);
  std::optional<TourPreview> planned;
  std::exception_ptr failed;
  try {
    planned = co_await host.planTour(tourKm * 1000, stops.empty() ? std::nullopt : std::optional<SpawnJson>(stops.back()), [this, guard, fill, label](double share) {
      if (!*guard) return;
      const double p = jsRound(std::max(0.0, std::min(1.0, share)) * 100);
      fill.style("width", jsStr(p) + "%");
      label.textContent(t("tourPlanning", {{"p", p}}));
    });
  } catch (...) {
    failed = std::current_exception();
  }
  if (!*guard) co_return;
  planning = false;
  if (failed) std::rethrow_exception(failed);
  tour = std::move(planned);
  // (the roads of the whole map are read in the background after the start: until then, a moment's patience)
  tourNote = tour ? "" : host.roadsKnown() ? t("tourNone") : t("realityLoading");
  if (tour) showAll(tour->line);
  fillPlan();
  redraw();
}

void BigMap::showAll(const std::vector<float>& line) {
  if (line.size() < 4) return;
  double minX = std::numeric_limits<double>::infinity();
  double maxX = -std::numeric_limits<double>::infinity();
  double minZ = std::numeric_limits<double>::infinity();
  double maxZ = -std::numeric_limits<double>::infinity();
  for (size_t i = 0; i + 1 < line.size(); i += 2) {
    minX = jsMin(minX, line[i]);
    maxX = jsMax(maxX, line[i]);
    minZ = jsMin(minZ, line[i + 1]);
    maxZ = jsMax(maxZ, line[i + 1]);
  }
  cx = (minX + maxX) / 2;
  cz = (minZ + maxZ) / 2;
  const dom::Rect r = canvas.el().getBoundingClientRect();
  scale = std::min(MAX_SCALE, std::max(MIN_SCALE, std::min(r.width / (maxX - minX + 200), r.height / (maxZ - minZ + 200)) * 0.8));
}

void BigMap::close() {
  if (!node) return;
  dom::window().off("resize", onResize);
  dom::window().off("keydown", onKey, true);
  onResize = onKey = 0;
  if (transit) transit->detach();
  drawer->detach();
  if (ungrip) ungrip();
  ungrip = nullptr;
  plat::cancelAnimationFrame(frame);
  frame = 0;
  pointers.clear();
  node.remove();
  node = El();
}

void BigMap::key(dom::Event& e) {
  {
    const El target = e.target();
    if (target && target.matches("input") && target.get("type") != "checkbox") return;
  }
  const double step = 90 / scale;
  bool used = true;
  if (e.code == "ArrowLeft") cx -= step;
  else if (e.code == "ArrowRight") cx += step;
  else if (e.code == "ArrowUp") cz -= step;
  else if (e.code == "ArrowDown") cz += step;
  else if (e.key == "+" || e.code == "NumpadAdd" || e.key == "=") zoom(1.6);
  else if (e.key == "-" || e.code == "NumpadSubtract") zoom(1 / 1.6);
  else if (e.code == "Home" && worldOn()) showAllRegions();   // 2.0: every region, and back
  else used = false;
  if (used) {
    // the arrows move the map here, not the car
    e.preventDefault();
    e.stopPropagation();
    redraw();
  }
}

void BigMap::zoom(double factor, std::optional<double> px, std::optional<double> py) {
  const dom::Rect r = canvas.el().getBoundingClientRect();
  const double ox = (px ? *px : r.left + r.width / 2) - r.left - r.width / 2;
  const double oy = (py ? *py : r.top + r.height / 2) - r.top - r.height / 2;
  // (2.0: minScale() – with the world the map goes out as far as WorldLayer::MIN_SCALE)
  const double next = std::min(MAX_SCALE, std::max(minScale(), scale * factor));
  // world point under the cursor before and after must be the same
  cx += ox / scale - ox / next;
  cz += oy / scale - oy / next;
  scale = next;
  redraw();
}

void BigMap::choose(double px, double py) {
  const dom::Rect r = canvas.el().getBoundingClientRect();
  // a place asked for (the place of a photo to upload: net/online.ts): handed over, nothing else
  if (extraLayers.pick) {
    extraLayers.pick(cx + (px - r.left - r.width / 2) / scale, cz + (py - r.top - r.height / 2) / scale);
    return;
  }
  // 2.0: far out, or beside the loaded map, a tap is the world's: it picks a region
  if (worldTap(px - r.left, py - r.top)) return;
  // a tram, a bus or a stop there: its small window instead
  if (transit && transit->tap(px - r.left, py - r.top)) {
    spot.reset();
    spotBar.hidden(true);
    return;
  }
  if (transit) transit->closePopup();
  // an emergency vehicle, a fuel station or a car wash there
  const MapScale sxf = [this, r](double x) { return r.width / 2 + (x - cx) * scale; };
  const MapScale syf = [this, r](double z) { return r.height / 2 + (z - cz) * scale; };
  const std::vector<MapService>* services = host.services ? host.services() : nullptr;
  const std::vector<MapEmergency>* emergencies = host.emergencies ? host.emergencies() : nullptr;
  const std::optional<LayerIcon> icon = layerIconAt(px - r.left, py - r.top, scale, sxf, syf, services ? *services : NO_SERVICES, emergencies ? *emergencies : NO_EMERGENCIES, layers);
  // (a photo of the players of a server: its viewer)
  if (icon && icon->extra) {
    icon->extra();
    return;
  }
  if (icon && icon->em) {
    spot.reset();
    const El bar = spotBar;
    bar.replaceChildren();
    bar.hidden(false);
    const char* const names[4] = {"emPolice", "emAmbulance", "emFire", "emDoctor"};
    El text = el("div");
    text.append(el("b", "", t(icon->em->kind >= 0 && icon->em->kind < 4 ? names[icon->em->kind] : "emPolice")), el("small", "", t(icon->em->call ? "emOnCall" : "emPatrol")));
    bar.append(text);
    if (host.watchPoint && host.canTeleport()) {
      El w = el("button", "btn", t("transitWatch"));
      w.type("button");
      const int id = icon->em->id;
      w.onclick([this, id] {
        if (host.watchPoint) host.watchPoint(BigMapHost::WatchPoint{.kind = "em", .id = id});
      });
      bar.append(w);
    }
    redraw();
    return;
  }
  if (icon && icon->service) {
    const MapService& sv = *icon->service;
    showSpot(sv.x, sv.z, !sv.name.empty() ? sv.name : !sv.brand.empty() ? sv.brand : t(sv.k == "fuel" ? "layerFuel" : "layerWash"));
    // its prices: today's where the player has a key of the price service, else typical ones
    El prices = el("div", "bm-prices");
    if (sv.k == "fuel") {
      bool live = false;
      const std::vector<std::string> fuels = !sv.fuels.empty() ? sv.fuels : std::vector<std::string>{"e10", "e5", "diesel"};
      for (const std::string& fuel : fuels) {
        if (!host.fuelPrice || !FUELS || !FUELS->count(fuel)) continue;
        const BigMapHost::FuelPrice p = host.fuelPrice(sv, fuel);
        live = live || p.live;
        prices.append(el("span", "", FUELS->at(fuel).label + " " + plat::formatNumber(p.price, localeOf(host.lang()), {.minimumFractionDigits = 3, .maximumFractionDigits = 3}) + " €"));
      }
      prices.append(el("small", "", live ? t("priceToday") + " · " + (host.priceSource ? host.priceSource() : "") : t("priceTypical")));
    } else prices.append(el("span", "", t("layerWash") + " 9,90 €"));
    spotBar.prepend(prices);
    return;
  }
  // roadworks or a demonstration there: what it is, where, until when
  const std::optional<RoadEventInfo> ev = eventAt(px - r.left, py - r.top, r.width, r.height);
  if (ev) {
    spot.reset();
    const El bar = spotBar;
    bar.replaceChildren();
    bar.hidden(false);
    El text = el("div");
    const std::string loc = localeOf(host.lang());
    const auto hm = [&loc](double ms) { return timeText(ms, loc); };
    if (ev->kind == "works")
      text.append(el("b", "", t(ev->detail == "lane" ? "eventWorksLane" : ev->detail == "light" ? "eventWorksLight" : ev->detail == "signs" ? "eventWorksSigns" : "eventWorks")),
                  el("small", "", ev->street));
    else
      text.append(el("b", "", ev->active ? t("eventDemoNow", {{"place", ev->detail}, {"until", hm(ev->until)}}) : t("eventDemo", {{"place", ev->detail}, {"from", hm(ev->from)}, {"until", hm(ev->until)}})),
                  el("small", "", ev->street));
    bar.append(text);
    if (host.watchPoint && host.canTeleport()) {
      El w = el("button", "btn", t("transitWatch"));
      w.type("button");
      const RoadEventInfo info = *ev;
      w.onclick([this, info] {
        if (host.watchPoint) host.watchPoint(BigMapHost::WatchPoint{.kind = "event", .ev = info});
      });
      bar.append(w);
    }
    redraw();
    return;
  }
  const double x = cx + (px - r.left - r.width / 2) / scale;
  const double z = cz + (py - r.top - r.height / 2) / scale;
  showSpot(x, z);
}

/**
 * The lines of public transport as the search may find them (world/…: trams
 * of lines.json.gz and buses of buslines.json.gz, by their number): each with
 * where its first stop is, for putting the map there.
 */
std::vector<LineSource> BigMap::lineSources() {
  TransitSource* src = host.transit ? host.transit() : nullptr;
  const Timetable* tt = src ? src->timetable() : nullptr;
  if (!src || !tt) return {};
  const std::vector<StopSource>* stopList = host.stops ? host.stops() : nullptr;
  const auto at = [stopList](const std::string& name) -> const StopSource* {
    if (!stopList) return nullptr;
    for (const StopSource& p : *stopList)
      if (p.name == name) return &p;
    return nullptr;
  };
  std::vector<LineSource> out;
  const auto add = [&](const char* kind, const std::vector<std::optional<TimetableLine>>& list) {
    for (const std::optional<TimetableLine>& l : list) {
      if (!l) continue;
      const StopSource* p = !l->patterns.empty() && !l->patterns[0].stops.empty() ? at(l->patterns[0].stops[0]) : nullptr;
      out.push_back({l->ref, kind, p ? p->x : 0, p ? p->z : 0, l->index});
    }
  };
  add("tram", tt->lines);
  add("bus", tt->buses);
  return out;
}

void BigMap::goTo(double x, double z, double scale_, const std::string& name) {
  cx = x;
  cz = z;
  scale = std::max(scale, scale_);
  if (transit) transit->closePopup();
  showSpot(x, z, name);
}

void BigMap::note(const std::string& text) {
  spot.reset();
  const El bar = spotBar;
  bar.replaceChildren();
  bar.hidden(false);
  El closeBtn = el("button", "btn ghost bm-spot-close", "✕");
  closeBtn.type("button");
  closeBtn.title(t("realityClose"));
  closeBtn.setAttribute("aria-label", t("realityClose"));
  closeBtn.onclick([this, bar] {
    bar.hidden(true);
    redraw();
  });
  bar.append(closeBtn, el("span", "", text));
  redraw();
}

void BigMap::showSpot(double x, double z, const std::string& name) {
  // 2.0: the place panel takes the place of a region's card
  if (chosen) {
    chosen.reset();
    fillRegion();
  }
  foreignSpot.reset();   // 2.0
  spot = host.spotAt(x, z);
  if (spot && !name.empty()) spot->name = name;
  const El bar = spotBar;
  bar.replaceChildren();
  bar.hidden(false);
  // (a way out of the panel without choosing anything: close it)
  El closeBtn = el("button", "btn ghost bm-spot-close", "✕");
  closeBtn.type("button");
  closeBtn.title(t("realityClose"));
  closeBtn.setAttribute("aria-label", t("realityClose"));
  closeBtn.onclick([this, bar] {
    spot.reset();
    bar.hidden(true);
    redraw();
  });
  bar.append(closeBtn);
  if (!spot) {
    bar.append(el("span", "", t("addressNoRoad")));
  } else {
    const SpawnJson s = *spot;
    const auto [lat, lon] = host.latLon(s.x, s.z);
    El text = el("div");
    text.append(el("b", "", !s.name.empty() ? s.name : t("mapPlace")), el("small", "", toFixed(lat, 5) + ", " + toFixed(lon, 5)));
    bar.append(text);
    if (host.canTeleport()) {
      // (the aeroplane: in the air there, at the height chosen beside the button)
      const bool air = host.flying && host.flying() && host.teleportAir;
      El go = el("button", "btn", t(air ? "mapTeleportPlane" : "mapTeleport"));
      go.type("button");
      go.onclick([this, air, s] {
        if (air) {
          if (host.teleportAir) host.teleportAir(s, airHeight);
        } else host.teleport(s);
      });
      // … or drive there (or have the car drive there by itself)
      El add = el("button", "btn ghost", t("planAdd"));
      add.type("button");
      add.onclick([this, s, bar] {
        stops.push_back(s);
        spot.reset();
        tour.reset();
        tourNote = "";
        bar.hidden(true);
        planBox.hidden(false);
        fillPlan();
        redraw();
      });
      if (air) {
        El h = el("label", "bm-height");
        El sel = el("select");
        for (const int m : {150, 300, 600, 1000, 1500, 2000, 3000}) {
          El op = el("option", "", std::to_string(m) + " m (" + jsStr(jsRound((m * 3.28084) / 50) * 50) + " ft)");
          op.value(std::to_string(m));
          sel.append(op);
        }
        sel.value(jsStr(airHeight));
        sel.onchange([this, sel] { airHeight = jsNumber(sel.value()); });
        h.append(el("span", "", t("planeHeight")), sel);
        bar.append(h, go);
      } else bar.append(go, add);
      // … or straight there by the autopilot (the aeroplane flies there and circles – or lands, if it is an airfield)
      El autoBtn = el("button", "btn", t(host.flying && host.flying() ? "autoFlyHere" : "autoDriveHere"));
      autoBtn.type("button");
      autoBtn.onclick([this, s] {
        stops.clear();
        tour.reset();
        host.startTrip({s}, false, nullptr);
      });
      bar.append(autoBtn);
    } else if (host.menu && host.menu()) {
      // (from the main menu: the drive is to begin there)
      El go = el("button", "btn", t("mapTeleport"));
      go.type("button");
      go.onclick([this, s] { host.teleport(s); });
      bar.append(go);
    }
    // a car put down here (on foot, at a crane): only on the area in memory, inside the dashed line
    const std::string can = host.canSpawnCar ? host.canSpawnCar(x, z) : "no";
    if (can == "ok") {
      El carBtn = el("button", "btn ghost", t("mapSpawnCar"));
      carBtn.type("button");
      carBtn.onclick([this, x, z] {
        if (host.spawnCar) host.spawnCar(x, z);
      });
      bar.append(carBtn);
    } else if (can == "outside") bar.append(el("small", "", t("spawnCarOutside")));
    // a favourite place: kept on this device, a star on the map, in the list of the plan panel
    std::optional<FavouriteRecord> fav;
    for (const FavouriteRecord& fr : loadFavourites())
      if (std::hypot(fr.x - s.x, fr.z - s.z) <= 20) {
        fav = fr;
        break;
      }
    El star = el("button", "btn ghost", fav ? "★ " + t("favRemove") : "☆ " + t("favAdd"));
    star.type("button");
    star.onclick([this, fav, s, x, z, name] {
      if (fav) removeFavourite(fav->x, fav->z);
      else saveFavourite({.name = !s.name.empty() ? s.name : t("mapPlace"), .x = s.x, .z = s.z, .yaw = s.yaw});
      showSpot(x, z, name);
      if (!planBox.hidden()) fillPlan();
    });
    bar.append(star);
  }
  redraw();
}

void BigMap::drawEvents(const Ctx2D& g, double W, double H, double s) {
  const std::vector<RoadEventInfo>* all = host.events ? host.events() : nullptr;
  std::vector<const RoadEventInfo*> list;
  if (all)
    for (const RoadEventInfo& e : *all)
      if (e.kind == "works" ? layers.works : layers.demos) list.push_back(&e);
  if (list.empty()) return;
  const auto sx = [&](double x) { return W / 2 + (x - cx) * s; };
  const auto sy = [&](double z) { return H / 2 + (z - cz) * s; };
  g.save();
  for (const RoadEventInfo* e : list) {
    if (e->kind != "demo" || e->line.size() < 4) continue;
    g.strokeStyle(e->active ? "rgba(230, 40, 40, 0.9)" : "rgba(230, 40, 40, 0.45)");
    g.lineWidth(std::max(3.0, std::min(8.0, 6 * s)));
    g.setLineDash({8, 6});
    g.beginPath();
    for (size_t i = 0; i + 1 < e->line.size(); i += 2) {
      if (i == 0) g.moveTo(sx(e->line[i]), sy(e->line[i + 1]));
      else g.lineTo(sx(e->line[i]), sy(e->line[i + 1]));
    }
    g.stroke();
  }
  g.setLineDash({});
  g.textAlign("center");
  g.textBaseline("middle");
  for (const RoadEventInfo* e : list) {
    const double x = sx(e->x);
    const double y = sy(e->z);
    if (x < -20 || y < -20 || x > W + 20 || y > H + 20) continue;
    // (a warning triangle: orange for works, red for a demonstration – faded when it is not on now)
    g.globalAlpha(e->active || e->kind == "works" ? 1 : 0.55);
    g.fillStyle(e->kind == "works" ? "#ff9f1a" : "#e62828");
    g.strokeStyle("#1b1208");
    g.lineWidth(2);
    g.beginPath();
    g.moveTo(x, y - 11);
    g.lineTo(x + 11, y + 8);
    g.lineTo(x - 11, y + 8);
    g.closePath();
    g.fill();
    g.stroke();
    g.fillStyle(e->kind == "works" ? "#1b1208" : "#ffffff");
    g.font("700 11px system-ui, sans-serif");
    g.fillText(e->kind == "works" ? "⚒" : "!", x, y + 2);
  }
  g.restore();
}

std::optional<RoadEventInfo> BigMap::eventAt(double px, double py, double W, double H) {
  const std::vector<RoadEventInfo>* all = host.events ? host.events() : nullptr;
  if (!all) return std::nullopt;
  const RoadEventInfo* best = nullptr;
  double bestD = 18;
  for (const RoadEventInfo& e : *all) {
    if (!(e.kind == "works" ? layers.works : layers.demos)) continue;
    const double d = std::hypot(W / 2 + (e.x - cx) * scale - px, H / 2 + (e.z - cz) * scale - py);
    if (d < bestD) {
      bestD = d;
      best = &e;
    }
  }
  if (!best) return std::nullopt;
  return *best;
}

void BigMap::redraw() {
  if (frame || !node) return;
  frame = plat::requestAnimationFrame([this](double) {
    frame = 0;
    draw();
  });
}

/** (the end of draw(): the scale, and whose pictures these are) */
void BigMap::drawScale(double W, double s, bool aerial) {
  // scale: a round number of metres that is about a fifth of the width
  const double want = std::min(W / 5, 150.0) / s;
  const double pow = std::pow(10.0, std::floor(std::log10(want)));
  const double metres = (want / pow >= 5 ? 5 : want / pow >= 2 ? 2 : 1) * pow;
  const std::string lang = host.lang();
  scaleBar.style("width", jsStr(jsRound(metres * s)) + "px");
  const std::string label = metres >= 1000 ? formatUnit(metres / 1000, Unit::Kilometer, lang) : formatUnit(metres, Unit::Meter, lang);
  if (scaleText.textContent() != label) scaleText.textContent(label);
  // whose pictures these are
  // 2.0: the credits of the sources on the screen (drawAerialLayer)
  const std::string creditText = aerial && photos ? aerialCredit : "";
  if (credit.textContent() != creditText) credit.textContent(creditText);
}

void BigMap::draw() {
  const El c = canvas.el();
  const Ctx2D g = canvas.getContext2D();
  if (!g || !node) return;
  const double dpr = std::min(2.0, plat::devicePixelRatio() > 0 ? plat::devicePixelRatio() : 1.0);
  const double W = c.clientWidth();
  const double H = c.clientHeight();
  if (canvas.width() != jsRound(W * dpr) || canvas.height() != jsRound(H * dpr)) {
    canvas.width(static_cast<int>(jsRound(W * dpr)));
    canvas.height(static_cast<int>(jsRound(H * dpr)));
  }
  const World& world = *host.world;
  const WorldMetaJson& m = world.json.meta;
  // do not lose the map: its middle stays within reach
  // (2.0: reach() – the loaded map, and with the world everything it shows)
  const std::array<double, 4> lim = reach();
  cx = std::min(lim[2], std::max(lim[0], cx));
  cz = std::min(lim[3], std::max(lim[1], cz));
  const double s = scale;
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  // (2.0: the colours of the style chosen – ui/mapstyle.h; the dark one is 1.7.7's)
  const MapStyle& st = style();
  g.fillStyle(st.background);
  g.fillRect(0, 0, W, H);
  // 2.0: the world beneath the map's own roads: land and sea, the borders, every region there is.
  // Below WORLD_SCALE it is the whole picture – the roads of one region are no more than a speck there.
  const bool withWorld = worldOn();
  const bool far = withWorld && s < WorldLayer::WORLD_SCALE;
  if (worldBtn) worldBtn.hidden(!withWorld);
  if (far != worldFar) {
    worldFar = far;
    fillRegion();
  }
  // 2.0: the view as the overviews take it, and the names written so far (none is to lie on another, across the maps drawn)
  MapView mv;
  mv.cx = cx;
  mv.cz = cz;
  mv.scale = s;
  mv.width = W;
  mv.height = H;
  std::vector<LabelBox> taken;
  if (withWorld) {
    const std::string current = currentRegion();
    // 2.0: the aerial photographs on the land, under the borders and the regions (ui/bigmap_aerial.cpp)
    const bool aerialHere = photos && host.aerial();
    worldLayer->draw(g, mv, current, chosen ? *chosen : current, host.lang(), st,
                     aerialHere ? std::function<void()>([&] { drawAerialLayer(g, W, H, dpr); }) : nullptr);
    // … and on it the regions one has zoomed into: their roads and places (fetched once they fill enough of the picture).
    // (1.7.7's map ended here far out; now the loaded map's own schema is drawn at every zoom, below)
    drawForeign(g, mv, dpr, st, taken);
  }
  // world → screen
  g.translate(W / 2, H / 2);
  g.scale(s, s);
  g.translate(-cx, -cz);
  const double minX = cx - W / 2 / s;
  const double maxX = cx + W / 2 / s;
  const double minZ = cz - H / 2 / s;
  const double maxZ = cz + H / 2 / s;

  // the area the map covers (2.0: with the world, the land is drawn already and the loaded region outlined)
  if (!withWorld) {
    g.fillStyle(st.area);
    g.fillRect(m.minX, m.minZ, m.maxX - m.minX, m.maxZ - m.minZ);
  }

  // aerial photographs under the roads (2.0: Germany and beyond, ui/bigmap_aerial.cpp – with the world, drawn with its land)
  const bool aerial = photos && host.aerial();
  if (aerial && !withWorld) drawAerialLayer(g, W, H, dpr);

  g.lineCap("round");
  g.lineJoin("round");
  // over a photograph the roads are only hinted at
  g.globalAlpha(aerial ? 0.38 : 1);
  // one path per class and width: roads of a class are drawn at their real width once that is more than the least
  std::vector<std::pair<double, RoadPath>> paths[4];
  const auto pathFor = [&](double tier, double width) -> RoadPath* {
    int ci = -1;
    for (int i = 0; i < 4; i++)
      if (tier >= CLASSES[i].min && tier <= CLASSES[i].max) {
        ci = i;
        break;
      }
    if (ci < 0 || s < CLASSES[ci].from) return nullptr;
    const double w = std::max(CLASSES[ci].px / s, jsRound(width));
    for (auto& p : paths[ci])
      if (p.first == w) return &p.second;
    paths[ci].emplace_back(w, RoadPath());
    return &paths[ci].back().second;
  };
  const NetIndex* net = host.net();
  // 2.0: zoomed out, the whole network from the overview's bands (ui/mapoverview.h): the big roads first, the small ones as one zooms in
  if (net && s < MapOverview::BAND_SCALE) ownOverview().drawRoads(g, mv, dpr, st, aerial ? 0.38 : 1);
  else if (net) {
    // the roads of the whole map (most of them are not loaded): from the file that holds them all in a few points
    const std::vector<uint8_t>& cls = net->net->edgeCls;
    net->edgesInBox(
        minX, minZ, maxX, maxZ,
        [&](int e) {
          const double tier = CLASS_TIER[cls[static_cast<size_t>(e)]];
          RoadPath* path = pathFor(tier, tier >= 6 ? 11 : tier >= 4 ? 7.5 : tier >= 3 ? 6.5 : tier >= 2 ? 5.5 : 3.5);
          if (path) net->trace(e, *path);
        },
        s < CLASSES[1].from);
  } else {
    world.edgesInBox(minX, minZ, maxX, maxZ, [&](const Edge& e) {
      RoadPath* path = pathFor(e.tier, e.width);
      if (!path) return;
      const std::vector<double>& p = e.pl.pts;
      path->moveTo(p[0], p[2]);
      for (size_t i = 3; i + 2 < p.size(); i += 3) path->lineTo(p[i], p[i + 2]);
    });
  }
  // (2.0: the style's colours for the four classes; the light map draws a rim round every road first)
  const RoadKind KIND_OF[4] = {RoadKind::minor, RoadKind::tertiary, RoadKind::secondary, RoadKind::motorway};
  for (int ci = 0; ci < 4; ci++) {
    const char* rim = st.casing[static_cast<int>(KIND_OF[ci])];
    if (!rim) continue;
    g.strokeStyle(rim);
    for (const auto& [w, path] : paths[ci]) {
      g.lineWidth(w + 1.6 / s);
      path.stroke(g);
    }
  }
  for (int ci = 0; ci < 4; ci++) {
    g.strokeStyle(st.road[static_cast<int>(KIND_OF[ci])]);
    for (const auto& [w, path] : paths[ci]) {
      g.lineWidth(w);
      path.stroke(g);
    }
  }

  g.globalAlpha(1);

  // the tram tracks: where a tram can go (one path for all of them, made once)
  const BigMapHost::Tramways* trams = host.tramways ? host.tramways() : nullptr;
  if (trams) {
    if (!tramPath || tramVersion != trams->version) {
      RoadPath p;
      for (const std::vector<float>& track : trams->tracks) {
        if (track.size() < 2) continue;
        p.moveTo(track[0], track[1]);
        for (size_t i = 2; i + 1 < track.size(); i += 2) p.lineTo(track[i], track[i + 1]);
      }
      tramPath = std::move(p);
      tramVersion = trams->version;
    }
    g.strokeStyle("#c070f0");
    g.lineWidth(std::max(2.4 / s, 1.6));
    g.globalAlpha(aerial ? 0.9 : 0.8);
    tramPath->stroke(g);
    g.globalAlpha(1);
  }

  // where the car has been
  if (showPath) {
    const std::vector<double>& p = host.path->points;
    g.strokeStyle("#38b6ff");
    g.lineWidth(std::max(3 / s, 2.5));
    RoadPath line;
    bool pen = false;
    for (size_t i = 0; i + 1 < p.size(); i += 2) {
      if (std::isnan(p[i])) {
        pen = false;
        continue;
      }
      if (pen) line.lineTo(p[i], p[i + 1]);
      else line.moveTo(p[i], p[i + 1]);
      pen = true;
    }
    line.stroke(g);
  }

  // a tour driven before (chosen from the list)
  if (pastTour && pastTour->size() >= 4) {
    const std::vector<float>& p = *pastTour;
    g.strokeStyle("#7fd4ff");
    g.lineWidth(std::max(4 / s, 3.5));
    g.globalAlpha(0.85);
    g.setLineDash({10 / s, 7 / s});
    g.beginPath();
    g.polyline(std::vector<double>(p.begin(), p.end()));
    g.stroke();
    g.setLineDash({});
    g.globalAlpha(1);
  }

  // the tour that was planned
  if (tour) {
    const std::vector<float>& p = tour->line;
    g.strokeStyle("#ff8a3d");
    g.lineWidth(std::max(4 / s, 3.5));
    g.globalAlpha(0.85);
    g.beginPath();
    g.polyline(std::vector<double>(p.begin(), p.end()));
    g.stroke();
    g.globalAlpha(1);
  }

  // from here on in screen pixels
  const auto sx = [&](double x) { return W / 2 + (x - cx) * s; };
  const auto sy = [&](double z) { return H / 2 + (z - cz) * s; };
  g.setTransform(dpr, 0, 0, dpr, 0, 0);

  // the tram stops: a dot each, their names when one is close enough to read them
  if (trams && s > 0.05) {
    g.fillStyle("#ffffff");
    g.strokeStyle("#7a2fb0");
    g.lineWidth(2);
    const bool named = s > 0.6;
    g.font("600 12px system-ui, sans-serif");
    g.textAlign("left");
    g.textBaseline("middle");
    for (const BigMapHost::Tramways::Stop& st : trams->stops) {
      const double x = sx(st.x);
      const double y = sy(st.z);
      if (x < -40 || y < -20 || x > W + 40 || y > H + 20) continue;
      g.beginPath();
      g.arc(x, y, named ? 4.5 : 3, 0, M_PI * 2);
      g.fill();
      g.stroke();
      if (named) {
        g.lineWidth(3);
        g.strokeStyle("rgba(12, 18, 24, 0.85)");
        g.strokeText(st.name, x + 7, y);
        g.fillStyle("#e8d4ff");
        g.fillText(st.name, x + 7, y);
        g.fillStyle("#ffffff");
        g.strokeStyle("#7a2fb0");
        g.lineWidth(2);
      }
    }
  }

  // names of towns and districts
  // (2.0: every place of the map, ranked city > town > village > suburb > hamlet, larger for the bigger, none on
  //  another – ui/mapoverview.h; 1.7.7 named the towns and districts only)
  const BigMapHost::Car car = host.car();
  const double carX = sx(car.x);
  const double carY = sy(car.z);
  ownOverview().drawNames(g, mv, st, taken, carX, carY);

  // the roadworks and demonstrations of the day, the fuel stations and car washes, the emergency vehicles
  drawEvents(g, W, H, s);
  // the favourite places: gold stars
  g.save();
  g.textAlign("center");
  g.textBaseline("middle");
  g.font("700 18px system-ui, sans-serif");
  for (const FavouriteRecord& fav : loadFavourites()) {
    const double fx = W / 2 + (fav.x - cx) * s;
    const double fy = H / 2 + (fav.z - cz) * s;
    if (fx < -12 || fy < -12 || fx > W + 12 || fy > H + 12) continue;
    g.lineWidth(3);
    g.strokeStyle("#1b1208");
    g.strokeText("★", fx, fy);
    g.fillStyle("#ffcc00");
    g.fillText("★", fx, fy);
  }
  g.restore();
  {
    const std::vector<MapService>* services = host.services ? host.services() : nullptr;
    const std::vector<MapEmergency>* emergencies = host.emergencies ? host.emergencies() : nullptr;
    drawLayerIcons(g, W, H, s, sx, sy, services ? *services : NO_SERVICES, emergencies ? *emergencies : NO_EMERGENCIES, layers, std::fmod(std::floor(plat::nowMs() / 400), 2) == 0);
  }
  // the area of the world in memory (where a car may be put down): a dashed line round it
  const std::vector<float>* area = host.loadedArea ? host.loadedArea() : nullptr;
  if (area && !area->empty()) {
    g.save();
    g.strokeStyle("rgba(56,182,255,0.9)");
    g.lineWidth(2);
    g.setLineDash({8, 6});
    g.beginPath();
    for (size_t i = 0; i + 3 < area->size(); i += 4) {
      g.moveTo(W / 2 + ((*area)[i] - cx) * s, H / 2 + ((*area)[i + 1] - cz) * s);
      g.lineTo(W / 2 + ((*area)[i + 2] - cx) * s, H / 2 + ((*area)[i + 3] - cz) * s);
    }
    g.stroke();
    g.restore();
  }
  // the trams and buses with their stops, and a way drawn on the map
  if (transit) transit->draw(g, W, H, s, cx, cz);
  drawer->draw(g, W, H, s, cx, cz);
  g.textAlign("center");
  g.textBaseline("middle");

  // the places of a demonstration drive, numbered in their order
  for (size_t i = 0; i < stops.size(); i++) {
    const double x = sx(stops[i].x);
    const double y = sy(stops[i].z);
    g.fillStyle("#38b6ff");
    g.strokeStyle("#06131c");
    g.lineWidth(2);
    g.beginPath();
    g.arc(x, y, 11, 0, M_PI * 2);
    g.fill();
    g.stroke();
    g.fillStyle("#06131c");
    g.font("700 13px system-ui, sans-serif");
    g.fillText(std::to_string(i + 1), x, y + 0.5);
  }

  // the chosen place
  if (spot) {
    const double x = sx(spot->x);
    const double y = sy(spot->z);
    g.fillStyle("#ff5252");
    g.strokeStyle("#fff");
    g.lineWidth(2);
    g.beginPath();
    g.arc(x, y - 14, 8, M_PI, 0);
    g.lineTo(x, y);
    g.closePath();
    g.fill();
    g.stroke();
  }

  // the car
  g.save();
  g.translate(sx(car.x), sy(car.z));
  g.rotate(-car.yaw);
  g.fillStyle("#ffcc00");
  g.strokeStyle("#111");
  g.lineWidth(2);
  g.beginPath();
  g.moveTo(0, -13);
  g.lineTo(8, 9);
  g.lineTo(0, 5);
  g.lineTo(-8, 9);
  g.closePath();
  g.fill();
  g.stroke();
  g.restore();

  drawScale(W, s, aerial);
}

// ---------------------------------------------------------------------------------------------
// 2.0: one map for all regions (docs/WORLD-MAP.md).  Nothing below is in the original.
// ---------------------------------------------------------------------------------------------

bool BigMap::worldOn() const { return worldLayer && worldLayer->ready() && worldLayer->hasRegions(); }

double BigMap::minScale() const { return worldOn() ? WorldLayer::MIN_SCALE : MIN_SCALE; }

std::string BigMap::currentRegion() const { return host.region ? host.region() : std::string(WorldLayer::SITE); }

MapView BigMap::view() const {
  MapView v;
  v.cx = cx;
  v.cz = cz;
  v.scale = scale;
  if (canvas) {
    const dom::Rect r = canvas.el().getBoundingClientRect();
    v.width = r.width;
    v.height = r.height;
  }
  return v;
}

std::array<double, 4> BigMap::reach() const {
  const WorldMetaJson& m = host.world->json.meta;
  std::array<double, 4> box = {m.minX, m.minZ, m.maxX, m.maxZ};
  if (!worldOn()) return box;
  const auto take = [&box](const std::array<double, 4>& b) {
    box[0] = std::min(box[0], b[0]);
    box[1] = std::min(box[1], b[1]);
    box[2] = std::max(box[2], b[2]);
    box[3] = std::max(box[3], b[3]);
  };
  for (const Region& r : worldLayer->regions()) take(r.box);
  if (!worldLayer->outline().empty()) take(worldLayer->outline().box);
  return box;
}

Task<void> BigMap::awaitWorld() {
  const std::shared_ptr<bool> guard = alive;
  const std::shared_ptr<WorldLayer> layer = worldLayer;
  co_await layer->whenReady();
  if (*guard && node) redraw();
}

void BigMap::setWorld(std::shared_ptr<WorldLayer> world) {
  worldLayer = std::move(world);
  worldAwaited = false;
  chosen.reset();
  worldBack.reset();
  if (!node) return;
  if (worldLayer && !worldLayer->ready()) {
    worldAwaited = true;
    awaitWorld().detach();
  }
  fillRegion();
  redraw();
}

const Region* BigMap::chosenRegion() const {
  if (!chosen || !worldOn()) return nullptr;
  return worldLayer->byId(*chosen);
}

void BigMap::showAllRegions() {
  if (!node || !worldOn()) return;
  if (worldBack) {
    // again: back to the view before
    cx = worldBack->cx;
    cz = worldBack->cz;
    scale = worldBack->scale;
    worldBack.reset();
  } else {
    worldBack = View{cx, cz, scale};
    const dom::Rect r = canvas.el().getBoundingClientRect();
    const MapView v = worldLayer->fitAll(r.width, r.height);
    cx = v.cx;
    cz = v.cz;
    // (the world's picture, also where the regions lie close together)
    scale = std::max(WorldLayer::MIN_SCALE, std::min(v.scale, WorldLayer::WORLD_SCALE * 0.75));
    spot.reset();
    spotBar.hidden(true);
    if (transit) transit->closePopup();
  }
  worldBtn.setAttribute("aria-pressed", worldBack ? "true" : "false");
  redraw();
}

void BigMap::showRegion(const std::string& id) {
  if (!node || !worldOn()) return;
  const Region* r = worldLayer->byId(id);
  if (!r) return;
  if (r->id == currentRegion()) {
    backToMap();
    return;
  }
  // (the globe then leads back to where the player was)
  if (!worldBack && scale >= WorldLayer::WORLD_SCALE) {
    worldBack = View{cx, cz, scale};
    worldBtn.setAttribute("aria-pressed", "true");
  }
  const dom::Rect rect = canvas.el().getBoundingClientRect();
  const MapView v = worldLayer->fit(*r, rect.width, rect.height);
  cx = v.cx;
  cz = v.cz;
  scale = std::max(WorldLayer::MIN_SCALE, std::min(v.scale, WorldLayer::WORLD_SCALE * 0.75));
  chooseRegion(*r);
}

bool BigMap::worldTap(double x, double y) {
  if (!worldOn()) return false;
  const MapView v = view();
  const bool far = scale < WorldLayer::WORLD_SCALE;
  const std::string current = currentRegion();
  if (!far) {
    // close up, a tap on the loaded map is the map's own: a place on a road
    const Region* own = worldLayer->byId(current);
    const WorldMetaJson& m = host.world->json.meta;
    const std::array<double, 4> box = own ? own->box : std::array<double, 4>{m.minX, m.minZ, m.maxX, m.maxZ};
    if (boxContains(box, v.wx(x), v.wz(y))) return false;
  }
  // a road of a region that is not loaded, where its schema is drawn: put the car there
  if (foreignTap(x, y)) return true;
  const Region* r = worldLayer->regionAt(v, x, y);
  if (!r) {
    if (!far) return false;
    // (a tap on the sea, or on land without a map: nothing is chosen any more)
    chosen.reset();
    fillRegion();
    redraw();
    return true;
  }
  if (r->id == current) {
    if (!far) return false;
    backToMap();
    return true;
  }
  chooseRegion(*r);
  return true;
}

void BigMap::chooseRegion(const Region& r) {
  chosen = r.id;
  spot.reset();
  spotBar.hidden(true);
  if (transit) transit->closePopup();
  fillRegion();
  // (with keys: Enter opens it)
  const El open = regionBar.querySelector(".bm-region-open");
  if (open) open.focus();
  redraw();
}

void BigMap::backToMap() {
  chosen.reset();
  if (worldBack && worldBack->scale >= WorldLayer::WORLD_SCALE) {
    cx = worldBack->cx;
    cz = worldBack->cz;
    scale = worldBack->scale;
  } else {
    const BigMapHost::Car c = host.car();
    cx = c.x;
    cz = c.z;
    scale = 0.22;
  }
  worldBack.reset();
  if (worldBtn) worldBtn.setAttribute("aria-pressed", "false");
  fillRegion();
  redraw();
}

void BigMap::fillRegion() {
  const El bar = regionBar;
  if (!bar) return;
  bar.replaceChildren();
  const bool on = worldOn();
  const bool far = on && scale < WorldLayer::WORLD_SCALE;
  const Region* r = chosenRegion();
  if (!on || (!r && !far)) {
    bar.hidden(true);
    return;
  }
  bar.hidden(false);
  if (r) {
    El closeBtn = el("button", "btn ghost bm-spot-close", "✕");
    closeBtn.type("button");
    closeBtn.title(t("realityClose"));
    closeBtn.setAttribute("aria-label", t("realityClose"));
    closeBtn.onclick([this] {
      chosen.reset();
      fillRegion();
      redraw();
    });
    // its name, its country, and how much there is to download where that is known
    El text = el("div");
    text.append(el("b", "", r->name));
    std::string sub = worldLayer->countryOf(*r, host.lang());
    if (const std::optional<double> bytes = worldLayer->downloadBytes(*r)) {
      if (!sub.empty()) sub += " · ";
      sub += t("worldSize", {{"mb", std::max(1.0, jsRound(*bytes / 1e6))}});
    }
    if (!sub.empty()) text.append(el("small", "", sub));
    El open = el("button", "btn bm-region-open", t("worldOpen"));
    open.type("button");
    const std::string id = r->id;
    open.onclick([this, id] {
      const Region* now = worldOn() ? worldLayer->byId(id) : nullptr;
      if (!now || !host.onOpenRegion) return;
      // (copies: the game may load the region at once, and take this map away with the old one)
      const Region region = *now;
      const std::function<void(const Region&)> openIt = host.onOpenRegion;
      openIt(region);
    });
    bar.append(closeBtn, text, open);
  } else bar.append(el("span", "bm-region-hint", t("worldHint")));
  if (far) {
    // far out: the way back into the map that is loaded
    const Region* own = worldLayer->byId(currentRegion());
    El back = el("button", "btn ghost bm-region-back", t("worldBack", {{"name", own ? own->name : host.world->json.meta.name}}));
    back.type("button");
    back.onclick([this] { backToMap(); });
    bar.append(back);
  }
}

void BigMap::setStyle(const std::string& name) {
  prefs.style = name == "light" ? "light" : "dark";
  saveMapPrefs(prefs);
  if (styleBtn) styleBtn.setAttribute("aria-pressed", prefs.style == "light" ? "true" : "false");
  if (node) node.toggleClass("bm-light", prefs.style == "light");
  redraw();
}

std::vector<RegionSource> BigMap::regionSources() const {
  std::vector<RegionSource> out;
  if (!worldOn()) return out;
  const std::string lang = host.lang();
  for (const Region& r : worldLayer->regions()) out.push_back({r.id, r.name, worldLayer->countryOf(r, lang), (r.box[0] + r.box[2]) / 2, (r.box[1] + r.box[3]) / 2});
  return out;
}

// ---------------------------------------------------------------------------------------------
// 2.0: the schema of the whole map, the search that finds everything, the regions not loaded
// ---------------------------------------------------------------------------------------------

MapOverview& BigMap::ownOverview() {
  const NetIndex* ni = host.net ? host.net() : nullptr;
  const std::shared_ptr<const NetData> net = ni ? ni->net : nullptr;
  if (!own || own->net() != net.get() || own->places().size() != host.world->json.places.size()) own = std::make_unique<MapOverview>(net, host.world->json.places);
  return *own;
}

void BigMap::noteStreets(const std::vector<Edge*>& edges) {
  MapOverview& ov = ownOverview();
  for (const Edge* e : edges) {
    if (!e || e->name.empty() || e->pl.pts.size() < 3) continue;
    // (the middle of the road; a street goes by the town nearest to it)
    const std::vector<double>& p = e->pl.pts;
    const size_t mid = (p.size() / 3 / 2) * 3;
    const double x = p[mid], z = p[mid + 2];
    if (streets.near(e->name, x, z, 1500)) continue;
    const PlaceJson* town = ov.nearestPlace(x, z, 6000);
    streets.add(e->name, x, z, town ? town->name : "");
  }
}

std::vector<PlaceHit> BigMap::browsePlaces() {
  // the cities and towns first, then the villages and the parts of towns – each kind the nearest first
  const std::vector<PlaceJson>& all = host.world->json.places;
  std::vector<const PlaceJson*> list;
  for (const PlaceJson& p : all)
    if (MapOverview::placeRank(p.kind) >= 2) list.push_back(&p);
  std::stable_sort(list.begin(), list.end(), [this](const PlaceJson* a, const PlaceJson* b) {
    const int ra = std::min(4, MapOverview::placeRank(a->kind)), rb = std::min(4, MapOverview::placeRank(b->kind));
    if (ra != rb) return ra > rb;
    return std::hypot(a->x - cx, a->z - cz) < std::hypot(b->x - cx, b->z - cz);
  });
  std::vector<PlaceHit> out;
  for (size_t i = 0; i < list.size() && i < 200; i++) {
    PlaceHit h;
    h.label = list[i]->name;
    h.street = "";
    h.x = list[i]->x;
    h.z = list[i]->z;
    h.exact = false;
    h.what = "town";
    out.push_back(std::move(h));
  }
  // … then the streets nearest to the middle of the map (those of the pieces loaded so far)
  for (AddressHit& a : streets.nearest(cx, cz, 60)) {
    PlaceHit h;
    static_cast<AddressHit&>(h) = std::move(a);
    h.what = "address";
    out.push_back(std::move(h));
  }
  return out;
}

Task<std::vector<PlaceHit>> BigMap::noPlaces() { co_return std::vector<PlaceHit>(); }

Task<std::vector<PlaceHit>> BigMap::searchOnline(std::string q) {
  const std::shared_ptr<bool> guard = alive;
  const std::pair<double, double> near = host.latLon(cx, cz);
  const std::string lang = host.lang();
  const std::vector<OnlinePlace> found = co_await searchPhoton(q, near, 6, lang);
  std::vector<PlaceHit> out;
  if (!*guard) co_return out;
  const WorldMetaJson& m = host.world->json.meta;
  const std::array<double, 4> ownBox = {m.minX, m.minZ, m.maxX, m.maxZ};
  const std::string current = currentRegion();
  for (const OnlinePlace& p : found) {
    const auto [x, z] = latLonToWorld(p.lat, p.lon);
    PlaceHit h;
    h.street = "";
    h.x = x;
    h.z = z;
    h.exact = false;
    const std::string label = p.detail.empty() ? p.name : p.name + ", " + p.detail;
    // in the loaded map: the map goes there; in another region: that region; else only listed (greyed)
    const Region* in = nullptr;
    if (!boxContains(ownBox, x, z) && worldOn())
      for (const Region& r : worldLayer->regions())
        if (r.id != current && boxContains(r.box, x, z) && (!in || (r.box[2] - r.box[0]) * (r.box[3] - r.box[1]) < (in->box[2] - in->box[0]) * (in->box[3] - in->box[1]))) in = &r;
    if (boxContains(ownBox, x, z)) {
      h.what = "online";
      h.label = label;
    } else if (in) {
      h.what = "regionPlace";
      h.region = in->id;
      h.label = label + " · " + in->name;
    } else {
      h.what = "far";
      h.label = label + " · " + t("map2.outside");
    }
    out.push_back(std::move(h));
  }
  co_return out;
}

BigMap::Foreign* BigMap::foreignOf(const std::string& id) {
  for (const std::shared_ptr<Foreign>& f : foreign)
    if (f->id == id) return f.get();
  return nullptr;
}

void BigMap::drawForeign(const Ctx2D& g, const MapView& v, double dpr, const MapStyle& st, std::vector<LabelBox>& taken) {
  if (!worldOn()) return;
  const std::string current = currentRegion();
  const WorldMetaJson& m = host.world->json.meta;
  /** the area of a box's part in the rectangle */
  const auto overlap = [](double x0, double z0, double x1, double z1, double a0, double b0, double a1, double b1) {
    return std::max(0.0, std::min(x1, a1) - std::max(x0, a0)) * std::max(0.0, std::min(z1, b1) - std::max(z0, b0));
  };
  std::vector<MapOverview*> shown;
  std::string loading;
  // (where a region overlaps the loaded map, the loaded map's own roads are drawn: the other's are kept out of its box)
  g.save();
  g.beginPath();
  g.moveTo(0, 0);
  g.lineTo(v.width, 0);
  g.lineTo(v.width, v.height);
  g.lineTo(0, v.height);
  g.closePath();
  g.moveTo(v.sx(m.minX), v.sy(m.minZ));
  g.lineTo(v.sx(m.minX), v.sy(m.maxZ));
  g.lineTo(v.sx(m.maxX), v.sy(m.maxZ));
  g.lineTo(v.sx(m.maxX), v.sy(m.minZ));
  g.closePath();
  g.clip();
  for (const Region& r : worldLayer->regions()) {
    if (r.id == current) continue;
    // the part of its box in the picture
    const double x0 = std::max(v.wx(0), r.box[0]), x1 = std::min(v.wx(v.width), r.box[2]);
    const double z0 = std::max(v.wz(0), r.box[1]), z1 = std::min(v.wz(v.height), r.box[3]);
    if (x1 <= x0 || z1 <= z0) continue;
    Foreign* f = foreignOf(r.id);
    if (!f) {
      // fetched once it fills enough of the picture – beside the loaded map – and its main roads are drawn at that scale
      const double share = ((x1 - x0) * (z1 - z0) - overlap(x0, z0, x1, z1, m.minX, m.minZ, m.maxX, m.maxZ)) * v.scale * v.scale / std::max(1.0, v.width * v.height);
      if (share < 0.15 || v.scale < MapOverview::roadFrom(RoadKind::primary)) continue;
      const std::string base = host.regionFiles ? host.regionFiles(r.id) : r.id.empty() ? std::string() : std::string(MAP_ASSETS_URL) + REGIONS_DIR + "/" + r.id + "/";
      if (base.empty()) continue;
      // (the last few are kept: a region's network is some megabytes)
      if (foreign.size() >= 3) {
        size_t oldest = 0;
        for (size_t i = 1; i < foreign.size(); i++)
          if (foreign[i]->used < foreign[oldest]->used) oldest = i;
        foreign.erase(foreign.begin() + static_cast<long>(oldest));
      }
      auto nf = std::make_shared<Foreign>();
      nf->id = r.id;
      foreign.push_back(nf);
      loadForeign(nf, base).detach();
      f = nf.get();
    }
    f->used = plat::nowMs();
    if (f->loading) {
      loading = r.name;
      continue;
    }
    if (!f->ov) continue;
    f->ov->drawRoads(g, v, dpr, st);
    shown.push_back(f->ov.get());
  }
  for (MapOverview* ov : shown) ov->drawNames(g, v, st, taken);
  g.restore();
  if (!loading.empty()) {
    // a small note while a region's roads are on their way
    g.save();
    g.textAlign("center");
    g.textBaseline("middle");
    g.font("600 13px system-ui, sans-serif");
    g.lineJoin("round");
    g.lineWidth(4);
    g.strokeStyle(st.halo);
    const std::string text = t("map2.loading", {{"name", loading}});
    g.strokeText(text, v.width / 2, v.height - 70);
    g.fillStyle(st.bigName);
    g.fillText(text, v.width / 2, v.height - 70);
    g.restore();
  }
}

Task<void> BigMap::loadForeign(std::shared_ptr<Foreign> f, std::string base) {
  const std::shared_ptr<bool> guard = alive;
  Task<plat::Response> netAsked = plat::fetch(base + "net.bin.gz");
  plat::Response mapRes = co_await plat::fetch(base + "map.json");
  plat::Response netRes = co_await std::move(netAsked);
  const auto plain = [](Bytes b) {
    if (plat::isGzip(b)) {
      Bytes p = plat::gunzip(b);
      if (!p.empty()) return p;
    }
    return b;
  };
  std::shared_ptr<const NetData> net;
  if (netRes.ok()) {
    try {
      net = std::make_shared<const NetData>(unpackNet(plain(std::move(netRes.body))));
    } catch (const std::exception& e) {
      plat::warn(std::string("[map] the roads of ") + f->id + " could not be read: " + e.what());
    }
  }
  std::vector<PlaceJson> places;
  if (mapRes.ok()) {
    const Bytes b = plain(std::move(mapRes.body));
    Json j;
    if (Json::parse(std::string(b.begin(), b.end()), j) && j.at("places").isArray())
      for (const Json& p : j.at("places").array) {
        if (!p.at("name").isString() || !p.at("x").isNumber() || !p.at("z").isNumber()) continue;
        places.push_back({p.at("name").str, p.at("kind").isString() ? p.at("kind").str : "", p.at("x").number, p.at("z").number});
      }
  }
  f->loading = false;
  f->failed = !net;
  if (net || !places.empty()) f->ov = std::make_unique<MapOverview>(net, std::move(places));
  if (*guard && node) redraw();
}

bool BigMap::foreignTap(double px, double py) {
  if (!worldOn() || scale < MapOverview::roadFrom(RoadKind::secondary)) return false;
  const MapView v = view();
  const double x = v.wx(px), z = v.wz(py);
  const std::string current = currentRegion();
  for (const std::shared_ptr<Foreign>& f : foreign) {
    if (!f->ov || !f->ov->net() || f->id == current) continue;
    const Region* r = worldLayer->byId(f->id);
    if (!r || !boxContains(r->box, x, z)) continue;
    // (within a finger's reach of a road)
    const std::optional<OverviewSpot> at = f->ov->nearest(x, z, 28 / scale);
    if (!at) continue;
    showForeignSpot(*r, *at);
    return true;
  }
  return false;
}

void BigMap::showForeignSpot(const Region& r, const OverviewSpot& at) {
  if (chosen) {
    chosen.reset();
    fillRegion();
  }
  if (transit) transit->closePopup();
  Foreign* f = foreignOf(r.id);
  const PlaceJson* town = f && f->ov ? f->ov->nearestPlace(at.x, at.z, 6000) : nullptr;
  SpawnJson s;
  s.name = town ? town->name : "";
  s.x = at.x;
  s.z = at.z;
  s.yaw = at.yaw;
  s.kind = "street";
  // (taken from the overview: put into a lane of that road once the region's roads are loaded there)
  s.rough = true;
  spot = s;
  foreignSpot = r.id;
  const El bar = spotBar;
  bar.replaceChildren();
  bar.hidden(false);
  El closeBtn = el("button", "btn ghost bm-spot-close", "✕");
  closeBtn.type("button");
  closeBtn.title(t("realityClose"));
  closeBtn.setAttribute("aria-label", t("realityClose"));
  closeBtn.onclick([this, bar] {
    spot.reset();
    foreignSpot.reset();
    bar.hidden(true);
    redraw();
  });
  const auto [lat, lon] = host.latLon(s.x, s.z);
  El text = el("div");
  text.append(el("b", "", !s.name.empty() ? s.name : t("mapPlace")), el("small", "", t("map2.regionPlace", {{"name", r.name}}) + " · " + toFixed(lat, 5) + ", " + toFixed(lon, 5)));
  bar.append(closeBtn, text);
  // "Put the car here": that region is loaded and the drive begins there (no autopilot: it cannot drive across maps)
  if (host.startInRegion && (host.canTeleport() || (host.menu && host.menu()))) {
    El go = el("button", "btn", t("mapTeleport"));
    go.type("button");
    const Region region = r;
    go.onclick([this, region, s] {
      // (copies: the game reloads, and takes this map away with it)
      const std::function<void(const Region&, const SpawnJson&)> startIt = host.startInRegion;
      startIt(region, s);
    });
    bar.append(go);
  }
  redraw();
}

}  // namespace fs
