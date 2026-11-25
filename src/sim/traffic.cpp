// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/traffic.ts @ v1.7.7
#include "sim/traffic.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "sim/jsmath.h"
#include "world/routegraph.h"

namespace fs {

namespace {

const int COLORS[] = {0xd9dadc, 0x1c1d20, 0x8d9197, 0x3a3f46, 0xa01c1c, 0x1f3f7a, 0xf0efe8, 0x55606a, 0x7a2f2a, 0x2c5a3a, 0xb8b08c, 0x394a66, 0xc4c7ca, 0x101113};
const int COLORS_LENGTH = static_cast<int>(sizeof(COLORS) / sizeof(COLORS[0]));

const PlayerState NOBODY = {.x = 1e9, .y = 1e9, .z = 1e9, .yaw = 0, .speed = 0, .length = 0, .width = 0};

/** list.some((o) => Math.abs(o.s - s) < d) */
bool someNear(const std::vector<TrafficCar*>* list, double s, double d) {
  if (!list) return false;
  for (const TrafficCar* o : *list)
    if (std::abs(o->s - s) < d) return true;
  return false;
}

/** map.get(key): the list, or null */
template <typename M, typename K>
auto* listOf(M& map, const K& key) {
  auto it = map.find(key);
  return it == map.end() ? nullptr : &it->second;
}

/** lane.out.find((q) => q.turn === TURN.STRAIGHT) ?? lane.out[0] */
Connector* straightOn(const Lane& lane) {
  for (Connector* q : lane.out)
    if (q->turn == TURN_STRAIGHT) return q;
  return lane.out.empty() ? nullptr : lane.out[0];
}

}  // namespace

int stopPlaceOf(int pole) { return static_cast<int>(std::floor(pole / 64.0)); }

Traffic::Traffic(World& world_, LaneGraph& graph_, SignalSystem& signals_) : world(world_), graph(graph_), signals(signals_), pastBlocks(*this), narrowings(std::make_unique<Narrowings>(*this)) {
  probe.id = 9007199254740991LL;  // Number.MAX_SAFE_INTEGER
  probe.length = 4.3;
  probe.width = 1.8;
  probe.temper = 1;
  box.length = 4.3;
  box.width = 1.8;
  crowd = std::make_unique<Crowd>(world, signals);
  cyclists = std::make_unique<Cyclists>(world, graph, signals);
  cyclists->lanesAt = [this](double x, double z) { return lanesAt(x, z); };
  addRoads(world.allEdges);
}

Traffic::~Traffic() {
  for (TrafficCar* car : cars) delete car;
}

const std::vector<Pedestrian*>& Traffic::peds() const {
  static const std::vector<Pedestrian*> none;
  return crowd ? crowd->people() : none;
}

void Traffic::addRoad(Edge& e) {
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[e.id];
  if (!el) return;
  for (const std::vector<Lane*>* side : {&el->fwd, &el->back}) {
    for (Lane* lane : *side) {
      for (const int64_t key : spawnCells(*lane)) {
        std::vector<Lane*>& list = spawnGrid[key];
        // main roads carry more traffic: list them more than once
        const int copies = lane->edge->tier >= 4 ? 4 : lane->edge->tier >= 3 ? 3 : 1;
        for (int k = 0; k < copies; k++) list.push_back(lane);
      }
    }
  }
}

void Traffic::removeRoadSet(const InsertionSet<Edge*>& away) {
  if (away.size() == 0) return;
  for (int i = static_cast<int>(cars.size()) - 1; i >= 0; i--) {
    TrafficCar* car = cars[static_cast<size_t>(i)];
    const bool on = car->lane ? away.has(car->lane->edge) : car->conn ? away.has(car->conn->from->edge) || away.has(car->conn->to->edge) : false;
    if (on) {
      detach(car);
      cars.erase(cars.begin() + i);
      gone.emplace_back(car);
    }
  }
  if (crowd) crowd->removeRoads(away);
  for (Edge* e : away) {
    const LaneGraph::EdgeLanes* el = graph.edgeLanes[e->id];
    if (!el) continue;
    for (const std::vector<Lane*>* side : {&el->fwd, &el->back}) {
      for (Lane* lane : *side) {
        for (const int64_t key : spawnCells(*lane)) {
          auto it = spawnGrid.find(key);
          if (it == spawnGrid.end()) continue;
          std::vector<Lane*>& list = it->second;
          list.erase(std::remove(list.begin(), list.end(), lane), list.end());
          if (list.empty()) spawnGrid.erase(it);
        }
      }
    }
  }
}

InsertionSet<int64_t> Traffic::spawnCells(const Lane& lane) {
  InsertionSet<int64_t> seen;
  if (!LaneGraph::drivable(*lane.edge, true) || lane.len < 25) return seen;
  if (lane.edge->flags & EF_ROUNDABOUT) return seen;
  for (double d = 0; d <= lane.len; d += 40) {
    tmp = lanePoint(lane, d, tmp);
    seen.add(cellKey(tmp.x, tmp.z));
  }
  return seen;
}

// --- bookkeeping -------------------------------------------------------------
void Traffic::attach(TrafficCar* car) {
  auto& map = car->conn ? onConn : onLane;
  const int key = car->conn ? car->conn->id : car->lane->id;
  map[key].push_back(car);
}

void Traffic::detach(TrafficCar* car) {
  auto& map = car->conn ? onConn : onLane;
  const int key = car->conn ? car->conn->id : car->lane->id;
  std::vector<TrafficCar*>* list = listOf(map, key);
  if (!list) return;
  auto it = std::find(list->begin(), list->end(), car);
  if (it != list->end()) list->erase(it);
}

Connector* Traffic::chooseNext(const Lane& lane) {
  const std::vector<Connector*>& outs = lane.out;
  if (outs.empty()) return nullptr;
  double total = 0;
  std::vector<double> w;
  for (const Connector* c : outs) {
    double weight = 0;
    if (LaneGraph::drivable(*c->to->edge, true) && !(roadEvents && roadEvents->closed(*c->to))) {
      weight = c->turn == TURN_STRAIGHT ? 4 : c->turn == TURN_UTURN ? 0.02 : 1.4;
      weight *= 0.5 + c->to->edge->tier * 0.5;
      if (c->to->edge->cls <= MOTORWAY_LINK && lane.edge->cls > MOTORWAY_LINK) weight *= 0.35;
    }
    w.push_back(weight);
    total += weight;
  }
  if (total <= 0) return outs[static_cast<size_t>(std::floor(rand() * static_cast<double>(outs.size())))];
  double r = rand() * total;
  for (size_t i = 0; i < outs.size(); i++) {
    r -= w[i];
    if (r <= 0) return outs[i];
  }
  return outs[outs.size() - 1];
}

int64_t Traffic::cellKey(double x, double z) {
  return static_cast<int64_t>((std::floor(x / 120) + 512) * 1024 + std::floor(z / 120) + 512);
}

const std::vector<Lane*>* Traffic::lanesAt(double x, double z) const { return listOf(spawnGrid, cellKey(x, z)); }

const std::vector<TrafficCar*>* Traffic::carsOn(const Lane& lane) const { return listOf(onLane, lane.id); }

const std::vector<TrafficCar*>* Traffic::carsOnConn(const Connector& c) const { return listOf(onConn, c.id); }

double Traffic::gapTo(TrafficCar& car, const PlayerState& box_, double horizon) { return playerAhead(car, box_, horizon); }

void Traffic::spawn(const PlayerState& player) {
  for (int attempt = 0; attempt < 10; attempt++) {
    // a random spot in the ring around the player
    const double ang = rand() * PI * 2;
    const double rad = 160 + rand() * (spawnRadius - 160);
    const std::vector<Lane*>* list = listOf(spawnGrid, cellKey(player.x + jsCos(ang) * rad, player.z + jsSin(ang) * rad));
    if (!list || list->empty()) continue;
    Lane* lane = (*list)[static_cast<size_t>(std::floor(rand() * static_cast<double>(list->size())))];
    const double s = 6 + rand() * jsMax(0, lane->len - 14);
    tmp = lanePoint(*lane, s, tmp);
    const double d = jsHypot(tmp.x - player.x, tmp.z - player.z);
    if (d < 150 || d > spawnRadius) continue;
    if (std::abs(tmp.y - player.y) > 60) continue;
    // (not on a road closed today, nor in or just before a work zone)
    if (roadEvents && roadEvents->blocked(*lane, s)) continue;
    // not right in front of the player's nose
    const double fx = -jsSin(player.yaw);
    const double fz = -jsCos(player.yaw);
    const double ahead_ = ((tmp.x - player.x) * fx + (tmp.z - player.z) * fz) / d;
    if (ahead_ > 0.82 && d < 260) continue;
    // free space on the lane
    const std::vector<TrafficCar*>* others = listOf(onLane, lane->id);
    if (someNear(others, s, 14)) continue;
    // now and then a city bus – on a street through the town, where stops are, or on the way of a line
    const bool lineHere = lines != nullptr && lines->has(wayOf(*lane));
    bool bus = (busStops != nullptr || lineHere) && rand() < busShare && (lineHere || (lane->edge->cls >= PRIMARY && lane->edge->cls <= RESIDENTIAL)) && lane->len > 30;
    if (bus) {
      for (const TrafficCar* o : cars) {
        if (o->bus && jsHypot(o->x - tmp.x, o->z - tmp.z) < 250) {
          bus = false;
          break;
        }
      }
    }
    bus = bus && !someNear(others, s, 22);
    Lane* at = lane;
    std::optional<BusOnLine> line;
    if (bus && lineHere) {
      // one of the lines that run here – on the lane of the road from which it can follow its way
      line = pickLine(*lane);
      if (line) {
        at = laneFor(*lane, *line);
        const std::vector<TrafficCar*>* there = listOf(onLane, at->id);
        if (at != lane && someNear(there, s, 22)) continue;
      }
    }
    // (where only lines of the timetables run, no bus by chance: a car)
    addCar(*at, s, bus && (line.has_value() || !lineHere || !busSchedule), line);
    return;
  }
}

TrafficCar* Traffic::addCar(Lane& lane, double s, bool bus) { return addCar(lane, s, bus, bus ? pickLine(lane) : std::nullopt); }

TrafficCar* Traffic::addCar(Lane& lane, double s, bool bus, const std::optional<BusOnLine>& line) {
  const std::optional<BusOnLine> on = bus ? line : std::nullopt;
  tmp = lanePoint(lane, s, tmp);
  const int style = bus ? BUS_STYLE : static_cast<int>(std::floor(rand() * 3));
  TrafficCar* car = new TrafficCar();
  car->id = nextId++;
  car->lane = &lane;
  car->conn = nullptr;
  car->s = s;
  car->v = jsMin(lane.limit, 13) * 0.8;
  car->a = 0;
  // (a bus on a line chooses below, once it exists: it may have to move over to another lane first)
  car->next = on ? nullptr : chooseNext(lane);
  car->length = bus ? 12 : style == 1 ? 4.55 : 4.25;
  car->width = bus ? 2.55 : 1.8;
  car->color = COLORS[static_cast<size_t>(std::floor(rand() * COLORS_LENGTH))];
  car->style = style;
  if (bus) {
    car->bus = TrafficCar::Bus{};
    car->bus->dwell = 0;
    car->bus->served = -1;
    car->bus->doors = false;
    car->bus->pullOut = 0;
    car->bus->line = on;
    car->bus->free = 0;
  }
  car->temper = 0.88 + rand() * 0.14;
  car->x = tmp.x;
  car->y = tmp.y;
  car->z = tmp.z;
  car->yaw = jsAtan2(-tmp.tx, -tmp.tz);
  car->pitch = 0;
  car->blinker = 0;
  car->braking = false;
  car->waited = 0;
  car->yieldWait = 0;
  car->stoppedAtLine = false;
  car->stuck = 0;
  car->blockedByPlayer = 0;
  car->playerFirstGap = JS_INF;
  car->playerSeen = 0;
  car->crashed = 0;
  car->wheelSpin = 0;
  if (on) car->next = nextFor(&*car->bus->line, lane, car);
  cars.push_back(car);
  attach(car);
  return car;
}

// --- the lines of the city buses ----------------------------------------------------

void Traffic::setBusLines(BusLines* lines_) {
  lines = lines_;
  for (TrafficCar* car : cars) {
    if (!car->bus) continue;
    car->bus->line = std::nullopt;
    car->bus->free = 0;
    car->merge = nullptr;
    car->detour = std::nullopt;
    if (!car->lane) continue;
    car->bus->line = pickLine(*car->lane);
    if (car->bus->line) car->next = nextFor(&*car->bus->line, *car->lane, car);
  }
}

BusOnLine Traffic::onLine(const BusLine& line, int k) { return BusOnLine{line.ref, line.to, line.index, k, 0}; }

std::optional<BusOnLine> Traffic::pickLine(const Lane& lane, const std::string& prefer) {
  if (!lines) return std::nullopt;
  // (not on the way round from the end of a route to the line back: there it shows where it came from)
  // (nor on a line of the timetables: its buses come by them – busSchedule)
  std::vector<BusLines::At> here;
  for (const BusLines::At& h : lines->at(wayOf(lane)))
    if (h.k < h.line->end && !(busSchedule && busSchedule->timed(h.line->index))) here.push_back(h);
  if (here.empty()) return std::nullopt;
  const BusLines::At* pick = nullptr;
  if (!prefer.empty()) {
    for (const BusLines::At& h : here) {
      if (h.line->ref == prefer) {
        pick = &h;
        break;
      }
    }
  }
  if (!pick) pick = &here[static_cast<size_t>(std::floor(rand() * static_cast<double>(here.size())))];
  return onLine(*pick->line, pick->k);
}

const std::vector<int>& Traffic::ahead(const BusOnLine& on, int n) {
  std::vector<int>& out = aheadWays;
  out.clear();
  if (!lines) return out;
  const BusLine* line = lines->lines[static_cast<size_t>(on.index)];
  size_t k = static_cast<size_t>(on.k + 1);
  int last = line->ways[static_cast<size_t>(on.k)];
  for (int hops = 0; static_cast<int>(out.size()) < n;) {
    if (k < line->ways.size()) {
      last = line->ways[k++];
      out.push_back(last);
      continue;
    }
    const BusLine* back = line->back;
    if (!back || ++hops > 2) break;
    line = back;
    // (the line back may begin on the very road this one ends on)
    k = !back->ways.empty() && back->ways[0] == last ? 1 : 0;
  }
  return out;
}

bool Traffic::reaches(const Lane& lane, const std::vector<int>& ws, size_t i, int depth) const {
  if (depth == 0 || i >= ws.size() || lane.out.empty()) return true;
  for (const Connector* c : lane.out)
    if (wayOf(*c->to) == ws[i] && reaches(*c->to, ws, i + 1, depth - 1)) return true;
  return false;
}

Lane* Traffic::laneFor(Lane& lane, const BusOnLine& on) {
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[lane.edge->id];
  const std::vector<Lane*>& list = lane.dir > 0 ? el->fwd : el->back;
  if (list.size() < 2) return &lane;
  const std::vector<int> ahead_ = ahead(on, 4);
  for (Lane* l : list)
    if (reaches(*l, ahead_, 0, 3)) return l;
  return list[0];
}

Connector* Traffic::nextFor(BusOnLine* on, Lane& lane, TrafficCar* car) {
  // (an emergency vehicle on its call takes its own way: src/sim/emergency)
  if (car && car->em) {
    std::vector<Connector*>& way_ = car->em->route;
    if (!way_.empty() && way_[0]->from == &lane) {
      Connector* first = way_[0];
      way_.erase(way_.begin());
      return first;
    }
  }
  if (!on || !lines || lane.out.empty()) return chooseNext(lane);
  if (on->off == 0) {
    const std::vector<int>& ahead_ = ahead(*on, 4);
    // (its line ends here, and none goes on from here)
    if (ahead_.empty()) return chooseNext(lane);
    Connector* best = nullptr;
    double bestScore = -JS_INF;
    for (Connector* c : lane.out) {
      if (wayOf(*c->to) != ahead_[0]) continue;
      const double score = (reaches(*c->to, ahead_, 1, 3) ? 100 : 0) - c->to->idx;
      if (score > bestScore) {
        bestScore = score;
        best = c;
      }
    }
    if (best) return best;
    Lane* other = car ? laneTowards(lane, ahead_) : nullptr;
    if (car && other) {
      car->merge = other;
      return nullptr;
    }
    on->off = 1;
  }
  // lost: along the shortest way back onto its line, where there is one
  if (car) {
    if (car->detour && !car->detour->empty() && (*car->detour)[0]->from != &lane) car->detour = std::nullopt;
    if (!car->detour) {
      std::optional<std::vector<Connector*>> back = wayBack(lane, *on);
      car->detour = back ? std::move(*back) : std::vector<Connector*>{};
    }
    if (!car->detour->empty()) {
      Connector* c = (*car->detour)[0];
      car->detour->erase(car->detour->begin());
      if (c) return c;
    }
  }
  return chooseNext(lane);
}

std::optional<std::vector<Connector*>> Traffic::wayBack(Lane& lane, const BusOnLine& on) {
  if (!lines) return std::nullopt;
  const BusLine* line = lines->lines[static_cast<size_t>(on.index)];
  std::unordered_set<int> goal;
  const int nWays = static_cast<int>(line->ways.size());
  for (int j = on.k + 1; j < std::min(nWays, on.k + 13); j++) goal.insert(line->ways[static_cast<size_t>(j)]);
  if (line->back && on.k >= nWays - 3)
    for (int j = 0; j < std::min(static_cast<int>(line->back->ways.size()), 6); j++) goal.insert(line->back->ways[static_cast<size_t>(j)]);
  goal.erase(wayOf(lane));
  if (goal.empty()) return std::nullopt;
  std::unordered_map<int, double> dist{{lane.id, 0.0}};
  std::unordered_map<int, Connector*> prev;
  MinHeap heap(64);
  heap.push(lane.id, 0);
  Lane* found = nullptr;
  const auto distOf = [&](int id) {
    auto it = dist.find(id);
    return it == dist.end() ? JS_INF : it->second;
  };
  for (int pops = 0; heap.size > 0 && pops < 6000; pops++) {
    const double d = heap.topKey();
    Lane* l = graph.lanes[heap.pop()];
    if (!l || d > distOf(l->id)) continue;
    if (l != &lane && goal.count(wayOf(*l))) {
      found = l;
      break;
    }
    for (Connector* c : l->out) {
      Lane* t = c->to;
      // (not through private roads – unless they are its line's own, a turning loop for buses)
      if (!LaneGraph::drivable(*t->edge, false) && !goal.count(wayOf(*t))) continue;
      const double nd = d + c->len + t->len + (c->turn == TURN_UTURN ? 80 : 0);
      if (nd > 2500 || nd >= distOf(t->id)) continue;
      dist[t->id] = nd;
      prev[t->id] = c;
      heap.push(t->id, nd);
    }
  }
  if (!found) return std::nullopt;
  std::vector<Connector*> path;
  for (Lane* l = found; l != &lane;) {
    Connector* c = prev[l->id];
    path.push_back(c);
    l = c->from;
  }
  std::reverse(path.begin(), path.end());
  return path;
}

Lane* Traffic::laneTowards(Lane& lane, const std::vector<int>& ahead_) {
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[lane.edge->id];
  const std::vector<Lane*>& list = lane.dir > 0 ? el->fwd : el->back;
  Lane* best = nullptr;
  double bestScore = -JS_INF;
  for (Lane* l : list) {
    if (l == &lane) continue;
    bool leads = false;
    for (const Connector* c : l->out) {
      if (!ahead_.empty() && wayOf(*c->to) == ahead_[0]) {
        leads = true;
        break;
      }
    }
    if (!leads) continue;
    const double score = (reaches(*l, ahead_, 0, 3) ? 100 : 0) - std::abs(l->idx - lane.idx);
    if (score > bestScore) {
      bestScore = score;
      best = l;
    }
  }
  return best;
}

void Traffic::moveOver(TrafficCar* car, bool listed) {
  Lane* to = car->merge;
  Lane* lane = car->lane;
  if (!to || !lane || to->edge != lane->edge || to->dir != lane->dir) {
    car->merge = nullptr;
    return;
  }
  if (car->s >= lane->len || !roomOn(*to, car->s, car->v, car->length)) {
    if (car->stuck > 20) {
      car->merge = nullptr;
      if (car->bus && car->bus->line) car->bus->line->off = 1;
      car->next = nextFor(car->bus && car->bus->line ? &*car->bus->line : nullptr, *lane, car);
    }
    return;
  }
  if (listed) detach(car);
  // (drawn where it was, it slides across: to the left of its new lane by the distance between the two)
  car->slide = car->slide + (lane->off - to->off) * lane->dir;
  car->lane = to;
  car->merge = nullptr;
  if (listed) attach(car);
  car->next = nextFor(car->bus && car->bus->line ? &*car->bus->line : nullptr, *to, car);
}

void Traffic::busOnLane(TrafficCar* car, Lane& lane) {
  if (!car->bus || !lines) return;
  TrafficCar::Bus& bus = *car->bus;
  const int w = wayOf(lane);
  if (!bus.line) {
    bus.free = bus.free + 1;
    if (bus.free > 2 && !bus.retired) {
      bus.line = pickLine(lane);
      if (bus.line) bus.served = -1;
    }
    return;
  }
  BusOnLine& on = *bus.line;
  const BusLine* line = lines->lines[static_cast<size_t>(on.index)];
  const int nWays = static_cast<int>(line->ways.size());
  // (a few ways ahead: where the bus went round something; further once it has lost its way)
  const int reach = on.off > 0 ? 60 : 6;
  for (int j = on.k + 1; j < std::min(nWays, on.k + 1 + reach); j++) {
    if (line->ways[static_cast<size_t>(j)] != w) continue;
    on.k = j;
    on.off = 0;
    car->detour = std::nullopt;
    return;
  }
  const BusLine* back = line->back;
  if (back && on.k >= nWays - 2) {
    for (int j = 0; j < std::min(static_cast<int>(back->ways.size()), 6); j++) {
      if (back->ways[static_cast<size_t>(j)] != w) continue;
      // the line back: its own number and destination, and its stops anew (a bus of the timetables has ended its trip: it goes)
      if (!bus.trip.empty() || bus.retired) {
        retireBus(car);
        return;
      }
      bus.line = onLine(*back, j);
      bus.served = -1;
      car->detour = std::nullopt;
      return;
    }
  }
  if (on.off == 0 && on.k >= nWays - 1) {
    // the end of its line, and none goes on from here: on without a line
    bus.line = std::nullopt;
    bus.free = 0;
    if (!bus.trip.empty()) retireBus(car);
    return;
  }
  // off its way: on along the way back if it has one (looked for anew now and then), else it gives up after a while
  on.off++;
  const bool hasDetour = car->detour && !car->detour->empty();
  if (on.off % 4 == 0 && !hasDetour) car->detour = std::nullopt;
  if (on.off > (car->detour && !car->detour->empty() ? 40 : 12)) {
    bus.line = std::nullopt;
    bus.free = 0;
    car->detour = std::nullopt;
    if (!bus.trip.empty()) retireBus(car);
  }
}

void Traffic::retireBus(TrafficCar* car) {
  if (!car->bus) return;
  car->bus->retired = true;
  car->bus->line = std::nullopt;
  car->detour = std::nullopt;
}

std::optional<Traffic::StopAhead> Traffic::stopAhead(TrafficCar* car, Lane* lane, Connector* conn) {
  if (!car->bus || car->bus->retired) return std::nullopt;
  const TrafficCar::Bus& bus = *car->bus;
  const BusOnLine* line = bus.line ? &*bus.line : nullptr;
  // (a bus on a line calls once at a stop place, at whichever of its poles)
  const auto skip = [&](int id) { return line ? stopPlaceOf(id) == stopPlaceOf(bus.served) : id == bus.served; };
  const double front = car->s + car->length / 2;
  if (lane) {
    for (const BusStopOnLane& st : stopsFor(*lane, line)) {
      if (skip(st.id)) continue;
      const double d = st.s - front;
      if (d < -1.5 || d > 70) continue;
      return StopAhead{d, st.id, true};
    }
  }
  const Connector* via = lane ? car->next : conn;
  if (!via) return std::nullopt;
  const double rest = lane ? lane->len - front + via->len : via->len - front;
  if (rest > 70) return std::nullopt;
  for (const BusStopOnLane& st : stopsFor(*via->to, line, line ? line->k + 1 : 0)) {
    if (skip(st.id)) continue;
    if (rest + st.s <= 70) return StopAhead{rest + st.s, st.id, false};
    return std::nullopt;
  }
  return std::nullopt;
}

std::vector<BusStopOnLane> Traffic::stopsFor(const Lane& lane, const BusOnLine* on) { return stopsFor(lane, on, on ? on->k : 0); }

std::vector<BusStopOnLane> Traffic::stopsFor(const Lane& lane, const BusOnLine* on, int k) {
  if (!on || !lines) return busStops ? busStops(lane) : std::vector<BusStopOnLane>{};
  std::vector<BusStopOnLane>& out = stopList;
  out.clear();
  if (on->off > 0) return out;
  const BusLine* line = lines->lines[static_cast<size_t>(on->index)];
  // (the build puts each stop on one road; its pole may stand just beyond the junction, on the road before or after)
  const std::vector<BusLineStop>& near = lines->stopsOn(*line, k - 1, k + 1, nearStops);
  if (near.empty()) return out;
  if (busStops) {
    for (const BusStopOnLane& p : busStops(lane)) {
      for (const BusLineStop& st : near) {
        if (st.place == stopPlaceOf(p.id)) {
          out.push_back(p);
          break;
        }
      }
    }
  }
  for (const BusLineStop& st : near) {
    if (st.k != k) continue;
    bool have = false;
    for (const BusStopOnLane& p : out) {
      if (stopPlaceOf(p.id) == st.place) {
        have = true;
        break;
      }
    }
    if (have) continue;
    // the front door at the stop: the front of the bus a metre and a half beyond it, and the whole bus past the junction
    const Edge& e = *lane.edge;
    const double along = ((lane.dir > 0 ? st.m : e.len - st.m) - lane.s0) * lane.dir + 1.5;
    out.push_back(BusStopOnLane{clamp(along, jsMin(12, lane.len - 0.5), lane.len - 0.5), st.place * 64 + 63});
  }
  std::stable_sort(out.begin(), out.end(), [](const BusStopOnLane& a, const BusStopOnLane& b) { return a.s < b.s; });
  return out;
}

void Traffic::removeCar(TrafficCar* car) { remove(car); }

void Traffic::remove(TrafficCar* car) {
  detach(car);
  auto it = std::find(cars.begin(), cars.end(), car);
  if (it != cars.end()) {
    cars.erase(it);
    gone.emplace_back(car);
  }
}

void Traffic::clear() {
  if (cyclists) cyclists->clear();
  if (wild) wild->clear();
  for (TrafficCar* car : cars) gone.emplace_back(car);
  cars.clear();
  if (busSchedule) busSchedule->reset();
  if (crowd) crowd->clear();
  onLane.clear();
  onConn.clear();
}

// --- for a vehicle that drives itself without being one of the traffic's cars ------

TrafficCar& Traffic::aim(SelfDriving& me) {
  TrafficCar& p = probe;
  p.lane = me.conn ? nullptr : me.lane;
  p.conn = me.conn;
  p.s = me.s;
  p.v = me.v;
  p.next = me.next;
  p.length = me.length;
  p.width = me.width;
  p.x = me.x;
  p.y = me.y;
  p.z = me.z;
  p.yaw = me.yaw;
  p.past = me.past;
  return p;
}

void Traffic::passBy(SelfDriving& me, double dt, double gap, bool held) {
  TrafficCar& p = aim(me);
  pastBlocks.look(p, dt, gap, held, nullptr, true);
  pastBlocks.slide(p, me.v * dt, dt);
  me.past = p.past;
}

Traffic::Ahead Traffic::aheadOf(SelfDriving& me, double horizon) {
  TrafficCar& p = aim(me);
  // (going round something: what is in the way there, by where it is)
  Ahead lead;
  if (p.past && p.past->phase != "wait") {
    const PastBlocks::Lead round = pastBlocks.lead(p, horizon);
    lead.gap = round.gap;
    lead.v = round.v;
  } else {
    const Leader l = leader(p, horizon);
    lead.gap = l.gap;
    lead.v = l.v;
  }
  lead.loose = false;
  for (const PlayerState* o : blocks) {
    if (std::abs(o->x - p.x) > horizon + 30 || std::abs(o->z - p.z) > horizon + 30) continue;
    const double gap = playerAhead(p, *o, horizon);
    if (gap >= lead.gap) continue;
    const double along = -jsSin(o->yaw) * -jsSin(p.yaw) + -jsCos(o->yaw) * -jsCos(p.yaw);
    lead.gap = gap;
    lead.v = jsMax(0, o->speed * along);
  }
  // Whoever stands or drives in the way without being "ahead on the lane": a car coming the
  // other way on the wrong side of a narrow street, one crossing, one standing half in the road.
  // (`loose`: found by where it is, not by the order of the cars on the lanes.)
  const double near = jsMin(horizon, 8 + p.v * 2.2);
  const std::vector<Connector*>* route = me.route;
  std::function<Connector*(Lane&)> onward = nullptr;
  if (route) {
    onward = [route](Lane& l) -> Connector* {
      for (Connector* c : *route)
        if (c->from == &l) return c;
      return nullptr;
    };
  }
  for (const TrafficCar* o : cars) {
    if (std::abs(o->x - p.x) > near + 8 || std::abs(o->z - p.z) > near + 8) continue;
    box.x = o->x;
    box.y = o->y;
    box.z = o->z;
    box.yaw = o->yaw;
    box.speed = o->v;
    box.length = o->length;
    box.width = o->width;
    const double gap = playerAhead(p, box, near, onward);
    if (gap >= lead.gap - 0.5) continue;
    const double along = -jsSin(o->yaw) * -jsSin(p.yaw) + -jsCos(o->yaw) * -jsCos(p.yaw);
    lead.gap = gap;
    lead.v = jsMax(0, o->v * along);
    lead.loose = true;
  }
  return lead;
}

void Traffic::walk(Lane* lane0, Connector* conn0, double s0, Connector* next0, const std::function<Connector*(Lane&)>& onward, double step, double reach,
                   const std::function<bool(double x, double z, double tx, double tz, double dist)>& visit) {
  Lane* lane = lane0;
  Connector* conn = conn0;
  Connector* next = next0;
  double at = s0;
  for (double dist = step; dist <= reach; dist += step) {
    at += step;
    if (conn) {
      if (at > conn->len) {
        at -= conn->len;
        lane = conn->to;
        conn = nullptr;
        next = nullptr;
      }
    } else if (lane && at > lane->len) {
      Connector* nx = next;
      if (!nx && onward) nx = onward(*lane);
      if (!nx) nx = straightOn(*lane);
      if (!nx) return;
      at -= lane->len;
      conn = nx;
      lane = nullptr;
    }
    if (conn) tmp2 = samplePolyline(conn->pl, at, tmp2);
    else if (lane) tmp2 = lanePoint(*lane, at, tmp2);
    else return;
    if (visit(tmp2.x, tmp2.z, tmp2.tx, tmp2.tz, dist)) return;
  }
}

double Traffic::crossingAhead(SelfDriving& me, double reach) {
  TrafficCar& p = aim(me);
  const std::vector<Connector*>* route = me.route;
  std::function<Connector*(Lane&)> onward = nullptr;
  if (route) {
    onward = [route](Lane& l) -> Connector* {
      for (Connector* c : *route)
        if (c->from == &l) return c;
      return nullptr;
    };
  }
  way.clear();
  walk(p.lane, p.conn, p.s, p.next, onward, 1.5, reach, [&](double x, double z, double tx, double tz, double dist) {
    way.push_back(x);
    way.push_back(z);
    way.push_back(tx);
    way.push_back(tz);
    way.push_back(dist);
    return false;
  });
  if (way.empty()) return JS_INF;
  const double vMe = jsMax(p.v, 2.5);
  double best = JS_INF;
  for (const TrafficCar* op : cars) {
    const TrafficCar& o = *op;
    if (o.v < 2.5 || o.a < -0.8) continue;
    const double dx = o.x - p.x;
    const double dz = o.z - p.z;
    if (dx * dx + dz * dz > 85 * 85) continue;
    walk(o.lane, o.conn, o.s, o.next, nullptr, 2, jsMin(75, o.v * 4.5), [&](double qx, double qz, double qtx, double qtz, double qd) {
      const double tq = qd / o.v;
      for (size_t i = 0; i < way.size(); i += 5) {
        const double d = way[i + 4];
        if (d >= best) break;
        const double ex = way[i] - qx;
        const double ez = way[i + 1] - qz;
        if (ex * ex + ez * ez > 2.3 * 2.3) continue;
        // going the same way (ahead of us, or beside us on its own lane) is no crossing
        if (way[i + 2] * qtx + way[i + 3] * qtz > 0.8) continue;
        if (std::abs(tq - d / vMe) > 2.8) continue;
        best = d;
        return true;
      }
      return false;
    });
  }
  return best;
}

bool Traffic::mayPass(SelfDriving& me, Connector& c, double distToLine, bool impatient) {
  TrafficCar& p = aim(me);
  if (boxBlocked(c)) return false;
  // a learner's gap: two seconds more than the traffic's own cars take
  return mayEnter(p, c, distToLine, NOBODY, impatient, 2);
}

bool Traffic::roomOn(const Lane& lane, double s, double v, double length) const {
  const std::vector<TrafficCar*>* list = listOf(onLane, lane.id);
  if (!list) return true;
  for (const TrafficCar* o : *list) {
    const double d = o->s - s;  // + = ahead of us
    if (d >= 0) {
      if (d < length + 4 + jsMax(0, v - o->v) * 1.5) return false;
    } else if (-d < length + 6 + jsMax(0, o->v - v) * 3.5) {
      return false;
    }
  }
  return true;
}

bool Traffic::roomBehind(const Lane& lane, double need) const {
  // (a lane shorter than the vehicle is no place to wait in anyway: it leads on)
  if (lane.len < need) return true;
  double free = lane.len;
  bool standing = false;
  if (const std::vector<TrafficCar*>* list = listOf(onLane, lane.id)) {
    for (const TrafficCar* o : *list) {
      if (o->v > 2.5) continue;
      standing = true;
      free = jsMin(free, o->s - o->length / 2);
    }
  }
  if (!standing) return true;
  for (const Connector* c : lane.in)
    if (const std::vector<TrafficCar*>* list = listOf(onConn, c->id))
      for (const TrafficCar* o : *list) free -= o->length + 2;
  return free >= need;
}

bool Traffic::clearOf(Connector& c) { return junctionEmpty(c); }

Pedestrian* Traffic::crossingAt(const CrossingJson& c) const {
  for (Pedestrian* p : peds())
    if (p->crossing == &c && p->state != "gone" && (p->state == "cross" || p->waited > 0.3)) return p;
  return nullptr;
}

// --- perception ----------------------------------------------------------------
Traffic::Leader Traffic::leader(TrafficCar& car, double horizon) {
  double best = JS_INF;
  double bestV = 0;
  const auto scanOne = [&](const TrafficCar* o, double offset) {
    if (o == &car) return;
    const double d = offset + o->s;
    if (d > 0 && d < best) {
      best = d - o->length / 2 - car.length / 2;
      bestV = o->v;
      if (best < 0) best = 0;
    }
  };
  const auto scan = [&](const std::vector<TrafficCar*>* list, double offset) {
    if (!list) return;
    for (const TrafficCar* o : *list) scanOne(o, offset);
  };
  double offset = -car.s;
  Lane* lane = car.lane;
  Connector* conn = car.conn;
  Connector* next = car.next;
  // cars that left our lane on another connector still share the first metres with us
  const auto siblings = [&](Connector& c, double off, double behindS) {
    for (const Sibling& sib : c.siblings) {
      const std::vector<TrafficCar*>* l2 = listOf(onConn, sib.other->id);
      if (l2)
        for (const TrafficCar* o : *l2)
          if (o != &car && o->s - o->length / 2 < sib.untilOther && o->s > behindS) scanOne(o, off);
    }
  };
  // Cars on other connectors into the same lane: whoever is nearer to the
  // end of its connector is "ahead"; side by side, the one that has to give
  // way (or, failing that, the higher id) drops back.
  // `off` = distance from our car to the start of connector c (negative once we are on it)
  const auto merging = [&](Connector& c, double off) {
    for (const Conflict& k : c.conflicts) {
      if (!k.merge) continue;
      Connector* other = k.other;
      const auto tie = [&](const TrafficCar* o) {
        bool backYield = false;
        for (const Conflict& q : other->conflicts) {
          if (q.other == &c) {
            backYield = q.yield;
            break;
          }
        }
        const bool iYield = k.yield || (!backYield && car.id > o->id);
        if (iYield && best > 0.3) {
          best = 0.3;
          bestV = jsMin(o->v, car.v) * 0.5;
        }
      };
      // already on the other connector: compare progress relative to where the paths meet
      const std::vector<TrafficCar*>* l2 = listOf(onConn, other->id);
      if (l2) {
        for (const TrafficCar* o : *l2) {
          const double d = off + k.sSelf + (o->s - k.sOther);
          if (d > 0.6) scanOne(o, off + k.sSelf - k.sOther);
          else if (d > -0.6) tie(o);
        }
      }
      // still on the neighbouring lane of the same carriageway (zip merging)
      if (other->from->armTo == c.from->armTo) {
        const std::vector<TrafficCar*>* l3 = listOf(onLane, other->from->id);
        if (l3) {
          for (const TrafficCar* o : *l3) {
            if (o->next != other) continue;
            const double ahead_ = off - (other->from->len - o->s);
            if (ahead_ > 0.8) {
              const double gap = ahead_ - o->length / 2 - car.length / 2;
              if (gap < best) {
                best = jsMax(0, gap);
                bestV = o->v;
              }
            } else if (ahead_ > -0.8) {
              tie(o);
            }
          }
        }
      }
    }
  };
  for (int hop = 0; hop < 6 && offset < horizon; hop++) {
    if (conn) {
      scan(listOf(onConn, conn->id), offset);
      siblings(*conn, offset, hop == 0 ? car.s : -1);
      merging(*conn, offset);
      offset += conn->len;
      lane = conn->to;
      conn = nullptr;
      next = nullptr;
    } else if (lane) {
      scan(listOf(onLane, lane->id), offset);
      offset += lane->len;
      conn = next ? next : (lane->out.size() == 1 ? lane->out[0] : nullptr);
      if (conn && hop == 0) merging(*conn, offset);
      if (!conn) {
        // route beyond this lane not chosen yet: anything just past its end may be in our way
        for (const Connector* c : lane->out) {
          const std::vector<TrafficCar*>* l2 = listOf(onConn, c->id);
          if (l2)
            for (const TrafficCar* o : *l2)
              if (o->s < 14) scanOne(o, offset);
        }
        break;
      }
      lane = nullptr;
    }
  }
  return Leader{best, bestV};
}

double Traffic::playerAhead(TrafficCar& car, const PlayerState& player, double horizon, const std::function<Connector*(Lane&)>& onward) {
  const double dx = player.x - car.x;
  const double dz = player.z - car.z;
  if (dx * dx + dz * dz > (horizon + 8) * (horizon + 8)) return JS_INF;
  if (std::abs(player.y - car.y) > 4) return JS_INF;
  const double c = jsCos(player.yaw);
  const double s = jsSin(player.yaw);
  const double hl = player.length / 2 + 0.3;
  const double hw = player.width / 2 + car.width / 2 + 0.25;
  double dist = 0;
  Lane* lane = car.lane;
  Connector* conn = car.conn;
  Connector* next = car.next;
  double at = car.s;
  const double step = 1.5;
  // (a car off the middle of its lane – making way, or passing those that do: its way runs beside the lane's)
  const double side0 = car.side;
  // (… and one going round something: its way runs across to where it goes)
  const PastState* past = car.past && car.past->phase != "wait" ? car.past.get() : nullptr;
  for (int n = 0; n < 60 && dist < horizon; n++) {
    at += step;
    dist += step;
    if (conn) {
      if (at > conn->len) {
        at -= conn->len;
        lane = conn->to;
        conn = nullptr;
        next = nullptr;
      }
    } else if (lane && at > lane->len) {
      // undecided beyond this lane: assume straight on (unless the way is known: `onward`)
      Connector* nx = next;
      if (!nx && onward) nx = onward(*lane);
      if (!nx) nx = straightOn(*lane);
      if (!nx) return JS_INF;
      at -= lane->len;
      conn = nx;
      lane = nullptr;
    }
    if (conn) {
      tmp2 = samplePolyline(conn->pl, at, tmp2);
    } else if (lane) {
      tmp2 = lanePoint(*lane, at, tmp2);
      // (where a side of the road is closed for roadworks, the way runs over the oncoming lane)
      if (roadEvents) roadEvents->offset(*lane, at, tmp2);
    } else {
      return JS_INF;
    }
    const double side = past ? side0 + offsetAt(*past, dist) : side0;
    if (side != 0) {
      tmp2.x += tmp2.tz * side;
      tmp2.z -= tmp2.tx * side;
    }
    // into the player's frame: forward = (-s, -c), right = (c, -s)
    const double px = tmp2.x - player.x;
    const double pz = tmp2.z - player.z;
    const double lf = -px * s - pz * c;
    const double lr = px * c - pz * s;
    // Passing the other's nose or tail crosswise takes half this car's width more than following
    // it does: `across` is how much the way runs across the other (0 = along it, 1 = at right angles).
    const double along = -s * tmp2.tx - c * tmp2.tz;
    const double across = std::sqrt(jsMax(0, 1 - along * along));
    if (std::abs(lf) < hl + across * (car.width / 2 + 0.2) && std::abs(lr) < hw) return jsMax(0, dist - car.length / 2);
  }
  return JS_INF;
}

bool Traffic::mayEnter(TrafficCar& car, Connector& c, double distToLine, const PlayerState& player, bool impatient, double caution) {
  // room behind the junction, so we never stop inside it
  const std::vector<TrafficCar*>* exit = listOf(onLane, c.to->id);
  if (exit)
    for (const TrafficCar* o : *exit)
      if (o->s < car.length + 3 && o->v < 1.5) return false;
  const double tEnter = car.v > 1 ? distToLine / car.v : std::sqrt((2 * jsMax(distToLine, 0.5)) / 1.5);
  for (const Conflict& k : c.conflicts) {
    // vehicles already inside the junction on a crossing path: never drive into them
    const std::vector<TrafficCar*>* inside = listOf(onConn, k.other->id);
    if (inside) {
      for (const TrafficCar* o : *inside) {
        if (k.merge || o->s - o->length / 2 < k.sOther + 2.5) return false;
      }
    }
    if (!k.yield) continue;
    // (`caution`: seconds of gap on top – for a driver who is to make nobody brake)
    const double myTime = tEnter + (k.sSelf + car.length) / jsMax(car.v, 3.5) + 1.2 + caution;
    // traffic approaching the conflicting movement – also from further upstream,
    // because the lane feeding it can be just a few metres long (roundabouts)
    const Lane* from = k.other->from;
    const bool zipper = from->armTo == c.from->armTo;
    const auto threat = [&](const TrafficCar* o, double d) {
      // lanes of one carriageway running together: only whoever is level or ahead goes first
      if (zipper) return d - k.sOther < distToLine + car.length + 1.5;
      const double t = d / jsMax(o->v, 2.5);
      // after a long wait accept a tighter (but still safe) gap
      return impatient ? t < 2.6 + caution && d < 60 + caution * 12 : t < myTime + 1.5 && d < 110 + caution * 20;
    };
    const std::vector<TrafficCar*>* approaching = listOf(onLane, from->id);
    if (approaching) {
      for (const TrafficCar* o : *approaching) {
        if (o->next && o->next != k.other) continue;
        if (threat(o, from->len - o->s + k.sOther)) return false;
      }
    }
    if (from->len < 70) {
      for (const Connector* cin : from->in) {
        const std::vector<TrafficCar*>* l1 = listOf(onConn, cin->id);
        if (l1)
          for (const TrafficCar* o : *l1)
            if (threat(o, cin->len - o->s + from->len + k.sOther)) return false;
        const Lane* up = cin->from;
        if (cin->len + from->len > 70) continue;
        const std::vector<TrafficCar*>* l2 = listOf(onLane, up->id);
        if (l2)
          for (const TrafficCar* o : *l2)
            if (o->next == cin && threat(o, up->len - o->s + cin->len + from->len + k.sOther)) return false;
      }
    }
    // the player on that lane or in the junction
    const std::optional<double> pd = playerOnPath(player, *k.other);
    if (pd) {
      const double d = *pd;
      if (d < 0) continue;  // already past the conflict point
      const double t = d / jsMax(player.speed, 2);
      if (player.speed > 0.8 && t < myTime + 2 && d < 120) return false;
      if (d < 8) return false;
    }
  }
  return true;
}

std::optional<double> Traffic::playerOnPath(const PlayerState& player, Connector& c) {
  const Lane& from = *c.from;
  const Edge& e = *from.edge;
  const double pad = 8;
  if (player.x < e.minX - pad || player.x > e.maxX + pad || player.z < e.minZ - pad || player.z > e.maxZ + pad) return std::nullopt;
  if (std::abs(player.y - c.node->y) > 8) return std::nullopt;
  // on the connector?
  double best = JS_INF;
  double bestS = 0;
  const size_t n = c.pl.cum.size();
  for (size_t i = 0; i < n; i++) {
    const double d = jsHypot(c.pl.pts[i * 3] - player.x, c.pl.pts[i * 3 + 2] - player.z);
    if (d < best) {
      best = d;
      bestS = c.pl.cum[i];
    }
  }
  if (best < 2.2) return c.len * 0.5 - bestS;
  // on the feeding lane, heading for the junction?
  const double steps = jsMin(40, std::ceil(from.len / 3));
  for (int i = 0; i <= steps; i++) {
    const double d = from.len - i * 3;
    if (d < 0) break;
    tmp2 = lanePoint(from, d, tmp2);
    if (jsHypot(tmp2.x - player.x, tmp2.z - player.z) < 2.4) {
      const double heading = -jsSin(player.yaw) * tmp2.tx - jsCos(player.yaw) * tmp2.tz;
      if (heading < 0.3) return std::nullopt;  // going the other way
      return i * 3 + c.len * 0.5;
    }
  }
  return std::nullopt;
}

// --- main update -----------------------------------------------------------------
void Traffic::update(double dt, const PlayerState& player) {
  events.clear();
  // (the cars taken out in the step before: nothing points at them any more)
  gone.clear();
  if (narrowings) narrowings->player = &player;
  // the level crossings: what the trains near them make them show (src/sim/levelcrossing)
  if (levelCrossings) {
    levelCrossings->player = &player;
    levelCrossings->update(dt, player.x, player.z);
  }
  // the roadworks and closures of the day: who is in or before each zone – and their fences, barriers and crowds are in the way, too
  if (roadEvents) roadEvents->step(dt, cars, player);
  const std::vector<PlayerState*>& obstacles_ = roadEvents ? roadEvents->withBlocks(obstacles, player) : obstacles;
  // what is in the cars' way besides each other: what the game names, and the people on bicycles
  blocks.clear();
  for (PlayerState* o : obstacles_) blocks.push_back(o);
  if (cyclists) {
    size_t n = 0;
    for (const Cyclist* c : cyclists->list) {
      if (n >= cycleBoxes.size()) cycleBoxes.push_back(PlayerState{.x = 0, .y = 0, .z = 0, .yaw = 0, .speed = 0, .length = CYCLE_LENGTH, .width = CYCLE_WIDTH});
      PlayerState& b = cycleBoxes[n++];
      b.x = c->x;
      b.y = c->y;
      b.z = c->z;
      b.yaw = c->yaw;
      b.speed = c->v;
      blocks.push_back(&b);
    }
  }
  // … and the people on foot who are on the carriageway
  if (crowd) crowd->addBlocks(blocks);
  // … and the animals crossing the road
  if (wild) {
    wild->update(dt, player);
    for (PlayerState* b : wild->boxes) blocks.push_back(b);
  }
  // … and the cars that go round something, off their lanes (src/sim/pastblock)
  pastBlocks.boxes(cars, blocks);
  spawnTimer -= dt;
  if (spawnTimer <= 0 && static_cast<int>(cars.size()) < targetCount) {
    spawn(player);
    spawnTimer = 0.12;
  }
  // the buses of the timetables: where and when their trips are (src/game/timedtransit)
  if (busSchedule) busSchedule->step(dt, player);
  // emergency vehicles: their calls, and how the others make way for them (src/sim/emergency)
  if (emergency) emergency->update(dt, player);
  for (int i = static_cast<int>(cars.size()) - 1; i >= 0; i--) {
    TrafficCar* car = cars[static_cast<size_t>(i)];
    const double d = jsHypot(car->x - player.x, car->z - player.z);
    if (d > spawnRadius + 110 || car->stuck > 45 || (car->crashed > 0 && (car->crashed -= dt) <= 0 && d > 60) ||
        (car->bus && car->bus->retired && car->bus->dwell <= 0 && d > 120)) {
      remove(car);
      continue;
    }
    drive(car, dt, player);
  }
  // the people on foot: along the pavements, over the streets (src/sim/pedestrians)
  static const std::vector<Cyclist*> noCyclists;
  if (crowd) crowd->update(dt, player, cars, cyclists ? cyclists->list : noCyclists, obstacles_, events);
  // people on bicycles: a few where there is traffic at all
  if (cyclists) {
    cyclists->target = targetCount > 0 ? static_cast<int>(jsMax(1, jsMin(4, jsRound(targetCount / 12.0)))) : 0;
    cyclists->update(dt, player, obstacles_, cars, events);
  }
}

void Traffic::drive(TrafficCar* carp, double dt, const PlayerState& player) {
  TrafficCar& car = *carp;
  // over to another lane of its road, where there is room (a bus whose line turns where its lane does not lead)
  if (car.merge && !(car.bus && car.bus->dwell > 0)) moveOver(carp);
  Lane* lane = car.lane;
  Connector* conn = car.conn;
  // a bus at its stop: the doors open, people get off and on – then the indicator, and off it goes
  TrafficCar::Bus* bus = car.bus ? &*car.bus : nullptr;
  if (bus && bus->dwell > 0) {
    bus->dwell -= dt;
    car.v = 0;
    car.a = 0;
    car.braking = true;
    car.blinker = 0;
    if (bus->dwell <= 0) {
      bus->doors = false;
      bus->pullOut = 3.5;
    }
    return;
  }
  if (bus && bus->pullOut > 0) bus->pullOut -= dt;
  // roadworks and closures (src/sim/roadevents): a site light, the oncoming traffic at a narrowing, a lane or a road closed ahead
  const Gate* works = roadEvents ? roadEvents->gateCar(car) : nullptr;
  if (works && works->move && !car.merge) car.merge = works->move;
  if (works && works->reroute && lane) car.next = chooseNext(*lane);
  // --- desired speed ---
  double limit = lane ? lane->limit : conn->to->limit;
  if (!std::isfinite(limit)) limit = 36;  // Richtgeschwindigkeit 130 km/h
  double v0 = limit * car.temper;
  if (works && works->cap < v0) v0 = works->cap;
  // a kerb build-out narrowing the road: slowly through the gap, and the oncoming traffic first where it is on our side (src/sim/narrowings)
  std::optional<NarrowGate> narrow;
  if (lane && narrowings) narrow = narrowings->gate(*lane, car.s + car.length / 2, car.v, car.length, &car);
  const double narrowStop = narrow ? narrow->stop : JS_INF;
  if (narrow && narrow->cap < v0) v0 = narrow->cap;
  // a level crossing ahead: its lights, its barriers, a train – and room behind the tracks (src/sim/levelcrossing)
  std::optional<LxGate> lx;
  if (lane && levelCrossings) lx = levelCrossings->gate(*lane, car.s + car.length / 2, car.v, car.length, car.next, &car);
  if (lx && lx->cap < v0) v0 = lx->cap;
  if (lane && lane->edge->zone == 7) v0 = jsMin(v0, 2.2);
  if ((lane && lane->edge->flags & EF_ROUNDABOUT) || (conn && conn->node->ctrl == CTRL_ROUNDABOUT)) v0 = jsMin(v0, 7.2);
  if (conn) {
    // junction speed from the tightness of the turn
    const double turnV = conn->turn == TURN_STRAIGHT ? 14 : conn->turn == TURN_UTURN ? 2.5 : clamp(std::sqrt(1.9 * jsMax(conn->len, 4) * 0.7), 3, 9);
    v0 = jsMin(v0, turnV);
  }
  // an emergency vehicle on its call goes faster; whoever makes way for one slows down (src/sim/emergency)
  if (emergency) v0 = emergency->desired(car, v0, limit);
  // going round something that blocks its lane: slowly (src/sim/pastblock)
  const PastState* past = car.past && car.past->phase != "wait" ? car.past.get() : nullptr;
  if (past) v0 = jsMin(v0, past->cap);
  const double A = 1.7;
  const double B = 2.4;
  double acc = A * (1 - std::pow(car.v / jsMax(v0, 0.5), 4));

  const auto brakeFor = [&](double gap, double vLead, double s0) {
    const double dv = car.v - vLead;
    const double sStar = s0 + jsMax(0, car.v * 1.25 + (car.v * dv) / (2 * std::sqrt(A * B)));
    const double g = jsMax(gap, 0.1);
    acc = jsMin(acc, A * (1 - std::pow(car.v / jsMax(v0, 0.5), 4) - std::pow(sStar / g, 2)));
  };

  // --- car ahead ---
  // (an emergency vehicle on its call passes those that make way for it: it sees past them)
  // (one going round something sees what is in its way there by where it is)
  Leader lead;
  if (past) {
    const PastBlocks::Lead l = pastBlocks.lead(car, 90);
    lead = Leader{l.gap, l.v};
  } else if (car.em && car.em->call && emergency) {
    const Emergencies::Lead l = emergency->lead(car);
    lead = Leader{l.gap, l.v};
  } else {
    lead = leader(car, 90);
  }
  if (lead.gap < JS_INF) brakeFor(lead.gap, lead.v, 2.3);

  // --- the player's car ---
  const double pGap = playerAhead(car, player, clamp(car.v * 3.5 + 12, 14, 80));
  bool playerBrake = false;
  /** how hard the player's car alone makes this one brake (a light or a car ahead may ask for more) */
  double playerAcc = 0;
  if (pGap < JS_INF) {
    const double before = acc;
    // the component of the player's speed along our direction
    const double along = -jsSin(player.yaw) * -jsSin(car.yaw) + -jsCos(player.yaw) * -jsCos(car.yaw);
    brakeFor(pGap, jsMax(0, player.speed * along), 2.6);
    playerBrake = acc < before - 0.3;
    playerAcc = acc;
    // how far away was it when it first got in the way?
    if (car.playerSeen <= 0) car.playerFirstGap = pGap / jsMax(car.v, 1);
    car.playerSeen = 1.5;
  } else if (car.playerSeen > 0) {
    car.playerSeen -= dt;
    if (car.playerSeen <= 0) car.playerFirstGap = JS_INF;
  }

  // --- whatever else is on the road: trams and trains crossing or running in the street ---
  /** the nearest thing ahead that is not a car of the traffic */
  double thingGap = pGap;
  {
    const PlayerState* own = car.past ? &car.past->box : nullptr;
    for (const PlayerState* o : blocks) {
      if (std::abs(o->x - car.x) > 110 || std::abs(o->z - car.z) > 110 || o == own) continue;
      const double gap = playerAhead(car, *o, clamp(car.v * 3.5 + 12, 14, 80));
      if (gap == JS_INF) continue;
      thingGap = jsMin(thingGap, gap);
      const double along = -jsSin(o->yaw) * -jsSin(car.yaw) + -jsCos(o->yaw) * -jsCos(car.yaw);
      brakeFor(gap, jsMax(0, o->speed * along), 2.6);
    }
  }

  // --- junction ahead ---
  double stopDist = JS_INF;
  if (lane) {
    Connector* next = car.next;
    const double distToLine = lane->stopAt - car.s - car.length / 2;
    const Node& node = *lane->to;
    const Arm& arm = *lane->armTo;
    // slow down for the turn before reaching it
    if (next && next->turn != TURN_STRAIGHT) {
      const double turnV = next->turn == TURN_UTURN ? 2.5 : clamp(std::sqrt(1.9 * jsMax(next->len, 4) * 0.7), 3, 9);
      const double d = jsMax(0, lane->len - car.s - car.length / 2);
      const double need = (car.v * car.v - turnV * turnV) / (2 * jsMax(d, 1));
      if (need > 0.6) acc = jsMin(acc, -need);
      car.blinker = d < 45 ? (next->turn == TURN_RIGHT ? 1 : -1) : 0;
    } else if (next && node.ctrl == CTRL_ROUNDABOUT && lane->edge->flags & EF_ROUNDABOUT && !(next->to->edge->flags & EF_ROUNDABOUT)) {
      car.blinker = 1;  // leaving the roundabout
    } else {
      car.blinker = 0;
    }

    bool yieldingNow = false;
    if (next && (node.arms.size() > 2 || next->conflicts.size() > 0) && distToLine < 70) {
      bool mustStop = false;
      if (arm.sg >= 0 && node.sc >= 0) {
        const int st = signals.get(node.sc, arm.sg);
        // the green arrow beside the light: whoever turns right may go while it is lit
        const int arrow = next->turn == TURN_RIGHT ? signals.arrow(&arm) : -1;
        if (arrow == SIG_GREEN) {
          mustStop = false;
        } else if (st == SIG_AMBER || arrow == SIG_AMBER) {
          // stop if that is comfortably possible
          const double need = (car.v * car.v) / (2 * jsMax(distToLine, 0.5));
          mustStop = distToLine > 0 && need < 3.2;
        } else if (st == SIG_RED || st == SIG_RED_AMBER) {
          mustStop = true;
        }
      }
      if (!mustStop && distToLine > -1.5) {
        const int role = arm.role;
        const bool needsStopSign = role == ROLE_STOP && !car.stoppedAtLine;
        bool anyYield = false;
        for (const Conflict& k : next->conflicts) {
          if (k.yield) {
            anyYield = true;
            break;
          }
        }
        const bool yielding = anyYield || role == ROLE_MINOR || role == ROLE_YIELD || role == ROLE_STOP;
        if (needsStopSign) {
          mustStop = true;
        } else if (yielding || node.ctrl == CTRL_RBL) {
          // only decide close to the line; approach at a speed that still allows stopping
          if (distToLine < 28) {
            const bool ok = mayEnter(car, *next, jsMax(distToLine, 0), player);
            if (!ok) {
              mustStop = true;
              yieldingNow = true;
              // stalemate (everybody waits for somebody): after a while creep through
              const double patience = node.ctrl == CTRL_RBL ? 6 + static_cast<double>(car.id % 5) : 10 + static_cast<double>(car.id % 7);
              if (car.yieldWait > patience && junctionEmpty(*next) && mayEnter(car, *next, jsMax(distToLine, 0), player, true)) mustStop = false;
            }
          } else if (role != ROLE_PRIORITY) {
            const double vApproach = 7;
            if (car.v > vApproach) acc = jsMin(acc, -1.4);
          }
        }
      }
      // whoever has the right of way: never drive into a car that is crossing our path
      if (!mustStop && distToLine > -1.5 && distToLine < 32 && boxBlocked(*next)) mustStop = true;
      // whoever turns right lets the cyclists beside and behind ride on first (§ 9 Abs. 3 StVO)
      if (!mustStop && next->turn == TURN_RIGHT && distToLine > -1.5 && distToLine < 20 && cyclists && cyclists->blocksRightTurn(*lane, car.s, car.length / 2)) mustStop = true;
      // … and lets the people on foot who cross the road it turns into go first (§ 9 Abs. 3 StVO)
      if (!mustStop && (next->turn == TURN_RIGHT || next->turn == TURN_LEFT) && distToLine > -1.5 && distToLine < 25 && peopleCrossing(*next->to, player)) mustStop = true;
      // an emergency vehicle on its call: past red lights and the others' right of way – slowly, and only once the way is clear
      if (car.em && car.em->call && emergency) {
        const double care = emergency->through(car, *next, distToLine, mustStop);
        mustStop = care < 0;
        if (care >= 0 && car.v > care) acc = jsMin(acc, (care - car.v) / 0.5);
      }
      if (mustStop && distToLine > -1.5) stopDist = jsMax(0, distToLine);
    }
    if (yieldingNow && car.v < 0.5) car.yieldWait += dt;
    else if (!yieldingNow) car.yieldWait = 0;
    // signalised or zebra crossings on this lane
    for (const CrossingJson& x : lane->edge->xings) {
      const double pos = lane->dir > 0 ? x.s - lane->s0 : lane->s0 - x.s;
      const double d = pos - x.w / 2 - 1.2 - car.s - car.length / 2;
      if (d < -2 || d > 60) continue;
      bool stop = false;
      if (x.k == XING_SIGNAL && x.sc) {
        const int st = signals.get(*x.sc, x.g.value_or(0));
        stop = st == SIG_RED || st == SIG_RED_AMBER || (st == SIG_AMBER && (car.v * car.v) / (2 * jsMax(d, 0.5)) < 3.2 && d > 0);
      } else if (x.k == XING_ZEBRA) {
        for (const Pedestrian* p : peds())
          if (p->crossing == &x && p->state != "gone" && (p->state == "cross" || p->waited > 0.3)) stop = true;
        // the player on foot: on the zebra, or at its kerb and about to step out (§ 26 Abs. 1 StVO)
        if (!stop && player.onFoot && atZebra(x, player)) stop = true;
      }
      if (stop) stopDist = jsMin(stopDist, jsMax(0, d));
    }
    // not yet over on the lane it needs: it waits before the stop line for room there
    if (car.merge) stopDist = jsMin(stopDist, jsMax(0, lane->stopAt - car.s - car.length / 2 - 0.5));
  }
  if (conn) {
    // turning: wait in the junction while somebody crosses the road it turns into
    if ((conn->turn == TURN_RIGHT || conn->turn == TURN_LEFT) && peopleCrossing(*conn->to, player)) stopDist = jsMin(stopDist, jsMax(0, conn->len - car.s - car.length / 2 - 1.8));
    for (const Conflict& k : conn->conflicts) {
      const double dSelf = k.sSelf - car.s - car.length / 2;
      if (dSelf < -1) continue;  // already past the conflict point
      const std::vector<TrafficCar*>* inside = listOf(onConn, k.other->id);
      if (!inside) continue;
      for (const TrafficCar* o : *inside) {
        const double dOther = k.sOther - o->s;
        if (dOther < -(o->length / 2 + 1.5)) continue;  // it has cleared the point
        bool wait;
        if (k.yield) {
          wait = true;
        } else {
          const Conflict* back = nullptr;
          for (const Conflict& q : k.other->conflicts) {
            if (q.other == conn) {
              back = &q;
              break;
            }
          }
          if (back && back->yield) {
            wait = false;
          } else {
            const double tSelf = jsMax(dSelf, 0) / (car.v + 0.5);
            const double tOther = jsMax(dOther - o->length / 2, 0) / (o->v + 0.5);
            wait = tSelf > tOther + 0.3 || (std::abs(tSelf - tOther) <= 0.3 && car.id > o->id);
          }
        }
        if (wait) stopDist = jsMin(stopDist, jsMax(0, dSelf - (k.merge ? 1.0 : 3.0)));
      }
    }
  }
  // a bus: the next stop on its way – its front comes to stand at the pole (on a line: the next of the line's stops)
  if (bus && (busStops || bus->line)) {
    const std::optional<StopAhead> st = stopAhead(carp, lane, conn);
    // (its front at the pole – also where its middle is still in the junction before a short road)
    if (st && st->d < 1.2 && car.v < 0.4) {
      bus->served = st->id;
      bus->doors = true;
      bus->dwell = 10 + rand() * 10;
      // (a bus of the timetables: until its time – no longer than the doors take when it is late)
      if (!bus->trip.empty() && busSchedule) bus->dwell = busSchedule->dwell(car, st->id, bus->dwell);
    } else if (st) {
      stopDist = jsMin(stopDist, jsMax(0, st->d));
      // (pulling in: the indicator to the right – once it is on the road of the stop, not while it turns into it)
      if (st->here && st->d < 45) car.blinker = 1;
    }
    if (bus->pullOut > 0) car.blinker = -1;
  }
  // making way for an emergency vehicle: stopping at the side, or before the junction it crosses (src/sim/emergency)
  if (emergency) stopDist = jsMin(stopDist, emergency->stopFor(car));
  if (works && works->stop < stopDist) {
    stopDist = jsMax(0, works->stop);
    // (waiting at a site light or for the oncoming traffic is not being stuck: a red there may last a minute or two)
    if (works->why == "light" || works->why == "oncoming") car.stuck = 0;
  }
  if (narrowStop < stopDist) {
    stopDist = narrowStop;
    car.stuck = 0;
  }
  if (lx && lx->stop < stopDist) {
    stopDist = lx->stop;
    car.stuck = 0;
  }
  if (stopDist < JS_INF) {
    brakeFor(stopDist + 0.4, 0, 0.6);
    if (stopDist < 1.2 && car.v < 0.4) {
      car.waited += dt;
      if (car.waited > 0.9) car.stoppedAtLine = true;
    }
  } else if (car.v > 1) {
    car.waited = 0;
  }

  // standing behind something that does not move: after a while, a way round it (src/sim/pastblock)
  if (!(car.bus && car.bus->dwell != 0 && car.bus->dwell == car.bus->dwell))
    pastBlocks.look(car, dt, jsMin(lead.gap, thingGap), stopDist < jsMin(lead.gap, thingGap) + 12 || works != nullptr || car.crashed > 0, &player);
  if (car.crashed > 0) acc = -6;
  acc = clamp(acc, -7.5, A);
  // report hard braking caused by the player (the rule monitor decides who was at fault) –
  // not the braking for a red light of a car that happens to follow the player
  if (playerBrake && playerAcc < -2.6 && car.v > 2) {
    car.blockedByPlayer += dt;
    if (car.blockedByPlayer > 0.12) {
      TrafficEvent ev;
      ev.type = "forcedBrake";
      ev.car = carp;
      ev.decel = -acc;
      ev.cutIn = car.playerFirstGap < 2.2;
      events.push_back(ev);
      car.blockedByPlayer = -3;  // one report per incident
    }
  } else if (car.blockedByPlayer > 0) {
    car.blockedByPlayer = 0;
  } else if (car.blockedByPlayer < 0) {
    car.blockedByPlayer = jsMin(0, car.blockedByPlayer + dt);
  }

  car.a = acc;
  car.braking = acc < -0.5 || (car.v < 0.2 && stopDist < 3);
  car.v = jsMax(0, car.v + acc * dt);
  if (car.v < 0.05 && acc <= 0) car.v = 0;
  car.stuck = car.v < 0.1 ? car.stuck + dt : 0;
  const double ds = car.v * dt;
  car.wheelSpin += ds / 0.31;
  if (car.past) pastBlocks.slide(car, ds, dt);

  // --- advance along the path ---
  detach(carp);
  car.s += ds;
  for (int guard = 0; guard < 4; guard++) {
    if (car.conn) {
      if (car.s < car.conn->len) break;
      car.s -= car.conn->len;
      car.lane = car.conn->to;
      car.lastConn = car.conn;
      car.conn = nullptr;
      if (car.bus) busOnLane(carp, *car.lane);
      car.next = nextFor(car.bus && car.bus->line ? &*car.bus->line : nullptr, *car.lane, carp);
      // (over at once where it can: a lane between two junctions may be a metre long)
      if (car.merge) moveOver(carp, false);
      car.stoppedAtLine = false;
      car.waited = 0;
      car.yieldWait = 0;
    } else if (car.lane) {
      if (car.s < car.lane->len) break;
      if (car.merge) {
        // (too late to move over: on where its lane leads – a bus loses its line's way, and looks for the way back)
        car.merge = nullptr;
        if (car.bus && car.bus->line) car.bus->line->off = 1;
      }
      Connector* next = car.next ? car.next : nextFor(car.bus && car.bus->line ? &*car.bus->line : nullptr, *car.lane, carp);
      if (!next && car.merge) {
        // (no moving over at the very end of the lane, either)
        car.merge = nullptr;
        if (car.bus && car.bus->line) car.bus->line->off = 1;
        next = nextFor(car.bus && car.bus->line ? &*car.bus->line : nullptr, *car.lane, carp);
      }
      if (!next) {
        // dead end of the map: leave the simulation
        car.s = car.lane->len;
        car.stuck = 100;
        break;
      }
      car.s -= car.lane->len;
      car.conn = next;
      car.lane = nullptr;
    }
  }
  attach(carp);

  // --- pose ---
  if (car.conn) {
    tmp = samplePolyline(car.conn->pl, car.s, tmp);
  } else {
    tmp = lanePoint(*car.lane, car.s, tmp);
    tmp.y = world.edgeY(*car.lane->edge, car.lane->s0 + car.lane->dir * car.s, tmp.x, tmp.z);
  }
  const double tangent = jsAtan2(-tmp.tx, -tmp.tz);
  double yaw = tangent;
  if (car.slide != 0 && car.slide == car.slide) {
    // just moved over to another lane: it slides across from where it was, turned a little towards it
    car.slide -= jsSign(car.slide) * jsMin(std::abs(car.slide), 1.2 * dt);
    if (std::abs(car.slide) < 0.02) car.slide = 0;
    // (left of the direction of travel (tx, tz) is (tz, -tx))
    tmp.x += tmp.tz * car.slide;
    tmp.z -= tmp.tx * car.slide;
    if (car.slide != 0) yaw -= jsSign(car.slide) * jsAtan2(1.2, jsMax(car.v, 3));
  }
  if (car.side != 0 && car.side == car.side) {
    // off the middle of its lane: making way for an emergency vehicle, or one passing the others (src/sim/emergency)
    tmp.x += tmp.tz * car.side;
    tmp.z -= tmp.tx * car.side;
  }
  if (car.past && car.past->off != 0) {
    // going round something that blocks its lane, turned a little while it moves across (src/sim/pastblock)
    tmp.x += tmp.tz * car.past->off;
    tmp.z -= tmp.tx * car.past->off;
    yaw += jsAtan(car.past->turn);
  }
  // passing a side of the road closed for roadworks: over on the oncoming lane, turned a little while it moves across
  RoadEvents* re = roadEvents;
  const Lane* pl = car.lane;
  if (re && pl) {
    const double pass = re->shift(*pl, car.s);
    if (pass != 0) {
      tmp.x += tmp.tz * pass;
      tmp.z -= tmp.tx * pass;
      yaw += jsAtan2(re->shift(*pl, car.s + 0.5) - re->shift(*pl, car.s - 0.5), 1);
    }
  }
  // past a kerb build-out: over in its gap, turned a little while it moves across (src/sim/narrowings)
  if (pl && narrowings && pl->edge->narrow) {
    Narrowings* nw = narrowings.get();
    const double hw = car.width / 2;
    const double hl = car.length / 2;
    const double ns = nw->shift(*pl, car.s, hw, hl);
    if (ns != 0) {
      tmp.x += tmp.tz * ns;
      tmp.z -= tmp.tx * ns;
      yaw += jsAtan2(nw->shift(*pl, car.s + 0.5, hw, hl) - nw->shift(*pl, car.s - 0.5, hw, hl), 1);
    }
  }
  // slope from the height change since the last frame
  if (ds > 0.01) car.pitch += (jsAtan2(tmp.y - car.y, ds) - car.pitch) * jsMin(1, dt * 6);
  // how it is drawn: turning gradually through the corners of its way, drifting rather than jumping sideways (src/sim/smoothpose)
  double way_ = wayYaw(car, chordHalf(car.length));
  if (!std::isfinite(way_)) way_ = tangent;
  if (!car.drawn) car.drawn = EasedPose{.x = car.x, .z = car.z, .yaw = car.yaw};
  EasedPose& drawn = *car.drawn;
  easePose(drawn, tmp.x, tmp.z, way_, yaw - tangent, ds, dt, car.bus ? 5.9 : car.length * 0.6);
  car.x = tmp.x;
  car.y = tmp.y;
  car.z = tmp.z;
  car.yaw = yaw;

  // --- contact with the player ---
  // (where it is drawn, as long as it is: a bus is 12 m – not only within 3 m of its middle, as before 1.7.1;
  // every picture of the contact pushes the player out of it, so that no one drives into a vehicle)
  const EasedPose& at = *car.drawn;
  const double dx = player.x - at.x;
  const double dz = player.z - at.z;
  const double reach = (car.length + player.length) / 2 + 1;
  if (dx * dx + dz * dz < reach * reach && std::abs(player.y - car.y) < 2.5) {
    const std::optional<BoxPush> push = boxPush(at.x, at.z, at.yaw, car.length / 2, car.width / 2, player.x, player.z, player.yaw, player.length / 2, player.width / 2);
    if (push) {
      if (car.crashed <= 0) {
        TrafficEvent ev;
        ev.type = "collision";
        ev.car = carp;
        ev.speed = std::abs(player.speed) + car.v;
        events.push_back(ev);
      }
      TrafficEvent ev;
      ev.type = "contact";
      ev.car = carp;
      ev.nx = push->nx;
      ev.nz = push->nz;
      ev.depth = push->depth;
      events.push_back(ev);
      car.crashed = 6;
      car.v = 0;
    }
  }
}

double Traffic::wayYaw(TrafficCar& car, double h) {
  const Connector* c = car.conn;
  if (c) {
    const double a = car.s - h;
    const double b = car.s + h;
    if (a >= 0) tmp2 = samplePolyline(c->pl, a, tmp2);
    else tmp2 = lanePoint(*c->from, c->from->len + a, tmp2);
    const double ax = tmp2.x;
    const double az = tmp2.z;
    if (b <= c->len) tmp2 = samplePolyline(c->pl, b, tmp2);
    else tmp2 = lanePoint(*c->to, b - c->len, tmp2);
    return chordYaw(ax, az, tmp2.x, tmp2.z);
  }
  const Lane* l = car.lane;
  if (!l) return JS_NAN;
  const Connector* back = car.s - h < 0 && car.lastConn && car.lastConn->to == l ? car.lastConn : nullptr;
  const Connector* on = car.s + h > l->len ? car.next : nullptr;
  if (!back && !on) {
    // (along the middle of its road)
    tmp2 = samplePolyline(l->edge->pl, l->s0 + l->dir * clamp(car.s - h, 0, l->len), tmp2);
    const double ax = tmp2.x;
    const double az = tmp2.z;
    tmp2 = samplePolyline(l->edge->pl, l->s0 + l->dir * clamp(car.s + h, 0, l->len), tmp2);
    return chordYaw(ax, az, tmp2.x, tmp2.z);
  }
  if (back) tmp2 = samplePolyline(back->pl, back->len + car.s - h, tmp2);
  else tmp2 = lanePoint(*l, car.s - h, tmp2);
  const double ax = tmp2.x;
  const double az = tmp2.z;
  if (on) tmp2 = samplePolyline(on->pl, car.s + h - l->len, tmp2);
  else tmp2 = lanePoint(*l, car.s + h, tmp2);
  return chordYaw(ax, az, tmp2.x, tmp2.z);
}

bool Traffic::boxBlocked(Connector& c) {
  for (const Conflict& k : c.conflicts) {
    const std::vector<TrafficCar*>* inside = listOf(onConn, k.other->id);
    if (!inside) continue;
    for (const TrafficCar* o : *inside)
      if (k.merge || o->s - o->length / 2 < k.sOther + 2.5) return true;
  }
  return false;
}

bool Traffic::junctionEmpty(Connector& c) {
  for (const Conflict& k : c.conflicts) {
    const std::vector<TrafficCar*>* list = listOf(onConn, k.other->id);
    if (list && !list->empty()) return false;
  }
  return true;
}

// --- pedestrians (src/sim/pedestrians) ---------------------------------------
bool Traffic::peopleCrossing(Lane& lane, const PlayerState& player) {
  for (const CrossingJson& x : lane.edge->xings) {
    if (x.k != XING_ZEBRA && x.k != XING_SIGNAL && x.k != XING_UNMARKED) continue;
    const double pos = lane.dir > 0 ? x.s - lane.s0 : lane.s0 - x.s;
    if (pos < -2 || pos > 20) continue;
    for (const Pedestrian* p : peds())
      if (p->crossing == &x && p->state == "cross") return true;
    if (player.onFoot && onCrossing(x, player)) return true;
  }
  // (people crossing it at the junction's mouth, where no crossing is drawn)
  return crowd ? crowd->crossingLane(lane) : false;
}

bool Traffic::onCrossing(const CrossingJson& c, const PlayerState& player) {
  const Edge* e = world.edges[c.e];
  if (!e) return false;
  tmp2 = samplePolyline(e->pl, c.s, tmp2);
  const double dx = player.x - tmp2.x;
  const double dz = player.z - tmp2.z;
  return std::abs(dx * tmp2.tx + dz * tmp2.tz) <= c.w / 2 + 0.8 && std::abs(dx * tmp2.tz - dz * tmp2.tx) <= e->width / 2 + 0.3;
}

bool Traffic::atZebra(const CrossingJson& c, const PlayerState& player) {
  const Edge* e = world.edges[c.e];
  if (!e) return false;
  tmp2 = samplePolyline(e->pl, c.s, tmp2);
  const double dx = player.x - tmp2.x;
  const double dz = player.z - tmp2.z;
  const double along = dx * tmp2.tx + dz * tmp2.tz;
  // (left of the edge direction is (tz, -tx))
  const double across = dx * tmp2.tz - dz * tmp2.tx;
  if (std::abs(along) > c.w / 2 + 0.6) return false;
  const double half = e->width / 2;
  if (std::abs(across) <= half) return true;
  if (std::abs(across) > half + 1.6) return false;
  // at the kerb: looking out over the road (towards the other side)
  const double fx = -jsSin(player.yaw);
  const double fz = -jsCos(player.yaw);
  const double out = -jsSign(across);
  return (fx * tmp2.tz - fz * tmp2.tx) * out > 0.5;
}

Pedestrian* Traffic::pedestrianAt(const CrossingJson& c) const {
  for (Pedestrian* p : peds())
    if (p->crossing == &c && p->state != "gone") return p;
  return nullptr;
}

std::optional<BoxPush> boxPush(double ax, double az, double ayaw, double ahl, double ahw, double bx, double bz, double byaw, double bhl, double bhw) {
  const double dx = bx - ax;
  const double dz = bz - az;
  std::optional<BoxPush> best;
  for (const double ang : {ayaw, ayaw + PI / 2, byaw, byaw + PI / 2}) {
    const double ux = -jsSin(ang);
    const double uz = -jsCos(ang);
    const double along = dx * ux + dz * uz;
    const double ra = ahl * std::abs(jsCos(ang - ayaw)) + ahw * std::abs(jsSin(ang - ayaw));
    const double rb = bhl * std::abs(jsCos(ang - byaw)) + bhw * std::abs(jsSin(ang - byaw));
    const double depth = ra + rb - std::abs(along);
    if (depth <= 0) return std::nullopt;
    if (!best || depth < best->depth) best = along >= 0 ? BoxPush{ux, uz, depth} : BoxPush{-ux, -uz, depth};
  }
  return best;
}

bool boxesOverlap(double ax, double az, double ayaw, double ahl, double ahw, double bx, double bz, double byaw, double bhl, double bhw) {
  const double dx = bx - ax;
  const double dz = bz - az;
  for (const double ang : {ayaw, ayaw + PI / 2, byaw, byaw + PI / 2}) {
    const double ux = -jsSin(ang);
    const double uz = -jsCos(ang);
    const double dist = std::abs(dx * ux + dz * uz);
    const double ra = ahl * std::abs(jsCos(ang - ayaw)) + ahw * std::abs(jsSin(ang - ayaw));
    const double rb = bhl * std::abs(jsCos(ang - byaw)) + bhw * std::abs(jsSin(ang - byaw));
    if (dist > ra + rb) return false;
  }
  return true;
}

}  // namespace fs
