// SPDX-License-Identifier: AGPL-3.0-only
// LINK: game sim world core platform geo i18n
// Origin: tests/sim/semitrailer.test.ts and tests/game/yardtask.test.ts @ v1.7.7
/**
 * The articulated lorry (sim/semitrailer.ts, game/semice.ts) and the coupling lesson with the real
 * vehicles (game/yardtask.ts): where the kingpin sits, its weight and its spring brakes, the
 * coupling lesson driven through with the truck and its drawbar trailer and with the tractor and
 * its semi-trailer.  (The original's cases with a towing vehicle moved by hand – the swept path and
 * the jack-knife limit – need a duck-typed vehicle; the jack-knife limit is driven here with the
 * real tractor instead.)
 */
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "game/semice.h"
#include "game/vehicles.h"
#include "game/yardtask.h"
#include "sim/payload.h"
#include "sim/semitrailer.h"
#include "sim/trailer.h"
#include "sim/vehicle/index.h"
#include "tests/vehicle_harness.h"

using namespace fs;
using namespace fs::harness;

namespace {

DriverInput in(double throttle = 0, double brake = 0, double handbrake = 0, int gear = 0) {
  DriverInput d;
  d.throttle = throttle;
  d.brake = brake;
  d.handbrake = handbrake;
  d.gear = gear;
  return d;
}

void geometry() {
  test("the kingpin stands over the tractor's rear axle, the trailer's axles 7.75 m behind it; 16.5 m in all");
  FS_OK(isSemiSpec(&SEMI_TRAILER) && !isSemiSpec(&TANDEM_TRAILER));
  PlaneGround ground;
  FS_OK(dynamic_cast<SemiTrailer*>(makeTrailer(SEMI_TRAILER, ground).get()) != nullptr);
  FS_OK(dynamic_cast<SemiTrailer*>(makeTrailer(TANDEM_TRAILER, ground).get()) == nullptr);
  Vehicle tractor = makeCar(ground, TRACTOR_UNIT);
  SemiTrailer t(SEMI_TRAILER, ground);
  t.coupleBehind(tractor);
  double hit[3];
  t.hitchPoint(tractor, hit);
  double mpos[3];
  tractor.getModelPosition(mpos);
  const double behind = hit[2] - mpos[2];
  FS_OK(std::abs(behind - FIFTH_WHEEL_AT) < 1e-6);
  FS_OK(behind < TRACTOR_UNIT.wheelbase / 2 && behind > TRACTOR_UNIT.wheelbase / 2 - 0.6);
  FS_OK(std::abs(hit[1] - mpos[1] - 1.15) < 0.01);
  FS_OK(std::abs(t.z - hit[2] - SEMI_TRAILER.hitchToAxle) < 1e-6);
  const TrailerOutline o = trailerOutline(SEMI_TRAILER);
  FS_OK(std::abs(o.front + o.rear - 13.6) < 1e-9);
  const double total = VEHICLES.at("semi").outline.front + behind + SEMI_TRAILER.length;
  FS_OK(std::abs(total - 16.5) < 0.05);
  FS_OK(VEHICLES.at("semi").licence == "CE");
  FS_OK(VEHICLES.at("semi").trailer == &SEMI_TRAILER);
  FS_OK(VEHICLES.at("truckSolo").licence == "C");
  FS_OK(VEHICLES.at("truckSolo").model != VEHICLES.at("lightTruck").model);
  FS_OK(SEMI_RIG.trailer.front > 9 && SEMI_RIG.trailerAxles.at.size() == 3);
}

void weight() {
  test("its weight rests on the fifth wheel; its spring brakes hold while its brake is set or the red line is off");
  PlaneGround ground;
  SemiTrailer t(SEMI_TRAILER, ground);
  FS_OK(t.kingpinLoad() == 0);
  Vehicle tractor0 = makeCar(ground, TRACTOR_UNIT);
  t.coupleBehind(tractor0);
  FS_OK(std::abs(t.kingpinLoad() - SEMI_TRAILER.mass * 9.81 * SEMI_TRAILER.kingpinShare) < 1e-6);
  FS_OK(!t.braked());
  t.supply = false;
  FS_OK(t.braked());
  t.supply = true;
  t.parkBrake = true;
  FS_OK(t.braked());
  // a real tractor cannot pull it away against its brakes
  Vehicle tractor = makeCar(ground, TRACTOR_UNIT);
  SemiTrailer s(SEMI_TRAILER, ground);
  s.coupleBehind(tractor);
  s.parkBrake = true;
  for (int i = 0; i < static_cast<int>(jsRound(4 / DT)); i++) {
    const DriverInput d = in(1, 0, 0, 1);
    tractor.step(DT, d);
    s.step(DT, tractor, d);
  }
  FS_OK(std::abs(tractor.speed) < 0.6);
}

void jackknife() {
  test("reversing with the wheel turned: the trailer swings round to the jack-knife limit and is held there, said once");
  PlaneGround ground;
  Vehicle tractor = makeCar(ground, TRACTOR_UNIT);
  SemiTrailer t(SEMI_TRAILER, ground);
  t.coupleBehind(tractor);
  int events = 0;
  double most = 0;
  for (int i = 0; i < static_cast<int>(jsRound(60 / DT)); i++) {
    DriverInput d = in(0, tractor.speed < -1.5 ? 0.3 : 0, 0, -1);
    d.steer = 1;
    tractor.step(DT, d);
    t.step(DT, tractor, d);
    if (t.jackknifeEvent) events++;
    most = std::max(most, std::abs(t.articulation));
  }
  FS_OK(t.jackknifed);
  FS_OK(events >= 1);
  FS_OK(most <= JACKKNIFE_LIMIT + 1e-9);
}

void payload() {
  test("a load makes the truck heavier, its centre of mass moves towards it, its inertia grows");
  FS_OK(withPayload(TRUCK, nullptr).mass == TRUCK.mass);
  Payload load{.mass = 3000, .z = 1.5, .y = 1.8, .len = 2.4, .wid = 2.0, .hgt = 1.2};
  const VehicleSpec s = withPayload(TRUCK, &load);
  FS_OK(s.mass == TRUCK.mass + 3000);
  FS_OK(s.cgHeight > TRUCK.cgHeight && s.cgToFront > TRUCK.cgToFront);
  FS_OK(s.inertia[0] > TRUCK.inertia[0] && s.inertia[1] > TRUCK.inertia[1] && s.inertia[2] > TRUCK.inertia[2]);
  FS_OK(s.name == TRUCK.name + ", 3000 kg geladen");
}

// ---- the coupling lesson with the real vehicles

struct Rig {
  PlaneGround ground;
  Vehicle truck;
  std::unique_ptr<Trailer> trailer;
  YardTask task;
  std::vector<std::string> faults;
  explicit Rig(bool semi)
      : truck(makeCar(ground, semi ? TRACTOR_UNIT : TRUCK)),
        trailer(semi ? std::unique_ptr<Trailer>(std::make_unique<SemiTrailer>(SEMI_TRAILER, ground)) : std::make_unique<Trailer>(TANDEM_TRAILER, ground)),
        task("couple", semi) {
    trailer->coupleBehind(truck);
  }
  SemiTrailer& semi() { return dynamic_cast<SemiTrailer&>(*trailer); }
};

void drive(Rig& r, double seconds, const std::function<DriverInput()>& d, const std::function<bool()>& done = [] { return false; }) {
  for (int i = 0; i < static_cast<int>(jsRound(seconds / DT)); i++) {
    const DriverInput inp = d();
    r.truck.step(DT, inp);
    r.trailer->step(DT, r.truck, inp);
    const CouplingStatus& st = r.task.couple(DT, r.truck, *r.trailer, inp.handbrake > 0.5);
    r.faults.insert(r.faults.end(), st.faults.begin(), st.faults.end());
    if (r.task.bumped) r.faults.push_back("ceFast");
    if (done()) return;
  }
}

void park(Rig& r, double seconds) {
  drive(
      r, seconds, [&r] { return in(0, std::abs(r.truck.speed) > 0.05 ? 0.6 : 0, std::abs(r.truck.speed) > 0.05 ? 0 : 1, 0); },
      [&r] { return std::abs(r.truck.speed) < 0.02 && r.task.state.handbrake; });
}

void onFoot(Rig& r, std::initializer_list<const char*> ids) {
  CouplingProcedure& proc = *r.task.coupling;
  for (const char* id : ids) {
    FS_OK(proc.onFoot(r.task.state));
    const std::vector<std::string> c = proc.choices();
    ok(std::find(c.begin(), c.end(), id) != c.end(), std::string(id) + " is offered");
    const std::optional<CouplingFault> fault = proc.act(id, r.task.state);
    ok(!fault, std::string(id) + ": " + fault.value_or(""));
  }
}

double gap(Rig& r) { return r.trailer->offsetFrom(r.truck).dist; }

void drawbarLesson() {
  test("the coupling lesson can be driven: uncouple, pull away, back up in two steps, couple, brake test");
  Rig r(false);
  park(r, 6);
  onFoot(r, {"uBrake", "uChocks", "uLeg", "uRed", "uYellow", "uCable", "uOpen"});
  const double at[2] = {r.trailer->x, r.trailer->z};
  drive(r, 20, [] { return in(0.25, 0, 0, 1); }, [&r] { return !r.trailer->coupled && gap(r) > 4; });
  FS_OK(!r.trailer->coupled);
  park(r, 6);
  FS_OK(std::hypot(r.trailer->x - at[0], r.trailer->z - at[1]) < 0.02);
  FS_OK(gap(r) > 3.5);
  onFoot(r, {"cCheck"});
  drive(r, 40, [&r] { return in(0, r.truck.speed < -0.6 ? 0.25 : 0, 0, -1); }, [&r] { return gap(r) < 1.1; });
  park(r, 6);
  between(gap(r), 0.3, 1.5, "stopped a metre before the eye");
  FS_OK(!r.trailer->coupled);
  onFoot(r, {"cHeight"});
  drive(r, 40, [&r] { return in(0, r.truck.speed < -0.3 ? 0.3 : 0, 0, -1); }, [&r] { return r.trailer->coupled; });
  FS_OK(r.trailer->coupled);
  park(r, 6);
  onFoot(r, {"cLocked", "cYellow", "cRed", "cCable", "cLights", "cLeg", "cChocks", "cBrake"});
  drive(r, 20, [] { return in(0.5, 0, 0, 1); }, [&r] { return r.truck.speed > 2.2; });
  drive(r, 10, [] { return in(0, 0.6, 0, 1); }, [&r] { return r.task.coupling->status.state == "done"; });
  FS_OK(r.faults.empty());
  FS_OK(r.task.coupling->status.state == "done");
  FS_OK(std::abs(r.trailer->articulation) < 0.1);
}

void drawbarBump() {
  test("backing into the standing trailer beside its eye: the truck is stopped at the drawbar, fast is a fault");
  Rig r(false);
  park(r, 6);
  onFoot(r, {"uBrake", "uChocks", "uLeg", "uRed", "uYellow", "uCable", "uOpen"});
  drive(r, 20, [] { return in(0.25, 0, 0, 1); }, [&r] { return !r.trailer->coupled && gap(r) > 5; });
  park(r, 6);
  onFoot(r, {"cCheck"});
  Trailer& t = *r.trailer;
  t.place(t.x + 0.4 * std::cos(t.yaw), t.z - 0.4 * std::sin(t.yaw), t.yaw);
  double fastest = 0;
  drive(r, 20, [&] {
    fastest = std::min(fastest, r.truck.speed);
    return in(0.6, 0, 0, -1);
  });
  FS_OK(fastest < -BUMP_SPEED);
  FS_OK(!t.coupled);
  FS_OK(std::find(r.faults.begin(), r.faults.end(), "ceFast") != r.faults.end());
  FS_OK(gap(r) < 1);
}

void semiLesson() {
  test("uncoupling and coupling the semi-trailer, step by step: legs, lines, fifth wheel, under the trailer, brake test");
  Rig r(true);
  SemiTrailer& t = r.semi();
  park(r, 8);
  FS_OK(r.task.coupling->act("uOpen", r.task.state) == "ceOpen");
  FS_OK(!r.task.coupling->done.count("uOpen"));
  onFoot(r, {"uBrake", "uChocks"});
  drive(r, 0.1, [] { return in(0, 0, 1, 0); });
  FS_OK(t.parkBrake);
  onFoot(r, {"uLeg"});
  drive(r, 2.5, [] { return in(0, 0, 1, 0); });
  FS_OK(t.legsDown && t.legs > 0.99);
  onFoot(r, {"uRed", "uYellow", "uCable"});
  drive(r, 0.1, [] { return in(0, 0, 1, 0); });
  FS_OK(!t.supply);
  onFoot(r, {"uOpen"});
  const double at[2] = {t.x, t.z};
  drive(r, 25, [] { return in(0.2, 0, 0, 1); }, [&] { return !t.coupled && gap(r) > 6; });
  FS_OK(!t.coupled);
  park(r, 8);
  FS_OK(std::hypot(t.x - at[0], t.z - at[1]) < 0.02);
  onFoot(r, {"cCheck"});
  drive(r, 40, [&r] { return in(0, r.truck.speed < -0.6 ? 0.3 : 0, 0, -1); }, [&r] { return gap(r) < 3.0; });
  park(r, 8);
  between(gap(r), 2.3, 4, "stopped before the trailer");
  FS_OK(r.task.coupling->done.count("cApproach"));
  onFoot(r, {"cHeight"});
  drive(r, 40, [&r] { return in(0, r.truck.speed < -0.3 ? 0.4 : 0, 0, -1); }, [&t] { return t.coupled; });
  FS_OK(t.coupled);
  park(r, 8);
  onFoot(r, {"cLocked", "cYellow", "cRed", "cCable", "cLights", "cLeg", "cChocks", "cBrake"});
  drive(r, 2.5, [] { return in(0, 0, 1, 0); });
  FS_OK(!t.legsDown && t.legs < 0.01);
  FS_OK(!t.braked());
  drive(r, 20, [] { return in(0.5, 0, 0, 1); }, [&r] { return r.truck.speed > 2.2; });
  drive(r, 10, [] { return in(0, 0.6, 0, 1); }, [&r] { return r.task.coupling->status.state == "done"; });
  ok(r.faults.empty(), "no fault on the way");
  FS_OK(r.task.coupling->status.state == "done");
  FS_OK(std::abs(t.articulation) < 0.1);
}

void semiMiss() {
  test("missing the kingpin: the tractor goes on under the trailer until its cab stands against the front – not coupled");
  Rig r(true);
  SemiTrailer& t = r.semi();
  park(r, 8);
  onFoot(r, {"uBrake", "uChocks", "uLeg", "uRed", "uYellow", "uCable", "uOpen"});
  drive(r, 25, [] { return in(0.2, 0, 0, 1); }, [&] { return !t.coupled && gap(r) > 6; });
  park(r, 8);
  onFoot(r, {"cCheck"});
  t.place(t.x + 0.6 * std::cos(t.yaw), t.z - 0.6 * std::sin(t.yaw), t.yaw);
  drive(r, 30, [&r] { return in(0, r.truck.speed < -0.5 ? 0.3 : 0, 0, -1); });
  FS_OK(!t.coupled);
  double mpos[3];
  r.truck.getModelPosition(mpos);
  between(t.hitchZ - mpos[2], 0.7, 1.1, "stopped with its cab at the trailer's front");
}

void texts() {
  test("the texts of the steps: the articulated lorry's own where it differs");
  FS_OK(taskTextKey(true, "step", "uLeg") == "semi.step.uLeg");
  FS_OK(taskTextKey(true, "step", "uRed") == "ce.step.uRed");
  FS_OK(taskTextKey(false, "step", "uLeg") == "ce.step.uLeg");
  FS_OK(taskTextKey(true, "do", "corner") == "ce.do.corner");
}

}  // namespace

int main() {
  geometry();
  weight();
  jackknife();
  payload();
  drawbarLesson();
  drawbarBump();
  semiLesson();
  semiMiss();
  texts();
  std::puts("semitrailer_test: ok");
  return 0;
}
