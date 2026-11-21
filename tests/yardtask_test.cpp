// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The coupling lesson with the real truck and trailer (their dynamics, on
 * level ground): uncoupled, driven away, backed up to the trailer in two
 * steps, coupled, brake test – as the game runs it through YardTask.
 *
 * Origin: tests/game/yardtask.test.ts (MaurerAnton/driving-simulator @
 * ff37c5d, v0.7.0).
 */
#include "../src/game/yardtask.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "../src/sim/vehicle/vehicle.h"
#include "../src/sim/trailer.h"

using namespace fs;

/** assert with a message the C preprocessor's CHECK() cannot carry */
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::abort();                                                     \
    }                                                                   \
  } while (0)

namespace {

constexpr double DT = 1.0 / 240;

/** level ground (the original's PlaneGround with grade 0) */
class Flat : public Ground {
 public:
  void sample(double, double, double, GroundSample& out) override {
    out.hit = true;
    out.y = 0;
    out.nx = 0;
    out.ny = 1;
    out.nz = 0;
    out.grip = 1;
    out.rolling = 1;
  }
};

struct Rig {
  Flat ground;
  Vehicle truck;
  Trailer trailer;
  YardTask task;
  std::vector<std::string> faults;

  Rig() : truck(TRUCK, ground), trailer(TANDEM_TRAILER, ground), task("couple") {
    truck.teleport(0, 0, 0, 0);
    truck.resetDrivetrain(true);
    trailer.coupleBehind(truck);
  }
};

DriverInput input(std::function<void(DriverInput&)> over = nullptr) {
  DriverInput d;   // steer 0, throttle 0, brake 0, clutch 0, handbrake 0, gear 0, ignition true
  if (over) over(d);
  return d;
}

/** Drive for at most `seconds`, until `done` says so. */
void drive(Rig& r, double seconds, const std::function<DriverInput()>& d,
           const std::function<bool()>& done = nullptr) {
  for (int i = 0; i < static_cast<int>(std::round(seconds / DT)); i++) {
    DriverInput inp = d();
    r.truck.step(DT, inp);
    r.trailer.step(DT, r.truck, inp);
    CouplingStatus st = r.task.couple(DT, r.truck, r.trailer, inp.handbrake > 0.5);
    r.faults.insert(r.faults.end(), st.faults.begin(), st.faults.end());
    if (r.task.bumped) r.faults.push_back("ceFast");
    if (done && done()) return;
  }
}

void park(Rig& r) {
  drive(r, 6,
        [&r]() {
          return input([&r](DriverInput& d) {
            d.gear = 0;
            d.brake = std::fabs(r.truck.speed) > 0.05 ? 0.6 : 0;
            d.handbrake = std::fabs(r.truck.speed) > 0.05 ? 0 : 1;
          });
        },
        [&r]() { return std::fabs(r.truck.speed) < 0.02 && r.task.state.handbrake; });
}

void onFoot(Rig& r, std::initializer_list<const char*> ids) {
  CouplingProcedure& proc = r.task.coupling;
  for (const char* id : ids) {
    CHECK(proc.onFoot(r.task.state));
    bool offered = false;
    for (const auto& c : proc.choices())
      if (c == id) offered = true;
    CHECK(offered);
    CHECK(proc.act(id, r.task.state) == "");
  }
}

double gap(const Rig& r) {
  double dist = 0, angle = 0;
  const_cast<Trailer&>(r.trailer).offsetFrom(r.truck, dist, angle);
  return dist;
}

bool has(const std::vector<std::string>& v, const std::string& id) {
  for (const auto& s : v)
    if (s == id) return true;
  return false;
}

}  // namespace

int main() {
  // ---- the coupling lesson can be driven: uncouple, pull away, back up in
  // two steps, couple, brake test
  {
    Rig r;
    park(r);
    onFoot(r, {"uBrake", "uChocks", "uLeg", "uRed", "uYellow", "uCable", "uOpen"});
    // pull forward: the trailer stays where it is
    const double trailerAtX = r.trailer.x, trailerAtZ = r.trailer.z;
    drive(r, 20,
          []() { return input([](DriverInput& d) { d.gear = 1; d.throttle = 0.25; }); },
          [&r]() { return !r.trailer.coupled && gap(r) > 4; });
    CHECK(!r.trailer.coupled);
    park(r);
    CHECK(std::hypot(r.trailer.x - trailerAtX, r.trailer.z - trailerAtZ) < 0.02);
    CHECK(gap(r) > 3.5);
    onFoot(r, {"cCheck"});
    // back to about a metre before the eye – at walking pace, with the brake against the creep
    drive(r, 40,
          [&r]() {
            return input([&r](DriverInput& d) {
              d.gear = -1;
              d.brake = r.truck.speed < -0.6 ? 0.25 : 0;
            });
          },
          [&r]() { return gap(r) < 1.1; });
    park(r);
    CHECK(gap(r) > 0.3 && gap(r) < 1.5);
    CHECK(!r.trailer.coupled);
    onFoot(r, {"cHeight"});
    // the last metre at a crawl
    drive(r, 40,
          [&r]() {
            return input([&r](DriverInput& d) {
              d.gear = -1;
              d.brake = r.truck.speed < -0.3 ? 0.3 : 0;
            });
          },
          [&r]() { return r.trailer.coupled; });
    CHECK(r.trailer.coupled);
    park(r);
    onFoot(r, {"cLocked", "cYellow", "cRed", "cCable", "cLights", "cLeg", "cChocks", "cBrake"});
    // brake test, with the trailer on the hook again
    drive(r, 20, []() { return input([](DriverInput& d) { d.gear = 1; d.throttle = 0.5; }); },
          [&r]() { return r.truck.speed > 2.2; });
    drive(r, 10, []() { return input([](DriverInput& d) { d.gear = 1; d.brake = 0.6; }); },
          [&r]() { return r.task.coupling.status.state == "done"; });
    CHECK(r.faults.empty());
    CHECK(r.task.coupling.status.state == "done");
    CHECK(std::fabs(r.trailer.articulation) < 0.1);
  }

  // ---- backing into the standing trailer beside its eye: the truck is
  // stopped at the drawbar, fast is a fault
  {
    Rig r;
    park(r);
    onFoot(r, {"uBrake", "uChocks", "uLeg", "uRed", "uYellow", "uCable", "uOpen"});
    drive(r, 20,
          []() { return input([](DriverInput& d) { d.gear = 1; d.throttle = 0.25; }); },
          [&r]() { return !r.trailer.coupled && gap(r) > 5; });
    park(r);
    onFoot(r, {"cCheck"});
    // the trailer stands 40 cm to the side of where the truck will come back to
    r.trailer.place(r.trailer.x + 0.4 * std::cos(r.trailer.yaw),
                    r.trailer.z - 0.4 * std::sin(r.trailer.yaw), r.trailer.yaw);
    double fastest = 0;
    drive(r, 20,
          [&r, &fastest]() {
            fastest = std::min(fastest, r.truck.speed);
            return input([](DriverInput& d) { d.gear = -1; d.throttle = 0.6; });
          });
    CHECK(fastest < -BUMP_SPEED);
    CHECK(!r.trailer.coupled);
    CHECK(has(r.faults, "ceFast"));
    // … and the truck has not driven through the trailer: its coupling is still about at the eye
    CHECK(gap(r) < 1);
  }

  std::printf("yardtask: all checks passed\n");
  return 0;
}
