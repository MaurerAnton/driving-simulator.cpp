// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/replay.ts @ v1.7.7
#include "game/replay.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "render/car.h"
#include "render/plane.h"

namespace fs {

PackedFields visualFields(CarVisualState& s) {
  return {
      {"speedKmh", &s.speedKmh},
      {"rpm", &s.rpm},
      {"gear", &s.gear},
      {"indicator", &s.indicator},
      {"hazard", &s.hazard},
      {"blinkOn", &s.blinkOn},
      {"lowBeam", &s.lowBeam},
      {"highBeam", &s.highBeam},
      {"handbrake", &s.handbrake},
      {"ignition", &s.ignition},
      {"engineRunning", &s.engineRunning},
      {"abs", &s.abs},
      {"seatbelt", &s.seatbelt},
      {"fuel", &s.fuel},
      {"automatic", &s.automatic},
      {"showGear", &s.showGear},
      {"odometerKm", &s.odometerKm},
      {"steerWheel", &s.steerWheel},
      {"wheelSteer", &s.wheelSteer},
      {"wheelSpin", &s.wheelSpin},
      {"wheelY", &s.wheelY},
      {"clutch", &s.clutch},
      {"brake", &s.brake},
      {"throttle", &s.throttle},
      {"handbrakeLever", &s.handbrakeLever},
      {"braking", &s.braking},
      {"reversing", &s.reversing},
  };
}

PackedFields planeFields(PlaneVisualState& s) {
  return {
      {"iasKt", &s.iasKt},
      {"altitudeFt", &s.altitudeFt},
      {"vsFpm", &s.vsFpm},
      {"headingDeg", &s.headingDeg},
      {"pitchDeg", &s.pitchDeg},
      {"bankDeg", &s.bankDeg},
      {"slip", &s.slip},
      {"turnRateDegS", &s.turnRateDegS},
      {"rpmGauge", &s.rpmGauge},
      {"fuel", &s.fuel},
      {"flapsGauge", &s.flapsGauge},
      {"stallWarning", &s.stallWarning},
      {"parkingBrake", &s.parkingBrake},
      {"engineOn", &s.engineOn},
      {"aileron", &s.aileron},
      {"elevator", &s.elevator},
      {"rudder", &s.rudder},
      {"flapsDeg", &s.flapsDeg},
      {"throttle", &s.throttle},
      {"rpm", &s.rpm},
      {"propAngle", &s.propAngle},
      {"pedals", &s.pedals},
  };
}

namespace {

const PackedRef* field(const PackedFields& o, const std::string& k) {
  for (const auto& [key, ref] : o)
    if (key == k) return &ref;
  return nullptr;
}

/** The angle a share u of the way from a to b (the short way round). */
double lerpAngle(double a, double b, double u) {
  double d = b - a;
  while (d > std::numbers::pi) d -= 2 * std::numbers::pi;
  while (d < -std::numbers::pi) d += 2 * std::numbers::pi;
  return a + d * u;
}

}  // namespace

// ------------------------------------------------------------------ VisualPacker

std::vector<float> VisualPacker::pack(const PackedFields& rec) {
  if (!keys_) {
    keys_.emplace();
    for (const auto& [k, v] : rec) {
      const char kind = std::holds_alternative<double*>(v) || std::holds_alternative<int*>(v) ? 'n' : std::holds_alternative<bool*>(v) ? 'b' : std::holds_alternative<std::string*>(v) ? 's' : 'a';
      keys_->push_back(k);
      kinds_.push_back(kind);
      const int n = kind == 'a' ? static_cast<int>(std::get<std::vector<double>*>(v)->size()) : 1;
      sizes_.push_back(n);
      size_ += n;
    }
  }
  std::vector<float> out(static_cast<size_t>(size_), 0.0F);
  size_t at = 0;
  for (size_t i = 0; i < keys_->size(); i++) {
    const PackedRef* v = field(rec, (*keys_)[i]);
    const char kind = kinds_[i];
    if (v) {
      if (kind == 'n') out[at] = static_cast<float>(std::holds_alternative<int*>(*v) ? *std::get<int*>(*v) : std::holds_alternative<double*>(*v) ? *std::get<double*>(*v) : 0);
      else if (kind == 'b') out[at] = std::holds_alternative<bool*>(*v) && *std::get<bool*>(*v) ? 1 : 0;
      else if (kind == 's') {
        const std::string text = std::holds_alternative<std::string*>(*v) ? *std::get<std::string*>(*v) : std::string();
        auto f = std::find(texts_.begin(), texts_.end(), text);
        size_t n = static_cast<size_t>(f - texts_.begin());
        if (f == texts_.end()) {
          texts_.push_back(text);
          n = texts_.size() - 1;
        }
        out[at] = static_cast<float>(n);
      } else if (std::holds_alternative<std::vector<double>*>(*v)) {
        const std::vector<double>& a = *std::get<std::vector<double>*>(*v);
        for (int j = 0; j < sizes_[i]; j++) out[at + static_cast<size_t>(j)] = static_cast<size_t>(j) < a.size() ? static_cast<float>(a[static_cast<size_t>(j)]) : 0;
      }
    }
    at += static_cast<size_t>(sizes_[i]);
  }
  return out;
}

void VisualPacker::unpack(const std::vector<float>& p, const PackedFields& rec) const {
  if (!keys_) return;
  size_t at = 0;
  for (size_t i = 0; i < keys_->size(); i++) {
    const PackedRef* v = field(rec, (*keys_)[i]);
    const char kind = kinds_[i];
    const float x = at < p.size() ? p[at] : 0;
    if (v) {
      if (kind == 'n') {
        if (std::holds_alternative<double*>(*v)) *std::get<double*>(*v) = x;
        else if (std::holds_alternative<int*>(*v)) *std::get<int*>(*v) = static_cast<int>(x);
      } else if (kind == 'b') {
        if (std::holds_alternative<bool*>(*v)) *std::get<bool*>(*v) = x == 1;
      } else if (kind == 's') {
        const size_t n = static_cast<size_t>(x);
        if (std::holds_alternative<std::string*>(*v)) *std::get<std::string*>(*v) = n < texts_.size() ? texts_[n] : std::string();
      } else if (std::holds_alternative<std::vector<double>*>(*v)) {
        std::vector<double>& a = *std::get<std::vector<double>*>(*v);
        if (a.size() < static_cast<size_t>(sizes_[i])) a.resize(static_cast<size_t>(sizes_[i]), 0);
        for (int j = 0; j < sizes_[i]; j++) a[static_cast<size_t>(j)] = at + static_cast<size_t>(j) < p.size() ? p[at + static_cast<size_t>(j)] : 0;
      }
    }
    at += static_cast<size_t>(sizes_[i]);
  }
}

// ------------------------------------------------------------------ Recorder

void Recorder::clear(const std::string& v) {
  frames.clear();
  notes.clear();
  vehicle = v;
  nextAt_ = nextOthers_ = nextSignals_ = 0;
}

std::optional<Recorder::Due> Recorder::due(double t) {
  if (t < nextAt_) return std::nullopt;
  nextAt_ = std::max(nextAt_ + REPLAY_STEP, t - REPLAY_STEP);
  const bool others = t >= nextOthers_;
  if (others) nextOthers_ = std::max(nextOthers_ + REPLAY_OTHERS_STEP, t - REPLAY_OTHERS_STEP);
  const bool signals = t >= nextSignals_;
  if (signals) nextSignals_ = std::max(nextSignals_ + REPLAY_SIGNALS_STEP, t - REPLAY_SIGNALS_STEP);
  return Due{.others = others, .signals = signals};
}

void Recorder::push(ReplayFrame f) {
  const double ft = f.t;
  frames.push_back(std::move(f));
  // (only the last half hour: what is older goes – keeping the first frame of what stays complete)
  const double from = ft - REPLAY_KEEP;
  size_t drop = 0;
  while (drop < frames.size() - 1 && frames[drop].t < from) drop++;
  if (drop > 50) {
    ReplayFrame& keep = frames[drop];
    // (the traffic, the trains and the lights of the first frame that stays: from the newest that had them)
    for (int i = static_cast<int>(drop); i >= 0 && (!keep.cars || !keep.rail || !keep.signals); i--) {
      const ReplayFrame& g = frames[static_cast<size_t>(i)];
      if (!keep.cars) keep.cars = g.cars;
      if (!keep.peds) keep.peds = g.peds;
      if (!keep.cyclists) keep.cyclists = g.cyclists;
      if (!keep.rail) keep.rail = g.rail;
      if (!keep.signals) keep.signals = g.signals;
    }
    frames.erase(frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(drop));
    size_t gone = 0;
    while (gone < notes.size() && notes[gone].t < frames[0].t) gone++;
    notes.erase(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(gone));
  }
}

void Recorder::note(ReplayNote n) { notes.push_back(std::move(n)); }

double Recorder::start() const { return !frames.empty() ? frames.front().t : 0; }

double Recorder::end() const { return !frames.empty() ? frames.back().t : 0; }

// ------------------------------------------------------------------ ReplayView

ReplayView::ReplayView(const Recorder& r, std::optional<double> from) : rec(r), t(std::max(r.start(), std::min(r.end(), from.value_or(r.start())))) {}

void ReplayView::advance(double dt) {
  if (!playing) return;
  t += dt * speed;
  if (t >= end()) {
    t = end();
    playing = false;
  }
}

void ReplayView::seek(double to) { t = std::max(start(), std::min(end(), to)); }

size_t ReplayView::index(double at) const {
  const auto& f = rec.frames;
  if (f.empty()) return 0;
  size_t lo = 0;
  size_t hi = f.size() - 1;
  while (lo < hi) {
    const size_t mid = (lo + hi + 1) >> 1;
    if (f[mid].t <= at) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

std::optional<ReplayView::Between> ReplayView::frame() {
  const auto& f = rec.frames;
  if (f.empty()) return std::nullopt;
  const size_t i = index(t);
  const ReplayFrame& a = f[i];
  const ReplayFrame& b = f[std::min(f.size() - 1, i + 1)];
  const double u = b.t > a.t ? std::max(0.0, std::min(1.0, (t - a.t) / (b.t - a.t))) : 0;
  // the player's model between the two (the turn the short way)
  auto& p = pose;
  for (size_t k = 0; k < 3; k++) p[k] = static_cast<float>(a.pose[k] + (b.pose[k] - a.pose[k]) * u);
  double dot = 0;
  for (size_t k = 3; k < 7; k++) dot += static_cast<double>(a.pose[k]) * b.pose[k];
  const double sign = dot < 0 ? -1 : 1;
  double len = 0;
  for (size_t k = 3; k < 7; k++) {
    p[k] = static_cast<float>(a.pose[k] + (sign * b.pose[k] - a.pose[k]) * u);
    len += static_cast<double>(p[k]) * p[k];
  }
  len = std::sqrt(len);
  if (len == 0) len = 1;
  for (size_t k = 3; k < 7; k++) p[k] = static_cast<float>(p[k] / len);
  return Between{.a = &a, .b = &b, .u = u};
}

template <class P>
int ReplayView::latest(P ReplayFrame::* key) const {
  const auto& frames = rec.frames;
  if (frames.empty()) return -1;
  for (int i = static_cast<int>(index(t)); i >= 0; i--)
    if (frames[static_cast<size_t>(i)].*key) return i;
  return -1;
}

const ReplayFrame* ReplayView::following(ReplayPart ReplayFrame::* key, int i) const {
  const auto& frames = rec.frames;
  for (size_t j = static_cast<size_t>(i) + 1; j < frames.size(); j++)
    if (frames[j].*key) return &frames[j];
  return nullptr;
}

std::shared_ptr<const SignalSnapshot> ReplayView::signals() const {
  const int i = latest(&ReplayFrame::signals);
  return i >= 0 ? rec.frames[static_cast<size_t>(i)].signals : nullptr;
}

void ReplayView::traffic() {
  carList.clear();
  const int c0 = latest(&ReplayFrame::cars);
  if (c0 >= 0) {
    const ReplayFrame& f0 = rec.frames[static_cast<size_t>(c0)];
    const ReplayFrame* c1 = following(&ReplayFrame::cars, c0);
    const double u = c1 && c1->t > f0.t ? std::max(0.0, std::min(1.0, (t - f0.t) / (c1->t - f0.t))) : 0;
    const std::vector<float>& a = *f0.cars;
    std::unordered_map<int64_t, size_t> next;
    if (c1 && c1->cars)
      for (size_t k = 0; k + CAR_FIELDS <= c1->cars->size(); k += CAR_FIELDS) next[static_cast<int64_t>((*c1->cars)[k])] = k;
    for (size_t k = 0; k + CAR_FIELDS <= a.size(); k += CAR_FIELDS) {
      const int64_t id = static_cast<int64_t>(a[k]);
      auto& slot = cars_[id];
      if (!slot) {
        slot = std::make_unique<ReplayCar>();
        slot->id = id;
      }
      ReplayCar& c = *slot;
      auto jt = next.find(id);
      const std::vector<float>* b = jt != next.end() ? c1->cars.get() : nullptr;
      const size_t j = jt != next.end() ? jt->second : 0;
      const double w = b ? u : 0;
      auto at = [&](size_t o) -> double { return b ? a[k + o] + ((*b)[j + o] - a[k + o]) * w : a[k + o]; };
      c.style = static_cast<int>(a[k + 1]);
      c.x = at(2);
      c.y = at(3);
      c.z = at(4);
      c.yaw = b ? lerpAngle(a[k + 5], (*b)[j + 5], w) : a[k + 5];
      c.pitch = at(6);
      c.wheelSpin = at(7);
      c.blinker = static_cast<int>(a[k + 8]);
      c.braking = a[k + 9] == 1;
      c.v = at(10);
      c.color = static_cast<int>(a[k + 11]);
      c.line = static_cast<int>(a[k + 12]);
      carList.push_back(&c);
    }
  }
  pedList.clear();
  const int p0 = latest(&ReplayFrame::peds);
  if (p0 >= 0) {
    const ReplayFrame& f0 = rec.frames[static_cast<size_t>(p0)];
    const ReplayFrame* p1 = following(&ReplayFrame::peds, p0);
    const double u = p1 && p1->t > f0.t ? std::max(0.0, std::min(1.0, (t - f0.t) / (p1->t - f0.t))) : 0;
    const std::vector<float>& a = *f0.peds;
    std::unordered_map<int, size_t> next;
    if (p1 && p1->peds)
      for (size_t k = 0; k + PED_FIELDS <= p1->peds->size(); k += PED_FIELDS) next[static_cast<int>((*p1->peds)[k])] = k;
    for (size_t k = 0; k + PED_FIELDS <= a.size(); k += PED_FIELDS) {
      const int id = static_cast<int>(a[k]);
      auto& slot = peds_[id];
      if (!slot) {
        slot = std::make_unique<Walker>();
        slot->id = id;
        slot->state = "wait";
      }
      Walker& p = *slot;
      auto jt = next.find(id);
      const std::vector<float>* b = jt != next.end() ? p1->peds.get() : nullptr;
      const size_t j = jt != next.end() ? jt->second : 0;
      auto at = [&](size_t o) -> double { return b ? a[k + o] + ((*b)[j + o] - a[k + o]) * u : a[k + o]; };
      p.colour = static_cast<int>(a[k + 1]);
      p.x = at(2);
      p.y = at(3);
      p.z = at(4);
      p.yaw = b ? lerpAngle(a[k + 5], (*b)[j + 5], u) : a[k + 5];
      p.state = a[k + 6] == 1 ? "cross" : "wait";
      p.phase = at(7);
      pedList.push_back(&p);
    }
  }
  cyclistList.clear();
  const int y0 = latest(&ReplayFrame::cyclists);
  if (y0 >= 0) {
    const ReplayFrame& f0 = rec.frames[static_cast<size_t>(y0)];
    const ReplayFrame* y1 = following(&ReplayFrame::cyclists, y0);
    const double u = y1 && y1->t > f0.t ? std::max(0.0, std::min(1.0, (t - f0.t) / (y1->t - f0.t))) : 0;
    const std::vector<float>& a = *f0.cyclists;
    std::unordered_map<int, size_t> next;
    if (y1 && y1->cyclists)
      for (size_t k = 0; k + CYCLIST_FIELDS <= y1->cyclists->size(); k += CYCLIST_FIELDS) next[static_cast<int>((*y1->cyclists)[k])] = k;
    for (size_t k = 0; k + CYCLIST_FIELDS <= a.size(); k += CYCLIST_FIELDS) {
      const int id = static_cast<int>(a[k]);
      auto& slot = cyclists_[id];
      if (!slot) {
        slot = std::make_unique<Cyclist>();
        slot->id = id;
      }
      Cyclist& c = *slot;
      auto jt = next.find(id);
      const std::vector<float>* b = jt != next.end() ? y1->cyclists.get() : nullptr;
      const size_t j = jt != next.end() ? jt->second : 0;
      auto at = [&](size_t o) -> double { return b ? a[k + o] + ((*b)[j + o] - a[k + o]) * u : a[k + o]; };
      c.colour = static_cast<int>(a[k + 1]);
      c.x = at(2);
      c.y = at(3);
      c.z = at(4);
      c.yaw = b ? lerpAngle(a[k + 5], (*b)[j + 5], u) : a[k + 5];
      c.phase = at(6);
      c.roll = at(7);
      c.down = a[k + 8];
      cyclistList.push_back(&c);
    }
  }
}

void ReplayView::trains() {
  railList.clear();
  const int r0 = latest(&ReplayFrame::rail);
  if (r0 < 0) return;
  const ReplayFrame& f0 = rec.frames[static_cast<size_t>(r0)];
  const ReplayFrame* r1 = following(&ReplayFrame::rail, r0);
  const double u = r1 && r1->t > f0.t ? std::max(0.0, std::min(1.0, (t - f0.t) / (r1->t - f0.t))) : 0;
  auto read = [](const std::vector<float>& a) {
    std::unordered_map<int, size_t> m;
    for (size_t k = 0; k + RAIL_HEAD <= a.size(); k += RAIL_HEAD + static_cast<size_t>(a[k + 5]) * RAIL_CAR_FIELDS) m[static_cast<int>(a[k])] = k;
    return m;
  };
  const std::vector<float>& a = *f0.rail;
  const std::unordered_map<int, size_t> next = r1 && r1->rail ? read(*r1->rail) : std::unordered_map<int, size_t>();
  for (size_t k = 0; k + RAIL_HEAD <= a.size(); k += RAIL_HEAD + static_cast<size_t>(a[k + 5]) * RAIL_CAR_FIELDS) {
    const int id = static_cast<int>(a[k]);
    const size_t n = static_cast<size_t>(a[k + 5]);
    auto& v = rail_[id];
    if (!v || v->cars.size() != n) {
      v = std::make_unique<ReplayRail>();
      v->id = id;
      v->kind = static_cast<int>(a[k + 1]);
      v->consist = consistOf(v->kind);
      v->panto = 1;
      v->cars.assign(n, RailCar());
    }
    v->kind = static_cast<int>(a[k + 1]);
    v->set = a[k + 2] == 1 ? "double" : a[k + 2] == 2 ? "trailer" : "single";
    v->consist = consistOf(v->kind, v->set);
    if (a[k + 3] >= 0) v->line = RailLineAt{.index = static_cast<int>(a[k + 3]), .k = 0};
    else v->line.reset();
    v->panto = a[k + 4];
    auto jt = next.find(id);
    const std::vector<float>* b = jt != next.end() && (*r1->rail)[jt->second + 5] == static_cast<float>(n) ? r1->rail.get() : nullptr;
    const size_t j = jt != next.end() ? jt->second : 0;
    for (size_t c = 0; c < n; c++) {
      const size_t o = k + RAIL_HEAD + c * RAIL_CAR_FIELDS;
      const size_t q = j + RAIL_HEAD + c * RAIL_CAR_FIELDS;
      RailCar& car = v->cars[c];
      car.x = b ? a[o] + ((*b)[q] - a[o]) * u : a[o];
      car.y = b ? a[o + 1] + ((*b)[q + 1] - a[o + 1]) * u : a[o + 1];
      car.z = b ? a[o + 2] + ((*b)[q + 2] - a[o + 2]) * u : a[o + 2];
      car.yaw = b ? lerpAngle(a[o + 3], (*b)[q + 3], u) : a[o + 3];
      car.pitch = b ? a[o + 4] + ((*b)[q + 4] - a[o + 4]) * u : a[o + 4];
    }
    railList.push_back(v.get());
  }
}

}  // namespace fs
