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

#include "geo/utm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fs {

namespace {
constexpr double PHYS_DT = 1.0 / 240.0;
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
           SignSource& signSource, TileSource& tileSource, int canvasWidth, int canvasHeight)
    : settings(loadSettings()),
      app(canvasWidth, canvasHeight),
      audio(audioBackend),
      host_(host),
      signSource_(signSource),
      tileSource_(tileSource),
      platform_(platform) {}


std::string Game::t(const std::string& key) const {
  Lang l = settings.lang == LangSetting::En ? Lang::En : Lang::De;
  return fs::tr(key, l);
}

// ---------------------------------------------------------------- addresses
/** The address list (house numbers of the map), read when somebody searches. */
AddressIndex* Game::loadAddresses() {
  if (addressAsked_) return addressIndex_.get();
  addressAsked_ = true;
  // the original fetches `map/addresses.json` here; the fetch layer lives in
  // the platform, so the same code runs in the browser and in tests
  addressIndex_.reset();   // no backend yet: nothing to search (see README)
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

/** Free drive: jump to an address (from the pause menu). */
void Game::teleportTo(const AddressHit& hit) {
  if (state != GameState::Pause || hasLesson_) return;
  SpawnJson spot;
  if (!spawnAt(hit, spot)) {
    hud->showPause();
    Lang l = settings.lang == LangSetting::En ? Lang::En : Lang::De;
    hud->message(fs::tr("addressNoRoad", l), hit.label);
    return;
  }
  spawn_ = spot;
  hasSpawn_ = true;
  traffic->clear();
  vehicle->vel[0] = vehicle->vel[1] = vehicle->vel[2] = 0;
  vehicle->angVel[0] = vehicle->angVel[1] = vehicle->angVel[2] = 0;
  input->c.handbrake = 1;
  state = GameState::Placing;
  car.root.visible = false;
  Lang l = settings.lang == LangSetting::En ? Lang::En : Lang::De;
  hud->showLoading(fs::tr("loadingTiles", l));
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
  a.pixelRatioCap = q == Quality::Low ? 1 : (q == Quality::Medium ? 1.5 : 2);
  a.tiles->viewRadius = q == Quality::Low ? 850 : (q == Quality::Medium ? 1250 : 1700);
  a.sky.fog->near = a.tiles->viewRadius * 0.3;
  a.sky.fog->far = a.tiles->viewRadius * 1.05;
  props.density = q == Quality::Low ? 0.45 : (q == Quality::Medium ? 0.8 : 1);
  a.resize(0, 0);
  audio.setVolume(s.volume);
  if (traffic) traffic->targetCount = static_cast<int>(std::lround(s.traffic * 34));
}

void Game::toMenu() {
  state = GameState::Menu;
  if (input) input->enabled = false;
  hud->setHudVisible(false);
  audio.suspend();
  int last = 0;
  if (hasSpawn_)
    for (size_t i = 0; i < worldJson_.spawns.size(); i++)
      if (worldJson_.spawns[i].name == spawn_.name) last = static_cast<int>(i);
  hud->showMenu(worldJson_.spawns, last);
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
  const VehicleSpec& spec = settings.transmission == Transmission::Automatic
                                ? compactCarAutomatic()
                                : (settings.transmission == Transmission::Assist
                                       ? compactCarAutoclutch()
                                       : compactCar());
  vehicle = std::make_unique<Vehicle>(spec, *ground);
  input->setAutomatic(spec.transmission == "automatic");
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
  nav->clear();
  targetIdx_ = 0;
  basicsStep_ = 0;
  visited_.clear();
  failT_ = -1;
  homeEdge_ = nullptr;
  state = GameState::Placing;
  car.root.visible = false;
  hud->showLoading(t("loadingTiles"));
}

void Game::tryPlace() {
  const SpawnJson& sp = spawn_;
  double ty = app.tiles->heightAt(sp.x, sp.z);
  if (std::isnan(ty) || !app.tiles->settled()) return;
  double y = world_->surfaceAt(sp.x, sp.z, ty + 3, hit_) ? hit_.y : ty;
  vehicle->teleport(sp.x, y + 0.02, sp.z, sp.yaw);
  lastGood_ = {sp.x, y, sp.z, sp.yaw, 0};
  car.root.visible = true;
  startOdo_ = vehicle->odometer;
  startTime_ = time_;
  resume();
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
  hud->showPause();
}

void Game::finish() {
  bool exam = hasLesson_ && lesson_.kind == "exam";
  bool failed = rules->failed() || rules->points() >= 10;
  state = GameState::Result;
  input->enabled = false;
  audio.suspend();
  hud->setHudVisible(false);
  int li = settings.lang == LangSetting::De ? 0 : 1;
  ResultSummary r;
  r.title = hasLesson_ ? (li == 0 ? lesson_.title.first : lesson_.title.second) : t("free");
  r.exam = exam;
  r.passed = !failed;
  r.log = rules->log;
  r.points = rules->points();
  r.distanceKm = (vehicle->odometer - startOdo_) / 1000;
  r.minutes = (time_ - startTime_) / 60;
  hud->showResult(r);
}

void Game::onAction(Action a) {
  if (a == Action::Pause) {
    if (state == GameState::Drive) pause();
    else if (state == GameState::Pause) resume();
  } else if (a == Action::View && state == GameState::Drive) {
    cameraMode = cameraMode == CameraMode::Cockpit ? CameraMode::Chase : CameraMode::Cockpit;
  } else if (a == Action::Reset && state == GameState::Drive) {
    resetToRoad();
  } else if (a == Action::Help) {
    if (state == GameState::Drive) {
      state = GameState::Pause;
      input->enabled = false;
      hud->showControls([this]() { resume(); });
    }
  } else if (a == Action::Map && state == GameState::Drive && !hasLesson_) {
    finish();
  }
}

void Game::resetToRoad() {
  const GoodSpot& g = lastGood_;
  vehicle->teleport(g.x, g.y + 0.05, g.z, g.yaw);
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
    acc_ -= PHYS_DT;
    if (v.stalledEvent) stalled = true;
    if (v.gearGrind) grind = true;
    double hit = collider.resolve(v);
    if (hit > impact_) impact_ = hit;
  }
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
  PlayerState player;
  player.x = mpos_[0];
  player.y = mpos_[1];
  player.z = mpos_[2];
  player.yaw = v.yaw;
  player.speed = v.speed;
  player.length = 4.28;
  player.width = 1.79;
  signals->update(dt);
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
  snap.lookYaw = c.lookYaw;
  snap.seatbelt = c.seatbelt;
  snap.handbrake = c.handbrake;
  snap.stalled = stalled;
  snap.hitScenery = impact;
  snap.length = 4.28;
  rules->update(dt, snap, events);
  rememberGoodSpot(player);
  updateLesson(dt, player);
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
    bool de = settings.lang == LangSetting::De;
    struct Step {
      std::string text;
      std::function<bool()> done;
    };
    std::vector<Step> steps = {
        {de ? "Anschnallen: B druecken" : "Fasten your seat belt: press B",
         [&]() { return c.seatbelt; }},
        {de ? (auto_ ? "Bremse treten (S) und Motor starten: Enter halten"
                     : "Motor starten: Enter halten (Gang raus oder Kupplung treten)")
            : (auto_ ? "Hold the brake (S) and start the engine: hold Enter"
                     : "Start the engine: hold Enter (neutral or clutch pressed)"),
         [&]() { return v->engineRunning; }},
        {de ? (auto_ ? "Waehlhebel auf D: E druecken" : "1. Gang einlegen: Taste 1")
            : (auto_ ? "Select D: press E" : "Select 1st gear: key 1"),
         [&]() { return auto_ ? c.gear == 1 : v->gear == 1; }},
        {de ? "Handbremse loesen: Leertaste" : "Release the handbrake: Space",
         [&]() { return c.handbrake < 0.5; }},
        {de ? "Sanft anfahren: etwas Gas (W), bis 15 km/h"
            : "Pull away gently: a little throttle (W) up to 15 km/h",
         [&]() { return v->speed * 3.6 > 15; }},
        {de ? (auto_ ? "Weiter beschleunigen bis 30 km/h"
                     : "In den 2. Gang schalten (E) und bis 30 km/h beschleunigen")
            : (auto_ ? "Accelerate to 30 km/h" : "Change up to 2nd (E) and accelerate to 30 km/h"),
         [&]() { return v->speed * 3.6 > 30 && (auto_ || v->gear >= 2); }},
        {de ? "Bis zum Stillstand bremsen (S)" : "Brake to a standstill (S)",
         [&]() { return std::fabs(v->speed) < 0.1; }},
        {de ? "Rueckwaertsgang: R - und ein Stueck zuruecksetzen"
            : "Reverse gear: R - and back up a little",
         [&]() { return v->speed < -1; }},
        {de ? "Anhalten und Handbremse anziehen (Leertaste)"
            : "Stop and apply the handbrake (Space)",
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
}

void Game::boot(const QueryParams& q) {
  App& a = app;
  // the world JSON comes from the platform (the original fetches world.json)
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
  a.scene.addRef(&signalHeads->group);
  car.root.visible = false;

  hud = std::make_unique<Hud>(host_, settings, *a.world);
  input = std::make_unique<Input>(platform_, settings);
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
  hud->attachInput(*input);
  rules->onViolation = [this](const Violation& v) {
    hud->toast(v);
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
  double dt = std::min(0.1, (now - last_) / 1000.0);
  last_ = now;
  time_ += dt;
  frames_++;
  App& a = app;
  TileManager* tiles = a.tiles.get();

  if (state == GameState::Placing) {
    if (hasSpawn_) tiles->update(spawn_.x, spawn_.z);
    tryPlace();
  } else if (state == GameState::Drive) {
    stepDrive(dt);
  }

  bool haveCar = vehicle != nullptr && car.root.visible;
  if (haveCar) {
    Vehicle& v = *vehicle;
    v.getModelPosition(mpos_);
    car.root.position.set(mpos_[0], mpos_[1], mpos_[2]);
    car.root.quaternion.set(v.quat[0], v.quat[1], v.quat[2], v.quat[3]);
    car.root.updateMatrixWorld(true);
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
  signs->flush();
  signalHeads->update(*signals);
  actors.update(traffic->cars, traffic->peds, time_);

  if (haveCar) {
    updateCamera(dt);
    Mirrors mode = settings.mirrors;
    if (cameraMode == CameraMode::Cockpit && mode != Mirrors::Off) {
      double far = std::min(450.0, a.tiles->viewRadius * 0.4);
      (void)far;
      (void)mirrorTurn_;
    }
    a.render(car.root.position);
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
  s.brake = c.brake;
  s.throttle = c.throttle;
  s.handbrakeLever = c.handbrake;
  s.braking = c.brake > 0.03;
  s.reversing = v.gear == -1;
  car.update(s, dt, time_);

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
    Lang l = settings.lang == LangSetting::En ? Lang::En : Lang::De;
    std::string what = h.what == "navRoundabout"
                           ? fs::tr("navRoundabout", l, {{"n", std::to_string(h.n)}})
                           : fs::tr(h.what, l);
    if (h.what == "navFollow" || h.dist < 0)
      navText = fs::tr("navFollow", l);
    else if (h.dist < 35)
      navText = fs::tr("navNow", l, {{"what", what}});
    else
      navText = fs::tr("navIn", l,
                       {{"d", std::to_string(std::lround(h.dist / 10) * 10)}, {"what", what}});
    speak(h.what, h.dist, navText);
    // § 9 StVO: get in lane in good time – the route goes on from a particular lane
    if (h.lane != 0)
      navText += " – " + fs::tr(h.lane < 0 ? "laneLeft" : "laneRight", l);
  }

  std::string tip;
  if (time_ < tipUntil_) tip = tipText_;
  if (tip.empty()) {
    Lang l = settings.lang == LangSetting::En ? Lang::En : Lang::De;
    bool stalledRecently = false;
    for (const auto& x : rules->log)
      if (x.id == "stall" && time_ - x.time < 6) stalledRecently = true;
    if (!v.engineRunning)
      tip = fs::tr(stalledRecently ? "engineStalled" : "startEngine", l);
    else if (!c.seatbelt && std::fabs(kmh) < 5)
      tip = fs::tr("fastenBelt", l);
    else if (c.handbrake > 0.5 && c.throttle > 0.1)
      tip = fs::tr("releaseHandbrake", l);
    else if (c.throttle > 0.1 && std::fabs(kmh) < 2 &&
             (s.automatic ? (c.gear == -2 || c.gear == 0) : v.gear == 0))
      tip = fs::tr(s.automatic ? "selectDrive" : "selectGear", l);
    else {
      Lane* lane = currentLane();
      if (lane && lane->to && lane->to->ctrl == CTRL_MAP_END &&
          lane->len - laneFront() < 120)
        tip = fs::tr("offMap", l);
      else if (!ctx.onRoad && std::fabs(kmh) < 3 && time_ - lastGood_.t > 6)
        tip = fs::tr("resetHint", l);
    }
  }

  std::string gearText;
  if (s.automatic)
    gearText = c.gear == -2 ? "P" : (c.gear == -1 ? "R" : (c.gear == 0 ? "N" : "D" + std::to_string(v.gear)));
  else
    gearText = v.gear == -1 ? "R" : (v.gear == 0 ? "N" : std::to_string(v.gear));

  HudState hs;
  hs.speedKmh = kmh;
  hs.rpm = v.rpm;
  hs.gear = gearText;
  hs.limit = ctx.limit;
  hs.zone = ctx.zone;
  hs.junction.set = ctx.junction.set;
  hs.junction.dist = ctx.junction.dist;
  hs.junction.role = ctx.junction.role;
  hs.junction.ctrl = ctx.junction.ctrl;
  hs.junction.signal = ctx.junction.signal;
  hs.navText = navText;
  hs.navSub = hasLesson_ && lesson_.kind != "basics" ? "" : ctx.roadName;
  hs.navArrow = h.arrow;
  hs.tip = tip;
  hs.showDash = cameraMode != CameraMode::Cockpit;
  hs.indicator = c.indicator;
  hs.hazard = c.hazard;
  hs.blinkOn = s.blinkOn;
  hs.points = rules->points();
  int faults = 0;
  for (const auto& x : rules->log)
    if (x.severity != Severity::Hint) faults++;
  hs.faults = faults;
  hs.x = mpos_[0];
  hs.z = mpos_[2];
  hs.yaw = v.yaw;
  if (navText.empty() && !hasLesson_) {
    Lang l = settings.lang == LangSetting::En ? Lang::En : Lang::De;
    hs.navText = ctx.roadName.empty()
                     ? fs::tr("free", l)
                     : ctx.roadName;
    hs.navSub = fs::tr("free", l);
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
  Object3D& carRoot = car.root;
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
    tmpQ_.setFromEuler(Euler(c.lookPitch, c.lookYaw, 0, Euler::YXZ));
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
