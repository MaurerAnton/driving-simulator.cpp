// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/queues.ts @ v1.7.7
#include "sim/queues.h"

#include <algorithm>
#include <cmath>

#include "sim/jsmath.h"

namespace fs {

namespace {

/** seconds for each car of a queue to move off once the first one goes */
const double PER_CAR = 1.9;

/** How long the last of `n` cars waits behind `head` (s; NaN: cannot be told). */
double waitOf(const TrafficCar& head, int n, const SignalSystem& signals) {
  // a bus at its stop: its time there
  if (head.bus && head.bus->dwell > 0) return head.bus->dwell + PER_CAR * (n - 1);
  const Lane* lane = head.lane;
  if (!lane) return JS_NAN;
  const Arm& arm = *lane->armTo;
  const int sc = lane->to->sc;
  if (arm.sg < 0 || sc < 0 || lane->stopAt - head.s - head.length / 2 > 15 || signals.isDark(sc)) return JS_NAN;
  const int st = signals.get(sc, arm.sg);
  const double move = PER_CAR * (n - 1) + 1;
  if (st == SIG_GREEN) return move;
  const std::optional<SignalChange> ch = signals.change(sc, arm.sg);
  if (!ch || !std::isfinite(ch->left)) return JS_NAN;
  // red: then red-amber (a second or so), then green
  if (st == SIG_RED) return ch->left + 1 + move;
  if (st == SIG_RED_AMBER) return ch->left + move;
  return JS_NAN;
}

}  // namespace

std::vector<Queue> QueueMeter::measure(const std::vector<TrafficCar*>& cars, SignalSystem* signals) {
  std::vector<Queue> out;
  measure(cars, signals, out);
  return out;
}

std::vector<Queue>& QueueMeter::measure(const std::vector<TrafficCar*>& cars, SignalSystem* signals, std::vector<Queue>& out) {
  out.clear();
  slow.clear();
  for (TrafficCar* c : cars) {
    if (c->v >= QUEUE_SLOW) continue;
    const void* piece = c->lane ? static_cast<const void*>(c->lane) : static_cast<const void*>(c->conn);
    if (!piece) continue;
    std::vector<TrafficCar*>* l = slow.get(piece);
    if (!l) l = &slow.set(piece, {});
    l->push_back(c);
  }
  for (auto& [piece, l] : slow)
    if (l.size() > 1) std::stable_sort(l.begin(), l.end(), [](const TrafficCar* a, const TrafficCar* b) { return b->s - a->s < 0; });
  // whoever has a slow car right in front of them is not the head of a queue
  followers.clear();
  for (auto& [piece, l] : slow)
    for (TrafficCar* c : l)
      if (behind(c)) followers.insert(found.car);
  used.clear();
  for (auto& [piece, l] : slow) {
    for (TrafficCar* head : l) {
      if (followers.count(head) || used.count(head)) continue;
      used.insert(head);
      int n = 1;
      double length = head->length;
      TrafficCar* tail = head;
      while (n < 400 && behind(tail)) {
        TrafficCar* next = found.car;
        if (used.count(next)) break;
        used.insert(next);
        length += found.gap + next->length;
        tail = next;
        n++;
      }
      if (n < QUEUE_MIN) continue;
      out.push_back(Queue{head, tail, n, length, signals ? waitOf(*head, n, *signals) : JS_NAN});
    }
  }
  return out;
}

bool QueueMeter::behind(TrafficCar* c) {
  const bool onLane = c->lane != nullptr;
  const void* piece = onLane ? static_cast<const void*>(c->lane) : static_cast<const void*>(c->conn);
  const double rear = c->s - c->length / 2;
  Found& f = found;
  f.car = nullptr;
  f.gap = JS_INF;
  // on the same lane or path
  const std::vector<TrafficCar*>* here = slow.get(piece);
  if (here) {
    const auto at = std::find(here->begin(), here->end(), c);
    // (indexOf gives −1 for one that is not in the list: then from the first)
    const size_t i = at == here->end() ? 0 : static_cast<size_t>(at - here->begin()) + 1;
    for (size_t k = i; k < here->size(); k++) {
      TrafficCar* o = (*here)[k];
      if (o->s >= c->s) continue;
      const double gap = rear - (o->s + o->length / 2);
      if (gap <= QUEUE_GAP) {
        f.car = o;
        f.gap = jsMax(0, gap);
        return true;
      }
      return false;
    }
  }
  if (rear > QUEUE_GAP) return false;
  // … else on what leads into it
  // (`slack`: a path through a junction between the two – nobody waits in the junction, so its length does not count)
  double eff = JS_INF;
  const auto take = [&](const std::vector<TrafficCar*>* list, double offset, const Connector* only, double slack) {
    if (!list) return;
    for (TrafficCar* o : *list) {
      if (only && o->next != only) continue;
      const double gap = offset - (o->s + o->length / 2);
      if (gap - slack < eff) {
        eff = gap - slack;
        f.gap = gap;
        f.car = o;
      }
      // (sorted: the first that leads there is the nearest)
      break;
    }
  };
  if (onLane) {
    for (const Connector* cin : c->lane->in) {
      take(slow.get(cin), rear + cin->len, nullptr, 0);
      take(slow.get(cin->from), rear + cin->len + cin->from->len, cin, cin->len);
    }
  } else {
    take(slow.get(c->conn->from), rear + c->conn->from->len, nullptr, 0);
  }
  if (!f.car || eff > QUEUE_GAP) {
    f.car = nullptr;
    return false;
  }
  f.gap = jsMax(0, f.gap);
  return true;
}

}  // namespace fs
