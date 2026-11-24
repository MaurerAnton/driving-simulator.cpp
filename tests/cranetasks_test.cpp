// SPDX-License-Identifier: AGPL-3.0-only
// LINK: game sim i18n world core platform geo
// Origin: tests/game/cranetasks.test.ts @ v1.7.7
/**
 * The work with a crane on the construction site (src/game/cranetasks.ts):
 * the site's layout (nothing lies in anything else, every load and every
 * place within its crane's reach and load chart), the guided start step by
 * step, the judging of a task, the faults told – and a whole task done with
 * the real tower crane (src/sim/crane.ts) by a simple automatic operator.
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "game/cranetasks.h"
#include "sim/crane.h"

using namespace fs;

namespace {

/** game/ce.ts YardFrame, as the test uses it (u along the yard, w across it; rot 1: the long side runs north–south) */
struct YardJson {
  double x, z, len, wid;
  int rot;
};
class YardFrame : public SitePlacement {
 public:
  const YardJson yard;
  explicit YardFrame(const YardJson& y) : yard(y) {
    const double phi = (-M_PI / 2) * y.rot;
    cos_ = std::cos(phi);
    sin_ = std::sin(phi);
    turn_ = -phi;
  }
  std::array<double, 2> toWorld(double u, double w) const override { return {yard.x + u * cos_ - w * sin_, yard.z + u * sin_ + w * cos_}; }
  std::array<double, 2> toLocal(double x, double z) const override {
    const double dx = x - yard.x;
    const double dz = z - yard.z;
    return {dx * cos_ + dz * sin_, -dx * sin_ + dz * cos_};
  }
  double yawToWorld(double yaw) const override { return yaw + turn_; }
  double yawToLocal(double yaw) const override { return yaw - turn_; }

 private:
  double cos_, sin_, turn_;
};

const YardJson YARD = {.x = 5665, .z = -455, .len = 150, .wid = 66, .rot = 0};
double flat(double, double) { return 0; }

/** a rectangle of the site in the plan, its half-sizes along u and w for a load of a kind turned by yaw */
SiteRect rectOf(double u, double w, double hu, double hw) { return {.u0 = u - hu, .u1 = u + hu, .w0 = w - hw, .w1 = w + hw}; }

bool overlap(const SiteRect& a, const SiteRect& b, double gap = 0) { return a.u0 < b.u1 + gap && b.u0 < a.u1 + gap && a.w0 < b.w1 + gap && b.w0 < a.w1 + gap; }

/** where each crane's slewing axis is on the site (u, w), working from its stand – the tower crane from its base */
std::array<double, 2> axisOf(const CraneType& type) {
  if (type == "tower") return {TOWER_SITE.u, TOWER_SITE.w};
  const SitePose& st = STANDS.at(type).pose;
  // (facing along +u: the carrier's z, backwards, is −u)
  const double z = type == "mobile" ? MOBILE_CRANE.slewZ : LOADER_CRANE.slewZ;
  return {st.u - z, st.w};
}

const std::vector<CraneType> TYPES = {"tower", "mobile", "loader"};

void testSite() {
  // the site: nothing lies in anything else – the loads, the places they go, the building, the tower crane, the truck, the offices
  std::vector<std::pair<std::string, SiteRect>> solids = {
      {"building", BUILDING},
      {"scaffold S", BUILDING.scaffold[0]},
      {"scaffold E", BUILDING.scaffold[1]},
      {"tower crane", rectOf(TOWER_SITE.u, TOWER_SITE.w, TOWER_SITE.base, TOWER_SITE.base)},
      {"truck", {.u0 = DELIVERY.bed.u0, .u1 = DELIVERY.cab.u1, .w0 = DELIVERY.cab.w0, .w1 = DELIVERY.cab.w1}},
  };
  for (size_t i = 0; i < OFFICES.size(); i++) solids.push_back({"office " + std::to_string(i), OFFICES[i]});
  std::vector<std::tuple<std::string, SiteRect, std::string>> things;
  for (const CraneType& type : TYPES) {
    const CraneWorkPlan& plan = PLANS.at(type);
    for (const SiteLoad& l : plan.loads) {
      if (l.bed) continue;
      const LoadDef& def = LOAD_DEFS.at(l.kind);
      things.push_back({type + " load " + l.id, rectOf(l.u, l.w, def.hx, def.hz), type});
    }
    for (const SiteTask& t : plan.tasks) {
      const SiteZone& z = *std::find_if(plan.zones.begin(), plan.zones.end(), [&](const SiteZone& x) { return x.id == t.zone; });
      if (z.y || z.bed) continue;
      const LoadDef& def = LOAD_DEFS.at(std::find_if(plan.loads.begin(), plan.loads.end(), [&](const SiteLoad& x) { return x.id == t.load; })->kind);
      things.push_back({type + " place " + z.id, rectOf(z.u, z.w, def.hx + z.hu, def.hz + z.hw), type});
    }
  }
  for (const SiteLoad& l : SITE_LOADS) things.push_back({"load " + l.id, rectOf(l.u, l.w, LOAD_DEFS.at(l.kind).hx, LOAD_DEFS.at(l.kind).hz), "site"});
  for (const auto& [name, r, _] : things) {
    for (const auto& [sname, s] : solids)
      if (overlap(r, s)) {
        std::fprintf(stderr, "%s lies in the %s\n", name.c_str(), sname.c_str());
        assert(false);
      }
    // (within the yard)
    assert(r.u0 > -YARD.len / 2 && r.u1 < YARD.len / 2 && r.w0 > -YARD.wid / 2 && r.w1 < YARD.wid / 2);
  }
  for (size_t i = 0; i < things.size(); i++) {
    for (size_t j = i + 1; j < things.size(); j++) {
      const auto& [a, ra, ta] = things[i];
      const auto& [b, rb, tb] = things[j];
      // (a crane's own loads and places may not meet; other cranes' loads lie about for good)
      if (ta != tb && ta != "site" && tb != "site") continue;
      if (overlap(ra, rb, 0.1)) {
        std::fprintf(stderr, "%s and %s lie in each other\n", a.c_str(), b.c_str());
        assert(false);
      }
    }
  }
  // the stands lie clear of everything
  for (const auto& [type, st] : STANDS)
    for (const auto& [sname, s] : solids) assert(!overlap(st.area, s));
}

void testReach() {
  // every task: its load and its place within its crane's reach – and within its load chart, with room to spare
  for (const CraneType& type : TYPES) {
    const CraneWorkPlan& plan = PLANS.at(type);
    const auto [au, aw] = axisOf(type);
    const CraneSpec& spec = type == "tower" ? TOWER_CRANE : type == "mobile" ? MOBILE_CRANE : LOADER_CRANE;
    std::set<std::string> ids;
    for (const SiteLoad& l : plan.loads) ids.insert(l.id);
    assert(ids.size() == plan.loads.size());
    for (const SiteTask& t : plan.tasks) {
      const auto li = std::find_if(plan.loads.begin(), plan.loads.end(), [&](const SiteLoad& x) { return x.id == t.load; });
      const auto zi = std::find_if(plan.zones.begin(), plan.zones.end(), [&](const SiteZone& x) { return x.id == t.zone; });
      assert(li != plan.loads.end() && zi != plan.zones.end());
      const LoadDef& def = LOAD_DEFS.at(li->kind);
      const double need = (def.t + spec.hook) / 0.95;
      // (a load on the loader's own flatbed: its place there, from the column)
      const auto radius = [&](double u, double w, std::optional<std::array<double, 2>> bed) {
        return bed ? std::hypot((*bed)[0], (*bed)[1] - LOADER_CRANE.slewZ) : std::hypot(u - au, w - aw);
      };
      const std::optional<std::array<double, 2>> lb = li->bed ? std::optional<std::array<double, 2>>({li->bed->x, li->bed->z}) : std::nullopt;
      const std::optional<std::array<double, 2>> zb = zi->bed ? std::optional<std::array<double, 2>>({zi->bed->x, zi->bed->z}) : std::nullopt;
      for (const auto& [what, r] : std::vector<std::pair<std::string, double>>{{"load", radius(li->u, li->w, lb)}, {"place", radius(zi->u, zi->w, zb)}}) {
        const double cap = type == "tower" ? towerCapacity(r) : type == "mobile" ? capacity("mobile", "full", r, 34) : capacity("loader", "full", r);
        const double reach = type == "tower" ? 48.5 : type == "mobile" ? 32 : LOADER_GEOMETRY.pinA + LOADER_GEOMETRY.inner + LOADER_GEOMETRY.outer1;
        if (!(r <= reach) || !(cap >= need)) {
          std::fprintf(stderr, "%s %s: the %s at %.1f m (reach %.1f) – the chart allows %.2f t, needed %.2f\n", type.c_str(), t.load.c_str(), what.c_str(), r, reach, cap, need);
          assert(false);
        }
      }
    }
    assert(GUIDE.at(type).back() == "release");
  }
}

void testScore() {
  // the score of a task: precise, calm, gentle and quick is three stars – a swinging, slow, overloaded one none
  const TaskScore best = scoreTask({.time = 60, .par = 120, .offset = 0.05, .swing = 0.2, .landing = 0.1, .overload = false, .hits = 0});
  assert(best.score == 100);
  assert(best.stars == 3);
  const TaskScore worst = scoreTask({.time = 400, .par = 100, .offset = 0.7, .swing = 2.5, .landing = 1.2, .overload = true, .hits = 3});
  assert(worst.score == 0);
  assert(worst.stars == 0);
  const TaskScore mid = scoreTask({.time = 150, .par = 120, .offset = 0.25, .swing = 0.8, .landing = 0.3, .overload = false, .hits = 0});
  assert(mid.stars >= 1 && mid.stars <= 2);
}

WorldLoad& loadById(CraneTasks& t, const std::string& id) {
  return *std::find_if(t.loads.begin(), t.loads.end(), [&](const WorldLoad& x) { return x.id == id; });
}

void testWorld() {
  // in the world: the site turned with its yard – zones keep their frame, a load turned round lies the same way
  for (const int rot : {0, 1}) {
    YardJson y = YARD;
    y.rot = rot;
    const YardFrame f(y);
    const SiteWorld geo = siteWorld(f, 140);
    for (const CraneBox3& b : geo.solids) assert(b.x1 > b.x0 && b.z1 > b.z0 && b.y1 > b.y0);
    CraneTasks t("tower", f, [](double, double) { return 140.0; }, 140);
    const WorldZone& zone = *std::find_if(t.zones.begin(), t.zones.end(), [](const WorldZone& z) { return z.id == "tCont"; });
    const ZonePoint p = inZone(zone, zone.x, zone.z);
    assert(std::abs(p.du) < 1e-9 && std::abs(p.dw) < 1e-9);
    // a step along the site's u is a step along the zone's u
    const auto [x, z] = f.toWorld(24 + 0.5, -12.5);
    const ZonePoint q = inZone(zone, x, z);
    assert(std::abs(q.du - 0.5) < 1e-9 && std::abs(q.dw) < 1e-9);
    const WorldLoad& l = loadById(t, "tB1");
    assert(l.y == 140);  // the pallet stands on the ground
  }
  assert(yawOff(0.1, 0.1 + M_PI) < 1e-9);  // turned round lies the same way
  assert(std::abs(yawOff(0, M_PI / 2) - M_PI / 2) < 1e-9);
}

// ------------------------------------------------------------------------------------ the guided start

Lmi restLmi() {
  Lmi lmi;
  lmi.load = 0.4;
  lmi.capacity = 10;
  lmi.ratio = 0.04;
  lmi.radius = 8;
  lmi.length = 10;
  lmi.angle = 0;
  lmi.hookHeight = 3;
  lmi.setting = "tyres";
  lmi.wind = 3;
  lmi.windLimit = 13;
  lmi.incline = 0.1;
  lmi.tipping = 0.1;
  lmi.warn = false;
  lmi.stop = false;
  lmi.notLevel = false;
  lmi.levelGood = 0.3;
  return lmi;
}

/** a snapshot of a crane at rest, to be changed step by step */
CraneSnapshot snapshot() {
  CraneSnapshot s;
  s.time = 1;
  s.incline = 0.8;
  s.stowed = true;
  s.head = {0, 3, 0};
  s.hook = {0, 1.5, 0};
  s.clearance = 1;
  s.lmi = restLmi();
  return s;
}

void testGuideMobile() {
  // the guided start of the mobile crane: drive, park, change over, outriggers, jacks, level, boom up, sling, lift, move, set down, let go
  const YardFrame f(YARD);
  CraneTasks t("mobile", f, flat, 0);
  const CraneTasks::WorldStand st = *t.stand;
  const CraneTasks::Target tg = *t.target();
  WorldLoad& load = *tg.load;
  const WorldZone& zone = *tg.zone;
  std::vector<std::string> seen;
  double time = 1;
  const auto go = [&](CraneSnapshot s) {
    time += 0.5;
    s.time = time;
    const WorkEvents ev = t.update(s);
    if (ev.stepDone) seen.push_back(t.step ? *t.step : "null");
  };
  const auto at = [&](double vx, double vz, double yaw, double speed, bool handbrake) {
    CraneSnapshot s = snapshot();
    s.vx = vx;
    s.vz = vz;
    s.yaw = yaw;
    s.speed = speed;
    s.handbrake = handbrake;
    return s;
  };
  // driving towards the stand, not there yet
  go(at(st.x - 60, st.z, st.yaw, 8, false));
  assert(t.step == "drive");
  go(at(st.x - 10, st.z, st.yaw, 3, false));
  assert(t.step == "park");
  // in the stand but turned across it: not parked
  go(at(st.x, st.z, st.yaw + 1, 0, true));
  assert(t.step == "park");
  go(at(st.x + 0.5, st.z + 0.3, st.yaw + 0.05, 0, true));
  assert(t.step == "operate");
  CraneSnapshot a = at(st.x, st.z, st.yaw, 0, true);
  a.operating = true;
  go(a);
  assert(t.step == "beams");
  a.beams = 0.6;
  go(a);
  assert(t.step == "beams");  // half out is not enough here
  a.beams = 1;
  go(a);
  a.padsDown = true;
  a.onOutriggers = true;
  a.incline = 0.7;
  go(a);
  assert(t.step == "level");
  a.incline = 0.2;
  go(a);
  assert(t.step == "raise");
  CraneSnapshot up = a;
  up.stowed = false;
  up.luff = 35;
  go(up);
  assert(t.step == "over");
  {
    CraneSnapshot s = up;
    s.head = {load.x + 0.5, 15, load.z};
    s.hook = {load.x + 0.5, 8, load.z};
    go(s);
  }
  assert(t.step == "lower");
  const double top = load.y + 2 * load.def.hy;
  {
    CraneSnapshot s = up;
    s.head = {load.x, 15, load.z};
    s.hook = {load.x, top + 1.6, load.z};
    s.resting = true;
    s.clearance = 0;
    go(s);
  }
  assert(t.step == "attach");
  load.hooked = true;
  {
    CraneSnapshot s = up;
    s.head = {load.x, 15, load.z};
    s.hook = {load.x, top + 2.8, load.z};
    s.hooked = load.id;
    s.resting = true;
    go(s);
  }
  assert(t.step == "lift");
  {
    CraneSnapshot s = up;
    s.head = {load.x, 15, load.z};
    s.hook = {load.x, top + 5, load.z};
    s.hooked = load.id;
    s.resting = false;
    s.clearance = 2;
    s.swing = 0.4;
    go(s);
  }
  assert(t.step == "move");
  {
    CraneSnapshot s = up;
    s.head = {zone.x, 15, zone.z};
    s.hook = {zone.x + 1, 6, zone.z};
    s.hooked = load.id;
    s.clearance = 2;
    s.swing = 0.9;
    go(s);
  }
  assert(t.step == "setDown");
  {
    CraneSnapshot s = up;
    s.head = {zone.x, 15, zone.z};
    s.hook = {zone.x + 0.1, 4, zone.z};
    s.hooked = load.id;
    s.resting = true;
    s.clearance = 0;
    s.events = {.hit = 0, .landed = 0.22, .liftedOff = false, .cut = false};
    go(s);
  }
  assert(t.step == "release");
  // let go on the mark
  load.hooked = false;
  load.x = zone.x + 0.1;
  load.z = zone.z;
  CraneSnapshot s = up;
  s.time = time + 1;
  s.hook = {zone.x, 4, zone.z};
  const WorkEvents ev = t.update(s);
  assert(ev.taskDone);  // the guided task is done
  assert(t.stage == "tasks");
  assert(t.task == 1);
  const TaskResult& r = t.results[0];
  assert(std::abs(r.offset - 0.1) < 1e-9 && std::abs(r.swing - 0.9) < 1e-9 && std::abs(r.landing - 0.22) < 1e-9);
  const std::vector<std::string> want = {"park", "operate", "beams", "jacks", "level", "raise", "over", "lower", "attach", "lift", "move", "setDown", "release"};
  assert(seen == want);
}

void testTasks() {
  // the tasks after the guide: on the truck the load must lie on its bed, not beside it; the faults are told once in a while
  const YardFrame f(YARD);
  CraneTasks t("mobile", f, flat, 0);
  // the guide's task is done
  const CraneTasks::Target g = *t.target();
  g.load->x = g.zone->x;
  g.load->z = g.zone->z;
  {
    CraneSnapshot s = snapshot();
    s.time = 2;
    t.update(s);
  }
  assert(t.stage == "tasks");
  const CraneTasks::Target tg = *t.target();
  WorldLoad& load = *tg.load;
  const WorldZone& zone = *tg.zone;
  assert(zone.id == "mTruck");
  // set down beside the truck, on the ground: not done
  load.x = zone.x;
  load.z = zone.z;
  load.y = 0;
  assert(!t.inPlace(load, zone));
  {
    CraneSnapshot s = snapshot();
    s.time = 3;
    t.update(s);
  }
  assert(t.task == 1);
  // on its bed
  load.y = zone.y;
  assert(t.inPlace(load, zone));
  // the faults: an overload cut-off with the load on, a hit, a hard landing – each told, and not again at once
  load.y = 0;
  CraneSnapshot s = snapshot();
  s.time = 4;
  s.hooked = load.id;
  s.operating = true;
  s.lmi.stop = true;
  s.lmi.ratio = 1.04;
  s.events = {.hit = 0.6, .landed = 0.7, .liftedOff = false, .cut = true};
  const WorkEvents ev = t.update(s);
  std::vector<std::string> ids;
  for (const CraneFault& x : ev.faults) ids.push_back(x.id);
  std::sort(ids.begin(), ids.end());
  assert((ids == std::vector<std::string>{"craneHardLanding", "craneLoadHit", "craneOverload"}));
  s.time = 4.5;
  s.events = {.hit = 0.6, .landed = 0, .liftedOff = false, .cut = true};
  const WorkEvents again = t.update(s);
  assert(again.faults.empty());  // not told again at once
  // a load lifted with the hook off its middle is pulled at an angle
  CraneSnapshot s2 = snapshot();
  s2.time = 20;
  s2.hooked = load.id;
  s2.operating = true;
  s2.head = {3, 10, 0};
  s2.hook = {0, 4, 0};
  s2.events = {.hit = 0, .landed = 0, .liftedOff = true, .cut = false};
  const WorkEvents ev2 = t.update(s2);
  assert(ev2.faults.size() == 1 && ev2.faults[0].id == "craneDiagonalPull");
}

void testReachable() {
  // which load the hook can take: its crane's own, the hook low over its middle
  const YardFrame f(YARD);
  CraneTasks t("tower", f, flat, 0);
  const WorldLoad& l = loadById(t, "tB2");
  const double top = l.y + 2 * l.def.hy;
  const double drop = TOWER_CRANE.hookDrop;
  const WorldLoad* r = t.reachable({l.x + 0.3, top + drop + 0.2, l.z}, drop);
  assert(r && r->id == "tB2");
  assert(t.reachable({l.x + 0.3, top + drop + 3, l.z}, drop) == nullptr);  // too high above it
  assert(t.reachable({l.x + 2, top + drop + 0.2, l.z}, drop) == nullptr);  // beside it
  const WorldLoad& fixed = *std::find_if(t.loads.begin(), t.loads.end(), [](const WorldLoad& x) { return x.fixed; });
  assert(t.reachable({fixed.x, fixed.y + 2 * fixed.def.hy + drop, fixed.z}, drop) == nullptr);  // nobody's to move
}

// ------------------------------------------------------------------------------------ a whole task with the real crane

/** The world round the crane as the game makes it (game/craneplay.ts): the site's slabs and solids, the loads lying about. */
CraneWorld siteCraneWorld(const SiteWorld& geo, const std::vector<WorldLoad>& loads) {
  CraneWorld w;
  w.floor = [&geo, &loads](double x, double z, double hx, double hz, double y) {
    double best = 0;
    for (const CraneBoxY& p : geo.platforms)
      if (p.y <= y + 0.05 && p.y > best && !(x + hx <= p.x0 || x - hx >= p.x1 || z + hz <= p.z0 || z - hz >= p.z1)) best = p.y;
    for (const WorldLoad& l : loads) {
      if (l.hooked) continue;
      const double t = l.y + 2 * l.def.hy;
      const auto [fx, fz] = loadFootprint(l.def, l.yaw);
      if (t <= y + 0.05 && t > best && !(x + hx <= l.x - fx || x - hx >= l.x + fx || z + hz <= l.z - fz || z - hz >= l.z + fz)) best = t;
    }
    return best;
  };
  w.boxes = [&geo, &loads]() {
    std::vector<CraneBox3> out = geo.solids;
    for (const WorldLoad& l : loads) {
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
    return out;
  };
  w.ground = flat;
  w.wind = []() { return CraneWind{.x = 0, .z = 0, .gust = 1}; };
  return w;
}

void testWholeTask() {
  // a whole task with the tower crane: over the pallet, slung, lifted, carried, set down on its mark – judged
  const YardFrame f(YARD);
  CraneTasks tasks("tower", f, flat, 0);
  const SiteWorld geo = siteWorld(f, 0);
  const CraneWorld world = siteCraneWorld(geo, tasks.loads);
  Crane c("tower");
  const auto [bx, bz] = f.toWorld(TOWER_SITE.u, TOWER_SITE.w);
  c.place(bx, 0, bz, f.yawToWorld(TOWER_SITE.yaw));
  const CraneTasks::Target tg = *tasks.target();
  WorldLoad& load = *tg.load;
  const WorldZone& zone = *tg.zone;
  const double DT = 1.0 / 240;
  double time = 0;
  CraneEvents events;
  /** steer the crane's head to (x, z) and its hook to height y – gently, as an operator would */
  const auto steer = [&](double x, double z, double y) {
    const auto [cx, cz] = c.toCarrier(x, z);
    const double slew = std::atan2(-cx, -cz);
    double ds = slew - c.q.slew;
    ds -= M_PI * 2 * jsRound(ds / (M_PI * 2));
    const double r = std::hypot(cx, cz);
    c.cmd.slew = std::max(-1.0, std::min(1.0, ds / 0.3));
    c.cmd.boom = std::max(-1.0, std::min(1.0, (r - c.q.trolley) / 4));
    c.cmd.hoist = std::max(-1.0, std::min(1.0, (y - c.hook.y) / 2));
    return std::hypot(c.head.x - x, c.head.z - z);
  };
  const auto snap = [&]() {
    CraneSnapshot s = snapshot();
    s.time = time;
    s.operating = true;
    s.stowed = false;
    s.head = {c.head.x, c.head.y, c.head.z};
    s.hook = {c.hook.x, c.hook.y, c.hook.z};
    s.hooked = c.load ? std::optional<std::string>(c.load->id) : std::nullopt;
    s.resting = c.resting;
    s.clearance = c.clearance;
    s.swing = c.swing;
    s.lmi = c.lmi;
    s.events = events;
    s.tipped = c.tipped.has_value();
    return s;
  };
  const auto step = [&]() {
    c.step(DT, world);
    time += DT;
    events.hit = std::max(events.hit, c.events.hit);
    events.landed = std::max(events.landed, c.events.landed);
    events.liftedOff = events.liftedOff || c.events.liftedOff;
    events.cut = events.cut || c.events.cut;
    if (c.load) {
      load.x = c.hook.x;
      load.z = c.hook.z;
      load.y = c.hook.y - c.drop();
    }
    // (the work looks a few times a second, as the game's frames do)
    if (static_cast<long>(jsRound(time / DT)) % 8 == 0) {
      tasks.update(snap());
      events.hit = 0;
      events.landed = 0;
      events.liftedOff = false;
      events.cut = false;
    }
  };
  const auto until = [&](double seconds, const std::function<bool()>& done) {
    for (int i = 0; i < seconds / DT; i++) {
      step();
      if (done()) return true;
    }
    return false;
  };
  const double top = load.y + 2 * load.def.hy;
  /** the hook hangs still: near its plumb line and hardly moving across it (not just passing through it) */
  const auto calm = [&](double off) { return c.swing < off && std::hypot(c.hook.vx - c.head.vx, c.hook.vz - c.head.vz) < 0.06; };
  const auto hookPoint = [&]() { return CranePoint{c.hook.x, c.hook.y, c.hook.z}; };
  // over the pallet, the hook high, then calm
  assert(until(300, [&] { return steer(load.x, load.z, 14) < 0.15 && calm(0.15) && std::abs(c.v.slew) < 0.003; }));
  // down onto it, creeping at the end
  assert(until(120, [&] {
    steer(load.x, load.z, top - 1);
    return tasks.reachable(hookPoint(), TOWER_CRANE.hookDrop) != nullptr && c.hook.y - TOWER_CRANE.hookDrop - top < 0.3;
  }));
  c.cmd.hoist = 0;
  until(1, [] { return false; });
  const WorldLoad* l = tasks.reachable(hookPoint(), TOWER_CRANE.hookDrop);
  assert(l && l->id == load.id);  // the hook can take the pallet
  load.hooked = true;
  c.attach({.id = load.id, .t = load.def.t, .hx = load.def.hx, .hy = load.def.hy, .hz = load.def.hz, .sling = load.def.sling, .yaw = load.yaw, .area = load.def.area}, load.x, top, load.z);
  // up to a safe height, over the mark, let the swing settle, down onto it
  assert(until(120, [&] {
    steer(load.x, load.z, 12);
    return c.hook.y > 11.5;
  }));  // lifted
  if (!until(400, [&] { return steer(zone.x, zone.z, 12) < 0.12 && calm(0.12) && std::abs(c.v.slew) < 0.003 && std::abs(c.v.trolley) < 0.02; })) {
    std::fprintf(stderr, "not carried over the mark (%.2f m, swing %.2f)\n", std::hypot(c.head.x - zone.x, c.head.z - zone.z), c.swing);
    assert(false);
  }
  assert(until(200, [&] {
    steer(zone.x, zone.z, -5);
    return c.resting;
  }));  // set down
  // (standing: hands off the slewing and the trolley, only the rope paid out until the slings go slack)
  c.cmd.slew = 0;
  c.cmd.boom = 0;
  c.cmd.hoist = -0.3;
  until(6, [&] { return c.slack > 0.05; });
  c.cmd.hoist = 0;
  assert(c.tension < 0.3 * load.def.t * 9.81);  // the slings slack
  // let go: the load stands where it was set down
  load.x = c.hook.x;
  load.z = c.hook.z;
  load.y = c.hook.y - c.drop();
  c.release();
  load.hooked = false;
  until(2, [&] { return !tasks.results.empty(); });
  assert(tasks.results.size() == 1);  // judged
  const TaskResult& r = tasks.results[0];
  std::printf("  whole task: %.0f cm off the mark, score %.0f (%d stars), %.0f s, swing %.2f m, landing %.2f m/s, hits %d\n", r.offset * 100, r.score, r.stars, r.time, r.swing, r.landing, r.hits);
  assert(r.offset < 0.4);
  assert(r.hits == 0 && !r.overload);  // nothing hit, not overloaded
  assert(r.stars >= 1);                // a fair score
  assert(tasks.stage == "tasks");
  // (the pallet stands on the ground of its mark)
  assert(std::abs(load.y) < 0.02);
}

}  // namespace

int main() {
  testSite();
  testReach();
  testScore();
  testWorld();
  testGuideMobile();
  testTasks();
  testReachable();
  testWholeTask();
  std::printf("cranetasks_test: all passed\n");
  return 0;
}
