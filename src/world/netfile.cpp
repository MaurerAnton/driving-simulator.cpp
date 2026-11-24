// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/netfile.ts @ v1.7.7
/** The road network of the whole map as one small file (see netfile.h). */
#include "world/netfile.h"

#include <stdexcept>
#include <string>

#include "core/js.h"
#include "world/tilefmt.h"

namespace fs {

namespace {

struct NetHeader {
  double v = 0;
  std::string kind;
};
FS_JSON_STRUCT(NetHeader, v, kind)

/** the numbers of a section in the type the network keeps them in (as they are, when the file has them in that type) */
template <class T>
std::vector<T> take(SectionArray* a, std::vector<T> SectionArray::* own) {
  std::vector<T>& same = a->*own;
  if (same.size() == a->size()) return std::move(same);
  std::vector<T> out(a->size());
  for (size_t i = 0; i < out.size(); i++) out[i] = static_cast<T>(a->at(i));
  return out;
}

}  // namespace

std::vector<uint8_t> packNet(const NetData& d) {
  Json header = Json::makeObject();
  header.set("v", 1);
  header.set("kind", "net");
  header.set("nodes", static_cast<double>(d.nodeX.size()));
  header.set("edges", static_cast<double>(d.edgeA.size()));
  SectionList sections;
  sections.emplace_back("nodeX", SectionArray(d.nodeX));
  sections.emplace_back("nodeZ", SectionArray(d.nodeZ));
  sections.emplace_back("edgeA", SectionArray(d.edgeA));
  sections.emplace_back("edgeB", SectionArray(d.edgeB));
  sections.emplace_back("edgeLen", SectionArray(d.edgeLen));
  sections.emplace_back("edgeCls", SectionArray(d.edgeCls));
  sections.emplace_back("wayKind", SectionArray(d.wayKind));
  sections.emplace_back("waySpeed", SectionArray(d.waySpeed));
  sections.emplace_back("first", SectionArray(d.first));
  sections.emplace_back("to", SectionArray(d.to));
  sections.emplace_back("turn", SectionArray(d.turn));
  sections.emplace_back("across", SectionArray(d.across));
  sections.emplace_back("polyFirst", SectionArray(d.polyFirst));
  sections.emplace_back("poly", SectionArray(d.poly));
  return packSections(header, sections);
}

NetData unpackNet(std::vector<uint8_t> buf) {
  UnpackedSections u = unpackSections(std::move(buf));
  NetHeader header;
  u.header(header);
  if (header.kind != "net") throw std::runtime_error("not a road-network file");
  auto need = [&](const char* name) -> SectionArray* {
    SectionArray* a = u.get(name);
    if (!a) throw std::runtime_error(std::string("road-network file without \"") + name + "\"");
    return a;
  };
  NetData d;
  d.nodeX = take(need("nodeX"), &SectionArray::f32);
  d.nodeZ = take(need("nodeZ"), &SectionArray::f32);
  d.edgeA = take(need("edgeA"), &SectionArray::u32);
  d.edgeB = take(need("edgeB"), &SectionArray::u32);
  d.edgeLen = take(need("edgeLen"), &SectionArray::f32);
  d.edgeCls = take(need("edgeCls"), &SectionArray::u8);
  d.wayKind = take(need("wayKind"), &SectionArray::u8);
  d.waySpeed = take(need("waySpeed"), &SectionArray::u8);
  d.first = take(need("first"), &SectionArray::u32);
  // (sections that have no entries are not stored)
  if (SectionArray* a = u.get("to")) d.to = take(a, &SectionArray::u32);
  if (SectionArray* a = u.get("turn")) d.turn = take(a, &SectionArray::u8);
  if (SectionArray* a = u.get("across")) d.across = take(a, &SectionArray::u16);
  d.polyFirst = take(need("polyFirst"), &SectionArray::u32);
  if (SectionArray* a = u.get("poly")) d.poly = take(a, &SectionArray::i16);
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
  g.across.resize(d.across.size());
  for (size_t k = 0; k < d.across.size(); k++) g.across[k] = static_cast<float>(d.across[k] / 10.0);
  g.firstIn.assign(n + 1, 0);
  g.from.assign(d.to.size(), 0);
  g.inTurn.assign(d.to.size(), 0);
  for (size_t e = 0; e < E; e++) {
    for (size_t dir = 0; dir < 2; dir++) {
      const size_t w = e * 2 + dir;
      const uint32_t a = dir == 0 ? d.edgeA[e] : d.edgeB[e];
      const uint32_t b = dir == 0 ? d.edgeB[e] : d.edgeA[e];
      g.cls[w] = d.edgeCls[e];
      g.x0[w] = d.nodeX[a];
      g.z0[w] = d.nodeZ[a];
      g.x1[w] = d.nodeX[b];
      g.z1[w] = d.nodeZ[b];
      if (d.waySpeed[w] == 0) continue;
      g.len[w] = static_cast<float>(jsMax(0.5, d.edgeLen[e]));
      g.secs[w] = static_cast<float>(static_cast<double>(g.len[w]) / jsMax(jsMin(d.waySpeed[w] / 3.6, 36), 2));
    }
  }
  std::vector<uint32_t> count(n + 1, 0);
  for (size_t k = 0; k < g.to.size(); k++) count[g.to[k] + 1]++;
  for (size_t w = 0; w < n; w++) count[w + 1] += count[w];
  g.firstIn = count;
  std::vector<uint32_t> fill(count.begin(), count.begin() + static_cast<long>(n));
  for (size_t w = 0; w < n; w++) {
    for (uint32_t k = g.first[w]; k < g.first[w + 1]; k++) {
      const uint32_t at = fill[g.to[k]]++;
      g.from[at] = static_cast<uint32_t>(w);
      g.inTurn[at] = k;
    }
  }
  return g;
}

std::vector<double> netPolyline(const NetData& d, int e_) {
  const size_t e = static_cast<size_t>(e_);
  double x = d.nodeX[d.edgeA[e]];
  double z = d.nodeZ[d.edgeA[e]];
  std::vector<double> out = {x, z};
  for (uint32_t k = d.polyFirst[e]; k < d.polyFirst[e + 1]; k++) {
    x += d.poly[static_cast<size_t>(k) * 2] * POLY_UNIT;
    z += d.poly[static_cast<size_t>(k) * 2 + 1] * POLY_UNIT;
    out.push_back(x);
    out.push_back(z);
  }
  out.push_back(d.nodeX[d.edgeB[e]]);
  out.push_back(d.nodeZ[d.edgeB[e]]);
  return out;
}

}  // namespace fs
