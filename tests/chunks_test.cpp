// SPDX-License-Identifier: AGPL-3.0-only
// LINK: world core platform geo
// Origin: tests/world/chunks.test.ts @ v1.7.7
/**
 * A road network that comes in pieces: the same network as in one piece,
 * whatever is loaded and dropped on the way.
 *
 * (The original's last test drives a route through parts of the map that are
 * not loaded yet with the navigation of the game – src/game/nav.ts: it belongs
 * to the game layer.  In its place the loader is asked a few more things here:
 * a map in one piece, a chunk that cannot be fetched, the files a map may be
 * without.)
 */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "platform/native/native.h"
#include "tests/town.h"
#include "world/grid.h"
#include "world/lanes.h"
#include "world/loader.h"
#include "world/netfile.h"
#include "world/world.h"
#include "world/worldjson.h"

using namespace fs;

namespace {

/** The pieces of a map in one piece: its junctions sorted into chunks by `chunkOf`, each with every road at them (in the order the chunks were first met). */
template <class ChunkOf>
std::vector<std::pair<std::string, WorldChunkJson>> cut(const WorldJson& json, ChunkOf&& chunkOf) {
  std::vector<std::pair<std::string, WorldChunkJson>> out;
  for (size_t i = 0; i < json.nodes.size(); i++) {
    const NodeJson& n = json.nodes[i];
    const std::string key = chunkOf(n.x, n.z);
    WorldChunkJson* c = nullptr;
    for (auto& entry : out)
      if (entry.first == key) c = &entry.second;
    if (!c) {
      out.emplace_back(key, WorldChunkJson());
      c = &out.back().second;
    }
    c->nodes.emplace_back(static_cast<int>(i), n);
    for (const ArmJson& arm : n.arms) {
      bool have = false;
      for (const auto& e : c->edges) have = have || e.first == arm.e;
      if (have) continue;
      EdgeJson e = json.edges[static_cast<size_t>(arm.e)];
      e.q = packEdgePoints(e.p);
      e.p.clear();
      c->edges.emplace_back(arm.e, std::move(e));
    }
  }
  return out;
}

const WorldChunkJson& piece(const std::vector<std::pair<std::string, WorldChunkJson>>& pieces, const std::string& key) {
  for (const auto& entry : pieces)
    if (entry.first == key) return entry.second;
  assert(false && "no such piece");
  return pieces.front().second;
}

WorldJson empty(const WorldJson& json) {
  WorldJson e = json;
  e.nodes.clear();
  e.edges.clear();
  e.banned.clear();
  e.signals.clear();
  e.crossings.clear();
  return e;
}

std::pair<size_t, size_t> count(const LaneGraph& lanes) { return {lanes.lanes.count(), lanes.connectors.size()}; }

size_t stubsOf(const World& world) {
  size_t n = 0;
  for (const Node* node : world.allNodes)
    if (node->stub) n++;
  return n;
}

/** (as src/sim/follow.ts keeps the lanes in step with the network) */
void followMap(MapLoader& map, LaneGraph& lanes) {
  MapListener l;
  l.added = [&lanes](const WorldChange& c) {
    for (Edge* e : c.edges) lanes.addEdge(*e);
    for (Node* n : c.nodes) lanes.completeNode(*n);
  };
  l.removed = [&lanes](const WorldChange& c) {
    for (Node* n : c.nodes) lanes.removeNode(*n);
    for (Edge* e : c.edges) lanes.removeEdge(*e);
  };
  map.listen(l);
}

/** The files of a map kept in memory: their texts by their paths.  The answer comes a turn later, as a promise's does. */
class MemoryFiles : public MapFiles {
 public:
  std::map<std::string, std::string> files;
  int asked = 0;

  Task<std::optional<Bytes>> bytes(std::string path) override {
    if (path.rfind("w/", 0) == 0) asked++;
    std::optional<Bytes> data;
    auto f = files.find(path);
    if (f != files.end()) data = Bytes(f->second.begin(), f->second.end());
    Resolver<std::optional<Bytes>> r;
    plat::soon([r, data = std::move(data)]() mutable { r.resolve(std::move(data)); });
    return r.task();
  }
};

std::string chunkKey(double x, double z) { return std::to_string(static_cast<int>(std::floor((x + 200000) / 4000))) + "_" + std::to_string(static_cast<int>(std::floor((z + 200000) / 4000))); }

/** a town 24 km wide, so that it spans several chunks of 4 km – as the files of a map in pieces */
std::shared_ptr<MemoryFiles> townFiles(const Town& town, size_t* pieceCount = nullptr) {
  const auto pieces = cut(town.json, chunkKey);
  MapIndexJson index;
  index.version = 2;
  index.meta = town.json.meta;
  std::vector<std::pair<int, int>> squares;
  for (const auto& entry : pieces) {
    const size_t bar = entry.first.find('_');
    squares.emplace_back(std::stoi(entry.first.substr(0, bar)), std::stoi(entry.first.substr(bar + 1)));
  }
  index.tiles = GridSet::of({}).toJSON();
  index.chunks = GridSet::of(squares).toJSON();
  index.nodes = static_cast<int>(town.json.nodes.size());
  index.edges = static_cast<int>(town.json.edges.size());
  auto files = std::make_shared<MemoryFiles>();
  files->files["map.json"] = jsonText(index);
  for (const auto& entry : pieces) files->files["w/" + entry.first + ".json.gz"] = jsonText(entry.second);
  if (pieceCount) *pieceCount = pieces.size();
  return files;
}

Task<void> loaderKeepsTheNetworkAround() {
  // a town 24 km wide, so that it spans several chunks of 4 km
  TownOptions o;
  o.cols = 13;
  o.rows = 3;
  o.block = 2000;
  const Town town = makeTown(o);
  const auto whole = count(*town.lanes);
  size_t pieceCount = 0;
  std::shared_ptr<MemoryFiles> files = townFiles(town, &pieceCount);
  std::shared_ptr<MapLoader> mapPtr = co_await MapLoader::open(files);
  MapLoader& map = *mapPtr;
  assert(map.pieces == true);
  LaneGraph lanes(map.world);
  followMap(map, lanes);
  map.loadRadius = 3000;
  map.keepRadius = 7000;
  assert(map.ready(1000, 2000) == false);
  co_await map.loadAround(1000, 2000);
  assert(map.ready(1000, 2000) == true);
  const auto near = count(lanes);
  assert(near.first > 0 && near.first < whole.first);  // a part of the network
  assert(map.ready(20000, 2000) == false);
  assert(map.progress(1000, 2000) == 1 && map.progress(20000, 2000) == 0);
  // drive east, a little at a time: the network follows
  SurfaceHit hit;
  for (double x = 1000; x <= 23000; x += 500) {
    co_await map.loadAround(x, 2000);
    for (int i = 0; i < 10; i++) map.update(x, 2000, JS_INF);
    assert(map.ready(x, 2000) == true);
    assert(map.world.surfaceAt(x, 2000, 101, hit));  // road under the wheels
  }
  assert(count(lanes).first < whole.first);  // the west was dropped on the way
  assert(map.world.surfaceAt(1000, 2000, 101, hit) == false);
  // everything at once, for tools
  co_await map.loadAll();
  assert(count(lanes) == whole);
  assert(static_cast<size_t>(files->asked) >= pieceCount);
}

Task<void> updateBringsThePiecesInPortions() {
  // the game's way: `update` every frame – a few pieces are asked for at a time, and what has arrived is added in portions
  TownOptions o;
  o.cols = 13;
  o.rows = 3;
  o.block = 2000;
  const Town town = makeTown(o);
  std::shared_ptr<MemoryFiles> files = townFiles(town);
  std::shared_ptr<MapLoader> mapPtr = co_await MapLoader::open(files);
  MapLoader& map = *mapPtr;
  LaneGraph lanes(map.world);
  followMap(map, lanes);
  map.loadRadius = 9000;
  map.keepRadius = 12000;
  int addedCalls = 0;
  MapListener l;
  l.added = [&addedCalls](const WorldChange&) { addedCalls++; };
  map.listen(l);
  map.update(12000, 2000, 0);
  // (nothing has arrived yet: the answers come with the next turn of the loop – and no more than three pieces are on their way at a time)
  assert(map.world.allEdges.size() == 0 && files->asked == 3 && !map.ready(12000, 2000) && map.progress(12000, 2000) == 0);
  int frames = 0;
  while (!map.ready(12000, 2000) && frames < 2000) {
    co_await plat::sleep(0);
    map.update(12000, 2000, 0);   // (no time at all: one portion with each call)
    frames++;
  }
  assert(map.ready(12000, 2000) && frames >= 6 && addedCalls >= 6);
  const double p = map.progress(12000, 2000);
  assert(p == 1);
  // what lies within reach is whole: no junction there is a stub
  for (const Node* n : map.world.allNodes)
    if (std::fabs(n->x - 12000) < 6000) assert(!n->stub);
}

Task<void> mapInOnePiece() {
  // a map in one piece (world.json): read whole, nothing to load or to drop
  const Town town = makeTown({});
  WorldJson json = town.json;
  json.meta.generated = "2026-01-02T03:04:05.678Z";
  json.tiles = {"0_0", "1_0"};
  RailJson rail;
  rail.k = RAIL_TRAM;
  rail.p = {0, 100, 0, 50, 100, 0};
  rail.st = {10};
  rail.sn = {"Markt"};
  json.rails.push_back(rail);
  YardJson yard;
  yard.x = 5;
  yard.len = 150;
  json.yard = yard;
  auto files = std::make_shared<MemoryFiles>();
  files->files["world.json"] = jsonText(json);
  std::shared_ptr<MapLoader> map = co_await MapLoader::open(files);
  assert(!map->pieces && map->version == "20260102030405");
  assert(map->world.allEdges.size() == town.world->allEdges.size() && map->world.allNodes.size() == town.world->allNodes.size());
  assert(map->tiles.x0 == json.meta.minX && map->tiles.z0 == json.meta.minZ && map->tiles.size == 500 && map->tiles.has(1, 0) && !map->tiles.has(0, 1));
  assert(map->ready(0, 0) && map->progress(0, 0) == 1);
  co_await map->loadAround(0, 0);
  co_await map->loadAll();
  map->update(0, 0);
  assert(files->asked == 0);
  const std::shared_ptr<const NetData> net = co_await map->net();
  assert(net == nullptr);
  const std::shared_ptr<const std::vector<RailJson>> rails = co_await map->rails();
  assert(rails && rails->size() == 1 && (*rails)[0].sn[0] == "Markt" && (*rails)[0].st[0] == 10 && !(*rails)[0].e);
  assert(co_await map->stops() == nullptr && co_await map->far() == nullptr && co_await map->lines() == nullptr && co_await map->timetable() == nullptr && co_await map->air() == nullptr);
  assert(map->world.json.yard && map->world.json.yard->len == 150);
  const std::optional<WorldJson> again = co_await map->json<WorldJson>("world.json");
  assert(again && again->edges.size() == json.edges.size());
  const std::optional<WorldJson> none = co_await map->json<WorldJson>("nothing.json");
  assert(!none);
  // no map at all: said so
  bool threw = false;
  try {
    co_await MapLoader::open(std::make_shared<MemoryFiles>());
  } catch (const std::exception& e) {
    threw = std::string(e.what()).rfind("The map is missing", 0) == 0;
  }
  assert(threw);
}

Task<void> piecesThatCannotBeFetched() {
  // a chunk the index names but the server does not have: asked for a few times, each time after a longer while – then the game goes on without it
  TownOptions o;
  o.cols = 5;
  o.rows = 2;
  o.block = 2000;
  const Town town = makeTown(o);
  std::shared_ptr<MemoryFiles> files = townFiles(town);
  // (the town spans the chunks 50_50, 51_50, 52_50: the middle one is lost, another one is no JSON)
  assert(files->files.count("w/51_50.json.gz") == 1);
  files->files.erase("w/51_50.json.gz");
  files->files["w/52_50.json.gz"] = "{\"v\":1,\"nodes\":[";
  // what a map in pieces may have beside its roads
  files->files["rails.json.gz"] = "{\"rails\":[{\"k\":1,\"p\":[0,1,2,3,4,5],\"a\":[],\"b\":[2],\"e\":1}]}";
  files->files["stops.json.gz"] = "{\"v\":1,\"towns\":[\"Kassel\"],\"places\":[[\"Rathaus\",0,64,1.5,2.5]],\"poles\":[[0,1,2,65]],\"platforms\":[[0,64,2.5,[1,2,3,4]]]}";
  files->files["lines.json.gz"] = "not json at all";
  files->files["air.json.gz"] = "{\"v\":1,\"airfields\":[{\"name\":\"Calden\",\"icao\":\"EDVK\",\"kind\":\"airport\",\"x\":1,\"z\":2,\"runways\":[{\"ref\":\"09/27\",\"x1\":0,\"z1\":0,\"x2\":2500,\"z2\":0,\"w\":45,\"s\":\"asphalt\"}],\"taxiways\":[{\"w\":15,\"s\":\"asphalt\",\"p\":[1,2,3,4]}],\"aprons\":[{\"s\":\"concrete\",\"p\":[1,2,3,4,5,6]}]}]}";
  files->files["far.bin.gz"] = std::string("FAR?", 4);
  plat::native::manualClock(true);
  std::shared_ptr<MapLoader> mapPtr = co_await MapLoader::open(files);
  MapLoader& map = *mapPtr;
  map.loadRadius = 5000;
  co_await map.loadAround(4000, 1000);
  // the piece that is there is in; the two others are not, and are not given up yet
  assert(!map.ready(4000, 1000) && map.world.allEdges.size() > 0);
  const double third = map.progress(4000, 1000);
  assert(std::fabs(third - 1.0 / 3) < 1e-12);
  const int askedOnce = files->asked;
  assert(askedOnce == 3);
  // at once again: not asked (a connection on the road comes and goes – but not within a moment)
  co_await map.loadAround(4000, 1000);
  assert(files->asked == askedOnce);
  // after 1.5 s the second try, after 3 s more the third, after 6 s more the fourth – and that was it
  for (double wait : {1600.0, 3100.0, 6100.0}) {
    plat::native::advance(wait);
    co_await map.loadAround(4000, 1000);
  }
  assert(files->asked == askedOnce + 3 * 2);
  assert(map.ready(4000, 1000));  // (given up: the game goes on without them)
  assert(map.progress(4000, 1000) == 1);
  plat::native::advance(100000);
  co_await map.loadAround(4000, 1000);
  assert(files->asked == askedOnce + 3 * 2);
  plat::native::manualClock(false);

  // the other files of the map: what is there is read (once), what is not or cannot be read is none
  const std::shared_ptr<const std::vector<RailJson>> rails = co_await map.rails();
  assert(rails && rails->size() == 1 && (*rails)[0].k == RAIL_LIGHT && (*rails)[0].b == std::vector<int>{2} && (*rails)[0].e == 1 && (*rails)[0].st.empty());
  assert((co_await map.rails()).get() == rails.get());
  const std::shared_ptr<const StopsJson> stops = co_await map.stops();
  assert(stops && stops->towns[0] == "Kassel" && std::get<0>(stops->places[0]) == "Rathaus" && std::get<2>(stops->places[0]) == 64 && std::get<4>(stops->places[0]) == 2.5 && std::get<3>(stops->poles[0]) == 65 &&
         std::get<3>(stops->platforms[0]).size() == 4);
  assert(co_await map.lines() == nullptr);      // (not JSON: as if it were not there)
  assert(co_await map.timetable() == nullptr);  // (not there)
  const std::shared_ptr<const AirJson> air = co_await map.air();
  assert(air && air->airfields[0].icao == "EDVK" && air->airfields[0].runways[0].x2 == 2500 && air->airfields[0].taxiways[0].p.size() == 4 && air->airfields[0].aprons[0].s == "concrete");
  const std::shared_ptr<const Bytes> far = co_await map.far();
  assert(far && far->size() == 4);
  assert(co_await map.net() == nullptr);  // (no net.bin.gz)
}

}  // namespace

int main() {
  // --- the course of a road survives being packed into steps of whole centimetres
  {
    const std::vector<double> p = {12.34, 100.5, -7.01, 13.0, 100.56, -9.99, -250.25, 99.0, 1234.56};
    const std::vector<double> back = edgePoints(packEdgePoints(p));
    assert(back.size() == p.size());
    for (size_t i = 0; i < p.size(); i++) assert(std::fabs(back[i] - p[i]) < 0.0051);
  }

  // --- a network added in two pieces is the network in one piece
  {
    TownOptions o;
    o.cols = 6;
    o.rows = 5;
    o.oneWay = {{2, 1, 3, 1}};
    o.deadEnd = 90;
    const Town town = makeTown(o);
    const auto whole = count(*town.lanes);
    const auto pieces = cut(town.json, [](double x, double) { return std::string(x < 250 ? "west" : "east"); });
    World world(empty(town.json));
    LaneGraph lanes(world);
    auto add = [&](const std::string& key) {
      const WorldChange change = world.add(piece(pieces, key));
      for (Edge* e : change.edges) lanes.addEdge(*e);
      for (Node* n : change.nodes) lanes.completeNode(*n);
    };
    add("west");
    // the streets that lead on to the east end at junctions of which nothing else is known yet
    size_t stubs = 0;
    for (const Node* n : world.allNodes) {
      if (!n->stub) continue;
      stubs++;
      assert(n->arms.size() == 1);
      assert(lanes.pathsThrough(*n).size() == 0);  // nothing leads through them
    }
    assert(stubs == 5);  // one for each street across the border
    assert(count(lanes).second < whole.second);
    add("east");
    assert(stubsOf(world) == 0);
    assert(world.allEdges.size() == town.world->allEdges.size());
    assert(count(lanes) == whole);  // as many lanes and paths through junctions as in one piece
    // every junction has its roads in the same order, every road its ends
    for (const Node* n : town.world->allNodes) {
      const Node* m = world.nodes[n->id];
      assert(m && m->arms.size() == n->arms.size());
      for (size_t i = 0; i < n->arms.size(); i++) {
        assert(m->arms[i]->edge->id == n->arms[i]->edge->id);
        assert(m->arms[i]->out == n->arms[i]->out && m->arms[i]->role == n->arms[i]->role && jsRound(m->arms[i]->cut * 100) == jsRound(n->arms[i]->cut * 100));
      }
      assert((m->shape == nullptr) == (n->shape == nullptr));
    }
    // a way across the border between the pieces
    const Lane* from = lanes.edgeLanes[town.street(0, 0, 1, 0)]->fwd[0];
    assert(!lanes.route(*from, *world.edges[town.street(4, 4, 5, 4)]).empty());  // a route from one piece into the other
  }

  // --- a piece that is dropped leaves the rest whole, and comes back as it was
  {
    TownOptions o;
    o.cols = 6;
    o.rows = 5;
    const Town town = makeTown(o);
    const auto whole = count(*town.lanes);
    const auto pieces = cut(town.json, [](double x, double) { return std::string(x < 250 ? "west" : "east"); });
    World world(empty(town.json));
    LaneGraph lanes(world);
    auto add = [&](const std::string& key) {
      const WorldChange change = world.add(piece(pieces, key));
      for (Edge* e : change.edges) lanes.addEdge(*e);
      for (Node* n : change.nodes) lanes.completeNode(*n);
    };
    add("west");
    add("east");
    {
      World nothing(empty(town.json));
      LaneGraph none(nothing);
      assert(count(none) == (std::pair<size_t, size_t>{0, 0}));
    }
    // away with the east
    std::vector<int> eastIds;
    for (const auto& n : piece(pieces, "east").nodes) eastIds.push_back(n.first);
    const WorldChange gone = world.remove(eastIds);
    for (Node* n : gone.nodes) lanes.removeNode(*n);
    for (Edge* e : gone.edges) lanes.removeEdge(*e);
    assert(gone.edges.size() > 0 && gone.nodes.size() == 15);
    for (const Edge* e : gone.edges) assert(world.edges[e->id] == nullptr);
    // (what went can still be looked at: it is the listeners' turn now)
    for (const Edge* e : gone.edges) assert(e->len > 0 && e->a && e->b && e->armA->edge == e);
    // what is left is what the west alone gives
    World alone(empty(town.json));
    alone.add(piece(pieces, "west"));
    assert(world.allEdges.size() == alone.allEdges.size());
    assert(world.allNodes.size() == alone.allNodes.size());
    {
      LaneGraph aloneLanes(alone);
      assert(count(lanes) == count(aloneLanes));
    }
    assert(world.crossings.size() == 0);
    // nothing of the east is found by place any more, the west still is
    SurfaceHit hit;
    assert(world.surfaceAt(450, 200, 101, hit) == false);
    assert(world.surfaceAt(50, 100, 101, hit) == true);
    // … and back again
    add("east");
    assert(count(lanes) == whole);
    assert(world.surfaceAt(450, 200, 101, hit) == true);
    // (the lanes and paths got new numbers: none is given twice)
    assert(lanes.lanes.size() > lanes.lanes.count());
    for (const Connector* c : lanes.connectors) assert(lanes.lanes[c->from->id] == c->from && lanes.lanes[c->to->id] == c->to);
  }

  // --- the loader keeps the network around a place loaded and drops what lies far away
  plat::native::wait(loaderKeepsTheNetworkAround());

  // --- beside the original's tests
  plat::native::wait(updateBringsThePiecesInPortions());
  plat::native::wait(mapInOnePiece());
  plat::native::wait(piecesThatCannotBeFetched());

  std::printf("chunks: all checks passed\n");
  return 0;
}
