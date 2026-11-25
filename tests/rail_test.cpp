// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Trams and trains on the tracks of the map: they follow the track through
 * switches, keep right on a double track, call at stops, brake for what is
 * on the track, turn back at the end of the line.
 *
 * Origin: tests/game/rail.test.ts @ v1.7.7
 */
#include "../src/sim/rail.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>

using namespace fs;

namespace {

/** A straight piece of track from (x0, z0) to (x1, z1), a point every 5 m. */
RailJson piece(double x0, double z0, double x1, double z1,
               const std::vector<int>& a = {}, const std::vector<int>& b = {},
               const std::vector<double>* stops = nullptr) {
  double len = std::hypot(x1 - x0, z1 - z0);
  int n = std::max(1, static_cast<int>(std::lround(len / 5)));
  RailJson r;
  r.k = RAIL_TRAM;
  for (int i = 0; i <= n; i++) {
    r.p.push_back(x0 + (x1 - x0) * i / n);
    r.p.push_back(100);
    r.p.push_back(z0 + (z1 - z0) * i / n);
  }
  r.a = a;
  r.b = b;
  if (stops) {
    r.st = *stops;
  }
  return r;
}

const RailPlayer far = {5000, 100, 5000, 0, 4.3, 1.8};

/** a small LCG, as the test's `seeded` (deterministic play) */
double seeded() {
  static uint32_t s = 12345;
  s = (s * 1103515245u + 12345u) % 2147483648u;
  return static_cast<double>(s) / 2147483648.0;
}

/** run the traffic for some seconds (no spawning: the test puts the vehicles) */
void run(RailTraffic& t, double seconds, const RailPlayer& player = far) {
  t.target = 0;
  t.far = 1e9;
  for (int i = 0; i < static_cast<int>(seconds * 50); i++) t.update(0.02, player);
}

std::pair<double, double> frontOf(const RailVehicle& v) {
  const RailCar& c = v.cars[0];
  double half = v.consist->carLen / 2;
  return {c.x - std::sin(c.yaw) * half, c.z - std::cos(c.yaw) * half};
}

}  // namespace

int main() {
  // ---- a tram runs along the track, its cars in a row behind the front end
  {
    RailTraffic t({piece(0, 0, 0, -600)}, seeded);
    RailVehicle* v = t.spawnAt(0, 100, 1);
    assert(v && "the tram fits on the track");
    assert(v->cars.size() == 3);
    const Consist* c = consistOf(RAIL_TRAM);
    for (int i = 0; i < 3; i++) {
      assert(std::fabs(v->cars[static_cast<size_t>(i)].x) < 0.01);
      assert(std::fabs(v->cars[static_cast<size_t>(i)].yaw) < 0.01);
      assert(std::fabs(v->cars[static_cast<size_t>(i)].z -
                       (-100 + c->carLen / 2 + i * (c->carLen + c->gap))) < 0.3);
    }
    run(t, 30);
    assert(v->speed > 12 && v->speed <= c->vmax + 1e-6);
    assert(frontOf(*v).second < -300);
    double gap = v->cars[1].z - v->cars[0].z;
    assert(std::fabs(gap - (c->carLen + c->gap)) < 0.2);
    assert(t.spawnAt(0, 5, 1) == nullptr);   // no room behind the front end
  }

  // ---- it calls at a stop once and goes on
  {
    std::vector<double> stops = {300, 310};
    RailTraffic t({piece(0, 0, 0, -800, {}, {}, &stops)}, seeded);
    RailVehicle* v = t.spawnAt(0, 100, 1);
    assert(v);
    double stood = 0, stoppedAt = 0;
    int stopsSeen = 0;
    bool was = false;
    t.target = 0;
    t.far = 1e9;
    for (int i = 0; i < 90 * 50; i++) {
      t.update(0.02, far);
      bool standing = v->dwell > 0;
      if (standing) {
        stood += 0.02;
        stoppedAt = v->s;
      }
      if (standing && !was) stopsSeen++;
      was = standing;
    }
    assert(stopsSeen == 1);
    assert(std::fabs(stoppedAt - 300) < 2);
    assert(stood > 11 && stood < 21);
    assert(v->s > 450);
  }

  // ---- it slows down for a curve
  {
    RailJson r;
    r.k = RAIL_TRAM;
    for (double z = 0; z >= -200; z -= 5) {
      r.p.push_back(0);
      r.p.push_back(100);
      r.p.push_back(z);
    }
    for (double a = 5; a <= 90; a += 5) {
      double rad = a * M_PI / 180;
      r.p.push_back(25 - 25 * std::cos(rad));
      r.p.push_back(100);
      r.p.push_back(-200 - 25 * std::sin(rad));
    }
    for (double x = 30; x <= 300; x += 5) {
      r.p.push_back(x);
      r.p.push_back(100);
      r.p.push_back(-225);
    }
    RailTraffic t({r}, seeded);
    RailVehicle* v = t.spawnAt(0, 60, 1);
    assert(v);
    double inBend = INFINITY, top = 0;
    t.target = 0;
    t.far = 1e9;
    for (int i = 0; i < 40 * 50; i++) {
      t.update(0.02, far);
      top = std::max(top, v->speed);
      if (v->s > 205 && v->s < 235) inBend = std::min(inBend, v->speed);
    }
    assert(top > 10);
    assert(inBend < 6.5);
    assert(v->s > 300);
    assert(std::fabs(v->cars[2].z + 225) < 0.6);
  }

  // ---- switches: straight on or the gentle branch, never across a crossing track
  {
    std::vector<RailJson> rails = {
        piece(0, 0, 0, -200, {}, {2, 4, 6}),         // 0: comes to a node
        piece(0, -200, 0, -500, {1, 4, 6}),          // 1: straight on
        piece(0, -200, 60, -400, {1, 2, 6}),         // 2: branches off at about 17°
        piece(0, -200, 200, -200, {1, 2, 4}),        // 3: crosses at 90°
    };
    RailNetwork net(rails);
    std::vector<RailNetwork::Way> ways = net.waysOn(0, 1, {RAIL_TRAM});
    assert(ways.size() == 2);
    assert(ways[0].path == 1 && ways[1].path == 2);   // straight first; the crossing track is no way on
    std::vector<RailNetwork::Way> back = net.waysOn(1, -1, {RAIL_TRAM});
    assert(back.size() == 1 && back[0].path == 0 && back[0].dir == -1);
    assert(net.waysOn(0, 1, {RAIL_TRAIN}).empty());
    std::set<int> taken;
    for (int k = 0; k < 12; k++) {
      // the draw decides straight (a low one) or branch (a high one) — the
      // test tries both flavours and finds both ways used
      double pick = (k % 4 == 0) ? 0.1 : 0.9;
      RailTraffic t(rails, [pick]() { return pick; });
      RailVehicle* v = t.spawnAt(0, 80, 1);
      assert(v);
      run(t, 22);
      taken.insert(v->path);
    }
    assert((taken == std::set<int>{1, 2}));
  }

  // ---- double track: keep right
  {
    RailNetwork net({piece(0, 0, 0, -300), piece(3, 0, 3, -300), piece(500, 0, 500, -300)});
    assert(net.paths[1].way == 1);
    assert(net.paths[0].way == -1);
    assert(net.paths[2].way == 0);
  }

  // ---- it brakes for a car on the track and goes on when the track is clear
  {
    RailTraffic t({piece(0, 0, 0, -600)}, seeded);
    RailVehicle* v = t.spawnAt(0, 100, 1);
    assert(v);
    RailPlayer player = {0.3, 100, -250, M_PI / 2, 4.3, 1.8};
    run(t, 40, player);
    assert(v->speed == 0);
    assert(v->blocked);
    double gap = frontOf(*v).second - player.z;
    assert(gap > 2 && gap < 9);
    assert(!t.touching(player));
    run(t, 15, far);
    assert(v->speed > 5 && frontOf(*v).second < -260);
  }

  // ---- the end of the line: wait, then back the way it came
  {
    RailTraffic t({piece(0, 0, 0, -300)}, seeded);
    RailVehicle* v = t.spawnAt(0, 200, 1);
    assert(v);
    run(t, 30);
    assert(v->dir == -1);
    assert(v->dwell > 0 && v->speed == 0);
    for (const RailCar& c : v->cars) assert(c.z > -300.5);
    run(t, 40);
    assert(v->dir == -1);
    assert(v->speed > 3);
    assert(v->s < 250);
    assert(std::fabs(std::fabs(v->cars[0].yaw) - M_PI) < 0.05);
    for (const RailCar& c : v->cars)
      assert(std::fabs(c.x) < 0.01 && c.z < 0.5 && c.z > -300.5);
  }

  // ---- touching a tram: pushed clear sideways
  {
    RailTraffic t({piece(0, 0, 0, -600)}, seeded);
    RailVehicle* v = t.spawnAt(0, 100, 1);
    assert(v);
    const RailCar& c = v->cars[1];
    RailPlayer p1 = {c.x + 1.6, 100, c.z, 0, 4.3, 1.8};
    const std::optional<RailTraffic::Touch> hit = t.touching(p1);
    assert(hit);
    assert(hit->nx > 0.99 && std::fabs(hit->nz) < 0.01);
    assert(hit->depth > 0.3 && hit->depth < 1);
    RailPlayer p2 = {c.x + 2.6, 100, c.z, 0, 4.3, 1.8};
    assert(!t.touching(p2));
    RailPlayer p3 = {c.x + 1.6, 110, c.z, 0, 4.3, 1.8};
    assert(!t.touching(p3));
    double nx, nz, depth;
    assert(!boxOverlap(0, 0, 0, 2, 1, 10, 0, 0.3, 2, 1, nx, nz, depth));
    assert(boxOverlap(0, 0, 0, 2, 1, 0, -2.5, M_PI / 2, 2, 1, nx, nz, depth));
    assert(nz > 0.99 && std::fabs(depth - 0.5) < 1e-9);
  }

  // ---- vehicles appear on tracks around the player and leave with the distance
  {
    std::vector<RailJson> rails;
    for (int i = 0; i < 6; i++) {
      std::vector<int> a = i > 0 ? std::vector<int>{(i - 1) * 2 + 1} : std::vector<int>();
      std::vector<int> b = i < 5 ? std::vector<int>{(i + 1) * 2} : std::vector<int>();
      rails.push_back(piece(-900 + i * 300, 0, -600 + i * 300, 0, a, b));
    }
    RailTraffic t(rails, seeded);
    t.target = 2;
    RailPlayer player = {0, 100, 30, 0, 4.3, 1.8};
    for (int i = 0; i < 50 * 40; i++) t.update(0.02, player);
    assert(t.vehicles.size() >= 1 && t.vehicles.size() <= 2);
    for (const auto& v : t.vehicles)
      for (const RailCar& c : v->cars) assert(std::fabs(c.z) < 0.01 && std::fabs(c.y - 100) < 0.01);
    RailTraffic none({}, seeded);
    for (int i = 0; i < 200; i++) none.update(0.02, player);
    assert(none.vehicles.empty());
    RailPlayer away = player;
    away.x = 9000;
    t.update(0.02, away);
    assert(t.vehicles.empty());
  }

  std::printf("rail: all checks passed\n");
  return 0;
}
