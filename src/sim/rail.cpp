// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Trams and trains (origin: src/sim/rail.ts @ ff37c5d, v0.7.0): the track
 * network built from the map's rails, and the vehicles that run on it.
 */
#include "sim/rail.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fs {

namespace {

constexpr double TRAIL_STEP = 0.5;
/** switches are followed up to this change of direction (cos 42°): sharper is a crossing track, not a way on */
constexpr double MAX_TURN_COS = 0.74;

double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** the sample the network fills once per query (as the original's `tmp`) */
struct Sample {
  double x = 0, y = 0, z = 0, tx = 0, tz = 0, cap = 0;
};
Sample gSample;

const Consist CONSISTS[3] = {
    // a low-floor city tram of three sections
    {3, 9.4, 0.7, 2.4, 3.35, 13.9, 1.0, 1.3, {RAIL_TRAM, RAIL_LIGHT}},
    // a regional tram that also uses railway lines
    {3, 11.8, 0.8, 2.65, 3.6, 19.4, 1.0, 1.2, {RAIL_TRAM, RAIL_LIGHT, RAIL_TRAIN}},
    // a regional train of three coaches
    {3, 23, 1.0, 2.85, 4.0, 25, 0.7, 0.9, {RAIL_TRAIN, RAIL_LIGHT}},
};

}  // namespace

const Consist* consistOf(int kind) {
  if (kind < 0 || kind > 2) kind = RAIL_TRAM;
  return &CONSISTS[kind];
}

void RailVehicle::Trail::init(int nIn) {
  n = nIn;
  x.assign(static_cast<size_t>(n), 0);
  y.assign(static_cast<size_t>(n), 0);
  z.assign(static_cast<size_t>(n), 0);
  s.assign(static_cast<size_t>(n), 0);
  path.assign(static_cast<size_t>(n), 0);
  dir.assign(static_cast<size_t>(n), 0);
  head = 0;
  count = 0;
  since = 0;
}

void RailVehicle::Trail::push(double px, double py, double pz, int pathIn, float sIn, int dirIn) {
  head = (head + 1) % n;
  x[static_cast<size_t>(head)] = px;
  y[static_cast<size_t>(head)] = py;
  z[static_cast<size_t>(head)] = pz;
  s[static_cast<size_t>(head)] = sIn;
  path[static_cast<size_t>(head)] = pathIn;
  dir[static_cast<size_t>(head)] = static_cast<int8_t>(dirIn);
  if (count < n) count++;
}

// ---------------------------------------------------------------------------
// RailNetwork
// ---------------------------------------------------------------------------

RailNetwork::RailNetwork(const std::vector<RailJson>& rails) {
  paths.reserve(rails.size());
  for (const RailJson& r : rails) {
    size_t n = r.p.size() / 3;
    RailPath p;
    p.kind = r.k;
    p.pts.assign(r.p.begin(), r.p.begin() + static_cast<long>(n * 3));
    p.cum.assign(n, 0);
    for (size_t i = 1; i < n; i++)
      p.cum[i] = p.cum[i - 1] +
                 std::hypot(p.pts[i * 3] - p.pts[i * 3 - 3], p.pts[i * 3 + 2] - p.pts[i * 3 - 1]);
    p.len = n > 0 ? p.cum[n - 1] : 0;
    p.a = r.a;
    p.b = r.b;
    p.stops = r.st;
    p.cap.assign(n, 99.0f);
    // radius of the curve through the points a dozen metres before and behind: v² / r stays gentle
    for (size_t i = 0; i < n; i++) {
      size_t a = i >= 2 ? i - 2 : 0;
      size_t b = std::min(n - 1, i + 2);
      if (a == i || b == i) continue;
      double ax = p.pts[i * 3] - p.pts[a * 3];
      double az = p.pts[i * 3 + 2] - p.pts[a * 3 + 2];
      double bx = p.pts[b * 3] - p.pts[i * 3];
      double bz = p.pts[b * 3 + 2] - p.pts[i * 3 + 2];
      double la = std::hypot(ax, az), lb = std::hypot(bx, bz);
      if (la < 0.5 || lb < 0.5) continue;
      double turn = std::fabs(std::atan2(ax * bz - az * bx, ax * bx + az * bz));
      if (turn > 0.01) p.cap[i] = static_cast<float>(std::sqrt(1.1 * ((la + lb) / 2 / turn)));
    }
    paths.push_back(std::move(p));
  }
  // points to start from, bucketed by 200 m cells: pairs of (piece, metres)
  for (size_t pi = 0; pi < paths.size(); pi++) {
    const RailPath& p = paths[pi];
    size_t n = p.cum.size();
    for (size_t i = 0; i < n; i += 4) {
      auto key = cell(p.pts[i * 3], p.pts[i * 3 + 2]);
      auto& list = cells_[key];
      list.push_back(static_cast<double>(pi));
      list.push_back(p.cum[i]);
    }
  }
  // double track: on which side does the neighbouring track lie?
  for (size_t pi = 0; pi < paths.size(); pi++) {
    RailPath& p = paths[pi];
    size_t n = p.cum.size();
    if (n < 2) continue;
    size_t m = n >> 1;
    size_t a = m >= 1 ? m - 1 : 0;
    double mx = (p.pts[a * 3] + p.pts[m * 3]) / 2;
    double mz = (p.pts[a * 3 + 2] + p.pts[m * 3 + 2]) / 2;
    double tx = p.pts[m * 3] - p.pts[a * 3];
    double tz = p.pts[m * 3 + 2] - p.pts[a * 3 + 2];
    double tl = std::hypot(tx, tz);
    if (tl < 1e-9) tl = 1;
    tx /= tl;
    tz /= tl;
    double best = 9;
    int side = 0;
    std::vector<char> seen(paths.size(), 0);
    for (int cx = -1; cx <= 1; cx++) {
      for (int cz = -1; cz <= 1; cz++) {
        std::vector<double> list = near(mx + cx * 200, mz + cz * 200);
        for (size_t k = 0; k < list.size(); k += 2) {
          size_t qi = static_cast<size_t>(list[k]);
          if (qi == pi || seen[qi]) continue;
          seen[qi] = 1;
          const RailPath& q = paths[qi];
          for (size_t j = 1; j < q.cum.size(); j++) {
            // nearest point of this stretch of the other track
            double x0 = q.pts[j * 3 - 3];
            double z0 = q.pts[j * 3 - 1];
            double dx = q.pts[j * 3] - x0;
            double dz = q.pts[j * 3 + 2] - z0;
            double l2 = dx * dx + dz * dz;
            if (l2 < 1e-6) continue;
            double u = clampD(((mx - x0) * dx + (mz - z0) * dz) / l2, 0, 1);
            double ox = x0 + dx * u - mx;
            double oz = z0 + dz * u - mz;
            double d = std::hypot(ox, oz);
            // a neighbour runs alongside, two to eight metres away
            if (d < 2 || d >= best || std::fabs(dx * tx + dz * tz) / std::sqrt(l2) < 0.9) continue;
            best = d;
            // left of the drawn direction (x east, z south): the cross product tells
            side = tx * oz - tz * ox < 0 ? 1 : -1;
          }
        }
      }
    }
    p.way = side;
  }
}

long long RailNetwork::cell(double x, double z) {
  return (static_cast<long long>(std::floor(x / 200)) + 4096) * 8192 +
         static_cast<long long>(std::floor(z / 200)) + 4096;
}

std::vector<double> RailNetwork::near(double x, double z) const {
  auto it = cells_.find(cell(x, z));
  return it == cells_.end() ? std::vector<double>() : it->second;
}

void RailNetwork::sample(int pathIn, double s, double& x, double& y, double& z, double& tx,
                         double& tz, double& cap) const {
  const RailPath& p = paths[static_cast<size_t>(pathIn)];
  size_t n = p.cum.size();
  if (n == 0) return;
  size_t lo = 0, hi = n - 1;
  while (hi - lo > 1) {
    size_t mid = (lo + hi) >> 1;
    if (p.cum[mid] <= s) lo = mid;
    else hi = mid;
  }
  double seg = p.cum[hi] - p.cum[lo];
  double t = seg > 1e-6 ? clampD((s - p.cum[lo]) / seg, 0, 1) : 0;
  size_t a = lo * 3, b = hi * 3;
  x = p.pts[a] + (p.pts[b] - p.pts[a]) * t;
  y = p.pts[a + 1] + (p.pts[b + 1] - p.pts[a + 1]) * t;
  z = p.pts[a + 2] + (p.pts[b + 2] - p.pts[a + 2]) * t;
  double l = std::hypot(p.pts[b] - p.pts[a], p.pts[b + 2] - p.pts[a + 2]);
  if (l < 1e-9) l = 1;
  tx = (p.pts[b] - p.pts[a]) / l;
  tz = (p.pts[b + 2] - p.pts[a + 2]) / l;
  cap = std::min(p.cap[lo], p.cap[hi]);
}

std::vector<RailNetwork::Way> RailNetwork::waysOn(int pathIn, int dir,
                                                  const std::vector<int>& tracks) const {
  const RailPath& p = paths[static_cast<size_t>(pathIn)];
  size_t n = p.cum.size();
  std::vector<Way> out;
  if (n < 2) return out;
  // direction of travel at the end that is being left
  size_t e = dir > 0 ? n - 1 : 0;
  size_t f = dir > 0 ? n - 2 : 1;
  double hx = p.pts[e * 3] - p.pts[f * 3];
  double hz = p.pts[e * 3 + 2] - p.pts[f * 3 + 2];
  double hl = std::hypot(hx, hz);
  if (hl < 1e-9) hl = 1;
  hx /= hl;
  hz /= hl;
  const std::vector<int>& links = dir > 0 ? p.b : p.a;
  for (int link : links) {
    int qi = link >> 1;
    if (qi < 0 || static_cast<size_t>(qi) >= paths.size()) continue;
    const RailPath& q = paths[static_cast<size_t>(qi)];
    if (q.cum.size() < 2) continue;
    if (std::find(tracks.begin(), tracks.end(), q.kind) == tracks.end()) continue;
    // entered at its start it is run along, entered at its end against
    int qdir = (link & 1) == 0 ? 1 : -1;
    size_t m = q.cum.size();
    size_t i0 = qdir > 0 ? 0 : m - 1;
    size_t i1 = qdir > 0 ? 1 : m - 2;
    double dx = q.pts[i1 * 3] - q.pts[i0 * 3];
    double dz = q.pts[i1 * 3 + 2] - q.pts[i0 * 3 + 2];
    double dl = std::hypot(dx, dz);
    if (dl < 1e-9) dl = 1;
    double straight = (dx * hx + dz * hz) / dl;
    if (straight >= MAX_TURN_COS) out.push_back({qi, qdir, straight});
  }
  std::sort(out.begin(), out.end(), [](const Way& a, const Way& b) { return a.straight > b.straight; });
  return out;
}

// ---------------------------------------------------------------------------
// RailTraffic
// ---------------------------------------------------------------------------

namespace {
/** the second sample the placement uses (as the original's pivotA/pivotB) */
struct Pivot {
  double x, y, z;
};
}  // namespace

RailTraffic::RailTraffic(const std::vector<RailJson>& rails, std::function<double()> random)
    : net(rails),
      random_(random ? std::move(random)
                     : std::function<double()>([] { return static_cast<double>(std::rand()) / RAND_MAX; })) {}

RailVehicle* RailTraffic::spawnAt(int pathIn, double s, int dir, double speed) {
  if (pathIn < 0 || static_cast<size_t>(pathIn) >= net.paths.size()) return nullptr;
  RailPath& p = net.paths[static_cast<size_t>(pathIn)];
  if (p.len < 2) return nullptr;
  const Consist* consist = consistOf(p.kind);
  double length = consist->cars * consist->carLen + (consist->cars - 1) * consist->gap;
  int n = static_cast<int>(std::ceil((length + 6) / TRAIL_STEP)) + 8;
  RailVehicle v;
  v.trail.init(n);
  // lay the vehicle out behind its front end: walk back along the track
  struct Back {
    double x, y, z;
    int path;
    double s;
    int dir;
  };
  std::vector<Back> back;
  back.reserve(static_cast<size_t>(n));
  int cp = pathIn;
  double cs = clampD(s, 0, p.len);
  int cd = dir > 0 ? -1 : 1;
  for (int k = 0; k < n; k++) {
    double sx, sy, sz, tx, tz, cap;
    net.sample(cp, cs, sx, sy, sz, tx, tz, cap);
    // (recorded with the direction the vehicle faces, not the one walked)
    back.push_back({sx, sy, sz, cp, cs, -cd});
    double step = TRAIL_STEP;
    while (step > 0) {
      const RailPath& q = net.paths[static_cast<size_t>(cp)];
      double room = cd > 0 ? q.len - cs : cs;
      if (room >= step) {
        cs += cd * step;
        step = 0;
      } else {
        step -= room;
        std::vector<RailNetwork::Way> ways = net.waysOn(cp, cd, consist->tracks);
        if (ways.empty()) return nullptr;
        cp = ways[0].path;
        cd = ways[0].dir;
        cs = cd > 0 ? 0 : net.paths[static_cast<size_t>(cp)].len;
      }
    }
  }
  for (auto it = back.rbegin(); it != back.rend(); ++it)
    v.trail.push(it->x, it->y, it->z, it->path, static_cast<float>(it->s), it->dir);

  v.id = nextId_++;
  v.kind = p.kind;
  v.consist = consist;
  v.path = pathIn;
  v.s = clampD(s, 0, p.len);
  v.dir = dir;
  v.speed = speed;
  v.dwell = 0;
  v.noStop = 30;
  v.cars.assign(static_cast<size_t>(consist->cars), RailCar());
  v.blocked = false;
  place(v);
  vehicles.push_back(std::move(v));
  return &vehicles.back();
}

void RailTraffic::circles(const RailVehicle* skip, std::vector<RailObstacle>& out) const {
  for (const RailVehicle& v : vehicles) {
    if (&v == skip) continue;
    double half = v.consist->carLen / 2;
    for (const RailCar& c : v.cars) {
      double fx = -std::sin(c.yaw);
      double fz = -std::cos(c.yaw);
      for (double k : {-0.7, 0.0, 0.7})
        out.push_back({c.x + fx * half * k, c.z + fz * half * k, v.consist->width / 2 + 0.2});
    }
  }
}

void RailTraffic::update(double dt, const RailPlayer& player, const std::vector<RailObstacle>& others) {
  // come and go with the player
  for (size_t i = vehicles.size(); i-- > 0;) {
    const RailCar& c = vehicles[i].cars[0];
    if (std::hypot(c.x - player.x, c.z - player.z) > far) vehicles.erase(vehicles.begin() + static_cast<long>(i));
  }
  spawnT_ -= dt;
  if (spawnT_ <= 0 && static_cast<int>(vehicles.size()) < target) {
    spawnT_ = 1.5;
    spawnNear(player);
  }

  for (RailVehicle& v : vehicles) {
    obstacles_.clear();
    // the player's car: two circles along its length
    double px = -std::sin(player.yaw);
    double pz = -std::cos(player.yaw);
    double off = std::max(0.0, player.length / 2 - player.width / 2);
    obstacles_.push_back({player.x + px * off, player.z + pz * off, player.width / 2 + 0.15});
    obstacles_.push_back({player.x - px * off, player.z - pz * off, player.width / 2 + 0.15});
    if (!v.cars.empty()) {
      for (const RailObstacle& o : others)
        if (std::fabs(o.x - v.cars[0].x) < 120 && std::fabs(o.z - v.cars[0].z) < 120)
          obstacles_.push_back(o);
    }
    circles(&v, obstacles_);
    step(v, dt, obstacles_);
  }
}

RailVehicle* RailTraffic::spawnNear(const RailPlayer& player) {
  for (int tries = 0; tries < 10; tries++) {
    double ang = random_() * M_PI * 2;
    double rad = ringMin + random_() * (ringMax - ringMin);
    std::vector<double> list = net.near(player.x + std::cos(ang) * rad, player.z + std::sin(ang) * rad);
    if (list.empty()) continue;
    size_t k = static_cast<size_t>(std::floor(random_() * (list.size() / 2))) * 2;
    int path = static_cast<int>(list[k]);
    double s = list[k + 1];
    double sx, sy, sz, tx, tz, cap;
    net.sample(path, s, sx, sy, sz, tx, tz, cap);
    double d = std::hypot(sx - player.x, sz - player.z);
    if (d < ringMin || d > ringMax + 150) continue;
    // not into another one
    bool clash = false;
    for (const RailVehicle& v : vehicles)
      for (const RailCar& c : v.cars)
        if (std::hypot(c.x - sx, c.z - sz) < 160) clash = true;
    if (clash) continue;
    // on a double track keep to the right-hand one; a single track is run both ways
    int way = net.paths[static_cast<size_t>(path)].way;
    int dir = way != 0 ? way : (random_() < 0.5 ? 1 : -1);
    RailVehicle* v = spawnAt(path, s, dir, 0);
    if (v) {
      v->speed = std::min(v->consist->vmax * 0.6, cap);
      return v;
    }
  }
  return nullptr;
}

void RailTraffic::step(RailVehicle& v, double dt, const std::vector<RailObstacle>& obstacles) {
  const Consist& c = *v.consist;
  if (v.dwell > 0) {
    v.dwell -= dt;
    v.speed = 0;
    place(v);
    return;
  }
  // ---- look ahead along the way it will take: curves, stops, the end of the
  // line, things on the track
  double reach = (v.speed * v.speed) / (2 * c.brake) + 25;
  double allowed = c.vmax;
  double stopIn = INFINITY, blockIn = INFINITY, endIn = INFINITY;
  int cp = v.path;
  double cs = v.s;
  int cd = v.dir;
  int planned = 0;
  double dist = 0;
  double halfW = c.width / 2;
  while (dist <= reach) {
    const RailPath& p = net.paths[static_cast<size_t>(cp)];
    double sx, sy, sz, tx, tz, cap;
    net.sample(cp, cs, sx, sy, sz, tx, tz, cap);
    // a curve further on limits the speed now by what braking takes
    allowed = std::min(allowed, std::sqrt(cap * cap + 2 * c.brake * 0.7 * dist));
    if (dist > 1) {
      for (const RailObstacle& o : obstacles) {
        double d = std::hypot(o.x - sx, o.z - sz);
        if (d < o.r + halfW + 0.25) {
          blockIn = std::min(blockIn, dist);
          break;
        }
      }
      if (blockIn < INFINITY) break;
    }
    const double stepLen = 2.5;
    double room = cd > 0 ? p.len - cs : cs;
    // stops on this piece within the next step
    if (v.noStop <= dist) {
      for (double st : p.stops) {
        double ahead = (st - cs) * cd;
        if (ahead >= 0 && ahead <= std::min(stepLen, room) + 0.01)
          stopIn = std::min(stopIn, dist + ahead);
      }
    }
    if (room > stepLen) {
      cs += cd * stepLen;
      dist += stepLen;
      continue;
    }
    // onto the next piece: the one planned, or choose now
    dist += room;
    std::pair<int, int> next;
    bool haveNext = static_cast<size_t>(planned) < v.plan.size();
    if (haveNext) {
      next = v.plan[static_cast<size_t>(planned)];
    } else {
      std::vector<RailNetwork::Way> ways = net.waysOn(cp, cd, c.tracks);
      if (!ways.empty()) {
        // mostly straight on, now and then the branch
        const RailNetwork::Way& pick =
            ways.size() > 1 && random_() < 0.3
                ? ways[1 + static_cast<size_t>(random_() * (ways.size() - 1))]
                : ways[0];
        next = {pick.path, pick.dir};
        v.plan.push_back(next);
        haveNext = true;
      }
    }
    if (!haveNext) {
      endIn = dist;
      break;
    }
    planned++;
    cp = next.first;
    cd = next.second;
    cs = cd > 0 ? 0 : net.paths[static_cast<size_t>(cp)].len;
  }

  // ---- speed
  v.blocked = blockIn < INFINITY;
  auto stopFor = [](double d, double margin, double brake) {
    return std::sqrt(2 * brake * std::max(0.0, d - margin));
  };
  double target = allowed;
  if (stopIn < INFINITY) target = std::min(target, stopFor(stopIn, 0.3, c.brake * 0.8));
  if (endIn < INFINITY) target = std::min(target, stopFor(endIn, 1.5, c.brake * 0.8));
  if (blockIn < INFINITY) target = std::min(target, stopFor(blockIn, 4.5, c.brake * 1.6));
  if (v.speed < target) v.speed = std::min(target, v.speed + c.accel * dt);
  else v.speed = std::max(target, v.speed - (blockIn < INFINITY ? c.brake * 2 : c.brake) * dt);
  if (v.speed < 0.05 && target < 0.3) v.speed = 0;

  // ---- arrived?
  if (v.speed == 0) {
    if (stopIn < 1.2) {
      v.dwell = 12 + random_() * 8;
      v.noStop = 70;
    } else if (endIn < 3 && blockIn == INFINITY) {
      // the end of the line: wait, then go back the way it came
      reverse(v);
      v.dwell = 20 + random_() * 10;
      v.noStop = 40;
    }
    place(v);
    return;
  }

  // ---- move the front end, leaving the trail the cars follow
  double move = v.speed * dt;
  v.noStop = std::max(0.0, v.noStop - move);
  while (move > 0) {
    const RailPath& p = net.paths[static_cast<size_t>(v.path)];
    double room = v.dir > 0 ? p.len - v.s : v.s;
    double go = std::min({move, room, TRAIL_STEP - v.trail.since});
    if (go < 0) go = 0;
    v.s += v.dir * go;
    move -= go;
    v.trail.since = static_cast<float>(v.trail.since + go);
    if (v.trail.since >= TRAIL_STEP - 1e-9) {
      double sx, sy, sz, tx, tz, cap;
      net.sample(v.path, v.s, sx, sy, sz, tx, tz, cap);
      v.trail.push(sx, sy, sz, v.path, static_cast<float>(v.s), v.dir);
      v.trail.since = 0;
    }
    if (go >= room - 1e-9 && move > 0) {
      if (v.plan.empty()) {
        v.speed = 0;
        break;
      }
      std::pair<int, int> next = v.plan.front();
      v.plan.erase(v.plan.begin());
      v.path = next.first;
      v.dir = next.second;
      v.s = v.dir > 0 ? 0 : net.paths[static_cast<size_t>(v.path)].len;
    }
  }
  place(v);
}

/** The rear end becomes the front end. */
void RailTraffic::reverse(RailVehicle& v) {
  RailVehicle::Trail& t = v.trail;
  double length = v.consist->cars * v.consist->carLen + (v.consist->cars - 1) * v.consist->gap;
  int k = std::min(t.count - 1, static_cast<int>(std::lround(length / TRAIL_STEP)));
  int tail = t.at(k);
  struct Entry {
    double x, y, z;
    int path;
    float s;
    int dir;
  };
  std::vector<Entry> entries;
  // from the old front end to the old rear end: that is oldest … newest of the way back
  for (int i = 0; i <= k; i++) {
    int e = t.at(i);
    entries.push_back({t.x[static_cast<size_t>(e)], t.y[static_cast<size_t>(e)],
                       t.z[static_cast<size_t>(e)], t.path[static_cast<size_t>(e)],
                       t.s[static_cast<size_t>(e)], -t.dir[static_cast<size_t>(e)]});
  }
  v.path = t.path[static_cast<size_t>(tail)];
  v.s = t.s[static_cast<size_t>(tail)];
  v.dir = t.dir[static_cast<size_t>(tail)] > 0 ? -1 : 1;
  t.count = 0;
  t.since = 0;
  for (const Entry& e : entries) t.push(e.x, e.y, e.z, e.path, e.s, e.dir);
  v.plan.clear();
}

/** Put the cars on the trail behind the front end. */
void RailTraffic::place(RailVehicle& v) {
  const Consist& c = *v.consist;
  RailVehicle::Trail& t = v.trail;
  auto at = [&](double dist, Pivot& out) {
    // (the front end is `since` metres past the newest entry of the trail)
    double f = t.count > 0 ? clampD((dist - t.since) / TRAIL_STEP, 0, t.count - 1.001) : 0;
    int i = static_cast<int>(std::floor(f));
    double u = f - i;
    int a = t.at(i);
    int b = t.at(i + 1);
    out.x = t.x[static_cast<size_t>(a)] +
            (t.x[static_cast<size_t>(b)] - t.x[static_cast<size_t>(a)]) * u;
    out.y = t.y[static_cast<size_t>(a)] +
            (t.y[static_cast<size_t>(b)] - t.y[static_cast<size_t>(a)]) * u;
    out.z = t.z[static_cast<size_t>(a)] +
            (t.z[static_cast<size_t>(b)] - t.z[static_cast<size_t>(a)]) * u;
  };
  Pivot pivotA{0, 0, 0}, pivotB{0, 0, 0};
  for (int i = 0; i < c.cars; i++) {
    double start = i * (c.carLen + c.gap);
    // each car rides on two pivots a little in from its ends
    at(start + c.carLen * 0.14, pivotA);
    at(start + c.carLen * 0.86, pivotB);
    RailCar& car = v.cars[static_cast<size_t>(i)];
    double fx = pivotA.x - pivotB.x;
    double fy = pivotA.y - pivotB.y;
    double fz = pivotA.z - pivotB.z;
    car.x = (pivotA.x + pivotB.x) / 2;
    car.y = (pivotA.y + pivotB.y) / 2;
    car.z = (pivotA.z + pivotB.z) / 2;
    double flat = std::hypot(fx, fz);
    if (flat > 1e-6) {
      car.yaw = std::atan2(-fx, -fz);
      car.pitch = std::atan2(fy, flat);
    }
  }
}

bool boxOverlap(double ax, double az, double ayaw, double ahl, double ahw, double bx, double bz,
                double byaw, double bhl, double bhw, double& nx, double& nz, double& depth) {
  // forward and right of each rectangle
  const double axes[4][2] = {
      {-std::sin(ayaw), -std::cos(ayaw)},
      {std::cos(ayaw), -std::sin(ayaw)},
      {-std::sin(byaw), -std::cos(byaw)},
      {std::cos(byaw), -std::sin(byaw)},
  };
  double dx = ax - bx;
  double dz = az - bz;
  depth = INFINITY;
  nx = 0;
  nz = 0;
  for (int k = 0; k < 4; k++) {
    double ux = axes[k][0], uz = axes[k][1];
    // half extent of each rectangle along this axis
    double ra = ahl * std::fabs(axes[0][0] * ux + axes[0][1] * uz) +
                ahw * std::fabs(axes[1][0] * ux + axes[1][1] * uz);
    double rb = bhl * std::fabs(axes[2][0] * ux + axes[2][1] * uz) +
                bhw * std::fabs(axes[3][0] * ux + axes[3][1] * uz);
    double d = dx * ux + dz * uz;
    double over = ra + rb - std::fabs(d);
    if (over <= 0) return false;
    if (over < depth) {
      depth = over;
      double sign = d >= 0 ? 1 : -1;
      nx = ux * sign;
      nz = uz * sign;
    }
  }
  return true;
}

const RailTraffic::Touch* RailTraffic::touching(const RailPlayer& player) {
  bool any = false;
  touch_ = Touch();
  for (const RailVehicle& v : vehicles) {
    for (const RailCar& car : v.cars) {
      if (std::fabs(car.y - player.y) > 4) continue;
      double reach = v.consist->carLen / 2 + player.length;
      if (std::fabs(car.x - player.x) > reach || std::fabs(car.z - player.z) > reach) continue;
      double nx, nz, depth;
      if (boxOverlap(player.x, player.z, player.yaw, player.length / 2, player.width / 2, car.x,
                     car.z, car.yaw, v.consist->carLen / 2, v.consist->width / 2, nx, nz, depth)) {
        if (!any || depth > touch_.depth) {
          any = true;
          touch_ = Touch();
          touch_.nx = nx;
          touch_.nz = nz;
          touch_.depth = depth;
          touch_.vx = -std::sin(car.yaw) * v.speed;
          touch_.vz = -std::cos(car.yaw) * v.speed;
        }
      }
    }
  }
  return any ? &touch_ : nullptr;
}

}  // namespace fs
