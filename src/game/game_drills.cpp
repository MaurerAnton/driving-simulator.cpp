// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (class B: the basic tasks on the practice yard – drillBegin, drillPlace, updateDrill, game.ts:2076–2131)
#include "audio/audio.h"
#include "game/drillplay.h"
#include "game/drills.h"
#include "game/game.h"
#include "i18n/index.h"
#include "input/input.h"
#include "render/drillyard.h"
#include "render/yard.h"
#include "sim/ground.h"
#include "sim/rules.h"
#include "sim/vehicle/vehicle.h"
#include "ui/hud.h"

namespace fs {

void Game::drillBegin(const std::vector<std::string>& ids) {
  const auto& o = def->outline;
  std::vector<DrillId> list;
  for (const std::string& id : ids) {
    if (id == "exam")
      for (const DrillId& d : examDrills()) list.push_back(d);
    else
      list.push_back(id);
  }
  drill = std::make_unique<DrillRun>(list, *yardFrame, CarShape{.front = o.front, .rear = o.rear, .hw = o.hw, .axles = def->axles, .track = def->track}, *yardScene, *drillScene,
                                     [this] { return settings.lang; });
  drillPlace();
  rules->yard = true;
  // the yard is asphalt while the lesson runs, the ramp of the hill start raises the ground
  DrillRun* d = drill.get();
  ground->paved = [d](double x, double z) { return d->paved(x, z); };
  ground->raise = [d](double x, double z) { return d->raise(x, z); };
  lessonLine = drill->line();
  taskSub = drill->sub();
}

void Game::drillPlace() {
  DrillRun& d = *drill;
  const Pose at = d.start();
  spawn = SpawnJson{.name = "", .x = at.x, .z = at.z, .yaw = at.yaw, .kind = "street", .rough = false, .g = ""};
  input->c.lookZoom = d.layout().zoom > 0 ? d.layout().zoom : def->zoom;
}

void Game::updateDrill(double dt) {
  DrillRun& d = *drill;
  Vehicle& v = *vehicle;
  const Controls& c = input->c;
  const std::string& lang = settings.lang;
  v.getModelPosition(drillPos.data());
  const DrillEvents& ev = d.update(dt, {.x = drillPos[0], .z = drillPos[2], .yaw = v.yaw, .speed = v.speed, .handbrake = c.handbrake > 0.5, .brake = c.brake, .engine = v.engineRunning},
                                   [this](double x, double z) { return app.tiles->heightAt(x, z); });
  for (const DrillFault& f : ev.faults) rules->note(f.id, f.severity, f.data, 0.5);
  if (ev.stop) {
    // (into a parked car: it stops there)
    v.vel = {0, 0, 0};
    v.angVel = {0, 0, 0};
    audio->click("bump");
  }
  if (ev.signal) {
    // the instructor's signal for the emergency stop
    audio->click("beep");
    hud->message(tr("drill.signal", lang), tr("drill.signalSub", lang));
  }
  if (!ev.remark.empty()) {
    tipText = ev.remark;
    tipUntil = time + 6;
  }
  lessonLine = d.line();
  taskSub = d.sub();
  const std::string next = ev.next;
  if (next == "again" || next == "next") {
    audio->click("chime");
    drillPlace();
    placeAgain();
  } else if (next == "over")
    finish();
}

}  // namespace fs
