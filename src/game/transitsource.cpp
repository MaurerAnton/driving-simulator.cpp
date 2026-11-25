// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/game/spectate.ts @ v1.7.7 (transitSource, lineTakeOver)
#include "game/transitsource.h"

#include <cmath>
#include <unordered_map>

#include "game/clock.h"
#include "sim/rail.h"
#include "world/netfile.h"
#include "world/netindex.h"
#include "world/world.h"

namespace fs {

namespace {

class Source : public TransitSource {
 public:
  explicit Source(TransitGetters get) : get_(std::move(get)) {}

  const Timetable* timetable() override { return get_.timetable(); }
  ServiceTime now() override { return serviceTime(gameNow()); }

  const LineJson* tramLine(int i) override {
    const LinesJson* l = get_.lines();
    return l && i >= 0 && i < static_cast<int>(l->lines.size()) ? &l->lines[static_cast<size_t>(i)] : nullptr;
  }

  std::optional<Spot> tramSpot(int i, const std::pair<int, double>& a, const std::pair<int, double>& b, double frac) override {
    RailTraffic* rail = get_.rail();
    if (!rail) return std::nullopt;
    const std::optional<LineSpot> sp = rail->lineSpot(i, PathMark{a.first, a.second}, PathMark{b.first, b.second}, frac);
    if (!sp) return std::nullopt;
    rail->net.sample(sp->path, sp->s, smp_);
    return Spot{smp_.x, smp_.z, std::atan2(-smp_.tx * sp->dir, -smp_.tz * sp->dir), std::nullopt};
  }

  TransitCourse tramCourse(int i) override {
    RailTraffic* rail = get_.rail();
    if (!rail) return nullptr;
    if (tramsOf_ != rail) {
      tramCourses_.clear();
      tramsOf_ = rail;
    }
    auto it = tramCourses_.find(i);
    if (it != tramCourses_.end()) return it->second;
    if (i < 0 || i >= static_cast<int>(rail->lines.size()) || rail->lines[static_cast<size_t>(i)].seq.empty()) return nullptr;
    auto out = std::make_shared<std::vector<float>>();
    for (const PathDir& pd : rail->lines[static_cast<size_t>(i)].seq) {
      if (pd.path < 0 || pd.path >= static_cast<int>(rail->net.paths.size())) continue;
      const RailPath& p = rail->net.paths[static_cast<size_t>(pd.path)];
      const size_t n = p.pts.size() / 3;
      for (size_t k = 0; k < n; k++) {
        const size_t i3 = (pd.dir > 0 ? k : n - 1 - k) * 3;
        out->push_back(static_cast<float>(p.pts[i3]));
        out->push_back(static_cast<float>(p.pts[i3 + 2]));
      }
    }
    tramCourses_[i] = out;
    return out;
  }

  BusLines* busLines() override { return get_.busLines(); }

  TransitCourse wayCourse(int w) override {
    const NetIndex* net = get_.net();
    if (waysOf_ != net || ways_.size() > 4000) {
      ways_.clear();
      waysOf_ = net;
    }
    auto have = ways_.find(w);
    if (have != ways_.end()) return have->second;
    const int e = w >> 1;
    std::optional<std::vector<double>> pts;
    const Edge* edge = get_.world ? get_.world->edges[e] : nullptr;
    if (edge) {
      std::vector<double> list;
      const std::vector<double>& p = edge->pl.pts;
      for (size_t i = 0; i + 2 < p.size(); i += 3) {
        list.push_back(p[i]);
        list.push_back(p[i + 2]);
      }
      pts = std::move(list);
    } else if (net && net->net && e >= 0 && static_cast<size_t>(e) < net->net->edgeA.size()) pts = netPolyline(*net->net, e);
    if (!pts || pts->size() < 4) return nullptr;
    auto out = std::make_shared<std::vector<float>>(pts->size());
    const std::vector<double>& P = *pts;
    if ((w & 1) == 0)
      for (size_t i = 0; i < P.size(); i++) (*out)[i] = static_cast<float>(P[i]);
    else
      for (size_t i = 0; i < P.size(); i += 2) {
        (*out)[i] = static_cast<float>(P[P.size() - 2 - i]);
        (*out)[i + 1] = static_cast<float>(P[P.size() - 1 - i]);
      }
    // (only roads of the whole map are kept: a loaded road's course is there anyway)
    if (!edge) ways_[w] = out;
    return out;
  }

  Stops* stops() override { return get_.stops(); }

 private:
  TransitGetters get_;
  RailSample smp_;
  // the courses of ways (bounded: they are asked for again as the buses move on)
  std::unordered_map<int, TransitCourse> ways_;
  const NetIndex* waysOf_ = nullptr;
  std::unordered_map<int, TransitCourse> tramCourses_;
  const RailTraffic* tramsOf_ = nullptr;
};

}  // namespace

std::unique_ptr<TransitSource> transitSource(TransitGetters get) { return std::make_unique<Source>(std::move(get)); }

TakeOver lineTakeOver(TransitSource& src, int index, int k) {
  TakeOver out;
  BusLines* lines = src.busLines();
  const BusLine* line = lines && index >= 0 && index < static_cast<int>(lines->lines.size()) ? lines->lines[static_cast<size_t>(index)] : nullptr;
  if (!line || k < 0) return out;
  for (int i = k; i < line->end; i++)
    if (line->ways[static_cast<size_t>(i)] >= 0) out.ways.push_back(line->ways[static_cast<size_t>(i)]);
  const BusLineStop* last = nullptr;
  for (const BusLineStop& s : line->stops)
    if (s.k < line->end) last = &s;
  const TransitCourse c = last ? src.wayCourse(line->ways[static_cast<size_t>(last->k)]) : nullptr;
  if (last && c) {
    // (its last stop: where along its way)
    double left = last->m;
    const std::vector<float>& C = *c;
    for (size_t i = 2; i + 1 < C.size(); i += 2) {
      const double l = std::hypot(static_cast<double>(C[i]) - C[i - 2], static_cast<double>(C[i + 1]) - C[i - 1]);
      if (left <= l || i + 3 >= C.size()) {
        const double f = l > 0 ? std::min(1.0, left / l) : 0;
        out.end = Spot{C[i - 2] + (C[i] - C[i - 2]) * f, C[i - 1] + (C[i + 1] - C[i - 1]) * f, std::atan2(-(static_cast<double>(C[i]) - C[i - 2]), -(static_cast<double>(C[i + 1]) - C[i - 1])), std::nullopt};
        break;
      }
      left -= l;
    }
  }
  return out;
}

}  // namespace fs
