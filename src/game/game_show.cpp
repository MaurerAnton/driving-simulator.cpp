// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Game: the test place – every model of the game on the practice yard
 * (game.ts:1643 … 1822): nameSign, levelTrailer, standModel, buildShowroom.
 * Which model stands where and is got into: game/showdrive.h.
 *
 * Origin: src/game/game.ts @ v1.7.7
 */
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/ce.h"
#include "game/game.h"
#include "game/showdrive.h"
#include "game/vehicles.h"
#include "i18n/index.h"
#include "platform/canvas.h"
#include "render/actors.h"
#include "render/bike.h"
#include "render/car.h"
#include "render/fleet.h"
#include "render/people.h"
#include "render/plane.h"
#include "render/semitruck.h"
#include "render/tiles.h"
#include "render/trams.h"
#include "render/truck.h"
#include "sim/flight.h"
#include "sim/ground.h"
#include "sim/rail.h"
#include "sim/trailer.h"
#include "sim/vehicle/fleet.h"
#include "sim/vehicle/index.h"

namespace fs {

/** A sign with a name, standing over a model (a sprite: it faces whoever looks). */
void Game::nameSign(const std::string& text, double x, double y, double z) {
  const Canvas c(512, 96);
  const Ctx2D g = c.getContext2D();
  g.fillStyle("rgba(16, 24, 34, 0.86)");
  g.beginPath();
  g.roundRect(4, 4, 504, 88, 20);
  g.fill();
  g.fillStyle("#ffffff");
  g.font("bold 40px system-ui, sans-serif");
  g.textAlign("center");
  g.textBaseline("middle");
  g.fillText(text, 256, 50, 480);
  TexturePtr tex = canvasTexture(c);
  tex->srgb = true;
  MaterialParams p;
  p.map = tex;
  p.depthWrite = false;
  auto sprite = make<Sprite>(spriteMaterial(p));
  sprite->scale.set(4.2, 0.79, 1);
  sprite->position.set(x, y, z);
  showGroup->add(sprite);
}

/**
 * A trailer just coupled behind a vehicle that stands (the menu, the test
 * place): its height and pitch – level at the vehicle's ground `y` where
 * the ground under its wheels gave an implausible slope (not loaded yet).
 */
std::array<double, 2> Game::levelTrailer(const Trailer& t, double y) {
  const double groundY = app.tiles->heightAt(t.x, t.z);
  if (std::abs(t.pitch) > 0.12 || std::isnan(groundY)) return {y, 0};
  return {t.y, t.pitch};
}

/** A vehicle of the game standing (engine off) at (x, z) on the ground, facing yaw – and the trailer it pulls. */
void Game::standModel(const VehicleDef& d, VehicleModel& model, double x, double z, double yaw, const std::function<double(double x, double z)>& groundAt, TowedModel* towed,
                      PlayerTruck* withTruck) {
  Vehicle v(d.spec("automatic", powerOf(d, settings.powers)), *ground);
  v.teleport(x, groundAt(x, z) + 0.02, z, yaw);
  v.getModelPosition(previewPos.data());
  model.root->position.set(previewPos[0], previewPos[1], previewPos[2]);
  model.root->quaternion.set(v.quat[0], v.quat[1], v.quat[2], v.quat[3]);
  parkedVisual(v);
  model.update(visual, 0, time);
  if (PlayerBike* bike = dynamic_cast<PlayerBike*>(&model)) bike->setRider(false);
  if (d.trailer) {
    Trailer t(*d.trailer, *ground);
    t.coupleBehind(v);
    const std::array<double, 2> level = levelTrailer(t, groundAt(x, z));
    if (withTruck) withTruck->setTrailer(t.x, level[0], t.z, t.yaw, level[1], 0, true);
    else if (towed) towed->place(t.x, level[0], t.z, t.yaw, level[1], 0, &visual);
  }
}

/**
 * The test place: on the practice yard every model of the game – the
 * vehicles of every licence class side by side, the cars, cyclists and
 * people of the traffic, the trams (two coupled, one with a trailer car)
 * and the aeroplane – each with its name.  Made once, the first time.
 */
void Game::buildShowroom() {
  if (!yardFrame || showBuilt) return;
  const YardFrame& frame = *yardFrame;
  showBuilt = true;
  TileManager& tiles = *app.tiles;
  const std::function<double(double, double)> groundAt = [&tiles](double x, double z) {
    const double y = tiles.heightAt(x, z);
    return std::isnan(y) ? 0.0 : y;
  };
  const double len = frame.yard.len;
  const double wid = frame.yard.wid;
  const std::string lang = settings.lang;
  // the small ones on the north side, noses to the aisle in the middle; the big ones on the south side
  const double north = frame.yawToWorld(M_PI);
  const double south = frame.yawToWorld(0);
  double u = -len / 2 + 8;
  const double smallRow = -wid / 2 + 13;
  const double bigRow = wid / 2 - 12;
  const auto place = [&](const VehicleKind& kind, double row, double facing, double gap) {
    const VehicleDef& d = VEHICLES.at(kind);
    std::unique_ptr<VehicleModel> made;
    PlayerTruck* truckModel = nullptr;
    if (d.model == "car") made = std::make_unique<PlayerCar>();
    else if (d.model == "motorcycle") made = std::make_unique<PlayerBike>(bikeVariant(kind));
    else if (d.model == "truckLight") made = std::make_unique<PlayerTruck>(LIGHT_TRUCK);
    else if (d.model == "truck") {
      auto t = std::make_unique<PlayerTruck>();
      truckModel = t.get();
      made = std::move(t);
    } else if (d.model == "semi") made = std::make_unique<PlayerSemi>();
    else if (d.model == "truckHeavy") made = std::make_unique<PlayerRigid>();
    else if (d.model == "bus") made = std::make_unique<PlayerBus>();
    else if (d.model == "minibus") made = std::make_unique<PlayerBus>(MINIBUS);
    else made = std::make_unique<PlayerTractor>();
    VehicleModel& m = *made;
    if (truckModel) {
      showGroup->add(truckModel->trailer);
      truckModel->trailer->visible = kind == "truck";
    }
    std::shared_ptr<TowedModel> towed = d.trailer && !truckModel ? towedModel(d.trailer, nullptr) : nullptr;
    if (towed) showGroup->add(towed->group);
    showGroup->add(m.root);
    const std::array<double, 2> at = frame.toWorld(u, row);
    const double x = at[0];
    const double z = at[1];
    standModel(d, m, x, z, facing, groundAt, towed.get(), truckModel);
    // (it can be got into: game/showdrive.ts)
    const bool small = d.model == "car" || d.model == "motorcycle";
    std::vector<Object3D*> roots{m.root.get()};
    if (towed) roots.push_back(towed->group.get());
    if (truckModel && kind == "truck") roots.push_back(truckModel->trailer.get());
    showSpots.add(ShowSpot{.kind = kind, .x = x, .y = groundAt(x, z), .z = z, .yaw = facing, .outline = d.outline, .height = small ? 1.6 : 3.4, .roots = roots});
    nameSign(tr(d.name, lang), x, groundAt(x, z) + (d.model == "car" || d.model == "motorcycle" ? 2.6 : 4.4), z);
    showModels.push_back(std::move(made));
    if (towed) showTowed.push_back(towed);
    u += gap;
  };
  for (const char* kind : {"car", "moped", "motorcycle125", "motorcycle35", "motorcycle", "tractorSlow", "tractor"}) {
    const std::string k = kind;
    place(k, smallRow, south, k.starts_with("moto") || k == "moped" ? 3.6 : 5.2);
  }
  // the cars of the traffic (three bodies), a cyclist and people
  static const std::array<CarDims, 3>* bodies = [] {
    CarDims b = COMPACT_DIMS;
    b.length = 4.6;
    b.height = 1.5;
    b.wheelbase = 2.7;
    b.tail = 1;
    CarDims c = COMPACT_DIMS;
    c.length = 4.45;
    c.height = 1.42;
    c.wheelbase = 2.68;
    c.tail = 2;
    return new std::array<CarDims, 3>{COMPACT_DIMS, b, c};
  }();
  static const std::array<int, 3> paints = {0x8a1c1c, 0x2f5f8a, 0xd8d8d0};
  for (size_t i = 0; i < bodies->size(); i++) {
    CarExteriorOptions o;
    o.dims = &(*bodies)[i];
    auto e = std::make_unique<CarExterior>(paints[i], o);
    const std::array<double, 2> at = frame.toWorld(u, smallRow);
    const double x = at[0];
    const double z = at[1];
    e->group->position.set(x, groundAt(x, z), z);
    e->group->rotation.y = south;
    const double r = e->dims.wheelRadius;
    e->setWheels({0, 0, 0, 0}, {0, 0, 0, 0}, {r, r, r, r});
    e->setLamps({.low = false, .high = false, .brake = false, .reverse = false, .left = false, .right = false});
    showGroup->add(e->group);
    // (each driven as the car)
    showSpots.add(ShowSpot{.kind = "car",
                           .x = x,
                           .y = groundAt(x, z),
                           .z = z,
                           .yaw = south,
                           .outline = Outline{.front = e->dims.length / 2, .rear = e->dims.length / 2, .hw = e->dims.width / 2},
                           .height = 1.5,
                           .roots = {e->group.get()}});
    if (i == 1) nameSign(tr("showTraffic", lang), x, groundAt(x, z) + 2.4, z);
    showExteriors.push_back(std::move(e));
    u += 4.6;
  }
  {
    const std::array<double, 2> at = frame.toWorld(u, smallRow);
    const double bx = at[0];
    const double bz = at[1];
    // (the cyclist's model: render/actors.h CyclistModel – while the cyclists are not in the build there is none to show)
    if (CyclistModel::create) {
      showCyclist = CyclistModel::create(0x2f8a4a);
      showCyclist->group->position.set(bx, groundAt(bx, bz), bz);
      showCyclist->group->rotation.y = south;
      showCyclist->pose(0, 0, false);
      showGroup->add(showCyclist->group);
    }
    nameSign(tr("showCyclist", lang), bx, groundAt(bx, bz) + 2.4, bz);
    u += 4;
  }
  std::vector<PersonPose> poses;
  for (int i = 0; i < 8; i++) {
    const std::array<double, 2> at = frame.toWorld(u + (i % 4) * 1.1, smallRow + std::floor(i / 4.0) * 1.3);
    PersonPose p;
    p.x = at[0];
    p.y = groundAt(at[0], at[1]);
    p.z = at[1];
    p.yaw = south;
    p.walk = -1;
    p.look = 101 + i * 37;
    poses.push_back(p);
  }
  showPeople->update(poses, time);
  {
    const std::array<double, 2> at = frame.toWorld(u + 1.6, smallRow);
    nameSign(tr("showPeople", lang), at[0], groundAt(at[0], at[1]) + 2.6, at[1]);
  }
  // the big ones
  u = -len / 2 + 8;
  for (const char* kind : {"carTrailer", "lightTruck", "lightTruckTrailer", "truckSolo", "truck", "semi", "minibus", "minibusTrailer", "bus", "busTrailer"}) place(kind, bigRow, north, 6.5);
  // the trams, lengthwise along the north edge: two coupled, and one with a trailer car
  const double east = frame.yawToWorld(-M_PI / 2);
  double front = len / 2 - 50;
  for (const char* set : {"double", "trailer"}) {
    const Consist* consist = consistOf(RAIL_TRAM, set);
    const std::vector<CarRole>& roles = carLayout(*consist);
    auto v = std::make_unique<RailVehicle>();
    v->id = -1 - static_cast<int>(showTrams.size());
    v->kind = RAIL_TRAM;
    v->consist = consist;
    v->panto = 1;
    for (const CarRole& r : roles) {
      const std::array<double, 2> at = frame.toWorld(front - r.start - r.len / 2, -wid / 2 + 3);
      v->cars.push_back(RailCar{.x = at[0], .y = groundAt(at[0], at[1]), .z = at[1], .yaw = east, .pitch = 0});
    }
    nameSign(tr(std::string(set) == "double" ? "tramSetDouble" : "tramSetTrailer", lang), v->cars[0].x, v->cars[0].y + 5.4, v->cars[0].z);
    front -= consistLength(*consist) + 8;
    showTrams.push_back(std::move(v));
  }
  showRail->update(showTrams);
  // the aeroplane at the east end of the aisle
  showPlane = std::make_unique<PlaneModel>();
  Aircraft a(TRAINER, 160);
  const std::array<double, 2> pa = frame.toWorld(len / 2 - 12, 0);
  const double ax = pa[0];
  const double az = pa[1];
  a.placeOnGround(ax, az, frame.yawToWorld(M_PI / 2), *ground, false);
  showPlane->root->position.set(a.pos.x, a.pos.y, a.pos.z);
  showPlane->root->quaternion.set(a.q[0], a.q[1], a.q[2], a.q[3]);
  showPlane->update(planeVisual(a), 0, time);
  showGroup->add(showPlane->root);
  showSpots.add(ShowSpot{.kind = "plane",
                         .x = ax,
                         .y = groundAt(ax, az),
                         .z = az,
                         .yaw = frame.yawToWorld(M_PI / 2),
                         .outline = VEHICLES.at("plane").outline,
                         .height = 3,
                         .roots = {showPlane->root.get()}});
  nameSign(tr("vehPlane", lang), ax, groundAt(ax, az) + 4.2, az);
  showGroup->traverse([](Object3D& o) {
    if (o.type == ObjectType::Mesh) o.castShadow = true;
  });
}

}  // namespace fs
