// LINK: game core platform
// SPDX-License-Identifier: AGPL-3.0-only
/**
 * What the playtest asked for, as far as it is the game's own: the log of faults and the
 * driven path survive a session.
 *
 * Origin: tests/game/playtest.test.ts @ v1.7.7 (its cases about the citations, the engine
 * power and the aerial photographs are those of src/i18n, src/sim and src/reality).
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/faultlog.h"
#include "game/track.h"

using namespace fs;

/** A browser's storage, in memory. */
struct MemoryStore : KeyValueStore {
  std::map<std::string, std::string> data;
  std::optional<std::string> getItem(const std::string& key) override {
    auto it = data.find(key);
    if (it == data.end()) return std::nullopt;
    return it->second;
  }
  bool setItem(const std::string& key, const std::string& value) override {
    data[key] = value;
    return true;
  }
  void removeItem(const std::string& key) override { data.erase(key); }
};

/** … and one that is full: nothing can be stored */
struct FullStore : KeyValueStore {
  std::optional<std::string> getItem(const std::string&) override { return std::nullopt; }
  bool setItem(const std::string&, const std::string&) override { return false; }
  void removeItem(const std::string&) override {}
};

static FaultEntry fault(double t, const std::string& id = "speed") {
  return {.t = t, .id = id, .severity = "minor", .data = {{"limit", 30.0}, {"speed", 42.0}}, .street = "Marktplatz", .mode = "free", .x = 10, .z = -20};
}

int main() {
  // the log of faults is still there in the next session
  {
    MemoryStore store;
    FaultLog log(&store);
    assert(log.entries.size() == 0);
    log.add(fault(1000));
    log.add(fault(2000, "redLight"));
    // (kept as the original keeps it)
    assert(store.data["fahrsim-baunatal.faults.v1"] ==
           R"([{"t":1000,"id":"speed","severity":"minor","data":{"limit":30,"speed":42},"street":"Marktplatz","mode":"free","x":10,"z":-20},)"
           R"({"t":2000,"id":"redLight","severity":"minor","data":{"limit":30,"speed":42},"street":"Marktplatz","mode":"free","x":10,"z":-20}])");
    FaultLog next(&store);
    assert(next.entries.size() == 2 && next.entries[0].id == "speed" && next.entries[1].id == "redLight");
    assert(next.entries[0] == fault(1000));
    next.clear();
    assert(FaultLog(&store).entries.size() == 0);
  }

  // the log keeps the newest faults, survives rubbish in the storage and a browser without storage
  {
    MemoryStore store;
    FaultLog log(&store);
    for (size_t i = 0; i < FAULT_LOG_MAX + 25; i++) log.add(fault(static_cast<double>(i)));
    assert(log.entries.size() == FAULT_LOG_MAX);
    assert(log.entries[0].t == 25);   // the oldest ones made room
    assert(FaultLog(&store).entries.size() == FAULT_LOG_MAX);

    MemoryStore broken;
    broken.setItem("fahrsim-baunatal.faults.v1", "{\"not\":\"a list\"");
    assert(FaultLog(&broken).entries.size() == 0);
    broken.setItem("fahrsim-baunatal.faults.v1",
                   R"([{"t":5,"id":"speed","severity":"minor","data":{"limit":30,"speed":42},"street":"Marktplatz","mode":"free","x":10,"z":-20},{"t":"x"},null,)"
                   R"({"t":6,"id":"speed","severity":"huge","data":{"limit":30,"speed":42},"street":"Marktplatz","mode":"free","x":10,"z":-20}])");
    const FaultLog taken(&broken);
    assert(taken.entries.size() == 1 && taken.entries[0].t == 5);   // only what is a fault is taken over
    // (what an entry lacks: no street, a free drive, the origin)
    broken.setItem("fahrsim-baunatal.faults.v1", R"([{"t":7,"id":"speed","severity":"hint","data":{"what":"kerb"},"x":"12.5"}])");
    const FaultLog bare(&broken);
    assert(bare.entries.size() == 1 && bare.entries[0].street == "" && bare.entries[0].mode == "free" && bare.entries[0].x == 12.5 && bare.entries[0].z == 0);
    assert(bare.entries[0].data.size() == 1 && std::get<std::string>(bare.entries[0].data[0].second) == "kerb");

    FaultLog none(nullptr);
    none.add(fault(1));
    assert(none.entries.size() == 1);   // without storage the log lives as long as the page
    FullStore full;
    FaultLog(&full).add(fault(1));   // (a storage that is full does not stop the game)
  }

  // the driven path: points a few metres apart, broken by jumps, kept between sessions
  {
    MemoryStore store;
    DrivenPath path(&store);
    for (int i = 0; i <= 100; i++) path.add(i, 0);   // 100 m straight on, one call per metre
    assert(path.points.size() / 2 >= 12 && path.points.size() / 2 <= 14);
    assert(std::fabs(path.length() - 96) < 9);
    path.add(5000, 5000);   // put somewhere else
    path.add(5010, 5000);
    const auto breaks = [](const DrivenPath& p) {
      size_t n = 0;
      for (double v : p.points) n += std::isnan(v) ? 1 : 0;
      return n / 2;
    };
    assert(breaks(path) == 1);   // a jump breaks the line
    assert(path.length() < 120);   // the jump is not driven distance
    path.save();
    // (kept as the original keeps it: whole metres, null for a break)
    assert(store.data["fahrsim-baunatal.path.v1"] == "[0,0,8,0,16,0,24,0,32,0,40,0,48,0,56,0,64,0,72,0,80,0,88,0,96,0,null,null,5000,5000,5010,5000]");
    DrivenPath next(&store);
    assert(std::fabs(next.length() - path.length()) < 2);   // the path of the last session is there
    next.add(5010, 5000);
    assert(breaks(next) == 2);   // a new session starts a new stretch
    for (size_t i = 0; i < PATH_MAX_POINTS + 500; i++) next.add(static_cast<double>(i) * 9, 9000);
    assert(next.points.size() <= PATH_MAX_POINTS * 2);   // old points make room
    next.clear();
    assert(DrivenPath(&store).points.size() == 0);
  }

  std::printf("game_playtest_test: ok\n");
  return 0;
}
