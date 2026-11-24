// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7 (the constructor, boot, the settings, the menu – game.ts:1 … 1952)
#include "game/game.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "audio/audio.h"
#include "core/js.h"
#include "game/address.h"
#include "game/arcadedrive.h"
#include "game/ce.h"
#include "game/clock.h"
#include "game/craneplay.h"
#include "game/faultlog.h"
#include "game/flycam.h"
#include "game/yardtask.h"
#include "game/nav.h"
#include "game/netstats.h"
#include "game/releases.h"
#include "game/routepresets.h"
#include "game/track.h"
#include "geo/utm.h"
#include "i18n/index.h"
#include "i18n/keys.h"
#include "input/input.h"
#include "platform/frame.h"
#include "platform/plat.h"
#include "render/actors.h"
#include "render/crane.h"
#include "render/far.h"
#include "render/pedlights.h"
#include "render/props.h"
#include "render/yard.h"
#include "render/semitruck.h"
#include "render/signs.h"
#include "render/truck.h"
#include "sim/arrows.h"
#include "sim/autodriver.h"
#include "sim/clearlanes.h"
#include "sim/collision.h"
#include "sim/follow.h"
#include "sim/ground.h"
#include "sim/rules.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "sim/trailer.h"
#include "sim/vehicle/fleet.h"
#include "sim/vehicle/index.h"
#include "ui/bigmap.h"
#include "ui/geoip.h"
#include "ui/hud.h"
#include "ui/pathdraw.h"
#include "ui/screentap.h"
#include "ui/tiltsteer.h"
#include "ui/touchstick.h"
#include "ui/worldtap.h"
#include "world/far.h"
#include "world/netindex.h"
#include "world/pedlights.h"
#include "world/regions.h"

namespace fs {

/** Which model a vehicle is drawn with: its kind of model – for a motorcycle, the build of its class. */
std::string modelKey(const VehicleDef& def) {
  // (later: bikeVariant, the motorcycles' builds – game.ts:263)
  return def.model;
}

// ---- URLSearchParams ----

URLSearchParams::URLSearchParams(const std::string& search) {
  const std::string s = !search.empty() && search[0] == '?' ? search.substr(1) : search;
  for (const std::string& part : split(s, "&")) {
    if (part.empty()) continue;
    const size_t eq = part.find('=');
    const auto plain = [](std::string t) { return decodeURIComponent(replaceAll(std::move(t), "+", " ")); };
    if (eq == std::string::npos) list_.emplace_back(plain(part), "");
    else list_.emplace_back(plain(part.substr(0, eq)), plain(part.substr(eq + 1)));
  }
}

std::optional<std::string> URLSearchParams::get(const std::string& name) const {
  for (const auto& [k, v] : list_)
    if (k == name) return v;
  return std::nullopt;
}

bool URLSearchParams::has(const std::string& name) const {
  for (const auto& [k, v] : list_)
    if (k == name) return true;
  return false;
}

namespace {

/** the car's centre display, for the HUD to draw the map on */
class CarScreen : public VehicleScreen {
 public:
  explicit CarScreen(PlayerCar& car) : VehicleScreen(car.mapCanvas), car_(car) {}
  void drawn() override { car_.mapTexture->needsUpdate(); }
  void setLive(bool on) override { car_.setMapOnScreen(on); }
  Object3D* hitMesh() override { return car_.screenMesh(); }

 private:
  PlayerCar& car_;
};

/** the tiles as the ground under the wheels sees them (sim/ground.h) */
class TilesTerrain : public TerrainSource {
 public:
  explicit TilesTerrain(App& app) : app_(app) {}
  double heightAt(double x, double z) override { return app_.tiles->heightAt(x, z); }
  int coverAt(double x, double z) override { return app_.tiles->coverAt(x, z); }

 private:
  App& app_;
};

/** `Number(text)` of a part of the address (null: 0, as `Number(q.get(…) ?? 0)`) */
double numberOf(const std::optional<std::string>& text) { return text ? jsNumber(*text) : 0; }

/** "1,2,3".split(',').map(Number) */
std::vector<double> numbersOf(const std::string& text) {
  std::vector<double> out;
  for (const std::string& p : split(text, ",")) out.push_back(jsNumber(p));
  return out;
}

/** `list[i] ?? fallback` */
double at(const std::vector<double>& list, size_t i, double fallback) { return i < list.size() ? list[i] : fallback; }

}  // namespace

Game::Demo::Demo() = default;
Game::Demo::~Demo() = default;
Game::Demo::Demo(Demo&&) noexcept = default;
Game::Demo& Game::Demo::operator=(Demo&&) noexcept = default;

Game::Game(const dom::El& canvas, const dom::El& ui)
    : app(canvas),
      ui(ui),
      props(std::make_unique<PropRenderer>()),
      actors(std::make_unique<TrafficRenderer>()),
      carScreen(std::make_unique<CarScreen>(car)),
      pedHeads(std::make_unique<PedLightRenderer>()),
      far(std::make_unique<FarTerrain>()),
      collider(std::make_unique<SceneryCollider>()),
      audio(std::make_unique<GameAudio>()),
      arcadeDrive(std::make_unique<ArcadeDrive>()),
      faultLog(std::make_unique<FaultLog>()),
      path(std::make_unique<DrivenPath>()),
      showNewsOnce(newsDue(playedBefore())),
      fly(std::make_unique<FlyCam>()),
      yardScene(std::make_unique<YardScene>()) {
  driver.steer = 0;
  driver.throttle = 0;
  driver.brake = 0;
  driver.clutch = 0;
  driver.handbrake = 1;
  driver.gear = 0;
  driver.ignition = true;
  driver.starter = false;
  visual.speedKmh = 0;
  visual.rpm = 0;
  visual.gear = 0;
  visual.indicator = 0;
  visual.hazard = false;
  visual.blinkOn = false;
  visual.lowBeam = false;
  visual.highBeam = false;
  visual.handbrake = true;
  visual.ignition = true;
  visual.engineRunning = false;
  visual.abs = false;
  visual.seatbelt = true;
  visual.fuel = 0.82;
  visual.automatic = false;
  visual.showGear = true;
  visual.odometerKm = 0;
  visual.steerWheel = 0;
  visual.wheelSteer = {0, 0, 0, 0};
  visual.wheelSpin = {0, 0, 0, 0};
  visual.wheelY = {0.316, 0.316, 0.316, 0.316};
  visual.clutch = 0;
  visual.brake = 0;
  visual.throttle = 0;
  visual.handbrakeLever = 1;
  visual.braking = false;
  visual.reversing = false;
  dom::window().on("keydown", [this](dom::Event& e) { keys.insert(e.code); });
  dom::window().on("keyup", [this](dom::Event& e) { keys.erase(e.code); });
}

Game::~Game() = default;

Task<void> Game::boot(URLSearchParams q) {
  App& app = this->app;
  // ?lang=xx chooses the language for this visit
  const std::optional<std::string> ql = q.get("lang");
  if (ql && !ql->empty() && std::any_of(LANGS.begin(), LANGS.end(), [&](const LangInfo& l) { return l.code == *ql; })) settings.lang = *ql;
  // the texts of the chosen language and the letters of the player's keyboard
  {
    Task<bool> langTask = loadLang(settings.lang);
    Task<void> keysTask = initKeyLabels();
    const bool ok = co_await langTask;
    co_await keysTask;
    if (!ok) settings.lang = "en";
  }
  applyLanguage();
  const std::string lang = settings.lang;
  // a first, world-less HUD only for the loading screen
  dom::El loading = dom::el("div");
  loading.className("overlay solid");
  dom::El panel = dom::el("div");
  panel.className("panel narrow");
  panel.innerHTML("<h1>Fahrsim <span>Baunatal</span></h1><p></p><div class=\"progress known\"><i style=\"width:0%\"></i></div><small class=\"progress-pct\">0 %</small>");
  panel.querySelector("p").textContent(tr("loading", lang));
  loading.append(panel);
  ui.append(loading);
  // how far it is: each step's share is reached when it is done – meanwhile the bar creeps on towards it
  struct Progress {
    dom::El bar;
    dom::El pct;
    double shown = 0;
    double from = 0.05;
    double to = 0.45;
    double since = plat::nowMs();
    /** the bar still creeps (the original's setInterval, until clearInterval) */
    bool creeping = true;
    void show(double p) {
      shown = std::max(shown, std::min(1.0, p));
      bar.style("width", jsStr(jsRound(shown * 100)) + "%");
      // (later: formatBytes(downloaded()) beside it – game/netstats.ts startNetStats, game.ts:602)
      pct.textContent(jsStr(jsRound(shown * 100)) + " %");
    }
    void step(double done, double next) {
      show(done);
      from = done;
      to = next;
      since = plat::nowMs();
    }
  };
  const std::shared_ptr<Progress> progress = std::make_shared<Progress>();
  progress->bar = panel.querySelector(".progress i");
  progress->pct = panel.querySelector(".progress-pct");
  {
    // setInterval(…, 120)
    auto creep = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> again = creep;
    *creep = [progress, again] {
      if (!progress->creeping) return;
      progress->show(progress->from + (progress->to - progress->from) * 0.9 * (1 - std::exp(-(plat::nowMs() - progress->since) / 2500)));
      if (auto f = again.lock()) plat::setTimeout([f] { (*f)(); }, 120);
    };
    plat::setTimeout([creep] { (*creep)(); }, 120);
  }
  progress->show(0.05);
  {
    std::exception_ptr failed;
    try {
      // (the first start: the region the player is in, from their IP – no permission asked;
      //  a region chosen in the settings is kept, and if it is not there the site's map loads)
      co_await firstRegionByIp();
      std::optional<Region> region = co_await chosenRegion();
      co_await app.loadWorld(nullptr, std::move(region));
    } catch (...) {
      failed = std::current_exception();
    }
    // (on an error the message takes the screen: the bar stops)
    if (!app.world) progress->creeping = false;
    if (failed) std::rethrow_exception(failed);
  }
  progress->step(0.45, 0.75);
  World& world = *app.world;
  // (later: outdoors, the weather and the season: setMaterialHook, props.setSeason, far.setSeason – game.ts:622)
  co_await plat::sleep(0);
  lanes = std::make_unique<LaneGraph>(world);
  signals = std::make_unique<SignalSystem>(world.signals, world.lightsOut);
  // (the green arrows of what is there already – a map in one piece; the pieces of a map in pieces get theirs as they come)
  markArrows(world, *lanes, *signals, world.allNodes);
  traffic = std::make_unique<Traffic>(world, *lanes, *signals);
  // (later: traffic.busSchedule, the buses of the lines with timetables – game.ts:634)
  // (later: traffic.emergency – game.ts:636)
  rules = std::make_unique<RuleMonitor>(world, *lanes, *signals, *traffic);
  nav = std::make_unique<Navigator>(*lanes);
  terrain = std::make_unique<TilesTerrain>(app);
  ground = std::make_unique<WorldGround>(world, *terrain);
  // (later: wading, ground.waterDepth – game.ts:640)
  signs = std::make_unique<SignRenderer>(plat::locationBase() + "signs/", app.renderer.getMaxAnisotropy());
  // (later: extraSigns, rules.timed: the signs the map was built without – game.ts:645)
  co_await signs->init();
  progress->step(0.75, 0.98);
  signalHeads = std::make_unique<SignalRenderer>(world, signals.get());
  collider->add({.key = "signals", .rings = nullptr, .ringCounts = nullptr, .segments = nullptr, .discs = signalHeads->poles});
  buildPedLights();
  // (later: transit, traffic.crowd's blocked, toStop, standing – game.ts:656)
  actors->view = app.camera.get();
  // (later: actors.highOf, the traffic's main beam – game/beamplay.ts, game.ts:663)
  // (later: airfieldView, services, spawnCars – game.ts:664)
  // (later: TapPicker, a tap on a vehicle: watch it – pickBoxes, picked, game.ts:669)
  app.tiles->onTile([this](LoadedTile& tile, bool loaded) { onTile(tile, loaded); });
  app.scene->add(car.root, actors->group, signalHeads->group, pedHeads->group, far->group, yardScene->group);
  // (later: railActors, passengerView, transit, airfieldView, drillScene, markers, figure, showGroup, showRail, showPeople, explosion in the scene; fadeEl – game.ts:671)
  if (world.json.yard) yardFrame.emplace(*world.json.yard);
  else yardFrame.reset();
  // (later: rail = new RailTraffic(world.json.rails) – game.ts:676)
  // The road network may come in pieces around the car: lanes, traffic and signals follow it.
  followers = std::make_unique<MapFollowers>();
  followers->lanes = lanes.get();
  followers->traffic = traffic.get();
  followers->signals = signals.get();
  followers->changed = [this] { signalsDirty = true; };
  followMap(*app.map, *followers);
  // (later: roadEvents, traffic.wild, lanes.wayCost, narrowCollide, levelCrossings, crossingView, roadworks – game.ts:679)
  if (app.map->pieces) {
    // … then the roads of the whole map (some megabytes: on a connection that asks to save data only
    // once they are needed) and the railway tracks are read while the menu is up
    // (later: navigator.connection.saveData, which the host has no word for yet – game.ts:707)
    const bool saving = false;
    if (!saving || autoTour > 0 || !autoDemo.empty() || !autoMap.empty()) wantNet();
    // (later: app.map.rails(), air(), lines(), timetable() – game.ts:709)
    // the ground of the whole region, coarse: what lies beyond the tiles
    [](Game* self) -> Task<void> {
      const std::shared_ptr<const Bytes> buf = co_await self->app.map->far();
      std::optional<FarGround> f = buf ? parseFar(*buf) : std::nullopt;
      if (f) self->far->set(std::move(f));
    }(this).detach();
    // (later: app.map.stops(): the stops, the bus lines – game.ts:759)
  }
  car.root->visible = false;
  progress->creeping = false;
  progress->show(1);
  loading.remove();

  hud = std::make_unique<Hud>(ui, settings, world);
  // (later: signalTimers – game.ts:781)
  input = std::make_unique<Input>(app.canvas, settings);
  input->onAction([this](const Action& a) { onAction(a); });
  hud->onStart = [this](const StartRequest& req) { begin(req.mode, req.spawn, false, req.address ? &*req.address : nullptr); };
  // (later: hud.onMenuPreview, transit.departuresFor – game.ts:788)
  hud->onResume = [this] { resume(); };
  // (later: hud.onReality – game.ts:798)
  // (later: hud.arcade, the arcade mode's panel: the world at another moment, the time faster – ui/arcade.ts, game.ts:800)
  hud->onChangeVehicle = [this](const VehicleKind& kind) { changeVehicle(kind); };
  // (later: hud.onLiveLocation, toggleLiveGeo – game.ts:844)
  // (later: hud.onNewVehicle, a new vehicle after one went up in flames – game.ts:848)
  hud->onArcade = [this] {
    pause();
    if (state == "pause") hud->showArcade();
  };
  // (later: hud.onGetOut, on foot beside the vehicle – game.ts:855)
  hud->onPause = [this] { pause(); };
  hud->loadAddresses = [this] { return loadAddresses(); };
  hud->onTeleport = [this](const AddressHit& hit) { teleportTo(hit); };
  // (later: reality – game.ts:872)
  hud->onQuit = [this] { toMenu(); };
  // (later: makeOnline – game.ts:886)
  hud->faultLog = faultLog.get();
  hud->onMap = [this] { openMap(); };
  hud->where = [this]() -> std::optional<AddressIndex::Near> {
    if (!vehicle) return std::nullopt;
    return AddressIndex::Near{mpos[0], mpos[2]};
  };
  hud->onDemo = [this] {
    // (later: the aeroplane's autopilot, toggleFlightAuto – game.ts:891)
    openMap();
    bigMap->plan();
  };
  hud->onFinish = [this] {
    if (state == "pause" && !lesson) finish();
  };
  {
    BigMapHost host;
    host.world = &world;
    host.lang = [this] { return settings.lang; };
    // (later: the menu preview's yaw while the map is opened from the menu – game.ts:906)
    host.car = [this] { return BigMapHost::Car{mpos[0], mpos[2], mapFrom == "menu" ? 0 : vehicle->yaw}; };
    host.path = path.get();
    host.net = [this] { return netIndex.get(); };
    host.spotAt = [this](double x, double z) -> std::optional<SpawnJson> {
      if (settings.placeExact) return exactSpot(tr("mapPlace", settings.lang), x, z);
      return roadSpot(x, z, "", 160);
    };
    // (later: loadedArea, canSpawnCar, spawnCar – game/spawncar.ts, game.ts:910)
    host.canTeleport = [this] { return mapFrom != "menu" && !lesson && !crane; };
    host.teleport = [this](const SpawnJson& spot) {
      if (mapFrom != "menu") return teleportSpot(spot);
      hud->menuStartAt(spot);
      closeMap();
    };
    host.menu = [this] { return mapFrom == "menu"; };
    // (later: events, stops, services, emergencies, fuelPrice, priceSource, watchPoint – game.ts:923)
    host.loadAddresses = [this] { return loadAddresses(); };
    host.spotOf = [this](const AddressHit& hit) { return spawnAt(hit); };
    host.startTrip = [this](const std::vector<SpawnJson>& stops, bool self, const TourPreview* tour) { startTrip(stops, self, tour); };
    host.startPreset = [this](const std::string& id) { startPreset(id); };
    host.planTour = [this](double metres, std::optional<SpawnJson> end, std::function<void(double share)> progress) { return planTour(metres, std::move(end), std::move(progress)); };
    // (later: tramways – game.ts:938)
    host.tourPrefs = [this] { return settings.tourPrefs; };
    host.setTourPrefs = [this](const TourPrefs& p) {
      settings.tourPrefs = p;
      saveSettings(settings);
    };
    host.roadsKnown = [this] { return routeGraphNow() != nullptr; };
    host.tripActive = [this] { return demo != nullptr; };
    host.endTrip = [this] { endDemo("ended"); };
    host.latLon = [](double x, double z) { return worldToLatLon(x, z); };
    host.aerial = [this] { return settings.reality.aerial; };
    host.setAerial = [this](bool on) {
      settings.reality.aerial = on;
      saveSettings(settings);
    };
    // (later: transit, watch – game.ts:953)
    host.roads = [this, &world]() -> const RoadSource* {
      RouteGraph* g = routeGraphNow();
      if (!g) return nullptr;
      mapRoads = std::make_unique<RoadSource>(roadSource(*g, netIndex.get(), world));
      return mapRoads.get();
    };
    // (later: flying, teleportAir – game.ts:959)
    host.close = [this] { closeMap(); };
    // 2.0: one map for all regions – which one is open, and "Open this map": the choice is kept and the game starts anew with it
    host.region = [this] { return this->app.regionId; };
    host.onOpenRegion = [this](const Region& r) {
      settings.region = r.id;
      saveSettings(settings);
      plat::reload();
    };
    bigMap = std::make_unique<BigMap>(std::move(host));
  }
  // the driven path is written down now and then; make sure the last stretch is, too
  dom::window().on("pagehide", [this](dom::Event&) { path->save(); });
  hud->onSettingsChanged = [this] { applySettings(); };
  hud->onTiltSteer = [this](bool on) { return setTiltSteer(on); };
  // (the autopilot's stop button: the player takes over the drive it was doing)
  hud->onStopAutopilot = [this] {
    if (demo) endDemo("taken");
  };
  // (later: hud.onTileVersion – game.ts:971)
  rules->onViolation = [this](const Violation& v) {
    hud->toast(v);
    faultLog->add({.t = plat::dateMs(), .id = v.id, .severity = v.severity, .data = v.data, .street = rules->context.roadName, .mode = lesson ? lesson->id : "free", .x = jsRound(v.x), .z = jsRound(v.z)});
    // (later: recorder.note, on the timeline of the replay too – game.ts:976)
    audio->click("chime");
    if (v.severity == "fail" && lesson && lesson->kind == "exam" && failT < 0) failT = time + 2.5;
  };
  hud->attachInput(*input);
  // test overrides (not saved): ?quality=low|medium|high&mirrors=off|alternate|full&traffic=0..1
  const std::optional<std::string> qq = q.get("quality");
  if (qq && (*qq == "low" || *qq == "medium" || *qq == "high" || *qq == "ultra" || *qq == "nomercy")) {
    settings.quality = *qq;
    settings.graphics = *GRAPHICS_PRESETS.find(*qq);
    if (*qq == "low") settings.render3d = 0.7;
  }
  const std::optional<std::string> qm = q.get("mirrors");
  if (qm && (*qm == "off" || *qm == "alternate" || *qm == "full")) settings.mirrors = *qm;
  if (q.get("traffic") && !q.get("traffic")->empty()) settings.traffic = jsNumber(*q.get("traffic"));
  // ?vehicle=truck|car chooses the vehicle for this visit
  const std::optional<std::string> qv = q.get("vehicle");
  const std::optional<VehicleKind> kind = vehicleKind(qv.value_or(""));
  // (a kind whose vehicle is not in this build – game/vehicles.h – is not chosen)
  if (kind && VEHICLES.find(*kind)) settings.vehicle = *kind;
  // (later: ?onfoot=1 – game.ts:996)
  applySettings();
  // (the tilt steering saved on: the listener is started here, so a phone held to steer does
  //  so without the switch being touched again – iOS asks its permission at the first touch,
  //  which ui/tiltsteer.ts waits for; a device that reports nothing leaves the setting off)
  if (settings.tiltSteer) setTiltSteer(true).detach();

  // developer / test shortcuts: ?start=<mode>&spawn=<n>&view=chase&auto=<throttle>&cam=x,y,z,yaw,pitch&look=yaw,pitch
  const std::optional<std::string> view = q.get("view");
  cameraMode = view && (*view == "chase" || *view == "cockpit" || *view == "free") ? *view : settings.view;
  input->resetLook(cameraMode == "chase", cameraMode == "free");
  const std::optional<std::string> cam = q.get("cam");
  if (cam && !cam->empty()) {
    const std::vector<double> c = numbersOf(*cam);
    cameraMode = "dev";
    free.pos.set(at(c, 0, JS_NAN), at(c, 1, JS_NAN), at(c, 2, JS_NAN));
    free.yaw = (at(c, 3, 0) * M_PI) / 180;
    free.pitch = (at(c, 4, -15) * M_PI) / 180;
  }
  autoDrive = numberOf(q.get("auto"));
  // (later: ?reality=, ?rv= – game.ts:1016)
  // ?notice=red|yellow|both shows sample notices, ?touch=on|off forces the on-screen controls,
  // ?addr=<street and number> starts at that address
  autoNotice = q.get("notice").value_or("");
  // ?map=1|aerial opens the full-screen map once the drive has started ("aerial": close up, with aerial photographs)
  autoMap = q.get("map").value_or("");
  // (later: ?tram=1 – game.ts:1024)
  // ?demo=x,z;x,z starts a demonstration drive to these places of the map once the drive has started,
  // ?tour=<km> one along a tour of the streets of that length; with &self=1 the player drives and is shown the way
  autoDemo.clear();
  for (const std::string& p : split(q.get("demo").value_or(""), ";"))
    if (p.find(',') != std::string::npos) autoDemo.push_back(numbersOf(p));
  {
    const double t = numberOf(q.get("tour"));
    autoTour = std::isnan(t) ? 0 : t;
  }
  autoSelf = q.get("self") == std::optional<std::string>("1");
  const std::optional<std::string> tc = q.get("touch");
  if (tc && (*tc == "on" || *tc == "off" || *tc == "auto")) settings.touchControls = *tc;
  const std::optional<std::string> look = q.get("look");
  if (look && !look->empty()) {
    const std::vector<double> l = numbersOf(*look);
    input->c.lookYaw = (at(l, 0, 0) * M_PI) / 180;
    debugLook = std::array<double, 2>{(at(l, 0, 0) * M_PI) / 180, (at(l, 1, 0) * M_PI) / 180};
  }
  const std::optional<std::string> atPlace = q.get("at");
  if (atPlace && !atPlace->empty()) {
    const std::vector<double> p = numbersOf(*atPlace);
    SpawnJson test;
    test.name = "Test";
    test.x = at(p, 0, JS_NAN);
    test.z = at(p, 1, JS_NAN);
    test.yaw = (at(p, 2, 0) * M_PI) / 180;
    test.kind = "street";
    world.json.spawns.insert(world.json.spawns.begin(), test);
  }
  last = plat::nowMs();
  looping = true;
  plat::requestAnimationFrame([this](double t) { frame(t); });
  const std::optional<std::string> start = q.get("start");
  const std::optional<std::string> addr = q.get("addr");
  const int spawnIdx = static_cast<int>(numberOf(q.get("spawn")));
  if (start && !start->empty() && addr && !addr->empty()) {
    [](Game* self, std::string start, std::string addr, int spawnIdx) -> Task<void> {
      AddressIndex* ix = co_await self->loadAddresses();
      const std::vector<SpawnJson>& spawns = self->app.world->json.spawns;
      std::optional<AddressIndex::Near> near;
      if (spawnIdx >= 0 && spawnIdx < static_cast<int>(spawns.size())) near = AddressIndex::Near{spawns[spawnIdx].x, spawns[spawnIdx].z};
      const std::vector<AddressHit> hits = ix ? ix->search(addr, 1, near) : std::vector<AddressHit>();
      self->begin(start, spawnIdx, true, hits.empty() ? nullptr : &hits[0]);
    }(this, *start, *addr, spawnIdx).detach();
  } else if (start && !start->empty()) begin(*start, spawnIdx, true);
  else {
    toMenu();
    startWhereIAm();
  }
}

/**
 * Developer: look from (x, height above the ground, z) towards the compass heading (degrees, 0 north, clockwise
 * as photos are) and pitch (degrees, up positive) with a vertical field of view – the world loads round it.
 */
double Game::devView(double x, double h, double z, double heading, double pitch, double fov, bool absolute) {
  cameraMode = "dev";
  // (the player's vehicle out of the picture – its parts, not its root: the loop draws while the root is shown)
  for (const ObjectPtr& c : model().root->children) c->visible = false;
  const double g = app.tiles->heightAt(x, z);
  free.pos.set(x, absolute ? h : (std::isnan(g) ? 200 : g) + h, z);
  free.yaw = (-heading * M_PI) / 180;
  free.pitch = (pitch * M_PI) / 180;
  devFov = fov;
  return std::isnan(g) ? JS_NAN : g;
}

void Game::onTile(LoadedTile& tile, bool loaded) {
  // (later: services.tile, spawnCars.tile – game.ts:1179)
  if (loaded) {
    // What stands in a lane (a fault of the map) is taken out of the way: sign posts are moved
    // beside the lane, lamps dropped, walls lose the pieces that reach in.  Maps built since do
    // this themselves and mark their tiles.
    // (later: extraSigns.mergeInto, the signs the map was built without – game.ts:1187)
    const bool clear = !(tile.header.clr && *tile.header.clr);
    if (clear) {
      clearSigns(*lanes, tile.header.signs);
      clearObjects(*lanes, tile.header.objects);
    }
    signsBesideRoad(tile.header.signs);
    props->add(tile);
    // (later: grass.add – game.ts:1195)
    signs->add(tile);
    std::vector<double> discs;
    for (const SignPost& s : tile.header.signs) discs.insert(discs.end(), {s.x, s.y, s.z, 0.06});
    for (const TileObject& o : tile.header.objects)
      if (o.k == "lamp") discs.insert(discs.end(), {o.x, o.y, o.z, 0.09});
    const auto f32 = [&](const char* name) -> const std::vector<float>* {
      const SectionArray* a = tile.data.get(name);
      return a ? &a->f32 : nullptr;
    };
    const auto u16 = [&](const char* name) -> const std::vector<uint16_t>* {
      const SectionArray* a = tile.data.get(name);
      return a ? &a->u16 : nullptr;
    };
    const auto u8 = [&](const char* name) -> const std::vector<uint8_t>* {
      const SectionArray* a = tile.data.get(name);
      return a ? &a->u8 : nullptr;
    };
    const std::vector<float>* trees = f32("trees");
    if (trees) {
      // only real trunks, and only where a car can plausibly get to
      for (size_t i = 0; i + 4 < trees->size(); i += 5) {
        const float kind = (*trees)[i + 4];
        if (kind == 2) continue;
        if (clear && postInLane(*lanes, (*trees)[i], (*trees)[i + 1], (*trees)[i + 2])) continue;
        discs.insert(discs.end(), {(*trees)[i], (*trees)[i + 1], (*trees)[i + 2], kind == 3 ? 0.12 : 0.22});
      }
    }
    const std::vector<float>* rings = f32("collPts");
    const std::vector<uint16_t>* ringCounts = u16("collCnt");
    const std::vector<float>* segments = f32("collSeg");
    if (clear) {
      const std::vector<float> walls = clearWalls(*lanes, rings, ringCounts, segments);
      collider->add({.key = tile.key, .rings = nullptr, .ringCounts = nullptr, .ringSkip = nullptr, .segments = &walls, .discs = discs});
    } else collider->add({.key = tile.key, .rings = rings, .ringCounts = ringCounts, .ringSkip = u8("collSkip"), .segments = segments, .discs = discs});
  } else {
    props->remove(tile);
    // (later: grass.remove – game.ts:1217)
    signs->remove(tile);
    collider->remove(tile.key);
  }
}

/** Language of the page: for the browser (hyphenation, fonts, screen readers) and the writing direction. */
void Game::applyLanguage() {
  const std::string& lang = settings.lang;
  // the full tag (zh-CN, ja-JP …) also tells the browser which glyphs to take for Han characters
  dom::root().lang(localeOf(lang));
  ui.dir(isRtl(lang) ? "rtl" : "ltr");
  dom::document().title(tr("pageTitle", lang));
}

void Game::applySettings() {
  Settings& s = settings;
  App& app = this->app;
  applyLanguage();
  // The graphics: what the preset (or the advanced settings) chose – and, apart from it, the
  // resolutions: how sharp on a sharp screen, which share of it the 3D picture takes, how big the interface is.
  const Graphics& gx = s.graphics;
  app.pixelRatioCap = s.pixelCap;
  app.renderScale = s.render3d;
  app.setShadows(gx.shadows == "off" ? 0 : gx.shadows == "near" ? 1536 : 3072, gx.shadows == "far" ? 120 : 55);
  app.tiles->viewRadius = gx.viewDistance;
  app.tiles->setFacades(gx.facades);
  // the ground beyond the tiles, as far as asked: then the haze lies over the far hills, not over the near streets
  const double farView = gx.farView;
  far->group->visible = farView > 0;
  far->radius = farView;
  far->inner = std::max(300.0, app.tiles->viewRadius - 200);
  app.sky.fog->near = app.tiles->viewRadius * (farView > 0 ? 0.6 : 0.3);
  app.sky.fog->far = farView > 0 ? std::max(app.tiles->viewRadius * 1.6, farView * 0.95) : app.tiles->viewRadius * 1.05;
  app.camera->far = std::max(4500.0, farView * 1.1);
  app.camera->updateProjectionMatrix();
  props->density = gx.propDensity;
  props->setShadows(gx.propShadows);
  // (later: grass, outdoors, services.dirtShown, railActors.detail – game.ts:1256)
  ui.style("zoom", jsStr(s.uiScale));
  app.resize();
  for (const std::unique_ptr<Mirror>& m : car.mirrors) m->setEnabled(s.mirrors != "off");
  // the player's vehicles in finer detail ("very high": render/cardetail.ts, render/fleetdetail.ts)
  car.setDetail(gx.vehicleDetail);
  car.setCabinOpacity(s.cabinOpacity);
  for (const auto& [key, model] : models) {
    if (model == &car) continue;
    for (const std::unique_ptr<Mirror>& m : model->mirrors) m->setEnabled(s.mirrors != "off");
    model->setCabinOpacity(s.cabinOpacity);
    model->setDetail(gx.vehicleDetail);
  }
  audio->setVolume(s.volume);
  // the music: off, now and then, or more often (made as it plays: audio/music.ts)
  audio->setMusic(s.music, s.musicVolume);
  if (traffic) traffic->targetCount = static_cast<int>(jsRound(s.traffic * 34));
  // (later: traffic.crowd.target = crowdTarget(…), the people on foot – game.ts:1283)
  // (later: rail.target, ringMax, far: trams and trains come with the traffic – game.ts:1285)
  // (later: applyDevMap – game.ts:1290)
}

/**
 * The first start: the region the player is in, read from their IP address
 * (ui/geoip.ts) – no permission asked, no prompt, the game simply opens where
 * one is.  Only when no region is chosen yet, and only when the place is in a
 * region that is not the one already loaded; otherwise nothing changes and the
 * site's own map loads as always.  Never throws, never blocks: a slow or
 * refusing service leaves the choice as it was.
 */
Task<void> Game::firstRegionByIp() {
  if (!settings.region.empty()) co_return;   // (a region is chosen: it stays)
  const std::optional<RegionIndex> index = co_await loadRegionIndex(plat::locationBase());
  if (!index || index->regions.empty()) co_return;
  const auto place = co_await ipPlace();
  const Region* region = ipRegion(&*index, place);
  if (!region || region->id == settings.region) co_return;
  settings.region = region->id;
  saveSettings(settings);
}

/**
 * The region chosen in the settings (settings: region): the one of regions.json,
 * ready to hand to loadWorld (the App fetches its files and its tiles by itself);
 * null when none is chosen, or the region is not there (the site's own map is
 * loaded then).
 */
Task<std::optional<Region>> Game::chosenRegion() {
  const std::string id = settings.region;
  if (id.empty()) co_return std::nullopt;
  const std::optional<RegionIndex> index = co_await loadRegionIndex(plat::locationBase());
  const Region* region = index ? regionById(&*index, id) : nullptr;
  if (!region) {
    plat::warn("[region] " + id + " is not in regions.json: the site's map is loaded");
    co_return std::nullopt;
  }
  co_return *region;
}

/**
 * Steering by tilting the phone (settings: tiltSteer): on, the phone's left/right
 * tilt drives input.touch.steer instead of the on-screen bar; off, it stops and
 * centres the steer.  `on` is asked for from a gesture (the settings toggle), where
 * iOS asks its permission (ui/tiltsteer.ts).
 */
Task<void> Game::setTiltSteer(bool on) {
  if (!on) {
    if (tilt) tilt->stop();
    tilt = nullptr;
    // (the tilt was the control: let go of it – the screen or the keys take over again)
    input->untouched();
    settings.tiltSteer = false;
    co_return;
  }
  if (tilt) co_return;
  std::unique_ptr<TiltSteer> t = std::make_unique<TiltSteer>([this](double steer) { input->touch.steer = steer; });
  const bool ok = co_await t->start();
  if (!ok) {
    // (no sensors, or the permission refused: the setting goes back off)
    settings.tiltSteer = false;
    co_return;
  }
  tilt = std::move(t);
  // (the tilt is the control now: the input reads touch.steer while it is on, without a touch)
  input->touched();
  settings.tiltSteer = true;
}

void Game::toMenu() {
  // (later: theory.close, endWatch – game.ts:1457)
  afterPlace = nullptr;
  // (a tour that is left for the menu is kept as far as it was driven)
  if (demo && demo->tour) endDemo("ended");
  state = "menu";
  demo = nullptr;
  endTasks();
  // (later: services.end – game.ts:1464)
  if (crane) crane->dispose();
  crane = nullptr;
  // (later: endForklift, figure.hide, spawnCars.clear, intoParked, previewKey, showOn – game.ts:1468)
  car.root->visible = false;
  for (const auto& [key, model] : models) model->root->visible = false;
  if (carTrailer) carTrailer->group->visible = false;
  if (truck) {
    truck->trailer->visible = false;
  }
  input->enabled = false;
  hud->setHudVisible(false);
  audio->suspend();
  hud->showMenu(app.world->json.spawns);
  // once after an update: what is new (to whoever played an earlier version)
  if (showNewsOnce) {
    showNewsOnce = false;
    hud->showNews([this] { hud->showMenu(app.world->json.spawns); });
  }
  if (!spawn) {
    // show the town behind the menu
    const std::vector<SpawnJson>& spawns = app.world->json.spawns;
    if (!spawns.empty()) menuFocus.set(spawns[0].x, 0, spawns[0].z);
  }
}

/** The model of a vehicle, made the first time it is needed (and the box trailer behind a car, a 7.5-tonner or a bus). */
VehicleModel& Game::ensureModel(const VehicleDef& def) {
  VehicleModel* m = nullptr;
  const auto it = models.find(modelKey(def));
  if (it != models.end()) m = it->second;
  if (!m) {
    m = makeModel(def);
    models[modelKey(def)] = m;
    app.scene->add(m->root);
    if (truck && m == truck) app.scene->add(truck->trailer);
    for (const std::unique_ptr<Mirror>& mirror : m->mirrors) mirror->setEnabled(settings.mirrors != "off");
    m->setCabinOpacity(settings.cabinOpacity);
    m->setDetail(settings.graphics.vehicleDetail);
  }
  // (the truck of class CE has its own trailer)
  // (and the semi-trailer of the articulated lorry its own: render/semitruck.ts)
  const std::shared_ptr<TowedModel> towed = def.trailer && def.model != "truck" ? towedModel(def.trailer, carTrailer) : carTrailer;
  if (towed != carTrailer) {
    if (carTrailer) app.scene->remove(carTrailer->group);
    carTrailer = towed;
    if (towed) app.scene->add(towed->group);
  }
  return *m;
}

/** A vehicle standing with its engine off: wheels at rest, lamps dark, the handbrake on. */
void Game::parkedVisual(Vehicle& v) {
  CarVisualState& s = visual;
  s.speedKmh = 0;
  s.rpm = 0;
  s.indicator = 0;
  s.hazard = false;
  s.blinkOn = false;
  s.lowBeam = false;
  s.highBeam = false;
  s.handbrake = true;
  s.ignition = false;
  s.engineRunning = false;
  s.abs = false;
  s.steerWheel = 0;
  v.getWheelHeights(wheelY.data());
  for (int i = 0; i < 4; i++) {
    s.wheelSteer[i] = 0;
    s.wheelSpin[i] = 0;
    s.wheelY[i] = wheelY[i];
  }
  s.clutch = 0;
  s.brake = 0;
  s.throttle = 0;
  s.handbrakeLever = 1;
  s.braking = false;
  s.reversing = false;
}

/** The model a kind of vehicle is drawn with – every kind named (a new one cannot end up as somebody else's). */
VehicleModel* Game::makeModel(const VehicleDef& def) {
  const ModelKind& mk = def.model;
  if (mk == "car") return &car;
  std::unique_ptr<VehicleModel> made;
  if (mk == "truck") {
    auto t = std::make_unique<PlayerTruck>();
    truck = t.get();
    made = std::move(t);
  } else if (mk == "truckLight") made = std::make_unique<PlayerTruck>(LIGHT_TRUCK);
  else if (mk == "truckHeavy") made = std::make_unique<PlayerRigid>();
  else if (mk == "semi") made = std::make_unique<PlayerSemi>();
  else if (mk == "bus") made = std::make_unique<PlayerBus>();
  else if (mk == "minibus") made = std::make_unique<PlayerBus>(MINIBUS);
  else if (mk == "tractor") made = std::make_unique<PlayerTractor>();
  else if (mk == "towerCrane" || mk == "mobileCrane" || mk == "loaderCrane") {
    craneModels.push_back(makeCraneModel(def.crane.value_or("tower"), settings.quality == "low" ? "low" : "full"));
    return craneModels.back().get();
  }
  // (later: tram – TramCab, render/fleet.h, with the tram's drive; walker, plane, motorcycle, forklift – game.ts:1906)
  if (made) {
    vehicleModels.push_back(std::move(made));
    return vehicleModels.back().get();
  }
  throw std::runtime_error("no model for the vehicle " + def.kind + " (" + mk + ") in this build");
}

/** The vehicle the player sits in: the car or the truck. */
VehicleModel& Game::model() {
  const auto it = models.find(modelKey(*def));
  return it != models.end() ? *it->second : car;
}

void Game::showModel(bool on) {
  car.root->visible = on && def->model == "car";
  const std::string key = modelKey(*def);
  // (later: spawnCars.keeps, the vehicle left standing stays drawn – game/spawncar.ts, game.ts:1937)
  for (const auto& [k, model] : models)
    if (model != &car) model->root->visible = on && k == key;
  if (carTrailer) carTrailer->group->visible = on && def->trailer && def->model != "truck";
  if (truck) {
    truck->trailer->visible = on && def->kind == "truck";
  }
}

}  // namespace fs
