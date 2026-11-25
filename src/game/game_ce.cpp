// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 – class CE: truck and trailer (truckSpot … updateTask, game.ts:1944)
#include <algorithm>
#include <cmath>

#include "audio/audio.h"
#include "core/random.h"
#include "game/address.h"
#include "game/game.h"
#include "game/nav.h"
#include "game/track.h"
#include "game/semice.h"
#include "game/yardtask.h"
#include "i18n/index.h"
#include "input/input.h"
#include "render/yard.h"
#include "sim/ground.h"
#include "sim/rules.h"
#include "sim/traffic.h"
#include "sim/trailer.h"
#include "sim/vehicle/index.h"
#include "ui/hud.h"
#include "ui/pathdraw.h"

namespace fs {

SpawnJson Game::truckSpot(const SpawnJson& near) {
  const auto wide = [](const Edge& e) { return (e.cls == PRIMARY || e.cls == SECONDARY || e.cls == TERTIARY) && e.len - e.cutA - e.cutB > 60; };
  std::optional<SpawnJson> spot = findRoadSpot(*app.world, *lanes, near.x, near.z, "", 900, usableLane(), wide, 20);
  if (!spot) return near;
  spot->name = near.name;
  return *spot;
}

void Game::nextTask() {
  const YardFrame& frame = *yardFrame;
  const TaskId id = tasksLeft.front();
  tasksLeft.erase(tasksLeft.begin());
  auto now = std::make_unique<YardTaskNow>();
  now->run = std::make_unique<YardTask>(id, def->kind == "semi");
  const TaskLayout& layout = now->run->layout;
  task = std::move(now);
  const Pose start = frame.poseToWorld(layout.start);
  SpawnJson at;
  at.name = "";
  at.x = start.x;
  at.z = start.z;
  at.yaw = start.yaw;
  at.kind = "street";
  spawn = at;
  rules->yard = true;
  // the yard is asphalt while the lesson runs, whatever grows on the field
  ground->paved = [&frame, &layout](double x, double z) {
    const auto [u, w] = frame.toLocal(x, z);
    return offArea(layout, u, w, 1.2) == 0;
  };
  taskSub = "";
  lessonLine = "";
  // reversing is watched from high above, coupling from close by
  input->c.lookZoom = id == "couple" ? 17 : 34;
}

void Game::endTasks() {
  if (task) input->c.lookZoom = 21;
  task = nullptr;
  tasksLeft.clear();
  yardScene->hide();
  // (later: drillScene.hide – game.ts:1978)
  ground->paved = nullptr;
  ground->raise = nullptr;
  rules->yard = false;
  hud->setChoices({}, [](const std::string&) {});
}

void Game::placeAgain() {
  traffic->clear();
  rail->clear();
  vehicle->vel = {0, 0, 0};
  vehicle->angVel = {0, 0, 0};
  input->c.handbrake = 1;
  path->breakLine();
  keepClock = true;
  state = "placing";
  showModel(false);
  hud->showLoading(tr("loadingTiles", settings.lang));
}

void Game::taskDone(bool ok) {
  audio->click("chime");
  hud->setChoices({}, [](const std::string&) {});
  if (!ok) {
    taskFailed = true;
    finish();
    return;
  }
  if (!tasksLeft.empty()) {
    nextTask();
    placeAgain();
    return;
  }
  endTasks();
  if (lesson && !lesson->targets.empty() && roadSpawn) {
    // the tasks are done: now the drive in traffic
    spawn = truckSpot(*roadSpawn);
    placeAgain();
    return;
  }
  finish();
}

void Game::updateTask(double dt) {
  YardTaskNow& now = *task;
  YardTask& run = *now.run;
  const YardFrame& frame = *yardFrame;
  Vehicle& v = *vehicle;
  Trailer& t = *trailer;
  const std::string lang = settings.lang;
  const bool handbrake = input->c.handbrake > 0.5;
  // (ground that was not loaded when the yard was laid out: once more, now and then)
  if (!now.laid && std::abs(time - laidTry) > 0.7) {
    laidTry = time;
    now.laid = yardScene->show(frame, run.layout, [this](double x, double z) { return app.tiles->heightAt(x, z); });
  }
  if (run.judge) {
    const ManoeuvreStatus st = run.manoeuvre(dt, frame, v, t, handbrake);
    // a basic manoeuvre that went wrong may be repeated once – as in the test
    for (const ManoeuvreFault& f : st.faults) rules->note(f, f == "ceLine" || !now.second ? "major" : "fail");
    lessonLine = tr(taskTextKey(run.semi, "do", run.id), lang);
    taskSub = tr("ce.corrections", lang, {{"n", st.corrections}, {"max", MAX_CORRECTIONS}});
    if (std::isfinite(st.gap) && st.gap < 15) taskSub += " · " + tr("ce.gap", lang, {{"d", formatUnit(std::max(0.0, st.gap), Unit::Meter, lang, 1)}});
    if (st.state == "failed" && !now.second) {
      tasksLeft.insert(tasksLeft.begin(), run.id);
      nextTask();
      task->second = true;
      placeAgain();
      hud->message(tr("ce.again", lang));
    } else if (st.state != "run") taskDone(st.state == "done");
    return;
  }
  CouplingProcedure& proc = *run.coupling;
  const CouplingStatus st = run.couple(dt, v, t, handbrake);
  if (run.bumped) rules->note("ceFast", "major");
  for (const CouplingFault& f : st.faults) rules->note(f, "major");
  if (st.couple) audio->click("chime");
  // what can be done on foot now – in an order of its own each time, so that the list does not give the order away
  std::vector<CouplingStepId> choices = st.state == "run" && proc.onFoot(run.state) ? proc.choices() : std::vector<CouplingStepId>{};
  for (const CouplingStepId& id : choices)
    if (!now.order.count(id)) now.order[id] = rnd();
  std::stable_sort(choices.begin(), choices.end(), [&now](const CouplingStepId& a, const CouplingStepId& b) { return now.order[a] < now.order[b]; });
  std::vector<Hud::Choice> items;
  for (const CouplingStepId& id : choices) items.push_back({id, tr(taskTextKey(run.semi, "step", id), lang)});
  YardTask* runPtr = &run;
  hud->setChoices(
      items,
      [this, runPtr](const std::string& id) {
        // (the task may have ended since the list was shown)
        if (!task || task->run.get() != runPtr || !runPtr->coupling) return;
        const std::optional<CouplingFault> fault = runPtr->coupling->act(id, runPtr->state);
        if (fault) rules->note(*fault, "major", {}, 0.5);
        else audio->click("chime");
      },
      // (nothing to do with the truck just now: it is the driver's turn on foot)
      !st.todo);
  lessonLine = st.todo ? tr(taskTextKey(run.semi, "do", *st.todo), lang) : !choices.empty() ? tr("ce.onFoot", lang) : tr("ce.do.uPark", lang);
  taskSub = std::to_string(proc.done.size()) + " / " + std::to_string(COUPLING_STEPS.size());
  if (st.state == "done") taskDone(true);
}

}  // namespace fs
