// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/levelcrossing.ts @ v1.7.7
/**
 * Level crossings (Bahnübergänge): where a road of the map crosses a track
 * (CrossingJson of kind XING.RAIL or XING.TRAM) and what happens there.
 *
 * The crossings of one road that lie close together – one for each track
 * of a double track – are one installation.  What it is equipped with
 * comes from src/world/levelxing.ts: half or full barriers, the yellow and
 * red lights (or the red flashing light of older ones), a bell – or only
 * the St Andrew's cross (Zeichen 201).
 *
 * The trains and trams of src/sim/rail.ts drive it: one that will be at the
 * crossing within `LX.WARN` seconds switches it on – yellow for 3 s, then
 * red; 6 s later (9 s after the lights came on) the barriers come down,
 * which takes 6 s, and they are down before the train is there.  A train
 * that comes to a crossing that is not secured yet brakes before it (as it
 * would at its "Überwachungssignal").  Once the last car of the train has
 * left the crossing, the barriers rise and the lights go out.
 *
 * The cars of the traffic (src/sim/traffic.ts) and the autopilot
 * (src/sim/autodriver.ts) ask `gate`: wait before the cross while the lights
 * are on and the barriers move or are down, at a crossing without them
 * while a train comes, and – wherever it is – when there is no room behind
 * the tracks for the whole car: nobody stops on the rails (§ 19 Abs. 3
 * StVO).  The rule monitor (src/sim/rules.ts) asks `judge` about the
 * player: past the cross on red, at lowered barriers or in front of a
 * train, too fast, stopped on the tracks, overtaking before the crossing.
 * The renderer (src/render/levelcrossings.ts) draws the lights, barriers
 * and signs from the same state, and rings the bell.
 *
 * The port: what the original asks of the traffic (`LxRoad`) it asks of the
 * Traffic.  The trains are handed in by `setRail` as the original's
 * RailTraffic (sim/rail at v1.7.7: its `hold`, `routeNext`, `lineNext`,
 * `carLayout`) – the code that reads them is a template, written out where
 * the game hands in its trains.
 */
#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/geom.h"
#include "core/js.h"
#include "sim/playerstate.h"
#include "world/lanes.h"
#include "world/levelxing.h"
#include "world/tilefmt.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

class Traffic;
struct TrafficCar;
class RuleMonitor;
struct PlayerSnapshot;

/** The times and distances of a level crossing. */
struct LxTimes {
  /** seconds of yellow before the red (where the lights have a yellow lamp) */
  double YELLOW = 3;
  /** seconds of red before the barriers begin to come down: the lights are on 9 s before they move */
  double RED_BEFORE = 6;
  /** seconds the barriers take to come down, and to rise */
  double LOWER = 6;
  double RAISE = 6;
  /** a train this many seconds from the crossing switches it on */
  double WARN = 22;
  /** seconds after the train has left before the barriers rise (or the lights go out) */
  double CLEAR = 2;
  /** the St Andrew's cross stands this far before the first track (m, along the road) – as the map's signs do */
  double CROSS = 4.5;
  /** where a vehicle's front stops: this far before the first track (just before the cross) */
  double STOP = 5;
  /** half the width of the danger area round a track along the road (m): a train's width and some */
  double ZONE = 2.6;
  /** at a crossing with only the cross: a train within this many seconds is waited for (a tram: TRAM_WAIT – it is slower, and in town) */
  double PASSIVE_WAIT = 12;
  double TRAM_WAIT = 6;
  /** … and one within this many seconds makes it a fault to drive over */
  double PASSIVE_FAULT = 8;
  double TRAM_FAULT = 4;
  /** the "moderate speed" of § 19 Abs. 1 StVO where there are no barriers (m/s: 50 km/h) */
  double MODERATE = 50 / 3.6;
  /** the beacons outside built-up areas (Zeichen 156, 159, 162): metres before the cross */
  std::array<double, 3> BEACONS{240, 160, 80};
  /** the warning sign alone (Zeichen 151) in town: metres before the crossing, as the map puts it */
  double WARNING = 140;
  /** installations further than this from the player are left alone (m) */
  double ACTIVE = 1500;
  /** crossings of one road closer together than this are one installation (m) */
  double GROUP = 14;
};
inline constexpr LxTimes LX{};

/** 'open' | 'yellow' | 'red' | 'lowering' | 'closed' | 'raising' */
using Phase = std::string;

/** Where a track crosses the road: its piece and metres (sim/rail.ts), and the point. */
struct TrackHit {
  int path = 0;
  double s = 0;
  double x = 0;
  double z = 0;
};

/** One level crossing: one road, one or more tracks. */
struct LevelCrossing {
  int id = 0;
  Edge* edge = nullptr;
  /** (the port: the road's number – the road itself may be gone when the installation is looked at again) */
  int edgeId = 0;
  /** the map's crossings it is made of */
  std::vector<const CrossingJson*> xings;
  /** the stations along the road of the first and the last track */
  double s0 = 0;
  double s1 = 0;
  /** its middle */
  double x = 0;
  double z = 0;
  Equipment eq;
  std::vector<TrackHit> hits;
  Phase phase = "open";
  /** seconds in the phase */
  double t = 0;
  /** the barriers: 0 up … 1 down */
  double lower = 0;
  /** seconds until the nearest train is at the crossing (Infinity: none coming) */
  double eta = JS_INF;
  /** a train is on the crossing */
  bool occupied = false;
  /** seconds no train has needed it */
  double quiet = 0;
  /** the beacons (outside built-up areas, on bigger roads): drawn by us in place of the map's warning sign */
  bool beacons = false;
  /** the overtaking ban begins this far before the cross (m; 0: no sign announces the crossing) */
  double zone = 0;
};

/** What `gate` asks of a vehicle: where to stop (m ahead of its front; Infinity: nowhere) and how fast at most. */
struct LxGate {
  double stop = JS_INF;
  double cap = JS_INF;
  /** '' | 'light' | 'barrier' | 'train' | 'queue' */
  std::string why;
};

/** Is the crossing passive: only the cross, nothing that switches? */
bool passive(const LevelCrossing& c);

/** The station of the cross for traffic going `dir` along the road (+1 a→b), and the one of the far side of the danger area. */
double entryOf(const LevelCrossing& c, int dir);
double exitOf(const LevelCrossing& c, int dir);

/** Are the lights showing red (steady or flashing) now – the barriers moving or down included? */
bool redOn(const LevelCrossing& c);

/** Does the bell ring now?  While the lights are on and the barriers come down – without barriers, the first seconds of red. */
bool ringing(const LevelCrossing& c);

/**
 * Is the crossing safe for a train: barriers down, or the lights red long
 * enough for whoever was about to go over to have stopped – a crossing with
 * only the cross is always (the road traffic waits for the train there).
 */
bool secured(const LevelCrossing& c);

/**
 * Must a vehicle `d` metres before the stop point going `v` m/s wait at the
 * crossing for what it shows (or a train it sees)?  Amber as at a light:
 * stop if that is comfortably possible.
 */
std::string mustWait(const LevelCrossing& c, double v, double d);

class LevelCrossings {
 public:
  World& world;
  Traffic& road;
  std::vector<std::unique_ptr<LevelCrossing>> list;
  /** the player (set by the traffic each step): it may stand in the queue behind a crossing */
  const PlayerState* player = nullptr;
  /** the game's time (s): the flashing lights blink by it */
  double time = 0;

  LevelCrossings(World& world, Traffic& road);
  virtual ~LevelCrossings();

  /** The trains that drive the crossings (none: they stay open) – the original's RailTraffic. */
  template <class Rail>
  void setRail(Rail* rail);

  /** The installations on a road (null: none). */
  const std::vector<LevelCrossing*>* on(const Edge& edge) const;

  /** The installation a crossing of the map belongs to. */
  LevelCrossing* of(const CrossingJson& x) const;

  /** Follow the network as it comes and goes in pieces: new crossings become installations, those of roads gone go. */
  void refresh();

  /** Each step, before the traffic and the trains: where the trains are, and what each crossing near (x, z) shows. */
  void update(double dt, double x, double z);

  /** The lights and barriers of one installation, one step on (a test holds them in one phase by its own). */
  virtual void step(LevelCrossing& c, double dt);

  /**
   * For a vehicle on `lane` with its front `front` metres along it, going
   * `v` m/s, `length` long, taking `next` at the end of the lane: where to
   * stop for a crossing ahead and how fast to come to it.  `self`: the car
   * of the traffic it is (or, `isPlayer`: the player's own, driven by the
   * autopilot – the original's 'player') – not counted in the queue.
   */
  LxGate gate(const Lane& lane, double front, double v, double length, const Connector* next, const TrafficCar* self, bool isPlayer = false);

  /**
   * Is the room behind the danger area too short for a vehicle `length`
   * long: something standing (or nearly) whose rear is less than the car's
   * length and a metre beyond the end of the area?  `exitAt`: that end, in
   * metres ahead of the front; `base`: where lane `l` begins.
   */
  bool blockedBeyond(const Lane& l, double base, double exitAt, double length, const TrafficCar* self, bool isPlayer);

  /**
   * The player's faults (called by the rule monitor with where the vehicle
   * is: its road, station and direction): over the cross on red, at
   * lowered barriers or in front of a train (§ 19 Abs. 2 StVO), too fast
   * where there are no barriers (Abs. 1), stopped on the tracks (Abs. 3),
   * overtaking from the warning sign on (Abs. 1).
   */
  void judge(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, int dir, double dt);

  /**
   * The signs we put up for an installation: the St Andrew's cross on the
   * mast of the lights (in place of the map's own), and outside built-up
   * areas the beacons with the warning sign (Zeichen 156, 159, 162) in place
   * of the map's warning sign.  `y` gives the ground at a point of the road.
   */
  std::vector<SignPost> signsFor(const LevelCrossing& c, const std::function<double(double s, double x, double z)>& y);

  /** A post on the right of traffic going `dir`, at station `s`, as the map's posts stand (tools/mapgen/signs.ts postBeside). */
  SignPost postAt(const Edge& e, double s, int dir, const std::vector<std::string>& codes, const std::function<double(double s, double x, double z)>& y);

  /**
   * A sign post of the map's tiles: as it is, without the codes we put up
   * ourselves at an installation (its St Andrew's crosses where we draw the
   * lights, its warning signs where we draw the beacons) – none when
   * nothing is left of it.
   */
  std::optional<SignPost> editPost(const SignPost& p);

 private:
  /** installations by the edge they are on */
  std::unordered_map<int, std::vector<LevelCrossing*>> byEdge;
  std::unordered_map<const CrossingJson*, LevelCrossing*> ofXing;
  /** the crossings of each piece of track: installation and metres */
  struct OnPath {
    LevelCrossing* c = nullptr;
    double s = 0;
  };
  std::unordered_map<int, std::vector<OnPath>> byPath;
  int known = -1;
  double refreshT = 0;
  int nextId = 1;
  /** for each rail vehicle: how far ahead the nearest crossing is that is not secured (sim/rail.ts brakes for it) */
  std::unordered_map<int, double> holds;
  /** the installations near the player this step */
  std::vector<LevelCrossing*> active;
  /** what the rule monitor saw last of the player: per road and direction where the front was, and since when it stands on the tracks */
  struct Front {
    const Edge* edge = nullptr;
    int dir = 0;
    double s = 0;
    double t = 0;
  };
  std::optional<Front> prevFront;
  double standT = 0;
  std::unordered_map<const TrafficCar*, double> sides;

  /** the trains (sim/rail.ts RailTraffic), as the template below reads them */
  struct Trains {
    virtual ~Trains() = default;
    virtual void findTracks(LevelCrossings& lx, LevelCrossing& c) = 0;
    virtual void ahead(LevelCrossings& lx) = 0;
    virtual void hold(LevelCrossings& lx) = 0;
  };
  template <class Rail>
  struct TrainsOf;
  std::unique_ptr<Trains> rail;

  void equip(LevelCrossing& c);
  void findTracks(LevelCrossing& c);
  void indexTracks();
  void gateOn(const Lane& l, double base, double v, double length, const TrafficCar* self, bool isPlayer, LxGate& out);
  void overtaking(RuleMonitor& rules, const PlayerSnapshot& p, const Edge& edge, double s, int dir, const LevelCrossing& c);
};

// ------------------------------------------------------------------ the trains (sim/rail.ts)

namespace lxdetail {
template <class T>
T& deref(T& v) {
  return v;
}
template <class T>
T& deref(const std::unique_ptr<T>& v) {
  return *v;
}
}  // namespace lxdetail

template <class Rail>
struct LevelCrossings::TrainsOf : LevelCrossings::Trains {
  Rail* rail;
  explicit TrainsOf(Rail* r) : rail(r) {}

  /** The tracks that cross the road at the installation (pieces of sim/rail.ts within a few metres of its crossings). */
  void findTracks(LevelCrossings&, LevelCrossing& c) override {
    auto& net = rail->net;
    PolySample smp;
    for (const CrossingJson* x : c.xings) {
      samplePolyline(c.edge->pl, x->s, smp);
      const double px = smp.x;
      const double pz = smp.z;
      std::unordered_set<int> seen;
      for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
          const auto near = net.near(px + i * 200, pz + j * 200);
          for (size_t k = 0; k < near.size(); k += 2) {
            const int pi = static_cast<int>(near[k]);
            if (seen.count(pi)) continue;
            seen.insert(pi);
            const auto& p = net.paths[pi];
            double bd = 6;
            double bs = -1;
            for (size_t q = 1; q < p.cum.size(); q++) {
              const double ax = p.pts[q * 3 - 3];
              const double az = p.pts[q * 3 - 1];
              const double dx = p.pts[q * 3] - ax;
              const double dz = p.pts[q * 3 + 2] - az;
              const double l2 = dx * dx + dz * dz;
              if (l2 < 1e-9) continue;
              const double u = std::max(0.0, std::min(1.0, ((px - ax) * dx + (pz - az) * dz) / l2));
              const double d = std::hypot(ax + dx * u - px, az + dz * u - pz);
              if (d < bd) {
                bd = d;
                bs = p.cum[q - 1] + std::sqrt(l2) * u;
              }
            }
            bool have = false;
            for (const TrackHit& h : c.hits) have = have || (h.path == pi && std::abs(h.s - bs) < 8);
            if (bs >= 0 && !have) c.hits.push_back(TrackHit{pi, bs, px, pz});
          }
        }
      }
    }
  }

  /** Along the way a train will take (its plan, then its line or the straightest way): each crossing within `reach` of its front. */
  template <class V, class See>
  void walk(LevelCrossings& lx, V& v, double reach, See&& see) {
    auto& net = rail->net;
    int cp = v.path;
    double cs = v.s;
    int cd = v.dir;
    double dist = 0;
    size_t planned = 0;
    for (int guard = 0; guard < 60 && dist <= reach; guard++) {
      if (cp < 0 || cp >= static_cast<int>(net.paths.size())) return;
      const auto& p = net.paths[cp];
      const auto on = lx.byPath.find(cp);
      if (on != lx.byPath.end()) {
        for (const OnPath& h : on->second) {
          const double ahead = (h.s - cs) * cd;
          if (ahead >= 0 && dist + ahead <= reach) see(*h.c, dist + ahead);
        }
      }
      dist += cd > 0 ? p.len - cs : cs;
      std::optional<std::pair<int, int>> next;
      if (planned < v.plan.size()) {
        next = std::pair<int, int>{v.plan[planned].path, v.plan[planned].dir};
        planned++;
      } else {
        planned++;
        if (const auto r = rail->routeNext(v, cp, cd)) next = std::pair<int, int>{r->path, r->dir};
        else if (const auto l = rail->lineNext(v, cp, cd)) next = std::pair<int, int>{l->path, l->dir};
        else {
          const auto ways = net.waysOn(cp, cd, v.consist->tracks);
          if (!ways.empty()) next = std::pair<int, int>{ways[0].path, ways[0].dir};
        }
      }
      if (!next) return;
      cp = next->first;
      cd = next->second;
      cs = cd > 0 ? 0 : (cp >= 0 && cp < static_cast<int>(net.paths.size()) ? net.paths[cp].len : 0);
    }
  }

  /** The crossings on the way ahead of each train: when it is at each; which it stands on. */
  void ahead(LevelCrossings& lx) override {
    for (auto& vp : rail->vehicles) {
      auto& v = lxdetail::deref(vp);
      // (the cars of the train on a crossing: by their boxes)
      const auto& layout = carLayout(*v.consist);
      for (size_t i = 0; i < v.cars.size(); i++) {
        const auto& car = v.cars[i];
        const double len = i < layout.size() ? layout[i].len : v.consist->carLen;
        const double fx = -std::sin(car.yaw);
        const double fz = -std::cos(car.yaw);
        for (LevelCrossing* c : lx.active) {
          if (std::abs(c->x - car.x) > 60 || std::abs(c->z - car.z) > 60) continue;
          for (const TrackHit& h : c->hits) {
            const double dx = h.x - car.x;
            const double dz = h.z - car.z;
            const double a = dx * fx + dz * fz;
            const double l = dx * fz - dz * fx;
            if (std::abs(a) <= len / 2 + 1.5 && std::abs(l) <= 3.5) c->occupied = true;
          }
        }
      }
      const double speed = std::max(v.speed, std::min(v.consist->vmax, 8.0));
      const double reach = std::max(300.0, v.consist->vmax * LX.WARN + 120);
      walk(lx, v, reach, [&](LevelCrossing& c, double d) {
        const double eta = std::max(0.0, v.dwell) + d / speed;
        if (eta < c.eta) c.eta = eta;
      });
    }
  }

  /** How far ahead of each train is the nearest crossing that is not secured – for it to brake for. */
  void hold(LevelCrossings& lx) override {
    for (auto& vp : rail->vehicles) {
      auto& v = lxdetail::deref(vp);
      if (v.player) continue;
      double hold = JS_INF;
      const double reach = std::max(250.0, (v.speed * v.speed) / (v.consist->brake * 1.2) + 80);
      walk(lx, v, reach, [&](LevelCrossing& c, double d) {
        if (!secured(c) && d < hold && d > 1) hold = d;
      });
      if (hold < JS_INF) lx.holds[v.id] = hold;
    }
  }
};

template <class Rail>
void LevelCrossings::setRail(Rail* r) {
  if (r) rail = std::make_unique<TrainsOf<Rail>>(r);
  else rail.reset();
  for (auto& c : list) findTracks(*c);
  indexTracks();
  if (r) r->hold = [this](auto& v) {
    const auto it = holds.find(v.id);
    return it == holds.end() ? JS_INF : it->second;
  };
}

}  // namespace fs
