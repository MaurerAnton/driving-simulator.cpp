// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Start at / jump to an address: the search in the house numbers and the
 * spot on the road in front of a house.
 *
 * Origin: tests/game/address.test.ts (MaurerAnton/driving-simulator @
 * 1c08919, v0.5.0).  The original runs the checks against the built map of
 * Baunatal (public/map/world.json); the C++ port's tests build their worlds
 * by hand (the map data is not part of this repository), so the same
 * properties are checked on a small network with the shape the search needs:
 * a through street with house numbers, a side street, a fragment beyond a
 * junction and a private road.
 */
#include "../src/game/address.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace fs;

namespace {

WorldJson makeMap() {
  WorldJson j;
  j.version = 1;
  j.meta.tileSize = 500;
  auto node_ = [](double x, double y, double z, int t) {
    NodeJson n;
    n.x = x;
    n.y = y;
    n.z = z;
    n.t = t;
    return n;
  };
  // 0–1 through street (Hauptstraße), 1–2 side street (Seitenweg),
  // 2–3 fragment at the rim, 0–4 private road
  j.nodes.push_back(node_(0, 0, 0, CTRL_NONE));
  j.nodes.push_back(node_(120, 0, 0, CTRL_NONE));
  j.nodes.push_back(node_(120, 0, 140, CTRL_NONE));
  j.nodes.push_back(node_(240, 0, 140, CTRL_NONE));
  j.nodes.push_back(node_(0, 0, 120, CTRL_NONE));
  auto edge = [](int a, int b, std::vector<double> p, const std::string& name, int c, int f,
                 int access = ACCESS_YES) {
    EdgeJson e;
    e.a = a;
    e.b = b;
    e.c = c;
    e.n = name;
    e.p = std::move(p);
    e.w = 7.0;
    e.lf = 1;
    e.lb = 1;
    e.lw = 3.5;
    e.mf = 50;
    e.mb = 50;
    e.f = f;
    e.ac = access;
    return e;
  };
  j.edges.push_back(edge(0, 1, {-60, 0, 0, 180, 0, 0}, "Hauptstraße", RESIDENTIAL, EF_URBAN));
  j.edges.push_back(edge(1, 2, {120, 0, 0, 120, 0, 200}, "Seitenweg", RESIDENTIAL, EF_URBAN));
  j.edges.push_back(edge(2, 3, {120, 0, 200, 300, 0, 200}, "Fragmentweg", RESIDENTIAL, EF_URBAN));
  j.edges.push_back(
      edge(0, 4, {-60, 0, 0, -60, 0, 180}, "Werkstattweg", SERVICE, EF_URBAN, ACCESS_PRIVATE));
  auto arm = [](int e, int r) {
    ArmJson a;
    a.e = e;
    a.r = r;
    return a;
  };
  // every end of an edge carries an arm (the lane graph reads its stop line
  // from it): node a "out", node b "approach"
  j.nodes[0].arms = {arm(0, ROLE_NONE), arm(3, ROLE_NONE)};
  j.nodes[1].arms = {arm(0, ROLE_NONE), arm(1, ROLE_NONE)};
  j.nodes[2].arms = {arm(1, ROLE_NONE), arm(2, ROLE_NONE)};
  j.nodes[3].arms = {arm(2, ROLE_NONE)};
  j.nodes[4].arms = {arm(3, ROLE_NONE)};
  SpawnJson sp;
  sp.name = "Baunatal";
  sp.x = 0;
  sp.z = 0;
  sp.yaw = 0;
  sp.kind = "street";
  j.spawns.push_back(sp);
  return j;
}

/** The address list the built map would hold (whole metres, as in the file). */
AddressJson makeAddresses() {
  AddressJson j;
  j.version = 1;
  AddressJson::StreetJson a;
  a.n = "Hauptstraße";
  a.c = "Baunatal";
  a.x = 60;
  a.z = 0;
  // "number, x, z, …" — house numbers as strings, coordinates as numbers,
  // exactly what tools/build-addresses.ts writes
  a.h = {"10", "1", "2", "10a", "3", "4", "2", "5", "6", "100", "7", "8", "1", "9", "10"};
  j.streets.push_back(a);
  AddressJson::StreetJson b;
  b.n = "Seitenweg";
  b.c = "Baunatal-Großenritte";
  b.x = 120;
  b.z = 100;
  b.h = {"5", "110", "60"};
  j.streets.push_back(b);
  AddressJson::StreetJson c;
  c.n = "Parkstraße";
  c.c = "Baunatal";
  c.x = 50;
  c.z = 60;
  j.streets.push_back(c);
  AddressJson::StreetJson d;
  d.n = "A 49";
  d.c = "Kirchbauna";
  d.x = 70;
  d.z = 80;
  j.streets.push_back(d);
  return j;
}

}  // namespace

int main() {
  // --- street names are compared the way people type them ------------------
  assert(normaliseAddress("Hauptstraße") == normaliseAddress("hauptstrasse"));
  assert(normaliseAddress("Hauptstraße") == normaliseAddress("Hauptstr."));
  assert(normaliseAddress("Hauptstraße") == normaliseAddress("HAUPTSTR"));
  assert(normaliseAddress("Brüder-Grimm-Straße") == normaliseAddress("brueder grimm str"));
  assert(normaliseAddress("Brüder-Grimm-Straße") == normaliseAddress("Bruder Grimm Strasse"));
  assert(normaliseAddress("Müllerweg") == normaliseAddress("Muellerweg"));
  assert(normaliseAddress("Adams Ruh'") == normaliseAddress("adams ruh"));
  assert(normaliseAddress("  Am   Stadtpark ") == "am stadtpark");
  assert(normaliseAddress("Kirchweg") != normaliseAddress("Kirchstraße"));

  // --- search: street, house number, town – in any order -------------------
  AddressIndex ix(makeAddresses());
  // "hauptstr 10a, 2, 100, 1" are four houses; "10" is a number and a house
  // too, but stored twice under the same key as 10 → 5 + 1 (Seitenweg) + 0 + 0
  assert(ix.size() == 6);
  assert(ix.streetCount() == 4);
  {
    // the house that was asked for
    auto hits = ix.search("hauptstr 10a");
    assert(hits.size() == 1);
    assert(hits[0].label == "Hauptstraße 10a, Baunatal");
    assert(hits[0].exact && hits[0].street == "Hauptstraße");
    assert(std::fabs(hits[0].x - 3) < 1e-9 && std::fabs(hits[0].z - 4) < 1e-9);

    // number first, letter apart: "10 a" is one house number too
    auto apart = ix.search("10 a hauptstr");
    assert(!apart.empty() && apart[0].label == "Hauptstraße 10a, Baunatal");

    // the number itself, then its letters, then the longer numbers
    auto many = ix.search("hauptstr 10");
    assert(many.size() >= 3);
    assert(many[0].label == "Hauptstraße 10, Baunatal");
    assert(many[1].label == "Hauptstraße 10a, Baunatal");
    assert(many[2].label == "Hauptstraße 100, Baunatal");

    // no number: the street itself
    auto streetOnly = ix.search("hauptstr");
    assert(!streetOnly.empty() && streetOnly[0].label == "Hauptstraße, Baunatal" &&
           !streetOnly[0].exact);

    // a number that does not exist: the street is offered instead
    auto miss = ix.search("hauptstr 77");
    assert(!miss.empty() && miss[0].label == "Hauptstraße, Baunatal" && !miss[0].exact &&
           std::fabs(miss[0].x - 60) < 1e-9);

    // the town narrows it down, in any spelling
    auto two = ix.search("seitenweg 5");
    assert(two.size() == 1 && two[0].label == "Seitenweg 5, Baunatal-Großenritte");
    auto gross = ix.search("seitenweg 5 grossenritte");
    assert(!gross.empty() && gross[0].label == "Seitenweg 5, Baunatal-Großenritte");

    // part of a word, best match first
    auto part = ix.search("park");
    assert(part.size() == 1);
    assert(part[0].label == "Parkstraße, Baunatal");

    // a road number is not mistaken for a house number
    auto road = ix.search("a 49");
    assert(road.size() == 1 && road[0].label == "A 49, Kirchbauna");
    auto noSpace = ix.search("A49");
    assert(!noSpace.empty() && noSpace[0].street == "A 49");

    // one letter only matches the beginning of a word
    auto one = ix.search("a");
    assert(!one.empty());
    assert(ix.search("xyz").empty());
    assert(ix.search("10").empty());   // a number alone says nothing
    assert(ix.search("").empty());
    assert(ix.search("straße", 2).size() <= 2);   // the limit holds
  }

  // --- the connected network ------------------------------------------------
  WorldJson net = makeMap();
  World w(net);
  LaneGraph g(w);
  std::set<int> main = mainNetwork(w, g);
  assert(!main.empty());
  const Lane* fragLane = nullptr;
  const Lane* privLane = nullptr;
  for (const Lane& l : g.lanes) {
    if (l.edge && l.edge->name == "Fragmentweg") fragLane = &l;
    if (l.edge && l.edge->name == "Werkstattweg") privLane = &l;
  }
  assert(fragLane && privLane);
  // the private road is never part of it (may not be driven on at all)
  assert(!main.count(privLane->id));
  // most of the network is connected, but not every lane: the fragment beyond
  // the junction is only reached through it (as on the real map, where the rim
  // keeps its fragments)
  assert(main.size() < g.lanes.size());

  // --- every house can be driven up to: on the road, clear of junctions ----
  struct Case {
    const char* what;
    double hx, hz;
    const char* street;
  };
  const Case cases[] = {
      {"Hauptstraße 10", 1, 2, "Hauptstraße"},
      {"Hauptstraße 2", 5, 6, "Hauptstraße"},
      {"Hauptstraße 1", 9, 10, "Hauptstraße"},
      {"Seitenweg 5", 110, 60, "Seitenweg"},
  };
  for (const Case& c : cases) {
    SpawnJson spot = findRoadSpot(w, g, c.hx, c.hz, c.street, 220, &main);
    assert(spot.kind == "street");
    assert(std::hypot(spot.x - c.hx, spot.z - c.hz) < 235);
    // on the carriageway of a road one may use
    bool onRoad = false;
    double along = 0;
    w.edgesInBox(spot.x - 12, spot.z - 12, spot.x + 12, spot.z + 12, [&](Edge& e) {
      PolyProjection p = projectOnPolyline(e.pl, spot.x, spot.z, PolyProjection());
      double dd = std::sqrt(p.d2);
      if (!LaneGraph::drivable(e, false) || dd > e.width / 2 + 0.05) return;
      onRoad = true;
      size_t i0 = 0;
      while (i0 + 1 < e.pl.cum.size() && e.pl.cum[i0 + 1] < p.s) i0++;
      double tx = e.pl.pts[i0 * 3 + 3] - e.pl.pts[i0 * 3];
      double tz = e.pl.pts[i0 * 3 + 5] - e.pl.pts[i0 * 3 + 2];
      double fx = -std::sin(spot.yaw), fz = -std::cos(spot.yaw);
      double l = std::hypot(tx, tz);
      along = (fx * tx + fz * tz) / (l > 0 ? l : 1);
    });
    assert(onRoad);
    assert(std::fabs(along) > 0.9);
    // the house lies to the right of the direction of travel
    double fx = -std::sin(spot.yaw), fz = -std::cos(spot.yaw);
    double side = (c.hx - spot.x) * -fz + (c.hz - spot.z) * fx;
    assert(side > 0);
  }

  // the street's own name selects that street, no name takes the nearest road
  auto named = findRoadSpot(w, g, 110, 60, "Seitenweg", 220, &main);
  auto plain = findRoadSpot(w, g, 110, 60, "", 220, &main);
  assert(named.kind == "street" && plain.kind == "street");
  // far outside every road's reach: no spot at all
  assert(findRoadSpot(w, g, 5000, 5000, "", 220, &main).kind.empty());

  // --- the address file itself --------------------------------------------
  {
    // exactly what tools/build-addresses.ts writes (whole metres, numbers as
    // strings for the house numbers, numbers for the coordinates)
    std::string text =
        "{\"version\":1,\"streets\":[\n"
        "{\"n\":\"A 44\",\"c\":\"Altenbauna\",\"x\":1559,\"z\":-1812,\"h\":[]},\n"
        "{\"n\":\"Adam-Riese-Straße\",\"c\":\"Baunatal\",\"x\":26,\"z\":67,"
        "\"h\":[\"7\",26,67,\"5\",29,83]}\n]}\n";
    AddressJson doc;
    assert(parseAddressJson(text, doc));
    assert(doc.version == 1);
    assert(doc.streets.size() == 2);
    assert(doc.streets[0].h.empty());
    assert(doc.streets[1].n == "Adam-Riese-Straße");
    assert(doc.streets[1].h.size() == 6);
    assert(doc.streets[1].h[0] == "7" && doc.streets[1].h[1] == "26");
    AddressIndex real(doc);
    assert(real.streetCount() == 2 && real.size() == 2);
    auto h = real.search("adam riese 7");
    assert(h.size() == 1 && h[0].exact);
    assert(!parseAddressJson("not json", doc));
  }

  std::printf("address: all checks passed\n");
  return 0;
}
