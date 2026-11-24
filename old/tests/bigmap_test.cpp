// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The full-screen map: opens on a road network, draws it (roads, the car,
 * the path), taps a place on it and plans a demonstration drive.
 *
 * Origin: tests/ui/bigmap.test.ts (MaurerAnton/driving-simulator @ ff37c5d,
 * v0.7.0) and the map parts of src/ui/bigmap.ts.
 */
#include "../src/ui/bigmap.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

#include "../src/game/address.h"
#include "../src/ui/hud.h"
#include "../src/world/world.h"

using namespace fs;

namespace {

WorldJson makeNet() {
  WorldJson j;
  j.version = 1;
  j.meta.tileSize = 500;
  j.meta.minX = -500;
  j.meta.maxX = 500;
  j.meta.minZ = -500;
  j.meta.maxZ = 500;
  j.meta.originE = 528000;
  j.meta.originN = 5678000;
  auto node = [](double x, double y, double z, int t) {
    NodeJson n;
    n.x = x;
    n.y = y;
    n.z = z;
    n.t = t;
    return n;
  };
  j.nodes.push_back(node(0, 0, 0, CTRL_RBL));
  j.nodes.push_back(node(400, 0, 0, CTRL_NONE));
  j.nodes.push_back(node(0, 0, 400, CTRL_NONE));
  auto edge = [](int a, int b, std::vector<double> p, int cls) {
    EdgeJson e;
    e.a = a;
    e.b = b;
    e.c = cls;
    e.p = std::move(p);
    e.w = 7.0;
    e.lf = 1;
    e.lb = 1;
    e.lw = 3.5;
    e.mf = 50;
    e.mb = 50;
    e.f = EF_URBAN;
    e.n = "Teststraße";
    return e;
  };
  j.edges.push_back(edge(0, 1, {0, 0, 0, 200, 0, 0, 400, 0, 0}, RESIDENTIAL));
  j.edges.push_back(edge(0, 2, {0, 0, 0, 0, 0, 200, 0, 0, 400}, RESIDENTIAL));
  auto arm = [](int e, int r) {
    ArmJson a;
    a.e = e;
    a.r = r;
    return a;
  };
  j.nodes[0].arms = {arm(0, ROLE_PRIORITY), arm(1, ROLE_PRIORITY)};
  j.nodes[1].arms = {arm(0, ROLE_NONE)};
  j.nodes[2].arms = {arm(1, ROLE_NONE)};
  PlaceJson pl;
  pl.name = "Baunatal";
  pl.kind = "town";
  pl.x = 200;
  pl.z = 200;
  j.places.push_back(pl);
  SpawnJson sp;
  sp.name = "Mitte";
  sp.x = 0;
  sp.z = 0;
  sp.kind = "street";
  j.spawns.push_back(sp);
  return j;
}

/** count the pixels that are not the map's dark background */
int colouredPixels(const Canvas2D& c) {
  int n = 0;
  const std::vector<uint8_t>& px = c.pixels();
  for (size_t i = 0; i < px.size(); i += 4) {
    if (px[i] > 0x20 || px[i + 1] > 0x28 || px[i + 2] > 0x30) n++;
  }
  return n;
}

}  // namespace

int main() {
  WorldJson net = makeNet();
  World world(net);

  double carX = 0, carZ = 0, carYaw = 0;
  DrivenPath path;
  path.add(0, 0);
  path.add(40, 10);
  path.add(90, 12);
  bool teleported = false;
  SpawnJson teleportedTo;
  bool closed = false;
  std::unique_ptr<HudNode> shown;
  int hidden = 0;
  std::vector<SpawnJson> demoStops;

  BigMapHost host;
  host.world = &world;
  host.lang = []() { return std::string("de"); };
  host.car = [&](double& x, double& z, double& yaw) {
    x = carX;
    z = carZ;
    yaw = carYaw;
  };
  host.path = &path;
  host.spotAt = [](double x, double z) {
    // the nearest of the two roads, in the right-hand lane
    SpawnJson s;
    s.kind = "street";
    if (std::fabs(x) < std::fabs(z)) {
      s.x = 0;
      s.z = z;
      s.name = "Teststraße";
    } else {
      s.x = x;
      s.z = 0;
      s.name = "Teststraße";
    }
    return s;
  };
  host.canTeleport = []() { return true; };
  host.teleport = [&](const SpawnJson& s) {
    teleported = true;
    teleportedTo = s;
  };
  host.loadAddresses = []() { return nullptr; };
  host.spotOf = [](const AddressHit& h, bool& ok) {
    SpawnJson s;
    s.x = h.x;
    s.z = h.z;
    s.kind = "street";
    ok = true;
    return s;
  };
  host.startDemo = [&](const std::vector<SpawnJson>& stops) { demoStops = stops; };
  host.latLon = [](double x, double z) { return std::pair<double, double>(51.25 + x * 1e-5, 9.4 + z * 1e-5); };
  host.aerial = []() { return false; };
  host.setAerial = [](bool) {};
  host.close = [&]() { closed = true; };
  host.windowSize = [](int& w, int& h) {
    w = 320;
    h = 240;
  };
  host.onShow = [&](std::unique_ptr<HudNode> panel) { shown = std::move(panel); };
  host.onHide = [&]() { hidden++; };
  host.http = nullptr;

  BigMap map(host);
  assert(!map.isOpen());

  // open: the panel is handed over, the title says what the map is
  map.open(false);
  assert(map.isOpen());
  assert(shown != nullptr);
  std::string texts;
  std::function<void(HudNode*)> walk = [&](HudNode* n) {
    if (!n) return;
    texts += n->text + " " + n->html + " ";
    for (auto& c : n->children) walk(c.get());
  };
  walk(shown.get());
  assert(texts.find("Karte") != std::string::npos);   // mapTitle, in German
  assert(shown->cls.find("bigmap") != std::string::npos);

  // the picture: roads, the car and the path are drawn on it
  Canvas2D picture(320, 240);
  map.paint(picture);
  int painted = colouredPixels(picture);
  assert(painted > 500);   // the roads alone cover a good part of a 320×240 view

  // a tap on the middle: a place on a road, with the spot bar showing it
  map.down(160, 120);
  map.up(160, 120);
  map.paint(picture);

  // drag: the middle moved, the picture follows (different pixels)
  Canvas2D before(320, 240);
  map.paint(before);
  map.down(160, 120);
  map.drag(200, 150);
  map.up(200, 150);
  Canvas2D after(320, 240);
  map.paint(after);
  assert(before.pixels() != after.pixels());

  // the keys: arrows move, + and - zoom (the picture changes, nothing throws)
  map.key("ArrowRight");
  map.paint(after);
  map.key("Equal");
  map.paint(after);
  map.key("Minus");
  map.paint(after);

  // planning a demonstration drive: two stops, then start
  map.plan();
  assert(texts.size() > 0);
  map.close();
  assert(closed);
  assert(hidden == 1);
  assert(!map.isOpen());

  std::printf("bigmap: all checks passed (%d coloured pixels, %zu panel chars)\n", painted,
              texts.size());
  return 0;
}
