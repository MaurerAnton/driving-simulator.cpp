// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/rail.ts @ v1.7.7
#include "sim/rail.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_set>

#include "core/js.h"
#include "core/random.h"

namespace fs {

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double TRAIL_STEP = 0.5;
/** switches are followed up to this change of direction (cos 42°): sharper is a crossing track, not a way on */
constexpr double MAX_TURN_COS = 0.74;
/** the slowest a tram of the timetable runs to keep to it (m/s) – earlier than that, it waits at the stop */
constexpr double PACE_MIN = 4;
/** a tram whose trip has ended goes when it is further than this from the player (m) */
constexpr double RETIRE_NEAR = 120;
/** a tram this far from where its trip is by the timetable (m) is put there anew, out of the player's sight */
constexpr double MISPLACED = 300;

double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

RailSample tmp;

std::string key(int path, int dir) { return std::to_string(path) + ":" + std::to_string(dir); }

}  // namespace

// ---------------------------------------------------------------------------
// consists
// ---------------------------------------------------------------------------

const std::map<int, Consist>& CONSISTS() {
  static auto* t = new std::map<int, Consist>{
      // a low-floor city tram of three sections
      {RAIL_TRAM, Consist{3, 9.4, 0.7, 2.4, 3.35, 13.9, 1.0, 1.3, {RAIL_TRAM, RAIL_LIGHT}, {}, {}, {}}},
      // a regional tram that also uses railway lines
      {RAIL_LIGHT, Consist{3, 11.8, 0.8, 2.65, 3.6, 19.4, 1.0, 1.2, {RAIL_TRAM, RAIL_LIGHT, RAIL_TRAIN}, {}, {}, {}}},
      // a regional train of three coaches
      {RAIL_TRAIN, Consist{3, 23, 1.0, 2.85, 4.0, 25, 0.7, 0.9, {RAIL_TRAIN, RAIL_LIGHT}, {}, {}, {}}},
  };
  return *t;
}

const std::vector<CarRole>& carLayout(const Consist& c) {
  static auto* layouts = new std::unordered_map<const Consist*, std::vector<CarRole>>();
  auto it = layouts->find(&c);
  if (it != layouts->end()) return it->second;
  std::vector<CarRole> list;
  const int units = std::max(1, c.units.value_or(1));
  const double couple = c.couple.value_or(1.3);
  double at = 0;
  for (int u = 0; u < units; u++) {
    for (int i = 0; i < c.cars; i++) {
      const bool last = i == c.cars - 1;
      CarRole r;
      r.unit = u;
      r.start = at;
      r.len = c.carLen;
      r.front = i == 0;
      r.back = last;
      r.lampsFront = u == 0 && i == 0;
      r.lampsBack = false;
      r.trailer = false;
      r.panto = c.cars == 1 || i == (c.cars - 1) / 2;
      r.bellows = !last;
      list.push_back(r);
      at += c.carLen + (last ? couple : c.gap);
    }
  }
  if (c.trailer && *c.trailer != 0) {
    CarRole r;
    r.unit = units;
    r.start = at;
    r.len = *c.trailer;
    r.trailer = true;
    list.push_back(r);
  }
  list.back().lampsBack = true;
  return layouts->emplace(&c, std::move(list)).first->second;
}

double consistLength(const Consist& c) {
  const std::vector<CarRole>& l = carLayout(c);
  const CarRole& last = l.back();
  return last.start + last.len;
}

const Consist* consistOf(int kind, const TramSet& set) {
  const std::map<int, Consist>& all = CONSISTS();
  auto it = all.find(kind);
  const Consist& base = it != all.end() ? it->second : all.at(RAIL_TRAM);
  if (set == "single" || kind == RAIL_TRAIN) return &base;
  static auto* variants = new std::map<std::string, Consist>();
  const std::string k = std::to_string(kind) + ":" + set;
  auto v = variants->find(k);
  if (v == variants->end()) {
    Consist c = base;
    if (set == "double") {
      c.units = 2;
      c.couple = 1.3;
    } else {
      c.trailer = kind == RAIL_LIGHT ? 14 : 12.4;
      c.couple = 1.1;
    }
    v = variants->emplace(k, c).first;
  }
  return &v->second;
}

ConsistMass consistMass(const Consist& c) {
  const int units = std::max(1, c.units.value_or(1));
  const double unitLen = c.cars * c.carLen;
  const double trailer = c.trailer && *c.trailer != 0 ? (*c.trailer / unitLen) * 0.8 : 0;
  return ConsistMass{units + trailer, static_cast<double>(units)};
}

// ---------------------------------------------------------------------------
// Trail
// ---------------------------------------------------------------------------

Trail::Trail(int nIn)
    : n(nIn),
      x(static_cast<size_t>(nIn), 0),
      y(static_cast<size_t>(nIn), 0),
      z(static_cast<size_t>(nIn), 0),
      path(static_cast<size_t>(nIn), 0),
      s(static_cast<size_t>(nIn), 0),
      dir(static_cast<size_t>(nIn), 0) {}

void Trail::push(double px, double py, double pz, int pathIn, double sIn, int dirIn) {
  head = (head + 1) % n;
  const size_t h = static_cast<size_t>(head);
  x[h] = px;
  y[h] = py;
  z[h] = pz;
  path[h] = pathIn;
  s[h] = static_cast<float>(sIn);
  dir[h] = static_cast<int8_t>(dirIn);
  if (count < n) count++;
}

// ---------------------------------------------------------------------------
// RailNetwork
// ---------------------------------------------------------------------------

RailNetwork::RailNetwork(const std::vector<RailJson>& rails) {
  for (const RailJson& r : rails) {
    const size_t n = r.p.size() / 3;
    RailPath p;
    p.kind = r.k;
    p.pts.assign(r.p.begin(), r.p.begin() + static_cast<long>(n * 3));
    p.cum.assign(n, 0);
    const std::vector<double>& pts = p.pts;
    for (size_t i = 1; i < n; i++) p.cum[i] = p.cum[i - 1] + std::hypot(pts[i * 3] - pts[i * 3 - 3], pts[i * 3 + 2] - pts[i * 3 - 1]);
    p.cap.assign(n, 99.0f);
    // radius of the curve through the points a dozen metres before and behind: v² / r stays gentle
    for (size_t i = 0; i < n; i++) {
      const size_t a = i >= 2 ? i - 2 : 0;
      const size_t b = std::min(n - 1, i + 2);
      if (a == i || b == i) continue;
      const double ax = pts[i * 3] - pts[a * 3];
      const double az = pts[i * 3 + 2] - pts[a * 3 + 2];
      const double bx = pts[b * 3] - pts[i * 3];
      const double bz = pts[b * 3 + 2] - pts[i * 3 + 2];
      const double la = std::hypot(ax, az);
      const double lb = std::hypot(bx, bz);
      if (la < 0.5 || lb < 0.5) continue;
      const double turn = std::abs(std::atan2(ax * bz - az * bx, ax * bx + az * bz));
      if (turn > 0.01) p.cap[i] = static_cast<float>(std::sqrt(1.1 * ((la + lb) / 2 / turn)));
    }
    p.len = n > 0 ? p.cum[n - 1] : 0;
    p.a = r.a;
    p.b = r.b;
    p.stops = r.st;
    for (size_t i = 0; i < p.stops.size(); i++) {
      p.stopNames.push_back(i < r.sn.size() ? r.sn[i] : "");
      p.stopSides.push_back(i < r.ss.size() ? r.ss[i] : 0);
    }
    p.wire = r.e ? *r.e : -1;
    p.way = 0;
    paths.push_back(std::move(p));
  }
  for (size_t pi = 0; pi < paths.size(); pi++) {
    const RailPath& p = paths[pi];
    const size_t n = p.cum.size();
    for (size_t i = 0; i < n; i += 4) {
      std::vector<double>& list = cells_[cell(p.pts[i * 3], p.pts[i * 3 + 2])];
      list.push_back(static_cast<double>(pi));
      list.push_back(p.cum[i]);
    }
  }
  // double track: on which side does the neighbouring track lie?
  for (size_t pi = 0; pi < paths.size(); pi++) {
    RailPath& p = paths[pi];
    const size_t n = p.cum.size();
    if (n < 2) continue;
    const size_t m = n >> 1;
    const size_t a = m >= 1 ? m - 1 : 0;
    const double mx = (p.pts[a * 3] + p.pts[m * 3]) / 2;
    const double mz = (p.pts[a * 3 + 2] + p.pts[m * 3 + 2]) / 2;
    double tx = p.pts[m * 3] - p.pts[a * 3];
    double tz = p.pts[m * 3 + 2] - p.pts[a * 3 + 2];
    double tl = std::hypot(tx, tz);
    if (tl == 0) tl = 1;
    tx /= tl;
    tz /= tl;
    double best = 9;
    int side = 0;
    std::unordered_set<int> seen;
    for (int cx = -1; cx <= 1; cx++) {
      for (int cz = -1; cz <= 1; cz++) {
        const std::vector<double>& list = near(mx + cx * 200, mz + cz * 200);
        for (size_t k = 0; k < list.size(); k += 2) {
          const int qi = static_cast<int>(list[k]);
          if (qi == static_cast<int>(pi) || seen.count(qi)) continue;
          seen.insert(qi);
          const RailPath& q = paths[static_cast<size_t>(qi)];
          for (size_t j = 1; j < q.cum.size(); j++) {
            // nearest point of this stretch of the other track
            const double x0 = q.pts[j * 3 - 3];
            const double z0 = q.pts[j * 3 - 1];
            const double dx = q.pts[j * 3] - x0;
            const double dz = q.pts[j * 3 + 2] - z0;
            const double l2 = dx * dx + dz * dz;
            if (l2 < 1e-6) continue;
            const double u = clamp(((mx - x0) * dx + (mz - z0) * dz) / l2, 0, 1);
            const double ox = x0 + dx * u - mx;
            const double oz = z0 + dz * u - mz;
            const double d = std::hypot(ox, oz);
            // a neighbour runs alongside, two to eight metres away
            if (d < 2 || d >= best || std::abs(dx * tx + dz * tz) / std::sqrt(l2) < 0.9) continue;
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

int64_t RailNetwork::cell(double x, double z) {
  return (static_cast<int64_t>(std::floor(x / 200)) + 4096) * 8192 + static_cast<int64_t>(std::floor(z / 200)) + 4096;
}

const std::vector<double>& RailNetwork::near(double x, double z) const {
  static const auto* none = new std::vector<double>();
  auto it = cells_.find(cell(x, z));
  return it != cells_.end() ? it->second : *none;
}

void RailNetwork::sample(int path, double s, RailSample& out) const {
  const RailPath& p = paths[static_cast<size_t>(path)];
  const int n = static_cast<int>(p.cum.size());
  int lo = 0;
  int hi = n - 1;
  while (hi - lo > 1) {
    const int mid = (lo + hi) >> 1;
    if (p.cum[static_cast<size_t>(mid)] <= s) lo = mid;
    else hi = mid;
  }
  const double seg = p.cum[static_cast<size_t>(hi)] - p.cum[static_cast<size_t>(lo)];
  const double t = seg > 1e-6 ? clamp((s - p.cum[static_cast<size_t>(lo)]) / seg, 0, 1) : 0;
  const size_t a = static_cast<size_t>(lo) * 3;
  const size_t b = static_cast<size_t>(hi) * 3;
  out.x = p.pts[a] + (p.pts[b] - p.pts[a]) * t;
  out.y = p.pts[a + 1] + (p.pts[b + 1] - p.pts[a + 1]) * t;
  out.z = p.pts[a + 2] + (p.pts[b + 2] - p.pts[a + 2]) * t;
  double l = std::hypot(p.pts[b] - p.pts[a], p.pts[b + 2] - p.pts[a + 2]);
  if (l == 0) l = 1;
  out.tx = (p.pts[b] - p.pts[a]) / l;
  out.tz = (p.pts[b + 2] - p.pts[a + 2]) / l;
  out.cap = std::min(p.cap[static_cast<size_t>(lo)], p.cap[static_cast<size_t>(hi)]);
}

std::vector<RailNetwork::Way> RailNetwork::waysOn(int path, int dir, const std::vector<int>& tracks) const {
  const RailPath& p = paths[static_cast<size_t>(path)];
  const size_t n = p.cum.size();
  if (n < 2) return {};
  // direction of travel at the end that is being left
  const size_t e = dir > 0 ? n - 1 : 0;
  const size_t f = dir > 0 ? n - 2 : 1;
  double hx = p.pts[e * 3] - p.pts[f * 3];
  double hz = p.pts[e * 3 + 2] - p.pts[f * 3 + 2];
  double hl = std::hypot(hx, hz);
  if (hl == 0) hl = 1;
  hx /= hl;
  hz /= hl;
  std::vector<Way> out;
  for (int link : dir > 0 ? p.b : p.a) {
    const int qi = link >> 1;
    if (qi < 0 || qi >= static_cast<int>(paths.size())) continue;
    const RailPath& q = paths[static_cast<size_t>(qi)];
    if (q.cum.size() < 2 || std::find(tracks.begin(), tracks.end(), q.kind) == tracks.end()) continue;
    // entered at its start it is run along, entered at its end against
    const int qdir = (link & 1) == 0 ? 1 : -1;
    const size_t m = q.cum.size();
    const size_t i0 = qdir > 0 ? 0 : m - 1;
    const size_t i1 = qdir > 0 ? 1 : m - 2;
    const double dx = q.pts[i1 * 3] - q.pts[i0 * 3];
    const double dz = q.pts[i1 * 3 + 2] - q.pts[i0 * 3 + 2];
    double dl = std::hypot(dx, dz);
    if (dl == 0) dl = 1;
    const double straight = (dx * hx + dz * hz) / dl;
    if (straight >= MAX_TURN_COS) out.push_back(Way{qi, qdir, straight});
  }
  std::stable_sort(out.begin(), out.end(), [](const Way& x, const Way& y) { return y.straight < x.straight; });
  return out;
}

// ---------------------------------------------------------------------------
// RailTraffic
// ---------------------------------------------------------------------------

RailTraffic::RailTraffic(const std::vector<RailJson>& rails, std::function<double()> random)
    : net(rails), random_(random ? std::move(random) : std::function<double()>(rnd)) {}

void RailTraffic::clear() {
  vehicles.clear();
  fresh_ = true;
}

void RailTraffic::resync() {
  for (size_t i = vehicles.size(); i-- > 0;)
    if (!vehicles[i]->player) vehicles.erase(vehicles.begin() + static_cast<long>(i));
  fresh_ = true;
  spawnT_ = 0;
}

RailVehicle* RailTraffic::byId(int id) const {
  for (const auto& v : vehicles)
    if (v->id == id) return v.get();
  return nullptr;
}

std::unique_ptr<RailVehicle> RailTraffic::remove(const RailVehicle* v) {
  for (size_t i = 0; i < vehicles.size(); i++) {
    if (vehicles[i].get() == v) {
      std::unique_ptr<RailVehicle> out = std::move(vehicles[i]);
      vehicles.erase(vehicles.begin() + static_cast<long>(i));
      return out;
    }
  }
  return nullptr;
}

std::optional<LineSpot> RailTraffic::lineSpot(int line, const PathMark& a, const PathMark& b, double frac) const {
  if (line < 0 || line >= static_cast<int>(lines.size())) return std::nullopt;
  const std::vector<PathDir>& seq = lines[static_cast<size_t>(line)].seq;
  int ka = -1;
  for (size_t k = 0; k < seq.size(); k++)
    if (seq[k].path == a.path) {
      ka = static_cast<int>(k);
      break;
    }
  if (ka < 0) return std::nullopt;
  int kb = -1;
  for (int k = ka; k < static_cast<int>(seq.size()); k++) {
    if (seq[static_cast<size_t>(k)].path != b.path) continue;
    // (on the same piece the second stop must lie ahead of the first)
    if (k == ka && (b.mark - a.mark) * seq[static_cast<size_t>(k)].dir < 0) continue;
    kb = k;
    break;
  }
  if (kb < 0) return std::nullopt;
  const auto len = [&](int k) -> double {
    const int p = seq[static_cast<size_t>(k)].path;
    return p >= 0 && p < static_cast<int>(net.paths.size()) ? net.paths[static_cast<size_t>(p)].len : 0;
  };
  /** metres from the start of seq[k] (in its direction) to a mark on it */
  const auto into = [&](int k, double mark) { return seq[static_cast<size_t>(k)].dir > 0 ? mark : len(k) - mark; };
  double total = 0;
  if (ka == kb) total = into(kb, b.mark) - into(ka, a.mark);
  else {
    total = len(ka) - into(ka, a.mark);
    for (int k = ka + 1; k < kb; k++) total += len(k);
    total += into(kb, b.mark);
  }
  double left = std::max(0.0, std::min(1.0, frac)) * std::max(0.0, total);
  int k = ka;
  double at = into(ka, a.mark);
  while (k < kb && at + left > len(k)) {
    left -= len(k) - at;
    at = 0;
    k++;
  }
  const double pos = std::min(len(k), at + left);
  const int dir = seq[static_cast<size_t>(k)].dir;
  return LineSpot{seq[static_cast<size_t>(k)].path, dir > 0 ? pos : len(k) - pos, dir, k};
}

void RailTraffic::setLines(std::vector<RailLine> linesIn) {
  lines = std::move(linesIn);
  onLine_.clear();
  for (size_t i = 0; i < lines.size(); i++)
    for (size_t k = 0; k < lines[i].seq.size(); k++) onLine_[key(lines[i].seq[k].path, lines[i].seq[k].dir)].emplace_back(static_cast<int>(i), static_cast<int>(k));
  for (auto& v : vehicles)
    if (!v->player) assignLine(*v);
}

bool RailTraffic::hasLine(int path, int dir) const {
  auto it = onLine_.find(key(path, dir));
  return it != onLine_.end() && !it->second.empty();
}

std::optional<RailLineAt> RailTraffic::assignLine(RailVehicle& v, const std::string& prefer) {
  std::vector<std::pair<int, int>> here;
  auto it = onLine_.find(key(v.path, v.dir));
  if (it != onLine_.end())
    for (const auto& e : it->second)
      if (lines[static_cast<size_t>(e.first)].kind == (v.kind == RAIL_LIGHT ? 1 : 0) || v.player) here.push_back(e);
  const std::pair<int, int>* pick = nullptr;
  for (const auto& e : here)
    if (lines[static_cast<size_t>(e.first)].ref == prefer) {
      pick = &e;
      break;
    }
  if (!pick) {
    // (as the original: here[floor(random * length)] – a draw is taken even when the list is empty)
    const size_t i = static_cast<size_t>(std::floor(random_() * static_cast<double>(here.size())));
    if (i < here.size()) pick = &here[i];
  }
  if (pick) v.line = RailLineAt{pick->first, pick->second};
  else v.line.reset();
  return v.line;
}

void RailTraffic::steer(RailVehicle& v, RailSchedule& sch) {
  const std::optional<PathMark> tg = sch.toward(v);
  const std::string k = tg ? std::to_string(tg->path) + ":" + jsStr(tg->mark) : "";
  if (v.routeFor && k == *v.routeFor) return;
  v.routeFor = k;
  v.route = tg ? wayTo(v.path, v.dir, tg->path, v.consist->tracks) : std::nullopt;
  // (what was planned beyond the piece it is on is planned anew along that way)
  if (v.route) v.plan.clear();
}

std::optional<std::vector<PathDir>> RailTraffic::wayTo(int path, int dir, int to, const std::vector<int>& tracks, double reach) const {
  const int start = path * 2 + (dir > 0 ? 1 : 0);
  if (path == to) return std::vector<PathDir>{PathDir{path, dir}};
  std::unordered_map<int, double> cost{{start, 0}};
  std::unordered_map<int, int> from;
  const auto costOf = [&](int s) {
    auto it = cost.find(s);
    return it != cost.end() ? it->second : INF;
  };
  // (a small heap of [cost, state])
  std::vector<std::pair<double, int>> heap{{0, start}};
  const auto push = [&](double c, int s) {
    heap.emplace_back(c, s);
    size_t i = heap.size() - 1;
    while (i > 0) {
      const size_t up = (i - 1) >> 1;
      if (heap[up].first <= heap[i].first) break;
      std::swap(heap[up], heap[i]);
      i = up;
    }
  };
  const auto pop = [&]() {
    const std::pair<double, int> top = heap[0];
    const std::pair<double, int> last = heap.back();
    heap.pop_back();
    if (!heap.empty()) {
      heap[0] = last;
      size_t i = 0;
      for (;;) {
        const size_t l = i * 2 + 1;
        const size_t r = l + 1;
        size_t m = i;
        if (l < heap.size() && heap[l].first < heap[m].first) m = l;
        if (r < heap.size() && heap[r].first < heap[m].first) m = r;
        if (m == i) break;
        std::swap(heap[m], heap[i]);
        i = m;
      }
    }
    return top;
  };
  int goal = -1;
  for (int guard = 0; !heap.empty() && guard < 40000; guard++) {
    const auto [c, s] = pop();
    if (c > costOf(s) || c > reach) continue;
    const int p = s >> 1;
    if (p == to) {
      goal = s;
      break;
    }
    for (const RailNetwork::Way& w : net.waysOn(p, (s & 1) == 1 ? 1 : -1, tracks)) {
      const int ns = w.path * 2 + (w.dir > 0 ? 1 : 0);
      const double nc = c + net.paths[static_cast<size_t>(w.path)].len;
      if (nc < costOf(ns)) {
        cost[ns] = nc;
        from[ns] = s;
        push(nc, ns);
      }
    }
  }
  if (goal < 0) return std::nullopt;
  std::vector<PathDir> out;
  for (int s = goal;;) {
    out.push_back(PathDir{s >> 1, (s & 1) == 1 ? 1 : -1});
    if (s == start) break;
    auto it = from.find(s);
    if (it == from.end()) break;
    s = it->second;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

std::optional<PathDir> RailTraffic::routeNext(const RailVehicle& v, int path, int dir) const {
  if (!v.route) return std::nullopt;
  const std::vector<PathDir>& r = *v.route;
  for (size_t i = 0; i + 1 < r.size(); i++)
    if (r[i].path == path && r[i].dir == dir) return r[i + 1];
  return std::nullopt;
}

std::optional<PathDir> RailTraffic::lineNext(const RailVehicle& v, int path, int dir) const {
  if (!v.line || v.line->index < 0 || v.line->index >= static_cast<int>(lines.size())) return std::nullopt;
  const RailLine& l = lines[static_cast<size_t>(v.line->index)];
  const int end = std::min(static_cast<int>(l.seq.size()) - 1, v.line->k + 16);
  for (int j = v.line->k; j < end; j++)
    if (l.seq[static_cast<size_t>(j)].path == path && l.seq[static_cast<size_t>(j)].dir == dir) return l.seq[static_cast<size_t>(j) + 1];
  return std::nullopt;
}

void RailTraffic::followLine(RailVehicle& v) {
  if (!v.line || v.line->index < 0 || v.line->index >= static_cast<int>(lines.size())) return;
  const RailLine& l = lines[static_cast<size_t>(v.line->index)];
  const int end = std::min(static_cast<int>(l.seq.size()), v.line->k + 16);
  for (int j = v.line->k; j < end; j++) {
    if (l.seq[static_cast<size_t>(j)].path == v.path && l.seq[static_cast<size_t>(j)].dir == v.dir) {
      v.line->k = j;
      return;
    }
  }
  // (a tram of the timetable keeps its line – and its number – wherever its way to its next stop leads)
  if (!v.trip.empty()) {
    for (size_t j = 0; j < l.seq.size(); j++)
      if (l.seq[j].path == v.path && l.seq[j].dir == v.dir) {
        v.line->k = static_cast<int>(j);
        break;
      }
    return;
  }
  // off its line (or past its end): the line back from here, else any that runs here
  const std::string ref = l.ref;
  auto it = onLine_.find(key(v.path, v.dir));
  if (it != onLine_.end())
    for (const auto& e : it->second)
      if (lines[static_cast<size_t>(e.first)].ref == ref) {
        v.line = RailLineAt{e.first, e.second};
        return;
      }
  assignLine(v);
}

RailVehicle* RailTraffic::spawnAt(int path, double s, int dir, double speed, const TramSet& set) {
  if (path < 0 || path >= static_cast<int>(net.paths.size())) return nullptr;
  const RailPath& p = net.paths[static_cast<size_t>(path)];
  if (p.len < 2) return nullptr;
  const Consist* consist = consistOf(p.kind, set);
  const double length = consistLength(*consist);
  const int n = static_cast<int>(std::ceil((length + 6) / TRAIL_STEP)) + 8;
  Trail trail(n);
  // lay the vehicle out behind its front end: walk back along the track
  struct Back {
    double x, y, z;
    int path;
    double s;
    int dir;
  };
  std::vector<Back> back;
  int cp = path;
  double cs = clamp(s, 0, p.len);
  int cd = dir > 0 ? -1 : 1;
  for (int k = 0; k < n; k++) {
    net.sample(cp, cs, tmp);
    // (recorded with the direction the vehicle faces, not the one walked)
    back.push_back(Back{tmp.x, tmp.y, tmp.z, cp, cs, -cd});
    double step = TRAIL_STEP;
    while (step > 0) {
      const RailPath& q = net.paths[static_cast<size_t>(cp)];
      const double room = cd > 0 ? q.len - cs : cs;
      if (room >= step) {
        cs += cd * step;
        step = 0;
      } else {
        step -= room;
        const std::vector<RailNetwork::Way> on = net.waysOn(cp, cd, consist->tracks);
        if (on.empty()) return nullptr;
        cp = on[0].path;
        cd = on[0].dir;
        cs = cd > 0 ? 0 : net.paths[static_cast<size_t>(cp)].len;
      }
    }
  }
  for (size_t k = back.size(); k-- > 0;) trail.push(back[k].x, back[k].y, back[k].z, back[k].path, back[k].s, back[k].dir);
  auto v = std::make_unique<RailVehicle>();
  v->id = nextId_++;
  v->kind = p.kind;
  v->consist = consist;
  v->path = path;
  v->s = clamp(s, 0, p.len);
  v->dir = dir;
  v->speed = speed;
  v->dwell = 0;
  v->noStop = 30;
  v->cars.assign(carLayout(*consist).size(), RailCar{});
  v->trail = std::move(trail);
  v->blocked = false;
  place(*v);
  vehicles.push_back(std::move(v));
  return vehicles.back().get();
}

void RailTraffic::circles(const RailVehicle* skip, std::vector<RailObstacle>& out) const {
  for (const auto& vp : vehicles) {
    const RailVehicle& v = *vp;
    if (&v == skip) continue;
    const std::vector<CarRole>& roles = carLayout(*v.consist);
    for (size_t i = 0; i < v.cars.size(); i++) {
      const RailCar& c = v.cars[i];
      const double half = roles[i].len / 2;
      const double fx = -std::sin(c.yaw);
      const double fz = -std::cos(c.yaw);
      for (double k : {-0.7, 0.0, 0.7}) out.push_back(RailObstacle{c.x + fx * half * k, c.z + fz * half * k, v.consist->width / 2 + 0.2});
    }
  }
}

void RailTraffic::update(double dt, const RailPlayer& player, const std::vector<RailObstacle>& others) {
  // come and go with the player
  for (size_t i = vehicles.size(); i-- > 0;) {
    const RailVehicle& v = *vehicles[i];
    const RailCar& c = v.cars[0];
    const double d = std::hypot(c.x - player.x, c.z - player.z);
    // (a tram whose trip has ended goes once it has left the player's side)
    if (!v.player && (d > far || (v.retire && v.dwell <= 0 && d > RETIRE_NEAR))) vehicles.erase(vehicles.begin() + static_cast<long>(i));
  }
  spawnT_ -= dt;
  if (schedule && !lines.empty()) {
    // by the timetable: the trips under way round the player – each once, the nearest first
    if (spawnT_ <= 0 && target > 0) {
      spawnT_ = 2;
      const bool all = fresh_;
      fresh_ = false;
      const std::vector<RailDue> list = schedule->due(player.x, player.z, ringMin, ringMax, all);
      struct Near {
        const RailDue* d;
        double r;
      };
      std::vector<Near> near;
      for (const RailDue& d : list) {
        net.sample(d.path, d.s, tmp);
        near.push_back(Near{&d, std::hypot(tmp.x - player.x, tmp.z - player.z)});
      }
      std::stable_sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.r < b.r; });
      // (as many as the timetable has about: a tram missing at a stop whose board announces it is worse than a few more to draw)
      const size_t cap = static_cast<size_t>(std::max(target + 8, target * 6));
      for (const Near& nr : near) {
        const RailDue& d = *nr.d;
        RailVehicle* had = nullptr;
        for (auto& v : vehicles)
          if (v->trip == d.trip) {
            had = v.get();
            break;
          }
        net.sample(d.path, d.s, tmp);
        if (had) {
          // a tram far behind (or ahead of) its trip – the game stood still a while, something blocked it – is put
          // where its trip is now, if the player sees neither place close up
          const RailCar& f = had->cars[0];
          const double off = std::hypot(f.x - tmp.x, f.z - tmp.z);
          if (had->player || off < MISPLACED || std::hypot(f.x - player.x, f.z - player.z) < ringMin || std::hypot(tmp.x - player.x, tmp.z - player.z) < ringMin) continue;
          remove(had);
        }
        if (vehicles.size() >= cap) break;
        // (not into a tram that is there already – one whose trip has ended makes room: it turns into this one)
        const auto close = [&](const RailVehicle& v) {
          for (const RailCar& c : v.cars)
            if (std::hypot(c.x - tmp.x, c.z - tmp.z) < 60) return true;
          return false;
        };
        bool taken = false;
        for (auto& v : vehicles)
          if (close(*v) && (!v->retire || v->player)) {
            taken = true;
            break;
          }
        if (taken) continue;
        for (size_t i = vehicles.size(); i-- > 0;)
          if (vehicles[i]->retire && !vehicles[i]->player && close(*vehicles[i])) vehicles.erase(vehicles.begin() + static_cast<long>(i));
        RailVehicle* v = spawnAt(d.path, d.s, d.dir, 0, random_() < doubleShare ? "double" : "single");
        if (!v) continue;
        v->trip = d.trip;
        v->line = RailLineAt{d.line, d.k};
        v->speed = std::min({d.speed, v->consist->vmax * 0.6, tmp.cap});
        if (d.dwell && *d.dwell > 0) {
          // standing at a stop until its time
          v->speed = 0;
          v->dwell = std::min(600.0, std::max(*d.dwell, 3.0));
          v->noStop = 70;
        }
      }
    }
  } else if (spawnT_ <= 0 && static_cast<int>(vehicles.size()) < target) {
    spawnT_ = 1.5;
    spawnNear(player);
  }

  // (by index: a step neither adds nor takes vehicles)
  for (size_t vi = 0; vi < vehicles.size(); vi++) {
    RailVehicle& v = *vehicles[vi];
    if (v.player) continue;
    std::vector<RailObstacle>& obstacles = obstacles_;
    obstacles.clear();
    // the player's car: two circles along its length
    const double px = -std::sin(player.yaw);
    const double pz = -std::cos(player.yaw);
    const double off = std::max(0.0, player.length / 2 - player.width / 2);
    obstacles.push_back(RailObstacle{player.x + px * off, player.z + pz * off, player.width / 2 + 0.15});
    obstacles.push_back(RailObstacle{player.x - px * off, player.z - pz * off, player.width / 2 + 0.15});
    for (const RailObstacle& o : others)
      if (std::abs(o.x - v.cars[0].x) < 120 && std::abs(o.z - v.cars[0].z) < 120) obstacles.push_back(o);
    circles(&v, obstacles);
    step(v, dt, obstacles);
  }
}

RailVehicle* RailTraffic::spawnNear(const RailPlayer& player) {
  for (int tries = 0; tries < 10; tries++) {
    const double ang = random_() * M_PI * 2;
    const double rad = ringMin + random_() * (ringMax - ringMin);
    const std::vector<double>& list = net.near(player.x + std::cos(ang) * rad, player.z + std::sin(ang) * rad);
    if (list.empty()) continue;
    const size_t k = static_cast<size_t>(std::floor(random_() * static_cast<double>(list.size() / 2))) * 2;
    const int path = static_cast<int>(list[k]);
    const double s = list[k + 1];
    net.sample(path, s, tmp);
    const double d = std::hypot(tmp.x - player.x, tmp.z - player.z);
    if (d < ringMin || d > ringMax + 150) continue;
    // not into another one
    bool inWay = false;
    for (const auto& v : vehicles)
      for (const RailCar& c : v->cars)
        if (std::hypot(c.x - tmp.x, c.z - tmp.z) < 160) inWay = true;
    if (inWay) continue;
    // on a double track keep to the right-hand one; a single track is run both ways
    const int way = net.paths[static_cast<size_t>(path)].way;
    const int dir = way != 0 ? way : random_() < 0.5 ? 1 : -1;
    // (now and then two units coupled together, as on the busy lines)
    RailVehicle* v = spawnAt(path, s, dir, 0, random_() < doubleShare ? "double" : "single");
    if (v) {
      v->speed = std::min(v->consist->vmax * 0.6, tmp.cap);
      assignLine(*v);
      return v;
    }
  }
  return nullptr;
}

bool RailTraffic::wired(int path) const {
  if (path < 0 || path >= static_cast<int>(net.paths.size())) return false;
  const RailPath& p = net.paths[static_cast<size_t>(path)];
  return p.wire == 1 || (p.wire == -1 && p.kind != RAIL_TRAIN);
}

void RailTraffic::step(RailVehicle& v, double dt, const std::vector<RailObstacle>& obstacles) {
  const Consist& c = *v.consist;
  // the doors: open while it stands at a stop – from a moment after it stops until a little before it leaves
  const bool open = v.dwell > 2.5 && v.speed == 0 && v.noStop > 0;
  const double d = v.doors;
  if (open != (d > 0.5) || (d > 0 && d < 1)) v.doors = open ? std::min(1.0, d + dt / DOOR_TIME) : std::max(0.0, d - dt / DOOR_TIME);
  // (the regional trams run on their engine where there is no wire: the pantographs come down there)
  if (v.kind != RAIL_TRAIN) {
    const double want = wired(v.path) ? 1 : 0;
    const double now = v.panto;
    if (now != want) v.panto = want > now ? std::min(1.0, now + dt / 3) : std::max(0.0, now - dt / 1.5);
  }
  if (v.dwell > 0) {
    v.dwell -= dt;
    v.speed = 0;
    place(v);
    return;
  }
  // ---- look ahead along the way it will take: curves, stops, the end of the line, things on the track
  const double reach = (v.speed * v.speed) / (2 * c.brake) + 25;
  double allowed = c.vmax;
  // (a tram of the timetable: no faster than it needs to be at its next stop on time – now and then worked out anew)
  RailSchedule* sch = !v.trip.empty() ? schedule : nullptr;
  if (sch && sch->hasToward()) steer(v, *sch);
  if (sch && sch->hasPace()) {
    v.paceT = v.paceT - dt;
    if (v.paceT <= 0 || !v.pace) {
      v.paceT = 1;
      std::function<bool(int, double)> accept = nullptr;
      if (sch->hasCalls()) accept = [&](int path, double mark) { return sch->calls(v, path, mark); };
      const std::optional<NextStop> next = nextStop(v, 4000, accept);
      v.pace = next ? sch->pace(v, next->path, next->mark, next->dist) : INF;
    }
    allowed = std::min(allowed, std::max(PACE_MIN, *v.pace));
  }
  double stopIn = INF;
  double blockIn = INF;
  double endIn = INF;
  int cp = v.path;
  double cs = v.s;
  int cd = v.dir;
  size_t planned = 0;
  double dist = 0;
  const double halfW = c.width / 2;
  while (dist <= reach) {
    const RailPath& p = net.paths[static_cast<size_t>(cp)];
    net.sample(cp, cs, tmp);
    // a curve further on limits the speed now by what braking takes
    allowed = std::min(allowed, std::sqrt(tmp.cap * tmp.cap + 2 * c.brake * 0.7 * dist));
    if (dist > 1) {
      for (const RailObstacle& o : obstacles) {
        const double od = std::hypot(o.x - tmp.x, o.z - tmp.z);
        if (od < o.r + halfW + 0.25) {
          blockIn = std::min(blockIn, dist);
          break;
        }
      }
      if (blockIn < INF) break;
    }
    const double step = 2.5;
    const double room = cd > 0 ? p.len - cs : cs;
    // stops on this piece within the next step
    if (v.noStop <= dist) {
      for (double st : p.stops) {
        // (a stop its trip does not call at it passes)
        if (sch && sch->hasCalls() && !sch->calls(v, cp, st)) continue;
        const double ahead = (st - cs) * cd;
        if (ahead >= 0 && ahead <= std::min(step, room) + 0.01) stopIn = std::min(stopIn, dist + ahead);
      }
    }
    if (room > step) {
      cs += cd * step;
      dist += step;
      continue;
    }
    // onto the next piece: the one planned, or choose now
    dist += room;
    std::optional<PathDir> next;
    if (planned < v.plan.size()) next = v.plan[planned];
    if (!next) {
      const std::vector<RailNetwork::Way> ways = net.waysOn(cp, cd, c.tracks);
      if (!ways.empty()) {
        // its line's way – else mostly straight on, now and then the branch
        std::optional<PathDir> want = routeNext(v, cp, cd);
        if (!want) want = lineNext(v, cp, cd);
        const RailNetwork::Way* onLine = nullptr;
        if (want)
          for (const RailNetwork::Way& w : ways)
            if (w.path == want->path && w.dir == want->dir) {
              onLine = &w;
              break;
            }
        const RailNetwork::Way* pick = onLine;
        if (!pick) pick = ways.size() > 1 && random_() < 0.3 ? &ways[1 + static_cast<size_t>(std::floor(random_() * static_cast<double>(ways.size() - 1)))] : &ways[0];
        next = PathDir{pick->path, pick->dir};
        v.plan.push_back(*next);
      }
    }
    if (!next) {
      endIn = dist;
      break;
    }
    planned++;
    cp = next->path;
    cd = next->dir;
    cs = cd > 0 ? 0 : net.paths[static_cast<size_t>(cp)].len;
  }

  // ---- speed
  v.blocked = blockIn < INF;
  const auto stopFor = [](double dd, double margin, double brake) { return std::sqrt(2 * brake * std::max(0.0, dd - margin)); };
  double target = allowed;
  if (stopIn < INF) target = std::min(target, stopFor(stopIn, 0.3, c.brake * 0.8));
  if (endIn < INF) target = std::min(target, stopFor(endIn, 1.5, c.brake * 0.8));
  if (blockIn < INF) target = std::min(target, stopFor(blockIn, 4.5, c.brake * 1.6));
  // (a level crossing whose barriers are not down yet: it waits before it)
  const double holdIn = hold ? hold(v) : INF;
  if (holdIn < INF) target = std::min(target, stopFor(holdIn, 12, c.brake * 0.9));
  if (v.speed < target) v.speed = std::min(target, v.speed + c.accel * dt);
  else v.speed = std::max(target, v.speed - (blockIn < INF ? c.brake * 2 : c.brake) * dt);
  if (v.speed < 0.05 && target < 0.3) v.speed = 0;

  // ---- arrived?
  if (v.speed == 0) {
    if (stopIn < 1.2) {
      v.dwell = 12 + random_() * 8;
      // (by the timetable: not before its time – and no longer than the doors take when it is late; at the end of its trip it goes)
      if (sch) {
        const RailPath& p = net.paths[static_cast<size_t>(v.path)];
        double mark = -1;
        for (double m : p.stops)
          if (std::abs(m - v.s) < 4) mark = m;
        if (mark >= 0) v.dwell = std::max(TIMED_DWELL, std::min(600.0, sch->wait(v, v.path, mark)));
        if (sch->hasEnds() && sch->ends(v)) v.retire = true;
      }
      v.pace.reset();
      v.noStop = 70;
    } else if (endIn < 3 && blockIn == INF) {
      // the end of the line: wait, then go back the way it came
      reverse(v);
      v.dwell = 20 + random_() * 10;
      v.noStop = 40;
    }
    place(v);
    return;
  }

  // ---- move the front end, leaving the trail the cars follow
  const double move = v.speed * dt;
  v.noStop = std::max(0.0, v.noStop - move);
  advance(v, move);
  place(v);
}

std::optional<RailTraffic::NextStop> RailTraffic::nextStop(const RailVehicle& v, double reach, const std::function<bool(int, double)>& accept) const {
  int cp = v.path;
  double cs = v.s;
  int cd = v.dir;
  double dist = 0;
  size_t planned = 0;
  for (int guard = 0; guard < 400 && dist <= reach; guard++) {
    if (cp < 0 || cp >= static_cast<int>(net.paths.size())) return std::nullopt;
    const RailPath& p = net.paths[static_cast<size_t>(cp)];
    std::optional<NextStop> best;
    for (double st : p.stops) {
      const double ahead = (st - cs) * cd;
      const double d = dist + ahead;
      if (ahead >= 0 && d > (v.dwell > 0 ? 2 : 0.5) && (!best || d < best->dist) && (!accept || accept(cp, st))) best = NextStop{cp, st, d};
    }
    if (best) return best;
    dist += cd > 0 ? p.len - cs : cs;
    std::optional<PathDir> next;
    if (planned < v.plan.size()) next = v.plan[planned];
    planned++;
    if (!next) next = routeNext(v, cp, cd);
    if (!next) {
      const std::vector<RailNetwork::Way> w = net.waysOn(cp, cd, v.consist->tracks);
      if (!w.empty()) next = PathDir{w[0].path, w[0].dir};
    }
    if (!next) return std::nullopt;
    cp = next->path;
    cd = next->dir;
    cs = cd > 0 ? 0 : net.paths[static_cast<size_t>(cp)].len;
  }
  return std::nullopt;
}

bool RailTraffic::advance(RailVehicle& v, double move) {
  while (move > 0) {
    const RailPath& p = net.paths[static_cast<size_t>(v.path)];
    const double room = v.dir > 0 ? p.len - v.s : v.s;
    const double go = std::min({move, room, TRAIL_STEP - v.trail.since});
    v.s += v.dir * go;
    move -= go;
    v.trail.since += go;
    if (v.trail.since >= TRAIL_STEP - 1e-9) {
      net.sample(v.path, v.s, tmp);
      v.trail.push(tmp.x, tmp.y, tmp.z, v.path, v.s, v.dir);
      v.trail.since = 0;
    }
    if (go >= room - 1e-9 && move > 0) {
      if (v.plan.empty()) {
        v.speed = 0;
        return false;
      }
      const PathDir next = v.plan.front();
      v.plan.erase(v.plan.begin());
      v.path = next.path;
      v.dir = next.dir;
      v.s = v.dir > 0 ? 0 : net.paths[static_cast<size_t>(v.path)].len;
      followLine(v);
    }
  }
  return true;
}

void RailTraffic::reverse(RailVehicle& v) {
  Trail& t = v.trail;
  const double length = consistLength(*v.consist);
  const int k = std::min(t.count - 1, static_cast<int>(std::lround(length / TRAIL_STEP)));
  const int tail = t.at(k);
  struct E {
    double x, y, z;
    int path;
    double s;
    int dir;
  };
  std::vector<E> entries;
  // from the old front end to the old rear end: that is oldest … newest of the way back
  for (int i = 0; i <= k; i++) {
    const size_t e = static_cast<size_t>(t.at(i));
    entries.push_back(E{t.x[e], t.y[e], t.z[e], t.path[e], t.s[e], -t.dir[e]});
  }
  v.path = t.path[static_cast<size_t>(tail)];
  v.s = t.s[static_cast<size_t>(tail)];
  v.dir = t.dir[static_cast<size_t>(tail)] > 0 ? -1 : 1;
  t.count = 0;
  t.since = 0;
  for (const E& e : entries) t.push(e.x, e.y, e.z, e.path, e.s, e.dir);
  v.plan.clear();
  // (at the end of its line: the line back)
  if (v.line) followLine(v);
}

void RailTraffic::place(RailVehicle& v) const {
  const Consist& c = *v.consist;
  const Trail& t = v.trail;
  struct P {
    double x = 0, y = 0, z = 0;
  };
  const auto at = [&](double dist, P& out) {
    // (the front end is `since` metres past the newest entry of the trail)
    const double f = clamp((dist - t.since) / TRAIL_STEP, 0, t.count - 1.001);
    const int i = static_cast<int>(std::floor(f));
    const double u = f - i;
    const size_t a = static_cast<size_t>(t.at(i));
    const size_t b = static_cast<size_t>(t.at(i + 1));
    out.x = t.x[a] + (t.x[b] - t.x[a]) * u;
    out.y = t.y[a] + (t.y[b] - t.y[a]) * u;
    out.z = t.z[a] + (t.z[b] - t.z[a]) * u;
  };
  const std::vector<CarRole>& roles = carLayout(c);
  P pivotA;
  P pivotB;
  for (size_t i = 0; i < roles.size(); i++) {
    const double start = roles[i].start;
    const double len = roles[i].len;
    // each car rides on two pivots a little in from its ends
    at(start + len * 0.14, pivotA);
    at(start + len * 0.86, pivotB);
    RailCar& car = v.cars[i];
    const double fx = pivotA.x - pivotB.x;
    const double fy = pivotA.y - pivotB.y;
    const double fz = pivotA.z - pivotB.z;
    car.x = (pivotA.x + pivotB.x) / 2;
    car.y = (pivotA.y + pivotB.y) / 2;
    car.z = (pivotA.z + pivotB.z) / 2;
    const double flat = std::hypot(fx, fz);
    if (flat > 1e-6) {
      car.yaw = std::atan2(-fx, -fz);
      car.pitch = std::atan2(fy, flat);
    }
  }
}

std::optional<RailTraffic::Touch> RailTraffic::touching(const RailPlayer& player) const {
  std::optional<Touch> best;
  for (const auto& vp : vehicles) {
    const RailVehicle& v = *vp;
    const std::vector<CarRole>& roles = carLayout(*v.consist);
    for (size_t i = 0; i < v.cars.size(); i++) {
      const RailCar& car = v.cars[i];
      if (std::abs(car.y - player.y) > 4) continue;
      const double half = roles[i].len / 2;
      const double reach = half + player.length;
      if (std::abs(car.x - player.x) > reach || std::abs(car.z - player.z) > reach) continue;
      double nx = 0;
      double nz = 0;
      double depth = 0;
      const bool hit = boxOverlap(player.x, player.z, player.yaw, player.length / 2, player.width / 2, car.x, car.z, car.yaw, half, v.consist->width / 2, nx, nz, depth);
      if (hit && (!best || depth > best->depth)) best = Touch{nx, nz, depth, -std::sin(car.yaw) * v.speed, -std::cos(car.yaw) * v.speed};
    }
  }
  return best;
}

bool boxOverlap(double ax, double az, double ayaw, double ahl, double ahw, double bx, double bz, double byaw, double bhl, double bhw, double& nxOut, double& nzOut, double& depthOut) {
  // forward and right of each rectangle
  const std::array<std::array<double, 2>, 4> axes = {{
      {-std::sin(ayaw), -std::cos(ayaw)},
      {std::cos(ayaw), -std::sin(ayaw)},
      {-std::sin(byaw), -std::cos(byaw)},
      {std::cos(byaw), -std::sin(byaw)},
  }};
  const double dx = ax - bx;
  const double dz = az - bz;
  double depth = INF;
  double nx = 0;
  double nz = 0;
  for (const auto& u : axes) {
    const double ux = u[0];
    const double uz = u[1];
    // half extent of each rectangle along this axis
    const double ra = ahl * std::abs(axes[0][0] * ux + axes[0][1] * uz) + ahw * std::abs(axes[1][0] * ux + axes[1][1] * uz);
    const double rb = bhl * std::abs(axes[2][0] * ux + axes[2][1] * uz) + bhw * std::abs(axes[3][0] * ux + axes[3][1] * uz);
    const double d = dx * ux + dz * uz;
    const double over = ra + rb - std::abs(d);
    if (over <= 0) return false;
    if (over < depth) {
      depth = over;
      const double sign = d >= 0 ? 1 : -1;
      nx = ux * sign;
      nz = uz * sign;
    }
  }
  nxOut = nx;
  nzOut = nz;
  depthOut = depth;
  return true;
}

}  // namespace fs
