// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The road network of the whole map as one small file (see netfile.h).
 *
 * Origin: src/world/netfile.ts (MaurerAnton/driving-simulator @ 3420ad0,
 * v0.8.0).
 */
#include "world/netfile.h"

#include <cstring>
#include <stdexcept>

#include "world/routegraph.h"

namespace fs {

namespace {
template <typename T>
void put(std::vector<uint8_t>& out, size_t at, T v) {
  std::memcpy(out.data() + at, &v, sizeof(T));
}
template <typename T>
T getAs(const std::vector<double>& v, size_t i) {
  return static_cast<T>(v[i]);
}
}  // namespace

std::vector<uint8_t> packNet(const NetData& d) {
  std::map<std::string, std::vector<double>> sections;
  auto putF = [&](const char* name, const std::vector<double>& v) { sections[name] = v; };
  std::vector<double> v;
  v.reserve(d.nodeX.size());
  for (double x : d.nodeX) v.push_back(x);
  putF("nodeX", v);
  v.clear();
  for (double z : d.nodeZ) v.push_back(z);
  putF("nodeZ", v);
  v.clear();
  for (uint32_t a : d.edgeA) v.push_back(a);
  putF("edgeA", v);
  v.clear();
  for (uint32_t b : d.edgeB) v.push_back(b);
  putF("edgeB", v);
  v.clear();
  for (double l : d.edgeLen) v.push_back(l);
  putF("edgeLen", v);
  v.clear();
  for (uint8_t c : d.edgeCls) v.push_back(c);
  putF("edgeCls", v);
  v.clear();
  for (uint8_t k : d.wayKind) v.push_back(k);
  putF("wayKind", v);
  v.clear();
  for (uint8_t s : d.waySpeed) v.push_back(s);
  putF("waySpeed", v);
  v.clear();
  for (uint32_t f : d.first) v.push_back(f);
  putF("first", v);
  v.clear();
  for (uint32_t t : d.to) v.push_back(t);
  putF("to", v);
  v.clear();
  for (uint8_t t : d.turn) v.push_back(t);
  putF("turn", v);
  v.clear();
  for (uint16_t a : d.across) v.push_back(a);
  putF("across", v);
  v.clear();
  for (uint32_t f : d.polyFirst) v.push_back(f);
  putF("polyFirst", v);
  v.clear();
  for (int16_t p : d.poly) v.push_back(p);
  putF("poly", v);

  std::string json = "{\"v\":1,\"kind\":\"net\",\"nodes\":" +
                     std::to_string(d.nodeX.size()) + ",\"edges\":" +
                     std::to_string(d.edgeA.size());
  return packSections(json, sections);
}

NetData unpackNet(const std::vector<uint8_t>& buf) {
  UnpackedSections u = unpackSections(buf);
  // the header's kind says what the file is
  {
    size_t k = u.json.find("\"kind\"");
    if (k == std::string::npos || u.json.compare(k, 12, "\"kind\":\"net\"") != 0)
      throw std::runtime_error("not a road-network file");
  }
  NetData d;
  auto need = [&](const char* name) -> const std::vector<double>* {
    const std::vector<double>* a = u.get(name);
    if (!a) throw std::runtime_error(std::string("road-network file without \"") + name + "\"");
    return a;
  };
  auto opt = [&](const char* name, std::vector<double>& out) {
    const std::vector<double>* a = u.get(name);
    if (a) out = *a;
  };
  const std::vector<double>* p = nullptr;
  p = need("nodeX");
  d.nodeX = *p;
  p = need("nodeZ");
  d.nodeZ = *p;
  p = need("edgeA");
  d.edgeA.assign(p->size(), 0);
  for (size_t i = 0; i < p->size(); i++) d.edgeA[i] = static_cast<uint32_t>((*p)[i]);
  p = need("edgeB");
  d.edgeB.assign(p->size(), 0);
  for (size_t i = 0; i < p->size(); i++) d.edgeB[i] = static_cast<uint32_t>((*p)[i]);
  p = need("edgeLen");
  d.edgeLen = *p;
  p = need("edgeCls");
  d.edgeCls.assign(p->size(), 0);
  for (size_t i = 0; i < p->size(); i++) d.edgeCls[i] = static_cast<uint8_t>((*p)[i]);
  p = need("wayKind");
  d.wayKind.assign(p->size(), 0);
  for (size_t i = 0; i < p->size(); i++) d.wayKind[i] = static_cast<uint8_t>((*p)[i]);
  p = need("waySpeed");
  d.waySpeed.assign(p->size(), 0);
  for (size_t i = 0; i < p->size(); i++) d.waySpeed[i] = static_cast<uint8_t>((*p)[i]);
  p = need("first");
  d.first.assign(p->size(), 0);
  for (size_t i = 0; i < p->size(); i++) d.first[i] = static_cast<uint32_t>((*p)[i]);
  std::vector<double> tmp;
  tmp.clear();
  opt("to", tmp);
  d.to.assign(tmp.size(), 0);
  for (size_t i = 0; i < tmp.size(); i++) d.to[i] = static_cast<uint32_t>(tmp[i]);
  tmp.clear();
  opt("turn", tmp);
  d.turn.assign(tmp.size(), 0);
  for (size_t i = 0; i < tmp.size(); i++) d.turn[i] = static_cast<uint8_t>(tmp[i]);
  tmp.clear();
  opt("across", tmp);
  d.across.assign(tmp.size(), 0);
  for (size_t i = 0; i < tmp.size(); i++) d.across[i] = static_cast<uint16_t>(tmp[i]);
  p = need("polyFirst");
  d.polyFirst.assign(p->size(), 0);
  for (size_t i = 0; i < p->size(); i++) d.polyFirst[i] = static_cast<uint32_t>((*p)[i]);
  tmp.clear();
  opt("poly", tmp);
  d.poly.assign(tmp.size(), 0);
  for (size_t i = 0; i < tmp.size(); i++) d.poly[i] = static_cast<int16_t>(tmp[i]);
  return d;
}

RouteGraph routeGraphOfNet(const NetData& d) {
  const size_t E = d.edgeA.size();
  const size_t n = E * 2;
  RouteGraph g;
  g.n = static_cast<int>(n);
  g.len.assign(n, 0);
  g.secs.assign(n, 0);
  g.kind = d.wayKind;
  g.cls.assign(n, 0);
  g.x0.assign(n, 0);
  g.z0.assign(n, 0);
  g.x1.assign(n, 0);
  g.z1.assign(n, 0);
  g.first = d.first;
  g.to = d.to;
  g.turn = d.turn;
  g.across.assign(d.across.size(), 0);
  for (size_t i = 0; i < d.across.size(); i++) g.across[i] = d.across[i] / 10.0;
  g.firstIn.assign(n + 1, 0);
  g.from.assign(d.to.size(), 0);
  g.inTurn.assign(d.to.size(), 0);
  for (size_t e = 0; e < E; e++) {
    for (int dir = 0; dir < 2; dir++) {
      const size_t w = e * 2 + static_cast<size_t>(dir);
      const size_t a = dir == 0 ? d.edgeA[e] : d.edgeB[e];
      const size_t b = dir == 0 ? d.edgeB[e] : d.edgeA[e];
      g.cls[w] = d.edgeCls[e];
      g.x0[w] = d.nodeX[a];
      g.z0[w] = d.nodeZ[a];
      g.x1[w] = d.nodeX[b];
      g.z1[w] = d.nodeZ[b];
      if (d.waySpeed[w] == 0) continue;
      g.len[w] = std::max(0.5, d.edgeLen[e]);
      g.secs[w] = g.len[w] / std::max(std::min(d.waySpeed[w] / 3.6, 36.0), 2.0);
    }
  }
  // the turns by where they lead
  std::vector<uint32_t> count(n + 1, 0);
  for (uint32_t t : g.to)
    if (static_cast<size_t>(t) + 1 < count.size()) count[static_cast<size_t>(t) + 1]++;
  for (size_t w = 0; w < n; w++) count[w + 1] += count[w];
  g.firstIn = count;
  std::vector<uint32_t> fill(count.begin(), count.begin() + static_cast<long>(n));
  for (size_t w = 0; w < n; w++) {
    for (uint32_t k = g.first[w]; k < g.first[w + 1]; k++) {
      const size_t t = g.to[k];
      const size_t at = fill[t]++;
      g.from[at] = static_cast<uint32_t>(w);
      g.inTurn[at] = k;
    }
  }
  return g;
}

std::vector<double> netPolyline(const NetData& d, int e) {
  double x = d.nodeX[d.edgeA[static_cast<size_t>(e)]];
  double z = d.nodeZ[d.edgeA[static_cast<size_t>(e)]];
  std::vector<double> out = {x, z};
  for (uint32_t k = d.polyFirst[static_cast<size_t>(e)];
       k < d.polyFirst[static_cast<size_t>(e) + 1]; k++) {
    x += d.poly[static_cast<size_t>(k) * 2] * POLY_UNIT;
    z += d.poly[static_cast<size_t>(k) * 2 + 1] * POLY_UNIT;
    out.push_back(x);
    out.push_back(z);
  }
  out.push_back(d.nodeX[d.edgeB[static_cast<size_t>(e)]]);
  out.push_back(d.nodeZ[d.edgeB[static_cast<size_t>(e)]]);
  return out;
}

}  // namespace fs
