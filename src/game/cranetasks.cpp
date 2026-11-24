// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/cranetasks.ts @ v1.7.7
#include "game/cranetasks.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/js.h"

namespace fs {

const std::vector<SiteLoad> SITE_LOADS = {
    {.id = "xB1", .kind = "bricks", .u = -27, .w = 5, .yaw = 0},
    {.id = "xB2", .kind = "bricks", .u = -27, .w = 6.4, .yaw = 0},
    {.id = "xB3", .kind = "bricks", .u = -25.5, .w = 5, .yaw = 0},
    {.id = "xF1", .kind = "formwork", .u = 30, .w = 12, .yaw = 0},
    {.id = "xF2", .kind = "formwork", .u = 30, .w = 15, .yaw = 0},
    {.id = "xM1", .kind = "mesh", .u = 32, .w = 20, .yaw = 0, .bearers = true},
};

const std::map<std::string, CraneWorkPlan> PLANS = {
    {"tower",
     {
         .type = "tower",
         .loads =
             {
                 {.id = "tB1", .kind = "bricks", .u = -12, .w = 8, .yaw = 0},
                 {.id = "tB2", .kind = "bricks", .u = -12, .w = 10.5, .yaw = 0},
                 {.id = "tS1", .kind = "beam", .u = -16, .w = 15, .yaw = 0, .bearers = true},
                 {.id = "tC1", .kind = "container", .u = -22, .w = 16.5, .yaw = 0},
             },
         .zones =
             {
                 {.id = "tG1", .u = 2, .w = 10, .yaw = 0, .hu = 0.75, .hw = 0.65, .y = std::nullopt},
                 {.id = "tPlat", .u = 15.6, .w = -7.3, .yaw = 0, .hu = 0.8, .hw = 0.6, .y = BUILDING.platform.y},
                 {.id = "tRoof", .u = 16, .w = 2, .yaw = 0, .hu = 0.9, .hw = 0.5, .y = BUILDING.floors[3]},
                 {.id = "tCont", .u = 24, .w = -12.5, .yaw = 0, .hu = 0.7, .hw = 0.6, .y = std::nullopt},
             },
         .tasks =
             {
                 {.load = "tB1", .zone = "tG1", .text = "craneTask.bricksHere", .par = 120},
                 {.load = "tB2", .zone = "tPlat", .text = "craneTask.bricksPlatform", .par = 110},
                 {.load = "tS1", .zone = "tRoof", .text = "craneTask.beamRoof", .par = 130},
                 {.load = "tC1", .zone = "tCont", .text = "craneTask.containerThere", .par = 150},
             },
     }},
    {"mobile",
     {
         .type = "mobile",
         .loads =
             {
                 {.id = "mB1", .kind = "bricks", .u = -2, .w = -7, .yaw = 0},
                 {.id = "mB2", .kind = "bricks", .u = 0.2, .w = -7, .yaw = 0},
                 {.id = "mS1", .kind = "beam", .u = -19, .w = -7.5, .yaw = 0, .bearers = true},
                 {.id = "mC1", .kind = "container", .u = -24, .w = -10.6, .yaw = 0},
             },
         .zones =
             {
                 {.id = "mG1", .u = -16, .w = -4.5, .yaw = 0, .hu = 0.75, .hw = 0.65, .y = std::nullopt},
                 {.id = "mTruck", .u = 8.2, .w = -20.5, .yaw = 0, .hu = 0.9, .hw = 0.4, .y = DELIVERY.bed.y},
                 {.id = "mRoof", .u = 15, .w = -2.2, .yaw = 0, .hu = 0.9, .hw = 0.5, .y = BUILDING.floors[3]},
                 {.id = "mCont", .u = -19, .w = 6, .yaw = 0, .hu = 0.7, .hw = 0.6, .y = std::nullopt},
             },
         .tasks =
             {
                 {.load = "mB1", .zone = "mG1", .text = "craneTask.bricksHere", .par = 120},
                 {.load = "mB2", .zone = "mTruck", .text = "craneTask.bricksTruck", .par = 110},
                 {.load = "mS1", .zone = "mRoof", .text = "craneTask.beamRoof", .par = 150},
                 {.load = "mC1", .zone = "mCont", .text = "craneTask.containerThere", .par = 150},
             },
     }},
    {"loader",
     {
         .type = "loader",
         .loads =
             {
                 {.id = "lP1", .kind = "bricks", .u = 0, .w = 0, .yaw = 0, .bed = BedPlace{.x = -0.62, .z = -0.32, .yaw = 0}},
                 {.id = "lP2", .kind = "bricks", .u = 0, .w = 0, .yaw = 0, .bed = BedPlace{.x = 0.62, .z = -0.32, .yaw = 0}},
                 {.id = "lM1", .kind = "mesh", .u = 0, .w = 0, .yaw = 0, .bed = BedPlace{.x = 0, .z = 2.7, .yaw = M_PI / 2}},
                 {.id = "lF1", .kind = "formwork", .u = -33.5, .w = -13.2, .yaw = 0},
             },
         .zones =
             {
                 {.id = "lG1", .u = -30.5, .w = -13, .yaw = 0, .hu = 0.75, .hw = 0.65, .y = std::nullopt},
                 {.id = "lG2", .u = -27.7, .w = -13, .yaw = 0, .hu = 0.6, .hw = 0.5, .y = std::nullopt},
                 {.id = "lG3", .u = -38.5, .w = -13, .yaw = 0, .hu = 0.7, .hw = 0.45, .y = std::nullopt, .bearers = true},
                 // (anywhere on the flatbed: its middle within)
                 {.id = "lBed", .u = 0, .w = 0, .yaw = 0, .hu = 1.0, .hw = 0.2, .y = LOADER_BED.y, .bed = BedPoint{.x = 0, .z = (LOADER_BED.z0 + LOADER_BED.z1) / 2}},
             },
         .tasks =
             {
                 {.load = "lP1", .zone = "lG1", .text = "craneTask.unloadPallet", .par = 100},
                 {.load = "lP2", .zone = "lG2", .text = "craneTask.unloadPallet2", .par = 90},
                 {.load = "lM1", .zone = "lG3", .text = "craneTask.meshBearers", .par = 110},
                 {.load = "lF1", .zone = "lBed", .text = "craneTask.formworkBed", .par = 110},
             },
     }},
};

const std::map<std::string, std::vector<GuideStep>> GUIDE = {
    {"tower", {"over", "lower", "attach", "lift", "move", "setDown", "release"}},
    {"mobile", {"drive", "park", "operate", "beams", "jacks", "level", "raise", "over", "lower", "attach", "lift", "move", "setDown", "release"}},
    {"loader", {"drive", "park", "operate", "beams", "jacks", "level", "raise", "over", "lower", "attach", "lift", "move", "setDown", "release"}},
};

// ------------------------------------------------------------------------------------ in the world

std::vector<CraneBoxY> bearersOf(const WorldZone& zone, const LoadDef& def, double ground) {
  const double along = def.hx > 1.5 ? def.hx - 0.8 : def.hx - 0.2;
  const double c = std::cos(zone.yaw);
  const double s = std::sin(zone.yaw);
  std::vector<CraneBoxY> out;
  for (const double k : {-along, along}) {
    // (each 12 cm square, as long as the load is wide and a little more)
    std::vector<std::array<double, 2>> pts;
    const double corners[4][2] = {
        {k - 0.06, -def.hz - 0.15},
        {k + 0.06, -def.hz - 0.15},
        {k - 0.06, def.hz + 0.15},
        {k + 0.06, def.hz + 0.15},
    };
    for (const auto& [du, dw] : corners) pts.push_back({zone.x + du * c + dw * s, zone.z - du * s + dw * c});
    double x0 = std::numeric_limits<double>::infinity(), x1 = -x0, z0 = x0, z1 = -x0;
    for (const auto& p : pts) {
      x0 = std::min(x0, p[0]);
      x1 = std::max(x1, p[0]);
      z0 = std::min(z0, p[1]);
      z1 = std::max(z1, p[1]);
    }
    CraneBoxY b;
    b.x0 = x0;
    b.x1 = x1;
    b.z0 = z0;
    b.z1 = z1;
    b.y0 = ground - 0.5;
    b.y1 = ground + BEARERS;
    b.y = ground + BEARERS;
    out.push_back(b);
  }
  return out;
}

CraneBox3 worldBox(const SitePlacement& p, const SiteRect& r, double y0, double y1) {
  const auto [ax, az] = p.toWorld(r.u0, r.w0);
  const auto [bx, bz] = p.toWorld(r.u1, r.w1);
  CraneBox3 b;
  b.x0 = std::min(ax, bx);
  b.x1 = std::max(ax, bx);
  b.z0 = std::min(az, bz);
  b.z1 = std::max(az, bz);
  b.y0 = y0;
  b.y1 = y1;
  return b;
}

std::array<double, 2> loadFootprint(const LoadDef& def, double yaw) {
  const double c = std::abs(std::cos(yaw));
  const double s = std::abs(std::sin(yaw));
  return {def.hx * c + def.hz * s, def.hx * s + def.hz * c};
}

SiteWorld siteWorld(const SitePlacement& p, double y0) {
  const Building& b = BUILDING;
  std::vector<CraneBoxY> platforms;
  std::vector<CraneBox3> solids;
  const auto plat = [&](const SiteRect& r, double y) {
    CraneBoxY box;
    static_cast<CraneBox3&>(box) = worldBox(p, r, y0 - 2, y0 + y);
    box.y = y0 + y;
    platforms.push_back(box);
  };
  // the building: solid up to its roof (nothing is put inside it from the crane), the roof to set things on
  solids.push_back(worldBox(p, b, y0 - 2, y0 + b.floors[3]));
  plat(b, b.floors[3]);
  solids.push_back(worldBox(p, b.core, y0 - 2, y0 + b.core.top));
  plat(b.core, b.core.top);
  // the loading platform at the second floor, its rails at the sides
  plat(b.platform, b.platform.y);
  solids.push_back(worldBox(p, {.u0 = b.platform.u0 - 0.06, .u1 = b.platform.u0, .w0 = b.platform.w0, .w1 = b.platform.w1}, y0 + b.platform.y, y0 + b.platform.y + 1.1));
  solids.push_back(worldBox(p, {.u0 = b.platform.u1, .u1 = b.platform.u1 + 0.06, .w0 = b.platform.w0, .w1 = b.platform.w1}, y0 + b.platform.y, y0 + b.platform.y + 1.1));
  for (const SiteRect& s : b.scaffold) {
    solids.push_back(worldBox(p, s, y0 - 2, y0 + b.scaffoldTop));
    plat(s, b.scaffoldTop);
  }
  for (const SiteRect& r : b.rails) solids.push_back(worldBox(p, r, y0 + b.floors[3], y0 + b.railTop));
  // the tower crane: its ballast and its mast
  const TowerSite& t = TOWER_SITE;
  solids.push_back(worldBox(p, {.u0 = t.u - t.base, .u1 = t.u + t.base, .w0 = t.w - t.base, .w1 = t.w + t.base}, y0 - 2, y0 + t.ballastTop));
  plat({.u0 = t.u - t.base, .u1 = t.u + t.base, .w0 = t.w - t.base, .w1 = t.w + t.base}, t.ballastTop);
  solids.push_back(worldBox(p, {.u0 = t.u - t.mast, .u1 = t.u + t.mast, .w0 = t.w - t.mast, .w1 = t.w + t.mast}, y0, y0 + t.mastTop));
  // the delivery truck: its cab and chassis, its bed
  const Delivery& d = DELIVERY;
  solids.push_back(worldBox(p, d.cab, y0 + d.cab.y0, y0 + d.cab.y1));
  solids.push_back(worldBox(p, d.chassis, y0 + d.chassis.y0, y0 + d.chassis.y1));
  plat(d.bed, d.bed.y);
  for (const SiteSolid& o : OFFICES) {
    solids.push_back(worldBox(p, o, y0 - 2, y0 + o.y1));
    plat(o, o.y1);
  }
  return {std::move(platforms), std::move(solids)};
}

std::vector<WorldLoad> siteLoads(const CraneType& type, const SitePlacement& p, const std::function<double(double, double)>& ground) {
  std::vector<WorldLoad> out;
  const auto add = [&](const SiteLoad& l, bool fixed) {
    const LoadDef& def = LOAD_DEFS.at(l.kind);
    const auto [x, z] = p.toWorld(l.u, l.w);
    const double yaw = p.yawToWorld(l.yaw);
    const auto [fx, fz] = loadFootprint(def, yaw);
    // (on the highest point of the ground under it)
    double y = -std::numeric_limits<double>::infinity();
    const double offs[5][2] = {{0, 0}, {-fx, -fz}, {fx, -fz}, {-fx, fz}, {fx, fz}};
    for (const auto& [dx, dz] : offs) {
      const double g = ground(x + dx, z + dz);
      if (std::isfinite(g)) y = std::max(y, g);
    }
    if (!std::isfinite(y)) y = 0;
    out.push_back({.id = l.id, .def = def, .x = x, .y = y + (l.bearers ? BEARERS : 0), .z = z, .yaw = yaw, .fixed = fixed, .hooked = false, .bed = l.bed, .bearers = l.bearers});
  };
  for (const SiteLoad& l : PLANS.at(type).loads) add(l, false);
  for (const SiteLoad& l : SITE_LOADS) add(l, true);
  return out;
}

std::vector<WorldZone> siteZones(const CraneType& type, const SitePlacement& p, double y0) {
  std::vector<WorldZone> out;
  for (const SiteZone& zn : PLANS.at(type).zones) {
    const auto [x, z] = p.toWorld(zn.u, zn.w);
    out.push_back({.id = zn.id, .x = x, .z = z, .yaw = p.yawToWorld(zn.yaw), .hu = zn.hu, .hw = zn.hw, .y = !zn.y ? std::nan("") : y0 + *zn.y, .bed = zn.bed, .bearers = zn.bearers});
  }
  return out;
}

ZonePoint inZone(const WorldZone& zone, double x, double z) {
  // (the zone's axes: its yaw turns u (east at 0) and w (south at 0) like the yard's frame)
  const double c = std::cos(zone.yaw);
  const double s = std::sin(zone.yaw);
  const double dx = x - zone.x;
  const double dz = z - zone.z;
  return {.du = dx * c - dz * s, .dw = dx * s + dz * c};
}

double yawOff(double a, double b) {
  double d = std::fmod(std::abs(a - b), M_PI);
  if (d > M_PI / 2) d = M_PI - d;
  return d;
}

// ------------------------------------------------------------------------------------ the work

TaskScore scoreTask(const TaskJudged& r) {
  const auto lin = [](double v, double good, double bad) { return std::max(0.0, std::min(1.0, (bad - v) / (bad - good))); };
  const double score = jsRound(30 * lin(r.offset, 0.1, 0.6) + 25 * lin(r.swing, 0.3, 1.8) + 15 * lin(r.landing, 0.15, 0.75) + 15 * lin(r.time, r.par, r.par * 3) +
                               std::max(0.0, 15 - 7.5 * r.hits - (r.overload ? 7.5 : 0)));
  return {.score = score, .stars = score >= 85 ? 3 : score >= 60 ? 2 : score >= 35 ? 1 : 0};
}

namespace {
std::optional<CraneTasks::WorldStand> standOf(const CraneType& type, const SitePlacement& place) {
  if (type == "tower") return std::nullopt;
  const Stand& s = STANDS.at(type);
  const auto [x, z] = place.toWorld(s.pose.u, s.pose.w);
  return CraneTasks::WorldStand{.x = x, .z = z, .yaw = place.yawToWorld(s.pose.yaw), .tol = s.tol};
}
}  // namespace

CraneTasks::CraneTasks(const CraneType& type, const SitePlacement& place, const std::function<double(double, double)>& ground, double y0)
    : type(type), plan(PLANS.at(type)), loads(siteLoads(type, place, ground)), zones(siteZones(type, place, y0)), step(GUIDE.at(type)[0]), stand(standOf(type, place)) {}

const SiteTask* CraneTasks::current() const {
  if (stage == "done") return nullptr;
  return task < static_cast<int>(plan.tasks.size()) ? &plan.tasks[task] : nullptr;
}

std::optional<CraneTasks::Target> CraneTasks::target() {
  const SiteTask* t = current();
  if (!t) return std::nullopt;
  WorldLoad* load = nullptr;
  for (WorldLoad& l : loads)
    if (l.id == t->load) {
      load = &l;
      break;
    }
  const WorldZone* zone = nullptr;
  for (const WorldZone& z : zones)
    if (z.id == t->zone) {
      zone = &z;
      break;
    }
  if (load && zone) return Target{load, zone};
  return std::nullopt;
}

WorldLoad* CraneTasks::reachable(const CranePoint& hook, double hookDrop) {
  WorldLoad* best = nullptr;
  double bestD = std::numeric_limits<double>::infinity();
  for (WorldLoad& l : loads) {
    if (l.fixed || l.hooked) continue;
    const double d = std::hypot(hook.x - l.x, hook.z - l.z);
    const double top = l.y + 2 * l.def.hy;
    const double above = hook.y - hookDrop - top;
    if (d <= ATTACH_REACH && above >= -0.25 && above <= ATTACH_ABOVE && d < bestD) {
      best = &l;
      bestD = d;
    }
  }
  return best;
}

bool CraneTasks::inStand(const CraneSnapshot& s) const {
  if (!stand) return true;
  const WorldStand& st = *stand;
  // (in the stand's own frame: along the way the vehicle faces, and across)
  const double fx = -std::sin(st.yaw);
  const double fz = -std::cos(st.yaw);
  const double dx = s.vx - st.x;
  const double dz = s.vz - st.z;
  const double along = dx * fx + dz * fz;
  const double across = dx * -fz + dz * fx;
  double dy = std::fmod(std::abs(s.yaw - st.yaw), M_PI * 2);
  if (dy > M_PI) dy = M_PI * 2 - dy;
  return std::abs(along) <= st.tol.u && std::abs(across) <= st.tol.w && dy <= st.tol.yaw;
}

bool CraneTasks::inPlace(const WorldLoad& load, const WorldZone& zone) const {
  if (load.hooked) return false;
  if (zone.bed && !load.bed) return false;
  if (zone.bed && load.bed) return std::abs(load.bed->x - zone.bed->x) <= zone.hw + 0.4 && std::abs(load.bed->z - zone.bed->z) <= zone.hu + 1.0;
  const ZonePoint p = inZone(zone, load.x, load.z);
  if (std::abs(p.du) > zone.hu || std::abs(p.dw) > zone.hw) return false;
  if (yawOff(load.yaw, zone.yaw) > 0.35) return false;
  // (on the surface meant – not on the ground under the platform)
  return std::isnan(zone.y) || std::abs(load.y - zone.y) < 0.3;
}

double CraneTasks::offset(const WorldLoad& load, const WorldZone& zone) const {
  if (zone.bed && load.bed) return std::abs(load.bed->x - zone.bed->x);
  return std::hypot(load.x - zone.x, load.z - zone.z);
}

void CraneTasks::fault(std::vector<CraneFault>& out, double time, const std::string& id, const std::string& severity, const CraneFaultData& data, double every) {
  const auto it = faultT_.find(id);
  const double at = it != faultT_.end() ? it->second : -std::numeric_limits<double>::infinity();
  if (time - at < every) return;
  faultT_[id] = time;
  out.push_back({id, severity, data});
}

WorkEvents CraneTasks::update(const CraneSnapshot& s) {
  WorkEvents ev;
  if (stage == "done") return ev;
  Cur& cur = cur_;
  if (cur.start == 0) cur.start = s.time;
  const std::optional<Target> tg = target();
  const bool hooked = s.hooked && tg && *s.hooked == tg->load->id;
  // what is watched while a load hangs
  if (s.hooked && !s.hooked->empty()) {
    if (!s.resting) {
      if (!cur.lifted) cur.lifted = true;
      cur.swing = std::max(cur.swing, s.swing);
      cur.peak = std::max(cur.peak, s.lmi.ratio);
      if (s.swing > 2) fault(ev.faults, s.time, "craneSwing", "minor", {{"m", jsRound(s.swing * 10) / 10}}, 10);
      if (s.lmi.wind > s.lmi.windLimit) fault(ev.faults, s.time, "craneWind", "major", {{"v", jsRound(s.lmi.wind)}, {"max", s.lmi.windLimit}}, 15);
    }
    if (s.events.landed > 0 && hooked) cur.landing = s.events.landed;
    if (s.events.landed > 0.5) fault(ev.faults, s.time, "craneHardLanding", "minor", {{"v", jsRound(s.events.landed * 10) / 10}}, 4);
    if (s.events.liftedOff) {
      // lifted with the hook not over the load's middle: it is dragged and swings off (no diagonal pull)
      const double off = std::hypot(s.head.x - s.hook.x, s.head.z - s.hook.z);
      if (off > 0.6) fault(ev.faults, s.time, "craneDiagonalPull", "minor", {{"m", jsRound(off * 10) / 10}}, 4);
    }
  }
  if (s.events.cut && (s.lmi.stop || s.lmi.notLevel)) {
    if (s.lmi.stop) {
      if (hooked) cur.overload = true;
      fault(ev.faults, s.time, "craneOverload", "major", {{"p", jsRound(s.lmi.ratio * 100)}}, 8);
    } else
      fault(ev.faults, s.time, "craneNotLevel", "minor", {{"d", jsRound(s.lmi.incline * 10) / 10}}, 10);
  }
  if (s.events.hit > 0.25) {
    if (s.hooked && !s.hooked->empty()) {
      if (hooked) cur.hits++;
      fault(ev.faults, s.time, "craneLoadHit", "major", {{"v", jsRound(s.events.hit * 10) / 10}}, 3);
    } else if (s.events.hit > 0.6)
      fault(ev.faults, s.time, "craneHookHit", "minor", {}, 5);
  }
  if (s.tipped) fault(ev.faults, s.time, "craneTipped", "fail", {}, 1000);
  // the guide
  if (stage == "guide" && step) {
    if (stepDone(*step, s, tg)) {
      ev.stepDone = true;
      stepIndex_++;
      const std::vector<GuideStep>& steps = GUIDE.at(type);
      step = stepIndex_ < static_cast<int>(steps.size()) ? std::optional<GuideStep>(steps[stepIndex_]) : std::nullopt;
    }
  }
  // a task is done once its load lies where it should, let go
  if (tg && inPlace(*tg->load, *tg->zone)) {
    const SiteTask& pt = plan.tasks[task];
    const TaskScore sc = scoreTask({.time = s.time - cur.start, .par = pt.par, .offset = offset(*tg->load, *tg->zone), .swing = cur.swing, .landing = cur.landing, .overload = cur.overload, .hits = cur.hits});
    results.push_back({
        .text = pt.text,
        .load = tg->load->def.name,
        .time = s.time - cur.start,
        .par = pt.par,
        .offset = offset(*tg->load, *tg->zone),
        .swing = cur.swing,
        .landing = cur.landing,
        .peak = cur.peak,
        .overload = cur.overload,
        .hits = cur.hits,
        .score = sc.score,
        .stars = sc.stars,
    });
    ev.taskDone = true;
    task++;
    stage = task >= static_cast<int>(plan.tasks.size()) ? "done" : "tasks";
    step = std::nullopt;
    cur_ = Cur{.start = s.time};
    if (stage == "done") ev.allDone = true;
  }
  return ev;
}

bool CraneTasks::stepDone(const GuideStep& step, const CraneSnapshot& s, const std::optional<Target>& tg) const {
  const std::optional<WorldStand>& st = stand;
  const bool isHooked = tg && s.hooked && *s.hooked == tg->load->id;
  // (set up somewhere else all the same: the guide goes on from there)
  if (step == "drive") return s.operating || (st && std::hypot(s.vx - st->x, s.vz - st->z) < 16);
  if (step == "park") return s.operating || (inStand(s) && std::abs(s.speed) < 0.2 && s.handbrake);
  if (step == "operate") return s.operating;
  if (step == "beams") return s.beams >= 0.98;
  if (step == "jacks") return type == "loader" ? s.padsDown : s.onOutriggers;
  if (step == "level") return s.onOutriggers && s.incline <= specOf(type).levelGood;
  if (step == "raise") return type == "mobile" ? s.luff >= 30 : s.unfolded;
  if (step == "over") return tg && std::hypot(s.head.x - tg->load->x, s.head.z - tg->load->z) < 1.2;
  if (step == "lower")
    return tg && (isHooked || (std::hypot(s.hook.x - tg->load->x, s.hook.z - tg->load->z) <= ATTACH_REACH && s.hook.y - (tg->load->y + 2 * tg->load->def.hy) < ATTACH_ABOVE + 1.0));
  if (step == "attach") return isHooked;
  if (step == "lift") return isHooked && !s.resting && s.clearance >= 1.5;
  if (step == "move") return isHooked && std::hypot(s.hook.x - tg->zone->x, s.hook.z - tg->zone->z) < 2;
  if (step == "setDown") return isHooked && s.resting && over(*tg->zone, s.hook.x, s.hook.z);
  if (step == "release") return tg && !tg->load->hooked && inPlace(*tg->load, *tg->zone);
  return false;
}

bool CraneTasks::over(const WorldZone& zone, double x, double z) const {
  if (zone.bed) return true;
  const ZonePoint p = inZone(zone, x, z);
  return std::abs(p.du) <= zone.hu + 0.3 && std::abs(p.dw) <= zone.hw + 0.3;
}

CraneInstruction CraneTasks::instruction(const CraneSnapshot& s) {
  const std::optional<Target> tg = target();
  if (stage == "done" || !tg) return {.key = "craneDone", .data = {}, .hint = ""};
  TrParams data{{"n", task + 1}, {"max", static_cast<int>(plan.tasks.size())}, {"load", tg->load->def.name}, {"d", specOf(type).levelGood}};
  if (stage == "guide" && step) {
    const std::string key = *step == "jacks" && type == "loader" ? "craneGuide.jacksLoader" : *step == "raise" && type == "loader" ? "craneGuide.unfold" : *step == "raise" ? "craneGuide.raise" : "craneGuide." + *step;
    return {.key = key, .data = data, .hint = hint(s, *tg)};
  }
  return {.key = plan.tasks[task].text, .data = data, .hint = hint(s, *tg)};
}

std::string CraneTasks::hint(const CraneSnapshot& s, const Target& tg) const {
  if (s.tipped) return "";
  if (s.lmi.stop) return "craneHint.overload";
  if (s.lmi.wind > s.lmi.windLimit) return "craneHint.wind";
  if (!s.operating) return type == "tower" ? "" : stage == "guide" ? "" : "craneHint.operate";
  if (s.lmi.notLevel) return "craneHint.level";
  const bool hasHook = s.hooked && !s.hooked->empty();
  if (hasHook && *s.hooked != tg.load->id) return "craneHint.wrongLoad";
  if (hasHook && *s.hooked == tg.load->id) {
    if (s.resting && over(*tg.zone, s.hook.x, s.hook.z)) return "craneHint.release";
    if (s.resting) return "craneHint.lift";
    if (s.swing > 0.8) return "craneHint.swing";
    return std::hypot(s.hook.x - tg.zone->x, s.hook.z - tg.zone->z) < 2 ? "craneHint.setDown" : "craneHint.toZone";
  }
  if (std::hypot(s.hook.x - tg.load->x, s.hook.z - tg.load->z) <= ATTACH_REACH) return "craneHint.attach";
  return "craneHint.toLoad";
}

}  // namespace fs
