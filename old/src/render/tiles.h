// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Streams map tiles around the viewer and turns them into three.js objects:
 * terrain (with ground-cover texture and 3 detail levels), road surfaces,
 * paint, structures and buildings.  Also answers terrain height queries for
 * the physics.
 *
 * Origin: src/render/tiles.ts (MaurerAnton/driving-simulator @ 5b6ddc7,
 * v0.1.0).  The fetch layer lives in the platform (TileSource), so the same
 * code runs in the browser and in the native build.
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/materials.h"
#include "engine/scene.h"
#include "sim/ground.h"
#include "world/tilefmt.h"
#include "world/types.h"
#include "world/world.h"

namespace fs {

/** One tile as the renderer sees it. */
struct LoadedTile {
  std::string key;
  TileHeader header;
  std::unique_ptr<Group> group;
  /** terrain heights in metres (terrainN × terrainN), absolute */
  std::vector<double> heights;
  /** ground-cover raster (coverN × coverN) */
  std::vector<uint8_t> cover;
  /** the cover as RGBA texels for the terrain's texture (empty = none) */
  std::vector<uint8_t> coverTex;
  int coverTexN = 0;
  UnpackedTile data;
};

/** Where a tile file comes from (the platform implements this). */
class TileSource {
 public:
  virtual ~TileSource() = default;
  /**
   * Read `map/tiles/<key>.fsb`, inflated.  False when it does not exist –
   * or when it is on its way and `fetching` says so (the page fetches in the
   * background and calls TileManager::arrived when the bytes are in).
   */
  virtual bool read(const std::string& key, std::vector<uint8_t>& out, bool* fetching = nullptr) = 0;
};

/** Notified when a tile is loaded (true) or unloaded (false). */
using TileListener = std::function<void(LoadedTile&, bool)>;

class TileManager : public TerrainSource {
 public:
  TileManager(const WorldJson& world, const std::string& baseUrl, TileSource& source);

  /** TerrainSource: the physics queries heights and cover through this. */
  double heightAt(double x, double z) const override;
  int coverAt(double x, double z) const override;

  Group group;                       // added to the scene
  std::map<std::string, std::unique_ptr<LoadedTile>> tiles;
  /** tiles are kept within this distance (metres) of the viewer */
  double viewRadius = 1400;
  int maxConcurrent = 3;

  void onTile(const TileListener& l) { listeners_.push_back(l); }
  /**
   * A tile the source was fetching has arrived: read it again (the page calls
   * this when its background fetch answers).
   */
  void arrived(const std::string& key);
  /** True when every tile wanted for the current position has arrived. */
  bool settled() const { return loading_.empty() && wantCount_ == 0; }

  /** Ask for the tiles around (x, z) and drop the ones that fell behind. */
  void update(double x, double z);


 private:
  WorldMetaJson meta_;
  std::set<std::string> available_;
  std::string baseUrl_;
  TileSource& source_;
  std::set<std::string> loading_;
  std::set<std::string> failed_;
  std::vector<TileListener> listeners_;
  int wantCount_ = 0;
  MaterialKind kindGround_ = MaterialKind::Ground;
  MaterialKind kindPaint_ = MaterialKind::Paint;
  MaterialKind kindStruct_ = MaterialKind::Struct;
  MaterialKind kindBldg_ = MaterialKind::Building;
  MaterialKind kindTerrain_ = MaterialKind::Terrain;

  /** Load one tile synchronously (the platform may call it from a worker). */
  void load(const std::string& key);
  void unload(LoadedTile& tile);
  std::unique_ptr<LoadedTile> build(const std::string& key, UnpackedTile& data);
};

}  // namespace fs
