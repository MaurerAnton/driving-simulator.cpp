// LINK: game sim render i18n audio input engine world core platform geo
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Start at / jump to an address: the search in the house numbers and the
 * spot on the road in front of a house – checked on the real map.
 *
 * Origin: tests/game/address.test.ts @ v1.7.7
 */
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/geom.h"
#include "game/address.h"
#include "game/nav.h"
#include "tests/game_realmap.h"
#include "world/lanes.h"
#include "world/types.h"

using namespace fs;

using H = std::vector<std::variant<std::string, double>>;

static std::vector<std::string> labels(const std::vector<AddressHit>& hits) {
  std::vector<std::string> out;
  for (const AddressHit& h : hits) out.push_back(h.label);
  return out;
}

static double numberOf(const std::variant<std::string, double>& v) { return std::holds_alternative<double>(v) ? std::get<double>(v) : std::stod(std::get<std::string>(v)); }
static std::string textOf(const std::variant<std::string, double>& v) { return std::holds_alternative<double>(v) ? jsStr(std::get<double>(v)) : std::get<std::string>(v); }

int main() {
  using L = std::vector<std::string>;

  // street names are compared the way people type them
  {
    const auto same = [](const char* a, const char* b) { assert(normaliseAddress(a) == normaliseAddress(b)); };
    same("Hauptstraße", "hauptstrasse");
    same("Hauptstraße", "Hauptstr.");
    same("Hauptstraße", "HAUPTSTR");
    same("Brüder-Grimm-Straße", "brueder grimm str");
    same("Brüder-Grimm-Straße", "Bruder Grimm Strasse");
    same("Müllerweg", "Muellerweg");
    same("Adams Ruh'", "adams ruh");
    assert(normaliseAddress("  Am   Stadtpark ") == "am stadtpark");
    assert(normaliseAddress("Kirchweg") != normaliseAddress("Kirchstraße"));
    // (and what else a keyboard, or a map, may hold)
    assert(normaliseAddress("Rue de l’Église") == "ru de l eglise");
    assert(normaliseAddress("ＡＢ １２ ﬁ ½") == "ab 12 fi 1 2");
    assert(normaliseAddress("İstanbul Ærø") == "istanbul r");
  }

  // texts in the order German sorts them: house numbers, names
  {
    assert(compareGerman("5", "5a") < 0 && compareGerman("5a", "5b") < 0 && compareGerman("5a", "5A") < 0 && compareGerman("5A", "5b") < 0);
    assert(compareGerman("5 a", "5a") < 0 && compareGerman("5-7", "5a") < 0 && compareGerman("5/1", "50") < 0);
    assert(compareGerman("Marktplatz", "Marktplatz") == 0 && compareGerman("Adorf", "Bedorf") < 0);
    assert(compareGerman("Mühlenweg", "Muhlenweg") > 0 && compareGerman("Mühlenweg", "Mullerweg") < 0);
    assert(compareGerman("Straße", "Strasse") > 0 && compareGerman("Straße", "Strassf") < 0);
    assert(compareGerman("a", "") > 0 && compareGerman("", "") == 0 && compareGerman("Z", "a") > 0);
  }

  // of streets that match equally well, the nearest comes first
  {
    AddressJson json;
    json.version = 1;
    json.streets = {
        {.n = "Marktplatz", .c = "Adorf", .x = 9000, .z = 0, .h = H{"5", 9000.0, 10.0}},
        {.n = "Marktplatz", .c = "Bedorf", .x = 100, .z = 0, .h = H{"5", 100.0, 10.0}},
        {.n = "Marktplatz", .c = "Cedorf", .x = -4000, .z = 0, .h = H{"5", -4000.0, 10.0}},
    };
    const AddressIndex ix(json);
    assert((labels(ix.search("marktplatz 5")) == L{"Marktplatz 5, Adorf", "Marktplatz 5, Bedorf", "Marktplatz 5, Cedorf"}));   // without a place: by name
    assert((labels(ix.search("marktplatz 5", 8, AddressIndex::Near{0, 0})) == L{"Marktplatz 5, Bedorf", "Marktplatz 5, Cedorf", "Marktplatz 5, Adorf"}));
    assert(ix.search("marktplatz 5 adorf", 8, AddressIndex::Near{0, 0})[0].label == "Marktplatz 5, Adorf");   // a town that is named counts more than what is near
  }

  // search: street, house number, town – in any order
  {
    AddressJson json;
    json.version = 1;
    json.streets = {
        {.n = "Am Stadtpark", .c = "Baunatal", .x = 10, .z = 20, .h = H{"10", 1.0, 2.0, "10a", 3.0, 4.0, "2", 5.0, 6.0, "100", 7.0, 8.0, "1", 9.0, 10.0}},
        {.n = "Hauptstraße", .c = "Baunatal-Großenritte", .x = 100, .z = 200, .h = H{"5", 11.0, 12.0}},
        {.n = "Hauptstraße", .c = "Schauenburg", .x = 300, .z = 400, .h = H{"5", 13.0, 14.0, "7", 15.0, 16.0}},
        {.n = "Parkstraße", .c = "Baunatal", .x = 50, .z = 60, .h = H{}},
        {.n = "A 49", .c = "Kirchbauna", .x = 70, .z = 80, .h = H{}},
    };
    const AddressIndex ix(json);
    assert(ix.size() == 8);
    assert(ix.streetCount() == 5);
    const auto found = [&ix](const std::string& q, size_t n = 8) { return labels(ix.search(q, n)); };
    assert((found("am stadtpark 10a") == L{"Am Stadtpark 10a, Baunatal"}));
    assert((found("10 a Am Stadtpark") == L{"Am Stadtpark 10a, Baunatal"}));   // number first, letter apart
    assert((found("stadtpark 10") == L{"Am Stadtpark 10, Baunatal", "Am Stadtpark 10a, Baunatal", "Am Stadtpark 100, Baunatal"}));   // the number itself, then its letters, then longer numbers
    assert((found("Am Stadtpark") == L{"Am Stadtpark, Baunatal"}));   // no number: the street
    const AddressHit hit = ix.search("am stadtpark 2")[0];
    assert(hit.x == 5 && hit.z == 6 && hit.exact == true && hit.street == "Am Stadtpark");
    // a number that does not exist: the street is offered instead
    const AddressHit miss = ix.search("am stadtpark 77")[0];
    assert(miss.label == "Am Stadtpark, Baunatal" && miss.exact == false && miss.x == 10);
    // the town narrows it down, in any spelling
    assert((found("hauptstr 5") == L{"Hauptstraße 5, Baunatal-Großenritte", "Hauptstraße 5, Schauenburg"}));
    assert((found("hauptstrasse 5 grossenritte") == L{"Hauptstraße 5, Baunatal-Großenritte"}));
    assert((found("Hauptstraße schauenburg 7") == L{"Hauptstraße 7, Schauenburg"}));
    // part of a word, best match first
    assert((found("park") == L{"Parkstraße, Baunatal", "Am Stadtpark, Baunatal"}));
    assert((found("a 49") == L{"A 49, Kirchbauna"}));   // a road number is not mistaken for a house number
    assert((found("A49") == L{"A 49, Kirchbauna"}));
    assert((found("a") == L{"A 49, Kirchbauna", "Am Stadtpark, Baunatal"}));   // one letter only matches the beginning of a word
    assert(found("xyz").empty());
    assert(found("5").empty());   // a number alone says nothing
    assert(found("").empty());
    assert(ix.search("str", 2).size() == 2);   // the limit holds
    // (the same from the text of the file)
    const char* text = R"({"version":1,"streets":[{"n":"Am Stadtpark","c":"Baunatal","x":10,"z":20,"h":["10",1,2,"10a",3,4,2,5,6,"100",7,8,"1",9,10]},{"n":"A 49","c":"Kirchbauna","x":70,"z":80,"h":[]}]})";
    const std::unique_ptr<AddressIndex> read = AddressIndex::parse(text);
    assert(read && read->size() == 5 && read->streetCount() == 2);
    assert((labels(read->search("stadtpark 10")) == L{"Am Stadtpark 10, Baunatal", "Am Stadtpark 10a, Baunatal", "Am Stadtpark 100, Baunatal"}));
    assert(read->search("am stadtpark 2")[0].x == 5);
    AddressJson parsed;
    assert(jsonParse(text, parsed) && parsed.version == 1 && parsed.streets.size() == 2 && parsed.streets[0].h.size() == 15);
    assert(!AddressIndex::parse("[1,2]") && !AddressIndex::parse("{\"streets\":"));
  }

  // The real map (it is built or fetched, not part of the repository: without it these cases are left out).
  // Its road network may come in pieces: the tests look at the town the first start point lies in.
  const std::unique_ptr<RealMap> opened = openRealMap();
  if (opened) {
    RealMap& r = *opened;
    World& world = *r.world;
    LaneGraph& graph = *r.lanes;
    const auto main = [&r](const Lane& l) { return r.usable(l); };
    assert(!world.json.spawns.empty());
    const SpawnJson home = world.json.spawns[0];
    r.loadAround(home.x, home.z, 5200);
    std::optional<AddressJson> addresses = plat::native::wait(r.map->json<AddressJson>("addresses.json.gz"));
    if (!addresses) addresses = plat::native::wait(r.map->json<AddressJson>("addresses.json"));
    assert(addresses);
    const AddressJson& json = *addresses;
    const AddressIndex real(json);
    /** how far a house lies from the start point the tests are held around */
    const auto fromHome = [&home](double x, double z) { return std::hypot(x - home.x, z - home.z); };

    // the address list of the map
    {
      assert(real.streetCount() > 400);
      assert(real.size() > 5000);
      // (every town has its Marktplatz: asked from the first start point, it is the one of that town)
      const AddressHit hit = real.search("Marktplatz 5", 8, AddressIndex::Near{home.x, home.z})[0];
      assert(hit.label == "Marktplatz 5, Baunatal");
      assert(hit.exact == true);
      assert(real.search("Marktplatz 5 Baunatal")[0].label == "Marktplatz 5, Baunatal");
      const std::vector<AddressHit> some = real.search("altenritter str");
      assert(std::any_of(some.begin(), some.end(), [](const AddressHit& h) { return h.street == "Altenritter Straße"; }));
    }

    // every house can be driven up to: on the road, in lane, clear of junctions, house on the right
    {
      PolyProjection proj;
      int n = 0;
      int onRight = 0;
      int twoWay = 0;
      int k = 0;
      for (const AddressJson::Street& s : json.streets) {
        for (size_t i = 0; i + 2 < s.h.size(); i += 3) {
          const double hx = numberOf(s.h[i + 1]);
          const double hz = numberOf(s.h[i + 2]);
          // (of the houses of the town around the start point, every 11th)
          if (fromHome(hx, hz) > 4000 || k++ % 11 != 0) continue;
          const std::string what = s.n + " " + textOf(s.h[i]) + ", " + s.c;
          const std::optional<SpawnJson> spot = findRoadSpot(world, graph, hx, hz, s.n, 220, main);
          if (!spot) std::fprintf(stderr, "%s: no road\n", what.c_str());
          assert(spot);
          n++;
          assert(std::hypot(spot->x - hx, spot->z - hz) < 235);
          // on the carriageway of a road one may use
          struct Best {
            const Edge* e;
            double d;
            double s;
          };
          std::optional<Best> best;
          world.edgesInBox(spot->x - 12, spot->z - 12, spot->x + 12, spot->z + 12, [&](Edge& e) {
            projectOnPolyline(e.pl, spot->x, spot->z, proj);
            const double d = std::sqrt(proj.d2);
            if (LaneGraph::drivable(e, false) && d <= e.width / 2 + 0.05 && (!best || d < best->d)) best = Best{&e, d, proj.s};
          });
          if (!best) std::fprintf(stderr, "%s: not on a carriageway\n", what.c_str());
          assert(best);
          const Best& b = *best;
          // heading along the road
          const std::vector<double>& cum = b.e->pl.cum;
          int first = -1;
          for (size_t c = 0; c < cum.size(); c++) {
            if (cum[c] > b.s) {
              first = static_cast<int>(c);
              break;
            }
          }
          const size_t i0 = static_cast<size_t>(std::min(static_cast<int>(cum.size()) - 2, std::max(0, first - 1)));
          const double tx = b.e->pl.pts[i0 * 3 + 3] - b.e->pl.pts[i0 * 3];
          const double tz = b.e->pl.pts[i0 * 3 + 5] - b.e->pl.pts[i0 * 3 + 2];
          const double fx = -std::sin(spot->yaw);
          const double fz = -std::cos(spot->yaw);
          const double along = (fx * tx + fz * tz) / std::hypot(tx, tz);
          if (!(std::fabs(along) > 0.9)) std::fprintf(stderr, "%s: standing across the road (%.2f)\n", what.c_str(), along);
          assert(std::fabs(along) > 0.9);
          // not against a one-way street
          assert((along > 0 ? b.e->lanesF : b.e->lanesB) > 0);
          if (b.e->lanesF > 0 && b.e->lanesB > 0) {
            twoWay++;
            // right of the driving direction (fx, fz) is (-fz, fx)
            if ((hx - spot->x) * -fz + (hz - spot->z) * fx > 0) onRight++;
          }
        }
      }
      std::printf("  %d houses checked, the house on the right for %d of %d (two-way streets)\n", n, onRight, twoWay);
      assert(n > 400);
      assert(onRight > twoWay * 0.9);
    }

    // the connected network: lessons can start at any address of the list
    {
      int all = 0;
      int connected = 0;
      for (Lane* l : graph.lanes) {
        if (!l) continue;
        all++;
        if (main(*l)) connected++;
      }
      assert(connected > all * 0.5);
      assert(connected < all);   // fragments at the rim and private roads are left out
      PolyProjection proj;
      int n = 0;
      int withTarget = 0;
      int k = 0;
      for (const AddressJson::Street& s : json.streets) {
        for (size_t i = 0; i + 2 < s.h.size(); i += 3) {
          if (fromHome(numberOf(s.h[i + 1]), numberOf(s.h[i + 2])) > 2200 || k++ % 13 != 0) continue;
          const std::optional<SpawnJson> spot = findRoadSpot(world, graph, numberOf(s.h[i + 1]), numberOf(s.h[i + 2]), s.n, 220, main);
          assert(spot);
          // the lane the car stands in
          Lane* lane = nullptr;
          double bd = std::numeric_limits<double>::infinity();
          world.edgesInBox(spot->x - 8, spot->z - 8, spot->x + 8, spot->z + 8, [&](Edge& e) {
            projectOnPolyline(e.pl, spot->x, spot->z, proj);
            const LaneGraph::EdgeLanes* el = graph.edgeLanes[e.id];
            if (!el) return;
            for (const std::vector<Lane*>* side : {&el->fwd, &el->back}) {
              for (Lane* l : *side) {
                const double d = std::fabs(proj.off - l->off) + std::max(0.0, std::sqrt(proj.d2) - e.width);
                const size_t i0 = static_cast<size_t>(std::min(static_cast<int>(e.pl.cum.size()) - 2, std::max(0, proj.seg)));
                const double tx = (e.pl.pts[i0 * 3 + 3] - e.pl.pts[i0 * 3]) * l->dir;
                const double tz = (e.pl.pts[i0 * 3 + 5] - e.pl.pts[i0 * 3 + 2]) * l->dir;
                if (-std::sin(spot->yaw) * tx - std::cos(spot->yaw) * tz > 0 && d < bd) {
                  bd = d;
                  lane = l;
                }
              }
            }
          });
          assert(lane && bd < 0.5);     // in a lane
          assert(main(*lane));          // not put on a road that leads nowhere
          n++;
          // a right-before-left junction to drive to, as the second lesson asks for
          if (findTarget(world, graph, "rbl", spot->x, spot->z, *lane, nullptr, {})) withTarget++;
        }
      }
      std::printf("  %d addresses checked, a lesson destination from %d of them\n", n, withTarget);
      assert(n > 80);
      assert(withTarget >= n * 0.98);
    }

    // a road without houses: the motorway by its number
    {
      const std::vector<AddressHit> hits = real.search("A 49");
      assert(!hits.empty());   // A 49 is in the list
      const AddressHit hit = hits[0];
      r.loadAround(hit.x, hit.z, 1500);
      const std::optional<SpawnJson> spot = findRoadSpot(world, graph, hit.x, hit.z, hit.street, 220, main);
      assert(spot);
      bool onMotorway = false;
      PolyProjection proj;
      world.edgesInBox(spot->x - 8, spot->z - 8, spot->x + 8, spot->z + 8, [&](Edge& e) {
        projectOnPolyline(e.pl, spot->x, spot->z, proj);
        if (e.cls == MOTORWAY && e.ref == "A 49" && std::sqrt(proj.d2) < e.width / 2) onMotorway = true;
      });
      assert(onMotorway);   // put on the A 49 itself
      assert(!findRoadSpot(world, graph, 1e6, 1e6));   // far from every road
    }
  }

  std::printf("game_address_test: ok\n");
  return 0;
}
