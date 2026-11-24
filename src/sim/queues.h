// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/queues.ts @ v1.7.7
/**
 * Queues in the traffic: where cars stand or crawl one behind another – at a
 * red light, before roadworks, behind something that blocks the way – how
 * many they are, how long the queue is from the front of the first to the
 * back of the last, and, where it can be told cheaply, how long the last one
 * will wait: at a red light the time until green and a couple of seconds
 * for each car ahead to move off, at a bus stop the bus's time there.
 *
 * A queue is followed back from its head along the lanes and the paths
 * through the junctions behind it (the queue from a light may reach back
 * over the junction before).  The labels over the queues are src/ui/queuelabels.
 */
#pragma once

#include <unordered_set>
#include <vector>

#include "core/js.h"
#include "sim/insertion_map.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/lanes.h"

namespace fs {

/** slower than this (m/s) a car stands or crawls */
inline constexpr double QUEUE_SLOW = 2.5;
/** at most this between one car's back and the next one's front (m) */
inline constexpr double QUEUE_GAP = 12;
/** a queue has at least this many cars */
inline constexpr int QUEUE_MIN = 3;

struct Queue {
  /** the first car (at the front) and the last one (at its end: where its label goes) */
  TrafficCar* head = nullptr;
  TrafficCar* tail = nullptr;
  int cars = 0;
  /** from the front of the first car to the back of the last (m) */
  double length = 0;
  /** seconds the last car can expect to wait before it moves on (NaN: cannot be told) */
  double wait = JS_NAN;
};

class QueueMeter {
 public:
  /** The queues among `cars` (written into `out`, which is returned). */
  std::vector<Queue>& measure(const std::vector<TrafficCar*>& cars, SignalSystem* signals, std::vector<Queue>& out);
  std::vector<Queue> measure(const std::vector<TrafficCar*>& cars, SignalSystem* signals);

 private:
  /** the slow cars on each lane and each path through a junction (the original's Piece = Lane | Connector) */
  InsertionMap<const void*, std::vector<TrafficCar*>> slow;
  std::unordered_set<TrafficCar*> followers;
  std::unordered_set<TrafficCar*> used;
  struct Found {
    TrafficCar* car = nullptr;
    double gap = 0;
  } found;

  /** The slow car right behind `c` (within QUEUE_GAP of its back) – in `found` – or false. */
  bool behind(TrafficCar* c);
};

}  // namespace fs
