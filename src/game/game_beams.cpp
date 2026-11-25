// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (the headlights: headlights – game.ts:3537–3570)
#include "game/beamplay.h"
#include "game/game.h"
#include "game/onfoot.h"
#include "game/openplay.h"
#include "i18n/index.h"
#include "input/input.h"
#include "render/car.h"
#include "render/fleet.h"
#include "sim/hooks.h"
#include "sim/pedestrians.h"
#include "sim/rules.h"
#include "sim/traffic.h"
#include "sim/vehicle/vehicle.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

void Game::headlights(double dt, VehicleModel* model) {
  Vehicle* v = vehicle.get();
  const bool drive = state == "drive";
  // (later: not the tram – game.ts:3541)
  const bool driving = model && v && !walker && !aircraft;
  const Controls& c = input->c;
  const bool night = beams->dark() > 0 && traffic;
  const Lane* lane = rules->laneNow();
  const Edge* edge = lane ? lane->edge : nullptr;
  BeamFrame f;
  f.dt = drive ? dt : 0;
  f.driving = driving;
  f.judged = drive && !(demo && demo->active) && !opens.passenger() && !rules->yard;
  f.root = model ? model->root.get() : nullptr;
  f.front = def->outline.front;
  f.height = def->model == "motorcycle" ? 1 : def->model == "car" ? 0.68 : 0.95;
  f.x = mpos[0];
  f.z = mpos[2];
  f.yaw = v ? v->yaw : 0;
  f.speed = v ? v->speed : 0;
  f.low = c.lowBeam;
  f.high = lastVisual ? lastVisual->highBeam : false;
  f.lit = edge && (edge->flags & (EF_URBAN | EF_LIT)) != 0;
  f.visibility = weatherNow().visibility;
  f.cheap = settings.graphics.shadows == "off";
  static const std::vector<TrafficCar*> none;
  f.cars = night ? &traffic->cars : &none;
  if (night) {
    if (traffic->cyclists)
      for (const Cyclist* k : traffic->cyclists->list) f.riders.push_back(BeamUser{k->id, k->x, k->z, k->yaw});
    if (traffic->crowd)
      for (const Walker* p : traffic->crowd->people()) f.riders.push_back(BeamUser{p->id, p->x, p->z, p->yaw});
  }
  f.scene = app.scene;
  f.camera = app.camera.get();
  f.heightPx = app.canvas.num("height");
  f.heightAt = [this](double x, double z) { return app.tiles->heightAt(x, z); };
  const BeamFrameResult r = beams->frame(f);
  for (const BeamFault& x : r.faults) rules->note(x.id, x.severity, {}, x.cooldown);
  if (!r.tip.empty() && driving && settings.hudRules && !rules->strict && !(demo && demo->active)) {
    tipText = tr(r.tip, settings.lang);
    tipUntil = time + 5;
  }
}

}  // namespace fs
