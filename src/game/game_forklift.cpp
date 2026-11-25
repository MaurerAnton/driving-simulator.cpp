// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (the forklift: makeForkliftWork, endForklift, forkView, forkliftDoor, stepForklift – game.ts:4453–4577)
#include "audio/audio.h"
#include "game/forkliftmodes.h"
#include "game/forkliftplay.h"
#include "game/game.h"
#include "game/onfoot.h"
#include "i18n/index.h"
#include "render/walker.h"
#include "input/input.h"
#include "sim/collision.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/vehicle/vehicle.h"

namespace fs {

std::unique_ptr<ForkliftWork> Game::makeForkliftWork(bool site) {
  ForkliftHost host;
  host.scene = app.scene;
  host.ui = ui;
  host.settings = &settings;
  host.yard = yardFrame ? &*yardFrame : nullptr;
  host.heightAt = [this](double x, double z) { return app.tiles->heightAt(x, z); };
  host.input = input.get();
  host.audio = audio.get();
  host.note = [this](const std::string& id, const Severity& severity, double x, double z, const ViolationData& data, double cooldown) { rules->noteAt(id, severity, x, z, data, cooldown); };
  host.blocked = [this](double x, double y, double z, double yaw) { return collider->blocked(x, y, z, yaw, 0.02); };
  return std::make_unique<ForkliftWork>(std::move(host), site);
}

void Game::endForklift() {
  if (!forklift) return;
  forklift->dispose();
  forklift = nullptr;
  cameraMode = settings.view;
  input->resetLook(cameraMode == "chase", false);
}

void Game::forkView() {
  ForkliftWork* fw = forklift.get();
  if (!fw) return;
  const ForkCamera cam = forkCamera(fw->view);
  cameraMode = cam.mode;
  input->resetLook(cam.mode == "chase", cam.mode == "free", cam.rest);
  fc.ready = false;
  camSnap = true;
}

void Game::forkliftDoor() {
  ForkliftWork* fw = forklift.get();
  if (!fw || state != "drive") return;
  if (const OnFoot* w = walker.get()) {
    if (!fw->canBoard(w->x, w->z)) {
      tipText = tr("forkTooFar", settings.lang);
      tipUntil = time + 3;
      return;
    }
    walker = nullptr;
    figure->hide();
    forkView();
    audio->click("door");
    return;
  }
  const std::optional<ForkExit> at = fw->getOff();
  if (!at) {
    fw->tell("stopFirst");
    return;
  }
  input->c.handbrake = 1;
  vehicle->getModelPosition(mpos.data());
  leaveOnFoot(at->x, at->z, at->yaw, mpos[1]);
}

void Game::stepForklift(double dt) {
  ForkliftWork& fw = *forklift;
  // (the operator got off: on foot – the forklift stands as it was left)
  OnFoot* w = walker.get();
  if (w) stepWalk(dt);
  else fw.step(dt, *vehicle, input->c);
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
    player.speed = vehicle->speed;
    player.length = 3.8;
    player.width = 1.2;
  }
  // (later: rail.update – game.ts:4570)
  traffic->obstacles.clear();
  traffic->update(dt, player);
  if (fw.over) finish();
}

}  // namespace fs
