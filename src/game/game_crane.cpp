// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (the cranes: makeCraneWork, craneToggle, stepCrane – game.ts:4402–4450)
#include <cmath>

#include "audio/audio.h"
#include "game/ce.h"
#include "game/craneplay.h"
#include "game/game.h"
#include "input/input.h"
#include "render/crane.h"
#include "sim/collision.h"
#include "sim/ground.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/vehicle.h"

namespace fs {

std::unique_ptr<CraneWork> Game::makeCraneWork(const CraneType& type) {
  CraneHost host;
  host.scene = app.scene;
  host.ui = ui;
  host.settings = &settings;
  host.yard = yardFrame ? &*yardFrame : nullptr;
  host.heightAt = [this](double x, double z) { return app.tiles->heightAt(x, z); };
  host.collider = collider.get();
  host.ground = ground.get();
  host.input = input.get();
  host.audio = audio.get();
  host.note = [this](const CraneFault& f, double x, double z) { rules->noteAt(f.id, f.severity, x, z, f.data, 0.5); };
  auto cw = std::make_unique<CraneWork>(type, std::move(host));
  cw->toggleRequested = [this] { craneToggle(); };
  return cw;
}

void Game::craneToggle() {
  CraneWork* cw = crane.get();
  if (!cw || state != "drive" || walker) return;
  const bool was = cw->operating;
  if (cw->toggle(*vehicle, input->c.handbrake > 0.5) || was == cw->operating) return;
  input->c.handbrake = 1;
  cameraMode = "cockpit";
  input->resetLook(false, false);
  input->c.lookZoom = cw->operating ? 22 : def->zoom;
}

void Game::stepCrane(double dt) {
  CraneWork& cw = *crane;
  // (climbed down from the tower crane: on foot – the crane stands as it was left)
  const OnFoot* w = walker.get();
  if (w) stepWalk(dt);
  else cw.work(dt, *vehicle);
  vehicle->getModelPosition(mpos.data());
  rules->time += dt;
  signals->update(dt);
  PlayerState player;
  if (w) {
    player.x = w->x;
    player.y = w->y;
    player.z = w->z;
    player.yaw = w->yaw;
    player.speed = w->speed;
    player.length = 0.6;
    player.width = 0.6;
    player.onFoot = true;
  } else {
    player.x = mpos[0];
    player.y = mpos[1];
    player.z = mpos[2];
    player.yaw = vehicle->yaw;
    player.speed = 0;
    player.length = 0.1;
    player.width = 0.1;
  }
  stepRail(dt, player, false);
  traffic->obstacles.clear();
  traffic->update(dt, player);
  if (cw.over) finish();
}

CraneModel* Game::craneModel() { return dynamic_cast<CraneModel*>(&model()); }

}  // namespace fs
