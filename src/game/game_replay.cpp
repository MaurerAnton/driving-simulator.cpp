// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Game: the replay of a drive (game.ts "replay", game.ts:3575 … 3804) –
 * offerReplay, captureFrame, replayState, startReplay, endReplay,
 * placeReplay, replayCamera.  What is written down and how it is played:
 * game/replay.h.
 *
 * Origin: src/game/game.ts @ v1.7.7
 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "audio/audio.h"
#include "game/game.h"
#include "game/replay.h"
#include "input/input.h"
#include "render/plane.h"
#include "render/props.h"
#include "render/trams.h"
#include "render/truck.h"
#include "render/walker.h"
#include "sim/flight.h"
#include "sim/pedestrians.h"
#include "sim/rail.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/trailer.h"
#include "sim/vehicle/index.h"
#include "ui/hud.h"

namespace fs {

Group& Game::replayRoot() { return def->model == "plane" && planeModel ? *planeModel->root : *model().root; }

/** The replay is offered once there is something to see again (a few seconds of the drive). */
void Game::offerReplay() {
  if (recorder.frames.size() > 30) hud->onReplay = [this] { startReplay(); };
  else hud->onReplay = nullptr;
}

/** One frame of the drive for its replay: what is drawn of the player, and – when due – of the others and the lights. */
ReplayFrame Game::captureFrame(bool others, bool lights) {
  Group& r = replayRoot();
  ReplayFrame f;
  f.t = time;
  f.pose = {static_cast<float>(r.position.x), static_cast<float>(r.position.y), static_cast<float>(r.position.z), static_cast<float>(r.quaternion.x),
            static_cast<float>(r.quaternion.y), static_cast<float>(r.quaternion.z), static_cast<float>(r.quaternion.w)};
  if (aircraft) {
    PlaneVisualState pv = planeVisual(*aircraft);
    f.look = std::make_shared<const std::vector<float>>(recorder.planeLook.pack(planeFields(pv)));
  } else if (!(def->kind == "pedestrian" || tram))
    f.look = std::make_shared<const std::vector<float>>(recorder.look.pack(visualFields(visual)));
  if (const Trailer* t = trailer.get())
    f.trailer = std::make_shared<const std::vector<float>>(std::vector<float>{static_cast<float>(t->x), static_cast<float>(t->y), static_cast<float>(t->z), static_cast<float>(t->yaw),
                                                                              static_cast<float>(t->pitch), static_cast<float>(t->wheelSpin), t->coupled ? 1.0F : 0.0F});
  if (const OnFoot* w = walker.get())
    // (later: riding, busRide – game.ts:3625: riding a tram or a bus as a passenger is not in the port)
    f.walker = std::make_shared<const std::vector<float>>(std::vector<float>{static_cast<float>(w->x), static_cast<float>(w->y), static_cast<float>(w->z), static_cast<float>(w->yaw),
                                                                             static_cast<float>(w->distance), w->speed > 0.25 ? 1.0F : 0.0F, 0.0F});
  if (lights) f.signals = std::make_shared<const SignalSnapshot>(signals->snapshot(r.position.x, r.position.z, 600));
  if (others) {
    const std::vector<TrafficCar*>& cars = traffic->cars;
    std::vector<float> c;
    c.reserve(cars.size() * CAR_FIELDS);
    // (as they are drawn: eased through the corners of their ways – sim/smoothpose.ts)
    for (const TrafficCar* k : cars) {
      const double x = k->drawn ? k->drawn->x : k->x;
      const double z = k->drawn ? k->drawn->z : k->z;
      const double yaw = k->drawn ? k->drawn->yaw : k->yaw;
      for (double v : {static_cast<double>(k->id), static_cast<double>(k->style), x, k->y, z, yaw, k->pitch, k->wheelSpin, static_cast<double>(k->blinker), k->braking ? 1.0 : 0.0, k->v,
                       static_cast<double>(k->color), k->bus && k->bus->line ? static_cast<double>(k->bus->line->index) : -1.0})
        c.push_back(static_cast<float>(v));
    }
    f.cars = std::make_shared<const std::vector<float>>(std::move(c));
    // (the people on foot near the player: those that matter in a replay – the town has many more)
    std::vector<float> p;
    if (traffic->crowd) {
      const std::vector<Walker*>& peds = traffic->crowd->near(r.position.x, r.position.z, 90);
      p.reserve(peds.size() * PED_FIELDS);
      for (const Walker* k : peds)
        for (double v : {static_cast<double>(k->id), static_cast<double>(k->colour), k->x, k->y, k->z, k->yaw, k->state == "cross" ? 1.0 : 0.0, k->phase}) p.push_back(static_cast<float>(v));
    }
    f.peds = std::make_shared<const std::vector<float>>(std::move(p));
    std::vector<float> y;
    if (traffic->cyclists) {
      for (const Cyclist* k : traffic->cyclists->list)
        for (double v : {static_cast<double>(k->id), static_cast<double>(k->colour), k->x, k->y, k->z, k->yaw, k->phase, k->roll, k->down}) y.push_back(static_cast<float>(v));
    }
    f.cyclists = std::make_shared<const std::vector<float>>(std::move(y));
    std::vector<float> railParts;
    for (const auto& vp : rail->vehicles) {
      const RailVehicle& v = *vp;
      const int set = v.consist->units.value_or(1) > 1 ? 1 : v.consist->trailer ? 2 : 0;
      for (double x : {static_cast<double>(v.id), static_cast<double>(v.kind), static_cast<double>(set), v.line ? static_cast<double>(v.line->index) : -1.0, v.panto,
                       static_cast<double>(v.cars.size())})
        railParts.push_back(static_cast<float>(x));
      for (const RailCar& c2 : v.cars)
        for (double x : {c2.x, c2.y, c2.z, c2.yaw, c2.pitch}) railParts.push_back(static_cast<float>(x));
    }
    f.rail = std::make_shared<const std::vector<float>>(std::move(railParts));
  }
  return f;
}

ReplayState Game::replayState() {
  const ReplayView& rp = *replay;
  return ReplayState{.t = rp.t, .start = rp.start(), .end = rp.end(), .playing = rp.playing, .speed = rp.speed, .camera = replayCam.mode};
}

/** Watch the drive again: from a few seconds before the last note of the instructor, else from its start. */
void Game::startReplay() {
  const Recorder& rec = recorder;
  if (rec.frames.size() < 2 || (state != "pause" && state != "result")) return;
  replayFrom = state == "result" ? "result" : "pause";
  replayLive = std::make_shared<const SignalSnapshot>(signals->snapshot(0, 0, std::numeric_limits<double>::infinity()));
  replayShown = nullptr;
  const ReplayNote* lastNote = rec.notes.empty() ? nullptr : &rec.notes.back();
  replay = std::make_unique<ReplayView>(rec, lastNote ? std::optional<double>(lastNote->t - 8) : std::optional<double>(rec.start()));
  ReplayCam& c = replayCam;
  c.mode = "behind";
  c.yaw = 0;
  c.pitch = -0.25;
  c.dist = def->long_ ? 24 : def->kind == "plane" ? 18 : def->kind == "pedestrian" ? 4.5 : 9;
  replayHidden = railActors->hidden;
  railActors->hidden = nullptr;
  state = "replay";
  hud->hideOverlay();
  hud->setHudVisible(false);
  input->enabled = true;
  input->resetLook(false, true);
  ReplayView* rp = replay.get();
  static const std::vector<double> speeds = {0.5, 1, 2, 4};
  hud->showReplay(
      ReplayBar{
          .toggle =
              [rp] {
                if (!rp->playing && rp->t >= rp->end() - 0.05) rp->seek(rp->start());
                rp->playing = !rp->playing;
              },
          .step = [rp](double sec) { rp->seek(rp->t + sec); },
          .speed =
              [rp] {
                const auto at = std::find(speeds.begin(), speeds.end(), rp->speed);
                const long i = at == speeds.end() ? -1 : at - speeds.begin();
                rp->speed = speeds[static_cast<size_t>((i + 1) % static_cast<long>(speeds.size()))];
              },
          .seek = [rp](double t) { rp->seek(t); },
          .camera =
              [this] {
                ReplayCam& cam = replayCam;
                cam.mode = cam.mode == "behind" ? "driver" : cam.mode == "driver" ? "free" : "behind";
                cam.yaw = 0;
                cam.pitch = cam.mode == "driver" ? 0 : -0.25;
              },
          .exit = [this] { endReplay(); },
      },
      rec.notes, replayState());
}

/** Back from the replay to where it was opened: the live lights, the pause menu or the result. */
void Game::endReplay() {
  if (state != "replay") return;
  if (replayLive) signals->restore(*replayLive);
  replayLive = nullptr;
  replayShown = nullptr;
  // (the bar's buttons hold the view: it goes once the bar has)
  hud->hideReplay();
  replay = nullptr;
  railActors->hidden = replayHidden;
  figure->hide();
  input->enabled = false;
  input->resetLook(cameraMode == "chase", cameraMode == "free");
  // (the vehicle where it really stands again)
  vehicle->getModelPosition(mpos.data());
  if (replayFrom == "result" && lastResult) {
    state = "result";
    hud->showResult(*lastResult);
  } else {
    state = "pause";
    hud->showPause();
  }
}

/** The replay's clock, and the player's model, trailer and figure as they were then. */
void Game::placeReplay(double dt) {
  ReplayView* rp = replay.get();
  if (!rp) return;
  rp->advance(dt);
  hud->updateReplay(replayState());
  const std::optional<ReplayView::Between> fr = rp->frame();
  if (!fr) return;
  const auto& p = rp->pose;
  VehicleModel& model = this->model();
  Group& root = replayRoot();
  root.position.set(p[0], p[1], p[2]);
  root.quaternion.set(p[3], p[4], p[5], p[6]);
  root.updateMatrixWorld(true);
  mpos[0] = p[0];
  mpos[1] = p[1];
  mpos[2] = p[2];
  const ReplayFrame& a = *fr->a;
  if (a.look && aircraft && planeModel) {
    PlaneVisualState vis = planeVisual(*aircraft);
    recorder.planeLook.unpack(*a.look, planeFields(vis));
    planeModel->update(vis, dt, time);
  } else if (a.look) {
    recorder.look.unpack(*a.look, visualFields(visual));
    model.update(visual, dt, time);
  }
  if (const auto& t = a.trailer) {
    const std::vector<float>& tr = *t;
    if (truck && def->kind == "truck") truck->setTrailer(tr[0], tr[1], tr[2], tr[3], tr[4], tr[5], tr[6] == 1);
    if (carTrailer && def->model != "truck") carTrailer->place(tr[0], tr[1], tr[2], tr[3], tr[4], tr[5], &visual);
  }
  const ReplayPart& w = a.walker;
  const ReplayPart& wb = fr->b->walker ? fr->b->walker : w;
  if (w && wb && (*w)[6] != 1) {
    const std::vector<float>& wa = *w;
    const std::vector<float>& wc = *wb;
    const double u = fr->u;
    double dy = static_cast<double>(wc[3]) - wa[3];
    while (dy > M_PI) dy -= 2 * M_PI;
    while (dy < -M_PI) dy += 2 * M_PI;
    figure->show(wa[0] + (wc[0] - wa[0]) * u, wa[1] + (wc[1] - wa[1]) * u, wa[2] + (wc[2] - wa[2]) * u, wa[3] + dy * u, wa[4] + (wc[4] - wa[4]) * u, wa[5] == 1, time);
  } else figure->hide();
  app.tiles->update(p[0], p[2]);
  props->update(p[0], p[2]);
}

/** The camera of the replay: behind the vehicle (turning with it), from the driver's seat, or free round it – dragged round, closer and further. */
void Game::replayCamera() {
  ReplayView* rp = replay.get();
  if (!rp) return;
  Camera& cam = *app.camera;
  ReplayCam& c = replayCam;
  const Input::Cam& t = input->takeCam();
  c.yaw += t.yaw;
  c.pitch = std::max(c.mode == "driver" ? -1.0 : -1.45, std::min(c.mode == "driver" ? 0.8 : 0.35, c.pitch + t.pitch));
  c.dist = std::max(2.5, std::min(150.0, c.dist * std::exp(-t.dolly * 0.25)));
  Group& root = replayRoot();
  const Vector3 fwd = tmpV.set(0, 0, -1).applyQuaternion(root.quaternion);
  const double heading = std::atan2(-fwd.x, -fwd.z);
  cam.fov = 60;
  cam.near = 0.1;
  const std::optional<ReplayView::Between> fr = rp->frame();
  const std::vector<float>* w = fr && fr->a->walker ? fr->a->walker.get() : nullptr;
  if (c.mode == "driver" && !(w && (*w)[6] != 1)) {
    // the driver's eyes, looking where the vehicle points (turned by dragging)
    Vector3& eye = headV.copy(def->model == "plane" && planeModel ? planeModel->eye : model().eye);
    root.localToWorld(eye);
    cam.position.copy(eye);
    cam.quaternion.setFromEuler(Euler(c.pitch, heading + c.yaw, 0, Euler::YXZ));
  } else {
    Vector3& target = headV;
    if (w && (*w)[6] != 1) target.set((*w)[0], (*w)[1] + 1.4, (*w)[2]);
    else target.set(root.position.x, root.position.y + (def->long_ ? 2 : 1.2), root.position.z);
    const double yaw = (c.mode == "behind" ? (w && (*w)[6] != 1 ? static_cast<double>((*w)[3]) : heading) : 0) + c.yaw;
    const double cp = std::cos(c.pitch);
    cam.position.set(target.x + std::sin(yaw) * cp * c.dist, target.y - std::sin(c.pitch) * c.dist, target.z + std::cos(yaw) * cp * c.dist);
    const double gy = app.tiles->heightAt(cam.position.x, cam.position.z);
    if (!std::isnan(gy)) cam.position.y = std::max(cam.position.y, gy + 0.6);
    cam.lookAt(target);
  }
  cam.updateProjectionMatrix();
}

}  // namespace fs
