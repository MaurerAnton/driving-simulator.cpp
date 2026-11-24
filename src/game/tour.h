// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/tour.ts @ v1.7.7
/**
 * A tour of the streets: a way of about a given length, from a road back to
 * it or on to another one, that leads through as many different streets as
 * it can and along as few of them twice.
 *
 * Finding the best such way is hard (it is a cousin of the Chinese postman's
 * and the orienteering problem), so it is looked for: the tour is walked
 * street by street, at each junction turning into a street not driven yet –
 * the one from which the walk can best go on through new streets –, and when
 * there is none, driving to the nearest new street the remaining length
 * still allows.  It always keeps enough length to get to the end.  This is
 * done many times with different inclinations (and some chance), and the
 * tour with the most new street is kept.
 *
 * It works on the route graph (world/routegraph.h): no turning round, no
 * private roads, no yards and car parks, no walking-pace streets.  Motorways
 * are used to get somewhere, but are not what the tour is after.
 */
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "core/task.h"
#include "world/routegraph.h"

namespace fs {

struct TourPlan {
  /** the ways to drive along, one after the other, after the one the tour starts on */
  std::vector<int> ways;
  /** its length in metres, */
  double metres = 0;
  /** of which along streets for the first time, */
  double fresh = 0;
  /** and along streets driven before (on this tour, or known already) */
  double again = 0;
  /** the new metres weighed by how welcome each street was (the wishes, see TourOptions.like) */
  double valued = 0;
};

struct TourOptions {
  /** roads (by edge) that are known already: driving them is nothing new */
  std::function<bool(int edge)> known;
  /** how long to look for a better tour, in milliseconds */
  std::optional<double> budgetMs;
  /** for the chance in it: the same seed gives the same tour */
  std::optional<double> seed;
  /** ways the tour must not use (a motorway, a main road – as the driver wishes) */
  std::function<bool(int way)> avoid;
  /** how welcome a road (by edge) is as a new street of the tour: 1 as any, 0 not looked for, 2 twice as welcome */
  std::function<double(int edge)> like;
};

/** … and, planned a slice at a time: who is told how far it is, and how long a slice may take (ms; 30 unless said) */
struct TourSliceOptions : TourOptions {
  std::function<void(double share)> progress;
  std::optional<double> sliceMs;
};

/**
 * Plan a tour of about `metres` that starts at the end of way `start` and
 * ends with way `goal` (the same way: a round trip).  Null when the end
 * cannot be reached from the start without turning round.
 */
std::optional<TourPlan> planTour(const RouteGraph& g, int start, int goal, double metres, const TourOptions& opt = {});

/**
 * The same, a slice at a time: a long tour takes the phone a while to work
 * out, and the page stays alive meanwhile – `progress` is told how far it is
 * (0 … 1) between the slices.
 *
 * (`g` is the map's network: it must stay where it is until the task has finished.)
 */
Task<std::optional<TourPlan>> planTourInSlices(const RouteGraph& g, int start, int goal, double metres, TourSliceOptions opt = {});

}  // namespace fs
