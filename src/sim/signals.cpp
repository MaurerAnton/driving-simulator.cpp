// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/sim/signals.ts @ v1.7.7
#include "sim/signals.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "core/geom.h"
#include "core/js.h"
#include "sim/jsmath.h"

namespace fs {

namespace {

/** R red, Y red+amber, G green, A amber – anything else counts as red */
int code(const std::string& s, int g) {
  const int k = std::min(g, static_cast<int>(s.size()) - 1);
  if (k < 0) return SIG_RED;
  switch (s[static_cast<size_t>(k)]) {
    case 'R':
      return SIG_RED;
    case 'Y':
      return SIG_RED_AMBER;
    case 'G':
      return SIG_GREEN;
    case 'A':
      return SIG_AMBER;
    default:
      return SIG_RED;
  }
}

/** a pedestrian crossing's light gives the vehicles at least this much green before a press is served (s) */
const double PED_LEAST_GREEN = 8;

/** how long an arrow shows amber when its time is over (s) */
const double ARROW_AMBER = 3;
/** a stream must have had red for this long before an arrow that crosses it comes on (s) */
const double ARROW_CLEAR = 1.5;

/** phases this short or shorter are the change between two main phases (all red), never cut short (s) */
const double CLEARANCE = 2;

/** st[Math.min(g, st.length - 1)] – −1 where the original reads past the list (nothing a light shows) */
inline int stateAt(const std::vector<uint8_t>& st, int g) {
  const int k = std::min(g, static_cast<int>(st.size()) - 1);
  return k < 0 ? -1 : st[static_cast<size_t>(k)];
}
inline double sinceAt(const std::vector<float>& since, int g) {
  const int k = std::min(g, static_cast<int>(since.size()) - 1);
  return k < 0 ? JS_NAN : static_cast<double>(since[static_cast<size_t>(k)]);
}

}  // namespace

std::function<bool(int controller)> lightsOutBy(double seed, double share) {
  return [seed, share](int i) { return share > 0 && hash01(i * 7919.0 + seed * 104729 + 17) < share; };
}

SignalSystem::SignalSystem(const Sparse<SignalControllerJson>& controllers_, std::function<bool(int controller)> lightsOut_)
    : controllers(controllers_), lightsOut(std::move(lightsOut_)) {
  sync();
}

bool SignalSystem::listed(int i) const { return controllers[i] != nullptr; }

bool SignalSystem::runs(int i) const { return i >= 0 && static_cast<size_t>(i) < running.size() && running[static_cast<size_t>(i)] != 0; }

void SignalSystem::sync() {
  bool fresh = false;
  const size_t n = controllers.size();
  if (running.size() < n) {
    running.resize(n, 0);
    cycle.resize(n, 0);
    state.resize(n);
    since.resize(n);
    dark.resize(n, 0);
    local.resize(n, 0);
    asked.resize(n, 0);
  }
  for (size_t i = 0; i < n; i++) {
    if (!listed(static_cast<int>(i))) continue;
    if (running[i]) continue;
    const SignalControllerJson& c = *controllers[static_cast<int64_t>(i)];
    ids.push_back(static_cast<int>(i));
    double sum = 0;
    for (const SignalPhaseJson& p : c.phases) sum = sum + p.d;
    cycle[i] = sum;
    running[i] = 1;
    state[i].assign(static_cast<size_t>(std::max(c.groups, 0)), 0);
    since[i].assign(static_cast<size_t>(std::max(c.groups, 0)), 0.0f);
    // (the light of a pedestrian crossing is never out: people could not cross there otherwise)
    dark[i] = !c.ped && lightsOut && lightsOut(static_cast<int>(i));
    // (… and it begins at rest: green for the vehicles, long enough that a press is served soon)
    local[i] = c.ped ? jsMin(PED_LEAST_GREEN, c.phases.empty() ? 0 : c.phases[0].d) : 0;
    asked[i] = 0;
    fresh = true;
  }
  if (fresh) update(0);
}

int SignalSystem::programmed(int i, int g, double ahead) const {
  const SignalControllerJson& c = *controllers[static_cast<int64_t>(i)];
  const double cyc = cycle[static_cast<size_t>(i)];
  const double loc = local[static_cast<size_t>(i)];
  double t = std::fmod((c.ped ? loc : time + c.off + loc) + ahead, cyc);
  if (t < 0) t += cyc;
  // (a programme without phases shows nothing: red)
  if (c.phases.empty()) return SIG_RED;
  const SignalPhaseJson* phase = &c.phases.back();
  for (const SignalPhaseJson& p : c.phases) {
    if (t < p.d) {
      phase = &p;
      break;
    }
    t -= p.d;
  }
  return code(phase->s, g);
}

void SignalSystem::update(double dt) {
  time += dt;
  for (const int id : ids) {
    const size_t i = static_cast<size_t>(id);
    const SignalControllerJson& c = *controllers[static_cast<int64_t>(i)];
    if (c.ped) {
      // at rest on the vehicles' green until the button is pressed; then once round
      const double green = c.phases.empty() ? 0 : c.phases[0].d;
      double t = local[i] + dt;
      if (local[i] < green) {
        if (asked[i] && t >= PED_LEAST_GREEN) t = jsMax(t, green);
        else t = jsMin(t, green - 0.01);
      }
      // (a request is used up when the pedestrians get green – one made after that waits for the next round)
      if (t >= cycle[i]) t -= cycle[i];
      local[i] = t;
    }
    std::vector<uint8_t>& st = state[i];
    std::vector<float>& sn = since[i];
    for (int g = 0; g < c.groups; g++) {
      const int s = programmed(id, g, 0);
      if (s != st[static_cast<size_t>(g)]) {
        st[static_cast<size_t>(g)] = static_cast<uint8_t>(s);
        sn[static_cast<size_t>(g)] = 0;
      } else {
        sn[static_cast<size_t>(g)] += dt;
      }
    }
  }
  for (auto& [arm, a] : arrows) {
    const bool lit = arrowLit(a);
    if (lit) {
      if (a.state != SIG_GREEN) {
        a.state = SIG_GREEN;
        a.since = 0;
      } else {
        a.since += dt;
      }
    } else if (a.state == SIG_GREEN) {
      // over: amber for a moment – unless the main light has just turned green, which says the same and more
      a.state = get(a.controller, a.own) == SIG_GREEN ? -1 : SIG_AMBER;
      a.since = 0;
    } else {
      a.since += dt;
      if (a.state == SIG_AMBER && (a.since >= ARROW_AMBER || get(a.controller, a.own) == SIG_GREEN)) {
        a.state = -1;
        a.since = 0;
      }
    }
  }
}

bool SignalSystem::arrowLit(const Arrow& a) const {
  if (!runs(a.controller) || dark[static_cast<size_t>(a.controller)]) return false;
  const std::vector<uint8_t>& st = state[static_cast<size_t>(a.controller)];
  const std::vector<float>& sn = since[static_cast<size_t>(a.controller)];
  const int own = stateAt(st, a.own);
  // Only while the main light is red (with the full green the arrow is not needed) – and has been
  // for a moment: those who come the other way and turn left across have the same light.
  if (own != SIG_RED_AMBER && !(own == SIG_RED && sinceAt(sn, a.own) >= ARROW_CLEAR)) return false;
  for (const int g : a.foes) {
    if (g == a.own) continue;
    const int s = stateAt(st, g);
    if (s != SIG_RED || sinceAt(sn, g) < ARROW_CLEAR) return false;
    // … and the arrow has gone out a second before their red ends
    if (programmed(a.controller, g, ARROW_AMBER + 1) != SIG_RED) return false;
  }
  for (const int g : a.friends)
    if (stateAt(st, g) != SIG_GREEN) return false;
  return true;
}

int SignalSystem::get(int controller, int group) const {
  if (!runs(controller)) return SIG_GREEN;
  if (dark[static_cast<size_t>(controller)]) return SIG_FLASH;
  return stateAt(state[static_cast<size_t>(controller)], group);
}

double SignalSystem::age(int controller, int group) const {
  if (!runs(controller)) return 0;
  return sinceAt(since[static_cast<size_t>(controller)], group);
}

bool SignalSystem::isDark(int controller) const { return runs(controller) && dark[static_cast<size_t>(controller)] != 0; }

bool SignalSystem::request(int controller) {
  if (!runs(controller) || dark[static_cast<size_t>(controller)] || asked[static_cast<size_t>(controller)]) return false;
  asked[static_cast<size_t>(controller)] = 1;
  return true;
}

SignalSnapshot SignalSystem::snapshot(double x, double z, double radius) const {
  SignalSnapshot out;
  std::unordered_set<int> near;
  for (const int id : ids) {
    const size_t i = static_cast<size_t>(id);
    const SignalControllerJson& c = *controllers[static_cast<int64_t>(i)];
    const std::vector<uint8_t>& st = state[i];
    if (std::abs(c.x - x) > radius || std::abs(c.z - z) > radius) continue;
    near.insert(id);
    out.ids.push_back(id);
    out.groups.push_back(static_cast<uint8_t>(st.size()));
    out.local.push_back(static_cast<float>(local[i]));
    out.asked.push_back(asked[i] ? 1 : 0);
    for (size_t g = 0; g < st.size(); g++) {
      out.states.push_back(st[g]);
      out.ages.push_back(since[i][g]);
    }
  }
  for (const auto& [arm, a] : arrows)
    if (near.count(a.controller)) out.arrows.emplace_back(arm, a.state, a.since);
  out.time = time;
  return out;
}

void SignalSystem::restore(const SignalSnapshot& s) {
  time = s.time;
  epoch++;
  size_t k = 0;
  for (size_t n = 0; n < s.ids.size(); n++) {
    const int i = s.ids[n];
    const size_t count = s.groups[n];
    if (runs(i) && state[static_cast<size_t>(i)].size() == count) {
      std::vector<uint8_t>& st = state[static_cast<size_t>(i)];
      local[static_cast<size_t>(i)] = s.local[n];
      asked[static_cast<size_t>(i)] = s.asked[n] == 1;
      for (size_t g = 0; g < count; g++) {
        st[g] = s.states[k + g];
        since[static_cast<size_t>(i)][g] = s.ages[k + g];
      }
    }
    k += count;
  }
  for (const auto& [arm, st, sn] : s.arrows) {
    Arrow* a = arrows.get(arm);
    if (a) {
      a->state = st;
      a->since = sn;
    }
  }
}

bool SignalSystem::requested(int controller) const {
  return controller >= 0 && static_cast<size_t>(controller) < asked.size() && asked[static_cast<size_t>(controller)] != 0;
}

std::string SignalSystem::pedestrian(int controller, int group) {
  const int st = get(controller, group);
  if (st == SIG_FLASH) return "dark";
  const bool go = st == SIG_RED && age(controller, group) > PED_CLEAR;
  if (go && controller >= 0 && static_cast<size_t>(controller) < asked.size()) asked[static_cast<size_t>(controller)] = 0;
  return go ? "green" : "red";
}

int SignalSystem::arrow(const Arm* arm) const {
  const Arrow* a = arrows.get(arm);
  return a ? a->state : -1;
}

double SignalSystem::arrowTime(int controller, int own, const std::vector<int>& foes, const std::vector<int>& friends) const {
  if (!listed(controller) || !runs(controller)) return 0;
  const double cyc = cycle[static_cast<size_t>(controller)];
  if (cyc == 0 || cyc != cyc || dark[static_cast<size_t>(controller)]) return 0;
  const double step = 0.5;
  double lit = 0;
  // (from the programme alone: a group's state at a time, and for how long it has had it)
  const auto at = [&](int g, double t) { return programmed(controller, g, t - time); };
  for (double t = 0; t < cyc; t += step) {
    const int mine = at(own, t);
    bool ok = mine == SIG_RED_AMBER || (mine == SIG_RED && at(own, t - ARROW_CLEAR) == SIG_RED);
    for (const int g : foes) {
      if (g == own) continue;
      ok = ok && (at(g, t) == SIG_RED && at(g, t - ARROW_CLEAR) == SIG_RED && at(g, t + ARROW_AMBER + 1) == SIG_RED);
    }
    for (const int g : friends) ok = ok && at(g, t) == SIG_GREEN;
    if (ok) lit += step;
  }
  return lit;
}

SignalSystem::PhaseAt SignalSystem::phaseAt(int i) const {
  const SignalControllerJson& c = *controllers[static_cast<int64_t>(i)];
  const double cyc = cycle[static_cast<size_t>(i)];
  const double loc = local[static_cast<size_t>(i)];
  double t = std::fmod(std::fmod(c.ped ? loc : time + c.off + loc, cyc) + cyc, cyc);
  for (size_t k = 0; k < c.phases.size(); k++) {
    const double d = c.phases[k].d;
    if (t < d) return {static_cast<int>(k), t, d - t};
    t -= d;
  }
  const int k = static_cast<int>(c.phases.size()) - 1;
  return {k, c.phases[static_cast<size_t>(k)].d, 0};
}

int SignalSystem::codeOf(int i, int k, int g) const { return code(controllers[i]->phases[static_cast<size_t>(k)].s, g); }

std::optional<SignalChange> SignalSystem::change(int controller, int group) const {
  if (!listed(controller) || !runs(controller) || dark[static_cast<size_t>(controller)]) return std::nullopt;
  const SignalControllerJson& c = *controllers[static_cast<int64_t>(controller)];
  if (c.phases.empty()) return std::nullopt;
  const int n = static_cast<int>(c.phases.size());
  const PhaseAt at = phaseAt(controller);
  const int k = at.k;
  const int now = codeOf(controller, k, group);
  double sum = at.left;
  if (c.ped && k == 0) {
    // at rest until somebody asks – then after the least green
    if (!asked[static_cast<size_t>(controller)]) return SignalChange{JS_INF, codeOf(controller, 1 % n, group)};
    sum = jsMax(0, PED_LEAST_GREEN - at.into);
  }
  for (int j = 1; j <= n; j++) {
    const int kk = (k + j) % n;
    const int s = codeOf(controller, kk, group);
    if (s != now) return SignalChange{sum, s};
    // (round to the resting phase of a pedestrian crossing: there it waits for the button)
    if (c.ped && kk == 0) return SignalChange{JS_INF, codeOf(controller, 1 % n, group)};
    sum += c.phases[static_cast<size_t>(kk)].d;
  }
  return SignalChange{JS_INF, now};
}

double SignalSystem::until(int i, int group, int want) const {
  const SignalControllerJson& c = *controllers[static_cast<int64_t>(i)];
  const int n = static_cast<int>(c.phases.size());
  const PhaseAt at = phaseAt(i);
  const int k = at.k;
  if (codeOf(i, k, group) == want) return 0;
  double sum = at.left;
  if (c.ped && k == 0) {
    if (!asked[static_cast<size_t>(i)]) return JS_INF;
    sum = jsMax(0, PED_LEAST_GREEN - at.into);
  }
  for (int j = 1; j <= n; j++) {
    const int kk = (k + j) % n;
    if (codeOf(i, kk, group) == want) return sum;
    if (c.ped && kk == 0) return JS_INF;
    sum += c.phases[static_cast<size_t>(kk)].d;
  }
  return JS_INF;
}

std::optional<SignalChange> SignalSystem::pedestrianChange(int controller, int group) const {
  const std::optional<SignalChange> veh = change(controller, group);
  if (!veh) return std::nullopt;
  if (get(controller, group) == SIG_RED) {
    const double a = age(controller, group);
    // (green for them a moment after the vehicles got red; red again when the vehicles get red-amber)
    return a > PED_CLEAR ? SignalChange{veh->left, SIG_RED} : SignalChange{PED_CLEAR - a, SIG_GREEN};
  }
  return SignalChange{until(controller, group, SIG_RED) + PED_CLEAR, SIG_GREEN};
}

std::optional<SignalChange> SignalSystem::arrowChange(const Arm* arm) {
  const Arrow* ap = arrows.get(arm);
  if (!ap || !runs(ap->controller) || dark[static_cast<size_t>(ap->controller)]) return std::nullopt;
  const Arrow& a = *ap;
  if (a.state == SIG_AMBER) {
    // amber for its seconds – or until the main light turns green, which says the same and more
    const std::optional<SignalChange> main = change(a.controller, a.own);
    double left = jsMax(0, ARROW_AMBER - a.since);
    if (main && main->next == SIG_GREEN) left = jsMin(left, main->left);
    return SignalChange{left, -1};
  }
  auto found_ = arrowNext.find(arm);
  if (found_ == arrowNext.end() || found_->second.state != a.state || found_->second.epoch != epoch || found_->second.at < time - 0.5) {
    const int i = a.controller;
    const auto at = [&](int g, double d) { return programmed(i, g, d); };
    const auto lit = [&](double d) {
      const int mine = at(a.own, d);
      if (!(mine == SIG_RED_AMBER || (mine == SIG_RED && at(a.own, d - ARROW_CLEAR) == SIG_RED))) return false;
      for (const int g : a.foes) {
        if (g == a.own) continue;
        if (at(g, d) != SIG_RED || at(g, d - ARROW_CLEAR) != SIG_RED || at(g, d + ARROW_AMBER + 1) != SIG_RED) return false;
      }
      for (const int g : a.friends)
        if (at(g, d) != SIG_GREEN) return false;
      return true;
    };
    // lit now: when does it go out – dark now: when does it come on
    const bool want = a.state != SIG_GREEN;
    double found = JS_INF;
    for (double d = 0.25; d <= cycle[static_cast<size_t>(i)] + 1; d += 0.25) {
      if (lit(d) == want) {
        found = d;
        break;
      }
    }
    // (going out while the main light turns green, it goes dark at once)
    const int next = want ? SIG_GREEN : found < JS_INF && at(a.own, found) == SIG_GREEN ? -1 : SIG_AMBER;
    found_ = arrowNext.insert_or_assign(arm, ArrowNext{a.state, time + found, next, epoch}).first;
  }
  const ArrowNext& known = found_->second;
  return SignalChange{jsMax(0, known.at - time), known.next};
}

bool SignalSystem::skip(int controller) {
  if (!listed(controller) || !runs(controller) || dark[static_cast<size_t>(controller)]) return false;
  const SignalControllerJson& c = *controllers[static_cast<int64_t>(controller)];
  if (c.phases.size() < 2) return false;
  const PhaseAt at = phaseAt(controller);
  const SignalPhaseJson& p = c.phases[static_cast<size_t>(at.k)];
  if (p.d <= CLEARANCE || p.s.find_first_of("AY") != std::string::npos) return false;
  if (c.ped && at.k == 0) {
    if (asked[static_cast<size_t>(controller)] && at.into >= PED_LEAST_GREEN) return false;
    asked[static_cast<size_t>(controller)] = 1;
    epoch++;
    return true;
  }
  // (how long the phase has really been showing: since the last of its lights changed – a tap before
  // may have put the programme's clock forward)
  double shown = at.into;
  for (const float s : since[static_cast<size_t>(controller)]) shown = jsMin(shown, s);
  const double jump = at.left - jsMax(0, SKIP_LEAST - shown);
  if (jump <= 0.05) return false;
  local[static_cast<size_t>(controller)] += jump;
  epoch++;
  return true;
}

}  // namespace fs
