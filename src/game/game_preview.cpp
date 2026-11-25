// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Game: the vehicle behind the main menu (game.ts:1527 … 1620, 1846 … 1885,
 * 4975): setMenuPreview, menuPreviewKey, clearPreview, placePreview,
 * menuShow, menuSpot – the vehicle chosen in the menu of rows or on the
 * stage (ui/carousel.h, ui/stage.h), standing where the drive would begin,
 * the camera circling it.
 *
 * Origin: src/game/game.ts @ v1.7.7
 */
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include "game/address.h"
#include "game/arcadedrive.h"
#include "game/craneplay.h"
#include "game/forkliftplay.h"
#include "game/game.h"
#include "game/spawncar.h"
#include "game/spawnplace.h"
#include "game/vehicles.h"
#include "render/bike.h"
#include "render/crane.h"
#include "render/plane.h"
#include "render/semitruck.h"
#include "render/tiles.h"
#include "render/truck.h"
#include "render/walker.h"
#include "render/props.h"
#include "sim/flight.h"
#include "sim/ground.h"
#include "sim/rail.h"
#include "sim/trailer.h"
#include "sim/tramdrive.h"
#include "sim/vehicle/index.h"
#include "ui/carousel.h"
#include "ui/hud.h"
#include "ui/stage.h"
#include "world/loader.h"

namespace fs {

namespace {

/** `n.toFixed(d)` */
std::string fixed(double n, int d) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", d, n);
  return buf;
}

}  // namespace

void Game::setMenuPreview(const MenuPreview* p) {
  // (a crane is shown where its work begins: on the construction site)
  const VehicleDef* d = p ? VEHICLES.find(p->kind) : nullptr;
  const std::optional<CraneType> crane = d ? d->crane : std::nullopt;
  const std::optional<CranePlace> at = crane ? craneStart(*crane, yardFrame ? &*yardFrame : nullptr) : std::nullopt;
  if (p && at) {
    MenuPreview q = *p;
    q.x = at->x;
    q.z = at->z;
    q.yaw = at->yaw;
    menuPreview = q;
  } else if (p) menuPreview = *p;
  else menuPreview.reset();
  if (!p) clearPreview();
}

std::string Game::menuPreviewKey(const MenuPreview& p) {
  return p.kind + "|" + fixed(p.x, 1) + "|" + fixed(p.z, 1) + "|" + fixed(p.yaw, 2) + "|" + (p.stop ? p.stop->name : "") + "|" + settings.tramSet;
}

void Game::clearPreview() {
  if (previewTram) {
    auto& list = rail->vehicles;
    for (auto it = list.begin(); it != list.end(); ++it)
      if (it->get() == previewTram) {
        list.erase(it);
        break;
      }
    previewTram = nullptr;
  }
  figure->hide();
  car.root->visible = false;
  for (const auto& [key, model] : models) model->root->visible = false;
  if (planeModel) planeModel->root->visible = false;
  if (carTrailer) carTrailer->group->visible = false;
  if (truck) truck->trailer->visible = false;
  CraneHost ch;
  ch.scene = app.scene;
  ch.settings = &settings;
  ch.yard = yardFrame ? &*yardFrame : nullptr;
  craneMenuSite(ch, std::nullopt, [](double, double) { return 0.0; });
  ForkliftHost fh;
  fh.scene = app.scene;
  fh.settings = &settings;
  fh.yard = yardFrame ? &*yardFrame : nullptr;
  forkliftMenuSite(fh, false, [](double, double) { return 0.0; });
  previewCentre.reset();
  previewKey = "";
}

void Game::placePreview(const MenuPreview& p0) {
  clearPreview();
  previewKey = menuPreviewKey(p0);
  // (where it sensibly stands: on the road in front of the place chosen, facing along it, outside the buildings)
  const Place fit = menuSpot(p0);
  MenuPreview p = p0;
  p.x = fit.x;
  p.z = fit.z;
  p.yaw = fit.yaw;
  const VehicleDef& d = VEHICLES.find(p.kind) ? *VEHICLES.find(p.kind) : VEHICLES.at("car");
  def = &d;
  TileManager& tiles = *app.tiles;
  const double ty = tiles.heightAt(p.x, p.z);
  const double y = app.world->surfaceAt(p.x, p.z, ty + 3, hit) ? hit.y : ty;
  if (d.kind == "tram") {
    const std::optional<TramStart> at = tramStart(*rail, p.x, p.z, {RAIL_TRAM, RAIL_LIGHT}, p.stop ? p.stop->name : "");
    const std::string set = settings.tramSet;
    RailVehicle* v = nullptr;
    if (at) {
      v = rail->spawnAt(at->path, at->s, at->dir, 0, set);
      if (!v) v = rail->spawnAt(at->path, at->s, at->dir > 0 ? -1 : 1, 0, set);
    }
    if (!v) return;
    // (it stays where it is: the traffic of the trams leaves it alone)
    v->player = true;
    rail->assignLine(*v);
    previewTram = v;
    const RailCar& mid = v->cars[v->cars.size() / 2];
    previewCentre = Vector3(mid.x, mid.y + 1.8, mid.z);
    previewRadius = consistLength(*v->consist) * 0.55 + 7;
    return;
  }
  if (d.kind == "pedestrian") {
    figure->show(p.x, y, p.z, p.yaw, 0, false, time);
    previewCentre = Vector3(p.x, y + 1, p.z);
    previewRadius = 4.2;
    return;
  }
  Group* root = nullptr;
  if (d.kind == "plane") {
    // (the aeroplane's model is kept beside the road vehicles': planeAdapter)
    PlaneModel& pm = planeAdapter();
    Aircraft a(TRAINER, powerOf(d, settings.powers));
    a.placeOnGround(p.x, p.z, p.yaw, flightGround(), false);
    pm.root->position.set(a.pos.x, a.pos.y, a.pos.z);
    pm.root->quaternion.set(a.q[0], a.q[1], a.q[2], a.q[3]);
    pm.root->updateMatrixWorld(true);
    pm.update(planeVisual(a), 0, time);
    previewRadius = 15;
    root = pm.root.get();
  } else {
    VehicleModel& model = ensureModel(d);
    Vehicle v(d.spec(arcadeTransmission(settings.arcadeDrive, settings.transmission), powerOf(d, settings.powers)), *ground);
    v.teleport(p.x, y + 0.02, p.z, p.yaw);
    v.getModelPosition(previewPos.data());
    model.root->position.set(previewPos[0], previewPos[1], previewPos[2]);
    model.root->quaternion.set(v.quat[0], v.quat[1], v.quat[2], v.quat[3]);
    model.root->updateMatrixWorld(true);
    parkedVisual(v);
    model.update(visual, 0, time);
    if (PlayerBike* bike = dynamic_cast<PlayerBike*>(&model)) bike->setRider(true);
    if (d.trailer) {
      Trailer t(*d.trailer, *ground);
      t.coupleBehind(v);
      // (where the ground under its wheels is not loaded yet the trailer would tip: level, then, at the vehicle's height)
      const std::array<double, 2> level = levelTrailer(t, y);
      if (truck && d.kind == "truck") truck->setTrailer(t.x, level[0], t.z, t.yaw, level[1], 0, true);
      else if (carTrailer) carTrailer->place(t.x, level[0], t.z, t.yaw, level[1], 0, &visual);
    }
    previewRadius = d.long_ ? 25 : d.model == "motorcycle" ? 5.5 : d.model == "car" ? (d.trailer ? 12 : 8) : 15;
    if (d.crane) {
      if (CraneModel* cm = dynamic_cast<CraneModel*>(&model)) {
        // a crane: on its construction site, folded as it travels (the tower crane standing high over it)
        CraneHost ch;
        ch.scene = app.scene;
        ch.settings = &settings;
        ch.yard = yardFrame ? &*yardFrame : nullptr;
        craneMenuSite(ch, d.crane, [this](double x, double z) { return app.tiles->heightAt(x, z); });
        craneMenuPose(*cm, *d.crane);
        previewRadius = *d.crane == "tower" ? 55 : 18;
      }
    }
    if (d.kind == "forklift") {
      ForkliftHost fh;
      fh.scene = app.scene;
      fh.settings = &settings;
      fh.yard = yardFrame ? &*yardFrame : nullptr;
      forkliftMenuSite(fh, true, [this](double x, double z) { return app.tiles->heightAt(x, z); });
      previewRadius = 6;
    }
    root = model.root.get();
  }
  showModel(true);
  const Vector3& r = root->position;
  previewCentre = Vector3(r.x, r.y + (d.model == "car" || d.model == "motorcycle" ? 0.9 : d.crane && *d.crane == "tower" ? 22 : 1.8), r.z);
}

bool Game::menuShow() {
  if (!menuPreview) return false;
  const MenuPreview p = *menuPreview;
  TileManager& tiles = *app.tiles;
  // (the ground and the roads where it stands: they come in pieces)
  tiles.update(p.x, p.z);
  app.map->update(p.x, p.z, 25);
  props->update(p.x, p.z);
  if (previewKey != menuPreviewKey(p)) {
    if (std::isnan(tiles.heightAt(p.x, p.z)) || (app.map->pieces && !app.map->ready(p.x, p.z, 150)) || (p.kind == "tram" && rail->net.paths.empty())) {
      hud->setMenuLoading(app.map->pieces ? 0.35 * app.map->progress(p.x, p.z, 150) + 0.65 * tiles.progress : tiles.progress);
      return false;
    }
    placePreview(p);
    hud->setMenuLoading(std::nullopt);
  }
  if (!previewCentre) return false;
  const Vector3 c = *previewCentre;
  const double r = previewRadius;
  Camera& cam = *app.camera;
  // (the menu of rows aims below the vehicle: it stands in the upper part of the picture, above the rows;
  // the stage – ui/stage.ts – has it large in the middle)
  const StageCam f = settings.menuStyle == "stage" ? stageFrame(time, p.kind, r, cam.aspect) : StageCam{.angle = time * 0.1, .dist = r, .rise = r * 0.3 + 0.6, .aim = -r * 0.36, .fov = 50};
  cam.position.set(c.x + std::cos(f.angle) * f.dist, c.y + f.rise, c.z + std::sin(f.angle) * f.dist);
  const double gy = tiles.heightAt(cam.position.x, cam.position.z);
  if (!std::isnan(gy)) cam.position.y = std::max(cam.position.y, gy + 1.2);
  cam.lookAt(c.x, c.y + f.aim, c.z);
  cam.fov = f.fov;
  cam.near = 0.3;
  cam.updateProjectionMatrix();
  menuFocus.set(c.x, c.y, c.z);
  return true;
}

Game::Place Game::menuSpot(const MenuPreview& p) {
  const VehicleDef& d = VEHICLES.find(p.kind) ? *VEHICLES.find(p.kind) : VEHICLES.at("car");
  if (d.kind == "tram" || d.kind == "plane" || d.crane) return Place{p.x, p.z, p.yaw};
  const std::optional<AddressHit>& a = settings.lastStart.address;
  const bool chosen = a && std::abs(a->x - p.x) < 0.01 && std::abs(a->z - p.z) < 0.01;
  const Outline& o = d.outline;
  if (chosen && settings.placeExact) return fitExact(Place{p.x, p.z, p.yaw}, o);
  const auto ok = [this, &o](double x, double z, double yaw) { return spawnCars->free(x, z, yaw, &o, false); };
  std::optional<SpawnJson> road = chosen ? findRoadSpot(*app.world, *lanes, p.x, p.z, a ? a->street : "", 220, usableLane()) : std::nullopt;
  const Place at = road ? Place{road->x, road->z, road->yaw} : Place{p.x, p.z, p.yaw};
  if (ok(at.x, at.z, at.yaw)) return at;
  const std::optional<Spot> lane = freeSpot(at.x, at.z, at.yaw, 0);
  if (lane && ok(lane->x, lane->z, lane->yaw)) return Place{lane->x, lane->z, lane->yaw};
  const std::optional<FreePlace> near = freeNear(at.x, at.z, at.yaw, ok, 40);
  return near ? Place{near->x, near->z, near->yaw} : at;
}

}  // namespace fs
