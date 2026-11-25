// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/serviceworld.ts @ v1.7.7
#include "game/serviceworld.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "core/random.h"
#include "game/vehicles.h"
#include "game/weather.h"
#include "i18n/index.h"
#include "platform/intl.h"
#include "platform/plat.h"
#include "render/car.h"
#include "render/fleet.h"
#include "render/semitruck.h"
#include "render/tiles.h"
#include "render/truck.h"
#include "sim/collision.h"
#include "sim/payload.h"
#include "sim/vehicle/vehicle.h"
#include "ui/hud.h"
#include "world/loader.h"
#include "world/tilefmt.h"

namespace fs {

using svc::FUELS;
using svc::GANTRY;
using svc::ServiceSite;

namespace {

/** Where a vehicle's filler is (model frame: x to the right, y up, z back), on its right side – the left one mirrors it. */
const std::array<double, 3>& fillerOf(const std::string& kind) {
  static const auto* f = new std::map<std::string, std::array<double, 3>>{
      {"car", {0.88, 0.86, 1.02}},    {"truck", {1.24, 0.95, -0.4}},  {"truckLight", {1.18, 0.9, -0.3}},    {"bus", {1.28, 0.8, -2.5}},
      {"minibus", {1.1, 0.75, 1.4}}, {"tractor", {0.95, 1.55, 0.3}}, {"motorcycle", {0.16, 1.02, -0.32}},
  };
  auto it = f->find(kind);
  return it != f->end() ? it->second : f->at("car");
}

/** How tall a vehicle is (m): the highest corner of its body (a car's when it says none). */
double heightOf(const VehicleSpec& spec) {
  double h = 1.45;
  if (spec.bodyPoints) {
    h = 0;
    for (const auto& p : *spec.bodyPoints) h = std::max(h, p[1]);
  }
  return h;
}

/** A number as the language writes it, with `digits` decimals. */
std::string num(double v, int digits, const std::string& lang) {
  return plat::formatNumber(v, localeOf(lang), {.minimumFractionDigits = digits, .maximumFractionDigits = digits, .numberingSystem = "latn"});
}

struct Roof {
  const std::vector<double>* ring;
  double top;
};

// ------------------------------------------------------------------ taking the canopy's block out of a tile

/**
 * Take the blocks the map drew for these roofs out of a tile: the triangles
 * of its buildings under each roof (its top inside the outline, its walls on
 * the outline as high as the roof), the outlines of the buildings for the
 * collisions whose middle lies under a roof, and the trees under the roofs.
 * (The original wraps the tile's data; here its sections are changed in place –
 * the hub reads the outlines and trees from them after this.)
 */
void clearRoofs(LoadedTile& tile, const std::vector<Roof>& roofs) {
  const TileHeader& h = tile.header;
  // the box round the roofs: most triangles are thrown out by it
  double x0 = JS_INF;
  double z0 = JS_INF;
  double x1 = -JS_INF;
  double z1 = -JS_INF;
  for (const Roof& r : roofs) {
    for (size_t i = 0; i + 1 < r.ring->size(); i += 2) {
      x0 = std::min(x0, (*r.ring)[i]);
      x1 = std::max(x1, (*r.ring)[i]);
      z0 = std::min(z0, (*r.ring)[i + 1]);
      z1 = std::max(z1, (*r.ring)[i + 1]);
    }
  }
  x0 -= 0.5;
  z0 -= 0.5;
  x1 += 0.5;
  z1 += 0.5;
  Mesh* mesh = nullptr;
  for (const ObjectPtr& o : tile.group->children)
    if (o->name == "bldg" && o->type == ObjectType::Mesh) {
      mesh = static_cast<Mesh*>(o.get());
      break;
    }
  const AttrPtr index = mesh && mesh->geometry ? mesh->geometry->index : nullptr;
  if (mesh && index) {
    const AttrPtr pos = mesh->geometry->getAttribute("position");
    std::vector<uint32_t> keep;
    std::array<double, 3> xs{};
    std::array<double, 3> zs{};
    std::array<double, 3> ys{};
    int dropped = 0;
    const size_t count = index->count();
    for (size_t t = 0; t + 2 < count; t += 3) {
      bool inBox = true;
      for (size_t k = 0; k < 3; k++) {
        const size_t vtx = static_cast<size_t>(index->getX(t + k));
        xs[k] = h.cx + pos->getX(vtx) * POS_XZ_SCALE;
        ys[k] = h.yBase + pos->getY(vtx) * POS_Y_SCALE;
        zs[k] = h.cz + pos->getZ(vtx) * POS_XZ_SCALE;
        if (xs[k] < x0 || xs[k] > x1 || zs[k] < z0 || zs[k] > z1) inBox = false;
      }
      bool drop = false;
      if (inBox) {
        const double cx = (xs[0] + xs[1] + xs[2]) / 3;
        const double cz = (zs[0] + zs[1] + zs[2]) / 3;
        const double top = std::max({ys[0], ys[1], ys[2]});
        for (const Roof& r : roofs) {
          if (svc::inRing(cx, cz, *r.ring) && svc::ringDistance(cx, cz, *r.ring) > 0.12) drop = true;
          else if (svc::ringDistance(xs[0], zs[0], *r.ring) < 0.25 && svc::ringDistance(xs[1], zs[1], *r.ring) < 0.25 && svc::ringDistance(xs[2], zs[2], *r.ring) < 0.25 &&
                   (!std::isfinite(r.top) || std::abs(top - r.top) < 0.5))
            drop = true;
          if (drop) break;
        }
      }
      if (drop) dropped++;
      else {
        for (size_t k = 0; k < 3; k++) keep.push_back(static_cast<uint32_t>(index->getX(t + k)));
      }
    }
    if (dropped > 0) {
      if (index->type == AttrType::U32) mesh->geometry->setIndex(attr(std::move(keep), 1));
      else mesh->geometry->setIndex(attr(std::vector<uint16_t>(keep.begin(), keep.end()), 1));
    }
  }
  // the outlines and the trees, as the hub reads them from the tile
  const auto under = [&roofs](double x, double z, double pad) {
    for (const Roof& r : roofs)
      if (svc::inRing(x, z, *r.ring) || svc::ringDistance(x, z, *r.ring) < pad) return true;
    return false;
  };
  SectionArray* pts = tile.data.get("collPts");
  SectionArray* cnt = tile.data.get("collCnt");
  SectionArray* skip = tile.data.get("collSkip");
  if (pts && cnt) {
    std::vector<float> outPts;
    std::vector<uint16_t> outCnt;
    std::vector<bool> sides;
    size_t o = 0;
    size_t side = 0;
    for (const uint16_t n : cnt->u16) {
      double mx = 0;
      double mz = 0;
      for (size_t i = 0; i < n; i++) {
        mx += pts->f32[o + i * 2];
        mz += pts->f32[o + i * 2 + 1];
      }
      const bool away = n > 0 && under(mx / n, mz / n, 0.3);
      if (!away) {
        outCnt.push_back(n);
        for (size_t i = 0; i < static_cast<size_t>(n) * 2; i++) outPts.push_back(pts->f32[o + i]);
        for (size_t i = 0; i < n; i++) sides.push_back(skip ? ((side + i) >> 3) < skip->u8.size() && (skip->u8[(side + i) >> 3] & (1 << ((side + i) & 7))) != 0 : false);
      }
      o += static_cast<size_t>(n) * 2;
      side += n;
    }
    pts->f32 = std::move(outPts);
    cnt->u16 = std::move(outCnt);
    if (skip) {
      std::vector<uint8_t> outSkip((sides.size() + 7) / 8, 0);
      for (size_t i = 0; i < sides.size(); i++)
        if (sides[i]) outSkip[i >> 3] |= static_cast<uint8_t>(1 << (i & 7));
      skip->u8 = std::move(outSkip);
    }
  }
  if (SectionArray* t = tile.data.get("trees")) {
    std::vector<float> out;
    for (size_t i = 0; i + 4 < t->f32.size(); i += 5)
      if (!under(t->f32[i], t->f32[i + 2], 1))
        for (size_t k = 0; k < 5; k++) out.push_back(t->f32[i + k]);
    t->f32 = std::move(out);
  }
}

/** The box body of a truck's model (render/boxbody.h), if it has one. */
BoxBody* bodyOf(VehicleModel& model) {
  if (auto* t = dynamic_cast<PlayerTruck*>(&model)) return t->body.get();
  if (auto* r = dynamic_cast<PlayerRigid*>(&model)) return r->body.get();
  return nullptr;
}

}  // namespace

ServiceWorld::ServiceWorld(ServiceHost host) : host_(std::move(host)) {
  group->name = "fuel stations and car washes";
  group->add(fillView_.group, spray_.points);
  host_.scene->add(group);
}

ServiceWorld::~ServiceWorld() {
  for (auto& [i, v] : views_) v->dispose();
  if (depot_) depot_->s->dispose();
  if (scene_) scene_->box.remove();
  group->removeFromParent();
}

const std::vector<ServiceSite>& ServiceWorld::sites() const {
  static const auto* none = new std::vector<ServiceSite>();
  return index_ ? index_->sites : *none;
}

Task<void> ServiceWorld::load() {
  std::weak_ptr<bool> life = life_;
  std::shared_ptr<MapLoader> map = host_.map;
  if (!map) co_return;
  std::optional<Json> json;
  try {
    json = co_await map->json<Json>("services.json.gz");
  } catch (const std::exception& e) {
    plat::warn(std::string("the fuel stations and car washes could not be read: ") + e.what());
    co_return;
  }
  if (life.expired() || !json) co_return;
  try {
    index_ = std::make_unique<svc::ServiceIndex>(svc::parseServices(svc::servicesFromJson(*json)));
  } catch (const std::exception& e) {
    plat::warn(std::string("the fuel stations and car washes could not be read: ") + e.what());
  }
}

// ---------------------------------------------------------------- the tiles of the map

void ServiceWorld::queueAdd(const ServiceSite* s) {
  if (std::find(queue_.begin(), queue_.end(), s) == queue_.end()) queue_.push_back(s);
}

void ServiceWorld::tile(LoadedTile& tile, bool loaded) {
  if (!index_) return;
  const TileHeader& h = tile.header;
  const double half = h.size / 2;
  const auto own = [&](const ServiceSite& s) { return std::abs(s.x - h.cx) <= half && std::abs(s.z - h.cz) < half && !(s.x == h.cx + half || s.z == h.cz + half); };
  if (!loaded) {
    std::vector<int> gone;
    for (const auto& [i, v] : views_)
      if (own(v->site)) gone.push_back(i);
    for (int i : gone) drop(i);
    std::erase_if(queue_, [&](const ServiceSite* s) { return own(*s); });
    return;
  }
  std::vector<Roof> roofs;
  for (const ServiceSite* s : index_->near(h.cx, h.cz, half + 30)) {
    if (own(*s) && (s->k == "fuel" ? !s->canopies.empty() : s->wash.has_value())) queueAdd(s);
    for (const svc::RoofJson& r : s->roofs) {
      if (r.src != "lod2") continue;
      // (only roofs that reach into this tile)
      bool inside = false;
      for (size_t i = 0; i + 1 < r.ring.size() && !inside; i += 2) inside = std::abs(r.ring[i] - h.cx) < half + 2 && std::abs(r.ring[i + 1] - h.cz) < half + 2;
      if (inside) roofs.push_back({&r.ring, r.y1.value_or(JS_NAN)});
    }
  }
  if (!roofs.empty()) clearRoofs(tile, roofs);
}

void ServiceWorld::drop(int i) {
  auto it = views_.find(i);
  if (it == views_.end()) return;
  SiteView& v = *it->second;
  if (doing_ && doing_->kind != "help" && doing_->site == &v.site) stopAll();
  v.dispose();
  host_.collider->remove("svc:" + std::to_string(i));
  views_.erase(it);
}

// ---------------------------------------------------------------- a drive

VehicleSpec ServiceWorld::begin(const VehicleDef& def, const VehicleSpec& spec, bool free) {
  end();
  def_ = &def;
  height_ = heightOf(spec);
  const TankSpec* ts = tankFor(def.kind);
  // (a free drive may begin with the reserve; a lesson, a test or a demonstration with a good part of the tank)
  if (ts) {
    double share;
    if (free) {
      const double a = rnd();
      const double b = rnd();
      share = startShare(a, b);
    } else share = 0.6 + rnd() * 0.35;
    tank = std::make_unique<FuelTank>(*ts, share);
  } else tank = nullptr;
  dirt = (def.model == "car" ? 0.03 : 0.1) + rnd() * 0.35;
  lowWarned_ = false;
  emptyWarned_ = false;
  goods_ = std::nullopt;
  const auto& kinds = LOADED_KINDS();
  if (free && std::find(kinds.begin(), kinds.end(), def.kind) != kinds.end()) {
    const LoadPlan plan = planLoad(spec.wheelbase, spec.width.value_or(2.5), startsWith(def.kind, "light") ? 1600 : 2400, [] { return rnd(); });
    goods_ = Goods{plan.pallets, plan.payload.mass};
    return withPayload(spec, &plan.payload);
  }
  return spec;
}

bool ServiceWorld::startScene(Vehicle& v, VehicleModel& model, std::function<void()> done) {
  BoxBody* body = bodyOf(model);
  const VehicleDef* def = def_;
  if (!goods_ || !body || !def) return false;
  v.getModelPosition(p_.data());
  const std::optional<DepotSpot> spot = findDepot(p_[0], p_[2], v.yaw, def->outline.front, def->outline.rear,
                                                  {.road = [this](double x, double z) { return host_.onRoad(x, z); },
                                                   .blocked = [this](double x, double z, double yaw) { return host_.blocked(x, z, yaw); },
                                                   .ground = [this](double x, double z) { return host_.ground(x, z); }});
  ScenePose pose{p_[0], p_[1], p_[2], v.yaw, false};
  if (spot) {
    const double y = host_.ground(spot->x, spot->z);
    v.teleport(spot->x, y + 0.02, spot->z, spot->yaw);
    pose = {spot->x, y, spot->z, spot->yaw, true};
  }
  std::vector<PalletLoad> loads;
  for (const PalletPlan& p : goods_->pallets) loads.push_back({p.height, p.kind, p.mass});
  auto s = std::make_shared<LoadingScene>(pose, *body, std::move(loads));
  group->add(s->group);
  if (pose.dock) {
    // the warehouse stands in the way like any building
    const double c = std::cos(pose.yaw);
    const double sn = std::sin(pose.yaw);
    std::vector<float> ring;
    for (const auto& [lx, lz] : {std::pair{-7.0, s->wallZ}, std::pair{7.0, s->wallZ}, std::pair{7.0, s->wallZ + 11}, std::pair{-7.0, s->wallZ + 11}}) {
      ring.push_back(static_cast<float>(pose.x + lx * c + lz * sn));
      ring.push_back(static_cast<float>(pose.z - lx * sn + lz * c));
    }
    const std::vector<uint16_t> counts = {4};
    host_.collider->add({.key = "svc:depot", .rings = &ring, .ringCounts = &counts, .segments = nullptr, .discs = {}});
  }
  // what is being done, and a button to skip it
  const dom::El box = dom::el("div", "svc-scene");
  const dom::El cap = dom::el("div", "svc-cap");
  const dom::El skip = dom::el("button", "svc-skip");
  skip.type("button");
  skip.textContent(tr("load.skip", host_.lang()) + " ⏭");
  std::weak_ptr<LoadingScene> ws = s;
  skip.onclick([ws](dom::Event& e) {
    e.stopPropagation();
    if (auto sc = ws.lock()) sc->skip();
  });
  box.append(cap, skip);
  host_.ui.append(box);
  host_.hideOverlay();
  scene_ = std::make_unique<Scene>(Scene{s, std::move(done), box, cap, ""});
  depot_ = Depot{s, pose.x, pose.z};
  caption();
  return true;
}

void ServiceWorld::caption() {
  Scene* sc = scene_.get();
  if (!sc) return;
  const std::string lang = host_.lang();
  const LoadingScene::Progress pr = sc->s->progress();
  const std::string text = "<b>" + tr("load.title", lang) + "</b><small>" + tr("load.progress", lang, {{"n", pr.loaded}, {"total", pr.total}, {"kg", num(pr.mass, 0, lang)}}) + "</small>";
  if (sc->shown != text) {
    sc->shown = text;
    sc->caption.innerHTML(text);
  }
}

void ServiceWorld::finishScene() {
  if (!scene_) return;
  std::unique_ptr<Scene> sc = std::move(scene_);
  sc->box.remove();
  const std::string lang = host_.lang();
  const LoadingScene::Progress pr = sc->s->progress();
  host_.message(tr("load.done", lang, {{"n", pr.total}, {"kg", num(goods_ ? goods_->mass : pr.mass, 0, lang)}}), tr("load.doneSub", lang));
  if (sc->done) sc->done();
}

void ServiceWorld::drive(double dt, Vehicle& v, DriverInput& d, const VehicleDef& def, bool walking, bool task, bool autoDrive) {
  FuelTank* tk = tank.get();
  const std::string lang = host_.lang();
  v.getModelPosition(p_.data());
  const double mid = (def.outline.rear - def.outline.front) / 2;
  const double x = p_[0] + std::sin(v.yaw) * mid;
  const double z = p_[2] + std::cos(v.yaw) * mid;
  standing_ = std::abs(v.speed) < 0.25;
  engineOn_ = v.engineRunning;
  // ---- the fuel
  if (tk && !autoDrive) {
    tk->burn(fuelFlow(v.spec, tk->spec, v.rpm, v.engineLoad, v.engineRunning), dt, std::abs(v.speed) * dt);
    if (tk->empty()) {
      // the engine stops and cannot be started
      d.ignition = false;
      d.starter = false;
      if (!emptyWarned_) {
        emptyWarned_ = true;
        host_.message(tr("svc.empty", lang), nearestText(x, z, v.yaw));
      }
    } else emptyWarned_ = false;
    if (tk->low() && !tk->empty() && !lowWarned_) {
      lowWarned_ = true;
      host_.message(tr("svc.reserve", lang, {{"l", num(tk->litres, 0, lang)}}), nearestText(x, z, v.yaw));
    } else if (!tk->low()) lowWarned_ = false;
  }
  // ---- the dirt: with the distance, more on a wet road, on snow, off the road
  double off = 0;
  for (const WheelState& w : v.wheels)
    if (w.contact && w.grip < 0.62) off += 0.25;
  const WeatherState& weather = weatherNow();
  if (!doing_ || doing_->kind != "wash") dirt = std::min(1.0, dirt + dirtGrowth(std::abs(v.speed) * dt, weather.wet, off, snowOnGround()));
  // ---- where the vehicle is
  here_ = whereIs(x, z, v, def, walking);
  if (!task && here_.onSite != yardSet_) {
    yardSet_ = here_.onSite;
    if (host_.yard) *host_.yard = yardSet_;
  }
  // ---- what is going on
  const StandState st{standing_, engineOn_};
  Activity* a = doing_.get();
  if (a && a->kind == "fill" && tk) {
    const std::string before = a->r->phase;
    a->r->step(dt, *tk, st);
    if (!a->noted && (before == "run" || before == "out") && a->r->phase == "back") {
      a->noted = true;
      const FillUp& f = a->r->fill;
      if (a->r->reason == "engine") host_.message(tr("svc.engineStarted", lang), "");
      else if (a->r->reason == "moved") host_.message(tr("svc.moved", lang), "");
      if (f.litres > 0.05) {
        auto it = FUELS->find(a->r->fuel);
        host_.message(tr("svc.paid", lang, {{"l", num(f.litres, 2, lang)}, {"fuel", it != FUELS->end() ? it->second.label : a->r->fuel}, {"cost", num(f.cost(), 2, lang)}}),
                      tr("svc.paidSub", lang, {{"pct", jsRound(tk->share() * 100)}}));
      }
      host_.click("chime");
    }
    if (a->r->done()) {
      doing_ = nullptr;
      fillShown_ = false;
      fillView_.hide();
    }
  } else if (a && a->kind == "wash") {
    a->w->update(dt, st);
    dirt = std::min(dirt, a->w->dirt());
    if (a->w->done()) {
      const bool aborted = a->w->aborted;
      doing_ = nullptr;
      if (aborted) host_.message(tr("svc.washAbort", lang), "");
      else {
        dirt = 0;
        host_.message(tr("svc.washDone", lang), tr("svc.washDoneSub", lang));
        host_.click("chime");
      }
    }
  } else if (a && a->kind == "help" && tk) {
    a->t += dt;
    if (a->t >= HELP.time) {
      doing_ = nullptr;
      tk->fill(HELP.litres);
      emptyWarned_ = false;
      host_.message(tr("svc.helpDone", lang, {{"l", HELP.litres}}), nearestText(x, z, v.yaw));
      host_.click("chime");
    }
  }
}

ServiceWorld::Here ServiceWorld::whereIs(double x, double z, Vehicle& v, const VehicleDef& def, bool walking) {
  Here here;
  if (depot_ && std::hypot(depot_->x - x, depot_->z - z) < 22) here.onSite = true;
  if (!index_) return here;
  const svc::Pose pose{x, z, v.yaw, v.spec.length.value_or(4.28), v.spec.width.value_or(1.79)};
  FuelTank* tk = tank.get();
  for (const ServiceSite* sp : index_->near(x, z, 30)) {
    const ServiceSite& s = *sp;
    if (!svc::onSite(s, x, z, 4)) continue;
    here.onSite = true;
    if (walking) continue;
    if (s.k == "fuel" && tk && !here.pump) {
      for (size_t c = 0; c < s.canopies.size(); c++) {
        const svc::CanopyPlan& plan = s.canopies[c];
        const std::optional<svc::AtPump> at = svc::pumpBeside(plan, pose);
        if (!at) continue;
        // which fuel: the vehicle's kind – petrol: E10 first, diesel: the lorry pump's for a heavy one
        const bool diesel = tk->spec.fuel == "diesel";
        const auto sells = [&s](const std::string& f) { return std::find(s.sells.begin(), s.sells.end(), f) != s.sells.end(); };
        const std::vector<std::string> want =
            diesel ? (def.heavy && sells("hgv") ? std::vector<std::string>{"hgv", "diesel"} : std::vector<std::string>{"diesel", "hgv"}) : std::vector<std::string>{"e10", "e5", "plus"};
        std::string fuel;
        for (const std::string& f : want)
          if (sells(f)) {
            fuel = f;
            break;
          }
        // (the room under the slab, as render/services.h builds it)
        const double clear = (std::isfinite(plan.y1) && std::isfinite(plan.y0) ? std::max(plan.y1 - plan.y0, 4.2) : std::isfinite(plan.h) ? std::max(4.2, plan.h) : 5.2) - 0.7 - height_;
        const std::string why = fuel.empty() ? "noFuel" : clear < 0.15 ? "tooHigh" : tk->share() > 0.97 ? "full" : !standing_ ? "moving" : engineOn_ ? "engine" : "";
        here.pump = Here::Pump{&s, static_cast<int>(c), at->pump, at->side, !fuel.empty() ? fuel : (diesel ? "diesel" : "e10"), why};
        break;
      }
    } else if (s.k == "wash" && s.wash && !here.bay) {
      const svc::InBay b = svc::inBay(*s.wash, pose);
      if (!b.inside) continue;
      const bool car = def.model == "car" && !def.trailer;
      const std::string why = !car || height_ > GANTRY.height - 0.15 || pose.width > GANTRY.width - 0.3 ? "carsOnly"
                              : !b.straight || std::abs(b.across) > 0.5                              ? "straight"
                              : b.along < -0.6                                                       ? "forward"
                              : b.along > 0.6                                                        ? "back"
                              : !standing_                                                           ? "moving"
                              : engineOn_                                                            ? "engine"
                                                                                                     : "";
      here.bay = Here::Bay{&s, b.dir, b.along, why};
    }
  }
  return here;
}

std::string ServiceWorld::nearestText(double x, double z, double yaw) const {
  const std::string lang = host_.lang();
  if (!index_) return "";
  const auto n = index_->nearest(x, z, "fuel");
  if (!n) return "";
  const std::string d = n->dist < 1000 ? formatUnit(jsRound(n->dist / 10) * 10, Unit::Meter, lang) : formatUnit(n->dist / 1000, Unit::Kilometer, lang, 1);
  const std::string name = !n->site->name.empty() ? n->site->name : !n->site->brand.empty() ? n->site->brand : tr("svc.station", lang);
  return tr("svc.nearest", lang, {{"name", name}, {"d", d}, {"dir", tr("svc.dir" + std::to_string(bearingSector(x, z, yaw, n->site->x, n->site->z)), lang)}});
}

// ---------------------------------------------------------------- the button

void ServiceWorld::act(const std::string& state) {
  if (scene_) {
    scene_->s->skip();
    return;
  }
  if (state != "drive") return;
  Activity* a = doing_.get();
  if (a && a->kind == "fill") {
    a->r->stop("stopped");
    return;
  }
  if (a) return;
  const Here& h = here_;
  FuelTank* tk = tank.get();
  if (h.pump && h.pump->why.empty() && tk) {
    const Here::Pump& p = *h.pump;
    const double price = svc::priceAt(*p.site, p.fuel);
    auto act = std::make_unique<Activity>();
    act->kind = "fill";
    act->r = std::make_unique<Refuelling>(p.fuel, price, tk->spec.flow);
    act->site = p.site;
    act->canopy = p.canopy;
    act->pump = p.pump;
    act->side = p.side;
    doing_ = std::move(act);
    host_.click("door");
  } else if (h.bay && h.bay->why.empty()) {
    auto act = std::make_unique<Activity>();
    act->kind = "wash";
    act->w = std::make_unique<Washing>(dirt);
    act->site = h.bay->site;
    act->dir = h.bay->dir;
    doing_ = std::move(act);
    host_.click("tick");
    host_.message(tr("svc.washPaid", host_.lang(), {{"cost", num(svc::WASH_PRICE, 2, host_.lang())}}), "");
  } else if (tk && tk->empty() && standing_) {
    auto act = std::make_unique<Activity>();
    act->kind = "help";
    doing_ = std::move(act);
    host_.click("tick");
  }
}

// ---------------------------------------------------------------- every frame: what is drawn

void ServiceWorld::visual(CarVisualState& s, VehicleModel& model, double dt) {
  // (the road vehicles: those with a tank – not the tram, the aeroplane, nor somebody on foot)
  if (tank) {
    s.fuel = tank->share();
    auto* car = dynamic_cast<PlayerCar*>(&model);
    dirtLook_.apply(car ? static_cast<Object3D&>(*car->exterior.group) : static_cast<Object3D&>(*model.root));
  }
  dirtLook_.update(tank && dirtShown ? dirt : 0, *model.root, height_);
  const double px = model.root->position.x;
  const double pz = model.root->position.z;
  buildNext(px, pz);
  // the nozzle in the filler and the pump's counter
  Activity* a = doing_.get();
  if (a && a->kind == "fill") {
    auto vit = views_.find(a->site->index);
    SiteView* view = vit != views_.end() ? vit->second.get() : nullptr;
    const int pi = pumpIndex(*a->site, a->canopy, a->pump);
    PumpView* pump = view && pi < static_cast<int>(view->pumps.size()) ? &view->pumps[static_cast<size_t>(pi)] : nullptr;
    const std::array<double, 3>& f = fillerOf(def_ ? def_->model : "car");
    if (view && pump) {
      // the filler on the side the pump is on
      Vector3 filler(f[0] * (a->side > 0 ? 1 : -1), f[1], f[2]);
      model.root->localToWorld(filler);
      // (which face of the pump looks at it)
      const double face = (filler.x - pump->x) * -pump->uz + (filler.z - pump->z) * pump->ux >= 0 ? 1 : -1;
      if (a->r->phase != "run" || !fillShown_) fillView_.show(*pump, face, filler, a->r->nozzle(), a->r->fuel);
      fillShown_ = a->r->phase == "run";
      fillView_.count(a->r->fill.litres, a->r->fill.cost());
    }
  }
  // the gantry: at its place, the brushes turning, foam and water
  for (auto& [i, view] : views_) {
    if (!view->gantry) continue;
    Activity* w = a && a->kind == "wash" && a->site == &view->site ? a : nullptr;
    const std::optional<Here::Bay>& bay = here_.bay && here_.bay->site == &view->site ? here_.bay : std::nullopt;
    const double face = w ? w->dir : (bay ? bay->dir : 1);
    if (w) {
      const std::string step = w->w->stepId();
      const double at = w->w->at() * face;
      const bool brushing = step == "brush" || step == "foam";
      view->placeGantry(at, face, brushing ? 1 : 0, step == "brush" ? height_ + 0.38 : 0, step == "brush" ? dt * 9 : step == "dry" ? 0 : dt * 2, -1);
      Vector3 g;
      view->gantryAt(g);
      const svc::Rect& r = view->gantry->rect;
      spray_.update(dt, g.x, g.y, g.z, -r.uz, r.ux, step == "foam" ? 110 : step == "rinse" || step == "brush" ? 160 : 0, step == "foam");
    } else {
      view->placeGantry(face, face, 0, 0, 0, bay ? (bay->why == "forward" || bay->why == "straight" ? 1 : -1) : 0);
    }
  }
  if (!a || a->kind != "wash") spray_.stop();
  // the loading of the truck
  if (Scene* sc = scene_.get()) {
    sc->s->update(dt);
    caption();
    if (sc->s->done()) finishScene();
  } else if (depot_) {
    depot_->s->update(dt);
    // (left standing once the truck is away; gone far behind)
    if (std::hypot(depot_->x - px, depot_->z - pz) > 450) dropDepot();
  }
}

int ServiceWorld::pumpIndex(const ServiceSite& site, int canopy, int pump) const {
  int k = 0;
  for (int c = 0; c < canopy; c++) k += static_cast<int>(site.canopies[static_cast<size_t>(c)].islands.size());
  return k + pump;
}

void ServiceWorld::buildNext(double px, double pz) {
  const ServiceSite* best = nullptr;
  double bestD = JS_INF;
  for (const ServiceSite* s : queue_) {
    const double d = std::hypot(s->x - px, s->z - pz);
    if (d < bestD) {
      bestD = d;
      best = s;
    }
  }
  if (!best) return;
  std::erase(queue_, best);
  if (views_.count(best->index)) return;
  const double g0 = host_.ground(best->x, best->z);
  if (std::isnan(g0)) return;
  auto view = std::make_unique<SiteView>(
      *best,
      [this, g0](double x, double z) {
        const double g = host_.ground(x, z);
        return std::isnan(g) ? g0 : g;
      },
      host_.shadows());
  group->add(view->group);
  std::vector<uint16_t> counts;
  std::vector<float> pts;
  for (const auto& r : view->rings) {
    counts.push_back(static_cast<uint16_t>(r.size() / 2));
    for (double v : r) pts.push_back(static_cast<float>(v));
  }
  const CollisionSource src{.key = "svc:" + std::to_string(best->index), .rings = pts.empty() ? nullptr : &pts, .ringCounts = counts.empty() ? nullptr : &counts, .segments = nullptr, .discs = view->discs};
  host_.collider->add(src);
  views_[best->index] = std::move(view);
}

void ServiceWorld::hud(HudState& hs, bool touch) {
  FuelTank* tk = tank.get();
  if (tk) {
    hs.fuel = tk->share();
    hs.fuelLow = tk->low();
  }
  const std::string lang = host_.lang();
  const auto say = [&](const std::string& key, const TrParams& params = {}, const std::string& action = "Digit9", bool touchKey = true) {
    hs.tip = tr(touch && touchKey ? key + "Touch" : key, lang, params);
    hs.tipAction = action;
  };
  Activity* a = doing_.get();
  if (a && a->kind == "fill") {
    const FillUp& f = a->r->fill;
    if (a->r->phase == "back") say("svc.fillEnd", {{"l", num(f.litres, 2, lang)}, {"cost", num(f.cost(), 2, lang)}}, "", false);
    else say("svc.filling", {{"l", num(f.litres, 2, lang)}, {"cost", num(f.cost(), 2, lang)}});
    return;
  }
  if (a && a->kind == "wash") {
    const std::string id = a->w->stepId();
    say("svc.washing", {{"step", tr("svc.step." + (!id.empty() ? id : std::string("dry")), lang)}, {"pct", jsRound(a->w->share() * 100)}}, "", false);
    return;
  }
  if (a && a->kind == "help") {
    say("svc.helpComing", {{"s", std::max(1.0, std::ceil(HELP.time - a->t))}}, "", false);
    return;
  }
  const Here& h = here_;
  if (h.pump && standing_) {
    const Here::Pump& p = *h.pump;
    auto it = FUELS->find(p.fuel);
    const std::string fuel = it != FUELS->end() ? it->second.label : p.fuel;
    if (p.why == "noFuel") say("svc.noFuel", {{"fuel", tr(tk && tk->spec.fuel == "diesel" ? "svc.diesel" : "svc.petrol", lang)}}, "", false);
    else if (p.why == "tooHigh") say("svc.tooHigh", {}, "", false);
    else if (p.why == "full") say("svc.tankFull", {}, "", false);
    else if (p.why == "engine") say("svc.engineOff", {}, "KeyI");
    else if (p.why.empty()) say("svc.refuel", {{"fuel", fuel}, {"price", num(svc::priceAt(*p.site, p.fuel), 3, lang)}});
    return;
  }
  if (h.bay) {
    const Here::Bay& b = *h.bay;
    if (b.why == "carsOnly") say("svc.washCarsOnly", {}, "", false);
    else if (b.why == "straight") say("svc.washStraight", {}, "", false);
    else if (b.why == "forward") say("svc.washForward", {{"d", num(-b.along, 1, lang)}}, "", false);
    else if (b.why == "back") say("svc.washBack", {{"d", num(b.along, 1, lang)}}, "", false);
    else if (b.why == "engine") say("svc.washEngineOff", {}, "KeyI");
    else if (b.why.empty()) say("svc.wash", {{"cost", num(svc::WASH_PRICE, 2, lang)}});
    return;
  }
  if (tk && tk->empty() && standing_) say("svc.emptyTip", {{"l", HELP.litres}});
}

void ServiceWorld::camera(Camera& cam) {
  if (scene_) scene_->s->camera(cam, [this](double x, double z) { return host_.ground(x, z); });
}

void ServiceWorld::stopAll() {
  doing_ = nullptr;
  fillShown_ = false;
  fillView_.hide();
  spray_.stop();
}

void ServiceWorld::dropDepot() {
  if (!depot_) return;
  if (scene_ && scene_->s == depot_->s) {
    scene_->box.remove();
    scene_ = nullptr;
  }
  depot_->s->dispose();
  depot_ = std::nullopt;
  host_.collider->remove("svc:depot");
}

void ServiceWorld::end() {
  stopAll();
  dropDepot();
  if (yardSet_) {
    yardSet_ = false;
    if (host_.yard) *host_.yard = false;
  }
  here_ = Here();
}

}  // namespace fs
