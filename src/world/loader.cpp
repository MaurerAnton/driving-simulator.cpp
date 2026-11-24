// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/loader.ts @ v1.7.7
/** A map, and how it gets into memory (see loader.h). */
#include "world/loader.h"

#include <cmath>
#include <exception>

#include "core/geom.h"

namespace fs {

namespace {

/**
 * How many roads / junctions are added in one go: a step of a few
 * milliseconds, so that a frame of the game in which a piece of the network
 * arrives is not held up noticeably (a junction costs several times a road:
 * its area and every path through it are worked out).
 */
const size_t EDGE_BATCH = 40;
const size_t NODE_BATCH = 12;
/** how often a chunk is asked for before the game goes on without it */
const int TRIES = 4;

/** `text.replace(/\D/g, '').slice(0, 14)` */
std::string digits14(const std::string& text) {
  std::string out;
  for (char c : text) {
    if (c >= '0' && c <= '9') out += c;
    if (out.size() == 14) break;
  }
  return out;
}

std::string chunkKey(int cx, int cz) { return std::to_string(cx) + "_" + std::to_string(cz); }

}  // namespace

std::string_view jsonTextOf(const Bytes& buf) {
  std::string_view text(reinterpret_cast<const char*>(buf.data()), buf.size());
  if (text.size() >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) text.remove_prefix(3);
  return text;
}

MapLoader::MapLoader(std::shared_ptr<MapFiles> files, std::unique_ptr<World> world__, TileGrid tiles_, std::optional<GridSet> chunks)
    : world(*world__),
      tiles(std::move(tiles_)),
      pieces(chunks.has_value()),
      // (a patch of tiles changes the address of every tile: a browser does not keep showing the ones it had)
      version(digits14(world__->json.meta.generated) + digits14(world__->json.meta.patched)),
      world_(std::move(world__)),
      files_(std::move(files)),
      chunks_(std::move(chunks)) {}

Task<std::shared_ptr<MapLoader>> MapLoader::open(std::shared_ptr<MapFiles> files, WorldOptions opts) {
  std::optional<MapIndexJson> index = co_await readJson<MapIndexJson>(files, "map.json");
  if (index) {
    WorldJson json;
    json.version = index->version;
    json.meta = index->meta;
    json.places = std::move(index->places);
    json.spawns = std::move(index->spawns);
    if (index->yard) json.yard = index->yard;
    auto tileSet = std::make_shared<const GridSet>(GridSet::from(index->tiles));
    TileGrid grid;
    grid.x0 = -GRID_OFFSET;
    grid.z0 = -GRID_OFFSET;
    grid.size = TILE;
    grid.has = [tileSet](int ix, int iz) { return tileSet->has(ix, iz); };
    co_return std::make_shared<MapLoader>(files, std::make_unique<World>(std::move(json), std::move(opts)), std::move(grid), GridSet::from(index->chunks));
  }
  std::optional<WorldJson> json = co_await readJson<WorldJson>(files, "world.json");
  // (the map is not part of the repository: `npm run map:fetch` gets it, see docs/MAP_DATA.md)
  if (!json) throw std::runtime_error("The map is missing (public/map/map.json). Fetch it with: npm run map:fetch");
  auto have = std::make_shared<const std::unordered_set<std::string>>(json->tiles.begin(), json->tiles.end());
  TileGrid grid;
  grid.x0 = json->meta.minX;
  grid.z0 = json->meta.minZ;
  grid.size = json->meta.tileSize;
  grid.has = [have](int ix, int iz) { return have->count(std::to_string(ix) + "_" + std::to_string(iz)) > 0; };
  co_return std::make_shared<MapLoader>(files, std::make_unique<World>(std::move(*json), std::move(opts)), std::move(grid), std::nullopt);
}

void MapLoader::listen(MapListener l) { listeners_.push_back(std::move(l)); }

bool MapLoader::isLoaded(const std::string& key) const {
  for (const auto& entry : loaded_)
    if (entry.first == key) return true;
  return false;
}

std::vector<std::string> MapLoader::around(double x, double z, double radius) const {
  std::vector<std::string> out;
  if (!chunks_) return out;
  for (int cx = gridIndex(x - radius, CHUNK); cx <= gridIndex(x + radius, CHUNK); cx++) {
    for (int cz = gridIndex(z - radius, CHUNK); cz <= gridIndex(z + radius, CHUNK); cz++) {
      if (!chunks_->has(cx, cz)) continue;
      // (the point of the chunk nearest to the place)
      const double nx = jsMax(gridStart(cx, CHUNK), jsMin(gridStart(cx + 1, CHUNK), x));
      const double nz = jsMax(gridStart(cz, CHUNK), jsMin(gridStart(cz + 1, CHUNK), z));
      if (jsHypot(nx - x, nz - z) <= radius) out.push_back(chunkKey(cx, cz));
    }
  }
  return out;
}

double MapLoader::progress(double x, double z, double radius) {
  int n = 0;
  int have = 0;
  for (const std::string& key : around(x, z, radius)) {
    n++;
    if (gaveUp(key) || (isLoaded(key) && !pending_.count(key))) have++;
  }
  return n > 0 ? static_cast<double>(have) / n : 1;
}

bool MapLoader::ready(double x, double z, double radius) {
  for (const std::string& key : around(x, z, radius)) {
    if (gaveUp(key)) continue;
    if (!isLoaded(key) || pending_.count(key)) return false;
  }
  return true;
}

bool MapLoader::gaveUp(const std::string& key) const {
  auto f = failed_.find(key);
  return (f == failed_.end() ? 0 : f->second.n) >= TRIES;
}

bool MapLoader::mayFetch(const std::string& key) const {
  auto f = failed_.find(key);
  return f == failed_.end() || (f->second.n < TRIES && plat::nowMs() - f->second.at > 1500 * std::pow(2, f->second.n - 1));
}

Task<void> MapLoader::fetchChunk(std::string key) {
  // (the loader lives on while a chunk is on its way)
  std::shared_ptr<MapLoader> self = shared_from_this();
  fetching_.insert(key);
  std::string failure;
  bool failedNow = false;
  try {
    std::optional<WorldChunkJson> chunk = co_await readJson<WorldChunkJson>(files_, "w/" + key + ".json.gz");
    if (!chunk) throw std::runtime_error("not there");
    failed_.erase(key);
    // roads first (in portions), then the junctions: a junction needs all its roads
    std::vector<int> nodeIds;
    nodeIds.reserve(chunk->nodes.size());
    for (const auto& n : chunk->nodes) nodeIds.push_back(n.first);
    bool known = false;
    for (auto& entry : loaded_) {
      if (entry.first == key) {
        entry.second = std::move(nodeIds);
        known = true;
        break;
      }
    }
    if (!known) loaded_.emplace_back(key, std::move(nodeIds));
    int parts = 0;
    for (size_t i = 0; i < chunk->edges.size(); i += EDGE_BATCH) {
      Work w;
      w.key = key;
      const size_t end = std::min(chunk->edges.size(), i + EDGE_BATCH);
      w.part.edges.assign(std::make_move_iterator(chunk->edges.begin() + static_cast<long>(i)), std::make_move_iterator(chunk->edges.begin() + static_cast<long>(end)));
      if (i == 0) w.part.signals = std::move(chunk->signals);
      work_.push_back(std::move(w));
      parts++;
    }
    for (size_t i = 0; i < chunk->nodes.size(); i += NODE_BATCH) {
      Work w;
      w.key = key;
      const size_t end = std::min(chunk->nodes.size(), i + NODE_BATCH);
      w.part.nodes.assign(std::make_move_iterator(chunk->nodes.begin() + static_cast<long>(i)), std::make_move_iterator(chunk->nodes.begin() + static_cast<long>(end)));
      work_.push_back(std::move(w));
      parts++;
    }
    if (parts > 0) pending_[key] = parts;
  } catch (const std::exception& err) {
    failedNow = true;
    failure = err.what();
  } catch (...) {
    failedNow = true;
    failure = "unknown error";
  }
  if (failedNow) {
    plat::warn("chunk of the road network failed " + key + " " + failure);
    Failed& f = failed_[key];
    f.n = f.n + 1;
    f.at = plat::nowMs();
  }
  fetching_.erase(key);
}

void MapLoader::addWork(double budgetMs) {
  const double until = plat::nowMs() + budgetMs;
  while (!work_.empty()) {
    Work w = std::move(work_.front());
    work_.pop_front();
    // (a chunk that was dropped again before it was all in)
    if (isLoaded(w.key)) {
      const WorldChange change = world.add(w.part);
      if (!change.edges.empty() || !change.nodes.empty()) {
        for (const MapListener& l : listeners_)
          if (l.added) l.added(change);
      }
    }
    auto p = pending_.find(w.key);
    const int left = (p == pending_.end() ? 1 : p->second) - 1;
    if (left > 0) pending_[w.key] = left;
    else pending_.erase(w.key);
    if (plat::nowMs() >= until) break;
  }
}

void MapLoader::update(double x, double z, double budgetMs) {
  if (!chunks_) return;
  for (const std::string& key : around(x, z, loadRadius)) {
    if (!isLoaded(key) && !fetching_.count(key) && mayFetch(key) && fetching_.size() < 3) fetchChunk(key).detach();
  }
  if (!work_.empty()) addWork(budgetMs);
  else {
    // far behind: away with it (one chunk per call)
    for (size_t i = 0; i < loaded_.size(); i++) {
      const std::string& key = loaded_[i].first;
      if (pending_.count(key)) continue;
      const size_t bar = key.find('_');
      const int cx = static_cast<int>(jsNumber(key.substr(0, bar)));
      const int cz = static_cast<int>(jsNumber(bar == std::string::npos ? std::string() : key.substr(bar + 1)));
      const double nx = jsMax(gridStart(cx, CHUNK), jsMin(gridStart(cx + 1, CHUNK), x));
      const double nz = jsMax(gridStart(cz, CHUNK), jsMin(gridStart(cz + 1, CHUNK), z));
      if (jsHypot(nx - x, nz - z) <= keepRadius) continue;
      const std::vector<int> nodes = std::move(loaded_[i].second);
      loaded_.erase(loaded_.begin() + static_cast<long>(i));
      const WorldChange change = world.remove(nodes);
      if (!change.edges.empty() || !change.nodes.empty()) {
        for (const MapListener& l : listeners_)
          if (l.removed) l.removed(change);
      }
      break;
    }
  }
}

Task<void> MapLoader::loadAround(double x, double z, double radius) {
  if (!chunks_) co_return;
  std::shared_ptr<MapLoader> self = shared_from_this();
  std::vector<std::string> keys;
  for (const std::string& k : around(x, z, radius))
    if (!isLoaded(k) && mayFetch(k)) keys.push_back(k);
  std::vector<Task<void>> fetches;
  for (const std::string& k : keys)
    if (!fetching_.count(k)) fetches.push_back(fetchChunk(k));
  co_await whenAll(std::move(fetches));
  while (!fetching_.empty()) co_await plat::sleep(5);
  addWork(JS_INF);
}

Task<void> MapLoader::loadAll() {
  if (!chunks_) co_return;
  std::shared_ptr<MapLoader> self = shared_from_this();
  for (int cz = chunks_->z0; cz < chunks_->z0 + chunks_->h; cz++) {
    for (int cx = chunks_->x0; cx < chunks_->x0 + chunks_->w; cx++) {
      const std::string key = chunkKey(cx, cz);
      if (chunks_->has(cx, cz) && !isLoaded(key)) co_await fetchChunk(key);
    }
  }
  addWork(JS_INF);
}

Task<std::shared_ptr<const NetData>> MapLoader::readNet() {
  std::shared_ptr<MapLoader> self = shared_from_this();
  std::optional<Bytes> buf = co_await files_->bytes("net.bin.gz");
  if (!buf) co_return nullptr;
  co_return std::make_shared<const NetData>(unpackNet(std::move(*buf)));
}

Task<std::shared_ptr<const NetData>> MapLoader::net() {
  if (!netData_.valid()) netData_ = pieces ? readNet() : resolved(std::shared_ptr<const NetData>());
  return netData_;
}

Task<std::shared_ptr<const std::vector<RailJson>>> MapLoader::readRails() {
  std::shared_ptr<MapLoader> self = shared_from_this();
  std::optional<RailsFileJson> r = co_await readJson<RailsFileJson>(files_, "rails.json.gz");
  co_return std::make_shared<const std::vector<RailJson>>(r ? std::move(r->rails) : std::vector<RailJson>());
}

Task<std::shared_ptr<const std::vector<RailJson>>> MapLoader::rails() {
  if (!railData_.valid()) railData_ = pieces ? readRails() : resolved(std::make_shared<const std::vector<RailJson>>(world.json.rails));
  return railData_;
}

template <class T>
Task<std::shared_ptr<const T>> MapLoader::readOptional(std::string path) {
  std::shared_ptr<MapLoader> self = shared_from_this();
  std::shared_ptr<const T> out;
  // (`.catch(() => null)`: a file that cannot be read is a file that is not there)
  try {
    std::optional<T> v = co_await readJson<T>(files_, path);
    if (v) out = std::make_shared<const T>(std::move(*v));
  } catch (...) {
    out = nullptr;
  }
  co_return out;
}

Task<std::shared_ptr<const StopsJson>> MapLoader::stops() {
  if (!stopData_.valid()) stopData_ = pieces ? readOptional<StopsJson>("stops.json.gz") : resolved(std::shared_ptr<const StopsJson>());
  return stopData_;
}

Task<std::shared_ptr<const Bytes>> MapLoader::readFar() {
  std::shared_ptr<MapLoader> self = shared_from_this();
  std::shared_ptr<const Bytes> out;
  try {
    std::optional<Bytes> buf = co_await files_->bytes("far.bin.gz");
    if (buf) out = std::make_shared<const Bytes>(std::move(*buf));
  } catch (...) {
    out = nullptr;
  }
  co_return out;
}

Task<std::shared_ptr<const Bytes>> MapLoader::far() {
  if (!farData_.valid()) farData_ = pieces ? readFar() : resolved(std::shared_ptr<const Bytes>());
  return farData_;
}

Task<std::shared_ptr<const LinesJson>> MapLoader::lines() {
  if (!lineData_.valid()) lineData_ = pieces ? readOptional<LinesJson>("lines.json.gz") : resolved(std::shared_ptr<const LinesJson>());
  return lineData_;
}

Task<std::shared_ptr<const TimetableJson>> MapLoader::timetable() {
  if (!timetableData_.valid()) timetableData_ = pieces ? readOptional<TimetableJson>("timetable.json.gz") : resolved(std::shared_ptr<const TimetableJson>());
  return timetableData_;
}

Task<std::shared_ptr<const AirJson>> MapLoader::air() {
  if (!airData_.valid()) airData_ = pieces ? readOptional<AirJson>("air.json.gz") : resolved(std::shared_ptr<const AirJson>());
  return airData_;
}

}  // namespace fs
