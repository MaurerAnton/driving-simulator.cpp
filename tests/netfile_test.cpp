// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
/**
 * The road network of a whole map as one small file (src/world/netfile.h), made
 * searchable by place (netindex.h) and changed by a diff (netdiff.h), against
 * the original: tools/diff/world_town.ts --part net makes such a network of the
 * test town with src/world/netfile.ts, netindex.ts and netdiff.ts @ v1.7.7 and
 * prints what they give (tests/fixtures/net-town.txt); this does the same with
 * the port and compares – down to the bytes of the file and the text of the diff.
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "platform/native/native.h"
#include "tests/dump.h"
#include "tests/town.h"
#include "world/netdiff.h"
#include "world/netfile.h"
#include "world/netindex.h"
#include "world/routegraph.h"
#include "world/tilefmt.h"

using namespace fs;
using fs::dump::out;
using fs::dump::sumOf;

namespace {

TownOptions townOptions() {
  TownOptions o;
  o.cols = 4;
  o.rows = 3;
  o.pavement = 2;
  o.deadEnd = 60;
  o.wide = {{1, 1, 2, 1}};
  o.oneWay = {{0, 0, 1, 0}};
  o.lights = {{1, 1}, {2, 1}};
  o.crossings = {{{2, 0, 3, 0}, 0.5, XING_ZEBRA}, {{1, 2, 2, 2}, 0.4, XING_SIGNAL}};
  o.bans = {{0, 1, 1, 1, 1, 0}};
  return o;
}

double sum(const std::vector<uint8_t>& b) {
  double t = 0;
  for (size_t i = 0; i < b.size(); i++) t += b[i] * static_cast<double>((i % 251) + 1);
  return t;
}

template <class T>
std::string joined(const std::vector<T>& list) {
  std::string s;
  for (const T& v : list) s += (s.empty() ? "" : ",") + jsNumberText(static_cast<double>(v));
  return s.empty() ? "-" : s;
}

std::string fails(const std::function<void()>& f) {
  try {
    f();
    return "no error";
  } catch (const std::exception& err) {
    return err.what();
  }
}

std::vector<uint8_t> bytesOf(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

}  // namespace

int main() {
  const Town town = makeTown(townOptions());
  const World& world = *town.world;
  const RouteGraph g = routeGraphOf(*town.lanes);
  const size_t E = world.edges.size();
  NetData net;
  net.waySpeed.assign(E * 2, 0);
  net.polyFirst.assign(E + 1, 0);
  for (const Edge* ep : world.edges) {
    const Edge& e = *ep;
    const LaneGraph::EdgeLanes* el = town.lanes->edgeLanes[e.id];
    const size_t id = static_cast<size_t>(e.id);
    net.waySpeed[id * 2] = !el->fwd.empty() ? static_cast<uint8_t>(jsMin(255, jsRound(el->fwd[0]->limit * 3.6))) : 0;
    net.waySpeed[id * 2 + 1] = !el->back.empty() ? static_cast<uint8_t>(jsMin(255, jsRound(el->back[0]->limit * 3.6))) : 0;
    net.polyFirst[id] = static_cast<uint32_t>(net.poly.size() / 2);
    // the points between its ends, each as the step from the one before in quarter metres
    double x = static_cast<float>(e.a->x);
    double z = static_cast<float>(e.a->z);
    const std::vector<double>& p = e.pl.pts;
    for (size_t i = 1; i + 1 < e.pl.cum.size(); i++) {
      const double sx = jsRound((p[i * 3] - x) / POLY_UNIT);
      const double sz = jsRound((p[i * 3 + 2] - z) / POLY_UNIT);
      net.poly.push_back(static_cast<int16_t>(sx));
      net.poly.push_back(static_cast<int16_t>(sz));
      x += sx * POLY_UNIT;
      z += sz * POLY_UNIT;
    }
  }
  net.polyFirst[E] = static_cast<uint32_t>(net.poly.size() / 2);
  for (const Node* n : world.nodes) {
    net.nodeX.push_back(static_cast<float>(n->x + 0.1));
    net.nodeZ.push_back(static_cast<float>(n->z - 0.3));
  }
  for (const Edge* e : world.edges) {
    net.edgeA.push_back(static_cast<uint32_t>(e->a->id));
    net.edgeB.push_back(static_cast<uint32_t>(e->b->id));
    net.edgeLen.push_back(static_cast<float>(e->len + 0.07));
    net.edgeCls.push_back(static_cast<uint8_t>(e->id % 5 == 0 ? 6 : e->cls));
  }
  net.wayKind = g.kind;
  net.first = g.first;
  net.to = g.to;
  net.turn = g.turn;
  for (float v : g.across) net.across.push_back(static_cast<uint16_t>(jsRound(static_cast<double>(v) * 10)));

  const std::vector<uint8_t> bytes = packNet(net);
  {
    const size_t jsonLen = bytes[4] | (bytes[5] << 8) | (bytes[6] << 16) | (static_cast<size_t>(bytes[7]) << 24);
    out("packed", bytes.size(), sum(bytes), trim(std::string(bytes.begin() + 8, bytes.begin() + 8 + static_cast<long>(jsonLen))));
  }
  const NetData back = unpackNet(bytes);
  out("back", sumOf(back.nodeX), sumOf(back.nodeZ), sumOf(back.edgeA), sumOf(back.edgeB), sumOf(back.edgeLen), sumOf(back.edgeCls), sumOf(back.wayKind), sumOf(back.waySpeed), sumOf(back.first), sumOf(back.to), sumOf(back.turn),
      sumOf(back.across), sumOf(back.polyFirst), sumOf(back.poly));
  const RouteGraph rg = routeGraphOfNet(back);
  out("rg", rg.n, sumOf(rg.len), sumOf(rg.secs), sumOf(rg.kind), sumOf(rg.cls), sumOf(rg.x0), sumOf(rg.z0), sumOf(rg.x1), sumOf(rg.z1), sumOf(rg.first), sumOf(rg.to), sumOf(rg.turn), sumOf(rg.across), sumOf(rg.firstIn), sumOf(rg.from),
      sumOf(rg.inTurn));
  const auto shared = std::make_shared<const NetData>(back);
  const NetIndex index(shared);
  out("main", std::vector<int>(index.main.begin(), index.main.end()));
  for (size_t e = 0; e < E; e += 4) out("course", e, netPolyline(back, static_cast<int>(e)));
  auto usable = [](int e) { return e % 2 == 1; };
  const double nears[][3] = {{50, 3, 10}, {50, 3, 2}, {150, 148, 80}, {-30, -30, 50}, {103, 97, 5}, {330, 5, 40}, {1000, 1000, 50}, {99.9, 60, 200}};
  for (const auto& q : nears) {
    const std::optional<NetSpot> a = index.nearest(q[0], q[1], q[2]);
    const std::optional<NetSpot> b = index.nearest(q[0], q[1], q[2], usable);
    out("nearest", a ? a->edge : -1, a ? a->x : 0.0, a ? a->z : 0.0, a ? a->yaw : 0.0, a ? a->d : 0.0, b ? b->edge : -1, b ? b->d : 0.0);
  }
  const double boxes[][4] = {{90, 90, 160, 110}, {-50, -50, 500, 500}, {900, 900, 1100, 1100}, {-2000, -10, 3000, 10}, {150, 50, 150, 50}};
  for (const auto& q : boxes) {
    std::vector<int> all;
    std::vector<int> big;
    index.edgesInBox(q[0], q[1], q[2], q[3], [&](int e) { all.push_back(e); });
    index.edgesInBox(q[0], q[1], q[2], q[3], [&](int e) { big.push_back(e); }, true);
    out("box", joined(all), joined(big));
  }
  {
    struct {
      std::string text;
      void moveTo(double x, double z) { text += "M" + jsNumberText(x) + "," + jsNumberText(z); }
      void lineTo(double x, double z) { text += "L" + jsNumberText(x) + "," + jsNumberText(z); }
    } path;
    index.trace(3, path);
    index.trace(static_cast<int>(E) - 1, path);
    out("trace", path.text);
  }
  const int pairs[][2] = {{0, 20}, {6, 1}, {9, 30}, {2, 2}};
  for (const auto& q : pairs) {
    const std::optional<std::vector<int>> r = quickest(index.graph, q[0], q[1]);
    out("quickest", q[0], q[1], r ? static_cast<int>(r->size()) : -1, r ? *r : std::vector<int>());
  }
  // a correction of values as a diff
  out("fingerprint", fingerprint(back));
  NetData neu = back;
  neu.wayKind[3] ^= 64;
  neu.wayKind[20] = 255;
  neu.across[0] = 65535;
  neu.across[7] = static_cast<uint16_t>(neu.across[7] + 13);
  neu.turn[5] = static_cast<uint8_t>((neu.turn[5] + 1) % 4);
  neu.waySpeed[11] = 30;
  const NetDiff diff = netDiff(back, neu);
  {
    // the changes as JSON.stringify writes them
    std::string set = "{";
    for (const auto& [name, list] : diff.set.entries) {
      set += (set.size() > 1 ? "," : "") + std::string("\"") + name + "\":[";
      for (size_t i = 0; i < list.size(); i++) set += (i ? "," : "") + std::string("[") + jsNumberText(list[i].first) + "," + jsNumberText(list[i].second) + "]";
      set += "]";
    }
    set += "}";
    out("diff", diff.v, diff.kind, diff.edges, diff.hash, set);
  }
  const std::vector<uint8_t> packedDiff = packNetDiff(diff);
  out("difftext", std::string(packedDiff.begin(), packedDiff.end()));
  const std::optional<NetDiff> read = unpackNetDiff(packedDiff);
  assert(read);
  const NetData applied = applyNetDiff(back, *read);
  const bool same = applied.wayKind == neu.wayKind && applied.waySpeed == neu.waySpeed && applied.turn == neu.turn && applied.across == neu.across && applied.edgeCls == neu.edgeCls && applied.first == neu.first &&
                    applied.to == neu.to && applied.edgeA == neu.edgeA;
  out("applied", same, matchesFingerprint(back, *read), matchesFingerprint(applied, *read), sumOf(applied.wayKind), sumOf(applied.across), sumOf(applied.turn), sumOf(applied.waySpeed), sumOf(back.wayKind), sumOf(back.across));
  {
    const NetDiff none = netDiff(back, back);
    const std::vector<uint8_t> text = packNetDiff(none);
    out("nodiff", none.set.entries.empty() ? "{}" : "?", std::string(text.begin(), text.end()));
  }
  auto with = [&](const std::function<void(NetDiff&)>& change) {
    NetDiff d = *read;
    change(d);
    return d;
  };
  out("wrongKind", fails([&] { applyNetDiff(back, with([](NetDiff& d) { d.kind = "other"; })); }));
  out("wrongHash", fails([&] { applyNetDiff(back, with([](NetDiff& d) { d.hash = "abc"; })); }));
  out("wrongEdges", fails([&] { applyNetDiff(back, with([](NetDiff& d) { d.edges = 1; })); }));
  out("wrongSection", fails([&] {
        applyNetDiff(back, with([](NetDiff& d) {
          d.set = Changes();
          d.set.set("first", {{0, 1}});
        }));
      }));
  out("outOfRange", fails([&] {
        applyNetDiff(back, with([](NetDiff& d) {
          d.set = Changes();
          d.set.set("turn", {{100000, 1}});
        }));
      }),
      fails([&] {
        applyNetDiff(back, with([](NetDiff& d) {
          d.set = Changes();
          d.set.set("turn", {{-1, 1}});
        }));
      }));
  {
    const NetData wrapped = applyNetDiff(back, with([](NetDiff& d) {
      d.set = Changes();
      d.set.set("wayKind", {{0, 300}, {1, -1}, {2, 2.9}});
      d.set.set("across", {{1, 70000}});
    }));
    out("wraps", joined(std::vector<int>(wrapped.wayKind.begin(), wrapped.wayKind.begin() + 3)), wrapped.across[1]);
  }
  out("changedLength", fails([&] {
        NetData shorter = back;
        shorter.turn.erase(shorter.turn.begin());
        netDiff(back, shorter);
      }));
  out("unpack", !unpackNetDiff(bytesOf("garbage")), !unpackNetDiff(bytesOf("{\"v\":2,\"kind\":\"netdiff\",\"hash\":\"x\",\"set\":{}}")), !unpackNetDiff(bytesOf("{\"v\":1,\"kind\":\"net\",\"hash\":\"x\",\"set\":{}}")),
      !unpackNetDiff(bytesOf("{\"v\":1,\"kind\":\"netdiff\",\"hash\":7,\"set\":{}}")), !unpackNetDiff(bytesOf("{\"v\":1,\"kind\":\"netdiff\",\"hash\":\"x\"}")),
      static_cast<bool>(unpackNetDiff(bytesOf("{\"v\":1,\"kind\":\"netdiff\",\"edges\":5,\"hash\":\"x\",\"set\":{\"turn\":[[1,2]]}}"))), !unpackNetDiff(bytesOf("null")), !unpackNetDiff(bytesOf("[]")));
  // (a file cut off inside its first eight bytes: the original fails with a message of its engine's – that it fails is what counts)
  out("notNet", fails([&] { unpackNet(packedDiff); }), fails([&] { unpackNet(bytesOf("FSB1")); }) != "no error");

  // --- beside what the original prints
  {
    // the index made in steps (the game's way) is the index made at once
    const std::shared_ptr<NetIndex> built = plat::native::wait(NetIndex::build(shared));
    assert(built && built->main == index.main && built->graph.secs == index.graph.secs && built->net == shared);
    // a file without a section the network needs
    Json header = Json::makeObject();
    header.set("kind", "net");
    assert(fails([&] { unpackNet(packSections(header, {{"nodeX", SectionArray(net.nodeX)}})); }) == "road-network file without \"nodeZ\"");
  }

  const int bad = dump::against("tests/fixtures/net-town.txt");
  if (bad) {
    std::fprintf(stderr, "netfile: %d lines differ from the original's\n", bad);
    return 1;
  }
  std::printf("netfile: all checks passed (%zu lines the same as the original's)\n", dump::lines().size());
  return 0;
}
