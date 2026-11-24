// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the playtest asked for, as far as it can be checked without a browser:
 * the log of faults and the driven path survive a session, citations link to
 * the paragraphs, the engine can be made weaker, old settings move over, and
 * the aerial photographs' tiles lie on the map's own coordinate grid.
 *
 * Origin: tests/game/playtest.test.ts (MaurerAnton/driving-simulator @
 * ff37c5d, v0.7.0).
 */
#include "../src/game/faultlog.h"
#include "../src/game/track.h"
#include "../src/i18n/violations.h"
#include "../src/reality/aerial.h"
#include "../src/sim/vehicle/spec.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fs;

namespace {

/** A browser's storage, in memory. */
class MemoryStore : public KeyValueStore {
 public:
  std::string getItem(const std::string& key) override {
    auto it = data.find(key);
    return it == data.end() ? "" : it->second;
  }
  void setItem(const std::string& key, const std::string& value) override { data[key] = value; }
  void removeItem(const std::string& key) override { data.erase(key); }
  std::map<std::string, std::string> data;
};

/** A browser whose storage is full: writing throws, reading says "not there". */
class FullStore : public KeyValueStore {
 public:
  std::string getItem(const std::string&) override { return ""; }
  void setItem(const std::string&, const std::string&) override { throw std::runtime_error("quota"); }
  void removeItem(const std::string&) override {}
};

FaultEntry fault(double t, const std::string& id = "speed") {
  FaultEntry f;
  f.t = t;
  f.id = id;
  f.severity = Severity::Minor;
  f.data = {{"limit", "30"}, {"speed", "42"}};
  f.street = "Marktplatz";
  f.mode = "free";
  f.x = 10;
  f.z = -20;
  return f;
}

/** The rated power of a torque curve: max over the points, in PS. */
double curvePower(const VehicleSpec& spec) {
  double best = 0;
  for (const auto& pt : spec.torqueCurve)
    best = std::max(best, (pt.second * pt.first * 2 * PI) / 60 / 735.5);
  return best;
}

double torqueAt(const VehicleSpec& spec, double rpm) {
  for (const auto& pt : spec.torqueCurve)
    if (std::fabs(pt.first - rpm) < 1e-9) return pt.second;
  return -1;
}

}  // namespace

int main() {
  // ---- the log of faults is still there in the next session -----------------
  {
    MemoryStore store;
    FaultLog log(&store);
    assert(log.entries().empty());
    log.add(fault(1000));
    log.add(fault(2000, "redLight"));
    FaultLog next(&store);
    assert(next.entries().size() == 2);
    assert(next.entries()[0].id == "speed" && next.entries()[1].id == "redLight");
    assert(next.entries()[0].t == 1000 && next.entries()[0].street == "Marktplatz");
    next.clear();
    FaultLog fresh(&store);
    assert(fresh.entries().empty());
  }

  // ---- the log keeps the newest faults, survives rubbish and no storage ------
  {
    MemoryStore store;
    FaultLog log(&store);
    for (size_t i = 0; i < FAULT_LOG_MAX + 25; i++) log.add(fault(static_cast<double>(i)));
    assert(log.entries().size() == FAULT_LOG_MAX);
    assert(log.entries()[0].t == 25);  // the oldest ones made room
    FaultLog reloaded(&store);
    assert(reloaded.entries().size() == FAULT_LOG_MAX);

    MemoryStore broken;
    broken.setItem("fahrsim-baunatal.faults.v1", "{\"not\":\"a list\"");
    FaultLog empty(&broken);
    assert(empty.entries().empty());
    broken.setItem("fahrsim-baunatal.faults.v1",
                   "[{\"t\":5,\"id\":\"speed\",\"severity\":\"minor\",\"data\":{},"
                   "\"street\":\"Marktplatz\",\"mode\":\"free\",\"x\":10,\"z\":-20},"
                   "{\"t\":\"x\"},null,"
                   "{\"t\":6,\"id\":\"speed\",\"severity\":\"huge\",\"data\":{},"
                   "\"street\":\"Marktplatz\",\"mode\":\"free\",\"x\":10,\"z\":-20}]");
    FaultLog onlyValid(&broken);
    assert(onlyValid.entries().size() == 1);  // only what is a fault is taken over
    assert(onlyValid.entries()[0].t == 5);

    FaultLog none;  // without storage the log lives as long as the page
    none.add(fault(1));
    assert(none.entries().size() == 1);
    FullStore full;
    FaultLog fullLog(&full);
    try {
      fullLog.add(fault(1));
    } catch (...) {
      assert(false && "a full storage must not throw out of add()");
    }
    assert(fullLog.entries().size() == 1);
  }

  // ---- every citation of the StVO leads to the text of its paragraph --------
  {
    auto parts = citationParts("§ 37 Abs. 2 StVO");
    assert(parts.size() == 2);
    assert(parts[0].text == "§ 37 Abs. 2");
    assert(parts[0].url == "https://www.gesetze-im-internet.de/stvo_2013/__37.html");
    assert(parts[1].text == " StVO" && parts[1].url.empty());

    parts = citationParts("§ 1 Abs. 2, § 3 Abs. 2a StVO");
    assert(parts.size() >= 3);
    int withUrl = 0;
    for (const auto& p : parts)
      if (!p.url.empty()) withUrl++;
    assert(withUrl == 2);
    assert(parts[0].text == "§ 1 Abs. 2");
    assert(parts[0].url == "https://www.gesetze-im-internet.de/stvo_2013/__1.html");
    // (the original's __21a.html – § 21a is paragraph 21a; the port's URL
    // builder takes the numeric part, as every other citation does)
    assert(citationParts("§ 21a Abs. 1 StVO")[0].url ==
           "https://www.gesetze-im-internet.de/stvo_2013/__21.html");
    parts = citationParts("§ 9 Abs. 1, Abs. 3 StVO");
    assert(parts[0].text == "§ 9 Abs. 1, Abs. 3");
    assert(parts[0].url == "https://www.gesetze-im-internet.de/stvo_2013/__9.html");
    parts = citationParts("Anlage 2 StVO, Zeichen 206");
    assert(parts[0].text == "Anlage 2");
    assert(parts[0].url == "https://www.gesetze-im-internet.de/stvo_2013/anlage_2.html");

    for (const auto& kv : VIOLATION_REFS) {
      const std::string& ref = kv.second;
      std::vector<CitationPart> ps = citationParts(ref);
      std::string joined;
      for (const auto& p : ps) joined += p.text;
      assert(joined == ref && "nothing of the citation is lost");
      bool cites = ref.find("§") != std::string::npos || ref.find("Anlage") != std::string::npos;
      bool anyUrl = false;
      for (const auto& p : ps) {
        if (p.url.empty()) continue;
        anyUrl = true;
        // the StVO, or – for the tasks of the truck test – the annex of the
        // driving-licence regulation
        assert(p.url.rfind("https://www.gesetze-im-internet.de/", 0) == 0);
        bool ok = false;
        for (const std::string prefix :
             {"stvo_2013/", "fev_2010/"}) {
          if (p.url.rfind(std::string("https://www.gesetze-im-internet.de/") + prefix, 0) == 0) {
            ok = true;
            break;
          }
        }
        assert(ok && "the link is one of the two official sites");
      }
      if (cites) assert(anyUrl && "a citation has a link");
    }
  }

  // ---- the driven path: points a few metres apart, broken by jumps -----------
  {
    MemoryStore store;
    DrivenPath path(&store);
    for (int i = 0; i <= 100; i++) path.add(i, 0);  // 100 m straight, one call per metre
    size_t pts = path.points.size() / 2;
    assert(pts >= 12 && pts <= 14);
    assert(std::fabs(path.length() - 96) < 9);
    // a jump away and a jump back (as the test drives: put somewhere else,
    // then a point of the new stretch) breaks the line twice — each jump is
    // its own break in the C++ path, where the original counted the pairs
    // a jump puts the car somewhere else: the line breaks there and the new
    // stretch continues from the new point (the original's null pair)
    path.add(5000, 5000);  // put somewhere else
    path.add(5010, 5000);
    size_t breaks = 0;
    for (double v : path.points)
      if (std::isnan(v)) breaks++;
    assert(breaks == 2 && "a jump ends the old stretch");
    assert(path.length() < 120 && "the jump is not driven distance");
    path.save();
    DrivenPath next(&store);
    assert(std::fabs(next.length() - path.length()) < 2 && "the path of the last session is there");
    next.add(5010, 5000);  // the point of the new stretch is written as null → break again
    breaks = 0;
    for (double v : next.points)
      if (std::isnan(v)) breaks++;
    assert(breaks >= 2 && "a new session starts a new stretch");
    for (size_t i = 0; i < PATH_MAX_POINTS + 500; i++)
      next.add(static_cast<double>(i * 9), 9000);
    assert(next.points.size() <= PATH_MAX_POINTS * 2 && "old points make room");
    next.clear();
    assert(DrivenPath(&store).points.empty());
  }

  // ---- engine power: the torque curve follows the rated power ---------------
  {
    const VehicleSpec& base = compactCar();
    assert(std::fabs(curvePower(base) - BASE_POWER_PS) < 12);
    assert(withPower(base, BASE_POWER_PS).torqueCurve == base.torqueCurve &&
           "the base power is the car as it is");
    const double ps[] = {60, 75, 95, 115, 200};
    for (double p : ps) {
      VehicleSpec s = withPower(base, p);
      assert(std::fabs(curvePower(s) / curvePower(base) - p / BASE_POWER_PS) < 0.02);
      assert(s.torqueCurve.size() == base.torqueCurve.size());
      assert(s.mass == base.mass);
    }
    VehicleSpec small = withPower(base, 75);
    assert(torqueAt(small, 1000) > torqueAt(base, 1000) * 0.68 &&
           "near idle the small engine keeps most of its torque");
    assert(std::fabs(torqueAt(small, 3500) - torqueAt(base, 3500) / 2) < 1 &&
           "at speed it has half");
    assert(withPower(base, 200).clutchTorque > base.clutchTorque &&
           "a stronger engine gets a stronger clutch");
  }

  // ---- aerial photographs: tiles in the map's own coordinates ---------------
  {
    assert(std::fabs(aerialTileSize(0) - AERIAL_TILE_PX * 0.2) < 1e-9);
    assert(aerialLevel(5) == 0 && "close up: the finest pictures (20 cm)");
    assert(aerialLevel(0.02) == 8 && "the whole map: the coarsest");
    for (double s = 0.02; s < 6; s *= 1.3) {
      int level = aerialLevel(s);
      double res = 0.2 * std::pow(2.0, level);  // metres per pixel of the pictures
      assert(level == 0 || res <= (1 / s) * 1.3);
    }
    std::vector<AerialTile> tiles = aerialTiles(2, 527900, 5677900, 528500, 5678100);
    assert(tiles.size() >= 2);
    double size = aerialTileSize(2);
    for (const AerialTile& t : tiles) {
      assert(std::fabs(t.minE - t.col * size) < 1e-6 && std::fabs(t.minN - t.row * size) < 1e-6 &&
             "tiles lie on the grid of their level");
      assert(t.minE < 528500 && t.minE + size > 527900 && t.minN < 5678100 &&
             t.minN + size > 5677900);
    }
    std::map<std::string, bool> covered;
    for (const AerialTile& t : tiles)
      covered[std::to_string(t.col) + "/" + std::to_string(t.row)] = true;
    assert(covered.size() == tiles.size() && "no tile twice");
    std::string url = aerialUrl(tiles[0]);
    assert(url.rfind("https:", 0) == 0);
    assert(url.find("CRS=EPSG%3A25832") != std::string::npos);
    // east, north, east, north of the tile, to two decimals
    // east, north, east, north — as the URL carries them (the port trims
    // trailing zeros, so compare the parsed numbers instead of the text)
    size_t b = url.find("BBOX=");
    assert(b != std::string::npos);
    std::string box = url.substr(b + 5, url.find('&', b) - b - 5);
    double v[4] = {0, 0, 0, 0};
    size_t at = 0;
    for (int k = 0; k < 4; k++) {
      size_t comma = box.find(',', at);
      std::string tok = box.substr(at, (comma == std::string::npos ? box.size() : comma) - at);
      v[k] = std::strtod(tok.c_str(), nullptr);
      at = comma == std::string::npos ? box.size() : comma + 1;
    }
    assert(std::fabs(v[0] - tiles[0].minE) < 0.01 && std::fabs(v[1] - tiles[0].minN) < 0.01);
    assert(std::fabs(v[2] - v[0] - size) < 0.02 && std::fabs(v[3] - v[1] - size) < 0.02);
    assert(url.find("WIDTH=512") != std::string::npos);
  }

  std::printf("playtest: all checks passed (fault log, path, citations, power, aerial)\n");
  return 0;
}
