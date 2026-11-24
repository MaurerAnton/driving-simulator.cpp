// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/craneplay.ts @ v1.7.7
#include "game/craneplay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include "audio/audio.h"
#include "game/settings.h"
#include "input/input.h"
#include "platform/intl.h"
#include "render/crane.h"
#include "render/cranesite.h"
#include "sim/collision.h"
#include "sim/ground.h"
#include "sim/hooks.h"
#include "sim/vehicle/vehicle.h"
#include "ui/cranepanel.h"
#include "ui/hud.h"

namespace fs {

namespace {

/** the crane's physics step (as the vehicles') */
constexpr double STEP = 1.0 / 240;

/** the site's drawing: one for the whole game, made the first time */
std::unique_ptr<CraneSiteScene> siteScene;
/** the crane's display and controls: one for the whole game */
std::unique_ptr<CranePanel> panel;

CraneSiteScene& sceneOf(const CraneHost& host) {
  if (!siteScene) {
    siteScene = std::make_unique<CraneSiteScene>();
    host.scene->add(siteScene->group);
  }
  return *siteScene;
}

CranePanel& panelOf(const CraneHost& host) {
  if (!panel) {
    const Settings* s = host.settings;
    panel = std::make_unique<CranePanel>(host.ui, [s] { return s->lang; });
  }
  return *panel;
}

/** (the yard as the site's drawing takes it, for the picture behind the menu) */
struct YardPlacement final : SitePlacement {
  const YardFrame* yard = nullptr;
  std::array<double, 2> toWorld(double u, double w) const override { return yard->toWorld(u, w); }
  std::array<double, 2> toLocal(double x, double z) const override { return yard->toLocal(x, z); }
  double yawToWorld(double yaw) const override { return yard->yawToWorld(yaw); }
  double yawToLocal(double yaw) const override { return yard->yawToLocal(yaw); }
};
YardPlacement menuPlace;

/** how far from the tower crane's axis its operator stands after climbing down (its cross base is 5 × 5 m), and from how far one climbs up */
constexpr double TOWER_OFF = 4.3;
constexpr double TOWER_REACH = 6.5;

/** The vehicle's slope from its turn (rise per metre along its x and z). */
struct SlopeXZ {
  double gx = 0;
  double gz = 0;
};
SlopeXZ slopeOf(const std::array<double, 4>& q) {
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  return {2 * (x * y + z * w), 2 * (y * z - x * w)};
}

/** '★★☆' */
std::string stars(int n) {
  std::string s;
  for (int i = 0; i < n; i++) s += "★";
  for (int i = 0; i < 3 - n; i++) s += "☆";
  return s;
}

}  // namespace

std::optional<CranePlace> craneStart(const CraneType& type, const YardFrame* yard) {
  if (!yard) return std::nullopt;
  const SitePose p = type == "tower" ? SitePose{.u = TOWER_SITE.u, .w = TOWER_SITE.w, .yaw = TOWER_SITE.yaw} : DEPOT_START;
  const auto [x, z] = yard->toWorld(p.u, p.w);
  return CranePlace{x, z, yard->yawToWorld(p.yaw)};
}

void craneMenuSite(const CraneHost& host, const std::optional<CraneType>& type, const std::function<double(double, double)>& heightAt) {
  if (!type || !host.yard) {
    if (siteScene) siteScene->hide();
    return;
  }
  CraneSiteScene& s = sceneOf(host);
  const auto [bx, bz] = host.yard->toWorld((BUILDING.u0 + BUILDING.u1) / 2, (BUILDING.w0 + BUILDING.w1) / 2);
  const double y0 = heightAt(bx, bz);
  menuPlace.yard = host.yard;
  s.show(menuPlace, heightAt, std::isnan(y0) ? 0 : y0, *type != "tower", host.settings->quality == "low" ? "low" : "full");
}

void craneMenuPose(CraneModel& model, const CraneType& type) {
  Object3D& r = *model.root;
  r.updateMatrixWorld(true);
  Crane c(type);
  c.place(r.position.x, r.position.y, r.position.z, Euler().setFromQuaternion(r.quaternion, Euler::YXZ).y);
  if (type == "tower") {
    // (the tower crane: its jib turned a little, the hook halfway down)
    c.q.slew = 0.5;
    c.q.trolley = 22;
    c.q.rope = 16;
    c.place(r.position.x, r.position.y, r.position.z, Euler().setFromQuaternion(r.quaternion, Euler::YXZ).y);
  }
  model.pose(c, 0);
}

CranePlace towerExit(double bx, double bz, double yaw, const std::function<bool(double x, double z)>& free) {
  std::optional<CranePlace> first;
  for (int k = 0; k < 8; k++) {
    const double a = yaw + PI / 2 + (k * PI) / 4;
    // (out along the heading a: forwards is (−sin a, −cos a); facing back to the tower is a + π)
    const CranePlace p{bx - std::sin(a) * TOWER_OFF, bz - std::cos(a) * TOWER_OFF, a + PI};
    if (free(p.x, p.z)) return p;
    if (!first) first = p;
  }
  return *first;
}

bool atTowerFoot(double bx, double bz, double x, double z) { return std::hypot(x - bx, z - bz) <= TOWER_REACH; }

// ------------------------------------------------------------------------------ the work

CraneWork::CraneWork(const CraneType& type_, CraneHost host)
    : type(type_), crane(type_), panel(panelOf(host)), host_(std::move(host)), site_(sceneOf(host_)) {
  place_.yard = host_.yard;
  operating = type == "tower";
  panel.onAttach = [this] { attach(); };
  panel.onToggle = [this] {
    if (toggleRequested) toggleRequested();
  };
  GameAudio* audio = host_.audio;
  panel.onClick = [audio] { audio->click("tick"); };
  panel.show(type);
  // (shown once the drive runs: render says so each picture)
  panel.root.hidden(true);
  world_.floor = [this](double x, double z, double hx, double hz, double y) { return floor(x, z, hx, hz, y); };
  world_.boxes = [this] { return boxes_; };
  world_.ground = [this](double x, double z) {
    const double h = host_.heightAt(x, z);
    return std::isnan(h) ? crane.base.y : h;
  };
  world_.wind = [this] {
    // the weather the game shows now (game/weather.ts): the mean wind, where it blows to, its gusts
    const WeatherState w = weatherNow();
    windVector(w, wind_);
    wind_.gust = w.wind > 0.5 ? std::max(1.0, w.gust / w.wind) : 1.3;
    return wind_;
  };
  setOperating(operating);
}

CraneWork::~CraneWork() = default;

// ---------------------------------------------------------------------------- the site

/** Lay the site out once its ground is loaded (called each frame until it is). */
void CraneWork::layout() {
  if (laid_) return;
  const YardFrame* yard = host_.yard;
  if (!yard || time_ - laidTry_ < 0.7) return;
  laidTry_ = time_;
  const std::function<double(double, double)> h = [this](double x, double z) { return host_.heightAt(x, z); };
  const auto [bx, bz] = yard->toWorld((BUILDING.u0 + BUILDING.u1) / 2, (BUILDING.w0 + BUILDING.w1) / 2);
  const double y0 = h(bx, bz);
  if (std::isnan(y0)) return;
  y0_ = y0;
  const CraneDetail detail = host_.settings->quality == "low" ? "low" : "full";
  if (!site_.show(place_, h, y0, type != "tower", detail)) return;
  laid_ = true;
  tasks = std::make_unique<CraneTasks>(type, place_, [this](double x, double z) { return world_.ground(x, z); }, y0);
  geo_ = siteWorld(place_, y0);
  // the timber bearers waiting at a place for their load: something to set it down on
  std::vector<CraneBoxY> bearers;
  for (const SiteTask& t : tasks->plan.tasks) {
    const WorldZone* zone = nullptr;
    for (const WorldZone& z : tasks->zones)
      if (z.id == t.zone) {
        zone = &z;
        break;
      }
    const WorldLoad* load = nullptr;
    for (const WorldLoad& l : tasks->loads)
      if (l.id == t.load) {
        load = &l;
        break;
      }
    if (zone && zone->bearers && load) {
      const std::vector<CraneBoxY> b = bearersOf(*zone, load->def, world_.ground(zone->x, zone->z));
      bearers.insert(bearers.end(), b.begin(), b.end());
    }
  }
  geo_->platforms.insert(geo_->platforms.end(), bearers.begin(), bearers.end());
  site_.addBearers(bearers);
  // the site's paved areas, for the wheels
  const std::vector<SiteRect> areas = {AREAS.depot, AREAS.road, AREAS.storage, AREAS.west, AREAS.site};
  host_.ground->paved = [yard, areas](double x, double z) {
    const auto [u, w] = yard->toLocal(x, z);
    for (const SiteRect& a : areas)
      if (u >= a.u0 && u <= a.u1 && w >= a.w0 && w <= a.w1) return true;
    return false;
  };
  colliders();
  refreshBoxes();
}

/** What the vehicles run into on the site: its buildings, the tower crane's base, the truck, the offices, the loads lying about. */
void CraneWork::colliders() {
  const YardFrame* yard = host_.yard;
  if (!yard || !geo_) return;
  rings_.clear();
  ringCounts_.clear();
  const auto rect = [this](double x0, double z0, double x1, double z1) {
    for (const double v : {x0, z0, x1, z0, x1, z1, x0, z1}) rings_.push_back(static_cast<float>(v));
    ringCounts_.push_back(4);
  };
  // (the solids that reach down to the ground; not the guard rails up on the roof)
  for (const CraneBox3& b : geo_->solids)
    if (b.y0 <= y0_ + 0.5) rect(b.x0, b.z0, b.x1, b.z1);
  if (tasks)
    for (const WorldLoad& l : tasks->loads) {
      if (l.hooked || l.bed) continue;
      const auto [fx, fz] = loadFootprint(l.def, l.yaw);
      rect(l.x - fx, l.z - fz, l.x + fx, l.z + fz);
    }
  host_.collider->remove("cranesite");
  CollisionSource src;
  src.key = "cranesite";
  src.rings = &rings_;
  src.ringCounts = &ringCounts_;
  host_.collider->add(src);
}

/** The solids the hook and the load stop at: the site's, the loads that lie about, the crane's own carrier. */
void CraneWork::refreshBoxes() {
  std::vector<CraneBox3> out = geo_ ? geo_->solids : std::vector<CraneBox3>{};
  if (tasks)
    for (const WorldLoad& l : tasks->loads) {
      if (l.hooked) continue;
      const auto [fx, fz] = loadFootprint(l.def, l.yaw);
      CraneBox3 b;
      b.x0 = l.x - fx;
      b.x1 = l.x + fx;
      b.z0 = l.z - fz;
      b.z1 = l.z + fz;
      b.y0 = l.y;
      b.y1 = l.y + 2 * l.def.hy;
      out.push_back(b);
    }
  out.insert(out.end(), own_.begin(), own_.end());
  boxes_ = std::move(out);
}

/** The highest thing under a footprint: the ground, the site's slabs and platforms, the loads lying about, the loader's own flatbed. */
double CraneWork::floor(double x, double z, double hx, double hz, double y) {
  const double NEG_INF = -std::numeric_limits<double>::infinity();
  double best = NEG_INF;
  const double corners[5][2] = {{0, 0}, {-hx, -hz}, {hx, -hz}, {-hx, hz}, {hx, hz}};
  for (const auto& d : corners) {
    const double g = host_.heightAt(x + d[0], z + d[1]);
    if (!std::isnan(g)) best = std::max(best, g);
  }
  if (best == NEG_INF) best = crane.base.y;
  const double top = y + 0.05;
  const double x0 = x - hx;
  const double x1 = x + hx;
  const double z0 = z - hz;
  const double z1 = z + hz;
  if (geo_)
    for (const CraneBoxY& p : geo_->platforms) {
      if (p.y > top || p.y <= best || x1 <= p.x0 || x0 >= p.x1 || z1 <= p.z0 || z0 >= p.z1) continue;
      best = p.y;
    }
  if (tasks)
    for (const WorldLoad& l : tasks->loads) {
      if (l.hooked) continue;
      const double t = l.y + 2 * l.def.hy;
      if (t > top || t <= best) continue;
      const auto [fx, fz] = loadFootprint(l.def, l.yaw);
      if (x1 <= l.x - fx || x0 >= l.x + fx || z1 <= l.z - fz || z0 >= l.z + fz) continue;
      best = t;
    }
  // the loader crane's own flatbed (with its boards)
  if (type == "loader") {
    const std::optional<CraneBoxY> bed = bedBox();
    if (bed && bed->y <= top && bed->y > best && !(x1 <= bed->x0 || x0 >= bed->x1 || z1 <= bed->z0 || z0 >= bed->z1)) best = bed->y;
  }
  return best;
}

/** the loader's flatbed in the world (its floor's height), from where the crane stands */
std::optional<CraneBoxY> CraneWork::bedBox() const {
  const Crane& c = crane;
  const auto& B = LOADER_BED;
  CranePoint a;
  CranePoint b;
  c.toWorld(-B.hw, B.y, B.z0, a);
  c.toWorld(B.hw, B.y, B.z1, b);
  CraneBoxY out;
  out.x0 = std::min(a.x, b.x);
  out.x1 = std::max(a.x, b.x);
  out.z0 = std::min(a.z, b.z);
  out.z1 = std::max(a.z, b.z);
  out.y0 = 0;
  out.y1 = 0;
  out.y = (a.y + b.y) / 2;
  return out;
}

/** the crane's own carrier as solids for its hook: the mobile crane's, the loader truck's cab and the boards of its flatbed */
void CraneWork::ownBoxes() {
  const Crane& c = crane;
  const auto box = [this, &c](double x0, double x1, double y0, double y1, double z0, double z1) {
    CranePoint pts[4];
    c.toWorld(x0, y0, z0, pts[0]);
    c.toWorld(x1, y1, z1, pts[1]);
    c.toWorld(x0, y0, z1, pts[2]);
    c.toWorld(x1, y1, z0, pts[3]);
    CraneBox3 b;
    b.x0 = std::min({pts[0].x, pts[1].x, pts[2].x, pts[3].x});
    b.x1 = std::max({pts[0].x, pts[1].x, pts[2].x, pts[3].x});
    b.z0 = std::min({pts[0].z, pts[1].z, pts[2].z, pts[3].z});
    b.z1 = std::max({pts[0].z, pts[1].z, pts[2].z, pts[3].z});
    b.y0 = std::min({pts[0].y, pts[1].y, pts[2].y, pts[3].y});
    b.y1 = std::max({pts[0].y, pts[1].y, pts[2].y, pts[3].y});
    own_.push_back(b);
  };
  own_.clear();
  if (type == "mobile") {
    box(-1.27, 1.27, 0, 1.75, -3.95, 4.6);
    box(-1.27, -0.5, 1.75, 3.2, -3.95, -2.25);
  } else if (type == "loader") {
    const auto& B = LOADER_BED;
    box(-1.25, 1.25, 0, 3.1, -3.9, -2.35);
    box(-B.hw, B.hw, 0, B.y + 1.1, B.z0 - 0.08, B.z0);
    box(-B.hw - 0.04, -B.hw, B.y, B.y + 0.4, B.z0, B.z1);
    box(B.hw, B.hw + 0.04, B.y, B.y + 0.4, B.z0, B.z1);
    box(-B.hw, B.hw, B.y, B.y + 0.4, B.z1 - 0.03, B.z1 + 0.03);
  }
}

// ---------------------------------------------------------------------------- driving and working

/** Take over the vehicle's pose (where it stands, its heading, the ground's slope). */
void CraneWork::fromVehicle(const Vehicle& v, bool handbrake) {
  double m[3] = {0, 0, 0};
  v.getModelPosition(m);
  veh_.x = m[0];
  veh_.y = m[1];
  veh_.z = m[2];
  veh_.yaw = v.yaw;
  veh_.speed = v.speed;
  veh_.handbrake = handbrake;
  vq_.set(v.quat[0], v.quat[1], v.quat[2], v.quat[3]);
}

void CraneWork::setOperating(bool on) {
  operating = on;
  panel.active = on;
  host_.input->crane = on;
  host_.ui.toggleClass("craning", on);
  lookReady_ = false;
}

void CraneWork::setAway(bool away) {
  if (type != "tower") return;
  panel.active = !away;
  host_.input->crane = !away;
  host_.ui.toggleClass("craning", !away);
  lookReady_ = false;
}

CranePlace CraneWork::towerExit(const std::function<bool(double x, double z)>& free) const {
  const auto& b = crane.base;
  return fs::towerExit(b.x, b.z, b.yaw, free);
}

bool CraneWork::atTowerFoot(double x, double z) const {
  const auto& b = crane.base;
  return fs::atTowerFoot(b.x, b.z, x, z);
}

std::optional<std::string> CraneWork::toggle(Vehicle& v, bool handbrake) {
  const auto no = [this](const std::string& key) -> std::optional<std::string> {
    say(key, 3);
    return key;
  };
  if (type == "tower") return std::nullopt;
  if (!operating) {
    if (std::abs(v.speed) > 0.3) return no("craneTip.stopFirst");
    if (!handbrake) return no("craneTip.handbrake");
    fromVehicle(v, handbrake);
    const SlopeXZ s = slopeOf(v.quat);
    crane.place(veh_.x, veh_.y, veh_.z, veh_.yaw, s.gx, s.gz);
    ownBoxes();
    refreshBoxes();
    // (the crane's hydraulics run off the engine: started from the crane's controls if it stood)
    if (!v.engineRunning) v.resetDrivetrain(true);
    setOperating(true);
    host_.audio->click("door");
    return std::nullopt;
  }
  if (crane.load) return no("craneTip.loadOn");
  if (!crane.stowed()) return no(type == "loader" ? "craneTip.fold" : "craneTip.stow");
  if (!crane.outriggersIn()) return no("craneTip.outriggersIn");
  setOperating(false);
  own_.clear();
  colliders();
  host_.audio->click("door");
  return std::nullopt;
}

void CraneWork::drive(double dt, const Vehicle& v, bool handbrake) {
  time_ += dt;
  layout();
  fromVehicle(v, handbrake);
  // (the crane goes with its vehicle: its hook hangs where it is stowed – the loader crane's is hooked in under the folded boom)
  const SlopeXZ s = slopeOf(v.quat);
  crane.place(veh_.x, veh_.y, veh_.z, veh_.yaw, s.gx, s.gz);
  if (type == "loader") crane.hook.y = crane.head.y - 0.15;
  carryLoads();
  updateTasks();
}

/** The loads on the loader crane's own flatbed: where the truck is. */
void CraneWork::carryLoads() {
  CraneTasks* t = tasks.get();
  if (!t || type != "loader") return;
  for (WorldLoad& l : t->loads) {
    if (!l.bed || l.hooked) continue;
    tmp_.set(l.bed->x, LOADER_BED.y, l.bed->z).applyQuaternion(vq_);
    l.x = veh_.x + tmp_.x;
    l.y = veh_.y + tmp_.y + crane.lift;
    l.z = veh_.z + tmp_.z;
    l.yaw = veh_.yaw + l.bed->yaw;
  }
  // (the place on the flatbed goes with it)
  for (WorldZone& zn : t->zones) {
    if (!zn.bed) continue;
    tmp_.set(zn.bed->x, LOADER_BED.y, zn.bed->z).applyQuaternion(vq_);
    zn.x = veh_.x + tmp_.x;
    zn.y = veh_.y + tmp_.y + crane.lift;
    zn.z = veh_.z + tmp_.z;
    zn.yaw = veh_.yaw;
  }
}

void CraneWork::work(double dt, Vehicle& v) {
  time_ += dt;
  layout();
  if (type == "tower" && crane.time == 0) {
    // (the tower crane stands where its vehicle was put: on its base – its drives are electric, no engine runs)
    fromVehicle(v, true);
    crane.place(veh_.x, veh_.y, veh_.z, veh_.yaw);
    v.engineRunning = false;
  }
  const CraneControls ctl = panel.read(dt);
  CraneCommand& cmd = crane.cmd;
  cmd.slew = -ctl.lx;
  cmd.hoist = ctl.ly;
  cmd.boom = ctl.ry;
  cmd.tele = ctl.rx;
  cmd.fine = ctl.fine;
  cmd.beams = ctl.beams;
  cmd.jacks = ctl.jacks;
  cmd.level = ctl.level;
  // the cargo on the loader crane's bed holds it down too
  std::vector<Weight> extra;
  if (type == "loader" && tasks)
    for (const WorldLoad& l : tasks->loads)
      if (l.bed && !l.hooked) extra.push_back(Weight{.t = l.def.t, .x = l.bed->x, .y = LOADER_BED.y + l.def.hy, .z = l.bed->z});
  crane.extra = extra;
  CraneEvents& ev = ev_;
  ev.hit = 0;
  ev.landed = 0;
  ev.liftedOff = false;
  ev.cut = false;
  acc_ = std::min(acc_ + dt, 0.1);
  const double wasLift = crane.lift;
  while (acc_ >= STEP) {
    crane.step(STEP, world_);
    const CraneEvents& e = crane.events;
    ev.hit = std::max(ev.hit, e.hit);
    ev.landed = std::max(ev.landed, e.landed);
    ev.liftedOff = ev.liftedOff || e.liftedOff;
    ev.cut = ev.cut || e.cut;
    acc_ -= STEP;
  }
  // (lifted or lowered by its jacks: what it carries goes with it, and so does its own outline for the hook)
  if (std::abs(crane.lift - wasLift) > 1e-4) ownBoxes();
  // the load on the hook goes where the hook goes
  if (WorldLoad* l = hooked()) {
    const auto& h = crane.hook;
    l->x = h.x;
    l->z = h.z;
    l->y = h.y - crane.drop();
  }
  carryLoads();
  if (ev.hit > 0.25) host_.audio->click(ev.hit > 0.8 ? "crash" : "bump");
  if (ev.landed > 0.3) host_.audio->click("bump");
  // the engine works the crane's hydraulics: it revs with what is moved (the tower crane's drives are electric)
  if (type != "tower") {
    const double act = std::min(1.0, std::abs(ctl.lx) + std::abs(ctl.ly) + std::abs(ctl.rx) + std::abs(ctl.ry) + (ctl.beams != 0 || ctl.jacks != 0 || ctl.level ? 0.6 : 0));
    v.rpm += (v.spec.idleRpm * (1 + act * 1.1) - v.rpm) * std::min(1.0, dt * 3);
  }
  // the limiter's warning: a beep now and then from 90 %, quickly once it cuts off
  const Lmi& lmi = crane.lmi;
  if (lmi.warn || lmi.stop) {
    beepT_ -= dt;
    if (beepT_ <= 0) {
      host_.audio->click("beep");
      beepT_ = lmi.stop ? 0.35 : 1.1;
    }
  } else
    beepT_ = 0;
  updateTasks();
}

/** the load on the hook (as the work knows it) */
WorldLoad* CraneWork::hooked() {
  if (!crane.load || crane.load->id.empty() || !tasks) return nullptr;
  for (WorldLoad& x : tasks->loads)
    if (x.id == crane.load->id) return &x;
  return nullptr;
}

/** What the crane is like now, for the tasks. */
const CraneSnapshot& CraneWork::snapshot() {
  CraneSnapshot& s = snap_;
  Crane& c = crane;
  s.time = time_;
  s.vx = veh_.x;
  s.vz = veh_.z;
  s.yaw = veh_.yaw;
  s.speed = veh_.speed;
  s.handbrake = veh_.handbrake;
  s.operating = operating;
  s.beams = c.beamSetting();
  s.padsDown = c.padsDown(world_);
  s.onOutriggers = c.onOutriggers(world_);
  s.incline = c.incline();
  s.stowed = c.stowed();
  s.luff = (c.q.luff * 180) / PI;
  s.unfolded = type == "loader" && !c.stowed() && c.q.inner + c.q.outer > -0.5;
  s.head.x = c.head.x;
  s.head.y = c.head.y;
  s.head.z = c.head.z;
  s.hook.x = c.hook.x;
  s.hook.y = c.hook.y;
  s.hook.z = c.hook.z;
  s.hooked = c.load ? std::optional<std::string>(c.load->id) : std::nullopt;
  s.resting = c.resting;
  s.clearance = c.clearance;
  s.swing = c.swing;
  s.lmi = c.lmi;
  s.events = ev_;
  s.tipped = c.tipped.has_value();
  return s;
}

void CraneWork::updateTasks() {
  CraneTasks* t = tasks.get();
  if (!t) return;
  const WorkEvents ev = t->update(snapshot());
  for (const CraneFault& f : ev.faults) host_.note(f, crane.hook.x, crane.hook.z);
  if (ev.stepDone) host_.audio->click("chime");
  if (ev.taskDone) {
    host_.audio->click("chime");
    const TaskResult& r = t->results.back();
    say("craneTip.taskDone", 4, {{"stars", stars(r.stars)}, {"score", r.score}});
  }
  if (ev.allDone && overAt_ < 0) overAt_ = time_ + 3;
  if (crane.tipped && overAt_ < 0) overAt_ = time_ + 4;
  // the events were told: the next frame's steps collect anew
  ev_.hit = 0;
  ev_.landed = 0;
  ev_.liftedOff = false;
  ev_.cut = false;
  if (overAt_ > 0 && time_ >= overAt_) over = true;
}

void CraneWork::say(const std::string& key, double seconds, const TrParams& data) {
  tip_ = key;
  sayData_ = data;
  tipUntil_ = time_ + seconds;
}

void CraneWork::attach() {
  CraneTasks* t = tasks.get();
  Crane& c = crane;
  if (!operating || !t || c.tipped) return;
  GameAudio& audio = *host_.audio;
  if (c.load) {
    WorldLoad* l = hooked();
    if (!l) return;
    // (the slinger unhooks only a load that stands and no longer pulls on the rope)
    if (!c.resting || c.tension > 0.3 * c.load->t * G) {
      say("craneTip.setDownFirst", 3);
      return;
    }
    l->x = c.hook.x;
    l->z = c.hook.z;
    l->y = c.hook.y - c.drop();
    c.release();
    l->hooked = false;
    // set down on the loader's own flatbed: it goes with the truck from now on
    if (type == "loader") {
      const std::optional<CraneBoxY> bed = bedBox();
      const auto [cx, cz] = c.toCarrier(l->x, l->z);
      const auto& B = LOADER_BED;
      if (bed && std::abs(l->y - bed->y) < 0.1 && std::abs(cx) <= B.hw && cz >= B.z0 && cz <= B.z1) l->bed = BedPlace{.x = cx, .z = cz, .yaw = l->yaw - veh_.yaw};
    }
    refreshBoxes();
    audio.click("clack");
    return;
  }
  WorldLoad* l = t->reachable(CranePoint{c.hook.x, c.hook.y, c.hook.z}, c.spec.hookDrop);
  if (!l) {
    bool near = false;
    for (const WorldLoad& x : t->loads)
      if (!x.fixed && !x.hooked && std::hypot(x.x - c.hook.x, x.z - c.hook.z) < 1.5) near = true;
    say(near ? "craneTip.lowerMore" : "craneTip.noLoad", 3);
    return;
  }
  // (off the flatbed it is in the world now: where the truck put it)
  l->bed = std::nullopt;
  l->bearers = false;
  l->hooked = true;
  c.attach(HookLoad{.id = l->id, .t = l->def.t, .hx = l->def.hx, .hy = l->def.hy, .hz = l->def.hz, .sling = l->def.sling, .yaw = l->yaw, .area = l->def.area}, l->x, l->y + 2 * l->def.hy, l->z);
  refreshBoxes();
  audio.click("clack");
}

// ---------------------------------------------------------------------------- what is shown

void CraneWork::render(CraneModel& model, double now, bool shown) {
  panel.root.hidden(!shown);
  model.root->updateMatrixWorld(true);
  model.pose(crane, now);
  CraneTasks* t = tasks.get();
  if (!t) return;
  const std::optional<CraneTasks::Target> tg = t->target();
  const Crane& c = crane;
  const Vector3* hook = c.load ? &tmp2_.set(c.hook.x, c.hook.y - c.spec.hookDrop, c.hook.z) : nullptr;
  std::optional<CraneSiteTarget> target;
  if (tg) target = CraneSiteTarget{.load = tg->load->id, .zone = *tg->zone};
  site_.update(t->loads, hook, target ? &*target : nullptr, now);
  site_.tick(now);
}

void CraneWork::hud(HudState& hs, bool touch, const std::optional<std::array<double, 2>>& walker) {
  const std::string& lang = host_.settings->lang;
  Crane& c = crane;
  if (walker) {
    // climbed down from the tower crane: the game's tips stay (and, at its foot, how to climb up again)
    if (type == "tower" && hs.tip.empty() && atTowerFoot((*walker)[0], (*walker)[1])) {
      hs.tip = tr(touch ? "craneTip.climbUpTouch" : "craneTip.climbUp", lang);
      hs.tipAction = "KeyG";
    }
    return;
  }
  panel.setMode(touch ? "touch" : "keys");
  CraneTasks* t = tasks.get();
  const bool canAttach = t && !c.load && t->reachable(CranePoint{c.hook.x, c.hook.y, c.hook.z}, c.spec.hookDrop) != nullptr;
  const auto slope = c.tilt();
  const bool stillOk = std::abs(veh_.speed) < 0.3 && veh_.handbrake;
  CraneInfo info;
  info.type = type;
  info.operating = operating;
  info.hooked = c.load.has_value();
  info.canAttach = canAttach;
  info.swing = c.swing;
  info.beams = c.beamSetting();
  info.padsDown = operating ? c.padsDown(world_) : false;
  info.onOutriggers = operating ? c.onOutriggers(world_) : false;
  info.slopeX = (std::atan(slope.gx) * 180) / PI;
  info.slopeZ = (std::atan(slope.gz) * 180) / PI;
  info.stowed = c.stowed();
  info.canDrive = c.stowed() && c.outriggersIn() && !c.load;
  info.canOperate = stillOk;
  info.fine = panel.fine();
  panel.update(c.lmi, info, time_);
  // what to do, as the work says it
  const auto tk = [&lang](const std::string& key, const TrParams& data) {
    TrParams d;
    for (const TrParam& p : data) d.add(p.name == "load" ? TrParam(p.name, tr(p.value, lang)) : p);
    return tr(key, lang, d);
  };
  if (t) {
    const CraneInstruction ins = t->instruction(snapshot());
    hs.navText = tk(ins.key, ins.data);
    const std::string progress = t->stage == "guide" ? tr("craneProgressGuide", lang) : t->stage == "done" ? std::string() : tr("craneProgress", lang, {{"n", t->task + 1}, {"max", static_cast<int>(t->plan.tasks.size())}});
    const std::string hint = !ins.hint.empty() ? tr(ins.hint, lang) : std::string();
    hs.navSub = progress.empty() ? hint : hint.empty() ? progress : progress + " · " + hint;
  } else {
    hs.navText = tr(host_.yard ? "craneSiteLoading" : "craneNoSite", lang);
    hs.navSub = "";
  }
  hs.navArrow = 0;
  hs.navActive = false;
  if (operating) {
    hs.limit = -1;
    hs.zone = 0;
    hs.junction = std::nullopt;
    hs.onFoot = true;
    hs.noKeyHints = true;
    hs.tipAction = "";
    // (the little map turns with the boom)
    const auto [dx, dz] = boomDir(c.q.slew);
    const double c0 = std::cos(c.base.yaw);
    const double s0 = std::sin(c.base.yaw);
    hs.yaw = std::atan2(-(dx * c0 + dz * s0), -(-dx * s0 + dz * c0));
    hs.tip = "";
  }
  // (the road's tips do not fit on the site: back onto the road, getting out – the door key changes to the crane here)
  if (!operating && (hs.tipAction == "KeyT" || hs.tipAction == "KeyG" || hs.tip == tr("offMap", lang))) {
    hs.tip = "";
    hs.tipAction = "";
  }
  if (time_ < tipUntil_) {
    hs.tip = tk(tip_, sayData_);
    hs.tipAction = "";
  } else if (!operating && type != "tower" && stillOk && t) {
    CraneSnapshot at;
    at.vx = veh_.x;
    at.vz = veh_.z;
    at.yaw = veh_.yaw;
    if (t->inStand(at)) {
      // parked in its stand: change to working the crane
      hs.tip = tr(touch ? "craneTip.operateTouch" : "craneTip.operate", lang);
      // (on a touch screen the tip itself is the button – as well as «Crane» beside the controls)
      hs.tipAction = "KeyG";
    }
  }
}

bool CraneWork::camera(Camera& cam, const std::string& mode, const Controls& c, double dt, CraneModel& model) {
  if (!operating) return false;
  const Crane& cr = crane;
  const auto& h = cr.hook;
  // what is watched: the load's middle on the hook, else the hook
  const WorldLoad* l = hooked();
  Vector3& target = tmp2_.set(h.x, l ? l->y + l->def.hy : h.y - 0.5, h.z);
  if (mode == "cockpit") {
    cam.near = 0.08;
    cam.fov = 62;
    model.operatorEye(tmp_);
    cam.position.copy(tmp_);
    // the eyes follow the load (smoothly), the player's look turns them from there
    if (!lookReady_) {
      look_.copy(target);
      lookReady_ = true;
    } else
      look_.lerp(target, std::min(1.0, dt * 3));
    const Vector3 d = look_.clone().sub(cam.position);
    const double yaw = std::atan2(-d.x, -d.z);
    const double pitch = std::atan2(d.y, std::hypot(d.x, d.z));
    cam.quaternion.setFromEuler(Euler(std::max(-1.45, std::min(1.2, pitch + c.lookPitch)), yaw + c.lookYaw, 0, Euler::YXZ));
    return true;
  }
  if (mode == "chase") {
    cam.near = 0.3;
    cam.fov = 55;
    // behind the hook as seen from the crane, the player's drag turning round it, the wheel in and out
    const double yawBase = std::atan2(-(h.x - cr.base.x), -(h.z - cr.base.z));
    const double yaw = yawBase + c.lookYaw;
    const double el = std::max(0.05, std::min(1.45, 0.42 - c.lookPitch));
    const double dist = std::max(6.0, std::min(90.0, c.lookZoom));
    Vector3& want = tmp_.set(target.x + std::sin(yaw) * std::cos(el) * dist, target.y + std::sin(el) * dist, target.z + std::cos(yaw) * std::cos(el) * dist);
    const double g = host_.heightAt(want.x, want.z);
    if (!std::isnan(g)) want.y = std::max(want.y, g + 1.2);
    if (!lookReady_) {
      cam.position.copy(want);
      lookReady_ = true;
    } else
      cam.position.lerp(want, std::min(1.0, dt * 4));
    cam.lookAt(target);
    return true;
  }
  return false;
}

std::optional<dom::El> CraneWork::resultNode() const {
  const CraneTasks* t = tasks.get();
  if (!t || t->results.empty()) return std::nullopt;
  const std::string& lang = host_.settings->lang;
  dom::El box = dom::el("div", "crane-results");
  const auto fmt = [&lang](double v, int d) {
    plat::NumberFormat o;
    o.minimumFractionDigits = d;
    o.maximumFractionDigits = d;
    o.numberingSystem = "latn";
    return plat::formatNumber(v, localeOf(lang), o);
  };
  double sum = 0;
  for (const TaskResult& r : t->results) sum += r.score;
  const double total = jsRound(sum / static_cast<double>(t->results.size()));
  dom::El head = dom::el("p", "crane-total");
  head.textContent(tr("craneResult.total", lang, {{"done", static_cast<int>(t->results.size())}, {"max", static_cast<int>(t->plan.tasks.size())}, {"score", total}}));
  box.append(head);
  for (size_t i = 0; i < t->results.size(); i++) {
    const TaskResult& r = t->results[i];
    dom::El row = dom::el("div", std::string("fault ") + (r.stars >= 2 ? "hint" : r.stars == 1 ? "minor" : "major"));
    dom::El b = dom::el("b");
    b.textContent(std::to_string(i + 1) + " · " + tr(r.text, lang, {{"load", tr(r.load, lang)}}) + "  " + stars(r.stars));
    dom::El small = dom::el("small");
    const int min = static_cast<int>(std::floor(r.time / 60));
    const int sec = static_cast<int>(jsRound(std::fmod(r.time, 60)));
    const std::string secs = sec < 10 ? "0" + std::to_string(sec) : std::to_string(sec);
    small.textContent(tr("craneResult.line", lang,
                         {{"score", r.score},
                          {"time", std::to_string(min) + ":" + secs},
                          {"off", jsRound(r.offset * 100)},
                          {"swing", fmt(r.swing, 1)},
                          {"landing", fmt(r.landing, 2)},
                          {"peak", jsRound(r.peak * 100)}}));
    row.append(b);
    row.append(small);
    if (r.overload || r.hits > 0) {
      dom::El em = dom::el("em");
      const std::string a = r.overload ? tr("craneResult.overload", lang) : std::string();
      const std::string h = r.hits > 0 ? tr("craneResult.hits", lang, {{"n", r.hits}}) : std::string();
      em.textContent(a.empty() ? h : h.empty() ? a : a + " · " + h);
      row.append(em);
    }
    box.append(row);
  }
  return box;
}

void CraneWork::dispose() {
  site_.hide();
  panel.show(std::nullopt);
  panel.onAttach = [] {};
  panel.onToggle = [] {};
  host_.collider->remove("cranesite");
  host_.ground->paved = nullptr;
  host_.input->crane = false;
  host_.ui.removeClass("craning");
}

}  // namespace fs
