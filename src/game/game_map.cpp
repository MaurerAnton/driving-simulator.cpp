// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/game.ts @ v1.7.7
/**
 * The Game's map: the addresses, putting the car elsewhere (an address, a place chosen
 * on the map, a tap on the picture), the full-screen map, the flying camera, "drive
 * there" and "turn round".
 *
 * The methods are the original's, by its names and in its order:
 * loadAddresses, spawnAt, teleportTo, teleportSpot, openMap (game.ts:2133), closeMap (game.ts:2677),
 * flyFree, flyCamera, flyHold, canTeleport, canDrive, teleportHere, driveHere, turnAround,
 * canTurnAround (game.ts:5182).
 */
#include <cmath>
#include <memory>
#include <array>
#include <optional>
#include <string>
#include <utility>

#include "audio/audio.h"
#include "core/geom.h"
#include "core/js.h"
#include "core/task.h"
#include "engine/engine.h"
#include "game/address.h"
#include "game/arcadedrive.h"
#include "game/flycam.h"
#include "game/game.h"
#include "game/nav.h"
#include "game/settings.h"
#include "game/track.h"
#include "game/turnaround.h"
#include "game/vehicles.h"
#include "i18n/index.h"
#include "input/input.h"
#include "platform/plat.h"
#include "render/tiles.h"
#include "sim/jsmath.h"
#include "sim/rail.h"
#include "sim/rules.h"
#include "sim/traffic.h"
#include "sim/vehicle/index.h"
#include "ui/bigmap.h"
#include "ui/hud.h"
#include "ui/maplayers.h"
#include "ui/touchstick.h"
#include "ui/worldlayer.h"
#include "world/loader.h"
#include "world/regions.h"
#include "world/types.h"
#include "world/worldjson.h"
#include "world/world.h"

namespace fs {

namespace {

/** `{ name, x, z, yaw, kind: 'street' }`: a place on a street (`rough`: taken from the overview of the whole map) */
SpawnJson streetSpot(std::string name, double x, double z, double yaw, bool rough = false) {
  SpawnJson s;
  s.name = std::move(name);
  s.x = x;
  s.z = z;
  s.yaw = yaw;
  s.kind = "street";
  s.rough = rough;
  return s;
}

/**
 * 2.0: fetch the world layer: the outline and the regions' index of the site, and the site's own map shown
 * among them – its extent and name from `here` (the site's map is the one loaded), else from its map.json
 * (`mapBase`: where the site's map lies; a region is loaded instead of it).
 */
Task<void> loadWorldLayer(std::shared_ptr<WorldLayer> layer, std::string mapBase, std::optional<WorldMetaJson> here) {
  std::optional<WorldMetaJson> meta = std::move(here);
  if (!meta) {
    try {
      const plat::Response res = co_await plat::fetch(mapBase + "map.json");
      MapIndexJson index;
      if (res.ok() && parseMapIndexJson(res.text(), index)) meta = index.meta;
    } catch (...) {
      // (offline: the site's map is left out of the regions)
    }
  }
  const std::array<double, 4> box = meta ? std::array<double, 4>{meta->minX, meta->minZ, meta->maxX, meta->maxZ} : std::array<double, 4>{0, 0, 0, 0};
  co_await layer->load(plat::locationBase(), box, meta ? meta->name : std::string());
}

}  // namespace

/** The address list (house numbers of the map), fetched when somebody first searches. */
Task<AddressIndex*> Game::loadAddresses() {
  if (!addressIndex.valid()) {
    // (the index is kept in `addresses` for as long as the game runs: the task gives its address)
    addressIndex = [](std::shared_ptr<MapLoader> map, std::unique_ptr<AddressIndex>* keep) -> Task<AddressIndex*> {
      try {
        std::optional<AddressJson> a = co_await map->json<AddressJson>("addresses.json.gz");
        if (!a) a = co_await map->json<AddressJson>("addresses.json");
        if (!a) co_return nullptr;
        *keep = std::make_unique<AddressIndex>(*a);
        co_return keep->get();
      } catch (...) {
        co_return nullptr;
      }
    }(app.map, &addresses);
  }
  return addressIndex;
}

/** The spot on the road in front of an address, as a starting point. */
std::optional<SpawnJson> Game::spawnAt(const AddressHit& hit) {
  std::optional<SpawnJson> spot = roadSpot(hit.x, hit.z, hit.street, 220);
  if (spot) {
    spot->name = hit.label;
    return spot;
  }
  // Neither the roads there nor those of the whole map are read yet: the house itself, for now –
  // the car is put into a lane of its street once the roads around it are there (tryPlace).
  if (app.map->pieces && !netIndex && !app.map->ready(hit.x, hit.z, 280)) return streetSpot(hit.label, hit.x, hit.z, 0, true);
  return std::nullopt;
}

/** Free drive: jump to an address (from the pause menu). */
void Game::teleportTo(const AddressHit& hit) {
  if (state != "pause" || lesson) return;
  const std::optional<SpawnJson> spot = startSpot(hit);
  spawnStreet = hit.street;
  if (!spot) {
    hud->showPause();
    hud->message(tr("addressNoRoad", settings.lang), hit.label);
    return;
  }
  teleportSpot(*spot);
}

/** Free drive: put the car at a spot on a road (an address, or a place chosen on the map). */
void Game::teleportSpot(const SpawnJson& spot_) {
  // (not from a lesson, nor from the crane's construction site)
  // (later: crane, forklift – game.ts:2169)
  if (lesson || (state != "pause" && state != "map")) return;
  // (the spot may be the map's own, which closing the map clears: held here)
  const SpawnJson spot = spot_;
  bigMap->close();
  // (later: endWatch – game.ts:2171)
  if (demo) {
    demo = nullptr;
    nav->clear();
    hud->routeLanes.clear();
    rules->allowed = nullptr;
  }
  spawn = spot;
  traffic->clear();
  // (later: rail.clear – game.ts:2180)
  // (later: tram – the tram put on the tracks anew at the stop nearest to the place chosen – game.ts:2181)
  // on foot: on the pavement there (and nobody rides a tram or a bus any more) – somebody who got out of the
  // vehicle gets out again beside it there (before, they stayed behind, tied to a vehicle far away)
  // (later: walker, leaveWalk, footAfterJump – game.ts:2192)
  // (later: riding, busRide, railActors, passengerView – game.ts:2196)
  vehicle->vel.fill(0);
  vehicle->angVel.fill(0);
  // a teleport in a free drive lands ready to go: in gear (D1 on an automatic),
  // no handbrake, held still until the driver gives gas.  A lesson keeps its
  // programme (handbrake on), and a tram has none of that.
  const bool tpFree = !lesson && def->kind != "tram";
  spawnHold = tpFree;
  input->c.handbrake = tpFree ? 0 : def->kind == "tram" ? 0 : 1;
  if (tpFree && arcadeTransmission(settings.arcadeDrive, settings.transmission) == "automatic") input->c.gear = 1; // (D1)
  path->breakLine();
  keepClock = true;
  state = "placing";
  showModel(false);
  hud->setHudVisible(true);
  hud->showLoading(tr("loadingTiles", settings.lang));
}

// ------------------------------------------------------------------ map
/** The full-screen map: where am I, where have I been – and, in a free drive, put the car elsewhere. */
void Game::openMap(bool closeUp) {
  wantNet();
  // (later: reality – from the photographs to the map – game.ts:2221)
  if (state != "drive" && state != "pause" && state != "menu") return;
  // (from the main menu: the map round the place the drive would begin – the vehicle shown there is put away meanwhile)
  // (later: menuPreview, clearPreview – game.ts:2228)
  mapFrom = state;
  state = "map";
  input->enabled = false;
  audio->suspend();
  hud->hideOverlay();
  hud->setHudVisible(false);
  if (mapFrom == "menu") {
    mpos[0] = menuFocus.x;
    mpos[2] = menuFocus.z;
  } else vehicle->getModelPosition(mpos.data());
  // 2.0: one map for all regions – the layer of every region (the countries' outlines, regions.json), made
  //  once and handed to the map; its "Open this map" (BigMapHost::onOpenRegion: settings.region kept, the
  //  page loaded anew) is given where the map is made (game.cpp)
  if (!worldLayer) {
    worldLayer = std::make_shared<WorldLayer>();
    std::optional<WorldMetaJson> here;
    if (app.regionId.empty()) here = app.world->json.meta;
    loadWorldLayer(worldLayer, app.baseUrl, std::move(here)).detach();
    bigMap->setWorld(worldLayer);
  }
  bigMap->open(ui, closeUp);
}

void Game::closeMap() {
  if (state != "map") return;
  // (a place that was to be picked for a photo: not any more)
  extraLayers.pick = nullptr;
  bigMap->close();
  if (mapFrom == "menu") {
    // (back to the main menu: it places the vehicle anew – where it stood, or where it was chosen on the map)
    state = "menu";
    hud->showMenu(app.world->json.spawns);
  } else if (mapFrom == "pause") {
    state = "pause";
    hud->setHudVisible(true);
    hud->showPause();
  } else resume();
}

// ------------------------------------------------- the flying camera, a tap to teleport (1.7.0)
// (game/flycam.ts, ui/touchstick.ts, ui/worldtap.ts)

/** The free camera flies, apart from the vehicle (not from the aeroplane, the tram or the forklift, nor on foot or watching: there it goes with them as before). */
bool Game::flyFree() {
  // (later: walker, watcher, aircraft, tram, forklift, crane, doorT – game.ts:5183)
  return cameraMode == "free" && state == "drive";
}

/** The flying camera (game/flycam.ts): from where the camera was – or from behind the vehicle – wherever it is flown. */
void Game::flyCamera(Camera& cam, double dt) {
  cam.near = 0.3;
  cam.fov = 55;
  FlyCam& fl = *fly;
  const Input::Cam& t = input->takeCam();
  const Vector3& car = model().root->position;
  if (!fl.ready && cam.position.distanceTo(car) > 2.5 && cam.position.distanceTo(car) < 400) {
    const Vector3& dir = cam.getWorldDirection(tmpV);
    fl.start(cam.position.x, cam.position.y, cam.position.z, dir.x, dir.y, dir.z);
  }
  if (!fl.ready || t.reset) {
    // (from the driver's seat, or a double click: behind the vehicle, a little above it, looking along it)
    const double yaw = vehicle->yaw;
    const double back = def->long_ ? 20 : 8;
    const double p = -0.28;
    fl.start(car.x + jsSin(yaw) * back, car.y + (def->long_ ? 7 : 3.2), car.z + jsCos(yaw) * back, -jsSin(yaw) * jsCos(p), jsSin(p), -jsCos(yaw) * jsCos(p));
  }
  // (what the camera was asked, as the flying camera reads it)
  const FlyInput asked{.yaw = t.yaw, .pitch = t.pitch, .right = t.right, .fwd = t.fwd, .keyFwd = t.keyFwd, .keyRight = t.keyRight, .keyUp = t.keyUp, .fast = t.fast, .lift = t.lift, .speed = t.speed};
  if (fl.update(dt, asked, [this](double x, double z) { return app.tiles->heightAt(x, z); })) {
    if (touchStick) touchStick->badge(tr("fly.speed", settings.lang, {{"v", jsRound(fl.speed() * 3.6)}}));
  }
  cam.position.set(fl.x, fl.y, fl.z);
  cam.quaternion.setFromEuler(Euler(fl.pitch, fl.yaw, 0, Euler::YXZ));
  camSnap = false;
}

/** Is the vehicle to stand still for the flying camera (see flyParked)? */
bool Game::flyHold() {
  const double d = jsHypot(fly->x - mpos[0], fly->z - mpos[2]);
  const bool far = flyFree() && fly->ready && ((std::fabs(vehicle->speed) < 0.5 && d > 250) || d > 600);
  if (far) flyParked = true;
  else if (flyParked && !std::isnan(app.tiles->heightAt(mpos[0], mpos[2])) && app.map->ready(mpos[0], mpos[2], 60)) flyParked = false;
  return flyParked;
}

/** May the player be teleported now (ui/worldtap.ts)?  In a drive of one's own, in a vehicle on the road or on foot. */
bool Game::canTeleport() {
  // (later: task, watcher, showOn – game.ts:5285)
  if (!settings.teleportTap || state != "drive" || lesson || (demo && demo->active)) return false;
  // (later: doorT, opens, riding, busRide, wrecked – game.ts:5286)
  // (later: walker – game.ts:5287)
  // (later: aircraft, tram, forklift, crane – game.ts:5288)
  return true;
}

/**
 * May the vehicle be sent off to a place tapped on the picture (ui/worldtap.ts "Drive there")?
 * The same as canTeleport, but for what can drive itself: the aeroplane flies there, a tram,
 * a forklift and a crane never do.  On foot it is allowed too – startTrip puts the walker
 * back into the seat first.
 */
bool Game::canDrive() {
  // (later: task, watcher, showOn – game.ts:5298)
  if (!settings.teleportTap || state != "drive" || lesson || (demo && demo->active)) return false;
  // (later: doorT, opens, riding, busRide, wrecked – game.ts:5299)
  // (later: walker – game.ts:5300)
  // (later: tram, forklift, crane – game.ts:5301)
  return true;
}

/** "Teleport here": somebody on foot to that very place; a vehicle into the lane of the road nearest it, facing along it (the way the camera looked, where it may). */
void Game::teleportHere(double x, double y, double z, double yaw) {
  if (!canTeleport()) return;
  // (later: walker – somebody on foot is put at that very place, walkCtl – game.ts:5307)
  std::optional<Spot> spot = freeSpot(x, z, yaw, 0);
  if (!spot || jsHypot(spot->x - x, spot->z - z) > 30) {
    // (no lane within a few metres: the road nearest within 150 m)
    const std::optional<SpawnJson> road = roadSpot(x, z, "", 150);
    if (road && !road->rough) {
      spot = freeSpot(road->x, road->z, road->yaw, 0);
      if (!spot) spot = Spot{.x = road->x, .y = footGround(road->x, road->z, app.tiles->heightAt(road->x, road->z) + 3), .z = road->z, .yaw = road->yaw};
    } else spot = std::nullopt;
  }
  if (!spot || !std::isfinite(spot->y)) {
    tipText = tr("teleportNoRoad", settings.lang);
    tipUntil = time + 4;
    return;
  }
  putOnRoad(*spot);
  path->breakLine();
  camSnap = true;
}

/**
 * "Drive there" (ui/worldtap.ts): the vehicle drives to the place tapped on the picture by
 * itself, the way the big map's "drive there" does (startTrip).  The aeroplane flies there
 * and circles it; a car or a lorry goes to the nearest road there.  Takes over again at
 * once as ever: a pedal or the wheel brings the player back.
 */
void Game::driveHere(double x, double z) {
  if (!canDrive()) return;
  const std::string lang = settings.lang;
  // (later: aircraft – the aeroplane needs no road: startTrip flies it there and circles the place – game.ts:5343)
  const std::optional<SpawnJson> road = roadSpot(x, z, "", 150);
  if (!road) {
    tipText = tr("teleportNoRoad", lang);
    tipUntil = time + 4;
    return;
  }
  startTrip({streetSpot("", road->x, road->z, road->yaw)}, false, nullptr);
}

/** Turn the vehicle round (input action 'turn'): into the lane of the other direction where there is one. */
void Game::turnAround() {
  if (!canTurnAround()) return;
  Vehicle& v = *vehicle;
  v.getModelPosition(mpos.data());
  const TurnSpot here{.x = mpos[0], .y = mpos[1], .z = mpos[2], .yaw = v.yaw};
  // (the lane the other way round, where there is one – else the vehicle turns where it stands)
  const std::optional<Spot> found = freeSpot(here.x, here.z, here.yaw + PI, 0);
  const TurnSpot spot = turnedSpot(here, found ? std::optional<TurnSpot>(TurnSpot{.x = found->x, .y = found->y, .z = found->z, .yaw = found->yaw}) : std::nullopt);
  putOnRoad(Spot{.x = spot.x, .y = spot.y, .z = spot.z, .yaw = spot.yaw});
  path->breakLine();
  camSnap = true;
  audio->click("tick");
}

/** May the vehicle be turned round now (input action 'turn', F3)?  A road vehicle while driving. */
bool Game::canTurnAround() {
  // (later: task, watcher, showOn – game.ts:5373)
  if (state != "drive" || lesson || (demo && demo->active)) return false;
  // (later: doorT, opens, riding, busRide – game.ts:5374)
  // not on foot, and not in what is driven by hand or by an operator: the aeroplane, the tram, the forklift, the crane
  // (later: walker, aircraft, tram, forklift, crane – game.ts:5376)
  return true;
}

}  // namespace fs
