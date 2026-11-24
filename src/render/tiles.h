// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Streams map tiles around the viewer and turns them into scene objects:
 * terrain (with ground-cover texture and 3 detail levels), road surfaces,
 * paint, structures and buildings.  Also answers terrain height queries for
 * the physics.
 *
 * Origin: src/render/tiles.ts @ v1.7.7
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/task.h"
#include "engine/engine.h"
#include "world/loader.h"
#include "world/tilefmt.h"

namespace fs {

struct LoadedTile {
  std::string key;
  TileHeader header;
  std::shared_ptr<Group> group;
  /** terrain heights in metres (terrainN × terrainN), absolute */
  std::vector<float> heights;
  /** ground-cover raster (coverN × coverN) */
  std::vector<uint8_t> cover;
  /**
   * the tile's sections – without the arrays that went to the graphics card
   * (the positions, indices and per-vertex bytes of ground, mark, struct and
   * bldg: those are the attributes of the meshes named so in `group`)
   */
  UnpackedTile data;
  /** the measured details of its walls (render/facadedetail): not built (yet) while `detailBuilt` is false */
  std::shared_ptr<Group> detail;
  bool detailBuilt = false;
};

/** which of the tiles' materials (for what teaches them the season and the wet) */
enum class TileMaterialKind { Terrain, Ground, Paint, Struct, Building };

using TileListener = std::function<void(LoadedTile& tile, bool loaded)>;

class TileManager {
 public:
  const std::shared_ptr<Group> group = make<Group>();
  /** the tiles that are loaded, by key */
  std::unordered_map<std::string, std::unique_ptr<LoadedTile>> tiles;
  /** tiles are kept within this distance (metres) of the viewer */
  double viewRadius = 1400;
  int maxConcurrent = 3;
  /** the share of the tiles around the current position that are there (0 … 1: for a progress bar) */
  double progress = 0;

  /** `version`: which build of the map (see MapLoader.version) – part of the address of each tile */
  TileManager(const TileGrid& grid, const std::string& baseUrl, const std::string& version = "");

  /** The season and the weather on the tiles' materials: the shared ones now, each tile's terrain as it loads. */
  void setMaterialHook(std::function<void(Material&, TileMaterialKind)> hook);
  /** Buildings with their windows, doors and roof tiles – or plain (graphics setting). */
  void setFacades(bool on);
  void onTile(TileListener l) { listeners_.push_back(std::move(l)); }
  /** True when every tile wanted for the current position has arrived. */
  bool settled() const { return loading_.empty() && wantCount_ == 0; }
  void update(double x, double z);
  /** Ground-cover class at a world position (COVER.MEADOW if the tile is not loaded). */
  int coverAt(double x, double z);
  /** Terrain height at a world position, or NaN if that tile is not loaded. */
  double heightAt(double x, double z);
  /** how a tile's bytes are fetched (the game sets it: a stored map pack, a region's part – world/mappack) */
  std::function<Task<plat::Response>(std::string url)> fetch;

 private:
  /**
   * The tile `heightAt` / `coverAt` looked up last – by its grid indices, so
   * that the ground under the wheels (many lookups a frame, nearly all in the
   * same tile) needs no key string and no map lookup.  Forgotten whenever a
   * tile comes or goes (`forgetLast`).
   */
  bool lastValid_ = false;
  int lastIx_ = 0, lastIz_ = 0;
  LoadedTile* lastTile_ = nullptr;
  std::unordered_set<std::string> loading_;
  /** tiles that could not be fetched: how often it was tried, and when last (they are asked for again a few times) */
  struct Failed {
    int n = 0;
    double at = 0;
  };
  std::unordered_map<std::string, Failed> failed_;
  /** where the tiles lie and which there are */
  TileGrid grid_;
  std::string baseUrl_;
  std::string query_;
  MaterialPtr groundMat_, paintMat_, structMat_, bldgMat_, plainMat_;
  std::function<void(Material&, TileMaterialKind)> materialHook_;
  /** buildings with windows, doors and roof tiles (false: plain walls and roofs) */
  bool facades_ = true;
  std::vector<TileListener> listeners_;
  int wantCount_ = 0;
  /** (what `this` is still alive for, when an answer arrives late) */
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  /** May a tile be asked for (again)?  After a failure only after a while, and not for ever: a connection on the road comes and goes. */
  bool mayLoad(const std::string& key);
  Task<void> load(std::string key);
  void unload(LoadedTile& tile);
  void forgetLast();
  /** The loaded tile with these grid indices (or null), remembered for the next call. */
  LoadedTile* tileAt(int ix, int iz);
  std::unique_ptr<LoadedTile> build(const std::string& key, UnpackedTile data);

 public:
  ~TileManager() { *alive_ = false; }
};

}  // namespace fs
