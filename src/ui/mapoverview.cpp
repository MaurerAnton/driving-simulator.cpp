// SPDX-License-Identifier: AGPL-3.0-only
// 2.0: the schema of a whole map (ui/mapoverview.h)
#include "ui/mapoverview.h"

#include <algorithm>
#include <cmath>

#include "i18n/unicode.h"

namespace fs {

namespace {

/** the least width of a kind of road on the screen (px), and its width on the ground (m) once that is more */
const double ROAD_PX[ROAD_KINDS] = {0.8, 1.1, 1.3, 1.5, 1.8, 2.3};
const double ROAD_M[ROAD_KINDS] = {3.5, 5.5, 6.5, 7, 7.5, 11};
/** from which scale a kind is drawn (service ways only close up: never from the bands) */
const double ROAD_FROM[ROAD_KINDS] = {0.18, 0.035, 0.008, 0.003, 0.0012, 0};
/** how many bands are kept */
const size_t KEEP = 4;

/** the tolerance of band `level` in metres: a point closer than that to the one before is left out */
double tolOf(int level) { return std::ldexp(1.0, level); }
/** the largest scale band `level` is used for */
double topOf(int level) { return 0.7 / std::ldexp(1.0, level); }

}  // namespace

double MapOverview::roadFrom(RoadKind k) { return ROAD_FROM[static_cast<int>(k)]; }

int MapOverview::placeRank(const std::string& kind) {
  if (kind == "city") return 5;
  if (kind == "town") return 4;
  if (kind == "village") return 3;
  if (kind == "suburb") return 2;
  if (kind == "quarter" || kind == "neighbourhood" || kind == "hamlet") return 1;
  return 0;
}

double MapOverview::placeFrom(int rank) {
  switch (rank) {
    case 5: return 0.0003;
    case 4: return 0.0016;
    case 3: return 0.006;
    case 2: return 0.02;
    case 1: return 0.06;
    default: return 0.35;
  }
}

int MapOverview::bandOf(double scale) {
  // (the band whose tolerance is less than 0.7 px; never finer than band 2, 4 m)
  const double l = std::floor(std::log2(0.7 / std::max(scale, 1e-9)));
  return static_cast<int>(std::max(2.0, std::min(16.0, l)));
}

MapOverview::MapOverview(std::shared_ptr<const NetData> net, std::vector<PlaceJson> places) : net_(std::move(net)), places_(std::move(places)) {
  std::stable_sort(places_.begin(), places_.end(), [](const PlaceJson& a, const PlaceJson& b) { return placeRank(a.kind) > placeRank(b.kind); });
  rank_.reserve(places_.size());
  for (const PlaceJson& p : places_) rank_.push_back(placeRank(p.kind));
  if (net_) {
    const NetData& n = *net_;
    for (size_t e = 0; e < n.edgeCls.size(); e++) edgesOf_[static_cast<int>(roadKindOfClass(n.edgeCls[e]))].push_back(static_cast<int>(e));
  }
}

void MapOverview::edgePoints(int e_, std::vector<double>& out) const {
  const NetData& n = *net_;
  const size_t e = static_cast<size_t>(e_);
  double x = n.nodeX[n.edgeA[e]];
  double z = n.nodeZ[n.edgeA[e]];
  out.push_back(x);
  out.push_back(z);
  for (uint32_t k = n.polyFirst[e]; k < n.polyFirst[e + 1]; k++) {
    x += n.poly[static_cast<size_t>(k) * 2] * POLY_UNIT;
    z += n.poly[static_cast<size_t>(k) * 2 + 1] * POLY_UNIT;
    out.push_back(x);
    out.push_back(z);
  }
  out.push_back(n.nodeX[n.edgeB[e]]);
  out.push_back(n.nodeZ[n.edgeB[e]]);
}

void MapOverview::buildKind(Band& b, int kind, double tol) const {
  const NetData& n = *net_;
  const std::vector<int>& E = edgesOf_[kind];
  const size_t m = E.size();
  if (m == 0) return;
  // the roads of this kind at each junction (local numbers), to join a road to the one it goes on into
  const size_t nodes = n.nodeX.size();
  std::vector<uint32_t> start(nodes + 1, 0);
  for (const int e : E) {
    start[n.edgeA[static_cast<size_t>(e)] + 1]++;
    start[n.edgeB[static_cast<size_t>(e)] + 1]++;
  }
  for (size_t i = 0; i < nodes; i++) start[i + 1] += start[i];
  std::vector<uint32_t> adj(start[nodes]);
  {
    std::vector<uint32_t> fill(start.begin(), start.end() - 1);
    for (size_t i = 0; i < m; i++) {
      adj[fill[n.edgeA[static_cast<size_t>(E[i])]]++] = static_cast<uint32_t>(i);
      adj[fill[n.edgeB[static_cast<size_t>(E[i])]]++] = static_cast<uint32_t>(i);
    }
  }
  const auto A = [&](size_t i) { return n.edgeA[static_cast<size_t>(E[i])]; };
  const auto B = [&](size_t i) { return n.edgeB[static_cast<size_t>(E[i])]; };
  std::vector<uint8_t> used(m, 0);
  /** from `node` on along roads that go on without a branch: (road, walked against its direction) */
  const auto walk = [&](uint32_t node, size_t last, std::vector<std::pair<size_t, bool>>& out) {
    out.clear();
    for (;;) {
      if (start[node + 1] - start[node] != 2) return;
      size_t next = m;
      for (uint32_t k = start[node]; k < start[node + 1]; k++)
        if (adj[k] != last && !used[adj[k]]) next = adj[k];
      if (next == m) return;
      used[next] = 1;
      const bool rev = A(next) != node;
      out.emplace_back(next, rev);
      node = rev ? A(next) : B(next);
      last = next;
    }
  };
  std::vector<std::pair<size_t, bool>> fwd, back, chain;
  std::vector<double> pts, one;
  std::vector<float>& outPts = b.pts[kind];
  std::vector<Line>& outLines = b.lines[kind];
  for (size_t i = 0; i < m; i++) {
    if (used[i]) continue;
    used[i] = 1;
    walk(B(i), i, fwd);
    walk(A(i), i, back);
    chain.clear();
    for (size_t k = back.size(); k-- > 0;) chain.emplace_back(back[k].first, !back[k].second);
    chain.emplace_back(i, false);
    chain.insert(chain.end(), fwd.begin(), fwd.end());
    // the points of the whole line, one road after the other
    pts.clear();
    for (const auto& [j, rev] : chain) {
      one.clear();
      edgePoints(E[j], one);
      const size_t cnt = one.size() / 2;
      for (size_t k = 0; k < cnt; k++) {
        const size_t q = rev ? cnt - 1 - k : k;
        if (k == 0 && !pts.empty()) continue;   // (the junction between two roads once)
        pts.push_back(one[q * 2]);
        pts.push_back(one[q * 2 + 1]);
      }
    }
    // thinned to the band: a point nearer than `tol` to the last one kept is left out (the last point stays)
    Line l{1e30f, 1e30f, -1e30f, -1e30f, static_cast<uint32_t>(outPts.size() / 2), 0};
    const size_t cnt = pts.size() / 2;
    double lx = 0, lz = 0;
    for (size_t k = 0; k < cnt; k++) {
      const double x = pts[k * 2], z = pts[k * 2 + 1];
      if (k > 0 && k + 1 < cnt && std::abs(x - lx) < tol && std::abs(z - lz) < tol) continue;
      outPts.push_back(static_cast<float>(x));
      outPts.push_back(static_cast<float>(z));
      l.minX = std::min(l.minX, static_cast<float>(x));
      l.minZ = std::min(l.minZ, static_cast<float>(z));
      l.maxX = std::max(l.maxX, static_cast<float>(x));
      l.maxZ = std::max(l.maxZ, static_cast<float>(z));
      lx = x;
      lz = z;
      l.n++;
    }
    if (l.n >= 2) outLines.push_back(l);
    else outPts.resize(static_cast<size_t>(l.at) * 2);
  }
}

MapOverview::Band& MapOverview::band(int level) {
  for (size_t i = 0; i < bands_.size(); i++)
    if (bands_[i]->level == level) {
      // (the one used last goes to the end: the first is the one given up when another is made)
      std::unique_ptr<Band> b = std::move(bands_[i]);
      bands_.erase(bands_.begin() + static_cast<long>(i));
      bands_.push_back(std::move(b));
      return *bands_.back();
    }
  auto b = std::make_unique<Band>();
  b->level = level;
  const double top = topOf(level);
  for (int k = 0; k < ROAD_KINDS; k++)
    if (ROAD_FROM[k] <= top && ROAD_FROM[k] < BAND_SCALE) buildKind(*b, k, tolOf(level));
  if (bands_.size() >= KEEP) bands_.erase(bands_.begin());
  bands_.push_back(std::move(b));
  return *bands_.back();
}

void MapOverview::drawRoads(const Ctx2D& g, const MapView& v, double dpr, const MapStyle& st, double alpha) {
  if (!net_) return;
  const double s = v.scale;
  Band& b = band(bandOf(s));
  const double minX = v.wx(0), maxX = v.wx(v.width), minZ = v.wz(0), maxZ = v.wz(v.height);
  g.save();
  // world → screen (in device pixels)
  g.setTransform(dpr * s, 0, 0, dpr * s, dpr * (v.width / 2 - v.cx * s), dpr * (v.height / 2 - v.cz * s));
  g.lineCap("round");
  g.lineJoin("round");
  g.globalAlpha(alpha);
  const auto stroke = [&](int k, double extraPx) {
    g.beginPath();
    for (const Line& l : b.lines[k]) {
      if (l.maxX < minX || l.minX > maxX || l.maxZ < minZ || l.minZ > maxZ) continue;
      scratch_.assign(b.pts[k].begin() + static_cast<long>(l.at) * 2, b.pts[k].begin() + static_cast<long>(l.at + l.n) * 2);
      g.polyline(scratch_.data(), l.n);
    }
    g.lineWidth(std::max(ROAD_PX[k] / s, ROAD_M[k]) + extraPx / s);
    g.stroke();
  };
  // (the light map: a rim round every road first, then the roads – the small ones under the big ones)
  for (int k = 0; k < ROAD_KINDS; k++) {
    if (s < ROAD_FROM[k] || !st.casing[k] || b.lines[k].empty()) continue;
    g.strokeStyle(st.casing[k]);
    stroke(k, 1.6);
  }
  for (int k = 0; k < ROAD_KINDS; k++) {
    if (s < ROAD_FROM[k] || b.lines[k].empty()) continue;
    g.strokeStyle(st.road[k]);
    stroke(k, 0);
  }
  g.restore();
}

void MapOverview::drawNames(const Ctx2D& g, const MapView& v, const MapStyle& st, std::vector<LabelBox>& taken, double avoidX, double avoidY) const {
  const double s = v.scale;
  g.save();
  g.textAlign("center");
  g.textBaseline("middle");
  g.lineJoin("round");
  g.strokeStyle(st.halo);
  std::string font;
  for (size_t i = 0; i < places_.size(); i++) {
    const int r = rank_[i];
    if (s < placeFrom(r)) continue;
    const PlaceJson& p = places_[i];
    const double x = v.sx(p.x);
    double y = v.sy(p.z);
    if (x < -80 || x > v.width + 80 || y < -20 || y > v.height + 20) continue;
    // bigger for bigger places: a city stands out at every zoom
    const int size = r == 5 ? 19 : r == 4 ? 16 : r == 3 ? 14 : r == 2 ? 12 : 11;
    const double half = static_cast<double>(i18n::utf16Length(p.name)) * size * 0.3 + 6;
    // a name that would lie under the car's arrow is written below it
    if (std::abs(x - avoidX) < half + 10 && std::abs(y - avoidY) < size * 0.6 + 14) y = avoidY + 18 + size * 0.6;
    bool clash = false;
    for (const LabelBox& tk : taken)
      if (std::abs(tk.x - x) < tk.half + half && std::abs(tk.y - y) < (tk.size + size) / 2 + 4) {
        clash = true;
        break;
      }
    if (clash) continue;
    taken.push_back({x, y, half, static_cast<double>(size)});
    const std::string f = std::string(r >= 4 ? "700 " : r == 3 ? "600 " : "500 ") + std::to_string(size) + "px system-ui, sans-serif";
    if (f != font) {
      g.font(f);
      font = f;
    }
    g.lineWidth(r >= 4 ? 4 : 3);
    g.strokeText(p.name, x, y);
    g.fillStyle(r >= 4 ? st.bigName : st.placeName);
    g.fillText(p.name, x, y);
  }
  g.restore();
}

std::optional<OverviewSpot> MapOverview::nearest(double x, double z, double maxDist) const {
  if (!net_) return std::nullopt;
  const NetData& n = *net_;
  std::optional<OverviewSpot> best;
  std::vector<double> pts;
  for (size_t e = 0; e < n.edgeCls.size(); e++) {
    if (roadKindOfClass(n.edgeCls[e]) == RoadKind::service) continue;
    // (a road whose start lies further away than it is long can come no nearer than that)
    const double da = std::hypot(n.nodeX[n.edgeA[e]] - x, n.nodeZ[n.edgeA[e]] - z);
    if (da - n.edgeLen[e] > (best ? best->d : maxDist)) continue;
    pts.clear();
    edgePoints(static_cast<int>(e), pts);
    for (size_t k = 0; k + 3 < pts.size(); k += 2) {
      const double ax = pts[k], az = pts[k + 1], bx = pts[k + 2], bz = pts[k + 3];
      const double dx = bx - ax, dz = bz - az;
      const double len2 = dx * dx + dz * dz;
      const double t = len2 > 0 ? std::max(0.0, std::min(1.0, ((x - ax) * dx + (z - az) * dz) / len2)) : 0;
      const double px = ax + dx * t, pz = az + dz * t;
      const double d = std::hypot(px - x, pz - z);
      if (d <= maxDist && (!best || d < best->d)) {
        best = OverviewSpot{static_cast<int>(e), px, pz, len2 > 0 ? std::atan2(-dx, -dz) : 0, d};
      }
    }
  }
  return best;
}

const PlaceJson* MapOverview::nearestPlace(double x, double z, double maxDist) const {
  const PlaceJson* best = nullptr;
  double bestD = maxDist;
  for (size_t i = 0; i < places_.size(); i++) {
    if (rank_[i] < 2) continue;
    const double d = std::hypot(places_[i].x - x, places_[i].z - z);
    if (d < bestD) {
      bestD = d;
      best = &places_[i];
    }
  }
  return best;
}

}  // namespace fs
