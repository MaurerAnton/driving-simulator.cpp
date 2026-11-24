// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/pedestrians.ts @ v1.7.7
#include "sim/pedestrians.h"

#include <algorithm>
#include <cmath>

#include "core/js.h"
#include "sim/hooks.h"
#include "sim/signals.h"
#include "sim/traffic.h"

namespace fs {

namespace {

constexpr double PED_PI = 3.14159265358979323846;

/** how many kinds of people render/people knows (its KINDS) */
constexpr int LOOK_KINDS = 17;

const double OUT[24] = {0.12, 0.08, 0.06, 0.05, 0.06, 0.12, 0.3, 0.6, 0.78, 0.82, 0.9, 0.95, 1, 1, 0.95, 0.95, 1, 1, 0.95, 0.82, 0.62, 0.45, 0.3, 0.18};

// ------------------------------------------------------------------------------------------------
/** the narrowest pavement people walk along (m) */
constexpr double MIN_PAVEMENT = 0.9;
/** points of the way ahead a person keeps at most */
constexpr int MAXP = PED_MAXP;
/** how often a person looks round (s) */
constexpr double THINK = 0.25;
/** where people come up round the player (m): from here … */
constexpr double NEAR_ = 40;
/** … to here; and from where on they are let go */
constexpr double FAR = 190;
constexpr double GONE = 245;
/** what people expect of a vehicle: that it can stop with this braking (m/s²) after this long to react (s) */
constexpr double COMFORT = 3;
constexpr double REACT = 0.8;
/** the buckets of the grid of the people (a power of two) */
constexpr int CELLS = 512;
/** from this far from the player (m) people take a step every third frame only – too small to see the difference */
constexpr double LOD = 90;

/** what somebody is doing */
namespace MODE {
constexpr int WALK = 0, KERB = 1, CROSS = 2, PAUSE = 3, FOLLOW = 4;
}
/** what comes when the way ahead has been walked */
namespace NEXT {
constexpr int PLAN = 0, JUNCTION = 1, MOVE = 2, KERB = 3, CROSSED = 4, BACK = 5, GOAL = 6, WINDOW = 7, INTO = 8, ENTER = 9, LANDED = 10;
}
/** the moves at a junction: round a corner, over the street of an arm, away along an arm */
constexpr int CORNER = 1;
constexpr int CROSS = 2;
constexpr int LEAVE = 3;
/** what rules a crossing for them: a gap in the traffic, a zebra crossing, a light */
namespace RULE {
constexpr int GAP = 0, ZEBRA = 1, LIGHT = 2;
}
/** where they are going */
namespace GOAL {
constexpr int ERRAND = 0, HOME = 1, STOP = 2, STROLL = 3;
}
/** why they stand */
namespace PAUSE {
constexpr int NONE = 0, WINDOW = 1, CHAT = 2, REST = 3, START = 4, STOP = 5;
}

PolySample tmp;
PolyProjection proj;
SurfaceHit hit;
/** how far aside somebody tries to step round a post (m), and where ahead they look for one */
const double ASIDE[6] = {0.45, -0.45, 0.85, -0.85, 1.2, -1.2};
const double PROBE[2] = {0.9, 1.8};

/** the length of (x, z) (Math.hypot is slow) */
inline double len(double x, double z) { return std::sqrt(x * x + z * z); }

int cellKey(int ix, int iz) { return (jsImul(ix, 73856093) ^ jsImul(iz, 19349663)) & (CELLS - 1); }

/** The pavement of `e` on `side` (+1 left of a→b): its width, 0 where there is none to walk on. */
double pavement(const Edge& e, int side) {
  if (e.cls <= MOTORWAY_LINK || (e.flags & EF_TUNNEL) != 0) return 0;
  const double w = side > 0 ? e.swL : e.swR;
  return w >= MIN_PAVEMENT ? w : 0;
}

/** Where a pavement begins and ends along its street (stations): at the junctions' kerbs. */
double startOf(const Edge& e) { return std::min(e.cutA, e.len / 2); }
double endOf(const Edge& e) { return std::max(e.len - e.cutB, e.len / 2); }

/** The side of the street (+1 left of a→b) that is the arm's side `sg` (+1 left, looking away from its junction). */
int edgeSide(const Arm& arm, int sg) { return arm.out ? sg : -sg; }

/** The station where an arm's pavement meets the corner of its junction. */
double mouthS(const Arm& arm) { return arm.out ? startOf(*arm.edge) : endOf(*arm.edge); }

/** How many people a street draws (0 … 1): the main streets of a town most. */
double busyness(const Edge& e) {
  if ((e.flags & EF_TUNNEL) != 0 || e.cls <= MOTORWAY_LINK) return 0;
  double w;
  if (e.cls <= TERTIARY_LINK) w = e.cls % 2 == 1 ? 0.35 : 1;
  else if (e.cls == UNCLASSIFIED) w = 0.6;
  else if (e.cls == RESIDENTIAL) w = 0.5;
  else if (e.cls == LIVING_STREET) w = 0.8;
  else w = 0.2;
  return (e.flags & EF_URBAN) != 0 ? w : w * 0.3;
}

/** A street of shops: a main street in town. */
bool shopping(const Edge& e) { return (e.flags & EF_URBAN) != 0 && e.cls >= PRIMARY && e.cls <= TERTIARY && e.cls % 2 == 0; }

// ---- sim/timetable.ts: the clocks in Kassel ------------------------------------------------------

double daysFromCivil(int y, int m, int d) {
  y -= m <= 2 ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097.0 + doe - 719468;
}

int utcYear(double ms) {
  const double days = std::floor(ms / 86400000.0);
  int z = static_cast<int>(days) + 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const int doe = z - era * 146097;
  const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = yoe + era * 400;
  const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int mp = (5 * doy + 2) / 153;
  const int m = mp + (mp < 10 ? 3 : -9);
  return y + (m <= 2 ? 1 : 0);
}

/** the day of the last Sunday of a month (0-based) */
int lastSunday(int y, int month0) {
  const int dim = month0 == 2 || month0 == 9 ? 31 : 30;
  const double t = daysFromCivil(y, month0 + 1, dim);
  // (1970-01-01 was a Thursday: weekday 4)
  const int wd = static_cast<int>(jsMod(t + 4, 7));
  return dim - wd;
}

/** How many seconds the clocks in Kassel are ahead of UTC at a moment (ms since 1970): 7200 in summer time, else 3600. */
double berlinOffset(double ms) {
  const int y = utcYear(ms);
  const double start = daysFromCivil(y, 3, lastSunday(y, 2)) * 86400000.0 + 3600000.0;
  const double end = daysFromCivil(y, 10, lastSunday(y, 9)) * 86400000.0 + 3600000.0;
  return ms >= start && ms < end ? 7200 : 3600;
}

}  // namespace

double berlinSeconds(double ms) {
  const double local = ms + berlinOffset(ms) * 1000;
  return jsMod(local, 86400000.0) / 1000;
}

int ageOfLook(int look) {
  const double r = hash01(jsImul(look, 0x2c1b3c6d) ^ jsImul(1, 0x297a2d39));
  const int kind = std::min(LOOK_KINDS - 1, static_cast<int>(std::floor(r * LOOK_KINDS)));
  return kind == 3 || kind == 4 ? AGE::OLD : kind == 6 || kind == 11 ? AGE::CHILD : AGE::ADULT;
}

int lookOfAge(int age, const std::function<double()>& rand) {
  for (int i = 0; i < 80; i++) {
    const int look = static_cast<int>(std::floor(rand() * 100000));
    if (ageOfLook(look) == age) return look;
  }
  return static_cast<int>(std::floor(rand() * 100000));
}

double rainOf(const std::optional<RainWeather>& w) {
  if (!w) return 0;
  if (w->kind == "rain" || w->kind == "storm") return clamp(0.35 + 0.65 * w->intensity, 0, 1);
  if (w->kind == "drizzle") return clamp(0.15 + 0.4 * w->intensity, 0, 1);
  if (w->kind == "snow") return clamp(0.25 + 0.4 * w->intensity, 0, 1);
  return 0;
}

int crowdTarget(double traffic, const std::string& quality) {
  const double base = quality == "low" ? 24 : quality == "medium" ? 80 : quality == "ultra" || quality == "nomercy" ? 160 : 120;
  return static_cast<int>(jsRound(std::min<double>(CROWD_MAX, base * clamp(traffic / 0.6, 0, 1.4))));
}

double outAt(double hour) {
  const double h = jsMod(jsMod(hour, 24) + 24, 24);
  const int i = static_cast<int>(std::floor(h));
  const double t = h - i;
  return OUT[i] + (OUT[(i + 1) % 24] - OUT[i]) * t;
}

/** One of the people, with all the original's Person holds. */
struct Person : Walker {
  Person() { phase = -1; }
  // ---- who they are
  int age = AGE::ADULT;
  /** the pace they like (m/s), and the time they want in hand when they cross in a gap (s) */
  double v0 = 1.35;
  double margin = 1.8;
  /** takes small gaps, crosses on red when the road is empty, on a slant */
  bool bold = false;
  bool hurry = false;
  /** how readily they stop for a chat or a shop window (0 … 1) */
  double idle = 0.5;
  /** has an umbrella with them */
  bool brolly = false;
  /** seconds left before they go home */
  double life = 400;
  /** a way to drift when they have nowhere in particular to go */
  double roamX = 1;
  double roamZ = 0;
  // ---- on the pavement of `edge` on `side` (+1 left of a→b), walking `dir` (+1 towards b); the way is planned up to station `s`
  Edge* edge = nullptr;
  int side = 1;
  int dir = 1;
  double s = 0;
  /** where across the pavement they walk (0 at the kerb … 1 at the houses) */
  double frac = 0.5;
  // ---- the way ahead: points x, y, z; the one walked to next
  std::array<double, MAXP * 3> path{};
  int np = 0;
  int pi = 0;
  int next = NEXT::PLAN;
  /** the point on the way (they are drawn `dodge` metres to the right of it), and the way's direction there */
  double bx = 0;
  double by = 0;
  double bz = 0;
  double wx = 0;
  double wz = -1;
  double walked = 0;
  int mode = MODE::WALK;
  double modeT = 0;
  double until = 0;
  int pause = PAUSE::NONE;
  /** the way to face while standing, and where the head is to turn */
  double face = 0;
  double headTo = 0;
  // ---- at a junction: the moves decided on, the next of them, where they stand (arm, its side looking away from the junction)
  Node* jnode = nullptr;
  int jarms = 0;
  std::vector<int> moves;
  int mi = 0;
  int mArm = 0;
  int mSide = 1;
  // ---- a crossing: the street, the station, the side they start from, what rules it
  Edge* xEdge = nullptr;
  double xS = 0;
  double xS2 = 0;
  /** where they wait for it (the line itself, or – at a junction – the end of the pavement beside its mouth) */
  double xW = 0;
  int xSide = 1;
  int xRule = RULE::GAP;
  int xSc = -1;
  int xG = 0;
  const CrossingJson* xCross = nullptr;
  Node* xNode = nullptr;
  /** after it: on with the moves at the junction (walking back to its mouth first?) – or on along the street, which way */
  bool xMoves = false;
  bool xBack = false;
  int xAfter = 1;
  /** the line across, from this kerb to the other, and its direction */
  double kx = 0;
  double kz = 0;
  double fx = 0;
  double fz = 0;
  double ax = 1;
  double az = 0;
  /** where they wait, and how far they have stepped back from it */
  double sx = 0;
  double sy = 0;
  double sz = 0;
  double kback = 0;
  double kcur = 0;
  bool registered = false;
  bool pressed = false;
  /** the place to wait has been moved clear of others already waiting there */
  bool spread = false;
  double lookT = 0;
  bool redRun = false;
  double hurryX = 1;
  // ---- where they are going
  int goal = GOAL::ERRAND;
  Edge* gEdge = nullptr;
  int gSide = 1;
  double gS = 0;
  double gx = 0;
  double gz = 0;
  int goals = 1;
  /** after a pause at a window: into the house */
  bool enter = false;
  // ---- among the others
  double dodge = 0;
  double dodgeTo = 0;
  double dmin = -0.3;
  double dmax = 0.3;
  double slow = 1;
  double think = 0;
  double stuck = 0;
  double ghost = 0;
  // ---- together with others: whom they walk with, and where beside them
  Person* leader = nullptr;
  double lat = 0;
  double fwd = 0;
  Person* partner = nullptr;
  double told = -100;
  /** put there on purpose (add): they stay when nobody else is to come */
  bool placed = false;
  /** how many walk with them */
  int group = 0;
  /** the time not yet stepped (far off, people step every third frame) */
  double lag = 0;
};

Crowd::Crowd(World& world_, SignalSystem& signals_, double seed)
    : world(world_), signals(signals_), rand_(rng(seed)), cellHead(CELLS, -1), cellNext(256, -1), otherHead(CELLS, -1), otherNext(64, -1) {}

Crowd::~Crowd() = default;

const std::vector<Walker*>& Crowd::people() {
  walkers_.assign(all.begin(), all.end());
  return walkers_;
}

void Crowd::clear() {
  for (Person* p : all) p->state = "gone";
  sweep();
  onRoad.clear();
  fresh = true;
  freshAt = time;
}

void Crowd::removeRoads(const InsertionSet<Edge*>& gone) {
  for (Person* p : all) {
    if ((p->edge && gone.has(p->edge)) || (p->xEdge && gone.has(p->xEdge)) || (p->gEdge && gone.has(p->gEdge))) p->state = "gone";
  }
  sweep();
}

const std::vector<Walker*>& Crowd::near(double x, double z, double r) {
  std::vector<Walker*>& out = nearList;
  out.clear();
  for (Person* p : all)
    if (std::abs(p->x - x) < r && std::abs(p->z - z) < r && (p->x - x) * (p->x - x) + (p->z - z) * (p->z - z) < r * r) out.push_back(p);
  return out;
}

std::string Crowd::modeOf(const Walker& w) const {
  const int m = static_cast<const Person&>(w).mode;
  return m == MODE::KERB ? "kerb" : m == MODE::CROSS ? "cross" : m == MODE::PAUSE ? "pause" : m == MODE::FOLLOW ? "follow" : "walk";
}

// ------------------------------------------------------------------------------------ for the cars
void Crowd::addBlocks(std::vector<PlayerState*>& out) {
  size_t n = 0;
  for (Person* p : onRoad) {
    if (p->state != "cross") continue;
    if (boxes.size() <= n) {
      auto b = std::make_unique<PlayerState>();
      b->length = 0.5;
      b->width = 0.5;
      boxes.push_back(std::move(b));
    }
    PlayerState* b = boxes[n++].get();
    b->x = p->x;
    b->y = p->y;
    b->z = p->z;
    b->yaw = p->yaw;
    b->speed = p->speed;
    out.push_back(b);
  }
}

bool Crowd::crossingLane(const Lane& lane) {
  for (Person* p : onRoad) {
    if (p->state != "cross" || p->xEdge != lane.edge) continue;
    const double pos = (p->xS - lane.s0) * lane.dir;
    if (pos > -4 && pos < 20) return true;
  }
  return false;
}

// ------------------------------------------------------------------------------------ people put somewhere
Walker* Crowd::add(Edge& edge, int side, double s, int dir, const WalkerOptions& opts) {
  Person* p = make(opts.look ? *opts.look : static_cast<int>(std::floor(rnd() * 100000)), opts.age);
  p->placed = true;
  if (opts.pace) p->v0 = *opts.pace;
  if (opts.margin) p->margin = *opts.margin;
  if (opts.bold) p->bold = *opts.bold;
  if (opts.hurry) p->hurry = *opts.hurry;
  if (opts.idle) p->idle = *opts.idle;
  onPavement(*p, edge, side, s, dir);
  if (opts.goal) {
    const WalkerOptions::Goal& g = *opts.goal;
    setGoal(*p, g.kind == "home" ? GOAL::HOME : g.kind == "stop" ? GOAL::STOP : g.kind == "stroll" ? GOAL::STROLL : GOAL::ERRAND, *g.edge, g.side, g.s);
  } else newGoal(*p);
  all.push_back(p);
  planAlong(*p);
  return p;
}

bool Crowd::adopt(double x, double y, double z, int look) {
  if (target <= 0 || static_cast<int>(all.size()) >= CROWD_MAX) return false;
  const std::optional<PavementAt> at = nearestPavement(x, z, 8);
  if (!at) return false;
  Person* p = make(look);
  p->x = p->bx = x;
  p->y = p->by = y;
  p->z = p->bz = z;
  onPavement(*p, *at->edge, at->side, at->s, rnd() < 0.5 ? 1 : -1, false);
  newGoal(*p);
  // first onto the pavement – over a bit of the road, if the door opened onto it
  p->np = 0;
  p->pi = 0;
  Edge& e = *at->edge;
  put(*p, e, p->side, p->s, e.width / 2 + p->frac * pavement(e, p->side), true);
  p->next = NEXT::LANDED;
  p->state = at->road ? "cross" : "walk";
  all.push_back(p);
  return true;
}

// ------------------------------------------------------------------------------------ the step
void Crowd::update(double dt, const PlayerState& player_, const std::vector<TrafficCar*>& cars, const std::vector<Cyclist*>& cyclists_,
                   const std::vector<PlayerState*>& obstacles_, std::vector<TrafficEvent>& events) {
  if (!(dt > 0)) return;
  time += dt;
  if (time - hourAt > 2) {
    // the hour and the weather (now and then: they change slowly)
    hourAt = time;
    const double h = clock ? clock() : 12;
    hour = std::isfinite(h) ? h : 12;
    rain = rainOf(weather ? weather() : std::nullopt);
  }
  player = player_.onFoot ? &player_ : nullptr;
  cyclists = &cyclists_;
  obstacles = &obstacles_;
  // (the player's own vehicle, standing on their way – on the pavement, on a crossing: they go round it)
  playerCar = player_.onFoot ? nullptr : &player_;
  see(dt, player_, cars, obstacles_);
  spawning(dt, player_);
  grid();
  // the horn: those near it start (once a press, and again while it is held)
  const bool horn = player_.horn && !player_.onFoot;
  if (horn && (!hornOn || time - hornAt > 1.5)) {
    hornAt = time;
    honk(player_);
  }
  hornOn = horn;
  frame++;
  for (size_t i = 0; i < all.size(); i++) {
    Person* p = all[i];
    if (p->state == "gone") continue;
    // (far off: a look round twice a second, a step every third frame for the time of three)
    const double dx = p->x - player_.x;
    const double dz = p->z - player_.z;
    const bool far = dx * dx + dz * dz > LOD * LOD;
    if (time >= p->think) {
      p->think = time + THINK * (far ? 2 : 1) * (0.8 + 0.4 * rnd());
      look(*p);
    }
    p->lag += dt;
    if (far && (frame + (p->leader ? p->leader : p)->id) % 3 != 0 && p->lag < 0.2) continue;
    const double step_ = p->lag;
    p->lag = 0;
    if (p->leader) follow(*p, step_);
    else step(*p, step_);
    if (!player_.onFoot) endanger(*p, player_, events);
  }
  // gone: into a house, to a stop, far away
  for (Person* p : all) {
    if (p->state == "gone") continue;
    if (p->leader && p->leader->state == "gone") p->state = "gone";
    else if (!p->leader && !p->placed && std::abs(p->x - player_.x) + std::abs(p->z - player_.z) > GONE && len(p->x - player_.x, p->z - player_.z) > GONE)
      p->state = "gone";
  }
  for (Person* p : all)
    if (p->leader && p->leader->state == "gone") p->state = "gone";
  sweep();
  onRoad.clear();
  for (Person* p : all)
    if (p->state == "cross") onRoad.push_back(p);
}

void Crowd::sweep() {
  size_t k = 0;
  for (size_t i = 0; i < all.size(); i++) {
    Person* p = all[i];
    if (p->state == "gone") {
      if (p->partner) p->partner->partner = nullptr;
      continue;
    }
    all[k++] = p;
  }
  all.resize(k);
  // (those gone are let go once nobody can point at them any more: a companion of theirs is gone by the next step)
  std::vector<std::unique_ptr<Person>> keep;
  keep.reserve(owned_.size());
  for (auto& up : owned_) {
    bool alive = up->state != "gone";
    if (!alive) {
      for (Person* q : all)
        if (q->leader == up.get()) alive = true;
    }
    if (alive) keep.push_back(std::move(up));
  }
  owned_ = std::move(keep);
}

// ------------------------------------------------------------------------------------ who comes
double Crowd::wanted() {
  const double t = std::min<double>(CROWD_MAX, std::max(0, target));
  if (t <= 0) return 0;
  return jsRound(t * outAt(hour) * (1 - 0.45 * clamp(rain, 0, 1)) * clamp(urban * 1.5, 0.12, 1));
}

void Crowd::spawning(double dt, const PlayerState& player_) {
  if (target <= 0) {
    // (people switched off: those who came by themselves go)
    fresh = true;
    freshAt = time;
    for (Person* p : all)
      if (!p->placed && !(p->leader && p->leader->placed)) p->state = "gone";
    return;
  }
  spawnT -= dt;
  if (spawnT > 0) return;
  spawnT = 0.3;
  const double want = wanted();
  double n = static_cast<double>(all.size());
  // at the beginning many at once, round the player; then one now and then, further off
  const int tries = fresh ? 24 : n < want * 0.5 ? 4 : 2;
  for (int k = 0; k < tries && n < want; k++) {
    const int made = spawn(player_, fresh);
    if (made > 0) n += made;
  }
  if (fresh && (n >= want * 0.8 || urban < 0.05)) fresh = false;
  if (fresh && time - freshAt > 4) fresh = false;
}

int Crowd::spawn(const PlayerState& player_, bool anywhere) {
  const double a = rnd() * PED_PI * 2;
  const double r0 = anywhere ? 6 : NEAR_;
  const double r = r0 + std::sqrt(rnd()) * (FAR - r0);
  const double x = player_.x + std::cos(a) * r;
  const double z = player_.z + std::sin(a) * r;
  const std::optional<PavementAt> at = samplePavement(x, z, 24);
  urban += ((at ? 1 : 0) - urban) * 0.04;
  if (!at) return 0;
  const At pl = pointOn(*at->edge, at->side, at->s, at->edge->width / 2 + 0.5 * at->sw);
  const double d = len(pl.x - player_.x, pl.z - player_.z);
  if (d < r0 * 0.8 || d > FAR + 10) return 0;
  // in a group: a couple, a parent with a child, friends
  const bool night = hour < 6 || hour >= 22;
  const int size = rnd() < (night ? 0.08 : 0.2) ? (rnd() < 0.25 ? 3 : 2) : 1;
  const bool childWith = size > 1 && !night && rnd() < 0.35;
  const int look0 = static_cast<int>(std::floor(rnd() * 100000));
  std::optional<int> age0;
  if (childWith) age0 = rnd() < 0.85 ? AGE::ADULT : AGE::OLD;
  Person* lead = make(look0, age0);
  if (size == 1 && lead->age == AGE::CHILD && night) {
    lead->state = "gone";
    owned_.pop_back();
    return 0;
  }
  lead->group = size - 1;
  const int dir = rnd() < 0.5 ? 1 : -1;
  onPavement(*lead, *at->edge, at->side, at->s, dir);
  if (!anywhere) {
    // out of a door: from the houses' side of the pavement on to the way
    const At out = pointOn(*at->edge, at->side, at->s, at->edge->width / 2 + at->sw * 0.97);
    lead->x = lead->bx = out.x;
    lead->y = lead->by = out.y;
    lead->z = lead->bz = out.z;
  }
  newGoal(*lead);
  all.push_back(lead);
  int made = 1;
  for (int k = 1; k < size; k++) {
    const int age = childWith && k == 1 ? AGE::CHILD : lead->age == AGE::OLD ? AGE::OLD : AGE::ADULT;
    Person* f = make(lookOfAge(age, [this] { return rnd(); }), age);
    f->leader = lead;
    f->mode = MODE::FOLLOW;
    f->lat = k == 1 ? 0.56 : -0.56;
    f->fwd = k == 1 ? -0.1 : -0.25;
    // (right of the nose (−sin yaw, −cos yaw) is (cos yaw, −sin yaw))
    f->x = f->bx = lead->x + f->lat * std::cos(lead->yaw);
    f->y = f->by = lead->y;
    f->z = f->bz = lead->z - f->lat * std::sin(lead->yaw);
    f->yaw = lead->yaw;
    lead->v0 = std::min(lead->v0, f->v0 + 0.05);
    lead->margin = std::max(lead->margin, f->margin);
    lead->bold = lead->bold && f->age != AGE::CHILD;
    all.push_back(f);
    made++;
  }
  planAlong(*lead);
  return made;
}

Person* Crowd::make(int look, std::optional<int> age) {
  owned_.push_back(std::make_unique<Person>());
  Person* p = owned_.back().get();
  p->id = nextId++;
  if (age && ageOfLook(look) != *age) look = lookOfAge(*age, [this] { return rnd(); });
  p->colour = look;
  p->age = ageOfLook(look);
  const double r = rnd();
  if (p->age == AGE::CHILD) {
    p->v0 = 1.35 + 0.35 * r;
    p->margin = 1.2;
    p->bold = rnd() < 0.1;
    p->idle = 0.3;
    p->brolly = rnd() < 0.4;
  } else if (p->age == AGE::OLD) {
    p->v0 = 0.8 + 0.3 * r;
    p->margin = 3 + rnd();
    p->idle = 0.7;
    p->brolly = rnd() < 0.85;
  } else {
    p->hurry = rnd() < 0.15;
    p->v0 = p->hurry ? 1.7 + 0.25 * r : 1.2 + 0.35 * r;
    p->margin = 1.6 + rnd() * 1.2;
    p->bold = rnd() < 0.06;
    p->idle = p->hurry ? 0.1 : 0.3 + 0.6 * rnd();
    p->brolly = rnd() < 0.75;
  }
  if (p->bold) p->margin *= 0.55;
  p->life = 240 + rnd() * 420;
  const double a = rnd() * PED_PI * 2;
  p->roamX = std::cos(a);
  p->roamZ = std::sin(a);
  p->think = time + rnd() * THINK;
  return p;
}

void Crowd::onPavement(Person& p, Edge& e, int side, double s, int dir, bool place) {
  p.edge = &e;
  p.side = side;
  p.dir = dir;
  p.s = clamp(s, startOf(e), endOf(e));
  p.frac = keepRight(e, side, dir, &p);
  if (place) {
    const At& at = pointOn(e, side, p.s, e.width / 2 + p.frac * pavement(e, side));
    p.x = p.bx = at.x;
    p.y = p.by = at.y;
    p.z = p.bz = at.z;
  }
  samplePolyline(e.pl, p.s, tmp);
  p.wx = tmp.tx * dir;
  p.wz = tmp.tz * dir;
  p.yaw = std::atan2(-p.wx, -p.wz);
}

double Crowd::keepRight(const Edge& e, int side, int dir, const Person* p) {
  const double w = pavement(e, side);
  const double j = (rnd() - 0.5) * 0.14;
  if (w < 1.5 || (p && p->group > 0)) return 0.5 + j * 0.5;
  // (walking along a→b, the left pavement has its kerb on the walker's right)
  return 0.5 - 0.2 * side * dir + j;
}

// ------------------------------------------------------------------------------------ places
const Crowd::At& Crowd::pointOn(const Edge& e, int side, double s, double lat) {
  samplePolyline(e.pl, s, tmp);
  const double u = side * lat;
  At& at = at_;
  at.x = tmp.x + tmp.tz * u;
  at.z = tmp.z - tmp.tx * u;
  at.y = world.edgeY(e, s, at.x, at.z) + KERB_H;
  return at;
}

void Crowd::put(Person& p, const Edge& e, int side, double s, double lat, bool kerb) {
  if (p.np >= MAXP) return;
  samplePolyline(e.pl, s, tmp);
  const double u = side * lat;
  const double x = tmp.x + tmp.tz * u;
  const double z = tmp.z - tmp.tx * u;
  const int k = p.np * 3;
  p.path[k] = x;
  p.path[k + 1] = world.edgeY(e, s, x, z) + (kerb ? KERB_H : 0);
  p.path[k + 2] = z;
  p.np++;
}

void Crowd::putXYZ(Person& p, double x, double y, double z) {
  if (p.np >= MAXP) return;
  const int k = p.np * 3;
  p.path[k] = x;
  p.path[k + 1] = y;
  p.path[k + 2] = z;
  p.np++;
}

std::optional<Crowd::PavementAt> Crowd::samplePavement(double x, double z, double r) {
  Edge* pick = nullptr;
  double total = 0;
  world.edgesInBox(x - r, z - r, x + r, z + r, [&](Edge& e) {
    const int sides = (pavement(e, 1) > 0 ? 1 : 0) + (pavement(e, -1) > 0 ? 1 : 0);
    if (sides == 0 || endOf(e) - startOf(e) < 4) return;
    const double w = busyness(e) * (sides == 2 ? 1 : 0.6);
    if (w <= 0) return;
    total += w;
    if (rnd() * total < w) pick = &e;
  });
  Edge* e = pick;
  if (!e) return std::nullopt;
  // (now and then nobody: a quiet street keeps quiet)
  if (rnd() > 0.35 + 0.65 * busyness(*e)) return std::nullopt;
  int side = pavement(*e, 1) > 0 ? 1 : -1;
  if (pavement(*e, -side) > 0 && rnd() < 0.5) side = -side;
  projectOnPolyline(e->pl, x, z, proj);
  const double lo = startOf(*e) + 1;
  const double hi = endOf(*e) - 1;
  const double s = clamp(proj.s + (rnd() - 0.5) * 2 * r, lo, hi);
  return PavementAt{e, side, s, pavement(*e, side), false};
}

std::optional<Crowd::PavementAt> Crowd::nearestPavement(double x, double z, double r) {
  Edge* best = nullptr;
  double bd = r;
  double bs = 0;
  int bside = 1;
  bool road = false;
  world.edgesInBox(x - r, z - r, x + r, z + r, [&](Edge& e) {
    projectOnPolyline(e.pl, x, z, proj);
    const int side = proj.off >= 0 ? 1 : -1;
    const double w = pavement(e, side);
    if (w <= 0) return;
    const double half = e.width / 2;
    const double off = std::abs(proj.off);
    const double d = off < half ? half - off + 0.3 : off > half + w ? off - half - w : 0;
    if (d < bd && proj.s > startOf(e) - 0.5 && proj.s < endOf(e) + 0.5) {
      bd = d;
      best = &e;
      bs = proj.s;
      bside = side;
      road = off < half;
    }
  });
  Edge* e = best;
  if (!e) return std::nullopt;
  return PavementAt{e, bside, clamp(bs, startOf(*e), endOf(*e)), 0, road};
}

// ------------------------------------------------------------------------------------ where to
void Crowd::setGoal(Person& p, int kind, Edge& e, int side, double s) {
  p.goal = kind;
  p.gEdge = &e;
  p.gSide = side;
  p.gS = clamp(s, startOf(e), endOf(e));
  const At& at = pointOn(e, side, p.gS, e.width / 2 + 0.5 * pavement(e, side));
  p.gx = at.x;
  p.gz = at.z;
}

void Crowd::newGoal(Person& p) {
  p.goals--;
  const double x = p.bx;
  const double z = p.bz;
  const double r = rnd();
  // a stop of the trams and buses
  if (r < 0.22 && stops && p.life > 60) {
    const std::vector<const StopPlace*> places = stops->near(x, z, 280);
    int tries = 0;
    for (const StopPlace* place : places) {
      if (++tries > 6) break;
      const size_t pk = static_cast<size_t>(std::floor(rnd() * static_cast<double>(place->poles.size())));
      if (pk >= place->poles.size()) continue;
      const StopPole& pole = place->poles[pk];
      const double d = len(pole.x - x, pole.z - z);
      if (d < 30 || d > 280) continue;
      const std::optional<PavementAt> at = nearestPavement(pole.x, pole.z, 12);
      if (!at) continue;
      setGoal(p, GOAL::STOP, *at->edge, at->side, at->s);
      return;
    }
  }
  // across a street with a zebra crossing or a light near them: over it there (a learner meets them there)
  if (r < 0.45 && crossingGoal(p)) return;
  // somewhere further on: a shop, a house – or on and on (strolling)
  for (int k = 0; k < 3; k++) {
    const double a = std::atan2(p.roamZ, p.roamX) + (rnd() - 0.5) * 2.4;
    const double d = 60 + rnd() * 200;
    const std::optional<PavementAt> at = samplePavement(x + std::cos(a) * d, z + std::sin(a) * d, 30);
    if (!at) continue;
    const int kind = rnd() < 0.25 ? GOAL::STROLL : rnd() < 0.45 ? GOAL::HOME : GOAL::ERRAND;
    setGoal(p, kind, *at->edge, at->side, at->s);
    if (kind == GOAL::STROLL) p.goals = 1 + static_cast<int>(std::floor(rnd() * 2));
    return;
  }
  // nowhere found: home, on this street
  if (p.edge) setGoal(p, GOAL::HOME, *p.edge, p.side, p.s + p.dir * (10 + rnd() * 40));
}

bool Crowd::crossingGoal(Person& p) {
  const double x = p.bx;
  const double z = p.bz;
  Edge* pick = nullptr;
  const CrossingJson* at = nullptr;
  int n = 0;
  world.edgesInBox(x - 90, z - 90, x + 90, z + 90, [&](Edge& e) {
    if (pavement(e, 1) <= 0 || pavement(e, -1) <= 0) return;
    for (const CrossingJson& c : e.xings) {
      if (c.k != XING_ZEBRA && c.k != XING_SIGNAL) continue;
      n++;
      if (rnd() * n < 1) {
        pick = &e;
        at = &c;
      }
    }
  });
  Edge* e = pick;
  const CrossingJson* c = at;
  if (!e || !c) return false;
  // (the far side of it, seen from where they are)
  projectOnPolyline(e->pl, x, z, proj);
  const int side = proj.off >= 0 ? -1 : 1;
  const int kind = rnd() < 0.5 ? GOAL::HOME : GOAL::ERRAND;
  setGoal(p, kind, *e, side, c->s + (rnd() < 0.5 ? -1 : 1) * (4 + rnd() * 18));
  return true;
}

// ------------------------------------------------------------------------------------ the way: along a pavement
void Crowd::planAlong(Person& p, bool turned) {
  Edge* ep = p.edge;
  if (!ep) {
    p.state = "gone";
    return;
  }
  Edge& e = *ep;
  p.mode = MODE::WALK;
  p.modeT = 0;
  p.state = "walk";
  p.crossing = nullptr;
  p.waited = 0;
  p.hurryX = 1;
  const double lo = startOf(e);
  const double hi = endOf(e);
  const double s0 = clamp(p.s, lo, hi);
  double stop = p.dir > 0 ? hi : lo;
  int next = NEXT::JUNCTION;
  auto ahead = [&](double s) { return (s - s0) * p.dir > 0.4 && (stop - s) * p.dir > -0.01; };
  // tired of it: home, a little further on
  if (p.life <= 0 && p.goal != GOAL::HOME && p.goal != GOAL::STOP) setGoal(p, GOAL::HOME, e, p.side, s0 + p.dir * (6 + rnd() * 20));
  if (p.gEdge == &e) {
    if (p.gSide == p.side) {
      if (ahead(p.gS) || std::abs(p.gS - s0) <= 0.4) {
        stop = p.gS;
        next = NEXT::GOAL;
      } else if (!turned) {
        // it lies behind: the other way along the pavement
        p.dir = -p.dir;
        p.frac = keepRight(e, p.side, p.dir, &p);
        planAlong(p, true);
        return;
      }
    } else if (pavement(e, -p.side) > 0) {
      // over the street to it: at a crossing near it, else straight over where it is
      const CrossingJson* nearC = crossingNear(e, p.gS, 35);
      const CrossingJson* c = nearC && (ahead(nearC->s) || std::abs(nearC->s - s0) < 1) ? nearC : nullptr;
      // (at the crossing – somewhere along its stripes –, else where the goal is, or right here when it lies behind)
      double xs;
      if (c) xs = clamp(c->s + (rnd() - 0.5) * std::max(0.0, c->w - 1.4), lo, hi);
      else xs = ahead(p.gS) ? p.gS : clamp(s0 + p.dir * 1.5, lo, hi);
      if ((xs - s0) * p.dir >= -0.01) {
        stop = xs;
        next = NEXT::KERB;
        p.np = 0;
        p.pi = 0;
        along(p, e, p.side, s0, xs, e.width / 2 + p.frac * pavement(e, p.side));
        wait(p, e, p.side, xs, xs, c, nullptr, nullptr, false, p.gS >= xs ? 1 : -1);
        p.s = xs;
        p.next = next;
        bounds(p);
        return;
      }
    }
  }
  // a shop window on the way
  const bool wet = rain > 0.2;
  if (next == NEXT::JUNCTION && shopping(e) && !p.hurry && !wet && hour > 8 && hour < 20 && rnd() < 0.3 * p.idle) {
    const double ws = s0 + p.dir * (6 + rnd() * 40);
    if (ahead(ws)) {
      stop = ws;
      next = NEXT::WINDOW;
    }
  }
  p.np = 0;
  p.pi = 0;
  const double reached = along(p, e, p.side, s0, stop, e.width / 2 + p.frac * pavement(e, p.side));
  if (reached != stop) next = NEXT::PLAN;
  p.s = reached;
  p.next = next;
  bounds(p);
}

void Crowd::bounds(Person& p) {
  const Edge* e = p.edge;
  if (!e) return;
  const double w = pavement(*e, p.side);
  // (stepping to the right moves them outwards where the kerb is on their left)
  const double out = p.side * p.dir > 0 ? -1 : 1;
  const double a = (0.28 - p.frac * w) * out;
  const double b = (w - 0.28 - p.frac * w) * out;
  p.dmin = std::min({a, b, 0.0});
  p.dmax = std::max({a, b, 0.0});
}

double Crowd::along(Person& p, const Edge& e, int side, double s0, double s1, double lat) {
  const std::vector<double>& cum = e.pl.cum;
  const int n = static_cast<int>(cum.size());
  const int dir = s1 >= s0 ? 1 : -1;
  // the bends between, then the end
  int k = 0;
  int lo = 0;
  int hi = n - 1;
  while (lo < hi) {
    const int mid = (lo + hi) >> 1;
    if (cum[mid] < s0) lo = mid + 1;
    else hi = mid;
  }
  k = lo;
  if (dir < 0) k = std::min(n - 1, k) - (cum[std::min(n - 1, k)] >= s0 ? 1 : 0);
  for (; k >= 0 && k < n; k += dir) {
    const double s = cum[k];
    if ((s - s0) * dir <= 0.3) continue;
    if ((s1 - s) * dir <= 0.3) break;
    if (p.np >= MAXP - 1) {
      // (more bends than room: on from here later)
      return s;
    }
    put(p, e, side, s, lat, true);
  }
  put(p, e, side, s1, lat, true);
  return s1;
}

const CrossingJson* Crowd::crossingNear(const Edge& e, double s, double range) {
  const CrossingJson* best = nullptr;
  double bd = range;
  for (const CrossingJson& c : e.xings) {
    if (c.k != XING_ZEBRA && c.k != XING_SIGNAL && c.k != XING_UNMARKED) continue;
    const double d = std::abs(c.s - s);
    if (d < bd) {
      bd = d;
      best = &c;
    }
  }
  return best;
}

// ------------------------------------------------------------------------------------ the way: at a junction
void Crowd::decide(Person& p) {
  Edge& e = *p.edge;
  Node& N = *(p.dir > 0 ? e.b : e.a);
  Arm* A = p.dir > 0 ? e.armB : e.armA;
  const int n = static_cast<int>(N.arms.size());
  if (N.stub || !A || A->idx >= n || N.arms[A->idx] != A) {
    turnRound(p);
    return;
  }
  const int i = A->idx;
  const int sg = edgeSide(*A, p.side);
  const std::vector<Arm*>& arms = N.arms;
  // where they want to go, seen from the junction
  double gx = p.roamX;
  double gz = p.roamZ;
  if (p.gEdge) {
    const double dx = p.gx - N.x;
    const double dz = p.gz - N.z;
    const double d = len(dx, dz);
    if (d > 2) {
      gx = dx / d;
      gz = dz / d;
    }
  }
  const double cost = p.bold ? 0.15 : p.age == AGE::OLD ? 0.5 : 0.3;
  auto ok = [&](int k, int s) { return pavement(*arms[k]->edge, edgeSide(*arms[k], s)) > 0; };
  auto score = [&](int k, int s, int crossings, bool back) {
    const Arm& L = *arms[k];
    double v = L.dx * gx + L.dz * gz;
    if (p.gEdge == L.edge) v += edgeSide(L, s) == p.gSide ? 1.6 : 0.7;
    v -= crossings * cost;
    if (back && n > 1) v -= 1.4;
    return v + (rnd() - 0.5) * 0.45;
  };
  // (round a corner from (k, s): the arm and side one comes to)
  auto cornerK = [&](int k, int s) { return s > 0 ? (k + 1) % n : (k - 1 + n) % n; };
  std::vector<int> best = {LEAVE, i, sg};
  double bestV = score(i, sg, 0, true);
  auto offer = [&](double v, std::vector<int> moves) {
    if (v > bestV) {
      bestV = v;
      best = std::move(moves);
    }
  };
  if (ok(i, -sg)) offer(score(i, -sg, 1, true), {CROSS, i, sg, LEAVE, i, -sg});
  if (n >= 2) {
    const int j = cornerK(i, sg);
    const int sj = -sg;
    if (j != i && ok(j, sj)) {
      offer(score(j, sj, 0, false), {CORNER, i, sg, LEAVE, j, sj});
      if (ok(j, -sj)) {
        offer(score(j, -sj, 1, false), {CORNER, i, sg, CROSS, j, sj, LEAVE, j, -sj});
        const int k2 = cornerK(j, -sj);
        if (k2 != i && k2 != j && ok(k2, sj)) offer(score(k2, sj, 1, false), {CORNER, i, sg, CROSS, j, sj, CORNER, j, -sj, LEAVE, k2, sj});
      }
    }
    if (ok(i, -sg)) {
      const int k3 = cornerK(i, -sg);
      if (k3 != i && ok(k3, sg)) offer(score(k3, sg, 1, false), {CROSS, i, sg, CORNER, i, -sg, LEAVE, k3, sg});
    }
  }
  p.jnode = &N;
  p.jarms = n;
  p.moves = best;
  p.mi = 0;
  p.mArm = i;
  p.mSide = sg;
  p.s = mouthS(*A);
  nextMove(p);
}

void Crowd::nextMove(Person& p) {
  Node* N = p.jnode;
  if (!N || static_cast<int>(N->arms.size()) != p.jarms || p.mi + 2 >= static_cast<int>(p.moves.size())) {
    // (the junction changed under them – a piece of the map came: on from where they are)
    turnRound(p);
    return;
  }
  const int code = p.moves[p.mi];
  const int k = p.moves[p.mi + 1];
  const int sg = p.moves[p.mi + 2];
  p.mi += 3;
  Arm& arm = *N->arms[k];
  if (code == CORNER) corner(p, *N, k, sg);
  else if (code == CROSS) crossArm(p, *N, arm, sg);
  else {
    p.edge = arm.edge;
    p.side = edgeSide(arm, sg);
    p.dir = arm.out ? 1 : -1;
    p.frac = keepRight(*arm.edge, p.side, p.dir, &p);
    p.jnode = nullptr;
    p.moves.clear();
    p.mi = 0;
    planAlong(p);
  }
}

void Crowd::corner(Person& p, Node& N, int k, int sg) {
  const int n = static_cast<int>(N.arms.size());
  const int ci = sg > 0 ? k : (k - 1 + n) % n;
  const int j = sg > 0 ? (k + 1) % n : (k - 1 + n) % n;
  Arm& to = *N.arms[j];
  const int sj = -sg;
  p.np = 0;
  p.pi = 0;
  // on to the pavement line of the next arm, where its pavement begins
  const int side = edgeSide(to, sj);
  const int dir = to.out ? 1 : -1;
  const double frac = keepRight(*to.edge, side, dir, &p);
  const double s = mouthS(to);
  const double lat = to.edge->width / 2 + frac * pavement(*to.edge, side);
  const JunctionCorner* c = N.shape && ci < static_cast<int>(N.shape->corners.size()) ? &N.shape->corners[ci] : nullptr;
  if (c && c->kerb.size() >= 4) {
    // along the kerb of the corner, on the pavement behind it: the kerb runs from arm ci's left mouth to
    // the next arm's right mouth, the pavement lies to the right of it – only the points on the way from
    // here to the next arm's pavement (at a bend the straight kerb's ends may lie behind or beyond)
    samplePolyline(to.edge->pl, s, tmp);
    const double ex = tmp.x + tmp.tz * side * lat;
    const double ez = tmp.z - tmp.tx * side * lat;
    const double dl = len(ex - p.bx, ez - p.bz);
    const double ux = dl > 1e-6 ? (ex - p.bx) / dl : 0;
    const double uz = dl > 1e-6 ? (ez - p.bz) / dl : 0;
    const std::vector<double>& K = c->kerb;
    const int m = static_cast<int>(K.size() / 2);
    const bool forward = sg > 0;
    const int stepK = std::max(1, static_cast<int>(std::ceil(static_cast<double>(m) / (MAXP - 2))));
    double done = 0.3;
    if (m == 2) {
      // (a straight kerb between the mouths – a bend: round the outside of its two roads, where the lines
      // of their pavements meet – unless they run on in one line, the side of a junction without a corner)
      const Arm& arm = *N.arms[k];
      // here + u·t1 = there + v·t2, both lines running in towards the junction
      const double ux2 = -arm.dx;
      const double uz2 = -arm.dz;
      const double vx = -to.dx;
      const double vz = -to.dz;
      const double det = vx * uz2 - ux2 * vz;
      if (std::abs(det) > 0.25) {
        const double qx = ex - p.bx;
        const double qz = ez - p.bz;
        const double t1 = (vx * qz - qx * vz) / det;
        const double t2 = (ux2 * qz - uz2 * qx) / det;
        if (t1 > -0.3 && t2 > -0.3 && t1 < 25 && t2 < 25) {
          const double x = p.bx + ux2 * t1;
          const double z = p.bz + uz2 * t1;
          putXYZ(p, x, world.planeY(N, x, z) + KERB_H, z);
          done = dl;
        }
      }
    }
    for (int t = 0; t < m; t += stepK) {
      const int idx = forward ? t : m - 1 - t;
      const int i0 = std::max(0, idx - 1);
      const int i1 = std::min(m - 1, idx + 1);
      const double tx = K[i1 * 2] - K[i0 * 2];
      const double tz = K[i1 * 2 + 1] - K[i0 * 2 + 1];
      double tl = len(tx, tz);
      if (tl == 0) tl = 1;
      const double w = c->swA + (c->swB - c->swA) * (idx / static_cast<double>(std::max(1, m - 1)));
      const double off = std::max(0.4, w * 0.5);
      const double x = K[idx * 2] + (-tz / tl) * off;
      const double z = K[idx * 2 + 1] + (tx / tl) * off;
      const double alongT = (x - p.bx) * ux + (z - p.bz) * uz;
      if (alongT <= done || alongT >= dl - 0.3) continue;
      done = alongT;
      putXYZ(p, x, world.planeY(N, x, z) + KERB_H, z);
    }
  }
  put(p, *to.edge, side, s, lat, true);
  p.frac = frac;
  p.mArm = j;
  p.mSide = sj;
  p.s = s;
  p.next = NEXT::MOVE;
  p.mode = MODE::WALK;
  p.dmin = -0.3;
  p.dmax = 0.3;
}

void Crowd::crossArm(Person& p, Node& N, Arm& arm, int sg) {
  Edge& e = *arm.edge;
  const int side = edgeSide(arm, sg);
  const double half = e.width / 2;
  const CrossingJson* c = nullptr;
  double cd = 25;
  for (const CrossingJson& x : e.xings) {
    if (x.k != XING_ZEBRA && x.k != XING_SIGNAL && x.k != XING_UNMARKED) continue;
    const double d = arm.out ? x.s : e.len - x.s;
    if (d > -1 && d < cd) {
      cd = d;
      c = &x;
    }
  }
  // the move after it: away along this same street on the other side – then there is no walking back to the mouth
  const bool after = p.mi + 2 < static_cast<int>(p.moves.size()) && p.moves[p.mi] == LEAVE && N.arms[p.moves[p.mi + 1]] == &arm;
  p.np = 0;
  p.pi = 0;
  double xs;
  double ws;
  if (c) {
    xs = ws = clamp(c->s + (rnd() - 0.5) * std::max(0.0, c->w - 1.4), startOf(e), endOf(e));
    along(p, e, side, p.s, xs, half + p.frac * pavement(e, side));
  } else if (N.arms.size() >= 3) {
    // at a junction: over just inside its mouth – clear of a car that waits at its line, and of one that
    // turns in and stops for them – waiting where the pavement ends
    ws = mouthS(arm);
    const double d = std::max(arm.cut - 0.6, std::min(arm.cut, 0.5));
    xs = arm.out ? d : e.len - d;
  } else {
    // at a bend or the end of a road: a little way along it
    const double d = std::min(arm.cut + 1, e.len / 2);
    xs = ws = arm.out ? d : e.len - d;
    along(p, e, side, p.s, ws, half + p.frac * pavement(e, side));
  }
  wait(p, e, side, xs, ws, c, &N, &arm, true, arm.out ? 1 : -1);
  p.xBack = !after;
  p.mArm = arm.idx;
  p.mSide = sg;
  p.next = NEXT::KERB;
  p.mode = MODE::WALK;
  p.dmin = -0.3;
  p.dmax = 0.3;
}

void Crowd::wait(Person& p, Edge& e, int side, double xs, double ws, const CrossingJson* c, Node* node, Arm* arm, bool moves, int after) {
  const double half = e.width / 2;
  p.xEdge = &e;
  p.xS = xs;
  p.xS2 = xs;
  p.xW = ws;
  p.xSide = side;
  p.xCross = c;
  p.xNode = node;
  p.xMoves = moves;
  p.spread = false;
  p.xAfter = after;
  p.xSc = -1;
  p.xG = 0;
  if (c && c->k == XING_ZEBRA) p.xRule = RULE::ZEBRA;
  else if (c && c->k == XING_SIGNAL && c->sc && !signals.isDark(*c->sc)) {
    p.xRule = RULE::LIGHT;
    p.xSc = *c->sc;
    p.xG = c->g ? *c->g : 0;
  } else if (!c && node && arm && node->ctrl == CTRL_SIGNALS && !node->dark && node->sc >= 0 && arm->sg >= 0 && !signals.isDark(node->sc)) {
    // a junction with lights and no crossing on the map: they go with the traffic beside them (while this arm has red)
    p.xRule = RULE::LIGHT;
    p.xSc = node->sc;
    p.xG = arm->sg;
  } else p.xRule = RULE::GAP;
  // the line across, kerb to kerb
  samplePolyline(e.pl, xs, tmp);
  p.kx = tmp.x + tmp.tz * side * half;
  p.kz = tmp.z - tmp.tx * side * half;
  p.fx = tmp.x - tmp.tz * side * half;
  p.fz = tmp.z + tmp.tx * side * half;
  double l = len(p.fx - p.kx, p.fz - p.kz);
  if (l == 0) l = 1;
  p.ax = (p.fx - p.kx) / l;
  p.az = (p.fz - p.kz) / l;
  // the place to wait, a little back from the kerb (beside the others there)
  put(p, e, side, ws, half + 0.3 + rnd() * 0.35, true);
}

void Crowd::turnRound(Person& p) {
  Edge* e = p.edge;
  p.jnode = nullptr;
  p.moves.clear();
  p.mi = 0;
  if (!e) {
    p.state = "gone";
    return;
  }
  p.dir = -p.dir;
  p.frac = keepRight(*e, p.side, p.dir, &p);
  planAlong(p, true);
}

// ------------------------------------------------------------------------------------ the way walked
void Crowd::arrive(Person& p) {
  const int next = p.next;
  p.next = NEXT::PLAN;
  switch (next) {
    case NEXT::JUNCTION:
      decide(p);
      break;
    case NEXT::MOVE:
      nextMove(p);
      break;
    case NEXT::KERB:
      atKerb(p);
      break;
    case NEXT::CROSSED:
      crossed(p);
      break;
    case NEXT::BACK:
      atKerb(p, true);
      break;
    case NEXT::GOAL:
      atGoal(p);
      break;
    case NEXT::WINDOW:
      atWindow(p);
      break;
    case NEXT::INTO: {
      // through the door: to the houses' side and in
      Edge& e = *p.edge;
      p.np = 0;
      p.pi = 0;
      put(p, e, p.side, p.s, e.width / 2 + pavement(e, p.side) + 0.6, true);
      p.next = NEXT::ENTER;
      p.mode = MODE::WALK;
      p.dmin = p.dmax = 0;
      break;
    }
    case NEXT::ENTER:
      p.state = "gone";
      break;
    case NEXT::LANDED:
      p.state = "walk";
      planAlong(p);
      break;
    default:
      planAlong(p);
  }
}

void Crowd::atGoal(Person& p) {
  Edge& e = *p.edge;
  if (p.goal == GOAL::STOP) {
    // at the stop: its people take them over – or they wait by it themselves
    if (toStop && toStop(p)) {
      p.state = "gone";
      for (Person* q : all) {
        if (q->leader != &p) continue;
        toStop(*q);
        q->state = "gone";
      }
      return;
    }
    p.goal = GOAL::ERRAND;
    p.gEdge = nullptr;
    stand(p, PAUSE::STOP, 30 + rnd() * 90, faceRoad(p));
    p.next = NEXT::PLAN;
    newGoal(p);
    return;
  }
  if (p.goal == GOAL::STROLL && p.goals > 0 && p.life > 0) {
    newGoal(p);
    planAlong(p);
    return;
  }
  // a shop or a house: to its door – a look into the window first, perhaps
  p.np = 0;
  p.pi = 0;
  put(p, e, p.side, p.s, e.width / 2 + std::max(0.5, pavement(e, p.side) - 0.35), true);
  p.enter = p.goal == GOAL::HOME || rnd() < 0.6 || p.life <= 0;
  p.next = p.goal == GOAL::HOME ? NEXT::INTO : NEXT::WINDOW;
  p.dmin = p.dmax = 0;
}

void Crowd::atWindow(Person& p) {
  Edge& e = *p.edge;
  const double w = pavement(e, p.side);
  const At at = pointOn(e, p.side, p.s, e.width / 2 + std::max(0.5, w - 0.35));
  if (len(at.x - p.bx, at.z - p.bz) > 0.6) {
    p.np = 0;
    p.pi = 0;
    putXYZ(p, at.x, at.y, at.z);
    p.next = NEXT::WINDOW;
    p.dmin = p.dmax = 0;
    return;
  }
  const bool enter = p.enter;
  p.enter = false;
  stand(p, PAUSE::WINDOW, 3 + rnd() * 9 * (0.5 + p.idle), faceRoad(p) + PED_PI);
  if (enter) p.next = NEXT::INTO;
  else {
    if (p.gEdge == &e && p.gSide == p.side && std::abs(p.gS - p.s) < 1) newGoal(p);
    p.next = NEXT::PLAN;
  }
}

double Crowd::faceRoad(Person& p) {
  const Edge& e = *p.edge;
  samplePolyline(e.pl, p.s, tmp);
  // (towards the middle of the street: against the side's outward direction)
  const double ox = -tmp.tz * p.side;
  const double oz = tmp.tx * p.side;
  return std::atan2(-ox, -oz);
}

void Crowd::stand(Person& p, int why, double seconds, double face) {
  p.mode = MODE::PAUSE;
  p.modeT = 0;
  p.pause = why;
  p.until = time + seconds;
  p.face = face;
  p.np = 0;
  p.pi = 0;
  p.speed = 0;
}

// ------------------------------------------------------------------------------------ crossing
void Crowd::atKerb(Person& p, bool back) {
  p.mode = MODE::KERB;
  p.modeT = back ? 1 : 0;
  p.lookT = 0;
  p.speed = 0;
  p.kback = 0;
  p.kcur = 0;
  p.hurryX = 1;
  p.sx = p.bx;
  p.sy = p.by;
  p.sz = p.bz;
  p.face = std::atan2(-p.ax, -p.az);
  p.wx = p.ax;
  p.wz = p.az;
  p.dodge = p.dodgeTo = 0;
  p.slow = 1;
  if (!back) {
    p.registered = false;
    p.pressed = false;
    p.redRun = p.bold && rnd() < 0.45;
    p.state = "walk";
    p.crossing = nullptr;
    p.waited = 0;
  }
  // at a light or in a gap they are someone waiting to cross from the start; at a zebra crossing once it is fair to the vehicles
  if (back || p.xRule != RULE::ZEBRA) registerAt(p);
}

void Crowd::registerAt(Person& p) {
  p.registered = true;
  p.crossing = p.xCross;
  p.state = "wait";
}

void Crowd::go(Person& p) {
  Edge& e = *p.xEdge;
  const double half = e.width / 2;
  double s2 = p.xS;
  // (a few cross on a slant towards where they are going, where there is no crossing)
  if (p.bold && p.xRule == RULE::GAP && !p.xCross && !p.xMoves) s2 = clamp(p.xS + p.xAfter * (2 + rnd() * 5), startOf(e), endOf(e));
  p.xS2 = s2 == p.xS ? p.xW : s2;
  // from where they stand (stepped back perhaps)
  p.bx = p.x;
  p.bz = p.z;
  p.np = 0;
  p.pi = 0;
  put(p, e, p.xSide, p.xS, half + 0.05, true);
  put(p, e, p.xSide, p.xS, half - 0.25, false);
  put(p, e, -p.xSide, s2, half - 0.25, false);
  put(p, e, -p.xSide, s2, half + 0.05, true);
  put(p, e, -p.xSide, p.xS2, half + 0.5, true);
  p.mode = MODE::CROSS;
  p.modeT = 0;
  p.slow = 1;
  p.state = "cross";
  p.crossing = p.xCross;
  p.registered = true;
  p.next = NEXT::CROSSED;
  p.dmin = -0.35;
  p.dmax = 0.35;
  p.dodgeTo = 0;
}

void Crowd::crossed(Person& p) {
  p.crossing = nullptr;
  p.state = "walk";
  p.waited = 0;
  p.registered = false;
  p.hurryX = 1;
  p.lookT = 0;
  Edge& e = *p.xEdge;
  if (p.xMoves && p.jnode) {
    p.mSide = -p.mSide;
    Arm* arm = p.mArm >= 0 && p.mArm < static_cast<int>(p.jnode->arms.size()) ? p.jnode->arms[p.mArm] : nullptr;
    if (p.xBack && arm && arm->edge == &e) {
      // back along the other pavement to the corner of the junction
      const int side = -p.xSide;
      const int dir = arm->out ? -1 : 1;
      p.frac = keepRight(e, side, dir, &p);
      p.np = 0;
      p.pi = 0;
      const double s = mouthS(*arm);
      along(p, e, side, p.xS2, s, e.width / 2 + p.frac * pavement(e, side));
      p.s = s;
      p.next = NEXT::MOVE;
      p.mode = MODE::WALK;
      p.dmin = -0.3;
      p.dmax = 0.3;
    } else {
      p.s = p.xS2;
      nextMove(p);
    }
    return;
  }
  p.edge = &e;
  p.side = -p.xSide;
  p.s = p.xS2;
  p.dir = p.xAfter;
  p.frac = keepRight(e, p.side, p.dir, &p);
  planAlong(p);
}

// ------------------------------------------------------------------------------------ a step
double Crowd::pace(Person& p) {
  double v = p.v0 * (1 + 0.25 * rain);
  if (hour < 6 || hour >= 22) v *= 1.06;
  if (p.mode == MODE::CROSS) v *= 1.12 * p.hurryX;
  // children: now a few quick steps, now slower
  if (p.age == AGE::CHILD) v *= 1 + 0.35 * std::max(0.0, std::sin(time * 0.9 + p.id * 1.7));
  return std::min(2.6, v) * p.slow;
}

void Crowd::step(Person& p, double dt) {
  p.modeT += dt;
  p.life -= dt;
  p.startle = std::max(0.0, p.startle - dt * 1.4);
  p.head += (p.headTo - p.head) * std::min(1.0, dt * 7);
  p.umbrella = p.brolly && rain > 0.12;
  switch (p.mode) {
    case MODE::KERB:
      atKerbStep(p, dt);
      break;
    case MODE::PAUSE:
      p.phase = -1;
      p.lean = 0;
      p.speed = 0;
      turnTo(p, p.face, dt, 4);
      if (p.pause == PAUSE::WINDOW) p.headTo = 0.35 * std::sin(p.modeT * 0.6 + p.id);
      else if (p.pause == PAUSE::CHAT) p.headTo = 0.12 * std::sin(p.modeT * 0.8 + p.id);
      else if (p.pause != PAUSE::START) p.headTo = 0.5 * std::sin(p.modeT * 0.23 + p.id) * std::sin(p.modeT * 0.07 + p.id * 3);
      if (time >= p.until) {
        p.mode = MODE::WALK;
        if (p.partner) {
          p.partner->partner = nullptr;
          p.partner = nullptr;
        }
        if (p.pi >= p.np) arrive(p);
      }
      break;
    default:
      walk(p, dt);
  }
}

void Crowd::spread(Person& p) {
  p.spread = true;
  const int k = (p.np - 1) * 3;
  const double y = p.path[k + 1];
  double x = p.path[k];
  double z = p.path[k + 2];
  for (int pass = 0; pass < 2; pass++) {
    for (Person* q : all) {
      if (q == &p || q->mode != MODE::KERB) continue;
      const double dx = x - q->sx;
      const double dz = z - q->sz;
      const double d = len(dx, dz);
      if (d >= 0.6) continue;
      // (along the kerb is across the way over: (−az, ax)) – beside them that way, or the other, where it is pavement still
      const double alongK = dx * -p.az + dz * p.ax;
      const double sgn = alongK >= 0 ? 1 : -1;
      for (const double s : {sgn * 0.62 - alongK, -sgn * 0.62 - alongK}) {
        const double nx = x - p.az * s;
        const double nz = z + p.ax * s;
        if (world.surfaceAt(nx, nz, y + 1, hit) && hit.zone == 0) continue;
        x = nx;
        z = nz;
        break;
      }
    }
  }
  p.path[k] = x;
  p.path[k + 2] = z;
}

void Crowd::walk(Person& p, double dt) {
  if (p.next == NEXT::KERB && !p.spread && p.pi == p.np - 1) {
    const int k = p.pi * 3;
    if (std::abs(p.path[k] - p.bx) + std::abs(p.path[k + 2] - p.bz) < 4) spread(p);
  }
  const double want = pace(p);
  p.speed += clamp(want - p.speed, -3 * dt, 1.5 * dt);
  if (p.speed < 0) p.speed = 0;
  double stepL = p.speed * dt;
  double moved = 0;
  while (stepL > 1e-6 && p.pi < p.np) {
    const int k = p.pi * 3;
    const double tx = p.path[k];
    const double ty = p.path[k + 1];
    const double tz = p.path[k + 2];
    const double dx = tx - p.bx;
    const double dz = tz - p.bz;
    const double d = len(dx, dz);
    if (d > 1e-4) {
      p.wx = dx / d;
      p.wz = dz / d;
    }
    if (d <= stepL) {
      p.bx = tx;
      p.by = ty;
      p.bz = tz;
      stepL -= d;
      moved += d;
      p.pi++;
    } else {
      const double f = stepL / d;
      p.bx += dx * f;
      p.by += (ty - p.by) * f;
      p.bz += dz * f;
      moved += stepL;
      stepL = 0;
    }
  }
  // (stepping aside: towards where they want to be – within what the way allows, quickly back inside it when the way changed)
  const double to = clamp(p.dodgeTo, p.dmin, p.dmax);
  const double rate = p.dodge < p.dmin - 0.02 || p.dodge > p.dmax + 0.02 ? 2.5 : 0.9;
  p.dodge += clamp(to - p.dodge, -rate * dt, rate * dt);
  p.x = p.bx - p.wz * p.dodge;
  p.y = p.by;
  p.z = p.bz + p.wx * p.dodge;
  if (moved > 1e-5) {
    p.walked += moved;
    p.phase = p.walked;
    turnTo(p, std::atan2(-p.wx, -p.wz), dt, 5);
  } else p.phase = -1;
  p.lean = p.phase >= 0 ? (p.hurry || p.hurryX > 1.2 ? 0.07 : 0) + 0.05 * rain : 0;
  // the head: looking out for the traffic on the road – the near half from the left, the far half from the right
  if (p.mode == MODE::CROSS) {
    const Edge* e = p.xEdge;
    const double across = e ? (p.x - p.kx) * p.ax + (p.z - p.kz) * p.az : 0;
    p.headTo = e && across < e->width / 2 ? 0.45 : -0.45;
  } else p.headTo = p.next == NEXT::KERB && p.np - p.pi <= 1 ? 0.5 : 0;
  if (p.pi >= p.np) arrive(p);
}

void Crowd::atKerbStep(Person& p, double dt) {
  p.lookT += dt;
  if (p.registered) p.waited += dt;
  // stepping back from the kerb (and forward again)
  const double to = p.kback;
  const double dk = clamp(to - p.kcur, -1.2 * dt, 1.6 * dt);
  p.kcur += dk;
  p.x = p.sx - p.ax * p.kcur;
  p.y = p.sy;
  p.z = p.sz - p.az * p.kcur;
  if (std::abs(dk) > 1e-5) {
    p.walked += std::abs(dk);
    p.phase = p.walked;
  } else p.phase = -1;
  if (p.kback > 0 && p.modeT > 1.5 && p.startle < 0.1) p.kback = 0;
  p.lean = 0;
  p.speed = 0;
  turnTo(p, p.face, dt, 5);
  // the head: at a light towards the light (with a glance at the traffic); elsewhere left, right, left – then now and then
  const double t = p.lookT;
  if (p.xRule == RULE::LIGHT) p.headTo = t < 0.6 ? 0.6 : 0.12 * std::sin(t * 0.7 + p.id) + (std::sin(t * 0.31 + p.id) > 0.85 ? 0.6 : 0);
  else p.headTo = t < 0.7 ? 0.85 : t < 1.5 ? -0.85 : t < 2.1 ? 0.75 : 0.55 * std::sin((t - 2.1) * 1.1 + p.id);
}

void Crowd::turnTo(Person& p, double yaw, double dt, double rate) { p.yaw += clamp(wrapAngle(yaw - p.yaw), -rate * dt, rate * dt); }

void Crowd::follow(Person& f, double dt) {
  const Person& L = *f.leader;
  f.modeT += dt;
  f.startle = std::max(0.0, f.startle - dt * 1.4);
  f.umbrella = f.brolly && rain > 0.12;
  const double fx = -std::sin(L.yaw);
  const double fz = -std::cos(L.yaw);
  const bool narrow = L.edge ? pavement(*L.edge, L.side) < 2.1 : false;
  const bool moving = L.phase >= 0;
  double lat = f.lat;
  double fwd = f.fwd;
  if (narrow && moving && L.mode == MODE::WALK) {
    lat = 0;
    fwd = f.lat > 0 ? -0.95 : -1.9;
  }
  if (f.age == AGE::CHILD) lat += 0.2 * std::sin(time * 0.7 + f.id);
  // (right of the leader's nose (fx, fz) is (−fz, fx))
  const double tx = L.x - fz * lat + fx * fwd;
  const double tz = L.z + fx * lat + fz * fwd;
  const double dx = tx - f.x;
  const double dz = tz - f.z;
  const double d = len(dx, dz);
  const double vmax = std::max(L.speed, 0.4) * (d > 1.5 ? 1.5 : 1.15) + 0.2;
  const double stepL = std::min(d, vmax * dt);
  if (d > 0.03 && stepL > 1e-4) {
    f.x += (dx / d) * stepL;
    f.z += (dz / d) * stepL;
    f.walked += stepL;
    f.phase = f.walked;
    f.speed = stepL / dt;
    turnTo(f, d > 0.15 ? std::atan2(-dx, -dz) : L.yaw, dt, 5);
  } else {
    f.phase = -1;
    f.speed = 0;
    turnTo(f, L.yaw, dt, 4);
  }
  f.y += (L.y - f.y) * std::min(1.0, dt * 6);
  f.state = L.state;
  f.crossing = L.crossing;
  f.waited = L.waited;
  f.mode = MODE::FOLLOW;
  f.headTo = L.headTo * 0.8;
  f.head += (f.headTo - f.head) * std::min(1.0, dt * 6);
  f.lean = L.lean;
}

// ------------------------------------------------------------------------------------ looking round
void Crowd::look(Person& p) {
  if (p.leader) {
    // (with somebody: they look at the cars all the same)
    if (std::abs(p.x - p.leader->x) + std::abs(p.z - p.leader->z) > 8) p.leader = nullptr;
    else {
      flinch(p);
      return;
    }
    if (!p.leader) {
      // lost their companion: on by themselves, from the nearest pavement
      const std::optional<PavementAt> at = nearestPavement(p.x, p.z, 10);
      if (!at) {
        p.state = "gone";
        return;
      }
      p.mode = MODE::WALK;
      onPavement(p, *at->edge, at->side, at->s, rnd() < 0.5 ? 1 : -1, false);
      p.bx = p.x;
      p.by = p.y;
      p.bz = p.z;
      newGoal(p);
      planAlong(p);
      return;
    }
  }
  switch (p.mode) {
    case MODE::WALK:
      if (!flinch(p) && p.mode == MODE::WALK) avoid(p);
      break;
    case MODE::KERB:
      flinch(p);
      kerbLook(p);
      break;
    case MODE::CROSS:
      crossLook(p);
      break;
    default:
      if (p.pause == PAUSE::STOP || p.pause == PAUSE::WINDOW) flinch(p);
  }
}

void Crowd::avoid(Person& p) {
  const double wx = p.wx;
  const double wz = p.wz;
  // (right of the way (wx, wz) is (−wz, wx))
  const double rx = -wz;
  const double rz = wx;
  const double v = std::max(p.speed, 0.7);
  double nearest = 4.2;
  double want = 0;
  double slow = 1;
  bool found = false;
  auto consider = [&](double qx, double qz, double qvx, double qvz, double clear, double range) {
    const double dx = qx - p.x;
    const double dz = qz - p.z;
    const double ahead = dx * wx + dz * wz;
    const bool fast = qvx * qvx + qvz * qvz > 9;
    // (a cyclist comes up from behind as well)
    if (ahead < (fast ? -6 : -0.3) || ahead > range) return;
    const double lat = dx * rx + dz * rz;
    const double qv = qvx * wx + qvz * wz;
    const double closing = ahead >= 0 ? v - qv : qv - v;
    if (ahead > 0.25 && closing < 0.08 && std::abs(lat) > 0.3) return;
    if (std::abs(lat) >= clear) return;
    const double dist = std::abs(ahead);
    if (dist >= nearest) return;
    nearest = dist;
    found = true;
    // where they will be, from the middle of my way: pass them on the right if they come towards me, on the left if I overtake
    const double q = lat + p.dodge;
    const double toRight = q + clear;
    const double toLeft = q - clear;
    const bool oncoming = qv < -0.2;
    const double first = oncoming || ahead < 0 ? toRight : qv > 0.2 ? toLeft : std::abs(toRight - p.dodge) < std::abs(toLeft - p.dodge) ? toRight : toLeft;
    const double second = first == toRight ? toLeft : toRight;
    if (first >= p.dmin && first <= p.dmax) want = first;
    else if (second >= p.dmin && second <= p.dmax) want = second;
    else {
      // no room to pass: behind them, at their pace – or wait
      want = clamp(p.dodge, p.dmin, p.dmax);
      slow = ahead > 0 && qv > 0.3 ? clamp(qv / v, 0.2, 1) : ahead > 0 ? 0 : 1;
    }
  };
  // the people near
  const int ix = static_cast<int>(std::floor(p.x / 4));
  const int iz = static_cast<int>(std::floor(p.z / 4));
  for (int cx = ix - 1; cx <= ix + 1; cx++) {
    for (int cz = iz - 1; cz <= iz + 1; cz++) {
      for (int k = cellHead[cellKey(cx, cz)]; k >= 0; k = cellNext[k]) {
        Person* q = k < static_cast<int>(all.size()) ? all[k] : nullptr;
        if (!q || q == &p || q->leader == &p || p.leader == q || q->state == "gone") continue;
        const bool moving = q->phase >= 0;
        const double qvx = moving ? -std::sin(q->yaw) * q->speed : 0;
        const double qvz = moving ? -std::cos(q->yaw) * q->speed : 0;
        consider(q->x, q->z, qvx, qvz, 0.68, 3.6);
        // two who know each other: a chat
        if (!moving || p.partner || q->partner || q->mode != MODE::WALK || q->leader) continue;
        const double d2 = (q->x - p.x) * (q->x - p.x) + (q->z - p.z) * (q->z - p.z);
        if (d2 < 4 && (-std::sin(q->yaw) * wx - std::cos(q->yaw) * wz) < -0.5 && chatty(p, *q)) {
          const double face = std::atan2(-(q->x - p.x), -(q->z - p.z));
          const double t = 8 + 22 * hash01(p.id * 131 + q->id);
          p.partner = q;
          q->partner = &p;
          pauseHere(p, PAUSE::CHAT, t, face);
          pauseHere(*q, PAUSE::CHAT, t, face + PED_PI);
          return;
        }
      }
    }
  }
  // those waiting at the stops
  for (int cx = ix - 1; cx <= ix + 1; cx++) {
    for (int cz = iz - 1; cz <= iz + 1; cz++) {
      for (int k = otherHead[cellKey(cx, cz)]; k >= 0; k = otherNext[k]) {
        if (k < static_cast<int>(others.size())) consider(others[k].x, others[k].z, 0, 0, 0.62, 3.6);
      }
    }
  }
  // the player on foot
  if (const PlayerState* pl = player) consider(pl->x, pl->z, -std::sin(pl->yaw) * pl->speed, -std::cos(pl->yaw) * pl->speed, 0.75, 3.6);
  // cyclists on the pavement's cycle strip
  if (cyclists) {
    for (const Cyclist* c : *cyclists) {
      if (std::abs(c->x - p.x) > 9 || std::abs(c->z - p.z) > 9) continue;
      consider(c->x, c->z, -std::sin(c->yaw) * c->v, -std::cos(c->yaw) * c->v, 1.0, 8);
    }
  }
  if (!found) {
    want = 0;
    // children wander from side to side; the old stop for a breath now and then
    if (p.age == AGE::CHILD) want = clamp(0.35 * std::sin(time * 0.8 + p.id), p.dmin, p.dmax);
    else if (p.age == AGE::OLD && rnd() < 0.004) {
      pauseHere(p, PAUSE::REST, 3 + rnd() * 5, p.yaw);
      return;
    }
  }
  // posts, walls, a vehicle standing on the pavement: a little aside, where it is free
  if (time > p.ghost && inWay(p, want)) {
    double free = std::nan("");
    for (const double d : ASIDE) {
      const double c = want + d;
      if (c < p.dmin || c > p.dmax) continue;
      if (!inWay(p, c)) {
        free = c;
        break;
      }
    }
    if (std::isnan(free)) {
      slow = std::min(slow, 0.25);
      p.stuck += THINK;
      // (stuck: through it after all, rather than stand there for ever)
      if (p.stuck > 2.5) {
        p.ghost = time + 4;
        p.stuck = 0;
      }
    } else {
      want = free;
      p.stuck = 0;
    }
  } else if (slow > 0.5) p.stuck = 0;
  else {
    p.stuck += THINK;
    if (p.stuck > 6) {
      // waited behind somebody long enough: past them, however close
      p.ghost = time + 3;
      slow = 1;
      p.stuck = 0;
    }
  }
  p.dodgeTo = want;
  p.slow = slow;
}

bool Crowd::chatty(const Person& p, const Person& q) {
  if (p.hurry || q.hurry || rain > 0.15 || hour < 7 || hour >= 21) return false;
  if (p.age == AGE::CHILD || q.age == AGE::CHILD) return false;
  return hash01(std::min(p.id, q.id) * 7919.0 + std::max(p.id, q.id)) < 0.1 * (p.idle + q.idle);
}

void Crowd::pauseHere(Person& p, int why, double seconds, double face) {
  p.mode = MODE::PAUSE;
  p.modeT = 0;
  p.pause = why;
  p.until = time + seconds;
  p.face = face;
  p.speed = 0;
  p.phase = -1;
}

bool Crowd::inWay(Person& p, double d) {
  const double rx = -p.wz;
  const double rz = p.wx;
  for (const double ahead : PROBE) {
    const double x = p.bx + p.wx * ahead + rx * d;
    const double z = p.bz + p.wz * ahead + rz * d;
    if (blocked && blocked(x, p.y, z, 0.24)) return true;
    const int no = obstacles ? static_cast<int>(obstacles->size()) : 0;
    for (int k = -1; k < no; k++) {
      const PlayerState* o = k < 0 ? playerCar : (*obstacles)[k];
      if (!o) continue;
      if (std::abs(o->x - x) > 12 || std::abs(o->z - z) > 12) continue;
      if (std::abs(o->speed) > 0.5) continue;
      const double fx = -std::sin(o->yaw);
      const double fz = -std::cos(o->yaw);
      const double lf = (x - o->x) * fx + (z - o->z) * fz;
      const double lr = (x - o->x) * -fz + (z - o->z) * fx;
      if (std::abs(lf) < o->length / 2 + 0.3 && std::abs(lr) < o->width / 2 + 0.3) return true;
    }
  }
  return false;
}

bool Crowd::flinch(Person& p) {
  if (p.mode == MODE::WALK && p.next == NEXT::ENTER) return false;
  for (int i = 0; i < nVeh; i++) {
    const Veh& V = vehs[i];
    if (std::abs(V.x - p.x) > 14 || std::abs(V.z - p.z) > 14) continue;
    const double sp = std::abs(V.v);
    if (sp < 1.5) continue;
    const double dx = p.x - V.x;
    const double dz = p.z - V.z;
    const double lf = dx * V.fx + dz * V.fz;
    const double lr = dx * -V.fz + dz * V.fx;
    const double d = len(std::max(0.0, std::abs(lf) - V.len / 2), std::max(0.0, std::abs(lr) - V.wid / 2));
    const bool toward = V.v > 0 ? lf > -V.len / 2 : lf < V.len / 2;
    // (a car passing the kerb at an ordinary distance is nothing to start at – close, or close and fast, is)
    if (!(d < 0.6 || (toward && ((d < 1.0 && sp > 12) || (d < 0.8 && sp > 7))))) continue;
    const bool hard = d < 0.6;
    p.startle = std::max(p.startle, hard ? 1.0 : 0.6);
    p.headTo = clamp(wrapAngle(std::atan2(-(V.x - p.x), -(V.z - p.z)) - p.yaw), -1.3, 1.3);
    if (p.mode == MODE::KERB) {
      p.kback = std::min(1.1, p.kback + (hard ? 0.6 : 0.4));
      p.modeT = 0;
      p.lookT = 0;
    } else if (p.mode == MODE::WALK) {
      // away from the road
      if (p.edge) {
        const double out = p.side * p.dir > 0 ? -1 : 1;
        p.dodgeTo = clamp(p.dodge + out * 0.6, p.dmin, p.dmax);
      }
      if (hard) pauseHere(p, PAUSE::START, 0.6 + rnd() * 0.6, p.yaw);
    }
    return true;
  }
  return false;
}

void Crowd::kerbLook(Person& p) {
  if (p.kback > 0.05) return;
  const double need = p.age == AGE::OLD ? 2.4 : p.age == AGE::CHILD ? (p.bold ? 0.5 : 0.9) : p.bold ? 0.8 : 1.9;
  int rule = p.xRule;
  if (rule == RULE::LIGHT) {
    const std::string st = signals.pedestrian(p.xSc, p.xG);
    if (st == "dark") {
      p.xRule = rule = RULE::GAP;
    } else {
      // the button (lights of the map's crossings have one)
      if (!p.pressed && p.xCross && st == "red") {
        signals.request(p.xSc);
        p.pressed = true;
      }
      if (st == "green") {
        if (p.lookT > 0.5 && noneRunning(p) && !turningIn(p, 9) && !closeIn(p, 7)) go(p);
      } else if (p.redRun && p.modeT > 4 && clearToGo(p, p.margin + 4, 70)) go(p);
      return;
    }
  }
  if (rule == RULE::ZEBRA) {
    if (!p.registered) {
      // they come up to the kerb once every vehicle could still stop for them
      if (allCanStop(p)) registerAt(p);
      else {
        p.lookT = std::min(p.lookT, 0.7);
        return;
      }
    }
    if (p.lookT > need && zebraClear(p) && !closeIn(p, 6)) go(p);
    return;
  }
  // a gap: the longer they wait, the smaller one they take
  const double margin = p.margin * std::max(0.45, 1 - p.modeT / 70);
  if (p.lookT > need && clearToGo(p, margin, 0) && !turningIn(p, 20) && !closeIn(p, 10)) go(p);
}

void Crowd::crossLook(Person& p) {
  if (p.xRule == RULE::LIGHT && signals.pedestrian(p.xSc, p.xG) == "red") p.hurryX = std::max(p.hurryX, 1.45);
  const Edge* e = p.xEdge;
  if (!e || p.next != NEXT::CROSSED) return;
  const double across = (p.x - p.kx) * p.ax + (p.z - p.kz) * p.az;
  double danger = JS_INF;
  for (int i = 0; i < nVeh; i++) {
    const Veh& V = vehs[i];
    if (std::abs(V.x - p.x) > 50 || std::abs(V.z - p.z) > 50) continue;
    const double sp = std::abs(V.v);
    if (sp < 2) continue;
    const double hx = V.v > 0 ? V.fx : -V.fx;
    const double hz = V.v > 0 ? V.fz : -V.fz;
    const double dx = p.x - V.x;
    const double dz = p.z - V.z;
    const double lf = dx * hx + dz * hz;
    const double lr = dx * -hz + dz * hx;
    if (lf < 0 || std::abs(lr) > V.wid / 2 + 1) continue;
    const double room = lf - V.len / 2 - 1;
    // (braking hard enough to stop short of them: no danger)
    if (V.a < -0.8 && room > (sp * sp) / (2 * -V.a)) continue;
    danger = std::min(danger, room / sp);
  }
  if (danger > 2.2) return;
  p.startle = std::max(p.startle, 0.8);
  if (across < e->width * 0.3) {
    // just stepped out: back on to the kerb
    p.np = 0;
    p.pi = 0;
    putXYZ(p, p.sx, p.sy, p.sz);
    p.next = NEXT::BACK;
    p.hurryX = 1.5;
  } else p.hurryX = 1.6;
}

bool Crowd::allCanStop(Person& p) {
  for (int i = 0; i < nVeh; i++) {
    const Veh& V = vehs[i];
    const double sp = std::abs(V.v);
    if (sp < 1) continue;
    const double front = toLine(V, p, 2.5);
    if (front == JS_INF || front < -V.len) continue;
    if (front < sp * REACT + (sp * sp) / (2 * COMFORT) + 2) return false;
  }
  return true;
}

bool Crowd::zebraClear(Person& p) {
  const double vw = p.v0 * 1.12;
  for (int i = 0; i < nVeh; i++) {
    const Veh& V = vehs[i];
    const double front = toLine(V, p, 2.5);
    if (front == JS_INF || front < -(V.len + 1)) continue;
    const double sp = std::abs(V.v);
    if (front < 0.5 && !(sp < 0.3 && front > 0)) return false;
    if (sp < 0.6) continue;
    // near the crossing: once it has stopped (or all but); further off: once it is clearly stopping – or so far off that it does not matter
    if (front < 25) return false;
    const bool stopping = V.a < -0.5 && front > (sp * sp) / (2 * std::max(0.5, -V.a)) + 3;
    if (stopping) continue;
    if (front / sp > (lineLen(p) + 2) / vw + 2.5) continue;
    return false;
  }
  return true;
}

bool Crowd::noneRunning(Person& p) {
  for (int i = 0; i < nVeh; i++) {
    const Veh& V = vehs[i];
    const double front = toLine(V, p, 2.5);
    if (front == JS_INF || front < -(V.len + 1)) continue;
    const double sp = std::abs(V.v);
    if (front < 0.5 && sp > 0.3) return false;
    if (sp < 1) continue;
    if (front < sp * 0.4 + (sp * sp) / (2 * 6) + 1) return false;
  }
  return true;
}

bool Crowd::clearToGo(Person& p, double margin, double empty) {
  const double vw = p.v0 * 1.12;
  for (int i = 0; i < nVeh; i++) {
    const Veh& V = vehs[i];
    const double front = toLine(V, p, 2.5);
    if (front == JS_INF || front < -(V.len + 1.5)) continue;
    const double sp = std::abs(V.v);
    if (empty > 0 && front < empty) return false;
    // (one standing just short of the way over – at its line – they pass in front of: it waits for them then)
    if (front < 0.6 && !(sp < 0.3 && front > 0)) return false;
    if (sp < 0.5) continue;
    // where it meets the line: when they are past its way – it is to come after that
    const double leave = (lineAt + 1.3) / vw;
    if (front / sp > leave + margin && front > sp * REACT + (sp * sp) / (2 * COMFORT) + 2) continue;
    return false;
  }
  return true;
}

bool Crowd::turningIn(Person& p, double range) {
  const Edge* e = p.xEdge;
  if (!e) return false;
  const double mx = (p.kx + p.fx) / 2;
  const double mz = (p.kz + p.fz) / 2;
  for (int i = 0; i < nVeh; i++) {
    const Veh& V = vehs[i];
    if (V.into != e || std::abs(V.v) < 1) continue;
    if (len(V.x - mx, V.z - mz) < range + V.len / 2) return true;
  }
  return false;
}

bool Crowd::closeIn(Person& p, double range) {
  const double L = lineLen(p);
  for (int i = 0; i < nVeh; i++) {
    const Veh& V = vehs[i];
    const double sp = std::abs(V.v);
    if (sp < 1.5 || std::abs(V.x - p.kx) > range + 20 || std::abs(V.z - p.kz) > range + 20) continue;
    const double t = clamp((V.x - p.kx) * p.ax + (V.z - p.kz) * p.az, 0, L);
    const double dx = p.kx + p.ax * t - V.x;
    const double dz = p.kz + p.az * t - V.z;
    const double d = len(dx, dz);
    if (d - V.len / 2 > range) continue;
    const double hx = V.v > 0 ? V.fx : -V.fx;
    const double hz = V.v > 0 ? V.fz : -V.fz;
    if (((dx * hx + dz * hz) / std::max(d, 0.1)) * sp > 1) return true;
  }
  return false;
}

double Crowd::lineLen(const Person& p) const { return len(p.fx - p.kx, p.fz - p.kz); }

double Crowd::toLine(const Veh& V, const Person& p, double reach) {
  if (std::abs(V.x - p.kx) > 140 || std::abs(V.z - p.kz) > 140) return JS_INF;
  const double ux = p.ax;
  const double uz = p.az;
  // (the line's normal: along the road)
  const double nx = -uz;
  const double nz = ux;
  const double hx = V.v >= 0 ? V.fx : -V.fx;
  const double hz = V.v >= 0 ? V.fz : -V.fz;
  const double dn = hx * nx + hz * nz;
  const double rel = (V.x - p.kx) * nx + (V.z - p.kz) * nz;
  const double L = lineLen(p);
  if (std::abs(dn) < 0.25) {
    // along the line: in the way only if it stands on it
    const double q = (V.x - p.kx) * ux + (V.z - p.kz) * uz;
    if (std::abs(rel) < V.wid / 2 + 1 && q > -V.len / 2 - 1 && q < L + V.len / 2 + 1) {
      lineAt = clamp(q, 0, L);
      return -0.1;
    }
    return JS_INF;
  }
  const double t = -rel / dn;
  const double qx = V.x + hx * t;
  const double qz = V.z + hz * t;
  const double q = (qx - p.kx) * ux + (qz - p.kz) * uz;
  if (q < -reach - V.wid / 2 || q > L + reach + V.wid / 2) return JS_INF;
  lineAt = clamp(q, 0, L);
  return t - V.len / 2 - 0.8;
}

void Crowd::endanger(Person& p, const PlayerState& pl, std::vector<TrafficEvent>& events) {
  const double sp = std::abs(pl.speed);
  if (sp < 1.2) return;
  const double dx = p.x - pl.x;
  const double dz = p.z - pl.z;
  const double r = pl.length / 2 + 3;
  if (dx * dx + dz * dz > r * r) return;
  const double fx = -std::sin(pl.yaw);
  const double fz = -std::cos(pl.yaw);
  const double lf = dx * fx + dz * fz;
  const double lr = dx * -fz + dz * fx;
  const double d = len(std::max(0.0, std::abs(lf) - pl.length / 2), std::max(0.0, std::abs(lr) - pl.width / 2));
  const bool onRoadNow = p.state == "cross";
  if (!((onRoadNow && d < 1.0) || d < 0.2)) return;
  if (time - p.told > 4) {
    p.told = time;
    TrafficEvent ev;
    ev.type = "pedestrianEndangered";
    ev.ped = &p;
    events.push_back(ev);
  }
  p.startle = 1;
  if (d < 0.3) {
    // out of its way, to the side they are on
    const double push = pl.width / 2 + 0.4 - std::abs(lr);
    if (push > 0) {
      const double sgn = lr >= 0 ? 1 : -1;
      p.x += -fz * push * sgn;
      p.z += fx * push * sgn;
      if (!p.leader && p.mode != MODE::KERB) {
        p.bx += -fz * push * sgn;
        p.bz += fx * push * sgn;
      }
    }
  }
}

void Crowd::honk(const PlayerState& pl) {
  const double fx = -std::sin(pl.yaw);
  const double fz = -std::cos(pl.yaw);
  for (Person* pp : all) {
    Person& p = *pp;
    const double dx = p.x - pl.x;
    const double dz = p.z - pl.z;
    const double d = len(dx, dz);
    if (d > 38) continue;
    const double ahead = dx * fx + dz * fz;
    if (d > 12 && ahead < 0) continue;
    p.startle = std::max(p.startle, d < 22 ? 1.0 : 0.65);
    p.headTo = clamp(wrapAngle(std::atan2(dx, dz) - p.yaw), -1.3, 1.3);
    if (p.leader) continue;
    if (p.mode == MODE::CROSS) p.hurryX = 1.5;
    else if (p.mode == MODE::KERB) {
      p.kback = std::max(p.kback, 0.5);
      p.modeT = 0;
      p.lookT = 0;
    } else if (p.mode == MODE::WALK) pauseHere(p, PAUSE::START, 0.5 + rnd() * 0.6, p.yaw);
  }
}

// ------------------------------------------------------------------------------------ what is around
void Crowd::see(double dt, const PlayerState& pl, const std::vector<TrafficCar*>& cars, const std::vector<PlayerState*>& obs) {
  size_t n = 0;
  const size_t need = cars.size() + 1 + obs.size();
  if (vehs.size() < need) vehs.resize(need);
  auto get = [&]() -> Veh& { return vehs[n++]; };
  for (const TrafficCar* c : cars) {
    Veh& V = get();
    V.x = c->x;
    V.y = c->y;
    V.z = c->z;
    V.fx = -std::sin(c->yaw);
    V.fz = -std::cos(c->yaw);
    V.v = c->v;
    V.a = c->braking ? std::min(c->a, -1.0) : c->a;
    V.len = c->length;
    V.wid = c->width;
    // (the road it turns into: in the junction now, or close before it)
    const Connector* conn = c->conn;
    if (conn && conn->turn != 0) V.into = conn->to->edge;
    else if (c->next && c->next->turn != 0 && c->lane && c->lane->len - c->s < 25) V.into = c->next->to->edge;
    else V.into = nullptr;
  }
  // the player's vehicle
  playerVeh = nullptr;
  if (!pl.onFoot) {
    const double raw = (pl.speed - pv) / dt;
    pa += (clamp(raw, -12, 8) - pa) * std::min(1.0, dt * 5);
    Veh& V = get();
    V.x = pl.x;
    V.y = pl.y;
    V.z = pl.z;
    V.fx = -std::sin(pl.yaw);
    V.fz = -std::cos(pl.yaw);
    V.v = pl.speed;
    // (backwards: braking is speeding up the other way)
    V.a = pl.speed >= 0 ? pa : -pa;
    V.len = pl.length;
    V.wid = pl.width;
    V.into = nullptr;
    playerVeh = &V;
  } else pa = 0;
  pv = pl.speed;
  for (const PlayerState* o : obs) {
    Veh& V = get();
    V.x = o->x;
    V.y = o->y;
    V.z = o->z;
    V.fx = -std::sin(o->yaw);
    V.fz = -std::cos(o->yaw);
    V.v = o->speed;
    V.a = 0;
    V.len = o->length;
    V.wid = o->width;
    V.into = nullptr;
  }
  nVeh = static_cast<int>(n);
}

void Crowd::grid() {
  if (cellNext.size() < all.size()) cellNext.assign(all.size() * 2, -1);
  std::fill(cellHead.begin(), cellHead.end(), -1);
  for (size_t i = 0; i < all.size(); i++) {
    const Person* p = all[i];
    const int k = cellKey(static_cast<int>(std::floor(p->x / 4)), static_cast<int>(std::floor(p->z / 4)));
    cellNext[i] = cellHead[k];
    cellHead[k] = static_cast<int32_t>(i);
  }
  others = standing ? standing() : std::vector<Spot>{};
  if (otherNext.size() < others.size()) otherNext.assign(others.size() * 2, -1);
  std::fill(otherHead.begin(), otherHead.end(), -1);
  for (size_t i = 0; i < others.size(); i++) {
    const int k = cellKey(static_cast<int>(std::floor(others[i].x / 4)), static_cast<int>(std::floor(others[i].z / 4)));
    otherNext[i] = otherHead[k];
    otherHead[k] = static_cast<int32_t>(i);
  }
}

}  // namespace fs
