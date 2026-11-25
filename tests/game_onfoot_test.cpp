// SPDX-License-Identifier: AGPL-3.0-only
/**
 * On foot in the game (2.1, beyond the original's tests – as game_test is), on the real map:
 *
 *  - ?onfoot=1: the drive begins on foot beside the car; walked round it (the keys, the walker turned
 *    towards each corner in turn) the person never goes through it, and the check before the drive
 *    counts what was looked at;
 *  - at the front, the key . opens the bonnet (game/openplay.ts, drawn by render/caropen.ts);
 *  - at the driver's door, G: the door opens, one sits in, the door shuts – the camera in the seat;
 *  - the engine started, the parking brake off, D, the throttle: the car drives;
 *  - a car put down on the map beside the person (game/spawncar.ts), got into with G – the car driven
 *    till then is left standing where it was;
 *  - the vehicle "on foot" (the pedestrian): the drive begins as a person in the town, who walks.
 *
 * Mirrored from tests/game/pedestrian.test.ts @ v1.7.7: "on foot without a vehicle: anywhere, at
 * walking pace or at the running speed the settings give" (game/onfoot.ts, with no map needed).
 *
 * The cases in the game need an OpenGL ES context off screen and the original's map (else they skip,
 * as game_test does).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "game/game.h"
#include "game/onfoot.h"
#include "game/spawncar.h"
#include "input/input.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "sim/vehicle/index.h"

using namespace fs;

static int failures = 0;
#define CHECK(cond, ...)               \
  do {                                 \
    if (!(cond)) {                     \
      std::printf("FAIL %s: ", #cond); \
      std::printf(__VA_ARGS__);        \
      std::printf("\n");               \
      failures++;                      \
    }                                  \
  } while (0)

/** tests/game/pedestrian.test.ts: on foot without a vehicle */
static void withoutVehicle() {
  const double DT = 1.0 / 30;
  const auto ground = [](double, double, double) { return 100.0; };
  const auto free = [](double, double, double, double) { return false; };
  OnFoot w(std::nullopt, OnFoot::Start{0, 100, 0, 0});
  w.runSpeed = 15 / 3.6;
  for (double t = 0; t < 60; t += DT) w.update(DT, WalkMove{1, 0, false}, WalkLook{}, ground, free);
  CHECK(std::abs(-w.z - WALK * 60) < 1, "walked north %.1f m in a minute", -w.z);
  CHECK(!w.tooFar, "nothing to stay near");
  const double z0 = w.z;
  for (double t = 0; t < 10; t += DT) w.update(DT, WalkMove{1, 0, true}, WalkLook{}, ground, free);
  CHECK(std::abs(z0 - w.z - (15 / 3.6) * 10) < 0.5, "ran %.1f m in ten seconds at 15 km/h", z0 - w.z);
  CHECK(w.running && std::abs(w.speed * 3.6 - 15) < 0.2, "running at %.1f km/h", w.speed * 3.6);
}

int main() {
  withoutVehicle();
  const std::string mapDir = "/home/user/driving-simulator/public/map/";
  if (!std::filesystem::exists(mapDir + "map.json")) {
    std::printf("SKIP: no map at %s\n", mapDir.c_str());
    return failures ? 1 : 0;
  }
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  plat::native::setViewport(640, 360, 1);
  plat::native::setLanguages({"de"});
  if (!plat::gl::createContext("game_onfoot_test", 640, 360)) {
    std::printf("SKIP: no OpenGL ES here\n");
    return failures ? 1 : 0;
  }

  Game game(dom::el("canvas"), dom::el("div"));
  plat::native::wait(game.boot(URLSearchParams("?start=free&vehicle=car&onfoot=1")));
  game.looping = false;
  double now = plat::nowMs();
  const auto picture = [&]() {
    plat::native::pump();
    now += 1000.0 / 60;
    game.frame(now);
  };
  const auto tap = [&](const std::string& code) {
    game.input->press(code, true);
    picture();
    game.input->press(code, false);
    picture();
  };

  for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
  CHECK(game.state == "drive", "state %s after placing", game.state.c_str());
  if (game.state != "drive") return 1;
  CHECK(game.walker != nullptr, "?onfoot=1: the drive does not begin on foot");
  if (!game.walker) return 1;
  CHECK(game.walkStart, "the check round the vehicle does not count");
  CHECK(game.markers->group->children.size() > 0, "no rings on the ground");

  // the car where it stands: its origin, heading and outline
  game.vehicle->getModelPosition(game.mpos.data());
  const double cx = game.mpos[0], cz = game.mpos[2], yaw = game.vehicle->yaw;
  const Outline o = game.def->outline;
  const double fx = -std::sin(yaw), fz = -std::cos(yaw), rx = std::cos(yaw), rz = -std::sin(yaw);
  const auto world = [&](double right, double ahead) { return std::array<double, 2>{cx + rx * right + fx * ahead, cz + rz * right + fz * ahead}; };
  const auto local = [&](double x, double z) { return std::array<double, 2>{(x - cx) * rx + (z - cz) * rz, (x - cx) * fx + (z - cz) * fz}; };
  std::printf("the car at %.1f, %.1f facing %.0f°; on foot at %.1f, %.1f\n", cx, cz, yaw * 180 / M_PI, game.walker->x, game.walker->z);
  {
    const std::array<double, 2> at = local(game.walker->x, game.walker->z);
    CHECK(at[0] < -o.hw, "one starts beside the driver's door, on the left: %.2f m right of the middle", at[0]);
  }

  // walking to a point: the walker turned towards it, W held (the "tank" way of the keys)
  bool inside = false;
  const auto walkTo = [&](double right, double ahead, int maxFrames) {
    const std::array<double, 2> to = world(right, ahead);
    game.input->press("KeyW", true);
    for (int i = 0; i < maxFrames; i++) {
      OnFoot* w = game.walker.get();
      if (!w) break;
      const double dx = to[0] - w->x, dz = to[1] - w->z;
      if (std::hypot(dx, dz) < 0.35) break;
      w->yaw = std::atan2(-dx, -dz);
      picture();
      const std::array<double, 2> at = local(w->x, w->z);
      if (std::abs(at[0]) < o.hw - 0.05 && at[1] < o.front - 0.05 && at[1] > -o.rear + 0.05) inside = true;
    }
    game.input->press("KeyW", false);
    picture();
    const OnFoot* w = game.walker.get();
    return w ? std::hypot(to[0] - w->x, to[1] - w->z) : 1e9;
  };

  // looking at what is to be checked near where one stands (its ring on this side of the car): to its ring, a look at it for a moment
  const auto lookRound = [&]() {
    OnFoot* w = game.walker.get();
    if (!w) return;
    for (size_t i = 0; i < w->spots.size(); i++) {
      const CheckSpot s = w->spots[i];
      if (s.done) continue;
      const std::array<double, 2> me = local(w->x, w->z), ring = local(s.rx, s.rz);
      const std::array<double, 2> mid = local((w->x + s.rx) / 2, (w->z + s.rz) / 2);
      if (std::hypot(s.rx - w->x, s.rz - w->z) > 4.5 || (std::abs(mid[0]) < o.hw + 0.3 && mid[1] < o.front + 0.3 && mid[1] > -o.rear - 0.3)) continue;
      (void)me;
      walkTo(ring[0], ring[1], 400);
      const double dx = s.x - w->x, dz = s.z - w->z;
      w->yaw = std::atan2(-dx, -dz);
      w->pitch = std::atan2(s.y - (w->y + EYE), std::hypot(dx, dz));
      for (int k = 0; k < 90 && !w->spots[i].done; k++) picture();
    }
  };
  // round the car: the front left corner, the front, the right side, the back, the left side again
  const double m = 1.3;
  const double startX = game.walker->x, startZ = game.walker->z;
  double gap = 0;
  lookRound();
  gap = std::max(gap, walkTo(-o.hw - m, o.front + m, 900));
  lookRound();
  gap = std::max(gap, walkTo(o.hw + m, o.front + m, 900));
  lookRound();
  gap = std::max(gap, walkTo(o.hw + m, -o.rear - m, 1500));
  lookRound();
  gap = std::max(gap, walkTo(-o.hw - m, -o.rear - m, 900));
  lookRound();
  std::printf("walked %.1f m round the car (furthest from a corner aimed at: %.2f m)\n", game.walker->distance, gap);
  CHECK(!inside, "walked through the car");
  CHECK(game.walker->distance > 2 * (o.front + o.rear), "walked only %.1f m", game.walker->distance);
  CHECK(std::hypot(game.walker->x - startX, game.walker->z - startZ) > 2, "the walker did not get round");
  int done = 0, of = 0;
  for (const OnFoot::Progress& p : game.walker->progress()) {
    done += p.done;
    of += p.of;
  }
  std::printf("checked %d of %d things round the car\n", done, of);
  CHECK(done == of && game.walker->allChecked(), "not everything was checked walking round the car");
  // (the car stood still meanwhile: nobody in the seat)
  game.vehicle->getModelPosition(game.mpos.data());
  CHECK(std::hypot(game.mpos[0] - cx, game.mpos[2] - cz) < 0.05, "the car moved while nobody sat in it");

  // in front of the bonnet: the key . opens it (round the front left corner, not through the car)
  walkTo(-o.hw - m, 0, 900);
  walkTo(-o.hw - m, o.front + m, 900);
  walkTo(0, o.front + 0.8, 900);
  {
    const std::optional<OpeningSpot> at = game.opens.at(game.walker.get());
    CHECK(at && at->id == "bonnet", "in front of the car, not at the bonnet: %s", at ? at->id.c_str() : "nothing");
  }
  tap("Period");
  for (int i = 0; i < 120; i++) picture();
  CHECK(game.opens.openings.get("bonnet") > 0.99, "the bonnet does not stand open: %.2f", game.opens.openings.get("bonnet"));
  tap("Period");
  for (int i = 0; i < 120; i++) picture();
  CHECK(game.opens.openings.get("bonnet") < 0.01, "the bonnet is not shut again: %.2f", game.opens.openings.get("bonnet"));

  // to the driver's door, and in: the door opens, one sits in, the door shuts
  walkTo(-o.hw - m, 1.5, 900);
  {
    const OnFoot::Spot door = game.walker->doorSpot();
    const std::array<double, 2> d = local(door.x, door.z);
    walkTo(d[0], d[1], 600);
  }
  tap("KeyG");
  CHECK(game.opens.busy(), "G at the driver's door: nothing happens (tip: %s)", game.tipText.c_str());
  for (int i = 0; i < 300 && (game.walker || game.opens.busy()); i++) picture();
  CHECK(!game.walker, "still on foot after getting in");
  CHECK(!game.opens.busy() && game.opens.seat == "driver", "not in the driver's seat");
  CHECK(game.opens.openings.get("fl") < 0.01, "the door is not shut: %.2f", game.opens.openings.get("fl"));
  {
    const Vector3& cp = game.app.camera->position;
    game.vehicle->getModelPosition(game.mpos.data());
    const double off = std::hypot(cp.x - game.mpos[0], cp.z - game.mpos[2]);
    CHECK(off < 2, "the camera is not in the car: %.2f m from it", off);
  }

  // drive: the engine started, the parking brake off, D, the throttle
  Controls& c = game.input->c;
  if (!c.ignition) tap("KeyI");
  game.input->press("Enter", true);
  for (int i = 0; i < 240 && !game.vehicle->engineRunning; i++) picture();
  game.input->press("Enter", false);
  picture();
  CHECK(game.vehicle->engineRunning, "the engine does not start");
  if (c.handbrake > 0.5) tap("Space");
  for (int i = 0; i < 4 && c.gear < 1; i++) tap("KeyE");
  game.vehicle->getModelPosition(game.mpos.data());
  const double x0 = game.mpos[0], z0 = game.mpos[2];
  game.input->press("KeyW", true);
  double top = 0;
  for (int i = 0; i < 300; i++) {
    picture();
    top = std::max(top, game.vehicle->speed);
  }
  game.input->press("KeyW", false);
  game.vehicle->getModelPosition(game.mpos.data());
  const double moved = std::hypot(game.mpos[0] - x0, game.mpos[2] - z0);
  std::printf("driven %.1f m, top %.1f km/h (gear %d)\n", moved, top * 3.6, c.gear);
  CHECK(moved > 5 && top > 2, "the car does not drive: %.1f m, %.1f m/s", moved, top);

  // to a stop, out again, a car put down beside the person and got into: the car till now stays where it stands
  game.input->press("Space", false);
  game.input->press("KeyS", true);
  for (int i = 0; i < 600 && std::abs(game.vehicle->speed) > 0.05; i++) picture();
  game.input->press("KeyS", false);
  tap("KeyG");
  for (int i = 0; i < 300 && !(game.walker && !game.opens.busy()); i++) picture();
  CHECK(game.walker != nullptr, "not out of the car (tip: %s)", game.tipText.c_str());
  if (game.walker) {
    CHECK(game.canSpawnCar(), "on foot in a free drive, no car may be put down");
    game.vehicle->getModelPosition(game.mpos.data());
    const double ox = game.mpos[0], oz = game.mpos[2];
    const bool put = game.spawnCars->spawnBeside();
    CHECK(put && game.spawnCars->car, "no car put down beside the person (tip: %s)", game.tipText.c_str());
    if (put && game.spawnCars->car) {
      const Parked p = *game.spawnCars->car;
      std::printf("a car put down at %.1f, %.1f (%.1f m from the person)\n", p.x, p.z, std::hypot(p.x - game.walker->x, p.z - game.walker->z));
      CHECK(p.root->parent != nullptr, "the car put down is not drawn");
      game.walker->x = p.x - std::cos(p.yaw) * (p.outline.hw + 0.8);
      game.walker->z = p.z + std::sin(p.yaw) * (p.outline.hw + 0.8);
      picture();
      CHECK(game.spawnCars->near(game.walker->x, game.walker->z) != nullptr, "beside the car put down, it is not near");
      tap("KeyG");
      for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
      for (int i = 0; i < 30; i++) picture();
      game.vehicle->getModelPosition(game.mpos.data());
      CHECK(std::hypot(game.mpos[0] - p.x, game.mpos[2] - p.z) < 1.5, "not in the car put down: %.1f m from it", std::hypot(game.mpos[0] - p.x, game.mpos[2] - p.z));
      CHECK(!game.walker, "the drive in the car put down begins on foot");
      CHECK(game.spawnCars->left && std::hypot(game.spawnCars->left->x - ox, game.spawnCars->left->z - oz) < 0.5, "the car driven before is not left standing");
    }
  }

  // the vehicle "on foot": the drive of a person in the town
  game.toMenu();
  game.settings.vehicle = "pedestrian";
  game.settings.startOutside = false;
  game.begin("free", 0, true);
  for (int i = 0; i < 20000 && game.state != "drive"; i++) picture();
  CHECK(game.state == "drive", "on foot: state %s after placing", game.state.c_str());
  CHECK(game.def->kind == "pedestrian" && game.walker, "the pedestrian's drive does not begin on foot (%s, walker %d, vehicle setting %s)", game.def->kind.c_str(),
        game.walker != nullptr, game.settings.vehicle.c_str());
  if (game.walker) {
    const double wx = game.walker->x, wz = game.walker->z;
    game.input->press("KeyW", true);
    for (int i = 0; i < 300; i++) picture();
    game.input->press("KeyW", false);
    const double walked = std::hypot(game.walker->x - wx, game.walker->z - wz);
    std::printf("the pedestrian walked %.1f m in five seconds\n", walked);
    CHECK(walked > 2, "the pedestrian does not walk: %.1f m", walked);
    game.vehicle->getModelPosition(game.mpos.data());
    CHECK(std::hypot(game.mpos[0] - game.walker->x, game.mpos[2] - game.walker->z) < 0.1, "the game's vehicle does not stand where the person is");
    const Vector3& cp = game.app.camera->position;
    CHECK(std::abs(cp.y - (game.walker->y + EYE)) < 0.3, "the camera is not at the person's eyes: %.2f above the feet", cp.y - game.walker->y);
  }

  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("game_onfoot: ok\n");
  return 0;
}
