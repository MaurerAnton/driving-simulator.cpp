// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Game orchestration: loading, menus, the main loop, cameras, lessons.
 *
 * Origin: src/game/game.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * The state machine, the fixed-step physics, the lesson bookkeeping and the
 * three camera modes are carried over; the platform supplies the clock and
 * the query string.
 */
#include "game/game.h"

#include <sstream>

#include "geo/utm.h"
#include "reality/testpano.h"
#include "platform/gunzip.h"
#include "render/truck.h"
#include "ui/bigmap.h"
#include "sim/autodriver.h"
#include "sim/vehicle/spec.h"
#include "render/yard.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace fs {

namespace {
constexpr double PHYS_DT = 1.0 / 240.0;

/** the pieces of a comma-separated query value ("a,b" → {"a", "b"}) */
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string one;
  while (std::getline(ss, one, sep)) out.push_back(one);
  return out;
}

/** the outlines of the vehicles for collisions with the scenery: circles along the axis
 *  (m from the model origin; - = front) */
const double CAR_CIRCLES[3] = {-1.28, -0.05, 1.18};
const double TRUCK_CIRCLES[4] = {-2.5, -0.6, 1.3, 3.2};
}  // namespace

/**
 * What the reality view asks the game: the saved settings, where on the map
 * a photo may belong, and closing it again (the panel goes back to the pause
 * menu through Game::closeReality).
 */
struct RealityHostImpl : RealityHost {
  Game& game;
  explicit RealityHostImpl(Game& g) : game(g) {}
  Settings& settings() override { return game.settings; }
  void saveSettings() override { fs::saveSettings(game.settings); }
  void close() override { game.closeReality(); }
  bool plausible(double lat, double lon) const override {
    const auto p = latLonToWorld(lat, lon);
    const WorldMetaJson& m = game.worldJson_.meta;
    return p.first > m.minX - 500 && p.first < m.maxX + 500 && p.second > m.minZ - 500 &&
           p.second < m.maxZ + 500;
  }
  int maxTexture() const override { return 8192; }
  void openAerial() override {
    // the map with aerial photographs is the browser's job (see the original's
    // ?at= link); here nothing happens
  }
};

Game::~Game() = default;

Game::Game(HudHost& host, Platform& platform, AudioBackend& audioBackend,
           SignSource& signSource, TileSource& tileSource, int canvasWidth, int canvasHeight,
           double pixelRatio, bool coarsePointer)
    : settings(loadSettings()),
      app(canvasWidth, canvasHeight, pixelRatio),
      coarsePointer_(coarsePointer),
      audio(audioBackend),
      host_(host),
      signSource_(signSource),
      tileSource_(tileSource),
      platform_(platform) {}


std::string Game::t(const std::string& key) const {
  return fs::tr(key, settings.lang);
}

// ---------------------------------------------------------------- addresses
/** The address list (house numbers of the map), read when somebody searches. */
AddressIndex* Game::loadAddresses() {
  if (addressAsked_) return addressIndex_.get();
  addressAsked_ = true;
  // the original fetches `map/addresses.json` here; the platform fetches it
  // (gzip in the browser, bytes in a test)
  std::vector<uint8_t> raw;
  if (platform_.addressList(raw)) {
    // gzip in the browser; the bytes of a test are the document itself
    std::string text(raw.begin(), raw.end());
    if (raw.size() > 2 && raw[0] == 0x1f && raw[1] == 0x8b) {
      std::string plain;
      if (gunzip(raw, plain)) text = plain;
    }
    AddressJson aj;
    if (parseAddressJson(text, aj)) addressIndex_ = std::make_unique<AddressIndex>(aj);
    else addressIndex_.reset();
  } else {
    addressIndex_.reset();
  }
  return addressIndex_.get();
}

/** The spot on the road in front of an address, as a starting point. */
bool Game::spawnAt(const AddressHit& hit, SpawnJson& out) {
  if (!mainLanes_) mainLanes_ = std::make_unique<std::set<int>>(mainNetwork(*app.world, *lanes));
  SpawnJson spot =
      findRoadSpot(*app.world, *lanes, hit.x, hit.z, hit.street, 220, mainLanes_.get());
  if (spot.kind.empty()) return false;
  out = spot;
  out.name = hit.label;
  return true;
}

/** The place on a road next to a point of the map where the car could stand. */
SpawnJson Game::spotAt(double x, double z) {
  if (!mainLanes_) mainLanes_ = std::make_unique<std::set<int>>(mainNetwork(*app.world, *lanes));
  SpawnJson s = findRoadSpot(*app.world, *lanes, x, z, "", 160, mainLanes_.get());
  return s;
}

/** Put the car at a spot chosen on the map (a free drive only). */
void Game::teleportSpot(const SpawnJson& spot) {
  if (hasLesson_ || (state != GameState::Pause && state != GameState::Map)) return;
  if (bigMap) bigMap->close();
  demo_.reset();
  nav->clear();
  hud->routeLanes.clear();
  rules->allowed = nullptr;
  spawn_ = spot;
  hasSpawn_ = true;
  traffic->clear();
  if (rail) rail->clear();
  vehicle->vel[0] = vehicle->vel[1] = vehicle->vel[2] = 0;
  vehicle->angVel[0] = vehicle->angVel[1] = vehicle->angVel[2] = 0;
  input->c.handbrake = 1;
  if (path_) path_->breakLine();
  keepClock_ = true;
  state = GameState::Placing;
  showModel(false);
  hud->setHudVisible(true);
  hud->showLoading(t("loadingTiles"));
}

/** Free drive: jump to an address (from the pause menu). */
void Game::teleportTo(const AddressHit& hit) {
  if (state != GameState::Pause || hasLesson_) return;
  SpawnJson spot;
  if (!spawnAt(hit, spot)) {
    hud->showPause();
    hud->message(fs::tr("addressNoRoad", settings.lang), hit.label);
    return;
  }
  spawn_ = spot;
  hasSpawn_ = true;
  traffic->clear();
  if (rail) rail->clear();
  vehicle->vel[0] = vehicle->vel[1] = vehicle->vel[2] = 0;
  vehicle->angVel[0] = vehicle->angVel[1] = vehicle->angVel[2] = 0;
  input->c.handbrake = 1;
  state = GameState::Placing;
  car.root.visible = false;
  hud->showLoading(fs::tr("loadingTiles", settings.lang));
}

// ---------------------------------------------------------- reality view
/** Real photographs of where the car is (own files and free imagery services). */
void Game::openReality() {
  if (state != GameState::Drive && state != GameState::Pause) return;
  state = GameState::Reality;
  input->enabled = false;
  audio.suspend();
  hud->hideOverlay();
  hud->setHudVisible(false);
  vehicle->getModelPosition(mpos_);
  auto ll = worldToLatLon(mpos_[0], mpos_[2]);
  // yaw: 0 = north, positive = to the left; a compass counts clockwise
  double heading = std::fmod(-vehicle->yaw * 180.0 / M_PI, 360.0);
  if (heading < 0) heading += 360.0;
  RealityPlace place;
  place.lat = ll.first;
  place.lon = ll.second;
  place.heading = heading;
  place.name = rules->context.roadName;
  reality_->open(place);
}

void Game::closeReality() {
  if (state != GameState::Reality) return;
  reality_->close();
  state = GameState::Pause;
  hud->setHudVisible(true);
  hud->showPause();
}

void Game::onTile(LoadedTile& tile, bool loaded) {
  if (loaded) {
    // the terrain's ground-cover texture (three.js: the DataTexture the
    // terrain material samples; uploaded when the tile arrives)
    if (!tile.coverTex.empty())
      app.renderer.uploadTexture("cover:" + tile.key, tile.coverTex.data(), tile.coverTexN,
                                 tile.coverTexN, true);
    props.add(tile);
    signs->add(tile);
    std::vector<double> discs;
    for (const auto& s : tile.header.signs) {
      discs.push_back(s.x);
      discs.push_back(s.y);
      discs.push_back(s.z);
      discs.push_back(0.06);
    }
    for (const auto& o : tile.header.objects)
      if (o.k == "lamp") {
        discs.push_back(o.x);
        discs.push_back(o.y);
        discs.push_back(o.z);
        discs.push_back(0.09);
      }
    const std::vector<double>* trees = tile.data.get("trees");
    if (trees) {
      // only real trunks, and only where a car can plausibly get to
      for (size_t i = 0; i + 4 < trees->size(); i += 5) {
        double kind = (*trees)[i + 4];
        if (static_cast<int>(kind) == 2) continue;
        discs.push_back((*trees)[i]);
        discs.push_back((*trees)[i + 1]);
        discs.push_back((*trees)[i + 2]);
        discs.push_back(static_cast<int>(kind) == 3 ? 0.12 : 0.22);
      }
    }
    CollisionSource src;
    src.key = tile.key;
    src.discs = discs.data();
    src.discCount = static_cast<int>(discs.size() / 4);
    collider.add(src);
  } else {
    props.remove(tile.key);
    signs->remove(tile.key);
    collider.remove(tile.key);
  }
}

void Game::applySettings() {
  const Settings& s = settings;
  App& a = app;
  Quality q = s.quality;
  // Low: for weak devices – a softer picture (70 % of the pixels per side),
  // no shadows, a short view.  Medium: sharp, shadows around the car.
  // High: every pixel of the screen, the longest view, all trees.
  a.pixelRatioCap = q == Quality::Low ? 1 : (q == Quality::Medium ? 1.5 : 3);
  a.renderScale = q == Quality::Low ? 0.7 : 1;
  a.setShadows(q == Quality::Low ? 0 : (q == Quality::Medium ? 1536 : 3072), q == Quality::High ? 120 : 55);
  a.tiles->viewRadius = q == Quality::Low ? 700 : (q == Quality::Medium ? 1250 : 2000);
  a.sky.fog->near = a.tiles->viewRadius * 0.3;
  a.sky.fog->far = a.tiles->viewRadius * 1.05;
  props.density = q == Quality::Low ? 0.35 : (q == Quality::Medium ? 0.8 : 1);
  props.setShadows(q == Quality::High);
  a.resize(0, 0);
  // the mirrors show what is behind (or nothing at all when switched off)
  {
    bool on = s.mirrors != Mirrors::Off;
    for (auto& m : car.mirrors) m->setEnabled(on);
    car.setCabinOpacity(s.cabinOpacity);
  }
  if (truck) {
    bool on = s.mirrors != Mirrors::Off;
    for (auto& m : truck->mirrors) m->setEnabled(on);
    truck->setCabinOpacity(s.cabinOpacity);
  }
  audio.setVolume(s.volume);
  if (traffic) traffic->targetCount = static_cast<int>(std::lround(s.traffic * 34));
  if (rail) {
    rail->target = s.traffic > 0.02 ? 4 : 0;
    rail->ringMax = std::min(620.0, a.tiles->viewRadius * 0.8);
    rail->far = std::min(900.0, a.tiles->viewRadius * 1.1);
  }
}

// ------------------------------------------------- class CE: truck and trailer

/** The vehicle the player sits in: the car or the truck (0.7.0). */
Group& Game::modelRoot() { return isTruck && truck ? truck->root : car.root; }

bool Game::modelVisible() const {
  if (isTruck && truck) return truck->root.visible;
  return car.root.visible;
}

void Game::showModel(bool on) {
  car.root.visible = on && !isTruck;
  if (truck) {
    truck->root.visible = on && isTruck;
    truck->trailer.visible = on && isTruck;
  }
}

/** A place on a road wide enough to start with a combination of 16 m, near a start point. */
SpawnJson Game::truckSpot(const SpawnJson& nearSpot) {
  if (!mainLanes_) mainLanes_ = std::make_unique<std::set<int>>(mainNetwork(*app.world, *lanes));
  auto wide = [](const Edge& e) {
    return (e.cls == PRIMARY || e.cls == SECONDARY || e.cls == TERTIARY) &&
           e.len - e.cutA - e.cutB > 60;
  };
  SpawnJson spot = findRoadSpot(*app.world, *lanes, nearSpot.x, nearSpot.z, "", 900, mainLanes_.get(),
                                wide, 20);
  if (spot.name.empty() && spot.kind.empty() && spot.x == 0 && spot.z == 0) return nearSpot;
  spot.name = nearSpot.name;
  return spot;
}

/** The next task on the practice yard: lay it out and stand the combination at its start. */
void Game::nextTask() {
  if (tasksLeft_.empty()) return;
  const std::string id = tasksLeft_.front();
  tasksLeft_.erase(tasksLeft_.begin());
  task_ = std::make_unique<Task>(id);
  Pose start = yardFrame_.poseToWorld(task_->run.layout.start);
  spawn_.name.clear();
  spawn_.x = start.x;
  spawn_.z = start.z;
  spawn_.yaw = start.yaw;
  spawn_.kind = "street";
  hasSpawn_ = true;
  rules->yard = true;
  // the yard is asphalt while the lesson runs, whatever grows on the field
  const TaskLayout& layout = task_->run.layout;
  ground->paved = [this, layout](double x, double z) {
    auto uw = yardFrame_.toLocal(x, z);
    return offArea(layout, uw[0], uw[1], 1.2) == 0;
  };
  taskSub_.clear();
  lessonLine_.clear();
  // reversing is watched from high above, coupling from close by
  input->c.lookZoom = id == "couple" ? 17 : 34;
}

void Game::endTasks() {
  if (task_ && input) input->c.lookZoom = 21;
  task_.reset();
  tasksLeft_.clear();
  if (yardScene) yardScene->hide();
  if (ground) ground->paved = nullptr;
  if (rules) rules->yard = false;
  if (hud) hud->setChoices({}, [](const std::string&) {});
}

/** Put the vehicle somewhere else in the middle of a lesson (the next task, the road after the yard). */
void Game::placeAgain() {
  traffic->clear();
  if (rail) rail->clear();
  vehicle->vel[0] = vehicle->vel[1] = vehicle->vel[2] = 0;
  vehicle->angVel[0] = vehicle->angVel[1] = vehicle->angVel[2] = 0;
  input->c.handbrake = 1;
  if (path_) path_->breakLine();
  keepClock_ = true;
  state = GameState::Placing;
  showModel(false);
  hud->showLoading(t("loadingTiles"));
  // ?demo=x,z;x,z: a demonstration drive to these places of the map
  if (!autoDemo_.empty() && !demoRunAuto_) {
    demoRunAuto_ = true;
    std::vector<SpawnJson> stops;
    std::stringstream ss(autoDemo_);
    std::string one;
    while (std::getline(ss, one, ';')) {
      size_t comma = one.find(',');
      if (comma == std::string::npos) continue;
      SpawnJson sp;
      sp.x = std::atof(one.substr(0, comma).c_str());
      sp.z = std::atof(one.substr(comma + 1).c_str());
      sp.kind = "street";
      stops.push_back(sp);
    }
    if (!stops.empty()) startDemo(stops);
  }
  // the menu's demonstration card: plan the drive as soon as the car stands
  if (planDemo_ && state == GameState::Drive) {
    planDemo_ = false;
    openBigMap();
  }
}

void Game::taskDone(bool ok) {
  audio.click(ClickKind::Chime);
  hud->setChoices({}, [](const std::string&) {});
  if (!ok) {
    taskFailed_ = true;
    finish();
    return;
  }
  if (!tasksLeft_.empty()) {
    nextTask();
    placeAgain();
    return;
  }
  endTasks();
  if (hasLesson_ && !lesson_.targets.empty() && hasRoadSpawn_) {
    // the tasks are done: now the drive in traffic
    spawn_ = truckSpot(roadSpawn_);
    placeAgain();
    return;
  }
  finish();
}

/** Judge the task on the yard and say what is to be done. */
void Game::updateTask(double dt) {
  if (!task_ || !trailer) return;
  Task& task = *task_;
  YardTask& run = task.run;
  const std::string& lang = settings.lang;
  bool handbrake = input->c.handbrake > 0.5;
  // (ground that was not loaded when the yard was laid out: once more, now and then)
  if (!task.laid && std::fabs(time_ - laidTry_) > 0.7) {
    laidTry_ = time_;
    task.laid = yardScene && yardScene->show(
                                 yardFrame_, run.layout,
                                 [this](double x, double z) { return app.tiles->heightAt(x, z); });
  }
  if (run.id != "couple") {
    ManoeuvreStatus st = run.manoeuvre(dt, yardFrame_, *vehicle, *trailer, handbrake);
    // a basic manoeuvre that went wrong may be repeated once – as in the test
    for (const std::string& f : st.faults)
      rules->note(f, (f == "ceLine" || !task.second) ? Severity::Major : Severity::Fail);
    lessonLine_ = fs::tr("ce.do." + run.id, lang);
    taskSub_ = fs::tr("ce.corrections", lang,
                      {{"n", std::to_string(st.corrections)},
                       {"max", std::to_string(MAX_CORRECTIONS)}});
    if (!std::isnan(st.gap) && st.gap < 15)
      taskSub_ += " · " +
                  fs::tr("ce.gap", lang,
                         {{"d", formatUnit(std::max(0.0, st.gap), "meter", lang, 1)}});
    if (st.state == "failed" && !task.second) {
      tasksLeft_.insert(tasksLeft_.begin(), run.id);
      nextTask();
      if (task_) task_->second = true;
      placeAgain();
      hud->message(fs::tr("ce.again", lang));
    } else if (st.state != "run") {
      taskDone(st.state == "done");
    }
    return;
  }
  CouplingStatus st = run.couple(dt, *vehicle, *trailer, handbrake);
  if (run.bumped) rules->note("ceFast", Severity::Major);
  for (const std::string& f : st.faults) rules->note(f, Severity::Major);
  if (st.couple) audio.click(ClickKind::Chime);
  // what can be done on foot now – in an order of its own each time, so that the
  // list does not give the order away
  std::vector<std::string> choices;
  if (st.state == "run" && run.coupling.onFoot(run.state)) choices = run.coupling.choices();
  for (const std::string& id : choices)
    if (!task.order.count(id)) task.order[id] = (*task.rng)();
  std::stable_sort(choices.begin(), choices.end(), [&task](const std::string& a, const std::string& b) {
    return task.order[a] < task.order[b];
  });
  std::vector<std::pair<std::string, std::string>> items;
  for (const std::string& id : choices) items.push_back({id, fs::tr("ce.step." + id, lang)});
  hud->setChoices(items, [this](const std::string& id) {
    if (!task_ || !trailer) return;
    CouplingProcedure& proc = task_->run.coupling;
    std::string fault = proc.act(id, task_->run.state);
    if (!fault.empty()) rules->note(fault, Severity::Major, {}, 0.5);
    else audio.click(ClickKind::Chime);
  });
  lessonLine_ = !st.todo.empty() ? fs::tr("ce.do." + st.todo, lang)
               : !choices.empty() ? fs::tr("ce.onFoot", lang)
                                   : fs::tr("ce.do.uPark", lang);
  taskSub_ = std::to_string(task.run.coupling.done.size()) + " / " +
             std::to_string(COUPLING_STEPS.size());
  if (st.state == "done") taskDone(true);
}

void Game::toMenu() {
  state = GameState::Menu;
  demo_.reset();
  endTasks();
  showModel(false);
  if (input) input->enabled = false;
  hud->setHudVisible(false);
  audio.suspend();
  hud->showMenu(worldJson_.spawns, 0);
  if (!hasSpawn_ && !worldJson_.spawns.empty()) {
    // show the town behind the menu
    const SpawnJson& sp = worldJson_.spawns[0];
    free_.pos.set(sp.x, 0, sp.z);
  }
}

void Game::begin(const std::string& mode, int spawnIdx, bool skipIntro,
                 const AddressHit* address) {
  (void)skipIntro;   // the intro overlay is shown by tryPlace() when it is set
  audio.start();
  // start at this address instead of one of the car parks (0.5.0)
  SpawnJson at;
  bool placed = address && spawnAt(*address, at);
  if (placed) {
    spawn_ = at;
    spawn_.name = address->label;
    hasSpawn_ = true;
  } else {
    const auto& spawns = worldJson_.spawns;
    if (!spawns.empty()) {
      int i = std::max(0, std::min(static_cast<int>(spawns.size()) - 1, spawnIdx));
      spawn_ = spawns[static_cast<size_t>(i)];
      hasSpawn_ = true;
    } else {
      spawn_ = SpawnJson();
      hasSpawn_ = true;
    }
  }
  hasLesson_ = false;
  for (const auto& l : lessons())
    if (l.id == mode) {
      lesson_ = l;
      hasLesson_ = true;
      break;
    }
  hud->canTeleport = !hasLesson_;
  // a free drive (and every mode but a lesson) can be ended by the driver
  hud->canFinish = !hasLesson_;
  demo_.reset();
  planDemo_ = mode == "demo";
  keepClock_ = false;
  path_->breakLine();
  // the camera a drive starts with: behind the car, or at the driver's eyes
  cameraMode = settings.view == View::Cockpit ? CameraMode::Cockpit : CameraMode::Chase;
  input->resetLook(cameraMode == CameraMode::Chase);
  // the car, or the truck with its trailer (class CE)
  isTruck = hasLesson_ && !lesson_.vehicle.empty() ? lesson_.vehicle == "truck"
                                                   : settings.vehicle == VehicleKind::Truck;
  const VehicleSpec& base = settings.transmission == Transmission::Automatic
                                ? compactCarAutomatic()
                                : (settings.transmission == Transmission::Assist
                                       ? compactCarAutoclutch()
                                       : compactCar());
  const VehicleSpec& spec = isTruck ? TRUCK : withPower(base, settings.power);
  vehicle = std::make_unique<Vehicle>(spec, *ground);
  input->setSteering(spec.wheelBase, spec.maxWheelAngle);
  input->setAutomatic(spec.transmission == "automatic");
  if (isTruck && !truck) {
    truck = std::make_unique<PlayerTruck>();
    app.scene.addRef(&truck->root);
    app.scene.addRef(&truck->trailer);
    truck->setCabinOpacity(settings.cabinOpacity);
  }
  trailer = isTruck ? std::make_unique<Trailer>(TANDEM_TRAILER, *ground) : nullptr;
  hud->gearboxAutomatic = isTruck;
  endTasks();
  // the camera stands further back for 16 m of vehicle
  input->c.lookZoom = isTruck ? 21 : 7;
  input->zoomMax = isTruck ? 45 : 18;
  rules->truck = isTruck;
  // the scenery collision follows the vehicle: three circles for the car, four for the truck
  collider.setShape(isTruck ? TRUCK_CIRCLES : CAR_CIRCLES, isTruck ? 4 : 3,
                    isTruck ? 1.25 : 0.93);
  taskFailed_ = false;
  roadSpawn_ = spawn_;
  hasRoadSpawn_ = hasSpawn_;
  tasksLeft_.clear();
  if (isTruck && hasYard_ && hasLesson_ && !lesson_.tasks.empty())
    tasksLeft_ = lesson_.tasks;
  taskCount_ = static_cast<int>(tasksLeft_.size());
  if (!tasksLeft_.empty()) nextTask();
  else if (isTruck && !address) spawn_ = truckSpot(spawn_);
  Controls& c = input->c;
  c.handbrake = 1;
  c.ignition = true;
  c.indicator = 0;
  c.hazard = false;
  // the basics lesson starts with a cold car; everything else is ready to go
  bool cold = hasLesson_ && lesson_.kind == "basics";
  c.seatbelt = !cold;
  vehicle->resetDrivetrain(!cold);
  rules->reset();
  hud->clearNotices();
  rules->strict = hasLesson_ && lesson_.kind == "exam";
  traffic->clear();
  if (rail) rail->clear();
  nav->clear();
  targetIdx_ = 0;
  basicsStep_ = 0;
  visited_.clear();
  failT_ = -1;
  homeEdge_ = nullptr;
  state = GameState::Placing;
  showModel(false);
  hud->showLoading(t("loadingTiles"));
}

void Game::tryPlace() {
  const SpawnJson& sp = spawn_;
  double ty = app.tiles->heightAt(sp.x, sp.z);
  if (std::isnan(ty) || !app.tiles->settled()) return;
  double y = world_->surfaceAt(sp.x, sp.z, ty + 3, hit_) ? hit_.y : ty;
  // the practice yard is laid out over the field as soon as the ground under it is there
  if (task_ && !task_->laid && yardScene)
    task_->laid = yardScene->show(
        yardFrame_, task_->run.layout,
        [this](double x, double z) { return app.tiles->heightAt(x, z); });
  vehicle->teleport(sp.x, y + 0.02, sp.z, sp.yaw);
  if (trailer) trailer->coupleBehind(*vehicle);
  lastGood_ = {sp.x, y, sp.z, sp.yaw, 0};
  camSnap_ = true;
  showModel(true);
  if (!keepClock_) {
    startOdo_ = vehicle->odometer;
    startTime_ = time_;
  }
  keepClock_ = false;
  resume();
  // ?demo=x,z;x,z: a demonstration drive to these places of the map
  if (!autoDemo_.empty() && !demoRunAuto_) {
    demoRunAuto_ = true;
    std::vector<SpawnJson> stops;
    std::stringstream ss(autoDemo_);
    std::string one;
    while (std::getline(ss, one, ';')) {
      size_t comma = one.find(',');
      if (comma == std::string::npos) continue;
      SpawnJson sp;
      sp.x = std::atof(one.substr(0, comma).c_str());
      sp.z = std::atof(one.substr(comma + 1).c_str());
      sp.kind = "street";
      stops.push_back(sp);
    }
    if (!stops.empty()) startDemo(stops);
  }
  // the menu's demonstration card: plan the drive as soon as the car stands
  if (planDemo_ && state == GameState::Drive) {
    planDemo_ = false;
    openBigMap();
  }
  // (a drive planned from the map stands for a moment first, so that the
  // camera and the physics have settled)
  if (autoRunAt_ >= 0 && time_ >= autoRunAt_) {
    autoRunAt_ = -1;
    if (demoPending_.size()) {
      std::vector<SpawnJson> stops;
      stops.swap(demoPending_);
      startDemo(stops);
    }
  }
}

void Game::resume() {
  hud->hideOverlay();
  hud->setHudVisible(true);
  input->enabled = true;
  audio.start();
  state = GameState::Drive;
}

void Game::pause() {
  if (state != GameState::Drive) return;
  state = GameState::Pause;
  input->enabled = false;
  audio.suspend();
  if (path_) path_->save();
  hud->showPause();
}

void Game::finish() {
  if (path_) path_->save();
  demo_.reset();   // (the original: this.demo = null – no drive runs over the result)
  bool exam = hasLesson_ && lesson_.kind == "exam";
  bool failed = rules->failed() || rules->points() >= 10 || taskFailed_;
  bool judged = taskCount_ > 0;
  endTasks();
  state = GameState::Result;
  input->enabled = false;
  audio.suspend();
  hud->setHudVisible(false);
  ResultSummary r;
  r.title = hasLesson_ ? lessonTitle(lesson_.id, settings.lang) : t("free");
  r.exam = exam || judged;
  r.passed = !failed;
  r.log = rules->log;
  r.points = rules->points();
  r.distanceKm = (vehicle->odometer - startOdo_) / 1000;
  r.minutes = (time_ - startTime_) / 60;
  hud->showResult(r);
  rememberDrive(r);
}

/**
 * The faults of the drive that just ended join the log (they keep the street
 * and the lesson they happened in – the original: the entries carry mode).
 */
void Game::rememberDrive(const ResultSummary& r) {
  if (!faultLog_) return;
  for (const Violation& v : r.log) {
    FaultEntry f;
    f.t = std::chrono::duration<double, std::milli>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
    f.id = v.id;
    f.severity = v.severity;
    f.data = v.data;
    f.street = rules->context.roadName;
    f.mode = hasLesson_ ? lesson_.id : "free";
    f.x = v.x;
    f.z = v.z;
    faultLog_->add(f);
  }
}

void Game::openBigMap() {
  if (state != GameState::Drive && state != GameState::Pause) return;
  if (!bigMap) return;
  mapFrom_ = state == GameState::Pause ? "pause" : "drive";
  bigMap->open(false);
  state = GameState::Map;
  hud->setHudVisible(false);
  if (hud->onMapOpened) hud->onMapOpened();
}

void Game::closeBigMap() {
  if (state != GameState::Map) return;
  if (bigMap) bigMap->close();
  if (mapFrom_ == "pause") {
    state = GameState::Pause;
    hud->setHudVisible(true);
    hud->showPause();
  } else {
    resume();
  }
}

bool Game::key(const std::string& code, bool down) {
  if (state == GameState::Map && bigMap && bigMap->isOpen()) {
    if (down) bigMap->key(code);
    return true;   // the map has the keyboard
  }
  if (input) input->onKey(code, down, false);
  return false;
}

void Game::onAction(Action a) {
  if (a == Action::Pause) {
    if (state == GameState::Drive) pause();
    else if (state == GameState::Pause) resume();
  } else if (a == Action::Pause && state == GameState::Map) {
    closeBigMap();
  } else if (a == Action::View && state == GameState::Drive) {
    cameraMode = cameraMode == CameraMode::Cockpit ? CameraMode::Chase : CameraMode::Cockpit;
    input->resetLook(cameraMode == CameraMode::Chase);
    // the next drive begins with the camera the player chose
    settings.view = cameraMode == CameraMode::Cockpit ? View::Cockpit : View::Chase;
    saveSettings(settings);
  } else if (a == Action::Reset && state == GameState::Drive) {
    resetToRoad();
  } else if (a == Action::Help) {
    if (state == GameState::Drive) {
      state = GameState::Pause;
      input->enabled = false;
      hud->showControls([this]() { resume(); });
    }
  } else if (a == Action::Map) {
    if (state == GameState::Map) closeBigMap();
    else openBigMap();
  }
}

// -------------------------------------------------- demonstration drives (0.7.0)

void Game::startDemo(const std::vector<SpawnJson>& stops) {
  if (hasLesson_ || stops.empty() ||
      (state != GameState::Map && state != GameState::Drive && state != GameState::Pause))
    return;
  if (state == GameState::Map) {
    // the plan closes first; the drive starts a moment later, when the car stands again
    demoPending_ = stops;
    autoRunAt_ = -1;
    closeBigMap();
    placeAgain();
    return;
  } else if (state == GameState::Pause) {
    resume();
  }
  // the driver has no hand for a gear lever: the car gets the automatic gearbox for it
  if (vehicle && vehicle->spec.transmission != "automatic") {
    Vehicle& old = *vehicle;
    old.getModelPosition(mpos_);
    const VehicleSpec& spec = withPower(compactCarAutomatic(), settings.power);
    auto fresh = std::make_unique<Vehicle>(spec, *ground);
    fresh->teleport(mpos_[0], mpos_[1] + 0.02, mpos_[2], old.yaw);
    fresh->odometer = old.odometer;
    vehicle = std::move(fresh);
    input->setSteering(spec.wheelBase, spec.maxWheelAngle);
    input->setAutomatic(true);
    hud->gearboxAutomatic = true;
  }
  Vehicle& v = *vehicle;
  v.resetDrivetrain(true);
  Controls& c = input->c;
  c.seatbelt = true;
  c.ignition = true;
  c.handbrake = 0;
  c.gear = 1;
  c.indicator = 0;
  c.hazard = false;
  nav->clear();
  AutoDriverOptions o;
  o.spec = &v.spec;
  o.front = isTruck ? TRUCK_OUTLINE.front
                    : (v.spec.length > 0 ? v.spec.length / 2 : 4.28 / 2);
  o.truck = isTruck;
  demo_ = std::make_unique<Demo>();
  demo_->stops = stops;
  demo_->driver = std::make_unique<AutoDriver>(*app.world, *signals, *rules, traffic.get(), o);
  hud->clearNotices();
}

void Game::endDemo(const std::string& why) {
  if (!demo_) return;
  demo_.reset();
  nav->clear();
  hud->routeLanes.clear();
  rules->allowed = nullptr;
  Controls& c = input->c;
  c.indicator = 0;
  const std::string& lang = settings.lang;
  if (why == "arrived") {
    c.handbrake = 1;
    audio.click(ClickKind::Chime);
    hud->message(fs::tr("demoArrived", lang));
  } else if (why == "lost") {
    c.handbrake = 1;
    hud->message(fs::tr("demoNoRoute", lang));
  } else {
    hud->message(fs::tr("demoTaken", lang), fs::tr("demoTakenSub", lang));
  }
}

/** Where the demonstration drive is heading: the route to the next of its places. */
void Game::updateDemo(double dt, const PlayerState& p) {
  Demo& demo = *demo_;
  Lane* lane = currentLane();
  if (!lane) {
    // not on a road of the network (a yard, a field): there is no way from here
    demo.lost += dt;
    if (demo.lost > 4 && !nav->goal) endDemo("lost");
    return;
  }
  bool last = demo.index >= static_cast<int>(demo.stops.size()) - 1;
  if (!nav->goal) {
    const SpawnJson& stop = demo.stops[static_cast<size_t>(demo.index)];
    SurfaceHit h;
    Edge* goal = app.world->nearestRoad(stop.x, stop.z, 60, 0, h) ? h.edge : nullptr;
    if (!goal || !nav->setGoal(goal, lane)) {
      // no way there from here (yet): try again shortly, then give up
      nav->clear();
      demo.lost += dt;
      if (demo.lost > 3) endDemo("lost");
      return;
    }
    demo.lost = 0;
    demo.driver->hasHalt = last;
    if (last) {
      demo.driver->haltX = stop.x;
      demo.driver->haltZ = stop.z;
    }
    hud->routeLanes = nav->routeLanes;
  }
  const std::string& lang = settings.lang;
  NavHint& h = nav->update(dt, lane, laneFront(), p.x, p.z);
  if (hud->routeLanes != nav->routeLanes) {
    hud->routeLanes = nav->routeLanes;
    allowedLanes_.clear();
    for (Lane* l : nav->routeLanes) allowedLanes_.insert(l->edge);
    rules->allowed = &allowedLanes_;
  }
  if (h.done && !last) {
    demo.index++;
    nav->clear();
    audio.click(ClickKind::Chime);
    hud->message(fs::tr("demoStop", lang,
                        {{"n", std::to_string(demo.index)}}));
  }
}

void Game::resetToRoad() {
  const GoodSpot& g = lastGood_;
  vehicle->teleport(g.x, g.y + 0.05, g.z, g.yaw);
  if (trailer && trailer->coupled) trailer->coupleBehind(*vehicle);
  if (demo_ && demo_->driver) demo_->driver->reset();
  input->c.handbrake = 1;
}

Lane* Game::currentLane() { return rules ? rules->currentLane() : nullptr; }

double Game::laneFront() { return rules ? rules->currentLaneS() : 0; }

void Game::rememberGoodSpot(const PlayerState& p) {
  const RuleContext& ctx = rules->context;
  Lane* lane = currentLane();
  if (ctx.onRoad && lane && std::fabs(p.speed) < 40 && !ctx.wrongWay &&
      time_ - lastGood_.t > 1.5) {
    lastGood_ = {p.x, p.y, p.z, p.yaw, time_};
  }
  // fell off the world or wandered beyond the map: back to the last good place
  if (std::isnan(app.tiles->heightAt(p.x, p.z)) || p.y < lastGood_.y - 60) resetToRoad();
}

void Game::stepDrive(double dt) {
  Vehicle& v = *vehicle;
  Controls c = input->update(dt, v.speed, v.steerTorque);

  DriverInput& d = driver_;
  d.steer = c.steer;
  d.throttle = c.throttle;
  d.brake = c.brake;
  d.clutch = c.clutch;
  d.handbrake = c.handbrake;
  d.gear = c.gear;
  d.ignition = c.ignition;
  d.starter = c.starter;
  // Speed limiter: a key or a pedal on the screen is all or nothing, which makes it hard to hold
  // 30 or 50 km/h.  With the limiter the throttle eases off just below the current limit – the
  // driver still has to brake when the limit drops.
  const int lim = rules ? rules->context.limit : -1;
  if (settings.limiter && lim > 0 && c.gear != -1 &&
      (c.device == "keyboard" || c.device == "touch"))
    d.throttle = std::min(d.throttle, std::max(0.0, (lim - 0.5 - v.speed * 3.6) / 4));
  // a truck is governed at 90 km/h
  if (isTruck && v.speed * 3.6 > 85)
    d.throttle = std::min(d.throttle, std::max(0.0, (89.5 - v.speed * 3.6) / 4));
  if (demo_ && demo_->active) {
    // Whoever touches a pedal or the steering takes the car over (held for a
    // moment: a wheel that is not quite centred, or a thumb resting on the
    // screen, is no decision).
    demo_->takeT = (c.throttle > 0.2 || c.brake > 0.2 || std::fabs(c.steer) > 0.35 ||
                    c.handbrake > 0.5)
                       ? demo_->takeT + dt
                       : 0;
    if (demo_->takeT > 0.18) {
      endDemo("taken");
    } else {
      v.getModelPosition(mpos_);
      AutoCommand cmd = demo_->driver->step(std::max(dt, 1e-3), v, mpos_, nav->route);
      // (until the way is known the car stands: no lane under it yet, or no route found yet)
      bool ready = nav->goal != nullptr;
      d.steer = ready ? cmd.steer : 0;
      d.throttle = ready ? cmd.throttle : 0;
      d.brake = ready ? cmd.brake : 0.5;
      d.clutch = 0;
      d.handbrake = 0;
      d.gear = 1;
      d.ignition = true;
      d.starter = !v.engineRunning;
      c.gear = 1;
      c.indicator = static_cast<int>(cmd.indicator);
      demo_->look = cmd.look;
      demo_->remark = cmd.remark;
      if (cmd.arrived) {
        d.brake = 1;
        endDemo("arrived");
      }
    }
  }
  if (autoDrive_ > 0) {
    // test hook: drive off by itself
    d.ignition = true;
    d.handbrake = 0;
    c.handbrake = 0;
    d.gear = 1;
    c.gear = 1;
    d.throttle = autoDrive_;
  }

  acc_ = std::min(acc_ + dt, 0.1);
  bool stalled = false, grind = false;
  while (acc_ >= PHYS_DT) {
    v.step(PHYS_DT, d);
    if (trailer) trailer->step(PHYS_DT, v, d);
    acc_ -= PHYS_DT;
    if (v.stalledEvent) stalled = true;
    if (v.gearGrind) grind = true;
    double hit = collider.resolve(v);
    if (hit > impact_) impact_ = hit;
  }
  if (path_) path_->add(v.pos[0], v.pos[2]);
  // a refused gear change: crunch, then the lever springs back
  if (grind) {
    if (grindT_ <= 0) audio.click(ClickKind::Grind);
    grindT_ += dt;
    if (grindT_ > 0.35) {
      c.gear = v.gear;
      grindT_ = 0;
    }
    tipText_ = t("pressClutch");
    tipUntil_ = time_ + 2.5;
  } else {
    grindT_ = 0;
  }

  v.getModelPosition(mpos_);
  // (the traffic wants the middle of the vehicle's length; a truck is measured
  // from the middle of its wheelbase)
  double mid = isTruck ? (TRUCK_OUTLINE.rear - TRUCK_OUTLINE.front) / 2 : 0;
  PlayerState player;
  player.x = mpos_[0] + std::sin(v.yaw) * mid;
  player.y = mpos_[1];
  player.z = mpos_[2] + std::cos(v.yaw) * mid;
  player.yaw = v.yaw;
  player.speed = v.speed;
  player.length = v.spec.length > 0 ? v.spec.length : 4.28;
  player.width = v.spec.width > 0 ? v.spec.width : 1.79;
  signals->update(dt);
  // trams and trains: they keep clear of the cars, the cars of them
  if (rail) {
    railOthers_.clear();
    for (const TrafficCar& car : traffic->cars) railOthers_.push_back({car.x, 0, car.z});
    RailPlayer rp;
    rp.x = player.x;
    rp.y = player.y;
    rp.z = player.z;
    rp.yaw = player.yaw;
    rp.length = player.length;
    rp.width = player.width;
    rail->update(dt, rp, railOthers_);
    // their cars are in the way of the traffic like any other vehicle …
    railBoxes_.clear();
    for (const RailVehicle& rv : rail->vehicles) {
      for (const RailCar& rc : rv.cars) {
        PlayerState box;
        box.x = rc.x;
        box.y = rc.y;
        box.z = rc.z;
        box.yaw = rc.yaw;
        box.speed = rv.speed;
        box.length = rv.consist->carLen;
        box.width = rv.consist->width;
        railBoxes_.push_back(box);
      }
    }
  }
  traffic->obstacles = railBoxes_;
  // … and the trailer joins them
  if (trailer) {
    const double off = (TRAILER_OUTLINE.front - TRAILER_OUTLINE.rear) / 2;
    PlayerState t;
    t.x = trailer->x - std::sin(trailer->yaw) * off;
    t.y = trailer->y;
    t.z = trailer->z - std::cos(trailer->yaw) * off;
    t.yaw = trailer->yaw;
    t.speed = trailer->coupled ? trailer->speed : 0;
    t.length = TRAILER_OUTLINE.front + TRAILER_OUTLINE.rear;
    t.width = TRAILER_OUTLINE.hw * 2;
    traffic->obstacles.push_back(t);
  }
  std::vector<TrafficEvent> events;
  traffic->update(dt, player, events);
  for (const auto& ev : events) {
    if (auto* col = std::get_if<CollisionEvent>(&ev)) {
      // shove the cars apart and kill most of the closing speed
      double dx = player.x - col->car->x;
      double dz = player.z - col->car->z;
      double l = std::hypot(dx, dz);
      if (l == 0) l = 1;
      v.translate((dx / l) * 0.25, 0, (dz / l) * 0.25);
      double vn = v.vel[0] * (dx / l) + v.vel[2] * (dz / l);
      if (vn < 0)
        v.applyImpulse(v.pos[0], v.pos[1], v.pos[2], -(dx / l) * vn * v.spec.mass * 1.2, 0,
                       -(dz / l) * vn * v.spec.mass * 1.2);
      audio.click(ClickKind::Crash);
    }
  }
  // run into a tram (or hit by one): pushed clear – it does not give way
  if (rail) {
    RailPlayer rp;
    rp.x = player.x;
    rp.y = player.y;
    rp.z = player.z;
    rp.yaw = player.yaw;
    rp.length = player.length;
    rp.width = player.width;
    if (const RailTraffic::Touch* tram = rail->touching(rp)) {
      v.translate(tram->nx * std::min(0.4, tram->depth + 0.02), 0,
                  tram->nz * std::min(0.4, tram->depth + 0.02));
      double closing = (v.vel[0] - tram->vx) * tram->nx + (v.vel[2] - tram->vz) * tram->nz;
      if (closing < 0) {
        v.applyImpulse(v.pos[0], v.pos[1], v.pos[2], -tram->nx * closing * v.spec.mass * 1.15, 0,
                       -tram->nz * closing * v.spec.mass * 1.15);
        impact_ = std::max(impact_, -closing);
      }
    }
  }
  // on its side or roof: that is the end of this drive – count it as a crash
  // and start again on the road
  if (v.upright < 0.35) {
    rolledT_ += dt;
    if (rolledT_ > 2.2 && std::fabs(v.speed) < 5) {
      rolledT_ = 0;
      impact_ = std::max(impact_, 12.0);
      resetToRoad();
      tipText_ = t("rolledOver");
      tipUntil_ = time_ + 6;
    }
  } else {
    rolledT_ = 0;
  }
  double impact = impact_;
  if (impact > 1.2) audio.click(impact > 4 ? ClickKind::Crash : ClickKind::Bump);
  impact_ = 0;

  PlayerSnapshot snap;
  snap.x = player.x;
  snap.y = player.y;
  snap.z = player.z;
  snap.yaw = v.yaw;
  snap.speed = v.speed;
  snap.indicator = c.indicator;
  snap.hazard = c.hazard;
  // (on a demonstration drive it is the driver who looks over the shoulder)
  snap.lookYaw = (demo_ && demo_->active) ? demo_->look : c.lookYaw;
  snap.seatbelt = c.seatbelt;
  snap.handbrake = c.handbrake;
  snap.stalled = stalled;
  snap.hitScenery = impact;
  snap.length = v.spec.length > 0 ? v.spec.length : 4.28;
  rules->update(dt, snap, events);
  rememberGoodSpot(player);
  if (task_) updateTask(dt);
  else if (demo_) updateDemo(dt, player);
  else updateLesson(dt, player);
  if (failT_ > 0 && time_ > failT_) finish();
}

void Game::updateLesson(double dt, const PlayerState& p) {
  if (!hasLesson_) return;
  const LessonDef& l = lesson_;
  Lane* lane = currentLane();
  Vehicle* v = vehicle.get();
  if (l.kind == "basics") {
    Controls& c = input->c;
    bool auto_ = v->spec.transmission == "automatic";
    struct Step {
      const char* key;
      std::function<bool()> done;
    };
    // the texts are the basics.* ones, in the reader's language (0.6.0)
    std::vector<Step> steps = {
        {"basics.belt", [&]() { return c.seatbelt; }},
        {auto_ ? "basics.startAuto" : "basics.startManual",
         [&]() { return v->engineRunning; }},
        {auto_ ? "basics.gearAuto" : "basics.gearManual",
         [&]() { return auto_ ? c.gear == 1 : v->gear == 1; }},
        {"basics.handbrake", [&]() { return c.handbrake < 0.5; }},
        {"basics.pullAway", [&]() { return v->speed * 3.6 > 15; }},
        {auto_ ? "basics.accelAuto" : "basics.accelManual",
         [&]() { return v->speed * 3.6 > 30 && (auto_ || v->gear >= 2); }},
        {"basics.brake", [&]() { return std::fabs(v->speed) < 0.1; }},
        {"basics.reverse", [&]() { return v->speed < -1; }},
        {"basics.stop",
         [&]() { return std::fabs(v->speed) < 0.1 && c.handbrake > 0.5; }},
    };
    if (basicsStep_ < static_cast<int>(steps.size()) &&
        steps[static_cast<size_t>(basicsStep_)].done()) {
      basicsStep_++;
      audio.click(ClickKind::Chime);
    }
    if (basicsStep_ >= static_cast<int>(steps.size())) {
      finish();
      return;
    }
    lessonLine_ = std::to_string(basicsStep_ + 1) + "/" + std::to_string(steps.size()) +
                  " \u00b7 " + fs::tr(steps[static_cast<size_t>(basicsStep_)].key, settings.lang);
    return;
  }

  // routed lessons and the exam
  if (!lane) return;
  if (!homeEdge_) homeEdge_ = lane->edge;
  if (!nav->goal) {
    TargetType type = l.targets[static_cast<size_t>(targetIdx_)];
    Edge* goal = findTarget(*world_, *lanes, type, p.x, p.z, *lane, homeEdge_, visited_);
    if (!goal) {
      // nothing suitable in reach: skip this part
      targetIdx_++;
      if (targetIdx_ >= static_cast<int>(l.targets.size())) finish();
      return;
    }
    visited_.insert(goal);
    nav->setGoal(goal, lane);
    hud->routeLanes = nav->routeLanes;
  }
  NavHint& h = nav->update(dt, lane, laneFront(), p.x, p.z);
  if (h.done) {
    targetIdx_++;
    nav->clear();
    audio.click(ClickKind::Chime);
    if (targetIdx_ >= static_cast<int>(l.targets.size())) finish();
  }
  lessonLine_ = lessonTitle(l.id, settings.lang) + " \u00b7 " +
                std::to_string(targetIdx_ + 1) + "/" + std::to_string(l.targets.size());
}

void Game::boot(const QueryParams& q) {
  App& a = app;
  // ?lang=xx chooses the language for this visit
  const std::string ql = q.get("lang");
  if (!ql.empty()) {
    bool known = false;
    for (const LangInfo& l : LANGS)
      if (l.code == ql) known = true;
    if (known) settings.lang = ql;
  }
  // the texts of the chosen language (built in or registered now; the
  // original fetches the file) – unknown or refused: English
  if (!loadLang(settings.lang)) settings.lang = "en";
  // the world JSON comes from the platform (the original fetches world.json)
  {
    std::string raw;
    if (platform_.worldJson(raw) && parseWorldJson(raw, worldJson_)) {}
  }
  a.loadWorld(worldJson_, tileSource_);
  lanes = std::make_unique<LaneGraph>(*a.world);
  signals = std::make_unique<SignalSystem>(a.world->signals());
  traffic = std::make_unique<Traffic>(*a.world, *lanes, *signals);
  rules = std::make_unique<RuleMonitor>(*a.world, *lanes, *signals, *traffic);
  nav = std::make_unique<Navigator>(*lanes);
  ground = std::make_unique<WorldGround>(*a.world, *a.tiles);
  signs = std::make_unique<SignRenderer>("signs/", signSource_);
  signs->init();
  signalHeads = std::make_unique<SignalRenderer>(*a.world);
  CollisionSource sigSrc;
  sigSrc.key = "signals";
  sigSrc.discs = signalHeads->poles.data();
  sigSrc.discCount = static_cast<int>(signalHeads->poles.size() / 4);
  collider.add(sigSrc);
  a.tiles->onTile([this](LoadedTile& t, bool l) { onTile(t, l); });
  a.scene.addRef(&car.root);
  a.scene.addRef(&actors.group);
  a.scene.addRef(&railActors.group);
  a.scene.addRef(&signalHeads->group);
  // trams and trains on the tracks around the map (0.7.0)
  rail = std::make_unique<RailTraffic>(worldJson_.rails);
  // the practice yard of the truck lessons, laid over the map's open field
  yardScene = std::make_unique<YardScene>();
  a.scene.addRef(&yardScene->group);
  hasYard_ = worldJson_.hasYard;
  if (hasYard_) yardFrame_ = YardFrame(worldJson_.yard);
  car.root.visible = false;

  hud = std::make_unique<Hud>(host_, settings, *a.world);
  // the full-screen map (0.7.0): everything it asks of the game
  {
    BigMapHost bh;
    bh.world = a.world.get();
    bh.lang = [this]() { return settings.lang; };
    bh.car = [this](double& x, double& z, double& yaw) {
      x = mpos_[0];
      z = mpos_[2];
      yaw = vehicle ? vehicle->yaw : 0;
    };
    bh.path = path_.get();
    bh.spotAt = [this](double x, double z) { return spotAt(x, z); };
    bh.canTeleport = [this]() { return !hasLesson_; };
    bh.teleport = [this](const SpawnJson& spot) { teleportSpot(spot); };
    bh.loadAddresses = [this]() { return loadAddresses(); };
    bh.spotOf = [this](const AddressHit& hit, bool& ok) {
      SpawnJson s;
      ok = spawnAt(hit, s);
      return s;
    };
    bh.startDemo = [this](const std::vector<SpawnJson>& stops) { startDemo(stops); };
    bh.latLon = [](double x, double z) { return worldToLatLon(x, z); };
    bh.aerial = [this]() { return settings.reality.aerial; };
    bh.setAerial = [this](bool on) {
      settings.reality.aerial = on;
      saveSettings(settings);
    };
    bh.close = [this]() { closeBigMap(); };
    bh.windowSize = [this](int& w, int& h) {
      w = app.renderer.width();
      h = app.renderer.height();
    };
    bh.onShow = [this](std::unique_ptr<HudNode> panel) { hud->setOverlay(std::move(panel)); };
    bh.onHide = [this]() { hud->setOverlay(nullptr); };
    bigMap = std::make_unique<BigMap>(std::move(bh));
  }
  // a finger moving over the on-screen controls reaches them (the page's host
  // reports it; the HUD knows what each widget does)
  host_.onMoved = [this](const std::string& id, double fx, double fy) {
    if (hud) hud->moved(id, fx, fy);
  };
  // the log of faults survives the session (the original: localStorage)
  faultLog_ = std::make_unique<FaultLog>(store_);
  path_ = std::make_unique<DrivenPath>(store_);
  hud->faultLog = faultLog_.get();
  input = std::make_unique<Input>(platform_, settings);
  // a touch screen starts the input in touch mode (matchMedia('(pointer: coarse)'))
  if (coarsePointer_) input->mode = "touch";
  input->onAction([this](Action act) { onAction(act); });
  hud->onStart = [this](const StartRequest& r) {
    begin(r.mode, r.spawn, false, r.hasAddress ? &r.address : nullptr);
  };
  hud->onResume = [this]() { resume(); };
  hud->onQuit = [this]() { toMenu(); };
  hud->onSettingsChanged = [this]() { applySettings(); };
  // reality view (0.4.0): real photographs of where the car is
  hud->onReality = [this]() { openReality(); };
  realityHost_ = std::make_unique<RealityHostImpl>(*this);
  reality_ = std::make_unique<RealityPanel>(host_, *realityHost_, nullptr);
  // 0.5.0: the pause menu (menu button in the HUD), the address search of the
  // start menu and jumping to an address while paused
  hud->onPause = [this]() { pause(); };
  hud->loadAddresses = [this]() { return loadAddresses(); };
  hud->onTeleport = [this](const AddressHit& hit) { teleportTo(hit); };
  hud->onMap = [this]() { openBigMap(); };
  hud->onFinish = [this]() {
    // a free drive ends when the driver says so: the result, then the menu
    if (state == GameState::Pause && !hasLesson_) finish();
  };
  // (the demonstration drive starts from the map's plan; its panel arrives
  // with ui/bigmap)
  hud->onDemo = [this]() { openBigMap(); };
  hud->attachInput(*input);
  rules->onViolation = [this](const Violation& v) {
    hud->toast(v);
    if (faultLog_) {
      FaultEntry f;
      f.t = std::chrono::duration<double, std::milli>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
      f.id = v.id;
      f.severity = v.severity;
      f.data = v.data;
      f.street = rules->context.roadName;
      f.mode = hasLesson_ ? lesson_.id : "free";
      f.x = static_cast<int>(std::lround(v.x));
      f.z = static_cast<int>(std::lround(v.z));
      faultLog_->add(f);
    }
    audio.click(ClickKind::Chime);
    if (v.severity == Severity::Fail && hasLesson_ && lesson_.kind == "exam" && failT_ < 0)
      failT_ = time_ + 2.5;
  };
  // test overrides (not saved): ?quality=|mirrors=|traffic=
  std::string qq = q.get("quality");
  if (qq == "low" || qq == "medium" || qq == "high")
    settings.quality = qq == "low" ? Quality::Low
                                   : (qq == "medium" ? Quality::Medium : Quality::High);
  std::string qm = q.get("mirrors");
  if (qm == "off" || qm == "alternate" || qm == "full")
    settings.mirrors = qm == "off" ? Mirrors::Off
                                   : (qm == "alternate" ? Mirrors::Alternate : Mirrors::Full);
  if (!q.get("traffic").empty()) settings.traffic = std::atof(q.get("traffic").c_str());
  // ?vehicle=truck|car chooses the vehicle for this visit
  const std::string qv = q.get("vehicle");
  if (qv == "truck") settings.vehicle = VehicleKind::Truck;
  else if (qv == "car") settings.vehicle = VehicleKind::Car;
  // ?demo=x,z;x,z starts a demonstration drive to these places of the map once
  // the drive has started (0.7.0)
  autoDemo_ = q.get("demo");
  // ?map=1|aerial opens the full-screen map once the drive has started ("aerial":
  // close up, with aerial photographs)
  autoMap_ = q.get("map");
  autoTram_ = q.get("tram") == "1";
  // (?touch and ?rv are read below, where the original reads them)
  // ?at=x,y[,yaw]: a test place, added in front of the others
  const std::string at = q.get("at");
  if (!at.empty()) {
    std::vector<std::string> p = split(at, ',');
    if (p.size() >= 2) {
      SpawnJson sp;
      sp.name = "Test";
      sp.x = std::atof(p[0].c_str());
      sp.z = std::atof(p[1].c_str());
      sp.yaw = p.size() > 2 ? std::atof(p[2].c_str()) * PI / 180 : 0;
      sp.kind = "street";
      worldJson_.spawns.insert(worldJson_.spawns.begin(), sp);
    }
  }
  // ?reality=1|test opens the reality view once the drive has started ("test":
  // with a drawn panorama), ?rv=panoramax,kartaview switches services on
  autoReality_ = q.get("reality");
  for (const std::string& r : {"panoramax", "kartaview", "mapillary"})
    if (q.get("rv").find(r) != std::string::npos) {
      if (r == "panoramax") settings.reality.panoramax = true;
      else if (r == "kartaview") settings.reality.kartaview = true;
      else settings.reality.mapillary = true;
    }
  // ?notice=red|yellow|both shows sample notices, ?touch=on|off|auto forces
  // the on-screen controls, ?addr=<street and number> starts at that address
  autoNotice_ = q.get("notice");
  const std::string tc = q.get("touch");
  if (tc == "on" || tc == "off" || tc == "auto") settings.touchControls = tc;
  applySettings();

  // developer / test shortcuts
  std::string view = q.get("view");
  if (view == "chase") cameraMode = CameraMode::Chase;
  else if (view == "cockpit") cameraMode = CameraMode::Cockpit;
  else if (view == "free") cameraMode = CameraMode::Free;
  else cameraMode = settings.view == View::Cockpit ? CameraMode::Cockpit : CameraMode::Chase;
  input->resetLook(cameraMode == CameraMode::Chase);
  autoDrive_ = std::atof(q.get("auto").c_str());

  std::string start = q.get("start");
  std::string addr = q.get("addr");
  if (!start.empty() && !addr.empty()) {
    // the address list is read once, then the first hit becomes the start
    AddressIndex* ix = loadAddresses();
    AddressHit hit;
    bool found = false;
    if (ix) {
      std::vector<AddressHit> hits = ix->search(addr, 1);
      if (!hits.empty()) {
        hit = hits[0];
        found = true;
      }
    }
    begin(start, std::atoi(q.get("spawn").c_str()), true, found ? &hit : nullptr);
  } else if (!start.empty()) {
    begin(start, std::atoi(q.get("spawn").c_str()), true);
  } else {
    toMenu();
  }
}

void Game::frame(double now) {
  // (never backwards: the time stamp of a frame that was held up by a long task
  // can lie before the moment the clock was last read, and a clock running
  // backwards freezes everything timed)
  double dt = std::min(0.1, std::max(0.0, (now - last_) / 1000.0));
  last_ = now;
  time_ += dt;
  frames_++;
  App& a = app;
  TileManager* tiles = a.tiles.get();

  if (state == GameState::Placing) {
    if (hasSpawn_) tiles->update(spawn_.x, spawn_.z);
    tryPlace();
  } else if (state == GameState::Drive) {
    // fingers or keys: follows what the driver last used, unless the settings
    // say otherwise (0.5.0); the texts name keys accordingly (0.6.0)
    const std::string& want = settings.touchControls;
    inputMode_ = want == "on" ? "touch" : (want == "off" ? "keys" : input->mode);
    hud->setInputMode(inputMode_);
    setKeyStyle(inputMode_ == "touch" ? KeyStyle::Touch : KeyStyle::Keys);
    stepDrive(dt);
    if (!autoNotice_.empty() && time_ - startTime_ > 0.3) {
      // (they stay: the automated check may take minutes for a frame)
      const std::string kind = autoNotice_;
      autoNotice_.clear();
      if (kind != "yellow") {
        Violation v;
        v.id = "redLight";
        v.severity = Severity::Fail;
        v.time = time_;
        hud->toast(v, 3600);
      }
      if (kind != "red") {
        Violation v;
        v.id = "speed";
        v.severity = Severity::Minor;
        v.time = time_;
        v.data = {{"limit", "30"}, {"speed", "42"}};
        hud->toast(v, 3600);
      }
    }
    // ?tram=1 puts a tram on the nearest track within 150 m
    if (autoTram_ && time_ - startTime_ > 0.3) {
      autoTram_ = false;
      if (rail) {
        int bestPath = -1;
        double bestS = 0, bestD = 0;
        bool any = false;
        for (int dx : {-200, 0, 200}) {
          for (int dz : {-200, 0, 200}) {
            std::vector<double> list = rail->net.near(mpos_[0] + dx, mpos_[2] + dz);
            for (size_t k = 0; k + 1 < list.size(); k += 2) {
              double sx, sy, sz, tx, tz, cap;
              rail->net.sample(static_cast<int>(list[k]), list[k + 1], sx, sy, sz, tx, tz, cap);
              double d = std::hypot(sx - mpos_[0], sz - mpos_[2]);
              if (!any || d < bestD) {
                any = true;
                bestD = d;
                bestPath = static_cast<int>(list[k]);
                bestS = list[k + 1];
              }
            }
          }
        }
        if (any && bestD < 150) {
          int way = rail->net.paths[static_cast<size_t>(bestPath)].way;
          if (!rail->spawnAt(bestPath, bestS, way == 0 ? 1 : way))
            rail->spawnAt(bestPath, bestS, way == 0 ? -1 : way);
        }
      }
    }
    // ?map=1|aerial opens the full-screen map once the drive has started
    if (!autoMap_.empty() && time_ - startTime_ > 0.3) {
      std::string mode;
      mode.swap(autoMap_);
      if (mode == "aerial") settings.reality.aerial = true;
      openBigMap();
      if (mode == "aerial" && reality_) {
        // a close-up of where the car is, with the photographs under it
        settings.reality.aerial = true;
      }
    }
    // ?reality=1|test opens the reality view once the drive has started
    if (!autoReality_.empty() && time_ - startTime_ > 0.3) {
      std::string mode;
      mode.swap(autoReality_);
      openReality();
      if (mode == "test" && reality_) {
        // drawn as if taken looking north-east, a few metres from the car
        std::pair<double, double> ll = worldToLatLon(mpos_[0], mpos_[2]);
        TestPanorama p = makeTestPanorama(ll.first + 0.00004, ll.second, 45);
        reality_->importFiles({{p.name, p.jpeg}});
      }
    }
  }

  if (bigMap && bigMap->isOpen()) {
    bigMap->poll();
    // the map covers the window: nothing of the world needs drawing
    int w = app.renderer.width(), h = app.renderer.height();
    if (w > 0 && h > 0) {
      Canvas2D picture(w, h);
      bigMap->paint(picture);
      host_.paint("bigmapCanvas", picture);
    }
  }
  bool haveCar = vehicle != nullptr && modelVisible();
  if (haveCar) {
    Vehicle& v = *vehicle;
    v.getModelPosition(mpos_);
    Object3D& model = modelRoot();
    model.position.set(mpos_[0], mpos_[1], mpos_[2]);
    model.quaternion.set(v.quat[0], v.quat[1], v.quat[2], v.quat[3]);
    model.updateMatrixWorld(true);
    if (truck && trailer && isTruck)
      truck->setTrailer(trailer->x, trailer->y, trailer->z, trailer->yaw, trailer->pitch,
                        trailer->wheelSpin, trailer->coupled);
    tiles->update(mpos_[0], mpos_[2]);
    props.update(mpos_[0], mpos_[2]);
    updateVisuals(dt);
  } else if (state == GameState::Menu || state == GameState::Loading) {
    // slow fly-over behind the menu
    double aa = time_ * 0.05;
    double cx = free_.pos.x + std::cos(aa) * 260;
    double cz = free_.pos.z + std::sin(aa) * 260;
    tiles->update(free_.pos.x, free_.pos.z);
    props.update(cx, cz);
    double gy = tiles->heightAt(free_.pos.x, free_.pos.z);
    double base = std::isnan(gy) ? 230 : gy;
    a.camera.position.set(cx, base + 95, cz);
    a.camera.lookAt(free_.pos.x, base + 10, free_.pos.z);
    a.camera.fov = 55;
    a.camera.near = 0.5;
    a.camera.updateProjectionMatrix();
    signals->update(dt);
  }
  signs->flush(a.renderer);
  signalHeads->update(*signals);
  actors.update(traffic->cars, traffic->peds, time_);
  if (rail) railActors.update(rail->vehicles);

  if (state == GameState::Map) {
    // the map covers the window: nothing of the world needs drawing
  } else if (haveCar) {
    updateCamera(dt);
    Mirrors mode = settings.mirrors;
    if (cameraMode == CameraMode::Cockpit && mode != Mirrors::Off) {
      // the mirrors show what is behind: every mirror when asked for, one per
      // frame while the player turns their head (the original's mirror pass)
      double far = std::min(450.0, a.tiles->viewRadius * 0.4);
      const Matrix4& carMatrix = modelRoot().matrixWorld;
      std::vector<std::unique_ptr<Mirror>>* ms = nullptr;
      if (isTruck && truck) ms = &truck->mirrors;
      else ms = &car.mirrors;
      if (mode == Mirrors::Full || frames_ < 4) {
        for (auto& m : *ms) m->render(a.renderer, a.scene, carMatrix, eyeWorld_, far);
      } else {
        size_t k = static_cast<size_t>(mirrorTurn_++) % ms->size();
        (*ms)[k]->render(a.renderer, a.scene, carMatrix, eyeWorld_, far);
      }
    }
    a.render(modelRoot().position);
  } else {
    a.render(free_.pos);
  }
}

void Game::updateVisuals(double dt) {
  Vehicle& v = *vehicle;
  Controls& c = input->c;
  CarVisualState& s = visual_;
  double kmh = v.speed * 3.6;
  s.speedKmh = std::fabs(kmh);
  s.rpm = v.rpm;
  s.gear = v.spec.transmission == "automatic" ? (c.gear == 1 ? v.gear : c.gear) : v.gear;
  s.indicator = c.indicator;
  s.hazard = c.hazard;
  s.blinkOn = static_cast<int>(std::floor(time_ * 3)) % 2 == 0;
  s.lowBeam = c.lowBeam;
  s.highBeam = c.highBeam;
  s.handbrake = c.handbrake > 0.5;
  s.ignition = c.ignition;
  s.engineRunning = v.engineRunning;
  s.abs = v.absActive;
  s.seatbelt = c.seatbelt;
  s.automatic = v.spec.transmission == "automatic";
  s.showGear = settings.hudGear;
  s.odometerKm = v.odometer / 1000;
  s.steerWheel = v.steerWheelAngle;
  v.getWheelHeights(wheelY_);
  for (int i = 0; i < 4; i++) {
    s.wheelSteer[i] = v.wheels[static_cast<size_t>(i)].steer;
    s.wheelSpin[i] = v.wheels[static_cast<size_t>(i)].spin;
    s.wheelY[i] = wheelY_[i];
  }
  s.clutch = v.clutchPedal;
  s.brake = driver_.brake;
  s.throttle = driver_.throttle;
  s.handbrakeLever = c.handbrake;
  s.braking = driver_.brake > 0.03;
  s.reversing = v.gear == -1;
  if (isTruck && truck) truck->update(s, dt, time_);
  else car.update(s, dt, time_);

  // sound
  double skid = 0, rough = 1;
  for (const auto& w : v.wheels) {
    skid = std::max(skid, w.skid);
    rough = std::max(rough, w.grip < 0.75 ? 2.4 : 1.0);
  }
  AudioState as;
  as.rpm = v.rpm;
  as.load = v.engineLoad;
  as.engineRunning = v.engineRunning;
  as.starter = c.starter;
  as.speed = v.speed;
  as.skid = skid;
  as.indicatorOn = (c.indicator != 0 || c.hazard) && state == GameState::Drive;
  as.blinkOn = s.blinkOn;
  as.horn = c.horn;
  as.rough = rough;
  audio.update(as);

  // HUD
  if (state != GameState::Drive) return;
  const RuleContext& ctx = rules->context;
  NavHint& h = nav->hint;
  std::string navText;
  if (nav->goal && hasLesson_ && lesson_.kind != "basics") {
    const std::string& lang = settings.lang;
    std::string what = h.what == "navRoundabout"
                           ? fs::tr("navRoundabout", lang, {{"n", std::to_string(h.n)}})
                           : fs::tr(h.what, lang);
    if (h.what == "navFollow" || h.dist < 0)
      navText = fs::tr("navFollow", lang);
    else if (h.dist < 35)
      navText = fs::tr("navNow", lang, {{"what", what}});
    else
      navText = fs::tr("navIn", lang,
                       {{"d", std::to_string(std::lround(h.dist / 10) * 10)}, {"what", what}});
    speak(h.what, h.dist, navText);
    // § 9 StVO: get in lane in good time – the route goes on from a particular lane
    if (h.lane != 0)
      navText += " – " + fs::tr(h.lane < 0 ? "laneLeft" : "laneRight", lang);
  } else if ((hasLesson_ && lesson_.kind == "basics") || task_) {
    navText = lessonLine_;   // the steps of the basics lesson, as in the original
  }

  std::string tip;
  if (time_ < tipUntil_) tip = tipText_;
  std::string tipAction;
  // on a touch screen the tip says what to tap – and is itself the button
  const bool touchTip = inputMode_ == "touch";
  auto say = [&](const char* key, const char* action) {
    tip = fs::tr(touchTip ? std::string(key) + "Touch" : std::string(key), settings.lang);
    tipAction = action;
  };
  if (tip.empty()) {
    const std::string& lang = settings.lang;
    bool stalledRecently = false;
    for (const auto& x : rules->log)
      if (x.id == "stall" && time_ - x.time < 6) stalledRecently = true;
    if (!v.engineRunning)
      say(stalledRecently ? "engineStalled" : "startEngine", "Enter");
    else if (!c.seatbelt && std::fabs(kmh) < 5)
      say("fastenBelt", "KeyB");
    else if (c.handbrake > 0.5 && c.throttle > 0.1)
      say("releaseHandbrake", "Space");
    else if (c.throttle > 0.1 && std::fabs(kmh) < 2 &&
             (s.automatic ? (c.gear == -2 || c.gear == 0) : v.gear == 0))
      say(s.automatic ? "selectDrive" : "selectGear", "KeyE");
    else {
      Lane* lane = currentLane();
      if (lane && lane->to && lane->to->ctrl == CTRL_MAP_END &&
          lane->len - laneFront() < 120)
        tip = fs::tr("offMap", lang);
      else if (!ctx.onRoad && std::fabs(kmh) < 3 && time_ - lastGood_.t > 6)
        say("resetHint", "KeyT");
    }
  }

  std::string gearText;
  if (s.automatic)
    gearText = c.gear == -2 ? "P" : (c.gear == -1 ? "R" : (c.gear == 0 ? "N" : "D" + std::to_string(v.gear)));
  else
    gearText = v.gear == -1 ? "R" : (v.gear == 0 ? "N" : std::to_string(v.gear));

  // driving against the handbrake: the lamp flashes and a chime sounds (once per attempt)
  bool hbWarn = c.handbrake > 0.5 && v.engineRunning && (c.throttle > 0.1 || std::fabs(kmh) > 3);
  if (hbWarn && !hbWarned_) audio.click(ClickKind::Chime);
  hbWarned_ = hbWarn;

  HudState hs;
  hs.speedKmh = kmh;
  hs.rpm = v.rpm;
  // (the bar is full a little above the limiter: 7000 for the car as before)
  hs.rpmMax = v.spec.limiterRpm * 1.077;
  hs.gear = gearText;
  hs.limit = ctx.limit;
  hs.zone = ctx.zone;
  hs.junction.set = ctx.junction.set;
  hs.junction.dist = ctx.junction.dist;
  hs.junction.role = ctx.junction.role;
  hs.junction.ctrl = ctx.junction.ctrl;
  hs.junction.signal = ctx.junction.signal;
  hs.navText = navText;
  hs.navSub = task_ ? taskSub_
             : (demo_ ? (demo_->remark.empty()
                             ? fs::tr("demoRunning", settings.lang,
                                      {{"n", std::to_string(demo_->index + 1)},
                                       {"max", std::to_string(demo_->stops.size())}})
                             : fs::tr("demo." + demo_->remark, settings.lang))
              : (hasLesson_ && lesson_.kind != "basics" ? lessonLine_ : ctx.roadName));
  hs.navArrow = h.arrow;
  hs.navActive = hasLesson_ && h.dist >= 0 && h.what != "navFollow" && h.what != "routeDone";
  hs.tip = tip;
  hs.tipAction = tipAction;
  // (the truck's instruments stand far away at the foot of its windscreen: the strip stays)
  hs.showDash = cameraMode != CameraMode::Cockpit || isTruck;
  hs.indicator = c.indicator;
  hs.hazard = c.hazard;
  hs.blinkOn = s.blinkOn;
  hs.handbrake = c.handbrake > 0.5;
  hs.handbrakeWarn = hbWarn;
  hs.seatbelt = c.seatbelt;
  hs.engineOn = v.engineRunning;
  hs.lights = c.lowBeam;
  hs.points = rules->points();
  int faults = 0;
  for (const auto& x : rules->log)
    if (x.severity != Severity::Hint) faults++;
  hs.faults = faults;
  hs.x = mpos_[0];
  hs.z = mpos_[2];
  hs.yaw = v.yaw;
  if (demo_) {
    // a demonstration drive: where it goes, and under it what the driver is doing just now and why
    if (navText.empty())
      hs.navText = ctx.roadName.empty() ? fs::tr("demo", settings.lang) : ctx.roadName;
  } else if (navText.empty() && !hasLesson_) {
    const std::string& lang = settings.lang;
    hs.navText = ctx.roadName.empty() ? fs::tr("free", lang) : ctx.roadName;
    hs.navSub = fs::tr(inputMode_ == "touch" ? "navSubTouch" : "navSubKeys", lang);
    hs.navArrow = 0;
  }
  hud->update(hs, time_);
}

void Game::speak(const std::string& what, double dist, const std::string& text) {
  if (!settings.voice) return;
  // announce each manoeuvre twice: early and right before it
  std::string stage = dist < 0 ? "" : (dist < 45 ? "now" : (dist < 260 ? "soon" : ""));
  std::string key = what + ":" + stage + ":" + std::to_string(targetIdx_);
  if (stage.empty() || what == "navFollow" || key == lastNavSpoken_) return;
  lastNavSpoken_ = key;
  (void)text;   // the platform speaks it (browser speechSynthesis in the original)
}

void Game::updateCamera(double dt) {
  Camera& cam = app.camera;
  Object3D& carRoot = modelRoot();
  Vehicle& v = *vehicle;
  Controls& c = input->c;
  // the eye is always needed for the mirrors
  Vector3 lean(-v.accLat * 0.004, 0, v.accLong * 0.005);
  lean.clampLength(0, 0.06);
  eyeWorld_ = car.eye;
  eyeWorld_.add(lean);
  eyeWorld_.applyMatrix4(carRoot.matrixWorld);
  if (cameraMode == CameraMode::Cockpit) {
    cam.near = 0.06;
    cam.fov = 60;
    cam.position.copy(eyeWorld_);
    // looking over the shoulder also moves the head a little towards that side
    // (on a demonstration drive the head turns as the driver looks over the shoulder)
    double want = (demo_ && demo_->active) ? demo_->look * 0.8 : 0;
    demoHead_ += std::max(-dt * 5.0, std::min(dt * 5.0, want - demoHead_));
    tmpQ_.setFromEuler(Euler(c.lookPitch, c.lookYaw + demoHead_, 0, Euler::YXZ));
    cam.quaternion.copy(carRoot.quaternion);
    cam.quaternion.multiply(tmpQ_);
  } else if (cameraMode == CameraMode::Chase) {
    cam.near = 0.3;
    cam.fov = 55;
    Vector3 back(std::sin(c.lookYaw) * 7, 2.6, std::cos(c.lookYaw) * 7);
    back.applyAxisAngle(Object3D::DEFAULT_UP, v.yaw);
    Vector3 target = carRoot.position;
    target.add(back);
    double g = app.tiles->heightAt(target.x, target.z);
    if (!std::isnan(g)) target.y = std::max(target.y, g + 1.2);
    cam.position.lerp(target, std::min(1.0, dt * 8));
    cam.lookAt(carRoot.position.x, carRoot.position.y + 1.1, carRoot.position.z);
  } else {
    cam.near = 0.2;
    cam.fov = 60;
    FreeCam& f = free_;
    double speed = (platform_.key("ShiftLeft") ? 80 : 12) * dt;
    if (platform_.key("Numpad8")) f.pos.z -= speed;
    if (platform_.key("Numpad2")) f.pos.z += speed;
    if (platform_.key("Numpad4")) f.pos.x -= speed;
    if (platform_.key("Numpad6")) f.pos.x += speed;
    if (freeRelative_) {
      cam.position.copy(f.pos);
      cam.position.applyQuaternion(carRoot.quaternion);
      cam.position.add(carRoot.position);
      tmpQ_.setFromEuler(Euler(f.pitch, f.yaw, 0, Euler::YXZ));
      cam.quaternion.copy(carRoot.quaternion);
      cam.quaternion.multiply(tmpQ_);
    }
  }
  cam.updateProjectionMatrix();
}

std::string Game::debugInfo() const {
  char buf[512];
  std::snprintf(buf, sizeof buf, "state=%d frames=%d", static_cast<int>(state), frames_);
  return buf;
}

}  // namespace fs
