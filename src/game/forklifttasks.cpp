// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/forklifttasks.ts @ v1.7.7
#include "game/forklifttasks.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"

namespace fs {

namespace {

constexpr double NORTH = 0;
static_assert(FORK_START.yaw == NORTH);

/** The pallets: their ids, where they stand (u, w, the level: 0 the ground, 1 and 2 the rack's beams), what they carry. */
struct PalletPlan {
  std::string id;
  double u;
  double w;
  int level;
  PalletDef def;
};

const PalletDef CRATE = {.len = 1.2, .wid = 0.8, .base = 0.144, .goods = 1.0, .mass = 3600, .kind = 3};

std::vector<PalletPlan> plan() {
  const auto rack = [](const std::string& id, int bay, int slot, int level, double goods, double mass, int kind) {
    const SlotPlace s = slotOf(bay, slot);
    return PalletPlan{id, s.u, s.w, level, euroPallet(goods, mass, kind)};
  };
  return {
      {"guide", -28, -3, 0, euroPallet(0.9, 420, 0)},
      {"load1", -19.6, -4, 0, euroPallet(1.1, 640, 0)},
      {"load2", -18.2, -4, 0, euroPallet(0.8, 780, 1)},
      {"load3", -16.8, -4, 0, euroPallet(1.0, 560, 2)},
      {"top", slotOf(1, 0).u, -5.2, 0, euroPallet(0.9, 520, 0)},
      {"crate", -2, -6, 0, CRATE},
      // (what stands in the rack already)
      rack("r1", 0, 0, 1, 1.0, 600, 0),
      rack("r2", 0, 1, 2, 0.8, 450, 1),
      rack("r3", 1, 1, 1, 1.1, 700, 0),
      rack("r4", 2, 0, 2, 0.9, 500, 0),
      rack("r5", 2, 1, 1, 1.0, 650, 2),
      rack("r6", 0, 1, 0, 1.0, 800, 1),
      rack("r7", 2, 0, 0, 1.1, 700, 0),
  };
}

const std::array<const char*, 5> GUIDE_STEPS = {"approach", "lift", "tilt", "drive", "setdown"};

struct TaskDef {
  const char* id;
  const char* text;
  double par;
};
const std::array<TaskDef, 2> TASKS = {TaskDef{"truck", "forkTask.truck", 300}, TaskDef{"top", "forkTask.top", 200}};

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

}  // namespace

SlotPlace slotOf(int bay, int slot) {
  const double mid = RACK.u0 + RACK.bay * (bay + 0.5);
  return {mid + (slot == 0 ? -0.55 : 0.55), (RACK.wFront + RACK.wBack) / 2};
}

std::vector<Pallet*> ForkSite::palletPtrs() {
  std::vector<Pallet*> out;
  for (Pallet& p : pallets) out.push_back(&p);
  return out;
}

ForkSite buildSite(const YardFrame& frame, double y0, const std::function<double(double, double)>& ground) {
  const double yaw = frame.yawToWorld(0);
  const auto box = [&](double u0, double u1, double w0, double w1, double ya, double yb) {
    const auto [x, z] = frame.toWorld((u0 + u1) / 2, (w0 + w1) / 2);
    return OBox{.x = x, .z = z, .hx = std::abs(u1 - u0) / 2, .hz = std::abs(w1 - w0) / 2, .yaw = yaw, .y0 = y0 + ya, .y1 = y0 + yb};
  };
  const auto shelf = [](const OBox& b, const std::string& id, double y) {
    Shelf s;
    static_cast<OBox&>(s) = b;
    s.id = id;
    s.y = y;
    return s;
  };
  ForkSite site;
  site.y0 = y0;
  std::vector<OBox>& obstacles = site.obstacles;
  std::vector<Shelf>& shelves = site.shelves;
  const RackPlan& R = RACK;
  const double h = R.upright / 2;
  for (int k = 0; k <= R.bays; k++) {
    const double u = R.u0 + k * R.bay;
    for (double w : {R.wFront, R.wBack}) obstacles.push_back(box(u - h, u + h, w - h, w + h, 0, R.height));
  }
  for (int b = 0; b < R.bays; b++) {
    const double ua = R.u0 + b * R.bay + h;
    const double ub = R.u0 + (b + 1) * R.bay - h;
    for (size_t i = 0; i < R.levels.size(); i++) {
      const double lv = R.levels[i];
      obstacles.push_back(box(ua, ub, R.wFront - 0.08, R.wFront, lv - R.beam, lv));
      obstacles.push_back(box(ua, ub, R.wBack, R.wBack + 0.08, lv - R.beam, lv));
      const OBox s = box(ua, ub, R.wBack, R.wFront, lv, lv);
      shelves.push_back(shelf(s, "rack" + std::to_string(i + 1) + ":" + std::to_string(b), y0 + lv));
    }
  }
  const TruckPlan& T = FORK_TRUCK;
  obstacles.push_back(box(T.bed.u0, T.bed.u1, T.bed.w0, T.bed.w1, T.bed.y - 0.15, T.bed.y));
  obstacles.push_back(box(T.chassis.u0, T.chassis.u1, T.chassis.w0, T.chassis.w1, T.chassis.y0, T.chassis.y1));
  obstacles.push_back(box(T.cab.u0, T.cab.u1, T.cab.w0, T.cab.w1, T.cab.y0, T.cab.y1));
  obstacles.push_back(box(T.head.u0, T.head.u1, T.head.w0, T.head.w1, T.head.y0, T.head.y1));
  for (double u : T.wheels) obstacles.push_back(box(u - T.wheelR, u + T.wheelR, T.bed.w0 + 0.05, T.bed.w1 - 0.05, 0, 2 * T.wheelR));
  const OBox bedBox = box(T.bed.u0, T.bed.u1, T.bed.w0, T.bed.w1, T.bed.y, T.bed.y);
  site.bed = shelf(bedBox, "truck", y0 + T.bed.y);
  shelves.push_back(site.bed);
  for (const PalletPlan& p : plan()) {
    const auto [x, z] = frame.toWorld(p.u, p.w);
    const double g = ground(x, z);
    const double y = p.level == 0 ? (std::isfinite(g) ? g : y0) : y0 + R.levels[size_t(p.level - 1)];
    const int bay = int(std::floor((p.u - R.u0) / R.bay));
    Pallet q;
    q.id = p.id;
    q.def = p.def;
    q.x = x;
    q.y = y;
    q.z = z;
    q.yaw = yaw;
    q.on = p.level == 0 ? "ground" : "rack" + std::to_string(p.level) + ":" + std::to_string(bay);
    site.pallets.push_back(q);
  }
  const GuideZone& Z = GUIDE_ZONE;
  site.zone = box(Z.u - Z.hu, Z.u + Z.hu, Z.w - Z.hw, Z.w + Z.hw, 0, 0);
  const SlotPlace ts = slotOf(1, 0);
  site.topSlot = box(ts.u - 0.45, ts.u + 0.45, R.wBack, R.wFront, R.levels[1], R.levels[1]);
  return site;
}

bool footprintIn(const Pallet& p, const OBox& b, double margin) {
  const double c = std::cos(p.yaw);
  const double s = std::sin(p.yaw);
  const double hx = p.def.wid / 2;
  const double hz = p.def.len / 2;
  for (const auto& [ax, az] : {std::pair{-hx, -hz}, std::pair{hx, -hz}, std::pair{hx, hz}, std::pair{-hx, hz}}) {
    if (!inBox(b, p.x + ax * c + az * s, p.z - ax * s + az * c, margin)) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------- the tasks

int ForkTasks::taskCount() const { return int(TASKS.size()); }

const Pallet& ForkTasks::pallet(const std::string& id) const {
  for (const Pallet& p : site.pallets)
    if (p.id == id) return p;
  return site.pallets.front();
}

void ForkTasks::fault(const std::string& severity) {
  if (severity == "major" || severity == "fail") major_++;
  else if (severity == "minor") minor_++;
}

bool ForkTasks::forksOut(const ForkSnapshot& s) const {
  if (s.load) return false;
  for (const Pallet* p : s.pallets)
    if (engagement(s.forks, *p).depth > 0) return false;
  return true;
}

int ForkTasks::onTruck() const {
  int n = 0;
  for (const char* id : {"load1", "load2", "load3"}) {
    const Pallet& p = pallet(id);
    if (p.on == "truck" && footprintIn(p, site.bed, 0.05)) n++;
  }
  return n;
}

ForkTargets ForkTasks::targets() const {
  if (stage == "guide") return {step < 4 && pallet("guide").on != "forks" ? std::vector<std::string>{"guide"} : std::vector<std::string>{}, step >= 3 ? &site.zone : nullptr, false};
  if (stage == "done") return {{}, nullptr, false};
  if (std::string(TASKS[size_t(task)].id) == "truck") {
    std::vector<std::string> ps;
    for (const char* id : {"load1", "load2", "load3"})
      if (pallet(id).on != "truck" && pallet(id).on != "forks") ps.push_back(id);
    return {ps, nullptr, true};
  }
  const Pallet& t = pallet("top");
  return {t.on == "forks" || startsWith(t.on, "rack2") ? std::vector<std::string>{} : std::vector<std::string>{"top"}, &site.topSlot, false};
}

ForkUpdate ForkTasks::update(const ForkSnapshot& s) {
  ForkUpdate out;
  if (!started_) {
    started_ = true;
    guideBegan_ = s.time;
  }
  peak_ = std::max(peak_, s.moment);
  if (stage == "done" || s.tipped) return out;
  if (stage == "guide") {
    const Pallet& g = pallet("guide");
    const bool onForks = g.on == "forks";
    const double above = g.y - s.ground;
    // (put down again before its place: from the start)
    if (step >= 1 && step <= 3 && !onForks) step = 0;
    bool done = false;
    switch (step) {
      case 0: done = onForks || (engagement(s.forks, g).fits && engagement(s.forks, g).depth >= 0.85); break;
      case 1: done = onForks && above > 0.07 && above < 0.45; break;
      case 2: done = onForks && s.tilt >= (2 * M_PI) / 180; break;
      case 3: done = onForks && std::hypot(g.x - site.zone.x, g.z - site.zone.z) < 1.6; break;
      case 4: done = !onForks && g.on == "ground" && inBox(site.zone, g.x, g.z, 0.3) && engagement(s.forks, g).depth == 0 && s.lift < 0.35; break;
    }
    if (done) {
      step++;
      out.stepDone = true;
      if (step >= int(GUIDE_STEPS.size())) {
        results.push_back(judge("forkTask.guide", s.time - guideBegan_, 240));
        beginTask(s.time);
        out.taskDone = true;
      }
    }
    return out;
  }
  const TaskDef& t = TASKS[size_t(task)];
  bool done = false;
  if (std::string(t.id) == "truck") done = onTruck() == 3 && forksOut(s);
  else {
    const Pallet& p = pallet("top");
    const Shelf* sh = nullptr;
    for (const Shelf& x : site.shelves)
      if (x.id == p.on) {
        sh = &x;
        break;
      }
    done = startsWith(p.on, "rack2") && sh && footprintIn(p, *sh, 0.12) && forksOut(s) && s.lift < 0.5;
  }
  if (done) {
    results.push_back(judge(t.text, s.time - began_, t.par));
    out.taskDone = true;
    task++;
    if (task >= int(TASKS.size())) {
      stage = "done";
      out.allDone = true;
    } else beginTask(s.time);
  }
  return out;
}

void ForkTasks::beginTask(double now) {
  stage = "task";
  began_ = now;
  minor_ = 0;
  major_ = 0;
  peak_ = 0;
}

ForkResult ForkTasks::judge(const std::string& text, double time, double par) const {
  const double slow = std::min(30.0, std::max(0.0, (time - par) / 5));
  const double score = std::max(0.0, jsRound(100 - 12 * minor_ - 30 * major_ - slow));
  const int stars = score >= 85 ? 3 : score >= 65 ? 2 : score >= 40 ? 1 : 0;
  return {text, time, score, stars, minor_, major_, peak_};
}

ForkInstruction ForkTasks::instruction(const ForkSnapshot& s) const {
  if (stage == "guide") {
    const std::string g = GUIDE_STEPS[size_t(std::min(step, int(GUIDE_STEPS.size()) - 1))];
    return {"forkGuide." + g, {}, "forkGuide." + g + "Hint"};
  }
  if (stage == "done") return {"forkDone", {}, ""};
  const TaskDef& t = TASKS[size_t(task)];
  if (std::string(t.id) == "truck") return {s.load ? "forkTask.truckCarry" : "forkTask.truck", {{"n", onTruck()}, {"max", 3}}, "forkTask.truckHint"};
  return {s.load && s.load->id == "top" ? "forkTask.topCarry" : "forkTask.top", {}, "forkTask.topHint"};
}

}  // namespace fs
