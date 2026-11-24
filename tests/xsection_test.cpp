// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
// Origin: tests/world/xsection.test.ts @ v1.7.7 (what of it concerns the game)
/**
 * The cross-section of streets beyond their lanes – parking, pavements,
 * verges, kerbs, cycle lanes, medians (src/world/xsection.h) –, what calms the
 * traffic on a road (roadfeatures.h) and what narrows it (narrowing.h).
 *
 * The original's test makes little towns of OpenStreetMap's tags with the map
 * build (tools/mapgen) and looks at them; the port has no map build, so the
 * towns are read as the original's map build writes them
 * (tests/fixtures/xsection-towns.json, written by tools/diff/world_town.ts) and
 * asked the same questions – those that do not need the map build's parked
 * cars.  The helpers themselves are asked one by one, and answer what the
 * original's answer (tests/fixtures/xsection-values.txt).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "tests/dump.h"
#include "world/lanes.h"
#include "world/narrowing.h"
#include "world/roadfeatures.h"
#include "world/world.h"
#include "world/worldjson.h"
#include "world/xsection.h"

using namespace fs;
using fs::dump::out;

namespace {

struct Widths {
  double w = 0;
  double lw = 0;
  int lf = 0;
  int lb = 0;
  double swL = 0;
  double swR = 0;
  std::shared_ptr<const RoadSideJson> xl;
  std::shared_ptr<const RoadSideJson> xr;
};
FS_JSON_STRUCT(Widths, w, lw, lf, lb, swL, swR, xl, xr)

/** a town of the fixture: its network, and which road came of which way of OpenStreetMap */
struct TownJson {
  WorldJson json;
  std::map<int, int> edgeOf;
};
void fromJson(JsonReader& r, TownJson& v) {
  r.object([&](std::string_view k) {
    if (k == "json") fromJson(r, v.json);
    else if (k == "edgeOf") {
      r.object([&](std::string_view way) {
        const int id = std::stoi(std::string(way));
        fromJson(r, v.edgeOf[id]);
      });
    }
  });
}

void toJson(JsonWriter& w, const TownJson& v) {
  w.beginObject();
  jsonMember(w, "json", v.json);
  w.key("edgeOf");
  w.beginObject();
  for (const auto& [way, edge] : v.edgeOf) jsonMember(w, std::to_string(way), edge);
  w.endObject();
  w.endObject();
}

struct Fixture {
  Widths widths;
  TownJson main;
  TownJson against;
  TownJson separate;
  TownJson median;
};
FS_JSON_STRUCT(Fixture, widths, main, against, separate, median)

RoadSideJson side(std::initializer_list<std::pair<const char*, double>> fields) {
  RoadSideJson s;
  for (const auto& [name, v] : fields) {
    const std::string k = name;
    if (k == "p") s.p = static_cast<int>(v);
    else if (k == "po") s.po = static_cast<int>(v);
    else if (k == "pw") s.pw = v;
    else if (k == "c") s.c = static_cast<int>(v);
    else if (k == "cw") s.cw = v;
    else if (k == "v") s.v = v;
    else if (k == "k") s.k = static_cast<int>(v);
    else if (k == "g") s.g = v;
    else if (k == "el") s.el = static_cast<int>(v);
    else assert(false);
  }
  return s;
}

void pair(std::vector<std::string>& into, const std::optional<std::array<double, 2>>& v) {
  into.push_back(v ? jsNumberText((*v)[0]) : "-");
  into.push_back(v ? jsNumberText((*v)[1]) : "-");
}

/** arms as the mouth test needs them: roads with or without a continuous pavement across their mouth */
struct MouthEdge {
  int mouth = 0;
};
struct MouthNode;
struct MouthArm {
  MouthEdge* edge = nullptr;
  MouthNode* node = nullptr;
};
struct MouthNode {
  std::vector<MouthArm*> arms;
};

std::vector<bool> flush(const std::vector<int>& mouths) {
  std::vector<MouthEdge> edges(mouths.size());
  std::vector<MouthArm> arms(mouths.size());
  MouthNode node;
  for (size_t i = 0; i < mouths.size(); i++) {
    edges[i].mouth = mouths[i];
    arms[i].edge = &edges[i];
    arms[i].node = &node;
    node.arms.push_back(&arms[i]);
  }
  std::vector<bool> result;
  for (const MouthArm& a : arms) result.push_back(flushMouth(a));
  return result;
}

bool same(const std::optional<std::array<double, 2>>& got, double a, double b) { return got && std::fabs((*got)[0] - a) < 1e-12 && std::fabs((*got)[1] - b) < 1e-12; }

}  // namespace

int main() {
  // --- the helpers asked one by one: what the original answers
  {
    const RoadSideJson empty;
    const std::vector<std::optional<RoadSideJson>> sides = {
        std::nullopt,
        empty,
        side({{"p", 1}}),
        side({{"p", 2}, {"po", 1}}),
        side({{"p", 2}, {"po", 2}, {"pw", 4.5}}),
        side({{"p", 3}}),
        side({{"p", 4}, {"po", 1}, {"v", 1.5}}),
        side({{"p", 5}, {"c", 1}}),
        side({{"c", 1}, {"cw", 1.85}}),
        side({{"c", 2}}),
        side({{"c", 2}, {"cw", 2}, {"v", 0.8}, {"k", 1}}),
        side({{"k", 2}, {"g", 0.3}}),
        side({{"k", 7}}),
        side({{"p", 0}, {"pw", 3}}),
        side({{"el", 0}}),
        side({{"el", 1}, {"g", 0}}),
        side({{"p", 2}, {"g", 0.5}}),
    };
    for (const std::optional<RoadSideJson>& one : sides) {
      const RoadSideJson* s = one ? &*one : nullptr;
      out("side", parkWidth(s), sideInner(s), sideOuter(s), kerbHeight(s), sideIsPlain(s), gutterWidth(s, true, 0.12), gutterWidth(s, false, 0.12), gutterWidth(s, true, 0.03), gutterWidth(s, true, 0), edgeLineOn(s, true),
          edgeLineOn(s, false));
      const double layouts[][2] = {{3.5, 2.5}, {6, 0}, {4, 6.5}};
      for (const auto& q : layouts) {
        const SideLayout l = sideLayout(q[0], q[1], s);
        std::vector<std::string> pairs;
        pair(pairs, l.cycleLane);
        pair(pairs, l.park);
        pair(pairs, l.verge);
        pair(pairs, l.track);
        pair(pairs, l.walk);
        out("layout", l.kerb, l.kerbH, l.laneEdge, pairs, l.outer);
      }
    }
    out("depth", parkDepth(0), parkDepth(1), parkDepth(2), parkDepth(9));
    out("median", medianBits(""), medianBits(""), medianBits("grass"), medianBits("grass;trees;hedge"), medianBits(" Grass ; TREE;parking;paved;sett;nonsense"), medianBits("cobblestone;paving_stones"));
    out("calmKind", calmKind("table"), calmKind("raised_table"), calmKind("hump"), calmKind("bump"), calmKind("cushion"), calmKind("choker"), calmKind(""));
    CalmedRoad road;
    road.calm = std::vector<double>{20, 1, 4, 0.08, 45, 2, 3.7, 0.08, 3, 3, 0.6, 0.06, 97, 4, 2.5, 0.06, 60, 1, 4, 0.1};
    road.len = 100;
    road.cutA = 5;
    road.cutB = 6;
    for (size_t i = 0; i < road.calm->size(); i += 4) out("centre", calmCentre(road, i), calmHalfLength((*road.calm)[i + 1], (*road.calm)[i + 2]));
    std::vector<double> lifts;
    for (double s = 0; s <= 100; s += 0.37) lifts.push_back(calmLift(road, s));
    out("lift", lifts);
    out("breaks", calmBreaks(road));
    CalmedRoad tight;
    tight.calm = std::vector<double>{5, 1, 4, 0.08};
    tight.len = 8;
    tight.cutA = 3;
    tight.cutB = 3;
    CalmedRoad none;
    none.len = 8;
    out("noroom", calmCentre(tight, 0), calmLift(tight, 4), calmLift(none, 4), calmBreaks(none).size(), calmBreaks(tight).size());
    {
      std::vector<int> mouths;
      for (const std::vector<int>& m : {std::vector<int>{1, 0, 0}, std::vector<int>{1, 1, 0}, std::vector<int>{1, 0, 0, 1}, std::vector<int>{0, 0}, std::vector<int>{1}})
        for (bool b : flush(m)) mouths.push_back(b ? 1 : 0);
      out("mouth", mouths);
    }
    const std::vector<BuildOutFeature> feats = {
        {"bump", 30, {{"traffic_calming", "choker"}}},
        {"bump", 50, {{"traffic_calming", "chicane"}, {"length", "6"}}},
        {"bump", 70.123, {{"traffic_calming", "choker"}, {"side", "left"}, {"traffic_calming:depth", "1.234"}, {"beacon", "yes"}}},
        {"bump", 80, {{"traffic_calming", "choker"}, {"traffic_calming:side", "right"}, {"planted", "no"}, {"tree", "no"}, {"length", "50"}}},
        {"bump", 90, {{"traffic_calming", "hump"}}},
        {"crossing", 95, {{"traffic_calming", "choker"}}},
        {"bump", 10, {{"traffic_calming", "chicane"}, {"length", "abc"}, {"traffic_calming:beacon", "yes"}, {"side", ""}}},
    };
    const std::vector<double> bo = buildOutEntries(feats);
    out("entries", bo);
    const NarrowRoad roads[] = {
        {5.5, 0, 0, 2.75, 1, 1, 0, 100, 4, 4},
        {7, 0.25, 0.5, 3.25, 1, 1, 64, 100, 0, 0},
        {3.5, 0, 0, 3.5, 1, 0, 0, 100, 0, 0},
        {13, 0, 0, 3.25, 2, 2, 0, 60, 20, 20},
    };
    for (const NarrowRoad& e : roads) {
      out("room", buildOutRoom(e, 1, false), buildOutRoom(e, -1, false), buildOutRoom(e, 1, true), buildOutRoom(e, -1, true));
      for (const Narrowing& n : narrowingsOf(e, &bo)) out("narrowing", n.s0, n.s1, n.at, n.side, n.depth, n.lo, n.hi, n.single, n.flags);
    }
    const std::vector<double> nothing;
    out("none", narrowingsOf(roads[0], nullptr).size(), narrowingsOf(roads[0], nullptr).size(), narrowingsOf(roads[0], &nothing).size());
    const int bad = dump::against("tests/fixtures/xsection-values.txt");
    if (bad) {
      std::fprintf(stderr, "xsection: %d lines differ from the original's\n", bad);
      return 1;
    }
  }

  // --- the towns of the original's test
  Fixture fx;
  {
    std::ifstream f("tests/fixtures/xsection-towns.json");
    std::stringstream text;
    text << f.rdbuf();
    assert(f.good() || f.eof());
    const bool read = jsonParse(text.str(), fx);
    assert(read && !fx.main.json.edges.empty());
  }

  // widths: lanes, parking and cycle lanes add up to the carriageway, the rest to the pavements
  {
    const Widths& a = fx.widths;
    assert(a.lf + a.lb == 2);
    assert(std::fabs(a.w - (2 * a.lw + 2.0 + 5.0 + 1.5)) < 1e-9);
    assert(sideInner(a.xr.get()) == 3.5);
    assert(sideInner(a.xl.get()) == 5.0);
    assert(std::fabs(a.swL - (2.5 + 1.5)) < 1e-9);  // pavement and verge beyond the left kerb
    // the layout of a side: from the lanes outwards, ending at the pavement's far edge
    const SideLayout lay = sideLayout(a.w / 2, a.swR, a.xr.get());
    assert(std::fabs(lay.laneEdge - (a.w / 2 - 3.5)) < 1e-9);
    assert(same(lay.cycleLane, a.w / 2 - 3.5, a.w / 2 - 2));
    assert(same(lay.park, a.w / 2 - 2, a.w / 2));
    assert(sideOuter(a.xr.get()) == 0);
    const SideLayout left = sideLayout(a.w / 2, a.swL, a.xl.get());
    assert(same(left.verge, a.w / 2, a.w / 2 + 1.5));
    assert(same(left.walk, a.w / 2 + 1.5, a.w / 2 + 4));
  }

  // network: the lanes keep clear of the parking and cycle lanes
  {
    World world(fx.main.json);
    LaneGraph lanes(world);
    const Edge& e = *world.edges[fx.main.edgeOf.at(2)];
    assert(std::fabs(e.marginR - 3.5) < 1e-9);
    assert(std::fabs(e.marginL - 5.3) < 1e-9);
    const LaneGraph::EdgeLanes* el = lanes.edgeLanes[e.id];
    const Lane& fwd = *el->fwd[0];
    const Lane& back = *el->back[0];
    // (+ = left of a→b; the street runs east, so its right is the south)
    assert(std::fabs(fwd.off - (-e.width / 2 + 3.5 + fwd.width / 2)) < 1e-9);
    assert(std::fabs(back.off - (e.width / 2 - 5.3 - back.width / 2)) < 1e-9);
    assert(fwd.off + fwd.width / 2 <= back.off - back.width / 2 + 1e-9);  // the lanes do not overlap
    // (the sides as the map build stored them with the road: marked bays on both, at an angle on the left; an advisory cycle lane on the right)
    assert(e.xl && e.xl->p == PARK_STREET_SIDE && e.xl->po == ORIENT_DIAGONAL && e.xl->pm == 1 && e.xr && e.xr->p == PARK_LANE && e.xr->c == CYCLE_LANE && e.xr->ca == 1);
    assert(e.kerbL == 0.12 && e.kerbR == 0.12 && e.name == "Hauptstra\xC3\x9F" "e");
    // the driveway meets the street over a continuous pavement: no kerbed corners there
    const Edge& drive = *world.edges[fx.main.edgeOf.at(5)];
    assert(drive.mouth == 1 && flushMouth(*drive.armA) && !flushMouth(*e.armA));
  }

  // one-way street drawn against its direction: parking on the side the tags mean
  {
    World world(fx.against.json);
    const Edge& e = *world.edges[fx.against.edgeOf.at(1)];
    assert(e.xr && e.xr->p == PARK_LANE && !(e.xl && e.xl->p.value_or(0)));
    assert(e.lanesF == 1 && e.lanesB == 0 && e.marginR == 2.0 && e.marginL == 0);
  }

  // separate pavement: the road's pavement reaches to it, a verge between
  {
    World world(fx.separate.json);
    const Edge& e = *world.edges[fx.separate.edgeOf.at(1)];
    const double half = e.width / 2;
    assert(std::fabs(e.swL - (6 + 1 - half)) < 0.05);
    assert(std::fabs((e.xl && e.xl->v ? *e.xl->v : 0) - (6 - 1 - half)) < 0.05);
    assert(e.swR == 0);
    assert(fx.separate.edgeOf.at(2) == -1);  // (the footway is no road)
  }

  // median: what lies between the carriageways
  {
    World world(fx.median.json);
    const Edge& e = *world.edges[fx.median.edgeOf.at(1)];
    assert(e.median == (MEDIAN_GRASS | MEDIAN_TREES | MEDIAN_PARKING));
    assert(world.edges[fx.median.edgeOf.at(2)]->median == 0);
  }

  std::printf("xsection: all checks passed (%zu lines the same as the original's)\n", dump::lines().size());
  return 0;
}
