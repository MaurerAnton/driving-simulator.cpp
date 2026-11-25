// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/tramdrive.ts @ v1.7.7
#include "sim/tramdrive.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "core/random.h"
#include "sim/signals.h"

namespace fs {

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();
/** the front may stand this far before or past a stop's mark */
constexpr double STOP_BEFORE = 7;
constexpr double STOP_PAST = 4;

RailSample tmp;

}  // namespace

TramDriver::TramDriver(RailTraffic& railIn, RailVehicle& vIn, World& world, SignalSystem& signals, std::function<double()> random)
    : v(vIn), rail(railIn), world_(world), signals_(signals), random_(random ? std::move(random) : std::function<double()>(rnd)) {
  v.player = true;
}

void TramDriver::togglePanto(std::vector<TramEvent>& events) {
  if (pantoUp && powered() && wired() && lever > 0.05 && v.speed > 0.5) events.push_back(TramEvent{"tramPantoLoad", "minor", {}});
  pantoUp = !pantoUp;
}

bool TramDriver::atUnservedStop() const {
  if (v.speed > 0.1 || doorsOpen) return false;
  const std::optional<PathMark> here = findStop();
  return here && !(lastAt_ && lastAt_->path == here->path && lastAt_->mark == here->mark);
}

void TramDriver::toggleDoors(std::vector<TramEvent>& events) {
  if (v.speed > 0.1) return;
  if (!doorsOpen) {
    doorsOpen = true;
    const std::optional<PathMark> here = findStop();
    if (here) {
      atStop_ = here;
      lastAt_ = here;
      if (!servedHere_) {
        // how many get off and on: a few, sometimes nobody
        alighting = static_cast<int>(std::floor(random_() * 6));
        boarding = static_cast<int>(std::floor(random_() * 7));
        nextPassenger_ = 1.5;
      }
    } else events.push_back(TramEvent{"tramStopPlace", "minor", {}});
    return;
  }
  if (passengers() > 0) events.push_back(TramEvent{"tramDoorsEarly", "minor", {{"n", static_cast<double>(passengers())}}});
  doorsOpen = false;
  boarding = alighting = 0;
}

std::optional<PathMark> TramDriver::findStop() const {
  const RailPath& p = rail.net.paths[static_cast<size_t>(v.path)];
  for (double mark : p.stops) {
    // ahead of the front (+) or behind it (−), along the direction of travel
    const double d = (mark - v.s) * v.dir;
    if (d <= STOP_BEFORE && d >= -STOP_PAST) return PathMark{v.path, mark};
  }
  return std::nullopt;
}

void TramDriver::drive(std::vector<TramEvent>& events) {
  const Ahead& A = ahead;
  emergency = false;
  // the pantograph: up under the wire, down before it ends (the regional tram goes on with its engine)
  if (wired() && A.wireEndIn > 40 && !pantoUp) pantoUp = true;
  if (pantoUp && (A.wireEndIn < 40 || !wired()) && hybrid()) {
    if (lever > 0) lever = 0;
    pantoUp = false;
  }
  if (doorsOpen) {
    lever = -0.5;
    if (passengers() == 0) toggleDoors(events);
    return;
  }
  if (atUnservedStop()) {
    lever = -0.5;
    toggleDoors(events);
    return;
  }
  const double b = TRAM.brake * 0.55;
  double target = std::min(limit / 3.6 - 0.6, A.curve - 0.3);
  // the next stop (not the one just served): come to stand with the front at its mark
  const bool servedOne = lastAt_ && A.stopPath == lastAt_->path && A.stopMark == lastAt_->mark;
  if (A.stop < INF && !servedOne) target = std::min(target, std::sqrt(2 * b * std::max(0.0, A.stop - 0.8)));
  // a red light – or amber, if there is room to stop before it – where the track crosses a junction
  const bool canStop = v.speed * v.speed <= 2 * b * 0.6 * std::max(A.signalIn - 2, 0.1);
  if (A.signalIn < 150 && (A.signal == SIG_RED || A.signal == SIG_RED_AMBER || (A.signal == SIG_AMBER && canStop))) {
    target = std::min(target, std::sqrt(2 * b * std::max(0.0, A.signalIn - 2.5)));
  }
  // (no power yet – the pantograph still rising –: wait standing)
  if (!powered() && v.speed < 0.3) target = 0;
  const double err = target - v.speed;
  lever = std::max(-1.0, std::min(1.0, err * 0.8));
  if (target < 0.15 && v.speed < 0.3) lever = -0.5;
}

std::optional<PathDir> TramDriver::choose(int path, int dir) const {
  const RailNetwork& net = rail.net;
  const std::vector<RailNetwork::Way> ways = net.waysOn(path, dir, v.consist->tracks);
  if (ways.empty()) return std::nullopt;
  // without a wish: the way of its line (if it runs one), else straight on
  if (wish == 0) {
    const std::optional<PathDir> want = rail.lineNext(v, path, dir);
    if (want)
      for (const RailNetwork::Way& w : ways)
        if (w.path == want->path && w.dir == want->dir) return PathDir{w.path, w.dir};
    return PathDir{ways[0].path, ways[0].dir};
  }
  if (ways.size() == 1) return PathDir{ways[0].path, ways[0].dir};
  // the direction of travel at the end, and of each way on: the angle (+ to the left)
  const RailPath& p = net.paths[static_cast<size_t>(path)];
  const size_t n = p.cum.size();
  const size_t e = dir > 0 ? n - 1 : 0;
  const size_t f = dir > 0 ? n - 2 : 1;
  const double hx = p.pts[e * 3] - p.pts[f * 3];
  const double hz = p.pts[e * 3 + 2] - p.pts[f * 3 + 2];
  const RailNetwork::Way* best = &ways[0];
  double bestTurn = 0;
  for (const RailNetwork::Way& w : ways) {
    const RailPath& q = net.paths[static_cast<size_t>(w.path)];
    const int m = static_cast<int>(q.cum.size());
    const int i0 = w.dir > 0 ? 0 : m - 1;
    const int i1 = w.dir > 0 ? std::min(m - 1, 3) : std::max(0, m - 4);
    const double dx = q.pts[static_cast<size_t>(i1) * 3] - q.pts[static_cast<size_t>(i0) * 3];
    const double dz = q.pts[static_cast<size_t>(i1) * 3 + 2] - q.pts[static_cast<size_t>(i0) * 3 + 2];
    const double turn = std::atan2(-(hx * dz - hz * dx), hx * dx + hz * dz);
    if (turn * wish > bestTurn * wish) {
      best = &w;
      bestTurn = turn;
    }
  }
  return PathDir{best->path, best->dir};
}

void TramDriver::step(double dt, std::vector<TramEvent>& events) {
  const RailNetwork& net = rail.net;
  // --- the way on at the next switch: asked for until a few metres before it, then set
  const RailPath& p = net.paths[static_cast<size_t>(v.path)];
  const double room = v.dir > 0 ? p.len - v.s : v.s;
  if (room > 8 || v.plan.empty()) {
    const std::optional<PathDir> next = choose(v.path, v.dir);
    v.plan.clear();
    if (next) v.plan.push_back(*next);
  }
  // --- the doors, as the door key has them (they take a moment to open and to shut)
  const double doors = v.doors;
  if (doorsOpen ? doors < 1 : doors > 0) v.doors = doorsOpen ? std::min(1.0, doors + dt / DOOR_TIME) : std::max(0.0, doors - dt / DOOR_TIME);
  // --- the pantograph: rising or coming down; where the wire ends it must be down
  const double want = pantoUp ? 1 : 0;
  if (panto != want) panto = want > panto ? std::min(1.0, panto + dt / PANTO_UP_TIME) : std::max(0.0, panto - dt / PANTO_DOWN_TIME);
  v.panto = panto;
  const bool isWired = wired();
  if (!isWired && wasWired_ && panto > 0.5) {
    // (it would catch on the end of the wire: lowered at once, and the instructor says so)
    events.push_back(TramEvent{"tramPantoWireEnd", "major", {}});
    pantoUp = false;
  }
  wasWired_ = isWired;
  // --- forces (a second unit pulls as much as it weighs; a trailer car is only weight)
  const double sp = v.speed;
  const ConsistMass cm = consistMass(*v.consist);
  double a = 0;
  if (emergency) a = -TRAM.emergency;
  else if (lever > 0.01 && powered()) a = ((lever * cm.driven) / cm.mass) * std::min(TRAM.accel, (TRAM.accel * TRAM.powerFrom) / std::max(sp, 1.0));
  else if (lever < -0.01) a = lever * TRAM.brake;
  if (sp >= TRAM.vmax && a > 0) a = 0;
  // the doors are open: the tram is held (as by the door interlock)
  if (doorsOpen && a > 0) a = 0;
  // gradient: from the height of the track under the front and 10 m behind it
  net.sample(v.path, v.s, tmp);
  const double yFront = tmp.y;
  const int back = v.trail.at(std::min(v.trail.count - 1, 20));
  const double slope = (yFront - v.trail.y[static_cast<size_t>(back)]) / 10;
  a -= 9.81 * std::max(-0.1, std::min(0.1, slope));
  a -= TRAM.roll + TRAM.air * sp * sp;
  double speed = sp + a * dt;
  if (speed < 0) speed = 0;
  if (sp == 0 && a <= 0) speed = 0;
  // moving with the doors open (they were opened while rolling, or it rolls away): a fault
  if (doorsOpen && speed > 0.3) events.push_back(TramEvent{"tramDoorsMoving", "fail", {}});
  v.speed = speed;
  odometer += speed * dt;
  if (!rail.advance(v, speed * dt)) v.speed = 0;
  rail.place(v);

  // --- passengers at an open door
  if (doorsOpen && atStop_ && passengers() > 0) {
    nextPassenger_ -= dt;
    if (nextPassenger_ <= 0) {
      if (alighting > 0) alighting--;
      else boarding--;
      nextPassenger_ = 1.1 + random_() * 0.8;
      if (passengers() == 0 && !servedHere_) {
        servedHere_ = true;
        served++;
      }
    }
  }
  if (v.speed > 0.5 && atStop_) {
    // off again: the stop is done with
    atStop_.reset();
    servedHere_ = false;
  }

  look(dt, events);
}

void TramDriver::look(double dt, std::vector<TramEvent>& events) {
  const RailNetwork& net = rail.net;
  Ahead& A = ahead;
  A.stop = INF;
  A.curve = INF;
  A.switchIn = INF;
  A.signalIn = INF;
  A.signal = -1;
  A.wireEndIn = INF;
  const bool wiredHere = rail.wired(v.path);
  int cp = v.path;
  double cs = v.s;
  int cd = v.dir;
  double dist = 0;
  size_t planned = 0;
  int firstNode = -1;
  for (int guard = 0; guard < 200 && dist < 300; guard++) {
    const RailPath& p = net.paths[static_cast<size_t>(cp)];
    if (wiredHere && A.wireEndIn == INF && !rail.wired(cp)) A.wireEndIn = dist;
    net.sample(cp, cs, tmp);
    A.curve = std::min(A.curve, std::sqrt(tmp.cap * tmp.cap + 2 * TRAM.brake * 0.7 * dist));
    // a junction with lights the track runs through (within 80 m)
    if (dist < 80 && A.signalIn == INF && world_.surfaceAt(tmp.x, tmp.z, tmp.y + 1, hit_) && hit_.node && hit_.node->ctrl == CTRL_SIGNALS && hit_.node->sc >= 0) {
      const Node& node = *hit_.node;
      // the approach the tram comes in on: the arm pointing back the way it comes
      const double hx = tmp.tx * cd;
      const double hz = tmp.tz * cd;
      int best = -1;
      double bestDot = 0.3;
      for (size_t i = 0; i < node.arms.size(); i++) {
        const Arm& arm = *node.arms[i];
        const double dot = -(arm.dx * hx + arm.dz * hz);
        if (dot > bestDot && arm.sg >= 0) {
          bestDot = dot;
          best = static_cast<int>(i);
        }
      }
      if (best >= 0) {
        A.signalIn = dist;
        A.signal = signals_.get(node.sc, node.arms[static_cast<size_t>(best)]->sg);
        firstNode = node.id;
      }
    }
    const double room = cd > 0 ? p.len - cs : cs;
    for (double st : p.stops) {
      const double d = (st - cs) * cd;
      if (d > -STOP_PAST && d <= room + 0.01 && dist + d < A.stop) {
        A.stop = std::max(-STOP_PAST, dist + d);
        A.stopPath = cp;
        A.stopMark = st;
      }
    }
    const double step = 3;
    if (room > step) {
      cs += cd * step;
      dist += step;
      continue;
    }
    dist += room;
    const std::vector<RailNetwork::Way> ways = net.waysOn(cp, cd, v.consist->tracks);
    const std::optional<PathDir> next = planned < v.plan.size() ? std::optional<PathDir>(v.plan[planned]) : choose(cp, cd);
    if (ways.size() > 1 && A.switchIn == INF) {
      A.switchIn = dist;
      A.switchTurn = next && next->path != ways[0].path ? (wish != 0 ? wish : 0) : 0;
    }
    if (!next) break;
    planned++;
    cp = next->path;
    cd = next->dir;
    cs = cd > 0 ? 0 : net.paths[static_cast<size_t>(cp)].len;
  }

  // --- the speed limit here: the road's where the track runs in the street, else 70 km/h
  net.sample(v.path, v.s, tmp);
  double lim = 70;
  if (world_.surfaceAt(tmp.x, tmp.z, tmp.y + 1, hit_) && hit_.edge && hit_.zone == 0) {
    const Edge& e = *hit_.edge;
    const double m = std::max(e.maxF, e.maxB);
    if (m > 0) lim = std::min(70.0, m);
  }
  limit = lim;
  const double kmh = v.speed * 3.6;
  if (kmh > lim + 5) {
    overLimit_ += dt;
    if (overLimit_ > 2) {
      overLimit_ = -8;
      events.push_back(TramEvent{"speed", kmh > lim + 20 ? "major" : "minor", {{"limit", lim}, {"speed", jsRound(kmh)}}});
    }
  } else if (overLimit_ > 0) overLimit_ = 0;
  else overLimit_ = std::min(0.0, overLimit_ + dt);
  // too fast for the curve under the front: the tram could derail
  if (v.speed > tmp.cap + 2.5) events.push_back(TramEvent{"tramCurve", "major", {{"speed", jsRound(kmh)}, {"limit", jsRound(tmp.cap * 3.6)}}});

  // --- into a junction on red (the light was red for a moment already)
  if (A.signalIn < 1.5 && firstNode != lastSignalNode_) {
    lastSignalNode_ = firstNode;
    if ((A.signal == SIG_RED || A.signal == SIG_RED_AMBER) && v.speed > 0.5) events.push_back(TramEvent{"redLight", "fail", {}});
  }
  if (A.signalIn > 40) lastSignalNode_ = -1;

  // --- a stop passed without stopping at it
  const std::optional<PathMark> here = findStop();
  const bool known = here && lastAt_ && here->path == lastAt_->path && here->mark == lastAt_->mark;
  if (here && !pending_ && !known) pending_ = Pending{here->path, here->mark, v.s};
  if (pending_) {
    const Pending pd = *pending_;
    const bool servedNow = atStop_ && atStop_->path == pd.path && atStop_->mark == pd.mark;
    if (servedNow) pending_.reset();
    else if (v.path != pd.path || (v.s - pd.mark) * v.dir > STOP_PAST + 4) {
      events.push_back(TramEvent{"tramStopMissed", "major", {}});
      pending_.reset();
    }
  }
}

std::optional<TramStart> tramStart(const RailTraffic& rail, double x, double z, const std::vector<int>& kinds, const std::string& name) {
  const RailNetwork& net = rail.net;
  std::optional<TramStart> best;
  double bd = INF;
  RailSample smp;
  for (size_t i = 0; i < net.paths.size(); i++) {
    const RailPath& p = net.paths[i];
    if (std::find(kinds.begin(), kinds.end(), p.kind) == kinds.end()) continue;
    for (size_t k = 0; k < p.stops.size(); k++) {
      const double s = p.stops[k];
      net.sample(static_cast<int>(i), s, smp);
      // (a stop of the name asked for comes first: a platform of it may lie further away than another stop)
      const double d = std::hypot(smp.x - x, smp.z - z) + (!name.empty() && p.stopNames[k] != name ? 5000 : 0);
      if (d >= bd) continue;
      bd = d;
      // the way the trams run there: as a line runs, else with the platform on the right (they keep to the right)
      const int side = k < p.stopSides.size() ? p.stopSides[k] : 0;
      const int dir = p.way != 0 ? p.way : rail.hasLine(static_cast<int>(i), 1) ? 1 : rail.hasLine(static_cast<int>(i), -1) ? -1 : side < 0 ? -1 : 1;
      best = TramStart{static_cast<int>(i), s, dir, smp.x, smp.z};
    }
  }
  return best;
}

}  // namespace fs
