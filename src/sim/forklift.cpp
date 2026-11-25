// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/forklift.ts @ v1.7.7
#include "sim/forklift.h"

#include <cmath>

#include "core/js.h"
#include "sim/vehicle/fleet.h"

namespace fs {

namespace {

constexpr double DEG = 3.141592653589793 / 180;

ForkEvents newEvents() { return ForkEvents{}; }

double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double approach(double cur, double target, double step) { return std::abs(target - cur) <= step ? target : cur + jsSign(target - cur) * step; }

struct Moments {
  double net;
  double inertia;
  double ratio;
};

Moments pitchMoments(const Loading& l, double p, const ForkliftSpec& spec) {
  const double mt = spec.mass;
  const double c = std::cos(p);
  const double s = std::sin(p);
  const double over = l.mass * FORK_G * (l.ahead * c + l.height * s) + l.decel * (l.mass * l.height + mt * spec.cgHeight) * c;
  const double hold = mt * FORK_G * (spec.cgBehind * c - spec.cgHeight * s);
  const double inertia = mt * (spec.cgBehind * spec.cgBehind + spec.cgHeight * spec.cgHeight) + l.mass * (l.ahead * l.ahead + l.height * l.height) + 1800;
  return {over - hold, inertia, hold > 0 ? over / hold : 9};
}

Moments rollMoments(const Loading& l, double r, const ForkliftSpec& spec) {
  const double mt = spec.mass;
  const double M = mt + l.mass;
  const double xc = (mt * spec.cgBehind - l.mass * l.ahead) / M;
  const double yc = (mt * spec.cgHeight + l.mass * l.height) / M;
  const double b = spec.halfTrack * (1 - clamp(xc / spec.wheelbase, 0, 1)) + spec.axleStops;
  const double c = std::cos(r);
  const double s = std::sin(r);
  const double over = M * std::abs(l.lateral) * yc * c;
  const double hold = M * FORK_G * (b * c - yc * s);
  return {over - hold, M * (b * b + yc * yc) + 1500, hold > 0 ? over / hold : 9};
}

}  // namespace

const VehicleSpec FORKLIFT_SPEC = [] {
  VehicleSpec s = TRACTOR;
  s.name = "Gabelstapler 2,5 t";
  s.mass = FORKLIFT.mass;
  s.inertia = {3200, 3600, 2600};
  s.wheelbase = FORKLIFT.wheelbase;
  s.track = FORKLIFT.halfTrack * 2;
  s.cgHeight = FORKLIFT.cgHeight;
  s.cgToFront = FORKLIFT.cgBehind;
  s.wheelRadius = 0.33;
  s.wheelInertia = 4;
  // (what the steering keys and the touch bar go by – input/input.ts asks for less lock at speed the larger it is:
  // set so that, held down at full speed, they turn no harder than the curve speed control's lateral acceleration)
  s.maxWheelAngle = 2.6;
  s.steeringRatio = 6;
  s.length = 3.75;
  s.width = 1.16;
  s.rearDrive = false;
  s.bodyPoints = std::nullopt;
  return s;
}();

PalletDef euroPallet(double goods, double mass, int kind) { return PalletDef{.len = 1.2, .wid = 0.8, .base = 0.144, .goods = goods, .mass = mass, .kind = kind}; }

// ---------------------------------------------------------------------------- geometry

bool inBox(const OBox& b, double x, double z, double margin) {
  const double c = std::cos(b.yaw);
  const double s = std::sin(b.yaw);
  const double dx = x - b.x;
  const double dz = z - b.z;
  // (the world → the box's own frame: the inverse of x' = x cos + z sin, z' = −x sin + z cos)
  const double lx = dx * c - dz * s;
  const double lz = dx * s + dz * c;
  return std::abs(lx) <= b.hx + margin && std::abs(lz) <= b.hz + margin;
}

bool circleBox(const OBox& b, double x, double z, double r) {
  const double c = std::cos(b.yaw);
  const double s = std::sin(b.yaw);
  const double dx = x - b.x;
  const double dz = z - b.z;
  const double lx = dx * c - dz * s;
  const double lz = dx * s + dz * c;
  const double qx = clamp(lx, -b.hx, b.hx);
  const double qz = clamp(lz, -b.hz, b.hz);
  return (lx - qx) * (lx - qx) + (lz - qz) * (lz - qz) < r * r;
}

bool boxBox(const OBox& a, const OBox& b) {
  using Ax = std::array<double, 2>;
  const auto axes = [](const OBox& o) -> std::array<Ax, 2> {
    const double c = std::cos(o.yaw);
    const double s = std::sin(o.yaw);
    // (the box's x axis and z axis in the world)
    return {Ax{c, -s}, Ax{s, c}};
  };
  const auto project = [&axes](const OBox& o, const Ax& ax) -> Ax {
    const auto [ox, oz] = axes(o);
    const double mid = o.x * ax[0] + o.z * ax[1];
    const double r = o.hx * std::abs(ox[0] * ax[0] + ox[1] * ax[1]) + o.hz * std::abs(oz[0] * ax[0] + oz[1] * ax[1]);
    return {mid - r, mid + r};
  };
  const auto aa = axes(a);
  const auto bb = axes(b);
  for (const Ax& ax : {aa[0], aa[1], bb[0], bb[1]}) {
    const auto [a0, a1] = project(a, ax);
    const auto [b0, b1] = project(b, ax);
    if (a1 <= b0 + 1e-6 || b1 <= a0 + 1e-6) return false;
  }
  return true;
}

OBox palletBox(const Pallet& p) { return OBox{.x = p.x, .z = p.z, .hx = p.def.wid / 2, .hz = p.def.len / 2, .yaw = p.yaw, .y0 = p.y, .y1 = p.y + p.def.base + p.def.goods}; }

Engagement engagement(const ForkFrame& f, const Pallet& p, const ForkliftSpec& spec) {
  const double dx = p.x - f.hx;
  const double dz = p.z - f.hz;
  const double along = dx * f.fx + dz * f.fz;
  const double lateral = dx * f.rx + dz * f.rz;
  // the pallet's own axes: z (its long side), x
  const double zx = -std::sin(p.yaw);
  const double zz = -std::cos(p.yaw);
  const double dotZ = std::abs(zx * f.fx + zz * f.fz);
  const double dotX = std::abs(zz * f.fx - zx * f.fz);
  const bool alongZ = dotZ >= dotX;
  const double misalign = std::acos(clamp(std::max(dotZ, dotX), 0, 1));
  const double D = alongZ ? p.def.len : p.def.wid;
  const double pen = clamp(spec.forkLength - (along - D / 2), 0, D);
  const double depth = along - D / 2 > spec.forkLength ? 0 : pen / D;
  // (from the bottom boards up to the deck: lowered further, the tines go down between the runners)
  const bool height = f.top <= p.y + DECK + 0.03;
  const bool fits = misalign < 8 * DEG && std::abs(lateral) < 0.12 && std::abs(f.tilt) < 3.5 * DEG && height && along > D / 2 - 0.15;
  return {fits, depth, along, lateral, misalign};
}

double capacityAt(double centre, const ForkliftSpec& spec) { return (spec.capacity * (spec.heelAhead + spec.ratedCentre)) / (spec.heelAhead + std::max(spec.ratedCentre, centre)); }

ForkStability stability(const Loading& l, const ForkliftSpec& spec) { return {pitchMoments(l, 0, spec).ratio, rollMoments(l, 0, spec).ratio}; }

// ---------------------------------------------------------------------------- the forklift

Forklift::Forklift(const ForkliftSpec& spec_) : spec(spec_), lift(spec_.liftMin) {}

void Forklift::place(double x_, double y_, double z_, double yaw_) {
  x = x_;
  y = y_;
  z = z_;
  yaw = yaw_;
  v = 0;
}

ForkFrame Forklift::forksAt(double x_, double z_, double yaw_, double lift_, double tilt_, double side_) const {
  const ForkliftSpec& s = spec;
  ForkFrame out;
  const double fx = -std::sin(yaw_);
  const double fz = -std::cos(yaw_);
  const double rx = std::cos(yaw_);
  const double rz = -std::sin(yaw_);
  // the mast leans about its foot near the front axle: tilted back, the carriage comes back with its height
  const double heel = s.wheelbase / 2 + s.heelAhead - (lift_ - MAST_PIVOT) * std::sin(tilt_);
  out.hx = x_ + fx * heel + rx * side_;
  out.hz = z_ + fz * heel + rz * side_;
  out.top = y + lift_;
  out.fx = fx;
  out.fz = fz;
  out.rx = rx;
  out.rz = rz;
  out.tilt = tilt_;
  return out;
}

Loading Forklift::loading() const {
  const Pallet* l = load;
  if (!l) return {.mass = 0, .ahead = 0, .height = 0, .decel = -aLong, .lateral = aLat};
  const ForkliftSpec& s = spec;
  const double ahead = s.heelAhead - (lift - MAST_PIVOT + l->def.goods / 2) * std::sin(tilt) + (l->fork ? l->fork->d : 0.6);
  const double height = lift - DECK + l->def.base + l->def.goods / 2;
  return {.mass = l->def.mass, .ahead = ahead, .height = height, .decel = -aLong, .lateral = aLat};
}

// ---------------------------------------------------------------------------- what it runs into

void Forklift::carry(Pallet& p, const ForkFrame& f) const {
  if (!p.fork) return;
  const PalletFork& k = *p.fork;
  p.x = f.hx + f.fx * k.d + f.rx * k.s;
  p.z = f.hz + f.fz * k.d + f.rz * k.s;
  p.y = f.top - DECK;
  p.yaw = yaw + k.yaw;
}

std::set<std::string> Forklift::touching(const ForkWorld& w) const {
  std::set<std::string> out;
  const ForkliftSpec& s = spec;
  const ForkFrame f = forks();
  const double fx = -std::sin(yaw);
  const double fz = -std::cos(yaw);
  const double yb0 = y + 0.05;
  const double yb1 = y + s.height;
  const auto bodyHits = [&](const OBox& b) {
    if (b.y1 <= yb0 || b.y0 >= yb1) return false;
    for (double c : s.circles)
      if (circleBox(b, x - fx * c, z - fz * c, s.radius)) return true;
    return false;
  };
  const auto forkHits = [&](const OBox& b) {
    for (double side : {-s.forkHalfGap, s.forkHalfGap}) {
      for (double a : {0.12, 0.6, s.forkLength - 0.02}) {
        const double top = f.top + a * std::sin(tilt);
        if (b.y1 <= top - s.forkThick || b.y0 >= top) continue;
        if (inBox(b, f.hx + f.fx * a + f.rx * side, f.hz + f.fz * a + f.rz * side)) return true;
      }
    }
    return false;
  };
  const std::optional<OBox> loadBox = load ? std::optional<OBox>(palletBox(*load)) : std::nullopt;
  const auto loadHits = [&](const OBox& b) { return loadBox && b.y1 > loadBox->y0 + 0.01 && b.y0 < loadBox->y1 && boxBox(*loadBox, b); };
  for (size_t i = 0; i < w.obstacles.size(); i++) {
    const OBox& b = w.obstacles[i];
    if (bodyHits(b) || forkHits(b) || loadHits(b)) out.insert("o" + std::to_string(i));
  }
  for (const Pallet* p : w.pallets) {
    if (p == load) continue;
    const OBox b = palletBox(*p);
    if (bodyHits(b) || loadHits(b) || (forkHits(b) && !engagement(f, *p, s).fits)) out.insert(p->id);
  }
  return out;
}

bool Forklift::newContact(const ForkWorld& w, const std::set<std::string>& before) const {
  for (const std::string& k : touching(w))
    if (!before.count(k)) return true;
  return false;
}

Forklift::Support Forklift::supportOf(const Pallet& p, const ForkWorld& w) const {
  double best = w.ground(p.x, p.z);
  std::string on = "ground";
  for (const Shelf& sh : w.shelves) {
    if (sh.y > p.y + 0.005 || sh.y <= best || !inBox(sh, p.x, p.z)) continue;
    best = sh.y;
    on = sh.id;
  }
  return {best, on};
}

// ---------------------------------------------------------------------------- a step

const ForkEvents& Forklift::step(double dt, const ForkCommand& cmd, const ForkWorld& w) {
  ev = newEvents();
  const ForkliftSpec& s = spec;
  if (!tipped.empty()) {
    // over it goes, until it lies
    v = approach(v, 0, dt * 6);
    const double end = tipped == "forward" ? 1.05 : 3.141592653589793 / 2 - 0.05;
    if (tipped == "forward") {
      pitchV += dt * 6;
      pitch = std::min(end, pitch + pitchV * dt);
    } else {
      rollV += dt * 6;
      roll = std::min(end, roll + rollV * dt);
    }
    aLong = 0;
    aLat = 0;
    return ev;
  }
  Pallet* laden = load;
  // ---- the mast
  const std::set<std::string> before = touching(w);
  const double lm = laden ? laden->def.mass : 0;
  double liftCmd = cmd.lift;
  if (liftCmd > 0 && laden && lm > s.relief * s.capacity) {
    liftCmd = 0;
    ev.relief = true;
  }
  const double liftRate = liftCmd > 0 ? s.liftSpeed * (1 - 0.3 * std::min(1.0, lm / s.capacity)) : s.lowerSpeed;
  const double nl = clamp(lift + liftCmd * liftRate * dt, s.liftMin, s.liftMax);
  const double nt = clamp(tilt + cmd.tilt * s.tiltRate * dt, -s.tiltForward, s.tiltBack);
  const double ns = clamp(side + cmd.shift * s.sideRate * dt, -s.sideMax, s.sideMax);
  const std::array<double, 3> was = {lift, tilt, side};
  if (nl != lift || nt != tilt || ns != side) {
    lift = nl;
    tilt = nt;
    side = ns;
    if (laden) carry(*laden, forks());
    // set down: lowered onto what is under it
    const std::optional<PalletFork> held = laden ? laden->fork : std::nullopt;
    if (laden && nl < was[0]) {
      const Support sup = supportOf(*laden, w);
      if (laden->y <= sup.y) {
        laden->y = sup.y;
        laden->on = sup.on;
        laden->fork = std::nullopt;
        load = nullptr;
        ev.placed = laden;
      }
    }
    if (newContact(w, before)) {
      // (blocked: the mast stays where it was, and the pallet, if one was just set down, is on the forks again)
      if (ev.placed) {
        load = ev.placed;
        load->fork = held;
        load->on = "forks";
        ev.placed = nullptr;
      }
      lift = was[0];
      tilt = was[1];
      side = was[2];
      if (load) carry(*load, forks());
    }
  }
  // ---- driving
  const double v0 = v;
  const bool wheelsDown = pitch < 0.02;
  const double want = clamp(cmd.steer, -1, 1) * s.maxSteer;
  steer = approach(steer, want, s.steerRate * dt);
  const double tan = std::tan(steer);
  const double vmax = cmd.dir < 0 ? s.vReverse : load ? s.vmaxLaden : s.vmax;
  const double vCurve = std::sqrt((s.curveLateral * s.wheelbase) / std::max(std::abs(tan), 1e-3));
  const double limit = std::min(vmax, vCurve);
  double a = 0;
  const double sv = jsSign(v);
  if (cmd.hold) a = -sv * 2.5;
  else {
    if (cmd.dir != 0 && cmd.throttle > 0.01 && wheelsDown) {
      if (jsSign(cmd.dir) == sv || sv == 0) a += cmd.dir * cmd.throttle * s.accel * (std::abs(v) < limit ? 1 : 0);
      else a += cmd.dir * cmd.throttle * s.brake;
    } else if (cmd.brake < 0.05) a -= sv * s.coast * (std::abs(v) > 0.05 ? 1 : 0);
    a -= sv * cmd.brake * s.brake;
    // (held to the curve speed: the drive brakes)
    if (std::abs(v) > limit + 0.05) a -= sv * 2;
  }
  double nv = v + a * dt;
  // (braking stops it, it does not drive it backwards)
  if (sv != 0 && jsSign(nv) != sv && (cmd.dir == 0 || jsSign(cmd.dir) == sv || cmd.hold || cmd.throttle <= 0.01)) nv = 0;
  if (cmd.hold && std::abs(nv) < 0.3) nv = 0;
  aLong = (nv - v0) / dt;
  v = nv;
  // about the front axle's middle: it goes straight on, the rear swings round it
  const double om = wheelsDown ? (-nv * tan) / s.wheelbase : 0;
  const double yaw1 = yaw + om * dt;
  const double mid = yaw + (om * dt) / 2;
  const double half = s.wheelbase / 2;
  const double fxa = x - std::sin(yaw) * half - std::sin(mid) * nv * dt;
  const double fza = z - std::cos(yaw) * half - std::cos(mid) * nv * dt;
  const double nx = fxa + std::sin(yaw1) * half;
  const double nz = fza + std::cos(yaw1) * half;
  const std::array<double, 4> old = {x, z, yaw, y};
  if (std::abs(nv) > 1e-5 || std::abs(om) > 1e-5) {
    const std::set<std::string> before2 = touching(w);
    x = nx;
    z = nz;
    yaw = yaw1;
    const double g = w.ground(nx, nz);
    if (std::isfinite(g)) y = g;
    if (load) carry(*load, forks());
    if (newContact(w, before2)) {
      x = old[0];
      z = old[1];
      yaw = old[2];
      y = old[3];
      if (load) carry(*load, forks());
      ev.hit = std::abs(nv);
      v = 0;
    } else {
      rolled[0] += nv * dt;
      rolled[1] += (nv * dt) / std::max(0.2, std::cos(steer));
    }
  }
  omega = om;
  // the lateral acceleration (outwards, + to the right): the turn's centre lies on the front axle's line,
  // so across the forklift it is the front axle's speed times the yaw rate (the rest pulls along it)
  aLat = om * nv;
  // ---- pallets taken onto the forks
  if (!load) {
    const ForkFrame f = forks();
    for (Pallet* p : w.pallets) {
      const Engagement e = engagement(f, *p, s);
      if (!e.fits || e.depth < 0.5 || f.top < p->y + DECK - 1e-4) continue;
      p->fork = PalletFork{.d = e.along, .s = e.lateral, .yaw = p->yaw - yaw};
      p->on = "forks";
      load = p;
      carry(*p, f);
      ev.picked = p;
      ev.pickedDepth = e.depth;
      break;
    }
  }
  // ---- tipping
  tip(dt, ev);
  return ev;
}

void Forklift::tip(double dt, ForkEvents& e) {
  const ForkliftSpec& s = spec;
  Loading l = loading();
  // a jolt (it was stopped by something) is short: what tips is the drive's and the brakes' deceleration
  l.decel = clamp(l.decel, -4, 4);
  const Moments pm = pitchMoments(l, pitch, s);
  const bool wasUp = pitch > 0.01;
  if (pitch <= 0 && pm.net <= 0) {
    pitch = 0;
    pitchV = 0;
  } else {
    pitchV += (pm.net / pm.inertia) * dt;
    pitchV *= 1 - std::min(0.5, dt * 0.8);
    pitch += pitchV * dt;
    if (pitch < 0) {
      pitch = 0;
      pitchV = 0;
    }
  }
  if (!wasUp && pitch > 0.01) e.rearLift = true;
  // sideways: towards where it is thrown
  const double sg = jsSign(aLat);
  const double want = sg != 0 ? sg : rollSide;
  if (roll <= 0) rollSide = want;
  Loading lat = l;
  lat.lateral = jsSign(aLat) == rollSide ? aLat : 0;
  const Moments rm = rollMoments(lat, roll, s);
  const bool wasSide = roll > 0.01;
  if (roll <= 0 && rm.net <= 0) {
    roll = 0;
    rollV = 0;
  } else {
    rollV += (rm.net / rm.inertia) * dt;
    rollV *= 1 - std::min(0.5, dt * 0.8);
    roll += rollV * dt;
    if (roll < 0) {
      roll = 0;
      rollV = 0;
    }
  }
  if (!wasSide && roll > 0.01) e.sideLift = true;
  if (pitch > s.tipForward) {
    tipped = "forward";
    e.tipped = true;
  } else if (roll > s.tipSide) {
    tipped = "side";
    e.tipped = true;
  }
  if (e.tipped && load) {
    // the load falls off
    Pallet* p = load;
    p->fork = std::nullopt;
    p->on = "ground";
    p->y = y;
    load = nullptr;
  }
}

}  // namespace fs
