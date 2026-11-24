// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The autopilot on the real map, with the whole game around it (a smoke test on the native host):
 * the game is booted into a free drive, the car is sent to a place some kilometres away the way a
 * tap on the picture sends it ("Drive there": Game::driveHere → roadSpot → startTrip), and the
 * game is stepped picture by picture until the car has followed its route for a kilometre –
 * on the carriageway, in its lane, without being put back onto the road by the drive's safety net
 * (updateDemo's rescue) and without standing still for long.
 *
 *   tools/t.sh autopilot_drive_test [--map DIR] [--metres 1000] [--frames-per-second 20] [--traffic 0]
 *
 * It needs every layer of the game (no "LINK" line: it links them all) and a graphics context,
 * as tools/dev/view.cpp does.  The map: /home/user/driving-simulator/public/map unless --map says another.
 *
 * (New with the port: the original has no test that runs the game itself; its tests/drive.sim.ts
 * drives the lessons with the same driver outside the game – tests/trips_test.cpp mirrors that.)
 */
#include <filesystem>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/geom.h"
#include "game/game.h"
#include "game/nav.h"
#include "platform/dom.h"
#include "platform/glhost.h"
#include "platform/native/native.h"
#include "platform/plat.h"
#include "sim/autodriver.h"
#include "sim/rules.h"
#include "sim/vehicle/index.h"
#include "ui/hud.h"
#include "world/lanes.h"
#include "world/types.h"
#include "world/world.h"

using namespace fs;

int main(int argc, char** argv) {
  std::string mapDir = "/home/user/driving-simulator/public/map/";
  double metres = 1000;
  double fps = 20;
  std::string traffic = "0";
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--map") mapDir = next();
    else if (a == "--metres") metres = std::atof(next());
    else if (a == "--frames-per-second") fps = std::atof(next());
    else if (a == "--traffic") traffic = next();
  }
  if (!mapDir.empty() && mapDir.back() != '/') mapDir += '/';
  // the page's own files (the languages, the outlines) from the repository, the map from its folder
  // (the real map is only on the machine the port is made on: elsewhere – CI – nothing to test)
  if (!std::filesystem::exists(mapDir + "map.json")) {
    std::printf("SKIP: no map at %s\n", mapDir.c_str());
    return 0;
  }
  plat::native::mount(plat::locationBase(), "web/");
  plat::native::mount(plat::locationBase() + "map/", mapDir);
  const int width = 640;
  const int height = 360;
  plat::native::setViewport(width, height, 1);
  if (!plat::gl::createContext("autopilot_drive_test", width, height)) {
    std::fprintf(stderr, "autopilot_drive_test: no OpenGL ES here\n");
    return 1;
  }

  // ---- the game, into a free drive at the map's first start place (no other traffic unless asked for; the lights work)
  Game game(dom::el("canvas"), dom::el("div"));
  plat::native::wait(game.boot(URLSearchParams("?start=free&lang=en&quality=low&traffic=" + traffic)));
  // (the test asks for the pictures itself)
  game.looping = false;
  double now = game.last;
  const double frameDt = 1 / fps;
  const auto frame = [&] {
    now += frameDt * 1000;
    game.frame(now);
    plat::native::pump();
  };
  for (int i = 0; i < 40000 && game.state != "drive"; i++) frame();
  std::printf("state '%s' after %.0f s of the game's time\n", game.state.c_str(), game.time);
  assert(game.state == "drive");   // the car was placed and the drive began
  assert(game.vehicle);

  // ---- the roads of the whole map, for a way that leads beyond what is loaded
  game.wantNet();
  for (int i = 0; i < 40000 && !game.routeGraphNow(); i++) frame();
  assert(game.routeGraphNow());   // the road network for planning ways is there

  // ---- a place some kilometres away: the start place of the map that lies nearest to 3 km from the car
  double pos[3] = {0, 0, 0};
  game.vehicle->getModelPosition(pos);
  const double x0 = pos[0];
  const double z0 = pos[2];
  std::vector<SpawnJson> places = game.app.world->json.spawns;
  std::stable_sort(places.begin(), places.end(), [&](const SpawnJson& a, const SpawnJson& b) { return std::fabs(jsHypot(a.x - x0, a.z - z0) - 3000) < std::fabs(jsHypot(b.x - x0, b.z - z0) - 3000); });
  std::optional<SpawnJson> to;
  for (const SpawnJson& p : places) {
    const double d = jsHypot(p.x - x0, p.z - z0);
    if (d < 2000 || d > 8000) continue;
    // (what "Drive there" asks: a road within 150 m of the place)
    if (!game.roadSpot(p.x, p.z, "", 150)) continue;
    to = p;
    break;
  }
  assert(to);   // the map has a place to drive to
  std::printf("from (%.0f, %.0f) to '%s' (%.0f, %.0f), %.1f km away\n", x0, z0, to->name.c_str(), to->x, to->z, jsHypot(to->x - x0, to->z - z0) / 1000);

  // ---- "Drive there"
  assert(game.canDrive());
  game.driveHere(to->x, to->z);
  assert(game.demo);   // a drive to places is under way,
  assert(game.demo->active && game.demo->driver);   // by itself, with its driver
  assert(game.demo->stops.size() == 1);
  assert(game.autopilotDriving());
  assert(game.vehicle->spec.transmission == "automatic");   // (the driver has no hand for a gear lever)
  assert(!game.canTurnAround() && !game.canTeleport());   // (not while the car drives by itself)

  // ---- picture by picture, until it has followed its route for a kilometre
  const double odo0 = game.vehicle->odometer;
  const double t0 = game.time;
  double driven = 0;
  double routeAt = -1;
  double standing = 0;
  double worstStanding = 0;
  double offRoad = 0;
  double offRoute = 0;
  double worstOff = 0;
  double worstOffWidth = 0;
  double said = 0;
  PolyProjection proj;
  SurfaceHit hit = newSurfaceHit();
  while (driven < metres && game.time - t0 < 900) {
    const double before = game.time;
    frame();
    const double dt = game.time - before;
    if (!game.demo) break;
    Vehicle& v = *game.vehicle;
    v.getModelPosition(pos);
    driven = v.odometer - odo0;
    if (routeAt < 0 && game.nav->busy()) {
      routeAt = game.time - t0;
      std::printf("  the way is known after %.1f s: %zu lanes of it loaded\n", routeAt, game.nav->routeLanes->size());
    }
    // standing still (a red light may take a while – a minute and a half does not pass at one)
    standing = routeAt >= 0 && std::fabs(v.speed) < 0.2 ? standing + dt : 0;
    worstStanding = std::max(worstStanding, standing);
    // on the carriageway (or in a junction)
    if (routeAt >= 0 && !(game.app.world->surfaceAt(pos[0], pos[2], pos[1] + 1, hit) && (hit.zone == 0 || hit.node))) offRoad += dt;
    if (const Lane* lane = game.currentLane()) {
      // in its lane (between the junctions): how far from its middle
      projectOnPolyline(lane->edge->pl, pos[0], pos[2], proj);
      const double s = lane->dir > 0 ? proj.s - lane->s0 : lane->s0 - proj.s;
      const double off = std::fabs(proj.off - lane->off);
      if (s > 0 && s < lane->len && off - lane->width / 2 > worstOff - worstOffWidth / 2) {
        worstOff = off;
        worstOffWidth = lane->width;
      }
      // on a road of its way
      if (routeAt >= 0) {
        bool onWay = false;
        for (const Lane* l : *game.nav->routeLanes) if (l->edge == lane->edge) onWay = true;
        if (!onWay) offRoute += dt;
      }
    }
    if (game.time - said >= 20) {
      said = game.time;
      std::printf("  %4.0f s: %5.0f m driven, %4.1f km/h at (%.0f, %.0f), %d junctions of the way loaded ahead, saying '%s'\n", game.time - t0, driven, v.speed * 3.6, pos[0], pos[2], game.nav->left(),
                  game.demo->remark.c_str());
    }
  }
  const double took = game.time - t0;
  std::printf("%.0f m in %.0f s (%.1f km/h); the way known after %.1f s; standing %.1f s at most; %.1f s off the carriageway, %.1f s on a road that is not of its way; %.2f m from the middle of a lane %.2f m wide at most\n",
              driven, took, took > 0 ? driven / took * 3.6 : 0, routeAt, worstStanding, offRoad, offRoute, worstOff, worstOffWidth);
  assert(game.demo);   // the drive did not end (lost, or arrived) before the kilometre
  assert(game.demo->active && !game.demo->held);
  assert(routeAt >= 0 && routeAt < 30);   // a way was found, soon
  assert(driven >= metres);   // a kilometre of it driven
  assert(game.demo->rescues == 0 && game.demo->rescuedAt == -100);   // never put back onto the road
  assert(worstStanding < 100);   // never standing still for long
  assert(offRoad < 1);   // on the carriageway all the way
  assert(offRoute < 5);   // on the roads of its way
  assert(worstOff < worstOffWidth / 2 + 0.6);   // in its lane
  assert(game.state == "drive");

  // ---- the stop button of the autopilot: the player takes the car over
  game.endDemo("taken");
  assert(!game.demo && !game.autopilotDriving());
  assert(!game.nav->busy() && game.hud->routeLanes.empty() && game.rules->allowed == nullptr);
  assert(game.canDrive() && game.canTurnAround());

  std::printf("autopilot_drive_test: ok\n");
  return 0;
}
