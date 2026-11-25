// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/cyclists.ts @ v1.7.7
#include "sim/cyclists.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/js.h"
#include "sim/signals.h"
#include "sim/traffic.h"
#include "world/types.h"

namespace fs {

namespace {

const std::array<int, 8> COLOURS = {0xc23b22, 0x1f6fb5, 0xe0b020, 0x2f8f4a, 0x7a3fa0, 0xd9d9d9, 0x30343a, 0xe56b1f};
/** how far ahead on their line they look (m) */
const std::array<double, 11> LOOK = {0.7, 1.9, 3.3, 5, 7, 9.5, 12, 14.5, 17, 19.5, 22};
/** the player's vehicle came on to their line less than this many seconds ahead of them: cut off */
constexpr double CUT_OFF = 2.2;

/** How long a vehicle `d` metres before the line takes to get there if it slows down to turn (s). */
double timeToLine(double d, double v) {
  const double turn = 4;
  const double brake = 1.5;
  if (v <= turn) return d / std::max(v, 3.0);
  const double braking = (v * v - turn * turn) / (2 * brake);
  return d > braking ? (d - braking) / v + (v - turn) / brake : (2 * d) / (v + turn);
}

/** (`o.speed ?? o.v ?? 0`: the player's vehicle and the obstacles have a speed, the cars of the traffic a v) */
double speedOf(const PlayerState& o) { return o.speed; }
double speedOf(const TrafficCar& o) { return o.v; }

/** Is the point within `margin` of the box – where the box is now, or where it will be in `t` seconds? */
template <class Box>
bool inBox(const Box& o, double x, double z, double margin, double t) {
  const double sin = std::sin(o.yaw);
  const double cos = std::cos(o.yaw);
  const double v = speedOf(o);
  const double dx = x - (o.x - sin * v * t);
  const double dz = z - (o.z - cos * v * t);
  const double lon = -dx * sin - dz * cos;
  const double lat = dx * cos - dz * sin;
  return std::abs(lon) < o.length / 2 + margin + CYCLE_LENGTH * 0.25 && std::abs(lat) < o.width / 2 + margin + CYCLE_WIDTH / 2;
}

}  // namespace

double cycleStrip(const Lane& lane) {
  const Edge& e = *lane.edge;
  const double pavement = lane.dir > 0 ? e.swR : e.swL;
  const double toEdge = e.width / 2 - std::abs(lane.off);
  return toEdge + (pavement >= 1 ? std::min(1.15, pavement * 0.58) : 0.45);
}

bool rideable(const Lane& lane) {
  const Edge& e = *lane.edge;
  if (lane.idx != 0 || (e.flags & EF_URBAN) == 0 || (e.flags & (EF_TUNNEL | EF_ROUNDABOUT | EF_LINK)) != 0) return false;
  if (e.cls <= TRUNK_LINK || e.cls >= TRACK || !LaneGraph::drivable(e, true)) return false;
  return (lane.dir > 0 ? e.swR : e.swL) >= 1;
}

Cyclists::Cyclists(World& world_, LaneGraph& graph_, SignalSystem& signals_) : world(world_), graph(graph_), signals(signals_) {}

Cyclists::~Cyclists() {
  for (Cyclist* c : list) delete c;
}

void Cyclists::clear() {
  for (Cyclist* c : list) gone.emplace_back(c);
  list.clear();
  decidedAhead = nullptr;
  decidedBehind = nullptr;
  waiting = 0;
  stageAt = time + 6;
}

void Cyclists::drop(size_t i) {
  gone.emplace_back(list[i]);
  list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
}

Cyclist* Cyclists::add(Lane& lane, double s, double cruise, bool staged, double lag) {
  auto* c = new Cyclist();
  c->id = nextId++;
  c->lane = &lane;
  c->conn = nullptr;
  c->s = s;
  c->v = cruise;
  c->cruise = cruise;
  c->next = pick(lane);
  c->out = cycleStrip(lane);
  c->phase = rand() * 6;
  c->roll = 0;
  c->braking = false;
  c->colour = COLOURS[static_cast<size_t>(std::floor(rand() * COLOURS.size()))];
  c->staged = staged ? &lane : nullptr;
  c->lag = lag;
  c->down = 0;
  c->told = -100;
  c->seen = 0;
  c->held = 0;
  c->atLight = false;
  place(*c);
  list.push_back(c);
  return c;
}

/**
 * The way on at the end of a lane: straight on where that is possible,
 * else to the right – never across the oncoming traffic, on to a fast road
 * or into a yard.  Null: their ride ends here.
 */
Connector* Cyclists::pick(const Lane& lane) {
  Connector* best = nullptr;
  double score = 0;
  for (Connector* c : lane.out) {
    if (c->turn != TURN_STRAIGHT && c->turn != TURN_RIGHT) continue;
    const Edge& e = *c->to->edge;
    if (e.cls <= TRUNK_LINK || e.cls >= TRACK || !LaneGraph::drivable(e, true)) continue;
    double sc = c->turn == TURN_STRAIGHT ? 3 : 1;
    if (!e.name.empty() && e.name == lane.edge->name) sc += 0.5;
    // (to the rightmost lane, beside which they ride)
    if (c->to->idx == 0) sc += 0.25;
    if (sc > score) {
      score = sc;
      best = c;
    }
  }
  return best;
}

/** The rightmost lane of the carriageway `lane` belongs to. */
Lane* Cyclists::rightmost(Lane& lane) {
  if (lane.idx == 0) return &lane;
  const LaneGraph::EdgeLanes* el = graph.edgeLanes[lane.edge->id];
  if (el)
    for (Lane* l : lane.dir > 0 ? el->fwd : el->back)
      if (l->idx == 0) return l;
  return &lane;
}

/** The point `d` metres further along a cyclist's line (into `out`), and how far to the right of the middle of the lane they ride there. */
double Cyclists::ahead(const Cyclist& c, double d, PolySample& out) {
  double s = c.s + d;
  Connector* conn = c.conn;
  if (c.lane) {
    if (s <= c.lane->len || !c.next) {
      lanePoint(*c.lane, std::min(s, c.lane->len), out);
      return c.out;
    }
    s -= c.lane->len;
    conn = c.next;
  }
  if (!conn) return c.out;
  if (s <= conn->len) {
    samplePolyline(conn->pl, s, out);
    return c.out;
  }
  Lane* l2 = rightmost(*conn->to);
  lanePoint(*l2, std::min(l2->len, s - conn->len), out);
  return cycleStrip(*l2);
}

void Cyclists::place(Cyclist& c) {
  const double out = ahead(c, 0, tmp);
  c.x = tmp.x - tmp.tz * out;
  c.z = tmp.z + tmp.tx * out;
  c.yaw = std::atan2(-tmp.tx, -tmp.tz);
  c.y = world.surfaceAt(c.x, c.z, tmp.y + 1.5, hit) ? hit.y : tmp.y;
}

void Cyclists::update(double dt, const PlayerState& player, const std::vector<PlayerState*>& obstacles, const std::vector<TrafficCar*>& cars,
                      std::vector<TrafficEvent>& events) {
  gone.clear();
  time += dt;
  spawnT -= dt;
  if (spawnT <= 0) {
    spawnT = 1.5;
    int own = 0;
    for (const Cyclist* c : list)
      if (!c->staged) own++;
    if (own < target) spawn(player);
  }
  if (staging && target > 0) stage(player, dt);
  for (int i = static_cast<int>(list.size()) - 1; i >= 0; i--) {
    Cyclist& c = *list[static_cast<size_t>(i)];
    const double far = std::hypot(c.x - player.x, c.z - player.z);
    // gone: far away, on a road that is not loaded any more, at the end of their ride – or picked up after a fall
    Lane* on = c.lane ? c.lane : c.conn ? c.conn->to : nullptr;
    if (far > 320 || !on || graph.lanes[on->id] != on || (c.down > 0 && (c.down -= dt) <= 0) || c.held > 30) {
      drop(static_cast<size_t>(i));
      continue;
    }
    if (c.down > 0) continue;
    ride(c, dt, player, obstacles, cars, events);
  }
}

void Cyclists::ride(Cyclist& c, double dt, const PlayerState& player, const std::vector<PlayerState*>& obstacles, const std::vector<TrafficCar*>& cars,
                    std::vector<TrafficEvent>& events) {
  Lane* lane = c.lane;
  // --- sent along: to be at the junction a moment after the player's vehicle ---
  if (c.staged) {
    Lane* pl = player.lane;
    Lane* goal = c.staged;
    const std::optional<UntilJunction> me = lane ? untilJunction(*lane, c.s) : std::nullopt;
    const std::optional<UntilJunction> them = pl && player.s ? untilJunction(*rightmost(*pl), *player.s + player.length / 2) : std::nullopt;
    if (!me || me->last != goal) c.staged = nullptr;
    else if (them && them->last == goal) {
      const double mine = me->dist;
      if (mine > 12) {
        const double theirs = std::max(0.0, them->dist);
        const double want = clamp(mine / (timeToLine(theirs, player.speed) + c.lag), 3.6, 7.3);
        c.cruise += clamp(want - c.cruise, -0.9 * dt, 0.9 * dt);
      }
    }
  }
  double v0 = c.cruise;
  /** how far they may still ride before they have to stand (m), and whether it is a light or the end of their ride that holds them */
  double room = JS_INF;
  bool ownStop = false;
  c.atLight = false;
  if (lane) {
    const Node& node = *lane->to;
    const Arm& arm = *lane->armTo;
    const double toLine = lane->stopAt - 1 - c.s;
    if (!c.next) c.next = pick(*lane);
    if (!c.next) {
      room = std::max(0.0, lane->len - 2.5 - c.s);
      ownStop = true;
    } else if (arm.sg >= 0 && node.sc >= 0) {
      // a light: theirs is that of the lane they ride beside
      const int st = signals.get(node.sc, arm.sg);
      if (toLine > -1.5 && (st == SIG_RED || st == SIG_RED_AMBER || (st == SIG_AMBER && toLine > 6))) {
        room = std::max(0.0, toLine);
        ownStop = true;
        c.atLight = true;
      }
    } else if (node.arms.size() > 2 && (arm.role == ROLE_YIELD || arm.role == ROLE_STOP || arm.role == ROLE_MINOR) && toLine > -1) {
      // where they have to give way they come up slowly and look
      v0 = std::min(v0, 2 + std::max(0.0, toLine) * 0.35);
    }
  }
  // --- what stands or drives on their line: the cars, the trams, the player's vehicle ---
  bool byPlayer = false;
  double seenAt = JS_INF;
  for (const double d : LOOK) {
    if (d - 1.5 > room) break;
    const double out = ahead(c, d, tmp2);
    const double px = tmp2.x - tmp2.tz * out;
    const double pz = tmp2.z + tmp2.tx * out;
    bool blocked = false;
    if (inBox(player, px, pz, 0.35, 0)) {
      blocked = true;
      byPlayer = true;
    }
    for (const PlayerState* o : obstacles) {
      if (std::abs(o->x - px) > 40 || std::abs(o->z - pz) > 40 || !inBox(*o, px, pz, 0.35, 0)) continue;
      blocked = true;
      if (o->own) byPlayer = true;
    }
    if (!blocked) {
      for (const TrafficCar* o : cars) {
        if (std::abs(o->x - px) > 9 || std::abs(o->z - pz) > 9) continue;
        // (where the car is, and – near them – where it will be in half a second)
        if (inBox(*o, px, pz, 0.3, 0) || (d < 8 && inBox(*o, px, pz, 0.3, 0.5))) {
          blocked = true;
          break;
        }
      }
    }
    if (blocked) {
      seenAt = std::max(0.0, d - 1.5);
      if (seenAt < room) {
        room = seenAt;
        ownStop = false;
      } else byPlayer = false;
      break;
    }
  }
  // --- their speed ---
  double a = c.v < v0 ? 1.4 * (1 - c.v / std::max(v0, 0.5)) : -1.2;
  if (room < JS_INF) {
    const double need = (c.v * c.v) / (2 * std::max(room, 0.05));
    if (room < 0.4) a = -6.5;
    else if (need > 0.7) a = -std::min(6.5, need * 1.15);
    else if (c.v > std::sqrt(2 * 0.7 * room)) a = std::min(a, 0.0);
  }
  c.braking = a < -2.6;
  // The player's vehicle came on to their line at short notice: they have to brake for it.
  // (Not: it has been standing or driving there for a while – then they just wait behind it.)
  // (Only whoever comes along the same street – beside or behind the vehicle that turns, or towards it:
  // a cyclist from a side street who has to give way, and stops, is nobody's victim.)
  Lane* pl = player.lane;
  Lane* mine = lane ? lane : c.conn ? c.conn->from : nullptr;
  bool sameStreet = false;
  if (pl && mine) {
    if (mine->edge == pl->edge) sameStreet = true;
    else {
      const std::optional<UntilJunction> u = untilJunction(*mine, lane ? c.s : mine->len);
      sameStreet = u && u->last->edge == pl->edge;
    }
  }
  if (byPlayer && sameStreet && c.seen <= 0 && c.v > 1 && room / c.v < CUT_OFF && time - c.told > 8) {
    c.told = time;
    TrafficEvent e;
    e.type = "cyclistEndangered";
    e.cyclist = &c;
    e.hit = false;
    events.push_back(e);
  }
  c.seen = byPlayer ? 1.5 : std::max(0.0, c.seen - dt);
  c.v = std::max(0.0, c.v + a * dt);
  // (standing behind something that does not move – or at the end of their ride: after a while they get off and are gone)
  const bool ended = lane != nullptr && !c.next;
  c.held = c.v < 0.2 && room < 1.2 && (!ownStop || ended) ? c.held + dt * (ended ? 6 : 1) : 0;
  c.s += c.v * dt;
  c.roll += (c.v * dt) / 0.34;
  // (they pedal while they ride on or speed up, and let the bicycle roll while they slow down)
  if (a > -0.6) c.phase += c.v * dt * 2.4;
  // --- on to the path through the junction, and to the next street ---
  if (lane && c.s >= lane->len) {
    if (c.next) {
      c.s -= lane->len;
      c.conn = c.next;
      c.lane = nullptr;
      c.next = nullptr;
    } else c.s = lane->len;
  }
  if (c.conn && c.s >= c.conn->len) {
    Lane* l2 = rightmost(*c.conn->to);
    c.s = std::min(c.s - c.conn->len, l2->len);
    // (riding in from a lane further left: the strip is found from the rightmost one)
    c.out += l2 == c.conn->to ? 0 : -std::abs(c.conn->to->off - l2->off);
    c.lane = l2;
    c.conn = nullptr;
    c.next = pick(*l2);
  }
  // (their place beside the lane changes from street to street: they swing over, they do not jump)
  if (c.lane) c.out += clamp(cycleStrip(*c.lane) - c.out, -1.2 * dt, 1.2 * dt);
  place(c);
  // --- run into by the player's vehicle: a fall ---
  bool struck = inBox(player, c.x, c.z, 0.08, 0);
  for (const PlayerState* o : obstacles)
    if (o->own && inBox(*o, c.x, c.z, 0.08, 0)) struck = true;
  if (struck) {
    c.down = 6;
    c.v = 0;
    c.braking = false;
    TrafficEvent e;
    e.type = "cyclistEndangered";
    e.cyclist = &c;
    e.hit = true;
    events.push_back(e);
    c.told = time;
  }
}

/** Somebody turns up on a bicycle on a street near the player – not right before their eyes. */
void Cyclists::spawn(const PlayerState& player) {
  for (int attempt = 0; attempt < 8; attempt++) {
    const double ang = rand() * PI * 2;
    const double rad = 90 + rand() * 130;
    const std::vector<Lane*>* lanes = lanesAt(player.x + std::cos(ang) * rad, player.z + std::sin(ang) * rad);
    if (!lanes || lanes->empty()) continue;
    Lane* lane = (*lanes)[static_cast<size_t>(std::floor(rand() * static_cast<double>(lanes->size())))];
    if (!rideable(*lane) || lane->len < 30) continue;
    const double s = 4 + rand() * (lane->len - 12);
    lanePoint(*lane, s, tmp);
    const double d = std::hypot(tmp.x - player.x, tmp.z - player.z);
    if (d < 80 || d > 250) continue;
    const double fx = -std::sin(player.yaw);
    const double fz = -std::cos(player.yaw);
    // not where the player is looking
    if (((tmp.x - player.x) * fx + (tmp.z - player.z) * fz) / d > 0.75 && d < 170) continue;
    bool taken = false;
    for (const Cyclist* c : list)
      if (c->lane == lane && std::abs(c->s - s) < 25) taken = true;
    if (taken) continue;
    add(*lane, s, 4.2 + rand() * 2.6);
    return;
  }
}

/**
 * The situation every driving lesson has: the vehicle comes up to a
 * junction where it can turn right, and a cyclist rides straight on beside
 * it.  Decided once per approach – in a good third of them somebody comes.
 */
void Cyclists::stage(const PlayerState& player, double dt) {
  Lane* own = player.lane;
  if (!own || !player.s) {
    waiting = 0;
    return;
  }
  const double front = *player.s + player.length / 2;
  // the next real junction along the street (over the joints where it was mapped in pieces)
  const std::optional<UntilJunction> next = untilJunction(*rightmost(*own), front);
  if (!next) {
    waiting = 0;
    return;
  }
  Lane* lane = next->last;
  const double toLine = next->dist;
  waiting = std::abs(player.speed) < 0.4 && toLine < 12 && toLine > -1 ? waiting + dt : 0;
  if (time < stageAt) return;
  const bool ahead_ = toLine >= 90 && toLine <= 280 && player.speed > 4 && lane != decidedAhead;
  const bool behind = waiting > 1.2 && lane != decidedBehind;
  if (!ahead_ && !behind) return;
  if (ahead_) decidedAhead = lane;
  else decidedBehind = lane;
  if (!rideable(*lane)) return;
  // a street to turn right into – and a way straight on for the cyclist
  bool right = false;
  for (const Connector* c : lane->out)
    if (c->turn == TURN_RIGHT && LaneGraph::drivable(*c->to->edge, false)) right = true;
  if (!right) return;
  const Connector* on = pick(*lane);
  if (!on || on->turn != TURN_STRAIGHT) return;
  for (const Cyclist* c : list) {
    Lane* l = c->lane ? c->lane : c->conn ? c->conn->from : nullptr;
    if (!l) continue;
    const std::optional<UntilJunction> u = untilJunction(*l, c->lane ? c->s : l->len);
    if (u && u->last == lane) return;
  }
  if (rand() > chance) return;
  const double lag = 0.9 + rand() * 1.3;
  double back;
  double cruise;
  if (ahead_) {
    // Riding unhurriedly, they are at the junction a moment after the vehicle: that says where they
    // are now.  Far ahead of it, that is – not turning up before the player's eyes –, and the
    // vehicle passes them on the way.
    cruise = 4.2;
    back = cruise * (timeToLine(toLine, player.speed) + lag);
    if (toLine - back < 55 || back < 25) return;
  } else {
    // far behind the vehicle that waits – small in its mirrors
    cruise = 5.5 + rand() * 1.5;
    back = toLine + player.length + 45 + rand() * 25;
  }
  // (counted back from the line, over the pieces of the street)
  const std::optional<Back> at = backFrom(*lane, lane->stopAt, back);
  if (!at || !rideable(*at->lane)) return;
  add(*at->lane, at->s, cruise, true, lag);
  Cyclist* c = list.back();
  c->staged = lane;
  stageAt = time + 25 + rand() * 40;
}

std::optional<Cyclists::UntilJunction> Cyclists::untilJunction(Lane& lane, double s) {
  Lane* l = &lane;
  double dist = l->stopAt - s;
  for (int hop = 0; hop < 12 && dist < 320; hop++) {
    if (l->to->arms.size() >= 3 || l->to->stub) return UntilJunction{l, dist};
    // (a joint: the one way on, to the lane at the kerb)
    Connector* on = nullptr;
    int n = 0;
    for (Connector* c : l->out)
      if (c->turn != TURN_UTURN) {
        on = c;
        n++;
      }
    if (n != 1) return std::nullopt;
    Lane* nx = rightmost(*on->to);
    dist += l->len - l->stopAt + on->len + nx->stopAt;
    l = nx;
  }
  return l->to->arms.size() >= 3 && dist < 320 ? std::optional<UntilJunction>(UntilJunction{l, dist}) : std::nullopt;
}

/** `back` metres before `s` on `lane`, over the joints behind it – none where the street began at a junction. */
std::optional<Cyclists::Back> Cyclists::backFrom(Lane& lane, double s, double back) {
  Lane* l = &lane;
  double at = s - back;
  for (int hop = 0; hop < 12; hop++) {
    if (at >= 2) return Back{l, at};
    if (l->from->arms.size() != 2) return std::nullopt;
    Connector* into = nullptr;
    int n = 0;
    for (Connector* c : l->in)
      if (c->turn != TURN_UTURN) {
        into = c;
        n++;
      }
    if (n != 1) return std::nullopt;
    Lane* prev = rightmost(*into->from);
    at += into->len + prev->len;
    l = prev;
  }
  return std::nullopt;
}

bool Cyclists::blocksRightTurn(const Lane& lane, double s, double half) {
  for (const Cyclist* c : list) {
    // (not: one who has fallen, who stands behind something and waits – or who has red: the vehicle
    // that may turn on the green arrow has not)
    if (c->down > 0 || c->held > 3 || (c->atLight && c->v < 2)) continue;
    if (c->lane && c->lane->edge == lane.edge && c->lane->dir == lane.dir) {
      const double rel = c->s - s;
      if (rel > -(half + 5 + c->v * 3) && rel < half + 4) return true;
    } else if (c->lane && c->lane->to->arms.size() == 2) {
      // on a piece of the same street further back: how far behind the vehicle?
      const std::optional<UntilJunction> ahead_ = untilJunction(*c->lane, c->s);
      const std::optional<UntilJunction> mine = untilJunction(*rightmost(const_cast<Lane&>(lane)), s);
      if (ahead_ && mine && ahead_->last == mine->last) {
        const double rel = mine->dist - ahead_->dist;
        if (rel > -(half + 5 + c->v * 3) && rel < half + 4) return true;
      }
    } else if (c->conn && c->conn->from->edge == lane.edge && c->conn->from->dir == lane.dir && c->s < std::min(c->conn->len, 9.0))
      return true;
  }
  return false;
}

Cyclist* Cyclists::nearest(double x, double z, double reach) {
  Cyclist* best = nullptr;
  double bd = reach;
  for (Cyclist* c : list) {
    const double d = std::hypot(c->x - x, c->z - z);
    if (d < bd && c->down <= 0) {
      bd = d;
      best = c;
    }
  }
  return best;
}

}  // namespace fs
