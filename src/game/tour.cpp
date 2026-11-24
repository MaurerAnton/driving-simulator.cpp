// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/tour.ts @ v1.7.7
#include "game/tour.h"

#include <algorithm>
#include <cmath>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <limits>
#include <unordered_set>
#include <utility>

#include "core/geom.h"
#include "core/js.h"
#include "platform/plat.h"
#include "world/lanes.h"
#include "world/types.h"

namespace fs {

namespace {

const double INF = std::numeric_limits<double>::infinity();

/**
 * `function*` of the original: something that runs to its next `yield` each time it is asked to go on
 * (`next()`), hands out what it yielded (`value()`), and in the end what it returned (`result()`).
 */
template <class Y, class R>
class Generator {
 public:
  struct promise_type {
    Y current{};
    std::optional<R> result;
    std::exception_ptr error;
    Generator get_return_object() { return Generator(std::coroutine_handle<promise_type>::from_promise(*this)); }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(Y v) {
      current = std::move(v);
      return {};
    }
    void return_value(R v) { result = std::move(v); }
    void unhandled_exception() { error = std::current_exception(); }
  };

  explicit Generator(std::coroutine_handle<promise_type> h) : h_(h) {}
  Generator(Generator&& o) noexcept : h_(std::exchange(o.h_, {})) {}
  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;
  ~Generator() {
    if (h_) h_.destroy();
  }

  /** `!it.next().done`: on to the next yield – false: it has returned */
  bool next() {
    h_.resume();
    if (h_.promise().error) std::rethrow_exception(h_.promise().error);
    return !h_.done();
  }
  const Y& value() const { return h_.promise().current; }
  R& result() { return *h_.promise().result; }

 private:
  std::coroutine_handle<promise_type> h_;
};

/** small deterministic generator of numbers in 0 … 1 */
std::function<double()> chance(double seed) {
  uint32_t a = jsUint32(seed);
  if (a == 0) a = 1;
  return [a]() mutable {
    a = a + 0x6d2b79f5u;
    uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  };
}

/** May a tour lead along this way? */
bool tourable(const RouteGraph& g, size_t w) { return g.len[w] > 0 && (g.kind[w] & (RG_NO_ENTRY | RG_ACCESS_ONLY | RG_MINOR)) == 0; }

/** how much a walk is drawn to streets with few ways on, to staying near the start, to turning right; `depth`: how far it looks ahead */
struct Lean {
  double few;
  double near;
  double right;
  double noise;
  double depth;
};

/** The planning itself: it stops now and then to say how far it is (a share of 0 … 1). */
Generator<double, std::optional<TourPlan>> planner(const RouteGraph& g, int startWay, int goalWay, double metres, const TourOptions& opt) {
  const size_t n = static_cast<size_t>(g.n);
  const size_t start = static_cast<size_t>(startWay);
  const size_t goal = static_cast<size_t>(goalWay);
  // (the network keeps its metres as 32-bit numbers: reckoned with as the 64-bit numbers they are in the original)
  const auto len = [&g](size_t w) { return static_cast<double>(g.len[w]); };
  const auto across = [&g](size_t k) { return static_cast<double>(g.across[k]); };
  std::vector<uint8_t> ok(n);
  for (size_t w = 0; w < n; w++) ok[w] = tourable(g, w) && !(opt.avoid && opt.avoid(static_cast<int>(w))) ? 1 : 0;
  // A tour may begin and end where a tour does not otherwise lead – on a car park, in a yard, on a
  // private road, in a traffic-calmed street: the ways from the start out to the streets, and from
  // the streets in to the end, may be driven (they are not what the tour is after).
  const auto open = [&](size_t first, bool forward) {
    std::vector<size_t> stack = {first};
    std::unordered_set<size_t> seen = {first};
    while (!stack.empty()) {
      const size_t w = stack.back();
      stack.pop_back();
      if (w != first && tourable(g, w)) continue;
      if (g.len[w] > 0) ok[w] = 1;
      if (forward) {
        for (size_t k = g.first[w]; k < g.first[w + 1]; k++) {
          const size_t v = g.to[k];
          if (g.turn[k] != TURN_UTURN && !seen.count(v) && g.len[v] > 0 && seen.size() < 400) {
            seen.insert(v);
            stack.push_back(v);
          }
        }
      } else {
        for (size_t p = g.firstIn[w]; p < g.firstIn[w + 1]; p++) {
          const size_t v = g.from[p];
          if (g.turn[g.inTurn[p]] != TURN_UTURN && !seen.count(v) && g.len[v] > 0 && seen.size() < 400) {
            seen.insert(v);
            stack.push_back(v);
          }
        }
      }
    }
  };
  open(start, true);
  open(goal, false);
  ok[goal] = g.len[goal] > 0 ? 1 : 0;

  // --- how far it is from the end of each way to the end of the tour (metres, without turning round) ---
  std::vector<double> home(n, INF);
  MinHeap heap(4096);
  home[goal] = 0;
  heap.push(static_cast<uint32_t>(goal), 0);
  size_t settled = 0;
  while (heap.size > 0) {
    const double key = heap.topKey();
    const size_t w = heap.pop();
    if (key > home[w]) continue;
    if ((++settled & 1023) == 0) co_yield std::min(0.1, (static_cast<double>(settled) / static_cast<double>(n)) * 0.1);
    for (size_t p = g.firstIn[w]; p < g.firstIn[w + 1]; p++) {
      const size_t v = g.from[p];
      const size_t k = g.inTurn[p];
      if (g.turn[k] == TURN_UTURN || (!ok[v] && v != start)) continue;
      const double nd = home[w] + across(k) + len(w);
      if (nd < home[v]) {
        home[v] = nd;
        heap.push(static_cast<uint32_t>(v), nd);
      }
    }
  }
  if (!std::isfinite(home[start])) co_return std::nullopt;

  // --- what is new: every road once, whichever way it is driven; motorways are not what a tour is for ---
  const size_t edges = n >> 1;
  std::vector<uint8_t> fresh0(edges);
  /** how welcome each road is as a new street (the wishes of the driver) */
  std::vector<float> worthOf(edges, 1.0f);
  const auto worth = [&worthOf](size_t e) { return static_cast<double>(worthOf[e]); };
  for (size_t e = 0; e < edges; e++) {
    const std::ptrdiff_t w = tourable(g, e * 2) ? static_cast<std::ptrdiff_t>(e * 2) : tourable(g, e * 2 + 1) ? static_cast<std::ptrdiff_t>(e * 2 + 1) : -1;
    if (w < 0 || g.cls[static_cast<size_t>(w)] <= TRUNK_LINK) continue;
    if (opt.known && opt.known(static_cast<int>(e))) continue;
    if (opt.like) worthOf[e] = static_cast<float>(jsMax(0, opt.like(static_cast<int>(e))));
    if (worth(e) < 0.05) continue;
    fresh0[e] = 1;
  }
  fresh0[start >> 1] = 0;
  const double sx = static_cast<double>(g.x1[start]);
  const double sz = static_cast<double>(g.z1[start]);

  // --- searching for the nearest new street from a way (metres) ---
  std::vector<double> dist(n);
  std::vector<uint32_t> stamp(n);
  std::vector<int32_t> prev(n);
  uint32_t round = 0;
  const auto toNearestFresh = [&](size_t from, double left, const std::vector<uint8_t>& fresh) -> std::optional<std::vector<size_t>> {
    round++;
    heap.clear();
    dist[from] = 0;
    stamp[from] = round;
    prev[from] = -1;
    heap.push(static_cast<uint32_t>(from), 0);
    while (heap.size > 0) {
      const double key = heap.topKey();
      const size_t w = heap.pop();
      if (key > dist[w]) continue;
      if (w != from && fresh[w >> 1]) {
        std::vector<size_t> path;
        for (size_t v = w; v != from; v = static_cast<size_t>(prev[v])) path.push_back(v);
        std::reverse(path.begin(), path.end());
        return path;
      }
      for (size_t k = g.first[w]; k < g.first[w + 1]; k++) {
        const size_t v = g.to[k];
        if (!ok[v] || g.turn[k] == TURN_UTURN) continue;
        const double nd = dist[w] + across(k) + len(v);
        // (only what leaves enough to get to the end from there)
        if (nd + home[v] > left) continue;
        if (stamp[v] != round || nd < dist[v]) {
          stamp[v] = round;
          dist[v] = nd;
          prev[v] = static_cast<int32_t>(w);
          heap.push(static_cast<uint32_t>(v), nd);
        }
      }
    }
    return std::nullopt;
  };

  /** how far the planning is: the first walk is most of it, the further ones fill the time it may take */
  double share = 0.1;
  bool first = true;
  const double t0 = plat::dateMs();
  /** One walk.  `lean`: how much it is drawn to streets with few ways on, to staying near the start, to turning right; `depth`: how far it looks ahead. */
  const auto walk = [&](const std::function<double()>& rand, Lean lean) -> Generator<double, TourPlan> {
    std::vector<uint8_t> fresh = fresh0;
    std::vector<int> ways;
    double used = 0;
    double gain = 0;
    size_t w = start;
    std::vector<size_t> touched;
    /** new metres a simple walk on from way v gathers within `depth` streets (v itself not counted) */
    const auto onward = [&](size_t v, double left, double depth) {
      double got = 0;
      size_t cur = v;
      for (double step = 0; step < depth; step++) {
        std::ptrdiff_t best = -1;
        double bl = 0;
        size_t bk = 0;
        for (size_t k = g.first[cur]; k < g.first[cur + 1]; k++) {
          const size_t u = g.to[k];
          if (!ok[u] || !fresh[u >> 1] || g.turn[k] == TURN_UTURN) continue;
          const double cost = across(k) + len(u);
          if (cost + home[u] > left) continue;
          if (len(u) * worth(u >> 1) > bl) {
            bl = len(u) * worth(u >> 1);
            best = static_cast<std::ptrdiff_t>(u);
            bk = k;
          }
        }
        if (best < 0) break;
        got += bl;
        left -= across(bk) + len(static_cast<size_t>(best));
        fresh[static_cast<size_t>(best) >> 1] = 0;
        touched.push_back(static_cast<size_t>(best) >> 1);
        cur = static_cast<size_t>(best);
      }
      for (size_t e : touched) fresh[e] = 1;
      touched.clear();
      return got;
    };
    double valued = 0;
    const auto take = [&](size_t v, double cost) {
      ways.push_back(static_cast<int>(v));
      used += cost;
      if (fresh[v >> 1]) {
        gain += len(v);
        valued += len(v) * worth(v >> 1);
        fresh[v >> 1] = 0;
      }
      w = v;
    };
    for (int guard = 0; guard < 400000; guard++) {
      if ((guard & 15) == 0) co_yield first ? 0.1 + 0.75 * std::min(1.0, used / std::max(metres, 1.0)) : share;
      const double left = metres - used;
      std::ptrdiff_t best = -1;
      double bestCost = 0;
      double bestScore = -INF;
      for (size_t k = g.first[w]; k < g.first[w + 1]; k++) {
        const size_t v = g.to[k];
        if (!ok[v] || !fresh[v >> 1] || g.turn[k] == TURN_UTURN) continue;
        const double cost = across(k) + len(v);
        if (cost + home[v] > left) continue;
        // how many new streets lead on from it: one that is nearly closed in is taken while we are here
        double exits = 0;
        for (size_t k2 = g.first[v]; k2 < g.first[v + 1]; k2++)
          if (ok[g.to[k2]] && fresh[g.to[k2] >> 1] && g.turn[k2] != TURN_UTURN && g.to[k2] >> 1 != v >> 1) exits++;
        fresh[v >> 1] = 0;
        const double ahead = onward(v, left - cost, lean.depth);
        fresh[v >> 1] = 1;
        const double far = jsHypot(static_cast<double>(g.x1[v]) - sx, static_cast<double>(g.z1[v]) - sz);
        const double score = (len(v) * worth(v >> 1) + ahead) / (cost + ahead + 1) + lean.few / (1 + exits) - (lean.near * far) / std::max(400.0, metres * 0.25) +
                             (g.turn[k] == TURN_RIGHT ? lean.right : 0) + rand() * lean.noise;
        if (score > bestScore) {
          bestScore = score;
          best = static_cast<std::ptrdiff_t>(v);
          bestCost = cost;
        }
      }
      if (best >= 0) {
        take(static_cast<size_t>(best), bestCost);
        continue;
      }
      const std::optional<std::vector<size_t>> path = toNearestFresh(w, left, fresh);
      if (path) {
        for (size_t v : *path) {
          double cost = len(v);
          for (size_t k = g.first[w]; k < g.first[w + 1]; k++)
            if (g.to[k] == v) cost += across(k);
          take(v, cost);
        }
        continue;
      }
      break;
    }
    // --- to the end: always downhill in the distance still to go ---
    for (int guard = 0; guard < 100000 && !(w == goal && !ways.empty() && home[w] == 0 && used > 0); guard++) {
      if (w == goal && !ways.empty()) break;
      std::ptrdiff_t best = -1;
      double bestCost = 0;
      double bd = INF;
      for (size_t k = g.first[w]; k < g.first[w + 1]; k++) {
        const size_t v = g.to[k];
        if (!ok[v] || g.turn[k] == TURN_UTURN) continue;
        const double cost = across(k) + len(v);
        if (cost + home[v] < bd) {
          bd = cost + home[v];
          best = static_cast<std::ptrdiff_t>(v);
          bestCost = cost;
        }
      }
      if (best < 0) break;
      take(static_cast<size_t>(best), bestCost);
    }
    co_return TourPlan{.ways = std::move(ways), .metres = used, .fresh = gain, .again = used - gain, .valued = valued};
  };

  const std::function<double()> rand = chance(opt.seed.value_or(1));
  const double until = plat::dateMs() + opt.budgetMs.value_or(350);
  std::optional<TourPlan> bestPlan;
  for (int i = 0; i < 400; i++) {
    const Lean lean = i == 0 ? Lean{0.5, 0.35, 0.05, 0.02, 6} : Lean{rand() * 1.2, rand() * 0.9, rand() * 0.2 - 0.05, 0.02 + rand() * 0.25, 2 + std::floor(rand() * 9)};
    // (`yield* walk(rand, lean)`: what the walk says on its way is passed on)
    Generator<double, TourPlan> steps = walk(rand, lean);
    while (steps.next()) co_yield steps.value();
    TourPlan plan = std::move(steps.result());
    first = false;
    share = std::max(share, 0.85 + 0.15 * std::min(1.0, (plat::dateMs() - t0) / std::max(opt.budgetMs.value_or(350), 1.0)));
    // must end where it is meant to
    if (!plan.ways.empty() && static_cast<size_t>(plan.ways.back()) != goal) continue;
    // (the best: the most of what was wished for – new streets, and of the kind asked for)
    if (!bestPlan || plan.valued > bestPlan->valued + 1 || (std::fabs(plan.valued - bestPlan->valued) <= 1 && plan.metres < bestPlan->metres)) bestPlan = std::move(plan);
    if (plat::dateMs() > until) break;
  }
  // (nowhere to go from the start but by turning round: no tour)
  if (bestPlan && !bestPlan->ways.empty()) co_return bestPlan;
  co_return std::nullopt;
}

}  // namespace

std::optional<TourPlan> planTour(const RouteGraph& g, int start, int goal, double metres, const TourOptions& opt) {
  Generator<double, std::optional<TourPlan>> it = planner(g, start, goal, metres, opt);
  while (it.next()) {
  }
  return std::move(it.result());
}

Task<std::optional<TourPlan>> planTourInSlices(const RouteGraph& g, int start, int goal, double metres, TourSliceOptions opt) {
  Generator<double, std::optional<TourPlan>> it = planner(g, start, goal, metres, opt);
  const double slice = opt.sliceMs.value_or(30);
  double t0 = plat::nowMs();
  bool more = it.next();
  while (more) {
    if (plat::nowMs() - t0 > slice) {
      if (opt.progress) opt.progress(it.value());
      co_await plat::sleep(0);
      t0 = plat::nowMs();
    }
    more = it.next();
  }
  if (opt.progress) opt.progress(1);
  co_return std::move(it.result());
}

}  // namespace fs
