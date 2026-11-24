// SPDX-License-Identifier: AGPL-3.0-only
// Origin: src/world/loader.ts @ v1.7.7
/**
 * A map, and how it gets into memory.
 *
 * A map in one piece (world.json: small test maps, maps of older builds) is
 * read whole.  A map in pieces (map.json, written by tools/build-map.ts) is
 * a region too large for that: an index says what there is; the road
 * network comes in chunks of 4 km that are loaded around the place where
 * something happens and dropped again far behind; one small file holds every
 * road of the region in a few numbers, for planning routes and drawing the
 * whole map (netfile.h).
 *
 * The same loader serves the game (files fetched from the server) and the
 * tools and tests (files read from disk): how a file is got is handed in.
 *
 * What the original hands out as a promise is a `Task` here.  The big things a
 * map has once (the network of the whole region, its tracks, its stops …) are
 * handed out behind a shared pointer – null where the original has null –, so
 * that nobody copies them; the task that brings them is the loader's own, read
 * as often as one likes: `auto net = co_await map.net();`.
 */
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/task.h"
#include "platform/json.h"
#include "platform/plat.h"
#include "world/grid.h"
#include "world/netfile.h"
#include "world/types.h"
#include "world/world.h"
#include "world/worldjson.h"

namespace fs {

/** How the files of a map folder are read. */
class MapFiles {
 public:
  virtual ~MapFiles() = default;
  /** a file of the folder as bytes (gunzipped if it is gzipped), or null if it is not there */
  virtual Task<std::optional<Bytes>> bytes(std::string path) = 0;
};

/** Which tiles a map has, and where they lie. */
struct TileGrid {
  /** the corner of tile (0, 0) and the side of a tile */
  double x0 = 0;
  double z0 = 0;
  double size = 0;
  std::function<bool(int ix, int iz)> has;
};

struct MapListener {
  /** roads and whole junctions that were added to the world */
  std::function<void(const WorldChange& change)> added;
  /** roads and junctions that have just been taken out of the world – they still hold what they held */
  std::function<void(const WorldChange& change)> removed;
};

/** The text of a file as `new TextDecoder().decode(buf)` hands it on: without a byte order mark at its start. */
std::string_view jsonTextOf(const Bytes& buf);

/**
 * A JSON file of a map folder read into a record (anything fromJson knows), or null if it is
 * not there.  Throws std::runtime_error if it is there but is not JSON.
 */
template <class T>
Task<std::optional<T>> readJson(std::shared_ptr<MapFiles> files, std::string path) {
  std::optional<Bytes> buf = co_await files->bytes(path);
  if (!buf) co_return std::nullopt;
  T out{};
  if (!jsonParse(jsonTextOf(*buf), out)) throw std::runtime_error(path + " is not JSON");
  co_return out;
}

class MapLoader : public std::enable_shared_from_this<MapLoader> {
 public:
  World& world;
  const TileGrid tiles;
  /** does the road network come in chunks? */
  const bool pieces;
  /**
   * Which build of the map this is (from the time it was made): part of the
   * address of every other file of the map, so that a browser never mixes
   * files it kept from an older map with those of a newer one.
   */
  const std::string version;
  /** the network is kept loaded within this distance (m) of the place of interest, and dropped beyond the other */
  double loadRadius = 3200;
  double keepRadius = 7000;

  static Task<std::shared_ptr<MapLoader>> open(std::shared_ptr<MapFiles> files, WorldOptions opts = {});

  void listen(MapListener l);

  /** The share of the pieces of the network within `radius` of a place that are there (0 … 1: for a progress bar). */
  double progress(double x, double z) { return progress(x, z, loadRadius); }
  double progress(double x, double z, double radius);

  /** Is the road network within `radius` of the place all there? */
  bool ready(double x, double z) { return ready(x, z, loadRadius); }
  bool ready(double x, double z, double radius);

  /**
   * Keep the road network around (x, z) loaded: asks for the chunks that are
   * missing, adds what has arrived (for `budgetMs` at most) and drops what
   * lies far away.  To be called every frame; does nothing for a map in one piece.
   */
  void update(double x, double z, double budgetMs = 4);

  /** Load the network within `radius` of a place and wait for it (tools, tests, and the game before a drive begins). */
  Task<void> loadAround(double x, double z) { return loadAround(x, z, loadRadius); }
  Task<void> loadAround(double x, double z, double radius);

  /** Load the whole network (small maps, tools). */
  Task<void> loadAll();

  /** A JSON file of the map folder (gzipped or not), or null if it is not there. */
  template <class T>
  Task<std::optional<T>> json(std::string path) {
    return readJson<T>(files_, std::move(path));
  }

  /** Every road of the map in a few numbers (for routes and the big map) – null for a map in one piece. */
  const Task<std::shared_ptr<const NetData>>& net();

  /** The tracks trams and trains run on. */
  const Task<std::shared_ptr<const std::vector<RailJson>>>& rails();

  /** The stops of the region – null for a map without them (one built before version 1.2.0, a test map). */
  const Task<std::shared_ptr<const StopsJson>>& stops();

  /** The far view of the region: its ground on a coarse grid (far.bin.gz, inflated) – null for a map without it. */
  const Task<std::shared_ptr<const Bytes>>& far();

  /** The lines of the trams – null for a map without them. */
  const Task<std::shared_ptr<const LinesJson>>& lines();

  /** The timetables of the trams (timetable.json.gz) – null for a map without them. */
  const Task<std::shared_ptr<const TimetableJson>>& timetable();

  /** The airfields of the region – null for a map without them. */
  const Task<std::shared_ptr<const AirJson>>& air();

  /** (made by `open`) */
  MapLoader(std::shared_ptr<MapFiles> files, std::unique_ptr<World> world, TileGrid tiles, std::optional<GridSet> chunks);
  MapLoader(const MapLoader&) = delete;
  MapLoader& operator=(const MapLoader&) = delete;

 private:
  struct Failed {
    int n = 0;
    double at = 0;
  };
  struct Work {
    std::string key;
    WorldChunkJson part;
  };

  std::unique_ptr<World> world_;
  std::shared_ptr<MapFiles> files_;
  std::optional<GridSet> chunks_;
  /** the junctions of each chunk that is loaded or being added (in the order they came: the first that lies far away is dropped first) */
  std::vector<std::pair<std::string, std::vector<int>>> loaded_;
  std::unordered_set<std::string> fetching_;
  /** chunks that could not be fetched: how often it was tried, and when last (they are asked for again a few times) */
  std::unordered_map<std::string, Failed> failed_;
  /** what has arrived and waits to be added, in small portions */
  std::deque<Work> work_;
  std::unordered_map<std::string, int> pending_;
  std::vector<MapListener> listeners_;
  Task<std::shared_ptr<const NetData>> netData_;
  Task<std::shared_ptr<const std::vector<RailJson>>> railData_;
  Task<std::shared_ptr<const StopsJson>> stopData_;
  Task<std::shared_ptr<const AirJson>> airData_;
  Task<std::shared_ptr<const TimetableJson>> timetableData_;
  Task<std::shared_ptr<const Bytes>> farData_;
  Task<std::shared_ptr<const LinesJson>> lineData_;

  bool isLoaded(const std::string& key) const;
  /** The chunks that reach into the circle of `radius` around (x, z): their keys. */
  std::vector<std::string> around(double x, double z, double radius) const;
  /** A chunk that was tried often enough: the game goes on without it. */
  bool gaveUp(const std::string& key) const;
  /** May a chunk be asked for (again)?  After a failure only after a while – a connection on the road comes and goes. */
  bool mayFetch(const std::string& key) const;
  Task<void> fetchChunk(std::string key);
  /** Add what has arrived, for at most `budgetMs` (at least one portion). */
  void addWork(double budgetMs);

  Task<std::shared_ptr<const NetData>> readNet();
  Task<std::shared_ptr<const std::vector<RailJson>>> readRails();
  Task<std::shared_ptr<const Bytes>> readFar();
  /** a JSON file the map may be without: null if it is not there or cannot be read */
  template <class T>
  Task<std::shared_ptr<const T>> readOptional(std::string path);
};

}  // namespace fs
