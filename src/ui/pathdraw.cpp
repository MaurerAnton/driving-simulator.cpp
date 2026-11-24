// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/ui/pathdraw.ts @ v1.7.7
#include "ui/pathdraw.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "core/js.h"
#include "ui/bigmap.h"
#include "world/lanes.h"
#include "world/netfile.h"

namespace fs {

using dom::El;
using dom::el;

RoadSource roadSource(const RouteGraph& g, const NetIndex* net, const World& world) {
  if (net) {
    RoadSource r;
    r.g = &g;
    r.roadsIn = [net](double a, double b, double c, double d, const std::function<void(int)>& visit) { net->edgesInBox(a, b, c, d, visit); };
    r.course = [net](int e) -> std::optional<std::vector<double>> { return netPolyline(*net->net, e); };
    r.usable = [net](int w) { return net->main[static_cast<size_t>(w)] == 1; };
    return r;
  }
  RoadSource r;
  r.g = &g;
  const World* wp = &world;
  r.roadsIn = [wp](double a, double b, double c, double d, const std::function<void(int)>& visit) { wp->edgesInBox(a, b, c, d, [&](const Edge& e) { visit(e.id); }); };
  r.course = [wp](int e) -> std::optional<std::vector<double>> {
    if (e < 0 || static_cast<size_t>(e) >= wp->edges.size() || !wp->edges[static_cast<size_t>(e)]) return std::nullopt;
    const std::vector<double>& p = wp->edges[static_cast<size_t>(e)]->pl.pts;
    std::vector<double> out;
    for (size_t i = 0; i + 2 < p.size(); i += 3) {
      out.push_back(p[i]);
      out.push_back(p[i + 2]);
    }
    return out;
  };
  return r;
}

namespace {

/** a road's course with the metres along it */
struct Course {
  std::vector<double> pts;
  std::vector<double> cum;
  double len = 0;
};

/** A candidate: a way near a sample, where on it (metres along it, in its direction) and what it costs. */
struct Cand {
  int w = 0;
  double s = 0;
  double len = 0;
  double x = 0;
  double z = 0;
  double cost = 0;
};

/** the most candidates kept per sample */
const size_t MAX_CANDS = 6;
/** what a U-turn on the way between two samples costs (m), and a way "for access only" used to get through */
const double UTURN_M = 150;
const double ACCESS_M = 60;

const double INF = std::numeric_limits<double>::infinity();

Course courseOf(std::vector<double> pts) {
  const size_t n = pts.size() >> 1;
  Course c;
  c.cum.assign(std::max<size_t>(1, n), 0.0);
  for (size_t i = 1; i < n; i++) c.cum[i] = c.cum[i - 1] + std::hypot(pts[i * 2] - pts[i * 2 - 2], pts[i * 2 + 1] - pts[i * 2 - 1]);
  c.len = n > 0 ? c.cum[n - 1] : 0;
  c.pts = std::move(pts);
  return c;
}

struct Projection {
  double d = INF;
  double s = 0;
  double px = 0;
  double pz = 0;
  double tx = 0;
  double tz = -1;
};

/** The point of a course nearest to (x, z): how far, how far along, and the direction of the course there (a to b). */
Projection project(const Course& c, double x, double z) {
  const std::vector<double>& p = c.pts;
  const size_t n = p.size() >> 1;
  Projection best;
  best.px = p[0];
  best.pz = p[1];
  for (size_t i = 0; i + 1 < n; i++) {
    const double ax = p[i * 2];
    const double az = p[i * 2 + 1];
    const double dx = p[i * 2 + 2] - ax;
    const double dz = p[i * 2 + 3] - az;
    const double l2 = dx * dx + dz * dz;
    if (l2 < 1e-9) continue;
    const double t = std::max(0.0, std::min(1.0, ((x - ax) * dx + (z - az) * dz) / l2));
    const double px = ax + dx * t;
    const double pz = az + dz * t;
    const double d = std::hypot(x - px, z - pz);
    if (d < best.d) {
      const double l = std::sqrt(l2);
      best = {d, c.cum[i] + l * t, px, pz, dx / l, dz / l};
    }
  }
  return best;
}

/** A way's course in the direction it is driven: the road's, turned round for direction 1. */
std::vector<double> wayPoints(const Course& c, int w) {
  const std::vector<double>& p = c.pts;
  std::vector<double> out;
  if ((w & 1) == 0) {
    for (size_t i = 0; i + 1 < p.size(); i += 2) {
      out.push_back(p[i]);
      out.push_back(p[i + 1]);
    }
  } else {
    for (long i = static_cast<long>(p.size()) - 2; i >= 0; i -= 2) {
      out.push_back(p[static_cast<size_t>(i)]);
      out.push_back(p[static_cast<size_t>(i) + 1]);
    }
  }
  return out;
}

/** The piece of a way's course (in its direction) from `a` to `b` metres along it, appended to `out` (x, z, …). */
void appendPiece(const std::vector<double>& pts, double a, double b, std::vector<double>& out) {
  const size_t n = pts.size() >> 1;
  double at = 0;
  const auto push = [&out](double x, double z) {
    const size_t k = out.size();
    if (k >= 2 && std::abs(out[k - 2] - x) < 0.01 && std::abs(out[k - 1] - z) < 0.01) return;
    out.push_back(x);
    out.push_back(z);
  };
  for (size_t i = 0; i + 1 < n; i++) {
    const double ax = pts[i * 2];
    const double az = pts[i * 2 + 1];
    const double dx = pts[i * 2 + 2] - ax;
    const double dz = pts[i * 2 + 3] - az;
    const double l = std::hypot(dx, dz);
    const double from = at;
    const double to = at + l;
    at = to;
    if (l < 1e-9 || to < a || from > b) continue;
    const double t0 = std::max(0.0, (a - from) / l);
    const double t1 = std::min(1.0, (b - from) / l);
    push(ax + dx * t0, az + dz * t0);
    push(ax + dx * t1, az + dz * t1);
  }
}

/** Samples of a line every `step` metres (its first and last point among them). */
std::vector<double> resample(const std::vector<double>& points, double step) {
  const size_t n = points.size() >> 1;
  std::vector<double> out;
  if (n == 0) return out;
  out.push_back(points[0]);
  out.push_back(points[1]);
  double carry = 0;
  for (size_t i = 1; i < n; i++) {
    const double ax = points[i * 2 - 2];
    const double az = points[i * 2 - 1];
    const double dx = points[i * 2] - ax;
    const double dz = points[i * 2 + 1] - az;
    const double l = std::hypot(dx, dz);
    if (l < 1e-9) continue;
    double at = step - carry;
    while (at <= l) {
      out.push_back(ax + (dx * at) / l);
      out.push_back(az + (dz * at) / l);
      at += step;
    }
    carry = l - (at - step);
  }
  const size_t k = out.size();
  const double lx = points[n * 2 - 2];
  const double lz = points[n * 2 - 1];
  if (std::hypot(out[k - 2] - lx, out[k - 1] - lz) > step * 0.3) {
    out.push_back(lx);
    out.push_back(lz);
  } else if (k > 2) {
    out[k - 2] = lx;
    out[k - 1] = lz;
  }
  return out;
}

struct Found {
  std::unordered_map<int, double> dist;
  std::unordered_map<int, int> prev;
  std::unordered_map<int, int> uturns;
};

/**
 * The shortest legal ways from the end of way `from` to the start of each
 * way in `goals`, within `limit` metres: for each way reached, the metres to
 * its start and the way before it.  (Dijkstra on the turns the junctions
 * allow; a way may be reached again through a loop, also `from` itself.)
 */
Found search(const RouteGraph& g, int from, const std::unordered_set<int>& goals, double limit, const std::function<bool(int)>& usable) {
  Found f;
  MinHeap heap(64);
  // (the start is the end of `from`: its own entry is −1 so that a way back round to it counts)
  const auto relax = [&](int w, double end, int u) {
    for (uint32_t k = g.first[static_cast<size_t>(w)]; k < g.first[static_cast<size_t>(w) + 1]; k++) {
      const int t = static_cast<int>(g.to[k]);
      const bool goal = goals.count(t) > 0;
      if (!(g.len[static_cast<size_t>(t)] > 0) || (!goal && ((g.kind[static_cast<size_t>(t)] & RG_NO_ENTRY) != 0 || (usable && !usable(t))))) continue;
      const bool turn = g.turn[k] == TURN_UTURN;
      const double nd = end + static_cast<double>(g.across[k]) + (turn ? UTURN_M : 0) + (!goal && (g.kind[static_cast<size_t>(t)] & RG_ACCESS_ONLY) != 0 ? ACCESS_M : 0);
      if (nd > limit) continue;
      const auto old = f.dist.find(t);
      if (old == f.dist.end() || nd < old->second) {
        f.dist[t] = nd;
        f.prev[t] = w;
        f.uturns[t] = u + (turn ? 1 : 0);
        heap.push(static_cast<uint32_t>(t), nd);
      }
    }
  };
  relax(from, 0, 0);
  size_t found = 0;
  while (heap.size > 0) {
    const double key = heap.topKey();
    const int w = static_cast<int>(heap.pop());
    const auto di = f.dist.find(w);
    const double d = di == f.dist.end() ? INF : di->second;
    if (key > d + 1e-6) continue;
    if (goals.count(w) && ++found >= goals.size()) break;
    const auto ui = f.uturns.find(w);
    relax(w, d + static_cast<double>(g.len[static_cast<size_t>(w)]), ui == f.uturns.end() ? 0 : ui->second);
  }
  return f;
}

/** The ways after `from` up to `goal` (the goal last), from a search's `prev`. */
std::vector<int> pathTo(const std::unordered_map<int, int>& prev, int from, int goal) {
  std::vector<int> out;
  int w = goal;
  for (int guard = 0; guard < 100000; guard++) {
    out.push_back(w);
    const auto p = prev.find(w);
    if (p == prev.end()) return {};
    if (p->second == from) break;
    w = p->second;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

struct Layer {
  size_t i = 0;
  std::vector<Cand> cands;
};

struct Step {
  double cost = INF;
  int back = -1;
  std::vector<int> path;
};

bool anyFinite(const std::vector<Step>& steps) {
  for (const Step& q : steps)
    if (std::isfinite(q.cost)) return true;
  return false;
}

}  // namespace

std::optional<MatchedPath> matchStroke(const std::vector<double>& points, const RoadSource& roads, const MatchOptions& opt) {
  const RouteGraph& g = *roads.g;
  double drawn = 0;
  for (size_t i = 2; i + 1 < points.size(); i += 2) drawn += std::hypot(points[i] - points[i - 2], points[i + 1] - points[i - 1]);
  if (points.size() < 2) return std::nullopt;
  const double step = opt.step ? *opt.step : std::max(15.0, std::min(60.0, drawn / 300));
  const double R = opt.radius ? *opt.radius : 35;
  const std::vector<double> smp = resample(points, step);
  const size_t n = smp.size() >> 1;
  // (null: the road has no course – kept, so that it is asked for once)
  std::unordered_map<int, std::unique_ptr<Course>> courses;
  const auto course = [&](int e) -> const Course* {
    auto it = courses.find(e);
    if (it == courses.end()) {
      std::optional<std::vector<double>> pts = roads.course(e);
      std::unique_ptr<Course> c = pts && pts->size() >= 4 ? std::make_unique<Course>(courseOf(std::move(*pts))) : nullptr;
      it = courses.emplace(e, std::move(c)).first;
    }
    return it->second.get();
  };

  // --- the candidates of each sample
  std::vector<Layer> layers;
  for (size_t i = 0; i < n; i++) {
    const double x = smp[i * 2];
    const double z = smp[i * 2 + 1];
    // the direction of drawing here: from the sample before to the one after
    const size_t a = i == 0 ? 0 : i - 1;
    const size_t b = std::min(n - 1, i + 1);
    double dx = smp[b * 2] - smp[a * 2];
    double dz = smp[b * 2 + 1] - smp[a * 2 + 1];
    const double dl = std::hypot(dx, dz);
    if (dl > 1e-6) {
      dx /= dl;
      dz /= dl;
    } else dx = dz = 0;
    std::vector<Cand> cands;
    roads.roadsIn(x - R, z - R, x + R, z + R, [&](int e) {
      const Course* c = course(e);
      if (!c) return;
      const Projection pr = project(*c, x, z);
      if (pr.d > R) return;
      for (int dir = 0; dir < 2; dir++) {
        const int w = e * 2 + dir;
        if (!(g.len[static_cast<size_t>(w)] > 0) || (g.kind[static_cast<size_t>(w)] & RG_NO_ENTRY) != 0 || (roads.usable && !roads.usable(w))) continue;
        const double tx = dir == 0 ? pr.tx : -pr.tx;
        const double tz = dir == 0 ? pr.tz : -pr.tz;
        const double cos = dx != 0 || dz != 0 ? tx * dx + tz * dz : 1;
        // (against the direction of drawing: not this way – a one-way street drawn the wrong way round is driven round)
        if (cos < -0.25) continue;
        cands.push_back({w, dir == 0 ? pr.s : c->len - pr.s, c->len, pr.px, pr.pz, (pr.d / R) * (pr.d / R) * 3 + (1 - cos) * 2});
      }
    });
    if (cands.empty()) continue;
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& p, const Cand& q) { return p.cost < q.cost; });
    // (the same way twice – a road whose course comes near twice – once, the better)
    std::unordered_set<int> seen;
    std::vector<Cand> keep;
    for (const Cand& c : cands) {
      if (seen.count(c.w)) continue;
      seen.insert(c.w);
      keep.push_back(c);
      if (keep.size() >= MAX_CANDS) break;
    }
    layers.push_back({i, std::move(keep)});
  }
  if (layers.empty()) return std::nullopt;

  // --- the best chain of candidates (Viterbi): each step costs the detour of the way by the road over the line
  std::vector<std::vector<Step>> nodes;
  {
    std::vector<Step> first;
    for (const Cand& c : layers[0].cands) first.push_back({c.cost, -1, {}});
    nodes.push_back(std::move(first));
  }
  std::vector<const Layer*> kept = {&layers[0]};
  for (size_t li = 1; li < layers.size(); li++) {
    const Layer& prevLayer = *kept.back();
    const std::vector<Step>& prevNodes = nodes.back();
    const Layer& layer = layers[li];
    const double D = std::hypot(smp[layer.i * 2] - smp[prevLayer.i * 2], smp[layer.i * 2 + 1] - smp[prevLayer.i * 2 + 1]);
    std::unordered_set<int> goals;
    for (const Cand& c : layer.cands) goals.insert(c.w);
    const auto advance = [&](double limit) {
      std::vector<Step> out(layer.cands.size());
      for (size_t ai = 0; ai < prevLayer.cands.size(); ai++) {
        const Cand& ca = prevLayer.cands[ai];
        const double base = prevNodes[ai].cost;
        if (!std::isfinite(base)) continue;
        std::optional<Found> found;
        for (size_t bi = 0; bi < layer.cands.size(); bi++) {
          const Cand& cb = layer.cands[bi];
          double L;
          std::vector<int> path;
          int u = 0;
          if (cb.w == ca.w && cb.s >= ca.s - 3) {
            // on along the same way
            L = std::max(0.0, cb.s - ca.s);
          } else {
            if (!found) found = search(g, ca.w, goals, limit, roads.usable);
            const auto d = found->dist.find(cb.w);
            if (d == found->dist.end()) continue;
            L = ca.len - ca.s + d->second + cb.s;
            path = pathTo(found->prev, ca.w, cb.w);
            if (path.empty()) continue;
            const auto ut = found->uturns.find(cb.w);
            u = ut == found->uturns.end() ? 0 : ut->second;
          }
          const double cost = base + cb.cost + std::abs(L - D) / 10 + u * 5;
          if (cost < out[bi].cost) out[bi] = {cost, static_cast<int>(ai), std::move(path)};
        }
      }
      return out;
    };
    std::vector<Step> next = advance(std::max(250.0, 3 * D + 120));
    // nothing leads there within reach: once more, further – else the sample is left out
    if (!anyFinite(next)) next = advance(std::max(4000.0, 6 * D));
    if (!anyFinite(next)) continue;
    nodes.push_back(std::move(next));
    kept.push_back(&layer);
  }

  // --- back from the best end: the chain, then the ways and the line along them
  const std::vector<Step>& last = nodes.back();
  int bi = 0;
  for (size_t k = 1; k < last.size(); k++)
    if (last[k].cost < last[static_cast<size_t>(bi)].cost) bi = static_cast<int>(k);
  struct Link {
    Cand c;
    std::vector<int> path;
  };
  std::vector<Link> chain;
  for (size_t li = nodes.size(); li-- > 0;) {
    const Step& node = nodes[li][static_cast<size_t>(bi)];
    chain.push_back({kept[li]->cands[static_cast<size_t>(bi)], node.path});
    bi = node.back;
  }
  std::reverse(chain.begin(), chain.end());
  const Cand first = chain[0].c;
  std::vector<int> ways = {first.w};
  // the pieces driven: way, from, to (metres along it)
  struct Piece {
    int w;
    double a;
    double b;
  };
  std::vector<Piece> pieces = {{first.w, first.s, first.len}};
  for (size_t k = 1; k < chain.size(); k++) {
    const std::vector<int>& path = chain[k].path;
    if (path.empty()) continue;
    for (int w : path) {
      const Course* c = course(w >> 1);
      const double len = c ? c->len : 0;
      ways.push_back(w);
      pieces.push_back({w, 0, len});
    }
  }
  const Cand end = chain.back().c;
  pieces.back().b = end.s;
  std::vector<double> line;
  double metres = 0;
  for (const Piece& p : pieces) {
    const Course* c = course(p.w >> 1);
    if (!c || p.b <= p.a) continue;
    appendPiece(wayPoints(*c, p.w), p.a, p.b, line);
    metres += p.b - p.a;
  }
  if (metres < 5 || line.size() < 4) return std::nullopt;
  const auto yawAt = [&line](size_t i, size_t j) { return std::atan2(-(line[j] - line[i]), -(line[j + 1] - line[i + 1])); };
  const size_t L = line.size();
  MatchedPath m;
  m.ways = std::move(ways);
  m.line.assign(line.begin(), line.end());
  m.metres = metres;
  m.start = {line[0], line[1], yawAt(0, 2)};
  m.end = {line[L - 2], line[L - 1], yawAt(L - 4, L - 2)};
  return m;
}

TourPreview previewOf(const MatchedPath& m) {
  TourPreview p;
  p.plan.ways.assign(m.ways.begin() + 1, m.ways.end());
  p.plan.metres = m.metres;
  p.plan.fresh = m.metres;
  p.plan.again = 0;
  p.plan.valued = m.metres;
  p.line = m.line;
  p.from = m.ways[0];
  p.end.name = "";
  p.end.x = m.end.x;
  p.end.z = m.end.z;
  p.end.yaw = m.end.yaw;
  p.end.kind = "street";
  return p;
}

// ---------------------------------------------------------------- drawing on the map

PathDrawer::PathDrawer(PathDrawHost host_) : host(std::move(host_)) {}

std::string PathDrawer::t(const std::string& key, const TrParams& p) const { return tr(key, host.lang(), p); }

PathDrawer::Finger* PathDrawer::finger(int id) {
  for (auto& f : fingers)
    if (f.first == id) return &f.second;
  return nullptr;
}

void PathDrawer::attach(const dom::El& root_, const dom::El& canvas_, const dom::El& ui, const dom::El& after) {
  root = root_;
  canvas = canvas_;
  banner = el("div", "bm-drawbar");
  banner.hidden(true);
  after.after(banner);
  // (in the capturing phase: while drawing, the map's own dragging does not see the finger)
  const dom::ListenOptions capture{.capture = true};
  listeners[0] = root.on("pointerdown", [this](dom::Event& e) { down(e); }, capture);
  listeners[1] = root.on("pointermove", [this](dom::Event& e) { move(e); }, capture);
  listeners[2] = root.on("pointerup", [this](dom::Event& e) { up(e); }, capture);
  listeners[3] = root.on("pointercancel", [this](dom::Event& e) { up(e); }, capture);
  active = false;
}

void PathDrawer::detach() {
  if (root) {
    root.off("pointerdown", listeners[0], true);
    root.off("pointermove", listeners[1], true);
    root.off("pointerup", listeners[2], true);
    root.off("pointercancel", listeners[3], true);
  }
  banner.remove();
  banner = El();
  root = El();
  canvas = El();
  active = false;
  cur.reset();
  fingers.clear();
}

void PathDrawer::down(dom::Event& e) {
  if (!active || !canvas || e.target() != canvas) return;
  e.stopPropagation();
  e.preventDefault();
  canvas.setPointerCapture(e.pointerId);
  if (Finger* f = finger(e.pointerId)) *f = {e.clientX, e.clientY};
  else fingers.emplace_back(e.pointerId, Finger{e.clientX, e.clientY});
  if (fingers.size() == 1) {
    const std::array<double, 2> w = host.toWorld(e.clientX, e.clientY);
    cur = std::vector<double>{w[0], w[1]};
    lastPx = {e.clientX, e.clientY};
  } else {
    // a second finger: the map is moved, not drawn on (a stroke hardly begun is dropped)
    if (cur && cur->size() >= 16) finish();
    cur.reset();
    pinch = 0;
  }
}

void PathDrawer::move(dom::Event& e) {
  Finger* f = finger(e.pointerId);
  if (!active || !f) return;
  e.stopPropagation();
  const double dx = e.clientX - f->x;
  const double dy = e.clientY - f->y;
  f->x = e.clientX;
  f->y = e.clientY;
  if (fingers.size() == 1 && cur) {
    if (std::hypot(e.clientX - lastPx.x, e.clientY - lastPx.y) < 4) return;
    lastPx = {e.clientX, e.clientY};
    const std::array<double, 2> w = host.toWorld(e.clientX, e.clientY);
    cur->push_back(w[0]);
    cur->push_back(w[1]);
    host.redraw();
  } else if (fingers.size() == 2) {
    const Finger a = fingers[0].second;
    const Finger b = fingers[1].second;
    const double d = std::hypot(a.x - b.x, a.y - b.y);
    host.pan(dx / 2, dy / 2);
    if (pinch > 0 && d > 0) host.zoom(d / pinch, (a.x + b.x) / 2, (a.y + b.y) / 2);
    pinch = d;
  }
}

void PathDrawer::up(dom::Event& e) {
  bool had = false;
  for (size_t i = 0; i < fingers.size(); i++)
    if (fingers[i].first == e.pointerId) {
      fingers.erase(fingers.begin() + static_cast<long>(i));
      had = true;
      break;
    }
  if (!had) return;
  e.stopPropagation();
  if (fingers.empty() && cur) finish();
  pinch = 0;
}

/** A stroke is done: it is added to the drawing, and the whole drawing laid onto the roads anew. */
void PathDrawer::finish() {
  std::optional<std::vector<double>> s = std::move(cur);
  cur.reset();
  if (!s || s->size() < 4) return;
  strokes.push_back(std::move(*s));
  rematch();
}

void PathDrawer::rematch() {
  match.reset();
  note = "";
  if (strokes.empty()) {
    showBanner();
    host.redraw();
    return;
  }
  const RoadSource* roads = host.roads();
  if (!roads) note = t("realityLoading");
  else {
    std::vector<double> flat;
    for (const std::vector<double>& s : strokes) flat.insert(flat.end(), s.begin(), s.end());
    match = matchStroke(flat, *roads);
    if (!match) note = t("drawNone");
  }
  showBanner();
  host.redraw();
}

void PathDrawer::setActive(bool on) {
  if (on == active) return;
  active = on;
  cur.reset();
  fingers.clear();
  // (the roads of the whole map may have been read meanwhile)
  if (!on && !strokes.empty() && !match) rematch();
  showBanner();
  host.mode(on);
  host.redraw();
}

void PathDrawer::clear() {
  strokes.clear();
  match.reset();
  note = "";
  cur.reset();
  showBanner();
  host.redraw();
}

/** The bar over the map while drawing: what to do, how long the way is, undo and done. */
void PathDrawer::showBanner() {
  const El b = banner;
  if (!b) return;
  b.hidden(!active);
  if (!active) return;
  b.replaceChildren();
  El text = el("div");
  text.append(el("b", "", t("drawActive")));
  const std::string sub = match ? t("drawResult", {{"km", formatUnit(match->metres / 1000, Unit::Kilometer, host.lang(), 1)}}) : note;
  if (!sub.empty()) text.append(el("small", "", sub));
  El undo = el("button", "btn ghost", t("drawUndo"));
  undo.type("button");
  undo.disabled(strokes.empty());
  undo.onclick([this] {
    if (!strokes.empty()) strokes.pop_back();
    rematch();
  });
  El done = el("button", "btn", t("drawDone"));
  done.type("button");
  done.onclick([this] { setActive(false); });
  b.append(text, undo, done);
}

dom::El PathDrawer::section() {
  El box = el("div", "bm-tour bm-draw");
  box.append(el("b", "", t("drawTitle")), el("small", "", t("drawHint")));
  El row = el("div", "bm-row");
  El draw = el("button", "btn ghost", t(!strokes.empty() ? "drawMore" : "drawStart"));
  draw.type("button");
  draw.onclick([this] { setActive(true); });
  row.append(draw);
  if (!strokes.empty()) {
    El clearBtn = el("button", "btn ghost", t("drawClear"));
    clearBtn.type("button");
    clearBtn.onclick([this, box] {
      clear();
      box.replaceWith(section());
    });
    row.append(clearBtn);
  }
  box.append(row);
  if (!strokes.empty() && !match && host.roads()) rematch();
  if (match) {
    box.append(el("small", "bm-result", t("drawResult", {{"km", formatUnit(match->metres / 1000, Unit::Kilometer, host.lang(), 1)}})));
    El go = el("div", "bm-row");
    El autoBtn = el("button", "btn", t("drawGo"));
    autoBtn.type("button");
    autoBtn.onclick([this] { drive(false); });
    El self = el("button", "btn ghost", t("tripSelf"));
    self.type("button");
    self.onclick([this] { drive(true); });
    go.append(autoBtn, self);
    box.append(go);
  } else if (!note.empty()) box.append(el("small", "bm-result", note));
  return box;
}

void PathDrawer::drive(bool self) {
  if (!match) return;
  const MatchedPath m = std::move(*match);
  strokes.clear();
  match.reset();
  host.drive(previewOf(m), self);
}

void PathDrawer::draw(const Ctx2D& g, double W, double H, double s, double cx, double cz) {
  if (strokes.empty() && !cur && !match) return;
  const auto sx = [&](double x) { return W / 2 + (x - cx) * s; };
  const auto sy = [&](double z) { return H / 2 + (z - cz) * s; };
  g.save();
  g.lineCap("round");
  g.lineJoin("round");
  // the way along the roads
  if (match) {
    const MatchedPath& m = *match;
    const std::vector<float>& p = m.line;
    g.strokeStyle("#0c1218");
    g.lineWidth(8);
    g.globalAlpha(0.5);
    g.beginPath();
    std::vector<double> xy(p.size());
    for (size_t i = 0; i + 1 < p.size(); i += 2) {
      xy[i] = sx(p[i]);
      xy[i + 1] = sy(p[i + 1]);
    }
    g.polyline(xy);
    g.stroke();
    g.globalAlpha(1);
    g.strokeStyle("#2ee66b");
    g.lineWidth(5);
    g.stroke();
    // arrows along it, the way it is driven
    g.fillStyle("#0c1218");
    double gap = 0;
    for (size_t i = 2; i + 1 < p.size(); i += 2) {
      const double ax = sx(p[i - 2]);
      const double ay = sy(p[i - 1]);
      const double bx = sx(p[i]);
      const double by = sy(p[i + 1]);
      const double l = std::hypot(bx - ax, by - ay);
      gap += l;
      if (gap < 90 || l < 1) continue;
      gap = 0;
      const double ux = (bx - ax) / l;
      const double uy = (by - ay) / l;
      const double mx = (ax + bx) / 2;
      const double my = (ay + by) / 2;
      g.beginPath();
      g.moveTo(mx + ux * 5, my + uy * 5);
      g.lineTo(mx - ux * 3 - uy * 4, my - uy * 3 + ux * 4);
      g.lineTo(mx - ux * 3 + uy * 4, my - uy * 3 - ux * 4);
      g.closePath();
      g.fill();
    }
    const std::pair<const PathEnd*, const char*> ends[2] = {{&m.start, "#2ee66b"}, {&m.end, "#ff5252"}};
    for (const auto& [pt, colour] : ends) {
      g.fillStyle(colour);
      g.strokeStyle("#ffffff");
      g.lineWidth(2);
      g.beginPath();
      g.arc(sx(pt->x), sy(pt->z), 7, 0, M_PI * 2);
      g.fill();
      g.stroke();
    }
  }
  // what was drawn by hand
  g.strokeStyle("#ffffff");
  g.lineWidth(2.5);
  g.globalAlpha(0.85);
  g.setLineDash({6, 5});
  const auto one = [&](const std::vector<double>& st) {
    if (st.size() < 4) return;
    g.beginPath();
    std::vector<double> xy(st.size());
    for (size_t i = 0; i + 1 < st.size(); i += 2) {
      xy[i] = sx(st[i]);
      xy[i + 1] = sy(st[i + 1]);
    }
    g.polyline(xy);
    g.stroke();
  };
  for (const std::vector<double>& st : strokes) one(st);
  if (cur) one(*cur);
  g.setLineDash({});
  g.restore();
}

}  // namespace fs
