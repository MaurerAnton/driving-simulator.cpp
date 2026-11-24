// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * A tour of the streets (game/tour.h) and the navigation that follows it (game/nav.h): the
 * route graph of a small town, the quickest way, round trips and tours to another street, the
 * paths through the junctions, being led back onto a tour, car parks, the driver's wishes.
 *
 * Origin: tests/game/tour.test.ts @ v1.7.7
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <vector>

#include "game/nav.h"
#include "game/tour.h"
#include "platform/native/native.h"
#include "tests/town.h"
#include "world/lanes.h"
#include "world/routegraph.h"

using namespace fs;

/** Is each way of the list reached from the one before by a turn the junction allows (and none of them by turning round)? */
static bool chained(const RouteGraph& g, int start, const std::vector<int>& ways) {
  size_t cur = static_cast<size_t>(start);
  for (int w : ways) {
    bool ok = false;
    for (size_t k = g.first[cur]; k < g.first[cur + 1]; k++)
      if (g.to[k] == static_cast<uint32_t>(w) && g.turn[k] != TURN_UTURN) ok = true;
    if (!ok) return false;
    cur = static_cast<size_t>(w);
  }
  return true;
}

static double len(const RouteGraph& g, int w) { return static_cast<double>(g.len[static_cast<size_t>(w)]); }

int main() {
  // the route graph has a way for each direction a street can be driven in
  {
    TownOptions opt;
    opt.oneWay = {{0, 0, 1, 0}};
    const Town town = makeTown(opt);
    const RouteGraph g = routeGraphOf(*town.lanes);
    assert(static_cast<size_t>(g.n) == town.world->edges.size() * 2);
    const int one = town.street(0, 0, 1, 0);
    assert(len(g, one * 2) > 0);         // the one-way street can be driven its way
    assert(len(g, one * 2 + 1) == 0);    // and not against it
    const int two = town.street(1, 1, 2, 1);
    assert(len(g, two * 2) > 0 && len(g, two * 2 + 1) > 0);
    // every turn is listed from both sides
    assert(g.from.size() == g.to.size());
    for (size_t w = 0; w < static_cast<size_t>(g.n); w++)
      for (size_t p = g.firstIn[w]; p < g.firstIn[w + 1]; p++) assert(g.to[g.inTurn[p]] == w);
  }

  // the quickest route leads from one street to another along connected ways
  {
    const Town town = makeTown();
    const RouteGraph g = routeGraphOf(*town.lanes);
    const int from = town.street(0, 0, 1, 0) * 2;
    const int to = town.street(2, 3, 3, 3) * 2;
    const std::optional<std::vector<int>> r = quickest(g, from, to);
    assert(r && r->size() >= 4);   // a route across the town
    assert(r->back() == to);
    size_t cur = static_cast<size_t>(from);
    for (int w : *r) {
      bool ok = false;
      for (size_t k = g.first[cur]; k < g.first[cur + 1]; k++)
        if (g.to[k] == static_cast<uint32_t>(w)) ok = true;
      assert(ok);   // each way follows the one before
      cur = static_cast<size_t>(w);
    }
  }

  // a round trip leads through many streets once and back to where it began
  {
    TownOptions opt;
    opt.cols = 6;
    opt.rows = 6;
    const Town town = makeTown(opt);
    const RouteGraph g = routeGraphOf(*town.lanes);
    const int start = town.street(2, 2, 3, 2) * 2;
    const std::optional<TourPlan> plan = planTour(g, start, start, 3000, {.budgetMs = 150, .seed = 5});
    assert(plan);   // a tour is found
    assert(plan->ways.size() > 10);
    assert(plan->ways.back() == start);        // it ends on the street it started from
    assert(chained(g, start, plan->ways));     // no turn that is not allowed, no turning round
    assert(plan->metres <= 3000 + 1);          // not longer than asked for
    assert(plan->metres >= 2400);              // and not much shorter
    assert(std::fabs(plan->fresh + plan->again - plan->metres) < 1);
    assert(plan->fresh / plan->metres > 0.75);   // mostly along new streets
    // the same seed plans the same tour
    const std::optional<TourPlan> again = planTour(g, start, start, 3000, {.budgetMs = 150, .seed = 5});
    assert(again && again->fresh >= plan->fresh * 0.9);
    // (the first walk of a seed is the same whatever time there is: with no time for more, the same tour twice)
    const std::optional<TourPlan> once = planTour(g, start, start, 3000, {.budgetMs = 0, .seed = 5});
    const std::optional<TourPlan> twice = planTour(g, start, start, 3000, {.budgetMs = 0, .seed = 5});
    assert(once && twice && once->ways == twice->ways && once->metres == twice->metres);
  }

  // a tour to another street ends there, and never enters a street it could only leave by turning round
  {
    TownOptions opt;
    opt.cols = 5;
    opt.rows = 4;
    opt.deadEnd = 120;
    const Town town = makeTown(opt);
    const RouteGraph g = routeGraphOf(*town.lanes);
    const int start = town.street(0, 0, 1, 0) * 2;
    const int goal = town.street(3, 3, 4, 3) * 2;
    const std::optional<TourPlan> plan = planTour(g, start, goal, 2500, {.budgetMs = 150, .seed = 2});
    assert(plan);
    assert(plan->ways.back() == goal);
    assert(chained(g, start, plan->ways));
    const int culDeSac = static_cast<int>(town.world->edges.size()) - 1;
    assert(std::none_of(plan->ways.begin(), plan->ways.end(), [culDeSac](int w) { return w >> 1 == culDeSac; }));   // the cul-de-sac is left out
    // streets that are known already are nothing new
    const std::optional<TourPlan> known = planTour(g, start, goal, 2500, {.known = [](int) { return true; }, .budgetMs = 100, .seed = 2});
    assert(known && known->fresh == 0);
    // asked for less than the way there takes: the way there it is
    const std::optional<TourPlan> shortOne = planTour(g, start, goal, 100, {.budgetMs = 50, .seed = 2});
    assert(shortOne && shortOne->ways.back() == goal && shortOne->metres > 100);
  }

  // a planned tour becomes lanes and paths through the junctions, and the navigation follows it to its end
  {
    TownOptions opt;
    opt.cols = 5;
    opt.rows = 5;
    const Town town = makeTown(opt);
    const RouteGraph g = routeGraphOf(*town.lanes);
    Lane* first = town.lanes->edgeLanes[town.street(1, 1, 2, 1)]->fwd[0];
    const int start = wayOf(*first);
    const std::optional<TourPlan> plan = planTour(g, start, start, 2200, {.budgetMs = 150, .seed = 9});
    assert(plan && plan->ways.size() > 6);
    const std::optional<std::vector<Connector*>> path = connectorsAlong(*town.lanes, plan->ways, *first);
    assert(path);   // the ways are connected lane by lane
    assert(path->size() == plan->ways.size());
    const Lane* lane = first;
    for (size_t i = 0; i < path->size(); i++) {
      assert(wayOf(*(*path)[i]->from) == wayOf(*lane));   // each path leaves the road the one before led onto
      assert(wayOf(*(*path)[i]->to) == plan->ways[i]);
      lane = (*path)[i]->to;
    }
    // drive it: the tour passes its first street again only at the very end – it is not over before
    Navigator nav(*town.lanes);
    nav.setPath(*path, *first);
    assert(nav.busy() && nav.left() == static_cast<int>(path->size()));
    assert(nav.routeLanes->size() == path->size() + 1 && nav.routeLanes->front() == first);
    NavHint h = nav.update(0.1, first, 1);
    assert(h.done == false);   // standing on the street the tour ends on, at its start: not arrived
    for (size_t i = 0; i < path->size(); i++) {
      h = nav.update(0.1, (*path)[i]->to, 1);
      assert(h.done == (i == path->size() - 1));   // arrived exactly at the end
    }
    assert(h.what == "routeDone");
    // (and with the same tour planned a slice at a time: the same ways, and word of how far it is)
    double said = -1;
    TourSliceOptions slices;
    slices.budgetMs = 0;
    slices.seed = 9;
    slices.sliceMs = 0;
    slices.progress = [&said](double share) { said = share; };
    const std::optional<TourPlan> sliced = plat::native::wait(planTourInSlices(g, start, start, 2200, slices));
    const std::optional<TourPlan> whole = planTour(g, start, start, 2200, {.budgetMs = 0, .seed = 9});
    assert(sliced && whole && sliced->ways == whole->ways && said == 1);
  }

  // whoever leaves a tour is led back onto it
  {
    TownOptions opt;
    opt.cols = 5;
    opt.rows = 5;
    const Town town = makeTown(opt);
    const RouteGraph g = routeGraphOf(*town.lanes);
    Lane* first = town.lanes->edgeLanes[town.street(1, 1, 2, 1)]->fwd[0];
    const std::optional<TourPlan> plan = planTour(g, wayOf(*first), wayOf(*first), 2200, {.budgetMs = 150, .seed = 9});
    assert(plan);
    const std::optional<std::vector<Connector*>> path = connectorsAlong(*town.lanes, plan->ways, *first);
    assert(path);
    Navigator nav(*town.lanes);
    nav.setPath(*path, *first);
    nav.update(0.1, first, 1);
    const auto lanesBefore = nav.routeLanes;
    // a wrong turn at the first junction
    Connector* wrong = nullptr;
    for (Connector* c : first->out) {
      if (c != (*path)[0] && c->turn != TURN_UTURN) {
        wrong = c;
        break;
      }
    }
    assert(wrong);   // there is another way to go at the first junction
    for (double t = 0; t < 2; t += 0.1) nav.update(0.1, wrong->to, 1);
    assert(nav.route && nav.route->size() > 0 && wayOf(*(*nav.route)[0]->from) == wayOf(*wrong->to));   // a new route from where the car is now
    const std::vector<Connector*>& route = *nav.route;
    // which joins the tour again and follows it to its end
    assert(route.size() >= 5 && std::equal(route.end() - 5, route.end(), path->end() - 5));
    // (whoever draws the route sees that it is another one)
    assert(nav.routeLanes != lanesBefore && nav.routeLanes->front() == wrong->to);
  }

  // a tour may begin and end on a car park that tours do not otherwise lead through
  {
    TownOptions opt;
    opt.cols = 5;
    opt.rows = 5;
    const Town town = makeTown(opt);
    RouteGraph g = routeGraphOf(*town.lanes);
    // two streets in a row become the aisles of a car park (to get somewhere, not to drive through)
    const int aisle[2] = {town.street(0, 0, 1, 0), town.street(1, 0, 2, 0)};
    for (int e : aisle)
      for (int d : {0, 1}) g.kind[static_cast<size_t>(e * 2 + d)] |= RG_ACCESS_ONLY;
    const int start = aisle[0] * 2;
    const std::optional<TourPlan> plan = planTour(g, start, start, 2000, {.budgetMs = 120, .seed = 4});
    assert(plan && plan->ways.size() > 5);   // a tour is found from the car park
    assert(plan->ways.back() == start);      // and ends there
    assert(chained(g, start, plan->ways));
    assert(plan->fresh > 1000);
    // another car park elsewhere is not driven through
    const int other = town.street(3, 3, 4, 3);
    for (int d : {0, 1}) g.kind[static_cast<size_t>(other * 2 + d)] |= RG_ACCESS_ONLY;
    const std::optional<TourPlan> plan2 = planTour(g, start, start, 3000, {.budgetMs = 120, .seed = 4});
    assert(plan2 && std::none_of(plan2->ways.begin(), plan2->ways.end(), [other](int w) { return w >> 1 == other; }));
  }

  // wishes: a road left out is never driven; the kind of street wished for gets most of the tour
  {
    TownOptions opt;
    opt.cols = 6;
    opt.rows = 6;
    const Town town = makeTown(opt);
    const RouteGraph g = routeGraphOf(*town.lanes);
    const int start = town.street(2, 2, 3, 2) * 2;
    // the streets of the middle row (z = 200) are left out: the tour goes round them
    std::set<int> middle;
    for (int i = 0; i < 5; i++) middle.insert(town.street(i, 2, i + 1, 2));
    middle.erase(town.street(2, 2, 3, 2));
    const std::optional<TourPlan> plan = planTour(g, start, start, 2600, {.budgetMs = 150, .seed = 3, .avoid = [&middle](int w) { return middle.count(w >> 1) > 0; }});
    assert(plan);   // a tour all the same
    assert(std::none_of(plan->ways.begin(), plan->ways.end(), [&middle](int w) { return middle.count(w >> 1) > 0; }));   // none of the streets left out is driven
    // the streets of the east of the town wished for – or those of the west: the tour goes where it is wished
    const auto x1 = [&g](int w) { return static_cast<double>(g.x1[static_cast<size_t>(w)]); };
    const auto east = [&x1](int e) { return std::min(x1(e * 2), x1(e * 2 + 1)) >= 300; };
    const auto west = [&x1](int e) { return std::max(x1(e * 2), x1(e * 2 + 1)) <= 200; };
    const std::optional<TourPlan> liked = planTour(g, start, start, 2600, {.budgetMs = 150, .seed = 3, .like = [&east](int e) { return east(e) ? 2.5 : 0.3; }});
    const std::optional<TourPlan> plain = planTour(g, start, start, 2600, {.budgetMs = 150, .seed = 3, .like = [&west](int e) { return west(e) ? 2.5 : 0.3; }});
    assert(liked && plain);
    const auto share = [&](const TourPlan& p) {
      double inEast = 0;
      double all = 0;
      for (int w : p.ways) {
        all += len(g, w);
        if (east(w >> 1)) inEast += len(g, w);
      }
      return inEast / all;
    };
    assert(share(*liked) > share(*plain) + 0.4);
  }

  std::printf("game_tour_test: ok\n");
  return 0;
}
